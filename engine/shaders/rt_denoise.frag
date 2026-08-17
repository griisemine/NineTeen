#version 450
/*
 * rt_denoise.frag — filtre à-trous guidé par la géométrie.
 *
 * Un lancer de rayons stochastique à un ou quatre rayons par pixel est
 * inutilisable brut : l'image grésille. L'accumulation temporelle règle le cas
 * de la caméra immobile, mais en mouvement l'historique doit être rejeté, et il
 * ne reste que le filtrage spatial.
 *
 * Le filtre moyenne les voisins, mais **seulement ceux qui appartiennent à la
 * même surface** : deux poids supplémentaires rejettent les échantillons dont la
 * profondeur ou la normale s'écartent trop. Sans ces poids, un flou ordinaire
 * baverait l'ombre d'une borne sur le mur derrière elle.
 *
 * L'espacement des prises est passé en paramètre : en enchaînant plusieurs
 * passes avec un espacement doublé à chaque fois (1, 2, 4…), on couvre un large
 * voisinage avec très peu de textures lues — c'est le principe du filtre à-trous.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_signal;   /* visibilité RT à filtrer */
layout(set = 2, binding = 1) uniform sampler2D u_depth;
layout(set = 2, binding = 2) uniform sampler2D u_normalRM;

layout(set = 3, binding = 0) uniform Params {
    vec4 u_step;      /* xy : pas en UV, z : sensibilité profondeur, w : sensibilité normale */
};

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
    float centerDepth = texture(u_depth, v_uv).r;
    if (centerDepth <= 0.0) {                 /* ciel : rien à filtrer */
        o_color = texture(u_signal, v_uv);
        return;
    }

    vec3 centerNormal = decodeOctahedral(texture(u_normalRM, v_uv).xy);
    vec4 centerValue  = texture(u_signal, v_uv);

    /* Noyau 5x5 séparable en poids (1,4,6,4,1)/16 par axe. */
    const float kernel[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);

    vec4  sum = vec4(0.0);
    float weightSum = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 offset = vec2(float(x), float(y)) * u_step.xy;
            vec2 uv = v_uv + offset;
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;

            float d = texture(u_depth, uv).r;
            if (d <= 0.0) continue;

            /* Poids de profondeur. On compare en profondeur normalisée : en
             * reverse-Z l'écart utile n'est pas linéaire, mais la comparaison
             * relative suffit à séparer deux surfaces distinctes. */
            float depthDelta = abs(d - centerDepth) / max(centerDepth, 1e-5);
            float wDepth = exp(-depthDelta * u_step.z);

            /* Poids de normale : rejette le mur d'à côté même s'il est à la
             * même distance. */
            vec3 n = decodeOctahedral(texture(u_normalRM, uv).xy);
            float wNormal = pow(max(dot(n, centerNormal), 0.0), u_step.w);

            float w = kernel[x + 2] * kernel[y + 2] * wDepth * wNormal;
            sum += texture(u_signal, uv) * w;
            weightSum += w;
        }
    }

    /* Si aucun voisin n'a été retenu (arête isolée, pixel unique), on garde la
     * valeur d'origine plutôt que de diviser par zéro. */
    o_color = (weightSum > 1e-5) ? (sum / weightSum) : centerValue;
}
