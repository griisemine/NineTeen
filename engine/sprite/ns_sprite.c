/* ns_sprite.c — voir ns_sprite.h pour le raisonnement. */
#include "ns_sprite.h"

#include <math.h>

#include "ns_core.h"
#include "ns_shaders.h"

#include <string.h>

#define NS_SPRITE_MAX_QUADS   4096
#define NS_SPRITE_MAX_BATCHES 128

typedef struct sprite_vertex {
    float position[2];
    float uv[2];
    float color[4];
} sprite_vertex;

typedef struct sprite_batch {
    uint32_t        first_index;
    uint32_t        index_count;
    SDL_GPUTexture *texture;
} sprite_batch;

struct ns_sprite {
    SDL_GPUGraphicsPipeline *pipeline;

    ns_buffer vertices;
    ns_buffer indices;

    sprite_vertex *cpu_verts;
    uint16_t      *cpu_indices;
    uint32_t       quad_count;

    sprite_batch batches[NS_SPRITE_MAX_BATCHES];
    uint32_t     batch_count;

    SDL_GPUTexture *current_texture;
    float           screen[2];

    ns_texture white;
    ns_texture font;          /* atlas de la fonte, R8 */
    bool       font_ready;
};

/* ==========================================================================
 * La fonte 5 x 7
 * ==========================================================================
 * Un octet par colonne, cinq colonnes par glyphe, bit 0 en haut. Elle couvre
 * l'ASCII imprimable de l'espace (32) au tilde (126).
 *
 * Elle est intégrée au binaire plutôt que chargée : quinze cents octets valent
 * mieux qu'un fichier de plus à retrouver, à monter et à faire échouer. Et une
 * fonte bitmap dessinée pour 7 pixels reste nette là où une fonte vectorielle
 * rendue à cette taille donne une bouillie grise — sur l'écran d'une borne,
 * c'est la différence entre un score lisible et une tache.
 */
static const uint8_t g_font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*   */ {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */ {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */ {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */ {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */ {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */ {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */ {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */ {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */ {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */ {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */ {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */ {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */ {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */ {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */ {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */ {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */ {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */ {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */ {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */ {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */ {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */ {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */ {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */ {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */ {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */ {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */ {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */ {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */ {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */ {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */ {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */ {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */ {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */ {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */ {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */ {0x0C,0x52,0x52,0x52,0x3E}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */ {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */ {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */ {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */ {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */ {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */ {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */ {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */ {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */ {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */ {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */ {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x08,0x2A,0x1C,0x08}, /* ~ */
};

#define FONT_W 5
#define FONT_H 7
#define FONT_GLYPHS 95
#define FONT_ADVANCE 6.0f      /* 5 colonnes + 1 d'espacement */

/* L'atlas est une bande d'un glyphe de haut : 95 x 5 pixels de large, 7 de haut.
 * Une bande plutôt qu'une grille parce qu'il n'y a rien à gagner à replier
 * quatre cent soixante-quinze pixels. */
static bool bake_font(ns_rhi *r, ns_texture *out)
{
    const uint32_t w = FONT_GLYPHS * FONT_W, h = FONT_H;
    uint8_t *pixels = (uint8_t *)SDL_calloc((size_t)w * h * 4, 1);
    if (!pixels) return false;

    for (int g = 0; g < FONT_GLYPHS; ++g) {
        for (int col = 0; col < FONT_W; ++col) {
            const uint8_t bits = g_font5x7[g][col];
            for (int row = 0; row < FONT_H; ++row) {
                const uint8_t on = (bits >> row) & 1u;
                const size_t i = ((size_t)row * w + (size_t)(g * FONT_W + col)) * 4;
                pixels[i + 0] = pixels[i + 1] = pixels[i + 2] = 255;
                pixels[i + 3] = on ? 255 : 0;
            }
        }
    }

    ns_texture_desc td;
    SDL_zero(td);
    td.width = w; td.height = h;
    td.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    td.sampled = true;
    td.name = "fonte 5x7";
    const bool ok = ns_texture_create(r, out, &td)
                 && ns_texture_upload(r, out, pixels, w * h * 4);
    SDL_free(pixels);
    return ok;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

ns_sprite *ns_sprite_create(ns_rhi *r, SDL_GPUTextureFormat target_format)
{
    ns_sprite *s = (ns_sprite *)SDL_calloc(1, sizeof *s);
    if (!s) return NULL;

    ns_shader_desc vsd, fsd;
    if (!ns_shader_desc_fill("sprite.vert", &vsd) || !ns_shader_desc_fill("sprite.frag", &fsd)) {
        SDL_free(s); return NULL;
    }
    SDL_GPUShader *vs = ns_shader_load(r, &vsd, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fs = ns_shader_load(r, &fsd, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
        if (fs) SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
        SDL_free(s);
        return NULL;
    }

    SDL_GPUVertexBufferDescription vb;
    SDL_zero(vb);
    vb.slot = 0;
    vb.pitch = sizeof(sprite_vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[3];
    SDL_zeroa(attrs);
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = offsetof(sprite_vertex, position);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[1].offset = offsetof(sprite_vertex, uv);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[2].offset = offsetof(sprite_vertex, color);

    /*
     * LE mélange alpha, premier du moteur. `src_alpha / 1 - src_alpha` est le
     * mélange « par-dessus » classique ; l'alpha de destination accumule pour que
     * dessiner dans une cible transparente (l'écran d'une borne) donne un alpha
     * juste, et non le seul alpha du dernier quad.
     */
    SDL_GPUColorTargetDescription target;
    SDL_zero(target);
    target.format = target_format;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;

    s->pipeline = SDL_CreateGPUGraphicsPipeline(ns_rhi_device(r), &info);
    SDL_ReleaseGPUShader(ns_rhi_device(r), vs);
    SDL_ReleaseGPUShader(ns_rhi_device(r), fs);
    if (!s->pipeline) {
        NS_ERROR("couche 2D : pipeline indisponible (%s)", SDL_GetError());
        SDL_free(s);
        return NULL;
    }

    s->cpu_verts = (sprite_vertex *)SDL_calloc(NS_SPRITE_MAX_QUADS * 4, sizeof(sprite_vertex));
    s->cpu_indices = (uint16_t *)SDL_calloc(NS_SPRITE_MAX_QUADS * 6, sizeof(uint16_t));
    if (!s->cpu_verts || !s->cpu_indices) { ns_sprite_destroy(r, s); return NULL; }

    /* Les indices ne changent JAMAIS : quatre sommets, six indices, toujours dans
     * le même ordre. On les remplit une fois et on les téléverse une fois. */
    for (uint32_t q = 0; q < NS_SPRITE_MAX_QUADS; ++q) {
        const uint16_t v = (uint16_t)(q * 4);
        uint16_t *i = &s->cpu_indices[q * 6];
        i[0] = v; i[1] = (uint16_t)(v + 1); i[2] = (uint16_t)(v + 2);
        i[3] = v; i[4] = (uint16_t)(v + 2); i[5] = (uint16_t)(v + 3);
    }

    if (!ns_buffer_create(r, &s->vertices, NS_BUFFER_VERTEX,
                          NS_SPRITE_MAX_QUADS * 4 * (uint32_t)sizeof(sprite_vertex), "sprites")
     || !ns_buffer_create(r, &s->indices, NS_BUFFER_INDEX,
                          NS_SPRITE_MAX_QUADS * 6 * (uint32_t)sizeof(uint16_t), "sprites idx")) {
        ns_sprite_destroy(r, s);
        return NULL;
    }
    ns_buffer_upload(r, &s->indices, s->cpu_indices,
                     NS_SPRITE_MAX_QUADS * 6 * (uint32_t)sizeof(uint16_t), 0);

    s->white = ns_texture_white(r);
    s->font_ready = bake_font(r, &s->font);
    if (!s->font_ready) NS_WARN("couche 2D : fonte indisponible, le texte ne sortira pas");

    NS_INFO("couche 2D prête : %u quads au maximum, mélange alpha actif", NS_SPRITE_MAX_QUADS);
    return s;
}

void ns_sprite_destroy(ns_rhi *r, ns_sprite *s)
{
    if (!s) return;
    if (s->pipeline) SDL_ReleaseGPUGraphicsPipeline(ns_rhi_device(r), s->pipeline);
    ns_buffer_destroy(r, &s->vertices);
    ns_buffer_destroy(r, &s->indices);
    if (s->font_ready) ns_texture_destroy(r, &s->font);
    /* `ns_texture_white` fabrique une texture NEUVE à chaque appel — ce n'est
     * pas un singleton du RHI, malgré le nom. Elle appartient donc au lot, et
     * elle n'était pas libérée. */
    ns_texture_destroy(r, &s->white);
    SDL_free(s->cpu_verts);
    SDL_free(s->cpu_indices);
    SDL_free(s);
}

/* ==========================================================================
 * Accumulation
 * ========================================================================== */

void ns_sprite_begin(ns_sprite *s, float width, float height)
{
    s->quad_count = 0;
    s->batch_count = 0;
    s->current_texture = NULL;
    s->screen[0] = (width > 1.0f) ? width : 1.0f;
    s->screen[1] = (height > 1.0f) ? height : 1.0f;
}

static void use_texture(ns_sprite *s, SDL_GPUTexture *tex)
{
    if (s->batch_count > 0 && s->current_texture == tex) return;
    if (s->batch_count >= NS_SPRITE_MAX_BATCHES) return;

    sprite_batch *b = &s->batches[s->batch_count++];
    b->first_index = s->quad_count * 6;
    b->index_count = 0;
    b->texture = tex;
    s->current_texture = tex;
}

void ns_sprite_texture(ns_sprite *s, const ns_texture *tex)
{
    use_texture(s, (tex && tex->handle) ? tex->handle : s->white.handle);
}

void ns_sprite_quad(ns_sprite *s, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, const float rgba[4])
{
    if (s->quad_count >= NS_SPRITE_MAX_QUADS) return;
    if (s->batch_count == 0) use_texture(s, s->white.handle);

    static const float opaque[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float *c = rgba ? rgba : opaque;

    sprite_vertex *v = &s->cpu_verts[s->quad_count * 4];
    const float px[4] = { x, x + w, x + w, x };
    const float py[4] = { y, y, y + h, y + h };
    const float pu[4] = { u0, u1, u1, u0 };
    const float pv[4] = { v0, v0, v1, v1 };
    for (int i = 0; i < 4; ++i) {
        v[i].position[0] = px[i]; v[i].position[1] = py[i];
        v[i].uv[0] = pu[i];       v[i].uv[1] = pv[i];
        memcpy(v[i].color, c, sizeof(float) * 4);
    }

    s->quad_count++;
    s->batches[s->batch_count - 1].index_count += 6;
}

void ns_sprite_quad_rot(ns_sprite *s, float cx, float cy, float w, float h, float angle,
                        float u0, float v0, float u1, float v1, const float rgba[4])
{
    if (s->quad_count >= NS_SPRITE_MAX_QUADS) return;
    if (s->batch_count == 0) use_texture(s, s->white.handle);

    static const float opaque[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float *c = rgba ? rgba : opaque;

    const float ca = cosf(angle), sa = sinf(angle);
    const float hw = w * 0.5f, hh = h * 0.5f;
    const float ox[4] = { -hw,  hw,  hw, -hw };
    const float oy[4] = { -hh, -hh,  hh,  hh };
    const float pu[4] = { u0, u1, u1, u0 };
    const float pv[4] = { v0, v0, v1, v1 };

    sprite_vertex *v = &s->cpu_verts[s->quad_count * 4];
    for (int i = 0; i < 4; ++i) {
        v[i].position[0] = cx + ox[i] * ca - oy[i] * sa;
        v[i].position[1] = cy + ox[i] * sa + oy[i] * ca;
        v[i].uv[0] = pu[i];
        v[i].uv[1] = pv[i];
        memcpy(v[i].color, c, sizeof(float) * 4);
    }

    s->quad_count++;
    s->batches[s->batch_count - 1].index_count += 6;
}

void ns_sprite_rect(ns_sprite *s, float x, float y, float w, float h, const float rgba[4])
{
    ns_sprite_texture(s, NULL);
    ns_sprite_quad(s, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, rgba);
}

void ns_sprite_rect_rot(ns_sprite *s, float cx, float cy, float w, float h, float angle,
                        const float rgba[4])
{
    ns_sprite_texture(s, NULL);
    ns_sprite_quad_rot(s, cx, cy, w, h, angle, 0.0f, 0.0f, 1.0f, 1.0f, rgba);
}

/* ==========================================================================
 * Texte
 * ========================================================================== */

float ns_sprite_text_width(const char *text, float scale)
{
    if (!text || !text[0]) return 0.0f;
    return (float)strlen(text) * FONT_ADVANCE * scale;
}

float ns_sprite_text_height(float scale) { return (float)FONT_H * scale; }

float ns_sprite_text(ns_sprite *s, float x, float y, float scale,
                     const float rgba[4], const char *text)
{
    if (!text || !s->font_ready) return 0.0f;
    ns_sprite_texture(s, &s->font);

    const float atlas_w = (float)(FONT_GLYPHS * FONT_W);
    float cursor = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const int g = (int)(*p) - 32;
        if (g >= 0 && g < FONT_GLYPHS && *p != ' ') {
            /*
             * Un demi-texel de marge de chaque côté. Sans elle, l'échantillonnage
             * linéaire va chercher la colonne du glyphe voisin et le « 1 » traîne
             * un bout de « 0 » — le genre d'artefact qu'on met une heure à
             * attribuer à la fonte plutôt qu'au filtrage.
             */
            const float u0 = ((float)(g * FONT_W) + 0.5f) / atlas_w;
            const float u1 = ((float)(g * FONT_W + FONT_W) - 0.5f) / atlas_w;
            ns_sprite_quad(s, cursor, y, (float)FONT_W * scale, (float)FONT_H * scale,
                           u0, 0.0f, u1, 1.0f, rgba);
        }
        cursor += FONT_ADVANCE * scale;
    }
    return cursor - x;
}

/* ==========================================================================
 * Dessin
 * ========================================================================== */

uint32_t ns_sprite_quad_count(const ns_sprite *s)  { return s->quad_count; }
uint32_t ns_sprite_batch_count(const ns_sprite *s) { return s->batch_count; }

void ns_sprite_end(ns_rhi *r, ns_sprite *s, SDL_GPUTexture *target,
                   uint32_t target_width, uint32_t target_height,
                   const float clear_rgba[4])
{
    SDL_GPUCommandBuffer *cmd = ns_rhi_cmd(r);
    if (!cmd || !target) return;

    if (s->quad_count > 0) {
        ns_rhi_stage_buffer(r, &s->vertices, s->cpu_verts,
                            s->quad_count * 4 * (uint32_t)sizeof(sprite_vertex), 0);
        /* La copie doit être exécutée avant la passe qui lit le tampon. */
        ns_rhi_flush_staging(r);
    }

    SDL_GPUColorTargetInfo ci;
    SDL_zero(ci);
    ci.texture = target;
    ci.store_op = SDL_GPU_STOREOP_STORE;
    if (clear_rgba) {
        ci.load_op = SDL_GPU_LOADOP_CLEAR;
        ci.clear_color.r = clear_rgba[0]; ci.clear_color.g = clear_rgba[1];
        ci.clear_color.b = clear_rgba[2]; ci.clear_color.a = clear_rgba[3];
    } else {
        ci.load_op = SDL_GPU_LOADOP_LOAD;
    }

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ci, 1, NULL);
    if (!pass) return;

    if (s->quad_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, s->pipeline);

        SDL_GPUViewport vp = { 0.0f, 0.0f, (float)target_width, (float)target_height,
                               0.0f, 1.0f };
        SDL_SetGPUViewport(pass, &vp);

        SDL_GPUBufferBinding vbind = { s->vertices.handle, 0 };
        SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
        SDL_GPUBufferBinding ibind = { s->indices.handle, 0 };
        SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        float screen[4] = { s->screen[0], s->screen[1], 0.0f, 0.0f };
        SDL_PushGPUVertexUniformData(cmd, 0, screen, sizeof screen);

        for (uint32_t b = 0; b < s->batch_count; ++b) {
            const sprite_batch *batch = &s->batches[b];
            if (batch->index_count == 0) continue;

            SDL_GPUTextureSamplerBinding tb;
            SDL_zero(tb);
            tb.texture = batch->texture;
            /* Filtrage au plus proche : une fonte bitmap et des sprites de jeu
             * d'arcade doivent rester francs. Le linéaire les rendrait mous, ce
             * qui est exactement l'inverse de ce qu'on cherche. */
            tb.sampler = ns_rhi_sampler(r, NS_SAMPLER_NEAREST_CLAMP);
            SDL_BindGPUFragmentSamplers(pass, 0, &tb, 1);

            SDL_DrawGPUIndexedPrimitives(pass, batch->index_count, 1, batch->first_index, 0, 0);
        }
    }

    SDL_EndGPURenderPass(pass);
}
