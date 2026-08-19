/*
 * test_shapes.c — les générateurs paramétriques.
 *
 * Deux familles de vérifications, et la seconde compte autant que la première.
 *
 * **Les invariants** : une boîte chanfreinée a le nombre de faces qu'on peut
 * compter à la main, un masque de faces retire exactement ce qu'on lui demande,
 * un pan percé d'une baie a l'aire du pan moins celle de la baie, un angle
 * mitré tombe sur les coordonnées qu'un dessinateur calculerait, une moulure
 * bouchée a le volume de sa section fois sa longueur. Ce sont des égalités
 * exactes, pas des « ça a l'air correct » : un générateur qui se trompe d'un
 * facteur ou d'un signe les casse toutes.
 *
 * **Les refus** : `geo_wall_run` s'arrête net sur une description incohérente,
 * et il faut le prouver, sinon la garantie ne vaut rien. Comme `tool_fatalf`
 * appelle `exit(1)`, chaque refus est un *processus* séparé — un cas nommé passé
 * en argument, déclaré côté CTest avec `WILL_FAIL`. C'est portable sur les trois
 * plateformes, contrairement à un fork.
 *
 * Un refus qui ne se déclencherait plus ferait donc échouer le test qui l'attend,
 * et non passer un test devenu vide.
 */
#include "geo_shapes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  ÉCHEC %s:%d — ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((a) - (b)) <= (eps), \
    "%s ≈ %s : %.6f vs %.6f", #a, #b, (double)(a), (double)(b))

/* -------------------------------------------------------------------------- */
/* Outils de mesure                                                            */
/* -------------------------------------------------------------------------- */

/* Aire cumulée des triangles dont la normale géométrique regarde `want`. C'est
 * ce qui permet de mesurer une face de mur indépendamment du découpage en
 * panneaux : la somme ne dépend pas du nombre de morceaux. */
static float area_facing(const geo_mesh *m, ns_v3 want, float min_dot)
{
    const ns_v3 w = ns_v3_norm(want);
    float total = 0.0f;
    for (size_t t = 0; t < geo_mesh_tri_count(m); ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        const gltf_vertex *a = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[0]);
        const gltf_vertex *b = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[1]);
        const gltf_vertex *c = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[2]);
        const ns_v3 e1 = ns_v3_make(b->position[0] - a->position[0],
                                    b->position[1] - a->position[1],
                                    b->position[2] - a->position[2]);
        const ns_v3 e2 = ns_v3_make(c->position[0] - a->position[0],
                                    c->position[1] - a->position[1],
                                    c->position[2] - a->position[2]);
        const ns_v3 cr = ns_v3_cross(e1, e2);
        const float area = 0.5f * ns_v3_len(cr);
        if (area < 1e-12f) continue;
        if (ns_v3_dot(ns_v3_norm(cr), w) >= min_dot) total += area;
    }
    return total;
}

/* Aire cumulée des triangles d'un matériau donné. Sur un contour fermé, mesurer
 * par direction ne dit rien : deux murs parallèles opposés ont chacun une face
 * qui regarde de ce côté, et leurs contributions s'additionnent quelle que soit
 * l'orientation. C'est le matériau qui distingue l'intérieur de l'extérieur. */
static float area_of_material(const geo_mesh *m, int32_t material)
{
    float total = 0.0f;
    for (size_t t = 0; t < geo_mesh_tri_count(m); ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        if (tri->material != material) continue;
        const gltf_vertex *a = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[0]);
        const gltf_vertex *b = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[1]);
        const gltf_vertex *c = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[2]);
        const ns_v3 e1 = ns_v3_make(b->position[0] - a->position[0],
                                    b->position[1] - a->position[1],
                                    b->position[2] - a->position[2]);
        const ns_v3 e2 = ns_v3_make(c->position[0] - a->position[0],
                                    c->position[1] - a->position[1],
                                    c->position[2] - a->position[2]);
        total += 0.5f * ns_v3_len(ns_v3_cross(e1, e2));
    }
    return total;
}

static bool all_finite(const geo_mesh *m)
{
    for (size_t i = 0; i < geo_mesh_vertex_count(m); ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&m->verts, gltf_vertex, i);
        for (int k = 0; k < 3; ++k) {
            if (!isfinite(v->position[k])) return false;
        }
        if (!isfinite(v->uv[0]) || !isfinite(v->uv[1])) return false;
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* La boîte                                                                    */
/* -------------------------------------------------------------------------- */

static void test_box_plain(void)
{
    printf("boîte sans chanfrein\n");

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(1.0f);
    geo_box(&m, ns_v3_make(2.0f, 3.0f, 4.0f), 0.0f, GEO_FACE_ALL, &uv, 0);

    CHECK(geo_mesh_tri_count(&m) == 12, "12 triangles, %zu obtenus", geo_mesh_tri_count(&m));

    /* La boîte repose sur Y = 0 et se centre en X/Z : c'est la convention du
     * générateur, et c'est celle que `roomgen` suppose pour poser un objet. */
    const ns_aabb b = geo_mesh_bounds(&m);
    CHECK_NEAR(b.min.x, -1.0f, 1e-5f);
    CHECK_NEAR(b.max.x,  1.0f, 1e-5f);
    CHECK_NEAR(b.min.y,  0.0f, 1e-5f);
    CHECK_NEAR(b.max.y,  3.0f, 1e-5f);
    CHECK_NEAR(b.min.z, -2.0f, 1e-5f);
    CHECK_NEAR(b.max.z,  2.0f, 1e-5f);

    /* Volume signé positif = enroulement correct partout. Le rendu n'élimine pas
     * les faces arrière, donc c'est ici, et nulle part ailleurs, qu'une face
     * retournée se ferait voir. */
    CHECK_NEAR(geo_signed_volume(&m), 24.0f, 1e-3f);

    /* Densité d'UV : une répétition par mètre sur toutes les faces. */
    float lo = 0.0f, hi = 0.0f;
    CHECK(geo_uv_density_range(&m, &lo, &hi), "densité mesurable");
    CHECK_NEAR(lo, 1.0f, 1e-4f);
    CHECK_NEAR(hi, 1.0f, 1e-4f);

    geo_mesh_free(&m);
}

static void test_box_chamfered(void)
{
    printf("boîte chanfreinée\n");

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(1.0f);
    geo_box(&m, ns_v3_make(1.0f, 1.0f, 1.0f), 0.02f, GEO_FACE_ALL, &uv, 0);

    /* 6 faces × 2 + 12 biseaux × 2 + 8 angles × 1 : un compte qui se fait à la
     * main, et qui casse dès qu'une arête ou un angle est oublié. */
    CHECK(geo_mesh_tri_count(&m) == 44, "44 triangles, %zu obtenus", geo_mesh_tri_count(&m));

    /* Le chanfrein rentre dans la boîte : l'encombrement ne change pas. */
    const ns_aabb b = geo_mesh_bounds(&m);
    CHECK_NEAR(b.max.x - b.min.x, 1.0f, 1e-5f);
    CHECK_NEAR(b.max.y - b.min.y, 1.0f, 1e-5f);
    CHECK_NEAR(b.max.z - b.min.z, 1.0f, 1e-5f);

    /* Un peu moins qu'un mètre cube, et pas beaucoup moins : le chanfrein enlève
     * douze prismes de section c²/2 et huit coins. */
    const float vol = geo_signed_volume(&m);
    const float prisms = 12.0f * (1.0f - 2.0f * 0.02f) * 0.02f * 0.02f * 0.5f;
    CHECK(vol < 1.0f && vol > 1.0f - prisms - 8.0f * 0.02f * 0.02f * 0.02f,
          "volume chanfreiné plausible : %.6f", (double)vol);

    /* Un chanfrein absurde est écrêté, pas refusé : c'est un réglage esthétique,
     * et l'écrêtage garde une face centrale au lieu de produire un octaèdre. */
    geo_mesh big; geo_mesh_init(&big);
    geo_box(&big, ns_v3_make(1.0f, 1.0f, 1.0f), 10.0f, GEO_FACE_ALL, &uv, 0);
    CHECK(geo_mesh_tri_count(&big) == 44, "chanfrein écrêté, pas dégénéré");
    CHECK(geo_signed_volume(&big) > 0.5f, "l'écrêtage laisse un volume : %.4f",
          (double)geo_signed_volume(&big));
    geo_mesh_free(&big);

    geo_mesh_free(&m);
}

static void test_box_face_mask(void)
{
    printf("masque de faces\n");

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(1.0f);
    geo_box(&m, ns_v3_make(1.0f, 2.0f, 1.0f), 0.02f, GEO_FACE_SIDES, &uv, 0);

    /* 4 flancs × 2 triangles, 4 arêtes verticales × 2 : un biseau relie deux
     * faces, donc ceux qui touchaient le dessus ou le dessous disparaissent avec
     * elles, et les huit angles aussi. */
    CHECK(geo_mesh_tri_count(&m) == 16, "16 triangles, %zu obtenus", geo_mesh_tri_count(&m));
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 1, 0), 0.9f), 0.0f, 1e-6f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, -1, 0), 0.9f), 0.0f, 1e-6f);

    geo_mesh no_bottom; geo_mesh_init(&no_bottom);
    geo_box(&no_bottom, ns_v3_make(1.0f, 2.0f, 1.0f), 0.0f, GEO_FACE_NO_BOTTOM, &uv, 0);
    CHECK(geo_mesh_tri_count(&no_bottom) == 10, "10 triangles sans le dessous, %zu obtenus",
          geo_mesh_tri_count(&no_bottom));
    CHECK_NEAR(area_facing(&no_bottom, ns_v3_make(0, -1, 0), 0.9f), 0.0f, 1e-6f);
    CHECK_NEAR(area_facing(&no_bottom, ns_v3_make(0, 1, 0), 0.9f), 1.0f, 1e-5f);
    geo_mesh_free(&no_bottom);

    geo_mesh_free(&m);
}

/* -------------------------------------------------------------------------- */
/* Le plan et le panneau                                                       */
/* -------------------------------------------------------------------------- */

static void test_plane(void)
{
    printf("plan subdivisé\n");

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(2.0f);
    geo_plane(&m, 8.0f, 8.0f, 4, 4, true, &uv, 0);

    CHECK(geo_mesh_tri_count(&m) == 32, "4x4 quads = 32 triangles, %zu obtenus",
          geo_mesh_tri_count(&m));
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 1, 0), 0.99f), 64.0f, 1e-3f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, -1, 0), 0.99f), 0.0f, 1e-6f);

    /* Une répétition tous les deux mètres = 0,5 répétition par mètre. C'est la
     * grandeur que l'ancien plafond avait à 0,056, et qui le faisait lire comme
     * un aplat. */
    float lo = 0.0f, hi = 0.0f;
    CHECK(geo_uv_density_range(&m, &lo, &hi), "densité mesurable");
    CHECK_NEAR(lo, 0.5f, 1e-4f);
    CHECK_NEAR(hi, 0.5f, 1e-4f);

    /* Retourné, c'est un plafond. */
    geo_mesh down; geo_mesh_init(&down);
    geo_plane(&down, 8.0f, 8.0f, 2, 2, false, &uv, 0);
    CHECK_NEAR(area_facing(&down, ns_v3_make(0, -1, 0), 0.99f), 64.0f, 1e-3f);
    geo_mesh_free(&down);

    geo_mesh_free(&m);
}

static void test_panel(void)
{
    printf("panneau à UV explicite\n");

    geo_mesh m; geo_mesh_init(&m);
    geo_panel(&m, ns_v3_make(0.0f, 1.5f, 0.0f), ns_v3_make(0, 0, 1), ns_v3_make(1, 0, 0),
              1.20f, 0.90f, 0.25f, 0.50f, 0.50f, 0.75f, 0);

    CHECK(geo_mesh_tri_count(&m) == 2, "un quad");
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, 1), 0.99f), 1.20f * 0.90f, 1e-5f);

    /* L'UV tombe exactement sur le rectangle demandé — c'est tout l'intérêt de ce
     * générateur : un écran de borne ou une affiche n'a pas droit à l'à-peu-près,
     * et surtout pas à une répétition. */
    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
    for (size_t i = 0; i < geo_mesh_vertex_count(&m); ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&m.verts, gltf_vertex, i);
        if (v->uv[0] < umin) umin = v->uv[0];
        if (v->uv[0] > umax) umax = v->uv[0];
        if (v->uv[1] < vmin) vmin = v->uv[1];
        if (v->uv[1] > vmax) vmax = v->uv[1];
    }
    CHECK_NEAR(umin, 0.25f, 1e-6f);
    CHECK_NEAR(umax, 0.50f, 1e-6f);
    CHECK_NEAR(vmin, 0.50f, 1e-6f);
    CHECK_NEAR(vmax, 0.75f, 1e-6f);

    /* Une normale approximative et un « vers la droite » approximatif suffisent :
     * le générateur orthogonalise. Une affiche penchée se décrit ainsi. */
    geo_mesh tilted; geo_mesh_init(&tilted);
    geo_panel(&tilted, ns_v3_make(0, 1, 0), ns_v3_make(0, 0.2f, 1), ns_v3_make(1, 0.3f, 0),
              0.5f, 0.7f, 0, 0, 1, 1, 0);
    CHECK_NEAR(area_facing(&tilted, ns_v3_make(0, 0.2f, 1), 0.99f), 0.35f, 1e-5f);
    geo_mesh_free(&tilted);

    geo_mesh_free(&m);
}

/* -------------------------------------------------------------------------- */
/* L'extrusion de profil                                                       */
/* -------------------------------------------------------------------------- */

static void test_extrude(void)
{
    printf("extrusion de profil\n");

    /* Section carrée de 5 cm, listée dans le sens direct, balayée sur 2 m. */
    const float a = 0.025f;
    const ns_v2 profile[4] = {
        { -a, 0.0f }, { a, 0.0f }, { a, 2.0f * a }, { -a, 2.0f * a }
    };
    const ns_v3 path[2] = { { 0, 0, 0 }, { 2.0f, 0, 0 } };

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(0.5f);
    geo_profile_extrude(&m, profile, 4, true, path, 2, false, &uv, 0);

    /* Volume = section × longueur. Un signe positif prouve d'un coup
     * l'enroulement du balayage **et** celui des deux bouchons : une seule face
     * retournée le ferait chuter de deux fois sa contribution. */
    CHECK_NEAR(geo_signed_volume(&m), (2.0f * a) * (2.0f * a) * 2.0f, 1e-6f);
    CHECK(all_finite(&m), "aucune coordonnée non finie");

    /* Chemin fermé : pas de bouchons, et l'onglet doit tenir sur les quatre
     * angles droits d'un rectangle. Une corniche qui fait le tour d'une pièce. */
    const ns_v3 loop[4] = { { 0, 0, 0 }, { 3, 0, 0 }, { 3, 0, 2 }, { 0, 0, 2 } };
    geo_mesh ring; geo_mesh_init(&ring);
    geo_profile_extrude(&ring, profile, 4, true, loop, 4, true, &uv, 0);
    CHECK(all_finite(&ring), "onglets finis sur un contour fermé");
    /* Le volume d'un tore rectangulaire mitré vaut la section fois la longueur de
     * la fibre neutre — 10 m ici, les onglets se compensant deux à deux. */
    CHECK_NEAR(geo_signed_volume(&ring), (2.0f * a) * (2.0f * a) * 10.0f, 1e-5f);
    geo_mesh_free(&ring);

    geo_mesh_free(&m);
}

/* -------------------------------------------------------------------------- */
/* Le pan de mur                                                               */
/* -------------------------------------------------------------------------- */

static geo_wall_desc straight_wall(const ns_v2 *pts, const geo_opening *ops, size_t nops)
{
    geo_wall_desc d;
    memset(&d, 0, sizeof d);
    d.name = "essai";
    d.points = pts;
    d.point_count = 2;
    d.closed = false;
    d.height = 3.0f;
    d.thickness = 0.20f;
    d.openings = ops;
    d.opening_count = nops;
    d.uv = geo_uv_tile(1.0f);
    d.material_inner = 0;
    d.material_outer = 1;
    d.material_reveal = 2;
    return d;
}

/*
 * Un profil CONCAVE extrudé : ses bouchons ne doivent pas se recouvrir.
 *
 * Les bouchons étaient un éventail depuis le centroïde — juste pour un profil
 * convexe, faux pour tout le reste : sur un profil creux le centroïde peut
 * tomber HORS du polygone, et les triangles se chevauchent en restant
 * coplanaires. Ça ne se voit pas franchement sur une image, mais chaque pixel
 * du flanc est peint plusieurs fois.
 *
 * C'est ce qui est arrivé avec la silhouette en gradins d'une borne d'arcade,
 * répétée dix-neuf fois. Le contrôle qui l'attrape est le VOLUME SIGNÉ : un
 * solide fermé et correctement triangulé a le volume de son profil multiplié
 * par sa longueur, et des bouchons qui se recouvrent le faussent.
 */
static void test_profil_concave(void)
{
    /* Un « L » : le cas le plus simple où le centroïde sort du polygone. */
    const ns_v2 prof[6] = {
        { 0.0f, 0.0f }, { 3.0f, 0.0f }, { 3.0f, 1.0f },
        { 1.0f, 1.0f }, { 1.0f, 3.0f }, { 0.0f, 3.0f },
    };
    /* Aire du L : 3x1 + 1x2 = 5. */
    const float area = 5.0f;
    const float length = 2.0f;

    const ns_v3 path[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, length } };

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(1.0f);
    geo_profile_extrude(&m, prof, 6, true, path, 2, false, &uv, 0);

    CHECK(m.tris.count > 0, "l'extrusion produit des triangles (%zu)", m.tris.count);

    const float v = geo_signed_volume(&m);
    CHECK_NEAR(fabsf(v), area * length, 0.05f);

    /* Et aucun triangle dégénéré : une découpe d'oreilles ratée en laisse. */
    CHECK(geo_check_degenerate(&m, 1e-9f) == 0,
          "aucun triangle dégénéré dans les bouchons");

    geo_mesh_free(&m);
}

static void test_wall_plain(void)
{
    printf("pan plein\n");

    const ns_v2 pts[2] = { { 0, 0 }, { 6, 0 } };
    geo_wall_desc d = straight_wall(pts, NULL, 0);

    geo_mesh m; geo_mesh_init(&m);
    geo_wall_run(&m, &d);

    /* Deux faces de 6 × 3 m. Sans arase ni joues : ce qu'on demande sous un
     * plafond plein, où l'arase ne serait jamais vue. */
    CHECK(geo_mesh_tri_count(&m) == 4, "deux quads, %zu triangles", geo_mesh_tri_count(&m));
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, -1), 0.99f), 18.0f, 1e-4f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, 1), 0.99f), 18.0f, 1e-4f);

    /* La face intérieure est du côté gauche de la marche : c'est **la** convention
     * d'orientation, et si elle s'inversait toute la salle se retournerait. */
    const ns_v2 left_dir = ns_v2_make(0.0f, -1.0f);   /* gauche de +X */
    float inner_area = 0.0f;
    for (size_t t = 0; t < geo_mesh_tri_count(&m); ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m.tris, geo_tri, t);
        if (tri->material != d.material_inner) continue;
        const gltf_vertex *v = &TOOL_VEC_AT(&m.verts, gltf_vertex, tri->i[0]);
        CHECK_NEAR(v->position[2], -0.10f, 1e-5f);
        inner_area += 1.0f;
    }
    CHECK(inner_area == 2.0f, "la face intérieure est en z = -0,10 (gauche = %.0f,%.0f)",
          (double)left_dir.x, (double)left_dir.y);

    /* Avec arase et joues, le pan devient un volume fermé de 6 × 3 × 0,2. */
    d.cap_top = true;
    d.cap_ends = true;
    geo_mesh closed_wall; geo_mesh_init(&closed_wall);
    geo_wall_run(&closed_wall, &d);
    /* Le dessous manque volontairement — il est sous le sol — donc le volume
     * signé n'est pas celui d'un solide fermé ; on vérifie les aires. */
    CHECK_NEAR(area_facing(&closed_wall, ns_v3_make(0, 1, 0), 0.99f), 6.0f * 0.20f, 1e-4f);
    CHECK_NEAR(area_facing(&closed_wall, ns_v3_make(1, 0, 0), 0.99f), 3.0f * 0.20f, 1e-4f);
    CHECK_NEAR(area_facing(&closed_wall, ns_v3_make(-1, 0, 0), 0.99f), 3.0f * 0.20f, 1e-4f);
    geo_mesh_free(&closed_wall);

    geo_mesh_free(&m);
}

static void test_wall_opening(void)
{
    printf("pan percé\n");

    const ns_v2 pts[2] = { { 0, 0 }, { 6, 0 } };
    geo_opening door;
    memset(&door, 0, sizeof door);
    snprintf(door.name, sizeof door.name, "porte");
    door.offset = 2.0f; door.width = 1.60f; door.sill = 0.0f; door.head = 2.10f;

    geo_wall_desc d = straight_wall(pts, &door, 1);

    geo_mesh m; geo_mesh_init(&m);
    geo_wall_run(&m, &d);

    /*
     * **L'invariant central du générateur** : l'aire de chaque face vaut l'aire du
     * pan moins celle de la baie. Il ne dépend pas du découpage en panneaux, donc
     * il tient quel que soit le nombre de trumeaux — et il tomberait tout de suite
     * si un panneau était oublié, dupliqué, ou dimensionné de travers.
     */
    const float expected = 6.0f * 3.0f - 1.60f * 2.10f;
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, -1), 0.99f), expected, 1e-4f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, 1), 0.99f), expected, 1e-4f);

    /* Les jambages donnent son épaisseur au percement : deux quads de
     * 2,10 × 0,20 m, l'un regardant +X, l'autre −X. */
    CHECK_NEAR(area_facing(&m, ns_v3_make(1, 0, 0), 0.99f), 2.10f * 0.20f, 1e-4f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(-1, 0, 0), 0.99f), 2.10f * 0.20f, 1e-4f);

    /* Le dessous du linteau existe ; l'appui, non — une porte n'a pas d'allège, et
     * un appui à hauteur nulle serait coplanaire avec le sol. */
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, -1, 0), 0.99f), 1.60f * 0.20f, 1e-4f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 1, 0), 0.99f), 0.0f, 1e-6f);

    geo_mesh_free(&m);

    /* Avec allège, l'appui apparaît — une fenêtre de comptoir. */
    geo_opening window;
    memset(&window, 0, sizeof window);
    snprintf(window.name, sizeof window.name, "passe-plat");
    window.offset = 1.0f; window.width = 1.20f; window.sill = 1.0f; window.head = 2.0f;

    geo_wall_desc dw = straight_wall(pts, &window, 1);
    geo_mesh w; geo_mesh_init(&w);
    geo_wall_run(&w, &dw);
    CHECK_NEAR(area_facing(&w, ns_v3_make(0, 0, -1), 0.99f), 6.0f * 3.0f - 1.20f * 1.0f, 1e-4f);
    CHECK_NEAR(area_facing(&w, ns_v3_make(0, 1, 0), 0.99f), 1.20f * 0.20f, 1e-4f);
    CHECK_NEAR(area_facing(&w, ns_v3_make(0, -1, 0), 0.99f), 1.20f * 0.20f, 1e-4f);
    geo_mesh_free(&w);
}

static void test_wall_mitre(void)
{
    printf("angles mitrés\n");

    /* Retour d'équerre : +X puis +Z. Le côté gauche de la marche est l'extérieur
     * du virage, donc son angle s'écarte, et l'intérieur se rentre — d'exactement
     * la demi-épaisseur, ce qui se calcule à la main. */
    const ns_v2 pts[3] = { { 0, 0 }, { 4, 0 }, { 4, 4 } };
    geo_wall_desc d;
    memset(&d, 0, sizeof d);
    d.name = "retour";
    d.points = pts; d.point_count = 3; d.closed = false;
    d.height = 3.0f; d.thickness = 0.20f;
    d.uv = geo_uv_tile(1.0f);

    geo_mesh m; geo_mesh_init(&m);
    geo_wall_run(&m, &d);

    const ns_aabb b = geo_mesh_bounds(&m);
    CHECK_NEAR(b.min.x,  0.0f, 1e-5f);
    CHECK_NEAR(b.max.x,  4.1f, 1e-5f);
    CHECK_NEAR(b.min.z, -0.1f, 1e-5f);
    CHECK_NEAR(b.max.z,  4.0f, 1e-5f);
    CHECK(all_finite(&m), "coordonnées finies");

    /* Sans onglet, les deux pans se recouvriraient dans l'angle et la face
     * extérieure mesurerait 2 × 4 × 3 = 24 m². Avec, elle vaut la longueur de la
     * ligne extérieure mitrée fois la hauteur. */
    const float outer_len = (4.0f - 0.1f) + (4.0f - 0.1f);
    const float inner_len = (4.0f + 0.1f) + (4.0f + 0.1f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, -1), 0.99f)
             + area_facing(&m, ns_v3_make(1, 0, 0), 0.99f), inner_len * 3.0f, 1e-3f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, 1), 0.99f)
             + area_facing(&m, ns_v3_make(-1, 0, 0), 0.99f), outer_len * 3.0f, 1e-3f);

    geo_mesh_free(&m);
}

static void test_wall_collinear(void)
{
    printf("joint droit sous 3 degrés\n");

    /*
     * Trois points alignés : les deux lignes décalées sont confondues et leur
     * sécante n'existe pas. Sans le repli, la division par un déterminant nul
     * produirait des coordonnées infinies — et un glTF que `bvhbake` refuserait
     * en parlant d'octets, à deux étapes de là.
     */
    const ns_v2 pts[3] = { { 0, 0 }, { 5, 0 }, { 10, 0 } };
    geo_wall_desc d;
    memset(&d, 0, sizeof d);
    d.name = "aligne";
    d.points = pts; d.point_count = 3; d.closed = false;
    d.height = 3.0f; d.thickness = 0.20f;
    d.uv = geo_uv_tile(1.0f);

    geo_mesh m; geo_mesh_init(&m);
    geo_wall_run(&m, &d);

    CHECK(all_finite(&m), "le repli évite les coordonnées infinies");
    const ns_aabb b = geo_mesh_bounds(&m);
    CHECK_NEAR(b.min.x,   0.0f, 1e-5f);
    CHECK_NEAR(b.max.x,  10.0f, 1e-5f);
    CHECK_NEAR(b.min.z,  -0.1f, 1e-5f);
    CHECK_NEAR(b.max.z,   0.1f, 1e-5f);
    CHECK_NEAR(area_facing(&m, ns_v3_make(0, 0, -1), 0.99f), 30.0f, 1e-3f);

    geo_mesh_free(&m);
}

static void test_wall_closed(void)
{
    printf("contour fermé\n");

    /*
     * Une pièce rectangulaire de 10 × 8 m en axes de murs, épaisseur 0,2 m.
     *
     * L'ordre des points applique la règle : en marchant de l'un au suivant,
     * l'intérieur est à gauche. Le premier segment monte en +Z le long du mur
     * x = −5 ; à gauche, c'est +X — donc l'intérieur. C'est le seul point à
     * vérifier à la main, tout le reste en découle.
     */
    const ns_v2 pts[4] = { { -5, -4 }, { -5, 4 }, { 5, 4 }, { 5, -4 } };
    geo_wall_desc d;
    memset(&d, 0, sizeof d);
    d.name = "coquille";
    d.points = pts; d.point_count = 4; d.closed = true;
    d.height = 3.10f; d.thickness = 0.20f;
    d.uv = geo_uv_tile(2.0f);
    d.material_inner = 0; d.material_outer = 1; d.material_reveal = 2;

    geo_mesh m; geo_mesh_init(&m);
    geo_wall_run(&m, &d);

    CHECK(all_finite(&m), "coordonnées finies");

    /* L'intérieur mesure 9,8 × 7,8 m, soit un périmètre de 35,2 m ; l'extérieur
     * 10,2 × 8,2, soit 36,8 m. Si l'onglet partait du mauvais côté les deux
     * valeurs seraient échangées — et une salle serait plus grande dehors que
     * dedans, ce qui ne se verrait sur aucune capture. */
    CHECK_NEAR(area_of_material(&m, 0), 35.2f * 3.10f, 2e-3f);
    CHECK_NEAR(area_of_material(&m, 1), 36.8f * 3.10f, 2e-3f);

    /* Et la mesure qui ne dépend d'aucun calcul de périmètre : chaque triangle du
     * matériau intérieur est effectivement dans le rectangle intérieur. */
    bool inner_inside = true;
    for (size_t t = 0; t < geo_mesh_tri_count(&m); ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m.tris, geo_tri, t);
        if (tri->material != 0) continue;
        for (int k = 0; k < 3; ++k) {
            const gltf_vertex *v = &TOOL_VEC_AT(&m.verts, gltf_vertex, tri->i[k]);
            if (fabsf(v->position[0]) > 4.9f + 1e-4f) inner_inside = false;
            if (fabsf(v->position[2]) > 3.9f + 1e-4f) inner_inside = false;
        }
    }
    CHECK(inner_inside, "la face intérieure est bien à l'intérieur");

    geo_mesh_free(&m);
}

/* -------------------------------------------------------------------------- */
/* Les refus — un processus par cas, déclaré WILL_FAIL côté CTest              */
/* -------------------------------------------------------------------------- */

static void run_refusal(const char *which)
{
    const ns_v2 pts[2] = { { 0, 0 }, { 6, 0 } };
    const ns_v2 corner[3] = { { 0, 0 }, { 4, 0 }, { 4, 4 } };
    geo_opening ops[2];
    memset(ops, 0, sizeof ops);
    snprintf(ops[0].name, sizeof ops[0].name, "baie_a");
    snprintf(ops[1].name, sizeof ops[1].name, "baie_b");

    geo_mesh m; geo_mesh_init(&m);
    geo_wall_desc d = straight_wall(pts, ops, 1);

    if (strcmp(which, "baie-deborde") == 0) {
        ops[0].offset = 5.0f; ops[0].width = 2.0f; ops[0].head = 2.1f;
    } else if (strcmp(which, "baie-a-cheval") == 0) {
        d.points = corner; d.point_count = 3;
        ops[0].offset = 3.5f; ops[0].width = 1.0f; ops[0].head = 2.1f;  /* franchit l'angle */
    } else if (strcmp(which, "baies-superposees") == 0) {
        d.opening_count = 2;
        ops[0].offset = 1.0f; ops[0].width = 2.0f; ops[0].head = 2.1f;
        ops[1].offset = 2.5f; ops[1].width = 1.0f; ops[1].head = 2.1f;
    } else if (strcmp(which, "allege-au-dessus-du-linteau") == 0) {
        ops[0].offset = 1.0f; ops[0].width = 1.0f; ops[0].sill = 2.2f; ops[0].head = 1.0f;
    } else if (strcmp(which, "linteau-trop-haut") == 0) {
        ops[0].offset = 1.0f; ops[0].width = 1.0f; ops[0].head = 4.0f;   /* pan de 3 m */
    } else if (strcmp(which, "points-confondus") == 0) {
        static const ns_v2 same[2] = { { 1, 1 }, { 1, 1 } };
        d.points = same; d.opening_count = 0;
    } else if (strcmp(which, "boite-plate") == 0) {
        const geo_uv uv = geo_uv_tile(1.0f);
        geo_box(&m, ns_v3_make(1.0f, 0.0f, 1.0f), 0.0f, GEO_FACE_ALL, &uv, 0);
        return;
    } else if (strcmp(which, "instance-miroir") == 0) {
        const geo_uv uv = geo_uv_tile(1.0f);
        geo_mesh src; geo_mesh_init(&src);
        geo_box(&src, ns_v3_make(1.0f, 1.0f, 1.0f), 0.0f, GEO_FACE_ALL, &uv, 0);
        geo_xform x = GEO_XFORM_IDENTITY;
        x.scale = -1.0f;
        geo_mesh_append(&m, &src, &x, -1);
        return;
    } else {
        printf("cas de refus inconnu : %s\n", which);
        return;                          /* sortie 0 : le test WILL_FAIL échouera */
    }

    geo_wall_run(&m, &d);
    printf("le cas « %s » aurait dû être refusé\n", which);
}

/* -------------------------------------------------------------------------- */

/*
 * Les bouchons portent leur PROPRE matériau, et leurs UV se cadrent sur le
 * profil. C'est ce qui permet à une borne d'arcade d'avoir un flanc sérigraphié
 * sans que la trame déborde sur le caisson, et une planche dessinée pour un
 * flanc doit s'y poser entière — donc de (0,0) à (1,1), quelle que soit la
 * taille de la borne.
 */
static void test_bouchons_a_part(void)
{
    printf("bouchons : matériau propre et UV cadrés\n");

    const float a = 0.025f;
    const ns_v2 profile[4] = {
        { -a, 0.0f }, { a, 0.0f }, { a, 2.0f * a }, { -a, 2.0f * a }
    };
    const ns_v3 path[2] = { { 0, 0, 0 }, { 2.0f, 0, 0 } };

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(0.5f);
    geo_profile_extrude_capped(&m, profile, 4, true, path, 2, false, &uv, 7, 3, true);

    /* La géométrie ne bouge pas d'un flottant : seuls le matériau et les UV des
     * bouchons changent. C'est l'invariant qui autorise à l'employer partout. */
    CHECK_NEAR(geo_signed_volume(&m), (2.0f * a) * (2.0f * a) * 2.0f, 1e-6f);
    CHECK(all_finite(&m), "aucune coordonnée non finie");

    const geo_tri *tris = (const geo_tri *)m.tris.data;
    const gltf_vertex *vx = (const gltf_vertex *)m.verts.data;

    size_t body = 0, caps = 0, other = 0;
    float u_min = 1e9f, u_max = -1e9f, v_min = 1e9f, v_max = -1e9f;
    for (size_t t = 0; t < m.tris.count; ++t) {
        if (tris[t].material == 7) { body++; continue; }
        if (tris[t].material != 3) { other++; continue; }
        caps++;
        for (int k = 0; k < 3; ++k) {
            const gltf_vertex *v = &vx[tris[t].i[k]];
            if (v->uv[0] < u_min) u_min = v->uv[0];
            if (v->uv[0] > u_max) u_max = v->uv[0];
            if (v->uv[1] < v_min) v_min = v->uv[1];
            if (v->uv[1] > v_max) v_max = v->uv[1];
        }
    }
    CHECK(other == 0, "aucun triangle hors des deux matériaux (%zu)", other);
    CHECK(body > 0 && caps > 0,
          "le corps et les bouchons existent tous les deux (%zu / %zu)", body, caps);
    CHECK_NEAR(u_min, 0.0f, 1e-5f);
    CHECK_NEAR(u_max, 1.0f, 1e-5f);
    CHECK_NEAR(v_min, 0.0f, 1e-5f);
    CHECK_NEAR(v_max, 1.0f, 1e-5f);

    /* Sans le cadrage, les UV restent en mètres par répétition — ce qu'il faut
     * pour une moulure, et ce qui couperait une sérigraphie. */
    geo_mesh n; geo_mesh_init(&n);
    geo_profile_extrude_capped(&n, profile, 4, true, path, 2, false, &uv, 7, 3, false);
    const geo_tri *ntris = (const geo_tri *)n.tris.data;
    const gltf_vertex *nvx = (const gltf_vertex *)n.verts.data;
    bool tiled = false;
    for (size_t t = 0; t < n.tris.count; ++t) {
        if (ntris[t].material != 3) continue;
        for (int k = 0; k < 3; ++k) {
            const float u = nvx[ntris[t].i[k]].uv[0];
            if (fabsf(u) > 1e-5f && fabsf(u - 1.0f) > 1e-5f) tiled = true;
        }
    }
    CHECK(tiled, "sans cadrage, les UV du bouchon restent en mètres par répétition");
    geo_mesh_free(&n);

    /* Et le comportement d'avant est INTACT : un seul matériau partout. */
    geo_mesh o; geo_mesh_init(&o);
    geo_profile_extrude(&o, profile, 4, true, path, 2, false, &uv, 5);
    const geo_tri *otris = (const geo_tri *)o.tris.data;
    size_t foreign = 0;
    for (size_t t = 0; t < o.tris.count; ++t) if (otris[t].material != 5) foreign++;
    CHECK(foreign == 0, "l'ancienne signature garde un matériau unique (%zu)", foreign);
    CHECK_NEAR(geo_signed_volume(&o), geo_signed_volume(&m), 1e-6f);
    geo_mesh_free(&o);

    geo_mesh_free(&m);
}

static void test_revolve(void)
{
    printf("surface de révolution\n");

    /*
     * Une SPHÈRE de rayon 1, faite d'un demi-cercle du pôle sud au pôle nord.
     * C'est le cas qui a motivé la primitive — la boule d'un manche d'arcade —
     * et c'est aussi le seul dont on connaisse le volume exact, donc le seul qui
     * puisse dire si la révolution est juste plutôt que plausible.
     */
    enum { RINGS = 33, SIDES = 48 };
    float prof[RINGS * 2];
    for (int k = 0; k < RINGS; ++k) {
        const float a = -NS_PI * 0.5f + (float)k / (float)(RINGS - 1) * NS_PI;
        prof[k * 2]     = cosf(a);
        prof[k * 2 + 1] = sinf(a);
    }

    geo_mesh m; geo_mesh_init(&m);
    const geo_uv uv = geo_uv_tile(1.0f);
    geo_revolve(&m, prof, RINGS, SIDES, &uv, 0);

    /*
     * Volume signé POSITIF : c'est le contrôle qui attrape un enroulement
     * inversé, et un solide retourné ne se voit pas autrement — le moteur ne
     * fait pas de face culling, et `gbuffer.frag` retourne la normale des faces
     * arrière, donc un objet à l'envers s'affiche normalement jusqu'à ce qu'on
     * s'étonne de son éclairage.
     */
    const float vol = geo_signed_volume(&m);
    const float exact = 4.0f / 3.0f * NS_PI;
    CHECK(vol > 0.0f, "volume signé positif (%.4f)", (double)vol);
    /* Un polyèdre INSCRIT est toujours plus petit que sa sphère : à 48x32 le
     * déficit mesuré est de 1,0 %. On vérifie qu'on est DESSOUS et pas loin —
     * au-dessus, c'est que des triangles se recouvrent ou qu'une calotte est
     * comptée à l'envers. */
    CHECK(vol < exact && vol > exact * 0.985f,
          "volume proche de 4pi/3 par en dessous : %.4f pour %.4f",
          (double)vol, (double)exact);

    CHECK(geo_check_degenerate(&m, 1e-9f) == 0,
          "aucun triangle dégénéré, y compris aux deux pôles");

    /*
     * Les pôles : un anneau de rayon nul ne doit produire qu'UN triangle par
     * méridien, pas deux dont un plat. Deux anneaux dégénérés, donc
     * 2 x SIDES triangles économisés sur le compte plein.
     */
    const size_t full = (size_t)(RINGS - 1) * SIDES * 2;
    CHECK(geo_mesh_tri_count(&m) == full - (size_t)SIDES * 2,
          "les pôles ne portent qu'un triangle par méridien (%zu pour %zu)",
          geo_mesh_tri_count(&m), full - (size_t)SIDES * 2);

    /*
     * Les normales pointent vers l'EXTÉRIEUR. Sur une sphère centrée en
     * l'origine c'est exactement la position normalisée, ce qui en fait le seul
     * cas où l'on peut vérifier chaque sommet plutôt qu'une moyenne.
     */
    float worst = 1.0f;
    for (size_t i = 0; i < geo_mesh_vertex_count(&m); ++i) {
        const gltf_vertex *vx = &TOOL_VEC_AT(&m.verts, gltf_vertex, i);
        const ns_v3 p = ns_v3_make(vx->position[0], vx->position[1], vx->position[2]);
        const ns_v3 n = ns_v3_make(vx->normal[0], vx->normal[1], vx->normal[2]);
        const float len = ns_v3_len(p);
        if (len < 1e-4f) continue;
        const float d = ns_v3_dot(ns_v3_scale(p, 1.0f / len), n);
        if (d < worst) worst = d;
    }
    CHECK(worst > 0.98f, "toutes les normales sortent (pire produit scalaire %.4f)",
          (double)worst);

    geo_mesh_free(&m);

    /*
     * Un CYLINDRE par révolution doit redonner le volume d'un cylindre : c'est
     * le contrôle que le profil n'est pas parcouru à l'envers ni décalé d'un
     * anneau. Rayon 2, hauteur 3 -> 12pi.
     */
    geo_mesh c; geo_mesh_init(&c);
    const float tube[4 * 2] = { 0.0f, 0.0f,  2.0f, 0.0f,  2.0f, 3.0f,  0.0f, 3.0f };
    geo_revolve(&c, tube, 4, 64, &uv, 0);
    CHECK_NEAR(geo_signed_volume(&c), 12.0f * NS_PI, 0.07f);
    geo_mesh_free(&c);

    /*
     * ET LE MÊME CYLINDRE PAR `geo_cylinder`, qui doit rendre le même volume.
     *
     * C'est ce contrôle qui a révélé que les deux bouchons de `geo_cylinder`
     * étaient enroulés à l'envers depuis A3 : le bouchon du haut se retranchait,
     * et un cylindre fermé rendait 12,55 au lieu de 37,70. Personne ne l'avait
     * vu parce que la normale de sommet, elle, était juste, et que le rendu
     * n'élimine pas les faces arrière — mais `gbuffer.frag` retourne la normale
     * d'une face vue de dos, si bien que le dessus de chaque bouton, de chaque
     * grille et de chaque rondelle de la salle était éclairé comme s'il
     * regardait le sol.
     *
     * Aucun test ne mesurait le volume d'un cylindre. C'est celui-là.
     */
    geo_mesh cy; geo_mesh_init(&cy);
    geo_cylinder(&cy, 2.0f, 2.0f, 3.0f, 64, true, true, &uv, 0);
    CHECK_NEAR(geo_signed_volume(&cy), 12.0f * NS_PI, 0.07f);
    CHECK(geo_signed_volume(&cy) > 0.0f, "et il est positif, pas retourné");
    geo_mesh_free(&cy);

    /* Un cône : le bouchon du bas seul, la pointe en haut. r=1, h=3 -> pi. */
    geo_mesh co; geo_mesh_init(&co);
    geo_cylinder(&co, 1.0f, 0.0f, 3.0f, 64, true, false, &uv, 0);
    CHECK_NEAR(geo_signed_volume(&co), NS_PI, 0.01f);
    geo_mesh_free(&co);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        /* Un refus attendu : `tool_fatalf` sort en 1, et CTest l'exige. */
        run_refusal(argv[1]);
        return 0;
    }

    printf("=== générateurs paramétriques ===\n\n");
    test_box_plain();
    test_box_chamfered();
    test_box_face_mask();
    test_plane();
    test_revolve();
    test_panel();
    test_extrude();
    test_profil_concave();
    test_bouchons_a_part();
    test_wall_plain();
    test_wall_opening();
    test_wall_mitre();
    test_wall_collinear();
    test_wall_closed();

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
