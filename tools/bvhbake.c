/*
 * bvhbake — construit la structure d'accélération spatiale de la salle.
 *
 * Entrée : le glTF produit par obj2gltf.
 * Sortie : un fichier .nsbvh chargé tel quel dans un storage buffer.
 *
 * Construction par découpage SAH (Surface Area Heuristic) avec binning : à
 * chaque nœud, on essaie 12 plans de coupe par axe et on retient celui qui
 * minimise le coût attendu de traversée. C'est plus lent à construire qu'une
 * médiane spatiale, mais la salle est construite une fois au build et traversée
 * des millions de fois par image — l'arbitrage est évident.
 *
 * Le résultat sert au rendu, à la collision et à l'occlusion audio.
 */
#include "tools_common.h"

#include "../engine/scene/ns_bvh_format.h"

#include <math.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define BINS 12
#define MAX_LEAF_TRIS 4

typedef struct build_tri {
    float    v0[3], v1[3], v2[3];
    float    centroid[3];
    float    bmin[3], bmax[3];
    uint32_t material;
} build_tri;

typedef struct builder {
    build_tri   *tris;
    uint32_t    *indices;      /* permutation des triangles */
    uint32_t     tri_count;
    ns_bvh_node *nodes;
    uint32_t     node_count;
    uint32_t     node_capacity;
    uint32_t     max_depth;
    uint32_t     leaf_count;
} builder;

static void aabb_reset(float bmin[3], float bmax[3])
{
    for (int i = 0; i < 3; ++i) { bmin[i] = 1e30f; bmax[i] = -1e30f; }
}

static void aabb_add_point(float bmin[3], float bmax[3], const float p[3])
{
    for (int i = 0; i < 3; ++i) {
        if (p[i] < bmin[i]) bmin[i] = p[i];
        if (p[i] > bmax[i]) bmax[i] = p[i];
    }
}

static void aabb_add_box(float bmin[3], float bmax[3], const float omin[3], const float omax[3])
{
    for (int i = 0; i < 3; ++i) {
        if (omin[i] < bmin[i]) bmin[i] = omin[i];
        if (omax[i] > bmax[i]) bmax[i] = omax[i];
    }
}

static float aabb_area(const float bmin[3], const float bmax[3])
{
    const float ex = bmax[0] - bmin[0];
    const float ey = bmax[1] - bmin[1];
    const float ez = bmax[2] - bmin[2];
    if (ex < 0.0f || ey < 0.0f || ez < 0.0f) return 0.0f;   /* boîte vide */
    return 2.0f * (ex * ey + ey * ez + ez * ex);
}

/* ========================================================================== */
/* Construction récursive                                                     */
/* ========================================================================== */

static uint32_t alloc_node(builder *b)
{
    if (b->node_count >= b->node_capacity) tool_fatalf("dépassement du nombre de nœuds prévu");
    return b->node_count++;
}

static void build_node(builder *b, uint32_t node_index, uint32_t first, uint32_t count, uint32_t depth)
{
    if (depth > b->max_depth) b->max_depth = depth;

    ns_bvh_node *node = &b->nodes[node_index];
    aabb_reset(node->bmin, node->bmax);
    for (uint32_t i = 0; i < count; ++i) {
        const build_tri *t = &b->tris[b->indices[first + i]];
        aabb_add_box(node->bmin, node->bmax, t->bmin, t->bmax);
    }

    /* Feuille si peu de triangles, ou si l'arbre devient trop profond — un
     * garde-fou nécessaire : des triangles exactement superposés ne peuvent
     * jamais être séparés et provoqueraient une récursion infinie. */
    if (count <= MAX_LEAF_TRIS || depth >= 60) {
        node->left_first = first;
        node->tri_count  = count;
        b->leaf_count++;
        return;
    }

    /* --- recherche du meilleur plan de coupe par binning SAH --- */
    float cmin[3], cmax[3];
    aabb_reset(cmin, cmax);
    for (uint32_t i = 0; i < count; ++i) {
        aabb_add_point(cmin, cmax, b->tris[b->indices[first + i]].centroid);
    }

    int best_axis = -1, best_bin = -1;
    float best_cost = 1e30f;

    for (int axis = 0; axis < 3; ++axis) {
        const float extent = cmax[axis] - cmin[axis];
        if (extent < 1e-9f) continue;                       /* rien à séparer sur cet axe */

        struct { float bmin[3], bmax[3]; uint32_t count; } bins[BINS];
        for (int i = 0; i < BINS; ++i) { aabb_reset(bins[i].bmin, bins[i].bmax); bins[i].count = 0; }

        const float scale = (float)BINS / extent;
        for (uint32_t i = 0; i < count; ++i) {
            const build_tri *t = &b->tris[b->indices[first + i]];
            int bi = (int)((t->centroid[axis] - cmin[axis]) * scale);
            if (bi < 0) bi = 0;
            if (bi >= BINS) bi = BINS - 1;
            bins[bi].count++;
            aabb_add_box(bins[bi].bmin, bins[bi].bmax, t->bmin, t->bmax);
        }

        /* Balayages gauche puis droite pour obtenir, en O(BINS), l'aire et le
         * compte cumulés de chaque côté de chacun des 11 plans candidats. */
        float    left_area[BINS - 1], right_area[BINS - 1];
        uint32_t left_count[BINS - 1], right_count[BINS - 1];

        float acc_min[3], acc_max[3];
        aabb_reset(acc_min, acc_max);
        uint32_t acc_count = 0;
        for (int i = 0; i < BINS - 1; ++i) {
            acc_count += bins[i].count;
            aabb_add_box(acc_min, acc_max, bins[i].bmin, bins[i].bmax);
            left_count[i] = acc_count;
            left_area[i]  = aabb_area(acc_min, acc_max);
        }

        aabb_reset(acc_min, acc_max);
        acc_count = 0;
        for (int i = BINS - 1; i > 0; --i) {
            acc_count += bins[i].count;
            aabb_add_box(acc_min, acc_max, bins[i].bmin, bins[i].bmax);
            right_count[i - 1] = acc_count;
            right_area[i - 1]  = aabb_area(acc_min, acc_max);
        }

        for (int i = 0; i < BINS - 1; ++i) {
            if (left_count[i] == 0 || right_count[i] == 0) continue;
            const float cost = left_area[i] * (float)left_count[i]
                             + right_area[i] * (float)right_count[i];
            if (cost < best_cost) { best_cost = cost; best_axis = axis; best_bin = i; }
        }
    }

    /* Si aucune coupe n'est meilleure que de garder le nœud entier, on s'arrête :
     * couper coûterait plus cher que traverser. */
    const float leaf_cost = aabb_area(node->bmin, node->bmax) * (float)count;
    if (best_axis < 0 || best_cost >= leaf_cost) {
        node->left_first = first;
        node->tri_count  = count;
        b->leaf_count++;
        return;
    }

    /* --- partition en place autour du plan retenu --- */
    const float extent = cmax[best_axis] - cmin[best_axis];
    const float scale  = (float)BINS / extent;
    uint32_t mid = first;
    for (uint32_t i = first; i < first + count; ++i) {
        const build_tri *t = &b->tris[b->indices[i]];
        int bi = (int)((t->centroid[best_axis] - cmin[best_axis]) * scale);
        if (bi < 0) bi = 0;
        if (bi >= BINS) bi = BINS - 1;
        if (bi <= best_bin) {
            const uint32_t tmp = b->indices[i];
            b->indices[i] = b->indices[mid];
            b->indices[mid] = tmp;
            mid++;
        }
    }

    const uint32_t left_count = mid - first;
    if (left_count == 0 || left_count == count) {
        /* Le binning n'a pas séparé (triangles confondus) : on coupe au milieu
         * plutôt que de récurser à l'infini. */
        mid = first + count / 2;
    }

    const uint32_t left = alloc_node(b);
    const uint32_t right = alloc_node(b);
    /* `node` a pu être invalidé si le tableau bougeait ; ici il est fixe, mais
     * on relit l'index pour rester correct si l'allocation devenait dynamique. */
    b->nodes[node_index].left_first = left;
    b->nodes[node_index].tri_count  = 0;

    build_node(b, left,  first, mid - first, depth + 1);
    build_node(b, right, mid, first + count - mid, depth + 1);
}

/* ========================================================================== */
/* Lecture du glTF                                                            */
/* ========================================================================== */

static void transform_point(const cgltf_float m[16], const float in[3], float out[3])
{
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8]  * in[2] + m[12];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9]  * in[2] + m[13];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14];
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "bvhbake — construit le BVH d'une scène glTF (rendu, collision, audio)\n"
            "usage : %s <scène.gltf> <sortie.nsbvh>\n", argv[0]);
        return 2;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];

    printf("bvhbake : %s\n", in_path);

    cgltf_options gopt;
    memset(&gopt, 0, sizeof gopt);
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&gopt, in_path, &data) != cgltf_result_success) {
        tool_fatalf("glTF illisible : %s", in_path);
    }
    if (cgltf_load_buffers(&gopt, data, in_path) != cgltf_result_success) {
        tool_fatalf("tampons glTF introuvables (le .bin est-il à côté du .gltf ?)");
    }
    if (cgltf_validate(data) != cgltf_result_success) {
        tool_warnf("le glTF ne passe pas la validation stricte — poursuite quand même");
    }

    /* --- matériaux condensés --- */
    tool_vec materials; tool_vec_init(&materials, sizeof(ns_bvh_material));
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material *src = &data->materials[i];
        ns_bvh_material *m = (ns_bvh_material *)tool_vec_push(&materials);
        memset(m, 0, sizeof *m);
        if (src->has_pbr_metallic_roughness) {
            m->albedo[0] = src->pbr_metallic_roughness.base_color_factor[0];
            m->albedo[1] = src->pbr_metallic_roughness.base_color_factor[1];
            m->albedo[2] = src->pbr_metallic_roughness.base_color_factor[2];
            m->metallic  = src->pbr_metallic_roughness.metallic_factor;
            m->roughness = src->pbr_metallic_roughness.roughness_factor;
        } else {
            m->albedo[0] = m->albedo[1] = m->albedo[2] = 0.8f;
            m->roughness = 0.8f;
        }
        const float strength = src->has_emissive_strength
                             ? src->emissive_strength.emissive_strength : 1.0f;
        m->emissive[0] = src->emissive_factor[0] * strength;
        m->emissive[1] = src->emissive_factor[1] * strength;
        m->emissive[2] = src->emissive_factor[2] * strength;
    }
    tool_infof("%zu matériaux", (size_t)materials.count);

    /* --- triangles, dans l'espace monde --- */
    tool_vec tris; tool_vec_init(&tris, sizeof(build_tri));

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;

        cgltf_float world[16];
        cgltf_node_transform_world(node, world);

        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive *prim = &node->mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor *pos = NULL;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    pos = prim->attributes[a].data;
                }
            }
            if (!pos || !prim->indices) continue;

            const uint32_t mat_index = prim->material
                ? (uint32_t)cgltf_material_index(data, prim->material) : 0u;

            const cgltf_size index_count = prim->indices->count;
            for (cgltf_size i = 0; i + 2 < index_count; i += 3) {
                float local[3][3], world_pos[3][3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    const cgltf_size vi = cgltf_accessor_read_index(prim->indices, i + (cgltf_size)k);
                    if (!cgltf_accessor_read_float(pos, vi, local[k], 3)) { ok = false; break; }
                    transform_point(world, local[k], world_pos[k]);
                }
                if (!ok) continue;

                build_tri *t = (build_tri *)tool_vec_push(&tris);
                memcpy(t->v0, world_pos[0], sizeof t->v0);
                memcpy(t->v1, world_pos[1], sizeof t->v1);
                memcpy(t->v2, world_pos[2], sizeof t->v2);
                t->material = mat_index;

                aabb_reset(t->bmin, t->bmax);
                aabb_add_point(t->bmin, t->bmax, t->v0);
                aabb_add_point(t->bmin, t->bmax, t->v1);
                aabb_add_point(t->bmin, t->bmax, t->v2);
                for (int k = 0; k < 3; ++k) {
                    t->centroid[k] = (t->v0[k] + t->v1[k] + t->v2[k]) / 3.0f;
                }
            }
        }
    }
    cgltf_free(data);

    if (tris.count == 0) tool_fatalf("aucun triangle trouvé dans la scène");
    tool_infof("%zu triangles collectés", (size_t)tris.count);

    /* --- construction --- */
    builder b;
    memset(&b, 0, sizeof b);
    b.tris      = (build_tri *)tris.data;
    b.tri_count = (uint32_t)tris.count;
    b.indices   = (uint32_t *)malloc(sizeof(uint32_t) * b.tri_count);
    if (!b.indices) tool_fatalf("mémoire épuisée (permutation)");
    for (uint32_t i = 0; i < b.tri_count; ++i) b.indices[i] = i;

    /* Un arbre binaire dont chaque feuille contient au moins un triangle a au
     * plus 2N-1 nœuds. On alloue cette borne une fois. */
    b.node_capacity = b.tri_count * 2u + 1u;
    b.nodes = (ns_bvh_node *)calloc(b.node_capacity, sizeof(ns_bvh_node));
    if (!b.nodes) tool_fatalf("mémoire épuisée (%u nœuds)", b.node_capacity);

    const uint32_t root = alloc_node(&b);
    build_node(&b, root, 0, b.tri_count, 0);

    tool_infof("%u nœuds, %u feuilles, profondeur max %u",
               b.node_count, b.leaf_count, b.max_depth);
    tool_infof("%.2f triangles par feuille en moyenne",
               b.leaf_count ? (double)b.tri_count / (double)b.leaf_count : 0.0);

    /* --- écriture --- */
    FILE *f = fopen(out_path, "wb");
    if (!f) tool_fatalf("écriture impossible : %s", out_path);

    ns_bvh_header hdr;
    memset(&hdr, 0, sizeof hdr);
    memcpy(hdr.magic, NS_BVH_MAGIC, 8);
    hdr.version        = NS_BVH_VERSION;
    hdr.node_count     = b.node_count;
    hdr.tri_count      = b.tri_count;
    hdr.material_count = (uint32_t)materials.count;
    hdr.max_depth      = b.max_depth;
    memcpy(hdr.bmin, b.nodes[root].bmin, sizeof hdr.bmin);
    memcpy(hdr.bmax, b.nodes[root].bmax, sizeof hdr.bmax);
    fwrite(&hdr, sizeof hdr, 1, f);

    fwrite(b.nodes, sizeof(ns_bvh_node), b.node_count, f);

    /* Les triangles sont écrits dans l'ordre de la permutation : les feuilles
     * référencent alors des plages contiguës, donc une seule ligne de cache par
     * feuille au lieu de N accès dispersés. */
    for (uint32_t i = 0; i < b.tri_count; ++i) {
        const build_tri *t = &b.tris[b.indices[i]];
        ns_bvh_tri out;
        memset(&out, 0, sizeof out);
        memcpy(out.v0, t->v0, sizeof out.v0);
        for (int k = 0; k < 3; ++k) {
            out.e1[k] = t->v1[k] - t->v0[k];
            out.e2[k] = t->v2[k] - t->v0[k];
        }
        /* Normale géométrique : produit vectoriel des arêtes, normalisé. */
        const float nx = out.e1[1] * out.e2[2] - out.e1[2] * out.e2[1];
        const float ny = out.e1[2] * out.e2[0] - out.e1[0] * out.e2[2];
        const float nz = out.e1[0] * out.e2[1] - out.e1[1] * out.e2[0];
        const float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12f) {
            out.normal[0] = nx / len; out.normal[1] = ny / len; out.normal[2] = nz / len;
        } else {
            out.normal[1] = 1.0f;                    /* triangle dégénéré résiduel */
        }
        out.material = t->material;

        if (t->material < materials.count) {
            const ns_bvh_material *m = &TOOL_VEC_AT(&materials, ns_bvh_material, t->material);
            if (m->emissive[0] + m->emissive[1] + m->emissive[2] > 0.01f) out.flags |= NS_BVH_TRI_EMISSIVE;
            if (m->roughness < 0.15f && m->metallic > 0.5f)               out.flags |= NS_BVH_TRI_MIRROR;
        }
        fwrite(&out, sizeof out, 1, f);
    }

    fwrite(materials.data, sizeof(ns_bvh_material), materials.count, f);
    const long total = ftell(f);
    fclose(f);

    tool_infof("écrit : %s (%.2f Mio)", out_path, (double)total / (1024.0 * 1024.0));

    free(b.indices);
    free(b.nodes);
    tool_vec_free(&tris);
    tool_vec_free(&materials);
    return 0;
}
