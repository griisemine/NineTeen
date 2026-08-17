/*
 * gltf_write.c — sérialisation glTF 2.0 + .bin.
 *
 * Le JSON est écrit à la main plutôt que par une bibliothèque : le document a
 * une forme fixe et connue, il n'y a rien à décider à l'exécution, et une
 * dépendance de plus dans la chaîne de build se paierait à chaque plateforme.
 */
#include "gltf_write.h"
#include "tools_common.h"

/* ========================================================================== */
/* Tampon binaire                                                             */
/* ========================================================================== */

typedef struct writer {
    FILE  *bin;
    size_t bin_offset;
} writer;

/* glTF exige que chaque accesseur soit aligné sur la taille de son composant.
 * Tous les nôtres sont des flottants ou des entiers 32 bits, donc 4 octets. */
static void bin_align(writer *w, size_t alignment)
{
    while (w->bin_offset % alignment) {
        fputc(0, w->bin);
        w->bin_offset++;
    }
}

static size_t bin_write(writer *w, const void *data, size_t bytes)
{
    const size_t offset = w->bin_offset;
    if (bytes && fwrite(data, 1, bytes, w->bin) != bytes) {
        tool_fatalf("écriture du .bin interrompue");
    }
    w->bin_offset += bytes;
    return offset;
}

/* ========================================================================== */
/* Écriture                                                                   */
/* ========================================================================== */

typedef struct idx_view { size_t offset, count; } idx_view;

size_t gltf_write(const gltf_scene *s, const char *out_path)
{
    const char *tex_prefix = s->texture_prefix ? s->texture_prefix : "textures/";
    const char *generator  = s->generator      ? s->generator      : "Nineteen V15";
    const char *scene_name = s->scene_name     ? s->scene_name     : "salle";

    char out_dir[512], out_base[256], bin_name[300], bin_path[1024];
    tool_dirname(out_path, out_dir, sizeof out_dir);
    tool_basename_noext(out_path, out_base, sizeof out_base);
    snprintf(bin_name, sizeof bin_name, "%s.bin", out_base);
    snprintf(bin_path, sizeof bin_path, "%s%s", out_dir, bin_name);

    writer w = { NULL, 0 };
    w.bin = fopen(bin_path, "wb");
    if (!w.bin) tool_fatalf("écriture impossible : %s", bin_path);

    /* ------------------------------------------------------------ attributs */
    /* Chacun tightly packed dans son propre bufferView : c'est ce que veut le
     * moteur, qui lie un tampon de sommets entrelacé reconstruit au chargement. */
    const size_t vcount = s->vert_count;
    float *scratch = (float *)malloc(sizeof(float) * 4 * (vcount ? vcount : 1));
    if (!scratch) tool_fatalf("mémoire épuisée (tampon d'écriture)");

    float pos_min[3] = { 1e30f, 1e30f, 1e30f }, pos_max[3] = { -1e30f, -1e30f, -1e30f };

    for (size_t i = 0; i < vcount; ++i) {
        const gltf_vertex *v = &s->verts[i];
        scratch[i * 3 + 0] = v->position[0];
        scratch[i * 3 + 1] = v->position[1];
        scratch[i * 3 + 2] = v->position[2];
        for (int k = 0; k < 3; ++k) {
            if (v->position[k] < pos_min[k]) pos_min[k] = v->position[k];
            if (v->position[k] > pos_max[k]) pos_max[k] = v->position[k];
        }
    }
    bin_align(&w, 4);
    const size_t off_pos = bin_write(&w, scratch, sizeof(float) * 3 * vcount);

    for (size_t i = 0; i < vcount; ++i) {
        const gltf_vertex *v = &s->verts[i];
        scratch[i * 3 + 0] = v->normal[0];
        scratch[i * 3 + 1] = v->normal[1];
        scratch[i * 3 + 2] = v->normal[2];
    }
    bin_align(&w, 4);
    const size_t off_nrm = bin_write(&w, scratch, sizeof(float) * 3 * vcount);

    for (size_t i = 0; i < vcount; ++i) {
        scratch[i * 2 + 0] = s->verts[i].uv[0];
        scratch[i * 2 + 1] = s->verts[i].uv[1];
    }
    bin_align(&w, 4);
    const size_t off_uv = bin_write(&w, scratch, sizeof(float) * 2 * vcount);

    for (size_t i = 0; i < vcount; ++i) {
        memcpy(&scratch[i * 4], s->verts[i].tangent, sizeof(float) * 4);
    }
    bin_align(&w, 4);
    const size_t off_tan = bin_write(&w, scratch, sizeof(float) * 4 * vcount);
    free(scratch);

    /* --------------------------------------------------------------- indices */
    /* Un bufferView par primitive, dans l'ordre des maillages : c'est cet ordre
     * qui rend contigus les lots de dessin d'un même objet côté moteur. */
    tool_vec idx_views; tool_vec_init(&idx_views, sizeof(idx_view));

    for (size_t m = 0; m < s->mesh_count; ++m) {
        const gltf_mesh *mesh = &s->meshes[m];
        for (size_t pr = 0; pr < mesh->prim_count; ++pr) {
            const gltf_primitive *p = &mesh->prims[pr];
            /*
             * Une primitive vide produirait un accesseur de longueur nulle, que
             * `cgltf_validate` rejette — et l'échec surviendrait alors dans
             * `bvhbake`, avec un décalage d'octets pour tout message. On refuse
             * ici, en nommant l'objet.
             */
            if (p->index_count == 0) {
                tool_fatalf("primitive vide dans « %s » : glTF interdit un accesseur "
                            "de longueur nulle", mesh->name);
            }
            if (p->index_count % 3 != 0) {
                tool_fatalf("« %s » : %zu indices, non multiple de 3",
                            mesh->name, p->index_count);
            }
            bin_align(&w, 4);
            idx_view *iv = (idx_view *)tool_vec_push(&idx_views);
            iv->offset = bin_write(&w, p->indices, sizeof(uint32_t) * p->index_count);
            iv->count  = p->index_count;
        }
    }
    const size_t bin_total = w.bin_offset;
    fclose(w.bin);

    /* ------------------------------------------------------------ le document */
    FILE *g = fopen(out_path, "wb");
    if (!g) tool_fatalf("écriture impossible : %s", out_path);

    fprintf(g, "{\n");
    fprintf(g, "  \"asset\": { \"version\": \"2.0\", \"generator\": \"%s\" },\n", generator);
    fprintf(g, "  \"extensionsUsed\": [\"KHR_materials_emissive_strength\"],\n");
    fprintf(g, "  \"scene\": 0,\n");

    if (s->texture_count) {
        fprintf(g, "  \"images\": [\n");
        for (size_t i = 0; i < s->texture_count; ++i) {
            fprintf(g, "    { \"uri\": \"%s%s\" }%s\n", tex_prefix, s->textures[i],
                    (i + 1 < s->texture_count) ? "," : "");
        }
        fprintf(g, "  ],\n");
        fprintf(g, "  \"samplers\": [ { \"magFilter\": 9729, \"minFilter\": 9987, "
                   "\"wrapS\": 10497, \"wrapT\": 10497 } ],\n");
        fprintf(g, "  \"textures\": [\n");
        for (size_t i = 0; i < s->texture_count; ++i) {
            fprintf(g, "    { \"source\": %zu, \"sampler\": 0 }%s\n", i,
                    (i + 1 < s->texture_count) ? "," : "");
        }
        fprintf(g, "  ],\n");
    }

    fprintf(g, "  \"materials\": [\n");
    for (size_t i = 0; i < s->material_count; ++i) {
        const gltf_material *m = &s->materials[i];
        fprintf(g, "    {\n      \"name\": \"%s\",\n", m->name);
        fprintf(g, "      \"pbrMetallicRoughness\": {\n");
        fprintf(g, "        \"baseColorFactor\": [%.6f, %.6f, %.6f, %.6f],\n",
                (double)m->base_color[0], (double)m->base_color[1],
                (double)m->base_color[2], (double)m->base_color[3]);
        if (m->texture >= 0) {
            fprintf(g, "        \"baseColorTexture\": { \"index\": %d },\n", m->texture);
        }
        fprintf(g, "        \"metallicFactor\": %.4f,\n", (double)m->metallic);
        fprintf(g, "        \"roughnessFactor\": %.4f\n", (double)m->roughness);
        fprintf(g, "      }");
        if (m->emissive_strength > 0.0f) {
            fprintf(g, ",\n      \"emissiveFactor\": [%.6f, %.6f, %.6f]",
                    (double)m->emissive[0], (double)m->emissive[1], (double)m->emissive[2]);
            fprintf(g, ",\n      \"extensions\": { \"KHR_materials_emissive_strength\": "
                       "{ \"emissiveStrength\": %.4f } }", (double)m->emissive_strength);
        }
        if (m->base_color[3] < 0.999f) {
            fprintf(g, ",\n      \"alphaMode\": \"BLEND\"");
        }
        fprintf(g, ",\n      \"doubleSided\": false\n    }%s\n",
                (i + 1 < s->material_count) ? "," : "");
    }
    fprintf(g, "  ],\n");

    fprintf(g, "  \"buffers\": [ { \"uri\": \"%s\", \"byteLength\": %zu } ],\n",
            bin_name, bin_total);
    fprintf(g, "  \"bufferViews\": [\n");
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 },\n",
            off_pos, sizeof(float) * 3 * vcount);
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 },\n",
            off_nrm, sizeof(float) * 3 * vcount);
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 },\n",
            off_uv, sizeof(float) * 2 * vcount);
    fprintf(g, "    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34962 }",
            off_tan, sizeof(float) * 4 * vcount);
    for (size_t i = 0; i < idx_views.count; ++i) {
        const idx_view *iv = &TOOL_VEC_AT(&idx_views, idx_view, i);
        fprintf(g, ",\n    { \"buffer\": 0, \"byteOffset\": %zu, \"byteLength\": %zu, \"target\": 34963 }",
                iv->offset, sizeof(uint32_t) * iv->count);
    }
    fprintf(g, "\n  ],\n");

    /* Accesseurs : 0..3 les attributs partagés, puis un par primitive. Seul
     * POSITION porte min/max, que glTF y rend obligatoire. */
    fprintf(g, "  \"accessors\": [\n");
    fprintf(g, "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC3\", "
               "\"min\": [%.6f, %.6f, %.6f], \"max\": [%.6f, %.6f, %.6f] },\n",
            vcount, (double)pos_min[0], (double)pos_min[1], (double)pos_min[2],
            (double)pos_max[0], (double)pos_max[1], (double)pos_max[2]);
    fprintf(g, "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC3\" },\n", vcount);
    fprintf(g, "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC2\" },\n", vcount);
    fprintf(g, "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": %zu, \"type\": \"VEC4\" }", vcount);
    for (size_t i = 0; i < idx_views.count; ++i) {
        const idx_view *iv = &TOOL_VEC_AT(&idx_views, idx_view, i);
        fprintf(g, ",\n    { \"bufferView\": %zu, \"componentType\": 5125, \"count\": %zu, \"type\": \"SCALAR\" }",
                4 + i, iv->count);
    }
    fprintf(g, "\n  ],\n");

    fprintf(g, "  \"meshes\": [\n");
    size_t prim_counter = 0;
    for (size_t m = 0; m < s->mesh_count; ++m) {
        const gltf_mesh *mesh = &s->meshes[m];
        if (m) fprintf(g, ",\n");
        fprintf(g, "    { \"name\": \"%s\", \"primitives\": [\n", mesh->name);
        for (size_t pr = 0; pr < mesh->prim_count; ++pr) {
            const gltf_primitive *p = &mesh->prims[pr];
            fprintf(g, "      { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, "
                       "\"TEXCOORD_0\": 2, \"TANGENT\": 3 }, \"indices\": %zu",
                    4 + prim_counter);
            if (p->material >= 0) fprintf(g, ", \"material\": %d", p->material);
            fprintf(g, ", \"mode\": 4 }%s\n", (pr + 1 < mesh->prim_count) ? "," : "");
            prim_counter++;
        }
        fprintf(g, "    ] }");
    }
    fprintf(g, "\n  ],\n");

    /* Nœuds à l'identité : la géométrie est déjà en espace monde. Voir
     * l'en-tête pour la raison — un accesseur partagé et une matrice par nœud
     * sont incompatibles. */
    fprintf(g, "  \"nodes\": [\n");
    for (size_t m = 0; m < s->mesh_count; ++m) {
        fprintf(g, "    { \"name\": \"%s\", \"mesh\": %zu }%s\n",
                s->meshes[m].name, m, (m + 1 < s->mesh_count) ? "," : "");
    }
    fprintf(g, "  ],\n");

    fprintf(g, "  \"scenes\": [ { \"name\": \"%s\", \"nodes\": [", scene_name);
    for (size_t m = 0; m < s->mesh_count; ++m) {
        fprintf(g, "%s%zu", m ? ", " : "", m);
    }
    fprintf(g, "] } ]\n}\n");
    fclose(g);

    tool_vec_free(&idx_views);
    return bin_total;
}
