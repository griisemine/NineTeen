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
 *
 * ---------------------------------------------------------------------------
 * POURQUOI LA PEAU EST PEINTE ICI, ET NON DANS UNE IMAGE
 * ---------------------------------------------------------------------------
 *
 * Ce que le propriétaire en a dit après avoir joué : il voulait « des mains plus
 * jolies / sans être réaliste de trop mais avec de bonne texture ». Le défaut
 * qu'il décrit se lit sur n'importe quelle capture : la main était UNE SEULE
 * COULEUR. Pas mal dessinée — la silhouette et le galbe étaient déjà là — mais
 * PLATE, sans ongle, sans pli, sans rien qui accroche la lumière. Et comme les
 * deux mains sont à l'image en permanence, c'est ce qu'on regarde le plus.
 *
 * Une planche peinte aurait obligé à inventer un atlas, un dépliage, un outil
 * pour la dessiner et un descripteur d'image que ce shader n'a pas. Pour sept
 * segments dont on ne voit jamais que le dos de la main, la peinture
 * PROCÉDURALE coûte ce fichier et rien d'autre : pas d'octet d'asset, pas
 * d'échantillonnage, pas d'étape de build.
 *
 * Elle a besoin d'une seule chose, savoir OÙ elle est sur la main. C'est ce que
 * `ns_viewmodel.h` fait porter au `uv` — dorsalité, code de pièce, avancement —
 * qui n'était jusque-là écrit par personne et lu par personne.
 *
 * La contrainte artistique est « sans être réaliste DE TROP » : on ne cherche
 * donc pas la peau photographique. Tout ce qui suit est délibérément retenu —
 * des écarts de quelques pour cent sur la couleur, pas des motifs qu'on
 * remarque. Ce qu'on veut est qu'une main se lise comme une main : un ongle qui
 * brille, une jointure qui se marque, des doigts qui se détachent l'un de
 * l'autre, une matière qui n'est pas du plastique.
 */

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
/* La position dans le segment, en mètres — le repère fixe du grain. Voir
 * `viewmodel.vert`, qui dit pourquoi ce n'est pas celle du monde. */
layout(location = 3) in vec3 v_local;

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
    ivec4 u_counts;       /* x : nombre de lumières, y : nature du segment, zw : libres */
};

const float PI = 3.14159265359;

/* ==========================================================================
 * La peinture
 * ========================================================================== */

/* Les codes de pièce, copie GLSL de `ns_viewmodel_part`. Le shader ne peut pas
 * inclure l'en-tête ; c'est lui qui fait foi. */
const int PART_LIMB   = 0;
const int PART_PALM   = 1;
const int PART_THENAR = 2;
const int PART_FINGER = 3;
const int PART_THUMB  = 4;
const int PART_TOKEN  = 5;

/* La compression de l'avancement, en accord avec `NS_VM_PART_SPAN`. */
const float PART_SPAN = 0.99;

/* La nature du segment, telle que `ns_render.c` la pousse dans `u_counts.y`.
 * Elle distingue ce que le code de pièce ne peut pas : une manche d'épaule d'un
 * avant-bras, qui sont la même pièce et n'ont pas le même poignet. */
const int SEG_SLEEVE  = 0;
const int SEG_FOREARM = 1;
const int SEG_HAND    = 2;
const int SEG_TOKEN   = 3;

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

/* Bruit de valeur trilinéaire. Trois octaves seraient du luxe : à la taille où
 * l'on voit ses mains, une seule suffit et se paie une fois. */
float bruit(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i + vec3(0, 0, 0)), hash13(i + vec3(1, 0, 0)), f.x),
                   mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x),
                   mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

/*
 * LA TOILE de la manche.
 *
 * Le défaut visible sur la capture n'était pas la couleur du tissu mais sa
 * FIN : l'avant-bras s'arrêtait net dans la main, sur un simple changement de
 * teinte, ce qui se lit comme deux objets posés bout à bout et non comme une
 * manche d'où sort un poignet. Le revers est là pour ça, et il n'est dessiné
 * qu'au bout de l'AVANT-BRAS : celui de la manche d'épaule tombe dans le coude,
 * où le dôme de l'avant-bras le recouvre.
 */
void toile(int segment, float dorsal, float along, vec3 local,
           inout vec3 albedo, inout float roughness, inout float occlusion)
{
    /* L'armure : un bruit fin pour la fibre, une trame régulière pour le tissage.
     * Mélangés, parce que la trame seule moire dès qu'on s'éloigne et que le
     * bruit seul fait du feutre. */
    float fibre = bruit(local * 380.0);
    float trame = 0.5 + 0.5 * sin(dorsal * 88.0) * sin(along * 190.0);
    albedo *= 0.90 + 0.20 * mix(fibre, trame, 0.40);
    roughness = clamp(roughness - 0.06 + 0.12 * fibre, 0.05, 1.0);

    if (segment != SEG_FOREARM) return;

    /* Le revers remonte sur la manche, et l'ourlet qui le termine attrape la
     * lumière — c'est l'ourlet, pas le pli, qui dit qu'un vêtement s'arrête. */
    float revers = smoothstep(0.80, 0.92, along);
    float ourlet = revers * (1.0 - smoothstep(0.92, 0.99, along));
    albedo *= 1.0 + 0.45 * ourlet;
    roughness = clamp(roughness - 0.10 * ourlet, 0.05, 1.0);
    /* Et l'intérieur de la manche, après l'ourlet, est dans l'ombre du tissu. */
    occlusion *= mix(1.0, 0.62, smoothstep(0.92, 1.0, along));
}

/*
 * LA PEAU.
 *
 * L'ordre compte : le ton d'abord, le grain ensuite, les accidents
 * (plis, jointures, ongles) en dernier — un ongle ne doit pas être repeint par
 * le grain qui le précède.
 */
void peau(int part, float dorsal, float along, vec3 local,
          inout vec3 albedo, inout float roughness, inout float occlusion)
{
    /*
     * LE TON. Le dos d'une main est plus sombre et plus jaune que la paume, qui
     * est plus rose : c'est la différence la plus visible d'une vraie main, et
     * elle ne coûte qu'un mélange. Sans elle une main tournée reste la même
     * couleur des deux côtés, ce qui la fait lire comme un objet peint.
     */
    vec3 dos   = albedo * vec3(0.96, 0.97, 0.94);
    vec3 paume = albedo * vec3(1.08, 0.92, 0.90);
    albedo = mix(paume, dos, smoothstep(0.25, 0.75, dorsal));

    /*
     * LE GRAIN, à deux échelles. Le fin casse le spéculaire — c'est lui qui
     * empêche la main de briller comme une bille — et le large pose les nuances
     * de rougeur qu'une peau a toujours et qu'un aplat n'a jamais.
     */
    float fin   = bruit(local * 560.0);
    float large = bruit(local * 88.0);
    albedo *= 0.95 + 0.10 * fin;
    albedo = mix(albedo, albedo * vec3(1.10, 0.93, 0.90), large * 0.30);
    roughness = clamp(roughness + 0.16 * (fin - 0.5), 0.05, 1.0);

    /*
     * LES TENDONS du dos de la main. Quatre reliefs qui courent du poignet aux
     * jointures : la géométrie de la paume est une section lisse et ne peut pas
     * les porter, mais l'ombre le peut, et c'est ce qui fait qu'un dos de main
     * n'est pas un galet. Leur écartement suit les colonnes de doigts.
     */
    if (part == PART_PALM) {
        float versDoigts = smoothstep(0.20, 0.90, along);
        float cote = smoothstep(0.50, 0.90, dorsal) * versDoigts;
        float tendon = 0.5 + 0.5 * cos(local.x * 310.0);
        albedo *= 1.0 + 0.07 * (tendon - 0.5) * cote;
        occlusion *= mix(1.0, 0.90, (1.0 - tendon) * cote);
    }

    if (part == PART_FINGER || part == PART_THUMB) {
        /*
         * LES FLANCS. Deux doigts voisins s'occultent l'un l'autre, et rien ne
         * calcule cette ombre-là : sans elle les quatre doigts se fondent en une
         * seule masse, ce qui est précisément ce qu'on reprochait à la capture.
         * Un doigt éteint sur ses côtés se détache de son voisin.
         */
        float flanc = 1.0 - abs(dorsal - 0.5) * 2.0;
        occlusion *= mix(1.0, 0.68, flanc * flanc);

        /*
         * LES ARTICULATIONS. Côté paume elles se plissent, côté dos elles
         * bombent et la peau y est plus sèche et plus rouge. Les deux se posent
         * au même endroit — là où le maillage plie déjà.
         */
        float a1 = (part == PART_FINGER) ? 0.333 : 0.500;
        float a2 = (part == PART_FINGER) ? 0.666 : 1.000;
        float j = max(1.0 - smoothstep(0.0, 0.060, abs(along - a1)),
                      1.0 - smoothstep(0.0, 0.060, abs(along - a2)));

        float pli = j * (1.0 - dorsal);
        occlusion *= mix(1.0, 0.74, pli);
        albedo = mix(albedo, albedo * vec3(1.06, 0.86, 0.84), pli * 0.55);

        float bosse = j * dorsal;
        roughness = clamp(roughness + 0.20 * bosse, 0.05, 1.0);
        albedo = mix(albedo, albedo * vec3(1.14, 0.84, 0.80), bosse * 0.45);

        /*
         * L'ONGLE, et c'est le détail qui fait basculer la lecture : une forme
         * arrondie et lisse au bout d'un doigt EST un doigt, la même sans ongle
         * est un tube. Il tient sur la dernière phalange, côté dos — la
         * dorsalité suffit à l'y poser sans que la pièce sache de quel doigt il
         * s'agit.
         *
         * Ce qui le signale n'est pas sa couleur mais sa RUGOSITÉ : la corne est
         * la seule surface vernie de la main, et un reflet net au milieu d'une
         * peau mate se voit même quand l'ongle fait six pixels.
         */
        float distale = (part == PART_FINGER) ? smoothstep(0.66, 0.78, along)
                                              : smoothstep(0.50, 0.64, along);
        float ongle = distale * smoothstep(0.56, 0.76, dorsal);
        albedo = mix(albedo, albedo * vec3(1.26, 1.24, 1.22) + vec3(0.035), ongle);
        roughness = mix(roughness, 0.12, ongle);
        /* Le bourrelet qui borde l'ongle : sans lui la corne est un autocollant. */
        float bord = ongle * (1.0 - ongle) * 4.0;
        occlusion *= mix(1.0, 0.86, bord);
    }

    /*
     * L'ÉMINENCE THÉNAR est du muscle sous une peau épaisse : plus rose et plus
     * mate que le dos, et c'est ce qui la distingue à l'œil du reste de la paume.
     */
    if (part == PART_THENAR) {
        albedo *= vec3(1.06, 0.95, 0.93);
        roughness = clamp(roughness + 0.06, 0.05, 1.0);
    }
}

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
    float occlusion = 1.0;

    /* Le décodage de ce que le maillage a écrit — voir `ns_viewmodel_part`. */
    int   part    = int(floor(v_uv.y));
    float along   = clamp(fract(v_uv.y) / PART_SPAN, 0.0, 1.0);
    float dorsal  = clamp(v_uv.x, 0.0, 1.0);
    int   segment = u_counts.y;

    if (part == PART_LIMB) {
        toile(segment, dorsal, along, v_local, albedo, roughness, occlusion);
    } else if (part != PART_TOKEN) {
        peau(part, dorsal, along, v_local, albedo, roughness, occlusion);

        /*
         * LE BORD QUI ROUGIT. Une peau est translucide : à l'endroit où elle
         * s'échappe du regard, la lumière l'a traversée et en ressort rouge.
         * C'est le seul emprunt au rendu de peau sérieux, et il est ici parce
         * qu'il coûte une puissance et qu'il fait, à lui seul, la différence
         * entre de la chair et du plastique couleur chair.
         *
         * Volontairement tenu bas : poussé, il donne le personnage de cire
         * rétroéclairé, qui est l'autre façon de rater une peau.
         */
        albedo = mix(albedo, albedo * vec3(1.30, 0.66, 0.56),
                     pow(1.0 - NdotV, 3.0) * 0.45);
    }

    vec3  F0 = mix(vec3(0.04), albedo, metallic);

    /*
     * Assombrissement de contact. `N` dit de quel côté regarde le fragment : la
     * face qui regarde le corps — vers l'arrière de la caméra — reçoit moins.
     * C'est ce qui « pose » les bras au lieu de les laisser flotter, aplatis par
     * un éclairage uniforme.
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
    o_color = vec4((Lo + ambient) * contact * occlusion, 1.0);
}
