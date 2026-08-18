/* geo_shapes.c — générateurs paramétriques. Voir geo_shapes.h pour les contrats. */
#include "geo_shapes.h"

#include <math.h>

/* ========================================================================== */
/* Émission de faces                                                          */
/* ========================================================================== */

/*
 * Toute la géométrie sort d'ici, ce qui concentre en un seul endroit les deux
 * choses faciles à rater : la normale de face et le sens d'enroulement.
 *
 * Un quad ou un triangle d'aire nulle n'est pas émis. Ce n'est pas une troncature
 * silencieuse — une face d'aire nulle n'est pas une surface, c'est son absence :
 * elle n'a ni normale ni tangente définies, et l'émettre ferait seulement
 * remonter un triangle dégénéré dans `geo_check_degenerate` et dans le BVH. Les
 * cas où elle apparaît (une dimension nulle, un chanfrein qui mange toute une
 * face) sont refusés en amont, par les générateurs qui les connaissent.
 */
static void tri3(geo_mesh *m, ns_v3 a, ns_v3 b, ns_v3 c,
                 ns_v2 ua, ns_v2 ub, ns_v2 uc, int32_t material)
{
    ns_v3 n = ns_v3_cross(ns_v3_sub(b, a), ns_v3_sub(c, a));
    const float len = ns_v3_len(n);
    if (len < 1e-12f) return;
    n = ns_v3_scale(n, 1.0f / len);

    const uint32_t ia = geo_mesh_push_vertex(m, a, n, ua.x, ua.y);
    const uint32_t ib = geo_mesh_push_vertex(m, b, n, ub.x, ub.y);
    const uint32_t ic = geo_mesh_push_vertex(m, c, n, uc.x, uc.y);
    geo_mesh_push_tri(m, ia, ib, ic, material);
}

static void quad3(geo_mesh *m, ns_v3 a, ns_v3 b, ns_v3 c, ns_v3 e,
                  ns_v2 ua, ns_v2 ub, ns_v2 uc, ns_v2 ue, int32_t material)
{
    ns_v3 n = ns_v3_cross(ns_v3_sub(b, a), ns_v3_sub(c, a));
    if (ns_v3_len_sq(n) < 1e-24f) n = ns_v3_cross(ns_v3_sub(c, a), ns_v3_sub(e, a));
    const float len = ns_v3_len(n);
    if (len < 1e-12f) return;
    n = ns_v3_scale(n, 1.0f / len);

    const uint32_t ia = geo_mesh_push_vertex(m, a, n, ua.x, ua.y);
    const uint32_t ib = geo_mesh_push_vertex(m, b, n, ub.x, ub.y);
    const uint32_t ic = geo_mesh_push_vertex(m, c, n, uc.x, uc.y);
    const uint32_t ie = geo_mesh_push_vertex(m, e, n, ue.x, ue.y);
    geo_mesh_push_quad(m, ia, ib, ic, ie, material);
}

/*
 * Variantes « orientées » : l'appelant dit vers où la face doit regarder, et
 * l'enroulement s'y conforme. Poser la question une fois ici plutôt que de
 * recompter l'ordre des sommets à chaque appel supprime la faute la plus
 * fréquente d'un générateur — d'autant que le rendu, qui n'élimine pas les faces
 * arrière, ne la signalerait pas.
 */
static void quad_facing(geo_mesh *m, ns_v3 a, ns_v3 b, ns_v3 c, ns_v3 e,
                        ns_v2 ua, ns_v2 ub, ns_v2 uc, ns_v2 ue,
                        ns_v3 want, int32_t material)
{
    const ns_v3 n = ns_v3_cross(ns_v3_sub(b, a), ns_v3_sub(c, a));
    if (ns_v3_dot(n, want) < 0.0f) quad3(m, e, c, b, a, ue, uc, ub, ua, material);
    else                           quad3(m, a, b, c, e, ua, ub, uc, ue, material);
}

static void tri_facing(geo_mesh *m, ns_v3 a, ns_v3 b, ns_v3 c,
                       ns_v2 ua, ns_v2 ub, ns_v2 uc, ns_v3 want, int32_t material)
{
    const ns_v3 n = ns_v3_cross(ns_v3_sub(b, a), ns_v3_sub(c, a));
    if (ns_v3_dot(n, want) < 0.0f) tri3(m, c, b, a, uc, ub, ua, material);
    else                           tri3(m, a, b, c, ua, ub, uc, material);
}

/* ========================================================================== */
/* Boîte chanfreinée                                                          */
/* ========================================================================== */

/*
 * Projection planaire sur l'axe dominant de la normale. C'est le paramétrage
 * naturel d'une boîte : chaque face reçoit les deux coordonnées monde qui ne
 * sont pas la sienne, donc la texture garde la même échelle sur toutes les faces
 * et suit l'objet quand on l'agrandit.
 *
 * Les faces opposées se retrouvent en miroir l'une de l'autre. C'est sans
 * conséquence pour un motif pavable — et pour ce qui ne l'est pas (une affiche,
 * un marquee), le générateur à utiliser est `geo_panel`, dont l'UV est explicite.
 */
static ns_v2 project_uv(ns_v3 n, ns_v3 p, ns_v3 half, const geo_uv *uv)
{
    const float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);

    float u, v, eu, ev;
    if (ax >= ay && ax >= az)      { u = p.z; v = p.y; eu = half.z; ev = half.y; }
    else if (ay >= az)             { u = p.x; v = p.z; eu = half.x; ev = half.z; }
    else                           { u = p.x; v = p.y; eu = half.x; ev = half.y; }

    if (uv->fit) {
        u = (eu > 1e-6f) ? (u + eu) / (2.0f * eu) : 0.0f;
        v = (ev > 1e-6f) ? (v + ev) / (2.0f * ev) : 0.0f;
        return ns_v2_make(u, v);
    }
    const float mpt = (uv->metres_per_tile > 1e-6f) ? uv->metres_per_tile : 1.0f;
    return ns_v2_make((u + uv->offset_u) / mpt, (v + uv->offset_v) / mpt);
}

/* Point d'une boîte centrée, en coordonnées d'axes. */
static ns_v3 axis_point(int i, float vi, int j, float vj, int k, float vk)
{
    float c[3] = { 0.0f, 0.0f, 0.0f };
    c[i] = vi; c[j] = vj; c[k] = vk;
    return ns_v3_make(c[0], c[1], c[2]);
}

void geo_box(geo_mesh *m, ns_v3 size, float chamfer, uint32_t faces,
             const geo_uv *uv, int32_t material)
{
    const geo_uv fallback = geo_uv_tile(1.0f);
    if (!uv) uv = &fallback;

    if (size.x <= 1e-5f || size.y <= 1e-5f || size.z <= 1e-5f) {
        tool_fatalf("geo_box : dimension nulle ou négative (%.4f x %.4f x %.4f)",
                    (double)size.x, (double)size.y, (double)size.z);
    }

    const ns_v3 half = ns_v3_scale(size, 0.5f);
    const float h[3] = { half.x, half.y, half.z };

    /* Un chanfrein qui mangerait la face qu'il borde ne borde plus rien. Un
     * tiers de la plus petite demi-dimension laisse toujours une face centrale. */
    const float smallest = ns_minf(h[0], ns_minf(h[1], h[2]));
    float ch = (chamfer > 0.0f) ? chamfer : 0.0f;
    if (ch > smallest / 3.0f) ch = smallest / 3.0f;

    const size_t first_vertex = geo_mesh_vertex_count(m);

    /* --------------------------------------------------------------- faces */
    for (int axis = 0; axis < 3; ++axis) {
        for (int s = 0; s < 2; ++s) {
            const float sign = s ? 1.0f : -1.0f;
            const uint32_t bit = 1u << (axis * 2 + (s ? 0 : 1));
            if (!(faces & bit)) continue;

            const int u_axis = (axis + 2) % 3;   /* X->Z, Y->X, Z->Y */
            const int v_axis = (axis + 1) % 3;   /* X->Y, Y->Z, Z->X */
            const float hu = h[u_axis] - ch;
            const float hv = h[v_axis] - ch;

            const ns_v3 n = axis_point(axis, sign, u_axis, 0.0f, v_axis, 0.0f);
            const ns_v3 p0 = axis_point(axis, sign * h[axis], u_axis, -hu, v_axis, -hv);
            const ns_v3 p1 = axis_point(axis, sign * h[axis], u_axis,  hu, v_axis, -hv);
            const ns_v3 p2 = axis_point(axis, sign * h[axis], u_axis,  hu, v_axis,  hv);
            const ns_v3 p3 = axis_point(axis, sign * h[axis], u_axis, -hu, v_axis,  hv);

            quad_facing(m, p0, p1, p2, p3,
                        project_uv(n, p0, half, uv), project_uv(n, p1, half, uv),
                        project_uv(n, p2, half, uv), project_uv(n, p3, half, uv),
                        n, material);
        }
    }

    if (ch > 1e-6f) {
        /* ------------------------------------------------------- biseaux */
        /* Un biseau relie deux faces : s'il en manque une, il n'y a rien à
         * relier et l'arête reste vive. */
        for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
                const int k = 3 - i - j;
                for (int si = 0; si < 2; ++si) {
                    for (int sj = 0; sj < 2; ++sj) {
                        const float fi = si ? 1.0f : -1.0f;
                        const float fj = sj ? 1.0f : -1.0f;
                        const uint32_t bi = 1u << (i * 2 + (si ? 0 : 1));
                        const uint32_t bj = 1u << (j * 2 + (sj ? 0 : 1));
                        if (!(faces & bi) || !(faces & bj)) continue;

                        const float hk = h[k] - ch;
                        const ns_v3 n = ns_v3_norm(axis_point(i, fi, j, fj, k, 0.0f));

                        const ns_v3 p0 = axis_point(i, fi * h[i], j, fj * (h[j] - ch), k, -hk);
                        const ns_v3 p1 = axis_point(i, fi * h[i], j, fj * (h[j] - ch), k,  hk);
                        const ns_v3 p2 = axis_point(i, fi * (h[i] - ch), j, fj * h[j], k,  hk);
                        const ns_v3 p3 = axis_point(i, fi * (h[i] - ch), j, fj * h[j], k, -hk);

                        quad_facing(m, p0, p1, p2, p3,
                                    project_uv(n, p0, half, uv), project_uv(n, p1, half, uv),
                                    project_uv(n, p2, half, uv), project_uv(n, p3, half, uv),
                                    n, material);
                    }
                }
            }
        }

        /* ------------------------------------------------------- angles */
        for (int sx = 0; sx < 2; ++sx) {
            for (int sy = 0; sy < 2; ++sy) {
                for (int sz = 0; sz < 2; ++sz) {
                    const uint32_t bx = 1u << (0 * 2 + (sx ? 0 : 1));
                    const uint32_t by = 1u << (1 * 2 + (sy ? 0 : 1));
                    const uint32_t bz = 1u << (2 * 2 + (sz ? 0 : 1));
                    if (!(faces & bx) || !(faces & by) || !(faces & bz)) continue;

                    const float fx = sx ? 1.0f : -1.0f;
                    const float fy = sy ? 1.0f : -1.0f;
                    const float fz = sz ? 1.0f : -1.0f;
                    const ns_v3 n = ns_v3_norm(ns_v3_make(fx, fy, fz));

                    const ns_v3 p0 = ns_v3_make(fx * h[0], fy * (h[1] - ch), fz * (h[2] - ch));
                    const ns_v3 p1 = ns_v3_make(fx * (h[0] - ch), fy * h[1], fz * (h[2] - ch));
                    const ns_v3 p2 = ns_v3_make(fx * (h[0] - ch), fy * (h[1] - ch), fz * h[2]);

                    tri_facing(m, p0, p1, p2,
                               project_uv(n, p0, half, uv), project_uv(n, p1, half, uv),
                               project_uv(n, p2, half, uv), n, material);
                }
            }
        }
    }

    /* La boîte est construite centrée — c'est le repère où `fit` a un sens — puis
     * relevée pour qu'elle repose sur Y = 0, ce qu'attend l'appelant. */
    for (size_t i = first_vertex; i < geo_mesh_vertex_count(m); ++i) {
        TOOL_VEC_AT(&m->verts, gltf_vertex, i).position[1] += half.y;
    }
}

/* ========================================================================== */
/* Plan subdivisé                                                             */
/* ========================================================================== */

void geo_plane(geo_mesh *m, float size_x, float size_z, int subdiv_x, int subdiv_z,
               bool face_up, const geo_uv *uv, int32_t material)
{
    const geo_uv fallback = geo_uv_tile(1.0f);
    if (!uv) uv = &fallback;

    if (size_x <= 1e-5f || size_z <= 1e-5f) {
        tool_fatalf("geo_plane : dimension nulle (%.4f x %.4f)",
                    (double)size_x, (double)size_z);
    }
    const int nx = (subdiv_x > 0) ? subdiv_x : 1;
    const int nz = (subdiv_z > 0) ? subdiv_z : 1;

    const float hx = size_x * 0.5f, hz = size_z * 0.5f;
    const float mpt = (uv->metres_per_tile > 1e-6f) ? uv->metres_per_tile : 1.0f;
    const ns_v3 n = ns_v3_make(0.0f, face_up ? 1.0f : -1.0f, 0.0f);

    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const float x0 = -hx + size_x * ((float)ix       / (float)nx);
            const float x1 = -hx + size_x * ((float)(ix + 1) / (float)nx);
            const float z0 = -hz + size_z * ((float)iz       / (float)nz);
            const float z1 = -hz + size_z * ((float)(iz + 1) / (float)nz);

            ns_v2 uv00, uv10, uv11, uv01;
            if (uv->fit) {
                uv00 = ns_v2_make((x0 + hx) / size_x, (z0 + hz) / size_z);
                uv10 = ns_v2_make((x1 + hx) / size_x, (z0 + hz) / size_z);
                uv11 = ns_v2_make((x1 + hx) / size_x, (z1 + hz) / size_z);
                uv01 = ns_v2_make((x0 + hx) / size_x, (z1 + hz) / size_z);
            } else {
                uv00 = ns_v2_make((x0 + uv->offset_u) / mpt, (z0 + uv->offset_v) / mpt);
                uv10 = ns_v2_make((x1 + uv->offset_u) / mpt, (z0 + uv->offset_v) / mpt);
                uv11 = ns_v2_make((x1 + uv->offset_u) / mpt, (z1 + uv->offset_v) / mpt);
                uv01 = ns_v2_make((x0 + uv->offset_u) / mpt, (z1 + uv->offset_v) / mpt);
            }

            quad_facing(m,
                        ns_v3_make(x0, 0.0f, z0), ns_v3_make(x1, 0.0f, z0),
                        ns_v3_make(x1, 0.0f, z1), ns_v3_make(x0, 0.0f, z1),
                        uv00, uv10, uv11, uv01, n, material);
        }
    }
}

/* ========================================================================== */
/* Panneau à UV explicite                                                     */
/* ========================================================================== */

void geo_panel(geo_mesh *m, ns_v3 centre, ns_v3 normal, ns_v3 right,
               float width, float height,
               float u0, float v0, float u1, float v1, int32_t material)
{
    if (width <= 1e-5f || height <= 1e-5f) {
        tool_fatalf("geo_panel : dimension nulle (%.4f x %.4f)",
                    (double)width, (double)height);
    }
    const ns_v3 n = ns_v3_norm(normal);

    /* On orthogonalise « droite » contre la normale plutôt que d'exiger de
     * l'appelant qu'il fournisse un repère parfait : une affiche inclinée se
     * décrit naturellement par sa normale et un « vers la droite » approximatif. */
    ns_v3 r = ns_v3_sub(right, ns_v3_scale(n, ns_v3_dot(n, right)));
    if (ns_v3_len_sq(r) < 1e-12f) {
        const ns_v3 axis = (fabsf(n.y) < 0.99f) ? ns_v3_make(0, 1, 0) : ns_v3_make(1, 0, 0);
        r = ns_v3_cross(axis, n);
    }
    r = ns_v3_norm(r);
    const ns_v3 up = ns_v3_cross(n, r);

    const ns_v3 dx = ns_v3_scale(r, width * 0.5f);
    const ns_v3 dy = ns_v3_scale(up, height * 0.5f);

    const ns_v3 a = ns_v3_sub(ns_v3_sub(centre, dx), dy);
    const ns_v3 b = ns_v3_sub(ns_v3_add(centre, dx), dy);
    const ns_v3 c = ns_v3_add(ns_v3_add(centre, dx), dy);
    const ns_v3 e = ns_v3_add(ns_v3_sub(centre, dx), dy);

    /*
     * `a` et `b` sont le bas du panneau, `c` et `e` le haut — et **V croît vers
     * le bas** : glTF place l'origine des UV en haut à gauche, là où OBJ la place
     * en bas à gauche. `obj2gltf` fait déjà cette conversion à la lecture
     * (`t->y = 1 - v`) ; ici il n'y a pas de conversion à faire, seulement la
     * bonne convention à respecter du premier coup.
     *
     * Elle ne l'était pas : `v0` était attribué au bas du quad, donc **tout
     * panneau déclaré était rendu à l'envers** — l'enseigne NINETEEN, les
     * marquees des dix-neuf bornes, leurs écrans, les huit affiches. Sur une
     * texture pavable un retournement en V ne se voit pas, et c'est pour ça que
     * le défaut a traversé A3 et A4 : il ne se lit que sur du texte. La
     * comparaison avec `--room=legacy`, dont les UV viennent du modèle de 2020,
     * l'a montrée en une capture.
     */
    quad_facing(m, a, b, c, e,
                ns_v2_make(u0, v1), ns_v2_make(u1, v1),
                ns_v2_make(u1, v0), ns_v2_make(u0, v0), n, material);
}

/* ========================================================================== */
/* Extrusion de profil                                                        */
/* ========================================================================== */

/* Écrêtage de l'onglet : au-delà, la section étirée dépasse largement le gabarit
 * de la moulure et il faut deux pièces, pas un onglet. cos(75°) ≈ 0,2588 donne un
 * étirement de 3,86 pour un retour à 150°. */
#define GEO_MITRE_MAX_STRETCH 4.0f

void geo_profile_extrude(geo_mesh *m,
                         const ns_v2 *profile, size_t profile_count, bool profile_closed,
                         const ns_v3 *path, size_t path_count, bool path_closed,
                         const geo_uv *uv, int32_t material)
{
    const geo_uv fallback = geo_uv_tile(1.0f);
    if (!uv) uv = &fallback;

    if (profile_count < 2) tool_fatalf("geo_profile_extrude : profil de %zu point(s)", profile_count);
    if (path_count < 2)    tool_fatalf("geo_profile_extrude : chemin de %zu point(s)", path_count);
    if (path_closed && path_count < 3) {
        tool_fatalf("geo_profile_extrude : un chemin fermé demande au moins 3 points");
    }

    const float mpt = (uv->metres_per_tile > 1e-6f) ? uv->metres_per_tile : 1.0f;
    const size_t np = path_count;
    const size_t nseg = path_closed ? np : np - 1;

    ns_v3 *tangent = (ns_v3 *)malloc(sizeof(ns_v3) * nseg);
    if (!tangent) tool_fatalf("mémoire épuisée (extrusion)");
    for (size_t i = 0; i < nseg; ++i) {
        const ns_v3 e = ns_v3_sub(path[(i + 1) % np], path[i]);
        if (ns_v3_len(e) < 1e-5f) tool_fatalf("geo_profile_extrude : segment de longueur nulle");
        tangent[i] = ns_v3_norm(e);
    }

    /* Repère par point du chemin : « droite » horizontale perpendiculaire à la
     * tangente moyenne, « haut » vertical, plus l'étirement d'onglet. */
    ns_v3 *frame_r = (ns_v3 *)malloc(sizeof(ns_v3) * np);
    ns_v3 *frame_t = (ns_v3 *)malloc(sizeof(ns_v3) * np);
    float *stretch = (float *)malloc(sizeof(float) * np);
    if (!frame_r || !frame_t || !stretch) tool_fatalf("mémoire épuisée (extrusion)");

    const ns_v3 world_up = ns_v3_make(0.0f, 1.0f, 0.0f);
    for (size_t i = 0; i < np; ++i) {
        const bool has_prev = path_closed || i > 0;
        const bool has_next = path_closed || i < np - 1;
        const ns_v3 tp = has_prev ? tangent[(i + nseg - 1) % nseg] : tangent[0];
        const ns_v3 tn = has_next ? tangent[i % nseg]              : tangent[nseg - 1];

        ns_v3 avg = ns_v3_norm(ns_v3_add(tp, tn));
        if (ns_v3_len_sq(avg) < 1e-8f) avg = tn;    /* demi-tour : pas d'onglet possible */
        frame_t[i] = avg;

        ns_v3 r = ns_v3_cross(world_up, avg);
        if (ns_v3_len_sq(r) < 1e-8f) r = ns_v3_cross(ns_v3_make(1, 0, 0), avg);
        frame_r[i] = ns_v3_norm(r);

        const float cos_half = ns_clampf(ns_v3_dot(avg, tn), 1.0f / GEO_MITRE_MAX_STRETCH, 1.0f);
        stretch[i] = 1.0f / cos_half;
        if (has_prev && has_next && ns_v3_dot(avg, tn) < 1.0f / GEO_MITRE_MAX_STRETCH) {
            tool_warnf("geo_profile_extrude : angle trop fermé au point %zu, "
                       "onglet écrêté — découper la moulure en deux pièces", i);
        }
    }

    /* Abscisses curvilignes : l'une le long du chemin (u), l'autre le long du
     * profil (v). C'est le paramétrage attendu d'une moulure — la texture suit la
     * pièce et ne se déforme pas dans les angles. */
    float *path_s = (float *)malloc(sizeof(float) * (np + 1));
    float *prof_s = (float *)malloc(sizeof(float) * (profile_count + 1));
    if (!path_s || !prof_s) tool_fatalf("mémoire épuisée (extrusion)");
    path_s[0] = 0.0f;
    for (size_t i = 0; i < nseg; ++i) {
        path_s[i + 1] = path_s[i] + ns_v3_len(ns_v3_sub(path[(i + 1) % np], path[i]));
    }
    prof_s[0] = 0.0f;
    for (size_t j = 0; j + 1 < profile_count; ++j) {
        prof_s[j + 1] = prof_s[j] + ns_v2_len(ns_v2_sub(profile[j + 1], profile[j]));
    }
    prof_s[profile_count] = prof_s[profile_count - 1]
                          + ns_v2_len(ns_v2_sub(profile[0], profile[profile_count - 1]));

    /* Position d'un point du profil dans le repère d'un point du chemin. */
    #define GEO_SWEEP_POINT(i, j) \
        ns_v3_add(path[(i) % np], \
                  ns_v3_add(ns_v3_scale(frame_r[(i) % np], profile[(j)].x * stretch[(i) % np]), \
                            ns_v3_scale(world_up, profile[(j)].y)))

    const size_t prof_edges = profile_closed ? profile_count : profile_count - 1;
    for (size_t i = 0; i < nseg; ++i) {
        const size_t i1 = (i + 1) % np;
        for (size_t j = 0; j < prof_edges; ++j) {
            const size_t j1 = (j + 1) % profile_count;

            const ns_v3 a = GEO_SWEEP_POINT(i,  j);
            const ns_v3 b = GEO_SWEEP_POINT(i,  j1);
            const ns_v3 c = GEO_SWEEP_POINT(i1, j1);
            const ns_v3 e = GEO_SWEEP_POINT(i1, j);

            /* Cet ordre — profil d'abord, chemin ensuite — est celui qui donne
             * des normales vers l'extérieur pour un profil listé dans le sens
             * direct. L'inverse produirait une moulure retournée. */
            quad3(m, a, b, c, e,
                  ns_v2_make(path_s[i]     / mpt, prof_s[j]  / mpt),
                  ns_v2_make(path_s[i]     / mpt, prof_s[j + 1] / mpt),
                  ns_v2_make(path_s[i + 1] / mpt, prof_s[j + 1] / mpt),
                  ns_v2_make(path_s[i + 1] / mpt, prof_s[j]  / mpt),
                  material);
        }
    }

    /* Bouchons : seulement pour un profil fermé sur un chemin ouvert — sinon il
     * n'y a rien à boucher, ou la section est déjà une surface ouverte. */
    if (profile_closed && !path_closed) {
        ns_v2 centroid = ns_v2_make(0.0f, 0.0f);
        for (size_t j = 0; j < profile_count; ++j) centroid = ns_v2_add(centroid, profile[j]);
        centroid = ns_v2_scale(centroid, 1.0f / (float)profile_count);

        for (int end = 0; end < 2; ++end) {
            const size_t i = end ? np - 1 : 0;
            const ns_v3 want = end ? frame_t[i] : ns_v3_neg(frame_t[i]);
            const ns_v3 hub = ns_v3_add(path[i],
                                        ns_v3_add(ns_v3_scale(frame_r[i], centroid.x * stretch[i]),
                                                  ns_v3_scale(world_up, centroid.y)));
            for (size_t j = 0; j < profile_count; ++j) {
                const size_t j1 = (j + 1) % profile_count;
                tri_facing(m, hub, GEO_SWEEP_POINT(i, j), GEO_SWEEP_POINT(i, j1),
                           ns_v2_make(centroid.x / mpt, centroid.y / mpt),
                           ns_v2_make(profile[j].x / mpt, profile[j].y / mpt),
                           ns_v2_make(profile[j1].x / mpt, profile[j1].y / mpt),
                           want, material);
            }
        }
    }

    #undef GEO_SWEEP_POINT

    free(tangent); free(frame_r); free(frame_t); free(stretch);
    free(path_s); free(prof_s);
}

/* ========================================================================== */
/* Pan de mur                                                                 */
/* ========================================================================== */

/* sin(3°). En deçà, les deux lignes décalées sont quasi confondues et leur
 * sécante part à l'infini : on prend un joint droit, visuellement identique. */
#define GEO_MITRE_MIN_SIN 0.05234f

static float v2_cross(ns_v2 a, ns_v2 b) { return a.x * b.y - a.y * b.x; }

/* Normale « à gauche » quand on marche dans la direction `d`, en plan (x, z).
 * Démonstration en une ligne : gauche = (+Y) × d = (d.z, 0, −d.x). */
static ns_v2 v2_left(ns_v2 d)  { return ns_v2_make(d.y, -d.x); }
static ns_v2 v2_right(ns_v2 d) { return ns_v2_make(-d.y, d.x); }

static ns_v3 plan3(ns_v2 p, float y) { return ns_v3_make(p.x, y, p.y); }

static ns_v2 offset_corner(ns_v2 vertex, ns_v2 d_prev, ns_v2 d_next,
                           bool has_prev, bool has_next, ns_v2 (*side)(ns_v2), float half_t)
{
    if (!has_prev) return ns_v2_add(vertex, ns_v2_scale(side(d_next), half_t));
    if (!has_next) return ns_v2_add(vertex, ns_v2_scale(side(d_prev), half_t));

    const ns_v2 p_prev = ns_v2_add(vertex, ns_v2_scale(side(d_prev), half_t));
    const ns_v2 p_next = ns_v2_add(vertex, ns_v2_scale(side(d_next), half_t));

    const float den = v2_cross(d_prev, d_next);
    if (fabsf(den) < GEO_MITRE_MIN_SIN) {
        return ns_v2_scale(ns_v2_add(p_prev, p_next), 0.5f);
    }
    const float s = v2_cross(ns_v2_sub(p_next, p_prev), d_next) / den;
    return ns_v2_add(p_prev, ns_v2_scale(d_prev, s));
}

/* Un panneau vertical entre deux abscisses de la ligne médiane, reporté sur la
 * ligne décalée par interpolation. Le report est linéaire : aux angles mitrés,
 * la face est un peu plus longue ou plus courte que la médiane, et une ouverture
 * y glisse de quelques millimètres. C'est sans conséquence tant qu'une baie n'est
 * pas collée à un angle — et une baie collée à un angle est refusée en amont. */
static void wall_panel(geo_mesh *m, ns_v2 a, ns_v2 b, float t0, float t1,
                       float y0, float y1, ns_v2 normal,
                       float u_base, float face_len, float mpt,
                       float offset_u, float offset_v, int32_t material)
{
    if (t1 - t0 < 1e-5f || y1 - y0 < 1e-5f) return;

    const ns_v2 p0 = ns_v2_add(a, ns_v2_scale(ns_v2_sub(b, a), t0));
    const ns_v2 p1 = ns_v2_add(a, ns_v2_scale(ns_v2_sub(b, a), t1));

    const float u0 = (u_base + t0 * face_len + offset_u) / mpt;
    const float u1 = (u_base + t1 * face_len + offset_u) / mpt;
    const float v0 = (y0 + offset_v) / mpt;
    const float v1 = (y1 + offset_v) / mpt;

    quad_facing(m, plan3(p0, y0), plan3(p1, y0), plan3(p1, y1), plan3(p0, y1),
                ns_v2_make(u0, v0), ns_v2_make(u1, v0),
                ns_v2_make(u1, v1), ns_v2_make(u0, v1),
                ns_v3_make(normal.x, 0.0f, normal.y), material);
}

/* Ouverture rapportée à son segment, après validation. */
typedef struct wall_opening_slot {
    const geo_opening *op;
    size_t segment;
    float  local_offset;    /* depuis le début du segment, sur la médiane */
} wall_opening_slot;

void geo_wall_run(geo_mesh *m, const geo_wall_desc *d)
{
    const char *name = (d->name && d->name[0]) ? d->name : "(pan sans nom)";

    if (d->point_count < 2) {
        tool_fatalf("pan « %s » : %zu point(s), il en faut au moins 2", name, d->point_count);
    }
    if (d->closed && d->point_count < 3) {
        tool_fatalf("pan « %s » : un contour fermé demande au moins 3 points", name);
    }
    if (d->height <= 1e-4f)    tool_fatalf("pan « %s » : hauteur nulle", name);
    if (d->thickness <= 1e-4f) tool_fatalf("pan « %s » : épaisseur nulle", name);

    const size_t np = d->point_count;
    const size_t nseg = d->closed ? np : np - 1;
    const float half_t = d->thickness * 0.5f;
    const float mpt = (d->uv.metres_per_tile > 1e-6f) ? d->uv.metres_per_tile : 1.0f;

    ns_v2 *dir = (ns_v2 *)malloc(sizeof(ns_v2) * nseg);
    float *seg_len = (float *)malloc(sizeof(float) * nseg);
    float *seg_start = (float *)malloc(sizeof(float) * (nseg + 1));
    if (!dir || !seg_len || !seg_start) tool_fatalf("mémoire épuisée (pan « %s »)", name);

    seg_start[0] = 0.0f;
    for (size_t i = 0; i < nseg; ++i) {
        const ns_v2 e = ns_v2_sub(d->points[(i + 1) % np], d->points[i]);
        seg_len[i] = ns_v2_len(e);
        if (seg_len[i] < 1e-4f) {
            tool_fatalf("pan « %s » : segment %zu de longueur nulle — deux points confondus",
                        name, i);
        }
        dir[i] = ns_v2_scale(e, 1.0f / seg_len[i]);
        seg_start[i + 1] = seg_start[i] + seg_len[i];
    }
    const float total_len = seg_start[nseg];

    /* -------------------------------------------------- validation des baies */
    wall_opening_slot *slots = NULL;
    if (d->opening_count) {
        slots = (wall_opening_slot *)malloc(sizeof(wall_opening_slot) * d->opening_count);
        if (!slots) tool_fatalf("mémoire épuisée (pan « %s »)", name);
    }

    for (size_t k = 0; k < d->opening_count; ++k) {
        const geo_opening *o = &d->openings[k];
        const char *oname = o->name[0] ? o->name : "(baie sans nom)";

        if (o->width <= 1e-4f) {
            tool_fatalf("pan « %s », baie « %s » : largeur nulle", name, oname);
        }
        if (o->sill < -1e-4f || o->head <= o->sill + 1e-4f) {
            tool_fatalf("pan « %s », baie « %s » : allège %.3f m et linteau %.3f m "
                        "ne délimitent rien", name, oname, (double)o->sill, (double)o->head);
        }
        if (o->head > d->height + 1e-4f) {
            tool_fatalf("pan « %s », baie « %s » : linteau à %.3f m sous un pan de %.3f m",
                        name, oname, (double)o->head, (double)d->height);
        }
        if (o->offset < -1e-4f || o->offset + o->width > total_len + 1e-4f) {
            tool_fatalf("pan « %s », baie « %s » : occupe [%.3f, %.3f] m sur un pan "
                        "de %.3f m", name, oname,
                        (double)o->offset, (double)(o->offset + o->width), (double)total_len);
        }

        /* Une baie doit tenir dans un seul segment : à cheval sur un angle, elle
         * demanderait un percement en trois dimensions, c'est-à-dire du CSG. */
        size_t seg = SIZE_MAX;
        for (size_t i = 0; i < nseg; ++i) {
            if (o->offset >= seg_start[i] - 1e-4f
             && o->offset + o->width <= seg_start[i + 1] + 1e-4f) { seg = i; break; }
        }
        if (seg == SIZE_MAX) {
            tool_fatalf("pan « %s », baie « %s » : à cheval sur deux segments "
                        "([%.3f, %.3f] m) — la découper en deux baies",
                        name, oname, (double)o->offset, (double)(o->offset + o->width));
        }
        slots[k].op = o;
        slots[k].segment = seg;
        slots[k].local_offset = o->offset - seg_start[seg];
    }

    /* Tri par abscisse, puis contrôle de recouvrement. Trier ici plutôt que
     * l'exiger de l'appelant : l'ordre d'écriture d'une description de salle suit
     * la lecture du plan, pas la règle graduée. */
    for (size_t k = 1; k < d->opening_count; ++k) {
        const wall_opening_slot tmp = slots[k];
        size_t j = k;
        while (j > 0 && slots[j - 1].op->offset > tmp.op->offset) { slots[j] = slots[j - 1]; --j; }
        slots[j] = tmp;
    }
    for (size_t k = 1; k < d->opening_count; ++k) {
        const geo_opening *prev = slots[k - 1].op;
        const geo_opening *cur  = slots[k].op;
        if (cur->offset < prev->offset + prev->width - 1e-4f) {
            tool_fatalf("pan « %s » : les baies « %s » et « %s » se chevauchent",
                        name, prev->name[0] ? prev->name : "?", cur->name[0] ? cur->name : "?");
        }
    }

    /* ------------------------------------------------------- coins mitrés */
    ns_v2 *inner = (ns_v2 *)malloc(sizeof(ns_v2) * np);
    ns_v2 *outer = (ns_v2 *)malloc(sizeof(ns_v2) * np);
    if (!inner || !outer) tool_fatalf("mémoire épuisée (pan « %s »)", name);

    for (size_t v = 0; v < np; ++v) {
        const bool has_prev = d->closed || v > 0;
        const bool has_next = d->closed || v + 1 < np;
        const ns_v2 dp = dir[(v + nseg - 1) % nseg];
        const ns_v2 dn = dir[v % nseg];
        inner[v] = offset_corner(d->points[v], dp, dn, has_prev, has_next, v2_left,  half_t);
        outer[v] = offset_corner(d->points[v], dp, dn, has_prev, has_next, v2_right, half_t);
    }

    /* ------------------------------------------------------------ panneaux */
    float u_inner = 0.0f, u_outer = 0.0f;

    for (size_t i = 0; i < nseg; ++i) {
        const size_t i1 = (i + 1) % np;
        const ns_v2 ia = inner[i], ib = inner[i1];
        const ns_v2 oa = outer[i], ob = outer[i1];
        const float len_i = ns_v2_len(ns_v2_sub(ib, ia));
        const float len_o = ns_v2_len(ns_v2_sub(ob, oa));
        const float len_c = seg_len[i];

        const ns_v2 n_in  = v2_left(dir[i]);
        const ns_v2 n_out = v2_right(dir[i]);

        /* Découpe du segment : trumeau, sous-allège, linteau, trumeau — dans
         * l'ordre où un dessinateur les tracerait. */
        float cursor = 0.0f;
        for (size_t k = 0; k <= d->opening_count; ++k) {
            const bool last = (k == d->opening_count);
            if (!last && slots[k].segment != i) continue;

            const float o_start = last ? len_c : slots[k].local_offset;
            const float o_end   = last ? len_c : (slots[k].local_offset + slots[k].op->width);

            if (o_start > cursor + 1e-5f) {
                const float t0 = cursor / len_c, t1 = o_start / len_c;
                wall_panel(m, ia, ib, t0, t1, 0.0f, d->height, n_in,
                           u_inner, len_i, mpt, d->uv.offset_u, d->uv.offset_v,
                           d->material_inner);
                wall_panel(m, oa, ob, t0, t1, 0.0f, d->height, n_out,
                           u_outer, len_o, mpt, d->uv.offset_u, d->uv.offset_v,
                           d->material_outer);
            }
            if (last) break;

            const geo_opening *o = slots[k].op;
            const float t0 = o_start / len_c, t1 = o_end / len_c;

            if (o->sill > 1e-4f) {
                wall_panel(m, ia, ib, t0, t1, 0.0f, o->sill, n_in,
                           u_inner, len_i, mpt, d->uv.offset_u, d->uv.offset_v,
                           d->material_inner);
                wall_panel(m, oa, ob, t0, t1, 0.0f, o->sill, n_out,
                           u_outer, len_o, mpt, d->uv.offset_u, d->uv.offset_v,
                           d->material_outer);
            }
            if (o->head < d->height - 1e-4f) {
                wall_panel(m, ia, ib, t0, t1, o->head, d->height, n_in,
                           u_inner, len_i, mpt, d->uv.offset_u, d->uv.offset_v,
                           d->material_inner);
                wall_panel(m, oa, ob, t0, t1, o->head, d->height, n_out,
                           u_outer, len_o, mpt, d->uv.offset_u, d->uv.offset_v,
                           d->material_outer);
            }

            /* Les quatre tableaux de la baie : deux jambages, l'appui, le
             * dessous du linteau. C'est ce qui donne au percement son épaisseur —
             * sans eux la baie se lit comme une découpe dans du carton. */
            const ns_v2 ji0 = ns_v2_add(ia, ns_v2_scale(ns_v2_sub(ib, ia), t0));
            const ns_v2 jo0 = ns_v2_add(oa, ns_v2_scale(ns_v2_sub(ob, oa), t0));
            const ns_v2 ji1 = ns_v2_add(ia, ns_v2_scale(ns_v2_sub(ib, ia), t1));
            const ns_v2 jo1 = ns_v2_add(oa, ns_v2_scale(ns_v2_sub(ob, oa), t1));

            const float vs = o->sill / mpt, vh = o->head / mpt;
            const float w0 = 0.0f, w1 = d->thickness / mpt;
            const float us = (seg_start[i] + o_start) / mpt;
            const float ue = (seg_start[i] + o_end) / mpt;

            quad_facing(m, plan3(ji0, o->sill), plan3(jo0, o->sill),
                           plan3(jo0, o->head), plan3(ji0, o->head),
                        ns_v2_make(w0, vs), ns_v2_make(w1, vs),
                        ns_v2_make(w1, vh), ns_v2_make(w0, vh),
                        ns_v3_make(dir[i].x, 0.0f, dir[i].y), d->material_reveal);

            quad_facing(m, plan3(ji1, o->sill), plan3(jo1, o->sill),
                           plan3(jo1, o->head), plan3(ji1, o->head),
                        ns_v2_make(w0, vs), ns_v2_make(w1, vs),
                        ns_v2_make(w1, vh), ns_v2_make(w0, vh),
                        ns_v3_make(-dir[i].x, 0.0f, -dir[i].y), d->material_reveal);

            /* L'appui n'est émis que s'il existe : pour une porte (allège nulle)
             * il serait coplanaire avec le sol, et deux surfaces coplanaires se
             * disputent le tampon de profondeur. */
            if (o->sill > 1e-4f) {
                quad_facing(m, plan3(ji0, o->sill), plan3(ji1, o->sill),
                               plan3(jo1, o->sill), plan3(jo0, o->sill),
                            ns_v2_make(us, w0), ns_v2_make(ue, w0),
                            ns_v2_make(ue, w1), ns_v2_make(us, w1),
                            ns_v3_make(0.0f, 1.0f, 0.0f), d->material_reveal);
            }
            if (o->head < d->height - 1e-4f) {
                quad_facing(m, plan3(ji0, o->head), plan3(ji1, o->head),
                               plan3(jo1, o->head), plan3(jo0, o->head),
                            ns_v2_make(us, w0), ns_v2_make(ue, w0),
                            ns_v2_make(ue, w1), ns_v2_make(us, w1),
                            ns_v3_make(0.0f, -1.0f, 0.0f), d->material_reveal);
            }

            cursor = o_end;
        }

        if (d->cap_top) {
            quad_facing(m, plan3(ia, d->height), plan3(ib, d->height),
                           plan3(ob, d->height), plan3(oa, d->height),
                        ns_v2_make(seg_start[i] / mpt, 0.0f),
                        ns_v2_make(seg_start[i + 1] / mpt, 0.0f),
                        ns_v2_make(seg_start[i + 1] / mpt, d->thickness / mpt),
                        ns_v2_make(seg_start[i] / mpt, d->thickness / mpt),
                        ns_v3_make(0.0f, 1.0f, 0.0f), d->material_reveal);
        }

        u_inner += len_i;
        u_outer += len_o;
    }

    /* ------------------------------------------------------------- joues */
    if (d->cap_ends && !d->closed) {
        for (int end = 0; end < 2; ++end) {
            const size_t v = end ? np - 1 : 0;
            const ns_v2 dd = dir[end ? nseg - 1 : 0];
            const ns_v3 want = end ? ns_v3_make(dd.x, 0.0f, dd.y)
                                   : ns_v3_make(-dd.x, 0.0f, -dd.y);
            quad_facing(m, plan3(inner[v], 0.0f), plan3(outer[v], 0.0f),
                           plan3(outer[v], d->height), plan3(inner[v], d->height),
                        ns_v2_make(0.0f, 0.0f),
                        ns_v2_make(d->thickness / mpt, 0.0f),
                        ns_v2_make(d->thickness / mpt, d->height / mpt),
                        ns_v2_make(0.0f, d->height / mpt),
                        want, d->material_reveal);
        }
    }

    free(dir); free(seg_len); free(seg_start);
    free(inner); free(outer); free(slots);
}

/* ========================================================================== */
/* Cylindre                                                                   */
/* ========================================================================== */

void geo_cylinder(geo_mesh *m, float r_bottom, float r_top, float height,
                  int sides, bool cap_bottom, bool cap_top,
                  const geo_uv *uv, int32_t material)
{
    const geo_uv fallback = geo_uv_tile(1.0f);
    if (!uv) uv = &fallback;

    if (sides < 3) {
        tool_fatalf("geo_cylinder : %d côtés, il en faut au moins trois", sides);
    }
    if (height <= 1e-5f || (r_bottom <= 1e-5f && r_top <= 1e-5f)) {
        tool_fatalf("geo_cylinder : dimension nulle ou négative (r %.4f -> %.4f, h %.4f)",
                    (double)r_bottom, (double)r_top, (double)height);
    }

    const float mpt = (uv->metres_per_tile > 1e-6f) ? uv->metres_per_tile : 1.0f;
    const float circ = NS_TAU * ns_maxf(r_bottom, r_top);

    /* La pente de la paroi entre dans la normale : sans ça un cône tronqué
     * s'éclaire comme un cylindre, et son arête haute accroche une lumière
     * qu'elle ne devrait pas avoir. */
    const float slope = (r_bottom - r_top) / height;
    const float nlen = sqrtf(1.0f + slope * slope);

    uint32_t *ring_lo = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(sides + 1));
    uint32_t *ring_hi = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(sides + 1));
    if (!ring_lo || !ring_hi) tool_fatalf("geo_cylinder : mémoire");

    for (int i = 0; i <= sides; ++i) {
        const float a = (float)i / (float)sides * NS_TAU;
        const float ca = cosf(a), sa = sinf(a);
        const ns_v3 n = ns_v3_make(ca / nlen, slope / nlen, sa / nlen);
        const float u = (circ * (float)i / (float)sides) / mpt;
        ring_lo[i] = geo_mesh_push_vertex(m, ns_v3_make(ca * r_bottom, 0.0f, sa * r_bottom),
                                          n, u, 0.0f);
        ring_hi[i] = geo_mesh_push_vertex(m, ns_v3_make(ca * r_top, height, sa * r_top),
                                          n, u, height / mpt);
    }
    for (int i = 0; i < sides; ++i) {
        geo_mesh_push_tri(m, ring_lo[i], ring_hi[i], ring_hi[i + 1], material);
        geo_mesh_push_tri(m, ring_lo[i], ring_hi[i + 1], ring_lo[i + 1], material);
    }

    for (int cap = 0; cap < 2; ++cap) {
        const bool want = cap ? cap_top : cap_bottom;
        const float r = cap ? r_top : r_bottom;
        if (!want || r <= 1e-5f) continue;

        const float y = cap ? height : 0.0f;
        const ns_v3 n = ns_v3_make(0.0f, cap ? 1.0f : -1.0f, 0.0f);
        const uint32_t centre = geo_mesh_push_vertex(m, ns_v3_make(0.0f, y, 0.0f), n,
                                                     0.5f * r / mpt, 0.5f * r / mpt);
        uint32_t first = 0, prev = 0;
        for (int i = 0; i <= sides; ++i) {
            const float a = (float)i / (float)sides * NS_TAU;
            const float ca = cosf(a), sa = sinf(a);
            const uint32_t v = geo_mesh_push_vertex(m, ns_v3_make(ca * r, y, sa * r), n,
                                                    (0.5f + ca * 0.5f) * r / mpt,
                                                    (0.5f + sa * 0.5f) * r / mpt);
            if (i == 0) { first = v; prev = v; continue; }
            if (cap) geo_mesh_push_tri(m, centre, prev, v, material);
            else     geo_mesh_push_tri(m, centre, v, prev, material);
            prev = v;
        }
        (void)first;
    }

    free(ring_lo); free(ring_hi);
}
