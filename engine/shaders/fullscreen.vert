#version 450
/*
 * fullscreen.vert — triangle plein écran sans tampon de sommets.
 *
 * Astuce classique : trois sommets générés depuis gl_VertexIndex couvrent tout
 * l'écran. Aucun vertex buffer à lier, et pas d'arête diagonale au milieu de
 * l'image comme avec deux triangles — le quad est un seul triangle débordant.
 * Toutes les passes de post-traitement l'utilisent.
 *
 * Convention d'axe Y : l'API GPU de SDL3 uniformise les backends sur l'origine
 * en HAUT à gauche du framebuffer, avec +Y vers le haut en coordonnées
 * normalisées. On émet donc des UV où (0,0) est le coin supérieur gauche, ce qui
 * correspond à la convention des textures et des images — sans quoi tout le
 * post-traitement travaille à l'envers. Le test de rendu vérifie précisément ce
 * point : il avait attrapé l'inversion.
 */

layout(location = 0) out vec2 v_uv;

void main()
{
    /* index 0 -> uv (0,0), 1 -> (2,0), 2 -> (0,2) */
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    /* x : [0,2] -> [-1,3]   y : [0,2] -> [1,-3]  (uv.y = 0 en haut de l'écran) */
    gl_Position = vec4(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0, 0.0, 1.0);
}
