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
#include "nstex.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

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
    float albedo;            /* réflectance linéaire visée ; 0 = pas de dé-cuisson */
    float albedo_contrast;   /* compression du contraste vers la moyenne */
    int   max_map;           /* côté maximal des CARTES ; 0 = aucun plafond */
    bool  bc;                /* écrire les cartes en blocs (.nstex) */
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


/* ==========================================================================
 * Compression par blocs
 * ========================================================================== */

/*
 * Réduction d'un facteur deux, par moyenne de boîte, sur trois canaux.
 *
 * Les mips d'une texture compressée doivent être DANS le fichier : aucune API
 * ne sait générer une mip sur une texture bloc par blit, et le moteur génère
 * les siennes ainsi. Les calculer ici est de toute façon meilleur — on filtre
 * l'image d'origine plutôt qu'un niveau déjà compressé, donc l'erreur ne
 * s'accumule pas de niveau en niveau.
 *
 * `renormalise` renormalise le vecteur après moyennage : indispensable pour une
 * carte de normales, faux pour un ORM dont les trois canaux sont indépendants.
 */
static void halve_rgb(const unsigned char *src, int sw, int sh,
                      unsigned char *dst, int dw, int dh, bool renormalise)
{
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            int acc[3] = { 0, 0, 0 }, n = 0;
            for (int by = 0; by < 2; ++by) {
                const int sy = y * 2 + by;
                if (sy >= sh) break;
                for (int bx = 0; bx < 2; ++bx) {
                    const int sx = x * 2 + bx;
                    if (sx >= sw) break;
                    const size_t o = ((size_t)sy * sw + sx) * 3;
                    for (int c = 0; c < 3; ++c) acc[c] += src[o + c];
                    n++;
                }
            }
            const size_t d = ((size_t)y * dw + x) * 3;
            if (renormalise) {
                float v[3];
                for (int c = 0; c < 3; ++c) v[c] = (float)acc[c] / (float)n / 127.5f - 1.0f;
                const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (len > 1e-6f) for (int c = 0; c < 3; ++c) v[c] /= len;
                for (int c = 0; c < 3; ++c) {
                    dst[d + c] = (unsigned char)((v[c] * 0.5f + 0.5f) * 255.0f + 0.5f);
                }
            } else {
                for (int c = 0; c < 3; ++c) dst[d + c] = (unsigned char)(acc[c] / n);
            }
        }
    }
}

/*
 * Compresse un niveau RGB en BC1 ou BC5.
 *
 * `stb_dxt` travaille par bloc de 4x4 en RGBA. Les blocs de bordure d'une
 * image dont le côté n'est pas multiple de 4 sont complétés en RÉPÉTANT le
 * dernier texel plutôt qu'en remplissant de noir : un bloc à moitié noir
 * déborde sur les texels valides du même bloc, et ça se voit comme un liseré
 * sombre sur le bord de chaque texture.
 */
static void compress_level(const unsigned char *rgb, int w, int h,
                           uint32_t format, unsigned char *out)
{
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const uint32_t bb = nstex_block_bytes(format);

    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            unsigned char block[64];
            for (int y = 0; y < 4; ++y) {
                int sy = by * 4 + y; if (sy >= h) sy = h - 1;
                for (int x = 0; x < 4; ++x) {
                    int sx = bx * 4 + x; if (sx >= w) sx = w - 1;
                    const size_t o = ((size_t)sy * w + sx) * 3;
                    unsigned char *t = &block[(y * 4 + x) * 4];
                    t[0] = rgb[o + 0]; t[1] = rgb[o + 1]; t[2] = rgb[o + 2]; t[3] = 255;
                }
            }
            unsigned char *dst = out + ((size_t)by * bw + bx) * bb;
            if (format == NSTEX_FORMAT_BC5) {
                /* BC5 = deux BC4 côte à côte : X puis Y. On extrait chaque
                 * canal dans un tampon de seize octets, ce que `stb` attend. */
                unsigned char ch[16];
                for (int i = 0; i < 16; ++i) ch[i] = block[i * 4 + 0];
                stb_compress_bc4_block(dst, ch);
                for (int i = 0; i < 16; ++i) ch[i] = block[i * 4 + 1];
                stb_compress_bc4_block(dst + 8, ch);
            } else {
                /* `alpha = 0` demande du BC1 (pas de canal alpha), ce qui est
                 * ce qu'on veut pour un ORM. */
                stb_compress_dxt_block(dst, block, 0, STB_DXT_HIGHQUAL);
            }
        }
    }
}

/*
 * Écrit une carte en `.nstex`, chaîne de mips comprise.
 *
 * Renvoie le nombre d'octets écrits, pour que l'appelant puisse le dire — un
 * gain qu'on annonce sans le mesurer n'est pas un gain.
 */
static size_t write_nstex(const char *path, const unsigned char *rgb, int w, int h,
                          uint32_t format, bool renormalise)
{
    /* Combien de niveaux : jusqu'au 1x1. On ne s'arrête pas au bloc de 4x4 —
     * un niveau 2x2 occupe un bloc entier et coûte huit octets, et l'absence
     * des derniers niveaux se voit sur une surface vue en enfilade. */
    int levels = 1;
    for (int lw = w, lh = h; lw > 1 || lh > 1; ++levels) {
        lw = (lw > 1) ? lw / 2 : 1;
        lh = (lh > 1) ? lh / 2 : 1;
    }

    size_t total = 0;
    for (int i = 0, lw = w, lh = h; i < levels; ++i) {
        total += nstex_level_bytes((uint32_t)lw, (uint32_t)lh, format);
        lw = (lw > 1) ? lw / 2 : 1;
        lh = (lh > 1) ? lh / 2 : 1;
    }

    unsigned char *blocks = (unsigned char *)malloc(total);
    unsigned char *cur = (unsigned char *)malloc((size_t)w * h * 3);
    unsigned char *next = (unsigned char *)malloc((size_t)w * h * 3);
    if (!blocks || !cur || !next) tool_fatalf("texgen : mémoire (nstex)");
    memcpy(cur, rgb, (size_t)w * h * 3);

    size_t off = 0;
    int lw = w, lh = h;
    for (int i = 0; i < levels; ++i) {
        compress_level(cur, lw, lh, format, blocks + off);
        off += nstex_level_bytes((uint32_t)lw, (uint32_t)lh, format);

        const int nw = (lw > 1) ? lw / 2 : 1, nh = (lh > 1) ? lh / 2 : 1;
        if (i + 1 < levels) {
            halve_rgb(cur, lw, lh, next, nw, nh, renormalise);
            unsigned char *swap = cur; cur = next; next = swap;
        }
        lw = nw; lh = nh;
    }

    unsigned char head[NSTEX_HEADER_BYTES];
    head[0] = NSTEX_MAGIC0; head[1] = NSTEX_MAGIC1;
    head[2] = NSTEX_MAGIC2; head[3] = NSTEX_MAGIC3;
    head[4] = (unsigned char)NSTEX_VERSION;
    head[5] = (unsigned char)format;
    head[6] = (unsigned char)(levels & 0xFF);
    head[7] = (unsigned char)((levels >> 8) & 0xFF);
    for (int i = 0; i < 4; ++i) {
        head[8 + i]  = (unsigned char)(((uint32_t)w >> (i * 8)) & 0xFF);
        head[12 + i] = (unsigned char)(((uint32_t)h >> (i * 8)) & 0xFF);
        head[16 + i] = (unsigned char)(((uint32_t)total >> (i * 8)) & 0xFF);
    }

    FILE *f = fopen(path, "wb");
    if (!f) tool_fatalf("écriture impossible : %s", path);
    if (fwrite(head, 1, sizeof head, f) != sizeof head
        || fwrite(blocks, 1, total, f) != total) {
        fclose(f);
        tool_fatalf("écriture incomplète : %s", path);
    }
    fclose(f);

    free(blocks); free(cur); free(next);
    return sizeof head + total;
}

int main(int argc, char **argv)
{
    options opt = {
        .normal_strength = 1.4f,
        .rough_min = 0.28f,
        .rough_max = 0.92f,
        .ao_strength = 1.0f,
        .upscale = 1,
        .blur_radius = 4,
        .albedo = 0.0f,
        .albedo_contrast = 0.70f,
        .max_map = 0,
        .bc = false,
    };

    const char *in_path = NULL, *out_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--strength=", 11) == 0)      opt.normal_strength = (float)atof(argv[i] + 11);
        else if (strncmp(argv[i], "--upscale=", 10) == 0)  opt.upscale = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--rough-min=", 12) == 0) opt.rough_min = (float)atof(argv[i] + 12);
        else if (strncmp(argv[i], "--rough-max=", 12) == 0) opt.rough_max = (float)atof(argv[i] + 12);
        else if (strncmp(argv[i], "--ao=", 5) == 0)        opt.ao_strength = (float)atof(argv[i] + 5);
        else if (strncmp(argv[i], "--blur=", 7) == 0)      opt.blur_radius = atoi(argv[i] + 7);
        else if (strncmp(argv[i], "--max-map=", 10) == 0)  opt.max_map = atoi(argv[i] + 10);
        else if (strcmp(argv[i], "--bc") == 0)             opt.bc = true;
        else if (strncmp(argv[i], "--albedo=", 9) == 0)    opt.albedo = (float)atof(argv[i] + 9);
        else if (strncmp(argv[i], "--albedo-contrast=", 18) == 0)
            opt.albedo_contrast = (float)atof(argv[i] + 18);
        else if (!in_path)  in_path = argv[i];
        else if (!out_dir)  out_dir = argv[i];
    }

    if (!in_path || !out_dir) {
        fprintf(stderr,
            "texgen — dérive normal map et ORM d'une texture diffuse\n"
            "usage : %s [options] <image> <répertoire de sortie>\n"
            "  --strength=F   amplitude du relief (défaut 1.4)\n"
            "  --upscale=N    agrandissement Mitchell avant traitement (défaut 1)\n"
            "  --rough-min=F  rugosité des zones lisses (défaut 0.28)\n"
            "  --rough-max=F  rugosité des zones texturées (défaut 0.92)\n"
            "  --ao=F         intensité de l'occlusion de cavité (défaut 1.0)\n"
            "  --blur=N       rayon de la référence basse fréquence (défaut 4)\n"
            "  --max-map=N    côté maximal des cartes _n et _orm (0 = aucun plafond)\n"
            "  --bc           écrit les cartes en blocs compressés (.nstex) au lieu de PNG\n"
            "  --albedo=F     réflectance linéaire visée : produit <nom>_c.png\n"
            "                 (0, le défaut, ne produit rien — voir la dé-cuisson)\n"
            "  --albedo-contrast=F  compression du contraste (défaut 0.70)\n", argv[0]);
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

    /*
     * LE PLAFOND DES CARTES, et pourquoi il n'y en avait pas.
     *
     * Les cartes étaient écrites à la résolution de l'ALBÉDO, quelle qu'elle
     * soit — d'où des normales en 3840 x 2160 pour l'image d'un écran de jeu,
     * et en 2048² pour le flanc d'une radio. Sur les 421 Mio de l'archive de
     * release, 330 sont ces deux familles de cartes.
     *
     * Une carte de normales et une carte ORM ne portent PAS la même information
     * qu'un albédo. L'albédo porte le dessin — le lettrage d'un marquee, la
     * trame d'une moquette — et se lit de près ; les cartes portent une
     * variation de pente et de rugosité que l'éclairage intègre sur plusieurs
     * pixels. Les rendre à la moitié du côté est invisible ; les rendre à
     * 3840 de large est du gaspillage pur, quatorze fois.
     *
     * On plafonne donc les cartes SEULES. L'albédo dé-cuit (`_c`) garde sa
     * résolution : c'est lui qu'on regarde.
     *
     * Le filtre est une moyenne de boîte sur des blocs entiers. Pour une
     * normale, moyenner puis renormaliser est correct — c'est la même chose
     * que d'aplatir légèrement le relief, ce que fait n'importe quel niveau de
     * mipmap. Pour l'ORM, ce sont trois scalaires : la moyenne est exactement
     * ce qu'il faut.
     */
    int mw = w, mh = h;
    unsigned char *map_n = normal, *map_orm = orm;
    unsigned char *down_n = NULL, *down_orm = NULL;
    if (opt.max_map > 0 && (w > opt.max_map || h > opt.max_map)) {
        int step = 1;
        while ((w + step - 1) / (step + 1) > 0
               && ((w + step) / (step + 1) > opt.max_map
                   || (h + step) / (step + 1) > opt.max_map)) {
            step++;
            if (step > 64) break;
        }
        step++;                       /* le facteur, pas l'indice */
        mw = (w + step - 1) / step;
        mh = (h + step - 1) / step;
        down_n   = (unsigned char *)malloc((size_t)mw * mh * 3);
        down_orm = (unsigned char *)malloc((size_t)mw * mh * 3);
        if (!down_n || !down_orm) tool_fatalf("texgen : mémoire (réduction)");

        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < mw; ++x) {
                int acc_n[3] = { 0, 0, 0 }, acc_o[3] = { 0, 0, 0 }, taken = 0;
                for (int by = 0; by < step; ++by) {
                    const int sy = y * step + by;
                    if (sy >= h) break;
                    for (int bx = 0; bx < step; ++bx) {
                        const int sx = x * step + bx;
                        if (sx >= w) break;
                        const size_t o = ((size_t)sy * w + sx) * 3;
                        for (int c = 0; c < 3; ++c) {
                            acc_n[c] += normal[o + c];
                            acc_o[c] += orm[o + c];
                        }
                        taken++;
                    }
                }
                const size_t d = ((size_t)y * mw + x) * 3;
                /* Renormalisation de la normale moyennée : sans elle, une pente
                 * moyennée se raccourcit et la surface s'aplatit deux fois. */
                float nx = (float)acc_n[0] / (float)taken / 127.5f - 1.0f;
                float ny = (float)acc_n[1] / (float)taken / 127.5f - 1.0f;
                float nz = (float)acc_n[2] / (float)taken / 127.5f - 1.0f;
                const float len = sqrtf(nx * nx + ny * ny + nz * nz);
                if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
                down_n[d + 0] = (unsigned char)((nx * 0.5f + 0.5f) * 255.0f + 0.5f);
                down_n[d + 1] = (unsigned char)((ny * 0.5f + 0.5f) * 255.0f + 0.5f);
                down_n[d + 2] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
                for (int c = 0; c < 3; ++c) {
                    down_orm[d + c] = (unsigned char)(acc_o[c] / taken);
                }
            }
        }
        map_n = down_n; map_orm = down_orm;
    }

    if (opt.bc) {
        /* Le PNG est remplacé, pas doublé : garder les deux annulerait le gain
         * et laisserait deux vérités pour la même carte. */
        char bc_n[768], bc_orm[768];
        snprintf(bc_n,   sizeof bc_n,   "%s/%s_n.nstex", out_dir, base);
        snprintf(bc_orm, sizeof bc_orm, "%s/%s_orm.nstex", out_dir, base);
        const size_t bn = write_nstex(bc_n, map_n, mw, mh, NSTEX_FORMAT_BC5, true);
        const size_t bo = write_nstex(bc_orm, map_orm, mw, mh, NSTEX_FORMAT_BC1, false);
        printf("texgen %s : %dx%d cartes en blocs, %.2f + %.2f Mio\n",
               base, mw, mh, (double)bn / 1048576.0, (double)bo / 1048576.0);
    } else {
        if (!stbi_write_png(out_n, mw, mh, 3, map_n, mw * 3))   tool_fatalf("écriture impossible : %s", out_n);
        if (!stbi_write_png(out_orm, mw, mh, 3, map_orm, mw * 3)) tool_fatalf("écriture impossible : %s", out_orm);
    }
    free(down_n); free(down_orm);

    /* ======================================================================
     * La dé-cuisson : rendre une couleur de base à une texture qui n'en a pas
     * ======================================================================
     *
     * Le vrai défaut, et il n'était pas dans l'éclairage. Mesuré sur les
     * sources de 2020 : la moquette a une réflectance linéaire de **0,029**,
     * le plafond de **0,002**, les poutres de 0,008. Du bitume frais réfléchit
     * 4 %. Le plafond de cette salle d'arcade était donc, littéralement, plus
     * noir que n'importe quel matériau de construction existant.
     *
     * Ce n'est pas une faute de l'auteur de 2020 : son moteur n'éclairait
     * rien. `SDL_RenderCopy` affichait la texture telle quelle, donc la
     * texture DEVAIT déjà ressembler à une salle tamisée — l'ombre était
     * peinte dedans. Un moteur PBR reprend cette texture et l'éclaire : la
     * pénombre est appliquée DEUX FOIS, et aucune quantité de lumière ne
     * rattrape une réflectance de 0,2 %.
     *
     * C'est ce qui explique en une ligne les deux reproches faits à l'image —
     * « les pièces sont trop sombres » et « les textures mal choisies » : c'est
     * le même défaut. Et c'est ce qui explique que les toilettes soient la
     * seule pièce lisible de la salle, leur faïence étant à 0,872.
     *
     * La conversion, en trois temps, et aucun n'est décoratif :
     *
     * 1. **Passage en linéaire.** Une moyenne calculée sur des octets sRGB ne
     *    veut rien dire : sRGB est perceptuel, et doubler la valeur d'un octet
     *    ne double pas la lumière renvoyée.
     * 2. **Recentrage sur la réflectance visée.** La moyenne linéaire de
     *    l'image est amenée à `--albedo`, qui est une propriété PHYSIQUE du
     *    matériau — 0,10 pour une moquette sombre, 0,55 pour une dalle de
     *    plafond, 0,30 pour un béton.
     * 3. **Compression du contraste vers cette moyenne.** L'écart entre les
     *    pixels d'une photo de moquette contient l'ombre peinte autant que le
     *    motif ; à `--albedo-contrast=0.70` on garde la trame et on rend au
     *    moteur le soin de creuser les creux. Sans cette étape, un simple
     *    facteur d'échelle ferait saturer en blanc le décile supérieur — la
     *    texture deviendrait un aplat troué de taches.
     *
     * Un genou doux borne le résultat sous 1 : une réflectance supérieure à 1
     * n'existe pas, et écrêter à la place produirait des plages plates.
     */
    if (opt.albedo > 0.0f) {
        double mean_lin = 0.0;
        for (int i = 0; i < w * h; ++i) {
            const double r = powf(src[i * 4 + 0] / 255.0f, 2.2f);
            const double g = powf(src[i * 4 + 1] / 255.0f, 2.2f);
            const double b = powf(src[i * 4 + 2] / 255.0f, 2.2f);
            mean_lin += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        }
        mean_lin /= (double)(w * h);
        if (mean_lin < 1e-6) mean_lin = 1e-6;

        unsigned char *colour = (unsigned char *)malloc((size_t)w * h * 3);
        if (!colour) tool_fatalf("mémoire épuisée pour la couleur de base");

        const float c = (opt.albedo_contrast > 0.05f) ? opt.albedo_contrast : 0.05f;

        /*
         * DEUX passes, et la seconde n'est pas un raffinement : sans elle
         * `--albedo` ne veut pas dire ce qu'il annonce.
         *
         * La compression de contraste est une puissance, et la moyenne d'une
         * puissance n'est pas la puissance de la moyenne — l'inégalité de
         * Jensen, pas une approximation numérique. Sur le plafond, dont le
         * facteur d'échelle vaut 240, l'écart mesuré était de 0,55 demandé
         * pour 0,39 obtenu : 30 % de moins, sur la plus grande surface de la
         * salle. On mesure donc ce que la première passe a produit et on
         * corrige d'un facteur unique, ce qui ne change ni le motif ni le
         * contraste.
         */
        float gain = 1.0f;
        for (int pass = 0; pass < 2; ++pass) {
            double got = 0.0;
            for (int i = 0; i < w * h; ++i) {
                float lrgb[3];
                for (int k = 0; k < 3; ++k) {
                    const float lin = powf(src[i * 4 + k] / 255.0f, 2.2f);
                    const float ratio = powf((float)(lin / mean_lin) + 1e-6f, c);
                    float out = opt.albedo * ratio * gain;
                    /* Genou doux : linéaire jusqu'à 0,8, puis on tend vers 1.
                     * Une réflectance supérieure à 1 n'existe pas, et écrêter à
                     * la place produirait des plages plates. */
                    if (out > 0.8f) out = 0.8f + 0.2f * (1.0f - expf(-(out - 0.8f) / 0.2f));
                    if (out < 0.0f) out = 0.0f;
                    if (out > 1.0f) out = 1.0f;
                    lrgb[k] = out;
                    if (pass == 1) {
                        colour[i * 3 + k] =
                            (unsigned char)(powf(out, 1.0f / 2.2f) * 255.0f + 0.5f);
                    }
                }
                got += 0.2126 * lrgb[0] + 0.7152 * lrgb[1] + 0.0722 * lrgb[2];
            }
            got /= (double)(w * h);
            if (pass == 0) {
                if (got < 1e-6) got = 1e-6;
                gain = (float)(opt.albedo / got);
                /* Borné : au-delà, c'est que la texture ne PEUT pas porter cette
                 * réflectance sans devenir un aplat, et il vaut mieux le dire. */
                if (gain > 4.0f) {
                    gain = 4.0f;
                    tool_infof("%s : réflectance visée hors de portée, gain borné à 4", base);
                }
            }
        }

        char out_c[768];
        snprintf(out_c, sizeof out_c, "%s/%s_c.png", out_dir, base);
        if (!stbi_write_png(out_c, w, h, 3, colour, w * 3)) tool_fatalf("écriture impossible : %s", out_c);
        tool_infof("%s", out_c);
        printf("texgen %s : réflectance %.4f -> %.4f visée\n", base, mean_lin, (double)opt.albedo);
        free(colour);
    }

    printf("texgen %s : %dx%d, contraste moyen %.4f\n", base, w, h, (double)variance_mean);
    tool_infof("%s", out_n);
    tool_infof("%s", out_orm);

    stbi_image_free(src);
    free(height); free(blurred); free(variance); free(normal); free(orm);
    return 0;
}
