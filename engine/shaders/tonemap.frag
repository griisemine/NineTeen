#version 450
/*
 * tonemap.frag — dernière étape : HDR linéaire vers l'écran.
 *
 * Compose le rendu éclairé et le halo, applique la courbe ACES, puis les
 * finitions qui donnent son grain à la salle. Sans tone mapping, tout ce qui
 * dépasse 1.0 (les néons, précisément ce qu'on a passé du temps à rendre
 * lumineux) saturerait en blanc plat.
 */

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_hdr;
layout(set = 2, binding = 1) uniform sampler2D u_bloom;

layout(set = 3, binding = 0) uniform Params {
    vec4 u_settings;   /* x : exposition, y : intensité du halo, z : vignettage, w : grain */
    vec4 u_extra;      /* x : temps, y : saturation, z : aberration chromatique, w : inutilisé */
};

/*
 * Approximation ACES de Narkowicz : une seule fraction rationnelle, très proche
 * de la courbe de référence de l'Academy, et qui préserve la teinte des hautes
 * lumières — un néon rouge saturé vire à l'orange en s'éclaircissant plutôt que
 * de virer au blanc rose.
 */
vec3 acesFilmic(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    /* Aberration chromatique très légère sur les bords : imite l'optique d'une
     * caméra et casse la netteté parfaite qui trahit le rendu de synthèse. */
    float ca = u_extra.z;
    vec2 fromCenter = v_uv - 0.5;
    float r2 = dot(fromCenter, fromCenter);

    vec3 hdr;
    if (ca > 0.0001) {
        vec2 shift = fromCenter * r2 * ca;
        hdr.r = texture(u_hdr, v_uv + shift).r;
        hdr.g = texture(u_hdr, v_uv).g;
        hdr.b = texture(u_hdr, v_uv - shift).b;
    } else {
        hdr = texture(u_hdr, v_uv).rgb;
    }

    vec3 bloom = texture(u_bloom, v_uv).rgb;
    vec3 color = hdr + bloom * u_settings.y;

    /* Filet de sécurité : un NaN ou un infini venu d'une passe amont
     * traverserait la courbe ACES et sortirait en noir, ce qui masque la cause
     * réelle. On le remplace par du noir explicite plutôt que de propager. */
    color = mix(color, vec3(0.0), vec3(isnan(color.r) || isinf(color.r),
                                       isnan(color.g) || isinf(color.g),
                                       isnan(color.b) || isinf(color.b)));

    color *= u_settings.x;                       /* exposition */
    color = acesFilmic(color);

    /* Saturation, réglable : la salle gagne à être un peu plus colorée que le
     * rendu physique brut, néons obligent. */
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, u_extra.y);

    /* Vignettage : concentre le regard vers le centre. */
    float vignette = 1.0 - u_settings.z * r2 * 2.0;
    color *= clamp(vignette, 0.0, 1.0);

    /* Grain animé, très discret : masque le banding dans les dégradés sombres,
     * qui est visible sur les grandes surfaces murales faiblement éclairées. */
    float grain = fract(sin(dot(v_uv * (1.0 + u_extra.x), vec2(12.9898, 78.233))) * 43758.5453);
    color += (grain - 0.5) * u_settings.w;

    /* La swapchain est en sRGB : la conversion est faite par le matériel à
     * l'écriture, on sort donc en linéaire. */
    o_color = vec4(max(color, vec3(0.0)), 1.0);
}
