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

struct ns_skin;

#include "ns_rhi.h"
#include "ns_scene.h"
#include "ns_particles.h"
#include "ns_viewmodel.h"

typedef struct ns_renderer ns_renderer;

/* Niveaux de qualité : un seul réglage que le joueur comprend, dérivé en
 * réglages fins par le moteur. */
typedef enum ns_quality {
    /*
     * Sous `low`, pour les GPU intégrés anciens et les machines de bureau.
     *
     * Ce n'est pas « low avec un chiffre de plus » : il coupe les deux passes
     * dont le coût ne dépend pas du contenu — le volumétrique et l'occlusion
     * ambiante — et rend à 60 % de la résolution. Ce qui reste est l'éclairage
     * direct, les écrans et le tone mapping, c'est-à-dire la salle, en moins
     * beau mais entière.
     */
    NS_QUALITY_POTATO = 0,
    NS_QUALITY_LOW,
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

    /*
     * Le verre bombé d'une borne : courbure du tube, force des lignes de
     * balayage, brillance de la vitre.
     *
     * Les deux premières existent dans `assets/scene/cabinets.json` depuis M4,
     * sous les noms `curvature` et `scanlineStrength`, et n'avaient jamais été
     * lues par personne. La troisième est neuve : c'est elle qui rend la vitre
     * lisse et légèrement métallique, donc capable de refléter les néons — et
     * c'est ce reflet qui fait comprendre qu'il y a un écran DERRIÈRE quelque
     * chose, plutôt qu'une image peinte sur une planche.
     */
    /* Densité de poussière, 0 à 1. C'est le levier du palier de qualité, et à 0
     * le système n'est pas seulement invisible : il n'est pas créé. */
    float              particle_density;

    float              screen_curvature;
    float              screen_scanlines;
    float              screen_glass;
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


/*
 * Combien de dalles peuvent être vivantes en même temps.
 *
 * Quatre, et le chiffre vient d'un usage : pendant qu'on joue sur une borne, la
 * borne de classement doit continuer d'afficher les scores — c'est même le seul
 * moment où on a envie de la regarder. Une seule dalle vivante ne le permettait
 * pas.
 */
/*
 * Dalles vivantes simultanees. C'etait 4 — une partie, le classement, et deux
 * de marge — et ca suffisait tant que dix-huit bornes sur dix-neuf affichaient
 * une image fixe. Depuis `room_attract`, elles jouent TOUTES : il en faut une
 * par borne, plus le classement.
 */
#define NS_MAX_LIVE_SCREENS 28

/*
 * Le PERSONNAGE : trente-deux os au plus.
 *
 * La borne est celle du budget d'uniforme, pas une limite du format glTF :
 * trente-deux matrices font 2 Kio, ce que la contrainte de push d'uniformes
 * accepte sur toutes les cibles. Un humanoïde sans doigts en demande une
 * vingtaine — celui du jeu en a dix-neuf.
 */
#define NS_MAX_CHARACTER_JOINTS 32

/*
 * COMBIEN DE CORPS À LA FOIS, et le calcul qui donne ce nombre-là.
 *
 * Dix-sept : le joueur, plus les seize pairs que `NS_RT_MAX_PEERS` autorise. Ce
 * n'est donc pas un plafond, c'est la borne du réseau recopiée — le rendu
 * n'écarte aucun pair que la présence aurait accepté. Le moteur ne peut pas
 * inclure `ns_realtime.h` (il ne dépend pas de son propre réseau), d'où la
 * valeur écrite ici et la raison écrite avec.
 *
 * CE QUE ÇA COÛTE, MESURÉ plutôt qu'estimé
 * ----------------------------------------
 * Le maillage ne monte au GPU QU'UNE FOIS — c'est le même personnage pour tout
 * le monde. Ce qui se répète par corps et par image, c'est :
 *
 *   * un bloc d'uniformes de sommet de 2 176 octets — 16 flottants de
 *     `view_proj`, 16 de `model`, puis 32 matrices d'os de 64 octets, soit
 *     2 048 octets à elles seules. C'est ce que `character_vs_ubo` mesure
 *     aujourd'hui, pour UN personnage ;
 *   * un bloc d'uniformes de fragment de 80 octets ;
 *   * un `SDL_DrawGPUIndexedPrimitives` de 4 672 triangles — le compte du
 *     modèle livré, relevé au chargement.
 *
 * À dix-sept corps : 544 matrices d'os par image (34 816 octets de matrices),
 * 36 992 octets d'uniformes de sommet, 1 360 de fragment, 17 appels de dessin et
 * 79 424 triangles.
 *
 * POURQUOI PAS DE TAMPON DE STOCKAGE, et c'est la question qu'il fallait poser
 * ----------------------------------------------------------------------------
 * Parce que rien ne déborde. Le budget qui s'appliquait déjà est celui d'un
 * push d'uniformes PAR APPEL — 4 Kio sur Metal — et 2 176 octets passent
 * dessous avec le même confort qu'avant : dix-sept corps, ce sont dix-sept
 * pushes de 2 176 octets, pas un push de 37 Kio. Le total par image n'est
 * borné par rien d'autre que la bande passante, et 36 Kio devant les mégaoctets
 * de G-buffer que la même image écrit ne se mesurent pas.
 *
 * Un tampon de stockage aurait permis un dessin instancié — un seul appel — et
 * c'est la bonne réponse à un problème qu'on n'a pas. Le coût réel est MESURÉ
 * dans `pass_character` : seize corps ajoutent 0,65 ms à une image qui en prend
 * 24,3, soit 2,7 %. On ne réécrit pas le chemin d'uniformes pour ça.
 */
#define NS_MAX_CHARACTERS 17

/*
 * Ce qu'il faut pour dessiner UN personnage à une image donnée : où il est, et
 * comment son squelette est plié.
 *
 * Séparé du maillage, qui ne change jamais et monte au GPU une fois. C'est le
 * même partage que le viewmodel — la géométrie appartient au moteur, la pose
 * vient de `room/` — et c'est ce qui permet de poser le personnage sans rien
 * savoir du rendu.
 */
typedef struct ns_character_draw {
    bool  visible;
    ns_m4 model;                                  /* repère du personnage -> monde */
    ns_m4 joint[NS_MAX_CHARACTER_JOINTS];
    int   joint_count;
    float tint[3];                                /* multiplié à la texture */
    float roughness, metallic;
    /*
     * L'OPACITÉ, et pourquoi elle existe.
     *
     * En troisième personne, le bras de caméra se raccourcit contre un mur. Dans
     * un couloir — c'est-à-dire au départ du jeu — il se raccourcit jusqu'à
     * mettre l'objectif contre la tempe du personnage, qui remplit alors le
     * tiers de l'image. Le rendre à 1 comme si de rien n'était était le défaut le
     * plus voyant du mode : `room_camera.c` calcule la valeur, ce champ la porte.
     *
     * 1 = plein, et c'est alors EXACTEMENT le rendu d'avant : le mélange avec un
     * alpha de 1 rend la source telle quelle. 0 = rien n'est dessiné, et la passe
     * sort avant d'allumer le GPU.
     *
     * Zéro par `SDL_zero` ne veut pas dire « invisible » par accident : c'est
     * `visible` qui décide, et une pose montée sans toucher ce champ serait
     * effacée. Le remplir est donc OBLIGATOIRE — `room/main.c` le fait pour le
     * joueur comme pour chaque pair, et c'est aussi par lui que passe le fondu
     * de sortie d'un joueur qui se tait.
     */
    float opacity;
} ns_character_draw;

/*
 * Fait afficher `texture` par le matériau `material`, en écrasant son albédo.
 *
 * C'est ce qui met un jeu qui tourne DANS l'écran d'une borne : la couche 2D
 * rend Flappy Bird dans une texture, et cet appel dit au G-buffer d'employer
 * cette texture-là pour le matériau d'écran de la borne où l'on joue. Le reste
 * du moteur n'a rien à savoir — l'écran reste une surface éclairée comme une
 * autre, avec son émissif, sa courbure et son reflet.
 *
 * Une surcharge plutôt qu'une écriture dans la table des matériaux : la scène
 * est chargée une fois et partagée, et une borne qui garderait l'image d'une
 * partie finie serait un défaut qu'on ne verrait qu'en revenant sur ses pas.
 * `material = -1` retire la surcharge.
 */
void ns_renderer_set_screen(ns_renderer *rd, int32_t material, SDL_GPUTexture *texture);

/*
 * Monte le maillage du personnage au GPU. Une fois, au démarrage : le maillage
 * ne change jamais, seules les matrices d'os bougent.
 *
 * Rend `false` et le DIT si quelque chose manque. Un personnage absent n'est pas
 * fatal — le jeu se joue à la première personne, et c'est ce que fait
 * l'appelant.
 */
bool ns_renderer_upload_character(ns_rhi *r, ns_renderer *rd, const struct ns_skin *skin);

/*
 * LES POSES DE L'IMAGE COURANTE — au PLURIEL, et c'est ce qui donne un corps aux
 * autres joueurs.
 *
 * L'appel était au singulier tant qu'il n'y avait qu'un personnage à poser. Il
 * n'y en a plus un : le joueur en troisième personne, et les pairs que la
 * présence rapporte. Un tableau plutôt qu'un appel répété parce que le rendu
 * ouvre UNE SEULE passe et lie le maillage UNE SEULE fois pour tout le monde ;
 * n'y varient que la matrice de modèle et les matrices d'os.
 *
 * `draw` à NULL ou `count` à zéro : rien n'est dessiné, exactement comme avant.
 * Au-delà de `NS_MAX_CHARACTERS` le surplus est ignoré en silence — la borne
 * vaut déjà celle du réseau, et un appelant qui la dépasse s'est trompé de
 * tableau, pas de nombre de joueurs.
 *
 * Les entrées dont `visible` est faux ou dont l'opacité est nulle sont sautées
 * sans coûter un triangle : c'est par là que passe le fondu de sortie d'un pair
 * qui se tait.
 */
void ns_renderer_set_characters(ns_renderer *rd, const ns_character_draw *draw,
                                uint32_t count);

/*
 * Déclare les zones de poussière. Passer `count = 0` éteint le système.
 *
 * La salle les déclare, comme ses lumières et ses points de vue : une zone est
 * une boîte, une densité et un courant d'air, et c'est au décor de dire où l'air
 * est chargé — au-dessus de l'îlot où les faisceaux tombent, pas dans les
 * toilettes.
 */
void ns_renderer_set_particle_zones(ns_renderer *rd, const ns_particle_zone *zones,
                                    uint32_t count, uint64_t seed);

/* Avance la poussière d'un pas fixe. Séparé du dessin : la simulation appartient
 * au pas de jeu, le dessin à l'image, et les deux n'ont pas la même cadence. */
void ns_renderer_tick_particles(ns_renderer *rd, float dt);

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
/*
 * Renvoie false quand RIEN n'a pu être dessiné — cibles de rendu indisponibles,
 * typiquement pendant un redimensionnement. L'appelant ne doit alors PAS
 * présenter l'image : la cible n'a pas été écrite, et la présenter donne une
 * image noire. `ns_rhi_cancel_frame` est là pour ça.
 */
bool ns_renderer_draw(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
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
