/*
 * posterart — les huit affiches des murs, dessinées.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * Ce n'est pas une question de goût, c'est une question de droit. Les huit
 * images accrochées aux murs de la salle — et copiées dans le paquet — étaient
 * des œuvres de tiers, employées sans droit. Ouvertes une par une :
 *
 *   poster_7.jpg  le flyer d'arcade PAC-MAN, logo Midway compris   Bandai Namco
 *   poster_8.jpg  le flyer d'arcade DONKEY KONG, logo Nintendo     Nintendo
 *   poster_3.jpg  le flyer ATARI « Video Pinball »                 Atari
 *   poster_6.jpg  l'affiche « Palace Arcade — Hawkins »            Netflix
 *   poster_5.jpg  une illustration « Arcade » de League of Legends Riot Games
 *   poster_2.png  « Space Paranoids — ENCOM », l'arcade de Tron    Disney
 *   poster_1.png  l'affiche « ARCADE ARMAGEDDON », qui montre elle
 *                 aussi une borne PAC-MAN et une borne GALAGA, et
 *                 cite « TWIN GALAXIES »                           plusieurs
 *   poster_4.jpg  une illustration de « gaming room » du commerce,
 *                 retitrée NINE 19 TEEN par-dessus                 inconnu
 *
 * Aucune rédaction de `LICENSES.md` ne rend ces fichiers distribuables : ce ne
 * sont pas des œuvres libres mal créditées, ce sont des œuvres sous droit
 * exclusif. Tant qu'elles sont dans le paquet, le jeu ne peut pas être vendu.
 *
 * Pourquoi dessinées plutôt que rapportées
 * ----------------------------------------
 * Même raison que `sideart`, `panelart` et `marqueeart`, et cette fois elle est
 * décisive : une planche produite par arithmétique n'a AUCUNE licence à
 * démêler. Elle se régénère à l'identique sur les trois plateformes, elle se
 * règle, et elle appartient au dépôt. Acheter huit affiches sous licence aurait
 * marché aussi — et aurait remis dans l'arbre huit fichiers dont il faudrait
 * garder la facture.
 *
 * L'iconographie des années 80 appartient à tout le monde : une grille en
 * perspective, un soleil à fentes, un dégradé, une trame de losanges, un plan
 * au trait. Ses PERSONNAGES et ses MARQUES n'appartiennent à personne d'autre
 * que leurs ayants droit. Rien ici n'est un « presque Pac-Man » : il n'y a pas
 * un seul personnage dans les huit planches.
 *
 * La fonte est celle DU JEU — `engine/sprite/ns_font5x7.h`, la même que les
 * scores, le classement et les enseignes de `marqueeart`. Une affiche écrite
 * dans une autre fonte se remarque tout de suite : elle cesse d'appartenir à la
 * salle.
 *
 * Comment c'est dessiné, et pourquoi pas autrement
 * ------------------------------------------------
 * En COUVERTURE ANALYTIQUE, pas en suréchantillonnage. Chaque primitive —
 * rectangle, disque, anneau, segment, triangle — calcule la fraction du pixel
 * qu'elle couvre et se fond dans ce qui est déjà là. C'est plus court qu'une
 * passe de suréchantillonnage, ça ne coûte pas quatre fois la mémoire, et
 * surtout ça garde le LETTRAGE NET : une cellule de fonte tombe sur un nombre
 * entier de pixels, donc un trait de la fonte reste un trait franc. Suréchan-
 * tillonner l'aurait rendu gris sur ses bords, ce qui est exactement ce qu'on
 * ne veut pas d'une affiche sérigraphiée.
 *
 * La taille du texte est CALCULÉE, pas choisie
 * --------------------------------------------
 * Les cadres font 0,777 x 0,971 m et on les voit, mesuré depuis les points de
 * vue nommés de `salle.room.json`, entre 1,1 m (les sanitaires, en enfilade) et
 * 13 m (le fond de la salle). Le cas qui décide est le milieu : vers 8 m, une
 * affiche de 0,78 m couvre 5,6 degrés, soit environ 120 pixels de large sur un
 * écran de 1280. Un titre qui occupe 86 % de la largeur y fait donc 100 pixels
 * — sept lettres de quatorze pixels, qui se lisent. Le même titre écrit à la
 * moitié de cette taille ne se lirait plus qu'à quatre mètres.
 *
 * D'où la règle tenue par toutes les planches : UN mot qui porte à travers la
 * salle, et le reste en petit, qui récompense celui qui s'approche. Une affiche
 * dont tout le texte est de la même taille ne se lit à aucune distance.
 *
 * Le texte est écrit ici, en dur, et c'est voulu
 * ----------------------------------------------
 * `marqueeart` prend son titre en argument parce qu'une enseigne de borne est
 * un gabarit : dix-neuf fois la même plaque, un nom qui change. Une affiche
 * n'est pas un gabarit — sa composition et ses mots sont la même décision. Les
 * sortir en ligne de commande donnerait huit appels de quinze arguments dans
 * `assets/CMakeLists.txt`, où personne ne les relirait, et permettrait d'écrire
 * un texte que la mise en page ne peut pas contenir.
 */
#include "tools_common.h"
#include "tool_json.h"
#include "ns_font5x7.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ==========================================================================
 * La palette : celle de la salle, pas une autre
 * ==========================================================================
 * Les teintes viennent d'où elles sont déjà employées — les enseignes de
 * `assets/CMakeLists.txt`, l'enseigne au néon, les laques des caissons, les
 * piliers rouges. Une affiche peinte dans des couleurs qui n'existent nulle
 * part ailleurs dans la salle se lit comme une pièce rapportée, même quand
 * elle est bien dessinée. Ce sont des valeurs LINÉAIRES : l'encodage gamma se
 * fait à l'écriture, une seule fois, en bas de ce fichier.
 */
static const float C_CYAN[3]    = { 0.24f, 0.78f, 1.00f };  /* l'enseigne NINETEEN */
static const float C_MAGENTA[3] = { 1.00f, 0.16f, 0.58f };  /* les néons du sol */
static const float C_AMBRE[3]   = { 1.00f, 0.84f, 0.16f };  /* le marquee PAC-MAN */
static const float C_ORANGE[3]  = { 1.00f, 0.36f, 0.14f };  /* le marquee SHOOTER */
static const float C_VERT[3]    = { 0.22f, 0.88f, 0.40f };  /* le marquee FLAPPY */
static const float C_VIOLET[3]  = { 0.72f, 0.45f, 1.00f };  /* le marquee PIANO */
static const float C_BLEU[3]    = { 0.34f, 0.52f, 1.00f };  /* le marquee TETRIS */
static const float C_ROUGE[3]   = { 0.62f, 0.07f, 0.07f };  /* les piliers de la salle */
static const float C_CREME[3]   = { 0.62f, 0.56f, 0.42f };  /* le papier, et les murs */
static const float C_BLANC[3]   = { 0.90f, 0.92f, 0.95f };
static const float C_ENCRE[3]   = { 0.030f, 0.022f, 0.020f };

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }

/* ==========================================================================
 * La toile
 * ========================================================================== */
typedef struct toile {
    int    w, h;
    float *px;      /* RVB linéaire, non prémultiplié */
} toile;

static void toile_init(toile *t, int w, int h)
{
    t->w = w; t->h = h;
    t->px = (float *)calloc((size_t)w * (size_t)h * 3u, sizeof(float));
    if (!t->px) tool_fatalf("mémoire épuisée (%d x %d)", w, h);
}

static void pose(toile *t, int x, int y, const float c[3], float a)
{
    if (a <= 0.0f) return;
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) return;
    if (a > 1.0f) a = 1.0f;
    float *p = &t->px[((size_t)y * (size_t)t->w + (size_t)x) * 3u];
    p[0] += (c[0] - p[0]) * a;
    p[1] += (c[1] - p[1]) * a;
    p[2] += (c[2] - p[2]) * a;
}

/* ==========================================================================
 * Les primitives, toutes en COUVERTURE
 * ==========================================================================
 * Chacune répond à la même question : quelle fraction de ce pixel la forme
 * recouvre-t-elle ? Le rectangle sait le dire exactement ; le disque, l'anneau,
 * le segment et le triangle l'approchent par la distance au bord, adoucie sur
 * un pixel. C'est faux d'un dixième de pixel sur les fortes courbures et
 * personne ne l'a jamais vu ; c'est juste sur les traits, qui sont l'essentiel
 * d'une affiche au trait.
 */

/* Le rectangle est EXACT, et c'est lui qui porte tout le lettrage : la fonte
 * est faite de cellules carrées, et une cellule alignée sur la grille des
 * pixels sort avec des bords francs. */
static void p_rect(toile *t, float x0, float y0, float x1, float y1,
                   const float c[3], float a)
{
    if (x1 < x0) { const float s = x0; x0 = x1; x1 = s; }
    if (y1 < y0) { const float s = y0; y0 = y1; y1 = s; }
    const int ix0 = (int)floorf(x0), ix1 = (int)ceilf(x1);
    const int iy0 = (int)floorf(y0), iy1 = (int)ceilf(y1);
    for (int y = iy0; y < iy1; ++y) {
        const float cy = minf(y1, (float)y + 1.0f) - maxf(y0, (float)y);
        if (cy <= 0.0f) continue;
        for (int x = ix0; x < ix1; ++x) {
            const float cx = minf(x1, (float)x + 1.0f) - maxf(x0, (float)x);
            if (cx <= 0.0f) continue;
            pose(t, x, y, c, a * cx * cy);
        }
    }
}

/* Un cadre : quatre plats, dessinés comme quatre rectangles plutôt que comme
 * un rectangle plein recouvert d'un autre — sans quoi le fond déjà posé serait
 * effacé au centre. */
static void p_cadre(toile *t, float x0, float y0, float x1, float y1,
                    float e, const float c[3], float a)
{
    p_rect(t, x0, y0, x1, y0 + e, c, a);
    p_rect(t, x0, y1 - e, x1, y1, c, a);
    p_rect(t, x0, y0 + e, x0 + e, y1 - e, c, a);
    p_rect(t, x1 - e, y0 + e, x1, y1 - e, c, a);
}

/* Anneau, et disque quand r0 vaut zéro. */
static void p_anneau(toile *t, float cx, float cy, float r0, float r1,
                     const float c[3], float a)
{
    const int ix0 = (int)floorf(cx - r1) - 1, ix1 = (int)ceilf(cx + r1) + 1;
    const int iy0 = (int)floorf(cy - r1) - 1, iy1 = (int)ceilf(cy + r1) + 1;
    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            const float cov = clamp01(r1 - d + 0.5f)
                            * (r0 > 0.0f ? clamp01(d - r0 + 0.5f) : 1.0f);
            pose(t, x, y, c, a * cov);
        }
    }
}

/* Distance d'un point au segment [a, b]. Le classique, et le même que celui de
 * `marqueeart` : recopié plutôt que partagé, pour six lignes d'arithmétique que
 * ni l'un ni l'autre ne changera. */
static float dist_segment(float px, float py, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay;
    const float wx = px - ax, wy = py - ay;
    const float l2 = vx * vx + vy * vy;
    float u = (l2 > 1e-9f) ? (wx * vx + wy * vy) / l2 : 0.0f;
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    return hypotf(px - (ax + vx * u), py - (ay + vy * u));
}

static void p_trait(toile *t, float ax, float ay, float bx, float by,
                    float demi, const float c[3], float a)
{
    const float m = demi + 1.0f;
    int ix0 = (int)floorf(minf(ax, bx) - m), ix1 = (int)ceilf(maxf(ax, bx) + m);
    int iy0 = (int)floorf(minf(ay, by) - m), iy1 = (int)ceilf(maxf(ay, by) + m);
    if (ix0 < 0) ix0 = 0; if (iy0 < 0) iy0 = 0;
    if (ix1 > t->w) ix1 = t->w; if (iy1 > t->h) iy1 = t->h;
    for (int y = iy0; y < iy1; ++y) {
        for (int x = ix0; x < ix1; ++x) {
            const float d = dist_segment((float)x + 0.5f, (float)y + 0.5f, ax, ay, bx, by);
            pose(t, x, y, c, a * clamp01(demi - d + 0.5f));
        }
    }
}

/*
 * Triangle plein, par les trois demi-plans.
 *
 * On prend le MAXIMUM des trois distances signées : un point n'est dedans que
 * s'il est du bon côté des trois arêtes. C'est exact pour un convexe, et un
 * triangle l'est toujours — ce qui n'aurait pas été vrai d'un polygone
 * quelconque, et c'est pour ça qu'il n'y a pas de primitive « polygone » ici.
 */
static void p_triangle(toile *t, const float p[6], const float c[3], float a)
{
    /* L'aire signée donne le sens de parcours ; sans elle, un triangle décrit
     * dans l'autre sens sort vide au lieu de sortir plein. */
    const float aire = (p[2] - p[0]) * (p[5] - p[1]) - (p[4] - p[0]) * (p[3] - p[1]);
    const float sens = (aire < 0.0f) ? -1.0f : 1.0f;

    float xmin = p[0], xmax = p[0], ymin = p[1], ymax = p[1];
    for (int i = 1; i < 3; ++i) {
        xmin = minf(xmin, p[i * 2]); xmax = maxf(xmax, p[i * 2]);
        ymin = minf(ymin, p[i * 2 + 1]); ymax = maxf(ymax, p[i * 2 + 1]);
    }
    int ix0 = (int)floorf(xmin) - 1, ix1 = (int)ceilf(xmax) + 1;
    int iy0 = (int)floorf(ymin) - 1, iy1 = (int)ceilf(ymax) + 1;
    if (ix0 < 0) ix0 = 0; if (iy0 < 0) iy0 = 0;
    if (ix1 > t->w) ix1 = t->w; if (iy1 > t->h) iy1 = t->h;

    for (int y = iy0; y < iy1; ++y) {
        for (int x = ix0; x < ix1; ++x) {
            const float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float pire = -1e9f;
            for (int i = 0; i < 3; ++i) {
                const float ax = p[i * 2], ay = p[i * 2 + 1];
                const float bx = p[((i + 1) % 3) * 2], by = p[((i + 1) % 3) * 2 + 1];
                const float ex = bx - ax, ey = by - ay;
                const float len = hypotf(ex, ey);
                if (len < 1e-6f) continue;
                /* Distance signée à l'arête, positive à l'extérieur. */
                const float d = sens * ((fx - ax) * ey - (fy - ay) * ex) / len;
                if (d > pire) pire = d;
            }
            pose(t, x, y, c, a * clamp01(0.5f - pire));
        }
    }
}

/* ==========================================================================
 * Le lettrage
 * ========================================================================== */

static const uint8_t *glyphe(char c)
{
    const int i = (int)(unsigned char)c - NS_FONT5X7_FIRST;
    if (i < 0 || i >= NS_FONT5X7_GLYPHS) return ns_font5x7[0];
    return ns_font5x7[i];
}

/* La largeur d'une chaîne, en pixels : cinq colonnes par glyphe et une de
 * chasse, sauf après la dernière. */
static float texte_largeur(const char *s, float cell)
{
    const int n = (int)strlen(s);
    if (n <= 0) return 0.0f;
    return (float)(n * (NS_FONT5X7_COLS + 1) - 1) * cell;
}

/*
 * La cellule qui fait tenir `s` dans `large`, ENTIÈRE et plafonnée en hauteur.
 *
 * Entière : une cellule de 8,4 pixels donne des traits de fonte tantôt de huit
 * pixels, tantôt de neuf, et le mot se lit comme une impression ratée. C'est le
 * même défaut que le crénelage des diagonales — invisible sur la planche,
 * évident sur le mur.
 *
 * Plafonnée : un mot court comme « PLAN » cadré sur la largeur donnerait des
 * lettres de deux cents pixels de haut, soit un cinquième de l'affiche pour
 * quatre caractères.
 */
static float cellule(const char *s, float large, float haut_max)
{
    const int n = (int)strlen(s);
    if (n <= 0) return 1.0f;
    const int cols = n * (NS_FONT5X7_COLS + 1) - 1;
    float c = large / (float)cols;
    const float ch = haut_max / (float)NS_FONT5X7_ROWS;
    if (c > ch) c = ch;
    c = floorf(c);
    return c < 1.0f ? 1.0f : c;
}

static void p_texte(toile *t, const char *s, float x, float y, float cell,
                    const float c[3], float a)
{
    for (int i = 0; s[i]; ++i) {
        const uint8_t *g = glyphe(s[i]);
        const float gx = x + (float)(i * (NS_FONT5X7_COLS + 1)) * cell;
        for (int col = 0; col < NS_FONT5X7_COLS; ++col) {
            const uint8_t bits = g[col];
            if (!bits) continue;
            for (int row = 0; row < NS_FONT5X7_ROWS; ++row) {
                if (!(bits & (1u << row))) continue;
                const float px = gx + (float)col * cell;
                const float py = y + (float)row * cell;
                p_rect(t, px, py, px + cell, py + cell, c, a);
            }
        }
    }
}

static void p_texte_c(toile *t, const char *s, float cx, float y, float cell,
                      const float c[3], float a)
{
    p_texte(t, s, cx - texte_largeur(s, cell) * 0.5f, y, cell, c, a);
}

/* Aligné à DROITE : les colonnes de chiffres d'un tableau de scores ne se
 * lisent que comme ça. */
static void p_texte_d(toile *t, const char *s, float xd, float y, float cell,
                      const float c[3], float a)
{
    p_texte(t, s, xd - texte_largeur(s, cell), y, cell, c, a);
}

/* Un titre posé sur un fond chargé a besoin d'un décalé sombre derrière lui :
 * sans ça il se perd dans ce qu'il traverse. Un tiers de cellule, comme un
 * repérage de sérigraphie manqué — ce qui est de toute façon la vérité d'une
 * affiche imprimée en deux passes. */
static void p_titre_c(toile *t, const char *s, float cx, float y, float cell,
                      const float c[3], const float ombre[3])
{
    const float d = floorf(cell * 0.34f + 0.5f);
    if (ombre) p_texte_c(t, s, cx + d, y + d, cell, ombre, 1.0f);
    p_texte_c(t, s, cx, y, cell, c, 1.0f);
}

/* ==========================================================================
 * Le bruit déterministe
 * ==========================================================================
 * Le même sur les trois plateformes, sans `rand()` : deux machines qui ne
 * produisent pas la même affiche font mentir la promesse de reconstructibilité
 * du dépôt, et la différence ne se verrait qu'en comparant deux paquets.
 */
static float bruit(unsigned x, unsigned y, unsigned sel)
{
    unsigned h = x * 73856093u ^ y * 19349663u ^ sel * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFF;
}

/* ==========================================================================
 * Les fonds partagés
 * ========================================================================== */

/* Un dégradé vertical plein : la seule façon d'avoir un fond qui ne soit pas un
 * aplat sans le payer d'une texture. */
static void fond_degrade(toile *t, const float haut[3], const float bas[3])
{
    for (int y = 0; y < t->h; ++y) {
        const float v = (float)y / (float)(t->h - 1);
        for (int x = 0; x < t->w; ++x) {
            float *p = &t->px[((size_t)y * (size_t)t->w + (size_t)x) * 3u];
            for (int k = 0; k < 3; ++k) p[k] = haut[k] + (bas[k] - haut[k]) * v;
        }
    }
}

static void fond_uni(toile *t, const float c[3])
{
    fond_degrade(t, c, c);
}

/*
 * La FINITION, commune aux huit : un grain, puis un assombrissement du pourtour.
 *
 * Le grain d'abord, et il n'est pas décoratif : un dégradé de mille pixels de
 * haut sur huit bits BANDE, et les bandes se voient d'autant mieux que le mur
 * est uni. Deux millièmes de bruit les cassent.
 *
 * L'assombrissement du pourtour ensuite : une affiche est un objet posé sur un
 * mur, pas une fenêtre découpée dedans. Sans lui, les quatre bords sortent
 * exactement aussi nets que la texture, et le rectangle se lit comme un
 * autocollant. C'est le même correctif que le pourtour de `sideart`.
 */
static void finition(toile *t)
{
    for (int y = 0; y < t->h; ++y) {
        const float by = (float)((y < t->h - 1 - y) ? y : (t->h - 1 - y));
        for (int x = 0; x < t->w; ++x) {
            const float bx = (float)((x < t->w - 1 - x) ? x : (t->w - 1 - x));
            const float bord = clamp01(bx / 7.0f) * clamp01(by / 7.0f);
            float *p = &t->px[((size_t)y * (size_t)t->w + (size_t)x) * 3u];
            const float g = (bruit((unsigned)x, (unsigned)y, 7u) - 0.5f) * 0.020f;
            const float k = 0.62f + 0.38f * bord;
            for (int c = 0; c < 3; ++c) p[c] = clamp01((p[c] + g) * k);
        }
    }
}

/* ==========================================================================
 * MOTIF 1 — TOURNOI. L'annonce de la saison.
 * ==========================================================================
 * Sa composition est un ARBRE : deux moitiés de tableau qui se referment sur
 * une seule place. C'est la seule des huit qui soit construite sur des
 * diagonales, et c'est ce qui la distingue de loin des sept autres, avant même
 * qu'on lise le mot.
 */
static void motif_tournoi(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float haut[3] = { 0.020f, 0.012f, 0.055f };
    const float bas[3]  = { 0.055f, 0.014f, 0.075f };
    fond_degrade(t, haut, bas);

    /* Un halo très large derrière l'arbre : il donne un centre à la planche, ce
     * qu'un aplat n'a pas. Quarante anneaux valent un dégradé radial. */
    for (int i = 40; i > 0; --i) {
        const float r = W * 0.62f * (float)i / 40.0f;
        p_anneau(t, W * 0.5f, H * 0.50f, 0.0f, r, C_VIOLET, 0.0045f);
    }

    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.022f, C_CYAN, 1.0f);
    p_cadre(t, W * 0.040f, W * 0.040f, W - W * 0.040f, H - W * 0.040f,
            W * 0.006f, C_MAGENTA, 1.0f);

    const float mx = W * 0.5f;
    const float utile = W * 0.80f;

    p_texte_c(t, "LA SAISON DES DIX-NEUF", mx, H * 0.075f,
              cellule("LA SAISON DES DIX-NEUF", utile, H * 0.03f), C_CYAN, 1.0f);

    const float ct = cellule("TOURNOI", W * 0.84f, H * 0.135f);
    p_titre_c(t, "TOURNOI", mx, H * 0.115f, ct, C_AMBRE, C_ENCRE);
    p_texte_c(t, "DES HUIT JEUX", mx, H * 0.115f + ct * 8.4f,
              cellule("DES HUIT JEUX", utile * 0.72f, H * 0.038f), C_MAGENTA, 1.0f);

    /*
     * L'ARBRE. Huit inscrits de chaque côté, trois tours, une finale.
     *
     * Il est dessiné par récurrence de tour en tour : à chaque tour le nombre
     * de branches est divisé par deux et l'abscisse avance vers le centre. Le
     * décrire branche par branche aurait demandé trente segments écrits à la
     * main, dont on aurait fini par en oublier un.
     */
    const float y0 = H * 0.325f, y1 = H * 0.735f;
    const float xg0 = W * 0.085f, xg1 = mx - W * 0.085f;
    const float ep = maxf(1.2f, W * 0.0035f);

    for (int cote = 0; cote < 2; ++cote) {
        const float sgn = cote ? -1.0f : 1.0f;
        const float xa = cote ? (W - xg0) : xg0;
        const float xb = cote ? (W - xg1) : xg1;
        int n = 8;
        for (int tour = 0; tour < 4; ++tour) {
            const float xd = xa + (xb - xa) * ((float)tour / 3.0f);
            const float xf = xa + (xb - xa) * ((float)(tour + 1) / 3.0f);
            const float pas = (y1 - y0) / (float)(n - 1 > 0 ? n - 1 : 1);
            const float yc = (y0 + y1) * 0.5f;
            for (int i = 0; i < n; ++i) {
                const float y = (n > 1) ? (y0 + pas * (float)i) : yc;
                /* Le trait horizontal du match. */
                p_trait(t, xd, y, xf - sgn * (W * 0.012f), y, ep, C_CYAN, 0.92f);
                if (tour == 3) break;
                /* Le raccord vers le tour suivant, en diagonale : une équerre
                 * aurait donné un organigramme, une diagonale donne un arbre. */
                const float ys = y0 + ((y1 - y0) / (float)((n / 2) > 1 ? (n / 2) - 1 : 1))
                                    * (float)(i / 2);
                p_trait(t, xf - sgn * (W * 0.012f), y, xf, ys, ep, C_MAGENTA, 0.92f);
            }
            if (n == 1) break;
            n /= 2;
        }
    }

    /* La place à gagner, au centre : un losange, c'est-à-dire un carré posé sur
     * la pointe. Deux triangles, parce qu'il n'y a pas de primitive quadrilatère
     * — et qu'un convexe se découpe toujours en triangles. */
    {
        const float yc = (y0 + y1) * 0.5f;
        const float r = W * 0.085f;
        const float a[6] = { mx, yc - r, mx + r, yc, mx, yc + r };
        const float b[6] = { mx, yc - r, mx, yc + r, mx - r, yc };
        p_triangle(t, a, C_AMBRE, 1.0f);
        p_triangle(t, b, C_AMBRE, 1.0f);
        const float ci = cellule("19", r * 1.05f, r * 0.9f);
        p_texte_c(t, "19", mx, yc - ci * 3.5f, ci, C_ENCRE, 1.0f);
    }

    p_texte_c(t, "SAMEDI 21 HEURES", mx, H * 0.800f,
              cellule("SAMEDI 21 HEURES", utile, H * 0.052f), C_BLANC, 1.0f);
    p_texte_c(t, "INSCRIPTION AU COMPTOIR", mx, H * 0.870f,
              cellule("INSCRIPTION AU COMPTOIR", utile, H * 0.030f), C_CYAN, 1.0f);
    p_texte_c(t, "UN JETON PAR TOUR", mx, H * 0.910f,
              cellule("UN JETON PAR TOUR", utile * 0.8f, H * 0.030f), C_CYAN, 1.0f);
}

/* ==========================================================================
 * MOTIF 2 — REGLEMENT. Le seul texte de la salle qui s'adresse au joueur.
 * ==========================================================================
 * Elle est CLAIRE, et c'est délibéré : sept planches sur huit sont sombres,
 * parce que le néon a besoin de noir. Huit affiches sombres alignées sur un mur
 * beige se lisent comme huit trous. Celle-ci est un papier imprimé, et c'est
 * elle qui donne à la série une respiration en valeur — le même service qu'un
 * silence dans une phrase.
 */
static void motif_reglement(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float pap_h[3] = { 0.66f, 0.60f, 0.45f };
    const float pap_b[3] = { 0.52f, 0.46f, 0.33f };
    fond_degrade(t, pap_h, pap_b);

    /* La trame d'un papier bon marché : des fibres, pas du bruit blanc. Une
     * ligne sur onze, très légèrement plus sombre. */
    for (int y = 0; y < t->h; y += 11) {
        p_rect(t, 0.0f, (float)y, W, (float)y + 1.0f, C_ENCRE, 0.045f);
    }

    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.030f, C_ROUGE, 1.0f);
    p_cadre(t, W * 0.052f, W * 0.052f, W - W * 0.052f, H - W * 0.052f,
            W * 0.005f, C_ROUGE, 1.0f);

    const float mx = W * 0.5f;

    /* Le bandeau de titre : blanc réservé dans un aplat rouge, comme une
     * affiche d'administration. */
    p_rect(t, W * 0.080f, H * 0.062f, W - W * 0.080f, H * 0.152f, C_ROUGE, 1.0f);
    p_texte_c(t, "REGLEMENT", mx, H * 0.083f,
              cellule("REGLEMENT", W * 0.72f, H * 0.048f), C_CREME, 1.0f);
    p_texte_c(t, "DE LA SALLE", mx, H * 0.170f,
              cellule("DE LA SALLE", W * 0.42f, H * 0.028f), C_ROUGE, 1.0f);

    /*
     * Les six articles. Deux lignes de dix-neuf caractères au plus : c'est ce
     * que la largeur utile autorise à une cellule de cinq pixels, et une
     * cellule plus fine ne se lirait plus du tout depuis l'allée.
     */
    static const char *const regles[6][2] = {
        { "UN JETON PAR",       "PARTIE, PAS DEUX"   },
        { "ON NE SECOUE PAS",   "LES BORNES"         },
        { "LA FILE ATTEND A",   "GAUCHE DE L'ECRAN"  },
        { "NI VERRE NI BOITE",  "SUR LES PANNEAUX"   },
        { "UN RECORD SE FAIT",  "VISER AU COMPTOIR"  },
        { "LA QUEUE RETOURNE",  "SUR SON RATELIER"   },
    };
    const float ytop = H * 0.225f;
    const float pas  = H * 0.108f;
    const float cr   = cellule("LA QUEUE RETOURNE", W * 0.665f, H * 0.030f);
    const float cn   = floorf(cr * 1.9f);
    const float xnum = W * 0.095f;
    const float xtxt = W * 0.235f;

    for (int i = 0; i < 6; ++i) {
        const float y = ytop + pas * (float)i;
        char n[2];
        n[0] = (char)('1' + i); n[1] = '\0';
        /* Le numéro, réservé dans un carré plein : c'est ce qui fait qu'on
         * compte les articles d'un coup d'œil au lieu de les lire. */
        const float s = cn * 8.0f;
        p_rect(t, xnum, y - cn * 0.5f, xnum + s, y - cn * 0.5f + s, C_ROUGE, 1.0f);
        p_texte_c(t, n, xnum + s * 0.5f, y - cn * 0.5f + cn * 0.5f, cn, C_CREME, 1.0f);

        p_texte(t, regles[i][0], xtxt, y, cr, C_ENCRE, 1.0f);
        p_texte(t, regles[i][1], xtxt, y + cr * 9.0f, cr, C_ENCRE, 1.0f);

        if (i < 5) {
            p_rect(t, xnum, y + pas * 0.76f, W - W * 0.095f, y + pas * 0.76f + 1.5f,
                   C_ROUGE, 0.40f);
        }
    }

    p_rect(t, W * 0.50f, H * 0.905f, W - W * 0.095f, H * 0.905f + 2.0f, C_ENCRE, 0.55f);
    p_texte_d(t, "LA DIRECTION", W - W * 0.095f, H * 0.920f,
              cellule("LA DIRECTION", W * 0.30f, H * 0.026f), C_ENCRE, 0.85f);
}

/* ==========================================================================
 * MOTIF 3 — JETONS. La promotion du monnayeur.
 * ==========================================================================
 * Composition RADIALE : un éclat de secteurs et une pièce frappée. C'est la
 * seule des huit qui n'ait ni horizon ni grille — tout y part du centre, ce qui
 * est exactement ce qu'on veut d'une promotion : un prix, et rien d'autre.
 */
static void motif_jetons(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float haut[3] = { 0.070f, 0.026f, 0.008f };
    const float bas[3]  = { 0.030f, 0.010f, 0.004f };
    fond_degrade(t, haut, bas);

    const float mx = W * 0.5f, my = H * 0.455f;

    /* L'éclat : quatorze secteurs sur vingt-huit, en triangles qui débordent
     * largement de la planche pour qu'aucun sommet ne se voie. */
    for (int i = 0; i < 28; i += 2) {
        const float a0 = (float)i * 6.2831853f / 28.0f;
        const float a1 = (float)(i + 1) * 6.2831853f / 28.0f;
        const float R = W * 2.2f;
        const float p[6] = { mx, my,
                             mx + R * cosf(a0), my + R * sinf(a0),
                             mx + R * cosf(a1), my + R * sinf(a1) };
        const float c[3] = { 0.150f, 0.052f, 0.012f };
        p_triangle(t, p, c, 1.0f);
    }

    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.020f, C_AMBRE, 1.0f);

    /* LA PIÈCE. Un jeton d'arcade n'est pas une médaille : il est CANNELÉ sur
     * la tranche, et c'est cette couronne d'encoches qui le rend reconnaissable
     * même quand on ne lit plus ce qui est frappé dessus. */
    const float r = W * 0.235f;
    const float or_sombre[3] = { 0.44f, 0.24f, 0.030f };
    const float or_clair[3]  = { 1.00f, 0.78f, 0.22f };
    p_anneau(t, mx, my, 0.0f, r * 1.045f, or_sombre, 1.0f);
    p_anneau(t, mx, my, 0.0f, r, C_AMBRE, 1.0f);
    for (int i = 0; i < 56; ++i) {
        const float a = (float)i * 6.2831853f / 56.0f;
        const float c = cosf(a), s = sinf(a);
        p_trait(t, mx + c * r * 0.925f, my + s * r * 0.925f,
                   mx + c * r * 1.020f, my + s * r * 1.020f,
                maxf(1.0f, W * 0.0045f), or_sombre, 0.85f);
    }
    p_anneau(t, mx, my, r * 0.80f, r * 0.86f, or_sombre, 0.9f);
    {
        const float ci = cellule("19", r * 0.95f, r * 0.80f);
        p_texte_c(t, "19", mx, my - ci * 3.5f, ci, or_sombre, 1.0f);
    }
    /* Un croissant clair en haut à gauche : sans lui la pièce est un disque
     * plat, et un jeton est un objet en relief. */
    for (int i = 0; i < 22; ++i) {
        const float a = 3.5f + (float)i * 1.6f / 22.0f;
        p_trait(t, mx + cosf(a) * r * 0.90f, my + sinf(a) * r * 0.90f,
                   mx + cosf(a + 0.09f) * r * 0.90f, my + sinf(a + 0.09f) * r * 0.90f,
                W * 0.012f, or_clair, 0.55f);
    }

    p_titre_c(t, "JETONS", mx, H * 0.055f,
              cellule("JETONS", W * 0.84f, H * 0.115f), C_AMBRE, C_ENCRE);
    p_texte_c(t, "LE COMPTOIR EN REND", mx, H * 0.170f,
              cellule("LE COMPTOIR EN REND", W * 0.72f, H * 0.030f), C_ORANGE, 1.0f);

    /* Le prix, en réserve dans un bandeau : c'est la seule information que
     * quelqu'un cherche vraiment sur cette affiche. */
    p_rect(t, W * 0.075f, H * 0.760f, W - W * 0.075f, H * 0.860f, C_AMBRE, 1.0f);
    p_texte_c(t, "5 POUR 1 EURO", mx, H * 0.780f,
              cellule("5 POUR 1 EURO", W * 0.78f, H * 0.058f), C_ENCRE, 1.0f);
    p_texte_c(t, "20 POUR 3 EUROS", mx, H * 0.888f,
              cellule("20 POUR 3 EUROS", W * 0.62f, H * 0.036f), C_AMBRE, 1.0f);
    p_texte_c(t, "LE JETON NE SE REPREND PAS", mx, H * 0.940f,
              cellule("LE JETON NE SE REPREND PAS", W * 0.72f, H * 0.024f),
              C_ORANGE, 0.9f);
}

/* ==========================================================================
 * MOTIF 4 — CONCERT. Un groupe inventé, sur l'estrade de la salle.
 * ==========================================================================
 * Composition d'HORIZON : un sol qui fuit, un ciel, et un disque à la jonction.
 * C'est le vocabulaire graphique de l'époque, et il n'appartient à personne —
 * ce qui appartient à quelqu'un, ce sont les personnages et les titres, dont il
 * n'y a rien ici. Le groupe et sa date sont inventés pour cette salle.
 */
static void motif_concert(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float ciel_h[3] = { 0.010f, 0.008f, 0.045f };
    const float ciel_b[3] = { 0.180f, 0.020f, 0.120f };
    fond_degrade(t, ciel_h, ciel_b);

    const float hy = H * 0.545f;   /* l'horizon */
    const float mx = W * 0.5f;

    /* Les étoiles, dans le tiers haut seulement : plus bas, le halo du soleil
     * les avalerait de toute façon. */
    for (unsigned i = 0; i < 130u; ++i) {
        const float x = bruit(i, 1u, 3u) * W;
        const float y = bruit(i, 2u, 3u) * hy * 0.72f;
        const float r = 0.6f + bruit(i, 3u, 3u) * 1.3f;
        p_anneau(t, x, y, 0.0f, r, C_BLANC, 0.35f + bruit(i, 4u, 3u) * 0.5f);
    }

    /*
     * LE SOLEIL À FENTES. Le disque est peint en bandes horizontales dont la
     * teinte descend de l'ambre au magenta ; les fentes sont RECREUSÉES ensuite
     * dans la couleur du ciel, de plus en plus larges vers le bas. C'est cet
     * élargissement qui fait le motif : des fentes régulières donnent un store,
     * pas un soleil.
     */
    const float sr = W * 0.285f, sy = hy - W * 0.055f;
    for (int i = 0; i < 220; ++i) {
        const float u = (float)i / 219.0f;
        const float y = sy - sr + 2.0f * sr * u;
        const float dx = sqrtf(maxf(0.0f, sr * sr - (y - sy) * (y - sy)));
        const float c[3] = {
            C_AMBRE[0] + (C_MAGENTA[0] - C_AMBRE[0]) * u,
            C_AMBRE[1] + (C_MAGENTA[1] - C_AMBRE[1]) * u,
            C_AMBRE[2] + (C_MAGENTA[2] - C_AMBRE[2]) * u,
        };
        p_rect(t, mx - dx, y, mx + dx, y + 2.0f * sr / 219.0f + 1.0f, c, 1.0f);
    }
    for (int i = 0; i < 9; ++i) {
        const float u = 0.30f + 0.70f * (float)i / 8.0f;
        const float y = sy - sr + 2.0f * sr * u;
        const float e = W * (0.006f + 0.030f * (float)i / 8.0f);
        p_rect(t, mx - sr - 2.0f, y - e * 0.5f, mx + sr + 2.0f, y + e * 0.5f,
               ciel_b, 1.0f);
    }
    /* Le soleil est coupé net par l'horizon : ce qui dépasse dessous devient le
     * sol, sinon le disque flotte au-dessus de la grille. */
    p_rect(t, 0.0f, hy, W, H, ciel_h, 1.0f);

    /*
     * LE SOL QUI FUIT. Les fuyantes convergent vers un point de fuite unique ;
     * les transversales s'espacent en puissance, ce qui est la bonne façon de
     * tricher une perspective sans matrice — une progression linéaire donne un
     * damier vu de biais, pas un sol qui s'éloigne.
     */
    const float sol[3] = { 0.030f, 0.004f, 0.030f };
    p_rect(t, 0.0f, hy, W, H, sol, 1.0f);
    for (int i = -14; i <= 14; ++i) {
        const float xb = mx + (float)i * W * 0.155f;
        p_trait(t, mx, hy, xb, H + 4.0f, maxf(1.0f, W * 0.0022f), C_MAGENTA, 0.75f);
    }
    for (int k = 1; k <= 11; ++k) {
        const float u = (float)k / 11.0f;
        const float y = hy + (H - hy) * powf(u, 2.3f);
        p_trait(t, 0.0f, y, W, y, maxf(1.0f, W * 0.0020f + u * W * 0.0030f),
                C_MAGENTA, 0.70f);
    }
    p_trait(t, 0.0f, hy, W, hy, maxf(1.2f, W * 0.0030f), C_CYAN, 0.95f);

    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.018f, C_MAGENTA, 1.0f);

    /* Le lettrage. Il vit dans le CIEL, au-dessus du soleil : posé sur les
     * fentes il aurait fallu un contour, et un contour sur une affiche de
     * concert la fait ressembler à un autocollant. */
    p_texte_c(t, "SUR L'ESTRADE, CE VENDREDI", mx, H * 0.055f,
              cellule("SUR L'ESTRADE, CE VENDREDI", W * 0.80f, H * 0.026f),
              C_CYAN, 1.0f);
    p_titre_c(t, "SIGNAUX", mx, H * 0.100f,
              cellule("SIGNAUX", W * 0.84f, H * 0.130f), C_BLANC, C_ENCRE);
    p_texte_c(t, "UN TRIO, DEUX SYNTHES", mx, H * 0.232f,
              cellule("UN TRIO, DEUX SYNTHES", W * 0.70f, H * 0.030f), C_AMBRE, 1.0f);

    /* En bas, sur le sol : une réserve sombre derrière, sans quoi la grille
     * traverse les lettres. */
    p_rect(t, W * 0.055f, H * 0.845f, W - W * 0.055f, H * 0.960f, ciel_h, 0.88f);
    p_texte_c(t, "MINUIT", mx, H * 0.858f,
              cellule("MINUIT", W * 0.50f, H * 0.058f), C_CYAN, 1.0f);
    p_texte_c(t, "ENTREE LIBRE, JETON EN PLUS", mx, H * 0.930f,
              cellule("ENTREE LIBRE, JETON EN PLUS", W * 0.80f, H * 0.024f),
              C_BLANC, 0.92f);
}

/* ==========================================================================
 * MOTIF 5 — PLAN. La salle elle-même, vue de dessus.
 * ==========================================================================
 * Elle est la seule des huit à ne rien inventer : elle LIT `salle.room.json` et
 * trace ce qu'elle y trouve — les trois coquilles, les quatre cloisons, les
 * dix-neuf bornes, et le point de départ du joueur.
 *
 * C'est le contraire d'un caprice. Un plan dessiné à la main serait une
 * deuxième description de la salle, et deux descriptions d'une même chose
 * finissent toujours par se contredire : celle-ci se contredirait EN SILENCE,
 * puisque le seul endroit où on la verrait est un mur, à cinq mètres. Déplacer
 * une borne dans la description refait donc l'affiche.
 */
typedef struct plan_source {
    float  murs[16][64][2];   /* jusqu'à 16 polylignes de 64 points */
    int    murs_n[16];
    bool   murs_ferme[16];
    int    n_murs;
    float  bornes[32][3];     /* x, z, lacet en degrés */
    int    n_bornes;
    float  depart[2];
    float  xmin, xmax, zmin, zmax;
} plan_source;

static void plan_lire(plan_source *p, const char *chemin)
{
    memset(p, 0, sizeof *p);

    FILE *f = fopen(chemin, "rb");
    if (!f) tool_fatalf("plan : « %s » introuvable", chemin);
    fseek(f, 0, SEEK_END);
    const long taille = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (taille <= 0) tool_fatalf("plan : « %s » est vide", chemin);
    char *texte = (char *)malloc((size_t)taille + 1u);
    if (!texte) tool_fatalf("mémoire épuisée");
    if (fread(texte, 1u, (size_t)taille, f) != (size_t)taille) {
        tool_fatalf("plan : lecture incomplète de « %s »", chemin);
    }
    texte[taille] = '\0';
    fclose(f);

    tool_json doc;
    tool_json_parse(&doc, texte, (size_t)taille, chemin);
    const tool_json_value *root = tool_json_root(&doc);

    p->xmin = p->zmin = 1e9f;
    p->xmax = p->zmax = -1e9f;

    const tool_json_value *murs = tool_json_get(&doc, root, "walls");
    const int nm = tool_json_array_count(&doc, murs);
    for (int i = 0; i < nm && p->n_murs < 16; ++i) {
        const tool_json_value *m = tool_json_at(&doc, murs, i);
        const tool_json_value *pts = tool_json_get(&doc, m, "points");
        const int np = tool_json_array_count(&doc, pts);
        if (np < 2) continue;
        const int k = p->n_murs++;
        p->murs_ferme[k] = tool_json_get_bool(&doc, m, "closed", false);
        for (int j = 0; j < np && j < 64; ++j) {
            const tool_json_value *pt = tool_json_at(&doc, pts, j);
            /* Un point de mur est un tableau NU — `[x, z]` — et non un objet a
             * clef : `tool_json_get_floats` ne sait pas le lire, elle attend un
             * nom de champ. On indexe donc les deux elements a la main. */
            const float xy[2] = {
                tool_json_value_float(&doc, tool_json_at(&doc, pt, 0), 0.0f),
                tool_json_value_float(&doc, tool_json_at(&doc, pt, 1), 0.0f),
            };
            p->murs[k][j][0] = xy[0];
            p->murs[k][j][1] = xy[1];
            p->murs_n[k]++;
            p->xmin = minf(p->xmin, xy[0]); p->xmax = maxf(p->xmax, xy[0]);
            p->zmin = minf(p->zmin, xy[1]); p->zmax = maxf(p->zmax, xy[1]);
        }
    }
    if (p->n_murs == 0) tool_fatalf("plan : aucune coquille dans « %s »", chemin);

    const tool_json_value *bornes = tool_json_get(&doc, root, "cabinets");
    const int nb = tool_json_array_count(&doc, bornes);
    for (int i = 0; i < nb && p->n_bornes < 32; ++i) {
        const tool_json_value *b = tool_json_at(&doc, bornes, i);
        float at[3] = { 0.0f, 0.0f, 0.0f };
        tool_json_get_vec3(&doc, b, "at", at, 0.0f);
        p->bornes[p->n_bornes][0] = at[0];
        p->bornes[p->n_bornes][1] = at[2];
        p->bornes[p->n_bornes][2] = tool_json_get_float(&doc, b, "yaw", 0.0f);
        p->n_bornes++;
    }

    const tool_json_value *depart = tool_json_get(&doc, root, "playerStart");
    if (depart) {
        float at[3] = { 0.0f, 0.0f, 0.0f };
        tool_json_get_vec3(&doc, depart, "position", at, 0.0f);
        p->depart[0] = at[0];
        p->depart[1] = at[2];
    }

    tool_json_free(&doc);
    free(texte);
}

static void motif_plan(toile *t, const char *salle)
{
    const float W = (float)t->w, H = (float)t->h;
    plan_source p;
    plan_lire(&p, salle);

    const float haut[3] = { 0.010f, 0.022f, 0.062f };
    const float bas[3]  = { 0.006f, 0.014f, 0.040f };
    fond_degrade(t, haut, bas);
    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.020f, C_CYAN, 1.0f);

    const float mx = W * 0.5f;
    const float ct = cellule("PLAN", W * 0.60f, H * 0.105f);
    p_texte_c(t, "PLAN", mx, H * 0.060f, ct, C_CYAN, 1.0f);
    p_texte_c(t, "DE LA SALLE", mx, H * 0.060f + ct * 8.6f,
              cellule("DE LA SALLE", W * 0.46f, H * 0.032f), C_BLANC, 1.0f);

    /* Le cadre de dessin, et l'échelle qui y fait tenir la salle SANS la
     * déformer : un plan à l'échelle est la seule chose qu'on demande à un
     * plan. Une marge d'un demi-mètre garde les murs à l'intérieur du trait. */
    const float bx0 = W * 0.070f, bx1 = W - W * 0.070f;
    const float by0 = H * 0.215f, by1 = H * 0.815f;
    const float etx = (p.xmax - p.xmin) + 1.0f;
    const float etz = (p.zmax - p.zmin) + 1.0f;
    const float ech = minf((bx1 - bx0) / etx, (by1 - by0) / etz);
    const float cx = (p.xmin + p.xmax) * 0.5f, cz = (p.zmin + p.zmax) * 0.5f;
    const float ox = (bx0 + bx1) * 0.5f, oy = (by0 + by1) * 0.5f;

    /* Le nord est en HAUT, donc les z croissants montent : d'où le signe. Sans
     * lui le plan serait juste, et retourné — c'est-à-dire inutilisable. */
#define PX(X) (ox + ((X) - cx) * ech)
#define PY(Z) (oy - ((Z) - cz) * ech)

    /* Le quadrillage métrique, très pâle : c'est lui qui donne l'échelle sans
     * qu'on ait à la lire. */
    {
        const float g[3] = { 0.10f, 0.26f, 0.42f };
        for (int i = (int)floorf(p.xmin) - 1; i <= (int)ceilf(p.xmax) + 1; ++i) {
            const float x = PX((float)i);
            p_rect(t, x, by0, x + 1.0f, by1, g, 0.16f);
        }
        for (int i = (int)floorf(p.zmin) - 1; i <= (int)ceilf(p.zmax) + 1; ++i) {
            const float y = PY((float)i);
            p_rect(t, bx0, y, bx1, y + 1.0f, g, 0.16f);
        }
    }

    /* Les murs. Les coquilles au trait fort, les cloisons au trait fin : c'est
     * la convention d'un plan, et c'est ce qui fait qu'on distingue la salle de
     * ce qu'on y a posé. */
    for (int i = 0; i < p.n_murs; ++i) {
        const float e = p.murs_ferme[i] ? maxf(1.6f, W * 0.0042f)
                                        : maxf(1.0f, W * 0.0024f);
        const int n = p.murs_n[i];
        for (int j = 0; j + 1 < n; ++j) {
            p_trait(t, PX(p.murs[i][j][0]), PY(p.murs[i][j][1]),
                       PX(p.murs[i][j + 1][0]), PY(p.murs[i][j + 1][1]),
                    e, C_CYAN, 1.0f);
        }
        if (p.murs_ferme[i] && n > 2) {
            p_trait(t, PX(p.murs[i][n - 1][0]), PY(p.murs[i][n - 1][1]),
                       PX(p.murs[i][0][0]), PY(p.murs[i][0][1]), e, C_CYAN, 1.0f);
        }
    }

    /* Les dix-neuf bornes, à leur empreinte au sol (0,62 x 0,80 m) et à leur
     * lacet. Deux triangles par borne, pour la même raison que le losange du
     * tournoi : il n'y a pas de primitive quadrilatère, et il n'en faut pas. */
    for (int i = 0; i < p.n_bornes; ++i) {
        const float a = p.bornes[i][2] * 3.14159265f / 180.0f;
        const float ca = cosf(a), sa = sinf(a);
        const float hw = 0.31f * ech, hd = 0.40f * ech;
        const float bxp = PX(p.bornes[i][0]), byp = PY(p.bornes[i][1]);
        float q[4][2];
        const float loc[4][2] = { { -hw, -hd }, { hw, -hd }, { hw, hd }, { -hw, hd } };
        for (int k = 0; k < 4; ++k) {
            q[k][0] = bxp + loc[k][0] * ca - loc[k][1] * sa;
            q[k][1] = byp + loc[k][0] * sa + loc[k][1] * ca;
        }
        const float t1[6] = { q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1] };
        const float t2[6] = { q[0][0], q[0][1], q[2][0], q[2][1], q[3][0], q[3][1] };
        p_triangle(t, t1, C_MAGENTA, 1.0f);
        p_triangle(t, t2, C_MAGENTA, 1.0f);
    }

    /* « VOUS ETES ICI » : le point de départ, qui est aussi la porte. Un plan
     * de salle sans ce repère est un plan d'architecte. */
    {
        const float x = PX(p.depart[0]), y = PY(p.depart[1]);
        const float r = W * 0.026f;
        p_anneau(t, x, y, r * 0.62f, r, C_AMBRE, 1.0f);
        p_anneau(t, x, y, 0.0f, r * 0.30f, C_AMBRE, 1.0f);
        const float c = cellule("VOUS ETES ICI", W * 0.26f, H * 0.020f);
        /* Le repère est au nord-est du plan : l'étiquette part donc vers
         * l'OUEST, faute de quoi elle sortirait de la planche. */
        p_texte_d(t, "VOUS ETES ICI", x - r * 1.6f, y - c * 3.5f, c, C_AMBRE, 1.0f);
    }

    /* La rose des vents, réduite à ce qui sert : une flèche et un N. */
    {
        const float x = bx0 + W * 0.055f, y = by0 + H * 0.048f;
        const float r = W * 0.030f;
        const float fl[6] = { x, y - r, x - r * 0.55f, y + r * 0.55f, x + r * 0.55f, y + r * 0.55f };
        p_triangle(t, fl, C_BLANC, 0.95f);
        const float c = cellule("N", W * 0.030f, H * 0.026f);
        p_texte_c(t, "N", x, y + r * 0.9f, c, C_BLANC, 0.95f);
    }

    /* La barre d'échelle : cinq mètres, mesurés sur le plan lui-même. */
    {
        const float y = by1 + H * 0.022f;
        const float x0 = bx0, x1 = bx0 + 5.0f * ech;
        p_rect(t, x0, y, x1, y + maxf(2.0f, W * 0.004f), C_BLANC, 0.9f);
        for (int i = 0; i <= 5; ++i) {
            const float x = x0 + (x1 - x0) * (float)i / 5.0f;
            p_rect(t, x - 1.0f, y - W * 0.008f, x + 1.0f, y + W * 0.008f, C_BLANC, 0.9f);
        }
        p_texte(t, "5 METRES", x0, y + H * 0.016f,
                cellule("5 METRES", W * 0.22f, H * 0.020f), C_BLANC, 0.85f);
    }

    /* La légende, et le compte des bornes est CELUI QU'ON A LU : écrire
     * « 19 BORNES » en dur ferait mentir l'affiche le jour où il y en aurait
     * vingt, et personne ne le verrait. */
    {
        char ligne[64];
        snprintf(ligne, sizeof ligne, "%d BORNES  -  UN JETON  -  SANS FIN", p.n_bornes);
        p_texte_c(t, ligne, mx, H * 0.930f,
                  cellule(ligne, W * 0.84f, H * 0.026f), C_CYAN, 1.0f);
    }
#undef PX
#undef PY
}

/* ==========================================================================
 * MOTIF 6 — ATTENTION. L'avertissement qu'une salle d'arcade doit afficher.
 * ==========================================================================
 * Composition de SIGNALISATION : chevrons, triangle, texte serré. Elle est la
 * seule des huit dont la forme est imposée par sa fonction — un avertissement
 * qui ressemble à une affiche décorative ne prévient personne.
 */
static void motif_attention(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float fond[3] = { 0.026f, 0.024f, 0.022f };
    fond_uni(t, fond);

    const float jaune[3] = { 1.00f, 0.72f, 0.03f };
    const float noir[3]  = { 0.012f, 0.011f, 0.010f };
    const float mx = W * 0.5f;

    /* Les deux bandeaux de chevrons. Ils sont dessinés par des traits obliques
     * qui débordent des deux côtés : un chevron coupé au bord se lit comme un
     * chevron, un chevron rentré se lit comme une flèche. */
    for (int b = 0; b < 2; ++b) {
        const float y0 = b ? H - H * 0.105f : 0.0f;
        const float y1 = b ? H : H * 0.105f;
        p_rect(t, 0.0f, y0, W, y1, jaune, 1.0f);
        const float e = (y1 - y0);
        for (float x = -e * 2.0f; x < W + e * 2.0f; x += e * 0.86f) {
            const float q[6] = { x, y1, x + e * 0.43f, y1, x + e * 0.43f + e, y0 };
            const float r[6] = { x, y1, x + e * 0.43f + e, y0, x + e, y0 };
            p_triangle(t, q, noir, 1.0f);
            p_triangle(t, r, noir, 1.0f);
        }
    }

    p_titre_c(t, "ATTENTION", mx, H * 0.145f,
              cellule("ATTENTION", W * 0.86f, H * 0.090f), jaune, C_ENCRE);

    /*
     * LE TRIANGLE. Contour épais et point d'exclamation en réserve, pris dans
     * la fonte du jeu comme tout le reste : dessiner un « ! » à la main ici
     * aurait donné le seul signe de la salle qui ne vienne pas de sa fonte.
     */
    {
        const float cxx = mx, cy = H * 0.395f;
        const float bw = W * 0.46f, bh = H * 0.185f;
        const float ext[6] = { cxx, cy - bh * 0.5f,
                               cxx + bw * 0.5f, cy + bh * 0.5f,
                               cxx - bw * 0.5f, cy + bh * 0.5f };
        const float k = 0.76f;
        const float in[6] = { cxx, cy - bh * 0.5f + bh * (1.0f - k) * 0.9f,
                              cxx + bw * 0.5f * k, cy + bh * 0.5f - bh * (1.0f - k) * 0.35f,
                              cxx - bw * 0.5f * k, cy + bh * 0.5f - bh * (1.0f - k) * 0.35f };
        p_triangle(t, ext, jaune, 1.0f);
        p_triangle(t, in, fond, 1.0f);
        const float c = cellule("!", W * 0.10f, bh * 0.42f);
        p_texte_c(t, "!", cxx, cy - c * 1.4f, c, jaune, 1.0f);
    }

    p_texte_c(t, "LUMIERES CLIGNOTANTES", mx, H * 0.545f,
              cellule("LUMIERES CLIGNOTANTES", W * 0.84f, H * 0.046f), jaune, 1.0f);

    static const char *const corps[5] = {
        "CERTAINES BORNES PRODUISENT",
        "DES ECLATS RAPIDES ET DES",
        "MOTIFS CONTRASTES.",
        "EN CAS DE MALAISE, ARRETEZ",
        "LA PARTIE ET PREVENEZ LE BAR.",
    };
    const float c = cellule("MOTIFS CONTRASTES.", W * 0.80f, H * 0.030f);
    for (int i = 0; i < 5; ++i) {
        p_texte_c(t, corps[i], mx, H * 0.640f + (float)i * c * 9.5f, c, C_BLANC, 0.95f);
    }

    p_texte_c(t, "MOINS DE SIX ANS : ACCOMPAGNE", mx, H * 0.845f,
              cellule("MOINS DE SIX ANS : ACCOMPAGNE", W * 0.80f, H * 0.026f),
              jaune, 0.85f);
}

/* ==========================================================================
 * MOTIF 7 — ORBITE. La réclame d'un jeu qui n'existe pas.
 * ==========================================================================
 * Une salle d'arcade annonce toujours un jeu qu'elle n'a pas encore. Celui-ci
 * est inventé de bout en bout — nom, promesse, emplacement —, et son affiche
 * est faite de ce que le graphisme vectoriel savait tracer : des étoiles, des
 * ellipses, un disque. Aucun vaisseau, aucun personnage : la planche parle
 * d'orbites, et une orbite n'appartient à personne.
 */
static void motif_orbite(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float haut[3] = { 0.006f, 0.007f, 0.016f };
    const float bas[3]  = { 0.002f, 0.003f, 0.008f };
    fond_degrade(t, haut, bas);

    /* Le champ d'étoiles, à trois tailles : un semis d'un seul calibre se lit
     * comme du bruit de capteur, pas comme un ciel. */
    for (unsigned i = 0; i < 320u; ++i) {
        const float x = bruit(i, 11u, 5u) * W;
        const float y = bruit(i, 12u, 5u) * H;
        const float q = bruit(i, 13u, 5u);
        const float r = (q > 0.965f) ? 2.1f : (q > 0.85f ? 1.35f : 0.75f);
        p_anneau(t, x, y, 0.0f, r, C_BLANC, 0.25f + q * 0.65f);
    }

    const float mx = W * 0.5f, my = H * 0.565f;

    /*
     * TROIS ORBITES, échantillonnées en 192 cordes. Une ellipse tracée par sa
     * distance implicite serait plus courte à écrire et donnerait une épaisseur
     * qui varie avec la courbure — sur une ellipse aussi plate que celles-ci,
     * les extrémités sortiraient deux fois plus grasses que les flancs.
     */
    static const float axes[3][2] = { { 0.400f, 0.132f },
                                      { 0.290f, 0.096f },
                                      { 0.178f, 0.059f } };
    static const float phases[3] = { 0.9f, 2.4f, 4.1f };
    for (int o = 0; o < 3; ++o) {
        const float ax = W * axes[o][0], az = W * axes[o][1];
        const float col[3] = { C_CYAN[0] * (0.55f + 0.22f * (float)o),
                               C_CYAN[1] * (0.55f + 0.22f * (float)o),
                               C_CYAN[2] * (0.55f + 0.22f * (float)o) };
        float px = mx + ax, py = my;
        for (int i = 1; i <= 192; ++i) {
            const float a = (float)i * 6.2831853f / 192.0f;
            const float qx = mx + ax * cosf(a), qy = my + az * sinf(a);
            p_trait(t, px, py, qx, qy, maxf(1.0f, W * 0.0022f), col, 0.95f);
            px = qx; py = qy;
        }
        /* Une sonde sur chaque orbite : un carré, pas un point — c'est ce qui
         * dit qu'un objet PARCOURT la trajectoire au lieu de la décorer. */
        const float a = phases[o];
        const float sx = mx + ax * cosf(a), sy = my + az * sinf(a);
        const float s = W * 0.011f;
        p_rect(t, sx - s, sy - s, sx + s, sy + s, C_AMBRE, 1.0f);
    }

    /* Le corps central. Un anneau clair sur son limbe supérieur suffit à lui
     * donner un volume ; un dégradé complet aurait demandé un éclairage, et un
     * éclairage sur une affiche au trait est un contresens. */
    p_anneau(t, mx, my, 0.0f, W * 0.088f, C_BLEU, 1.0f);
    p_anneau(t, mx, my, W * 0.070f, W * 0.088f, C_VIOLET, 0.55f);
    p_anneau(t, mx, my, W * 0.100f, W * 0.106f, C_CYAN, 0.75f);

    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.016f, C_CYAN, 0.9f);

    p_texte_c(t, "PROCHAINEMENT SUR LA RANGEE OUEST", mx, H * 0.062f,
              cellule("PROCHAINEMENT SUR LA RANGEE OUEST", W * 0.84f, H * 0.024f),
              C_CYAN, 1.0f);
    p_titre_c(t, "ORBITE 9", mx, H * 0.105f,
              cellule("ORBITE 9", W * 0.86f, H * 0.115f), C_BLANC, C_ENCRE);
    p_texte_c(t, "LE JEU QUI NE S'ARRETE PAS", mx, H * 0.222f,
              cellule("LE JEU QUI NE S'ARRETE PAS", W * 0.76f, H * 0.028f),
              C_VIOLET, 1.0f);

    static const char *const promesses[3] = { "UN JETON", "TROIS VIES", "AUCUNE SORTIE" };
    const float c = cellule("AUCUNE SORTIE", W * 0.60f, H * 0.040f);
    for (int i = 0; i < 3; ++i) {
        p_texte_c(t, promesses[i], mx, H * 0.828f + (float)i * c * 9.0f, c, C_AMBRE, 1.0f);
    }
}

/* ==========================================================================
 * MOTIF 8 — RECORDS. Le tableau du mois, affiché à côté du vrai.
 * ==========================================================================
 * Composition d'ÉCRAN, et c'est la seule : un dormant, une dalle noire, du
 * phosphore vert et des lignes de balayage. La salle a déjà une borne de
 * classement ; cette affiche est ce qu'on en imprime, avec la maladresse qui va
 * avec — on photographie l'écran et on colle le résultat au mur.
 */
static void motif_records(toile *t)
{
    const float W = (float)t->w, H = (float)t->h;
    const float fond[3] = { 0.030f, 0.030f, 0.034f };
    fond_uni(t, fond);
    p_cadre(t, 0.0f, 0.0f, W, H, W * 0.014f, C_VERT, 0.8f);

    const float mx = W * 0.5f;
    p_texte_c(t, "TABLEAU DU MOIS", mx, H * 0.048f,
              cellule("TABLEAU DU MOIS", W * 0.70f, H * 0.042f), C_VERT, 1.0f);

    /* Le dormant, puis la dalle. Le premier est un aplat gris ; c'est le
     * contraste entre les deux qui fait lire « écran » plutôt que « cadre ». */
    const float dx0 = W * 0.055f, dx1 = W - W * 0.055f;
    const float dy0 = H * 0.135f, dy1 = H * 0.855f;
    const float dormant[3] = { 0.075f, 0.075f, 0.080f };
    p_rect(t, dx0, dy0, dx1, dy1, dormant, 1.0f);
    const float ex0 = dx0 + W * 0.045f, ex1 = dx1 - W * 0.045f;
    const float ey0 = dy0 + W * 0.045f, ey1 = dy1 - W * 0.045f;
    const float dalle[3] = { 0.004f, 0.010f, 0.006f };
    p_rect(t, ex0, ey0, ex1, ey1, dalle, 1.0f);

    const float exm = (ex0 + ex1) * 0.5f;
    p_texte_c(t, "MEILLEURS SCORES", exm, ey0 + H * 0.030f,
              cellule("MEILLEURS SCORES", (ex1 - ex0) * 0.86f, H * 0.036f), C_VERT, 1.0f);
    p_rect(t, ex0 + W * 0.045f, ey0 + H * 0.082f, ex1 - W * 0.045f,
           ey0 + H * 0.082f + 2.0f, C_VERT, 0.65f);

    /*
     * Les huit lignes. Trois colonnes, dont la dernière est alignée à DROITE :
     * une colonne de scores alignée à gauche ne se compare pas, et c'est la
     * seule chose qu'on fait avec un tableau de scores.
     *
     * Les noms sont des initiales inventées. Il n'y a là aucun clin d'œil à
     * chercher : les cinq noms qu'on lisait sur l'affiche remplacée étaient
     * ceux des personnages d'une série, et c'est précisément ce qui la rendait
     * indistribuable.
     */
    static const char *const lignes[8][3] = {
        { "1", "LEA", "812340" }, { "2", "NOE", "774100" },
        { "3", "IVA", "690820" }, { "4", "ZOE", "651975" },
        { "5", "OMA", "604300" }, { "6", "RIK", "588640" },
        { "7", "YAN", "512205" }, { "8", "PAM", "497880" },
    };
    const float cl = cellule("MMMMMMMMMMM", (ex1 - ex0) * 0.82f, H * 0.034f);
    const float y0 = ey0 + H * 0.115f;
    const float pas = H * 0.0655f;
    for (int i = 0; i < 8; ++i) {
        const float y = y0 + pas * (float)i;
        /* La première ligne est plus claire : sur un tube, la ligne du haut du
         * tableau est celle qui clignote. */
        const float a = (i == 0) ? 1.0f : 0.80f;
        p_texte(t, lignes[i][0], ex0 + W * 0.055f, y, cl, C_VERT, a);
        p_texte(t, lignes[i][1], ex0 + W * 0.155f, y, cl, C_VERT, a);
        p_texte_d(t, lignes[i][2], ex1 - W * 0.055f, y, cl, C_VERT, a);
    }

    /*
     * LE BALAYAGE. Une ligne sombre sur quatre, à un quart d'opacité.
     *
     * Réglé par la mesure et non par le goût : à une ligne sur deux, la mip-map
     * du moteur ramène la dalle à un gris uniforme dès trois mètres et il ne
     * reste qu'un écran délavé. À une sur quatre il reste du noir entre les
     * lignes, la moyenne descend moins, et le lettrage garde son contraste.
     */
    for (float y = ey0; y < ey1; y += 4.0f) {
        p_rect(t, ex0, y, ex1, y + 1.6f, dalle, 0.55f);
    }
    /* Les coins d'un tube sont sombres : c'est ce qui empêche la dalle de se
     * lire comme un rectangle de papier vert. */
    for (int i = 0; i < 26; ++i) {
        const float u = (float)i / 25.0f;
        p_cadre(t, ex0 + u * W * 0.0f, ey0, ex1, ey1, 1.0f + u * W * 0.06f,
                dalle, 0.030f);
    }

    p_texte_c(t, "RELEVE LE PREMIER DE CHAQUE MOIS", mx, H * 0.895f,
              cellule("RELEVE LE PREMIER DE CHAQUE MOIS", W * 0.84f, H * 0.026f),
              C_VERT, 0.9f);
    p_texte_c(t, "LE VOTRE SE VALIDE AU COMPTOIR", mx, H * 0.940f,
              cellule("LE VOTRE SE VALIDE AU COMPTOIR", W * 0.80f, H * 0.024f),
              C_VERT, 0.7f);
}

/* ==========================================================================
 * Le programme
 * ========================================================================== */

typedef struct motif_def {
    const char *nom;
    const char *quoi;
} motif_def;

static const motif_def g_motifs[] = {
    { "tournoi",   "l'annonce du tournoi de la saison"        },
    { "reglement", "le reglement de la salle"                 },
    { "jetons",    "la promotion du monnayeur"                },
    { "concert",   "un concert sur l'estrade"                 },
    { "plan",      "le plan de la salle, lu dans sa description" },
    { "attention", "l'avertissement lumieres clignotantes"    },
    { "orbite",    "la reclame d'un jeu invente"              },
    { "records",   "le tableau des meilleurs scores"          },
};

static void ecrire(const toile *t, const char *chemin)
{
    unsigned char *out = (unsigned char *)malloc((size_t)t->w * (size_t)t->h * 3u);
    if (!out) tool_fatalf("mémoire épuisée à l'écriture");
    for (size_t i = 0; i < (size_t)t->w * (size_t)t->h * 3u; ++i) {
        out[i] = (unsigned char)(powf(clamp01(t->px[i]), 1.0f / 2.2f) * 255.0f + 0.5f);
    }
    if (!stbi_write_png(chemin, t->w, t->h, 3, out, t->w * 3)) {
        tool_fatalf("écriture impossible : %s", chemin);
    }
    free(out);
}

int main(int argc, char **argv)
{
    int width = 820, height = 1024;
    const char *motif = NULL, *salle = NULL, *out_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--motif=", 8) == 0)        motif = argv[i] + 8;
        else if (strncmp(argv[i], "--salle=", 8) == 0)   salle = argv[i] + 8;
        else if (strncmp(argv[i], "--width=", 8) == 0)   width = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--height=", 9) == 0)  height = atoi(argv[i] + 9);
        else if (!out_path) out_path = argv[i];
        else tool_fatalf("argument inattendu : %s", argv[i]);
    }

    if (!motif || !out_path) {
        fprintf(stderr,
            "posterart — dessine une affiche murale de la salle\n"
            "usage : %s --motif=NOM [options] <sortie.png>\n"
            "  --motif=NOM      le motif à dessiner :\n", argv[0]);
        for (size_t i = 0; i < sizeof g_motifs / sizeof g_motifs[0]; ++i) {
            fprintf(stderr, "                     %-10s %s\n",
                    g_motifs[i].nom, g_motifs[i].quoi);
        }
        fprintf(stderr,
            "  --salle=FICHIER  la description de la salle (motif « plan » seulement)\n"
            "  --width=N --height=N  dimensions (défaut 820x1024, soit le 0,777 x\n"
            "                   0,971 m des cadres relevés en 2020)\n");
        return 2;
    }
    /* 256 pixels de côté au minimum : en dessous, une cellule de fonte tombe à
     * un pixel et le texte des affiches devient un peigne. */
    if (width < 256 || height < 256) tool_fatalf("planche trop petite (%dx%d)", width, height);
    if (width > 8192 || height > 8192) tool_fatalf("planche trop grande (%dx%d)", width, height);

    toile t;
    toile_init(&t, width, height);

    if (strcmp(motif, "tournoi") == 0)        motif_tournoi(&t);
    else if (strcmp(motif, "reglement") == 0) motif_reglement(&t);
    else if (strcmp(motif, "jetons") == 0)    motif_jetons(&t);
    else if (strcmp(motif, "concert") == 0)   motif_concert(&t);
    else if (strcmp(motif, "plan") == 0) {
        if (!salle) tool_fatalf("le motif « plan » demande --salle=<salle.room.json> : "
                                "il TRACE la salle, il ne la dessine pas de mémoire");
        motif_plan(&t, salle);
    }
    else if (strcmp(motif, "attention") == 0) motif_attention(&t);
    else if (strcmp(motif, "orbite") == 0)    motif_orbite(&t);
    else if (strcmp(motif, "records") == 0)   motif_records(&t);
    else tool_fatalf("motif inconnu : « %s »", motif);

    finition(&t);
    ecrire(&t, out_path);

    printf("posterart %s : %dx%d\n", motif, width, height);
    tool_infof("%s", out_path);
    free(t.px);
    return 0;
}
