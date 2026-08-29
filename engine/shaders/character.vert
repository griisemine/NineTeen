#version 450
/*
 * character.vert — le personnage, déformé par son squelette.
 *
 * C'est le seul shader du moteur qui fasse du « linear blend skinning », et il
 * est court : la position d'un sommet est la moyenne, pondérée par ses poids,
 * de ce que chacun de ses quatre os en ferait.
 *
 * Ce qui compte, et qu'on ne voit pas en le lisant vite
 * -----------------------------------------------------
 * La NORMALE se transforme par la même moyenne, PAS par une inverse
 * transposée. C'est correct ici, et seulement ici, parce que les matrices d'os
 * de ce personnage sont des rotations et des translations — sans échelle. Sur
 * un squelette qui porterait une échelle non uniforme il faudrait la matrice
 * cofacteur, et l'éclairage des membres mis à l'échelle serait faux sans que
 * rien ne le dise. `ns_skin` ne rejette pas ce cas : c'est une limite connue,
 * écrite ici pour qu'on la retrouve le jour où un personnage à échelle
 * apparaît.
 *
 * La matrice de modèle est appliquée APRÈS le skinning. L'ordre n'est pas
 * indifférent : le squelette travaille dans le repère du personnage, et c'est
 * le modèle qui le pose, l'oriente et le met à la taille du jeu.
 */

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec2  a_uv;
layout(location = 3) in vec4  a_joints;    /* quatre indices d'os, en flottants */
layout(location = 4) in vec4  a_weights;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

/*
 * TRENTE-DEUX os au plus, et la borne est celle du budget d'uniforme, pas une
 * limite du format : trente-deux matrices font 2 Kio, ce que la contrainte de
 * push d'uniformes accepte partout. Un humanoïde sans doigts en demande une
 * vingtaine ; celui du jeu en a dix-neuf.
 */
const int MAX_JOINTS = 32;

layout(set = 1, binding = 0) uniform Character {
    mat4 u_viewProj;
    mat4 u_model;
    mat4 u_joint[MAX_JOINTS];
};

void main()
{
    const ivec4 j = ivec4(a_joints);
    /*
     * La somme des poids VAUT UN — `ns_skin` renormalise au chargement, et
     * `test_skin` le vérifie. On ne redivise donc pas ici : ce serait quatre
     * opérations par sommet pour corriger une faute qui ne peut plus arriver,
     * et surtout ça masquerait le jour où elle arriverait.
     */
    mat4 skin = a_weights.x * u_joint[j.x]
              + a_weights.y * u_joint[j.y]
              + a_weights.z * u_joint[j.z]
              + a_weights.w * u_joint[j.w];

    vec4 local = skin * vec4(a_position, 1.0);
    vec4 world = u_model * local;

    v_world  = world.xyz;
    v_normal = mat3(u_model) * (mat3(skin) * a_normal);
    v_uv     = a_uv;

    gl_Position = u_viewProj * world;
}
