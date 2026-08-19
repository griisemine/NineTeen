/* ns_render.c — implémentation du pipeline de rendu. */
#include "ns_render.h"
#include "ns_shaders.h"
#include "ns_particles.h"
#include "ns_viewmodel.h"

#include <string.h>

#define BLOOM_MIPS 5

/* Formats des cibles. Choisis pour la bande passante autant que pour la
 * précision : le G-buffer est relu à chaque pixel par la passe d'éclairage. */
#define FMT_ALBEDO   SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
#define FMT_NORMAL   SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define FMT_EMISSIVE SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define FMT_DEPTH    SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define FMT_HDR      SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define FMT_VIS      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
/* Quatre entiers de 16 bits : par lumière dominante, l'indice sur huit bits de
 * poids fort et la visibilité sur huit bits de poids faible. */
#define FMT_LIGHT_SHADOW SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT

/*
 * Diviseur de résolution du lancer de rayons.
 *
 * Le rendre à pleine résolution était la première cause du jeu à une image par
 * seconde sur un M1 Pro. Ce signal-là est débruité par deux passes à-trous puis
 * accumulé sur plusieurs images : sa fréquence utile est bien inférieure à celle
 * du pixel. À demi-résolution on divise par quatre le nombre de rayons, et la
 * différence ne se voit pas — le filtre l'effaçait déjà.
 */
#define RT_DOWNSCALE 2u

/* ========================================================================== */
/* Blocs d'uniformes — la disposition doit correspondre exactement au GLSL     */
/* ========================================================================== */

typedef struct camera_ubo {
    float view_proj[16];
    float prev_view_proj[16];
    float camera_pos[4];
} camera_ubo;

typedef struct material_ubo {
    float base_color[4];
    float emissive[4];
    float params[4];       /* métal, rugosité, a-t-on une normal map, temps */
    /* Le verre bombé d'une borne : courbure, lignes de balayage, reflet, et un
     * drapeau qui dit si le matériau est un écran. Les deux premières valeurs
     * viennent de `cabinets.json`, où elles dorment depuis M4. */
    float screen[4];
} material_ubo;

typedef struct frame_ubo {
    float   inv_view_proj[16];
    float   camera_pos[4];
    float   ambient[4];
    float   fog[4];
    int32_t counts[4];
} frame_ubo;

typedef struct ssao_ubo {
    float   inv_view_proj[16];
    float   view_proj[16];
    float   camera_pos[4];
    float   settings[4];
    int32_t counts[4];
} ssao_ubo;

typedef struct bloom_threshold_ubo { float settings[4]; } bloom_threshold_ubo;
typedef struct bloom_blur_ubo      { float direction[4]; } bloom_blur_ubo;
typedef struct tonemap_ubo         { float settings[4]; float extra[4]; } tonemap_ubo;
typedef struct debug_ubo           { int32_t mode[4]; float scale[4]; } debug_ubo;

typedef struct denoise_ubo { float step[4]; } denoise_ubo;

typedef struct viewmodel_vs_ubo {
    float view_proj[16];
    float model[16];
    float params[4];        /* longueur du segment, libres */
} viewmodel_vs_ubo;

typedef struct viewmodel_fs_ubo {
    float   base_color[4];  /* rgb + rugosité */
    float   camera[4];      /* xyz + métallicité */
    float   ambient[4];
    int32_t counts[4];
} viewmodel_fs_ubo;

typedef struct exposure_ubo {
    float settings[4];      /* exposition de base, vitesse, min, max */
    float frame[4];         /* dt, première image, libres */
} exposure_ubo;

typedef struct volumetric_ubo {
    float   inv_view_proj[16];
    float   camera_pos[4];
    float   fog[4];
    int32_t config[4];      /* lumières, pas, largeur, hauteur (demi-résolution) */
    float   params[4];      /* anisotropie, distance max, ambiant, libre */
} volumetric_ubo;

typedef struct vol_composite_ubo {
    float size[4];          /* xy : demi-résolution, zw : pleine */
    float settings[4];      /* intensité, sensibilité de profondeur, libres */
} vol_composite_ubo;

typedef struct raytrace_ubo {
    float   inv_view_proj[16];
    float   camera_pos[4];
    float   ambient[4];
    int32_t config[4];      /* lumières, mode, rayons/pixel, n° d'image */
    float   accum[4];       /* poids historique, largeur, hauteur, libre */
} raytrace_ubo;

/* ========================================================================== */

struct ns_renderer {
    ns_render_settings settings;

    uint32_t width, height;             /* résolution de rendu */
    uint32_t target_width, target_height;
    /* Résolution du lancer de rayons. Le signal est débruité puis accumulé dans
     * le temps : le rendre à pleine résolution coûte quatre fois plus pour un
     * résultat que le filtre à-trous efface de toute façon. */
    uint32_t rt_width, rt_height;

    /* Cibles */
    ns_texture gbuffer_albedo;
    ns_texture gbuffer_normal;
    ns_texture gbuffer_emissive;
    ns_texture depth;
    ns_texture hdr;
    ns_texture visibility;          /* SSAO, écrit par une passe de rendu */
    ns_texture rt_visibility[2];    /* ombres + indirect, ping-pong pour l'accumulation */
    ns_texture rt_filtered[2];      /* sortie du débruitage, deux passes à-trous */
    ns_texture reflections;
    /* Ombres des quatre lumières dominantes par pixel : (indice << 8) | visibilité,
     * un canal par lumière. Entière, donc jamais filtrée. */
    ns_texture rt_light_shadow;
    uint32_t   rt_current;          /* index d'écriture du ping-pong */
    uint32_t   accum_frames;        /* images accumulées depuis le dernier mouvement */
    ns_texture *rt_denoised;        /* dernière sortie de débruitage utilisable */
    ns_texture bloom[BLOOM_MIPS];
    ns_texture bloom_tmp[BLOOM_MIPS];

    /* Brouillard volumétrique : marché à demi-résolution, recomposé en pleine.
     * `hdr_fogged` reçoit la composition — on ne peut pas lire et écrire la même
     * cible dans une passe, et le halo comme le tone mapping doivent lire la
     * version brumeuse. */
    ns_texture volumetric;
    ns_texture hdr_fogged;

    /* Profondeur PROPRE au viewmodel. Non négociable : l'accumulation temporelle
     * du lancer de rayons ne reprojette pas, donc des bras écrits dans la
     * profondeur partagée traîneraient dans les ombres à chaque mouvement. */
    ns_texture viewmodel_depth;

    /* Pipelines */
    SDL_GPUGraphicsPipeline *pipe_gbuffer;
    SDL_GPUGraphicsPipeline *pipe_ssao;
    SDL_GPUGraphicsPipeline *pipe_lighting;
    SDL_GPUGraphicsPipeline *pipe_bloom_threshold;
    SDL_GPUGraphicsPipeline *pipe_bloom_blur;
    SDL_GPUGraphicsPipeline *pipe_tonemap;
    SDL_GPUGraphicsPipeline *pipe_debug;
    SDL_GPUComputePipeline  *pipe_raytrace;
    SDL_GPUGraphicsPipeline *pipe_denoise;
    SDL_GPUComputePipeline  *pipe_volumetric;
    SDL_GPUGraphicsPipeline *pipe_vol_composite;
    SDL_GPUComputePipeline  *pipe_exposure;
    SDL_GPUGraphicsPipeline *pipe_viewmodel;
    SDL_GPUTextureFormat     tonemap_format;

    /* Tampon des lumières, réécrit à chaque image (elles scintillent). */
    ns_buffer lights;

    /* Exposition mesurée : un seul flottant, mais il persiste d'une image à
     * l'autre — c'est lui qui porte l'adaptation. */
    ns_buffer exposure;

    /* La poussière en suspension. Créée à la demande : au palier `low` elle
     * n'existe pas du tout, plutôt que d'exister et de ne rien dessiner. */
    ns_particles *particles;

    /* L'écran vivant : quel matériau, et quelle texture à sa place. */
    /*
     * Les dalles VIVANTES : jusqu'à quatre matériaux dont la texture est
     * remplacée à l'image par une cible de rendu.
     *
     * Une seule ne suffisait pas, et le manque s'est vu tout de suite : pendant
     * qu'on joue sur une borne, la borne de CLASSEMENT doit continuer d'afficher
     * les scores — c'est même le seul moment où on a envie de la regarder. Quatre
     * est un compte, pas une limite de principe : chaque entrée coûte une
     * comparaison par lot dessiné.
     */
    struct { int32_t material; SDL_GPUTexture *texture; } screens[NS_MAX_LIVE_SCREENS];
    uint32_t        screen_count;

    /* Les bras : géométrie construite une fois au démarrage, jamais réécrite.
     * Seules les matrices changent, et elles passent par un uniforme. */
    ns_buffer vm_vertices, vm_indices;
    uint32_t  vm_first_index[NS_VM_SEGMENT_COUNT];
    uint32_t  vm_index_count[NS_VM_SEGMENT_COUNT];
    bool      vm_ready;
    double    last_time;
    bool      exposure_primed;

    float prev_view_proj[16];
    ns_render_stats stats;
    bool targets_ready;
    bool reflections_cleared;
};

/* ========================================================================== */
/* Vues de débogage                                                           */
/* ========================================================================== */

static const char *const g_debug_names[NS_DEBUG_COUNT] = {
    "none", "albedo", "normal", "emissive", "depth", "visibility", "hdr", "bloom",
    "volumetric"
};

const char *ns_debug_view_name(int view)
{
    if (view < 0 || view >= NS_DEBUG_COUNT) return "?";
    return g_debug_names[view];
}

int ns_debug_view_from_name(const char *name)
{
    if (!name) return NS_DEBUG_NONE;
    for (int i = 0; i < NS_DEBUG_COUNT; ++i) {
        if (SDL_strcasecmp(name, g_debug_names[i]) == 0) return i;
    }
    return -1;
}

/* ========================================================================== */
/* Réglages par défaut                                                        */
/* ========================================================================== */

void ns_render_settings_defaults(ns_render_settings *s, ns_quality quality)
{
    SDL_zerop(s);
    s->quality = quality;
    s->render_scale = 1.0f;
    s->exposure = 1.35f;
    s->bloom_intensity = 0.7f;
    /* Sous 1.0, les panneaux lumineux du plafond (émissifs à exactement 1.0
     * dans le modèle d'origine) ne fleurissaient pas du tout et ressortaient
     * comme des trous blancs découpés au lieu de sources de lumière. */
    s->bloom_threshold = 0.85f;
    s->vignette = 0.32f;
    s->grain = 0.012f;
    s->saturation = 1.12f;
    s->chromatic_aberration = 0.0f;

    /* Brouillard très léger, teinté de l'ambre des plafonniers : donne de la
     * profondeur au fond de la salle sans laiter l'image.
     *
     * Il était bleu, comme les néons — et c'était l'erreur de raisonnement : le
     * brouillard prend la couleur de ce qui l'éclaire, et ce qui éclaire l'air
     * d'une salle d'arcade, ce sont ses plafonniers, pas les deux tubes accrochés
     * aux murs. Un brouillard froid dans une salle chaude annule le peu de
     * chaleur qu'il reste, et il le fait sur toute la profondeur du cadre. */
    s->fog_density = 0.006f;
    s->fog_color[0] = 0.098f; s->fog_color[1] = 0.070f; s->fog_color[2] = 0.048f;

    /* Diffusion vers l'avant marquée : c'est ce qui distingue un halo d'un voile.
     * La distance de marche couvre la salle (22 m de long) sans la dépasser —
     * marcher plus loin ne coûterait que du temps. */
    s->fog_anisotropy = 0.62f;
    /*
     * L'intensité n'est pas à 1, et ce n'est pas un réglage au jugé.
     *
     * L'éclairage d'une surface passe par un albédo divisé par pi — de l'ordre
     * de 0,25 pour un mur clair, 0,01 pour la moquette noire. La diffusion dans
     * l'air, elle, n'a pas ce facteur : à intensité égale, une source de 500
     * éclaire l'air **plusieurs fois plus** que le mur qu'elle éclaire. Avec 46
     * sources dans un hall de 22 m, la première image sortait entièrement
     * blanche, la salle noyée dans son propre brouillard.
     *
     * Ce coefficient est donc l'albédo de diffusion du milieu, absorbé ici plutôt
     * que réparti dans le shader. La valeur vient de trois captures comparées, pas
     * d'un calcul.
     */
    s->fog_intensity = 0.16f;
    s->fog_max_distance = 26.0f;
    s->fog_ambient = 0.35f;

    /* Adaptation d'exposition : montée lente, l'œil met du temps à s'habituer à
     * la pénombre. Les bornes évitent qu'une salle presque noire soit remontée
     * jusqu'au grain, ou qu'un écran plein cadre éteigne tout le reste. */
    s->exposure_adapt = 1.2f;
    /*
     * Bornes resserrées, et c'est le réglage qui décide de l'ambiance.
     *
     * Une adaptation libre ANNULE l'obscurité : elle voit une salle sombre,
     * pousse l'exposition au maximum, et rend une salle claire — le travail
     * d'éclairage est effacé par la mesure censée le servir. À 1,6 au plafond,
     * l'adaptation lisse encore le passage d'un couloir noir à un écran de borne,
     * mais elle ne peut plus transformer une salle tamisée en salle éclairée.
     */
    /*
     * Le verre bombé. `curvature` et `scanlineStrength` existent dans
     * `cabinets.json` depuis M4 et n'ont jamais été lus ; ce sont leurs valeurs.
     * Le reflet, lui, est neuf : c'est lui qui fait qu'on VOIT la vitre, et donc
     * qu'on comprend qu'il y a un écran derrière plutôt qu'une affiche.
     */
    /*
     * Densité de poussière par palier. `low` n'en a pas du tout — c'est le palier
     * des machines modestes, et un grain de poussière coûte quatre sommets écrits
     * par le CPU à chaque image.
     */
    switch (quality) {
        case NS_QUALITY_POTATO:
        case NS_QUALITY_LOW:    s->particle_density = 0.0f;  break;
        case NS_QUALITY_MEDIUM: s->particle_density = 0.55f; break;
        case NS_QUALITY_HIGH:   s->particle_density = 0.85f; break;
        default:                s->particle_density = 1.0f;  break;
    }

    s->screen_curvature = 0.16f;
    s->screen_scanlines = 0.35f;
    s->screen_glass     = 0.85f;

    s->exposure_min = 0.55f;
    s->exposure_max = 1.60f;

    /*
     * Ambiance. Une salle d'arcade tire sa lumière de ses machines plutôt que
     * d'un éclairage général, et c'est ce contraste qui la rend crédible — mais
     * une ambiance trop faible écrase tout ce que les sources n'atteignent pas,
     * et la salle devient illisible. Ce niveau garde les recoins lisibles sans
     * effacer le relief que créent les néons.
     */
    /*
     * Réécrite pour une salle SOMBRE. À 0,26 x 0,95, l'ambiance portait à elle
     * seule l'essentiel de l'image : les néons ne se détachaient de rien et la
     * salle avait l'éclairage d'un couloir de bureau. Ici elle ne fait plus que
     * garder les recoins lisibles — le relief vient des sources, et surtout des
     * écrans de bornes, qui sont ce qui éclaire une vraie salle d'arcade.
     *
     * Teintée du bleu froid des tubes : une ambiance neutre grise à ce niveau se
     * lit comme un voile sale.
     */
    /*
     * L'indirect d'une salle éclairée au tungstène est CHAUD, et c'est une
     * conséquence, pas un goût : la lumière rebondit sur des murs et une moquette
     * eux-mêmes éclairés en orangé, donc ce qu'elle rapporte est orangé. La valeur
     * précédente — (0,030 ; 0,036 ; 0,052), bleu nuit — simulait un ciel
     * d'extérieur dans une salle sans la moindre fenêtre. C'est ce qui rendait
     * l'image froide partout où aucun luminaire ne portait, c'est-à-dire dans la
     * plus grande partie de la salle.
     *
     * Le niveau monte aussi, de 0,04 à 0,10 de luminance : c'est le plancher
     * au-dessous duquel on cesse de distinguer un caisson d'un mur. Le tamisé se
     * fabrique avec du contraste de couleur, pas en descendant le plancher.
     */
    s->ambient[0] = 0.112f; s->ambient[1] = 0.086f; s->ambient[2] = 0.062f;
    s->ambient_intensity = 1.0f;

    s->ssao_radius = 0.45f;
    s->ssao_intensity = 0.85f;

    switch (quality) {
    /*
     * `potato` : tout ce dont le coût ne dépend pas de la scène est coupé.
     *
     * Mesuré sur lavapipe en 1280 x 720 depuis le point de vue `allee` : le
     * volumétrique et l'occlusion ambiante sont les deux passes dont le prix se
     * paie par pixel quoi qu'il y ait à l'écran. Les couper, plus 0,60 de
     * résolution de rendu — 36 % des pixels — laisse l'éclairage direct, les
     * écrans, le halo et le tone mapping : la salle est là, elle est juste moins
     * belle.
     */
    case NS_QUALITY_POTATO:
        s->raytracing = NS_RT_OFF;
        s->render_scale = 0.60f;
        s->ssao_samples = 0;
        s->ssao_intensity = 0.0f;
        s->rt_rays_per_pixel = 0;
        s->chromatic_aberration = 0.0f;
        s->volumetric_steps = 0;
        s->exposure_adapt = 0.0f;
        s->bloom_intensity *= 0.6f;
        break;
    case NS_QUALITY_LOW:
        s->raytracing = NS_RT_OFF;
        s->render_scale = 0.75f;
        s->ssao_samples = 6;
        s->rt_rays_per_pixel = 0;
        s->chromatic_aberration = 0.0f;
        /* Pas de volumétrique : c'est la passe la plus chère du moteur, et ce
         * palier existe pour les machines qui n'en veulent pas. */
        s->volumetric_steps = 0;
        s->exposure_adapt = 0.0f;
        break;
    /*
     * MEDIUM est le palier par défaut, et il n'a PAS de lancer de rayons.
     *
     * Une traversée de BVH en compute, par pixel, est une fonction de capture —
     * pas de temps réel sur un GPU intégré. Mesuré : le jeu tournait à une image
     * par seconde sur un MacBook Pro M1 Pro en `high`, et je ne l'avais pas vu
     * parce que mes mesures headless n'attendaient jamais le GPU. L'ambiance de
     * la salle repose sur l'éclairage et le volumétrique, pas sur les ombres
     * lancées : les couper coûte peu à l'image et beaucoup au compteur.
     */
    case NS_QUALITY_MEDIUM:
        s->raytracing = NS_RT_OFF;
        s->ssao_samples = 10;
        s->rt_rays_per_pixel = 0;
        s->volumetric_steps = 14;
        break;
    case NS_QUALITY_HIGH:
        s->raytracing = NS_RT_SHADOWS;
        s->ssao_samples = 14;
        s->rt_rays_per_pixel = 1;
        s->volumetric_steps = 20;
        break;
    case NS_QUALITY_ULTRA:
        s->raytracing = NS_RT_FULL;
        s->ssao_samples = 20;
        s->rt_rays_per_pixel = 2;
        s->volumetric_steps = 28;
        break;
    }

    /*
     * L'ambiante compense ce que les ombres lancées RETIRENT — sinon monter la
     * qualité assombrit la salle.
     *
     * Mesuré au point de vue `allee`, médiane de luminance sur 255 :
     * bas 53, moyen 63, **haut 40**, ultra 73. Le palier du milieu, celui que
     * choisit la moitié des joueurs, était le plus sombre des quatre — et
     * l'ordre n'a rien d'un hasard.
     *
     * `high` allume les ombres lancées : la lumière directe cesse de traverser
     * les caissons et les cloisons, ce qui est juste. Mais il n'allume PAS
     * l'illumination globale, qui est ce qui remplit une ombre dans la
     * réalité — un point à l'ombre d'une borne reçoit encore la lumière
     * rebondie par le mur d'en face. On retire donc le direct sans rendre
     * l'indirect, et le résultat est plus faux que de n'avoir aucune ombre.
     *
     * L'ambiante constante EST ce stand-in. Elle doit donc monter exactement là
     * où les ombres apparaissent, et redescendre à `ultra` où l'indirect tracé
     * la remplace pour de bon (`lighting.frag` : `u_counts.y >= 3`).
     *
     * Le facteur 2,6 est mesuré, pas choisi : il porte `high` de 40 à 55 sans
     * changer d'un dixième de point la proportion de pixels brûlés (7,8 %
     * avant, 8,1 % après). Ce qu'il remplit, ce sont les ombres — c'est-à-dire
     * exactement ce qu'il devait remplir.
     *
     * `high` reste un peu sous `medium` (55 contre 63), et c'est normal plutôt
     * qu'un reliquat : `medium` n'a AUCUNE ombre lancée, donc chaque lumière y
     * traverse les caissons et les cloisons. Il est plus clair parce qu'il est
     * plus faux.
     */
    switch (s->raytracing) {
    case NS_RT_SHADOWS:
    case NS_RT_REFLECTIONS:
        s->ambient_intensity = 2.6f;
        break;
    default:
        /* Sans ombre lancée, rien n'est retiré ; avec l'indirect tracé, la
         * constante n'est même pas lue. Dans les deux cas, 1. */
        s->ambient_intensity = 1.0f;
        break;
    }
}

/* ========================================================================== */
/* Cibles de rendu                                                            */
/* ========================================================================== */

static void destroy_targets(ns_rhi *r, ns_renderer *rd)
{
    ns_texture_destroy(r, &rd->gbuffer_albedo);
    ns_texture_destroy(r, &rd->gbuffer_normal);
    ns_texture_destroy(r, &rd->gbuffer_emissive);
    ns_texture_destroy(r, &rd->depth);
    ns_texture_destroy(r, &rd->hdr);
    ns_texture_destroy(r, &rd->visibility);
    ns_texture_destroy(r, &rd->rt_visibility[0]);
    ns_texture_destroy(r, &rd->rt_visibility[1]);
    ns_texture_destroy(r, &rd->rt_filtered[0]);
    ns_texture_destroy(r, &rd->rt_filtered[1]);
    ns_texture_destroy(r, &rd->reflections);
    ns_texture_destroy(r, &rd->rt_light_shadow);
    ns_texture_destroy(r, &rd->volumetric);
    ns_texture_destroy(r, &rd->hdr_fogged);
    ns_texture_destroy(r, &rd->viewmodel_depth);
    for (int i = 0; i < BLOOM_MIPS; ++i) {
        ns_texture_destroy(r, &rd->bloom[i]);
        ns_texture_destroy(r, &rd->bloom_tmp[i]);
    }
    rd->targets_ready = false;
}

static bool make_target(ns_rhi *r, ns_texture *t, uint32_t w, uint32_t h,
                        SDL_GPUTextureFormat fmt, bool depth, const char *name)
{
    ns_texture_desc d;
    SDL_zero(d);
    d.width = w;
    d.height = h;
    d.format = fmt;
    d.sampled = true;
    d.render_target = !depth;
    d.depth_target = depth;
    d.name = name;
    return ns_texture_create(r, t, &d);
}

bool ns_renderer_resize(ns_rhi *r, ns_renderer *rd, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return false;

    const float scale = ns_clampf(rd->settings.render_scale, 0.4f, 2.0f);
    const uint32_t rw = (uint32_t)ns_maxf(1.0f, (float)width * scale);
    const uint32_t rh = (uint32_t)ns_maxf(1.0f, (float)height * scale);

    if (rd->targets_ready && rw == rd->width && rh == rd->height
        && width == rd->target_width && height == rd->target_height) {
        return true;
    }

    destroy_targets(r, rd);
    rd->width = rw;
    rd->height = rh;
    rd->target_width = width;
    rd->target_height = height;

    bool ok = true;
    ok = ok && make_target(r, &rd->gbuffer_albedo,   rw, rh, FMT_ALBEDO,   false, "gbuffer albédo");
    ok = ok && make_target(r, &rd->gbuffer_normal,   rw, rh, FMT_NORMAL,   false, "gbuffer normale");
    ok = ok && make_target(r, &rd->gbuffer_emissive, rw, rh, FMT_EMISSIVE, false, "gbuffer émissif");
    ok = ok && make_target(r, &rd->depth,            rw, rh, FMT_DEPTH,    true,  "profondeur");
    ok = ok && make_target(r, &rd->hdr,              rw, rh, FMT_HDR,      false, "HDR");
    ok = ok && make_target(r, &rd->visibility,       rw, rh, FMT_VIS,      false, "SSAO");

    /* Les cibles du lancer de rayons sont écrites par un compute shader et lues
     * par la passe d'éclairage : il leur faut les deux usages. Le ping-pong
     * évite d'avoir à lire et écrire la même image dans un seul dispatch, ce que
     * tous les backends ne garantissent pas. */
    /* Le lancer de rayons et tout ce qui en dérive vivent à leur propre
     * résolution, plus basse. */
    rd->rt_width  = (rw / RT_DOWNSCALE) > 0u ? (rw / RT_DOWNSCALE) : 1u;
    rd->rt_height = (rh / RT_DOWNSCALE) > 0u ? (rh / RT_DOWNSCALE) : 1u;

    for (int i = 0; i < 2 && ok; ++i) {
        ns_texture_desc d;
        SDL_zero(d);
        d.width = rd->rt_width; d.height = rd->rt_height;
        d.format = FMT_HDR;
        d.sampled = true;
        d.storage_write = true;
        d.name = "visibilité RT";
        ok = ok && ns_texture_create(r, &rd->rt_visibility[i], &d);
    }
    {
        ns_texture_desc d;
        SDL_zero(d);
        d.width = rd->rt_width; d.height = rd->rt_height;
        d.format = FMT_LIGHT_SHADOW;
        d.sampled = true;
        d.storage_write = true;
        d.name = "ombres par lumière";
        ok = ok && ns_texture_create(r, &rd->rt_light_shadow, &d);
    }
    {
        /* Demi-résolution : le brouillard est un signal très basse fréquence, et
         * le rendre en plein coûterait quatre fois plus pour rien de visible. */
        ns_texture_desc d;
        SDL_zero(d);
        d.width  = (rw / 2u) > 0u ? (rw / 2u) : 1u;
        d.height = (rh / 2u) > 0u ? (rh / 2u) : 1u;
        d.format = FMT_HDR;
        d.sampled = true;
        d.storage_write = true;
        d.name = "brouillard volumétrique";
        ok = ok && ns_texture_create(r, &rd->volumetric, &d);
    }
    ok = ok && make_target(r, &rd->hdr_fogged, rw, rh, FMT_HDR, false, "HDR embrumé");
    ok = ok && make_target(r, &rd->viewmodel_depth, rw, rh, FMT_DEPTH, true, "profondeur viewmodel");
    {
        ns_texture_desc d;
        SDL_zero(d);
        d.width = rd->rt_width; d.height = rd->rt_height;
        d.format = FMT_HDR;
        d.sampled = true;
        d.storage_write = true;
        d.render_target = true;      /* pour l'effacement explicite */
        d.name = "réflexions";
        ok = ok && ns_texture_create(r, &rd->reflections, &d);
    }
    /* Cibles de débruitage : deux passes à-trous en aller-retour. */
    for (int i = 0; i < 2 && ok; ++i) {
        ok = ok && make_target(r, &rd->rt_filtered[i], rd->rt_width, rd->rt_height,
                               FMT_HDR, false, "RT filtré");
    }
    rd->rt_current = 0;
    rd->accum_frames = 0;
    rd->rt_denoised = NULL;
    /*
     * L'adaptation d'exposition repart de zéro elle aussi.
     *
     * `accum_frames` était remis à zéro ici, `exposure_primed` non — donc après
     * une recréation de cibles (redimensionnement de fenêtre, changement de
     * palier ou d'échelle depuis le menu) l'adaptation continuait depuis une
     * luminance moyenne mesurée sur des cibles qui n'existent plus, et pouvait
     * plonger jusqu'à `exposure_min` pendant quelques images. C'est un des deux
     * « flashs noirs ». Remise à faux, la première image se cale d'un coup —
     * exactement ce que fait déjà le démarrage.
     */
    rd->exposure_primed = false;

    /* Chaîne de halo : chaque niveau à la moitié du précédent. Le flou large
     * s'obtient ainsi en quelques passes au lieu d'un noyau énorme. */
    uint32_t bw = rw / 2, bh = rh / 2;
    for (int i = 0; i < BLOOM_MIPS && ok; ++i) {
        if (bw < 1) bw = 1;
        if (bh < 1) bh = 1;
        char name[48];
        SDL_snprintf(name, sizeof name, "halo %d (%ux%u)", i, bw, bh);
        ok = ok && make_target(r, &rd->bloom[i], bw, bh, FMT_HDR, false, "halo");
        ok = ok && make_target(r, &rd->bloom_tmp[i], bw, bh, FMT_HDR, false, "halo tmp");
        bw /= 2; bh /= 2;
    }

    if (!ok) {
        NS_ERROR("cibles de rendu non créées en %ux%u", rw, rh);
        destroy_targets(r, rd);
        return false;
    }

    rd->targets_ready = true;
    rd->reflections_cleared = false;
    NS_INFO("cibles de rendu : %ux%u (fenêtre %ux%u, échelle %.2f)", rw, rh, width, height, (double)scale);
    return true;
}

/* ========================================================================== */
/* Pipelines                                                                  */
/* ========================================================================== */

/* Pipeline plein écran : pas de tampon de sommets, pas de test de profondeur.
 *
 * Les compteurs de ressources ne sont plus passés au point d'appel : ils
 * viennent de `ns_shaders.c`, qui est aussi ce contre quoi la traduction MSL est
 * testée. Deux copies des mêmes chiffres, c'était une de trop. */
static SDL_GPUGraphicsPipeline *make_fullscreen_pipeline(
    ns_rhi *r, const char *frag_name,
    const SDL_GPUTextureFormat *formats, uint32_t format_count)
{
    ns_shader_desc vsd, fsd;
    if (!ns_shader_desc_fill("fullscreen.vert", &vsd)) return NULL;
    if (!ns_shader_desc_fill(frag_name, &fsd)) return NULL;

    SDL_GPUShader *vs = ns_shader_load(r, &vsd, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs = ns_shader_load(r, &fsd, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
        if (fs) SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
        return NULL;
    }

    SDL_GPUColorTargetDescription targets[4];
    SDL_zeroa(targets);
    for (uint32_t i = 0; i < format_count && i < 4; ++i) targets[i].format = formats[i];

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = targets;
    info.target_info.num_color_targets = format_count;

    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(ns_rhi_device(r), &info);
    SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
    SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
    if (!p) NS_ERROR("pipeline « %s » refusé : %s", frag_name, SDL_GetError());
    return p;
}

static SDL_GPUGraphicsPipeline *make_gbuffer_pipeline(ns_rhi *r)
{
    ns_shader_desc vsd, fsd;
    if (!ns_shader_desc_fill("gbuffer.vert", &vsd)) return NULL;
    if (!ns_shader_desc_fill("gbuffer.frag", &fsd)) return NULL;

    SDL_GPUShader *vs = ns_shader_load(r, &vsd, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs = ns_shader_load(r, &fsd, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
        if (fs) SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
        return NULL;
    }

    /* Disposition du sommet : doit correspondre à ns_vertex, octet pour octet. */
    SDL_GPUVertexBufferDescription vb;
    SDL_zero(vb);
    vb.slot = 0;
    vb.pitch = sizeof(ns_vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[4];
    SDL_zeroa(attrs);
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = offsetof(ns_vertex, position);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = offsetof(ns_vertex, normal);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = offsetof(ns_vertex, uv);
    attrs[3].location = 3; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[3].offset = offsetof(ns_vertex, tangent);

    SDL_GPUColorTargetDescription targets[3];
    SDL_zeroa(targets);
    targets[0].format = FMT_ALBEDO;
    targets[1].format = FMT_NORMAL;
    targets[2].format = FMT_EMISSIVE;

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    /* Le modèle d'origine a des faces orientées de façon incohérente (murs
     * modélisés en plans simples) : éliminer les faces arrière ferait
     * disparaître des pans entiers. Le shader retourne la normale à la place. */
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    /* Reverse-Z : on garde le fragment dont la profondeur est la plus GRANDE. */
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;

    info.target_info.color_target_descriptions = targets;
    info.target_info.num_color_targets = 3;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = FMT_DEPTH;

    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(ns_rhi_device(r), &info);
    SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
    SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
    if (!p) NS_ERROR("pipeline G-buffer refusé : %s", SDL_GetError());
    return p;
}

/*
 * Pipeline du viewmodel : même disposition de sommet que la scène, mais une
 * matrice de modèle par tirage, une seule cible couleur (la HDR déjà éclairée) et
 * sa PROPRE profondeur.
 */
static SDL_GPUGraphicsPipeline *make_viewmodel_pipeline(ns_rhi *r, SDL_GPUTextureFormat color)
{
    ns_shader_desc vsd, fsd;
    if (!ns_shader_desc_fill("viewmodel.vert", &vsd)) return NULL;
    if (!ns_shader_desc_fill("viewmodel.frag", &fsd)) return NULL;

    SDL_GPUShader *vs = ns_shader_load(r, &vsd, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs = ns_shader_load(r, &fsd, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
        if (fs) SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
        return NULL;
    }

    SDL_GPUVertexBufferDescription vb;
    SDL_zero(vb);
    vb.slot = 0;
    vb.pitch = sizeof(ns_vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[4];
    SDL_zeroa(attrs);
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = offsetof(ns_vertex, position);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = offsetof(ns_vertex, normal);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = offsetof(ns_vertex, uv);
    attrs[3].location = 3; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[3].offset = offsetof(ns_vertex, tangent);

    SDL_GPUColorTargetDescription target;
    SDL_zero(target);
    target.format = color;

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    /* Même parti que le reste du moteur : le shader retourne la normale sur les
     * faces arrière, et une erreur d'enroulement reste ainsi invisible plutôt que
     * de faire disparaître un segment. */
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;

    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = FMT_DEPTH;

    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(ns_rhi_device(r), &info);
    SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
    SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
    if (!p) NS_ERROR("pipeline viewmodel refusé : %s", SDL_GetError());
    return p;
}

/* Construit la géométrie des bras et la téléverse une fois pour toutes. */
static void build_viewmodel(ns_rhi *r, ns_renderer *rd)
{
    /* Relevé de 2048/4096 : les mains à doigts (six troncs chacune au lieu d'un)
     * portent le maillage de ~350 à ~950 sommets, et la marge d'un facteur deux
     * est ce qui permet d'ajouter un segment sans rouvrir ce fichier. */
    enum { MAX_V = 4096, MAX_I = 8192 };
    static ns_vertex verts[MAX_V];
    static uint32_t  indices[MAX_I];

    uint32_t vcount = 0, icount = 0;
    ns_viewmodel_build(verts, MAX_V, &vcount, indices, MAX_I, &icount,
                       rd->vm_first_index, rd->vm_index_count);
    if (vcount == 0 || icount == 0) return;

    bool ok = ns_buffer_create(r, &rd->vm_vertices, NS_BUFFER_VERTEX,
                               (uint32_t)(sizeof(ns_vertex) * vcount), "sommets viewmodel");
    ok = ok && ns_buffer_create(r, &rd->vm_indices, NS_BUFFER_INDEX,
                                (uint32_t)(sizeof(uint32_t) * icount), "indices viewmodel");
    ok = ok && ns_buffer_upload(r, &rd->vm_vertices, verts,
                                (uint32_t)(sizeof(ns_vertex) * vcount), 0);
    ok = ok && ns_buffer_upload(r, &rd->vm_indices, indices,
                                (uint32_t)(sizeof(uint32_t) * icount), 0);
    rd->vm_ready = ok;
    if (ok) {
        NS_INFO("viewmodel : %u sommets, %u triangles", vcount, icount / 3u);
    } else {
        NS_WARN("viewmodel : géométrie non téléversée — bras absents");
    }
}

ns_renderer *ns_renderer_create(ns_rhi *r, const ns_render_settings *settings)
{
    ns_renderer *rd = (ns_renderer *)ns_calloc(1, sizeof *rd);
    if (!rd) return NULL;

    if (settings) rd->settings = *settings;
    else ns_render_settings_defaults(&rd->settings, NS_QUALITY_HIGH);

    /* Aucun écran vivant tant qu'on n'en déclare pas un. `ns_calloc` mettrait 0,
     * qui est un index de matériau valide — d'où le −1 explicite. */
    rd->screen_count = 0;

    const SDL_GPUTextureFormat hdr_fmt = FMT_HDR;
    const SDL_GPUTextureFormat vis_fmt = FMT_VIS;
    rd->tonemap_format = ns_rhi_swapchain_format(r);

    rd->pipe_gbuffer = make_gbuffer_pipeline(r);
    rd->pipe_ssao    = make_fullscreen_pipeline(r, "ssao.frag", &vis_fmt, 1);
    rd->pipe_lighting = make_fullscreen_pipeline(r, "lighting.frag", &hdr_fmt, 1);
    rd->pipe_bloom_threshold = make_fullscreen_pipeline(r, "bloom_threshold.frag", &hdr_fmt, 1);
    rd->pipe_bloom_blur = make_fullscreen_pipeline(r, "bloom_blur.frag", &hdr_fmt, 1);
    rd->pipe_tonemap = make_fullscreen_pipeline(r, "tonemap.frag", &rd->tonemap_format, 1);
    rd->pipe_debug   = make_fullscreen_pipeline(r, "debug_view.frag", &rd->tonemap_format, 1);

    /* Couche de lancer de rayons. Son absence n'est pas fatale : le rendu
     * retombe sur l'espace écran, ce qui reste jouable. */
    {
        ns_compute_desc cd;
        if (ns_compute_desc_fill("raytrace.comp", &cd)) {
            rd->pipe_raytrace = ns_compute_pipeline_create(r, &cd);
        }
        if (!rd->pipe_raytrace) {
            NS_WARN("pipeline de lancer de rayons indisponible : repli sur l'espace écran");
        }
    }
    rd->pipe_denoise = make_fullscreen_pipeline(r, "rt_denoise.frag", &hdr_fmt, 1);

    /* Brouillard volumétrique. Comme le lancer de rayons, son absence n'est pas
     * fatale : le rendu perd son ambiance, pas son image. */
    {
        ns_compute_desc cd;
        if (ns_compute_desc_fill("volumetric.comp", &cd)) {
            rd->pipe_volumetric = ns_compute_pipeline_create(r, &cd);
        }
        if (!rd->pipe_volumetric) {
            NS_WARN("passe volumétrique indisponible : brouillard de distance seul");
        }
    }
    rd->pipe_vol_composite = make_fullscreen_pipeline(r, "volumetric_composite.frag", &hdr_fmt, 1);

    /* Adaptation d'exposition. Absente, le tone mapping retombe sur la valeur
     * constante des réglages. */
    {
        ns_compute_desc cd;
        if (ns_compute_desc_fill("exposure.comp", &cd)) {
            rd->pipe_exposure = ns_compute_pipeline_create(r, &cd);
        }
        if (!rd->pipe_exposure) {
            NS_WARN("mesure d'exposition indisponible : exposition constante");
        }
    }

    /* Les bras. Leur absence n'est pas fatale non plus : on joue sans mains. */
    rd->pipe_viewmodel = make_viewmodel_pipeline(r, FMT_HDR);

    /* La poussière. Créée seulement si le palier la demande — à `low` elle
     * n'existe pas du tout, plutôt que d'exister et de ne rien dessiner. */
    if (rd->settings.particle_density > 0.0f) {
        rd->particles = ns_particles_create(r, FMT_HDR, FMT_DEPTH, 3000);
        if (!rd->particles) NS_WARN("particules indisponibles — la salle sera sans poussière");
    }

    if (!rd->pipe_gbuffer || !rd->pipe_ssao || !rd->pipe_lighting
        || !rd->pipe_bloom_threshold || !rd->pipe_bloom_blur || !rd->pipe_tonemap
        || !rd->pipe_debug) {
        NS_ERROR("un ou plusieurs pipelines de rendu manquent — abandon");
        ns_renderer_destroy(r, rd);
        return NULL;
    }

    if (!ns_buffer_create(r, &rd->lights, NS_BUFFER_STORAGE,
                          sizeof(ns_light_gpu) * NS_MAX_LIGHTS, "lumières")) {
        ns_renderer_destroy(r, rd);
        return NULL;
    }

    /* Quatre flottants : exposition, luminance lissée, deux de remplissage pour
     * l'alignement std430. Initialisé, jamais laissé indéfini — le tone mapping
     * le lit à la toute première image. */
    {
        const float initial[4] = { rd->settings.exposure, 0.18f, 0.0f, 0.0f };
        if (!ns_buffer_create(r, &rd->exposure, NS_BUFFER_STORAGE_RW,
                              sizeof initial, "exposition")) {
            ns_renderer_destroy(r, rd);
            return NULL;
        }
        ns_buffer_upload(r, &rd->exposure, initial, sizeof initial, 0);
    }

    build_viewmodel(r, rd);

    SDL_memcpy(rd->prev_view_proj, ns_m4_identity().m, sizeof rd->prev_view_proj);
    NS_INFO("rendu prêt (qualité %d, ray tracing %d)", (int)rd->settings.quality,
            (int)rd->settings.raytracing);
    return rd;
}

void ns_renderer_destroy(ns_rhi *r, ns_renderer *rd)
{
    if (rd && rd->particles) { ns_particles_destroy(r, rd->particles); rd->particles = NULL; }
    if (!rd) return;
    SDL_GPUDevice *dev = ns_rhi_device(r);
    if (rd->pipe_gbuffer)          SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_gbuffer);
    if (rd->pipe_ssao)             SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_ssao);
    if (rd->pipe_lighting)         SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_lighting);
    if (rd->pipe_bloom_threshold)  SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_bloom_threshold);
    if (rd->pipe_bloom_blur)       SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_bloom_blur);
    if (rd->pipe_tonemap)          SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_tonemap);
    if (rd->pipe_debug)            SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_debug);
    if (rd->pipe_raytrace)         SDL_ReleaseGPUComputePipeline(dev, rd->pipe_raytrace);
    if (rd->pipe_denoise)          SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_denoise);
    if (rd->pipe_volumetric)       SDL_ReleaseGPUComputePipeline(dev, rd->pipe_volumetric);
    if (rd->pipe_vol_composite)    SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_vol_composite);
    if (rd->pipe_exposure)         SDL_ReleaseGPUComputePipeline(dev, rd->pipe_exposure);
    if (rd->pipe_viewmodel)        SDL_ReleaseGPUGraphicsPipeline(dev, rd->pipe_viewmodel);
    ns_buffer_destroy(r, &rd->lights);
    ns_buffer_destroy(r, &rd->exposure);
    ns_buffer_destroy(r, &rd->vm_vertices);
    ns_buffer_destroy(r, &rd->vm_indices);
    destroy_targets(r, rd);
    ns_free(rd);
}

void ns_renderer_set_particle_zones(ns_renderer *rd, const ns_particle_zone *zones,
                                    uint32_t count, uint64_t seed)
{
    if (!rd->particles) return;
    ns_particles_set_density(rd->particles, rd->settings.particle_density);
    ns_particles_set_zones(rd->particles, zones, count, seed);
}

void ns_renderer_tick_particles(ns_renderer *rd, float dt)
{
    if (rd && rd->particles) ns_particles_tick(rd->particles, dt);
}

void ns_renderer_set_screen(ns_renderer *rd, int32_t material, SDL_GPUTexture *texture)
{
    if (!rd) return;
    /* Sans texture, on efface TOUT : c'est l'appel « plus rien de vivant », et
     * l'appelant qui le fait ne veut pas avoir à énumérer ce qu'il avait posé. */
    if (!texture || material < 0) { rd->screen_count = 0; return; }

    for (uint32_t i = 0; i < rd->screen_count; ++i) {
        if (rd->screens[i].material == material) { rd->screens[i].texture = texture; return; }
    }
    if (rd->screen_count >= NS_MAX_LIVE_SCREENS) {
        NS_WARN("dalles vivantes : %d au maximum, le matériau %d est ignoré",
                NS_MAX_LIVE_SCREENS, material);
        return;
    }
    rd->screens[rd->screen_count].material = material;
    rd->screens[rd->screen_count].texture = texture;
    rd->screen_count++;
}

void ns_renderer_set_settings(ns_rhi *r, ns_renderer *rd, const ns_render_settings *s)
{
    const float old_scale = rd->settings.render_scale;
    rd->settings = *s;
    if (old_scale != s->render_scale && rd->targets_ready) {
        ns_renderer_resize(r, rd, rd->target_width, rd->target_height);
    }
}

const ns_render_settings *ns_renderer_settings(const ns_renderer *rd) { return &rd->settings; }
ns_render_stats ns_renderer_stats(const ns_renderer *rd) { return rd->stats; }

/* ========================================================================== */
/* Élimination par frustum                                                    */
/* ========================================================================== */
/*
 * Six plans extraits de la matrice vue-projection (méthode de Gribb-Hartmann).
 * La salle compte 187 lots ; en éliminer la moitié à chaque image coûte
 * quelques microsecondes de CPU et économise autant de commandes GPU.
 */
typedef struct frustum { float p[6][4]; } frustum;

static void frustum_from_matrix(frustum *f, const ns_m4 *m)
{
    const float *v = &m->m[0][0];
    /* colonne-majeure : v[col*4 + row] */
    for (int i = 0; i < 4; ++i) {
        f->p[0][i] = v[i * 4 + 3] + v[i * 4 + 0];   /* gauche  */
        f->p[1][i] = v[i * 4 + 3] - v[i * 4 + 0];   /* droite  */
        f->p[2][i] = v[i * 4 + 3] + v[i * 4 + 1];   /* bas     */
        f->p[3][i] = v[i * 4 + 3] - v[i * 4 + 1];   /* haut    */
        f->p[4][i] = v[i * 4 + 3] - v[i * 4 + 2];   /* proche (reverse-Z) */
        f->p[5][i] = v[i * 4 + 2];                  /* lointain */
    }
    for (int i = 0; i < 6; ++i) {
        const float len = sqrtf(f->p[i][0] * f->p[i][0] + f->p[i][1] * f->p[i][1] + f->p[i][2] * f->p[i][2]);
        if (len > 1e-6f) {
            for (int k = 0; k < 4; ++k) f->p[i][k] /= len;
        }
    }
}

static bool frustum_test_aabb(const frustum *f, ns_aabb b)
{
    for (int i = 0; i < 6; ++i) {
        /* Sommet le plus favorable au plan : s'il est derrière, la boîte
         * entière l'est. */
        const float x = (f->p[i][0] >= 0.0f) ? b.max.x : b.min.x;
        const float y = (f->p[i][1] >= 0.0f) ? b.max.y : b.min.y;
        const float z = (f->p[i][2] >= 0.0f) ? b.max.z : b.min.z;
        if (f->p[i][0] * x + f->p[i][1] * y + f->p[i][2] * z + f->p[i][3] < 0.0f) return false;
    }
    return true;
}

/* ========================================================================== */
/* Passes                                                                     */
/* ========================================================================== */

static SDL_GPUTexture *texture_or(const ns_scene *s, int32_t index, ns_texture fallback)
{
    if (index >= 0 && (uint32_t)index < s->texture_count && s->textures[index].handle) {
        return s->textures[index].handle;
    }
    return fallback.handle;
}

static void pass_gbuffer(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
                         const ns_m4 *view_proj, const ns_camera *cam, double time)
{
    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);

    SDL_GPUColorTargetInfo colors[3];
    SDL_zeroa(colors);
    colors[0].texture = rd->gbuffer_albedo.handle;
    colors[1].texture = rd->gbuffer_normal.handle;
    colors[2].texture = rd->gbuffer_emissive.handle;
    for (int i = 0; i < 3; ++i) {
        colors[i].load_op = SDL_GPU_LOADOP_CLEAR;
        colors[i].store_op = SDL_GPU_STOREOP_STORE;
        colors[i].clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 0.0f };
    }

    SDL_GPUDepthStencilTargetInfo ds;
    SDL_zero(ds);
    ds.texture = rd->depth.handle;
    ds.load_op = SDL_GPU_LOADOP_CLEAR;
    ds.store_op = SDL_GPU_STOREOP_STORE;
    ds.clear_depth = 0.0f;            /* reverse-Z : le plan lointain vaut 0 */
    ds.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    ds.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, colors, 3, &ds);
    SDL_BindGPUGraphicsPipeline(pass, rd->pipe_gbuffer);

    camera_ubo cam_ubo;
    SDL_memcpy(cam_ubo.view_proj, view_proj->m, sizeof cam_ubo.view_proj);
    SDL_memcpy(cam_ubo.prev_view_proj, rd->prev_view_proj, sizeof cam_ubo.prev_view_proj);
    cam_ubo.camera_pos[0] = cam->position.x;
    cam_ubo.camera_pos[1] = cam->position.y;
    cam_ubo.camera_pos[2] = cam->position.z;
    cam_ubo.camera_pos[3] = (float)time;
    SDL_PushGPUVertexUniformData(cmd, 0, &cam_ubo, sizeof cam_ubo);

    SDL_GPUBufferBinding vb = { scene->vertices.handle, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUBufferBinding ib = { scene->indices.handle, 0 };
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    frustum fr;
    frustum_from_matrix(&fr, view_proj);

    rd->stats.batches_drawn = 0;
    rd->stats.batches_culled = 0;
    rd->stats.triangles = 0;

    int32_t last_material = -2;
    for (uint32_t i = 0; i < scene->batch_count; ++i) {
        const ns_draw_batch *b = &scene->batches[i];

        if (ns_aabb_valid(b->bounds) && !frustum_test_aabb(&fr, b->bounds)) {
            rd->stats.batches_culled++;
            continue;
        }

        /* Les lots arrivent groupés par matériau depuis obj2gltf : ne relier
         * les textures que sur changement évite des centaines de liaisons
         * redondantes par image. */
        if (b->material != last_material) {
            const ns_material_gpu *m = (b->material >= 0 && (uint32_t)b->material < scene->material_count)
                                     ? &scene->material_data[b->material] : NULL;

            SDL_GPUTextureSamplerBinding tex[3];
            SDL_zeroa(tex);
            SDL_GPUSampler *aniso = ns_rhi_sampler(r, NS_SAMPLER_ANISO_REPEAT);
            tex[0].texture = m ? texture_or(scene, m->albedo_texture, scene->fallback_white) : scene->fallback_white.handle;
            /* L'écran vivant : le jeu qui tourne remplace l'image fixe. Le
             * `nearest` est indispensable — un jeu en gros pixels filtré en
             * linéaire devient une bouillie, et c'est justement la netteté qui
             * fait « écran de borne » plutôt que « affiche rétroéclairée ». */
            bool live_screen = false;
            for (uint32_t k = 0; k < rd->screen_count; ++k) {
                if (rd->screens[k].material != b->material) continue;
                tex[0].texture = rd->screens[k].texture;
                live_screen = true;
                break;
            }
            tex[1].texture = m ? texture_or(scene, m->normal_texture, scene->fallback_normal) : scene->fallback_normal.handle;
            tex[2].texture = m ? texture_or(scene, m->orm_texture, scene->fallback_orm) : scene->fallback_orm.handle;
            for (int k = 0; k < 3; ++k) tex[k].sampler = aniso;
            if (live_screen) tex[0].sampler = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
            SDL_BindGPUFragmentSamplers(pass, 0, tex, 3);

            material_ubo mu;
            SDL_zero(mu);
            if (m) {
                SDL_memcpy(mu.base_color, m->base_color, sizeof mu.base_color);
                mu.emissive[0] = m->emissive[0];
                mu.emissive[1] = m->emissive[1];
                mu.emissive[2] = m->emissive[2];
                mu.emissive[3] = m->emissive_strength;
                mu.params[0] = m->metallic;
                mu.params[1] = m->roughness;
                mu.params[2] = (m->normal_texture >= 0) ? 1.0f : 0.0f;
            } else {
                mu.base_color[0] = mu.base_color[1] = mu.base_color[2] = mu.base_color[3] = 1.0f;
                mu.params[1] = 0.8f;
            }
            mu.params[3] = (float)time;

            /*
             * Le traitement de tube n'est appliqué QUE sur l'écran vivant.
             *
             * On pourrait le mettre sur tous les matériaux d'écran, y compris les
             * images fixes des dix-huit autres bornes. On ne le fait pas : la
             * courbure déplace les UV, et une image fixe déjà cadrée pour la
             * dalle se retrouverait rognée. L'écran qui tourne, lui, est rendu
             * pour ça — c'est nous qui produisons son image.
             */
            if (live_screen) {
                mu.screen[0] = rd->settings.screen_curvature;
                mu.screen[1] = rd->settings.screen_scanlines;
                mu.screen[2] = rd->settings.screen_glass;
                mu.screen[3] = 1.0f;
            }
            SDL_PushGPUFragmentUniformData(cmd, 0, &mu, sizeof mu);

            last_material = b->material;
        }

        SDL_DrawGPUIndexedPrimitives(pass, b->index_count, 1, b->first_index, 0, 0);
        rd->stats.batches_drawn++;
        rd->stats.triangles += b->index_count / 3;
    }

    SDL_EndGPURenderPass(pass);
}

/* Passe plein écran générique : lie N textures, pousse un uniforme, dessine. */
static void fullscreen_pass(ns_rhi *r, SDL_GPUGraphicsPipeline *pipe,
                            SDL_GPUTexture *target, SDL_GPULoadOp load,
                            SDL_GPUTexture *const *textures, SDL_GPUSampler *const *samplers,
                            uint32_t texture_count,
                            const void *uniform, uint32_t uniform_size,
                            SDL_GPUBuffer *storage_buffer)
{
    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = target;
    cti.load_op = load;
    cti.store_op = SDL_GPU_STOREOP_STORE;
    cti.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, pipe);

    if (texture_count > 0) {
        SDL_GPUTextureSamplerBinding binds[8];
        SDL_zeroa(binds);
        for (uint32_t i = 0; i < texture_count && i < 8; ++i) {
            binds[i].texture = textures[i];
            binds[i].sampler = samplers[i];
        }
        SDL_BindGPUFragmentSamplers(pass, 0, binds, texture_count);
    }
    if (storage_buffer) {
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &storage_buffer, 1);
    }
    if (uniform && uniform_size) {
        SDL_PushGPUFragmentUniformData(cmd, 0, uniform, uniform_size);
    }

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

/* ========================================================================== */
/* Passe de lancer de rayons                                                  */
/* ========================================================================== */
/*
 * Un dispatch de compute par image. Les rayons partent des points visibles
 * reconstruits depuis le G-buffer, ce qui évite de lancer des rayons primaires :
 * la rasterisation les a déjà résolus, et bien plus vite.
 */
static void pass_raytrace(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
                          const ns_m4 *inv_view_proj, const ns_camera *cam,
                          uint32_t light_count, double time_seconds)
{
    if (!rd->pipe_raytrace || !scene->bvh.loaded) return;
    if (rd->settings.raytracing == NS_RT_OFF) return;

    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);

    const uint32_t write = rd->rt_current ^ 1u;
    const uint32_t read  = rd->rt_current;

    SDL_GPUStorageTextureReadWriteBinding outputs[3];
    SDL_zeroa(outputs);
    outputs[0].texture = rd->rt_visibility[write].handle;
    outputs[1].texture = rd->reflections.handle;
    outputs[2].texture = rd->rt_light_shadow.handle;
    /* `cycle` demande au pilote une ressource fraîche si l'ancienne est encore
     * lue par le GPU : sans cela, on attendrait la fin de l'image précédente. */
    outputs[0].cycle = true;
    outputs[1].cycle = true;
    outputs[2].cycle = true;

    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, outputs, 3, NULL, 0);
    SDL_BindGPUComputePipeline(pass, rd->pipe_raytrace);

    SDL_GPUSampler *nearest = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
    SDL_GPUSampler *linear  = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
    /* Trois textures : l'albédo du G-buffer était lié en quatrième position sans
     * que le shader ne l'échantillonne jamais. */
    SDL_GPUTextureSamplerBinding tex[3];
    SDL_zeroa(tex);
    tex[0].texture = rd->depth.handle;                    tex[0].sampler = nearest;
    tex[1].texture = rd->gbuffer_normal.handle;           tex[1].sampler = nearest;
    tex[2].texture = rd->rt_visibility[read].handle;      tex[2].sampler = linear;
    SDL_BindGPUComputeSamplers(pass, 0, tex, 3);

    SDL_GPUBuffer *buffers[4] = {
        scene->bvh.gpu_nodes.handle,
        scene->bvh.gpu_tris.handle,
        scene->bvh.gpu_materials.handle,
        rd->lights.handle,
    };
    SDL_BindGPUComputeStorageBuffers(pass, 0, buffers, 4);

    raytrace_ubo u;
    SDL_zero(u);
    SDL_memcpy(u.inv_view_proj, inv_view_proj->m, sizeof u.inv_view_proj);
    u.camera_pos[0] = cam->position.x;
    u.camera_pos[1] = cam->position.y;
    u.camera_pos[2] = cam->position.z;
    u.camera_pos[3] = (float)time_seconds;
    SDL_memcpy(u.ambient, rd->settings.ambient, sizeof(float) * 3);
    u.ambient[3] = rd->settings.ambient_intensity;
    u.config[0] = (int32_t)light_count;
    u.config[1] = (int32_t)rd->settings.raytracing;
    u.config[2] = rd->settings.rt_rays_per_pixel;
    u.config[3] = (int32_t)(ns_rhi_frame_index(r) & 0xFFFFu);

    /* Poids de l'historique : croît avec le nombre d'images accumulées, plafonné
     * pour que l'image reste réactive. Remis à zéro dès que la caméra bouge —
     * un historique conservé à tort produit des traînées bien plus visibles que
     * le bruit qu'il supprime. */
    const float weight = (rd->accum_frames == 0)
                       ? 0.0f
                       : ns_minf(0.95f, 1.0f - 1.0f / (float)(rd->accum_frames + 1));
    u.accum[0] = weight;
    u.accum[1] = (float)rd->rt_width;
    u.accum[2] = (float)rd->rt_height;
    /* Le shader bornait son index de matériau avec `materials.length()`. Cette
     * fonction n'existe pas en MSL : le traducteur la remplace par un tampon de
     * tailles que SDL ne lie jamais, donc le shader lirait dans le vide sur
     * Metal sans que rien ne le signale. Le compte vient d'ici. */
    u.accum[3] = (float)scene->bvh.material_count;

    SDL_PushGPUComputeUniformData(cmd, 0, &u, sizeof u);

    /* Groupes de 8x8, arrondis au supérieur ; le shader borne lui-même. */
    SDL_DispatchGPUCompute(pass, (rd->rt_width + 7) / 8, (rd->rt_height + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);

    rd->rt_current = write;
    rd->accum_frames++;

    /*
     * Débruitage : deux passes à-trous, espacement doublé à la seconde. Deux
     * suffisent ici parce que l'accumulation temporelle fait le gros du travail
     * dès que la caméra ralentit ; une troisième passe commencerait à effacer
     * les petites ombres de contact sous les bornes.
     *
     * L'agressivité est modulée par le nombre d'images accumulées : une image
     * déjà convergée n'a pas besoin d'être lissée, et le filtre lui ferait
     * perdre du détail.
     */
    if (rd->pipe_denoise) {
        const float converged = ns_minf(1.0f, (float)rd->accum_frames / 24.0f);
        SDL_GPUSampler *nearest_s = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);

        for (int pass_index = 0; pass_index < 2; ++pass_index) {
            const float spacing = (pass_index == 0) ? 1.0f : 2.0f;
            denoise_ubo du;
            SDL_zero(du);
            du.step[0] = spacing / (float)rd->width;
            du.step[1] = spacing / (float)rd->height;
            du.step[2] = 220.0f;                       /* sensibilité à la profondeur */
            du.step[3] = 24.0f;                        /* sensibilité à la normale */
            /* Filtre désactivé en douceur quand l'image a convergé. */
            if (converged >= 1.0f && pass_index == 1) break;

            SDL_GPUTexture *src = (pass_index == 0)
                                ? rd->rt_visibility[rd->rt_current].handle
                                : rd->rt_filtered[0].handle;
            SDL_GPUTexture *tex_in[3] = { src, rd->depth.handle, rd->gbuffer_normal.handle };
            SDL_GPUSampler *smp_in[3] = { nearest_s, nearest_s, nearest_s };

            fullscreen_pass(r, rd->pipe_denoise, rd->rt_filtered[pass_index].handle,
                            SDL_GPU_LOADOP_CLEAR, tex_in, smp_in, 3, &du, sizeof du, NULL);
            rd->rt_denoised = &rd->rt_filtered[pass_index];
        }
    }
}

/* ========================================================================== */
/* Dessin complet                                                             */
/* ========================================================================== */

/*
 * Brouillard volumétrique, à demi-résolution.
 *
 * Renvoie true si le brouillard a été produit — auquel cas l'appelant doit
 * composer, et `lighting.frag` doit avoir sauté son brouillard de distance.
 */
static bool pass_volumetric(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
                            const ns_m4 *inv_view_proj, const ns_camera *cam,
                            uint32_t light_count, double time_seconds)
{
    if (!rd->pipe_volumetric || !scene->bvh.loaded) return false;
    if (rd->settings.volumetric_steps <= 0) return false;

    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);

    SDL_GPUStorageTextureReadWriteBinding out;
    SDL_zero(out);
    out.texture = rd->volumetric.handle;
    out.cycle = true;

    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &out, 1, NULL, 0);
    SDL_BindGPUComputePipeline(pass, rd->pipe_volumetric);

    SDL_GPUTextureSamplerBinding tex;
    SDL_zero(tex);
    tex.texture = rd->depth.handle;
    tex.sampler = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
    SDL_BindGPUComputeSamplers(pass, 0, &tex, 1);

    /* Les matériaux ne sont pas liés : le brouillard ne colore pas ce qu'il
     * occulte, il a seulement besoin de savoir si quelque chose bloque. */
    SDL_GPUBuffer *buffers[3] = {
        scene->bvh.gpu_nodes.handle,
        scene->bvh.gpu_tris.handle,
        rd->lights.handle,
    };
    SDL_BindGPUComputeStorageBuffers(pass, 0, buffers, 3);

    volumetric_ubo u;
    SDL_zero(u);
    SDL_memcpy(u.inv_view_proj, inv_view_proj->m, sizeof u.inv_view_proj);
    u.camera_pos[0] = cam->position.x;
    u.camera_pos[1] = cam->position.y;
    u.camera_pos[2] = cam->position.z;
    u.camera_pos[3] = (float)time_seconds;
    SDL_memcpy(u.fog, rd->settings.fog_color, sizeof(float) * 3);
    u.fog[3] = rd->settings.fog_density;
    u.config[0] = (int32_t)light_count;
    u.config[1] = rd->settings.volumetric_steps;
    u.config[2] = (int32_t)rd->volumetric.width;
    u.config[3] = (int32_t)rd->volumetric.height;
    u.params[0] = rd->settings.fog_anisotropy;
    u.params[1] = rd->settings.fog_max_distance;
    u.params[2] = rd->settings.fog_ambient;
    SDL_PushGPUComputeUniformData(cmd, 0, &u, sizeof u);

    SDL_DispatchGPUCompute(pass, (rd->volumetric.width + 7) / 8,
                                 (rd->volumetric.height + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);
    return true;
}

/* Mesure de la luminance de l'image et adaptation. Sampler la cible HDR plutôt
 * que la relire côté CPU : une relecture imposerait une clôture par image. */
static bool pass_exposure(ns_rhi *r, ns_renderer *rd, SDL_GPUTexture *lit, double time_seconds)
{
    if (!rd->pipe_exposure) return false;
    if (rd->settings.exposure_adapt <= 0.0f) return false;

    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);

    SDL_GPUStorageBufferReadWriteBinding rw;
    SDL_zero(rw);
    rw.buffer = rd->exposure.handle;
    /* Surtout PAS `cycle` : la valeur doit survivre d'une image à l'autre, c'est
     * elle qui porte l'adaptation. Recycler le tampon repartirait de zéro à
     * chaque image, et l'exposition ne s'adapterait jamais. */
    rw.cycle = false;

    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &rw, 1);
    SDL_BindGPUComputePipeline(pass, rd->pipe_exposure);

    SDL_GPUTextureSamplerBinding tex;
    SDL_zero(tex);
    tex.texture = lit;
    tex.sampler = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
    SDL_BindGPUComputeSamplers(pass, 0, &tex, 1);

    /* dt borné : une image longue (chargement, fenêtre déplacée) ferait sinon
     * sauter l'adaptation d'un coup, ce qui se voit comme un flash. */
    double dt = rd->exposure_primed ? (time_seconds - rd->last_time) : 0.0;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.1) dt = 0.1;

    exposure_ubo u;
    SDL_zero(u);
    u.settings[0] = rd->settings.exposure;
    u.settings[1] = rd->settings.exposure_adapt;
    u.settings[2] = rd->settings.exposure_min;
    u.settings[3] = rd->settings.exposure_max;
    u.frame[0] = (float)dt;
    u.frame[1] = rd->exposure_primed ? 0.0f : 1.0f;
    SDL_PushGPUComputeUniformData(cmd, 0, &u, sizeof u);

    SDL_DispatchGPUCompute(pass, 1, 1, 1);
    SDL_EndGPUComputePass(pass);

    rd->exposure_primed = true;
    rd->last_time = time_seconds;
    return true;
}

/*
 * Les bras, en forward, dans la cible HDR déjà éclairée et embrumée.
 *
 * Deux choses distinguent cette passe de toutes les autres :
 *   - elle CHARGE la couleur au lieu de l'effacer — elle se pose sur l'image ;
 *   - elle EFFACE sa propre profondeur, qui n'est partagée avec personne.
 */
static void pass_viewmodel(ns_rhi *r, ns_renderer *rd, const ns_camera *cam,
                           const ns_m4 *view, SDL_GPUTexture *target,
                           const ns_viewmodel_pose *pose, uint32_t light_count)
{
    if (!pose || !rd->pipe_viewmodel || !rd->vm_ready) return;

    bool any = false;
    for (int i = 0; i < NS_VM_SEGMENT_COUNT; ++i) any = any || pose->draw[i];
    if (!any) return;

    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);

    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.texture = target;
    cti.load_op = SDL_GPU_LOADOP_LOAD;
    cti.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo ds;
    SDL_zero(ds);
    ds.texture = rd->viewmodel_depth.handle;
    ds.load_op = SDL_GPU_LOADOP_CLEAR;
    ds.store_op = SDL_GPU_STOREOP_DONT_CARE;
    ds.clear_depth = 0.0f;              /* reverse-Z : le plan lointain vaut 0 */
    ds.cycle = true;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, &ds);
    SDL_BindGPUGraphicsPipeline(pass, rd->pipe_viewmodel);

    SDL_GPUBufferBinding vb = { rd->vm_vertices.handle, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUBufferBinding ib = { rd->vm_indices.handle, 0 };
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_GPUBuffer *lights = rd->lights.handle;
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &lights, 1);

    /* Projection propre : plus étroite que celle de la scène, et un plan proche
     * très court parce que les mains passent à vingt centimètres de l'œil. */
    const float aspect = (float)rd->width / (float)ns_maxf(1.0f, (float)rd->height);
    const float fov = (pose->fov_y_degrees > 1.0f) ? pose->fov_y_degrees : 45.0f;
    const ns_m4 proj = ns_m4_perspective(fov * NS_DEG2RAD, aspect, 0.02f, 6.0f, true);
    const ns_m4 view_proj = ns_m4_mul(proj, *view);

    /*
     * Une matière par segment, et non une seule pour les sept.
     *
     * Ce n'est pas de la décoration : avec une couleur unique, la manche,
     * l'avant-bras, la main et le jeton forment un seul tube indistinct — c'est
     * ce que montrait la première capture, où l'on ne pouvait pas dire où
     * finissait le bras et où commençait la main. Trois valeurs qui se
     * détachent l'une de l'autre suffisent à donner une silhouette, et une
     * silhouette est ce qui fait lire un bras.
     *
     * L'ordre suit `ns_viewmodel_segment` : manche, avant-bras, main, à gauche
     * puis à droite, puis le jeton.
     *
     * LA PEAU EST PLUS SATURÉE QU'ELLE N'EN A L'AIR, et il faut le dire parce
     * que la valeur surprend hors contexte. Mesuré sur une capture au ras d'une
     * borne, l'ancienne peau (0,315 0,215 0,170 — un rapport 1 : 0,68 : 0,54)
     * ressortait à (178 162 150), soit 1 : 0,91 : 0,84 : presque grise. Les
     * mains n'étaient pas surexposées — 178 n'est pas 255 — elles étaient
     * DÉLAVÉES, parce qu'ACES désature ce qui est clair et que le spéculaire
     * blanc s'ajoute par-dessus. Une peau qui doit se lire comme de la peau à
     * cette luminance doit donc partir plus saturée que la mesure d'un
     * nuancier : 1 : 0,51 : 0,36.
     */
    static const struct { float rgb[3], roughness, metallic; } vm_material[NS_VM_SEGMENT_COUNT] = {
        { { 0.085f, 0.095f, 0.125f }, 0.88f, 0.0f },   /* manche : toile sombre */
        { { 0.085f, 0.095f, 0.125f }, 0.88f, 0.0f },
        { { 0.360f, 0.185f, 0.130f }, 0.55f, 0.0f },   /* main : peau — voir plus bas */
        { { 0.085f, 0.095f, 0.125f }, 0.88f, 0.0f },
        { { 0.085f, 0.095f, 0.125f }, 0.88f, 0.0f },
        { { 0.360f, 0.185f, 0.130f }, 0.55f, 0.0f },
        { { 0.72f,  0.56f,  0.24f  }, 0.28f, 0.9f },   /* jeton : laiton */
    };

    for (int i = 0; i < NS_VM_SEGMENT_COUNT; ++i) {
        if (!pose->draw[i] || rd->vm_index_count[i] == 0) continue;

        viewmodel_fs_ubo fu;
        SDL_zero(fu);
        fu.base_color[0] = vm_material[i].rgb[0];
        fu.base_color[1] = vm_material[i].rgb[1];
        fu.base_color[2] = vm_material[i].rgb[2];
        fu.base_color[3] = vm_material[i].roughness;
        fu.camera[0] = cam->position.x;
        fu.camera[1] = cam->position.y;
        fu.camera[2] = cam->position.z;
        fu.camera[3] = vm_material[i].metallic;
        SDL_memcpy(fu.ambient, rd->settings.ambient, sizeof(float) * 3);
        fu.ambient[3] = rd->settings.ambient_intensity;
        fu.counts[0] = (int32_t)light_count;
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);

        viewmodel_vs_ubo vu;
        SDL_zero(vu);
        SDL_memcpy(vu.view_proj, view_proj.m, sizeof vu.view_proj);
        SDL_memcpy(vu.model, pose->segment[i].m, sizeof vu.model);
        vu.params[0] = pose->length[i];
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);

        SDL_DrawGPUIndexedPrimitives(pass, rd->vm_index_count[i], 1,
                                     rd->vm_first_index[i], 0, 0);
    }

    SDL_EndGPURenderPass(pass);
}

bool ns_renderer_draw(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
                      const ns_camera *camera, const ns_viewmodel_pose *viewmodel,
                      SDL_GPUTexture *target,
                      uint32_t target_width, uint32_t target_height,
                      double time_seconds)
{
    NS_ASSERT(rd && scene && camera && target);

    /*
     * Rien n'a pu être dessiné : on le DIT, au lieu de sortir en silence.
     *
     * L'appelant enchaînait sur `ns_rhi_end_frame`, qui présente une swapchain
     * jamais remplie — c'est-à-dire une image noire. C'est l'autre « flash
     * noir », et il se déclenche pendant un redimensionnement de fenêtre ou à
     * un changement de qualité. Une image sautée ne se voit pas ; une image
     * noire, si.
     */
    if (!ns_renderer_resize(r, rd, target_width, target_height)) return false;

    /* --- matrices --- */
    const float aspect = (float)rd->width / (float)ns_maxf(1.0f, (float)rd->height);
    const ns_m4 view = ns_m4_look_at(camera->position,
                                     ns_v3_add(camera->position, camera->forward),
                                     camera->up);
    const ns_m4 proj = ns_m4_perspective(camera->fov_y_degrees * NS_DEG2RAD, aspect,
                                         camera->znear, camera->zfar, true);
    const ns_m4 view_proj = ns_m4_mul(proj, view);
    const ns_m4 inv_view_proj = ns_m4_inverse(view_proj);

    /* --- lumières : réécrites chaque image, elles scintillent --- */
    uint32_t light_count = scene->light_count;
    if (light_count > NS_MAX_LIGHTS) light_count = NS_MAX_LIGHTS;
    if (light_count > 0) {
        ns_rhi_stage_buffer(r, &rd->lights, scene->lights,
                            (uint32_t)(sizeof(ns_light_gpu) * light_count), 0);
    }
    rd->stats.lights_active = light_count;

    /* Les copies en attente doivent être exécutées avant la première passe de
     * rendu qui lit le tampon. */
    ns_rhi_flush_staging(r);

    /* --- 0. Mise à zéro des cibles que la couche RT alimentera ---
     * Tant que le ray tracing n'écrit pas dans `reflections`, cette cible doit
     * contenir du noir défini et non de la mémoire GPU réutilisée : une seule
     * valeur non finie suffit à noircir toute l'image après le tone mapping. */
    if (!rd->reflections_cleared || rd->settings.raytracing == NS_RT_OFF) {
        SDL_GPUColorTargetInfo clear;
        SDL_zero(clear);
        clear.texture = rd->reflections.handle;
        clear.load_op = SDL_GPU_LOADOP_CLEAR;
        clear.store_op = SDL_GPU_STOREOP_STORE;
        clear.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };
        SDL_GPURenderPass *cp = SDL_BeginGPURenderPass(ns_rhi_cmd(r), &clear, 1, NULL);
        SDL_EndGPURenderPass(cp);
        rd->reflections_cleared = true;
    }

    /* --- 1. G-buffer --- */
    pass_gbuffer(r, rd, scene, &view_proj, camera, time_seconds);

    /* --- 2. Visibilité (SSAO ; la couche RT écrasera r et b en M4) --- */
    {
        ssao_ubo u;
        SDL_zero(u);
        SDL_memcpy(u.inv_view_proj, inv_view_proj.m, sizeof u.inv_view_proj);
        SDL_memcpy(u.view_proj, view_proj.m, sizeof u.view_proj);
        u.camera_pos[0] = camera->position.x;
        u.camera_pos[1] = camera->position.y;
        u.camera_pos[2] = camera->position.z;
        u.settings[0] = rd->settings.ssao_radius;
        u.settings[1] = rd->settings.ssao_intensity;
        u.settings[2] = 0.0008f;                 /* biais anti auto-occlusion */
        u.settings[3] = (float)time_seconds;
        u.counts[0] = rd->settings.ssao_samples;

        SDL_GPUTexture *tex[2] = { rd->depth.handle, rd->gbuffer_normal.handle };
        SDL_GPUSampler *smp[2] = { ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP),
                                   ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP) };
        fullscreen_pass(r, rd->pipe_ssao, rd->visibility.handle, SDL_GPU_LOADOP_CLEAR,
                        tex, smp, 2, &u, sizeof u, NULL);
    }

    /* --- 2b. Lancer de rayons ---
     * L'accumulation temporelle n'est valable que si la caméra n'a pas bougé :
     * on compare la matrice de cette image à celle de la précédente. */
    {
        float diff = 0.0f;
        const float *a = &view_proj.m[0][0];
        for (int i = 0; i < 16; ++i) diff += fabsf(a[i] - rd->prev_view_proj[i]);
        if (diff > 1e-4f) rd->accum_frames = 0;
    }
    pass_raytrace(r, rd, scene, &inv_view_proj, camera, light_count, time_seconds);

    /* --- 2c. Brouillard volumétrique ---
     * Marché ici, composé après l'éclairage : il lui faut la profondeur (déjà
     * écrite) mais pas la couleur. Le calculer maintenant permet à l'éclairage
     * de savoir qu'il doit sauter son brouillard de distance. */
    const bool volumetric_on =
        pass_volumetric(r, rd, scene, &inv_view_proj, camera, light_count, time_seconds);

    /* --- 3. Éclairage --- */
    {
        frame_ubo u;
        SDL_zero(u);
        SDL_memcpy(u.inv_view_proj, inv_view_proj.m, sizeof u.inv_view_proj);
        u.camera_pos[0] = camera->position.x;
        u.camera_pos[1] = camera->position.y;
        u.camera_pos[2] = camera->position.z;
        u.camera_pos[3] = (float)time_seconds;
        SDL_memcpy(u.ambient, rd->settings.ambient, sizeof(float) * 3);
        u.ambient[3] = rd->settings.ambient_intensity;
        SDL_memcpy(u.fog, rd->settings.fog_color, sizeof(float) * 3);
        u.fog[3] = rd->settings.fog_density;
        u.counts[0] = (int32_t)light_count;
        /* 0 : aucun lancer de rayons ; 1 : ombres ; 2+ : réflexions disponibles. */
        u.counts[1] = (int32_t)rd->settings.raytracing;
        /* Le brouillard de distance de `lighting.frag` est un mélange vers une
         * couleur constante. Quand le volumétrique tourne, il faut le couper :
         * deux brouillards superposés donnent une salle laiteuse. */
        u.counts[2] = volumetric_on ? 1 : 0;
        /* Diviseur de résolution du lancer de rayons : `lighting.frag` lit la
         * cible d'ombres par lumière au texel près, et doit donc savoir de
         * combien elle est réduite. */
        u.counts[3] = (int32_t)RT_DOWNSCALE;

        SDL_GPUSampler *clamp = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
        SDL_GPUSampler *nearest = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
        SDL_GPUTexture *tex[8] = {
            rd->gbuffer_albedo.handle, rd->gbuffer_normal.handle, rd->gbuffer_emissive.handle,
            rd->depth.handle, rd->visibility.handle,
            (rd->rt_denoised ? rd->rt_denoised->handle : rd->rt_visibility[rd->rt_current].handle),
            rd->reflections.handle,
            rd->rt_light_shadow.handle
        };
        /* La dernière est une texture entière : le filtrage linéaire mélangerait
         * des indices de lumière, ce qui n'a aucun sens — et Vulkan l'interdit. */
        SDL_GPUSampler *smp[8] = { nearest, nearest, nearest, nearest, clamp, clamp, clamp, nearest };

        fullscreen_pass(r, rd->pipe_lighting, rd->hdr.handle, SDL_GPU_LOADOP_CLEAR,
                        tex, smp, 8, &u, sizeof u, rd->lights.handle);
    }

    /* --- 3b. Composition du brouillard ---
     * Entre l'éclairage et le halo, et pas ailleurs : après le halo les rais ne
     * fleuriraient pas, or c'est leur débordement qui les rend crédibles. */
    SDL_GPUTexture *lit = rd->hdr.handle;
    if (volumetric_on && rd->pipe_vol_composite) {
        vol_composite_ubo cu;
        SDL_zero(cu);
        cu.size[0] = (float)rd->volumetric.width;
        cu.size[1] = (float)rd->volumetric.height;
        cu.size[2] = (float)rd->width;
        cu.size[3] = (float)rd->height;
        cu.settings[0] = rd->settings.fog_intensity;
        /* Sensibilité de la remontée guidée par la profondeur. En reverse-Z les
         * écarts utiles sont minuscules : une valeur trop faible rendrait le
         * filtre bilinéaire, une valeur trop forte ne garderait qu'un voisin. */
        cu.settings[1] = 900.0f;

        SDL_GPUSampler *clampS = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
        SDL_GPUSampler *nearestS = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
        SDL_GPUTexture *tex[3] = { rd->hdr.handle, rd->volumetric.handle, rd->depth.handle };
        SDL_GPUSampler *smp[3] = { clampS, clampS, nearestS };
        fullscreen_pass(r, rd->pipe_vol_composite, rd->hdr_fogged.handle, SDL_GPU_LOADOP_CLEAR,
                        tex, smp, 3, &cu, sizeof cu, NULL);
        lit = rd->hdr_fogged.handle;
    }

    /* --- 3b bis. Les bras ---
     * Après le brouillard (ils le prennent), avant le halo (ils fleurissent), et
     * avant la mesure d'exposition : une main qui passe devant un néon doit peser
     * dans la luminance moyenne, sinon l'image pompe quand on lève le bras. */
    /* --- 3b bis. La poussière ---
     *
     * Après le brouillard, avant les bras et le halo. L'ordre n'est pas neutre :
     * après le brouillard, les grains flottent DANS les cônes de lumière plutôt
     * que devant ; avant le halo, ils fleurissent, ce qui est exactement ce qui
     * les fait exister. Avant les bras, parce qu'une poussière passe derrière une
     * main, pas devant.
     */
    if (rd->particles && rd->settings.particle_density > 0.0f) {
        ns_particles_set_lights(rd->particles, scene->lights, light_count);
        const ns_v3 fwd = ns_v3_norm(camera->forward);
        const ns_v3 right = ns_v3_norm(ns_v3_cross(fwd, camera->up));
        const ns_v3 up = ns_v3_cross(right, fwd);
        ns_particles_draw(r, rd->particles, &view_proj, camera->position, right, up,
                          lit, rd->depth.handle, rd->width, rd->height);
    }

    pass_viewmodel(r, rd, camera, &view, lit, viewmodel, light_count);

    /* --- 3c. Mesure de l'exposition ---
     * Après la composition du brouillard : c'est bien l'image finale avant halo
     * dont on veut la luminance, brume comprise. */
    const bool exposure_measured = pass_exposure(r, rd, lit, time_seconds);

    /* --- 4. Halo --- */
    {
        SDL_GPUSampler *clamp = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);

        bloom_threshold_ubo tu;
        SDL_zero(tu);
        tu.settings[0] = rd->settings.bloom_threshold;
        tu.settings[1] = 0.55f;                   /* douceur du coude */
        tu.settings[2] = 1.0f;
        SDL_GPUTexture *src = lit;
        fullscreen_pass(r, rd->pipe_bloom_threshold, rd->bloom[0].handle, SDL_GPU_LOADOP_CLEAR,
                        &src, &clamp, 1, &tu, sizeof tu, NULL);

        /* Descente : chaque niveau est le flou du précédent, réduit de moitié.
         * Le flou horizontal puis vertical donne un noyau séparable. */
        for (int i = 0; i < BLOOM_MIPS; ++i) {
            if (i > 0) {
                SDL_GPUTexture *prev = rd->bloom[i - 1].handle;
                bloom_blur_ubo bu;
                SDL_zero(bu);
                fullscreen_pass(r, rd->pipe_bloom_blur, rd->bloom[i].handle, SDL_GPU_LOADOP_CLEAR,
                                &prev, &clamp, 1, &bu, sizeof bu, NULL);
            }

            bloom_blur_ubo bh;
            SDL_zero(bh);
            bh.direction[0] = 1.0f / (float)rd->bloom[i].width;
            SDL_GPUTexture *a = rd->bloom[i].handle;
            fullscreen_pass(r, rd->pipe_bloom_blur, rd->bloom_tmp[i].handle, SDL_GPU_LOADOP_CLEAR,
                            &a, &clamp, 1, &bh, sizeof bh, NULL);

            bloom_blur_ubo bv;
            SDL_zero(bv);
            bv.direction[1] = 1.0f / (float)rd->bloom[i].height;
            SDL_GPUTexture *b = rd->bloom_tmp[i].handle;
            fullscreen_pass(r, rd->pipe_bloom_blur, rd->bloom[i].handle, SDL_GPU_LOADOP_CLEAR,
                            &b, &clamp, 1, &bv, sizeof bv, NULL);
        }
    }

    /* --- 5a. Vue de débogage, si demandée --- */
    if (rd->settings.debug_view != NS_DEBUG_NONE) {
        SDL_GPUTexture *src = NULL;
        switch (rd->settings.debug_view) {
        case NS_DEBUG_ALBEDO:     src = rd->gbuffer_albedo.handle;   break;
        case NS_DEBUG_NORMAL:     src = rd->gbuffer_normal.handle;   break;
        case NS_DEBUG_EMISSIVE:   src = rd->gbuffer_emissive.handle; break;
        case NS_DEBUG_DEPTH:      src = rd->depth.handle;            break;
        case NS_DEBUG_VISIBILITY: src = (rd->settings.raytracing != NS_RT_OFF && rd->rt_denoised)
                                      ? rd->rt_denoised->handle
                                      : rd->visibility.handle; break;
        case NS_DEBUG_HDR:        src = lit;                         break;
        case NS_DEBUG_VOLUMETRIC: src = rd->volumetric.handle;       break;
        case NS_DEBUG_BLOOM:      src = rd->bloom[BLOOM_MIPS - 1].handle; break;
        default: break;
        }
        if (src) {
            debug_ubo du;
            SDL_zero(du);
            du.mode[0] = (int32_t)rd->settings.debug_view;
            du.scale[0] = 1.0f;
            SDL_GPUSampler *nearest = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
            fullscreen_pass(r, rd->pipe_debug, target, SDL_GPU_LOADOP_CLEAR,
                            &src, &nearest, 1, &du, sizeof du, NULL);
            SDL_memcpy(rd->prev_view_proj, view_proj.m, sizeof rd->prev_view_proj);
            return true;   /* la vue de débogage EST l'image : elle compte */
        }
    }

    /* --- 5b. Tone mapping vers la cible finale --- */
    {
        tonemap_ubo u;
        SDL_zero(u);
        u.settings[0] = rd->settings.exposure;
        u.settings[1] = rd->settings.bloom_intensity;
        u.settings[2] = rd->settings.vignette;
        u.settings[3] = rd->settings.grain;
        u.extra[0] = (float)time_seconds;
        u.extra[1] = rd->settings.saturation;
        u.extra[2] = rd->settings.chromatic_aberration;
        u.extra[3] = exposure_measured ? 1.0f : 0.0f;

        SDL_GPUSampler *clamp = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
        /* Le niveau de halo le plus flou porte l'essentiel du rayonnement ; les
         * niveaux intermédiaires y ont déjà été fondus par la descente. */
        SDL_GPUTexture *tex[2] = { lit, rd->bloom[BLOOM_MIPS - 1].handle };
        SDL_GPUSampler *smp[2] = { clamp, clamp };
        fullscreen_pass(r, rd->pipe_tonemap, target, SDL_GPU_LOADOP_CLEAR,
                        tex, smp, 2, &u, sizeof u, rd->exposure.handle);
    }

    SDL_memcpy(rd->prev_view_proj, view_proj.m, sizeof rd->prev_view_proj);
    return true;
}
