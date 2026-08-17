/* ns_rhi.c — implémentation de la couche de rendu au-dessus de SDL3 GPU. */
#include "ns_rhi.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO           /* on lit par nos propres points de montage */
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <string.h>

/* Taille de l'anneau de transfert : dimensionnée pour une image chargée
 * (instances + particules + sprites d'un mini-jeu). Le dépassement est signalé
 * plutôt que silencieusement contourné. */
#define NS_STAGING_RING_BYTES (16u * 1024u * 1024u)
#define NS_STAGING_MAX_COPIES 512

typedef struct staged_copy {
    SDL_GPUBuffer *dst;
    uint32_t       src_offset;
    uint32_t       dst_offset;
    uint32_t       size;
} staged_copy;

struct ns_rhi {
    SDL_Window     *window;
    SDL_GPUDevice  *device;
    const char     *backend;

    /* Frame courante */
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture       *swapchain;
    uint32_t              sc_width, sc_height;
    SDL_GPUTextureFormat  sc_format;
    uint64_t              frame_index;
    bool                  frame_active;

    /* Anneau de transfert */
    SDL_GPUTransferBuffer *staging;
    uint8_t               *staging_mapped;
    uint32_t               staging_head;
    staged_copy            copies[NS_STAGING_MAX_COPIES];
    uint32_t               copy_count;

    SDL_GPUSampler *samplers[NS_SAMPLER_COUNT];

    /* Capture demandée pour l'image en cours */
    char screenshot_path[1024];
    bool screenshot_pending;

    bool headless;
    bool vsync;
};

/* ========================================================================== */
/* Utilitaires internes                                                       */
/* ========================================================================== */

static uint32_t format_bytes_per_pixel(SDL_GPUTextureFormat f)
{
    switch (f) {
    case SDL_GPU_TEXTUREFORMAT_R8_UNORM:              return 1;
    case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:            return 2;
    case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:             return 2;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
    case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT:
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:     return 4;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:    return 8;
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:    return 16;
    default:                                          return 4;
    }
}

static bool format_is_bgra(SDL_GPUTextureFormat f)
{
    return f == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM
        || f == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
}

static uint32_t mip_count_for(uint32_t w, uint32_t h)
{
    uint32_t levels = 1;
    while (w > 1 || h > 1) { w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1; levels++; }
    return levels;
}

/* ========================================================================== */
/* Création / destruction                                                     */
/* ========================================================================== */

static void create_default_samplers(ns_rhi *r)
{
    struct { ns_sampler_kind kind; SDL_GPUFilter filter; SDL_GPUSamplerAddressMode mode;
             bool aniso; bool compare; SDL_GPUSamplerMipmapMode mip; } defs[] = {
        { NS_SAMPLER_LINEAR_REPEAT,  SDL_GPU_FILTER_LINEAR,  SDL_GPU_SAMPLERADDRESSMODE_REPEAT,         false, false, SDL_GPU_SAMPLERMIPMAPMODE_LINEAR },
        { NS_SAMPLER_LINEAR_CLAMP,   SDL_GPU_FILTER_LINEAR,  SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,  false, false, SDL_GPU_SAMPLERMIPMAPMODE_LINEAR },
        { NS_SAMPLER_NEAREST_REPEAT, SDL_GPU_FILTER_NEAREST, SDL_GPU_SAMPLERADDRESSMODE_REPEAT,         false, false, SDL_GPU_SAMPLERMIPMAPMODE_NEAREST },
        { NS_SAMPLER_NEAREST_CLAMP,  SDL_GPU_FILTER_NEAREST, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,  false, false, SDL_GPU_SAMPLERMIPMAPMODE_NEAREST },
        { NS_SAMPLER_ANISO_REPEAT,   SDL_GPU_FILTER_LINEAR,  SDL_GPU_SAMPLERADDRESSMODE_REPEAT,         true,  false, SDL_GPU_SAMPLERMIPMAPMODE_LINEAR },
        { NS_SAMPLER_SHADOW,         SDL_GPU_FILTER_LINEAR,  SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,  false, true,  SDL_GPU_SAMPLERMIPMAPMODE_NEAREST },
    };

    for (size_t i = 0; i < SDL_arraysize(defs); ++i) {
        SDL_GPUSamplerCreateInfo info;
        SDL_zero(info);
        info.min_filter     = defs[i].filter;
        info.mag_filter     = defs[i].filter;
        info.mipmap_mode    = defs[i].mip;
        info.address_mode_u = defs[i].mode;
        info.address_mode_v = defs[i].mode;
        info.address_mode_w = defs[i].mode;
        info.max_lod        = 1000.0f;
        if (defs[i].aniso) {
            info.enable_anisotropy = true;
            info.max_anisotropy    = 16.0f;
        }
        if (defs[i].compare) {
            info.enable_compare = true;
            /* Reverse-Z : le fragment est éclairé quand sa profondeur est
             * SUPÉRIEURE à celle stockée dans la shadow map. */
            info.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        }
        r->samplers[defs[i].kind] = SDL_CreateGPUSampler(r->device, &info);
        if (!r->samplers[defs[i].kind]) {
            NS_ERROR("échantillonneur %d non créé : %s", (int)defs[i].kind, SDL_GetError());
        }
    }
}

ns_rhi *ns_rhi_create(const ns_rhi_desc *desc)
{
    NS_ASSERT(desc != NULL);

    ns_rhi *r = (ns_rhi *)ns_calloc(1, sizeof *r);
    if (!r) return NULL;

    r->headless = desc->headless;
    r->vsync    = desc->vsync;

    /* On demande tous les formats de shaders que l'on sait produire ; SDL choisit
     * le backend en fonction de la plateforme. SPIR-V couvre Vulkan, DXIL le
     * D3D12, MSL le Metal — le même GLSL source produit les trois via la CI. */
    const SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;

    r->device = SDL_CreateGPUDevice(formats, desc->debug, NULL);
    if (!r->device) {
        NS_ERROR("aucun périphérique GPU utilisable : %s", SDL_GetError());
        ns_free(r);
        return NULL;
    }
    r->backend = SDL_GetGPUDeviceDriver(r->device);
    NS_INFO("GPU : backend %s%s", r->backend ? r->backend : "?", desc->debug ? " (validation active)" : "");

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc->fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    if (desc->headless)   flags |= SDL_WINDOW_HIDDEN;

    r->window = SDL_CreateWindow(desc->window_title ? desc->window_title : "Nineteen",
                                 desc->width > 0 ? desc->width : 1280,
                                 desc->height > 0 ? desc->height : 720,
                                 flags);
    if (!r->window) {
        NS_ERROR("fenêtre non créée : %s", SDL_GetError());
        SDL_DestroyGPUDevice(r->device);
        ns_free(r);
        return NULL;
    }

    if (!SDL_ClaimWindowForGPUDevice(r->device, r->window)) {
        NS_ERROR("fenêtre non rattachée au GPU : %s", SDL_GetError());
        SDL_DestroyWindow(r->window);
        SDL_DestroyGPUDevice(r->device);
        ns_free(r);
        return NULL;
    }

    ns_rhi_set_vsync(r, desc->vsync);
    r->sc_format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window);

    if (desc->frames_in_flight > 0) {
        SDL_SetGPUAllowedFramesInFlight(r->device, desc->frames_in_flight);
    }

    /* Anneau de transfert persistant. */
    SDL_GPUTransferBufferCreateInfo tb;
    SDL_zero(tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size  = NS_STAGING_RING_BYTES;
    r->staging = SDL_CreateGPUTransferBuffer(r->device, &tb);
    if (!r->staging) {
        NS_ERROR("anneau de transfert non créé : %s", SDL_GetError());
    }

    create_default_samplers(r);

    NS_INFO("RHI prêt : %dx%d, format swapchain %d%s",
            desc->width, desc->height, (int)r->sc_format, desc->headless ? ", headless" : "");
    return r;
}

void ns_rhi_destroy(ns_rhi *r)
{
    if (!r) return;

    SDL_WaitForGPUIdle(r->device);

    for (int i = 0; i < NS_SAMPLER_COUNT; ++i) {
        if (r->samplers[i]) SDL_ReleaseGPUSampler(r->device, r->samplers[i]);
    }
    if (r->staging) SDL_ReleaseGPUTransferBuffer(r->device, r->staging);
    if (r->window) {
        SDL_ReleaseWindowFromGPUDevice(r->device, r->window);
        SDL_DestroyWindow(r->window);
    }
    SDL_DestroyGPUDevice(r->device);
    ns_free(r);
}

SDL_Window    *ns_rhi_window(ns_rhi *r)  { return r->window; }
SDL_GPUDevice *ns_rhi_device(ns_rhi *r)  { return r->device; }
const char    *ns_rhi_backend_name(ns_rhi *r) { return r->backend ? r->backend : "?"; }
SDL_GPUCommandBuffer *ns_rhi_cmd(ns_rhi *r) { return r->cmd; }
SDL_GPUTexture       *ns_rhi_swapchain_texture(ns_rhi *r) { return r->swapchain; }
SDL_GPUTextureFormat  ns_rhi_swapchain_format(ns_rhi *r) { return r->sc_format; }
uint64_t              ns_rhi_frame_index(ns_rhi *r) { return r->frame_index; }

void ns_rhi_drawable_size(ns_rhi *r, uint32_t *w, uint32_t *h)
{
    int iw = 0, ih = 0;
    SDL_GetWindowSizeInPixels(r->window, &iw, &ih);
    if (w) *w = (uint32_t)(iw > 0 ? iw : 0);
    if (h) *h = (uint32_t)(ih > 0 ? ih : 0);
}

void ns_rhi_set_vsync(ns_rhi *r, bool vsync)
{
    r->vsync = vsync;
    /* MAILBOX (triple buffering) évite le déchirement sans plafonner la
     * simulation ; il n'est pas garanti partout, d'où le repli sur VSYNC. */
    SDL_GPUPresentMode mode = vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (!vsync && !SDL_WindowSupportsGPUPresentMode(r->device, r->window, mode)) {
        mode = SDL_GPU_PRESENTMODE_VSYNC;
    }
    if (!SDL_SetGPUSwapchainParameters(r->device, r->window,
                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        NS_WARN("mode de présentation refusé : %s", SDL_GetError());
    }
}

/* ========================================================================== */
/* Frame                                                                      */
/* ========================================================================== */

bool ns_rhi_begin_frame(ns_rhi *r)
{
    NS_ASSERT(!r->frame_active);

    r->cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!r->cmd) {
        NS_ERROR("command buffer indisponible : %s", SDL_GetError());
        return false;
    }

    /* En headless il n'y a pas de swapchain exploitable : le rendu vise des
     * cibles hors écran et la capture les lit directement. */
    if (!r->headless) {
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(r->cmd, r->window, &r->swapchain,
                                                   &r->sc_width, &r->sc_height)) {
            NS_WARN("swapchain indisponible : %s", SDL_GetError());
            SDL_SubmitGPUCommandBuffer(r->cmd);
            r->cmd = NULL;
            return false;
        }
        if (!r->swapchain) {          /* fenêtre minimisée : image sautée, pas une erreur */
            SDL_SubmitGPUCommandBuffer(r->cmd);
            r->cmd = NULL;
            return false;
        }
    } else {
        r->swapchain = NULL;
        ns_rhi_drawable_size(r, &r->sc_width, &r->sc_height);
    }

    r->staging_head = 0;
    r->copy_count   = 0;
    r->frame_active = true;
    return true;
}

void ns_rhi_end_frame(ns_rhi *r)
{
    NS_ASSERT(r->frame_active);

    ns_rhi_flush_staging(r);

    if (!SDL_SubmitGPUCommandBuffer(r->cmd)) {
        NS_ERROR("soumission refusée : %s", SDL_GetError());
    }
    r->cmd = NULL;
    r->swapchain = NULL;
    r->frame_active = false;
    r->frame_index++;
}

/* ========================================================================== */
/* Tampons                                                                    */
/* ========================================================================== */

bool ns_buffer_create(ns_rhi *r, ns_buffer *out, ns_buffer_kind kind, uint32_t size, const char *name)
{
    NS_ASSERT(out != NULL);
    NS_ASSERT(size > 0);
    SDL_zerop(out);

    SDL_GPUBufferUsageFlags usage = 0;
    switch (kind) {
    case NS_BUFFER_VERTEX:     usage = SDL_GPU_BUFFERUSAGE_VERTEX; break;
    case NS_BUFFER_INDEX:      usage = SDL_GPU_BUFFERUSAGE_INDEX;  break;
    case NS_BUFFER_STORAGE:    usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ
                                     | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ; break;
    case NS_BUFFER_STORAGE_RW: usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
                                     | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE
                                     | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ; break;
    case NS_BUFFER_INDIRECT:   usage = SDL_GPU_BUFFERUSAGE_INDIRECT; break;
    }

    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = usage;
    info.size  = size;

    out->handle = SDL_CreateGPUBuffer(r->device, &info);
    if (!out->handle) {
        NS_ERROR("tampon « %s » (%u octets) non créé : %s", name ? name : "?", size, SDL_GetError());
        return false;
    }
    out->size = size;
    out->kind = kind;
    out->name = name;
    if (name) SDL_SetGPUBufferName(r->device, out->handle, name);
    return true;
}

void ns_buffer_destroy(ns_rhi *r, ns_buffer *b)
{
    if (!b || !b->handle) return;
    SDL_ReleaseGPUBuffer(r->device, b->handle);
    SDL_zerop(b);
}

bool ns_buffer_upload(ns_rhi *r, ns_buffer *b, const void *data, uint32_t size, uint32_t offset)
{
    NS_ASSERT(b && b->handle && data);
    if (size == 0) return true;
    if (offset > b->size || size > b->size - offset) {
        NS_ERROR("téléversement hors bornes dans « %s » : %u+%u > %u",
                 b->name ? b->name : "?", offset, size, b->size);
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tb;
    SDL_zero(tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size  = size;

    SDL_GPUTransferBuffer *tmp = SDL_CreateGPUTransferBuffer(r->device, &tb);
    if (!tmp) {
        NS_ERROR("tampon de transfert non créé : %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(r->device, tmp, false);
    if (!mapped) {
        NS_ERROR("projection du transfert impossible : %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(r->device, tmp);
        return false;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(r->device, tmp);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src = { tmp, 0 };
    SDL_GPUBufferRegion dst = { b->handle, offset, size };
    SDL_UploadToGPUBuffer(pass, &src, &dst, false);

    SDL_EndGPUCopyPass(pass);

    /* Attente explicite : l'appelant s'attend à pouvoir libérer `data` au retour. */
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(r->device, tmp);
    return true;
}

/* ========================================================================== */
/* Anneau de transfert                                                        */
/* ========================================================================== */

bool ns_rhi_stage_buffer(ns_rhi *r, ns_buffer *dst, const void *data, uint32_t size, uint32_t dst_offset)
{
    NS_ASSERT(r->frame_active);
    if (size == 0) return true;

    if (!r->staging) return false;
    if (r->copy_count >= NS_STAGING_MAX_COPIES) {
        NS_ERROR("trop de copies en attente dans l'image (%u)", r->copy_count);
        return false;
    }

    /* Alignement à 16 octets : exigé par plusieurs pilotes pour les copies. */
    const uint32_t head = (r->staging_head + 15u) & ~15u;
    if (head + size > NS_STAGING_RING_BYTES) {
        NS_ERROR("anneau de transfert saturé : %u octets demandés, %u restants",
                 size, NS_STAGING_RING_BYTES - head);
        return false;
    }

    if (!r->staging_mapped) {
        /* `cycle = true` sur la première projection de l'image : SDL fournit une
         * zone qui n'est plus lue par le GPU, ce qui évite d'attendre. */
        r->staging_mapped = (uint8_t *)SDL_MapGPUTransferBuffer(r->device, r->staging, true);
        if (!r->staging_mapped) {
            NS_ERROR("projection de l'anneau impossible : %s", SDL_GetError());
            return false;
        }
    }

    SDL_memcpy(r->staging_mapped + head, data, size);

    r->copies[r->copy_count].dst        = dst->handle;
    r->copies[r->copy_count].src_offset = head;
    r->copies[r->copy_count].dst_offset = dst_offset;
    r->copies[r->copy_count].size       = size;
    r->copy_count++;
    r->staging_head = head + size;
    return true;
}

void ns_rhi_flush_staging(ns_rhi *r)
{
    if (r->copy_count == 0) return;

    if (r->staging_mapped) {
        SDL_UnmapGPUTransferBuffer(r->device, r->staging);
        r->staging_mapped = NULL;
    }

    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(r->cmd);
    for (uint32_t i = 0; i < r->copy_count; ++i) {
        SDL_GPUTransferBufferLocation src = { r->staging, r->copies[i].src_offset };
        SDL_GPUBufferRegion dst = { r->copies[i].dst, r->copies[i].dst_offset, r->copies[i].size };
        SDL_UploadToGPUBuffer(pass, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(pass);
    r->copy_count = 0;
}

/* ========================================================================== */
/* Textures                                                                   */
/* ========================================================================== */

bool ns_texture_create(ns_rhi *r, ns_texture *out, const ns_texture_desc *d)
{
    NS_ASSERT(out && d);
    NS_ASSERT(d->width > 0 && d->height > 0);
    SDL_zerop(out);

    SDL_GPUTextureUsageFlags usage = 0;
    if (d->sampled || (!d->render_target && !d->depth_target && !d->storage_write)) {
        usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    }
    if (d->render_target)  usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (d->depth_target)   usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (d->storage_read)   usage |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ
                                  | SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
    if (d->storage_write)  usage |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

    const uint32_t layers = d->layers ? d->layers : 1;
    const uint32_t mips   = d->mip_levels ? d->mip_levels : 1;

    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type                 = d->type ? d->type : SDL_GPU_TEXTURETYPE_2D;
    info.format               = d->format;
    info.usage                = usage;
    info.width                = d->width;
    info.height               = d->height;
    info.layer_count_or_depth = layers;
    info.num_levels           = mips;
    info.sample_count         = SDL_GPU_SAMPLECOUNT_1;

    out->handle = SDL_CreateGPUTexture(r->device, &info);
    if (!out->handle) {
        NS_ERROR("texture « %s » %ux%u non créée : %s",
                 d->name ? d->name : "?", d->width, d->height, SDL_GetError());
        return false;
    }
    out->width = d->width; out->height = d->height;
    out->layers = layers;  out->mip_levels = mips;
    out->format = d->format;
    out->name = d->name;
    if (d->name) SDL_SetGPUTextureName(r->device, out->handle, d->name);
    return true;
}

void ns_texture_destroy(ns_rhi *r, ns_texture *t)
{
    if (!t || !t->handle) return;
    SDL_ReleaseGPUTexture(r->device, t->handle);
    SDL_zerop(t);
}

bool ns_texture_upload(ns_rhi *r, ns_texture *t, const void *pixels, uint32_t bytes)
{
    NS_ASSERT(t && t->handle && pixels);

    const uint32_t expected = t->width * t->height * format_bytes_per_pixel(t->format);
    if (bytes < expected) {
        NS_ERROR("téléversement trop court pour « %s » : %u octets fournis, %u attendus",
                 t->name ? t->name : "?", bytes, expected);
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tb;
    SDL_zero(tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size  = expected;

    SDL_GPUTransferBuffer *tmp = SDL_CreateGPUTransferBuffer(r->device, &tb);
    if (!tmp) {
        NS_ERROR("transfert de texture non créé : %s", SDL_GetError());
        return false;
    }
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tmp, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(r->device, tmp);
        return false;
    }
    SDL_memcpy(mapped, pixels, expected);
    SDL_UnmapGPUTransferBuffer(r->device, tmp);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src;
    SDL_zero(src);
    src.transfer_buffer = tmp;
    src.pixels_per_row  = t->width;
    src.rows_per_layer  = t->height;

    SDL_GPUTextureRegion dst;
    SDL_zero(dst);
    dst.texture = t->handle;
    dst.w = t->width; dst.h = t->height; dst.d = 1;

    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);

    if (t->mip_levels > 1) {
        SDL_GenerateMipmapsForGPUTexture(cmd, t->handle);
    }

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(r->device, tmp);
    return true;
}

bool ns_texture_load(ns_rhi *r, ns_texture *out, const char *logical_path, bool srgb, bool gen_mips)
{
    NS_ASSERT(out && logical_path);

    ns_arena tmp;
    if (!ns_arena_init(&tmp, 64u * 1024u * 1024u, "chargement image")) return false;

    size_t file_size = 0;
    void *file = ns_file_read_all(&tmp, logical_path, &file_size);
    if (!file) { ns_arena_free(&tmp); return false; }

    int w = 0, h = 0, channels = 0;
    /* Toujours 4 canaux : les formats à 3 canaux n'existent pas côté GPU. */
    stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)file, (int)file_size, &w, &h, &channels, 4);
    ns_arena_free(&tmp);

    if (!pixels) {
        NS_ERROR("image illisible (%s) : %s", logical_path, stbi_failure_reason());
        return false;
    }

    ns_texture_desc d;
    SDL_zero(d);
    d.width  = (uint32_t)w;
    d.height = (uint32_t)h;
    d.format = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                    : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    d.sampled = true;
    d.mip_levels = gen_mips ? mip_count_for((uint32_t)w, (uint32_t)h) : 1;
    d.name = logical_path;

    /* Les mipmaps sont générées sur GPU, ce qui exige que la texture soit aussi
     * une cible de rendu — contrainte de l'API, pas un choix. */
    if (gen_mips) d.render_target = true;

    bool ok = ns_texture_create(r, out, &d);
    if (ok) ok = ns_texture_upload(r, out, pixels, (uint32_t)(w * h * 4));

    stbi_image_free(pixels);
    if (ok) {
        NS_DEBUG("texture %s : %dx%d, %u niveaux, %s", logical_path, w, h, d.mip_levels,
                 srgb ? "sRGB" : "linéaire");
    }
    return ok;
}

static ns_texture make_solid(ns_rhi *r, uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa, const char *name)
{
    ns_texture t;
    ns_texture_desc d;
    SDL_zero(d);
    d.width = d.height = 1;
    d.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    d.sampled = true;
    d.name = name;
    if (!ns_texture_create(r, &t, &d)) { SDL_zero(t); return t; }
    const uint8_t px[4] = { rr, gg, bb, aa };
    ns_texture_upload(r, &t, px, 4);
    return t;
}

ns_texture ns_texture_white(ns_rhi *r)       { return make_solid(r, 255, 255, 255, 255, "blanc 1x1"); }
ns_texture ns_texture_black(ns_rhi *r)       { return make_solid(r, 0, 0, 0, 255, "noir 1x1"); }
ns_texture ns_texture_flat_normal(ns_rhi *r) { return make_solid(r, 128, 128, 255, 255, "normale plate 1x1"); }

SDL_GPUSampler *ns_rhi_sampler(ns_rhi *r, ns_sampler_kind kind)
{
    NS_ASSERT(kind >= 0 && kind < NS_SAMPLER_COUNT);
    return r->samplers[kind];
}

/* ========================================================================== */
/* Capture                                                                    */
/* ========================================================================== */

bool ns_rhi_capture_texture_png(ns_rhi *r, SDL_GPUTexture *src,
                                uint32_t width, uint32_t height,
                                SDL_GPUTextureFormat format, const char *out_path)
{
    NS_ASSERT(src && out_path);
    if (width == 0 || height == 0) return false;

    const uint32_t bpp   = format_bytes_per_pixel(format);
    const uint32_t bytes = width * height * bpp;

    SDL_GPUTransferBufferCreateInfo tb;
    SDL_zero(tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tb.size  = bytes;

    SDL_GPUTransferBuffer *dl = SDL_CreateGPUTransferBuffer(r->device, &tb);
    if (!dl) {
        NS_ERROR("tampon de relecture non créé : %s", SDL_GetError());
        return false;
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureRegion region;
    SDL_zero(region);
    region.texture = src;
    region.w = width; region.h = height; region.d = 1;

    SDL_GPUTextureTransferInfo dst;
    SDL_zero(dst);
    dst.transfer_buffer = dl;
    dst.pixels_per_row  = width;
    dst.rows_per_layer  = height;

    SDL_DownloadFromGPUTexture(pass, &region, &dst);
    SDL_EndGPUCopyPass(pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }

    void *mapped = SDL_MapGPUTransferBuffer(r->device, dl, false);
    if (!mapped) {
        NS_ERROR("relecture impossible : %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(r->device, dl);
        return false;
    }

    uint8_t *rgba = (uint8_t *)ns_alloc(width * height * 4);
    if (!rgba) {
        SDL_UnmapGPUTransferBuffer(r->device, dl);
        SDL_ReleaseGPUTransferBuffer(r->device, dl);
        return false;
    }

    const uint8_t *srcpx = (const uint8_t *)mapped;
    const bool swap_rb = format_is_bgra(format);
    for (uint32_t i = 0; i < width * height; ++i) {
        const uint8_t *p = srcpx + (size_t)i * bpp;
        rgba[i * 4 + 0] = swap_rb ? p[2] : p[0];
        rgba[i * 4 + 1] = p[1];
        rgba[i * 4 + 2] = swap_rb ? p[0] : p[2];
        rgba[i * 4 + 3] = (bpp >= 4) ? p[3] : 255;
    }
    SDL_UnmapGPUTransferBuffer(r->device, dl);
    SDL_ReleaseGPUTransferBuffer(r->device, dl);

    const int ok = stbi_write_png(out_path, (int)width, (int)height, 4, rgba, (int)width * 4);
    ns_free(rgba);

    if (!ok) {
        NS_ERROR("écriture PNG impossible : %s", out_path);
        return false;
    }
    NS_INFO("capture écrite : %s (%ux%u)", out_path, width, height);
    return true;
}

bool ns_rhi_request_screenshot(ns_rhi *r, const char *out_path)
{
    if (!out_path) return false;
    SDL_strlcpy(r->screenshot_path, out_path, sizeof r->screenshot_path);
    r->screenshot_pending = true;
    return true;
}
