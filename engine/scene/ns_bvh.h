/*
 * ns_bvh.h — la structure d'accélération de la salle, côté moteur.
 *
 * Un seul BVH, construit hors-ligne par tools/bvhbake, sert trois besoins :
 *
 *   1. le lancer de rayons du rendu, traversé en compute shader ;
 *   2. la collision du joueur, traversée sur le CPU ;
 *   3. l'occlusion audio — un mur entre une source et l'oreille étouffe le son.
 *
 * Les faire partager la même structure n'est pas qu'une économie de mémoire :
 * c'est la garantie que ce que le joueur voit, ce contre quoi il se cogne et ce
 * qu'il entend décrivent le même monde. Dans la V1, la collision était une liste
 * de boîtes écrites à la main, qui dérivait de la géométrie à chaque retouche
 * du modèle.
 */
#ifndef NS_BVH_H
#define NS_BVH_H

#include "ns_bvh_format.h"
#include "ns_core.h"
#include "ns_math.h"
#include "ns_rhi.h"

typedef struct ns_bvh {
    /* Copie CPU : collision et audio. */
    ns_bvh_node     *nodes;
    ns_bvh_tri      *tris;
    ns_bvh_material *materials;
    uint32_t         node_count, tri_count, material_count;
    ns_aabb          bounds;
    uint32_t         max_depth;

    /* Copie GPU : lancer de rayons. */
    ns_buffer gpu_nodes;
    ns_buffer gpu_tris;
    ns_buffer gpu_materials;

    ns_arena arena;
    bool     loaded;
} ns_bvh;

bool ns_bvh_load(ns_rhi *r, ns_bvh *out, const char *logical_path);
void ns_bvh_unload(ns_rhi *r, ns_bvh *b);

/* ========================================================================== */
/* Requêtes CPU                                                               */
/* ========================================================================== */

typedef struct ns_ray_hit {
    float    t;
    ns_v3    position;
    ns_v3    normal;
    uint32_t triangle;
    uint32_t material;
    bool     hit;
} ns_ray_hit;

/* Rayon le plus proche. `dir` n'a pas besoin d'être normalisé, mais `t` est
 * exprimé dans son unité. */
ns_ray_hit ns_bvh_raycast(const ns_bvh *b, ns_v3 origin, ns_v3 dir, float max_distance);

/* Test d'occultation : s'arrête au premier contact, donc nettement moins cher
 * qu'un raycast complet. C'est ce qu'utilisent les ombres et l'audio. */
bool ns_bvh_occluded(const ns_bvh *b, ns_v3 origin, ns_v3 dir, float max_distance);

/*
 * Déplacement d'une capsule verticale contre la géométrie, avec glissement le
 * long des surfaces. Remplace les boîtes de collision codées en dur de la V1 :
 * on ne traverse plus une borne en l'abordant de biais.
 *
 * Renvoie la position finale ; `out_grounded` indique si l'on repose sur le sol.
 */
ns_v3 ns_bvh_move_capsule(const ns_bvh *b, ns_v3 position, ns_v3 motion,
                          float radius, float height, bool *out_grounded);

/* Atténuation due aux obstacles entre deux points, dans [0,1].
 * 1 = trajet dégagé, 0 = complètement bloqué. Utilisée par l'audio. */
float ns_bvh_occlusion_factor(const ns_bvh *b, ns_v3 from, ns_v3 to);

#endif /* NS_BVH_H */
