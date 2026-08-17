#version 450
/*
 * debug_view.frag — affiche une cible intermédiaire de façon lisible.
 *
 * Chaque cible a besoin d'un traitement différent pour être interprétable à
 * l'œil : une profondeur en reverse-Z est presque toute noire si on l'affiche
 * telle quelle, une normale encodée en octaédrique ne veut rien dire sans
 * décodage, et le HDR dépasse 1. Sans cette passe, diagnostiquer un écran noir
 * revient à deviner.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_source;

layout(set = 3, binding = 0) uniform Params {
    ivec4 u_mode;      /* x : type de cible */
    vec4  u_scale;     /* x : facteur d'échelle appliqué avant affichage */
};

/* Doit rester identique au décodage de lighting.frag. */
vec3 decodeOctahedral(vec2 e)
{
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

void main()
{
    vec4 s = texture(u_source, v_uv);
    vec3 c;

    switch (u_mode.x) {
    case 1:  /* albédo + occlusion dans l'alpha */
        c = s.rgb;
        break;

    case 2:  /* normale (xy octaédrique) + rugosité + métal */
        c = decodeOctahedral(s.xy) * 0.5 + 0.5;
        break;

    case 3:  /* émissif : peut dépasser 1, on compresse */
        c = s.rgb / (1.0 + s.rgb);
        break;

    case 4:  /* profondeur reverse-Z : 1 tout près, 0 au loin.
              * Une courbe puissance rend les écarts lointains visibles. */
        c = vec3(pow(s.r, 0.25));
        break;

    case 5:  /* visibilité : r ombres, g occlusion, b indirect */
        c = s.rgb;
        break;

    case 6:  /* HDR linéaire */
    case 7:  /* halo */
        c = s.rgb / (1.0 + s.rgb);
        break;

    default:
        c = s.rgb;
        break;
    }

    o_color = vec4(c * u_scale.x, 1.0);
}
