/*
 * murart — le crépi des murs, dessiné.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * Parce que `legacy/room/textures/mur_gris.jpg` habille 197 m² — la plus grande
 * surface VERTICALE de la salle, celle qui est derrière chaque borne et dans
 * chaque capture — et qu'elle ne se lit pas comme un mur. Deux mesures le
 * disent, et une capture les confirme.
 *
 * LA MESURE DE GRAIN. L'image fait 600 px pour 0,70 m de mur, donc 1,17 mm par
 * texel. `salle.room.json` justifie ce 0,70 en écrivant que « le grain visible
 * de trois a quatre texels » donne 3,5 mm, la cote d'un crépi écrasé. La
 * prémisse est fausse : ouverte et agrandie, la tache dominante de cette photo
 * fait dix à douze texels, donc 12 à 14 mm. Ce n'est pas un crépi, c'est un
 * enduit tyrolien projeté — du gravier. La capture du couloir le montre sans
 * qu'on ait à mesurer : à un mètre, les murs sont couverts de pop-corn.
 *
 * LA MESURE DE CONTRASTE. Son écart-type de luminance vaut 32,3 sur 255. Une
 * peinture mate sur enduit ne varie pas comme ça en ALBÉDO : ce qu'on voit sur
 * un vrai mur est du RELIEF, c'est-à-dire de l'ombre, et l'ombre est produite
 * par la lumière de la pièce, pas peinte dans la texture. Un albédo à 32
 * d'écart-type rejoue la même ombre partout, y compris là où aucune lampe ne
 * la justifierait, et il la rejoue à une fréquence que l'écran ne peut pas
 * résoudre : à trois mètres, une tache de 12 mm tombe sous deux pixels et
 * devient du bruit de télévision. C'est ce que la vue « travée » montrait.
 *
 * Ce qui est gardé, et c'est l'essentiel
 * --------------------------------------
 * LA RÉFLECTANCE. Mesurée sur l'image remplacée : 0,3427 / 0,3338 / 0,2970 en
 * linéaire, soit 0,3245 de moyenne. `docs/DESIGN-SALLE.md` la liste parmi les
 * cinq textures « déjà correctes » qu'on ne dé-cuit pas. On vise donc EXACTEMENT
 * la même, canal par canal : l'équilibre lumineux de la salle a été réglé
 * contre ce chiffre, et le déplacer obligerait à tout reprendre. L'outil
 * l'imprime à chaque exécution — c'est la seule façon de ne pas le perdre.
 *
 * Comment c'est dessiné
 * ---------------------
 * En BRUIT DE VALEUR à plusieurs octaves, et le point important n'est pas le
 * bruit, c'est sa BANDE PASSANTE.
 *
 * Une photo a de l'énergie jusqu'au dernier texel : à Nyquist et au-delà. Aucun
 * filtrage ne la rattrape — c'est ce que le mip fait de son mieux, et le mip
 * fonctionne, la capture du couloir le prouve (le grain grossit correctement
 * quand on s'approche). Ce qui manque n'est pas un filtre, c'est une texture
 * dont le spectre s'arrête AVANT la limite. Ici la maille la plus fine fait
 * QUATRE texels : son énergie culmine à un quart de la fréquence
 * d'échantillonnage, une octave sous Nyquist, et l'interpolation en
 * `smoothstep` fait tomber ce qui reste au-dessus. Le mur ne peut donc plus
 * scintiller, par construction et non par réglage.
 *
 * LE SPECTRE EST EN U, et c'est ce qui fait qu'on lit un mur peint :
 *
 *   - la DENT du crépi, 3 à 6 mm, celle qu'on voit en s'approchant ;
 *   - un creux entre 2 et 10 cm, parce qu'un mur n'a rien à cette échelle-là ;
 *   - la MOUCHETURE du rouleau, 20 à 40 cm, celle qui empêche un mur vu de loin
 *     d'être un aplat de plastique.
 *
 * Une photo de mur a le spectre inverse — tout dans le fin, rien dans le large —
 * et c'est exactement pour ça qu'elle grésille de près et s'aplatit de loin.
 *
 * LA TRUELLE. Une octave est échantillonnée sur des coordonnées étirées : les
 * moucheture s'allongent légèrement, comme la trace d'une lisseuse. Sans elle,
 * le bruit isotrope se lit comme du bruit ; avec elle, comme un enduit appliqué.
 *
 * LA TEINTE. Le matériau `mur` multiplie déjà la texture par [0,52 ; 0,44 ;
 * 0,34]. La planche reste donc quasi neutre — mais pas tout à fait : les larges
 * octaves modulent très légèrement la chromaticité, parce qu'un enduit
 * parfaitement neutre sur toute sa surface est le signe le plus sûr d'une
 * texture générée.
 *
 * Le pavage
 * ---------
 * Chaque octave a une maille qui DIVISE le côté de l'image, et le hachage est
 * pris modulo le nombre de mailles : le bruit est périodique par construction,
 * donc l'image se pave sans couture. Sur un mur de 14,43 m à 0,80 m par
 * répétition, cela fait dix-huit carreaux — invisible, faute de motif à
 * reconnaître.
 */
#include "tools_common.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* --------------------------------------------------------------------------
 * Le bruit de valeur, périodique
 * -------------------------------------------------------------------------- */

/*
 * Un hachage entier, et pourquoi pas `rand()`.
 *
 * Le bruit doit être une FONCTION de la coordonnée de maille : la même case
 * doit rendre la même valeur qu'on la visite depuis l'octave, depuis son voisin
 * de droite ou depuis le bord opposé de l'image — c'est précisément ce qui rend
 * le pavage invisible. Un générateur à état ne peut pas faire ça.
 */
static float hash2(uint32_t x, uint32_t y, uint32_t graine)
{
    uint32_t h = x * 374761393u + y * 668265263u + graine * 2654435761u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)h * (1.0f / 4294967296.0f);
}

static float lisse(float t) { return t * t * (3.0f - 2.0f * t); }

/*
 * Une octave. `mailles` est le nombre de cases sur le côté ; il divise le côté
 * de l'image, donc la période du hachage est exactement l'image.
 */
static float octave(float u, float v, int mailles, uint32_t graine)
{
    const float fx = u * (float)mailles;
    const float fy = v * (float)mailles;
    const int   ix = (int)floorf(fx);
    const int   iy = (int)floorf(fy);
    const float tx = lisse(fx - (float)ix);
    const float ty = lisse(fy - (float)iy);

    const uint32_t m = (uint32_t)mailles;
    const uint32_t x0 = (uint32_t)((ix % mailles + mailles) % mailles);
    const uint32_t y0 = (uint32_t)((iy % mailles + mailles) % mailles);
    const uint32_t x1 = (x0 + 1u) % m;
    const uint32_t y1 = (y0 + 1u) % m;

    const float a = hash2(x0, y0, graine), b = hash2(x1, y0, graine);
    const float c = hash2(x0, y1, graine), d = hash2(x1, y1, graine);
    const float ab = a + (b - a) * tx;
    const float cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

/* --------------------------------------------------------------------------
 * Le spectre
 * -------------------------------------------------------------------------- */

/*
 * LA TABLE EST LE DESSIN. Chaque ligne donne la maille en TEXELS et le poids.
 * La colonne « monde » est la taille réelle du motif à 0,80 m par répétition
 * sur une planche de 1024 px, soit 0,781 mm par texel — c'est l'échelle pour
 * laquelle cette table est réglée, et la seule à laquelle ses chiffres veulent
 * dire quelque chose.
 *
 *   maille    monde     poids   ce que c'est
 *   ------    -----     -----   -------------------------------------------
 *      4     3,1 mm     1,00    la dent du crépi
 *      8     6,3 mm     0,85    la même, sa moitié basse
 *     16    12,5 mm     0,55    fin de la dent
 *     32     2,5 cm     0,34    le creux : un mur n'a rien à cette échelle
 *     64     5,0 cm     0,38    "
 *    128    10,0 cm     0,55    début de la moucheture
 *    256    20,0 cm     0,74    la trace du rouleau
 *    512    40,0 cm     0,90    l'inégalité large, celle qu'on voit de loin
 *
 * La maille de 4 texels est le PLANCHER, pas un réglage : c'est elle qui met le
 * sommet du spectre une octave sous Nyquist. La descendre à 2 rendrait la
 * planche aussi scintillante que la photo qu'elle remplace.
 */
typedef struct { int maille; float poids; } bande;

static const bande SPECTRE[] = {
    {   4, 1.00f }, {   8, 0.85f }, {  16, 0.55f }, {  32, 0.34f },
    {  64, 0.38f }, { 128, 0.55f }, { 256, 0.74f }, { 512, 0.90f },
};
#define SPECTRE_N ((int)(sizeof SPECTRE / sizeof SPECTRE[0]))

/* L'étirement de la truelle : les octaves larges sont échantillonnées sur un
 * repère allongé. 1,0 les laisserait isotropes. */
#define TRUELLE 2.6f

static float sRGB(float lineaire)
{
    if (lineaire < 0.0f) lineaire = 0.0f;
    if (lineaire > 1.0f) lineaire = 1.0f;
    return (lineaire <= 0.0031308f)
             ? lineaire * 12.92f
             : 1.055f * powf(lineaire, 1.0f / 2.4f) - 0.055f;
}

int main(int argc, char **argv)
{
    int cote = 1024;
    /* Les trois canaux de `mur_gris.jpg`, mesurés en linéaire sur l'image
     * entière. On ne vise pas une moyenne grise : la salle a été éclairée
     * contre CE triplet, dont la légère chaleur fait partie du réglage. */
    float cible[3] = { 0.3427f, 0.3338f, 0.2970f };
    /*
     * L'ECART-TYPE RELATIF de la reflectance. 0,10 n'est pas un gout : c'est
     * le chiffre qui place l'ecart-type de luminance sRGB a 7 sur 255, contre
     * 32,3 pour la photo remplacee. Le calcul se verifie a la main — la derivee
     * de l'encodage sRGB vaut 218 par unite de lineaire autour de 0,32, donc
     * 0,10 x 0,32 x 218 = 7,0 — et l'outil l'IMPRIME, ce qui est la seule
     * verification qui compte.
     *
     * Sept, et pas trente-deux : ce qu'on voit sur un vrai mur peint est du
     * RELIEF eclaire par la piece, pas une ombre peinte dans l'albedo. Le
     * relief, lui, sort de `texgen`, qui derive la normale de cette planche.
     */
    float ampleur = 0.10f;
    uint32_t graine = 19u;
    const char *sortie = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--cote=", 7) == 0)          cote = atoi(argv[i] + 7);
        else if (strncmp(argv[i], "--ampleur=", 10) == 0) ampleur = (float)atof(argv[i] + 10);
        else if (strncmp(argv[i], "--graine=", 9) == 0)   graine = (uint32_t)atoi(argv[i] + 9);
        else if (strncmp(argv[i], "--rouge=", 8) == 0)    cible[0] = (float)atof(argv[i] + 8);
        else if (strncmp(argv[i], "--vert=", 7) == 0)     cible[1] = (float)atof(argv[i] + 7);
        else if (strncmp(argv[i], "--bleu=", 7) == 0)     cible[2] = (float)atof(argv[i] + 7);
        else if (!sortie) sortie = argv[i];
        else tool_fatalf("argument inattendu : %s", argv[i]);
    }
    if (!sortie) {
        fprintf(stderr,
            "murart — dessine le crepi des murs de la salle\n"
            "usage : %s [options] <sortie.png>\n"
            "  --cote=N       cote de la planche, carree (defaut 1024)\n"
            "  --ampleur=F    ecart-type relatif de la reflectance (defaut 0.10)\n"
            "  --graine=N     graine du bruit (defaut 19)\n"
            "  --rouge/--vert/--bleu=F  reflectance LINEAIRE visee par canal\n"
            "                 (defaut 0.3427 0.3338 0.2970 : les trois canaux\n"
            "                 de mur_gris.jpg, mesures sur l'image entiere)\n",
            argv[0]);
        return 2;
    }
    if (cote < 256 || cote > 4096) tool_fatalf("cote hors bornes (%d)", cote);
    /* La maille la plus large doit diviser le côté : sinon le hachage n'est
     * plus périodique et le pavage montre une couture. */
    if (cote % SPECTRE[SPECTRE_N - 1].maille != 0) {
        tool_fatalf("le cote %d n'est pas un multiple de %d, la maille la plus "
                    "large : le bruit cesserait d'etre periodique et la planche "
                    "ne se paverait plus", cote, SPECTRE[SPECTRE_N - 1].maille);
    }

    const size_t n = (size_t)cote * (size_t)cote;
    float *champ = (float *)malloc(n * sizeof *champ);      /* le grain, centré */
    float *large = (float *)malloc(n * sizeof *large);      /* les seules larges */
    if (!champ || !large) tool_fatalf("mémoire épuisée");

    float poids_total = 0.0f;
    for (int o = 0; o < SPECTRE_N; ++o) poids_total += SPECTRE[o].poids;

    for (int y = 0; y < cote; ++y) {
        for (int x = 0; x < cote; ++x) {
            const float u = ((float)x + 0.5f) / (float)cote;
            const float v = ((float)y + 0.5f) / (float)cote;
            float somme = 0.0f, somme_large = 0.0f;
            for (int o = 0; o < SPECTRE_N; ++o) {
                const int m = SPECTRE[o].maille;
                if (m > cote) continue;
                /*
                 * La truelle ne touche QUE les octaves larges — au-delà de
                 * 64 texels. Étirer la dent du crépi la ferait lire comme un
                 * peigne, ce qui est un autre enduit et pas celui-là.
                 */
                float uu = u, vv = v;
                if (m >= 64) { uu = u * TRUELLE; }
                const float e = octave(uu, vv, m, graine + (uint32_t)o * 101u) - 0.5f;
                somme += e * SPECTRE[o].poids;
                if (m >= 128) somme_large += e * SPECTRE[o].poids;
            }
            champ[(size_t)y * (size_t)cote + (size_t)x] = somme / poids_total;
            large[(size_t)y * (size_t)cote + (size_t)x] = somme_large / poids_total;
        }
    }

    /*
     * LE CHAMP EST NORMALISE SUR SON ECART-TYPE MESURE, et non sur la somme des
     * poids. La difference n'est pas cosmetique : huit octaves independantes
     * dont on divise la somme par la somme des poids rendent un champ dont
     * l'ecart-type est celui d'une MOYENNE, donc plus petit d'un facteur voisin
     * de la racine du nombre d'octaves. Mesure de la premiere version : 1,3
     * d'ecart-type de luminance la ou on en voulait sept, c'est-a-dire un mur
     * parfaitement uni. En normalisant sur l'ecart-type reel, `--ampleur`
     * signifie enfin ce que son nom dit — la variation RELATIVE de reflectance,
     * un ecart-type — et le meme reglage donne le meme resultat quel que soit
     * le nombre de bandes qu'on met dans la table.
     */
    double sc = 0.0, sc2 = 0.0;
    for (size_t i = 0; i < n; ++i) { sc += champ[i]; sc2 += (double)champ[i] * champ[i]; }
    const double moy_c = sc / (double)n;
    const double ect_c = sqrt(sc2 / (double)n - moy_c * moy_c);
    if (ect_c < 1e-6) tool_fatalf("champ constant : le spectre ne produit rien");
    for (size_t i = 0; i < n; ++i) {
        champ[i] = (float)(((double)champ[i] - moy_c) / ect_c);
        large[i] = (float)((double)large[i] / ect_c);
    }

    unsigned char *px = (unsigned char *)malloc(n * 3u);
    if (!px) tool_fatalf("mémoire épuisée");

    double acc[3] = { 0.0, 0.0, 0.0 };
    double lum_somme = 0.0, lum_carre = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const float g = champ[i];
        for (int k = 0; k < 3; ++k) {
            /*
             * L'amplitude est RELATIVE à la cible du canal : un même écart
             * absolu sur un canal sombre et un canal clair déplacerait la
             * teinte au lieu de moduler la clarté.
             *
             * La modulation chromatique vaut 18 % de l'amplitude et suit les
             * seules octaves LARGES : les plages un peu plus chaudes ou plus
             * froides d'un enduit sont des plages, jamais un moucheté.
             */
            const float chroma = (k == 0 ? 1.0f : (k == 1 ? 0.0f : -1.0f))
                               * large[i] * ampleur * 0.18f;
            float lin = cible[k] * (1.0f + g * ampleur + chroma);
            if (lin < 0.0f) lin = 0.0f;
            acc[k] += (double)lin;
            px[i * 3u + (size_t)k] = (unsigned char)(sRGB(lin) * 255.0f + 0.5f);
        }
        const double l = 0.2126 * px[i * 3u] + 0.7152 * px[i * 3u + 1]
                       + 0.0722 * px[i * 3u + 2];
        lum_somme += l;
        lum_carre += l * l;
    }

    if (!stbi_write_png(sortie, cote, cote, 3, px, cote * 3))
        tool_fatalf("écriture impossible : %s", sortie);

    /*
     * LES DEUX CHIFFRES QUI COMPTENT, imprimés. Le premier est la réflectance
     * qu'il ne faut pas perdre ; le second est ce qu'on est venu corriger.
     * `mur_gris.jpg` valait 0,3427 / 0,3338 / 0,2970 et 32,3 d'écart-type.
     */
    const double moy = lum_somme / (double)n;
    const double ect = sqrt(lum_carre / (double)n - moy * moy);
    printf("murart : %dx%d, réflectance linéaire %.4f %.4f %.4f "
           "(moyenne %.4f), luminance sRGB %.1f, écart-type %.1f\n",
           cote, cote, acc[0] / (double)n, acc[1] / (double)n, acc[2] / (double)n,
           (acc[0] + acc[1] + acc[2]) / (3.0 * (double)n), moy, ect);
    tool_infof("%s", sortie);

    free(px); free(champ); free(large);
    return 0;
}
