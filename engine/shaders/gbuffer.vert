#version 450
/*
 * gbuffer.vert — passe géométrique de la salle.
 *
 * La scène est statique et déjà transformée en espace monde au chargement :
 * il n'y a donc pas de matrice de modèle ici, seulement la vue-projection.
 * C'est ce qui permet de dessiner les 127 objets sans changer d'uniforme entre
 * eux, là où la V1 empilait un glPushMatrix par objet.
 */

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_tangent;   /* xyz : tangente, w : chiralité */

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent;

/* set = 1 : uniformes du vertex shader (convention SDL3 GPU pour le SPIR-V) */
layout(set = 1, binding = 0) uniform Camera {
    mat4 u_viewProj;
    mat4 u_prevViewProj;   /* image précédente : réutilisée par l'accumulation temporelle */
    vec4 u_cameraPos;      /* xyz : position, w : temps en secondes */
};

void main()
{
    v_world   = a_position;
    v_normal  = a_normal;
    v_uv      = a_uv;
    v_tangent = a_tangent;

    gl_Position = u_viewProj * vec4(a_position, 1.0);
}
