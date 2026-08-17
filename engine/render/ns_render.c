/* ns_render.c — implémentation du pipeline de rendu. */
#include "ns_render.h"

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
    uint32_t   rt_current;          /* index d'écriture du ping-pong */
    uint32_t   accum_frames;        /* images accumulées depuis le dernier mouvement */
    ns_texture *rt_denoised;        /* dernière sortie de débruitage utilisable */
    ns_texture bloom[BLOOM_MIPS];
    ns_texture bloom_tmp[BLOOM_MIPS];

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
    SDL_GPUTextureFormat     tonemap_format;

    /* Tampon des lumières, réécrit à chaque image (elles scintillent). */
    ns_buffer lights;

    float prev_view_proj[16];
    ns_render_stats stats;
    bool targets_ready;
    bool reflections_cleared;
};

/* ========================================================================== */
/* Vues de débogage                                                           */
/* ========================================================================== */

static const char *const g_debug_names[NS_DEBUG_COUNT] = {
    "none", "albedo", "normal", "emissive", "depth", "visibility", "hdr", "bloom"
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

    /* Brouillard très léger, teinté du bleu froid des néons : donne de la
     * profondeur au fond de la salle sans laiter l'image. */
    s->fog_density = 0.012f;
    s->fog_color[0] = 0.055f; s->fog_color[1] = 0.062f; s->fog_color[2] = 0.085f;

    /*
     * Ambiance. Une salle d'arcade tire sa lumière de ses machines plutôt que
     * d'un éclairage général, et c'est ce contraste qui la rend crédible — mais
     * une ambiance trop faible écrase tout ce que les sources n'atteignent pas,
     * et la salle devient illisible. Ce niveau garde les recoins lisibles sans
     * effacer le relief que créent les néons.
     */
    s->ambient[0] = 0.26f; s->ambient[1] = 0.27f; s->ambient[2] = 0.34f;
    s->ambient_intensity = 0.95f;

    s->ssao_radius = 0.45f;
    s->ssao_intensity = 0.85f;

    switch (quality) {
    case NS_QUALITY_LOW:
        s->raytracing = NS_RT_OFF;
        s->render_scale = 0.75f;
        s->ssao_samples = 6;
        s->rt_rays_per_pixel = 0;
        s->chromatic_aberration = 0.0f;
        break;
    case NS_QUALITY_MEDIUM:
        s->raytracing = NS_RT_SHADOWS;
        s->ssao_samples = 10;
        s->rt_rays_per_pixel = 1;
        break;
    case NS_QUALITY_HIGH:
        s->raytracing = NS_RT_REFLECTIONS;
        s->ssao_samples = 16;
        s->rt_rays_per_pixel = 2;
        break;
    case NS_QUALITY_ULTRA:
        s->raytracing = NS_RT_FULL;
        s->ssao_samples = 24;
        s->rt_rays_per_pixel = 4;
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
    for (int i = 0; i < 2 && ok; ++i) {
        ns_texture_desc d;
        SDL_zero(d);
        d.width = rw; d.height = rh;
        d.format = FMT_HDR;
        d.sampled = true;
        d.storage_write = true;
        d.name = "visibilité RT";
        ok = ok && ns_texture_create(r, &rd->rt_visibility[i], &d);
    }
    {
        ns_texture_desc d;
        SDL_zero(d);
        d.width = rw; d.height = rh;
        d.format = FMT_HDR;
        d.sampled = true;
        d.storage_write = true;
        d.render_target = true;      /* pour l'effacement explicite */
        d.name = "réflexions";
        ok = ok && ns_texture_create(r, &rd->reflections, &d);
    }
    /* Cibles de débruitage : deux passes à-trous en aller-retour. */
    for (int i = 0; i < 2 && ok; ++i) {
        ok = ok && make_target(r, &rd->rt_filtered[i], rw, rh, FMT_HDR, false, "RT filtré");
    }
    rd->rt_current = 0;
    rd->accum_frames = 0;
    rd->rt_denoised = NULL;

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

/* Pipeline plein écran : pas de tampon de sommets, pas de test de profondeur. */
static SDL_GPUGraphicsPipeline *make_fullscreen_pipeline(
    ns_rhi *r, const char *frag_name, uint32_t samplers, uint32_t storage_buffers,
    const SDL_GPUTextureFormat *formats, uint32_t format_count)
{
    ns_shader_desc vsd = { .name = "fullscreen.vert" };
    ns_shader_desc fsd = {
        .name = frag_name,
        .num_samplers = samplers,
        .num_storage_buffers = storage_buffers,
        .num_uniform_buffers = 1,
    };

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
    ns_shader_desc vsd = { .name = "gbuffer.vert", .num_uniform_buffers = 1 };
    ns_shader_desc fsd = { .name = "gbuffer.frag", .num_samplers = 3, .num_uniform_buffers = 1 };

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

ns_renderer *ns_renderer_create(ns_rhi *r, const ns_render_settings *settings)
{
    ns_renderer *rd = (ns_renderer *)ns_calloc(1, sizeof *rd);
    if (!rd) return NULL;

    if (settings) rd->settings = *settings;
    else ns_render_settings_defaults(&rd->settings, NS_QUALITY_HIGH);

    const SDL_GPUTextureFormat hdr_fmt = FMT_HDR;
    const SDL_GPUTextureFormat vis_fmt = FMT_VIS;
    rd->tonemap_format = ns_rhi_swapchain_format(r);

    rd->pipe_gbuffer = make_gbuffer_pipeline(r);
    rd->pipe_ssao    = make_fullscreen_pipeline(r, "ssao.frag", 2, 0, &vis_fmt, 1);
    rd->pipe_lighting = make_fullscreen_pipeline(r, "lighting.frag", 7, 1, &hdr_fmt, 1);
    rd->pipe_bloom_threshold = make_fullscreen_pipeline(r, "bloom_threshold.frag", 1, 0, &hdr_fmt, 1);
    rd->pipe_bloom_blur = make_fullscreen_pipeline(r, "bloom_blur.frag", 1, 0, &hdr_fmt, 1);
    rd->pipe_tonemap = make_fullscreen_pipeline(r, "tonemap.frag", 2, 0, &rd->tonemap_format, 1);
    rd->pipe_debug   = make_fullscreen_pipeline(r, "debug_view.frag", 1, 0, &rd->tonemap_format, 1);

    /* Couche de lancer de rayons. Son absence n'est pas fatale : le rendu
     * retombe sur l'espace écran, ce qui reste jouable. */
    {
        ns_compute_desc cd;
        SDL_zero(cd);
        cd.name = "raytrace.comp";
        cd.num_samplers = 4;                      /* profondeur, normale, albédo, historique */
        cd.num_readonly_storage_buffers = 4;      /* nœuds, triangles, matériaux, lumières */
        cd.num_readwrite_storage_textures = 2;    /* visibilité, réflexions */
        cd.num_uniform_buffers = 1;
        cd.threads_x = 8; cd.threads_y = 8; cd.threads_z = 1;
        rd->pipe_raytrace = ns_compute_pipeline_create(r, &cd);
        if (!rd->pipe_raytrace) {
            NS_WARN("pipeline de lancer de rayons indisponible : repli sur l'espace écran");
        }
    }
    rd->pipe_denoise = make_fullscreen_pipeline(r, "rt_denoise.frag", 3, 0, &hdr_fmt, 1);

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

    SDL_memcpy(rd->prev_view_proj, ns_m4_identity().m, sizeof rd->prev_view_proj);
    NS_INFO("rendu prêt (qualité %d, ray tracing %d)", (int)rd->settings.quality,
            (int)rd->settings.raytracing);
    return rd;
}

void ns_renderer_destroy(ns_rhi *r, ns_renderer *rd)
{
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
    ns_buffer_destroy(r, &rd->lights);
    destroy_targets(r, rd);
    ns_free(rd);
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
            tex[1].texture = m ? texture_or(scene, m->normal_texture, scene->fallback_normal) : scene->fallback_normal.handle;
            tex[2].texture = m ? texture_or(scene, m->orm_texture, scene->fallback_orm) : scene->fallback_orm.handle;
            for (int k = 0; k < 3; ++k) tex[k].sampler = aniso;
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

    SDL_GPUStorageTextureReadWriteBinding outputs[2];
    SDL_zeroa(outputs);
    outputs[0].texture = rd->rt_visibility[write].handle;
    outputs[1].texture = rd->reflections.handle;
    /* `cycle` demande au pilote une ressource fraîche si l'ancienne est encore
     * lue par le GPU : sans cela, on attendrait la fin de l'image précédente. */
    outputs[0].cycle = true;
    outputs[1].cycle = true;

    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, outputs, 2, NULL, 0);
    SDL_BindGPUComputePipeline(pass, rd->pipe_raytrace);

    SDL_GPUSampler *nearest = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
    SDL_GPUSampler *linear  = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
    SDL_GPUTextureSamplerBinding tex[4];
    SDL_zeroa(tex);
    tex[0].texture = rd->depth.handle;                    tex[0].sampler = nearest;
    tex[1].texture = rd->gbuffer_normal.handle;           tex[1].sampler = nearest;
    tex[2].texture = rd->gbuffer_albedo.handle;           tex[2].sampler = nearest;
    tex[3].texture = rd->rt_visibility[read].handle;      tex[3].sampler = linear;
    SDL_BindGPUComputeSamplers(pass, 0, tex, 4);

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
    u.accum[1] = (float)rd->width;
    u.accum[2] = (float)rd->height;

    SDL_PushGPUComputeUniformData(cmd, 0, &u, sizeof u);

    /* Groupes de 8x8, arrondis au supérieur ; le shader borne lui-même. */
    SDL_DispatchGPUCompute(pass, (rd->width + 7) / 8, (rd->height + 7) / 8, 1);
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

void ns_renderer_draw(ns_rhi *r, ns_renderer *rd, const ns_scene *scene,
                      const ns_camera *camera, SDL_GPUTexture *target,
                      uint32_t target_width, uint32_t target_height,
                      double time_seconds)
{
    NS_ASSERT(rd && scene && camera && target);

    if (!ns_renderer_resize(r, rd, target_width, target_height)) return;

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

        SDL_GPUSampler *clamp = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
        SDL_GPUSampler *nearest = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
        SDL_GPUTexture *tex[7] = {
            rd->gbuffer_albedo.handle, rd->gbuffer_normal.handle, rd->gbuffer_emissive.handle,
            rd->depth.handle, rd->visibility.handle,
            (rd->rt_denoised ? rd->rt_denoised->handle : rd->rt_visibility[rd->rt_current].handle),
            rd->reflections.handle
        };
        SDL_GPUSampler *smp[7] = { nearest, nearest, nearest, nearest, clamp, clamp, clamp };

        fullscreen_pass(r, rd->pipe_lighting, rd->hdr.handle, SDL_GPU_LOADOP_CLEAR,
                        tex, smp, 7, &u, sizeof u, rd->lights.handle);
    }

    /* --- 4. Halo --- */
    {
        SDL_GPUSampler *clamp = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);

        bloom_threshold_ubo tu;
        SDL_zero(tu);
        tu.settings[0] = rd->settings.bloom_threshold;
        tu.settings[1] = 0.55f;                   /* douceur du coude */
        tu.settings[2] = 1.0f;
        SDL_GPUTexture *src = rd->hdr.handle;
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
        case NS_DEBUG_HDR:        src = rd->hdr.handle;              break;
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
            return;
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

        SDL_GPUSampler *clamp = ns_rhi_sampler(r, NS_SAMPLER_LINEAR_CLAMP);
        /* Le niveau de halo le plus flou porte l'essentiel du rayonnement ; les
         * niveaux intermédiaires y ont déjà été fondus par la descente. */
        SDL_GPUTexture *tex[2] = { rd->hdr.handle, rd->bloom[BLOOM_MIPS - 1].handle };
        SDL_GPUSampler *smp[2] = { clamp, clamp };
        fullscreen_pass(r, rd->pipe_tonemap, target, SDL_GPU_LOADOP_CLEAR,
                        tex, smp, 2, &u, sizeof u, NULL);
    }

    SDL_memcpy(rd->prev_view_proj, view_proj.m, sizeof rd->prev_view_proj);
}
