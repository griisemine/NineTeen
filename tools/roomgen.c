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
#include "geo_import.h"
#include "geo_shapes.h"
#include "gltf_write.h"
#include "tool_json.h"

#include <math.h>

/* Budgets. Ils viennent des constantes du moteur (engine/scene/ns_scene.h) ou
 * d'une mesure : l'ancienne salle fait 102 231 triangles, et un plafond de dalles
 * plus des chanfreins partout montent vite. Dépassement = arrêt. */
/*
 * 64 était le budget d'origine, et il était plein au dernier octet. Le porter à
 * 128 n'est pas un renoncement : c'est ce qui permet de donner à chaque famille
 * de bornes sa propre teinte de caisson, et une salle d'arcade est justement un
 * endroit où rien n'a la même couleur que son voisin. Le moteur, lui, n'a aucune
 * limite fixe — il alloue les matériaux dans son arène — donc le seul coût est
 * ce tableau et un changement de matériau de plus par lot, ce qui ne se mesure
 * pas face à 600 tirages.
 */
#define RG_MAX_MATERIALS   128
#define RG_MAX_TEXTURE_DIRS 4
#define RG_MAX_TEXTURES    128   /* relevé de 64 : les douze albédos des modèles CC0 ont fait déborder */
#define RG_MAX_LIGHTS     128        /* NS_MAX_LIGHTS */
#define RG_MAX_OBJECTS   1024        /* NS_MAX_OBJECTS */
#define RG_MAX_CABINETS    24        /* NS_MAX_CABINETS */
#define RG_MAX_POIS        32        /* NS_MAX_POI */
#define RG_MAX_SOLIDS     512        /* obstacles suivis pour le contrôle des points de vue */
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

/*
 * Une borne, telle qu'elle sera **déclarée** au moteur.
 *
 * Aujourd'hui le moteur devine tout cela : l'écran par des fractions inventées de
 * la boîte englobante (centre 31 cm trop bas), l'orientation par le barycentre du
 * troupeau de bornes, l'affectation des jeux par un tri en X puis Z qui ne colle
 * pas aux images peintes sur les marquees. Trois déductions, trois erreurs.
 */
typedef struct rg_cabinet {
    char  name[64];
    char  game[32];
    char  difficulty[16];
    int   slot;
    float bounds_min[3], bounds_max[3];
    float screen_center[3];
    float screen_normal[3];
    float screen_size[2];
    float screen_tilt;          /* radians ; la dalle bascule vers l'arrière */       /* largeur et hauteur utiles de la dalle */
    float player_anchor[3];
    int   screen_material;      /* index du matériau de la dalle */
    float panel_centre[3];      /* là où la main appuie */
    float coin_slot[3];         /* là où le jeton entre */
    float stick_top[3];         /* là où la main gauche empoigne */
    bool  attract;
} rg_cabinet;

/*
 * Les points remarquables d'une borne, en repère **local** — c'est-à-dire dans
 * le même repère que les boîtes de `build_cabinet`, avant placement.
 *
 * Pourquoi les faire sortir d'ici plutôt que de les redériver côté `room/` :
 * ces cotes sont écrites une fois, dans le générateur, à trois lignes des
 * boîtes qu'elles désignent. Les recalculer ailleurs à partir de la boîte
 * englobante en ferait une seconde copie — et une seconde copie d'un chiffre
 * est une copie qui dérive. C'est exactement l'erreur qu'on vient de retirer
 * du moteur, où le centre d'écran était deviné à 31 cm près.
 */
typedef struct rg_cab_anchors {
    int   screen_material;   /* index du matériau de la dalle */
    float screen[3];
    float screen_size[2];
    float panel[3];     /* centre de la grappe de boutons, sur la face du dessus */
    float coin[3];      /* fente à jetons, sur la face avant de la trappe */
    float stick[3];     /* le dessus de la boule du joystick */
    /*
     * L'inclinaison de la dalle, en radians, comptée depuis la verticale.
     *
     * Elle est ici parce que la scène l'exportait FAUSSE : `screenNormal`
     * valait l'horizontale exacte alors que la dalle est penchée de 10° dans
     * la géométrie depuis A4. Une donnée qui ment sur sa propre géométrie est
     * pire qu'une donnée absente — le placement des bras et le reflet de
     * l'écran s'appuient dessus sans pouvoir s'en apercevoir.
     */
    float screen_tilt;
} rg_cab_anchors;

typedef struct rg_poi {
    char    name[64];
    char    kind[24];
    ns_aabb bounds;
    float   anchor[3];
} rg_poi;

/* Un obstacle : le nom sert au message d'erreur, pas au format de sortie.
 *
 * Le genre décide de la sévérité. Deux bornes qui se croisent sont une faute —
 * on ne peut pas jouer sur une borne encastrée dans sa voisine. Deux props qui
 * se croisent sont souvent voulus : un tabouret glissé sous un comptoir, une
 * affiche plaquée contre un mur, une flaque posée sur le sol. En faire une
 * erreur obligerait à déclarer des exceptions partout, et une exception qu'on
 * écrit dix fois cesse d'être lue. */
typedef enum rg_solid_kind {
    RG_SOLID_BOX = 0,
    RG_SOLID_CABINET,
    RG_SOLID_PROP
} rg_solid_kind;

typedef struct rg_solid {
    char          name[64];
    ns_aabb       bounds;
    rg_solid_kind kind;
} rg_solid;

typedef struct rg_builder {
    tool_vec verts;         /* gltf_vertex — un seul pool, partagé (cf. gltf_write.h) */
    tool_vec meshes;        /* gltf_mesh */
    tool_vec prim_blocks;   /* geo_primitives, gardés vivants jusqu'à l'écriture */

    gltf_material materials[RG_MAX_MATERIALS];
    char          material_names[RG_MAX_MATERIALS][64];
    float         material_uv[RG_MAX_MATERIALS];
    bool          material_fit[RG_MAX_MATERIALS];
    /* Classe de pas, déclarée par le matériau. Vide = on ne marche pas dessus. */
    char          material_footstep[RG_MAX_MATERIALS][24];
    size_t        material_count;

    /* Racine des assets, pour résoudre `"model": "cc0/models/..."`. Elle est
     * donnée en ligne de commande plutôt que déduite du chemin du fichier de
     * salle : le fichier de salle est une SOURCE de build, et rien ne garantit
     * qu'il soit rangé au même endroit que les assets qu'il désigne. */
    char asset_root[512];
    size_t imported_triangles;

    const char *textures[RG_MAX_TEXTURES];
    char        texture_storage[RG_MAX_TEXTURES][128];
    size_t      texture_count;

    rg_light lights[RG_MAX_LIGHTS];
    size_t   light_count;

    rg_cabinet cabinets[RG_MAX_CABINETS];
    size_t     cabinet_count;

    rg_poi pois[RG_MAX_POIS];
    size_t poi_count;

    /*
     * Emprise des objets SOLIDES — bornes, caisses, mobilier. Sert à refuser un
     * point de vue posé dedans.
     *
     * Ce n'est pas un raffinement : le point de vue `allee` s'est retrouvé dans
     * une borne en A4 et rendait un cadre noir, puis `plafond` a été avalé par la
     * borne de classement du même palier — deux fois le même défaut, découvert
     * deux fois sur une capture. Les points de vue sont de la donnée qui se périme
     * quand la salle change ; l'outil qui connaît les deux doit le dire.
     *
     * Seuls les solides comptent. La coquille est un objet unique dont la boîte
     * englobante couvre toute la salle : l'y inclure déclarerait chaque caméra
     * « dans un mur ».
     */
    rg_solid solids[RG_MAX_SOLIDS];
    size_t   solid_count;

    size_t triangle_count;
    ns_aabb bounds;
    /* Emprise du dernier objet émis. Sert aux props qui déclarent un point
     * d'intérêt : son volume est celui de sa géométrie, pas une boîte réécrite à
     * la main dans la description — c'est précisément le genre de doublon qui
     * finit par mentir. */
    ns_aabb last_bounds;

    /*
     * Les répertoires où chercher une texture nommée. Il y en a deux : les 58
     * images de 2020, et les albédos CC0 rapportés là où le modèle d'origine
     * n'avait qu'un aplat de couleur. `--textures=` est donc répétable plutôt
     * que d'exiger de tout entasser dans un seul dossier — mélanger l'art
     * d'origine et l'art rapporté rendrait impossible de dire, plus tard, d'où
     * vient quoi.
     */
    const char *texture_dirs[RG_MAX_TEXTURE_DIRS];
    size_t      texture_dir_count;
    int expect_textures;        /* < 0 : pas de contrôle de couverture */
    size_t      retired_count;  /* images déclarées volontairement inemployées */
} rg_builder;

/* Enregistre le dernier objet émis comme obstacle. Appelé explicitement par les
 * sections qui produisent du volume plein, jamais par les autres. */
static void record_solid(rg_builder *b, const char *name, rg_solid_kind kind)
{
    if (b->solid_count >= RG_MAX_SOLIDS) return;   /* le contrôle n'est pas critique */
    rg_solid *s = &b->solids[b->solid_count++];
    /* Tronqué sciemment : ce nom ne sert qu'aux messages, pas à un appariement. */
    snprintf(s->name, sizeof s->name, "%.63s", name);
    s->bounds = b->last_bounds;
    s->kind = kind;
}

/*
 * Deux meubles ne doivent pas occuper le même volume, et c'est à l'outil de le
 * dire.
 *
 * Ce contrôle existe parce que le défaut s'est produit : en redimensionnant la
 * salle j'ai multiplié les POSITIONS par un facteur sans toucher aux TAILLES —
 * correct pour un meuble, faux pour tout ce qui dépend d'un écart. L'entraxe des
 * bornes est passé de 0,80 m à 0,69 m pour un caisson de 0,72 m : les douze
 * bornes de l'îlot se sont encastrées les unes dans les autres, et personne ne
 * l'a vu avant que le joueur ne le signale.
 *
 * La tolérance vaut 5 mm : deux meubles bord à bord sont voulus, deux meubles à
 * un centimètre l'un dans l'autre sont une faute de frappe.
 */
#define RG_OVERLAP_TOLERANCE 0.005f

static float overlap_1d(float amin, float amax, float bmin, float bmax)
{
    const float lo = amin > bmin ? amin : bmin;
    const float hi = amax < bmax ? amax : bmax;
    return hi - lo;
}

/*
 * Un luminaire doit se trouver dans le lieu dont il porte le nom.
 *
 * Ce contrôle vient d'une bévue réelle, et de deux occurrences plutôt qu'une :
 * en réécrivant la disposition j'ai déplacé les lieux sans renommer les
 * lumières. `plafonnier_toilettes` s'est retrouvé à **17 m** du bloc sanitaire,
 * dans le sas d'entrée ; `plafonnier_bar` à **12,6 m** du comptoir. Les
 * toilettes n'avaient donc plus aucun éclairage, et le seul néon qui grésille de
 * la salle portait le nom d'une pièce où il n'était pas.
 *
 * Rien ne l'aurait signalé : une lumière mal placée éclaire quelque chose, donc
 * l'image reste plausible. C'est le pire cas de figure — un défaut qui produit
 * un résultat, et qu'on ne trouve qu'en mesurant.
 *
 * La règle est volontairement grossière : un nom se terminant par `_<lieu>` doit
 * être à moins de `RG_FIXTURE_RADIUS` de l'ancre de ce lieu. Elle n'attrape pas
 * un décalage d'un mètre, et ce n'est pas le but : elle attrape le nom qui ment.
 */
#define RG_FIXTURE_RADIUS 6.0f

static void check_fixture_naming(const rg_builder *b)
{
    size_t checked = 0;
    for (size_t i = 0; i < b->light_count; ++i) {
        const rg_light *l = &b->lights[i];

        /* Une déclaration répétée numérote ses exemplaires : `plafonnier_bar`
         * devient `plafonnier_bar_1`, `_2`… Sans retirer ce suffixe le contrôle
         * ne voyait qu'un luminaire sur quatorze — et laissait justement passer
         * les deux qu'il devait attraper. */
        char stem[64];
        snprintf(stem, sizeof stem, "%s", l->name);
        char *last = strrchr(stem, '_');
        if (last) {
            bool all_digits = last[1] != '\0';
            for (const char *c = last + 1; *c; ++c) {
                if (*c < '0' || *c > '9') { all_digits = false; break; }
            }
            if (all_digits) *last = '\0';
        }

        const char *tail = strrchr(stem, '_');
        if (!tail) continue;
        tail++;

        for (size_t j = 0; j < b->poi_count; ++j) {
            const rg_poi *poi = &b->pois[j];
            if (strcmp(tail, poi->kind) != 0) continue;

            const float dx = l->position[0] - poi->anchor[0];
            const float dz = l->position[2] - poi->anchor[2];
            const float d = sqrtf(dx * dx + dz * dz);
            checked++;
            if (d > RG_FIXTURE_RADIUS) {
                tool_fatalf("« %s » est à %.1f m de « %s » (%s), qu'il prétend éclairer.\n"
                            "  luminaire  (%.2f, %.2f, %.2f)\n"
                            "  lieu       (%.2f, %.2f, %.2f)\n"
                            "  Soit le luminaire est mal placé, soit il porte le nom d'un "
                            "autre lieu. Les deux se corrigent ici ; aucun ne se voit sur "
                            "une capture.",
                            l->name, (double)d, poi->name, poi->kind,
                            (double)l->position[0], (double)l->position[1],
                            (double)l->position[2],
                            (double)poi->anchor[0], (double)poi->anchor[1],
                            (double)poi->anchor[2]);
            }
        }
    }
    if (checked) printf("  %zu luminaire(s) confronté(s) au lieu qu'ils nomment\n", checked);
}

static void check_solid_overlaps(const rg_builder *b)
{
    size_t warned = 0;
    for (size_t i = 0; i < b->solid_count; ++i) {
        for (size_t j = i + 1; j < b->solid_count; ++j) {
            const rg_solid *A = &b->solids[i], *B = &b->solids[j];

            const float ox = overlap_1d(A->bounds.min.x, A->bounds.max.x,
                                        B->bounds.min.x, B->bounds.max.x);
            const float oy = overlap_1d(A->bounds.min.y, A->bounds.max.y,
                                        B->bounds.min.y, B->bounds.max.y);
            const float oz = overlap_1d(A->bounds.min.z, A->bounds.max.z,
                                        B->bounds.min.z, B->bounds.max.z);
            if (ox <= RG_OVERLAP_TOLERANCE || oy <= RG_OVERLAP_TOLERANCE
             || oz <= RG_OVERLAP_TOLERANCE) {
                continue;                       /* disjoints sur au moins un axe */
            }

            /*
             * Fatal dès qu'une BORNE est en cause, et seulement là.
             *
             * Une borne doit se tenir dans du vide : on ne joue pas sur un
             * caisson encastré dans son voisin. Le reste s'imbrique légitimement
             * — une poutre repose sur ses piliers, un tabouret glisse sous un
             * comptoir, une affiche se plaque contre un mur. Le premier essai de
             * ce contrôle refusait la poutre et son pilier, ce qui aurait
             * transformé un garde-fou utile en bruit qu'on apprend à ignorer.
             */
            const bool fatal = (A->kind == RG_SOLID_CABINET) || (B->kind == RG_SOLID_CABINET);
            if (fatal) {
                tool_fatalf("« %s » et « %s » occupent le même volume : "
                            "recouvrement de %.3f x %.3f x %.3f m\n"
                            "  %s : [%.2f %.2f %.2f] - [%.2f %.2f %.2f]\n"
                            "  %s : [%.2f %.2f %.2f] - [%.2f %.2f %.2f]",
                            A->name, B->name, (double)ox, (double)oy, (double)oz,
                            A->name,
                            (double)A->bounds.min.x, (double)A->bounds.min.y, (double)A->bounds.min.z,
                            (double)A->bounds.max.x, (double)A->bounds.max.y, (double)A->bounds.max.z,
                            B->name,
                            (double)B->bounds.min.x, (double)B->bounds.min.y, (double)B->bounds.min.z,
                            (double)B->bounds.max.x, (double)B->bounds.max.y, (double)B->bounds.max.z);
            }
            if (warned < 8) {
                tool_warnf("« %s » et « %s » se recouvrent de %.2f x %.2f x %.2f m "
                           "(structure ou mobilier : toléré)",
                           A->name, B->name, (double)ox, (double)oy, (double)oz);
                warned++;
            }
        }
    }
    if (warned >= 8) tool_infof("... et d'autres recouvrements de mobilier");
}

/* Comme `material_index`, mais rend −1 au lieu d'arrêter l'outil : pour les
 * matériaux facultatifs, dont l'absence a un repli sensé. */
static int material_index_opt(const rg_builder *b, const char *name)
{
    if (!name || !name[0]) return -1;
    for (size_t i = 0; i < b->material_count; ++i) {
        if (strcmp(b->material_names[i], name) == 0) return (int)i;
    }
    return -1;
}

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
    b->last_bounds = geo_mesh_bounds(m);
    b->bounds = ns_aabb_union(b->bounds, b->last_bounds);

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
    if (b->texture_dir_count) {
        bool found = false;
        for (size_t d = 0; d < b->texture_dir_count && !found; ++d) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s", b->texture_dirs[d], file);
            FILE *f = fopen(path, "rb");
            if (f) { fclose(f); found = true; }
        }
        if (!found) {
            char dirs[1024] = { 0 };
            for (size_t d = 0; d < b->texture_dir_count; ++d) {
                snprintf(dirs + strlen(dirs), sizeof dirs - strlen(dirs),
                         "%s%s", d ? ", " : "", b->texture_dirs[d]);
            }
            tool_fatalf("le matériau « %s » réclame la texture « %s », absente de %s",
                        used_by, file, dirs);
        }
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
        tool_json_get_string(doc, e, "footstep", b->material_footstep[slot],
                             sizeof b->material_footstep[slot]);
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
            record_solid(b, name, RG_SOLID_BOX);
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
/* Bornes d'arcade                                                            */
/* ========================================================================== */

/*
 * La borne mérite un générateur à elle : elle est répétée dix-neuf fois, elle
 * porte l'écran — la seule surface du décor dont la géométrie doit être exacte,
 * puisqu'un jeu s'y affichera — et sa silhouette est ce qui fait lire la salle
 * comme une salle d'arcade.
 *
 * **Cotes réelles**, et c'est un changement visible qu'il vaut mieux annoncer que
 * laisser découvrir : 0,72 x 1,86 x 0,88 m. Le modèle de 2020 mélangeait deux
 * échelles — ses bornes faisaient 2,39 m dans une salle de 2,92 m, soit des
 * bornes de géant ou un plafond de cave. On garde l'échelle de l'architecture et
 * on redimensionne les bornes ; elles paraîtront donc plus petites par rapport à
 * la salle qu'à l'origine, et le hall y gagne le dégagement demandé.
 *
 * Repère local : la borne regarde +Z, son socle est en Y = 0, et elle est centrée
 * en X. Le placement se fait ensuite par un lacet, comme pour tout le reste.
 */
#define RG_CAB_W  0.72f
#define RG_CAB_H  1.88f

/*
 * LES HAUTEURS DE LA BORNE, et pourquoi elles ont été RÉÉCRITES une par une.
 *
 * Trois versions, dont deux fausses. Elles sont toutes racontées ici parce que
 * la deuxième erreur vient directement de la façon dont j'avais corrigé la
 * première, et que la table seule ne le dirait pas.
 *
 * 1. A4 ramène les bornes de 2,39 m — la cote du modèle de 2020 — à 1,86 m, au
 *    motif que c'était la cote « réelle ». Le plan annonçait la conséquence
 *    (« les bornes paraîtront plus petites ») ; je l'ai écrite, livrée, et
 *    jamais mesurée.
 *
 * 2. Constat manette en main : l'œil est à 1,70 m, le centre de la dalle à
 *    1,26 m, et l'on se tient à 68 cm — soit 33° SOUS l'horizontale. Le champ
 *    de vision vertical vaut 62°, donc ±31° : le centre de la dalle tombait
 *    DEHORS, sous le bord bas de l'image. D'où « je suis obligé de
 *    m'accroupir » : à 1,31 m d'œil, l'écran revient au centre.
 *
 * 3. Ma première correction a été un facteur unique de 1,18 appliqué à toutes
 *    les cotes au-dessus du socle. Ça ramenait bien la plongée à 19°, et c'est
 *    FAUX quand même : la borne montait à 2,18 m — plus haute que n'importe
 *    quelle borne d'arcade ayant existé, les plus grandes plafonnant vers
 *    1,88 m — et le panneau de commande partait à 1,12 m, soit vingt
 *    centimètres au-dessus d'un plan de travail. On jouait sur une étagère.
 *    Corriger « on doit s'accroupir » en aggravant « les bornes ne ressemblent
 *    à rien » n'est pas une correction, c'est un échange.
 *
 * Ce que fait cette table-ci : elle garde des cotes de VRAIE borne — 1,88 m
 * hors tout, panneau de commande à 0,96 m (les uprights des années 80 sont
 * entre 0,91 et 0,97 m) — et remonte la DALLE À L'INTÉRIEUR du caisson, de
 * 1,26 m à 1,38 m. C'est la proportion des bornes réelles, où le centre de
 * l'écran est vers 73 % de la hauteur, contre 68 % chez nous.
 *
 * Ce que ça donne : 25° de plongée au lieu de 33°, donc la dalle est DANS le
 * cadre au lieu d'être sous lui, et avec les 15° d'inclinaison il reste 10°
 * hors axe. Ce n'est pas 0° : une borne d'arcade se regarde en baissant les
 * yeux, et c'est pour ça que la caméra vient se poser sur la dalle quand la
 * partie démarre (`room/main.c`) — c'est le mouvement de tête qu'on fait
 * devant une vraie borne, et c'est lui qui ferme la question, pas un caisson
 * qu'on étire.
 */

/* Le socle touche le sol : c'est du mobilier posé. */
#define RG_CAB_BASE 0.10f
#define RG_CAB_D  0.88f

/*
 * La dalle d'une borne a son PROPRE matériau, même quand deux bornes affichent
 * le même jeu.
 *
 * Sans ça, `ecran_snake` était un seul matériau partagé par les deux bornes
 * Snake — et comme le moteur allume un écran vivant en surchargeant un MATÉRIAU
 * (`ns_renderer_set_screen`), jouer sur l'une faisait apparaître la partie sur
 * l'autre, à l'autre bout de la salle. Sept matériaux d'écran sur dix-neuf
 * bornes étaient dans ce cas ; le défaut ne se voyait pas tant qu'un seul jeu
 * était porté, parce qu'il fallait deux bornes du même jeu dans le même cadre.
 *
 * Le clone garde la texture et les réglages : c'est la même image d'attente, ce
 * n'est plus la même surface.
 */
static int clone_screen_material(rg_builder *b, int src, const char *cabinet)
{
    if (src < 0) return src;
    if (b->material_count >= RG_MAX_MATERIALS) {
        tool_fatalf("plus de %d matériaux (clone d'écran pour « %s »)",
                    RG_MAX_MATERIALS, cabinet);
    }
    /* On copie la source AVANT d'écrire la destination : les deux vivent dans le
     * même tableau, et `snprintf` d'un tampon vers un autre du même objet est un
     * chevauchement que le compilateur signale à juste titre. */
    char src_name[64];
    snprintf(src_name, sizeof src_name, "%s", b->material_names[src]);
    char src_step[24];
    snprintf(src_step, sizeof src_step, "%s", b->material_footstep[src]);

    const size_t slot = b->material_count++;
    b->materials[slot] = b->materials[src];
    b->material_uv[slot] = b->material_uv[src];
    b->material_fit[slot] = b->material_fit[src];
    snprintf(b->material_footstep[slot], sizeof b->material_footstep[slot], "%s", src_step);
    snprintf(b->material_names[slot], sizeof b->material_names[slot], "%.40s@%.20s",
             src_name, cabinet);
    return (int)slot;
}

static void build_cabinet(rg_builder *b, geo_mesh *out, const tool_json *doc,
                          const tool_json_value *e, const char *owner,
                          rg_cab_anchors *anchors)
{
    float *const screen_local = anchors->screen;
    float *const screen_size  = anchors->screen_size;

    char m_body[64], m_screen[64], m_marquee[64], m_panel[64], m_trim[64], m_side[64];
    tool_json_get_string(doc, e, "materialBody", m_body, sizeof m_body);
    tool_json_get_string(doc, e, "materialSide", m_side, sizeof m_side);
    tool_json_get_string(doc, e, "screen", m_screen, sizeof m_screen);
    tool_json_get_string(doc, e, "marquee", m_marquee, sizeof m_marquee);
    tool_json_get_string(doc, e, "materialPanel", m_panel, sizeof m_panel);
    tool_json_get_string(doc, e, "materialTrim", m_trim, sizeof m_trim);

    const int body   = material_index(b, m_body, owner);
    const int screen = clone_screen_material(b, material_index(b, m_screen, owner), owner);
    anchors->screen_material = screen;
    const int marq   = material_index(b, m_marquee[0] ? m_marquee : m_body, owner);
    const int panel  = material_index(b, m_panel[0] ? m_panel : m_body, owner);
    const int trim   = material_index(b, m_trim[0] ? m_trim : m_body, owner);
    /*
     * Le flanc. Les deux bouchons de l'extrusion SONT les deux flancs de la
     * borne, et un flanc de borne d'arcade porte une sérigraphie — le dégradé et
     * la trame en losanges des vues de référence — que le caisson n'a pas.
     *
     * Facultatif : sans `materialSide`, le flanc reprend la peinture du caisson
     * et la borne est exactement celle d'avant. C'est ce qui permet de l'ajouter
     * borne par borne sans casser une salle qui ne le déclare pas.
     */
    const int flank  = material_index(b, m_side[0] ? m_side : m_body, owner);
    /*
     * Le noir des bandeaux. Facultatif : une salle qui ne le déclare pas retombe
     * sur le cadre clair et reste correcte — c'était l'état d'avant.
     */
    const int dark_i = material_index_opt(b, "borne_noir");
    const int dark   = (dark_i >= 0) ? dark_i : trim;
    /*
     * La grille de haut-parleur, et pourquoi elle mérite son propre matériau.
     *
     * Les deux couronnes étaient dessinées dans le NOIR du bandeau — noir sur
     * noir, donc invisibles. Résultat : entre l'écran et le marquee, la borne
     * n'avait qu'une plaque morte d'un seul tenant, qui occupe le quart de sa
     * face vue d'en haut. Sur les images de référence c'est exactement le
     * contraire : les deux grilles rondes sont ce qui donne un visage à la
     * machine.
     *
     * Une grille se lit parce qu'elle est MÉTALLIQUE : elle accroche une
     * lumière que la plaque mate absorbe. C'est le seul écart nécessaire —
     * même noir, même place, même géométrie.
     *
     * Facultative comme `borne_noir` : sans elle, on retombe sur le bandeau et
     * la borne est celle d'avant.
     */
    const int grille_i = material_index_opt(b, "borne_grille");
    const int grille   = (grille_i >= 0) ? grille_i : dark;

    const geo_uv uv_body = material_uv(b, body);
    const geo_uv uv_trim = material_uv(b, trim);
    const geo_uv uv_dark = material_uv(b, dark);
    const float hw = RG_CAB_W * 0.5f, hd = RG_CAB_D * 0.5f;

    /* ======================================================================
     * Le caisson : une EXTRUSION DE PROFIL, plus une boîte
     * ======================================================================
     * Ce qui fait qu'on reconnaît une borne d'arcade au premier coup d'œil n'est
     * ni sa couleur ni son marquee : c'est son PROFIL LATÉRAL EN GRADINS. Le
     * caisson était un seul `geo_box` — un parallélépipède avec des
     * décalcomanies posées dessus. Aucun des neuf gradins ci-dessous n'existait,
     * et c'est toute la raison pour laquelle la borne se lisait comme une armoire.
     *
     * Le profil se lit sur l'arête AVANT, du sol vers le haut ; le dos, lui, est
     * vertical du sol au plafond. Les cotes viennent des vues de référence.
     *
     * Repère : `geo_profile_extrude` dessine le profil perpendiculairement au
     * chemin, `x` du profil vers `cross(+Y, tangente)`. Avec un chemin le long
     * de +X, ça donne −Z : un point AVANT (z positif) a donc un `x` de profil
     * NÉGATIF, d'où le `-z` à la conversion. C'est écrit une fois ici plutôt
     * que quinze fois dans la table.
     */
    struct { float z, y; } silhouette[] = {
        /* Le caisson commence à 10 cm : il REPOSE sur son socle, et c'est le
         * socle seul qui touche le sol. Descendre le profil jusqu'à zéro faisait
         * mordre la borne dans le tapis technique de 8 mm — le contrôle de
         * chevauchement de `roomgen` l'a dit tout de suite, ce pour quoi il
         * existe. */
        { 0.30f, 0.10f },   /* pied avant, sur le socle en retrait     */
        { 0.30f, 0.86f },   /* face avant verticale — porte à monnayeur */
        { 0.42f, 0.90f },   /* le panneau de commande JAILLIT           */
        { 0.42f, 0.96f },   /* son nez                                 */
        { 0.34f, 1.02f },  /* sa tôle, inclinée vers le joueur        */
        { 0.18f, 1.10f },   /* on recule franchement sous l'écran      */
        { 0.14f, 1.15f },   /* pied du cadre d'écran                   */
        { 0.10f, 1.61f },   /* l'écran, incliné vers l'arrière         */
        { 0.04f, 1.67f },   /* pied du panneau haut-parleurs           */
        {-0.04f, 1.72f },   /* le panneau HP, incliné                  */
        {-0.04f, 1.88f },   /* montant jusqu'au sommet                 */
        {-hd,    1.88f },   /* le dessus, vers l'arrière               */
        {-hd,    0.10f },   /* le dos, vertical                        */
    };
    const size_t sil_count = sizeof silhouette / sizeof silhouette[0];

    ns_v2 profile[sizeof silhouette / sizeof silhouette[0]];
    for (size_t i = 0; i < sil_count; ++i) {
        profile[i] = ns_v2_make(-silhouette[i].z, silhouette[i].y);
    }

    /* Le chemin traverse la largeur. Un profil FERMÉ sur un chemin OUVERT reçoit
     * deux bouchons en éventail : ce sont les deux flancs, et ce sont eux qui
     * portent l'art latéral. */
    const ns_v3 sweep[2] = { ns_v3_make(-hw, 0.0f, 0.0f), ns_v3_make(hw, 0.0f, 0.0f) };

    geo_mesh part; geo_mesh_init(&part);
    /* `cap_uv_fit` : la planche du flanc se pose ENTIÈRE sur la silhouette, de
     * (0,0) à (1,1). Sans lui les UV du bouchon seraient en mètres par
     * répétition comme pour une moulure, et la sérigraphie arriverait coupée. */
    geo_profile_extrude_capped(&part, profile, sil_count, true, sweep, 2, false,
                               &uv_body, body, flank, true);
    geo_xform x = GEO_XFORM_IDENTITY;
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    /* Le socle noir. Il est en retrait de 3 cm : c'est l'ombre de ce retrait qui
     * fait qu'une borne « pose » sur le sol au lieu d'y être posée. */
    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(RG_CAB_W - 0.06f, 0.10f, RG_CAB_D - 0.06f), 0.006f,
            GEO_FACE_SIDES, &uv_dark, dark);
    x = GEO_XFORM_IDENTITY;
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    /* --- le panneau haut-parleurs ----------------------------------------- */
    /*
     * Le plan noir incliné entre l'écran et le marquee, et ses DEUX grilles
     * rondes. C'est un détail qu'on ne remarque que quand il manque : sans lui
     * la borne a un trou entre son écran et son enseigne.
     *
     * Il suit EXACTEMENT le gradin du profil, entre (0,04 ; 1,56) et
     * (−0,04 ; 1,68). Le premier jet posait une boîte à une hauteur devinée et
     * elle ne couvrait rien : la face rouge du caisson restait visible en
     * dessous, et le panneau se lisait comme une marche flottante. Une plaque
     * calée sur le segment ne peut pas rater sa cible.
     */
    /* La plaque couvre les DEUX gradins, du haut du cadre d'écran au marquee.
     * Limitée au seul gradin supérieur, elle laissait une bande rouge de
     * caisson entre l'écran et les haut-parleurs — sur les vues de référence
     * tout ce bandeau est noir d'un seul tenant. */
    const float hp_z0 = 0.10f, hp_y0 = 1.61f, hp_z1 = -0.04f, hp_y1 = 1.72f;
    const float hp_dz = hp_z1 - hp_z0, hp_dy = hp_y1 - hp_y0;
    const float hp_len = sqrtf(hp_dz * hp_dz + hp_dy * hp_dy);
    /* Normale du gradin, tournée vers l'avant et le haut : (dy, −dz) normalisé
     * dans le plan (z, y). */
    const ns_v3 hp_n = ns_v3_make(0.0f, hp_dy / hp_len, -hp_dz / hp_len);
    /* `pitch` envoie +Y sur (0, cos p, sin p) : c'est la même convention que le
     * panneau de commande et le cadre d'écran. */
    const float hp_pitch = atan2f(hp_n.z, hp_n.y);
    const ns_v3 hp_mid = ns_v3_make(0.0f, (hp_y0 + hp_y1) * 0.5f + hp_n.y * 0.006f,
                                    (hp_z0 + hp_z1) * 0.5f + hp_n.z * 0.006f);

    geo_panel(out, hp_mid, hp_n, ns_v3_make(1, 0, 0),
              RG_CAB_W - 0.03f, hp_len, 0.0f, 0.0f, 1.0f, 1.0f, dark);

    for (int side = -1; side <= 1; side += 2) {
        /*
         * UNE couronne de 12 côtés, et non deux de 20 et 16.
         *
         * Le premier jet en mettait deux par grille, soit ~360 sommets de plus
         * par borne et 6 800 sur les dix-neuf. Multiplié par les autres détails
         * ajoutés ici, la salle est passée de 144 941 à 155 239 sommets — et
         * au-delà d'environ 150 000, le rasteriseur logiciel du conteneur cesse
         * de composer l'image : la même vue en « ultra » est devenue noire à
         * 95 %. Sur un vrai GPU ce serait invisible ; ici ça m'empêche de
         * VÉRIFIER, et un détail qu'on ne peut pas regarder ne vaut rien.
         *
         * Une couronne à 12 côtés à deux mètres se lit exactement pareil.
         */
        geo_mesh_init(&part);
        geo_cylinder(&part, 0.050f, 0.040f, 0.009f, 12, false, true, &uv_dark, grille);
        x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make((float)side * 0.185f,
                              hp_mid.y + 0.045f + hp_n.y * 0.002f,
                              hp_mid.z - 0.033f + hp_n.z * 0.002f);
        x.pitch = hp_pitch;
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);
    }

    /* --- le marquee, caisson lumineux en SURPLOMB -------------------------- */
    /*
     * Un marquee est une boîte lumineuse rapportée qui DÉBORDE, pas une
     * décalcomanie sur la face — c'est ce surplomb qui donne à la borne sa
     * silhouette de haut.
     *
     * 16 cm de haut, et c'est une correction : il en faisait 26, ce qui n'est
     * plus une enseigne mais un panneau publicitaire. Les marquees réels sont
     * entre 15 et 18 cm, et le nôtre couvre EXACTEMENT la tête du caisson
     * (1,72 -> 1,88) au lieu de flotter par-dessus.
     */
    /*
     * `mq_y` est le BAS du caisson, pas son centre : `geo_box` est posée sur
     * Y = 0 (cf. `geo_shapes.h`). La tête du profil va de 1,72 à 1,88, le
     * marquee la couvre donc exactement — il ne dépasse plus le sommet du
     * caisson, ce qu'aucune borne réelle ne fait.
     */
    const float mq_y = 1.72f, mq_h = 0.16f;
    /* Le surplomb mesuré sur les vues : ~16 cm. Un marquee trop profond cesse
     * d'être une enseigne et devient un auvent — c'est ce que donnait le premier
     * jet, et ça se voyait tout de suite dans l'allée. La face avant tombe à
     * z = 0,20, soit 2,5 cm devant le verre de la dalle : sur une vraie borne
     * l'enseigne affleure la vitre, elle ne la surplombe pas. */
    const float mq_depth = 0.16f, mq_front = 0.20f;

    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(RG_CAB_W + 0.02f, mq_h, mq_depth), 0.008f,
            GEO_FACE_ALL & ~GEO_FACE_PZ, &uv_dark, dark);
    x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(0.0f, mq_y, mq_front - mq_depth * 0.5f);
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    /* L'enseigne elle-même, en saillie de 8 mm sur la face du caisson lumineux,
     * centrée à mi-hauteur — d'où le `+ mq_h/2` : `mq_y` est le bas de la
     * boîte, `geo_panel` prend un centre. */
    geo_panel(out, ns_v3_make(0.0f, mq_y + mq_h * 0.5f, mq_front + 0.008f),
              ns_v3_make(0.0f, 0.0f, 1.0f), ns_v3_make(1, 0, 0),
              RG_CAB_W - 0.04f, mq_h - 0.05f, 0.0f, 0.0f, 1.0f, 1.0f, marq);

    /* --- la porte à monnayeur ---------------------------------------------- */
    /*
     * LE détail qui dit « borne » plus fort que tout le reste — et le premier
     * jet ne le disait pas. Vu de près il donnait un caisson noir en relief,
     * une trappe noire posée dessus, et deux pastilles rouges de 3 cm perdues
     * au milieu : de loin un boîtier aux lettres vissé sur le meuble, de près
     * un aplat noir sur un aplat noir.
     *
     * Ce qui fait lire une porte à monnayeur n'est pas sa forme, c'est son
     * CHROME. Une vraie porte est en tôle noire avec trois pièces brillantes —
     * les deux plaques de fente et la lèvre du sébile — et ce sont elles qu'on
     * voit à deux mètres, parce qu'elles accrochent la lumière quand le noir
     * l'absorbe. C'est exactement le raisonnement de `borne_grille` pour les
     * haut-parleurs, et il vaut ici pour la même raison.
     *
     * Le relief passe aussi de 30 à 18 mm : une porte de monnayeur affleure
     * presque la face, elle ne s'y boulonne pas.
     */
    const float door_z = 0.30f;
    const int chrome_i = material_index_opt(b, "borne_grille");
    const int chrome   = (chrome_i >= 0) ? chrome_i : trim;
    const geo_uv uv_chrome = material_uv(b, chrome);

    /* La tôle de la porte. `geo_box` est posée sur Y = 0 : `origin.y` est donc
     * le BAS de la porte, pas son centre. */
    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(0.20f, 0.46f, 0.018f), 0.004f,
            GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_dark, dark);
    x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(0.0f, 0.30f, door_z);
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    /* Les deux plaques de fente, en chrome, et la fente noire dedans. */
    for (int side = -1; side <= 1; side += 2) {
        const float sx = (float)side * 0.048f;

        geo_mesh_init(&part);
        geo_box(&part, ns_v3_make(0.052f, 0.090f, 0.008f), 0.002f,
                GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_chrome, chrome);
        x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(sx, 0.615f, door_z + 0.018f);
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);

        /* La fente elle-même : une rainure sombre dans la plaque. Sans elle la
         * plaque est un rectangle brillant qui ne dit rien. */
        geo_mesh_init(&part);
        geo_box(&part, ns_v3_make(0.006f, 0.038f, 0.004f), 0.0f,
                GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_dark, dark);
        x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(sx, 0.640f, door_z + 0.026f);
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);
    }

    /*
     * Le sébile de rendu : un creux noir, et sa LÈVRE chromée par-dessous.
     * C'est la lèvre qui se voit — le creux, par définition, est une ombre.
     */
    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(0.11f, 0.055f, 0.010f), 0.003f,
            GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_dark, dark);
    x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(0.0f, 0.395f, door_z + 0.018f);
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(0.13f, 0.014f, 0.022f), 0.003f,
            GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_chrome, chrome);
    x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(0.0f, 0.381f, door_z + 0.018f);
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    /* La trappe de caisse, sous le sébile : un panneau à peine en relief, avec
     * sa serrure. Elle était aussi haute que la moitié de la porte et posée en
     * plein milieu, ce qui la faisait lire comme un trou. */
    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(0.15f, 0.055f, 0.006f), 0.003f,
            GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_dark, dark);
    x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(0.0f, 0.315f, door_z + 0.018f);
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    geo_mesh_init(&part);
    geo_box(&part, ns_v3_make(0.016f, 0.016f, 0.006f), 0.002f,
            GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_chrome, chrome);
    x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(0.0f, 0.334f, door_z + 0.024f);
    geo_mesh_append(out, &part, &x, -1);
    geo_mesh_free(&part);

    /* --- écran ------------------------------------------------------------ */
    /*
     * **La seule surface dont la géométrie doit être juste.** Elle est déclarée
     * au moteur, coordonnées comprises, et c'est ce qui remplace les fractions
     * inventées de `load_cabinet_assignment` — lesquelles plaçaient le centre de
     * l'écran 31 cm trop bas et large de 1,1 unité.
     *
     * Inclinée de 10° vers l'arrière, comme une vraie dalle d'arcade, et à
     * 1,26 m : la hauteur d'yeux d'un joueur debout de 1,70 m qui regarde
     * légèrement vers le bas.
     *
     * **16:9, et ce n'est pas un choix esthétique : c'est ce que les images
     * disent.** Les onze écrans peints de 2020 — `flappy_easy_font.jpg`,
     * `snake_font.jpg`, `tetris_font.jpg`… — font tous 1920 x 1080, et le jeu
     * de 2020 tourne lui-même en 1920 x 1080 (`WINDOW_L` / `WINDOW_H`,
     * `legacy/games/3_flappy_bird/flappy_bird.c:22`). La dalle était en 4:3
     * (0,56 x 0,42) : chaque image d'écran de la salle y était donc rognée ou
     * déformée, et la partie en cours s'y affichait en boîte aux lettres, moitié
     * moins haute que le marquee juste au-dessus.
     *
     * La largeur est ce qui contraint : 0,72 m de caisson moins deux plats de
     * 35 mm et deux jeux de 10 mm laissent 0,62 m, d'où 0,349 m de haut. La
     * cible de rendu de la partie fait 512 x 288 — le même rapport, donc l'image
     * remplit la dalle exactement, sans bande ni étirement.
     */
    const float sw = 0.62f, sh = 0.349f;
    /*
     * La dalle est légèrement EN SAILLIE du caisson, pas enfoncée dedans.
     *
     * Elle était à `hd − 0,055`, soit **5,5 cm à l'intérieur** d'une boîte
     * pleine dont la face avant est à `hd` : le caisson, plus proche de l'œil,
     * gagnait le test de profondeur et l'écran n'a jamais été visible depuis A4.
     * Ce qu'on prenait pour l'écran sur les captures était le marquee.
     *
     * Le vrai remède serait une découpe dans la face avant — `geo_box` ne sait
     * pas la faire, et écrire un générateur de caisson à quatre panneaux pour
     * cette seule ouverture coûterait plus que ça ne vaut. Une dalle proéminente
     * de 8 mm, encadrée par ses plats eux-mêmes proéminents, donne exactement la
     * même lecture : une vitre sertie dans un cadre.
     */
    /*
     * 4 cm de saillie, et non 8 mm — parce que la dalle est INCLINÉE.
     *
     * Elle bascule de 10° vers l'arrière autour de son centre : son bord
     * supérieur recule donc de `sin(10°) x sh/2` = 3,0 cm. À 8 mm de saillie, ce
     * bord se retrouvait 2,2 cm DANS le caisson, et la moitié haute de l'image
     * était mangée par le meuble — le titre du classement et sa première ligne
     * n'étaient pas visibles. Même défaut qu'en A4, où la dalle entière était
     * enfoncée de 5,5 cm ; cette fois c'est l'inclinaison qui l'y remettait, et
     * il a fallu un tableau de scores pour s'en apercevoir : sur un jeu qui
     * remplit l'écran, perdre le haut de l'image ne se remarque pas.
     *
     * 4 cm laissent 1 cm de dégagement au bord haut. Les plats du cadre suivent
     * la même inclinaison et restent solidaires.
     */
    /*
     * `sz` n'est plus `hd + marge` : le caisson n'est plus une boîte, sa face
     * RECULE en montant. La dalle se pose donc sur la face du profil à sa
     * hauteur, plus la saillie.
     *
     * À y = 1,26 la face court entre (0,14 ; 1,10) et (0,10 ; 1,49), soit
     * z = 0,124. Les 4 cm de saillie restent nécessaires pour la même raison
     * qu'avant : la dalle bascule de 10°, son bord haut recule donc de
     * sin(10°) x sh/2 = 3,0 cm, et sans marge ce bord repasserait dans le
     * meuble — c'est ce qui avait mangé le tiers haut de l'image en B6c.
     */
    const float sy = 1.38f;
    /*
     * 15° et non 10°. Un moniteur de borne d'arcade est monté entre 10 et 20°
     * de la verticale ; 10° était le bas de la fourchette, choisi quand la
     * dalle était encore à 1,26 m et qu'on la regardait 23° hors axe de toute
     * façon. Avec 25° de plongée, 15° d'inclinaison ramènent l'écart à 10° —
     * l'image cesse d'être fuyante.
     *
     * La marge devant la face suit : le bord HAUT de la dalle recule de
     * sin(15°) x sh/2 = 4,5 cm, contre 3,0 cm à 10°. Les 40 mm d'avant le
     * feraient repasser dans le meuble — c'est exactement ce qui avait mangé
     * le tiers haut de l'image en B6c, et la marge est calculée ici plutôt que
     * réécrite à la main pour que ça ne puisse plus arriver.
     */
    const float tilt = 15.0f * NS_DEG2RAD;
    /*
     * La face du profil à la hauteur de la dalle : elle court de (0,14 ; 1,15)
     * à (0,10 ; 1,61), donc z = 0,12 à y = 1,38. La marge est CALCULÉE depuis
     * l'inclinaison et la hauteur d'image, plus posée à la main : c'est la
     * seule forme qui reste juste quand on retouche l'une ou l'autre.
     */
    const float sz = 0.12f + sinf(tilt) * sh * 0.5f + 0.010f;
    const ns_v3 snormal = ns_v3_make(0.0f, sinf(tilt), cosf(tilt));

    geo_panel(out, ns_v3_make(0.0f, sy, sz), snormal, ns_v3_make(1, 0, 0),
              sw, sh, 0.0f, 0.0f, 1.0f, 1.0f, screen);

    anchors->screen_tilt = tilt;
    screen_local[0] = 0.0f; screen_local[1] = sy; screen_local[2] = sz;
    screen_size[0] = sw; screen_size[1] = sh;

    /* Cadre de la dalle : quatre plats qui enferment l'écran. Sans eux l'image
     * flotte sur le caisson et la borne perd son épaisseur. */
    const float bez = 0.035f;
    const struct { float cx, cy, w, h; } bezel[4] = {
        {  0.0f, sy + sh * 0.5f + bez * 0.5f, sw + bez * 2.0f, bez },
        {  0.0f, sy - sh * 0.5f - bez * 0.5f, sw + bez * 2.0f, bez },
        { -(sw * 0.5f + bez * 0.5f), sy, bez, sh },
        {  (sw * 0.5f + bez * 0.5f), sy, bez, sh },
    };
    for (int i = 0; i < 4; ++i) {
        geo_mesh_init(&part);
        geo_box(&part, ns_v3_make(bezel[i].w, bezel[i].h, 0.05f), 0.004f,
                GEO_FACE_ALL & ~GEO_FACE_NZ, &uv_dark, dark);
        x = GEO_XFORM_IDENTITY;
        /* Les plats suivent l'inclinaison de la dalle. */
        /* Les plats débordent la dalle de 12 mm vers l'avant : c'est ce qui fait
         * un cadre, et c'est ce qui donne l'ombre portée sur le verre. */
        x.origin = ns_v3_make(bezel[i].cx,
                              bezel[i].cy - bezel[i].h * 0.5f,
                              sz + 0.012f + (bezel[i].cy - sy) * tanf(tilt));
        x.pitch = -tilt;
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);
    }

    /* --- panneau de commande, joystick, boutons --------------------------- */
    /*
     * Le panneau est INCLINÉ, et tout ce qui se pose dessus doit suivre.
     *
     * Le premier jet posait le manche et les boutons à des hauteurs fixes, en
     * ignorant les 9° du panneau. Résultat mesuré : l'embase du manche flottait
     * 1,9 cm au-dessus de la tôle, la rangée de boutons la plus proche
     * dépassait de 1,2 mm — donc invisible — et la rangée du fond flottait de
     * 8 mm. Personne ne l'avait vu parce que jusqu'à ce que les mains se posent
     * dessus, on ne regardait jamais un panneau de commande d'aussi près.
     *
     * `panel_surface(dz)` rend la hauteur de la face supérieure à `dz` du centre
     * du panneau, en tenant compte de l'inclinaison. Une rotation de +θ autour
     * de X envoie (y, z) sur (y cos θ − z sin θ, ...), d'où le signe.
     */
    /*
     * Attention au repère de `geo_box` : elle occupe Y de 0 à `size.y`, pas
     * −size.y/2 à +size.y/2. C'est écrit dans son en-tête, et l'oublier ici a
     * coûté une passe complète — la première version de ce calcul employait la
     * DEMI-hauteur et plaçait donc toute la quincaillerie 2,8 cm trop bas,
     * c'est-à-dire dans la tôle.
     */
    /*
     * Le panneau de commande est maintenant DANS le profil : c'est le gradin
     * entre (0,42 ; 0,93) et (0,34 ; 0,985). Il n'y a donc plus de caisse
     * rapportée à poser dessus — elle ferait double emploi et se verrait comme
     * une marche en trop.
     *
     * `panel_surface(dz)` rend la hauteur de cette face à `dz` du repère du
     * panneau. `PANEL_H` vaut zéro : la surface EST la ligne du profil, il n'y a
     * plus de tôle d'épaisseur à franchir.
     */
    const float PANEL_Y = 0.96f, PANEL_H = 0.0f, PANEL_Z = 0.38f;
    /* La pente du gradin : 0,055 de haut pour 0,08 de profondeur. */
    const float PANEL_PITCH = atanf(0.055f / 0.08f);
    #define panel_surface(dz) \
        (PANEL_Y + (0.42f - (PANEL_Z + (dz))) * (0.055f / 0.08f))

    /* La tôle du panneau : une plaque MINCE posée sur le gradin, pour lui donner
     * sa matière propre (le stratifié coloré des vues de référence) sans
     * rajouter de volume. */
    geo_panel(out,
              ns_v3_make(0.0f, panel_surface(0.0f) + 0.004f, PANEL_Z),
              ns_v3_make(0.0f, cosf(PANEL_PITCH), sinf(PANEL_PITCH)),
              ns_v3_make(1, 0, 0),
              RG_CAB_W - 0.03f, 0.135f, 0.0f, 0.0f, 1.0f, 1.0f, panel);
    (void)PANEL_H;

    /*
     * Le manche : une rondelle, une tige, et une VRAIE BOULE.
     *
     * Généalogie de cette pièce, parce qu'elle a été fausse deux fois. Le
     * premier jet était une boîte de 5 cm chanfreinée : posée sous les mains du
     * joueur, elle se lisait pour ce qu'elle était, un cube. Le second empilait
     * trois troncs de cône à dix côtés — raccord, ventre, calotte — et donnait
     * un ÉCROU : les deux arêtes horizontales entre les tronçons sont vives,
     * elles accrochent chacune un liseré, et l'œil compte trois anneaux au lieu
     * de voir une sphère.
     *
     * C'est l'objet que le joueur a le plus près des yeux pendant toute une
     * partie, et le seul qu'il touche. Il méritait la primitive qui lui manquait
     * plutôt qu'un troisième contournement : `geo_revolve` fait tourner un profil
     * CONTINU, donc les normales se lissent par le seuil d'angle de `geo_mesh`
     * et il n'y a plus une seule arête à accrocher.
     *
     * Le coût est nul, et c'est ce qui rend l'arbitrage facile : douze méridiens
     * sur six parallèles font 132 triangles là où les trois troncs en faisaient
     * une centaine, pour une silhouette ronde à toutes les distances.
     */
    const int stick_mat = material_index(b, m_trim[0] ? m_trim : m_body, owner);
    /* Posé SUR la tôle, rondelle à demi enfoncée : 6 mm sous la surface, ce qui
     * la fait tenir au lieu de flotter. */
    const ns_v3 stick_at = ns_v3_make(-0.20f, panel_surface(0.0f) - 0.006f, PANEL_Z);

    /* La rondelle anti-poussière et la tige restent des cylindres : ce sont
     * vraiment des cylindres. Douze côtés au lieu de dix — une tige de 20 mm
     * regardée à cinquante centimètres montrait ses facettes. */
    struct { float r0, r1, h, dy; bool cap_lo, cap_hi; float roll; } stick[2] = {
        { 0.036f, 0.032f, 0.010f, 0.000f, false, true,  0.0f  },  /* rondelle */
        { 0.010f, 0.009f, 0.062f, 0.008f, false, false, 0.16f },  /* tige     */
    };
    for (int i = 0; i < 2; ++i) {
        geo_mesh_init(&part);
        geo_cylinder(&part, stick[i].r0, stick[i].r1, stick[i].h, 12,
                     stick[i].cap_lo, stick[i].cap_hi, &uv_trim, stick_mat);
        x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(stick_at.x, stick_at.y + stick[i].dy, stick_at.z);
        x.roll = stick[i].roll;   /* la rondelle reste à plat, le manche penche */
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);
    }

    /*
     * La boule : un demi-cercle de rayon 21 mm, du pôle sud au pôle nord, très
     * légèrement aplati en bas (0,86) là où elle coiffe la tige — une boule
     * d'arcade est moulée sur son insert, elle n'est pas parfaitement ronde.
     */
    {
        const float R = 0.021f;
        float ball[7 * 2];
        for (int k = 0; k < 7; ++k) {
            const float t = (float)k / 6.0f;            /* 0 = bas, 1 = haut */
            const float a = -NS_PI * 0.5f + t * NS_PI;
            ball[k * 2]     = cosf(a) * R;
            ball[k * 2 + 1] = sinf(a) * R * (sinf(a) < 0.0f ? 0.86f : 1.0f);
        }
        geo_mesh_init(&part);
        geo_revolve(&part, ball, 7, 12, &uv_trim, stick_mat);
        x = GEO_XFORM_IDENTITY;
        /* Le centre de la boule est à R au-dessus du sommet de la tige, moins le
         * chevauchement qui l'empêche de flotter. */
        x.origin = ns_v3_make(stick_at.x, stick_at.y + 0.070f + R * 0.86f, stick_at.z);
        x.roll = 0.16f;
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);
    }

    /*
     * Le SOMMET DE LA BOULE — un point qu'on touche, exactement comme
     * `panel` désigne le dessus des pastilles et `coin` la fente. Les trois
     * ancres d'une borne ont la même sémantique, et il a fallu qu'elles ne
     * l'aient pas pour qu'on s'en aperçoive : la première version donnait ici le
     * point où se pose le POIGNET, une paume plus haut, et la main gauche
     * refermait ses doigts à trois centimètres au-dessus du manche.
     *
     * C'est la pose qui ajoute la paume, parce que c'est elle qui sait de quelle
     * longueur est une main. La cote sort d'ici pour la même raison que les deux
     * autres : elle est écrite à dix lignes de la géométrie qu'elle désigne.
     *
     * 0,106 = la rondelle 0,010 (dont 0,006 enfoncés) + la tige 0,062 posée à
     * 0,008, soit un sommet de tige à 0,070, plus le diamètre utile de la boule
     * (0,021 x 0,86 sous son centre, 0,021 au-dessus) = 0,070 + 0,018 + 0,021.
     * Le chiffre est écrit ici parce qu'il se relit dix lignes au-dessus.
     */
    anchors->stick[0] = stick_at.x;
    anchors->stick[1] = stick_at.y + 0.109f;
    anchors->stick[2] = stick_at.z;

    /*
     * Les boutons sont VIFS, et pas du matériau du panneau.
     *
     * Ils l'étaient : quatre pastilles bordeaux sur un panneau bordeaux, donc
     * invisibles — et le défaut ne se voyait qu'une fois les mains posées
     * dessus, parce qu'avant ça on ne regardait jamais un panneau de commande de
     * si près. Un bouton d'arcade est en plastique vif et brillant, et c'est
     * exactement sa fonction : se trouver sans être cherché.
     */
    const int btn_mat[2] = { material_index(b, "bouton_rouge", owner),
                             material_index(b, "bouton_jaune", owner) };
    const geo_uv uv_btn = material_uv(b, btn_mat[0]);

    float btn_x = 0.0f, btn_y = 0.0f, btn_z = 0.0f;
    for (int i = 0; i < 4; ++i) {
        /* La pastille dépasse de 5 mm de la tôle, à la hauteur que l'inclinaison
         * lui donne — et non à une hauteur fixe, qui noyait une rangée dans le
         * panneau et faisait flotter l'autre. */
        /*
         * `bz` était `hd + 0,045`, c'est-à-dire calé sur la face avant de
         * l'ANCIEN caisson-boîte. Le caisson est devenu un profil et son
         * panneau de commande est un gradin entre z = 0,34 et z = 0,42 : les
         * boutons se retrouvaient donc 6 cm DEVANT le panneau et 7 cm en
         * dessous, suspendus dans le vide au-dessus de la porte à monnayeur.
         * Ils sont maintenant repérés PAR RAPPORT au panneau, ce qui les tient
         * quoi qu'il arrive au profil.
         */
        const float bz = PANEL_Z - 0.028f + (float)(i / 2) * 0.056f;
        const ns_v3 at = ns_v3_make(0.02f + (float)(i % 2) * 0.075f,
                                    panel_surface(bz - PANEL_Z) - 0.003f,
                                    bz);
        btn_x += at.x * 0.25f; btn_y += at.y * 0.25f; btn_z += at.z * 0.25f;

        /* Rond et bombé : 19 mm de rayon en bas, 16 en haut. Un bouton d'arcade
         * est une pastille convexe, et c'est le bombé qui accroche la lumière
         * du plafonnier — un palet plat reste éteint quel que soit l'éclairage. */
        geo_mesh_init(&part);
        geo_cylinder(&part, 0.019f, 0.016f, 0.014f, 12, false, true, &uv_btn,
                     btn_mat[i % 2]);
        x = GEO_XFORM_IDENTITY;
        x.origin = at;
        x.pitch = PANEL_PITCH;   /* la pastille suit la pente du gradin */
        geo_mesh_append(out, &part, &x, -1);
        geo_mesh_free(&part);
    }

    /* Le doigt touche le **dessus** des boutons, pas leur centre : demi-hauteur
     * de la boîte (8 mm) plus l'épaisseur d'une pulpe (5 mm). Sans ça la main
     * s'enfonce dans le panneau — le genre de détail qui ne se voit qu'une fois
     * les bras à l'écran, et qui coûte alors une heure à retrouver. */
    anchors->panel[0] = btn_x;
    anchors->panel[1] = btn_y + 0.014f;   /* hauteur de pastille, moins l'enfoncement */
    anchors->panel[2] = btn_z;

    /* --- la fente à jetons, et une boîte qui flottait -----------------------
     *
     * Il y avait ICI un second monnayeur : une boîte de 20 x 24 cm plaquée en
     * `z = hd − 5 mm`, c'est-à-dire sur la face avant du caisson **du temps où
     * le caisson était une boîte**. Depuis que c'est un profil en gradins, la
     * face avant à cette hauteur est retombée à z = 0,30 : la boîte est restée
     * où elle était et s'est mise à FLOTTER 13 cm devant la borne, en gris
     * clair (`borne_cadre`), au beau milieu de la face.
     *
     * C'était visible sur chacune des dix-neuf bornes, et c'est très
     * probablement le premier morceau de ce que tu voyais en disant « les
     * bornes ne ressemblent pas du tout » — une machine à affranchir vissée sur
     * une borne d'arcade. Aucune capture ne l'avait montré parce que toutes
     * étaient prises de trois quarts ou depuis l'allée, jamais de face à
     * hauteur de hanche.
     *
     * Elle disparaît, et l'ancre part sur la VRAIE fente : la plaque chromée de
     * gauche de la porte à monnayeur, celle qui existe déjà à 15 cm au-dessus.
     * Une borne a un monnayeur, pas deux.
     */
    anchors->coin[0] = -0.048f;
    anchors->coin[1] = 0.640f;
    anchors->coin[2] = door_z + 0.030f;

    (void)hw;
}

static void parse_cabinets(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "cabinets");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        /* 64 octets, la taille de `ns_cabinet.name` : un nom tronqué ici ne
         * correspondrait plus à celui du maillage glTF, et le moteur ne pourrait
         * plus relier la borne à sa géométrie — en silence. */
        char name[64];
        tool_json_get_string(doc, e, "name", name, sizeof name);
        if (!name[0]) tool_fatalf("une borne sans nom (entrée %d)", i);
        if (b->cabinet_count >= RG_MAX_CABINETS) {
            tool_fatalf("plus de %d bornes : le moteur en tient %d (NS_MAX_CABINETS)",
                        RG_MAX_CABINETS, RG_MAX_CABINETS);
        }

        float at[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        const float yaw_deg = tool_json_get_float(doc, e, "yaw", 0.0f);
        const float yaw = yaw_deg * NS_DEG2RAD;

        rg_cab_anchors anchors; memset(&anchors, 0, sizeof anchors);
        geo_mesh local; geo_mesh_init(&local);
        build_cabinet(b, &local, doc, e, name, &anchors);

        geo_xform x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(at[0], at[1], at[2]);
        x.yaw = yaw;

        geo_mesh placed; geo_mesh_init(&placed);
        geo_mesh_append(&placed, &local, &x, -1);
        geo_mesh_free(&local);
        emit_object(b, name, &placed, RG_SMOOTH_HARD);
        record_solid(b, name, RG_SOLID_CABINET);

        /* Report du repère local vers le monde. Le lacet suit la convention de
         * `geo_xform` : X' = X cos + Z sin, Z' = -X sin + Z cos. */
        const float c = cosf(yaw), s = sinf(yaw);
        rg_cabinet *cab = &b->cabinets[b->cabinet_count];
        memset(cab, 0, sizeof *cab);
        snprintf(cab->name, sizeof cab->name, "%s", name);
        tool_json_get_string(doc, e, "game", cab->game, sizeof cab->game);
        tool_json_get_string(doc, e, "difficulty", cab->difficulty, sizeof cab->difficulty);
        if (!cab->game[0]) {
            tool_fatalf("la borne « %s » ne dit pas à quoi elle joue — c'est "
                        "précisément ce que le moteur devinait de travers", name);
        }
        cab->slot = (int)tool_json_get_float(doc, e, "slot", (float)b->cabinet_count);
        cab->attract = tool_json_get_bool(doc, e, "attract", true);

        cab->bounds_min[0] = b->last_bounds.min.x;
        cab->bounds_min[1] = b->last_bounds.min.y;
        cab->bounds_min[2] = b->last_bounds.min.z;
        cab->bounds_max[0] = b->last_bounds.max.x;
        cab->bounds_max[1] = b->last_bounds.max.y;
        cab->bounds_max[2] = b->last_bounds.max.z;

#define RG_TO_WORLD(dst, src)                                              \
        do {                                                               \
            (dst)[0] = at[0] + (src)[0] * c + (src)[2] * s;                \
            (dst)[1] = at[1] + (src)[1];                                   \
            (dst)[2] = at[2] - (src)[0] * s + (src)[2] * c;                \
        } while (0)

        RG_TO_WORLD(cab->screen_center, anchors.screen);
        RG_TO_WORLD(cab->panel_centre,  anchors.panel);
        RG_TO_WORLD(cab->coin_slot,     anchors.coin);
        RG_TO_WORLD(cab->stick_top,     anchors.stick);
#undef RG_TO_WORLD

        /*
         * LA NORMALE EXPORTÉE DOIT ÊTRE CELLE DE LA GÉOMÉTRIE.
         *
         * Elle était forcée à l'horizontale, alors que la dalle bascule de 10°
         * vers l'arrière depuis B14. La donnée annonçait donc un écran vertical
         * que le maillage n'a jamais eu — et tout ce qui s'appuie dessus (viser
         * la dalle, orienter un reflet, placer un joueur) travaillait sur un
         * plan qui n'existe pas.
         */
        const float ct = cosf(anchors.screen_tilt), st = sinf(anchors.screen_tilt);
        cab->screen_normal[0] = s * ct;
        cab->screen_normal[1] = st;
        cab->screen_normal[2] = c * ct;
        cab->screen_material = anchors.screen_material;
        cab->screen_size[0] = anchors.screen_size[0];
        cab->screen_size[1] = anchors.screen_size[1];

        /*
         * Où se plante le joueur : 46 cm devant le PANNEAU DE COMMANDE.
         *
         * L'ancre était dérivée de l'ÉCRAN, ce qui marchait tant que le caisson
         * était une boîte et que l'écran et le panneau étaient à la même
         * profondeur. Avec le profil en gradins, l'écran recule de 32 cm quand
         * le panneau n'en recule que 12 : dériver du premier plantait le joueur
         * beaucoup trop près, le nez dans la tôle.
         *
         * Le panneau est de toute façon la bonne référence : c'est ce qu'on
         * doit ATTEINDRE, et c'est lui qui décide de la distance à laquelle on
         * se tient. L'écran, lui, se regarde d'où l'on est.
         */
        /* Le joueur se plante à l'HORIZONTALE : on prend la direction du meuble
         * (s, c) et pas la normale de la dalle, qui pointe maintenant un peu
         * vers le haut et le reculerait de quelques centimètres. */
        cab->player_anchor[0] = cab->panel_centre[0] + s * 0.46f;
        cab->player_anchor[1] = at[1];
        cab->player_anchor[2] = cab->panel_centre[2] + c * 0.46f;

        b->cabinet_count++;
    }
}

/* ========================================================================== */
/* Objets composés : mobilier, agencements, décor                             */
/* ========================================================================== */

/*
 * Un « prop » est un objet nommé fait de plusieurs morceaux, placé et pivoté
 * d'un bloc. Billard, comptoir, bureau, canapés, cabines de toilettes,
 * distributeur, jukebox, affiches, enseignes : tout cela est une composition de
 * boîtes et de panneaux, et n'a pas besoin d'un générateur dédié par meuble.
 *
 * Le compromis est assumé : un générateur par meuble donnerait de plus belles
 * formes, mais vingt générateurs paramétrés une seule fois chacun sont vingt
 * fois plus de code que de résultat. Les formes qui méritent vraiment un
 * générateur — la borne d'arcade, le plafond — en ont un.
 *
 * Les coordonnées des morceaux sont LOCALES au prop : on décrit le meuble à
 * l'origine, une fois, puis on le pose. C'est ce qui rend une description
 * relisable, et ce qui permet de déplacer un meuble sans recalculer dix lignes.
 */
static void emit_prop_parts(rg_builder *b, const tool_json *doc, const tool_json_value *e,
                            geo_mesh *out, const char *owner)
{
    /*
     * Un objet est SOIT un empilement de morceaux paramétriques, SOIT un modèle
     * importé — jamais les deux.
     *
     * Les deux voies partagent tout le reste : le nommage des instances, la
     * répétition, le point d'intérêt déclaré, l'enregistrement du solide et le
     * contrôle de chevauchement. C'est ce qui justifie de brancher ici plutôt
     * que d'ajouter une liste « models » à côté : le mobilier importé est du
     * mobilier, il n'a aucune raison d'avoir sa propre plomberie.
     */
    char model_src[192];
    tool_json_get_string(doc, e, "model", model_src, sizeof model_src);
    if (model_src[0]) {
        char mat_name[64];
        tool_json_get_string(doc, e, "material", mat_name, sizeof mat_name);
        if (!mat_name[0]) {
            tool_fatalf("« %s » : un modèle importé doit déclarer son \"material\" — "
                        "le glTF apporte sa géométrie et ses UV, la salle décide "
                        "de la matière", owner);
        }
        const int mat = material_index(b, mat_name, owner);

        /*
         * La correspondance de matériaux, primitive par primitive. Un poste de
         * radio a un corps et des haut-parleurs, un extincteur un corps, un verre
         * et une étiquette : sans cette table, tout l'objet prendrait la même
         * matière et l'on perdrait précisément ce qui distingue un vrai modèle
         * d'une boîte peinte.
         */
        int32_t by_index[8];
        size_t  by_index_count = 0;
        const tool_json_value *mats = tool_json_get(doc, e, "materials");
        const int mats_count = tool_json_array_count(doc, mats);
        if (mats_count > (int)(sizeof by_index / sizeof by_index[0])) {
            tool_fatalf("« %s » : %d matériaux déclarés, %zu au maximum",
                        owner, mats_count, sizeof by_index / sizeof by_index[0]);
        }
        for (int i = 0; i < mats_count; ++i) {
            char n[64];
            tool_json_string_at(doc, mats, i, n, sizeof n);
            by_index[by_index_count++] = n[0] ? material_index(b, n, owner) : -1;
        }

        if (!b->asset_root[0]) {
            tool_fatalf("« %s » : un modèle est demandé mais --assets= n'a pas été "
                        "donné — le chemin « %s » ne peut pas être résolu",
                        owner, model_src);
        }
        char path[768];
        snprintf(path, sizeof path, "%s/%s", b->asset_root, model_src);

        geo_xform mx = GEO_XFORM_IDENTITY;
        mx.scale = tool_json_get_float(doc, e, "scale", 1.0f);
        if (mx.scale <= 0.0f) {
            tool_fatalf("« %s » : échelle %.4f — nulle ou négative", owner, (double)mx.scale);
        }
        /* Le modèle peut avoir besoin de son propre calage vertical : les
         * modèles de Poly Haven posent leur origine au sol, mais un extincteur
         * s'accroche au mur. */
        float lift[3];
        tool_json_get_vec3(doc, e, "modelOffset", lift, 0.0f);
        mx.origin = ns_v3_make(lift[0], lift[1], lift[2]);
        mx.pitch = tool_json_get_float(doc, e, "modelPitch", 0.0f) * NS_DEG2RAD;
        mx.roll  = tool_json_get_float(doc, e, "modelRoll", 0.0f) * NS_DEG2RAD;

        const size_t tri = geo_import_gltf(out, path, &mx, mat,
                                           by_index_count ? by_index : NULL,
                                           by_index_count, owner);
        b->imported_triangles += tri;
        return;
    }

    const tool_json_value *parts = tool_json_get(doc, e, "parts");
    const int count = tool_json_array_count(doc, parts);
    if (count <= 0) {
        tool_fatalf("« %s » n'a ni morceaux (\"parts\") ni modèle (\"model\")", owner);
    }

    for (int i = 0; i < count; ++i) {
        const tool_json_value *p = tool_json_at(doc, parts, i);

        char mat_name[64];
        tool_json_get_string(doc, p, "material", mat_name, sizeof mat_name);
        const int mat = material_index(b, mat_name, owner);

        char type[16];
        tool_json_get_string(doc, p, "type", type, sizeof type);
        if (!type[0]) snprintf(type, sizeof type, "box");

        float at[3];
        tool_json_get_vec3(doc, p, "at", at, 0.0f);
        const float yaw   = tool_json_get_float(doc, p, "yaw", 0.0f) * NS_DEG2RAD;
        const float pitch = tool_json_get_float(doc, p, "pitch", 0.0f) * NS_DEG2RAD;
        const float roll  = tool_json_get_float(doc, p, "roll", 0.0f) * NS_DEG2RAD;

        geo_uv uv = material_uv(b, mat);
        const float over = tool_json_get_float(doc, p, "uvMetres", 0.0f);
        if (over > 0.0f) uv = geo_uv_tile(over);

        geo_mesh piece; geo_mesh_init(&piece);

        if (strcmp(type, "box") == 0) {
            float size[3];
            if (tool_json_get_floats(doc, p, "size", size, 3, 0.0f) != 3) {
                tool_fatalf("« %s », morceau %d : pas de taille [x, y, z]", owner, i);
            }
            uint32_t faces = GEO_FACE_ALL;
            char mask[16];
            tool_json_get_string(doc, p, "faces", mask, sizeof mask);
            if (strcmp(mask, "sides") == 0)         faces = GEO_FACE_SIDES;
            else if (strcmp(mask, "noBottom") == 0) faces = GEO_FACE_NO_BOTTOM;
            geo_box(&piece, ns_v3_make(size[0], size[1], size[2]),
                    tool_json_get_float(doc, p, "chamfer", 0.008f), faces, &uv, mat);
        } else if (strcmp(type, "panel") == 0) {
            float size[2];
            if (tool_json_get_floats(doc, p, "size", size, 2, 0.0f) != 2) {
                tool_fatalf("« %s », morceau %d : pas de taille [largeur, hauteur]", owner, i);
            }
            /* Le panneau est décrit dans son plan local (normale +Z, droite +X)
             * puis orienté par la transformation, comme tout le reste. Son UV est
             * explicite : une affiche n'a pas droit à la répétition.
             *
             * `tool_json_get_floats` garnit TOUTES les composantes du repli avant
             * de chercher la clé — c'est ce qui permet de distinguer « absent » de
             * « présent mais court ». Écrire directement dans `rect` écrasait donc
             * le rectangle par défaut (0,0,1,1) par (0,0,0,0) dès que la clé
             * manquait, et les huit affiches échantillonnaient un unique texel :
             * elles sortaient en aplats pâles, ce que la cible d'albédo a montré
             * du premier coup. */
            float rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            float given[4];
            if (tool_json_get_floats(doc, p, "uvRect", given, 4, 0.0f) == 4) {
                memcpy(rect, given, sizeof rect);
            }
            geo_panel(&piece, ns_v3_zero(), ns_v3_make(0, 0, 1), ns_v3_make(1, 0, 0),
                      size[0], size[1], rect[0], rect[1], rect[2], rect[3], mat);
        } else if (strcmp(type, "cylinder") == 0) {
            /*
             * Le cylindre et le cône, enfin disponibles pour un prop.
             *
             * Ils manquaient, et ça se voyait à un endroit précis : la
             * suspension du billard. La salle déclarait sa LUMIÈRE — chaude,
             * basse, avec un commentaire expliquant qu'« une table de billard a
             * sa propre lampe » — et aucun LUMINAIRE. Une source sans objet
             * visible est exactement ce que le plan s'interdit : c'est ce qui
             * fait qu'une pièce paraît éclairée par magie. On ne pouvait pas
             * l'ajouter parce qu'un prop ne savait faire que des boîtes.
             */
            const float r0 = tool_json_get_float(doc, p, "radius", 0.0f);
            const float r1 = tool_json_get_float(doc, p, "radiusTop", r0);
            const float hh = tool_json_get_float(doc, p, "height", 0.0f);
            if (r0 <= 0.0f || hh <= 0.0f) {
                tool_fatalf("« %s », morceau %d : cylindre sans « radius » ou « height »",
                            owner, i);
            }
            const int sides = (int)tool_json_get_float(doc, p, "sides", 16.0f);
            geo_cylinder(&piece, r0, r1, hh, sides,
                         tool_json_get_bool(doc, p, "capBottom", true),
                         tool_json_get_bool(doc, p, "capTop", true), &uv, mat);
        } else if (strcmp(type, "sphere") == 0) {
            /* Une sphère par révolution : les billes, les ampoules, les boules.
             * Le profil est un demi-cercle, donc la primitive suffit et il n'y a
             * pas de générateur de sphère à écrire. */
            const float r0 = tool_json_get_float(doc, p, "radius", 0.0f);
            if (r0 <= 0.0f) {
                tool_fatalf("« %s », morceau %d : sphère sans « radius »", owner, i);
            }
            const int sides = (int)tool_json_get_float(doc, p, "sides", 12.0f);
            const int rings = (int)tool_json_get_float(doc, p, "rings", 7.0f);
            if (rings < 3 || rings > 33) {
                tool_fatalf("« %s », morceau %d : « rings » = %d, attendu 3..33",
                            owner, i, rings);
            }
            float prof[33 * 2];
            for (int k = 0; k < rings; ++k) {
                const float t = (float)k / (float)(rings - 1);
                const float a = -NS_PI * 0.5f + t * NS_PI;
                prof[k * 2]     = cosf(a) * r0;
                prof[k * 2 + 1] = sinf(a) * r0 + r0;   /* posée sur Y = 0 */
            }
            geo_revolve(&piece, prof, rings, sides, &uv, mat);
        } else {
            tool_fatalf("« %s », morceau %d : type « %s » inconnu "
                        "(box, panel, cylinder, sphere)", owner, i, type);
        }

        geo_xform x = GEO_XFORM_IDENTITY;
        x.origin = ns_v3_make(at[0], at[1], at[2]);
        x.yaw = yaw; x.pitch = pitch; x.roll = roll;
        geo_mesh_append(out, &piece, &x, -1);
        geo_mesh_free(&piece);
    }
}

static void parse_props(rg_builder *b, const tool_json *doc, const tool_json_value *root)
{
    const tool_json_value *list = tool_json_get(doc, root, "props");
    const int count = tool_json_array_count(doc, list);

    for (int i = 0; i < count; ++i) {
        const tool_json_value *e = tool_json_at(doc, list, i);

        /* 60 octets : `instance_name` peut y ajouter un suffixe « _12 », et le
         * tout doit tenir dans les 64 de `ns_poi.name` sans être tronqué. */
        char base_name[56];
        tool_json_get_string(doc, e, "name", base_name, sizeof base_name);
        if (!base_name[0]) tool_fatalf("un objet sans nom (entrée %d de \"props\")", i);

        float at[3];
        tool_json_get_vec3(doc, e, "at", at, 0.0f);
        const float yaw = tool_json_get_float(doc, e, "yaw", 0.0f);

        const rg_repeat rep = read_repeat(doc, e);
        for (int k = 0; k < rep.count; ++k) {
            geo_mesh local; geo_mesh_init(&local);
            emit_prop_parts(b, doc, e, &local, base_name);

            geo_xform x = GEO_XFORM_IDENTITY;
            x.origin = ns_v3_make(at[0] + rep.step.x * (float)k,
                                  at[1] + rep.step.y * (float)k,
                                  at[2] + rep.step.z * (float)k);
            x.yaw = (yaw + rep.yaw_step * (float)k) * NS_DEG2RAD;

            geo_mesh placed; geo_mesh_init(&placed);
            geo_mesh_append(&placed, &local, &x, -1);
            geo_mesh_free(&local);

            /* 64 octets, comme `ns_poi.name` : un nom tronqué ici désignerait un
             * lieu que le moteur ne saurait plus rattacher à sa géométrie. */
            char name[64];
            instance_name(name, sizeof name, base_name, k, rep.count);
            emit_object(b, name, &placed, RG_SMOOTH_HARD);
            record_solid(b, name, RG_SOLID_PROP);

            /* Un point d'intérêt déclaré : le moteur cessera de repérer le
             * billard et le canapé en cherchant des sous-chaînes dans les noms de
             * nœuds du glTF. */
            char poi_kind[24];
            tool_json_get_string(doc, e, "poi", poi_kind, sizeof poi_kind);
            if (poi_kind[0]) {
                if (b->poi_count >= RG_MAX_POIS) {
                    tool_fatalf("plus de %d points d'intérêt (NS_MAX_POI)", RG_MAX_POIS);
                }
                rg_poi *poi = &b->pois[b->poi_count++];
                memset(poi, 0, sizeof *poi);
                snprintf(poi->name, sizeof poi->name, "%s", name);
                snprintf(poi->kind, sizeof poi->kind, "%s", poi_kind);
                float anchor[3];
                tool_json_get_vec3(doc, e, "anchor", anchor, 0.0f);
                /* L'ancre est locale au meuble, comme ses morceaux. */
                const float c = cosf(x.yaw), s = sinf(x.yaw);
                poi->anchor[0] = x.origin.x + anchor[0] * c + anchor[2] * s;
                poi->anchor[1] = x.origin.y + anchor[1];
                poi->anchor[2] = x.origin.z - anchor[0] * s + anchor[2] * c;
                poi->bounds = b->last_bounds;
            }
        }
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

        /* La liste EXHAUSTIVE de ce qu'une lumière peut déclarer. Elle existe
         * parce que six des seize lumières de cette salle écrivaient « colour »
         * pour « color » : elles s'affichaient en blanc pur, et rien ne le
         * disait. Une clé mal orthographiée casse maintenant le build. */
        static const char *const light_keys[] = {
            "name", "at", "color", "intensity", "range", "radius",
            "flicker", "ceilingPanel", "panelSize", "repeat", NULL
        };
        char what[128];
        snprintf(what, sizeof what, "lumière « %s »", base_name);
        tool_json_reject_unknown_keys(doc, e, light_keys, what);

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

        /*
         * Un point de vue posé dans un meuble rend un cadre noir, et rien ne le
         * dit : ni le build, ni le journal d'exécution. C'est arrivé deux fois,
         * aux mêmes captures, quand A4 a meublé la salle sous des points de vue
         * écrits pour la salle vide. L'outil connaît les deux : il refuse.
         *
         * La marge correspond au rayon du corps du joueur (32 cm) : une caméra
         * qui frôle une borne de trois centimètres a déjà la face avant du
         * meuble en plein cadre.
         *
         * Les orbites sont exclues : leur `position` est un CENTRE de rotation,
         * pas un point où la caméra se tient. Celle de la salle tourne
         * précisément autour de la borne centrale.
         */
        if (!orbit) {
            const float margin = 0.32f;
            for (size_t k = 0; k < b->solid_count; ++k) {
                const ns_aabb *bb = &b->solids[k].bounds;
                if (p[0] > bb->min.x - margin && p[0] < bb->max.x + margin
                 && p[1] > bb->min.y - margin && p[1] < bb->max.y + margin
                 && p[2] > bb->min.z - margin && p[2] < bb->max.z + margin) {
                    tool_fatalf("le point de vue « %s » est posé dans « %s » "
                                "(%.2f, %.2f, %.2f dans [%.2f %.2f %.2f]-[%.2f %.2f %.2f]) — "
                                "il rendrait un cadre noir",
                                name, b->solids[k].name,
                                (double)p[0], (double)p[1], (double)p[2],
                                (double)bb->min.x, (double)bb->min.y, (double)bb->min.z,
                                (double)bb->max.x, (double)bb->max.y, (double)bb->max.z);
                }
            }
        }

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
    fprintf(f, "  ],\n");

    /*
     * Les bornes, DÉCLARÉES. C'est le bloc qui supprime trois heuristiques d'un
     * coup : les fractions inventées de la boîte englobante pour situer l'écran,
     * le barycentre du troupeau pour deviner vers où une borne regarde, et le tri
     * en X puis Z pour affecter les jeux. Aucune ne pouvait être juste, parce
     * qu'aucune n'avait l'information.
     */
    fprintf(f, "  \"cabinets\": [\n");
    for (size_t i = 0; i < b->cabinet_count; ++i) {
        const rg_cabinet *c = &b->cabinets[i];
        fprintf(f, "    { \"name\": \"%s\", \"slot\": %d, \"game\": \"%s\", "
                   "\"difficulty\": \"%s\", \"attract\": %s,\n",
                c->name, c->slot, c->game, c->difficulty[0] ? c->difficulty : "normal",
                c->attract ? "true" : "false");
        fprintf(f, "      \"bboxMin\": [%.4f, %.4f, %.4f], "
                   "\"bboxMax\": [%.4f, %.4f, %.4f],\n",
                (double)c->bounds_min[0], (double)c->bounds_min[1], (double)c->bounds_min[2],
                (double)c->bounds_max[0], (double)c->bounds_max[1], (double)c->bounds_max[2]);
        fprintf(f, "      \"screenCenter\": [%.4f, %.4f, %.4f], "
                   "\"screenNormal\": [%.4f, %.4f, %.4f], "
                   "\"screenWidth\": %.4f, \"screenHeight\": %.4f,\n",
                (double)c->screen_center[0], (double)c->screen_center[1],
                (double)c->screen_center[2],
                (double)c->screen_normal[0], (double)c->screen_normal[1],
                (double)c->screen_normal[2],
                (double)c->screen_size[0], (double)c->screen_size[1]);
        fprintf(f, "      \"playerAnchor\": [%.4f, %.4f, %.4f],\n",
                (double)c->player_anchor[0], (double)c->player_anchor[1],
                (double)c->player_anchor[2]);
        /* Les deux points que la main vise. Ils sortent d'ici pour la même raison
         * que le centre d'écran : ils sont écrits à trois lignes des boîtes
         * qu'ils désignent, et une seconde copie dériverait. */
        /* L'index du matériau de la dalle. C'est ce qui permet d'y faire tourner
         * un jeu : le moteur remplace la texture de CE matériau-là, sans avoir à
         * deviner lequel des lots d'une borne est son écran. */
        fprintf(f, "      \"screenMaterial\": %d,\n", c->screen_material);
        fprintf(f, "      \"panelCentre\": [%.4f, %.4f, %.4f],\n"
                   "      \"coinSlot\": [%.4f, %.4f, %.4f],\n"
                   "      \"stickTop\": [%.4f, %.4f, %.4f] }%s\n",
                (double)c->panel_centre[0], (double)c->panel_centre[1],
                (double)c->panel_centre[2],
                (double)c->coin_slot[0], (double)c->coin_slot[1],
                (double)c->coin_slot[2],
                (double)c->stick_top[0], (double)c->stick_top[1],
                (double)c->stick_top[2],
                (i + 1 < b->cabinet_count) ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* Les lieux du décor, déclarés eux aussi. Le moteur les repérait en cherchant
     * des sous-chaînes dans les noms de nœuds du glTF — une seconde analyse du
     * fichier, dont le champ `bounds` n'était d'ailleurs jamais rempli. */
    /*
     * La classe de pas de chaque matériau, dans l'ORDRE DES MATÉRIAUX du glTF.
     *
     * C'est l'ordre qui fait tout le travail : `ns_bvh_move_capsule` renvoie
     * l'index de matériau du triangle sous les pieds, et cet index désigne la
     * même entrée ici. Écrire un nom de matériau à la place obligerait le moteur
     * à faire une recherche par chaîne à chaque pas, pour retrouver un index
     * qu'il avait déjà.
     */
    /*
     * Les zones de poussière, recopiées telles quelles depuis la description.
     *
     * `roomgen` ne les transforme pas — il les VALIDE et les transmet. La
     * validation compte : une boîte vide ou inversée donnerait zéro grain sans
     * le moindre message, et on chercherait du côté du rendu.
     */
    {
        const tool_json_value *dust = tool_json_get(doc, root, "dust");
        const int n = tool_json_array_count(doc, dust);
        fprintf(f, "  \"dust\": [\n");
        for (int i = 0; i < n; ++i) {
            const tool_json_value *e = tool_json_at(doc, dust, i);
            char name[64];
            tool_json_get_string(doc, e, "name", name, sizeof name);

            float mn[3], mx[3], col[3];
            tool_json_get_vec3(doc, e, "min", mn, 0.0f);
            tool_json_get_vec3(doc, e, "max", mx, 0.0f);
            tool_json_get_vec3(doc, e, "color", col, 1.0f);
            for (int k = 0; k < 3; ++k) {
                if (mx[k] <= mn[k]) {
                    tool_fatalf("zone de poussière « %s » : la boîte est vide ou inversée sur "
                                "l'axe %d (%.3f à %.3f). Elle ne produirait aucun grain, en "
                                "silence.", name[0] ? name : "?", k,
                                (double)mn[k], (double)mx[k]);
                }
            }
            float drift[3];
            tool_json_get_vec3(doc, e, "drift", drift, 0.0f);

            fprintf(f, "    { \"name\": \"%s\", \"min\": [%.3f, %.3f, %.3f], "
                       "\"max\": [%.3f, %.3f, %.3f], \"density\": %.3f, "
                       "\"drift\": [%.4f, %.4f, %.4f], \"size\": %.4f, "
                       "\"color\": [%.3f, %.3f, %.3f], \"brightness\": %.3f }%s\n",
                    name, (double)mn[0], (double)mn[1], (double)mn[2],
                    (double)mx[0], (double)mx[1], (double)mx[2],
                    (double)tool_json_get_float(doc, e, "density", 0.5f),
                    (double)drift[0], (double)drift[1], (double)drift[2],
                    (double)tool_json_get_float(doc, e, "size", 0.02f),
                    (double)col[0], (double)col[1], (double)col[2],
                    (double)tool_json_get_float(doc, e, "brightness", 0.5f),
                    (i + 1 < n) ? "," : "");
        }
        fprintf(f, "  ],\n");
        if (n) printf("  %d zone(s) de poussière\n", n);
    }

    /*
     * Les zones de réverbération. Même forme que la poussière, et pour la même
     * raison : une boîte nommée, déclarée par la salle, que le moteur applique
     * sans rien deviner. Ce qu'elles portent est ce qu'on ENTEND quand on est
     * dedans — un carrelage rend, une moquette avale.
     */
    {
        const tool_json_value *zones = tool_json_get(doc, root, "soundZones");
        const int n = tool_json_array_count(doc, zones);
        fprintf(f, "  \"soundZones\": [\n");
        for (int i = 0; i < n; ++i) {
            const tool_json_value *e = tool_json_at(doc, zones, i);
            char name[64];
            tool_json_get_string(doc, e, "name", name, sizeof name);

            float mn[3], mx[3];
            tool_json_get_vec3(doc, e, "min", mn, 0.0f);
            tool_json_get_vec3(doc, e, "max", mx, 0.0f);
            for (int k = 0; k < 3; ++k) {
                if (mx[k] <= mn[k]) {
                    tool_fatalf("zone sonore « %s » : la boîte est vide ou inversée sur "
                                "l'axe %d (%.3f à %.3f). On n'y entrerait jamais, en "
                                "silence.", name[0] ? name : "?", k,
                                (double)mn[k], (double)mx[k]);
                }
            }
            const float wet = tool_json_get_float(doc, e, "wet", 0.0f);
            const float decay = tool_json_get_float(doc, e, "decay", 0.0f);
            if (wet < 0.0f || wet > 0.9f || decay < 0.0f || decay > 0.85f) {
                tool_fatalf("zone sonore « %s » : wet %.2f et decay %.2f doivent tenir "
                            "dans [0 ; 0,9] et [0 ; 0,85] — au-delà, la contre-réaction "
                            "s'emballe et la queue ne s'éteint plus",
                            name[0] ? name : "?", (double)wet, (double)decay);
            }
            fprintf(f, "    { \"name\": \"%s\", \"min\": [%.3f, %.3f, %.3f], "
                       "\"max\": [%.3f, %.3f, %.3f], \"wet\": %.3f, \"decay\": %.3f }%s\n",
                    name, (double)mn[0], (double)mn[1], (double)mn[2],
                    (double)mx[0], (double)mx[1], (double)mx[2],
                    (double)wet, (double)decay, (i + 1 < n) ? "," : "");
        }
        fprintf(f, "  ],\n");
        if (n) printf("  %d zone(s) sonore(s)\n", n);
    }

    fprintf(f, "  \"materialFootsteps\": [\n");
    for (size_t i = 0; i < b->material_count; ++i) {
        fprintf(f, "    \"%s\"%s\n",
                b->material_footstep[i][0] ? b->material_footstep[i] : "",
                (i + 1 < b->material_count) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"pois\": [\n");
    for (size_t i = 0; i < b->poi_count; ++i) {
        const rg_poi *p = &b->pois[i];
        fprintf(f, "    { \"name\": \"%s\", \"kind\": \"%s\", "
                   "\"boundsMin\": [%.3f, %.3f, %.3f], "
                   "\"boundsMax\": [%.3f, %.3f, %.3f], "
                   "\"anchor\": [%.3f, %.3f, %.3f] }%s\n",
                p->name, p->kind,
                (double)p->bounds.min.x, (double)p->bounds.min.y, (double)p->bounds.min.z,
                (double)p->bounds.max.x, (double)p->bounds.max.y, (double)p->bounds.max.z,
                (double)p->anchor[0], (double)p->anchor[1], (double)p->anchor[2],
                (i + 1 < b->poi_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

/* ========================================================================== */
/* Programme                                                                  */
/* ========================================================================== */

static void usage(void)
{
    fprintf(stderr,
        "usage : roomgen <salle.room.json> <sortie.gltf> [--textures=REP]\n"
        "                [--expect-textures=N] [--assets=REP]\n"
        "\n"
        "  --textures=REP        vérifie que chaque texture nommée existe dans REP.\n"
        "                        Sans cette option, une faute de frappe se découvre\n"
        "                        à l'exécution, en substitut procédural.\n"
        "  --expect-textures=N   exige que la salle en référence exactement N.\n"
        "                        CMake y met le nombre de fichiers réellement\n"
        "                        présents : une texture d'origine qui cesserait\n"
        "                        d'être employée casse alors le build au lieu de\n"
        "                        disparaître du décor sans un mot.\n"
        "  --assets=REP          racine à laquelle sont résolus les chemins de\n"
        "                        modèles (\"model\": \"cc0/models/...\"). Donnée\n"
        "                        plutôt que déduite : le fichier de salle est une\n"
        "                        source de build, rien ne le range à côté de ses\n"
        "                        assets.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL;
    const char *texture_dirs[RG_MAX_TEXTURE_DIRS];
    size_t texture_dir_count = 0;
    int expect_textures = -1;
    const char *asset_root = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--textures=", 11) == 0) {
            if (texture_dir_count >= RG_MAX_TEXTURE_DIRS) {
                tool_fatalf("plus de %d répertoires de textures", RG_MAX_TEXTURE_DIRS);
            }
            texture_dirs[texture_dir_count++] = argv[i] + 11;
        }
        else if (strncmp(argv[i], "--expect-textures=", 18) == 0) {
            expect_textures = atoi(argv[i] + 18);
        }
        else if (strncmp(argv[i], "--assets=", 9) == 0) {
            asset_root = argv[i] + 9;
        }
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
    for (size_t d = 0; d < texture_dir_count; ++d) b.texture_dirs[d] = texture_dirs[d];
    b.texture_dir_count = texture_dir_count;
    b.expect_textures = expect_textures;
    if (asset_root) snprintf(b.asset_root, sizeof b.asset_root, "%s", asset_root);

    const tool_json_value *room = tool_json_get(&doc, root, "room");
    if (!room) tool_fatalf("%s : pas de bloc \"room\"", in_path);
    const float height = tool_json_get_float(&doc, room, "height", 3.10f);
    const float thickness = tool_json_get_float(&doc, room, "wallThickness", 0.20f);

    /*
     * Les images volontairement inemployées. Chacune porte son motif, et le motif
     * est lu par un humain, pas par l'outil : ce qui compte est qu'il soit écrit.
     * L'outil vérifie seulement que le fichier EXISTE — retirer une image qui
     * n'est plus là ne prouve rien.
     */
    {
        const tool_json_value *retired = tool_json_get(&doc, root, "retiredTextures");
        const int n = tool_json_array_count(&doc, retired);
        for (int i = 0; i < n; ++i) {
            const tool_json_value *e = tool_json_at(&doc, retired, i);
            char file[128];
            tool_json_get_string(&doc, e, "file", file, sizeof file);
            if (!file[0]) tool_fatalf("`retiredTextures[%d]` n'a pas de clé « file »", i);

            char why[8];
            tool_json_get_string(&doc, e, "why", why, sizeof why);
            if (!why[0]) {
                tool_fatalf("« %s » est déclarée retirée sans motif. Le motif est le seul "
                            "intérêt de cette liste : sans lui, elle ne fait que masquer "
                            "le contrôle qu'elle est censée rendre plus fin.", file);
            }

            bool exists = false;
            for (size_t d = 0; d < b.texture_dir_count && !exists; ++d) {
                char path[512];
                snprintf(path, sizeof path, "%s/%s", b.texture_dirs[d], file);
                FILE *f = fopen(path, "rb");
                if (f) { fclose(f); exists = true; }
            }
            if (!exists) {
                tool_fatalf("« %s » est déclarée retirée, mais aucun dossier d'art ne la "
                            "contient : la ligne ne retire rien et fausse le compte", file);
            }
            b.retired_count++;
        }
    }

    parse_materials(&b, &doc, root);
    /* Les lumières d'abord : le plafond s'en sert pour savoir où poser ses
     * panneaux lumineux, ce qui évite d'avoir deux listes à tenir d'accord. */
    parse_lights(&b, &doc, root);

    parse_floors(&b, &doc, root);
    parse_walls(&b, &doc, root, height, thickness);
    parse_ceilings(&b, &doc, root);
    parse_boxes(&b, &doc, root);
    parse_mouldings(&b, &doc, root);
    parse_cabinets(&b, &doc, root);
    parse_props(&b, &doc, root);

    /* Après tout le mobilier, avant d'écrire quoi que ce soit. */
    check_solid_overlaps(&b);
    check_fixture_naming(&b);

    if (b.meshes.count == 0) tool_fatalf("%s : la description ne produit aucun objet", in_path);

    /*
     * Couverture des textures d'origine.
     *
     * Les 58 images de 2020 sont ce qui garde la même salle : c'est le seul
     * élément du décor qui n'a pas été réécrit. En référencer 57 signifierait
     * qu'un pan du décor a disparu — et c'est exactement le genre de perte qui ne
     * se remarque pas sur une capture, puisqu'il n'y a rien à voir là où il n'y a
     * plus rien.
     */
    /*
     * Couverture des textures : chaque image doit être soit EMPLOYÉE, soit
     * RETIRÉE explicitement, avec sa raison.
     *
     * Le contrôle ne comptait auparavant que les employées, et exigeait le total.
     * C'était juste tant que les 58 images de 2020 formaient tout le budget d'art
     * — une image qui cessait de servir était forcément une régression. Ça ne
     * l'est plus : huit de ces images sont des aplats de couleur de 24 x 24 px
     * (`bordeau_uni.jpg` fait 1 x 1), et les retirer est une correction, pas une
     * perte. Un simple ajustement du nombre attendu aurait fait disparaître ce
     * fait du dépôt ; la liste `retiredTextures` le grave, avec le motif.
     */
    if (b.expect_textures >= 0) {
        const int accounted = (int)(b.texture_count + b.retired_count);
        if (accounted != b.expect_textures) {
            tool_fatalf("%zu texture(s) employée(s) + %zu retirée(s) = %d, pour %d "
                        "présente(s) dans les dossiers d'art.\n"
                        "  Une image a donc cessé d'être employée sans être déclarée dans "
                        "`retiredTextures`, ou une a été ajoutée sans être placée.\n"
                        "  Retirer une image est légitime — la retirer en silence ne l'est "
                        "pas : c'est ainsi qu'un décor se vide sans que personne ne le voie.",
                        b.texture_count, b.retired_count, accounted, b.expect_textures);
        }
        printf("  textures : %zu employée(s), %zu retirée(s) et déclarée(s)\n",
               b.texture_count, b.retired_count);
    }

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
               "%zu textures, %zu lumières, %zu bornes, %zu lieux",
               b.meshes.count, b.triangle_count, b.verts.count,
               b.material_count, b.texture_count, b.light_count,
               b.cabinet_count, b.poi_count);
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
