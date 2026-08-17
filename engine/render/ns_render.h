/*
 * ns_render.h — le pipeline de rendu de la salle.
 *
 * Chaîne complète, dans l'ordre :
 *
 *   1. G-buffer      géométrie -> albédo/AO, normale/rugosité/métal, émissif, profondeur
 *   2. Visibilité    occlusion ambiante en espace écran, ou lancer de rayons (M4)
 *   3. Éclairage     PBR différé, toutes les lumières de la salle
 *   4. Halo          seuil + flou séparable sur les néons et les écrans
 *   5. Tone mapping  ACES, vignettage, grain -> écran
 *
 * Tout est dimensionné à la résolution de rendu, qui peut différer de celle de
 * la fenêtre (`renderScale`) : c'est le levier de performance le plus efficace
 * sur une machine modeste, et il ne dégrade pas l'interface.
 */
#ifndef NS_RENDER_H
#define NS_RENDER_H

#include "ns_rhi.h"
#include "ns_scene.h"
#include "ns_viewmodel.h"

typedef struct ns_renderer ns_renderer;

/* Niveaux de qualité : un seul réglage que le joueur comprend, dérivé en
 * réglages fins par le moteur. */
typedef enum ns_quality {
    NS_QUALITY_LOW = 0,
    NS_QUALITY_MEDIUM,
    NS_QUALITY_HIGH,
    NS_QUALITY_ULTRA
} ns_quality;

typedef enum ns_raytracing_mode {
    NS_RT_OFF = 0,          /* repli espace écran : SSAO seul */
    NS_RT_SHADOWS,          /* ombres lancées */
    NS_RT_REFLECTIONS,      /* + réflexions */
    NS_RT_FULL              /* + illumination globale à un rebond */
} ns_raytracing_mode;

typedef struct ns_render_settings {
    ns_quality         quality;
    ns_raytracing_mode raytracing;
    float              render_scale;      /* 0.5 à 2.0 */
    float              exposure;
    float              bloom_intensity;
    float              bloom_threshold;
    float              vignette;
    float              grain;
    float              saturation;
    float              chromatic_aberration;
    float              fog_density;
    float              fog_color[3];

    /*
     * Brouillard volumétrique. `volumetric_steps` à 0 le désactive et rend son
     * bloc distant à `lighting.frag` — les deux ne se cumulent jamais, deux
     * brouillards superposés donnant une salle laiteuse.
     */
    int                volumetric_steps;    /* 0 = désactivé */
    float              fog_anisotropy;      /* Henyey-Greenstein, 0 = isotrope */
    float              fog_intensity;
    float              fog_max_distance;    /* longueur de marche, en mètres */
    float              fog_ambient;         /* diffusion de la lumière d'ambiance */

    /*
     * Adaptation d'exposition. `exposure` reste l'exposition de base ; quand
     * `exposure_adapt` est non nul, elle est corrigée par la luminance moyenne
     * mesurée sur l'image précédente, bornée par les deux valeurs suivantes.
     */
    float              exposure_adapt;      /* vitesse, 0 = désactivée */
    float              exposure_min, exposure_max;
    float              ambient[3];
    float              ambient_intensity;
    float              ssao_radius;
    float              ssao_intensity;
    int                ssao_samples;
    int                rt_rays_per_pixel;

    /* Visualisation d'une cible intermédiaire à la place de l'image finale.
     * Indispensable pour diagnostiquer : un écran noir peut venir de six
     * endroits différents de la chaîne, et les regarder un par un est le seul
     * moyen fiable de savoir lequel. */
    enum ns_debug_view {
        NS_DEBUG_NONE = 0,
        NS_DEBUG_ALBEDO,
        NS_DEBUG_NORMAL,
        NS_DEBUG_EMISSIVE,
        NS_DEBUG_DEPTH,
        NS_DEBUG_VISIBILITY,
        NS_DEBUG_HDR,
        NS_DEBUG_BLOOM,
        NS_DEBUG_VOLUMETRIC,
        NS_DEBUG_COUNT
    } debug_view;
} ns_render_settings;

/* Nom lisible d'une vue de débogage, pour l'affichage et la ligne de commande. */
const char *ns_debug_view_name(int view);
int         ns_debug_view_from_name(const char *name);

void ns_render_settings_defaults(ns_render_settings *s, ns_quality quality);

typedef struct ns_camera {
    ns_v3 position;
    ns_v3 forward;
    ns_v3 up;
    float fov_y_degrees;
    float znear, zfar;
} ns_camera;

ns_renderer *ns_renderer_create(ns_rhi *r, const ns_render_settings *settings);
void         ns_renderer_destroy(ns_rhi *r, ns_renderer *rd);

/* Redimensionne les cibles internes. Sans effet si la taille est inchangée. */
bool ns_renderer_resize(ns_rhi *r, ns_renderer *rd, uint32_t width, uint32_t height);

void ns_renderer_set_settings(ns_rhi *r, ns_renderer *rd, const ns_render_settings *s);
const ns_render_settings *ns_renderer_settings(const ns_renderer *rd);

/*
 * Dessine la scène. `target` reçoit l'image finale ; passer la texture de
 * swapchain pour l'affichage, ou une texture hors écran pour une capture.
 * À appeler entre ns_rhi_begin_frame et ns_rhi_end_frame.
 *
 * `viewmodel` peut être NULL : les bras ne sont alors pas dessinés. C'est le cas
 * en caméra libre et en orbite, où ils n'auraient aucun sens. Le moteur dessine
 * la pose, il ne la calcule pas — cette séparation est ce qui lui évite de
 * connaître la machine à états de l'interaction.
 */
void ns_renderer_draw(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
                      const ns_camera *camera, const ns_viewmodel_pose *viewmodel,
                      SDL_GPUTexture *target,
                      uint32_t target_width, uint32_t target_height,
                      double time_seconds);

/* Statistiques de la dernière image, pour l'affichage de débogage et pour la
 * décision automatique de repli du ray tracing. */
typedef struct ns_render_stats {
    uint32_t batches_drawn;
    uint32_t batches_culled;
    uint32_t triangles;
    uint32_t lights_active;
} ns_render_stats;

ns_render_stats ns_renderer_stats(const ns_renderer *rd);

#endif /* NS_RENDER_H */
