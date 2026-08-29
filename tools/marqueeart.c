/*
 * marqueeart — l'enseigne d'une borne, dessinée.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * Huit des dix-neuf bornes affichaient « votre publicité ici ? contactez-nous »
 * au-dessus de leur écran. C'est un remplissage de 2020, et il tenait à une
 * raison simple : quatre jeux — Shooter, Démineur, Pac-Man, Piano — n'ont
 * jamais eu d'enseigne dessinée. Les quatre autres en ont une, donc huit bornes
 * portaient la pancarte du régisseur.
 *
 * Une salle d'arcade se lit d'abord par ses enseignes : on traverse l'allée en
 * lisant les noms au-dessus des écrans, et c'est comme ça qu'on choisit sa
 * borne. Huit pancartes identiques cassent exactement ce geste-là.
 *
 * Pourquoi dessinée plutôt que rapportée
 * --------------------------------------
 * Même raison que `sideart` et `panelart` : aucune source n'existe, il n'y a
 * donc rien à importer. Une planche produite par arithmétique n'a ni licence à
 * démêler ni fichier à retrouver, se régénère à l'identique sur les trois
 * plateformes, et se règle.
 *
 * La fonte est celle DU JEU — `engine/sprite/ns_font5x7.h`, la même que les
 * scores et le classement. Une enseigne dessinée avec une autre fonte se
 * remarque immédiatement : le marquee et le score d'une même borne ne se
 * ressembleraient plus.
 *
 * Ce qui est produit
 * ------------------
 * Une planche 4:1 en couleur, faite pour être vue RÉTROÉCLAIRÉE — c'est ce
 * qu'est un marquee : une transparence derrière laquelle il y a un tube. D'où
 * un fond qui n'est pas noir mais coloré et sombre, et des lettres qui portent
 * un cœur clair, un liseré et un halo.
 */
#include "tools_common.h"
#include "ns_font5x7.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Le glyphe d'un caractère, ou l'espace si la fonte ne le couvre pas. */
static const uint8_t *glyphe(char c)
{
    const int i = (int)(unsigned char)c - NS_FONT5X7_FIRST;
    if (i < 0 || i >= NS_FONT5X7_GLYPHS) return ns_font5x7[0];
    return ns_font5x7[i];
}

/*
 * Distance d'un pixel au TEXTE, en pixels, par recherche du plein le plus
 * proche.
 *
 * On pourrait dessiner les lettres puis flouter ; ça coûterait une passe de
 * flou séparable et ça donnerait un halo carré autour des pixels de la fonte,
 * parce qu'un flou de boîte ne connaît pas la forme. Mesurer la distance donne
 * un halo ROND autour de chaque trait, ce qui est ce que fait un tube.
 *
 * Le coût est celui d'une recherche locale : on ne regarde que les cellules de
 * fonte dans le rayon utile, jamais toute la planche.
 */
typedef struct plaque {
    const char *texte;
    int   len;
    float x0, y0;     /* coin haut-gauche du texte, en pixels */
    float cell;       /* côté d'un pixel de fonte */
    bool  neon;       /* tube fin sur panneau sombre, plutôt que plaque pleine */
} plaque;

/* Distance d'un point au segment [a, b]. Le classique, écrit une fois. */
static float dist_segment(float px, float py, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay;
    const float wx = px - ax, wy = py - ay;
    const float l2 = vx * vx + vy * vy;
    float t = (l2 > 1e-9f) ? (wx * vx + wy * vy) / l2 : 0.0f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return hypotf(px - (ax + vx * t), py - (ay + vy * t));
}

static float distance_au_texte(const plaque *p, float px, float py, float portee)
{
    float best = portee;
    const float inv = 1.0f / p->cell;
    /* Les colonnes de fonte concernées, et rien de plus. */
    const int c0 = (int)floorf((px - portee - p->x0) * inv);
    const int c1 = (int)ceilf((px + portee - p->x0) * inv);
    const int r0 = (int)floorf((py - portee - p->y0) * inv);
    const int r1 = (int)ceilf((py + portee - p->y0) * inv);

    for (int col = (c0 < 0 ? 0 : c0); col <= c1; ++col) {
        const int ch = col / (NS_FONT5X7_COLS + 1);
        const int sub = col % (NS_FONT5X7_COLS + 1);
        if (ch >= p->len) break;
        if (sub >= NS_FONT5X7_COLS) continue;      /* l'espacement entre lettres */
        const uint8_t bits = glyphe(p->texte[ch])[sub];
        if (!bits) continue;
        for (int row = (r0 < 0 ? 0 : r0); row <= r1 && row < NS_FONT5X7_ROWS; ++row) {
            if (!(bits & (1u << row))) continue;
            /* Le centre de la cellule pleine. */
            const float cx = p->x0 + ((float)col + 0.5f) * p->cell;
            const float cy = p->y0 + ((float)row + 0.5f) * p->cell;

            float d;
            if (p->neon) {
                /*
                 * LE SQUELETTE, et c'est ce qui distingue un néon d'une plaque.
                 *
                 * Un tube de verre fait douze millimètres : sur une enseigne de
                 * trois mètres, c'est un trait BEAUCOUP plus fin qu'une cellule
                 * de fonte. Prendre la distance à la cellule pleine donne un
                 * trait large d'une cellule entière — et avec le halo d'un
                 * néon par-dessus, les huit lettres fusionnent en une barre
                 * blanche. Mesuré : c'est exactement ce qu'on obtenait.
                 *
                 * On mesure donc la distance à la LIGNE MOYENNE : le centre de
                 * la cellule, plus les segments qui le joignent à ses voisines
                 * allumées. Un tube quelconque en sort, d'épaisseur choisie et
                 * continu là où les cellules se touchent — ce qu'un simple
                 * chapelet de disques ne donnerait pas.
                 */
                d = hypotf(px - cx, py - cy);
                /* Vers la droite : la voisine peut être dans la lettre suivante
                 * si l'espacement l'y met, mais alors elle n'est pas allumée —
                 * `sub >= COLS` a déjà écarté la colonne d'espacement. */
                if (sub + 1 < NS_FONT5X7_COLS && (glyphe(p->texte[ch])[sub + 1] & (1u << row))) {
                    const float e = dist_segment(px, py, cx, cy, cx + p->cell, cy);
                    if (e < d) d = e;
                }
                if (row + 1 < NS_FONT5X7_ROWS && (bits & (1u << (row + 1)))) {
                    const float e = dist_segment(px, py, cx, cy, cx, cy + p->cell);
                    if (e < d) d = e;
                }
                /*
                 * Les DIAGONALES, et sans elles le tube se casse.
                 *
                 * Une fonte 5x7 fait ses obliques en ESCALIER : les cellules
                 * d'un jambage de « N » ou de « M » ne se touchent que par le
                 * coin. Joindre seulement la droite et le bas laisse donc un
                 * trou à chaque marche — parfaitement visible sur le premier
                 * essai, où le N et le M étaient coupés en trois morceaux.
                 *
                 * On ne joint la diagonale QUE si les deux cellules
                 * orthogonales sont éteintes : sinon on doublerait un coin déjà
                 * plein et on l'épaissirait, ce qui donne un renflement à
                 * chaque angle droit.
                 */
                if (row + 1 < NS_FONT5X7_ROWS && sub + 1 < NS_FONT5X7_COLS) {
                    const uint8_t droite = glyphe(p->texte[ch])[sub + 1];
                    const bool diag = (droite & (1u << (row + 1))) != 0;
                    const bool orth = ((bits & (1u << (row + 1))) != 0)
                                   || ((droite & (1u << row)) != 0);
                    if (diag && !orth) {
                        const float e = dist_segment(px, py, cx, cy,
                                                     cx + p->cell, cy + p->cell);
                        if (e < d) d = e;
                    }
                }
                if (row > 0 && sub + 1 < NS_FONT5X7_COLS) {
                    const uint8_t droite = glyphe(p->texte[ch])[sub + 1];
                    const bool diag = (droite & (1u << (row - 1))) != 0;
                    const bool orth = ((bits & (1u << (row - 1))) != 0)
                                   || ((droite & (1u << row)) != 0);
                    if (diag && !orth) {
                        const float e = dist_segment(px, py, cx, cy,
                                                     cx + p->cell, cy - p->cell);
                        if (e < d) d = e;
                    }
                }
            } else {
                /* Distance à la CELLULE (un carré), pas à son centre : sans ça
                 * les traits épais se creusent en chapelet de disques. */
                const float dx = fabsf(px - cx) - p->cell * 0.5f;
                const float dy = fabsf(py - cy) - p->cell * 0.5f;
                const float ax = dx > 0.0f ? dx : 0.0f;
                const float ay = dy > 0.0f ? dy : 0.0f;
                d = sqrtf(ax * ax + ay * ay);
            }
            if (d < best) best = d;
        }
    }
    return best;
}

int main(int argc, char **argv)
{
    int width = 1024, height = 256;
    const char *titre = NULL, *out_path = NULL;
    bool neon = false;
    float hue[3] = { 1.00f, 0.42f, 0.12f };   /* la teinte du gaz */

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--titre=", 8) == 0)        titre = argv[i] + 8;
        else if (strncmp(argv[i], "--width=", 8) == 0)   width = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--height=", 9) == 0)  height = atoi(argv[i] + 9);
        else if (strncmp(argv[i], "--teinte=", 9) == 0) {
            if (sscanf(argv[i] + 9, "%f,%f,%f", &hue[0], &hue[1], &hue[2]) != 3) {
                tool_fatalf("--teinte attend r,g,b entre 0 et 1");
            }
        }
        else if (strcmp(argv[i], "--neon") == 0)         neon = true;
        else if (!out_path) out_path = argv[i];
    }
    if (!titre || !out_path) {
        fprintf(stderr,
            "marqueeart — dessine l'enseigne d'une borne d'arcade\n"
            "usage : %s --titre=NOM [options] <sortie.png>\n"
            "  --titre=NOM       le nom du jeu, en majuscules\n"
            "  --teinte=r,g,b    la couleur du gaz (défaut 1,0.42,0.12)\n"
            "  --width=N --height=N  dimensions (défaut 1024x256)\n"
            "  --neon            tube de verre nu sur panneau sombre, et non\n"
            "                    plaque de plexiglas rétroéclairée\n", argv[0]);
        return 2;
    }
    if (width < 64 || height < 32) tool_fatalf("planche trop petite");

    const int len = (int)strlen(titre);
    if (len < 1 || len > 16) tool_fatalf("titre de 1 à 16 caractères, pas %d", len);

    /*
     * La taille du texte est CALCULÉE, pas choisie.
     *
     * Le nom le plus long — DEMINEUR, huit lettres — et le plus court — PIANO,
     * cinq — doivent remplir la même planche de la même façon, sinon la rangée
     * de bornes donne l'impression que certaines enseignes ont raté leur
     * impression. On cadre donc sur la largeur utile, avec une réserve pour le
     * halo, et on plafonne par la hauteur.
     */
    const float marge = (float)width * 0.075f;
    const float largeur_utile = (float)width - 2.0f * marge;
    const int colonnes = len * (NS_FONT5X7_COLS + 1) - 1;
    float cell = largeur_utile / (float)colonnes;
    const float hauteur_max = (float)height * 0.46f / (float)NS_FONT5X7_ROWS;
    if (cell > hauteur_max) cell = hauteur_max;

    plaque p;
    p.texte = titre;
    p.len = len;
    p.cell = cell;
    p.x0 = ((float)width - (float)colonnes * cell) * 0.5f;
    p.y0 = ((float)height - (float)NS_FONT5X7_ROWS * cell) * 0.5f;
    p.neon = neon;

    /*
     * DEUX objets differents, et il fallait les distinguer.
     *
     * Un MARQUEE de borne est une plaque de plexiglas retroeclairee : toute la
     * plaque s'allume, le lettrage est epais, et le fond garde la teinte du jeu.
     * C'est ce que ce programme faisait, et c'est juste pour ce qu'on lui
     * demandait.
     *
     * Un NEON n'est pas ca du tout. C'est un tube de verre nu devant un panneau
     * SOMBRE : seul le tube brille, il est FIN — douze millimetres, donc un
     * trait de trois pixels sur une enseigne de trois metres —, et son halo
     * porte loin en se saturant. Le fond ne s'allume pas ; il recoit seulement
     * ce que le tube lui renvoie.
     *
     * Rendre un neon avec les reglages d'un marquee donne exactement ce qu'on
     * avait : une plaque lumineuse a gros lettrage, qui se lit comme une
     * enseigne de kebab et pas comme un neon de bar.
     */
    const float portee = cell * (neon ? 3.4f : 2.2f);   /* la portee du halo */
    const float liseré = cell * (neon ? 0.26f : 0.34f); /* la demi-epaisseur du tube */

    unsigned char *px = (unsigned char *)malloc((size_t)width * height * 3);
    if (!px) tool_fatalf("mémoire épuisée");

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fx = (float)x + 0.5f, fy = (float)y + 0.5f;

            /*
             * Le fond. Un marquee éteint n'est pas noir : c'est une plaque
             * teintée que le tube traverse. On part donc de la teinte du jeu,
             * très assombrie, avec un dégradé vertical — le tube est derrière
             * le haut de la plaque sur presque toutes les bornes.
             */
            const float v = fy / (float)height;
            /* Le panneau d'un neon est SOMBRE, et c'est ce qui fait le neon :
             * sans noir autour, un tube n'est qu'un trait clair. */
            const float fond = neon ? 0.012f
                                    : 0.055f + 0.075f * (1.0f - v) * (1.0f - v);
            float r = hue[0] * fond, g = hue[1] * fond, b = hue[2] * fond;

            /* Le cadre : deux filets, comme sur une vraie plaque sérigraphiée. */
            const float bx = fx < (float)width * 0.5f ? fx : (float)width - fx;
            const float by = fy < (float)height * 0.5f ? fy : (float)height - fy;
            const float bord = bx < by ? bx : by;
            const float e = cell * 0.22f;
            /* Pas de filet serigraphie sur un neon : le panneau est nu, et le
             * seul contour qui existe est celui des tubes. */
            if (!neon && bord > e * 1.6f && bord < e * 2.4f) {
                r += hue[0] * 0.22f; g += hue[1] * 0.22f; b += hue[2] * 0.22f;
            }

            /* Le texte : cœur, liseré, halo. */
            const float d = distance_au_texte(&p, fx, fy, portee);
            if (d < portee) {
                /* Cœur : presque blanc, c'est le verre saturé du tube. */
                const float coeur = clamp01((liseré - d) / (cell * (neon ? 0.09f : 0.22f)));
                /* Halo : décroissance en cloche sur la portée. */
                const float t = d / portee;
                /* 7,5 et non 4,5 : a une decroissance trop lente, le halo
                 * atteint la portee avec une valeur encore non nulle, et la
                 * coupure y dessine le RECTANGLE de recherche autour de
                 * chaque lettre. Ici il est retombe a 0,0004 avant la
                 * coupure, donc sous le quantum de l'octet. */
                /*
                 * Le halo d'un néon est plus fort et plus étalé : un tube de
                 * verre rayonne dans toutes les directions, une plaque
                 * rétroéclairée ne rayonne que par sa face.
                 *
                 * Et il est RAMENÉ À ZÉRO à la portée. Le commentaire d'origine
                 * avait raison sur le fond — une décroissance qui n'a pas fini
                 * de tomber dessine le rectangle de recherche autour de chaque
                 * lettre — mais sa parade était de choisir un exposant assez
                 * grand pour passer sous le quantum de l'octet. Elle ne tient
                 * plus dès qu'on veut un halo plus doux : à k = 5, le résidu
                 * vaut 0,0071, soit deux niveaux sur 255, et les huit blocs
                 * réapparaissent. Mesuré sur le second essai.
                 *
                 * On soustrait donc la valeur au bord et on renormalise : le
                 * halo vaut exactement zéro à la coupure, quel que soit `k`, et
                 * l'exposant redevient un réglage d'aspect au lieu d'être une
                 * contrainte numérique.
                 */
                const float k_halo = neon ? 5.0f : 7.5f;
                const float bord_h = expf(-k_halo);
                const float halo = ((expf(-t * t * k_halo) - bord_h) / (1.0f - bord_h))
                                 * (neon ? 1.05f : 0.80f);

                const float k = clamp01(halo + coeur);
                r += (hue[0] + coeur * (1.0f - hue[0])) * k;
                g += (hue[1] + coeur * (1.0f - hue[1])) * k;
                b += (hue[2] + coeur * (1.0f - hue[2])) * k;
            }

            unsigned char *o = &px[((size_t)y * width + x) * 3];
            o[0] = (unsigned char)(powf(clamp01(r), 1.0f / 2.2f) * 255.0f + 0.5f);
            o[1] = (unsigned char)(powf(clamp01(g), 1.0f / 2.2f) * 255.0f + 0.5f);
            o[2] = (unsigned char)(powf(clamp01(b), 1.0f / 2.2f) * 255.0f + 0.5f);
        }
    }

    if (!stbi_write_png(out_path, width, height, 3, px, width * 3)) {
        tool_fatalf("écriture impossible : %s", out_path);
    }
    printf("marqueeart %s : %dx%d, %d lettres, cellule %.1f px\n",
           titre, width, height, len, (double)cell);
    tool_infof("%s", out_path);
    free(px);
    return 0;
}
