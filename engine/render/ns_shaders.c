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
    /* Une matrice de modèle PAR SEGMENT : c'est ce que le G-buffer ne sait pas
     * faire, et la raison d'être du pipeline du viewmodel. */
    { "sprite.vert",           NS_SHADER_STAGE_VERTEX,   0, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "viewmodel.vert",  NS_SHADER_STAGE_VERTEX,   0, 0, 0, 0, 0, 1,  0, 0, 0 },

    /* ------------------------------------------------------------ fragments */
    /* Aucun pipeline ne l'emploie aujourd'hui ; la table décrit le shader. */
    { "blit.frag",             NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 0,  0, 0, 0 },
    { "bloom_blur.frag",       NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "bloom_threshold.frag",  NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "debug_view.frag",       NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "gbuffer.frag",          NS_SHADER_STAGE_FRAGMENT, 3, 0, 0, 0, 0, 1,  0, 0, 0 },
    /* 8 textures (G-buffer, SSAO, visibilité RT, réflexions, ombres par lumière)
     * puis le tampon des lumières, dans le même set. C'est aussi le plafond de
     * `fullscreen_pass`, qui ne sait en lier que huit. */
    { "lighting.frag",         NS_SHADER_STAGE_FRAGMENT, 8, 0, 1, 0, 0, 1,  0, 0, 0 },
    { "rt_denoise.frag",       NS_SHADER_STAGE_FRAGMENT, 3, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "ssao.frag",             NS_SHADER_STAGE_FRAGMENT, 2, 0, 0, 0, 0, 1,  0, 0, 0 },
    { "test_gradient.frag",    NS_SHADER_STAGE_FRAGMENT, 0, 0, 0, 0, 0, 1,  0, 0, 0 },
    /* Deux textures puis le tampon d'exposition, dans le même set. */
    { "tonemap.frag",          NS_SHADER_STAGE_FRAGMENT, 2, 0, 1, 0, 0, 1,  0, 0, 0 },
    /* HDR éclairé, brouillard demi-résolution, profondeur pleine résolution. */
    { "volumetric_composite.frag", NS_SHADER_STAGE_FRAGMENT, 3, 0, 0, 0, 0, 1,  0, 0, 0 },
    /* Aucune texture, mais le tampon des lumières — le même que l'éclairage. */
    { "viewmodel.frag",        NS_SHADER_STAGE_FRAGMENT, 0, 0, 1, 0, 0, 1,  0, 0, 0 },
    /* La couche 2D : un atlas, aucun tampon, aucun uniforme de fragment. La
     * couleur voyage dans le sommet, ce qui permet de teinter chaque quad sans
     * couper le lot. */
    { "sprite.frag",           NS_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0, 0, 0,  0, 0, 0 },

    /* ------------------------------------------------------------- compute */
    /* set 0 : 3 textures (profondeur, normale, historique) puis 4 tampons
     * (nœuds, triangles, matériaux, lumières) ; set 1 : 2 images de sortie
     * (visibilité, réflexions) ; set 2 : les paramètres d'image.
     *
     * Trois textures et non quatre : l'albédo du G-buffer était lié sans être
     * lu. Voir le commentaire de `raytrace.comp`.
     *
     * Trois images en écriture depuis A5 : la troisième porte les ombres des
     * quatre lumières dominantes de chaque pixel. */
    { "raytrace.comp",         NS_SHADER_STAGE_COMPUTE,  3, 0, 4, 3, 0, 1,  8, 8, 1 },

    /* set 0 : la profondeur, puis trois tampons (nœuds, triangles, lumières) —
     * pas les matériaux : le brouillard ne colore pas ce qu'il occulte ;
     * set 1 : la cible demi-résolution ; set 2 : les paramètres. */
    { "volumetric.comp",       NS_SHADER_STAGE_COMPUTE,  1, 0, 3, 1, 0, 1,  8, 8, 1 },

    /* Un unique groupe de travail : une texture en entrée, un tampon d'un seul
     * flottant en lecture-écriture — le premier du moteur. */
    { "exposure.comp",         NS_SHADER_STAGE_COMPUTE,  1, 0, 0, 0, 1, 1,  8, 8, 1 },
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
