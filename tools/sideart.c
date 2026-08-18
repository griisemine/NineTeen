/*
 * sideart — l'art latéral d'une borne d'arcade, dessiné plutôt que téléchargé.
 *
 * Les vues de référence montrent, sur le flanc de la borne, un dégradé qui
 * s'éclaircit vers le haut et une trame de losanges en diagonale. C'est ce qui
 * distingue une borne d'arcade d'un caisson peint, et c'est la dernière chose
 * qui manquait à la silhouette refaite en B14.
 *
 * Pourquoi un générateur et pas un fichier
 * ----------------------------------------
 * Le modèle free3d demande un compte : le versionner casserait la promesse de
 * reconstructibilité hors ligne et la clarté des licences d'`assets/cc0`. Une
 * image dessinée par vingt lignes d'arithmétique n'a aucun de ces deux
 * problèmes, se régénère à l'identique sur les trois plateformes, et se règle
 * — le pas de la trame, l'épaisseur des lignes, le dégradé — au lieu d'être
 * subie.
 *
 * Ce qui est produit
 * ------------------
 * Une planche RVB en portrait, faite pour être TEINTÉE par le matériau de la
 * borne : le dégradé et la trame portent la forme, la couleur vient du jeu.
 * C'est ce qui permet de garder les dix-neuf bornes reconnaissables une par une
 * — on trouve « sa » borne à sa couleur, de loin — tout en donnant à chacune le
 * flanc de la référence.
 */
#include "tools_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Distance signée d'un point à la grille de losanges, en unités de pas. Une
 * trame en losanges est l'intersection de DEUX familles de bandes à ±45° : on
 * mesure la distance à chacune et on garde la plus proche. */
static float lattice(float x, float y, float step)
{
    const float a = fmodf((x + y) / step + 1024.0f, 1.0f);
    const float b = fmodf((x - y) / step + 1024.0f, 1.0f);
    const float da = fabsf(a - 0.5f) * 2.0f;   /* 0 au centre de la bande */
    const float db = fabsf(b - 0.5f) * 2.0f;
    return (da < db) ? da : db;
}

int main(int argc, char **argv)
{
    int   width = 512, height = 1024;
    float step = 96.0f;      /* pas de la trame, en pixels */
    float line = 0.10f;      /* épaisseur des lignes, en fraction du pas */
    float low = 0.16f;       /* luminance en bas du dégradé */
    float high = 0.86f;      /* luminance en haut : on garde de la réserve pour
                              * la teinte du jeu ET pour que les lignes de la
                              * trame restent plus claires que le fond jusqu'en
                              * haut. À 1,0 le sommet saturait et la trame y
                              * disparaissait — le seul endroit où on la voit
                              * de loin. */
    const char *out_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--width=", 8) == 0)       width  = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--height=", 9) == 0) height = atoi(argv[i] + 9);
        else if (strncmp(argv[i], "--step=", 7) == 0)   step   = (float)atof(argv[i] + 7);
        else if (strncmp(argv[i], "--line=", 7) == 0)   line   = (float)atof(argv[i] + 7);
        else if (strncmp(argv[i], "--low=", 6) == 0)    low    = (float)atof(argv[i] + 6);
        else if (strncmp(argv[i], "--high=", 7) == 0)   high   = (float)atof(argv[i] + 7);
        else if (!out_path) out_path = argv[i];
    }
    if (!out_path) {
        fprintf(stderr,
            "sideart — dessine l'art latéral d'une borne d'arcade\n"
            "usage : %s [options] <sortie.png>\n"
            "  --width=N --height=N   dimensions (défaut 512x1024)\n"
            "  --step=F               pas de la trame en pixels (défaut 96)\n"
            "  --line=F               épaisseur des lignes, fraction du pas (défaut 0.10)\n"
            "  --low=F --high=F       dégradé bas/haut (défaut 0.16 / 0.86)\n", argv[0]);
        return 2;
    }
    if (width < 8 || height < 8 || step < 4.0f) tool_fatalf("dimensions invalides");

    unsigned char *px = (unsigned char *)malloc((size_t)width * height * 3);
    if (!px) tool_fatalf("mémoire épuisée");

    for (int y = 0; y < height; ++y) {
        /* Le dégradé monte vers le HAUT de la planche, donc vers y = 0. Une
         * borne d'arcade s'éclaircit vers son marquee : c'est ce qui la fait
         * paraître plus haute qu'elle n'est, et c'est ce que montrent les vues
         * de référence. */
        const float t = 1.0f - (float)y / (float)(height - 1);
        /* Courbe en S plutôt que rampe : une rampe linéaire se lit comme un
         * dégradé d'outil de dessin, pas comme une sérigraphie. */
        const float g = low + (high - low) * (t * t * (3.0f - 2.0f * t));

        for (int x = 0; x < width; ++x) {
            const float d = lattice((float)x, (float)y, step);
            /*
             * Les lignes de la trame sont plus CLAIRES que le fond, avec un bord
             * adouci sur un pixel : sans ce fondu, la diagonale crénèle et la
             * planche se lit comme un damier de pixels dès qu'on s'approche.
             */
            const float edge = clamp01((line - d) / (line * 0.45f));
            float v = g * (1.0f + 0.45f * edge);

            /* Un léger assombrissement sur le pourtour : c'est le bord de la
             * sérigraphie, et c'est ce qui empêche le flanc de paraître découpé
             * dans du papier. */
            const float bx = (float)((x < width - 1 - x) ? x : (width - 1 - x));
            const float by = (float)((y < height - 1 - y) ? y : (height - 1 - y));
            const float border = clamp01(bx / 24.0f) * clamp01(by / 24.0f);
            v *= 0.55f + 0.45f * border;

            /* Un grain très fin, déterministe : une teinte parfaitement plate
             * révèle le banding sur un dégradé de mille pixels de haut. */
            const unsigned h1 = (unsigned)(x * 73856093u) ^ (unsigned)(y * 19349663u);
            const float grain = ((float)(h1 & 0xFFu) / 255.0f - 0.5f) * 0.018f;

            const float c = clamp01(v + grain);
            const size_t o = ((size_t)y * width + x) * 3;
            px[o + 0] = px[o + 1] = px[o + 2] = (unsigned char)(c * 255.0f + 0.5f);
        }
    }

    if (!stbi_write_png(out_path, width, height, 3, px, width * 3)) {
        tool_fatalf("écriture impossible : %s", out_path);
    }
    printf("sideart %s : %dx%d, trame %.0f px\n", out_path, width, height, (double)step);
    free(px);
    return 0;
}
