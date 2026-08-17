/* geo_mesh.c — tampon de maillage et finition. Voir geo_mesh.h pour les contrats. */
#include "geo_mesh.h"

#include <math.h>

/* ========================================================================== */
/* Construction                                                               */
/* ========================================================================== */

void geo_mesh_init(geo_mesh *m)
{
    tool_vec_init(&m->verts, sizeof(gltf_vertex));
    tool_vec_init(&m->tris, sizeof(geo_tri));
}

void geo_mesh_free(geo_mesh *m)
{
    tool_vec_free(&m->verts);
    tool_vec_free(&m->tris);
}

void geo_mesh_reserve(geo_mesh *m, size_t verts, size_t tris)
{
    if (verts) tool_vec_reserve(&m->verts, m->verts.count + verts);
    if (tris)  tool_vec_reserve(&m->tris, m->tris.count + tris);
}

size_t geo_mesh_vertex_count(const geo_mesh *m) { return m->verts.count; }
size_t geo_mesh_tri_count(const geo_mesh *m)    { return m->tris.count; }

uint32_t geo_mesh_push_vertex(geo_mesh *m, ns_v3 position, ns_v3 normal, float u, float v)
{
    gltf_vertex *out = (gltf_vertex *)tool_vec_push(&m->verts);
    memset(out, 0, sizeof *out);
    out->position[0] = position.x;
    out->position[1] = position.y;
    out->position[2] = position.z;
    out->normal[0] = normal.x;
    out->normal[1] = normal.y;
    out->normal[2] = normal.z;
    out->uv[0] = u;
    out->uv[1] = v;
    /* Tangente provisoire : `geo_generate_tangents` la remplace. */
    out->tangent[0] = 1.0f;
    out->tangent[3] = 1.0f;
    return (uint32_t)(m->verts.count - 1);
}

void geo_mesh_push_tri(geo_mesh *m, uint32_t a, uint32_t b, uint32_t c, int32_t material)
{
    geo_tri *t = (geo_tri *)tool_vec_push(&m->tris);
    t->i[0] = a; t->i[1] = b; t->i[2] = c;
    t->material = material;
}

void geo_mesh_push_quad(geo_mesh *m, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                        int32_t material)
{
    geo_mesh_push_tri(m, a, b, c, material);
    geo_mesh_push_tri(m, a, c, d, material);
}

/* ========================================================================== */
/* Instanciation                                                              */
/* ========================================================================== */

/* Rotation lacet-tangage-roulis, appliquée dans cet ordre (Y, puis X, puis Z),
 * cohérente avec la convention main droite / Y vers le haut du moteur. */
static ns_m4 xform_matrix(const geo_xform *x)
{
    const float s = (x->scale > 0.0f) ? x->scale : 1.0f;

    const float cy = cosf(x->yaw),   sy = sinf(x->yaw);
    const float cp = cosf(x->pitch), sp = sinf(x->pitch);
    const float cr = cosf(x->roll),  sr = sinf(x->roll);

    /* R = Ry * Rx * Rz */
    ns_m4 r = ns_m4_identity();
    r.m[0][0] = (cy * cr + sy * sp * sr) * s;
    r.m[0][1] = (cp * sr) * s;
    r.m[0][2] = (-sy * cr + cy * sp * sr) * s;

    r.m[1][0] = (-cy * sr + sy * sp * cr) * s;
    r.m[1][1] = (cp * cr) * s;
    r.m[1][2] = (sy * sr + cy * sp * cr) * s;

    r.m[2][0] = (sy * cp) * s;
    r.m[2][1] = (-sp) * s;
    r.m[2][2] = (cy * cp) * s;

    r.m[3][0] = x->origin.x;
    r.m[3][1] = x->origin.y;
    r.m[3][2] = x->origin.z;
    return r;
}

void geo_mesh_append(geo_mesh *dst, const geo_mesh *src, const geo_xform *x,
                     int32_t material_override)
{
    if (src->verts.count == 0) return;

    const geo_xform id = GEO_XFORM_IDENTITY;
    if (!x) x = &id;
    if (x->scale < 0.0f) {
        /* Voir geo_mesh.h : une échelle négative inverserait la chiralité des
         * tangentes sans que le rendu ne le signale. On refuse plutôt que de
         * produire un objet dont seules les normal maps seraient fausses. */
        tool_fatalf("échelle négative (%.3f) : les instances miroir sont interdites, "
                    "passer un paramètre au générateur", (double)x->scale);
    }

    const ns_m4 mat = xform_matrix(x);
    const uint32_t base = (uint32_t)dst->verts.count;

    geo_mesh_reserve(dst, src->verts.count, src->tris.count);

    for (size_t i = 0; i < src->verts.count; ++i) {
        const gltf_vertex *sv = &TOOL_VEC_AT(&src->verts, gltf_vertex, i);
        gltf_vertex *dv = (gltf_vertex *)tool_vec_push(&dst->verts);
        *dv = *sv;

        const float *p = sv->position;
        dv->position[0] = mat.m[0][0]*p[0] + mat.m[1][0]*p[1] + mat.m[2][0]*p[2] + mat.m[3][0];
        dv->position[1] = mat.m[0][1]*p[0] + mat.m[1][1]*p[1] + mat.m[2][1]*p[2] + mat.m[3][1];
        dv->position[2] = mat.m[0][2]*p[0] + mat.m[1][2]*p[1] + mat.m[2][2]*p[2] + mat.m[3][2];

        /* Rotation seule pour la normale, et renormalisation : l'échelle étant
         * uniforme, la transposée de l'inverse est inutile. */
        const float *n = sv->normal;
        float nx = mat.m[0][0]*n[0] + mat.m[1][0]*n[1] + mat.m[2][0]*n[2];
        float ny = mat.m[0][1]*n[0] + mat.m[1][1]*n[1] + mat.m[2][1]*n[2];
        float nz = mat.m[0][2]*n[0] + mat.m[1][2]*n[1] + mat.m[2][2]*n[2];
        const float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
        dv->normal[0] = nx; dv->normal[1] = ny; dv->normal[2] = nz;
    }

    for (size_t i = 0; i < src->tris.count; ++i) {
        const geo_tri *st = &TOOL_VEC_AT(&src->tris, geo_tri, i);
        geo_tri *dt = (geo_tri *)tool_vec_push(&dst->tris);
        dt->i[0] = st->i[0] + base;
        dt->i[1] = st->i[1] + base;
        dt->i[2] = st->i[2] + base;
        dt->material = (material_override >= 0) ? material_override : st->material;
    }
}

ns_aabb geo_mesh_bounds(const geo_mesh *m)
{
    ns_aabb b = ns_aabb_empty();
    for (size_t i = 0; i < m->verts.count; ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&m->verts, gltf_vertex, i);
        b = ns_aabb_add_point(b, ns_v3_make(v->position[0], v->position[1], v->position[2]));
    }
    return b;
}

/* ========================================================================== */
/* Soudure                                                                    */
/* ========================================================================== */

/*
 * Soudure, avec ou sans prise en compte de la normale.
 *
 * Deux usages, et la différence compte :
 *
 *   - **avant** le calcul des normales, on fusionne sur position + UV seulement.
 *     Les normales sont encore celles que le générateur a posées par face ; les
 *     comparer empêcherait la fusion aux arêtes, et le lissage ne verrait jamais
 *     les faces voisines d'un sommet.
 *   - **après**, il faut au contraire les comparer : le lissage a délibérément
 *     dupliqué les sommets dont le voisinage diffère, et les refusionner
 *     effacerait le chanfrein qu'on vient de préserver.
 */
static void weld_impl(geo_mesh *m, float epsilon, bool compare_normals)
{
    const size_t n = m->verts.count;
    if (n == 0) return;
    if (epsilon <= 0.0f) epsilon = 1e-4f;

    /* Hachage spatial sur une grille de côté `epsilon` : deux sommets à fusionner
     * tombent dans la même cellule ou dans une voisine, donc on ne compare qu'un
     * voisinage de 3×3×3 au lieu de tout le maillage. */
    const float inv = 1.0f / epsilon;
    size_t buckets = 1;
    while (buckets < n * 2) buckets <<= 1;

    int32_t *heads = (int32_t *)malloc(sizeof(int32_t) * buckets);
    int32_t *next  = (int32_t *)malloc(sizeof(int32_t) * n);
    uint32_t *remap = (uint32_t *)malloc(sizeof(uint32_t) * n);
    if (!heads || !next || !remap) tool_fatalf("mémoire épuisée (soudure)");
    for (size_t i = 0; i < buckets; ++i) heads[i] = -1;

    tool_vec kept; tool_vec_init(&kept, sizeof(gltf_vertex));
    tool_vec_reserve(&kept, n);

    for (size_t i = 0; i < n; ++i) {
        const gltf_vertex *v = &TOOL_VEC_AT(&m->verts, gltf_vertex, i);
        const int32_t gx = (int32_t)floorf(v->position[0] * inv);
        const int32_t gy = (int32_t)floorf(v->position[1] * inv);
        const int32_t gz = (int32_t)floorf(v->position[2] * inv);

        int32_t found = -1;
        for (int dz = -1; dz <= 1 && found < 0; ++dz) {
            for (int dy = -1; dy <= 1 && found < 0; ++dy) {
                for (int dx = -1; dx <= 1 && found < 0; ++dx) {
                    const uint32_t h = (uint32_t)((gx + dx) * 73856093)
                                     ^ (uint32_t)((gy + dy) * 19349663)
                                     ^ (uint32_t)((gz + dz) * 83492791);
                    for (int32_t c = heads[h & (buckets - 1)]; c >= 0; c = next[c]) {
                        const gltf_vertex *o = &TOOL_VEC_AT(&kept, gltf_vertex, (size_t)remap[c]);
                        if (fabsf(o->position[0] - v->position[0]) > epsilon) continue;
                        if (fabsf(o->position[1] - v->position[1]) > epsilon) continue;
                        if (fabsf(o->position[2] - v->position[2]) > epsilon) continue;
                        /* Les UV doivent coïncider aussi : souder deux sommets qui
                         * partagent un coin mais pas leur coordonnée de texture
                         * coudrait ensemble deux morceaux d'atlas. */
                        if (fabsf(o->uv[0] - v->uv[0]) > 1e-5f) continue;
                        if (fabsf(o->uv[1] - v->uv[1]) > 1e-5f) continue;
                        if (compare_normals) {
                            /* 0.9999 ≈ 0,8° : assez serré pour préserver un
                             * chanfrein, assez lâche pour absorber l'erreur
                             * d'arrondi de la renormalisation. */
                            const float d = o->normal[0] * v->normal[0]
                                          + o->normal[1] * v->normal[1]
                                          + o->normal[2] * v->normal[2];
                            if (d < 0.9999f) continue;
                        }
                        found = c;
                        break;
                    }
                }
            }
        }

        if (found >= 0) {
            remap[i] = remap[found];
        } else {
            gltf_vertex *dst = (gltf_vertex *)tool_vec_push(&kept);
            *dst = *v;
            remap[i] = (uint32_t)(kept.count - 1);
        }

        const uint32_t h = (uint32_t)(gx * 73856093) ^ (uint32_t)(gy * 19349663)
                         ^ (uint32_t)(gz * 83492791);
        next[i] = heads[h & (buckets - 1)];
        heads[h & (buckets - 1)] = (int32_t)i;
    }

    for (size_t i = 0; i < m->tris.count; ++i) {
        geo_tri *t = &TOOL_VEC_AT(&m->tris, geo_tri, i);
        t->i[0] = remap[t->i[0]];
        t->i[1] = remap[t->i[1]];
        t->i[2] = remap[t->i[2]];
    }

    tool_vec_free(&m->verts);
    m->verts = kept;

    free(heads); free(next); free(remap);
}

void geo_weld(geo_mesh *m, float epsilon)
{
    weld_impl(m, epsilon, false);
}

/* ========================================================================== */
/* Normales                                                                   */
/* ========================================================================== */

void geo_smooth_normals(geo_mesh *m, float angle_degrees)
{
    const size_t vcount = m->verts.count;
    const size_t tcount = m->tris.count;
    if (vcount == 0 || tcount == 0) return;

    const float cos_limit = cosf(angle_degrees * NS_DEG2RAD);

    /* Normale de face, non normalisée : sa longueur vaut deux fois l'aire, ce qui
     * donne la pondération par l'aire sans calcul supplémentaire. */
    ns_v3 *face_n = (ns_v3 *)malloc(sizeof(ns_v3) * tcount);
    if (!face_n) tool_fatalf("mémoire épuisée (normales)");

    for (size_t t = 0; t < tcount; ++t) {
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
        face_n[t] = ns_v3_cross(e1, e2);
    }

    /* Listes d'incidence sommet -> faces, en deux passes (compter, puis remplir) :
     * une allocation au lieu d'une par sommet. */
    uint32_t *start = (uint32_t *)calloc(vcount + 1, sizeof(uint32_t));
    if (!start) tool_fatalf("mémoire épuisée (incidences)");
    for (size_t t = 0; t < tcount; ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        for (int k = 0; k < 3; ++k) start[tri->i[k] + 1]++;
    }
    for (size_t i = 0; i < vcount; ++i) start[i + 1] += start[i];

    const uint32_t total = start[vcount];
    uint32_t *incident = (uint32_t *)malloc(sizeof(uint32_t) * (total ? total : 1));
    uint32_t *cursor = (uint32_t *)malloc(sizeof(uint32_t) * (vcount + 1));
    if (!incident || !cursor) tool_fatalf("mémoire épuisée (incidences)");
    memcpy(cursor, start, sizeof(uint32_t) * (vcount + 1));
    for (size_t t = 0; t < tcount; ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        for (int k = 0; k < 3; ++k) incident[cursor[tri->i[k]]++] = (uint32_t)t;
    }

    /*
     * Pour chaque coin de triangle, on somme les faces incidentes au sommet dont
     * la normale est à moins de `angle_degrees` de celle de la face courante. Un
     * coin dont le voisinage diffère obtient donc sa propre normale, ce qui
     * duplique le sommet — c'est ce qui garde un chanfrein net.
     */
    tool_vec out_verts; tool_vec_init(&out_verts, sizeof(gltf_vertex));
    tool_vec_reserve(&out_verts, vcount);

    for (size_t t = 0; t < tcount; ++t) {
        geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        const ns_v3 fn = ns_v3_norm(face_n[t]);

        for (int k = 0; k < 3; ++k) {
            const uint32_t vi = tri->i[k];
            ns_v3 acc = ns_v3_zero();
            for (uint32_t e = start[vi]; e < start[vi + 1]; ++e) {
                const uint32_t other = incident[e];
                const ns_v3 on = ns_v3_norm(face_n[other]);
                if (ns_v3_dot(fn, on) >= cos_limit) acc = ns_v3_add(acc, face_n[other]);
            }
            if (ns_v3_len_sq(acc) < 1e-20f) acc = face_n[t];

            const ns_v3 n = ns_v3_norm(acc);
            const gltf_vertex *src = &TOOL_VEC_AT(&m->verts, gltf_vertex, vi);
            gltf_vertex *dst = (gltf_vertex *)tool_vec_push(&out_verts);
            *dst = *src;
            dst->normal[0] = n.x; dst->normal[1] = n.y; dst->normal[2] = n.z;
            tri->i[k] = (uint32_t)(out_verts.count - 1);
        }
    }

    tool_vec_free(&m->verts);
    m->verts = out_verts;

    free(face_n); free(start); free(incident); free(cursor);

    /* La duplication systématique ci-dessus produit trois sommets par triangle ;
     * une soudure les refusionne partout où position, UV **et normale**
     * coïncident — sans la normale, on effacerait le lissage qu'on vient de
     * calculer. */
    weld_impl(m, 1e-4f, true);
}

/* ========================================================================== */
/* Tangentes                                                                  */
/* ========================================================================== */

void geo_generate_tangents(geo_mesh *m)
{
    const size_t vcount = m->verts.count;
    if (vcount == 0) return;

    ns_v3 *tan = (ns_v3 *)calloc(vcount, sizeof(ns_v3));
    ns_v3 *bit = (ns_v3 *)calloc(vcount, sizeof(ns_v3));
    if (!tan || !bit) tool_fatalf("mémoire épuisée (tangentes)");

    for (size_t t = 0; t < m->tris.count; ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        const gltf_vertex *a = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[0]);
        const gltf_vertex *b = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[1]);
        const gltf_vertex *c = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[2]);

        const float x1 = b->position[0] - a->position[0];
        const float y1 = b->position[1] - a->position[1];
        const float z1 = b->position[2] - a->position[2];
        const float x2 = c->position[0] - a->position[0];
        const float y2 = c->position[1] - a->position[1];
        const float z2 = c->position[2] - a->position[2];

        const float s1 = b->uv[0] - a->uv[0], t1 = b->uv[1] - a->uv[1];
        const float s2 = c->uv[0] - a->uv[0], t2 = c->uv[1] - a->uv[1];

        const float det = s1 * t2 - s2 * t1;
        if (fabsf(det) < 1e-12f) continue;    /* UV dégénérées : on saute */
        const float r = 1.0f / det;

        const ns_v3 sdir = ns_v3_make((t2 * x1 - t1 * x2) * r,
                                      (t2 * y1 - t1 * y2) * r,
                                      (t2 * z1 - t1 * z2) * r);
        const ns_v3 tdir = ns_v3_make((s1 * x2 - s2 * x1) * r,
                                      (s1 * y2 - s2 * y1) * r,
                                      (s1 * z2 - s2 * z1) * r);
        for (int k = 0; k < 3; ++k) {
            tan[tri->i[k]] = ns_v3_add(tan[tri->i[k]], sdir);
            bit[tri->i[k]] = ns_v3_add(bit[tri->i[k]], tdir);
        }
    }

    for (size_t i = 0; i < vcount; ++i) {
        gltf_vertex *v = &TOOL_VEC_AT(&m->verts, gltf_vertex, i);
        const ns_v3 n = ns_v3_make(v->normal[0], v->normal[1], v->normal[2]);
        ns_v3 t = tan[i];

        /* Gram-Schmidt : ne garder que la part de la tangente orthogonale à la
         * normale. */
        t = ns_v3_sub(t, ns_v3_scale(n, ns_v3_dot(n, t)));
        if (ns_v3_len_sq(t) < 1e-16f) {
            /* Aucune information d'UV exploitable : n'importe quelle direction
             * orthogonale fait l'affaire, il suffit qu'elle soit cohérente. */
            const ns_v3 axis = (fabsf(n.y) < 0.99f) ? ns_v3_make(0, 1, 0) : ns_v3_make(1, 0, 0);
            t = ns_v3_cross(axis, n);
            if (ns_v3_len_sq(t) < 1e-16f) t = ns_v3_make(1, 0, 0);
        }
        t = ns_v3_norm(t);

        /* w porte la chiralité : glTF s'en sert pour reconstruire la binormale. */
        const float w = (ns_v3_dot(ns_v3_cross(n, t), bit[i]) < 0.0f) ? -1.0f : 1.0f;
        v->tangent[0] = t.x; v->tangent[1] = t.y; v->tangent[2] = t.z; v->tangent[3] = w;
    }

    free(tan); free(bit);
}

/* ========================================================================== */
/* Contrôles                                                                  */
/* ========================================================================== */

int geo_check_degenerate(const geo_mesh *m, float min_area)
{
    int bad = 0;
    for (size_t t = 0; t < m->tris.count; ++t) {
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
        if (0.5f * ns_v3_len(ns_v3_cross(e1, e2)) < min_area) bad++;
    }
    return bad;
}

float geo_signed_volume(const geo_mesh *m)
{
    double vol = 0.0;
    for (size_t t = 0; t < m->tris.count; ++t) {
        const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
        const gltf_vertex *a = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[0]);
        const gltf_vertex *b = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[1]);
        const gltf_vertex *c = &TOOL_VEC_AT(&m->verts, gltf_vertex, tri->i[2]);
        /* Somme des tétraèdres (origine, a, b, c). */
        vol += ((double)a->position[0] * ((double)b->position[1] * c->position[2]
                                        - (double)b->position[2] * c->position[1])
              - (double)a->position[1] * ((double)b->position[0] * c->position[2]
                                        - (double)b->position[2] * c->position[0])
              + (double)a->position[2] * ((double)b->position[0] * c->position[1]
                                        - (double)b->position[1] * c->position[0])) / 6.0;
    }
    return (float)vol;
}

bool geo_uv_density_range(const geo_mesh *m, float *out_min, float *out_max)
{
    float lo = 1e30f, hi = 0.0f;
    bool any = false;

    for (size_t t = 0; t < m->tris.count; ++t) {
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
        const float area = 0.5f * ns_v3_len(ns_v3_cross(e1, e2));
        if (area < 1e-9f) continue;

        const float s1 = b->uv[0] - a->uv[0], t1 = b->uv[1] - a->uv[1];
        const float s2 = c->uv[0] - a->uv[0], t2 = c->uv[1] - a->uv[1];
        const float uv_area = 0.5f * fabsf(s1 * t2 - s2 * t1);
        if (uv_area < 1e-12f) continue;

        /* Répétitions de texture par mètre : racine du rapport des aires. */
        const float density = sqrtf(uv_area / area);
        if (density < lo) lo = density;
        if (density > hi) hi = density;
        any = true;
    }

    if (!any) return false;
    if (out_min) *out_min = lo;
    if (out_max) *out_max = hi;
    return true;
}

/* ========================================================================== */
/* Conversion en primitives glTF                                              */
/* ========================================================================== */

void geo_build_primitives(const geo_mesh *m, geo_primitives *out)
{
    memset(out, 0, sizeof *out);
    if (m->tris.count == 0) return;

    /* Matériaux rencontrés, dans l'ordre d'apparition. */
    tool_vec mats; tool_vec_init(&mats, sizeof(int32_t));
    for (size_t t = 0; t < m->tris.count; ++t) {
        const int32_t mat = TOOL_VEC_AT(&m->tris, geo_tri, t).material;
        bool seen = false;
        for (size_t k = 0; k < mats.count && !seen; ++k) {
            seen = (TOOL_VEC_AT(&mats, int32_t, k) == mat);
        }
        if (!seen) *(int32_t *)tool_vec_push(&mats) = mat;
    }

    out->count   = mats.count;
    out->prims   = (gltf_primitive *)calloc(mats.count, sizeof(gltf_primitive));
    out->storage = (uint32_t *)malloc(sizeof(uint32_t) * m->tris.count * 3);
    if (!out->prims || !out->storage) tool_fatalf("mémoire épuisée (primitives)");

    size_t cursor = 0;
    for (size_t k = 0; k < mats.count; ++k) {
        const int32_t mat = TOOL_VEC_AT(&mats, int32_t, k);
        const size_t first = cursor;
        for (size_t t = 0; t < m->tris.count; ++t) {
            const geo_tri *tri = &TOOL_VEC_AT(&m->tris, geo_tri, t);
            if (tri->material != mat) continue;
            out->storage[cursor++] = tri->i[0];
            out->storage[cursor++] = tri->i[1];
            out->storage[cursor++] = tri->i[2];
        }
        out->prims[k].indices     = &out->storage[first];
        out->prims[k].index_count = cursor - first;
        out->prims[k].material    = mat;
    }

    tool_vec_free(&mats);
}

void geo_primitives_free(geo_primitives *p)
{
    free(p->prims);
    free(p->storage);
    memset(p, 0, sizeof *p);
}
