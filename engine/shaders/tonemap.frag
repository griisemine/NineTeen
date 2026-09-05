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

/* Le tampon de stockage vient après les textures dans le set 2. Il porte
 * l'exposition mesurée par `exposure.comp` sur l'image précédente. */
layout(std430, set = 2, binding = 2) readonly buffer Exposure {
    float u_measuredExposure;
    float u_measuredLuma;
    float _pad0;
    float _pad1;
};

layout(set = 3, binding = 0) uniform Params {
    vec4 u_settings;   /* x : exposition de repli, y : intensité du halo, z : vignettage, w : grain */
    vec4 u_extra;      /* x : temps, y : saturation, z : aberration chromatique,
                        * w : 1 = employer l'exposition mesurée */
    /*
     * LA TEINTE ACHETEE. rgb : la couleur ; w : sa force, 0 = inerte.
     *
     * Elle existe pour un lot de la vitrine — « PLAQUE DOREE », 320 tickets,
     * soit une trentaine de parties — qui promettait que « la salle vous passe
     * en or » et ne faisait rien. Un lot paye qui ne change rien est le pire
     * defaut qu'un jeu puisse avoir : il apprend au joueur que ses tickets ne
     * valent rien.
     *
     * C'est une correction de teinte et non un filtre pose par-dessus : elle
     * s'applique APRES le mapping de tons, sur une image deja bornee a [0,1],
     * donc elle ne peut ni bruler les hautes lumieres ni ecraser les noirs. La
     * cible est la LUMINANCE multipliee par la teinte — un or monochrome — et
     * on melange vers elle. A force partielle, les ecrans de bornes gardent
     * donc leur couleur propre : la salle vire a l'or, les jeux non, et c'est
     * ce qui la garde jouable.
     */
    vec4 u_grade;
};

/*
 * Approximation ACES de Narkowicz : une seule fraction rationnelle, très proche
 * de la courbe de référence de l'Academy, et qui préserve la teinte des hautes
 * lumières — un néon rouge saturé vire à l'orange en s'éclaircissant plutôt que
 * de virer au blanc rose.
 *
 * ELLE EST APPLIQUÉE CANAL PAR CANAL, ET LE SOUPÇON HABITUEL EST À L'ENVERS.
 *
 * Quand une salle de néon sort délavée, on accuse le mapping de tons, et
 * nommément « le Reinhard naïf, qui désature violemment les hautes lumières ».
 * Vérifié en chiffres plutôt que cru sur parole, sur un rose de néon de
 * saturation HSV 0,900, entrée linéaire (3,0 ; 0,30 ; 2,4), exposition 1,60 :
 *
 *   Reinhard sur la luminance   0,818   il RESCALE les trois canaux du même
 *                                       facteur, donc il conserve le rapport
 *   Reinhard par canal          0,608
 *   ACES par canal, ici         0,387
 *   ACES sur le canal fort seul 0,900
 *
 * C'est donc la courbe EN PLACE qui désature le plus, et le Reinhard accusé qui
 * désature le moins. Sauf que ce n'est pas un défaut ici : c'est exactement ce
 * que demande la photo de référence, un cœur de source qui part au blanc. Et la
 * courbe ne le fait qu'en haut — sur le même rose à un sixième de l'intensité,
 * (0,5 ; 0,05 ; 0,4), celle du HALO, la saturation reste à 0,879. Cœur blanc,
 * halo coloré, sans qu'on ait rien à écrire pour ça.
 *
 * La variante « préservation de teinte » a quand même été écrite et mesurée :
 * ne faire passer par la courbe que le canal le plus fort, garder le rapport
 * des trois, et ne blanchir qu'au-dessus de 0,88 de sortie. Mélangée à 0,30,
 * sur les cinq vues nommées et la même salle, elle rapporte au mieux 0,012 de
 * saturation pondérée sur une vue — rien ou moins que rien sur deux autres — et
 * coûte 3 à 5 points d'écart interquartile SUR LES CINQ, parce que conserver le
 * rapport LINÉAIRE des canaux remonte les ombres colorées. On paierait quatre
 * points de noir pour un demi-point de couleur. Elle n'est pas retenue, et
 * c'est écrit ici pour que la mesure ne soit pas refaite une troisième fois.
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

    /* Exposition mesurée quand l'adaptation tourne, constante sinon. Le repli
     * n'est pas décoratif : au palier « low » la passe de mesure n'existe pas,
     * et lire un tampon jamais écrit donnerait une image noire ou blanche. */
    float exposure = (u_extra.w > 0.5) ? u_measuredExposure : u_settings.x;
    color *= exposure;
    color = acesFilmic(color);

    /* Saturation, réglable : la salle gagne à être un peu plus colorée que le
     * rendu physique brut, néons obligent. */
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, u_extra.y);

    /* La teinte achetee, avant le vignettage : elle fait partie de l'image, pas
     * de l'optique. */
    if (u_grade.w > 0.0) {
        float gl = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(color, gl * u_grade.rgb, clamp(u_grade.w, 0.0, 1.0));
    }

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
