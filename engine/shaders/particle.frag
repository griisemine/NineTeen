#version 450
/*
 * particle.frag — un grain de poussière.
 *
 * Aucune texture : un disque à bord fondu se calcule, et une texture de 32 x 32
 * pixels pour un dégradé radial serait un fichier de plus à charger, à monter et
 * à faire échouer. Le carré du rayon évite la racine.
 */

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 o_color;

void main()
{
    const vec2 d = v_uv * 2.0 - 1.0;
    const float r2 = dot(d, d);
    if (r2 > 1.0) discard;

    /*
     * Bord fondu au carré : un grain a un cœur lumineux et pas de contour. Un
     * `smoothstep` linéaire donnerait un disque net, qui se lit comme un confetti
     * — c'est précisément ce qu'on ne veut pas voir dans un faisceau.
     */
    const float falloff = (1.0 - r2) * (1.0 - r2);
    o_color = vec4(v_color.rgb, v_color.a * falloff);
    if (o_color.a <= 0.002) discard;
}
