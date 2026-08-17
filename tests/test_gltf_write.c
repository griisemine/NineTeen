/*
 * test_gltf_write.c — l'écrivain glTF produit-il un document que la chaîne
 * accepte ?
 *
 * Pourquoi ce test existe : `bvhbake` appelle `cgltf_validate()` sur le fichier
 * produit par l'étape précédente. Si l'écrivain émet un document mal formé —
 * un accesseur de longueur nulle, un décalage mal aligné, un indice de matériau
 * hors bornes — l'échec ne survient pas à l'écriture mais **cinq secondes plus
 * tard, dans un autre outil**, avec un message qui parle de décalages d'octets
 * et non de l'objet coupable.
 *
 * Le test écrit donc de vraies scènes minuscules dans un fichier temporaire et
 * les relit avec le même validateur que la chaîne de build. Il ne vérifie pas le
 * formatage du JSON, volontairement : on veut pouvoir améliorer la sortie sans
 * casser un test, mais jamais pouvoir la rendre invalide.
 *
 * Sans GPU, sans asset : utilisable en intégration continue.
 */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "gltf_write.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char *g_out_path = "test-gltf-write.gltf";

/* -------------------------------------------------------------------------- */
/* Une scène jouet : deux quads, deux matériaux, une texture.                  */
/* -------------------------------------------------------------------------- */

static gltf_vertex make_vertex(float x, float y, float z, float u, float v)
{
    gltf_vertex out;
    memset(&out, 0, sizeof out);
    out.position[0] = x; out.position[1] = y; out.position[2] = z;
    out.normal[1]   = 1.0f;
    out.uv[0] = u; out.uv[1] = v;
    out.tangent[0] = 1.0f; out.tangent[3] = 1.0f;
    return out;
}

/* Relit le document avec cgltf, exactement comme bvhbake, et renvoie le nombre
 * de primitives vues. -1 si la lecture ou la validation échoue. */
static long reload_and_validate(const char *path, cgltf_data **out_data)
{
    cgltf_options options;
    memset(&options, 0, sizeof options);

    cgltf_data *data = NULL;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        printf("  ÉCHEC — cgltf_parse_file a refusé %s\n", path);
        g_failures++;
        return -1;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        printf("  ÉCHEC — cgltf_load_buffers a refusé le .bin de %s\n", path);
        g_failures++;
        cgltf_free(data);
        return -1;
    }
    if (cgltf_validate(data) != cgltf_result_success) {
        printf("  ÉCHEC — cgltf_validate a refusé %s\n", path);
        g_failures++;
        cgltf_free(data);
        return -1;
    }

    long prims = 0;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        prims += (long)data->meshes[m].primitives_count;
    }

    if (out_data) *out_data = data;
    else          cgltf_free(data);
    return prims;
}

static void test_roundtrip(void)
{
    printf("aller-retour d'écriture et validation\n");

    gltf_vertex verts[6];
    verts[0] = make_vertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    verts[1] = make_vertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    verts[2] = make_vertex(1.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    verts[3] = make_vertex(0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    verts[4] = make_vertex(2.0f, 3.0f, 0.0f, 0.0f, 0.0f);
    verts[5] = make_vertex(3.0f, 3.0f, 1.0f, 1.0f, 1.0f);

    /* Deux triangles pour le premier objet, un pour le second : des comptes
     * différents, pour que le décalage d'un bufferView à l'autre soit exercé. */
    const uint32_t idx_a[6] = { 0, 1, 2, 0, 2, 3 };
    const uint32_t idx_b[3] = { 3, 4, 5 };

    gltf_primitive prims_a[1] = { { idx_a, 6, 0 } };
    gltf_primitive prims_b[1] = { { idx_b, 3, 1 } };

    gltf_mesh meshes[2];
    memset(meshes, 0, sizeof meshes);
    snprintf(meshes[0].name, sizeof meshes[0].name, "sol");
    meshes[0].prims = prims_a; meshes[0].prim_count = 1;
    snprintf(meshes[1].name, sizeof meshes[1].name, "borne_arcade_1");
    meshes[1].prims = prims_b; meshes[1].prim_count = 1;

    gltf_material mats[2];
    memset(mats, 0, sizeof mats);
    snprintf(mats[0].name, sizeof mats[0].name, "moquette");
    mats[0].base_color[0] = mats[0].base_color[1] = mats[0].base_color[2] = 0.5f;
    mats[0].base_color[3] = 1.0f;
    mats[0].roughness = 0.96f;
    mats[0].texture   = 0;

    /* Le second matériau est émissif *et* semi-transparent : il exerce à la fois
     * l'extension d'intensité émissive et le basculement en alphaMode BLEND. */
    snprintf(mats[1].name, sizeof mats[1].name, "ecran");
    mats[1].base_color[0] = mats[1].base_color[1] = mats[1].base_color[2] = 1.0f;
    mats[1].base_color[3] = 0.5f;
    mats[1].roughness = 0.1f;
    mats[1].emissive[0] = mats[1].emissive[1] = mats[1].emissive[2] = 1.0f;
    mats[1].emissive_strength = 2.4f;
    mats[1].texture = -1;

    static const char *const textures[1] = { "moquette.jpg" };

    gltf_scene scene;
    memset(&scene, 0, sizeof scene);
    scene.verts = verts;               scene.vert_count     = 6;
    scene.meshes = meshes;             scene.mesh_count     = 2;
    scene.materials = mats;            scene.material_count = 2;
    scene.textures = textures;         scene.texture_count  = 1;
    scene.generator = "Nineteen test";
    scene.scene_name = "jouet";

    const size_t bin_bytes = gltf_write(&scene, g_out_path);

    /* 6 sommets × (3+3+2+4) flottants = 72 flottants, plus 9 indices. */
    CHECK(bin_bytes == 6 * 12 * sizeof(float) + 9 * sizeof(uint32_t),
          "taille du .bin : %zu octets", bin_bytes);

    cgltf_data *data = NULL;
    const long prims = reload_and_validate(g_out_path, &data);
    CHECK(prims == 2, "primitives relues : %ld", prims);
    if (!data) return;

    CHECK(data->meshes_count == 2, "maillages : %zu", (size_t)data->meshes_count);
    CHECK(data->nodes_count == 2, "nœuds : %zu", (size_t)data->nodes_count);
    CHECK(data->materials_count == 2, "matériaux : %zu", (size_t)data->materials_count);
    CHECK(data->images_count == 1, "images : %zu", (size_t)data->images_count);

    /* Les noms d'objets doivent survivre : c'est par eux que le moteur relie une
     * borne à sa géométrie, et l'ancienne salle n'en avait pas d'utilisable. */
    if (data->meshes_count == 2) {
        CHECK(data->meshes[0].name && strcmp(data->meshes[0].name, "sol") == 0,
              "nom du maillage 0 : %s", data->meshes[0].name ? data->meshes[0].name : "(nul)");
        CHECK(data->meshes[1].name && strcmp(data->meshes[1].name, "borne_arcade_1") == 0,
              "nom du maillage 1 : %s", data->meshes[1].name ? data->meshes[1].name : "(nul)");
    }
    if (data->nodes_count == 2) {
        CHECK(data->nodes[1].name && strcmp(data->nodes[1].name, "borne_arcade_1") == 0,
              "nom du nœud 1 : %s", data->nodes[1].name ? data->nodes[1].name : "(nul)");
    }

    /*
     * Tous les accesseurs d'attributs doivent être **partagés**. Ce n'est pas une
     * préférence : `ns_scene_load` compte les sommets une fois par accesseur
     * distinct, et un accesseur par primitive avait transformé 96 067 sommets en
     * 17,5 millions — l'arène avait refusé net 843 Mio.
     */
    if (data->meshes_count == 2
        && data->meshes[0].primitives_count == 1
        && data->meshes[1].primitives_count == 1) {
        const cgltf_primitive *pa = &data->meshes[0].primitives[0];
        const cgltf_primitive *pb = &data->meshes[1].primitives[0];
        CHECK(pa->attributes_count == 4 && pb->attributes_count == 4,
              "attributs : %zu et %zu", (size_t)pa->attributes_count,
              (size_t)pb->attributes_count);
        CHECK(pa->attributes[0].data == pb->attributes[0].data,
              "les deux primitives doivent partager l'accesseur POSITION");
        CHECK(pa->indices != pb->indices,
              "chaque primitive doit avoir son propre accesseur d'indices");
        CHECK(pa->indices && pa->indices->count == 6, "indices de la primitive 0");
        CHECK(pb->indices && pb->indices->count == 3, "indices de la primitive 1");

        /* POSITION doit porter min/max : glTF les y rend obligatoires, et cgltf
         * les réclame à la validation. */
        CHECK(pa->attributes[0].data->has_min && pa->attributes[0].data->has_max,
              "POSITION doit porter min et max");
        if (pa->attributes[0].data->has_max) {
            CHECK(fabsf(pa->attributes[0].data->max[1] - 3.0f) < 1e-5f,
                  "max Y attendu 3.0, lu %.4f", (double)pa->attributes[0].data->max[1]);
        }
    }

    /* L'extension d'intensité émissive doit avoir été écrite ET déclarée. */
    if (data->materials_count == 2) {
        CHECK(data->materials[1].has_emissive_strength,
              "le matériau émissif doit porter KHR_materials_emissive_strength");
        if (data->materials[1].has_emissive_strength) {
            CHECK(fabsf(data->materials[1].emissive_strength.emissive_strength - 2.4f) < 1e-4f,
                  "intensité émissive lue : %.4f",
                  (double)data->materials[1].emissive_strength.emissive_strength);
        }
        CHECK(data->materials[1].alpha_mode == cgltf_alpha_mode_blend,
              "un matériau à alpha 0.5 doit passer en BLEND");
        CHECK(data->materials[0].alpha_mode == cgltf_alpha_mode_opaque,
              "un matériau opaque doit rester OPAQUE");
    }

    cgltf_free(data);
}

/*
 * Une scène sans texture ne doit pas émettre de bloc `samplers` orphelin.
 * cgltf refuse un `textures` vide, et un sampler sans texture est du bruit —
 * c'est exactement le genre de détail qui ne casse qu'une plateforme sur trois.
 */
static void test_no_texture(void)
{
    printf("\nscène sans texture\n");

    gltf_vertex verts[3];
    verts[0] = make_vertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    verts[1] = make_vertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    verts[2] = make_vertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);

    const uint32_t idx[3] = { 0, 1, 2 };
    gltf_primitive prim[1] = { { idx, 3, -1 } };

    gltf_mesh mesh;
    memset(&mesh, 0, sizeof mesh);
    snprintf(mesh.name, sizeof mesh.name, "triangle");
    mesh.prims = prim; mesh.prim_count = 1;

    gltf_scene scene;
    memset(&scene, 0, sizeof scene);
    scene.verts = verts;  scene.vert_count = 3;
    scene.meshes = &mesh; scene.mesh_count = 1;

    gltf_write(&scene, g_out_path);

    cgltf_data *data = NULL;
    const long prims = reload_and_validate(g_out_path, &data);
    CHECK(prims == 1, "primitives : %ld", prims);
    if (!data) return;

    CHECK(data->images_count == 0, "aucune image attendue, %zu écrite(s)",
          (size_t)data->images_count);
    CHECK(data->samplers_count == 0, "aucun sampler attendu, %zu écrit(s)",
          (size_t)data->samplers_count);
    /* Une primitive sans matériau est légale, et le moteur a un matériau de
     * repli — mais elle doit rester sans matériau, pas pointer sur l'indice 0. */
    if (data->meshes_count == 1 && data->meshes[0].primitives_count == 1) {
        CHECK(data->meshes[0].primitives[0].material == NULL,
              "une primitive sans matériau ne doit pas en désigner un");
    }
    cgltf_free(data);
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    if (argc > 1) g_out_path = argv[1];

    printf("=== écriture glTF ===\n\n");
    test_roundtrip();
    test_no_texture();

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
