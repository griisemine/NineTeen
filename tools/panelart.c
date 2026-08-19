/*
 * panelart — la sérigraphie du panneau de commande, dessinée plutôt que subie.
 *
 * Ce qu'elle remplace
 * -------------------
 * Les dix-neuf bornes portaient le même `bordeaux.jpg` de 2020 en pavage : un
 * APLAT rouge, sans bord, sans motif, sans repère. C'est la plus grande surface
 * que le joueur ait sous les yeux quand il joue — plus grande que la dalle — et
 * c'était la seule pièce de la borne à n'avoir aucun dessin.
 *
 * Pourquoi un générateur, encore
 * ------------------------------
 * Même raison qu'`art latéral` : le modèle de référence demande un compte, et le
 * versionner casserait la reconstructibilité hors ligne et la clarté des
 * licences d'`assets/cc0`. Vingt lignes d'arithmétique n'ont ni l'un ni l'autre
 * problème, se régénèrent à l'identique sur les trois plateformes, et se règlent.
 *
 * Le motif est celui du FLANC, resserré
 * -------------------------------------
 * La trame en losanges de `sideart` est reprise ici, à un pas trois fois plus
 * court et sur un fond plus sombre. Ce n'est pas une économie : c'est ce qui
 * fait qu'une borne se lit comme un objet dessiné d'un seul geste plutôt que
 * comme un assemblage de pièces qui ne se connaissent pas. Le flanc, le panneau
 * et le bandeau partagent une famille.
 *
 * Ce qui est produit
 * ------------------
 * Une planche RVB en paysage, en NIVEAUX DE GRIS, faite pour être teintée par le
 * matériau — exactement comme le flanc. La forme vient de la planche, la couleur
 * vient du jeu, et les dix-neuf bornes restent reconnaissables une par une.
 *
 * Le repère de la planche
 * -----------------------
 * `v = 0` est le bord ARRIÈRE du panneau (contre l'écran), `v = 1` le bord AVANT
 * (contre le ventre du joueur). C'est ce qui permet de poser la bande claire du
 * nez de panneau du bon côté : sur une vraie borne, l'arête qu'on touche est
 * usée et plus claire, pas celle du fond.
 */
#include "tools_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/*
 * Distance à la trame de losanges, en fraction du pas — la même fonction que
 * `sideart`, recopiée volontairement.
 *
 * La partager demanderait un troisième fichier pour six lignes d'arithmétique
 * que ni l'un ni l'autre ne changera jamais. Ce qui doit rester d'accord entre
 * les deux outils, c'est l'ANGLE (±45°) et la façon de mesurer, et ça se vérifie
 * en les mettant côte à côte — pas en les couplant.
 */
static float lattice(float x, float y, float step)
{
    const float a = fmodf((x + y) / step + 1024.0f, 1.0f);
    const float b = fmodf((x - y) / step + 1024.0f, 1.0f);
    const float da = fabsf(a - 0.5f) * 2.0f;
    const float db = fabsf(b - 0.5f) * 2.0f;
    return (da < db) ? da : db;
}

int main(int argc, char **argv)
{
    int   width = 1024, height = 256;
    float step = 34.0f;      /* pas de la trame, en pixels */
    float line = 0.13f;      /* épaisseur des lignes, en fraction du pas */
    float field = 0.17f;     /* le fond : sombre, pour que les boutons ressortent */
    float lift = 0.62f;      /* le nez de panneau, usé donc plus clair */
    const char *out_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--width=", 8) == 0)       width  = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--height=", 9) == 0) height = atoi(argv[i] + 9);
        else if (strncmp(argv[i], "--step=", 7) == 0)   step   = (float)atof(argv[i] + 7);
        else if (strncmp(argv[i], "--line=", 7) == 0)   line   = (float)atof(argv[i] + 7);
        else if (strncmp(argv[i], "--field=", 8) == 0)  field  = (float)atof(argv[i] + 8);
        else if (strncmp(argv[i], "--lift=", 7) == 0)   lift   = (float)atof(argv[i] + 7);
        else if (!out_path) out_path = argv[i];
    }
    if (!out_path) {
        fprintf(stderr,
            "panelart — dessine la sérigraphie d'un panneau de commande\n"
            "usage : %s [options] <sortie.png>\n"
            "  --width=N --height=N   dimensions (défaut 1024x256)\n"
            "  --step=F               pas de la trame en pixels (défaut 34)\n"
            "  --line=F               épaisseur des lignes, fraction du pas (défaut 0.13)\n"
            "  --field=F              valeur du fond (défaut 0.17)\n"
            "  --lift=F               valeur du nez de panneau (défaut 0.62)\n", argv[0]);
        return 2;
    }
    if (width < 8 || height < 8 || step < 4.0f) tool_fatalf("dimensions invalides");

    unsigned char *px = (unsigned char *)malloc((size_t)width * height * 3);
    if (!px) tool_fatalf("mémoire épuisée");

    for (int y = 0; y < height; ++y) {
        const float v = (float)y / (float)(height - 1);   /* 0 = arrière, 1 = avant */

        for (int x = 0; x < width; ++x) {
            const float u = (float)x / (float)(width - 1);

            /* Le fond, très légèrement plus clair vers l'avant : un panneau
             * incliné prend la lumière des plafonniers par son nez. */
            float c = field * (0.86f + 0.28f * v);

            /* La trame, plus claire que le fond, bord adouci sur un pixel. */
            const float d = lattice((float)x, (float)y, step);
            const float edge = clamp01((line - d) / (line * 0.45f));
            c *= 1.0f + 0.70f * edge;

            /*
             * Le NEZ DE PANNEAU : une bande claire sur le bord avant, celle que
             * les avant-bras usent. C'est le détail qui dit « on a joué ici », et
             * il n'existe que d'un côté — le mettre des deux ferait un cadre, ce
             * qui est le contraire de l'usure.
             */
            const float nose = clamp01((v - 0.80f) / 0.20f);
            c += lift * nose * nose;

            /*
             * Deux filets qui courent d'un bout à l'autre, sous la rangée de
             * boutons. Une sérigraphie de panneau en a presque toujours : c'est
             * ce qui donne une direction à une surface autrement muette.
             */
            for (int k = 0; k < 2; ++k) {
                const float at = (k == 0) ? 0.30f : 0.38f;
                const float w  = (k == 0) ? 0.030f : 0.016f;
                const float t  = clamp01(1.0f - fabsf(v - at) / w);
                c += 0.34f * t * t;
            }

            /*
             * Le bord de la sérigraphie : elle s'arrête avant l'arête, comme une
             * impression sur une tôle pliée. Sans ça le motif paraît découpé au
             * massicot et le panneau perd son épaisseur.
             */
            const float bx = (float)((x < width - 1 - x) ? x : (width - 1 - x));
            const float margin = clamp01(bx / 18.0f);
            c *= 0.42f + 0.58f * margin;

            /*
             * Une usure large et douce au CENTRE-AVANT, là où les mains passent.
             * Elle éclaircit très légèrement — une surface parfaitement régulière
             * sur soixante-dix centimètres se lit comme du plastique moulé.
             */
            const float wu = (u - 0.5f) * 2.0f;
            const float wear = clamp01(1.0f - (wu * wu + (1.0f - v) * (1.0f - v)) * 1.4f);
            c *= 1.0f + 0.10f * wear;

            /* Grain fin déterministe : sans lui, le dégradé du fond bande. */
            const unsigned h1 = (unsigned)(x * 73856093u) ^ (unsigned)(y * 19349663u);
            const float grain = ((float)(h1 & 0xFFu) / 255.0f - 0.5f) * 0.020f;

            const float f = clamp01(c + grain);
            const size_t o = ((size_t)y * width + x) * 3;
            px[o + 0] = px[o + 1] = px[o + 2] = (unsigned char)(f * 255.0f + 0.5f);
        }
    }

    if (!stbi_write_png(out_path, width, height, 3, px, width * 3)) {
        tool_fatalf("écriture impossible : %s", out_path);
    }
    printf("panelart %s : %dx%d, trame %.0f px\n", out_path, width, height, (double)step);
    free(px);
    return 0;
}
