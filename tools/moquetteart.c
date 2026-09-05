/*
 * moquetteart — le tapis néon de la salle, dessiné.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * Parce que `legacy/room/textures/floor.jpg` — la moquette du hall, la plus
 * grande surface de la salle, celle qu'on voit sur toutes les captures —
 * PORTE DES PERSONNAGES DE TIERS. Ouverte et agrandie :
 *
 *   - un PAC-MAN au néon jaune : le disque, la part de camembert retiree, la
 *     bouche ouverte vers la droite. Ce n'est pas une allusion, c'est le
 *     personnage ;                                              Bandai Namco
 *   - un de ses FANTOMES au néon rose : le dôme arrondi, la jupe ondulée à
 *     trois vagues, le creux de l'oeil ;                        Bandai Namco
 *   - les CERISES, qui sont le premier bonus du même jeu ;       Bandai Namco
 *   - une MANETTE de console reconnaissable à sa silhouette : deux poignées,
 *     la croix directionnelle à gauche, les quatre boutons à droite et les
 *     deux sticks au centre.                                    Sony
 *
 * C'est la même faute que les huit affiches et que les planches des mini-jeux,
 * au dernier endroit où personne n'avait regardé : le SOL. On venait de retirer
 * Pac-Man du jeu de labyrinthe et de le débaptiser ; il restait allumé sous les
 * pieds du joueur, dans chaque image de la salle.
 *
 * Ce qui est gardé, et ce n'est pas rien
 * --------------------------------------
 * `salle.room.json` documente longuement pourquoi ce tapis a été REMIS après
 * avoir été abandonné : mesuré en linéaire il réfléchit 0,0392 contre 0,0292
 * pour la boucle bordeaux qui l'avait remplacé, et c'est « l'objet le plus
 * reconnaissable de la salle ». Cette décision-là est juste et on la garde
 * ENTIÈREMENT : un tissage sombre, des motifs fluo, et l'émissif à 0,10 qui ne
 * fait luire que les motifs parce que `gbuffer.frag` multiplie l'émissif par
 * l'albédo texel par texel. Ce sont les FIGURES qui changent, pas le tapis.
 *
 * Un tapis d'arcade au néon appartient à tout le monde : c'est un genre, comme
 * la grille en perspective des affiches. Ses ICONES sont donc redessinées et
 * choisies pour n'appartenir à personne — un manche, un jeton, une étoile, un
 * éclair, un dé, une note, une cible, une planète. Aucun personnage, aucun
 * logo, aucune silhouette de matériel identifiable.
 *
 * Comment c'est dessiné
 * ---------------------
 * En SPLATS le long des tracés. Chaque primitive — segment, cercle, ellipse —
 * est parcourue à pas fixe et dépose un noyau radial : un coeur plein qui donne
 * le trait, et une décroissance qui donne le halo. C'est exactement ce qu'est un
 * tube au néon vu de loin, ça se règle avec deux rayons, et surtout ça marche
 * pour n'importe quelle forme sans écrire un rastériseur par primitive.
 *
 * L'accumulation est ADDITIVE et se sature en fin de course : deux traits qui se
 * croisent brillent plus fort à leur croisement, comme deux tubes superposés.
 *
 * Le raccord
 * ----------
 * La moquette est RÉPÉTÉE — `uvMetres` 1,6 dans `salle.room.json` — donc tout
 * déborde d'un bord revient par l'autre. Les splats et le tissage sont écrits
 * modulo la largeur et la hauteur, sans exception : un seul dessin non cyclique
 * met une couture qui traverse toute la salle, et on ne la voit qu'une fois
 * posée.
 *
 * La taille
 * ---------
 * 377 x 510, exactement celle de l'image remplacée. `uvMetres` donne l'emprise
 * en mètres d'une répétition ; changer le rapport de la texture changerait donc
 * l'échelle du motif au sol, et il faudrait re-régler une valeur qui a été
 * mesurée. On ne re-règle pas ce qui n'a pas besoin de l'être.
 */
#include "tools_common.h"

#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ==========================================================================
 * La toile : de la LUMIÈRE accumulée, encodée en gamma à l'écriture
 * ==========================================================================
 * Contrairement aux planches de sprites, ici on additionne des halos : ça ne
 * veut dire quelque chose qu'en LINÉAIRE. L'encodage gamma se fait une seule
 * fois, en bas de ce fichier — la même règle que `posterart`.
 */
typedef struct toile {
    int w, h;
    float *px;                  /* w * h * 3, linéaire */
} toile;

static void toile_init(toile *t, int w, int h)
{
    t->w = w; t->h = h;
    t->px = (float *)calloc((size_t)w * (size_t)h * 3u, sizeof(float));
    if (!t->px) tool_fatalf("mémoire épuisée (%d x %d)", w, h);
}

/* Cyclique dans les deux sens : voir « Le raccord » en tête de fichier. */
static void ajoute(toile *t, int x, int y, const float rgb[3], float k)
{
    x %= t->w; if (x < 0) x += t->w;
    y %= t->h; if (y < 0) y += t->h;
    float *p = &t->px[((size_t)y * (size_t)t->w + (size_t)x) * 3u];
    p[0] += rgb[0] * k;
    p[1] += rgb[1] * k;
    p[2] += rgb[2] * k;
}

/* ==========================================================================
 * Le néon : un noyau radial déposé le long d'un tracé
 * ==========================================================================
 * `coeur` est le rayon du tube lui-même, `halo` celui de la lueur. Entre les
 * deux, une décroissance en carré — c'est ce qui donne l'auréole molle d'un
 * tube, plutôt que le bord net d'un trait plein.
 */
static void splat(toile *t, float cx, float cy, const float rgb[3],
                  float coeur, float halo)
{
    const int r = (int)ceilf(halo) + 1;
    const int x0 = (int)floorf(cx), y0 = (int)floorf(cy);
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const float ddx = (float)(x0 + dx) + 0.5f - cx;
            const float ddy = (float)(y0 + dy) + 0.5f - cy;
            const float d = sqrtf(ddx * ddx + ddy * ddy);
            if (d >= halo) continue;
            float k;
            if (d <= coeur) {
                k = 1.0f;
            } else {
                const float u = 1.0f - (d - coeur) / (halo - coeur);
                k = u * u * 0.42f;
            }
            ajoute(t, x0 + dx, y0 + dy, rgb, k * 0.30f);
        }
    }
}

/* Le pas d'échantillonnage : un tiers de pixel. Plus grossier, un tracé
 * oblique se lit comme un chapelet de perles ; plus fin, on paie sans rien
 * voir de plus. */
#define PAS 0.34f

static void segment(toile *t, float ax, float ay, float bx, float by,
                    const float rgb[3], float coeur, float halo)
{
    const float dx = bx - ax, dy = by - ay;
    const float len = sqrtf(dx * dx + dy * dy);
    const int n = (int)(len / PAS) + 1;
    for (int i = 0; i <= n; ++i) {
        const float u = (float)i / (float)n;
        splat(t, ax + dx * u, ay + dy * u, rgb, coeur, halo);
    }
}

/* Un arc d'ellipse, en radians. Sert aussi de cercle (a == b, tour complet) et
 * d'anneau de planète. */
static void arc(toile *t, float cx, float cy, float a, float b,
                float t0, float t1, const float rgb[3], float coeur, float halo)
{
    const float perimetre = 3.1415927f * (a + b);          /* Ramanujan grossier, ça suffit */
    const int n = (int)(perimetre * fabsf(t1 - t0) / 6.2831853f / PAS) + 8;
    for (int i = 0; i <= n; ++i) {
        const float u = t0 + (t1 - t0) * (float)i / (float)n;
        splat(t, cx + a * cosf(u), cy + b * sinf(u), rgb, coeur, halo);
    }
}

static void disque(toile *t, float cx, float cy, float r,
                   const float rgb[3], float halo)
{
    for (float rr = 0.0f; rr <= r; rr += PAS)
        arc(t, cx, cy, rr, rr, 0.0f, 6.2831853f, rgb, 1.0f, halo);
}

static void polyligne(toile *t, const float *pts, int n, bool ferme,
                      const float rgb[3], float coeur, float halo)
{
    for (int i = 0; i + 1 < n; ++i)
        segment(t, pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3],
                rgb, coeur, halo);
    if (ferme && n > 2)
        segment(t, pts[(n - 1) * 2], pts[(n - 1) * 2 + 1], pts[0], pts[1],
                rgb, coeur, halo);
}

/* ==========================================================================
 * Les huit icônes, et ce qu'elles ne sont pas
 * ==========================================================================
 * Chacune est un OBJET de salle d'arcade, pas un personnage : c'est la seule
 * règle, et elle décide tout le reste. On peut dessiner une salle d'arcade
 * entière sans dessiner le héros de personne.
 *
 * Chaque fonction dessine dans un carré de côté `s` centré sur (cx, cy), pour
 * que la table de placement plus bas n'ait à connaître ni les proportions ni
 * les tracés.
 */

/* Le MANCHE : boule, fût, embase. La forme d'un contrôle, pas d'un jeu. */
static void icone_manche(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    arc(t, cx, cy - 30.0f * k, 15.0f * k, 15.0f * k, 0.0f, 6.2831853f, c, 1.6f * k, 5.0f * k);
    segment(t, cx, cy - 16.0f * k, cx, cy + 18.0f * k, c, 1.6f * k, 5.0f * k);
    arc(t, cx, cy + 26.0f * k, 30.0f * k, 12.0f * k, 0.0f, 6.2831853f, c, 1.6f * k, 5.0f * k);
}

/* Le JETON : deux cercles et une fente. Un jeton d'arcade est un objet nu. */
static void icone_jeton(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    arc(t, cx, cy, 38.0f * k, 38.0f * k, 0.0f, 6.2831853f, c, 1.8f * k, 5.5f * k);
    arc(t, cx, cy, 26.0f * k, 26.0f * k, 0.0f, 6.2831853f, c, 1.2f * k, 5.0f * k);
    segment(t, cx - 12.0f * k, cy, cx + 12.0f * k, cy, c, 1.6f * k, 5.0f * k);
}

/* L'ÉTOILE à cinq branches, tracée d'un trait. */
static void icone_etoile(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    float p[20];
    for (int i = 0; i < 10; ++i) {
        const float a = -1.5707963f + 6.2831853f * (float)i / 10.0f;
        const float r = (i & 1) ? 17.0f * k : 40.0f * k;
        p[i * 2]     = cx + r * cosf(a);
        p[i * 2 + 1] = cy + r * sinf(a);
    }
    polyligne(t, p, 10, true, c, 1.8f * k, 5.5f * k);
}

/* L'ÉCLAIR : le zigzag, tracé fermé. */
static void icone_eclair(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    const float p[14] = {
        cx +  6.0f * k, cy - 42.0f * k,
        cx - 22.0f * k, cy -  4.0f * k,
        cx -  4.0f * k, cy -  4.0f * k,
        cx - 10.0f * k, cy + 42.0f * k,
        cx + 22.0f * k, cy -  2.0f * k,
        cx +  4.0f * k, cy -  2.0f * k,
        cx + 16.0f * k, cy - 42.0f * k,
    };
    polyligne(t, p, 7, true, c, 1.8f * k, 5.5f * k);
}

/* Le DÉ : un carré aux coins coupés et cinq points. */
static void icone_de(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    const float a = 34.0f * k, b = 26.0f * k;
    const float p[16] = {
        cx - b, cy - a,  cx + b, cy - a,
        cx + a, cy - b,  cx + a, cy + b,
        cx + b, cy + a,  cx - b, cy + a,
        cx - a, cy + b,  cx - a, cy - b,
    };
    polyligne(t, p, 8, true, c, 1.8f * k, 5.5f * k);
    static const float PIP[5][2] = { { -16, -16 }, { 16, -16 }, { 0, 0 },
                                     { -16, 16 }, { 16, 16 } };
    for (int i = 0; i < 5; ++i)
        disque(t, cx + PIP[i][0] * k, cy + PIP[i][1] * k, 4.0f * k, c, 5.0f * k);
}

/* La NOTE : une hampe, une tête, un crochet. */
static void icone_note(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    segment(t, cx + 8.0f * k, cy - 40.0f * k, cx + 8.0f * k, cy + 22.0f * k, c, 1.8f * k, 5.5f * k);
    arc(t, cx - 6.0f * k, cy + 26.0f * k, 15.0f * k, 11.0f * k, 0.0f, 6.2831853f,
        c, 1.6f * k, 5.0f * k);
    disque(t, cx - 6.0f * k, cy + 26.0f * k, 9.0f * k, c, 5.0f * k);
    arc(t, cx + 8.0f * k, cy - 26.0f * k, 20.0f * k, 16.0f * k, -1.5f, 0.6f, c, 1.6f * k, 5.0f * k);
}

/* La CIBLE : trois anneaux et un centre. */
static void icone_cible(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    arc(t, cx, cy, 40.0f * k, 40.0f * k, 0.0f, 6.2831853f, c, 1.8f * k, 5.5f * k);
    arc(t, cx, cy, 26.0f * k, 26.0f * k, 0.0f, 6.2831853f, c, 1.4f * k, 5.0f * k);
    arc(t, cx, cy, 13.0f * k, 13.0f * k, 0.0f, 6.2831853f, c, 1.4f * k, 5.0f * k);
    disque(t, cx, cy, 5.0f * k, c, 5.0f * k);
}

/* La PLANÈTE à anneau : un cercle et une ellipse inclinée. */
static void icone_planete(toile *t, float cx, float cy, float s, const float c[3])
{
    const float k = s / 100.0f;
    arc(t, cx, cy, 24.0f * k, 24.0f * k, 0.0f, 6.2831853f, c, 1.8f * k, 5.5f * k);
    /* L'anneau, incliné : deux demi-arcs d'une ellipse tournée de 0,32 rad.
     * Tracé point par point pour ne pas avoir à écrire une rotation dans
     * `arc`, qui n'en a besoin nulle part ailleurs. */
    const float ca = cosf(0.32f), sa = sinf(0.32f);
    const int n = 220;
    for (int i = 0; i <= n; ++i) {
        const float u = 6.2831853f * (float)i / (float)n;
        const float ex = 44.0f * k * cosf(u), ey = 13.0f * k * sinf(u);
        splat(t, cx + ex * ca - ey * sa, cy + ex * sa + ey * ca, c, 1.4f * k, 5.0f * k);
    }
}

/* ==========================================================================
 * Le tissage
 * ==========================================================================
 * Une moquette bouclée, vue de dessus, est un bruit à grain fin, sombre et à
 * peine coloré. Le grain est produit par un mélange ENTIER — deux builds
 * doivent rendre le même fichier octet pour octet, et `rand()` ne le garantit
 * pas d'une bibliothèque C à l'autre.
 *
 * Le grain tombe sur une trame de deux pixels : c'est ce qui le fait lire comme
 * une boucle de fil, et non comme du bruit de capteur.
 */
static uint32_t melange(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void tissage(toile *t)
{
    for (int y = 0; y < t->h; ++y) {
        for (int x = 0; x < t->w; ++x) {
            const uint32_t h = melange((uint32_t)(x / 3) * 73856093u
                                     ^ (uint32_t)(y / 3) * 19349663u);
            const float n = (float)(h & 0xffffu) / 65535.0f;
            /* 0,012 à 0,040 en linéaire : le tissage seul ressort autour de
             * 0,024, et les motifs montent la moyenne aux 0,039 mesurés sur
             * l'image d'origine. C'est ce chiffre-là que `salle.room.json`
             * documente, et il ne doit pas bouger. */
            const float v = 0.044f + 0.048f * n * n;
            float *p = &t->px[((size_t)y * (size_t)t->w + (size_t)x) * 3u];
            p[0] = v * 1.00f;
            p[1] = v * 0.96f;
            p[2] = v * 0.98f;
        }
    }
}

/* ==========================================================================
 * Le placement
 * ==========================================================================
 * Huit icônes sur une trame de deux colonnes et quatre rangées, décalées d'une
 * demi-case une rangée sur deux. La trame régulière est ce qui fait qu'une
 * moquette RÉPÉTÉE ne montre pas d'alignement : un semis « au hasard » produit
 * des paquets et des trous qui, une fois carrelés, dessinent une grille.
 *
 * Les teintes sont celles des néons de la salle — le cyan de l'enseigne, le
 * magenta du sol, l'ambre, le vert, le violet — parce qu'un tapis peint dans
 * des couleurs qui n'existent nulle part ailleurs se lit comme une pièce
 * rapportée. Ce sont des valeurs LINÉAIRES.
 */
typedef void (*icone_fn)(toile *, float, float, float, const float *);

int main(int argc, char **argv)
{
    int width = 377, height = 510;
    const char *out_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--width=", 8) == 0)       width = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--height=", 9) == 0) height = atoi(argv[i] + 9);
        else if (!out_path) out_path = argv[i];
        else tool_fatalf("argument inattendu : %s", argv[i]);
    }
    if (!out_path) {
        fprintf(stderr,
            "moquetteart — dessine le tapis neon du hall\n"
            "usage : %s [--width=N --height=N] <sortie.png>\n"
            "  defaut 377 x 510, la taille de l'image remplacee : `uvMetres`\n"
            "  donne l'emprise d'une repetition, donc changer le rapport\n"
            "  changerait l'echelle du motif au sol.\n", argv[0]);
        return 2;
    }
    if (width < 64 || height < 64 || width > 4096 || height > 4096)
        tool_fatalf("taille hors bornes (%dx%d)", width, height);

    static const float CYAN[3]    = { 0.16f, 0.72f, 1.00f };
    static const float MAGENTA[3] = { 1.00f, 0.14f, 0.52f };
    static const float AMBRE[3]   = { 1.00f, 0.72f, 0.12f };
    static const float VERT[3]    = { 0.24f, 0.94f, 0.42f };
    static const float VIOLET[3]  = { 0.62f, 0.38f, 1.00f };
    static const float BLANC[3]   = { 0.80f, 0.90f, 1.00f };

    /* Colonne, rangée, icône, teinte, taille relative. Huit entrées, deux
     * colonnes, quatre rangées : la table EST le dessin. */
    static const struct { int col, rang; icone_fn f; const float *teinte; float taille; }
    SEMIS[] = {
        { 0, 0, icone_manche,  AMBRE,   0.86f },
        { 1, 0, icone_jeton,   CYAN,    0.74f },
        { 0, 1, icone_eclair,  BLANC,   0.78f },
        { 1, 1, icone_planete, VIOLET,  0.90f },
        { 0, 2, icone_etoile,  MAGENTA, 0.80f },
        { 1, 2, icone_cible,   VERT,    0.76f },
        { 0, 3, icone_note,    CYAN,    0.78f },
        { 1, 3, icone_de,      AMBRE,   0.74f },
    };

    toile t;
    toile_init(&t, width, height);
    tissage(&t);

    const float case_w = (float)width / 2.0f;
    const float case_h = (float)height / 4.0f;
    const float cote = (case_w < case_h ? case_w : case_h) * 0.92f;

    for (size_t i = 0; i < sizeof SEMIS / sizeof SEMIS[0]; ++i) {
        /* Le demi-décalage une rangée sur deux : c'est lui qui empêche les
         * icônes de former des colonnes visibles une fois le tapis carrelé. */
        const float dec = (SEMIS[i].rang & 1) ? case_w * 0.5f : 0.0f;
        const float cx = ((float)SEMIS[i].col + 0.5f) * case_w + dec;
        const float cy = ((float)SEMIS[i].rang + 0.5f) * case_h;
        SEMIS[i].f(&t, cx, cy, cote * SEMIS[i].taille, SEMIS[i].teinte);
    }

    /* L'encodage gamma, une seule fois, avec saturation. Le coeur d'un tube au
     * néon EST saturé — c'est ce qui lui donne son blanc au centre. */
    unsigned char *px = (unsigned char *)malloc((size_t)width * (size_t)height * 3u);
    if (!px) tool_fatalf("mémoire épuisée");
    double somme = 0.0;
    for (size_t i = 0; i < (size_t)width * (size_t)height * 3u; ++i) {
        float v = t.px[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        somme += (double)v;
        const float s = (v <= 0.0031308f) ? v * 12.92f
                                          : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
        px[i] = (unsigned char)(s * 255.0f + 0.5f);
    }
    if (!stbi_write_png(out_path, width, height, 3, px, width * 3))
        tool_fatalf("écriture impossible : %s", out_path);

    /* La réflectance moyenne, IMPRIMÉE. `salle.room.json` documente 0,0392
     * pour l'image remplacée, mesurée en linéaire ; c'est le chiffre à ne pas
     * perdre, et le seul moyen de ne pas le perdre est de le regarder. */
    printf("moquetteart : %dx%d, réflectance linéaire moyenne %.4f\n",
           width, height, somme / ((double)width * (double)height * 3.0));
    tool_infof("%s", out_path);
    free(px);
    free(t.px);
    return 0;
}
