#version 450
/*
 * viewmodel.vert — les bras du joueur.
 *
 * Contrairement à `gbuffer.vert`, qui n'a AUCUNE matrice de modèle parce que la
 * salle est cuite en espace monde au chargement, chaque segment de bras porte la
 * sienne : c'est précisément ce que la passe géométrique ne sait pas faire, et la
 * raison pour laquelle le viewmodel a son propre pipeline.
 *
 * La vue-projection n'est pas celle de la scène : même matrice de vue, mais un
 * champ de vision plus étroit. C'est la pratique courante, et c'est ce qui évite
 * qu'une main s'étire en approchant du bord du cadre.
 */

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_tangent;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

layout(set = 1, binding = 0) uniform Segment {
    mat4 u_viewProj;      /* vue de la scène, projection étroite */
    mat4 u_model;         /* segment -> monde */
    vec4 u_params;        /* x : longueur du segment, yzw : libres */
};

void main()
{
    /* Le maillage est modélisé sur une longueur de 1 le long de −Z : la pose
     * porte la vraie longueur. Mettre l'échelle ici plutôt que dans la matrice
     * évite d'avoir à corriger les normales d'une échelle non uniforme. */
    vec3 local = vec3(a_position.xy, a_position.z * u_params.x);

    vec4 world = u_model * vec4(local, 1.0);
    v_world = world.xyz;
    /* La matrice de pose n'a qu'une rotation et une translation — pas d'échelle,
     * justement pour que la normale se transforme sans matrice inverse. */
    v_normal = mat3(u_model) * a_normal;
    v_uv = a_uv;

    gl_Position = u_viewProj * world;
}
