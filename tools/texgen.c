/*
 * texgen — fabrique des matériaux PBR à partir des textures diffuses de 2020.
 *
 * Les 60 textures de la salle sont des JPEG plats : une photo de bois, de
 * moquette ou de carrelage, sans relief ni variation de brillance. Posées sur
 * un rendu éclairé, elles donnent des surfaces en plastique mat. Cet outil
 * produit, pour chacune, les cartes qui manquaient :
 *
 *   <nom>_n.png    normale — le relief, dérivé du contraste local
 *   <nom>_orm.png  occlusion (R), rugosité (G), métallicité (B), convention glTF
 *
 * Aucune de ces cartes n'est « vraie » : on ne retrouve pas une information
 * physique absente de la photo. Ce sont des approximations perceptuelles, dont
 * l'effet est de rendre la lumière lisible sur la surface. Chaque heuristique
 * est documentée à son endroit, et réglable en ligne de commande — un artiste
 * peut remplacer n'importe quelle sortie par une carte peinte à la main.
 */
#include "tools_common.h"

#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

typedef struct options {
    float normal_strength;   /* amplitude du relief déduit */
    float rough_min, rough_max;
    float ao_strength;
    int   upscale;           /* facteur d'agrandissement, 1 = aucun */
    int   blur_radius;       /* rayon du flou servant de référence basse fréquence */
} options;

/* Luminance perceptuelle (Rec. 709) : c'est ce que l'œil lit comme « clair ou
 * sombre », donc la meilleure approximation de hauteur qu'on puisse tirer d'une
 * photo diffuse. */
static float luminance(const unsigned char *px)
{
    return (0.2126f * (float)px[0] + 0.7152f * (float)px[1] + 0.0722f * (float)px[2]) / 255.0f;
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float sample_height(const float *h, int w, int hgt, int x, int y)
{
    /* Bord répété : les textures de la salle sont majoritairement carrelées,
     * un bord miroir créerait une couture visible. */
    if (x < 0)    x += w;
    if (x >= w)   x -= w;
    if (y < 0)    y += hgt;
    if (y >= hgt) y -= hgt;
    return h[(size_t)y * (size_t)w + (size_t)x];
}

/* Flou séparable en deux passes : O(r) par pixel au lieu de O(r²). */
static void box_blur(const float *src, float *dst, int w, int h, int radius)
{
    if (radius <= 0) { memcpy(dst, src, sizeof(float) * (size_t)w * (size_t)h); return; }

    float *tmp = (float *)malloc(sizeof(float) * (size_t)w * (size_t)h);
    if (!tmp) tool_fatalf("mémoire épuisée (flou)");

    const float inv = 1.0f / (float)(2 * radius + 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) sum += sample_height(src, w, h, x + k, y);
            tmp[(size_t)y * (size_t)w + (size_t)x] = sum * inv;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) sum += sample_height(tmp, w, h, x, y + k);
            dst[(size_t)y * (size_t)w + (size_t)x] = sum * inv;
        }
    }
    free(tmp);
}

int main(int argc, char **argv)
{
    options opt = {
        .normal_strength = 3.0f,
        .rough_min = 0.28f,
        .rough_max = 0.92f,
        .ao_strength = 1.0f,
        .upscale = 1,
        .blur_radius = 4,
    };

    const char *in_path = NULL, *out_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--strength=", 11) == 0)      opt.normal_strength = (float)atof(argv[i] + 11);
        else if (strncmp(argv[i], "--upscale=", 10) == 0)  opt.upscale = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--rough-min=", 12) == 0) opt.rough_min = (float)atof(argv[i] + 12);
        else if (strncmp(argv[i], "--rough-max=", 12) == 0) opt.rough_max = (float)atof(argv[i] + 12);
        else if (strncmp(argv[i], "--ao=", 5) == 0)        opt.ao_strength = (float)atof(argv[i] + 5);
        else if (strncmp(argv[i], "--blur=", 7) == 0)      opt.blur_radius = atoi(argv[i] + 7);
        else if (!in_path)  in_path = argv[i];
        else if (!out_dir)  out_dir = argv[i];
    }

    if (!in_path || !out_dir) {
        fprintf(stderr,
            "texgen — dérive normal map et ORM d'une texture diffuse\n"
            "usage : %s [options] <image> <répertoire de sortie>\n"
            "  --strength=F   amplitude du relief (défaut 3.0)\n"
            "  --upscale=N    agrandissement Mitchell avant traitement (défaut 1)\n"
            "  --rough-min=F  rugosité des zones lisses (défaut 0.28)\n"
            "  --rough-max=F  rugosité des zones texturées (défaut 0.92)\n"
            "  --ao=F         intensité de l'occlusion de cavité (défaut 1.0)\n"
            "  --blur=N       rayon de la référence basse fréquence (défaut 4)\n", argv[0]);
        return 2;
    }
    if (opt.upscale < 1 || opt.upscale > 4) tool_fatalf("--upscale doit être entre 1 et 4");

    int w = 0, h = 0, comp = 0;
    unsigned char *src = stbi_load(in_path, &w, &h, &comp, 4);
    if (!src) tool_fatalf("image illisible (%s) : %s", in_path, stbi_failure_reason());

    /* Agrandissement optionnel. Mitchell est un bon compromis netteté/anneaux
     * pour de la photo ; sur une texture de 24x24 pixels (il y en a dans la
     * salle) cela évite un résultat en gros carrés. */
    if (opt.upscale > 1) {
        const int nw = w * opt.upscale, nh = h * opt.upscale;
        unsigned char *big = (unsigned char *)malloc((size_t)nw * (size_t)nh * 4);
        if (!big) tool_fatalf("mémoire épuisée (agrandissement)");
        if (!stbir_resize_uint8_linear(src, w, h, 0, big, nw, nh, 0, STBIR_RGBA)) {
            tool_fatalf("agrandissement impossible");
        }
        stbi_image_free(src);
        src = big; w = nw; h = nh;
    }

    const size_t n = (size_t)w * (size_t)h;
    float *height = (float *)malloc(sizeof(float) * n);
    float *blurred = (float *)malloc(sizeof(float) * n);
    if (!height || !blurred) tool_fatalf("mémoire épuisée (cartes intermédiaires)");

    for (size_t i = 0; i < n; ++i) height[i] = luminance(&src[i * 4]);
    box_blur(height, blurred, w, h, opt.blur_radius);

    unsigned char *normal = (unsigned char *)malloc(n * 3);
    unsigned char *orm    = (unsigned char *)malloc(n * 3);
    if (!normal || !orm) tool_fatalf("mémoire épuisée (cartes de sortie)");

    /* La rugosité dépend de la variance locale, donc d'une mesure globale de
     * référence : sans normalisation, une texture uniformément bruitée et une
     * texture uniformément lisse recevraient la même carte. On fait donc une
     * première passe pour connaître l'échelle de contraste de cette image. */
    float variance_sum = 0.0f, variance_max = 0.0f;
    float *variance = (float *)malloc(sizeof(float) * n);
    if (!variance) tool_fatalf("mémoire épuisée (variance)");

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * (size_t)w + (size_t)x;
            const float c = height[i];
            float acc = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const float d = sample_height(height, w, h, x + dx, y + dy) - c;
                    acc += d * d;
                }
            }
            const float v = sqrtf(acc / 9.0f);
            variance[i] = v;
            variance_sum += v;
            if (v > variance_max) variance_max = v;
        }
    }
    const float variance_mean = variance_sum / (float)n;
    /* Échelle robuste : la moyenne plutôt que le maximum, pour qu'un seul pixel
     * aberrant ne compresse pas toute la plage. */
    const float variance_scale = (variance_mean > 1e-5f) ? (1.0f / (variance_mean * 3.0f)) : 1.0f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * (size_t)w + (size_t)x;

            /* --- normale : Sobel sur la hauteur --- */
            const float tl = sample_height(height, w, h, x - 1, y - 1);
            const float t  = sample_height(height, w, h, x,     y - 1);
            const float tr = sample_height(height, w, h, x + 1, y - 1);
            const float l  = sample_height(height, w, h, x - 1, y);
            const float r  = sample_height(height, w, h, x + 1, y);
            const float bl = sample_height(height, w, h, x - 1, y + 1);
            const float b  = sample_height(height, w, h, x,     y + 1);
            const float br = sample_height(height, w, h, x + 1, y + 1);

            const float dx = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
            const float dy = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);

            float nx = -dx * opt.normal_strength;
            float ny = -dy * opt.normal_strength;
            float nz = 1.0f;
            const float len = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= len; ny /= len; nz /= len;

            normal[i * 3 + 0] = (unsigned char)(clamp01(nx * 0.5f + 0.5f) * 255.0f + 0.5f);
            normal[i * 3 + 1] = (unsigned char)(clamp01(ny * 0.5f + 0.5f) * 255.0f + 0.5f);
            normal[i * 3 + 2] = (unsigned char)(clamp01(nz * 0.5f + 0.5f) * 255.0f + 0.5f);

            /* --- occlusion de cavité ---
             * Un pixel nettement plus sombre que son voisinage flouté est,
             * perceptuellement, un creux : c'est là que la lumière ambiante
             * pénètre le moins. */
            const float cavity = clamp01(1.0f - (blurred[i] - height[i]) * 4.0f * opt.ao_strength);

            /* --- rugosité ---
             * Une zone à fort micro-contraste diffuse la lumière ; une zone
             * lisse la réfléchit. On mappe donc la variance locale sur
             * l'intervalle de rugosité demandé. */
            const float rough = opt.rough_min
                              + (opt.rough_max - opt.rough_min) * clamp01(variance[i] * variance_scale);

            /* --- métallicité ---
             * Indécidable depuis une photo diffuse : on écrit 0, et les rares
             * matériaux métalliques sont marqués dans le glTF par obj2gltf, qui
             * dispose lui du terme spéculaire du MTL. Écrire une valeur inventée
             * ici serait pire que ne rien dire. */
            orm[i * 3 + 0] = (unsigned char)(cavity * 255.0f + 0.5f);
            orm[i * 3 + 1] = (unsigned char)(clamp01(rough) * 255.0f + 0.5f);
            orm[i * 3 + 2] = 0;
        }
    }

    char base[256], out_n[768], out_orm[768];
    tool_basename_noext(in_path, base, sizeof base);
    snprintf(out_n,   sizeof out_n,   "%s/%s_n.png", out_dir, base);
    snprintf(out_orm, sizeof out_orm, "%s/%s_orm.png", out_dir, base);

    if (!stbi_write_png(out_n, w, h, 3, normal, w * 3))   tool_fatalf("écriture impossible : %s", out_n);
    if (!stbi_write_png(out_orm, w, h, 3, orm, w * 3))    tool_fatalf("écriture impossible : %s", out_orm);

    printf("texgen %s : %dx%d, contraste moyen %.4f\n", base, w, h, (double)variance_mean);
    tool_infof("%s", out_n);
    tool_infof("%s", out_orm);

    stbi_image_free(src);
    free(height); free(blurred); free(variance); free(normal); free(orm);
    return 0;
}
