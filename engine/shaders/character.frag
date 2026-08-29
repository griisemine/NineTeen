#version 450
/*
 * character.frag — éclairage du personnage, en forward.
 *
 * Le même éclairage que `viewmodel.frag`, et ce n'est pas de la paresse : ce
 * fichier partage avec lui le tampon de lumières ET la formule d'atténuation,
 * rayon de source compris. Deux formules donneraient un personnage qui ne
 * s'accorde pas avec le mur derrière lui — le genre d'écart qu'on ne sait plus
 * expliquer trois paliers plus tard.
 *
 * Ce qui change, et pourquoi
 * --------------------------
 * 1. L'ALBÉDO vient d'une TEXTURE. Les bras sont deux teintes en dur — de la
 *    toile et de la peau — parce qu'ils sont générés. Un personnage importé
 *    apporte la sienne, et la remplacer par un aplat reviendrait à jeter ce
 *    pour quoi on l'a importé.
 *
 * 2. Pas d'assombrissement de contact. Sur les bras il « pose » les membres,
 *    dont on ne voit que la face externe. Un personnage se voit de dos, de
 *    profil et de trois quarts : le même terme y creuserait la face qui
 *    s'éloigne de la caméra, c'est-à-dire une ombre qui tourne avec le joueur.
 *
 * 3. La texture prend le binding 0 du set 2, donc le tampon de lumières passe
 *    au binding 1. C'est la seule différence de câblage.
 *
 * 4. L'OPACITÉ. Les bras ne s'effacent jamais — on les regarde depuis
 *    l'intérieur du crâne, rien ne peut venir entre eux et l'objectif. Le
 *    personnage, lui, s'efface quand la caméra lui rentre dedans, ce qui arrive
 *    dès qu'un mur raccourcit le bras de caméra. Le bloc de trame porte donc un
 *    vec4 de plus que celui du viewmodel ; voir `character_fs_ubo` dans
 *    `ns_render.c`, dont il est la copie exacte.
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

layout(set = 2, binding = 0) uniform sampler2D u_albedo;

/* La texture prend le binding 0 : le tampon de lumières passe au 1. */
layout(std430, set = 2, binding = 1) readonly buffer Lights {
    Light lights[];
};

layout(set = 3, binding = 0) uniform Frame {
    vec4  u_baseColor;    /* rgb : teinte multipliée à la texture, a : rugosité */
    vec4  u_cameraPos;    /* xyz : position, w : métallicité */
    vec4  u_ambient;      /* rgb : ambiance, a : intensité */
    ivec4 u_counts;       /* x : nombre de lumières, yzw : libres */
    vec4  u_fade;         /* x : opacité, yzw : libres */
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
    vec3  albedo = texture(u_albedo, v_uv).rgb * u_baseColor.rgb;
    vec3  F0 = mix(vec3(0.04), albedo, metallic);


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

    /*
     * L'alpha sort TEL QUEL et la couleur n'est PAS prémultipliée : le mélange
     * déclaré côté pipeline est `src.rgb * src.a + dst.rgb * (1 - src.a)`,
     * c'est lui qui fait la multiplication. La prémultiplier ici la ferait deux
     * fois, et le personnage s'effacerait au carré — c'est-à-dire beaucoup trop
     * vite, avec pour seul symptôme « le fondu part trop tôt ».
     */
    o_color = vec4(Lo + ambient, clamp(u_fade.x, 0.0, 1.0));
}
