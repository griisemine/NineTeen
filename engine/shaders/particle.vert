#version 450
/*
 * particle.vert — la poussière en suspension.
 *
 * Les quads arrivent DÉJÀ orientés vers la caméra : le CPU les construit en
 * espace monde. C'est un choix, et il se justifie par le nombre — quelques
 * milliers de particules, pas quelques centaines de milliers. À cette échelle,
 * quatre sommets écrits par le CPU coûtent moins qu'un tampon d'instances, une
 * disposition d'attributs par instance et le raisonnement qui va avec.
 */

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(set = 1, binding = 0) uniform Frame {
    mat4 u_viewProj;
};

void main()
{
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_viewProj * vec4(a_position, 1.0);
}
