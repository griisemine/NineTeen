#version 450
/*
 * bloom_blur.frag — flou gaussien séparable, une direction par passe.
 *
 * Neuf prises par passe, mais placées entre les texels pour que le filtrage
 * bilinéaire du matériel en lise deux à la fois : on obtient un noyau de
 * dix-sept texels pour le prix de neuf. C'est ce qui rend le halo des néons
 * assez large pour être crédible sans écrouler le temps d'image.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_source;

layout(set = 3, binding = 0) uniform Params {
    vec4 u_direction;   /* xy : pas en UV (horizontal ou vertical), zw : inutilisé */
};

void main()
{
    /* Poids gaussiens (sigma ≈ 3) et décalages ajustés pour les prises doubles. */
    const float offsets[5] = float[](0.0, 1.4117647, 3.2941176, 5.1764706, 7.0588235);
    const float weights[5] = float[](0.1963806, 0.2969070, 0.0944703, 0.0103813, 0.0003663);

    vec3 result = texture(u_source, v_uv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 delta = u_direction.xy * offsets[i];
        result += texture(u_source, v_uv + delta).rgb * weights[i];
        result += texture(u_source, v_uv - delta).rgb * weights[i];
    }
    o_color = vec4(result, 1.0);
}
