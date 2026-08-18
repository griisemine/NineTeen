/* geo_import.c — voir geo_import.h pour le raisonnement. */
#include "geo_import.h"

/*
 * `tools_common.h` et NON `tool_json.h` : ce dernier inclut le jsmn vendoré, et
 * cgltf embarque sa PROPRE copie de jsmn. Les deux dans la même unité de
 * compilation, ce sont douze redéclarations d'énumérateurs et un message qui ne
 * dit pas d'où vient le conflit. On n'a besoin ici que des fonctions de
 * journalisation, qui vivent dans `tools_common.h`.
 */
#include "tools_common.h"

/* L'implementation vit ICI pour `roomgen` : `bvhbake` a la sienne, mais les
 * deux outils sont des binaires distincts, donc pas de double definition. */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

static void mat_mul_point(const cgltf_float m[16], const float in[3], float out[3])
{
    /* cgltf rend une matrice colonne-majeure, comme glTF : m[0..3] est la
     * première COLONNE. */
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8]  * in[2] + m[12];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9]  * in[2] + m[13];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14];
}

/*
 * La normale se transforme par la TRANSPOSÉE DE L'INVERSE, pas par la matrice.
 *
 * Avec une échelle uniforme les deux ne diffèrent que d'un facteur, que la
 * normalisation efface — donc on pourrait s'en passer ici, puisque l'échelle
 * uniforme est exigée. On ne s'en passe pas : le jour où un modèle arrivera avec
 * un nœud interne à échelle non uniforme (ce que le glTF autorise et que la
 * contrainte de CE fichier ne couvre pas, elle ne porte que sur l'instance),
 * l'éclairage serait faux sans qu'aucune vérification ne bronche.
 */
static void normal_matrix(const cgltf_float m[16], float out[9])
{
    const float a = m[0], b = m[4], c = m[8];
    const float d = m[1], e = m[5], f = m[9];
    const float g = m[2], h = m[6], i = m[10];

    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) {
        /* Nœud dégénéré : on rend l'identité plutôt que des NaN. Le triangle
         * sera de toute façon dégénéré et écarté plus loin. */
        out[0] = 1; out[1] = 0; out[2] = 0;
        out[3] = 0; out[4] = 1; out[5] = 0;
        out[6] = 0; out[7] = 0; out[8] = 1;
        return;
    }
    const float inv = 1.0f / det;
    /* Transposée de l'inverse = comatrice / det. */
    out[0] = (e * i - f * h) * inv;
    out[1] = (c * h - b * i) * inv;
    out[2] = (b * f - c * e) * inv;
    out[3] = (f * g - d * i) * inv;
    out[4] = (a * i - c * g) * inv;
    out[5] = (c * d - a * f) * inv;
    out[6] = (d * h - e * g) * inv;
    out[7] = (b * g - a * h) * inv;
    out[8] = (a * e - b * d) * inv;
    /* La transposée : la comatrice ci-dessus est déjà celle de l'inverse
     * transposé si on la lit en ligne-majeure, donc rien à retourner. */
}

static void apply3(const float m[9], const float in[3], float out[3])
{
    out[0] = m[0] * in[0] + m[1] * in[1] + m[2] * in[2];
    out[1] = m[3] * in[0] + m[4] * in[1] + m[5] * in[2];
    out[2] = m[6] * in[0] + m[7] * in[1] + m[8] * in[2];
}

static cgltf_data *open_gltf(const char *path, const char *owner)
{
    cgltf_options opt;
    memset(&opt, 0, sizeof opt);
    cgltf_data *data = NULL;

    if (cgltf_parse_file(&opt, path, &data) != cgltf_result_success) {
        tool_fatalf("« %s » : glTF illisible (%s)", owner, path);
    }
    if (cgltf_load_buffers(&opt, data, path) != cgltf_result_success) {
        cgltf_free(data);
        tool_fatalf("« %s » : tampons du glTF introuvables (%s) — le .bin est-il à côté ?",
                    owner, path);
    }
    if (cgltf_validate(data) != cgltf_result_success) {
        cgltf_free(data);
        /*
         * Le même contrôle que `bvhbake`, mais ICI — c'est-à-dire pendant qu'on
         * sait quel objet de la salle est en cause. Laissé à `bvhbake`, trois
         * étapes plus loin, le message parle d'un décalage d'octets dans un
         * fichier qui n'existait pas encore quand la faute a été commise.
         */
        tool_fatalf("« %s » : glTF invalide (%s)", owner, path);
    }
    return data;
}

/*
 * Le repère de l'instance, et le contrôle qui va avec.
 *
 * `geo_xform` porte lacet, tangage, roulis et une échelle UNIFORME. On construit
 * la matrice ici plutôt que d'appeler `geo_mesh_append` sur un maillage
 * intermédiaire : ça éviterait une copie complète du modèle, et surtout ça
 * permet de composer la transformation du nœud glTF et celle de l'instance en
 * une seule multiplication par sommet.
 */
static void instance_matrix(const geo_xform *x, float m[16], const char *owner)
{
    const float s = (x->scale > 0.0f) ? x->scale : 1.0f;
    if (s <= 0.0f) {
        tool_fatalf("« %s » : échelle %.4f — une instance miroir retournerait "
                    "l'orientation des triangles sans que rien ne le signale",
                    owner, (double)x->scale);
    }

    const float cy = cosf(x->yaw),   sy = sinf(x->yaw);
    const float cp = cosf(x->pitch), sp = sinf(x->pitch);
    const float cr = cosf(x->roll),  sr = sinf(x->roll);

    /* R = Ry * Rx * Rz, le même ordre que `geo_mesh_append`. Le vérifier plutôt
     * que le supposer : un modèle importé et une boîte posés au même lacet
     * doivent regarder dans la même direction. */
    const float r[9] = {
        cy * cr + sy * sp * sr,   -cy * sr + sy * sp * cr,   sy * cp,
        cp * sr,                   cp * cr,                 -sp,
        -sy * cr + cy * sp * sr,   sy * sr + cy * sp * cr,   cy * cp,
    };

    /* Colonne-majeure, comme cgltf. */
    m[0]  = r[0] * s; m[1]  = r[3] * s; m[2]  = r[6] * s; m[3]  = 0.0f;
    m[4]  = r[1] * s; m[5]  = r[4] * s; m[6]  = r[7] * s; m[7]  = 0.0f;
    m[8]  = r[2] * s; m[9]  = r[5] * s; m[10] = r[8] * s; m[11] = 0.0f;
    m[12] = x->origin.x; m[13] = x->origin.y; m[14] = x->origin.z; m[15] = 1.0f;
}

static void mat_mul(const float a[16], const cgltf_float b[16], float out[16])
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c * 4 + r] = a[0 * 4 + r] * (float)b[c * 4 + 0]
                           + a[1 * 4 + r] * (float)b[c * 4 + 1]
                           + a[2 * 4 + r] * (float)b[c * 4 + 2]
                           + a[3 * 4 + r] * (float)b[c * 4 + 3];
        }
    }
}

/* -------------------------------------------------------------------------- */

typedef struct import_ctx {
    geo_mesh *out;
    float     min[3], max[3];
    size_t    triangles;
    size_t    dropped;
    bool      collect_only;
} import_ctx;

/*
 * L'aire d'un triangle, pour écarter les dégénérés.
 *
 * Ce contrôle est FATAL pour la géométrie qu'on génère — un triangle d'aire
 * nulle sorti de `geo_box` est une faute dans notre code, et il faut la voir.
 * Il ne peut pas l'être pour de la géométrie IMPORTÉE : un modèle de neuf mille
 * triangles fait chez quelqu'un d'autre en contient deux qui ne valent rien, et
 * casser le build pour ça reviendrait à n'importer que des modèles parfaits,
 * c'est-à-dire aucun.
 *
 * On les écarte donc, et on DIT combien. Un triangle d'aire nulle n'apporte rien
 * à l'image — il n'a ni normale ni tangente définies — et le garder ne ferait que
 * propager des NaN dans la base tangente de ses voisins au moment de la soudure.
 */
static float tri_area2(const float a[3], const float b[3], const float c[3])
{
    const float u[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    const float v[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    const float n[3] = { u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0] };
    return n[0]*n[0] + n[1]*n[1] + n[2]*n[2];
}

static void bounds_add(import_ctx *c, const float p[3])
{
    for (int k = 0; k < 3; ++k) {
        if (p[k] < c->min[k]) c->min[k] = p[k];
        if (p[k] > c->max[k]) c->max[k] = p[k];
    }
}

static void walk(cgltf_data *data, import_ctx *c, const float inst[16],
                 int32_t material, const int32_t *by_index, size_t by_index_count,
                 const char *owner, const char *path)
{
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;

        cgltf_float local[16];
        cgltf_node_transform_world(node, local);
        float world[16];
        mat_mul(inst, local, world);
        float nm[9];
        normal_matrix(world, nm);

        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive *prim = &node->mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;
            if (!prim->indices) continue;

            const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                const cgltf_attribute *at = &prim->attributes[a];
                if (at->type == cgltf_attribute_type_position) pos = at->data;
                else if (at->type == cgltf_attribute_type_normal) nrm = at->data;
                else if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uv = at->data;
            }
            if (!pos) continue;

            int32_t mat = material;
            if (by_index && prim->material) {
                const size_t mi = (size_t)cgltf_material_index(data, prim->material);
                if (mi < by_index_count && by_index[mi] >= 0) mat = by_index[mi];
            }

            /*
             * **On réutilise l'indexation du glTF au lieu d'éclater les
             * triangles.**
             *
             * Le premier jet poussait trois sommets neufs par triangle : un
             * modèle de quatorze mille triangles entrait avec quarante-deux
             * mille sommets au lieu de huit mille, et quatre meubles suffisaient
             * à tripler le poids de la salle. Ce n'était pas un détail de
             * performance — c'était la raison pour laquelle la salle cessait de
             * s'afficher au-delà d'une certaine taille.
             *
             * La table associe l'indice source à l'indice produit, par
             * primitive : deux primitives ne partagent pas leurs sommets, elles
             * n'ont ni le même matériau ni forcément les mêmes attributs.
             */
            const cgltf_size vcount = pos->count;
            uint32_t *remap = (uint32_t *)malloc(sizeof(uint32_t) * vcount);
            if (!remap) tool_fatalf("« %s » : mémoire (%zu sommets)", owner, (size_t)vcount);
            for (cgltf_size v = 0; v < vcount; ++v) remap[v] = UINT32_MAX;

            const cgltf_size n = prim->indices->count;
            for (cgltf_size i = 0; i + 2 < n; i += 3) {
                cgltf_size vi[3];
                float wp[3][3], wn[3][3], uvs[3][2];
                bool ok = true;

                for (int k = 0; k < 3; ++k) {
                    vi[k] = cgltf_accessor_read_index(prim->indices, i + (cgltf_size)k);
                    float lp[3] = {0};
                    if (!cgltf_accessor_read_float(pos, vi[k], lp, 3)) { ok = false; break; }
                    mat_mul_point(world, lp, wp[k]);

                    float ln[3] = { 0.0f, 1.0f, 0.0f };
                    if (nrm) (void)cgltf_accessor_read_float(nrm, vi[k], ln, 3);
                    apply3(nm, ln, wn[k]);
                    const float len = sqrtf(wn[k][0]*wn[k][0] + wn[k][1]*wn[k][1]
                                          + wn[k][2]*wn[k][2]);
                    if (len > 1e-8f) { wn[k][0] /= len; wn[k][1] /= len; wn[k][2] /= len; }
                    else { wn[k][0] = 0.0f; wn[k][1] = 1.0f; wn[k][2] = 0.0f; }

                    uvs[k][0] = uvs[k][1] = 0.0f;
                    if (uv) (void)cgltf_accessor_read_float(uv, vi[k], uvs[k], 2);
                }
                if (!ok) break;

                /* Le seuil est le carré d'une aire : 1e-14 correspond à un
                 * triangle d'environ un dixième de micromètre de côté. */
                if (tri_area2(wp[0], wp[1], wp[2]) < 1e-14f) { c->dropped++; continue; }

                for (int k = 0; k < 3; ++k) bounds_add(c, wp[k]);
                c->triangles++;
                if (c->collect_only) continue;

                uint32_t idx[3];
                for (int k = 0; k < 3; ++k) {
                    if (vi[k] < vcount && remap[vi[k]] != UINT32_MAX) {
                        idx[k] = remap[vi[k]];
                        continue;
                    }
                    idx[k] = geo_mesh_push_vertex(c->out,
                                                  ns_v3_make(wp[k][0], wp[k][1], wp[k][2]),
                                                  ns_v3_make(wn[k][0], wn[k][1], wn[k][2]),
                                                  uvs[k][0], uvs[k][1]);
                    if (vi[k] < vcount) remap[vi[k]] = idx[k];
                }
                geo_mesh_push_tri(c->out, idx[0], idx[1], idx[2], mat);
            }
            free(remap);
        }
    }

    if (c->triangles == 0) {
        tool_fatalf("« %s » : aucun triangle dans %s — le modèle est-il bien "
                    "un maillage, et pas une scène vide ?", owner, path);
    }
    if (c->dropped && !c->collect_only) {
        tool_infof("« %s » : %zu triangle(s) dégénéré(s) écarté(s) sur %zu",
                   owner, c->dropped, c->triangles + c->dropped);
    }
}

/* -------------------------------------------------------------------------- */

size_t geo_import_gltf(geo_mesh *out, const char *path, const geo_xform *x,
                       int32_t material, const int32_t *material_by_index,
                       size_t material_by_index_count, const char *owner)
{
    float inst[16];
    instance_matrix(x, inst, owner);

    cgltf_data *data = open_gltf(path, owner);

    import_ctx c;
    memset(&c, 0, sizeof c);
    c.out = out;
    c.min[0] = c.min[1] = c.min[2] =  1e30f;
    c.max[0] = c.max[1] = c.max[2] = -1e30f;

    walk(data, &c, inst, material, material_by_index, material_by_index_count, owner, path);
    cgltf_free(data);
    return c.triangles;
}

void geo_import_bounds(const char *path, const geo_xform *x,
                       float out_min[3], float out_max[3], const char *owner)
{
    float inst[16];
    instance_matrix(x, inst, owner);

    cgltf_data *data = open_gltf(path, owner);

    import_ctx c;
    memset(&c, 0, sizeof c);
    c.collect_only = true;
    c.min[0] = c.min[1] = c.min[2] =  1e30f;
    c.max[0] = c.max[1] = c.max[2] = -1e30f;

    walk(data, &c, inst, -1, NULL, 0, owner, path);
    cgltf_free(data);

    memcpy(out_min, c.min, sizeof c.min);
    memcpy(out_max, c.max, sizeof c.max);
}
