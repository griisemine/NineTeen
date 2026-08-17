#version 450
/*
 * gbuffer.frag — écrit les propriétés de surface dans le G-buffer.
 *
 * Trois cibles, choisies pour tenir dans le moins de bande passante possible
 * tout en gardant assez de précision :
 *
 *   RT0  RGBA8 sRGB   albédo.rgb + occlusion ambiante
 *   RT1  RGBA16F      normale encodée en octaédrique (xy) + rugosité + métallicité
 *   RT2  R11G11B10F   émissif (les néons et les écrans de bornes)
 *
 * L'encodage octaédrique met une normale unitaire dans deux composantes sans
 * perte visible, ce qui libère deux canaux pour la rugosité et la métallicité —
 * une cible de rendu en moins par rapport à un stockage naïf.
 */

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_tangent;

layout(location = 0) out vec4 o_albedo_ao;
layout(location = 1) out vec4 o_normal_rm;
layout(location = 2) out vec4 o_emissive;

/* set = 2 : textures du fragment shader */
layout(set = 2, binding = 0) uniform sampler2D u_albedo;
layout(set = 2, binding = 1) uniform sampler2D u_normalMap;
layout(set = 2, binding = 2) uniform sampler2D u_orm;      /* occlusion / rugosité / métal */

/* set = 3 : uniformes du fragment shader */
layout(set = 3, binding = 0) uniform Material {
    vec4  u_baseColor;
    vec4  u_emissive;          /* rgb : couleur, a : intensité */
    vec4  u_params;            /* x : métallicité, y : rugosité, z : a-t-on une normal map, w : temps */
};

/* Encodage octaédrique : projette la sphère unité sur un carré [-1,1]². */
vec2 encodeOctahedral(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xy;
    if (n.z < 0.0) {
        /* Repliement de l'hémisphère inférieur sur les coins du carré. */
        e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return e;
}

void main()
{
    vec4 albedo = texture(u_albedo, v_uv) * u_baseColor;

    /* Le découpage alpha sert aux grilles et aux affiches découpées. Le faire
     * ici plutôt que dans la passe d'éclairage évite d'écrire un G-buffer que
     * l'on rejettera ensuite. */
    if (albedo.a < 0.35) discard;

    vec3 orm = texture(u_orm, v_uv).rgb;
    float occlusion = orm.r;
    float roughness = clamp(orm.g * u_params.y, 0.03, 1.0);
    float metallic  = clamp(u_params.x, 0.0, 1.0);

    /* --- normale --- */
    vec3 N = normalize(v_normal);
    if (u_params.z > 0.5) {
        vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
        vec3 B = cross(N, T) * v_tangent.w;
        vec3 tn = texture(u_normalMap, v_uv).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, N) * tn);
    }
    /* Une face vue de dos (mur regardé depuis l'extérieur, géométrie non
     * fermée du modèle d'origine) doit renvoyer sa normale, sinon elle
     * apparaît noire. */
    if (!gl_FrontFacing) N = -N;

    o_albedo_ao = vec4(albedo.rgb, occlusion);
    o_normal_rm = vec4(encodeOctahedral(N), roughness, metallic);
    o_emissive  = vec4(u_emissive.rgb * u_emissive.a, 1.0);
}
