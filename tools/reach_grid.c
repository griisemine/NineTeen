/* reach_grid.c — voir reach_grid.h pour ce que ce fichier mesure et pourquoi. */
#include "reach_grid.h"

#include "tools_common.h"

#include <math.h>

/* --------------------------------------------------------------------------
 * Construction
 * -------------------------------------------------------------------------- */

void reach_grid_init(reach_grid *g, float min_x, float min_z,
                     float max_x, float max_z, float cell)
{
    memset(g, 0, sizeof *g);
    if (cell <= 1e-4f) tool_fatalf("reach_grid : pas de grille nul");
    if (max_x <= min_x || max_z <= min_z) {
        tool_fatalf("reach_grid : emprise vide ou inversée "
                    "(%.3f..%.3f en x, %.3f..%.3f en z)",
                    (double)min_x, (double)max_x, (double)min_z, (double)max_z);
    }

    g->cell = cell;
    g->x0 = min_x;
    g->z0 = min_z;
    /* Arrondi au SUPÉRIEUR : une grille qui s'arrête un demi-centimètre avant le
     * dernier mur laisserait le remplissage sortir par là. */
    g->nx = (int)ceil(((double)max_x - (double)min_x) / (double)cell);
    g->nz = (int)ceil(((double)max_z - (double)min_z) / (double)cell);
    if (g->nx < 1) g->nx = 1;
    if (g->nz < 1) g->nz = 1;

    /* Quarante millions de cellules, c'est un pas choisi par erreur : une salle
     * de 200 m² au pas de 5 cm en fait 80 000. Mieux vaut le dire que réserver
     * un gigaoctet en silence. */
    const size_t n = (size_t)g->nx * (size_t)g->nz;
    if (n > 40u * 1000u * 1000u) {
        tool_fatalf("reach_grid : %d x %d cellules au pas de %.3f m — l'emprise "
                    "ou le pas est faux", g->nx, g->nz, (double)cell);
    }

    g->solid    = (unsigned char *)calloc(n, 1);
    g->walkable = (unsigned char *)calloc(n, 1);
    g->label    = (int *)malloc(n * sizeof(int));
    if (!g->solid || !g->walkable || !g->label) {
        tool_fatalf("reach_grid : mémoire épuisée (%zu cellules)", n);
    }
    for (size_t i = 0; i < n; ++i) g->label[i] = -1;
}

void reach_grid_release(reach_grid *g)
{
    free(g->solid);
    free(g->walkable);
    free(g->label);
    free(g->component_cells);
    memset(g, 0, sizeof *g);
}

void reach_grid_centre(const reach_grid *g, int ix, int iz, float *x, float *z)
{
    if (x) *x = g->x0 + ((float)ix + 0.5f) * g->cell;
    if (z) *z = g->z0 + ((float)iz + 0.5f) * g->cell;
}

bool reach_grid_cell_of(const reach_grid *g, float x, float z, int *ix, int *iz)
{
    const float fx = (x - g->x0) / g->cell;
    const float fz = (z - g->z0) / g->cell;
    if (fx < 0.0f || fz < 0.0f) return false;
    const int cx = (int)fx, cz = (int)fz;
    if (cx >= g->nx || cz >= g->nz) return false;
    if (ix) *ix = cx;
    if (iz) *iz = cz;
    return true;
}

unsigned char *reach_mask_new(const reach_grid *g)
{
    const size_t n = (size_t)g->nx * (size_t)g->nz;
    unsigned char *m = (unsigned char *)calloc(n, 1);
    if (!m) tool_fatalf("reach_grid : mémoire épuisée (masque de %zu cellules)", n);
    return m;
}

void reach_mask_free(unsigned char *mask) { free(mask); }

void reach_grid_add(reach_grid *g, const unsigned char *mask)
{
    const size_t n = (size_t)g->nx * (size_t)g->nz;
    for (size_t i = 0; i < n; ++i) g->solid[i] |= mask[i];
}

/* --------------------------------------------------------------------------
 * Marquage
 * -------------------------------------------------------------------------- */

/* Bornes de balayage, en cellules, pour une emprise en mètres élargie de
 * `slack`. Bornées à la grille ; l'intervalle rendu peut être vide. */
static void span_of(const reach_grid *g, float min_x, float min_z,
                    float max_x, float max_z, float slack,
                    int *ix0, int *iz0, int *ix1, int *iz1)
{
    const float inv = 1.0f / g->cell;
    int a = (int)floorf((min_x - slack - g->x0) * inv);
    int b = (int)floorf((min_z - slack - g->z0) * inv);
    int c = (int)ceilf ((max_x + slack - g->x0) * inv);
    int d = (int)ceilf ((max_z + slack - g->z0) * inv);
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (c > g->nx - 1) c = g->nx - 1;
    if (d > g->nz - 1) d = g->nz - 1;
    *ix0 = a;
    *iz0 = b;
    *ix1 = c;
    *iz1 = d;
}

void reach_mask_segment(const reach_grid *g, unsigned char *mask,
                        ns_v2 a, ns_v2 b, float half_width,
                        float cap_a, float cap_b)
{
    ns_v2 axis = ns_v2_sub(b, a);
    const float len = ns_v2_len(axis);
    if (len < 1e-6f) return;    /* deux points confondus : rien à barrer */
    axis = ns_v2_scale(axis, 1.0f / len);

    const ns_v2 normal = ns_v2_make(-axis.y, axis.x);
    const float full = len + cap_a + cap_b;
    const ns_v2 start = ns_v2_sub(a, ns_v2_scale(axis, cap_a));
    const ns_v2 mid = ns_v2_add(start, ns_v2_scale(axis, full * 0.5f));

    /* Séparation d'axes, réduite à ses deux axes utiles : les deux autres — X et
     * Z — sont exactement les bornes de balayage ci-dessous. */
    const float half = g->cell * 0.5f;
    const float ext_axis   = half * (fabsf(axis.x)   + fabsf(axis.y));
    const float ext_normal = half * (fabsf(normal.x) + fabsf(normal.y));

    const float rx = fabsf(axis.x) * full * 0.5f + fabsf(normal.x) * half_width;
    const float rz = fabsf(axis.y) * full * 0.5f + fabsf(normal.y) * half_width;

    int ix0, iz0, ix1, iz1;
    span_of(g, mid.x - rx, mid.y - rz, mid.x + rx, mid.y + rz, half,
            &ix0, &iz0, &ix1, &iz1);

    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            float cx, cz;
            reach_grid_centre(g, ix, iz, &cx, &cz);
            const ns_v2 rel = ns_v2_make(cx - mid.x, cz - mid.y);
            if (fabsf(ns_v2_dot(rel, axis))   > full * 0.5f + ext_axis)   continue;
            if (fabsf(ns_v2_dot(rel, normal)) > half_width + ext_normal)  continue;
            mask[reach_index(g, ix, iz)] = 1;
        }
    }
}

/* Distance d'un point au SEGMENT [a, b], au carré. */
static float seg_dist2(ns_v2 a, ns_v2 b, ns_v2 p)
{
    const ns_v2 e = ns_v2_sub(b, a);
    const float len2 = ns_v2_dot(e, e);
    float t = 0.0f;
    if (len2 > 1e-12f) t = ns_clampf(ns_v2_dot(ns_v2_sub(p, a), e) / len2, 0.0f, 1.0f);
    const ns_v2 d = ns_v2_sub(p, ns_v2_add(a, ns_v2_scale(e, t)));
    return ns_v2_dot(d, d);
}

/* Distance d'un point au triangle PLEIN, au carré. */
static float tri_dist2(ns_v2 a, ns_v2 b, ns_v2 c, ns_v2 p)
{
    float best = seg_dist2(a, b, p);
    float other = seg_dist2(b, c, p);
    if (other < best) best = other;
    other = seg_dist2(c, a, p);
    if (other < best) best = other;
    if (best <= 0.0f) return 0.0f;

    /*
     * L'intérieur, par le signe des trois produits en croix.
     *
     * LE TRIANGLE PLAT EST LE CAS NORMAL ICI, pas une exception : toute face
     * verticale se projette en plan sur un segment. Ce test ne le trahit pas, et
     * la raison mérite d'être écrite parce qu'elle n'est pas évidente. Pour trois
     * points alignés, les trois produits valent la distance signée du point à la
     * droite multipliée par (B−A), (C−B) et (A−C) — trois facteurs dont la somme
     * est NULLE. Ils ne peuvent donc jamais partager un même signe strict : la
     * condition ne se déclenche pas, et c'est la distance aux arêtes qui répond,
     * ce qui est exactement juste. Un point pile sur la droite, lui, est déjà
     * sorti plus haut avec une distance nulle.
     *
     * Ce qui protège vraiment du désastre — un mur de neuf mètres barrant un
     * carré de neuf mètres de côté — reste de mesurer une DISTANCE plutôt que de
     * remplir une boîte englobante. Le test d'intérieur n'est là que pour les
     * triangles qui en ont un.
     */
    const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    if (d1 > 0.0f && d2 > 0.0f && d3 > 0.0f) return 0.0f;
    if (d1 < 0.0f && d2 < 0.0f && d3 < 0.0f) return 0.0f;
    return best;
}

void reach_mask_triangle(const reach_grid *g, unsigned char *mask,
                         ns_v2 a, ns_v2 b, ns_v2 c)
{
    /*
     * Le critère est « le triangle passe à moins d'une demi-diagonale du centre »
     * plutôt que « il recouvre le carré » : le disque circonscrit contient le
     * carré, donc on marque un sur-ensemble du vrai recouvrement. On ne rate
     * jamais un obstacle, et on en déborde d'au plus 3,5 cm au pas de 5 cm — très
     * en deçà des 64 cm qu'il faut pour passer.
     */
    const float reach = g->cell * 0.70711f;
    const float reach2 = reach * reach;

    float mnx = a.x, mxx = a.x, mnz = a.y, mxz = a.y;
    if (b.x < mnx) mnx = b.x;
    if (c.x < mnx) mnx = c.x;
    if (b.x > mxx) mxx = b.x;
    if (c.x > mxx) mxx = c.x;
    if (b.y < mnz) mnz = b.y;
    if (c.y < mnz) mnz = c.y;
    if (b.y > mxz) mxz = b.y;
    if (c.y > mxz) mxz = c.y;

    int ix0, iz0, ix1, iz1;
    span_of(g, mnx, mnz, mxx, mxz, reach, &ix0, &iz0, &ix1, &iz1);

    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            const size_t k = reach_index(g, ix, iz);
            if (mask[k]) continue;
            float cx, cz;
            reach_grid_centre(g, ix, iz, &cx, &cz);
            if (tri_dist2(a, b, c, ns_v2_make(cx, cz)) <= reach2) mask[k] = 1;
        }
    }
}

void reach_mask_carve(const reach_grid *g, unsigned char *mask,
                      ns_v2 a, ns_v2 b, float half_width)
{
    ns_v2 axis = ns_v2_sub(b, a);
    const float len = ns_v2_len(axis);
    if (len < 1e-6f) return;
    axis = ns_v2_scale(axis, 1.0f / len);
    const ns_v2 normal = ns_v2_make(-axis.y, axis.x);

    const ns_v2 mid = ns_v2_make((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float rx = fabsf(axis.x) * len * 0.5f + fabsf(normal.x) * half_width;
    const float rz = fabsf(axis.y) * len * 0.5f + fabsf(normal.y) * half_width;

    int ix0, iz0, ix1, iz1;
    span_of(g, mid.x - rx, mid.y - rz, mid.x + rx, mid.y + rz, 0.0f,
            &ix0, &iz0, &ix1, &iz1);

    /*
     * Le CENTRE de la cellule doit tomber dans la baie, là où le marquage retient
     * toute cellule effleurée. Ce n'est pas une inattention : percer large
     * ouvrirait un passage que la géométrie n'a pas, et une porte imaginaire est
     * le seul défaut que ce contrôle ne pourrait pas rattraper — il déclarerait
     * la salle traversable et se tairait.
     */
    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            float cx, cz;
            reach_grid_centre(g, ix, iz, &cx, &cz);
            const ns_v2 rel = ns_v2_make(cx - mid.x, cz - mid.y);
            if (fabsf(ns_v2_dot(rel, axis))   > len * 0.5f)  continue;
            if (fabsf(ns_v2_dot(rel, normal)) > half_width)  continue;
            mask[reach_index(g, ix, iz)] = 0;
        }
    }
}

void reach_grid_block_segment(reach_grid *g, ns_v2 a, ns_v2 b, float half_width,
                              float cap_a, float cap_b)
{
    reach_mask_segment(g, g->solid, a, b, half_width, cap_a, cap_b);
}

void reach_grid_block_triangle(reach_grid *g, ns_v2 a, ns_v2 b, ns_v2 c)
{
    reach_mask_triangle(g, g->solid, a, b, c);
}

/* --------------------------------------------------------------------------
 * Le dehors
 * -------------------------------------------------------------------------- */

bool reach_point_in_polygon(const ns_v2 *poly, size_t count, float x, float z)
{
    bool in = false;
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        if ((poly[i].y > z) != (poly[j].y > z)) {
            const float t = (z - poly[i].y) / (poly[j].y - poly[i].y);
            if (x < poly[i].x + t * (poly[j].x - poly[i].x)) in = !in;
        }
    }
    return in;
}

float reach_point_segment_distance(ns_v2 a, ns_v2 b, float x, float z)
{
    return sqrtf(seg_dist2(a, b, ns_v2_make(x, z)));
}

void reach_grid_close_outside(reach_grid *g, const reach_contour *contours, size_t count)
{
    if (count == 0) return;

    /* L'emprise de chaque contour, élargie de son épaisseur : quatre
     * comparaisons écartent d'emblée la quasi-totalité de la rue, et seul ce qui
     * reste vaut une distance au contour. Sans ce pré-filtre, cette passe coûte
     * plus cher que tout le reste du remplissage réuni. */
    float *box = (float *)malloc(count * 4 * sizeof(float));
    if (!box) tool_fatalf("reach_grid : mémoire épuisée (emprises des contours)");
    for (size_t c = 0; c < count; ++c) {
        const reach_contour *k = &contours[c];
        float lo_x = k->points[0].x, hi_x = k->points[0].x;
        float lo_z = k->points[0].y, hi_z = k->points[0].y;
        for (size_t i = 1; i < k->count; ++i) {
            if (k->points[i].x < lo_x) lo_x = k->points[i].x;
            if (k->points[i].x > hi_x) hi_x = k->points[i].x;
            if (k->points[i].y < lo_z) lo_z = k->points[i].y;
            if (k->points[i].y > hi_z) hi_z = k->points[i].y;
        }
        box[c * 4 + 0] = lo_x - k->thickness;
        box[c * 4 + 1] = lo_z - k->thickness;
        box[c * 4 + 2] = hi_x + k->thickness;
        box[c * 4 + 3] = hi_z + k->thickness;
    }

    for (int iz = 0; iz < g->nz; ++iz) {
        for (int ix = 0; ix < g->nx; ++ix) {
            const size_t k = reach_index(g, ix, iz);
            if (g->solid[k]) continue;
            float cx, cz;
            reach_grid_centre(g, ix, iz, &cx, &cz);

            bool inside = false;
            for (size_t c = 0; c < count && !inside; ++c) {
                if (cx < box[c * 4 + 0] || cx > box[c * 4 + 2]
                 || cz < box[c * 4 + 1] || cz > box[c * 4 + 3]) continue;
                const reach_contour *k2 = &contours[c];
                if (reach_point_in_polygon(k2->points, k2->count, cx, cz)) {
                    inside = true;
                    break;
                }
                /* Dehors, mais dans l'épaisseur du mur : c'est le mur, pas la
                 * rue — et si la cellule n'est pas déjà pleine, c'est qu'une
                 * baie y passe. Voir l'en-tête : c'est ce cas-là qui rendait
                 * deux pièces mitoyennes incommunicables. */
                const float half = k2->thickness * 0.5f;
                for (size_t i = 0; i < k2->count && !inside; ++i) {
                    const ns_v2 a = k2->points[i];
                    const ns_v2 b = k2->points[(i + 1) % k2->count];
                    inside = reach_point_segment_distance(a, b, cx, cz) <= half;
                }
            }
            if (!inside) g->solid[k] = 1;
        }
    }
    free(box);
}

/* --------------------------------------------------------------------------
 * Érosion par le corps, puis composantes connexes
 * -------------------------------------------------------------------------- */

void reach_grid_solve(reach_grid *g, float body_radius)
{
    const size_t n = (size_t)g->nx * (size_t)g->nz;

    /*
     * Le disque du corps, en décalages de cellules, calculé une fois.
     *
     * `need` est le rayon MOINS un pas de grille : c'est la tolérance de
     * discrétisation rendue au joueur, et l'en-tête dit ce qu'elle coûte. Sans
     * elle, une porte à la largeur exacte du corps serait refusée selon l'endroit
     * où la grille tombe — un contrôle dont le verdict dépend de l'alignement de
     * sa propre grille ne serait pas cru longtemps.
     */
    const float need = body_radius - g->cell;
    int *offsets = NULL;
    size_t offset_count = 0;

    if (need > 0.0f) {
        const int span = (int)floorf(need / g->cell);
        const float span2 = (need / g->cell) * (need / g->cell);
        const size_t cap = (size_t)(2 * span + 1) * (size_t)(2 * span + 1);
        offsets = (int *)malloc(cap * 2 * sizeof(int));
        if (!offsets) tool_fatalf("reach_grid : mémoire épuisée (disque du corps)");
        for (int dz = -span; dz <= span; ++dz) {
            for (int dx = -span; dx <= span; ++dx) {
                if ((float)(dx * dx + dz * dz) > span2) continue;
                offsets[offset_count * 2 + 0] = dx;
                offsets[offset_count * 2 + 1] = dz;
                offset_count++;
            }
        }
    }

    for (int iz = 0; iz < g->nz; ++iz) {
        for (int ix = 0; ix < g->nx; ++ix) {
            const size_t k = reach_index(g, ix, iz);
            g->label[k] = -1;
            if (g->solid[k]) { g->walkable[k] = 0; continue; }

            bool fits = true;
            for (size_t o = 0; o < offset_count; ++o) {
                const int jx = ix + offsets[o * 2 + 0];
                const int jz = iz + offsets[o * 2 + 1];
                /* Hors grille = plein. La grille porte une marge, donc le cas ne
                 * se produit qu'au ras du cadre — et un remplissage qui
                 * s'échapperait par le bord mesurerait le vide autour de la
                 * salle, ce qui est le contraire de ce qu'on cherche. */
                if (jx < 0 || jz < 0 || jx >= g->nx || jz >= g->nz) { fits = false; break; }
                if (g->solid[reach_index(g, jx, jz)]) { fits = false; break; }
            }
            g->walkable[k] = fits ? (unsigned char)1 : (unsigned char)0;
        }
    }
    free(offsets);

    /* Étiquetage, voisinage à quatre, avec une pile explicite : une salle de
     * 80 000 cellules ferait déborder la pile d'appel d'un remplissage récursif,
     * et le symptôme — un plantage — n'aurait aucun rapport visible avec sa
     * cause. */
    int *stack = (int *)malloc(n * sizeof(int));
    if (!stack) tool_fatalf("reach_grid : mémoire épuisée (pile de %zu cellules)", n);

    free(g->component_cells);
    g->component_cells = NULL;
    g->components = 0;
    size_t capacity = 0;

    for (int sz = 0; sz < g->nz; ++sz) {
        for (int sx = 0; sx < g->nx; ++sx) {
            const size_t seed = reach_index(g, sx, sz);
            if (!g->walkable[seed] || g->label[seed] >= 0) continue;

            if ((size_t)g->components >= capacity) {
                capacity = capacity ? capacity * 2 : 64;
                size_t *grown = (size_t *)realloc(g->component_cells,
                                                  capacity * sizeof(size_t));
                if (!grown) tool_fatalf("reach_grid : mémoire épuisée (composantes)");
                g->component_cells = grown;
            }
            const int id = g->components++;

            size_t top = 0, cells = 0;
            stack[top++] = (int)seed;
            g->label[seed] = id;
            while (top > 0) {
                const int cur = stack[--top];
                cells++;
                const int cx = cur % g->nx, cz = cur / g->nx;
                static const int step_x[4] = { 1, -1, 0, 0 };
                static const int step_z[4] = { 0, 0, 1, -1 };
                for (int s = 0; s < 4; ++s) {
                    const int jx = cx + step_x[s], jz = cz + step_z[s];
                    if (jx < 0 || jz < 0 || jx >= g->nx || jz >= g->nz) continue;
                    const size_t j = reach_index(g, jx, jz);
                    if (!g->walkable[j] || g->label[j] >= 0) continue;
                    g->label[j] = id;
                    stack[top++] = (int)j;
                }
            }
            g->component_cells[id] = cells;
        }
    }
    free(stack);
}

/* --------------------------------------------------------------------------
 * Lecture du résultat
 * -------------------------------------------------------------------------- */

int reach_grid_label_at(const reach_grid *g, float x, float z)
{
    int ix = 0, iz = 0;
    if (!reach_grid_cell_of(g, x, z, &ix, &iz)) return -1;
    return g->label[reach_index(g, ix, iz)];
}

size_t reach_grid_cell_count(const reach_grid *g, int label)
{
    if (label < 0 || label >= g->components || !g->component_cells) return 0;
    return g->component_cells[label];
}

float reach_grid_area(const reach_grid *g, int label)
{
    return (float)reach_grid_cell_count(g, label) * g->cell * g->cell;
}

int reach_grid_label_in_box(const reach_grid *g, float min_x, float min_z,
                            float max_x, float max_z, float *hit_x, float *hit_z)
{
    int ix0, iz0, ix1, iz1;
    span_of(g, min_x, min_z, max_x, max_z, 0.0f, &ix0, &iz0, &ix1, &iz1);

    /* La PLUS GRANDE composante qui touche la boîte, et non la première
     * rencontrée : une zone qui déborde d'un mur mordrait sinon sur une poche de
     * deux cellules coincée derrière un meuble, et le verdict tiendrait à l'ordre
     * de balayage. */
    int best = -1;
    size_t best_cells = 0;
    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            const int id = g->label[reach_index(g, ix, iz)];
            if (id < 0) continue;
            const size_t cells = reach_grid_cell_count(g, id);
            if (cells <= best_cells) continue;
            best = id;
            best_cells = cells;
            if (hit_x || hit_z) reach_grid_centre(g, ix, iz, hit_x, hit_z);
        }
    }
    return best;
}

void reach_grid_extent(const reach_grid *g, int label,
                       float *min_x, float *min_z, float *max_x, float *max_z)
{
    float lo_x = 0.0f, lo_z = 0.0f, hi_x = 0.0f, hi_z = 0.0f;
    bool any = false;
    for (int iz = 0; iz < g->nz; ++iz) {
        for (int ix = 0; ix < g->nx; ++ix) {
            if (g->label[reach_index(g, ix, iz)] != label) continue;
            float x, z;
            reach_grid_centre(g, ix, iz, &x, &z);
            if (!any) {
                lo_x = hi_x = x;
                lo_z = hi_z = z;
                any = true;
                continue;
            }
            if (x < lo_x) lo_x = x;
            if (x > hi_x) hi_x = x;
            if (z < lo_z) lo_z = z;
            if (z > hi_z) hi_z = z;
        }
    }
    if (min_x) *min_x = lo_x;
    if (min_z) *min_z = lo_z;
    if (max_x) *max_x = hi_x;
    if (max_z) *max_z = hi_z;
}
