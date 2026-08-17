/*
 * ns_scene.h — la salle chargée en mémoire GPU.
 *
 * Charge le glTF produit par obj2gltf, ses matériaux PBR, ses textures et les
 * lumières déduites, puis expose ce qu'il faut pour dessiner : un tampon de
 * sommets, un tampon d'indices, une liste de brouillons de dessin triés par
 * matériau, et la liste des lumières.
 *
 * Différence de fond avec la V1 : là où room.c parcourait la scène assimp à
 * chaque image en émettant un glBegin/glEnd par face, tout est ici téléversé
 * une fois au chargement et dessiné par lots.
 */
#ifndef NS_SCENE_H
#define NS_SCENE_H

#include "ns_core.h"
#include "ns_math.h"
#include "ns_bvh.h"
#include "ns_rhi.h"

#define NS_MAX_LIGHTS    128
#define NS_MAX_CABINETS  24

/* ========================================================================== */
/* Format de sommet                                                           */
/* ========================================================================== */
/*
 * 48 octets, entrelacés. L'entrelacement est volontaire : le G-buffer lit les
 * quatre attributs du même sommet, donc les garder contigus évite quatre accès
 * mémoire dispersés par sommet.
 */
typedef struct ns_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4];
} ns_vertex;

/* ========================================================================== */
/* Matériaux                                                                  */
/* ========================================================================== */
/*
 * Aligné pour un storage buffer std430 : les vec3 y sont alignés sur 16 octets,
 * donc on complète explicitement plutôt que de laisser le compilateur C et le
 * compilateur GLSL se mettre d'accord tout seuls — ils ne le font pas.
 */
typedef struct ns_material_gpu {
    float base_color[4];
    float emissive[3];
    float emissive_strength;
    float metallic;
    float roughness;
    int32_t albedo_texture;      /* -1 si absent */
    int32_t normal_texture;
    int32_t orm_texture;
    int32_t _pad[3];
} ns_material_gpu;

/* ========================================================================== */
/* Lumières                                                                   */
/* ========================================================================== */

typedef enum ns_light_type {
    NS_LIGHT_POINT = 0,
    NS_LIGHT_SPOT,
    NS_LIGHT_DIRECTIONAL
} ns_light_type;

typedef struct ns_light_gpu {
    float position[3];
    float range;
    float color[3];
    float intensity;
    float direction[3];
    float spot_cos;
    int32_t type;
    int32_t shadow_index;        /* -1 si la lumière ne projette pas d'ombre */
    float   _pad[2];
} ns_light_gpu;

/* Données CPU associées : ce qui anime la lumière mais n'a pas à monter au GPU. */
typedef struct ns_light_anim {
    bool  flicker;
    bool  screen;      /* écran de borne : pulsation douce plutôt que grésillement */
    float phase;
    float base_intensity;
} ns_light_anim;

/* ========================================================================== */
/* Bornes                                                                     */
/* ========================================================================== */

/*
 * Points d'intérêt du décor. Le modèle contient un billard, un canapé, un bar
 * d'accueil, des radios et des toilettes que la V1 n'utilisait pas : ce n'était
 * que du décor. Les repérer permet d'y accrocher une interaction, une source
 * sonore positionnelle, ou une zone de réverbération distincte.
 */
typedef enum ns_poi_kind {
    NS_POI_NONE = 0,
    NS_POI_BILLIARD,
    NS_POI_SOFA,
    NS_POI_BAR,
    NS_POI_RADIO,
    NS_POI_TOILETS,
    NS_POI_EXIT,
    NS_POI_LEADERBOARD,
    NS_POI_KIND_COUNT
} ns_poi_kind;

typedef struct ns_poi {
    char        name[64];
    ns_poi_kind kind;
    ns_aabb     bounds;
    ns_v3       anchor;       /* où se place le joueur pour interagir */
} ns_poi;

#define NS_MAX_POI 32

typedef struct ns_cabinet {
    char    name[64];
    char    game[32];
    char    difficulty[16];
    int     slot;
    ns_aabb bounds;
    ns_v3   screen_center;       /* point d'où part la lumière de l'écran */
    ns_v3   screen_normal;       /* vers où la borne regarde */
    ns_v3   player_anchor;       /* où se place le joueur pour jouer */
    bool    attract;
} ns_cabinet;

/* ========================================================================== */
/* Lots de dessin                                                             */
/* ========================================================================== */

typedef struct ns_draw_batch {
    uint32_t first_index;
    uint32_t index_count;
    int32_t  material;
    ns_aabb  bounds;             /* pour l'élimination par frustum */
} ns_draw_batch;

/* ========================================================================== */
/* Scène                                                                      */
/* ========================================================================== */

typedef struct ns_scene {
    ns_buffer vertices;
    ns_buffer indices;
    ns_buffer materials;         /* storage buffer de ns_material_gpu */

    ns_draw_batch *batches;
    uint32_t       batch_count;

    ns_texture *textures;
    uint32_t    texture_count;

    ns_material_gpu *material_data;
    uint32_t         material_count;

    ns_light_gpu   lights[NS_MAX_LIGHTS];
    ns_light_anim  light_anim[NS_MAX_LIGHTS];
    uint32_t       light_count;

    ns_cabinet cabinets[NS_MAX_CABINETS];
    uint32_t   cabinet_count;

    ns_poi     pois[NS_MAX_POI];
    uint32_t   poi_count;

    ns_aabb  bounds;        /* toute la géométrie, décor lointain compris */
    /*
     * Emprise de la salle jouable, déduite des bornes et des lumières.
     * Distincte de `bounds` : le modèle d'origine contient du décor très
     * éloigné (sol et toit débordants) qui étire la boîte englobante à plus de
     * cinquante mètres, alors que la salle elle-même en fait une vingtaine.
     * Placer une caméra d'après `bounds` la met à l'extérieur, dans le noir.
     */
    ns_aabb  room_bounds;
    uint32_t vertex_count;
    uint32_t index_count;

    /* Substituts utilisés quand une texture manque : évite un test par matériau
     * dans le shader, et surtout évite l'écran noir en cas d'asset absent. */
    ns_texture fallback_white;
    ns_texture fallback_normal;
    ns_texture fallback_orm;

    /* Structure d'accélération : rendu, collision et audio la partagent. */
    ns_bvh bvh;

    ns_arena arena;
} ns_scene;

/*
 * Charge la scène complète : géométrie, matériaux, textures (diffus + cartes
 * générées), lumières déduites, affectation des bornes.
 *
 * `gltf_logical` est un chemin logique ("scene/salle.gltf"). Les fichiers
 * annexes sont trouvés à côté : salle.bin, salle.lights.json, cabinets.json,
 * et les cartes PBR dans materials/.
 */
bool ns_scene_load(ns_rhi *r, ns_scene *out, const char *gltf_logical);
void ns_scene_unload(ns_rhi *r, ns_scene *s);

/* Fait vivre l'éclairage : scintillement des néons, pulsation des écrans. */
void ns_scene_animate_lights(ns_scene *s, double time_seconds);

/* Trouve la borne la plus proche d'un point, dans un rayon donné. */
const ns_cabinet *ns_scene_nearest_cabinet(const ns_scene *s, ns_v3 position, float max_distance);

/* Point d'intérêt le plus proche (billard, bar, canapé, radio…). */
const ns_poi *ns_scene_nearest_poi(const ns_scene *s, ns_v3 position, float max_distance);

/* Libellé lisible d'un type de point d'intérêt, pour l'invite d'interaction. */
const char *ns_poi_label(ns_poi_kind kind);

/* Couleur d'écran associée à un jeu : sert à la lumière que la borne projette
 * devant elle, et au halo de son marquee. */
void ns_game_screen_color(const char *game, float out_rgb[3]);

#endif /* NS_SCENE_H */
