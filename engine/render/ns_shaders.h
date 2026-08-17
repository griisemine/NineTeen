/*
 * ns_shaders.h — la table des shaders : nom, étage, ressources déclarées.
 *
 * Pourquoi une table plutôt que des littéraux au point d'appel
 * ------------------------------------------------------------
 * Les compteurs de ressources passés à SDL (`num_samplers`,
 * `num_storage_buffers`, `num_uniform_buffers`…) doivent correspondre **exactement**
 * à ce que le GLSL déclare. Un écart ne produit ni erreur de compilation ni
 * message : le pipeline est refusé, ou pire il se crée et lit des ressources
 * décalées. C'est le mode d'échec le plus coûteux du moteur, et il était
 * jusqu'ici encodé dans des arguments dispersés dans `ns_render.c`.
 *
 * Les regrouper ici sert deux fins :
 *   - un seul endroit à corriger quand un shader gagne une texture ;
 *   - une référence contre laquelle **tester** la traduction MSL. Sans table
 *     partagée, le test aurait sa propre copie des chiffres et ne prouverait
 *     rien d'autre que sa propre cohérence.
 *
 * La table décrit les shaders, pas les pipelines : `blit.frag` y figure avec ses
 * vraies ressources même si aucun pipeline ne l'utilise aujourd'hui.
 */
#ifndef NS_SHADERS_H
#define NS_SHADERS_H

#include "ns_rhi.h"

typedef enum ns_shader_stage_kind {
    NS_SHADER_STAGE_VERTEX,
    NS_SHADER_STAGE_FRAGMENT,
    NS_SHADER_STAGE_COMPUTE
} ns_shader_stage_kind;

typedef struct ns_shader_info {
    const char          *name;
    ns_shader_stage_kind stage;

    /* Étages graphiques. En compute, ces trois-là comptent les ressources en
     * LECTURE SEULE — c'est la répartition que SDL impose par les sets. */
    uint32_t num_samplers;
    uint32_t num_storage_textures;
    uint32_t num_storage_buffers;

    /* Compute uniquement : les ressources en lecture-écriture. */
    uint32_t num_rw_storage_textures;
    uint32_t num_rw_storage_buffers;

    uint32_t num_uniform_buffers;

    /* Compute uniquement. */
    uint32_t threads_x, threads_y, threads_z;
} ns_shader_info;

extern const ns_shader_info ns_shader_table[];
extern const size_t         ns_shader_table_count;

/* NULL si le nom est inconnu — l'appelant décide si c'est fatal. */
const ns_shader_info *ns_shader_info_find(const char *name);

/* Remplissent les descripteurs de l'RHI depuis la table. Renvoient false si le
 * nom est inconnu ou si l'étage ne correspond pas au descripteur demandé. */
bool ns_shader_desc_fill(const char *name, ns_shader_desc *out);
bool ns_compute_desc_fill(const char *name, ns_compute_desc *out);

#endif /* NS_SHADERS_H */
