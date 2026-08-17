/*
 * ns_rhi_shader.c — chargement des shaders embarqués et création des pipelines.
 *
 * Les blobs SPIR-V sont générés par cmake/EmbedShaders.cmake et liés dans le
 * binaire. On les retrouve ici par nom, ce qui donne un message d'erreur clair
 * quand un shader manque — plutôt qu'un écran noir sans explication, qui est le
 * mode d'échec habituel quand on se trompe de nombre de ressources déclarées.
 */
#include "ns_rhi.h"

#include "shader_blobs.h"

#include <string.h>

static const ns_shader_blob *find_blob(const char *name)
{
    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        if (SDL_strcmp(ns_shader_registry[i].name, name) == 0) {
            return &ns_shader_registry[i];
        }
    }
    return NULL;
}

/* Liste les shaders disponibles : sans ça, une faute de frappe dans un nom
 * coûte une demi-heure de recherche. */
static void log_available_shaders(void)
{
    NS_ERROR("shaders embarqués disponibles (%zu) :", ns_shader_registry_count);
    for (size_t i = 0; i < ns_shader_registry_count; ++i) {
        NS_ERROR("    %s", ns_shader_registry[i].name);
    }
}

SDL_GPUShader *ns_shader_load(ns_rhi *r, const ns_shader_desc *desc, SDL_GPUShaderStage stage)
{
    NS_ASSERT(desc && desc->name);

    const ns_shader_blob *blob = find_blob(desc->name);
    if (!blob) {
        NS_ERROR("shader « %s » absent du binaire", desc->name);
        log_available_shaders();
        return NULL;
    }

    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.code       = blob->bytes;
    info.code_size  = *blob->len;
    info.entrypoint = "main";
    info.format     = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage      = stage;
    info.num_samplers         = desc->num_samplers;
    info.num_storage_textures = desc->num_storage_textures;
    info.num_storage_buffers  = desc->num_storage_buffers;
    info.num_uniform_buffers  = desc->num_uniform_buffers;

    /* Sur Metal et D3D12, SDL ne consomme pas de SPIR-V : la CI produit alors
     * du MSL/DXIL via SDL_shadercross et le registre contient ces variantes.
     * Si le format n'est pas supporté, autant le dire tout de suite. */
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(ns_rhi_device(r));
    if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) == 0) {
        NS_ERROR("le backend %s n'accepte pas le SPIR-V — recompiler les shaders "
                 "avec SDL_shadercross (option NINETEEN_SHADERCROSS)", ns_rhi_backend_name(r));
        return NULL;
    }

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

    const ns_shader_blob *blob = find_blob(desc->name);
    if (!blob) {
        NS_ERROR("shader compute « %s » absent du binaire", desc->name);
        log_available_shaders();
        return NULL;
    }

    SDL_GPUComputePipelineCreateInfo info;
    SDL_zero(info);
    info.code       = blob->bytes;
    info.code_size  = *blob->len;
    info.entrypoint = "main";
    info.format     = SDL_GPU_SHADERFORMAT_SPIRV;
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
