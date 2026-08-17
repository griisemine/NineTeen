/*
 * ns_bvh_format.h — format binaire du BVH de la salle.
 *
 * Une seule structure d'accélération sert trois usages, ce qui garantit qu'ils
 * ne divergent jamais :
 *   1. le lancer de rayons du rendu (réflexions, illumination globale, ombres
 *      de contact), traversé en compute shader ;
 *   2. la collision du joueur, en capsule balayée sur le CPU ;
 *   3. l'occlusion audio — un mur entre une source et l'oreille étouffe le son.
 *
 * La disposition mémoire est pensée pour être envoyée telle quelle dans un
 * storage buffer std430 : champs alignés sur 16 octets, pas de padding implicite
 * qui différerait entre le C et le GLSL.
 *
 * Partagé entre l'outil `bvhbake` (écriture) et le moteur (lecture), pour qu'un
 * changement de format casse la compilation des deux côtés plutôt que de
 * produire silencieusement des rayons faux.
 */
#ifndef NS_BVH_FORMAT_H
#define NS_BVH_FORMAT_H

#include <stdint.h>

#define NS_BVH_MAGIC   "NSBVH\0\0\0"      /* 8 octets exactement */
#define NS_BVH_VERSION 1u

/*
 * Nœud : 32 octets. `left_first` désigne le fils gauche pour un nœud interne
 * (le fils droit est toujours left_first+1, ce qui économise un champ) ou le
 * premier triangle pour une feuille. `tri_count` vaut 0 sur un nœud interne.
 */
typedef struct ns_bvh_node {
    float    bmin[3];
    uint32_t left_first;
    float    bmax[3];
    uint32_t tri_count;
} ns_bvh_node;

/*
 * Triangle : 64 octets. On stocke les arêtes précalculées plutôt que les trois
 * sommets — c'est exactement ce que consomme Möller-Trumbore, donc deux
 * soustractions de moins par test d'intersection, sur des millions de rayons.
 * La normale géométrique sert à l'ombrage des rebonds sans avoir à charger les
 * attributs du maillage.
 */
typedef struct ns_bvh_tri {
    float    v0[3];
    uint32_t material;
    float    e1[3];        /* v1 - v0 */
    uint32_t flags;        /* bit 0 : émissif, bit 1 : miroir */
    float    e2[3];        /* v2 - v0 */
    uint32_t _pad0;
    float    normal[3];
    uint32_t _pad1;
} ns_bvh_tri;

#define NS_BVH_TRI_EMISSIVE (1u << 0)
#define NS_BVH_TRI_MIRROR   (1u << 1)

/*
 * Matériau vu par le lancer de rayons : une version condensée du matériau glTF,
 * suffisante pour un rebond diffus ou une réflexion, sans échantillonner de
 * texture. 32 octets.
 */
typedef struct ns_bvh_material {
    float albedo[3];
    float roughness;
    float emissive[3];
    float metallic;
} ns_bvh_material;

typedef struct ns_bvh_header {
    char     magic[8];
    uint32_t version;
    uint32_t node_count;
    uint32_t tri_count;
    uint32_t material_count;
    float    bmin[3];
    float    bmax[3];
    uint32_t max_depth;
    uint32_t _reserved;
} ns_bvh_header;

/* Le fichier est : header, puis nodes, puis triangles, puis matériaux. */

#endif /* NS_BVH_FORMAT_H */
