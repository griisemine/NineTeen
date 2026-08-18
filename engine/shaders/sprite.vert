#version 450
/*
 * sprite.vert — la couche 2D.
 *
 * Les sommets arrivent en **pixels logiques**, pas en coordonnées d'écran
 * normalisées : c'est ce qui permet d'écrire « le score est à 12 pixels du bord
 * » sans savoir dans quelle résolution on rend. La conversion tient en deux
 * multiplications, et elle est ici plutôt que côté CPU pour que le même tampon
 * de sommets serve à dessiner dans l'écran d'une borne (256 x 320) et en plein
 * écran (1600 x 900).
 */

layout(location = 0) in vec2 a_position;   /* pixels logiques */
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(set = 1, binding = 0) uniform Screen {
    vec4 u_screen;   /* xy : taille logique, zw : libre */
};

void main()
{
    /* Origine en HAUT à gauche, comme toute API 2D — et comme les UV de glTF.
     * L'axe Y est donc retourné ici, une fois, plutôt que dans chaque appelant. */
    vec2 ndc = vec2( (a_position.x / u_screen.x) * 2.0 - 1.0,
                     1.0 - (a_position.y / u_screen.y) * 2.0 );
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
