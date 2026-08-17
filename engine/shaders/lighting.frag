#version 450
/*
 * lighting.frag — éclairage différé, modèle PBR metallic-roughness.
 *
 * C'est ici que la salle cesse d'être un décor peint. La V1 utilisait
 * GL_LIGHT0 : une lumière, un modèle de Lambert-Phong, aucune notion de
 * matériau. On applique désormais la BRDF de Cook-Torrance (GGX + Smith +
 * Fresnel), avec les 25 sources déduites du modèle Blender : les six appliques
 * murales, les néons, et les écrans de bornes qui éclairent réellement le sol
 * devant eux.
 *
 * Les ombres ne viennent pas de shadow maps mais de la couche de lancer de
 * rayons (voir raytrace.comp), qui écrit sa visibilité dans une texture lue
 * ici. Sur matériel modeste, cette texture contient à la place une occlusion
 * ambiante en espace écran — d'où le nom neutre u_visibility.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_albedoAO;
layout(set = 2, binding = 1) uniform sampler2D u_normalRM;
layout(set = 2, binding = 2) uniform sampler2D u_emissive;
layout(set = 2, binding = 3) uniform sampler2D u_depth;
layout(set = 2, binding = 4) uniform sampler2D u_visibility;   /* r : ombres, g : AO, ba : GI */
layout(set = 2, binding = 5) uniform sampler2D u_reflections;

struct Light {
    vec3  position;
    float range;
    vec3  color;
    float intensity;
    vec3  direction;
    float spotCos;
    int   type;
    int   shadowIndex;
    vec2  _pad;
};

/* Les storage buffers viennent après les textures dans le set 2. */
layout(std430, set = 2, binding = 6) readonly buffer Lights {
    Light lights[];
};

layout(set = 3, binding = 0) uniform Frame {
    mat4  u_invViewProj;
    vec4  u_cameraPos;        /* xyz : position, w : temps */
    vec4  u_ambient;          /* rgb : lumière d'ambiance, a : intensité */
    vec4  u_fog;              /* rgb : couleur, a : densité */
    ivec4 u_counts;           /* x : nombre de lumières, y : ray tracing actif, z/w : libres */
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

/* Reconstruction de la position monde depuis la profondeur : évite de stocker
 * une cible de rendu supplémentaire pour les positions. */
vec3 worldFromDepth(vec2 uv, float depth)
{
    /* uv a son origine en haut à gauche, les coordonnées normalisées ont +Y
     * vers le haut : d'où l'inversion sur y. */
    vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    vec4 world = u_invViewProj * clip;
    return world.xyz / world.w;
}

/* --- Cook-Torrance --- */

float distributionGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float geometrySmith(float NdotV, float NdotL, float roughness)
{
    /* Approximation de Schlick-GGX, paramétrage direct (k = (r+1)²/8). */
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

/*
 * Atténuation d'une source ponctuelle.
 *
 * Trois éléments, chacun pour une raison précise :
 *
 *  1. Normalisation par 4π. L'intensité du fichier de scène est une puissance
 *     émise dans toutes les directions ; l'éclairement à distance d est cette
 *     puissance répartie sur la sphère de rayon d. Sans ce facteur, les valeurs
 *     ne veulent rien dire physiquement et il faut re-régler chaque lumière à la
 *     main dès qu'on touche à l'exposition.
 *
 *  2. Rayon de source. Une lumière ponctuelle mathématique a une énergie infinie
 *     à distance nulle : une surface frôlant une applique recevait ici plusieurs
 *     centaines de fois l'exposition et ressortait en blanc pur. On borne donc la
 *     distance par le rayon physique de l'ampoule, ce qui revient à traiter la
 *     source comme une petite sphère — ce qu'elle est.
 *
 *  3. Fenêtrage à la portée. La contribution est ramenée à zéro en douceur avant
 *     la coupure, sinon on voit un cercle net au sol là où la boucle s'arrête.
 */
const float LIGHT_SOURCE_RADIUS = 0.22;   /* ampoule d'applique, en mètres */

float attenuation(float dist, float range)
{
    float d2 = max(dist * dist, LIGHT_SOURCE_RADIUS * LIGHT_SOURCE_RADIUS);
    float falloff = 1.0 / (4.0 * PI * d2);
    float t = clamp(1.0 - pow(dist / max(range, 0.001), 4.0), 0.0, 1.0);
    return falloff * t * t;
}

void main()
{
    float depth = texture(u_depth, v_uv).r;

    vec4 albedoAO = texture(u_albedoAO, v_uv);
    vec4 normalRM = texture(u_normalRM, v_uv);
    vec3 emissive = texture(u_emissive, v_uv).rgb;

    /* En reverse-Z, la profondeur 0 est le plan lointain : c'est le ciel, ou le
     * fond vide. On y met le brouillard plutôt que du noir. */
    if (depth <= 0.0) {
        o_color = vec4(u_fog.rgb * 0.35 + emissive, 1.0);
        return;
    }

    vec3  albedo    = albedoAO.rgb;
    float occlusion = albedoAO.a;
    vec3  N         = decodeOctahedral(normalRM.xy);
    float roughness = clamp(normalRM.z, 0.03, 1.0);
    float metallic  = clamp(normalRM.w, 0.0, 1.0);

    vec3 world = worldFromDepth(v_uv, depth);
    vec3 V = normalize(u_cameraPos.xyz - world);
    float NdotV = max(dot(N, V), 1e-4);

    /* Réflectivité à incidence normale : 4 % pour un diélectrique, l'albédo
     * lui-même pour un métal — c'est ce qui distingue le chrome du plastique. */
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec4 visibility = texture(u_visibility, v_uv);
    float shadow = (u_counts.y != 0) ? visibility.r : 1.0;
    float ao     = occlusion * visibility.g;

    vec3 Lo = vec3(0.0);
    int count = min(u_counts.x, 64);

    for (int i = 0; i < count; ++i) {
        Light li = lights[i];

        vec3  L;
        float atten;
        if (li.type == 2) {                       /* directionnelle */
            L = normalize(-li.direction);
            atten = 1.0;
        } else {
            vec3 toLight = li.position - world;
            float dist = length(toLight);
            if (dist > li.range) continue;        /* hors de portée : on saute */
            L = toLight / max(dist, 1e-4);
            atten = attenuation(dist, li.range);

            if (li.type == 1) {                   /* projecteur */
                float cd = dot(-L, normalize(li.direction));
                if (cd < li.spotCos) continue;
                /* Bord adouci sur les dix derniers pour cent du cône. */
                atten *= smoothstep(li.spotCos, mix(li.spotCos, 1.0, 0.1), cd);
            }
        }

        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;

        vec3  H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float D = distributionGGX(NdotH, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3  F = fresnelSchlick(VdotH, F0);

        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        /* Un métal n'a pas de composante diffuse. */
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

        vec3 radiance = li.color * li.intensity * atten;
        Lo += (kd * albedo / PI + specular) * radiance * NdotL * shadow;
    }

    /* Ambiance. En l'absence de couche de ray tracing, c'est une constante
     * modulée par l'occlusion ; avec elle, l'illumination globale calculée
     * remplace cette approximation. */
    vec3 indirect = (u_counts.y != 0) ? visibility.b * u_ambient.rgb * u_ambient.a
                                      : u_ambient.rgb * u_ambient.a;
    vec3 ambient = indirect * albedo * ao;

    /* Réflexions : uniquement si la couche de ray tracing les a produites.
     * Lire une cible qui n'a jamais été écrite renvoie de la mémoire GPU
     * arbitraire — donc potentiellement des NaN, qui contaminent toute la
     * couleur et ressortent en noir après le tone mapping. Le test explicite
     * est ce qui garantit qu'on ne dépend jamais d'un contenu indéfini. */
    if (u_counts.y >= 2) {
        vec3 reflections = texture(u_reflections, v_uv).rgb;
        vec3 F_env = fresnelSchlick(NdotV, F0);
        ambient += reflections * F_env * (1.0 - roughness * 0.85);
    }

    vec3 color = Lo + ambient + emissive;

    /* Brouillard exponentiel : donne de la profondeur à la salle et adoucit les
     * limites du modèle, qui n'est pas fermé de tous les côtés. */
    float dist = length(u_cameraPos.xyz - world);
    float fogFactor = 1.0 - exp(-dist * u_fog.a);
    color = mix(color, u_fog.rgb, clamp(fogFactor, 0.0, 1.0));

    o_color = vec4(color, 1.0);
}
