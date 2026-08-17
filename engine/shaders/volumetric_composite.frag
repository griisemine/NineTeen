#version 450
/*
 * volumetric_composite.frag — remonte le brouillard demi-résolution et le compose.
 *
 *     couleur = couleur * transmittance + diffusion
 *
 * L'ordre dans la chaîne n'est pas indifférent : cette passe s'insère **après
 * l'éclairage et avant le halo**. Après le halo, les rais de lumière ne
 * fleuriraient pas — or c'est justement leur débordement qui les rend crédibles.
 * Avant l'éclairage, il n'y aurait rien à atténuer.
 *
 * La remontée est **guidée par la profondeur** et non bilinéaire. Une silhouette
 * de borne devant un mur lointain donne, à demi-résolution, des texels qui
 * mélangent les deux distances : interpolés naïvement, le brouillard du fond
 * bave d'un demi-texel sur la borne, et ce liseré se voit. On pondère donc les
 * quatre voisins par l'écart de profondeur, et on retombe sur le plus proche
 * quand aucun ne correspond.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_color;       /* HDR éclairé */
layout(set = 2, binding = 1) uniform sampler2D u_scatter;     /* demi-résolution */
layout(set = 2, binding = 2) uniform sampler2D u_depth;       /* pleine résolution */

layout(set = 3, binding = 0) uniform Params {
    vec4 u_size;      /* xy : taille demi-résolution, zw : taille pleine */
    vec4 u_settings;  /* x : intensité, y : sensibilité de profondeur, z/w : libres */
};

void main()
{
    vec3 color = texture(u_color, v_uv).rgb;

    const vec2 halfSize = max(u_size.xy, vec2(1.0));
    const vec2 texel = 1.0 / halfSize;

    /* Profondeur de référence : celle du pixel plein écran qu'on compose. */
    float depthRef = texture(u_depth, v_uv).r;

    /* Les quatre texels demi-résolution qui entourent ce pixel. */
    vec2 coord = v_uv * halfSize - 0.5;
    vec2 base = floor(coord);
    vec2 frac = coord - base;

    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    vec4 nearest = vec4(0.0, 0.0, 0.0, 1.0);
    float nearestDelta = 1e9;

    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            vec2 tap = (base + vec2(i, j) + 0.5) * texel;
            vec4 s = texture(u_scatter, tap);

            /* Profondeur au même endroit, prise en pleine résolution : c'est ce
             * qui permet de savoir si ce texel décrit la même surface. */
            float d = texture(u_depth, tap).r;
            float delta = abs(d - depthRef);

            float bilinear = (i == 0 ? 1.0 - frac.x : frac.x) * (j == 0 ? 1.0 - frac.y : frac.y);
            float depthWeight = exp(-delta * u_settings.y);
            float w = bilinear * depthWeight;

            sum += s * w;
            weightSum += w;

            if (delta < nearestDelta) { nearestDelta = delta; nearest = s; }
        }
    }

    /* Aucun voisin plausible : on prend celui dont la profondeur est la plus
     * proche, plutôt qu'une moyenne qui mélangerait deux surfaces. */
    vec4 fog = (weightSum > 1e-4) ? (sum / weightSum) : nearest;

    float transmittance = clamp(fog.a, 0.0, 1.0);
    vec3  scatter = max(fog.rgb, vec3(0.0)) * u_settings.x;

    o_color = vec4(color * transmittance + scatter, 1.0);
}
