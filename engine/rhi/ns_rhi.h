/*
 * ns_rhi.h — interface de rendu matériel (Render Hardware Interface).
 *
 * Fine couche au-dessus de l'API GPU de SDL3, qui expose elle-même Vulkan,
 * Metal et Direct3D 12 derrière une seule API C. Le reste du moteur ne parle
 * qu'à ce fichier : c'est ce qui permet d'avoir le même rendu sur les trois
 * plateformes sans #ifdef dans le code de jeu.
 *
 * Ce que la couche apporte au-dessus de SDL_gpu.h :
 *   - un cycle de frame explicite (acquisition, passes, soumission) ;
 *   - un anneau de tampons de transfert pour les téléversements, au lieu d'en
 *     créer un par appel ;
 *   - la création de shaders depuis les blobs SPIR-V embarqués, par nom ;
 *   - des descripteurs de pipeline avec des valeurs par défaut raisonnables,
 *     pour éviter les structures de 40 champs à remplir à chaque fois ;
 *   - la capture d'écran vers PNG, qui sert autant à l'utilisateur qu'aux
 *     tests de rendu automatisés en headless.
 *
 * Rappel de la V1 : le rendu passait par glBegin/glEnd en mode immédiat, avec
 * un appel de dessin par quad. Ici tout passe par des tampons persistants et
 * des pipelines compilés une fois.
 */
#ifndef NS_RHI_H
#define NS_RHI_H

#include "ns_core.h"
#include "ns_math.h"

#include <SDL3/SDL.h>

typedef struct ns_rhi ns_rhi;

/* ========================================================================== */
/* Initialisation                                                             */
/* ========================================================================== */

typedef struct ns_rhi_desc {
    const char *window_title;
    int         width;
    int         height;
    bool        fullscreen;
    bool        vsync;
    bool        debug;          /* couches de validation du pilote */
    bool        headless;       /* fenêtre invisible : tests et capture hors écran */
    uint32_t    frames_in_flight;
} ns_rhi_desc;

ns_rhi *ns_rhi_create(const ns_rhi_desc *desc);
void    ns_rhi_destroy(ns_rhi *r);

SDL_Window   *ns_rhi_window(ns_rhi *r);
SDL_GPUDevice *ns_rhi_device(ns_rhi *r);
const char   *ns_rhi_backend_name(ns_rhi *r);

void ns_rhi_drawable_size(ns_rhi *r, uint32_t *w, uint32_t *h);
void ns_rhi_set_vsync(ns_rhi *r, bool vsync);

/*
 * La définition de la fenêtre et le plein écran, EN COURS DE PARTIE.
 *
 * Existe pour le menu de réglages : `window.width`, `window.height` et
 * `window.fullscreen` étaient lus au démarrage et jamais écrits, donc réglables
 * seulement en éditant à la main un fichier que le joueur ne sait pas où
 * trouver. En plein écran, la définition passée reste celle du mode FENÊTRÉ —
 * c'est elle qu'on retrouve en ressortant.
 */
void ns_rhi_set_window_mode(ns_rhi *r, int width, int height, bool fullscreen);

/* ========================================================================== */
/* Cycle de frame                                                             */
/* ========================================================================== */
/*
 *   if (ns_rhi_begin_frame(r)) {
 *       … passes de rendu …
 *       ns_rhi_end_frame(r);
 *   }
 *
 * begin_frame renvoie false quand la swapchain n'est pas disponible (fenêtre
 * minimisée, redimensionnement en cours) : ce n'est pas une erreur, on saute
 * simplement l'image.
 */
bool ns_rhi_begin_frame(ns_rhi *r);
void ns_rhi_end_frame(ns_rhi *r);

/*
 * Abandonne l'image au lieu de la présenter. À employer quand le rendu n'a rien
 * écrit dans la cible : présenter une swapchain vide donne une image NOIRE, et
 * c'est ce qu'on voyait par intermittence pendant un redimensionnement ou un
 * changement de palier de qualité.
 */
void ns_rhi_cancel_frame(ns_rhi *r);

/*
 * Attend que le GPU ait tout terminé.
 *
 * À n'employer QUE pour mesurer : en fonctionnement normal, laisser le CPU
 * prendre de l'avance sur le GPU est précisément ce qui donne du débit. Mais sans
 * cette attente, un chronomètre côté CPU mesure la vitesse à laquelle on
 * *enregistre* les commandes, pas celle à laquelle elles s'exécutent — et c'est
 * ainsi qu'un rendu à 1 image par seconde a pu être annoncé à 1 793.
 */
void ns_rhi_wait_idle(ns_rhi *r);

SDL_GPUCommandBuffer *ns_rhi_cmd(ns_rhi *r);
SDL_GPUTexture       *ns_rhi_swapchain_texture(ns_rhi *r);
SDL_GPUTextureFormat  ns_rhi_swapchain_format(ns_rhi *r);
uint64_t              ns_rhi_frame_index(ns_rhi *r);

/* ========================================================================== */
/* Tampons                                                                    */
/* ========================================================================== */

typedef enum ns_buffer_kind {
    NS_BUFFER_VERTEX = 0,
    NS_BUFFER_INDEX,
    NS_BUFFER_STORAGE,        /* lisible par les shaders — BVH, lumières, instances */
    NS_BUFFER_STORAGE_RW,     /* lisible et inscriptible par le compute */
    NS_BUFFER_INDIRECT
} ns_buffer_kind;

typedef struct ns_buffer {
    SDL_GPUBuffer *handle;
    uint32_t       size;
    ns_buffer_kind kind;
    const char    *name;
} ns_buffer;

bool ns_buffer_create(ns_rhi *r, ns_buffer *out, ns_buffer_kind kind, uint32_t size, const char *name);
void ns_buffer_destroy(ns_rhi *r, ns_buffer *b);

/* Téléversement immédiat (crée et soumet son propre command buffer).
 * À réserver au chargement : pendant le jeu, préférer ns_rhi_stage_*. */
bool ns_buffer_upload(ns_rhi *r, ns_buffer *b, const void *data, uint32_t size, uint32_t offset);

/* ========================================================================== */
/* Textures                                                                   */
/* ========================================================================== */

typedef struct ns_texture_desc {
    uint32_t             width, height;
    uint32_t             layers;          /* 1 par défaut ; 6 pour une cubemap */
    uint32_t             mip_levels;      /* 0 = chaîne complète */
    SDL_GPUTextureFormat format;
    SDL_GPUTextureType   type;
    bool                 render_target;
    bool                 depth_target;
    bool                 storage_read;    /* lisible en compute */
    bool                 storage_write;   /* inscriptible en compute */
    bool                 sampled;         /* échantillonnable (défaut : vrai) */
    const char          *name;
} ns_texture_desc;

typedef struct ns_texture {
    SDL_GPUTexture      *handle;
    uint32_t             width, height, layers, mip_levels;
    SDL_GPUTextureFormat format;
    const char          *name;
} ns_texture;

bool ns_texture_create(ns_rhi *r, ns_texture *out, const ns_texture_desc *desc);
void ns_texture_destroy(ns_rhi *r, ns_texture *t);

/* Téléversement immédiat de pixels (niveau 0, couche 0). */
bool ns_texture_upload(ns_rhi *r, ns_texture *t, const void *pixels, uint32_t bytes);

/* Charge une image (PNG/JPEG) depuis un chemin logique et crée la texture.
 * `srgb` doit être vrai pour les couleurs (albédo, émissif) et faux pour les
 * données (normales, rugosité) — se tromper là-dessus est la première cause
 * de rendu PBR qui « ne ressemble à rien ». */
bool ns_texture_load(ns_rhi *r, ns_texture *out, const char *logical_path, bool srgb, bool gen_mips);

/* Texture 1x1 utilisable comme substitut quand un asset manque. */
ns_texture ns_texture_white(ns_rhi *r);
ns_texture ns_texture_black(ns_rhi *r);
ns_texture ns_texture_flat_normal(ns_rhi *r);

/* ========================================================================== */
/* Échantillonneurs                                                           */
/* ========================================================================== */

typedef enum ns_sampler_kind {
    NS_SAMPLER_LINEAR_REPEAT = 0,
    NS_SAMPLER_LINEAR_CLAMP,
    NS_SAMPLER_NEAREST_REPEAT,
    NS_SAMPLER_NEAREST_CLAMP,     /* jeux 2D : pas de flou sur les pixel arts */
    NS_SAMPLER_ANISO_REPEAT,      /* sols et murs vus en rasant */
    NS_SAMPLER_SHADOW,            /* comparaison de profondeur, filtrage matériel */
    NS_SAMPLER_COUNT
} ns_sampler_kind;

SDL_GPUSampler *ns_rhi_sampler(ns_rhi *r, ns_sampler_kind kind);

/* ========================================================================== */
/* Shaders et pipelines                                                       */
/* ========================================================================== */
/*
 * Les shaders sont retrouvés par nom dans le registre embarqué généré par
 * cmake/EmbedShaders.cmake ("room_gbuffer.vert"). Les compteurs de ressources
 * doivent correspondre aux déclarations du GLSL : SDL en a besoin pour préparer
 * les tables de descripteurs, et une erreur ici se traduit par un écran noir
 * silencieux — d'où la vérification explicite au chargement.
 */
typedef struct ns_shader_desc {
    const char *name;                 /* p. ex. "room_gbuffer.vert" */
    uint32_t    num_samplers;
    uint32_t    num_storage_textures;
    uint32_t    num_storage_buffers;
    uint32_t    num_uniform_buffers;
} ns_shader_desc;

SDL_GPUShader *ns_shader_load(ns_rhi *r, const ns_shader_desc *desc, SDL_GPUShaderStage stage);

typedef struct ns_compute_desc {
    const char *name;
    uint32_t    num_samplers;
    uint32_t    num_readonly_storage_textures;
    uint32_t    num_readonly_storage_buffers;
    uint32_t    num_readwrite_storage_textures;
    uint32_t    num_readwrite_storage_buffers;
    uint32_t    num_uniform_buffers;
    uint32_t    threads_x, threads_y, threads_z;
} ns_compute_desc;

SDL_GPUComputePipeline *ns_compute_pipeline_create(ns_rhi *r, const ns_compute_desc *desc);

/* ========================================================================== */
/* Téléversement par anneau (chemin chaud)                                    */
/* ========================================================================== */
/*
 * Les données qui changent chaque image (instances, particules, sprites) passent
 * par un anneau de tampons de transfert dimensionné une fois. Cela évite la
 * création/destruction d'un tampon par appel, qui écroulerait le framerate.
 */
bool ns_rhi_stage_buffer(ns_rhi *r, ns_buffer *dst, const void *data, uint32_t size, uint32_t dst_offset);
void ns_rhi_flush_staging(ns_rhi *r);   /* appelé automatiquement par end_frame */

/* ========================================================================== */
/* Capture d'écran                                                            */
/* ========================================================================== */
/*
 * Lit une texture couleur et l'écrit en PNG. Deux usages :
 *   - la touche de capture pour le joueur ;
 *   - la validation automatisée du rendu (`--screenshot=…` en headless), qui
 *     permet de comparer deux versions du moteur sans GPU ni écran.
 */
bool ns_rhi_capture_texture_png(ns_rhi *r, SDL_GPUTexture *src,
                                uint32_t width, uint32_t height,
                                SDL_GPUTextureFormat format, const char *out_path);

/* Capture l'image courante. À appeler entre begin_frame et end_frame. */
bool ns_rhi_request_screenshot(ns_rhi *r, const char *out_path);

#endif /* NS_RHI_H */
