/*
 * test_geo.c — le substrat de génération de géométrie.
 *
 * Soudure, normales lissées, tangentes : trois traitements qui « marchent à
 * moitié » sans rien signaler. Une soudure trop gourmande efface un chanfrein ;
 * une soudure trop timide multiplie les sommets ; un lissage mal seuillé arrondit
 * les arêtes vives ; une tangente calculée avant les UV définitives éclaire les
 * normal maps du mauvais côté. Aucun de ces défauts ne se voit sur une capture
 * fixe, et tous se voient en mouvement — donc on les teste ici, sans GPU.
 */
#include "geo_mesh.h"

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
/* Un cube de côté 1, centré sur l'origine, faces séparées (24 sommets).       */
/* Chaque face porte sa propre normale et des UV 0..1.                         */
/* -------------------------------------------------------------------------- */

static void build_cube(geo_mesh *m, float half, int32_t material)
{
    geo_mesh_init(m);

    /* axe, signe, et les deux axes tangents de chaque face */
    static const int axis[6]  = { 0, 0, 1, 1, 2, 2 };
    static const float sign[6] = { +1, -1, +1, -1, +1, -1 };

    for (int f = 0; f < 6; ++f) {
        const int a = axis[f];
        const int u = (a + 1) % 3;
        const int v = (a + 2) % 3;

        ns_v3 n = ns_v3_zero();
        float *nc = &n.x;
        nc[a] = sign[f];

        uint32_t idx[4];
        for (int c = 0; c < 4; ++c) {
            /* (0,0) (1,0) (1,1) (0,1) dans le repère de la face */
            const float uu = (c == 1 || c == 2) ? +1.0f : -1.0f;
            const float vv = (c >= 2) ? +1.0f : -1.0f;

            ns_v3 p = ns_v3_zero();
            float *pc = &p.x;
            pc[a] = sign[f] * half;
            pc[u] = uu * half * sign[f];   /* garde l'enroulement direct */
            pc[v] = vv * half;

            idx[c] = geo_mesh_push_vertex(m, p, n,
                                          (uu * 0.5f + 0.5f), (vv * 0.5f + 0.5f));
        }
        geo_mesh_push_quad(m, idx[0], idx[1], idx[2], idx[3], material);
    }
}

/* -------------------------------------------------------------------------- */

static void test_build_and_bounds(void)
{
    printf("construction et emprise\n");

    geo_mesh m;
    build_cube(&m, 0.5f, 0);

    CHECK(geo_mesh_vertex_count(&m) == 24, "24 sommets, %zu obtenus",
          geo_mesh_vertex_count(&m));
    CHECK(geo_mesh_tri_count(&m) == 12, "12 triangles, %zu obtenus",
          geo_mesh_tri_count(&m));

    const ns_aabb b = geo_mesh_bounds(&m);
    CHECK_NEAR(b.min.x, -0.5f, 1e-5f);
    CHECK_NEAR(b.max.y, 0.5f, 1e-5f);
    CHECK_NEAR(b.max.z - b.min.z, 1.0f, 1e-5f);

    CHECK(geo_check_degenerate(&m, 1e-9f) == 0, "aucun triangle dégénéré attendu");

    geo_mesh_free(&m);
}

/*
 * Le lissage doit préserver les arêtes vives.
 *
 * Sur un cube, un seuil serré (1°) doit laisser 24 sommets : les trois faces qui
 * se rejoignent à un coin sont à 90° les unes des autres, donc chacune garde sa
 * normale. Avec un seuil large (120°), les trois fusionnent et il ne reste que
 * 8 sommets, aux normales diagonales — c'est le comportement « boule », qui est
 * exactement ce qu'il ne faut PAS obtenir sur une boîte chanfreinée.
 */
static void test_smoothing_preserves_edges(void)
{
    printf("\nlissage : arêtes vives préservées\n");

    geo_mesh sharp;
    build_cube(&sharp, 0.5f, 0);
    geo_weld(&sharp, 1e-4f);
    geo_smooth_normals(&sharp, 1.0f);
    geo_generate_tangents(&sharp);

    CHECK(geo_mesh_vertex_count(&sharp) == 24,
          "seuil serré : 24 sommets attendus, %zu obtenus", geo_mesh_vertex_count(&sharp));
    CHECK(geo_mesh_tri_count(&sharp) == 12, "les triangles ne bougent pas");

    /* Chaque normale doit rester alignée sur un axe. */
    bool all_axis_aligned = true;
    for (size_t i = 0; i < geo_mesh_vertex_count(&sharp); ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&sharp.verts, gltf_vertex, i);
        const float m = fmaxf(fabsf(v->normal[0]),
                              fmaxf(fabsf(v->normal[1]), fabsf(v->normal[2])));
        if (m < 0.999f) { all_axis_aligned = false; break; }
    }
    CHECK(all_axis_aligned, "les normales doivent rester alignées sur les axes");

    geo_mesh smooth;
    build_cube(&smooth, 0.5f, 0);
    geo_weld(&smooth, 1e-4f);
    geo_smooth_normals(&smooth, 120.0f);

    CHECK(geo_mesh_vertex_count(&smooth) < geo_mesh_vertex_count(&sharp),
          "seuil large : les sommets doivent fusionner (%zu contre %zu)",
          geo_mesh_vertex_count(&smooth), geo_mesh_vertex_count(&sharp));

    geo_mesh_free(&sharp);
    geo_mesh_free(&smooth);
}

/*
 * La soudure ne doit pas coudre deux morceaux d'atlas.
 *
 * Deux quads qui partagent une arête géométrique mais pas leurs UV — le cas d'un
 * mur et de sa plinthe, ou de deux faces d'atlas voisines — doivent garder des
 * sommets distincts, sans quoi la texture se déchirerait le long de la couture.
 */
static void test_weld_respects_uv_seams(void)
{
    printf("\nsoudure : les coutures d'UV sont respectées\n");

    geo_mesh m;
    geo_mesh_init(&m);
    const ns_v3 n = ns_v3_make(0, 1, 0);

    /* Deux quads adjacents en x = 0, avec des UV discontinues à la jonction. */
    const uint32_t a0 = geo_mesh_push_vertex(&m, ns_v3_make(-1, 0, 0), n, 0.0f, 0.0f);
    const uint32_t a1 = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 0),  n, 1.0f, 0.0f);
    const uint32_t a2 = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 1),  n, 1.0f, 1.0f);
    const uint32_t a3 = geo_mesh_push_vertex(&m, ns_v3_make(-1, 0, 1), n, 0.0f, 1.0f);
    geo_mesh_push_quad(&m, a0, a1, a2, a3, 0);

    /* Mêmes positions à la jonction, UV repartant de 0 : couture. */
    const uint32_t b0 = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 0), n, 0.0f, 0.0f);
    const uint32_t b1 = geo_mesh_push_vertex(&m, ns_v3_make(1, 0, 0), n, 1.0f, 0.0f);
    const uint32_t b2 = geo_mesh_push_vertex(&m, ns_v3_make(1, 0, 1), n, 1.0f, 1.0f);
    const uint32_t b3 = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 1), n, 0.0f, 1.0f);
    geo_mesh_push_quad(&m, b0, b1, b2, b3, 0);

    CHECK(geo_mesh_vertex_count(&m) == 8, "8 sommets avant soudure");
    geo_weld(&m, 1e-4f);
    CHECK(geo_mesh_vertex_count(&m) == 8,
          "la couture doit survivre à la soudure : %zu sommets",
          geo_mesh_vertex_count(&m));

    /* En revanche, des UV identiques doivent bien fusionner. */
    geo_mesh same;
    geo_mesh_init(&same);
    const uint32_t c0 = geo_mesh_push_vertex(&same, ns_v3_make(0, 0, 0), n, 0.5f, 0.5f);
    const uint32_t c1 = geo_mesh_push_vertex(&same, ns_v3_make(1, 0, 0), n, 1.0f, 0.5f);
    const uint32_t c2 = geo_mesh_push_vertex(&same, ns_v3_make(1, 0, 1), n, 1.0f, 1.0f);
    geo_mesh_push_tri(&same, c0, c1, c2, 0);
    const uint32_t d0 = geo_mesh_push_vertex(&same, ns_v3_make(0, 0, 0), n, 0.5f, 0.5f);
    const uint32_t d1 = geo_mesh_push_vertex(&same, ns_v3_make(1, 0, 1), n, 1.0f, 1.0f);
    const uint32_t d2 = geo_mesh_push_vertex(&same, ns_v3_make(0, 0, 1), n, 0.5f, 1.0f);
    geo_mesh_push_tri(&same, d0, d1, d2, 0);

    CHECK(geo_mesh_vertex_count(&same) == 6, "6 sommets avant soudure");
    geo_weld(&same, 1e-4f);
    CHECK(geo_mesh_vertex_count(&same) == 4,
          "les sommets identiques doivent fusionner : %zu obtenus",
          geo_mesh_vertex_count(&same));

    geo_mesh_free(&m);
    geo_mesh_free(&same);
}

/*
 * Volume signé : positif si l'objet est correctement enroulé, négatif s'il est
 * retourné. C'est le contrôle qui attrape un générateur dont l'ordre des sommets
 * a été inversé par distraction — invisible au rendu, puisque l'élimination des
 * faces arrière est désactivée, mais bien visible sur l'éclairage.
 */
static void test_signed_volume(void)
{
    printf("\nvolume signé\n");

    geo_mesh m;
    build_cube(&m, 0.5f, 0);
    const float v = geo_signed_volume(&m);
    CHECK_NEAR(v, 1.0f, 1e-4f);

    /* Retourner tous les triangles doit inverser le signe. */
    for (size_t i = 0; i < m.tris.count; ++i) {
        geo_tri *t = &TOOL_VEC_AT(&m.tris, geo_tri, i);
        const uint32_t tmp = t->i[1]; t->i[1] = t->i[2]; t->i[2] = tmp;
    }
    CHECK_NEAR(geo_signed_volume(&m), -1.0f, 1e-4f);

    geo_mesh_free(&m);
}

/*
 * Densité de texels, en répétitions de texture par mètre.
 *
 * C'est le garde-fou du pavage : l'ancien plafond bouclait une fois tous les
 * 17,9 m, soit 0,056 répétition par mètre, et se lisait comme un aplat gris.
 */
static void test_uv_density(void)
{
    printf("\ndensité de texels\n");

    geo_mesh m;
    geo_mesh_init(&m);
    const ns_v3 n = ns_v3_make(0, 1, 0);

    /* Quad de 2 m de côté, UV de 0 à 1 : une répétition tous les 2 m. */
    const uint32_t a = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 0), n, 0.0f, 0.0f);
    const uint32_t b = geo_mesh_push_vertex(&m, ns_v3_make(2, 0, 0), n, 1.0f, 0.0f);
    const uint32_t c = geo_mesh_push_vertex(&m, ns_v3_make(2, 0, 2), n, 1.0f, 1.0f);
    const uint32_t d = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 2), n, 0.0f, 1.0f);
    geo_mesh_push_quad(&m, a, b, c, d, 0);

    float lo = 0.0f, hi = 0.0f;
    CHECK(geo_uv_density_range(&m, &lo, &hi), "densité mesurable");
    CHECK_NEAR(lo, 0.5f, 1e-4f);
    CHECK_NEAR(hi, 0.5f, 1e-4f);

    geo_mesh_free(&m);
}

/*
 * Tangentes : pour un quad dans le plan XZ dont l'UV u croît selon +X, la
 * tangente doit pointer vers +X. Se tromper de signe ici retourne l'éclairage des
 * normal maps sur un axe, ce qui donne un relief creusé au lieu de bombé.
 */
static void test_tangents(void)
{
    printf("\ntangentes\n");

    geo_mesh m;
    geo_mesh_init(&m);
    const ns_v3 n = ns_v3_make(0, 1, 0);
    const uint32_t a = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 0), n, 0.0f, 0.0f);
    const uint32_t b = geo_mesh_push_vertex(&m, ns_v3_make(1, 0, 0), n, 1.0f, 0.0f);
    const uint32_t c = geo_mesh_push_vertex(&m, ns_v3_make(1, 0, 1), n, 1.0f, 1.0f);
    const uint32_t d = geo_mesh_push_vertex(&m, ns_v3_make(0, 0, 1), n, 0.0f, 1.0f);
    geo_mesh_push_quad(&m, a, b, c, d, 0);

    geo_generate_tangents(&m);

    const gltf_vertex *v0 = &TOOL_VEC_AT(&m.verts, gltf_vertex, 0);
    CHECK_NEAR(v0->tangent[0], 1.0f, 1e-4f);
    CHECK_NEAR(v0->tangent[1], 0.0f, 1e-4f);
    CHECK_NEAR(v0->tangent[2], 0.0f, 1e-4f);
    CHECK(fabsf(v0->tangent[3]) == 1.0f, "la chiralité vaut ±1, lue %.3f",
          (double)v0->tangent[3]);

    /* Et la tangente doit être orthogonale à la normale, sur tous les sommets. */
    for (size_t i = 0; i < geo_mesh_vertex_count(&m); ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&m.verts, gltf_vertex, i);
        const float dot = v->tangent[0] * v->normal[0]
                        + v->tangent[1] * v->normal[1]
                        + v->tangent[2] * v->normal[2];
        CHECK(fabsf(dot) < 1e-4f, "tangente orthogonale à la normale (%.6f)", (double)dot);
    }

    geo_mesh_free(&m);
}

/*
 * Instanciation : la transformation doit déplacer et tourner, et une échelle
 * uniforme ne doit pas dénormaliser les normales.
 */
static void test_append_xform(void)
{
    printf("\ninstanciation\n");

    geo_mesh unit;
    build_cube(&unit, 0.5f, 0);

    geo_mesh world;
    geo_mesh_init(&world);

    geo_xform x = GEO_XFORM_IDENTITY;
    x.origin = ns_v3_make(10.0f, 0.0f, -4.0f);
    x.yaw = 90.0f * NS_DEG2RAD;
    x.scale = 2.0f;
    geo_mesh_append(&world, &unit, &x, -1);

    const ns_aabb b = geo_mesh_bounds(&world);
    const ns_v3 centre = ns_aabb_center(b);
    CHECK_NEAR(centre.x, 10.0f, 1e-4f);
    CHECK_NEAR(centre.z, -4.0f, 1e-4f);
    CHECK_NEAR(b.max.y - b.min.y, 2.0f, 1e-4f);   /* cube de 1 m mis à l'échelle 2 */

    bool normalised = true;
    for (size_t i = 0; i < geo_mesh_vertex_count(&world); ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&world.verts, gltf_vertex, i);
        const float len = sqrtf(v->normal[0]*v->normal[0]
                              + v->normal[1]*v->normal[1]
                              + v->normal[2]*v->normal[2]);
        if (fabsf(len - 1.0f) > 1e-4f) { normalised = false; break; }
    }
    CHECK(normalised, "l'échelle uniforme ne doit pas dénormaliser les normales");

    /* Le remplacement de matériau doit s'appliquer à tous les triangles ajoutés. */
    geo_mesh_append(&world, &unit, NULL, 7);
    bool overridden = true;
    for (size_t i = geo_mesh_tri_count(&unit); i < geo_mesh_tri_count(&world); ++i) {
        if (TOOL_VEC_AT(&world.tris, geo_tri, i).material != 7) { overridden = false; break; }
    }
    CHECK(overridden, "material_override doit s'appliquer");

    geo_mesh_free(&unit);
    geo_mesh_free(&world);
}

/* Regroupement par matériau, tel que l'écrivain glTF le consomme. */
static void test_primitives(void)
{
    printf("\nregroupement en primitives\n");

    geo_mesh m;
    build_cube(&m, 0.5f, 3);
    /* Deux faces passent sur un autre matériau. */
    TOOL_VEC_AT(&m.tris, geo_tri, 0).material = 5;
    TOOL_VEC_AT(&m.tris, geo_tri, 1).material = 5;

    geo_primitives p;
    geo_build_primitives(&m, &p);

    CHECK(p.count == 2, "deux matériaux, %zu primitives", p.count);
    size_t total = 0;
    for (size_t i = 0; i < p.count; ++i) {
        total += p.prims[i].index_count;
        CHECK(p.prims[i].index_count % 3 == 0, "indices multiples de 3");
    }
    CHECK(total == geo_mesh_tri_count(&m) * 3, "tous les triangles sont émis : %zu", total);
    /* L'ordre d'apparition est conservé : le matériau 5 est vu en premier. */
    CHECK(p.count == 2 && p.prims[0].material == 5, "ordre d'apparition conservé");

    geo_primitives_free(&p);
    geo_mesh_free(&m);
}

/* -------------------------------------------------------------------------- */

int main(void)
{
    printf("=== substrat de géométrie ===\n\n");
    test_build_and_bounds();
    test_smoothing_preserves_edges();
    test_weld_respects_uv_seams();
    test_signed_volume();
    test_uv_density();
    test_tangents();
    test_append_xform();
    test_primitives();

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
