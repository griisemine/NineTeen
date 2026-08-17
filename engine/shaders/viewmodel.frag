#version 450
/*
 * viewmodel.frag — éclairage des bras, en forward.
 *
 * Le G-buffer écrit trois cibles encodées pour la passe d'éclairage différée ;
 * ici on écrit UNE couleur directement dans la cible HDR, donc il faut faire son
 * éclairage soi-même. Rien en aval ne l'ombrera : la passe d'éclairage est déjà
 * passée quand celle-ci s'exécute.
 *
 * Le tampon des lumières est **le même** que celui de `lighting.frag`, et
 * l'atténuation aussi, rayon de source compris. Deux formules donneraient des
 * mains qui ne s'accordent pas avec le mur derrière elles — c'est le genre
 * d'écart qu'on ne sait plus expliquer trois paliers plus tard.
 *
 * Pas de rayon d'ombre : c'est la pratique courante pour un viewmodel, et
 * l'accumulation temporelle du lancer de rayons ne saurait de toute façon pas
 * suivre des mains qui bougent. À la place, un assombrissement de contact
 * analytique — la face interne d'un avant-bras est occultée par le corps, et
 * sans lui les bras flottent, uniformément éclairés, comme collés sur l'image.
 */

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

struct Light {
    vec3  position;  float range;
    vec3  color;     float intensity;
    vec3  direction; float spotCos;
    int   type;      int   shadowIndex;
    float sourceRadius;
    float _pad;
};

/* Aucune texture : le tampon de stockage occupe donc le binding 0 du set 2. */
layout(std430, set = 2, binding = 0) readonly buffer Lights {
    Light lights[];
};

layout(set = 3, binding = 0) uniform Frame {
    vec4  u_baseColor;    /* rgb : couleur, a : rugosité */
    vec4  u_cameraPos;    /* xyz : position, w : métallicité */
    vec4  u_ambient;      /* rgb : ambiance, a : intensité */
    ivec4 u_counts;       /* x : nombre de lumières, yzw : libres */
};

const float PI = 3.14159265359;

float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float geometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float attenuation(float dist, float range, float sourceRadius)
{
    float r = max(sourceRadius, 0.05);
    float d2 = max(dist * dist, r * r);
    float falloff = 1.0 / (4.0 * PI * d2);
    float t = clamp(1.0 - pow(dist / max(range, 0.001), 4.0), 0.0, 1.0);
    return falloff * t * t;
}

void main()
{
    vec3  N = normalize(v_normal);
    vec3  V = normalize(u_cameraPos.xyz - v_world);
    if (!gl_FrontFacing) N = -N;

    float NdotV = max(dot(N, V), 1e-4);
    float roughness = clamp(u_baseColor.a, 0.05, 1.0);
    float metallic  = clamp(u_cameraPos.w, 0.0, 1.0);
    vec3  albedo = u_baseColor.rgb;
    vec3  F0 = mix(vec3(0.04), albedo, metallic);

    /*
     * Assombrissement de contact. `v_uv.y` court de l'articulation haute (0) vers
     * la basse (1) le long du segment, et `N` dit de quel côté regarde le
     * fragment. La face qui regarde le corps — vers l'arrière de la caméra —
     * reçoit moins : c'est ce qui « pose » les bras au lieu de les laisser
     * flotter, aplatis par un éclairage uniforme.
     */
    float facing = clamp(dot(N, V), 0.0, 1.0);
    float contact = mix(0.35, 1.0, facing * 0.6 + 0.4);

    vec3 Lo = vec3(0.0);
    int count = min(u_counts.x, 128);
    for (int i = 0; i < count; ++i) {
        Light li = lights[i];

        vec3  L;
        float atten;
        if (li.type == 2) {
            L = normalize(-li.direction);
            atten = 1.0;
        } else {
            vec3 toLight = li.position - v_world;
            float dist = length(toLight);
            if (dist > li.range) continue;
            L = toLight / max(dist, 1e-4);
            atten = attenuation(dist, li.range, li.sourceRadius);
            if (li.type == 1) {
                float cd = dot(-L, normalize(li.direction));
                if (cd < li.spotCos) continue;
                atten *= smoothstep(li.spotCos, mix(li.spotCos, 1.0, 0.1), cd);
            }
        }

        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;

        vec3  H = normalize(V + L);
        float D = distributionGGX(max(dot(N, H), 0.0), roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3  F = fresnelSchlick(max(dot(V, H), 0.0), F0);

        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

        Lo += (kd * albedo / PI + specular) * li.color * li.intensity * atten * NdotL;
    }

    vec3 ambient = u_ambient.rgb * u_ambient.a * albedo;
    o_color = vec4((Lo + ambient) * contact, 1.0);
}
