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
layout(set = 2, binding = 4) uniform sampler2D u_ssao;         /* g : occlusion ambiante */
layout(set = 2, binding = 5) uniform sampler2D u_rtVisibility; /* r : ombres, gba : indirect */
layout(set = 2, binding = 6) uniform sampler2D u_reflections;
/*
 * Ombres des quatre lumières dominantes du pixel, produites par raytrace.comp.
 * Chaque canal porte `(indice << 8) | visibilité sur 8 bits`, indice 255 =
 * « aucune ». Texture entière, donc `usampler2D` et échantillonnage au plus
 * proche — un filtrage bilinéaire mélangerait des indices, ce qui n'a aucun sens.
 */
layout(set = 2, binding = 7) uniform usampler2D u_lightShadow;

struct Light {
    vec3  position;
    float range;
    vec3  color;
    float intensity;
    vec3  direction;
    float spotCos;
    int   type;
    int   shadowIndex;
    float sourceRadius;   /* mètres ; borne la décroissance en 1/d² */
    float _pad;
};

/* Les storage buffers viennent après les textures dans le set 2 : ajouter une
 * texture au-dessus décale donc ce binding, et l'oublier ne produit aucun
 * message — seulement des lumières lues dans le vide. */
layout(std430, set = 2, binding = 8) readonly buffer Lights {
    Light lights[];
};

layout(set = 3, binding = 0) uniform Frame {
    mat4  u_invViewProj;
    vec4  u_cameraPos;        /* xyz : position, w : temps */
    vec4  u_ambient;          /* rgb : lumière d'ambiance, a : intensité */
    vec4  u_fog;              /* rgb : couleur, a : densité */
    ivec4 u_counts;           /* x : nombre de lumières, y : ray tracing actif,
                               * z : brouillard volumétrique actif,
                               * w : diviseur de résolution du lancer de rayons */
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
 *     distance par le rayon physique de la source, ce qui revient à la traiter
 *     comme une petite sphère — ce qu'elle est.
 *
 *     Ce rayon vient désormais **de la lumière**, plus d'une constante globale.
 *     Elle valait 0,22 m, taillée pour l'ampoule d'une applique ; un pavé lumineux
 *     de faux plafond fait 1,20 m, et traité comme une ampoule il brûlait les
 *     dalles à 20 cm tout en n'éclairant presque rien à trois mètres. Il n'y a pas
 *     d'intensité qui rattrape un rapport de 1 à 3 600 : c'est le modèle de source
 *     qu'il fallait corriger, pas le réglage.
 *
 *  3. Fenêtrage à la portée. La contribution est ramenée à zéro en douceur avant
 *     la coupure, sinon on voit un cercle net au sol là où la boucle s'arrête.
 */
/* Repli pour une lumière qui n'en déclare pas : l'ampoule d'applique d'avant. */
const float LIGHT_SOURCE_RADIUS_MIN = 0.05;

float attenuation(float dist, float range, float sourceRadius)
{
    float r = max(sourceRadius, LIGHT_SOURCE_RADIUS_MIN);
    float d2 = max(dist * dist, r * r);
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

    /* L'occlusion ambiante vient toujours de l'espace écran : elle capte le
     * contact rapproché mieux, et moins cher, que des rayons stochastiques. */
    float ao = occlusion * texture(u_ssao, v_uv).g;

    /* Les ombres et l'indirect viennent du lancer de rayons quand il tourne. */
    vec4  rt = texture(u_rtVisibility, v_uv);
    float shadow = (u_counts.y != 0) ? clamp(rt.r, 0.0, 1.0) : 1.0;

    /*
     * Ombres par lumière.
     *
     * `shadow` ci-dessus est une moyenne pondérée sur TOUTES les sources. Elle
     * était appliquée telle quelle à chacune, ce qui est faux : un point à
     * l'ombre d'un pilier perdait aussi la lumière des écrans de bornes, à
     * l'autre bout de la salle. C'est ce qui rendait `docs/render-raytracing.png`
     * nettement plus sombre que le rendu sans lancer de rayons.
     *
     * Les quatre lumières qui comptent le plus pour ce pixel portent désormais
     * leur visibilité exacte. Les autres gardent la moyenne : elles sont faibles
     * par construction, puisqu'elles n'ont pas été retenues.
     */
    int   shadowIdx[4] = int[4](-1, -1, -1, -1);
    float shadowVis[4] = float[4](1.0, 1.0, 1.0, 1.0);
    if (u_counts.y != 0) {
        /* Lecture SEULEMENT quand la couche de lancer de rayons tourne : sans
         * elle, cette cible n'a jamais été écrite, et lire une cible indéfinie
         * est précisément ce qui avait noirci toute l'image en M4. */
        /* La cible du lancer de rayons est plus petite que l'image : sans cette
         * division, on lirait le quart supérieur gauche étiré sur tout l'écran. */
        const int rtDiv = max(u_counts.w, 1);

        /*
         * UN FILTRAGE BILINEAIRE FAIT A LA MAIN, ET POURQUOI IL A FALLU L'ECRIRE.
         *
         * Cette cible est ENTIERE — chaque canal porte `(indice << 8) |
         * visibilite` — donc le materiel ne peut pas l'interpoler : melanger
         * deux indices de lumiere ne veut rien dire, et Vulkan l'interdit. Le
         * code lisait donc UN texel au plus proche. A rtDiv = 2 et une echelle
         * de rendu de 0,75, un texel de cette cible couvre 2,7 pixels de
         * fenetre : chaque bord d'ombre sortait en marches d'escalier de trois
         * pixels, et le debruiteur a-trous les elargissait encore. Mesure a
         * l'oeil sur la vue « plafond » : a --quality=medium, ou les ombres ne
         * passent pas par cette cible, les memes bords sont nets ; a
         * --quality=high ils sont en escalier. Le palier que `--help` annonce
         * « superbe en capture » etait donc le plus laid des deux, et toutes
         * les captures du depot sont prises a ce palier.
         *
         * Ce qu'on ne peut pas interpoler, c'est l'INDICE. La VISIBILITE, elle,
         * s'interpole tres bien — a condition de ne melanger que des valeurs
         * qui parlent de la MEME lumiere. D'ou : les quatre voisins sont lus,
         * et chacun ne contribue au creneau k que si son propre creneau k porte
         * le meme indice que le texel le plus proche. Quand aucun ne le porte —
         * a une vraie discontinuite, la ou l'ensemble des lumieres dominantes
         * change — on retombe exactement sur l'ancien comportement, c'est-a-dire
         * le plus proche. Le filtre ne peut donc pas inventer d'ombre la ou il
         * n'y en a pas.
         *
         * L'HYPOTHESE, ecrite parce qu'elle est fausse quelque part. On compare
         * le creneau k au creneau k, et non chaque creneau a tous les autres :
         * seize comparaisons au lieu de soixante-quatre. Elle tient parce que
         * `raytrace.comp` classe les lumieres par la meme fonction sur des
         * positions voisines, donc l'ordre est stable d'un texel a l'autre. La
         * ou il ne l'est pas, la condition echoue et on retombe sur le plus
         * proche : le pire cas de l'hypothese est l'ancien rendu.
         */
        const ivec2 rtMax = textureSize(u_lightShadow, 0) - ivec2(1);
        const vec2  rtc   = gl_FragCoord.xy / float(rtDiv) - 0.5;
        const ivec2 base  = ivec2(floor(rtc));
        const vec2  frac  = rtc - vec2(base);

        const ivec2 pres = clamp(base + ivec2(frac.x >= 0.5 ? 1 : 0,
                                              frac.y >= 0.5 ? 1 : 0),
                                 ivec2(0), rtMax);
        uvec4 packed = texelFetch(u_lightShadow, pres, 0);
        shadowIdx = int[4](int(packed.x >> 8), int(packed.y >> 8),
                           int(packed.z >> 8), int(packed.w >> 8));
        shadowVis = float[4](float(packed.x & 0xFFu) / 255.0,
                             float(packed.y & 0xFFu) / 255.0,
                             float(packed.z & 0xFFu) / 255.0,
                             float(packed.w & 0xFFu) / 255.0);

        float visAcc[4] = float[4](0.0, 0.0, 0.0, 0.0);
        float visSum[4] = float[4](0.0, 0.0, 0.0, 0.0);
        for (int n = 0; n < 4; ++n) {
            const ivec2 off = ivec2(n & 1, n >> 1);
            const float w = ((off.x == 1) ? frac.x : 1.0 - frac.x)
                          * ((off.y == 1) ? frac.y : 1.0 - frac.y);
            if (w <= 0.0) continue;
            const uvec4 q = texelFetch(u_lightShadow,
                                       clamp(base + off, ivec2(0), rtMax), 0);
            const uint qi[4] = uint[4](q.x >> 8, q.y >> 8, q.z >> 8, q.w >> 8);
            const uint qv[4] = uint[4](q.x & 0xFFu, q.y & 0xFFu,
                                       q.z & 0xFFu, q.w & 0xFFu);
            for (int k = 0; k < 4; ++k) {
                if (int(qi[k]) == shadowIdx[k]) {
                    visAcc[k] += w * float(qv[k]) / 255.0;
                    visSum[k] += w;
                }
            }
        }
        for (int k = 0; k < 4; ++k) {
            if (visSum[k] > 1e-4) shadowVis[k] = visAcc[k] / visSum[k];
        }
    }

    vec3 Lo = vec3(0.0);
    int count = min(u_counts.x, 128);

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
            atten = attenuation(dist, li.range, li.sourceRadius);

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

        /* Visibilité exacte si cette lumière est l'une des quatre dominantes,
         * moyenne pondérée sinon. */
        float vis = shadow;
        if (u_counts.y != 0) {
            if      (i == shadowIdx[0]) vis = shadowVis[0];
            else if (i == shadowIdx[1]) vis = shadowVis[1];
            else if (i == shadowIdx[2]) vis = shadowVis[2];
            else if (i == shadowIdx[3]) vis = shadowVis[3];
        }

        vec3 radiance = li.color * li.intensity * atten;
        Lo += (kd * albedo / PI + specular) * radiance * NdotL * vis;
    }

    /*
     * Ambiance. Sans ray tracing, c'est une constante modulée par l'occlusion —
     * une approximation qui éclaire de la même façon un coin de mur et le
     * milieu de la salle. Avec l'illumination globale, chaque point reçoit la
     * lumière effectivement rebondie autour de lui : c'est ce qui fait que la
     * moquette prend la couleur de la borne qui la surplombe.
     */
    vec3 indirect = (u_counts.y >= 3) ? rt.gba : u_ambient.rgb * u_ambient.a;
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
     * limites du modèle, qui n'est pas fermé de tous les côtés.
     *
     * Coupé quand la passe volumétrique tourne (u_counts.z) : elle fait la même
     * chose en mieux, et superposer les deux donne une salle laiteuse. */
    if (u_counts.z == 0) {
        float dist = length(u_cameraPos.xyz - world);
        float fogFactor = 1.0 - exp(-dist * u_fog.a);
        color = mix(color, u_fog.rgb, clamp(fogFactor, 0.0, 1.0));
    }

    o_color = vec4(color, 1.0);
}
