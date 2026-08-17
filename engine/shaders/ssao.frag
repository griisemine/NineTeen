#version 450
/*
 * ssao.frag — occlusion ambiante en espace écran.
 *
 * Double rôle :
 *   - c'est le repli quand la couche de ray tracing est désactivée (matériel
 *     modeste, ou budget d'image dépassé) ;
 *   - même avec le ray tracing actif, elle apporte le contact rapproché à
 *     faible coût, là où les rayons stochastiques seraient bruités.
 *
 * Méthode : échantillonnage hémisphérique orienté par la normale, avec une
 * rotation par pixel tirée d'un bruit entrelacé — plus régulier qu'un bruit
 * blanc, donc moins de grain après le flou.
 *
 * La sortie s'écrit dans le canal g de la texture de visibilité, dont le canal
 * r (ombres) et b (illumination globale) sont remplis par la couche RT.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_visibility;

layout(set = 2, binding = 0) uniform sampler2D u_depth;
layout(set = 2, binding = 1) uniform sampler2D u_normalRM;

layout(set = 3, binding = 0) uniform Params {
    mat4  u_invViewProj;
    mat4  u_viewProj;
    vec4  u_cameraPos;
    vec4  u_settings;      /* x : rayon (m), y : intensité, z : biais, w : temps */
    ivec4 u_counts;        /* x : nombre d'échantillons */
};

const float PI = 3.14159265359;

vec3 decodeOctahedral(vec2 e)
{
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

vec3 worldFromDepth(vec2 uv, float depth)
{
    vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    vec4 world = u_invViewProj * clip;
    return world.xyz / world.w;
}

/* Bruit entrelacé d'Ynnall : une seule instruction, motif 3x3 très stable dans
 * le temps, ce qui évite le fourmillement d'un bruit blanc en mouvement. */
float interleavedGradientNoise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

/* Suite de Hammersley : couvre l'hémisphère régulièrement avec peu de points. */
vec2 hammersley(uint i, uint n)
{
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}

void main()
{
    float depth = texture(u_depth, v_uv).r;
    if (depth <= 0.0) {                       /* ciel : rien à occlure */
        o_visibility = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 P = worldFromDepth(v_uv, depth);
    vec3 N = decodeOctahedral(texture(u_normalRM, v_uv).xy);

    /* Base orthonormale autour de la normale, tournée par pixel. */
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    T = T * cos(angle) + B * sin(angle);
    B = cross(N, T);

    const float radius = u_settings.x;
    const float bias   = u_settings.z;
    int samples = clamp(u_counts.x, 4, 32);

    float occlusion = 0.0;
    for (int i = 0; i < samples; ++i) {
        vec2 xi = hammersley(uint(i), uint(samples));

        /* Distribution cosinus : concentre les échantillons là où ils comptent
         * pour un terme diffus. */
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);
        float sinTheta = sqrt(xi.y);
        vec3 dir = T * (cos(phi) * sinTheta) + B * (sin(phi) * sinTheta) + N * cosTheta;

        /* Répartir les échantillons en profondeur dans le rayon, plutôt que tous
         * à la même distance : sinon on ne détecte que les contacts à r exactement. */
        float scale = mix(0.15, 1.0, float(i) / float(samples));
        vec3 samplePos = P + dir * radius * scale;

        vec4 clip = u_viewProj * vec4(samplePos, 1.0);
        if (clip.w <= 0.0) continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 suv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;

        float sceneDepth = texture(u_depth, suv).r;
        if (sceneDepth <= 0.0) continue;
        vec3 sceneP = worldFromDepth(suv, sceneDepth);

        /* En reverse-Z, une profondeur plus grande signifie plus proche. */
        bool occluded = sceneDepth > ndc.z + bias;

        /* Atténuation par la distance : un objet lointain vu au même pixel ne
         * doit pas assombrir la surface (c'est l'artefact classique du SSAO). */
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(length(sceneP - P), 1e-4));
        occlusion += occluded ? rangeCheck : 0.0;
    }

    occlusion = 1.0 - (occlusion / float(samples)) * u_settings.y;
    occlusion = clamp(occlusion, 0.0, 1.0);

    /* r : ombres (1 = éclairé, la couche RT écrase cette valeur)
     * g : occlusion ambiante
     * b : facteur d'illumination indirecte (1 par défaut)
     * a : réservé */
    o_visibility = vec4(1.0, occlusion, 1.0, 1.0);
}
