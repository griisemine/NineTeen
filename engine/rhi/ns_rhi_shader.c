/*
 * ns_rhi_shader.c — chargement des shaders embarqués et création des pipelines.
 *
 * Les blobs sont générés par cmake/EmbedShaders.cmake et liés dans le binaire.
 * On les retrouve ici par nom **et par format**, ce qui donne un message d'erreur
 * clair quand un shader manque — plutôt qu'un écran noir sans explication, qui est
 * le mode d'échec habituel quand on se trompe de nombre de ressources déclarées.
 *
 * Le format compte parce que les backends ne consomment pas la même chose :
 * Vulkan veut du SPIR-V, Metal du MSL. Le registre peut porter les deux variantes
 * d'un même shader ; on choisit celle que le périphérique accepte. Auparavant le
 * format était écrit en dur à SPIR-V, ce qui rendait tout le chemin macOS
 * inutilisable — et ne se voyait qu'au démarrage, sur un Mac.
 */
#include "ns_rhi.h"

#include "shader_blobs.h"

#include <string.h>

/* Nom lisible d'un format, pour les messages. Les valeurs sont celles de
 * SDL_GPU_SHADERFORMAT_*, portées telles quelles par le registre généré. */
static const char *format_name(SDL_GPUShaderFormat f)
{
    switch (f) {
        case SDL_GPU_SHADERFORMAT_SPIRV:    return "SPIR-V";
        case SDL_GPU_SHADERFORMAT_MSL:      return "MSL";
        case SDL_GPU_SHADERFORMAT_METALLIB: return "metallib";
        case SDL_GPU_SHADERFORMAT_DXIL:     return "DXIL";
        case SDL_GPU_SHADERFORMAT_DXBC:     return "DXBC";
        default:                            return "?";
    }
}

/* Le point d'entrée dépend du format : SPIRV-Cross renomme `main` en `main0` en
 * MSL, parce que `main` est réservé. Passer le mauvais nom produit un « Creating
 * MTLFunction failed » sans autre indication. */
static const char *entrypoint_for(SDL_GPUShaderFormat f)
{
    return (f == SDL_GPU_SHADERFORMAT_MSL || f == SDL_GPU_SHADERFORMAT_METALLIB)
         ? "main0" : "main";
}

/* Liste les shaders disponibles : sans ça, une faute de frappe dans un nom
 * coûte une demi-heure de recherche. */
static void log_available_shaders(void)
{
    NS_ERROR("shaders embarqués disponibles (%zu) :", ns_shader_registry_count);
    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        NS_ERROR("    %s [%s]", ns_shader_registry[i].name,
                 format_name((SDL_GPUShaderFormat)ns_shader_registry[i].format));
    }
}

/*
 * Choisit la variante du shader que le périphérique sait consommer.
 *
 * Les deux échecs possibles sont distincts et méritent des messages distincts :
 * un nom absent est une faute de frappe, un format absent est une erreur de
 * configuration du build — et c'est cette seconde qui a laissé le jeu injouable
 * sur macOS.
 */
static const ns_shader_blob *pick_blob(ns_rhi *r, const char *name)
{
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(ns_rhi_device(r));
    bool name_seen = false;

    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        const ns_shader_blob *b = &ns_shader_registry[i];
        if (SDL_strcmp(b->name, name) != 0) continue;
        name_seen = true;
        if (((SDL_GPUShaderFormat)b->format & supported) != 0) return b;
    }

    if (!name_seen) {
        NS_ERROR("shader « %s » absent du binaire", name);
        log_available_shaders();
        return NULL;
    }

    NS_ERROR("shader « %s » : aucune variante acceptée par le backend %s", name,
             ns_rhi_backend_name(r));
    NS_ERROR("  le binaire embarque :");
    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        if (SDL_strcmp(ns_shader_registry[i].name, name) == 0) {
            NS_ERROR("    %s", format_name((SDL_GPUShaderFormat)ns_shader_registry[i].format));
        }
    }
    NS_ERROR("  le backend accepte : %s%s%s%s",
             (supported & SDL_GPU_SHADERFORMAT_SPIRV) ? "SPIR-V " : "",
             (supported & SDL_GPU_SHADERFORMAT_MSL) ? "MSL " : "",
             (supported & SDL_GPU_SHADERFORMAT_METALLIB) ? "metallib " : "",
             (supported & SDL_GPU_SHADERFORMAT_DXIL) ? "DXIL " : "");
    NS_ERROR("  reconfigurer avec -DNINETEEN_SHADERCROSS=ON (MSL) ou "
             "-DNINETEEN_SHADER_SPIRV=ON (Vulkan)");
    return NULL;
}

SDL_GPUShader *ns_shader_load(ns_rhi *r, const ns_shader_desc *desc, SDL_GPUShaderStage stage)
{
    NS_ASSERT(desc && desc->name);

    const ns_shader_blob *blob = pick_blob(r, desc->name);
    if (!blob) return NULL;

    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.code       = blob->bytes;
    info.code_size  = *blob->len;
    info.format     = (SDL_GPUShaderFormat)blob->format;
    info.entrypoint = entrypoint_for(info.format);
    info.stage      = stage;
    info.num_samplers         = desc->num_samplers;
    info.num_storage_textures = desc->num_storage_textures;
    info.num_storage_buffers  = desc->num_storage_buffers;
    info.num_uniform_buffers  = desc->num_uniform_buffers;

    SDL_GPUShader *sh = SDL_CreateGPUShader(ns_rhi_device(r), &info);
    if (!sh) {
        NS_ERROR("shader « %s » refusé par le pilote : %s", desc->name, SDL_GetError());
        NS_ERROR("  vérifier que les compteurs déclarés (samplers %u, storage tex %u, "
                 "storage buf %u, uniformes %u) correspondent au GLSL",
                 desc->num_samplers, desc->num_storage_textures,
                 desc->num_storage_buffers, desc->num_uniform_buffers);
        return NULL;
    }
    NS_DEBUG("shader %s chargé (%u octets)", desc->name, *blob->len);
    return sh;
}

SDL_GPUComputePipeline *ns_compute_pipeline_create(ns_rhi *r, const ns_compute_desc *desc)
{
    NS_ASSERT(desc && desc->name);

    /* Ce chemin n'avait pas de contrôle de format du tout : sur un backend qui
     * refuse le SPIR-V, il remontait le message générique du pilote au lieu de
     * nommer la cause. */
    const ns_shader_blob *blob = pick_blob(r, desc->name);
    if (!blob) return NULL;

    SDL_GPUComputePipelineCreateInfo info;
    SDL_zero(info);
    info.code       = blob->bytes;
    info.code_size  = *blob->len;
    info.format     = (SDL_GPUShaderFormat)blob->format;
    info.entrypoint = entrypoint_for(info.format);
    info.num_samplers                     = desc->num_samplers;
    info.num_readonly_storage_textures    = desc->num_readonly_storage_textures;
    info.num_readonly_storage_buffers     = desc->num_readonly_storage_buffers;
    info.num_readwrite_storage_textures   = desc->num_readwrite_storage_textures;
    info.num_readwrite_storage_buffers    = desc->num_readwrite_storage_buffers;
    info.num_uniform_buffers              = desc->num_uniform_buffers;
    info.threadcount_x = desc->threads_x ? desc->threads_x : 1;
    info.threadcount_y = desc->threads_y ? desc->threads_y : 1;
    info.threadcount_z = desc->threads_z ? desc->threads_z : 1;

    SDL_GPUComputePipeline *p = SDL_CreateGPUComputePipeline(ns_rhi_device(r), &info);
    if (!p) {
        NS_ERROR("pipeline compute « %s » refusé : %s", desc->name, SDL_GetError());
        return NULL;
    }
    NS_DEBUG("pipeline compute %s (%ux%ux%u)", desc->name,
             info.threadcount_x, info.threadcount_y, info.threadcount_z);
    return p;
}
