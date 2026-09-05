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
    /*
     * xy : pas en UV (horizontal ou vertical) — nul pour un simple rééchantillonnage.
     * z  : GAIN appliqué au résultat. Il sert la remontée de la pyramide, où
     *      chaque niveau est ajouté au précédent : c'est lui qui décide de la
     *      forme du halo, cœur serré ou nappe large. Il doit être écrit à chaque
     *      passe — un `SDL_zero` de la structure le laisserait à zéro, donc noir.
     * w  : inutilisé.
     */
    vec4 u_direction;
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
    o_color = vec4(result * u_direction.z, 1.0);
}
