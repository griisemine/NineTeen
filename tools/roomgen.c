/*
 * roomgen.c — la salle, générée depuis sa description.
 *
 * Lit `assets/scene/salle.room.json` et écrit trois fichiers :
 *
 *   salle.gltf + salle.bin   la géométrie, via `gltf_write`
 *   salle.lights.json        les lumières et l'emprise, au format que
 *                            `load_lights` lit déjà (ns_scene.c:83)
 *   salle.scene.json         l'échelle, l'emprise jouable, le départ du joueur
 *                            et les points de vue nommés, au format que
 *                            `load_scene_sidecar` lit déjà (ns_scene.c:174)
 *
 * **Pourquoi un générateur plutôt qu'un modèle.** La salle de 2020 est un OBJ de
 * 127 objets dont la moitié s'appelle `Cube.0XX`. Le moteur devait donc *deviner*
 * quelle borne joue à quoi, où est son écran, quelle boîte est une lampe — et
 * chacune de ces déductions se trompe quelque part. Ici tout est déclaré : le
 * nom, la place, le matériau, la lumière. La classe entière de bugs disparaît
 * avec les heuristiques, au lieu d'être corrigée une par une.
 *
 * **L'unité est le mètre**, et c'est la seconde raison. Le modèle d'origine est
 * en unités Blender à 2,06 par mètre — l'auteur l'a écrit lui-même
 * (`HAUTEUR_CAMERA_DEBOUT 3.5F`, legacy/room/room.c:90) — et le moteur V15 l'a
 * lu comme des mètres, ce qui donnait un joueur de 82 cm. Le problème cesse
 * d'exister par construction.
 *
 * **Ce qui casse le build plutôt que de passer.** Un matériau inconnu, une
 * texture absente, un budget dépassé, une baie incohérente : tout est fatal, et
 * nommé. Un outil de build qui tronque en silence produit un asset dont le
 * défaut se découvre à l'exécution, deux étapes plus loin, dans un message qui
 * parle d'octets.
 */
#include "geo_shapes.h"
#include "gltf_write.h"
#include "tool_json.h"

#include <math.h>

/* Budgets. Ils viennent des constantes du moteur (engine/scene/ns_scene.h) ou
 * d'une mesure : l'ancienne salle fait 102 231 triangles, et un plafond de dalles
 * plus des chanfreins partout montent vite. Dépassement = arrêt. */
#define RG_MAX_MATERIALS   64
#define RG_MAX_TEXTURES    64
#define RG_MAX_LIGHTS     128        /* NS_MAX_LIGHTS */
#define RG_MAX_OBJECTS   1024        /* NS_MAX_OBJECTS */
#define RG_MAX_TRIANGLES 250000

/* Angles de lissage. Une arête sous ce seuil est adoucie, au-dessus elle reste
 * vive. Un chanfrein doit rester net — sans quoi il cesse de se lire comme un
 * chanfrein, et l'objet redevient une boîte molle. */
#define RG_SMOOTH_HARD    1.0f       /* boîtes, panneaux, murs */
#define RG_SMOOTH_PROFILE 35.0f      /* moulures : le galbe s'adoucit, l'arête non */

/* ========================================================================== */
/* État de construction                                                       */
/* ========================================================================== */

typedef struct rg_light {
    char  name[64];
    float position[3];
    float color[3];
    float intensity, range, source_radius;
    bool  flicker;
    bool  ceiling_panel;
    float panel_size[2];    /* emprise du luminaire dans la trame, en mètres */
} rg_light;

typedef struct rg_builder {
    tool_vec verts;         /* gltf_vertex — un seul pool, partagé (cf. gltf_write.h) */
    tool_vec meshes;        /* gltf_mesh */
    tool_vec prim_blocks;   /* geo_primitives, gardés vivants jusqu'à l'écriture */

    gltf_material materials[RG_MAX_MATERIALS];
    char          material_names[RG_MAX_MATERIALS][64];
    float         material_uv[RG_MAX_MATERIALS];
    bool          material_fit[RG_MAX_MATERIALS];
    size_t        material_count;

    const char *textures[RG_MAX_TEXTURES];
    char        texture_storage[RG_MAX_TEXTURES][128];
    size_t      texture_count;

    rg_light lights[RG_MAX_LIGHTS];
    size_t   light_count;

    size_t triangle_count;
    ns_aabb bounds;

    const char *texture_dir;    /* pour vérifier l'existence, NULL si non fourni */
} rg_builder;

static int material_index(const rg_builder *b, const char *name, const char *used_by)
{
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < b->material_count; ++i) {
        if (strcmp(b->material_names[i], name) == 0) return (int)i;
    }
    tool_fatalf("« %s » réclame le matériau « %s », qui n'est pas déclaré",
                used_by, name);
    return -1;
}

/* Le paramétrage d'UV suit le matériau, pas l'objet : c'est une propriété de la
 * texture (une moquette boucle tous les 1,5 m, une dalle tous les 0,60 m), et la
 * répéter sur chaque objet finirait par diverger. Un objet peut malgré tout la
 * surcharger quand sa géométrie l'exige. */
static geo_uv material_uv(const rg_builder *b, int mat)
{
    if (mat < 0 || (size_t)mat >= b->material_count) return geo_uv_tile(1.0f);
    if (b->material_fit[mat]) return geo_uv_fit();
    return geo_uv_tile(b->material_uv[mat]);
}

/* ========================================================================== */
/* Émission d'un objet                                                        */
/* ========================================================================== */

/*
 * Un objet nommé de la description devient un maillage glTF, et donc un
 * `ns_scene_object` côté moteur. La finition se fait **par objet** et pas sur le
 * pool global : après soudure, un mur et sa plinthe partagent des positions, et
 * un lissage global moyennerait leurs normales de part et d'autre de la frontière
 * de matériau — la plinthe cesserait de se détacher du mur.
 */
static void emit_object(rg_builder *b, const char *name, geo_mesh *m, float smooth_angle)
{
    if (geo_mesh_tri_count(m) == 0) {
        tool_fatalf("l'objet « %s » ne produit aucun triangle — `cgltf_validate` "
                    "refuserait l'accesseur vide au moment du BVH, dans un message "
                    "parlant d'octets", name);
    }

    geo_weld(m, 1e-4f);
    geo_smooth_normals(m, smooth_angle);
    geo_generate_tangents(m);

    const int degenerate = geo_check_degenerate(m, 1e-9f);
    if (degenerate > 0) {
        tool_fatalf("l'objet « %s » contient %d triangle(s) dégénéré(s) : normale et "
                    "tangente y sont indéfinies", name, degenerate);
    }

    const size_t base = b->verts.count;
    if (base + geo_mesh_vertex_count(m) > 0xFFFFFFFFu) {
        tool_fatalf("plus de 2^32 sommets — les indices sont en uint32");
    }
    tool_vec_reserve(&b->verts, base + geo_mesh_vertex_count(m));
    for (size_t i = 0; i < geo_mesh_vertex_count(m); ++i) {
        *(gltf_vertex *)tool_vec_push(&b->verts) = TOOL_VEC_AT(&m->verts, gltf_vertex, i);
    }

    geo_primitives prims;
    geo_build_primitives(m, &prims);
    for (size_t i = 0; i < geo_mesh_tri_count(m) * 3; ++i) prims.storage[i] += (uint32_t)base;
    *(geo_primitives *)tool_vec_push(&b->prim_blocks) = prims;

    gltf_mesh *mesh = (gltf_mesh *)tool_vec_push(&b->meshes);
    memset(mesh, 0, sizeof *mesh);
    snprintf(mesh->name, sizeof mesh->name, "%s", name);
    mesh->prims = prims.prims;
    mesh->prim_count = prims.count;

    b->triangle_count += geo_mesh_tri_count(m);
    b->bounds = ns_aabb_union(b->bounds, geo_mesh_bounds(m));

    if (b->meshes.count > RG_MAX_OBJECTS) {
        tool_fatalf("plus de %d objets : le moteur en tient %d (NS_MAX_OBJECTS)",
                    RG_MAX_OBJECTS, RG_MAX_OBJECTS);
    }
    if (b->triangle_count > RG_MAX_TRIANGLES) {
        tool_fatalf("budget de triangles dépassé à l'objet « %s » : %zu > %d",
                    name, b->triangle_count, RG_MAX_TRIANGLES);
    }

    geo_mesh_free(m);
}

/* ========================================================================== */
/* Matériaux                                                                  */
/* ========================================================================== */

static int texture_index(rg_builder *b, const char *file, const char *used_by)
{
    if (!file || !file[0]) return -1;
    for (size_t i = 0; i < b->texture_count; ++i) {
        if (strcmp(b->texture_storage[i], file) == 0) return (int)i;
    }
    if (b->texture_count >= RG_MAX_TEXTURES) {
        tool_fatalf("plus de %d textures distinctes", RG_MAX_TEXTURES);
    }

    /* Une texture nommée mais absente donnerait, à l'exécution, un substitut
     * procédural et un avertissement noyé dans le journal. La faute de frappe se
     * paie ici, au build, avec le nom du matériau fautif. */
    if (b->texture_dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", b->texture_dir, file);
        FILE *f = fopen(path, "rb");
        if (!f) tool_fatalf("le matériau « %s » réclame la texture « %s », absente de %s",
                            used_by, file, b->texture_dir);
        fclose(f);
    }

    const size_t idx = b->texture_count++;
    snprintf(b->texture_storage[idx], sizeof b->texture_storage[idx], "%s", file);
    b->textures[idx] = b->texture_storage[idx];
    return (int)idx;
}

static void parse_materials(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "materials");
    const int count = tool_json_array_count(doc, list);
    if (count <= 0) tool_fatalf("la description ne déclare aucun matériau");

    for (int i = 0; i < count; ++i) {
        if (b->material_count >= RG_MAX_MATERIALS) {
            tool_fatalf("plus de %d matériaux", RG_MAX_MATERIALS);
        }
        const tool_json_value *e = tool_json_at(doc, list, i);
        const size_t slot = b->material_count++;

        /* Le nom transite par un tampon local : il est écrit à deux endroits du
         * même `rg_builder`, et le copier de l'un vers l'autre ferait croire à un
         * recouvrement (`-Wrestrict`) que le compilateur ne peut pas écarter. */
        char mat_name[64];
        tool_json_get_string(doc, e, "name", mat_name, sizeof mat_name);
        if (!mat_name[0]) tool_fatalf("le matériau numéro %d n'a pas de nom", i);
        for (size_t k = 0; k < slot; ++k) {
            if (strcmp(b->material_names[k], mat_name) == 0) {
                tool_fatalf("deux matériaux nommés « %s »", mat_name);
            }
        }
        snprintf(b->material_names[slot], sizeof b->material_names[slot], "%s", mat_name);

        gltf_material *m = &b->materials[slot];
        memset(m, 0, sizeof *m);
        snprintf(m->name, sizeof m->name, "%s", mat_name);

        char texture[128];
        tool_json_get_string(doc, e, "texture", texture, sizeof texture);
        m->texture = texture_index(b, texture, mat_name);

        tool_json_get_vec4(doc, e, "baseColor", m->base_color, 1.0f);
        m->metallic  = tool_json_get_float(doc, e, "metallic", 0.0f);
        m->roughness = tool_json_get_float(doc, e, "roughness", 0.85f);
        tool_json_get_vec3(doc, e, "emissive", m->emissive, 0.0f);
        m->emissive_strength = tool_json_get_float(doc, e, "emissiveStrength", 0.0f);

        b->material_uv[slot]  = tool_json_get_float(doc, e, "uvMetres", 1.0f);
        /* Le mode de pavage est **déclaré**, jamais deviné : `marbre_toilettes.jpg`
         * et les affiches sont des photos, pas des motifs, et les paver produirait
         * une couture visible tous les mètres. C'est exactement l'erreur que cette
         * reconstruction supprime. */
        b->material_fit[slot] = tool_json_get_bool(doc, e, "fit", false);
    }
}

/* ========================================================================== */
/* Lecture des placements                                                     */
/* ========================================================================== */

typedef struct rg_repeat {
    int   count;
    ns_v3 step;
    float yaw_step;
} rg_repeat;

static rg_repeat read_repeat(const tool_json *doc, const tool_json_value *e)
{
    rg_repeat r = { 1, { 0, 0, 0 }, 0.0f };
    const tool_json_value *rep = tool_json_get(doc, e, "repeat");
    if (!rep) return r;

    r.count = (int)tool_json_get_float(doc, rep, "count", 1.0f);
    if (r.count < 1) r.count = 1;
    float step[3];
    tool_json_get_vec3(doc, rep, "step", step, 0.0f);
    r.step = ns_v3_make(step[0], step[1], step[2]);
    r.yaw_step = tool_json_get_float(doc, rep, "yawStep", 0.0f);
    return r;
}

/* Nom d'un exemplaire : « pilier » seul quand il est unique, « pilier_3 » quand
 * la série en compte plusieurs. Chaque exemplaire est un objet distinct côté
 * moteur — c'est ce qui permettra de désigner *cette* borne-là. */
static void instance_name(char *out, size_t out_size, const char *base, int index, int count)
{
    if (count <= 1) snprintf(out, out_size, "%s", base);
    else            snprintf(out, out_size, "%s_%d", base, index + 1);
}

/* ========================================================================== */
/* Sols                                                                       */
/* ========================================================================== */

static void parse_floors(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "floors");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("un sol sans nom (entrée %d)", i);

        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, name);

        float centre[2], size[2];
        tool_json_get_vec2(doc, e, "centre", centre, 0.0f);
        if (tool_json_get_floats(doc, e, "size", size, 2, 0.0f) != 2) {
            tool_fatalf("le sol « %s » n'a pas de taille [x, z]", name);
        }
        const float y = tool_json_get_float(doc, e, "y", 0.0f);
        const bool up = tool_json_get_bool(doc, e, "faceUp", true);

        /* Une subdivision tous les deux mètres par défaut. Elle ne sert pas à la
         * silhouette mais à l'éclairage : le volumétrique et la remontée
         * bilatérale (A5) échantillonnent par sommet, et un sol de 22 m en deux
         * triangles ne leur donne aucun point d'appui. */
        const float pitch = tool_json_get_float(doc, e, "subdivMetres", 2.0f);
        const int nx = (int)ceilf(size[0] / ns_maxf(pitch, 0.1f));
        const int nz = (int)ceilf(size[1] / ns_maxf(pitch, 0.1f));

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);
        /* Le décalage d'UV suit le centre pour que deux sols voisins d'un même
         * matériau restent alignés — sinon la moquette a une couture à chaque
         * changement de dalle. */
        uv.offset_u = centre[0];
        uv.offset_v = centre[1];

        geo_mesh m; geo_mesh_init(&m);
        geo_plane(&m, size[0], size[1], nx, nz, up, &uv, mat);

        geo_xform x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(centre[0], y, centre[1]);
        geo_mesh placed; geo_mesh_init(&placed);
        geo_mesh_append(&placed, &m, &x, -1);
        geo_mesh_free(&m);

        emit_object(b, name, &placed, RG_SMOOTH_HARD);
    }
}

/* ========================================================================== */
/* Murs                                                                       */
/* ========================================================================== */

#define RG_MAX_WALL_POINTS   64
#define RG_MAX_WALL_OPENINGS 16

static size_t read_plan_points(const tool_json *doc, const tool_json_value *e,
                               const char *key, ns_v2 *out, size_t max,
                               const char *owner)
{
    const tool_json_value *arr = tool_json_get(doc, e, key);
    const int count = tool_json_array_count(doc, arr);
    if (count <= 0) tool_fatalf("« %s » : pas de « %s »", owner, key);
    if ((size_t)count > max) {
        tool_fatalf("« %s » : %d points, maximum %zu", owner, count, max);
    }
    for (int i = 0; i < count; ++i) {
        const tool_json_value *p = tool_json_at(doc, arr, i);
        if (tool_json_array_count(doc, p) < 2) {
            tool_fatalf("« %s » : le point %d n'est pas un couple [x, z]", owner, i);
        }
        out[i] = ns_v2_make(tool_json_value_float(doc, tool_json_at(doc, p, 0), 0.0f),
                            tool_json_value_float(doc, tool_json_at(doc, p, 1), 0.0f));
    }
    return (size_t)count;
}

static void parse_walls(rg_builder *b, const tool_json *doc, const tool_json_value *root,
                        float default_height, float default_thickness)
{
    const tool_json_value *list = tool_json_get(doc, root, "walls");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("un mur sans nom (entrée %d)", i);

        ns_v2 points[RG_MAX_WALL_POINTS];
        const size_t point_count = read_plan_points(doc, e, "points", points,
                                                    RG_MAX_WALL_POINTS, name);

        geo_opening openings[RG_MAX_WALL_OPENINGS];
        memset(openings, 0, sizeof openings);
        const tool_json_value *ops = tool_json_get(doc, e, "openings");
        const int op_count = tool_json_array_count(doc, ops);
        if (op_count > RG_MAX_WALL_OPENINGS) {
            tool_fatalf("le mur « %s » a %d baies, maximum %d",
                        name, op_count, RG_MAX_WALL_OPENINGS);
        }
        for (int k = 0; k < op_count; ++k) {
            const tool_json_value *o = tool_json_at(doc, ops, k);
            tool_json_get_string(doc, o, "name", openings[k].name, sizeof openings[k].name);
            openings[k].offset = tool_json_get_float(doc, o, "offset", 0.0f);
            openings[k].width  = tool_json_get_float(doc, o, "width", 0.0f);
            openings[k].sill   = tool_json_get_float(doc, o, "sill", 0.0f);
            openings[k].head   = tool_json_get_float(doc, o, "head", 2.10f);
        }

        char inner[64], outer[64], reveal[64];
        tool_json_get_string(doc, e, "materialInner", inner, sizeof inner);
        tool_json_get_string(doc, e, "materialOuter", outer, sizeof outer);
        tool_json_get_string(doc, e, "materialReveal", reveal, sizeof reveal);
        if (!outer[0])  snprintf(outer, sizeof outer, "%s", inner);
        if (!reveal[0]) snprintf(reveal, sizeof reveal, "%s", inner);

        geo_wall_desc d;
        memset(&d, 0, sizeof d);
        d.name = name;
        d.points = points;
        d.point_count = point_count;
        d.closed = tool_json_get_bool(doc, e, "closed", false);
        d.height = tool_json_get_float(doc, e, "height", default_height);
        d.thickness = tool_json_get_float(doc, e, "thickness", default_thickness);
        d.openings = openings;
        d.opening_count = (size_t)op_count;
        d.material_inner  = material_index(b, inner, name);
        d.material_outer  = material_index(b, outer, name);
        d.material_reveal = material_index(b, reveal, name);
        d.cap_top  = tool_json_get_bool(doc, e, "capTop", false);
        d.cap_ends = tool_json_get_bool(doc, e, "capEnds", true);

        d.uv = material_uv(b, d.material_inner);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) d.uv = geo_uv_tile(over);

        geo_mesh m; geo_mesh_init(&m);
        geo_wall_run(&m, &d);
        emit_object(b, name, &m, RG_SMOOTH_HARD);
    }
}

/* ========================================================================== */
/* Boîtes : piliers, poutres, plates-formes, socles                           */
/* ========================================================================== */

static void parse_boxes(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "boxes");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char base_name[96];
        tool_json_get_string(doc, e, "name", base_name, sizeof base_name);
        if (!base_name[0]) tool_fatalf("une boîte sans nom (entrée %d)", i);

        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, base_name);

        float at[3], size[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        if (tool_json_get_floats(doc, e, "size", size, 3, 0.0f) != 3) {
            tool_fatalf("la boîte « %s » n'a pas de taille [x, y, z]", base_name);
        }
        /* 12 mm par défaut : assez pour qu'un liseré se forme sur une arête vue de
         * près, assez peu pour ne pas arrondir la silhouette. */
        const float chamfer = tool_json_get_float(doc, e, "chamfer", 0.012f);
        const float yaw = tool_json_get_float(doc, e, "yaw", 0.0f);

        uint32_t faces = GEO_FACE_ALL;
        char mask[32];
        tool_json_get_string(doc, e, "faces", mask, sizeof mask);
        if (strcmp(mask, "sides") == 0)          faces = GEO_FACE_SIDES;
        else if (strcmp(mask, "noBottom") == 0)  faces = GEO_FACE_NO_BOTTOM;
        else if (mask[0] && strcmp(mask, "all") != 0) {
            tool_fatalf("la boîte « %s » demande le masque « %s », inconnu "
                        "(all, sides, noBottom)", base_name, mask);
        }

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);

        const rg_repeat rep = read_repeat(doc, e);
        for (int k = 0; k < rep.count; ++k) {
            geo_mesh m; geo_mesh_init(&m);
            geo_box(&m, ns_v3_make(size[0], size[1], size[2]), chamfer, faces, &uv, mat);

            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(at[0] + rep.step.x * (float)k,
                                  at[1] + rep.step.y * (float)k,
                                  at[2] + rep.step.z * (float)k);
            x.yaw = (yaw + rep.yaw_step * (float)k) * NS_DEG2RAD;

            geo_mesh placed; geo_mesh_init(&placed);
            geo_mesh_append(&placed, &m, &x, -1);
            geo_mesh_free(&m);

            char name[GLTF_MAX_NAME];
            instance_name(name, sizeof name, base_name, k, rep.count);
            emit_object(b, name, &placed, RG_SMOOTH_HARD);
        }
    }
}

/* ========================================================================== */
/* Moulures                                                                   */
/* ========================================================================== */

/*
 * Profils nommés plutôt que listes de points dans le JSON : une plinthe se décrit
 * par sa saillie et sa hauteur, pas par six coordonnées qu'il faudrait relire
 * pour comprendre de quoi il s'agit. Chacun est paramétré par [saillie, hauteur].
 *
 * L'axe x du profil pointe vers l'intérieur de la pièce — c'est `cross(+Y, T)`,
 * qui coïncide avec le « gauche » de `geo_wall_run`. Un chemin qui suit l'ordre
 * des points du mur pose donc la moulure du bon côté sans réglage.
 */
static size_t named_profile(const char *kind, float depth, float height,
                            ns_v2 *out, size_t max, const char *owner)
{
    size_t n = 0;
    #define P(px, py) do { if (n >= max) tool_fatalf("profil trop long"); \
                           out[n++] = ns_v2_make((px), (py)); } while (0)

    if (strcmp(kind, "plinthe") == 0) {
        /* Plinthe : pied droit, petit congé, arase. */
        P(0.0f, 0.0f);
        P(depth, 0.0f);
        P(depth, height * 0.78f);
        P(depth * 0.55f, height * 0.92f);
        P(depth * 0.55f, height);
        P(0.0f, height);
    } else if (strcmp(kind, "corniche") == 0) {
        /* Corniche : la hauteur se compte **vers le bas** depuis le chemin, qui
         * court au niveau du plafond. */
        P(0.0f, 0.0f);
        P(0.0f, -height);
        P(depth * 0.30f, -height * 0.86f);
        P(depth * 0.78f, -height * 0.34f);
        P(depth, 0.0f);
    } else if (strcmp(kind, "cimaise") == 0) {
        P(0.0f, 0.0f);
        P(depth, height * 0.22f);
        P(depth, height * 0.72f);
        P(depth * 0.42f, height);
        P(0.0f, height);
    } else if (strcmp(kind, "nez") == 0) {
        /* Nez de marche : déborde de la plate-forme et retombe. */
        P(0.0f, 0.0f);
        P(depth, 0.0f);
        P(depth, height * 0.55f);
        P(depth * 0.45f, height);
        P(0.0f, height);
    } else {
        tool_fatalf("« %s » demande le profil « %s », inconnu "
                    "(plinthe, corniche, cimaise, nez)", owner, kind);
    }
    #undef P
    return n;
}

/* Le sens d'un profil décide de celui de ses normales. Plutôt que d'exiger de
 * l'auteur du profil qu'il le liste dans le bon ordre — une règle qu'on oublie et
 * dont l'erreur ne se voit pas, le rendu n'éliminant pas les faces arrière — on
 * mesure l'aire signée et on retourne si besoin. */
static void ensure_ccw(ns_v2 *pts, size_t count)
{
    double area = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const ns_v2 a = pts[i], b = pts[(i + 1) % count];
        area += (double)a.x * b.y - (double)b.x * a.y;
    }
    if (area >= 0.0) return;
    for (size_t i = 0; i < count / 2; ++i) {
        const ns_v2 tmp = pts[i];
        pts[i] = pts[count - 1 - i];
        pts[count - 1 - i] = tmp;
    }
}

#define RG_MAX_PROFILE_POINTS 32

static void parse_mouldings(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "mouldings");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("une moulure sans nom (entrée %d)", i);

        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, name);

        char kind[32];
        tool_json_get_string(doc, e, "profile", kind, sizeof kind);
        float section[2];
        if (tool_json_get_floats(doc, e, "section", section, 2, 0.0f) != 2) {
            tool_fatalf("la moulure « %s » n'a pas de section [saillie, hauteur]", name);
        }

        ns_v2 profile[RG_MAX_PROFILE_POINTS];
        size_t profile_count = named_profile(kind, section[0], section[1],
                                             profile, RG_MAX_PROFILE_POINTS, name);
        ensure_ccw(profile, profile_count);

        ns_v2 plan[RG_MAX_WALL_POINTS];
        const size_t plan_count = read_plan_points(doc, e, "points", plan,
                                                   RG_MAX_WALL_POINTS, name);
        const bool closed = tool_json_get_bool(doc, e, "closed", false);
        const float y = tool_json_get_float(doc, e, "y", 0.0f);
        /* Décalage perpendiculaire, positif vers la gauche de la marche — donc
         * vers l'intérieur quand le chemin reprend les points du mur. La demi-
         * épaisseur du mur pose la moulure exactement sur sa face. */
        const float offset = tool_json_get_float(doc, e, "offset", 0.0f);

        if (plan_count < 2 || (closed && plan_count < 3)) {
            tool_fatalf("la moulure « %s » : %zu point(s)", name, plan_count);
        }

        ns_v3 path[RG_MAX_WALL_POINTS];
        for (size_t k = 0; k < plan_count; ++k) {
            /* Le décalage se prend sur la direction sortante du point, ce qui
             * suffit : `geo_profile_extrude` remitre ensuite le repère. */
            const size_t next = (k + 1 < plan_count) ? k + 1 : (closed ? 0 : k);
            const size_t from = (next == k) ? k - 1 : k;
            const size_t to   = (next == k) ? k     : next;
            ns_v2 dir = ns_v2_norm(ns_v2_sub(plan[to], plan[from]));
            const ns_v2 left = ns_v2_make(dir.y, -dir.x);
            const ns_v2 p = ns_v2_add(plan[k], ns_v2_scale(left, offset));
            path[k] = ns_v3_make(p.x, y, p.y);
        }

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, e, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);

        geo_mesh m; geo_mesh_init(&m);
        geo_profile_extrude(&m, profile, profile_count, true,
                            path, plan_count, closed, &uv, mat);
        emit_object(b, name, &m, RG_SMOOTH_PROFILE);
    }
}

/* ========================================================================== */
/* Plafond en dalles                                                          */
/* ========================================================================== */

/*
 * Le générateur au meilleur rapport valeur/ligne du palier : il corrige d'un coup
 * l'éclairage **et** l'aspect.
 *
 * L'ancien plafond était une dalle unique de 55 × 44 unités dont les UV bouclaient
 * une fois tous les 17,9 m — 0,056 répétition par mètre, ce qui se lit comme un
 * aplat quelle que soit la texture. Et comme son matériau portait `Ke 1 1 1`,
 * `obj2gltf` en tirait 16 lumières ponctuelles posées sur la boîte englobante de
 * la dalle, dont **7 tombaient hors des murs**.
 *
 * Ici : une dalle de 0,60 m, donc une répétition par dalle, soit 30 fois la
 * densité de texels ; de vrais rails en T ; et des panneaux lumineux **là où des
 * lumières sont déclarées**, pas ailleurs. La correspondance est calculée depuis
 * la liste des lumières plutôt que recopiée à la main : deux listes à tenir
 * d'accord divergent, une seule ne peut pas.
 */
static void parse_ceilings(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "ceilings");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char name[GLTF_MAX_NAME];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("un plafond sans nom (entrée %d)", i);

        char tile_mat[64], rail_mat[64], panel_mat[64], void_mat[64];
        tool_json_get_string(doc, e, "material", tile_mat, sizeof tile_mat);
        tool_json_get_string(doc, e, "railMaterial", rail_mat, sizeof rail_mat);
        tool_json_get_string(doc, e, "panelMaterial", panel_mat, sizeof panel_mat);
        tool_json_get_string(doc, e, "voidMaterial", void_mat, sizeof void_mat);
        const int m_tile  = material_index(b, tile_mat, name);
        const int m_rail  = material_index(b, rail_mat[0] ? rail_mat : tile_mat, name);
        const int m_panel = material_index(b, panel_mat[0] ? panel_mat : tile_mat, name);
        const int m_void  = material_index(b, void_mat[0] ? void_mat : rail_mat, name);

        float centre[2], size[2];
        tool_json_get_vec2(doc, e, "centre", centre, 0.0f);
        if (tool_json_get_floats(doc, e, "size", size, 2, 0.0f) != 2) {
            tool_fatalf("le plafond « %s » n'a pas de taille [x, z]", name);
        }
        const float y = tool_json_get_float(doc, e, "y", 3.10f);
        const float tile = ns_maxf(tool_json_get_float(doc, e, "tile", 0.60f), 0.10f);
        const float rail_w = tool_json_get_float(doc, e, "railWidth", 0.024f);
        const float rail_h = tool_json_get_float(doc, e, "railDrop", 0.045f);
        const float plenum = tool_json_get_float(doc, e, "plenum", 0.38f);

        /* Le pas est ajusté pour que la trame tombe juste sur l'emprise : une
         * demi-dalle au bord se verrait immédiatement. */
        const int nx = (int)ns_maxf(1.0f, floorf(size[0] / tile + 0.5f));
        const int nz = (int)ns_maxf(1.0f, floorf(size[1] / tile + 0.5f));
        const float sx = size[0] / (float)nx;
        const float sz = size[1] / (float)nz;
        const float x0 = centre[0] - size[0] * 0.5f;
        const float z0 = centre[1] - size[1] * 0.5f;

        /* Dalles manquantes, en indices de trame. Un indice hors trame est une
         * faute de frappe, pas une dalle qu'on renonce à retirer. */
        const tool_json_value *missing = tool_json_get(doc, e, "missing");
        const int missing_count = tool_json_array_count(doc, missing);
        int holes[16][2];
        if (missing_count > 16) tool_fatalf("le plafond « %s » : trop de dalles retirées", name);
        for (int k = 0; k < missing_count; ++k) {
            const tool_json_value *p = tool_json_at(doc, missing, k);
            holes[k][0] = (int)tool_json_value_float(doc, tool_json_at(doc, p, 0), -1.0f);
            holes[k][1] = (int)tool_json_value_float(doc, tool_json_at(doc, p, 1), -1.0f);
            if (holes[k][0] < 0 || holes[k][0] >= nx || holes[k][1] < 0 || holes[k][1] >= nz) {
                tool_fatalf("le plafond « %s » : la dalle retirée (%d, %d) est hors de la "
                            "trame %d x %d", name, holes[k][0], holes[k][1], nx, nz);
            }
        }

        geo_mesh m; geo_mesh_init(&m);
        geo_mesh_reserve(&m, (size_t)nx * (size_t)nz * 4, (size_t)nx * (size_t)nz * 2);

        const geo_uv rail_uv = geo_uv_tile(0.60f);
        int panels_placed = 0;

        for (int iz = 0; iz < nz; ++iz) {
            for (int ix = 0; ix < nx; ++ix) {
                const float cx = x0 + ((float)ix + 0.5f) * sx;
                const float cz = z0 + ((float)iz + 0.5f) * sz;

                bool hole = false;
                for (int k = 0; k < missing_count; ++k) {
                    if (holes[k][0] == ix && holes[k][1] == iz) hole = true;
                }

                /*
                 * Une dalle devient panneau lumineux quand son **centre** tombe
                 * dans l'emprise d'un luminaire déclaré. Tester le centre plutôt
                 * qu'un recouvrement évite l'égalité douteuse au bord : un
                 * luminaire de 1,20 m posé sur un joint prend exactement les deux
                 * dalles qu'il couvre, jamais trois.
                 */
                bool lit = false;
                for (size_t l = 0; l < b->light_count; ++l) {
                    const rg_light *lg = &b->lights[l];
                    if (!lg->ceiling_panel) continue;
                    if (fabsf(lg->position[1] - y) > 0.50f) continue;
                    if (fabsf(lg->position[0] - cx) >= lg->panel_size[0] * 0.5f - 1e-3f) continue;
                    if (fabsf(lg->position[2] - cz) >= lg->panel_size[1] * 0.5f - 1e-3f) continue;
                    lit = true;
                    break;
                }
                if (lit) panels_placed++;

                if (hole) {
                    /* Le vide au-dessus, vu par en dessous : les faces internes du
                     * plénum. Le rendu n'élimine pas les faces arrière et retourne
                     * la normale, donc une boîte vue de l'intérieur s'éclaire
                     * correctement — c'est ce qui rend ce trou possible sans
                     * générateur dédié. */
                    geo_mesh box; geo_mesh_init(&box);
                    const geo_uv vuv = geo_uv_tile(0.5f);
                    geo_box(&box, ns_v3_make(sx - rail_w, plenum, sz - rail_w), 0.0f,
                            GEO_FACE_SIDES | GEO_FACE_PY, &vuv, m_void);
                    geo_xform x = GEO_XFORM_IDENTITY;
                    x.origin = ns_v3_make(cx, y, cz);
                    geo_mesh_append(&m, &box, &x, -1);
                    geo_mesh_free(&box);

                    /* Et le câble qui pend, qui est ce qui fait lire le trou comme
                     * un trou plutôt que comme une dalle noire. */
                    geo_mesh cable; geo_mesh_init(&cable);
                    geo_box(&cable, ns_v3_make(0.018f, plenum * 0.75f, 0.018f), 0.0f,
                            GEO_FACE_SIDES, &vuv, m_void);
                    geo_xform xc = GEO_XFORM_IDENTITY;
                    xc.origin = ns_v3_make(cx + sx * 0.18f, y - plenum * 0.30f, cz - sz * 0.12f);
                    xc.roll = 0.22f;
                    geo_mesh_append(&m, &cable, &xc, -1);
                    geo_mesh_free(&cable);
                    continue;
                }

                geo_panel(&m, ns_v3_make(cx, y, cz),
                          ns_v3_make(0.0f, -1.0f, 0.0f), ns_v3_make(1.0f, 0.0f, 0.0f),
                          sx - rail_w, sz - rail_w,
                          0.0f, 0.0f, 1.0f, 1.0f, lit ? m_panel : m_tile);
            }
        }

        /* Les rails en T, dans les deux sens. Ils descendent sous le plan des
         * dalles : c'est cette ombre portée de quelques millimètres qui donne au
         * plafond son relief, et qu'une simple texture ne peut pas imiter. */
        for (int ix = 0; ix <= nx; ++ix) {
            geo_mesh rail; geo_mesh_init(&rail);
            geo_box(&rail, ns_v3_make(rail_w, rail_h, size[1]), 0.0f,
                    GEO_FACE_ALL & ~GEO_FACE_PY, &rail_uv, m_rail);
            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(x0 + (float)ix * sx, y - rail_h, centre[1]);
            geo_mesh_append(&m, &rail, &x, -1);
            geo_mesh_free(&rail);
        }
        for (int iz = 0; iz <= nz; ++iz) {
            geo_mesh rail; geo_mesh_init(&rail);
            geo_box(&rail, ns_v3_make(size[0], rail_h, rail_w), 0.0f,
                    GEO_FACE_ALL & ~GEO_FACE_PY, &rail_uv, m_rail);
            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(centre[0], y - rail_h, z0 + (float)iz * sz);
            geo_mesh_append(&m, &rail, &x, -1);
            geo_mesh_free(&rail);
        }

        tool_infof("plafond « %s » : %d x %d dalles de %.3f x %.3f m, %d panneau(x) "
                   "lumineux, %d dalle(s) retirée(s)",
                   name, nx, nz, (double)sx, (double)sz, panels_placed, missing_count);

        emit_object(b, name, &m, RG_SMOOTH_HARD);
    }
}

/* ========================================================================== */
/* Lumières                                                                   */
/* ========================================================================== */

static void parse_lights(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "lights");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        char base_name[64];
        tool_json_get_string(doc, e, "name", base_name, sizeof base_name);
        if (!base_name[0]) tool_fatalf("une lumière sans nom (entrée %d)", i);

        float at[3], color[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        tool_json_get_vec3(doc, e, "color", color, 1.0f);
        const float intensity = tool_json_get_float(doc, e, "intensity", 120.0f);
        const float range = tool_json_get_float(doc, e, "range", 9.0f);
        /* Rayon apparent de la source. 0,22 m est l'ampoule d'applique pour
         * laquelle le shader avait une constante globale ; un pavé de faux
         * plafond de 1,20 x 0,60 m se déclare à 0,45. Sans ce champ, un luminaire
         * étendu brûle tout ce qui est à moins d'un mètre de lui. */
        const float radius = tool_json_get_float(doc, e, "radius", 0.22f);
        const bool flicker = tool_json_get_bool(doc, e, "flicker", false);
        const bool panel = tool_json_get_bool(doc, e, "ceilingPanel", false);
        float panel_size[2] = { 0.60f, 0.60f };
        tool_json_get_vec2(doc, e, "panelSize", panel_size, 0.60f);

        const rg_repeat rep = read_repeat(doc, e);
        for (int k = 0; k < rep.count; ++k) {
            if (b->light_count >= RG_MAX_LIGHTS) {
                tool_fatalf("plus de %d lumières : le moteur en tient %d "
                            "(NS_MAX_LIGHTS), et les écrans de bornes en ajouteront "
                            "dix-neuf", RG_MAX_LIGHTS, RG_MAX_LIGHTS);
            }
            rg_light *l = &b->lights[b->light_count++];
            memset(l, 0, sizeof *l);
            instance_name(l->name, sizeof l->name, base_name, k, rep.count);
            l->position[0] = at[0] + rep.step.x * (float)k;
            l->position[1] = at[1] + rep.step.y * (float)k;
            l->position[2] = at[2] + rep.step.z * (float)k;
            memcpy(l->color, color, sizeof color);
            l->intensity = intensity;
            l->range = range;
            l->source_radius = radius;
            l->flicker = flicker;
            l->ceiling_panel = panel;
            l->panel_size[0] = panel_size[0];
            l->panel_size[1] = panel_size[1];
        }
    }
}

/* ========================================================================== */
/* Fichiers annexes                                                           */
/* ========================================================================== */

static void write_lights_json(const rg_builder *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) tool_fatalf("impossible d'écrire %s", path);

    fprintf(f, "{\n");
    fprintf(f, "  \"_comment\": \"Généré par roomgen depuis salle.room.json. "
               "Contrairement au fichier que produisait obj2gltf, rien n'est ici "
               "déduit d'une aire émissive : chaque lumière est déclarée, avec son "
               "nom, sa place et sa portée.\",\n");
    fprintf(f, "  \"bounds\": { \"min\": [%.4f, %.4f, %.4f], \"max\": [%.4f, %.4f, %.4f] },\n",
            (double)b->bounds.min.x, (double)b->bounds.min.y, (double)b->bounds.min.z,
            (double)b->bounds.max.x, (double)b->bounds.max.y, (double)b->bounds.max.z);
    fprintf(f, "  \"lights\": [\n");
    for (size_t i = 0; i < b->light_count; ++i) {
        const rg_light *l = &b->lights[i];
        fprintf(f, "    { \"name\": \"%s\", \"type\": \"point\", "
                   "\"position\": [%.4f, %.4f, %.4f], \"color\": [%.4f, %.4f, %.4f], "
                   "\"intensity\": %.3f, \"range\": %.3f, \"radius\": %.3f, "
                   "\"flicker\": %s }%s\n",
                l->name,
                (double)l->position[0], (double)l->position[1], (double)l->position[2],
                (double)l->color[0], (double)l->color[1], (double)l->color[2],
                (double)l->intensity, (double)l->range, (double)l->source_radius,
                l->flicker ? "true" : "false",
                (i + 1 < b->light_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

static void write_scene_json(const tool_json *doc, const tool_json_value *root,
                             const rg_builder *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) tool_fatalf("impossible d'écrire %s", path);

    float pmin[3], pmax[3];
    const tool_json_value *playable = tool_json_get(doc, tool_json_get(doc, root, "room"),
                                                    "playable");
    if (!playable) tool_fatalf("la description ne déclare pas room.playable");
    tool_json_get_vec3(doc, playable, "min", pmin, 0.0f);
    tool_json_get_vec3(doc, playable, "max", pmax, 0.0f);

    fprintf(f, "{\n");
    fprintf(f, "  \"_comment\": \"Généré par roomgen. La salle reconstruite est en "
               "mètres, donc unitsPerMetre vaut 1 — contre 2,06 pour le modèle de "
               "2020, dont l'auteur avait écrit HAUTEUR_CAMERA_DEBOUT 3.5F.\",\n");
    fprintf(f, "  \"unitsPerMetre\": 1.0,\n");
    fprintf(f, "  \"roomBounds\": { \"min\": [%.3f, %.3f, %.3f], "
               "\"max\": [%.3f, %.3f, %.3f] },\n",
            (double)pmin[0], (double)pmin[1], (double)pmin[2],
            (double)pmax[0], (double)pmax[1], (double)pmax[2]);

    const tool_json_value *start = tool_json_get(doc, root, "playerStart");
    if (start) {
        float p[3];
        tool_json_get_vec3(doc, start, "position", p, 0.0f);
        fprintf(f, "  \"playerStart\": { \"position\": [%.3f, %.3f, %.3f], "
                   "\"yaw\": %.2f },\n",
                (double)p[0], (double)p[1], (double)p[2],
                (double)tool_json_get_float(doc, start, "yaw", -90.0f));
    }

    /* Les points de vue sont recopiés tels quels : ils portent les **mêmes noms**
     * que ceux de `salle-legacy.scene.json`, exprimés ici en mètres. C'est ce qui
     * rend `--view=allee` comparable entre deux salles d'échelles différentes,
     * là où `--pos=` désignerait deux endroits sans rapport. */
    const tool_json_value *views = tool_json_get(doc, root, "captures");
    const int view_count = tool_json_array_count(doc, views);
    fprintf(f, "  \"captures\": [\n");
    for (int i = 0; i < view_count; ++i) {
        const tool_json_value *e = tool_json_at(doc, views, i);
        char name[32];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        float p[3];
        tool_json_get_vec3(doc, e, "position", p, 0.0f);
        const bool orbit = tool_json_get_bool(doc, e, "orbit", false);

        fprintf(f, "    { \"name\": \"%s\", \"position\": [%.3f, %.3f, %.3f]",
                name, (double)p[0], (double)p[1], (double)p[2]);
        if (orbit) {
            fprintf(f, ", \"orbit\": true, \"radius\": %.3f, \"height\": %.3f",
                    (double)tool_json_get_float(doc, e, "radius", 8.0f),
                    (double)tool_json_get_float(doc, e, "height", 4.0f));
        } else {
            fprintf(f, ", \"yaw\": %.2f, \"pitch\": %.2f",
                    (double)tool_json_get_float(doc, e, "yaw", 0.0f),
                    (double)tool_json_get_float(doc, e, "pitch", 0.0f));
        }
        fprintf(f, " }%s\n", (i + 1 < view_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);

    (void)b;
}

/* ========================================================================== */
/* Programme                                                                  */
/* ========================================================================== */

static void usage(void)
{
    fprintf(stderr,
        "usage : roomgen <salle.room.json> <sortie.gltf> [--textures=REP]\n"
        "\n"
        "  --textures=REP  vérifie que chaque texture nommée existe dans REP.\n"
        "                  Sans cette option, une faute de frappe se découvre à\n"
        "                  l'exécution, sous forme de substitut procédural.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL, *texture_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--textures=", 11) == 0) texture_dir = argv[i] + 11;
        else if (argv[i][0] == '-') usage();
        else if (!in_path) in_path = argv[i];
        else if (!out_path) out_path = argv[i];
        else usage();
    }
    if (!in_path || !out_path) usage();

    size_t size = 0;
    char *text = tool_read_file(in_path, &size);
    if (!text) tool_fatalf("description introuvable : %s", in_path);

    tool_json doc;
    tool_json_parse(&doc, text, size, in_path);
    const tool_json_value *root = tool_json_root(&doc);
    if (!root) tool_fatalf("%s : document vide", in_path);

    char units[16];
    tool_json_get_string(&doc, root, "units", units, sizeof units);
    if (strcmp(units, "metres") != 0) {
        tool_fatalf("%s : \"units\" doit valoir \"metres\" — c'est la moitié de "
                    "l'intérêt de la reconstruction", in_path);
    }

    rg_builder b;
    memset(&b, 0, sizeof b);
    tool_vec_init(&b.verts, sizeof(gltf_vertex));
    tool_vec_init(&b.meshes, sizeof(gltf_mesh));
    tool_vec_init(&b.prim_blocks, sizeof(geo_primitives));
    b.bounds = ns_aabb_empty();
    b.texture_dir = texture_dir;

    const tool_json_value *room = tool_json_get(&doc, root, "room");
    if (!room) tool_fatalf("%s : pas de bloc \"room\"", in_path);
    const float height = tool_json_get_float(&doc, room, "height", 3.10f);
    const float thickness = tool_json_get_float(&doc, room, "wallThickness", 0.20f);

    parse_materials(&b, &doc, root);
    /* Les lumières d'abord : le plafond s'en sert pour savoir où poser ses
     * panneaux lumineux, ce qui évite d'avoir deux listes à tenir d'accord. */
    parse_lights(&b, &doc, root);

    parse_floors(&b, &doc, root);
    parse_walls(&b, &doc, root, height, thickness);
    parse_ceilings(&b, &doc, root);
    parse_boxes(&b, &doc, root);
    parse_mouldings(&b, &doc, root);

    if (b.meshes.count == 0) tool_fatalf("%s : la description ne produit aucun objet", in_path);

    gltf_scene scene;
    memset(&scene, 0, sizeof scene);
    scene.verts = (const gltf_vertex *)b.verts.data;
    scene.vert_count = b.verts.count;
    scene.meshes = (const gltf_mesh *)b.meshes.data;
    scene.mesh_count = b.meshes.count;
    scene.materials = b.materials;
    scene.material_count = b.material_count;
    scene.textures = b.textures;
    scene.texture_count = b.texture_count;
    scene.generator = "Nineteen V15 roomgen";
    scene.scene_name = "salle";

    const size_t bin_size = gltf_write(&scene, out_path);

    char base[256];
    tool_basename_noext(out_path, base, sizeof base);
    char dir[512];
    tool_dirname(out_path, dir, sizeof dir);

    char annex[800];
    snprintf(annex, sizeof annex, "%s%s.lights.json", dir, base);
    write_lights_json(&b, annex);
    snprintf(annex, sizeof annex, "%s%s.scene.json", dir, base);
    write_scene_json(&doc, root, &b, annex);

    tool_infof("salle : %zu objets, %zu triangles, %zu sommets, %zu matériaux, "
               "%zu textures, %zu lumières",
               b.meshes.count, b.triangle_count, b.verts.count,
               b.material_count, b.texture_count, b.light_count);
    tool_infof("emprise : (%.2f %.2f %.2f) à (%.2f %.2f %.2f) m, binaire %zu octets",
               (double)b.bounds.min.x, (double)b.bounds.min.y, (double)b.bounds.min.z,
               (double)b.bounds.max.x, (double)b.bounds.max.y, (double)b.bounds.max.z,
               bin_size);

    for (size_t i = 0; i < b.prim_blocks.count; ++i) {
        geo_primitives_free(&TOOL_VEC_AT(&b.prim_blocks, geo_primitives, i));
    }
    tool_vec_free(&b.prim_blocks);
    tool_vec_free(&b.meshes);
    tool_vec_free(&b.verts);
    tool_json_free(&doc);
    free(text);
    return 0;
}
