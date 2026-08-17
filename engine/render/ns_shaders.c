/*
 * ns_shaders.c — la table, et rien d'autre.
 *
 * Chaque ligne se lit directement dans le GLSL correspondant : compter les
 * `layout(set = N, binding = B)` du fichier donne les colonnes. Les sets suivent
 * la convention SPIR-V de SDL3 : sommet 0/1, fragment 2/3, compute 0/1/2.
 */
#include "ns_shaders.h"

#include <string.h>

const ns_shader_info ns_shader_table[] = {
    /* ------------------------------------------------------------- sommets */
    { "fullscreen.vert", NS_SHADER_STAGE_VERTEX,   0, 0, 0, 0, 0, 0,  0, 0, 0 },
    { "gbuffer.vert",    NS_SHADER_STAGE_VERTEX,   0, 0, 0, 0, 0, 1,  0, 0, 0 },

    /* ------------------------------------------------------------ fragments */
    /* Aucun pipeline ne l'emploie aujourd'hui ; la table décrit le shader. */
    { "blit.frag",             NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 0,  0, 0, 0 },
    { "bloom_blur.frag",       NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "bloom_threshold.frag",  NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "debug_view.frag",       NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "gbuffer.frag",          NS_SHADER_STAGE_FRAGMENT, 3, 0, 0, 0, 0, 1,  0, 0, 0 },
    /* 7 textures du G-buffer + le tampon des lumières, dans le même set. */
    { "lighting.frag",         NS_SHADER_STAGE_FRAGMENT, 7, 0, 1, 0, 0, 1,  0, 0, 0 },
    { "rt_denoise.frag",       NS_SHADER_STAGE_FRAGMENT, 3, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "ssao.frag",             NS_SHADER_STAGE_FRAGMENT, 2, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "test_gradient.frag",    NS_SHADER_STAGE_FRAGMENT, 0, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "tonemap.frag",          NS_SHADER_STAGE_FRAGMENT, 2, 0, 0, 0, 0, 1,  0, 0, 0 },

    /* ------------------------------------------------------------- compute */
    /* set 0 : 3 textures (profondeur, normale, historique) puis 4 tampons
     * (nœuds, triangles, matériaux, lumières) ; set 1 : 2 images de sortie
     * (visibilité, réflexions) ; set 2 : les paramètres d'image.
     *
     * Trois textures et non quatre : l'albédo du G-buffer était lié sans être
     * lu. Voir le commentaire de `raytrace.comp`. */
    { "raytrace.comp",         NS_SHADER_STAGE_COMPUTE,  3, 0, 4, 2, 0, 1,  8, 8, 1 },
};

const size_t ns_shader_table_count = sizeof ns_shader_table / sizeof ns_shader_table[0];

const ns_shader_info *ns_shader_info_find(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < ns_shader_table_count; ++i) {
        if (strcmp(ns_shader_table[i].name, name) == 0) return &ns_shader_table[i];
    }
    return NULL;
}

bool ns_shader_desc_fill(const char *name, ns_shader_desc *out)
{
    const ns_shader_info *i = ns_shader_info_find(name);
    if (!i) {
        NS_ERROR("shader « %s » absent de la table (engine/render/ns_shaders.c)", name ? name : "(null)");
        return false;
    }
    if (i->stage == NS_SHADER_STAGE_COMPUTE) {
        NS_ERROR("shader « %s » est un compute : employer ns_compute_desc_fill", name);
        return false;
    }
    SDL_zerop(out);
    out->name                 = i->name;
    out->num_samplers         = i->num_samplers;
    out->num_storage_textures = i->num_storage_textures;
    out->num_storage_buffers  = i->num_storage_buffers;
    out->num_uniform_buffers  = i->num_uniform_buffers;
    return true;
}

bool ns_compute_desc_fill(const char *name, ns_compute_desc *out)
{
    const ns_shader_info *i = ns_shader_info_find(name);
    if (!i) {
        NS_ERROR("shader compute « %s » absent de la table (engine/render/ns_shaders.c)",
                 name ? name : "(null)");
        return false;
    }
    if (i->stage != NS_SHADER_STAGE_COMPUTE) {
        NS_ERROR("shader « %s » n'est pas un compute", name);
        return false;
    }
    SDL_zerop(out);
    out->name                           = i->name;
    out->num_samplers                   = i->num_samplers;
    out->num_readonly_storage_textures  = i->num_storage_textures;
    out->num_readonly_storage_buffers   = i->num_storage_buffers;
    out->num_readwrite_storage_textures = i->num_rw_storage_textures;
    out->num_readwrite_storage_buffers  = i->num_rw_storage_buffers;
    out->num_uniform_buffers            = i->num_uniform_buffers;
    out->threads_x = i->threads_x ? i->threads_x : 1;
    out->threads_y = i->threads_y ? i->threads_y : 1;
    out->threads_z = i->threads_z ? i->threads_z : 1;
    return true;
}
