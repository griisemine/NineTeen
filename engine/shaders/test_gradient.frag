#version 450
/*
 * test_gradient.frag — motif de validation du pipeline graphique.
 *
 * Produit un dégradé rouge sur X, vert sur Y, et un bleu piloté par un uniforme.
 * Chacun des trois canaux vérifie une chose distincte : l'interpolation des
 * varyings, l'orientation de l'image (une image retournée serait détectée), et
 * le passage des constantes au fragment shader.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 3, binding = 0) uniform Params {
    vec4 u_tint;    /* xyz : teinte, w : inutilisé */
};

void main()
{
    o_color = vec4(v_uv.x * u_tint.r,
                   v_uv.y * u_tint.g,
                   u_tint.b,
                   1.0);
}
