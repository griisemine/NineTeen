/* ns_bvh.c — chargement et traversée CPU de la structure d'accélération. */
#include "ns_bvh.h"

#include <string.h>

/* Profondeur maximale de la pile de traversée. Le BVH de la salle plafonne à 28
 * niveaux ; 64 laisse une marge confortable pour une scène plus lourde, et
 * l'arbre est de toute façon borné à 60 à la construction. */
#define NS_BVH_STACK 64

/* ========================================================================== */
/* Chargement                                                                 */
/* ========================================================================== */

bool ns_bvh_load(ns_rhi *r, ns_bvh *out, const char *logical_path)
{
    NS_ASSERT(out && logical_path);
    SDL_zerop(out);

    /* Arène dimensionnée sur le fichier : 8 Mio pour la salle. On prend large
     * une fois plutôt que de faire croître pendant la lecture. */
    if (!ns_arena_init(&out->arena, 64u * 1024u * 1024u, "BVH")) return false;

    size_t size = 0;
    uint8_t *data = (uint8_t *)ns_file_read_all(&out->arena, logical_path, &size);
    if (!data) {
        NS_ERROR("BVH introuvable : %s", logical_path);
        ns_arena_free(&out->arena);
        return false;
    }

    if (size < sizeof(ns_bvh_header)) {
        NS_ERROR("BVH tronqué : %zu octets", size);
        ns_arena_free(&out->arena);
        return false;
    }

    const ns_bvh_header *hdr = (const ns_bvh_header *)data;
    if (SDL_memcmp(hdr->magic, NS_BVH_MAGIC, 8) != 0) {
        NS_ERROR("BVH : signature invalide");
        ns_arena_free(&out->arena);
        return false;
    }
    if (hdr->version != NS_BVH_VERSION) {
        NS_ERROR("BVH version %u, attendu %u — relancer bvhbake", hdr->version, NS_BVH_VERSION);
        ns_arena_free(&out->arena);
        return false;
    }

    /* Vérifier que le fichier contient bien ce que l'en-tête annonce : un
     * fichier tronqué produirait sinon des lectures hors bornes silencieuses
     * pendant la traversée. */
    const size_t expected = sizeof(ns_bvh_header)
                          + (size_t)hdr->node_count * sizeof(ns_bvh_node)
                          + (size_t)hdr->tri_count * sizeof(ns_bvh_tri)
                          + (size_t)hdr->material_count * sizeof(ns_bvh_material);
    if (size < expected) {
        NS_ERROR("BVH incomplet : %zu octets pour %zu annoncés", size, expected);
        ns_arena_free(&out->arena);
        return false;
    }

    uint8_t *cursor = data + sizeof(ns_bvh_header);
    out->nodes = (ns_bvh_node *)cursor;
    cursor += (size_t)hdr->node_count * sizeof(ns_bvh_node);
    out->tris = (ns_bvh_tri *)cursor;
    cursor += (size_t)hdr->tri_count * sizeof(ns_bvh_tri);
    out->materials = (ns_bvh_material *)cursor;

    out->node_count     = hdr->node_count;
    out->tri_count      = hdr->tri_count;
    out->material_count = hdr->material_count;
    out->max_depth      = hdr->max_depth;
    out->bounds.min = ns_v3_make(hdr->bmin[0], hdr->bmin[1], hdr->bmin[2]);
    out->bounds.max = ns_v3_make(hdr->bmax[0], hdr->bmax[1], hdr->bmax[2]);

    /* Copie GPU. */
    bool ok = true;
    ok = ok && ns_buffer_create(r, &out->gpu_nodes, NS_BUFFER_STORAGE,
                                (uint32_t)(hdr->node_count * sizeof(ns_bvh_node)), "BVH nœuds");
    ok = ok && ns_buffer_create(r, &out->gpu_tris, NS_BUFFER_STORAGE,
                                (uint32_t)(hdr->tri_count * sizeof(ns_bvh_tri)), "BVH triangles");
    ok = ok && ns_buffer_create(r, &out->gpu_materials, NS_BUFFER_STORAGE,
                                (uint32_t)(ns_maxf(1.0f, (float)hdr->material_count)
                                           * sizeof(ns_bvh_material)), "BVH matériaux");
    ok = ok && ns_buffer_upload(r, &out->gpu_nodes, out->nodes,
                                (uint32_t)(hdr->node_count * sizeof(ns_bvh_node)), 0);
    ok = ok && ns_buffer_upload(r, &out->gpu_tris, out->tris,
                                (uint32_t)(hdr->tri_count * sizeof(ns_bvh_tri)), 0);
    if (hdr->material_count) {
        ok = ok && ns_buffer_upload(r, &out->gpu_materials, out->materials,
                                    (uint32_t)(hdr->material_count * sizeof(ns_bvh_material)), 0);
    }

    if (!ok) {
        NS_ERROR("téléversement du BVH impossible");
        ns_bvh_unload(r, out);
        return false;
    }

    out->loaded = true;
    NS_INFO("BVH : %u nœuds, %u triangles, %u matériaux, profondeur %u (%.1f Mio)",
            out->node_count, out->tri_count, out->material_count, out->max_depth,
            (double)size / (1024.0 * 1024.0));
    return true;
}

void ns_bvh_unload(ns_rhi *r, ns_bvh *b)
{
    if (!b) return;
    ns_buffer_destroy(r, &b->gpu_nodes);
    ns_buffer_destroy(r, &b->gpu_tris);
    ns_buffer_destroy(r, &b->gpu_materials);
    ns_arena_free(&b->arena);
    SDL_zerop(b);
}

/* ========================================================================== */
/* Traversée                                                                  */
/* ========================================================================== */

static inline ns_aabb node_bounds(const ns_bvh_node *n)
{
    ns_aabb b;
    b.min = ns_v3_make(n->bmin[0], n->bmin[1], n->bmin[2]);
    b.max = ns_v3_make(n->bmax[0], n->bmax[1], n->bmax[2]);
    return b;
}

/* Inverse de la direction, avec un infini contrôlé sur les composantes nulles :
 * la méthode des dalles gère correctement les infinis, mais pas les NaN issus
 * de 0/0. */
static inline ns_v3 safe_inverse(ns_v3 d)
{
    const float e = 1e-8f;
    return ns_v3_make(1.0f / ((fabsf(d.x) > e) ? d.x : copysignf(e, d.x)),
                      1.0f / ((fabsf(d.y) > e) ? d.y : copysignf(e, d.y)),
                      1.0f / ((fabsf(d.z) > e) ? d.z : copysignf(e, d.z)));
}

ns_ray_hit ns_bvh_raycast(const ns_bvh *b, ns_v3 origin, ns_v3 dir, float max_distance)
{
    ns_ray_hit hit;
    SDL_zero(hit);
    hit.t = max_distance;
    if (!b || !b->loaded || b->node_count == 0) return hit;

    const ns_v3 inv = safe_inverse(dir);

    uint32_t stack[NS_BVH_STACK];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        const uint32_t index = stack[--sp];
        const ns_bvh_node *node = &b->nodes[index];

        float tbox = 0.0f;
        if (!ns_ray_aabb(origin, inv, node_bounds(node), hit.t, &tbox)) continue;
        /* Un nœud dont l'entrée est plus loin que le meilleur contact déjà
         * trouvé ne peut rien améliorer. */
        if (tbox > hit.t) continue;

        if (node->tri_count > 0) {
            for (uint32_t i = 0; i < node->tri_count; ++i) {
                const uint32_t ti = node->left_first + i;
                if (ti >= b->tri_count) break;
                const ns_bvh_tri *tri = &b->tris[ti];

                const ns_v3 v0 = ns_v3_make(tri->v0[0], tri->v0[1], tri->v0[2]);
                const ns_v3 v1 = ns_v3_add(v0, ns_v3_make(tri->e1[0], tri->e1[1], tri->e1[2]));
                const ns_v3 v2 = ns_v3_add(v0, ns_v3_make(tri->e2[0], tri->e2[1], tri->e2[2]));

                float t = 0.0f, u = 0.0f, v = 0.0f;
                if (ns_ray_triangle(origin, dir, v0, v1, v2, hit.t, &t, &u, &v)) {
                    hit.hit = true;
                    hit.t = t;
                    hit.triangle = ti;
                    hit.material = tri->material;
                    hit.normal = ns_v3_make(tri->normal[0], tri->normal[1], tri->normal[2]);
                }
            }
        } else {
            /* Empiler les deux fils. On ne trie pas ici : le test d'entrée
             * ci-dessus élague déjà l'essentiel, et trier coûterait plus que
             * ça ne rapporte sur un arbre de cette taille. */
            if (sp + 2 <= NS_BVH_STACK) {
                stack[sp++] = node->left_first;
                stack[sp++] = node->left_first + 1;
            }
        }
    }

    if (hit.hit) {
        hit.position = ns_v3_add(origin, ns_v3_scale(dir, hit.t));
        /* Orienter la normale vers le rayon : la géométrie d'origine n'est pas
         * cohérente en orientation de faces. */
        if (ns_v3_dot(hit.normal, dir) > 0.0f) hit.normal = ns_v3_neg(hit.normal);
    }
    return hit;
}

bool ns_bvh_occluded(const ns_bvh *b, ns_v3 origin, ns_v3 dir, float max_distance)
{
    if (!b || !b->loaded || b->node_count == 0) return false;

    const ns_v3 inv = safe_inverse(dir);

    uint32_t stack[NS_BVH_STACK];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        const uint32_t index = stack[--sp];
        const ns_bvh_node *node = &b->nodes[index];

        float tbox = 0.0f;
        if (!ns_ray_aabb(origin, inv, node_bounds(node), max_distance, &tbox)) continue;

        if (node->tri_count > 0) {
            for (uint32_t i = 0; i < node->tri_count; ++i) {
                const uint32_t ti = node->left_first + i;
                if (ti >= b->tri_count) break;
                const ns_bvh_tri *tri = &b->tris[ti];
                const ns_v3 v0 = ns_v3_make(tri->v0[0], tri->v0[1], tri->v0[2]);
                const ns_v3 v1 = ns_v3_add(v0, ns_v3_make(tri->e1[0], tri->e1[1], tri->e1[2]));
                const ns_v3 v2 = ns_v3_add(v0, ns_v3_make(tri->e2[0], tri->e2[1], tri->e2[2]));
                float t, u, v;
                /* Premier contact suffit : on sort immédiatement. */
                if (ns_ray_triangle(origin, dir, v0, v1, v2, max_distance, &t, &u, &v)) return true;
            }
        } else if (sp + 2 <= NS_BVH_STACK) {
            stack[sp++] = node->left_first;
            stack[sp++] = node->left_first + 1;
        }
    }
    return false;
}

/* ========================================================================== */
/* Collision du joueur                                                        */
/* ========================================================================== */
/*
 * Déplacement en trois itérations avec glissement. À chaque itération : on lance
 * un rayon dans la direction du mouvement depuis plusieurs hauteurs de la
 * capsule ; en cas de contact, on projette le mouvement restant sur le plan de
 * la surface pour glisser au lieu de s'arrêter net.
 *
 * Trois itérations suffisent : elles couvrent le coin (deux murs) et le coin
 * plus le sol. Au-delà, on préfère arrêter le mouvement que boucler.
 */
ns_v3 ns_bvh_move_capsule(const ns_bvh *b, ns_v3 position, ns_v3 motion,
                          float radius, float height, bool *out_grounded)
{
    if (out_grounded) *out_grounded = false;
    if (!b || !b->loaded) return ns_v3_add(position, motion);

    /* Trois hauteurs de sonde : pieds, taille, tête. Une seule sonde centrale
     * laisserait passer les obstacles bas (marches, socles de bornes). */
    const float probe_heights[3] = { radius * 1.05f, height * 0.5f, height - radius * 1.05f };

    ns_v3 remaining = motion;
    for (int iter = 0; iter < 3; ++iter) {
        const float dist = ns_v3_len(remaining);
        if (dist < 1e-5f) break;
        const ns_v3 dir = ns_v3_scale(remaining, 1.0f / dist);

        float nearest = dist + radius;
        ns_v3  nearest_normal = ns_v3_zero();
        bool   blocked = false;

        for (int p = 0; p < 3; ++p) {
            const ns_v3 origin = ns_v3_add(position, ns_v3_make(0.0f, probe_heights[p], 0.0f));
            const ns_ray_hit h = ns_bvh_raycast(b, origin, dir, dist + radius);
            if (h.hit && h.t < nearest) {
                nearest = h.t;
                nearest_normal = h.normal;
                blocked = true;
            }
        }

        if (!blocked) {
            position = ns_v3_add(position, remaining);
            break;
        }

        /* Avancer jusqu'au contact, en gardant l'épaisseur de la capsule. */
        const float allowed = ns_maxf(0.0f, nearest - radius);
        position = ns_v3_add(position, ns_v3_scale(dir, allowed));

        /* Glissement : retirer la composante du mouvement qui entre dans la
         * surface. C'est ce qui permet de longer un mur au lieu de coller. */
        ns_v3 rest = ns_v3_scale(dir, dist - allowed);
        rest = ns_v3_sub(rest, ns_v3_scale(nearest_normal, ns_v3_dot(rest, nearest_normal)));
        remaining = rest;
    }

    /* Contact avec le sol : sonde vers le bas depuis les genoux. */
    const ns_v3 foot = ns_v3_add(position, ns_v3_make(0.0f, radius * 1.05f, 0.0f));
    const ns_ray_hit ground = ns_bvh_raycast(b, foot, ns_v3_make(0.0f, -1.0f, 0.0f), radius * 1.6f);
    if (ground.hit) {
        if (out_grounded) *out_grounded = true;
        /* Recoller au sol évite l'accumulation de micro-chutes qui finit par
         * faire passer le joueur au travers. */
        position.y = ground.position.y;
    }
    return position;
}

/* ========================================================================== */
/* Occlusion audio                                                            */
/* ========================================================================== */

float ns_bvh_occlusion_factor(const ns_bvh *b, ns_v3 from, ns_v3 to)
{
    if (!b || !b->loaded) return 1.0f;

    const ns_v3 delta = ns_v3_sub(to, from);
    const float dist = ns_v3_len(delta);
    if (dist < 1e-4f) return 1.0f;
    const ns_v3 dir = ns_v3_scale(delta, 1.0f / dist);

    /* Trois rayons légèrement écartés plutôt qu'un seul : une source juste
     * derrière l'arête d'une borne doit être partiellement audible, pas coupée
     * net. Le son contourne les obstacles, un test binaire s'entend. */
    ns_v3 up = (fabsf(dir.y) < 0.9f) ? ns_v3_make(0, 1, 0) : ns_v3_make(1, 0, 0);
    const ns_v3 side = ns_v3_norm(ns_v3_cross(dir, up));
    up = ns_v3_cross(side, dir);

    const float spread = 0.35f;      /* écartement des rayons, en mètres */
    const ns_v3 origins[3] = {
        from,
        ns_v3_add(from, ns_v3_scale(side, spread)),
        ns_v3_add(from, ns_v3_scale(up, spread)),
    };

    int clear = 0;
    for (int i = 0; i < 3; ++i) {
        if (!ns_bvh_occluded(b, origins[i], dir, dist - 0.05f)) clear++;
    }

    /* Un trajet complètement bloqué laisse tout de même passer un peu de son :
     * les cloisons ne sont pas des isolants parfaits, et couper à zéro s'entend
     * comme un bug. */
    return 0.18f + 0.82f * ((float)clear / 3.0f);
}
