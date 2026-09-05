/*
 * skinart — HABILLE le personnage : sa texture est peinte, pas téléchargée.
 *
 * Le problème, et il se voit en une image
 * --------------------------------------
 * `assets/models/personnage/personnage.glb` est CesiumMan, le mannequin
 * d'exemple de Khronos. Le choix du modèle était bon — licence claire, peau et
 * animation dans un seul fichier, dix-neuf os, 3 273 sommets : voir
 * `assets/cc0/LICENSES.md` § « Le personnage ». Ce qui ne l'était pas, c'est
 * son APPARENCE : la texture qu'il porte est le LOGOTYPE de Cesium, des rubans
 * bleus et verts sur fond blanc. Un joueur qui le voit à la troisième personne
 * ne voit pas un personnage, il voit un fichier de test.
 *
 * Pourquoi le peindre plutôt que le remplacer
 * -------------------------------------------
 * Parce que la mesure dit que c'est possible, et elle a été faite avant de
 * choisir. Trois chiffres, tous vérifiables en relançant cet outil avec
 * `--mesures` :
 *
 *   - les UV existent, tiennent dans [0,1] et couvrent **58,4 %** de la
 *     planche ;
 *   - **aucun texel n'est couvert deux fois** : le dépliage est propre, une
 *     couleur posée quelque part n'apparaît pas ailleurs sur le corps ;
 *   - la densité de texels varie d'un facteur 4,3 entre le cinquième et le
 *     quatre-vingt-quinzième centile, ce qui est une variation ordinaire de
 *     dépliage — pas un atlas dégénéré.
 *
 * Un damier rendu dans le jeu l'a confirmé à l'œil : les cases tombent carrées
 * et de taille comparable sur la tête, le torse, les bras et les jambes.
 * Redessiner un personnage entier en C — squelette, poids, cycle — aurait été
 * dix fois le travail pour un résultat qui, mal fait, aurait été PIRE que
 * CesiumMan habillé.
 *
 * Comment on peint sans jamais regarder l'atlas
 * ---------------------------------------------
 * C'est le point qui rend la chose faisable. On ne dessine pas « une veste
 * quelque part dans le carré » : on RASTÉRISE le maillage DANS l'espace UV, ce
 * qui donne pour chaque texel la position et la normale du point du corps qui
 * lui correspond. La peinture ne raisonne ensuite qu'en coordonnées de CORPS —
 * « à 53 % de la hauteur, à moins de 24 % de l'envergure du bras » — et se
 * moque complètement de la façon dont les îlots sont posés dans la planche.
 *
 * Conséquence directe : un ourlet de veste posé à une hauteur donnée tombe à la
 * MÊME hauteur des deux côtés d'une couture, parce que les deux côtés de la
 * couture sont à la même hauteur dans le corps. Aucune retouche de raccord
 * n'est nécessaire, et il n'y en a aucune.
 *
 * Et les COTES ne sont pas devinées non plus : la hauteur, l'envergure, la
 * tête, les poignets et les pieds sont MESURÉS sur le maillage (voir
 * `mesurer`), et tout le reste s'exprime en fraction de ces mesures. Un modèle
 * exporté à une autre échelle s'habille donc tout seul.
 *
 * Ce que l'outil écrit
 * --------------------
 * Le GLB lui-même : la planche remplace l'image embarquée, tout le reste du
 * fichier est recopié tel quel. C'est la seule sortie qui ne demande RIEN à la
 * chaîne de build — `assets/CMakeLists.txt` copie déjà `personnage.glb` vers le
 * répertoire d'assets, et il continue de le faire sans savoir que l'image a
 * changé. Le prix est que le fichier versionné devient une ŒUVRE DÉRIVÉE de
 * CesiumMan, ce que CC BY 4.0 oblige à déclarer : c'est fait dans
 * `assets/cc0/LICENSES.md`, § « Le personnage ».
 *
 * L'outil est donc joué À LA MAIN, et son résultat versionné — comme
 * `assets/blender/import_cc0.py`, et pour la même raison : il modifie une
 * source, et une source ne se modifie pas pendant un build.
 */
#include "glb_image.h"
#include "tools_common.h"

/* cgltf est un en-tête à implémentation unique : c'est ici qu'il se déplie,
 * comme dans `geo_import.c` et `bvhbake.c`. */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ------------------------------------------------------------ arithmétique */

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* Rampe adoucie entre `a` et `b` : sert partout où une frontière de vêtement ne
 * doit pas créneler. Un bord franc dans une planche de mille pixels se voit à
 * l'écran comme un escalier, et le JPEG l'entoure en plus d'un halo. */
static float smoothstep(float a, float b, float x)
{
    if (a == b) return x < a ? 0.0f : 1.0f;
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

/* Une bande : 1 à l'intérieur de [a,b], 0 dehors, avec `f` de fondu de chaque
 * côté. C'est la brique de tous les liserés — ourlets, poignets, coutures. */
static float bande(float x, float a, float b, float f)
{
    return smoothstep(a - f, a + f, x) * (1.0f - smoothstep(b - f, b + f, x));
}

/* Bruit déterministe dans [-1,1]. Déterministe est le mot important : la
 * planche doit ressortir à l'identique sur les trois plateformes, sinon deux
 * machines produisent deux personnages et la différence d'image cesse d'être
 * attribuable. */
static float grain(int x, int y, unsigned sel)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ sel * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xFFFFu) / 32767.5f - 1.0f;
}

typedef struct rvb { float r, g, b; } rvb;

static rvb rvb_make(float r, float g, float b) { rvb c; c.r = r; c.g = g; c.b = b; return c; }
static rvb rvb_mix(rvb a, rvb b, float t)
{
    return rvb_make(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t));
}
static rvb rvb_scale(rvb a, float k) { return rvb_make(a.r * k, a.g * k, a.b * k); }

/* ------------------------------------------------- le maillage, en mémoire */

typedef struct sommet {
    float p[3];     /* position au repos */
    float n[3];     /* normale au repos */
    float uv[2];
    int   os;       /* os DOMINANT : celui dont le poids est le plus fort */
} sommet;

typedef struct maillage {
    sommet   *v;
    uint32_t  vcount;
    uint32_t *idx;
    uint32_t  icount;
    int       os_count;
} maillage;

/*
 * LES COTES DU CORPS, mesurées et non écrites en dur.
 *
 * Tout ce que la peinture emploie ensuite est une FRACTION de ces valeurs. Le
 * seul repère « anatomique » qui ne soit pas une fraction de la hauteur est la
 * tête, et elle n'est pas devinée non plus : c'est l'os qui porte les sommets
 * les plus HAUTS. Ça évite de dépendre du nom des os — la convention de
 * nommage change d'un exportateur à l'autre, la géométrie non.
 */
typedef struct anatomie {
    float sol;          /* z du point le plus bas */
    float haut;         /* hauteur totale */
    float envergure;    /* max |y| : du plan médian au bout des doigts */
    int   os_tete;
    float menton;       /* z du bas de la tête */
    float tete_cx, tete_cz;             /* centre de la tête */
    float tete_rx, tete_ry, tete_rz;    /* demi-dimensions de la tête */
    float poignet;      /* |y| où la manche s'arrête */
    float cou_pied;     /* z du haut du pied : la chaussure va jusque-là */
} anatomie;

/* ------------------------------------------------------------ la planche */

/*
 * Chaque texel porte le point du corps qu'il habille. C'est CE tableau qui
 * permet de peindre en coordonnées de corps sans jamais savoir où les îlots
 * sont posés.
 */
typedef struct carte {
    int    taille;
    float *p;       /* 3 par texel */
    float *n;       /* 3 par texel */
    /* 0 : vide ; 1 : couvert par la marge d'un triangle ; 2 : couvert
     * franchement ; 3 : rempli par dilatation. */
    uint8_t *plein;
} carte;

static void carte_init(carte *c, int taille)
{
    c->taille = taille;
    const size_t n = (size_t)taille * (size_t)taille;
    c->p = (float *)calloc(n * 3, sizeof(float));
    c->n = (float *)calloc(n * 3, sizeof(float));
    c->plein = (uint8_t *)calloc(n, 1);
    if (!c->p || !c->n || !c->plein) tool_fatalf("mémoire épuisée (carte %dx%d)", taille, taille);
}

static void carte_free(carte *c) { free(c->p); free(c->n); free(c->plein); }

/*
 * Rastérisation d'un triangle DANS L'ESPACE UV.
 *
 * On garde le premier triangle qui couvre un texel plutôt que de mélanger :
 * la mesure dit qu'aucun texel n'est couvert deux fois sur ce modèle, donc
 * « premier » et « seul » sont la même chose ici. Le compteur de recouvrement
 * est rendu quand même, parce que le jour où il cesse d'être nul, la peinture
 * devient fausse et il vaut mieux le lire que le découvrir à l'écran.
 */
static void rasteriser(carte *c, const maillage *m, uint32_t *recouverts)
{
    const int T = c->taille;
    for (uint32_t t = 0; t + 2 < m->icount; t += 3) {
        const sommet *s[3] = { &m->v[m->idx[t]], &m->v[m->idx[t + 1]], &m->v[m->idx[t + 2]] };
        float xs[3], ys[3];
        for (int k = 0; k < 3; ++k) {
            xs[k] = s[k]->uv[0] * (float)T;
            /* La convention glTF met v = 0 en HAUT de l'image, comme les
             * formats d'image eux-mêmes : la planche s'écrit donc ligne 0 en
             * premier, sans retournement. */
            ys[k] = s[k]->uv[1] * (float)T;
        }
        const float den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2]);
        if (fabsf(den) < 1e-9f) continue;

        int x0 = (int)floorf(minf(minf(xs[0], xs[1]), xs[2]));
        int x1 = (int)ceilf(maxf(maxf(xs[0], xs[1]), xs[2]));
        int y0 = (int)floorf(minf(minf(ys[0], ys[1]), ys[2]));
        int y1 = (int)ceilf(maxf(maxf(ys[0], ys[1]), ys[2]));
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 > T - 1) x1 = T - 1; if (y1 > T - 1) y1 = T - 1;

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = (float)x + 0.5f, py = (float)y + 0.5f;
                float l0 = ((ys[1] - ys[2]) * (px - xs[2]) + (xs[2] - xs[1]) * (py - ys[2])) / den;
                float l1 = ((ys[2] - ys[0]) * (px - xs[2]) + (xs[0] - xs[2]) * (py - ys[2])) / den;
                float l2 = 1.0f - l0 - l1;
                /* Marge d'un demi-texel : sans elle, deux triangles voisins
                 * laissent une couture d'un texel non peint entre eux, et cette
                 * couture ressort en blanc à l'écran. */
                if (l0 < -0.001f || l1 < -0.001f || l2 < -0.001f) continue;
                const size_t o = (size_t)y * (size_t)T + (size_t)x;
                /* STRICTEMENT dedans : le centre du texel est dans le triangle
                 * et non sur son bord élargi. Deux triangles STRICTEMENT sur le
                 * même texel, c'est un vrai chevauchement d'îlots ; deux
                 * triangles qui s'y touchent par la marge, non. */
                const bool franc = (l0 > 0.001f && l1 > 0.001f && l2 > 0.001f);
                if (c->plein[o]) {
                    if (recouverts && franc && c->plein[o] == 2u) ++*recouverts;
                    continue;
                }
                c->plein[o] = franc ? 2u : 1u;
                for (int k = 0; k < 3; ++k) {
                    c->p[o * 3 + (size_t)k] = l0 * s[0]->p[k] + l1 * s[1]->p[k] + l2 * s[2]->p[k];
                    c->n[o * 3 + (size_t)k] = l0 * s[0]->n[k] + l1 * s[1]->n[k] + l2 * s[2]->n[k];
                }
            }
        }
    }
}

/*
 * DILATATION du bord des îlots.
 *
 * Le filtrage de texture et les mipmaps vont chercher des texels HORS de
 * l'îlot dès qu'on s'éloigne. Si ces texels-là sont vides, le bord de chaque
 * îlot se borde d'un liseré de la couleur du vide — c'est le défaut classique
 * du « seam bleeding », et sur un personnage il dessine des cicatrices claires
 * le long des coutures. On recopie donc les attributs vers l'extérieur sur
 * quelques texels, et la peinture s'applique aussi là : le bord de l'îlot
 * continue naturellement au lieu de s'arrêter net.
 */
static void dilater(carte *c, int passes)
{
    const int T = c->taille;
    const size_t n = (size_t)T * (size_t)T;
    uint8_t *src = (uint8_t *)malloc(n);
    if (!src) tool_fatalf("mémoire épuisée (dilatation)");
    for (int pass = 0; pass < passes; ++pass) {
        memcpy(src, c->plein, n);
        for (int y = 0; y < T; ++y) {
            for (int x = 0; x < T; ++x) {
                const size_t o = (size_t)y * (size_t)T + (size_t)x;
                if (src[o]) continue;
                float acc[6] = { 0, 0, 0, 0, 0, 0 };
                int cnt = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= T || sy >= T) continue;
                        const size_t so = (size_t)sy * (size_t)T + (size_t)sx;
                        if (!src[so]) continue;
                        for (int k = 0; k < 3; ++k) { acc[k] += c->p[so * 3 + (size_t)k]; acc[3 + k] += c->n[so * 3 + (size_t)k]; }
                        ++cnt;
                    }
                }
                if (!cnt) continue;
                for (int k = 0; k < 3; ++k) {
                    c->p[o * 3 + (size_t)k] = acc[k] / (float)cnt;
                    c->n[o * 3 + (size_t)k] = acc[3 + k] / (float)cnt;
                }
                c->plein[o] = 3u;
            }
        }
    }
    free(src);
}

/* -------------------------------------------------------------- la mesure */

static void mesurer(const maillage *m, anatomie *a)
{
    memset(a, 0, sizeof *a);
    float zmin = 1e30f, zmax = -1e30f, ymax = 0.0f;
    for (uint32_t i = 0; i < m->vcount; ++i) {
        zmin = minf(zmin, m->v[i].p[2]);
        zmax = maxf(zmax, m->v[i].p[2]);
        ymax = maxf(ymax, fabsf(m->v[i].p[1]));
    }
    a->sol = zmin;
    a->haut = zmax - zmin;
    a->envergure = ymax;
    if (a->haut <= 0.0f || a->envergure <= 0.0f) tool_fatalf("maillage dégénéré : ni hauteur ni envergure");

    /* La TÊTE : l'os dont les sommets ont la plus haute moyenne. Les mains :
     * ceux dont les sommets sont les plus écartés du plan médian. Les pieds :
     * les plus bas. Aucun nom d'os n'entre là-dedans. */
    enum { OS_MAX = 256 };
    double sz[OS_MAX], sy[OS_MAX];
    int    cnt[OS_MAX];
    float  os_zmax[OS_MAX], os_ymin[OS_MAX];
    for (int i = 0; i < OS_MAX; ++i) { sz[i] = 0.0; sy[i] = 0.0; cnt[i] = 0; os_zmax[i] = -1e30f; os_ymin[i] = 1e30f; }
    const int nos = (m->os_count < OS_MAX) ? m->os_count : OS_MAX;
    for (uint32_t i = 0; i < m->vcount; ++i) {
        const int o = m->v[i].os;
        if (o < 0 || o >= nos) continue;
        sz[o] += (double)m->v[i].p[2];
        sy[o] += (double)fabsf(m->v[i].p[1]);
        os_zmax[o] = maxf(os_zmax[o], m->v[i].p[2]);
        os_ymin[o] = minf(os_ymin[o], fabsf(m->v[i].p[1]));
        ++cnt[o];
    }
    int tete = -1, main_os = -1;
    double meilleur_z = -1e30, meilleur_y = -1e30;
    for (int o = 0; o < nos; ++o) {
        if (!cnt[o]) continue;
        const double mz = sz[o] / cnt[o], my = sy[o] / cnt[o];
        if (mz > meilleur_z) { meilleur_z = mz; tete = o; }
        if (my > meilleur_y) { meilleur_y = my; main_os = o; }
    }
    if (tete < 0 || main_os < 0) tool_fatalf("aucun os pesé : le personnage n'a pas de peau");
    a->os_tete = tete;
    a->poignet = os_ymin[main_os];

    /* Le haut du PIED : le plus haut sommet des os dont la moyenne en z est
     * sous un dixième de la hauteur. Une chaussure monte jusque-là. */
    float pied = a->sol;
    for (int o = 0; o < nos; ++o) {
        if (!cnt[o]) continue;
        if ((float)(sz[o] / cnt[o]) - a->sol < 0.10f * a->haut) pied = maxf(pied, os_zmax[o]);
    }
    a->cou_pied = pied;

    /* La boîte de la tête, sur ses seuls sommets. */
    float hxmin = 1e30f, hxmax = -1e30f, hymax = 0.0f, hzmin = 1e30f, hzmax = -1e30f;
    for (uint32_t i = 0; i < m->vcount; ++i) {
        if (m->v[i].os != tete) continue;
        hxmin = minf(hxmin, m->v[i].p[0]); hxmax = maxf(hxmax, m->v[i].p[0]);
        hymax = maxf(hymax, fabsf(m->v[i].p[1]));
        hzmin = minf(hzmin, m->v[i].p[2]); hzmax = maxf(hzmax, m->v[i].p[2]);
    }
    a->menton  = hzmin;
    a->tete_cx = 0.5f * (hxmin + hxmax);
    a->tete_cz = 0.5f * (hzmin + hzmax);
    a->tete_rx = maxf(0.5f * (hxmax - hxmin), 1e-4f);
    a->tete_ry = maxf(hymax, 1e-4f);
    a->tete_rz = maxf(0.5f * (hzmax - hzmin), 1e-4f);
}

/* ------------------------------------------------------------ la peinture */

/*
 * La garde-robe. Des couleurs SATURÉES et contrastées entre elles, pour une
 * raison qui n'est pas de goût : la salle est éclairée au tungstène, dans les
 * bruns, et un personnage habillé dans les mêmes valeurs s'y dissout. Le rouge
 * de la veste et l'indigo du pantalon sont les deux teintes qu'on distingue
 * encore à trois mètres sous cette lumière, et la césure entre les deux DIT la
 * taille — c'est elle qui fait lire une silhouette humaine plutôt qu'un tube.
 */
static const rvb C_VESTE     = { 0.470f, 0.145f, 0.130f };   /* rouge brique */
static const rvb C_VESTE_DOS = { 0.395f, 0.115f, 0.105f };   /* le dos, un ton sous le devant */
static const rvb C_COTES     = { 0.845f, 0.815f, 0.735f };   /* bords côtelés : col, poignets, ourlet */
static const rvb C_PANTALON  = { 0.215f, 0.250f, 0.360f };   /* indigo */
static const rvb C_CHAUSSURE = { 0.800f, 0.780f, 0.740f };   /* toile écrue */
static const rvb C_SEMELLE   = { 0.235f, 0.225f, 0.215f };
static const rvb C_PEAU      = { 0.760f, 0.575f, 0.455f };
static const rvb C_CHEVEUX   = { 0.115f, 0.085f, 0.070f };
static const rvb C_OEIL      = { 0.905f, 0.885f, 0.855f };
static const rvb C_IRIS      = { 0.135f, 0.105f, 0.085f };
static const rvb C_BOUCHE    = { 0.480f, 0.265f, 0.245f };

/* La chair du personnage, texel par texel. `x` et `y` ne servent qu'au grain :
 * tout le reste vient de `p` et `n`, c'est-à-dire du CORPS. */
static rvb peindre(const anatomie *a, const float p[3], const float n[3], int x, int y)
{
    const float H  = a->haut;
    const float z  = (p[2] - a->sol) / H;          /* 0 sous la semelle, 1 au sommet du crâne */
    const float r  = fabsf(p[1]) / a->envergure;   /* 0 au plan médian, 1 au bout des doigts */
    const float dev = n[0];                        /* > 0 : la face regarde l'avant */

    /* Les repères de coupe, tous en fraction de la hauteur mesurée. */
    const float z_pied    = (a->cou_pied - a->sol) / H;
    const float z_semelle = z_pied * 0.28f;
    const float z_ourlet  = 0.530f;   /* bas de la veste */
    const float z_cotes   = 0.564f;   /* haut de la bande côtelée du bas */
    const float z_menton  = (a->menton - a->sol) / H;
    const float r_bras    = 0.240f;   /* au-delà, c'est un bras et non le tronc */
    const float r_poignet = a->poignet / a->envergure;

    /* L'OMBRAGE CUIT. Le personnage n'a pas de carte de normales : sans un
     * peu d'ombre dans l'albédo, tous ses volumes sortent plats. On assombrit
     * ce qui regarde vers le BAS — le dessous du menton, l'intérieur des
     * jambes, le dessous des manches. C'est faux physiquement et juste
     * visuellement : ce sont exactement les endroits que le ciel n'atteint
     * pas. */
    const float occ = 0.800f + 0.200f * (0.5f + 0.5f * n[2]);

    rvb c;
    const bool est_tete = (p[2] >= a->menton - 0.002f);

    if (est_tete) {
        /* --- la tête : peau, cheveux, visage --- */
        const float dx = (p[0] - a->tete_cx) / a->tete_rx;
        const float dy = p[1] / a->tete_ry;
        const float dz = (p[2] - a->tete_cz) / a->tete_rz;
        const float len = sqrtf(dx * dx + dy * dy + dz * dz) + 1e-6f;
        const float ux = dx / len, uy = dy / len, uz = dz / len;
        const float az = atan2f(uy, ux);           /* 0 devant, ±pi derrière */
        const float el = uz;                       /* -1 sous le menton, +1 au sommet */

        c = C_PEAU;
        /* Un peu de rose sur les pommettes et le nez, un peu d'ombre sous la
         * mâchoire : une peau d'une seule couleur se lit comme du plastique. */
        c = rvb_mix(c, rvb_make(0.795f, 0.545f, 0.455f),
                    0.35f * smoothstep(0.45f, 0.95f, ux) * bande(el, -0.30f, 0.25f, 0.25f));
        c = rvb_scale(c, 1.0f - 0.18f * smoothstep(-0.30f, -0.80f, el));

        /* LE NEZ n'existe pas dans le maillage : la tête est un ovoïde lisse.
         * On ne le dessine donc pas — un nez peint sur une surface plate se lit
         * comme une tache. On pose seulement son OMBRE PORTÉE, très douce, ce
         * qui suffit à casser la platitude du milieu du visage. */
        {
            const float dn = sqrtf((az / 0.16f) * (az / 0.16f) + ((el + 0.13f) / 0.17f) * ((el + 0.13f) / 0.17f));
            c = rvb_scale(c, 1.0f - 0.075f * (1.0f - smoothstep(0.5f, 1.4f, dn)));
        }

        /* LES YEUX. Ellipse blanche, iris sombre, et une ombre de paupière en
         * haut : sans elle, l'œil est un galet posé sur la joue. */
        {
            const float aaz = fabsf(az);
            const float eaz = (aaz - 0.335f) / 0.150f;
            const float eel = (el - 0.075f) / 0.072f;
            const float d = sqrtf(eaz * eaz + eel * eel);
            const float k = 1.0f - smoothstep(0.85f, 1.05f, d);
            if (k > 0.0f) {
                rvb oeil = C_OEIL;
                const float ia = (aaz - 0.335f) / 0.052f, ie = (el - 0.068f) / 0.052f;
                const float di = sqrtf(ia * ia + ie * ie);
                oeil = rvb_mix(oeil, C_IRIS, 1.0f - smoothstep(0.85f, 1.05f, di));
                oeil = rvb_scale(oeil, 1.0f - 0.30f * smoothstep(0.15f, 0.95f, eel));
                c = rvb_mix(c, oeil, k);
            }
        }
        /* LES SOURCILS : deux arcs. C'est le trait qui donne une expression ;
         * sans eux le visage reste vide même avec des yeux. */
        {
            const float aaz = fabsf(az);
            const float t = (aaz - 0.335f) / 0.215f;               /* -1..1 le long du sourcil */
            const float ligne = 0.258f + 0.026f * (1.0f - t * t);  /* légèrement bombé */
            const float k = bande(t, -1.0f, 1.0f, 0.25f) * bande(el, ligne - 0.026f, ligne + 0.026f, 0.020f);
            c = rvb_mix(c, C_CHEVEUX, 0.72f * k);
        }
        /* LA BOUCHE : un trait, pas un sourire. Un sourire peint sur un
         * personnage qu'on voit surtout de dos vieillit mal. */
        {
            const float t = az / 0.215f;
            const float ligne = -0.395f - 0.028f * (1.0f - t * t);
            const float k = bande(t, -1.0f, 1.0f, 0.30f) * bande(el, ligne - 0.028f, ligne + 0.028f, 0.022f);
            c = rvb_mix(c, C_BOUCHE, 0.90f * k);
        }

        /* LES CHEVEUX. La limite est une fonction de l'azimut : haute devant
         * (le front reste dégagé), basse derrière (la nuque est couverte). Un
         * peu de bruit dessus, sinon la ligne se lit comme un trait de compas
         * et le personnage porte un casque. */
        {
            const float ligne = -0.140f + 0.560f * cosf(az)
                              + 0.030f * sinf(az * 7.0f) + 0.016f * sinf(az * 13.0f + 1.1f);
            float k = smoothstep(ligne - 0.045f, ligne + 0.045f, el);
            /* Les pattes : la chevelure redescend devant l'oreille. */
            const float aaz = fabsf(az);
            k = maxf(k, bande(aaz, 1.30f, 2.05f, 0.22f) * bande(el, -0.28f, 0.22f, 0.12f));
            rvb cheveux = rvb_scale(C_CHEVEUX, 1.0f + 0.17f * grain(x, y, 11u)
                                              + 0.10f * sinf(p[1] * 230.0f));
            c = rvb_mix(c, cheveux, clamp01(k));
        }
    } else if (z < z_pied) {
        /* --- la chaussure --- */
        c = C_CHAUSSURE;
        c = rvb_mix(c, C_SEMELLE, smoothstep(z_semelle + 0.004f, z_semelle - 0.004f, z));
        /* Le bourrelet du col de la chaussure, et le renfort de bout. */
        c = rvb_mix(c, rvb_scale(C_CHAUSSURE, 0.72f), 0.9f * bande(z, z_pied - 0.014f, z_pied, 0.004f));
        c = rvb_mix(c, rvb_scale(C_CHAUSSURE, 1.06f),
                    0.8f * smoothstep(0.55f, 0.95f, p[0] / (0.5f * a->haut * 0.24f)));
        /* Les lacets : trois traits sombres en travers du dessus du pied. */
        {
            const float dessus = smoothstep(0.35f, 0.85f, n[2]);
            const float u = p[0] / (0.16f * a->haut);
            const float l = bande(fmodf(fabsf(u) * 5.0f, 1.0f), 0.36f, 0.64f, 0.14f);
            c = rvb_mix(c, rvb_scale(C_CHAUSSURE, 0.45f), 0.55f * dessus * l * smoothstep(0.20f, 0.55f, u));
        }
        c = rvb_scale(c, 1.0f + 0.030f * grain(x, y, 3u));
    } else if (r < r_bras && z < z_ourlet) {
        /* --- le pantalon --- */
        c = C_PANTALON;
        /* Le SERGÉ du denim : de fines diagonales. C'est ce qui distingue un
         * jean d'un aplat bleu, et ça ne coûte qu'une ligne. */
        {
            const float u = fabsf(p[1]) * 175.0f + p[2] * 165.0f + p[0] * 110.0f;
            c = rvb_scale(c, 1.0f + 0.038f * sinf(u) + 0.045f * grain(x, y, 5u));
        }
        /* Devant plus clair que derrière : c'est l'usure d'un jean, et surtout
         * c'est ce qui donne du volume à une cuisse cylindrique. */
        c = rvb_scale(c, 1.0f + 0.13f * smoothstep(-0.2f, 1.0f, dev) * bande(z, 0.10f, 0.42f, 0.10f));
        /* Les plis derrière le genou, et l'ourlet du bas. */
        c = rvb_scale(c, 1.0f - 0.16f * bande(z, 0.205f, 0.245f, 0.030f) * smoothstep(0.2f, -0.8f, dev));
        c = rvb_scale(c, 1.0f - 0.22f * bande(z, z_pied, z_pied + 0.016f, 0.007f));
        /* L'entrejambe : deux jambes séparées se lisent, un tube non. */
        c = rvb_scale(c, 1.0f - 0.30f * bande(fabsf(p[1]) / a->envergure, 0.0f, 0.020f, 0.014f)
                                     * bande(z, 0.10f, 0.42f, 0.08f));
        /* La ceinture, juste sous la veste : on n'en voit qu'un liseré, mais
         * ce liseré empêche la veste de flotter au-dessus du pantalon. */
        c = rvb_mix(c, rvb_make(0.130f, 0.105f, 0.095f), 0.9f * bande(z, z_ourlet - 0.032f, z_ourlet - 0.004f, 0.006f));
        c = rvb_scale(c, 1.0f + 0.025f * grain(x, y, 6u));
    } else if (r > r_poignet + 0.030f) {
        /* --- les mains --- */
        c = C_PEAU;
        c = rvb_scale(c, 1.0f - 0.10f * smoothstep(0.85f, 1.00f, r));   /* les doigts, plus sombres */
        c = rvb_scale(c, 1.0f + 0.020f * grain(x, y, 9u));
    } else {
        /* --- la veste : tronc, épaules et manches d'un seul tenant --- */
        const float ang = atan2f(p[1], p[0] - 0.02f * a->haut);   /* 0 devant, ±pi derrière */
        c = rvb_mix(C_VESTE, C_VESTE_DOS, smoothstep(0.35f, 1.00f, fabsf(ang) / 3.14159265f));

        /* Le tissu : une trame très fine, sinon la veste est un aplat. */
        c = rvb_scale(c, 1.0f + 0.030f * grain(x, y, 13u)
                            + 0.020f * sinf((p[1] + p[2]) * 420.0f));

        /* LES BORDS CÔTELÉS — ourlet, poignets, col. C'est ce qui fait lire un
         * blouson plutôt qu'un tee-shirt long, et c'est aussi ce qui ferme la
         * silhouette en bas et aux extrémités. */
        const float cote_bas = bande(z, z_ourlet, z_cotes, 0.006f) * (r < r_bras ? 1.0f : 0.0f);
        const float cote_bras = bande(r, r_poignet - 0.060f, r_poignet + 0.030f, 0.006f);
        const float cote_col = bande(z, z_menton - 0.035f, z_menton + 0.004f, 0.006f) * (r < r_bras ? 1.0f : 0.0f);
        float cote = maxf(maxf(cote_bas, cote_bras), cote_col);
        if (cote > 0.0f) {
            /* Les côtes elles-mêmes : des rainures dans le sens de la bande. */
            const float u = (cote_bras > 0.5f) ? (p[2] * 240.0f) : ((p[1] + p[0]) * 240.0f);
            rvb cotes = rvb_scale(C_COTES, 1.0f + 0.085f * sinf(u));
            c = rvb_mix(c, cotes, cote);
        }

        /* LES DEUX LISERÉS DE MANCHE, juste au-dessus du poignet. Un blouson
         * d'équipe en porte, et de dos — c'est-à-dire de la vue où l'on voit ce
         * personnage 90 % du temps — ce sont eux qui donnent son rythme à la
         * silhouette. */
        {
            const float l = maxf(bande(r, r_poignet - 0.125f, r_poignet - 0.103f, 0.005f),
                                 bande(r, r_poignet - 0.093f, r_poignet - 0.071f, 0.005f));
            c = rvb_mix(c, C_COTES, 0.92f * l);
        }

        /* LA FERMETURE À GLISSIÈRE, au milieu du devant. Sans elle, un blouson
         * rouge de face est une pièce d'étoffe. */
        {
            const float k = bande(fabsf(ang), 0.0f, 0.085f, 0.045f)
                          * bande(z, z_ourlet, z_menton, 0.010f) * (r < r_bras ? 1.0f : 0.0f);
            c = rvb_mix(c, rvb_scale(C_COTES, 0.55f), 0.85f * k);
            c = rvb_mix(c, rvb_make(0.62f, 0.60f, 0.57f),
                        0.75f * bande(fabsf(ang), 0.0f, 0.030f, 0.018f)
                              * bande(z, z_ourlet, z_menton, 0.010f) * (r < r_bras ? 1.0f : 0.0f));
        }

        /* L'EMMANCHURE : la couture épaule/manche. Deux centimètres de couture
         * sombre suffisent à séparer le bras du torse, et sans elle les épaules
         * sont un bloc. */
        c = rvb_scale(c, 1.0f - 0.20f * bande(r, r_bras - 0.012f, r_bras + 0.012f, 0.010f));
        /* Le pli d'aisselle et le pli de coude, du côté intérieur. */
        c = rvb_scale(c, 1.0f - 0.14f * bande(r, 0.44f, 0.50f, 0.05f) * smoothstep(0.2f, -0.8f, n[2]));
    }

    return rvb_scale(c, occ);
}

/* ------------------------------------------------------- lecture du modèle */

static void lire_maillage(cgltf_data *d, maillage *m)
{
    const cgltf_primitive *prim = NULL;
    for (cgltf_size mi = 0; mi < d->meshes_count && !prim; ++mi) {
        for (cgltf_size pi = 0; pi < d->meshes[mi].primitives_count; ++pi) {
            const cgltf_primitive *p = &d->meshes[mi].primitives[pi];
            if (p->type != cgltf_primitive_type_triangles || !p->indices) continue;
            prim = p; break;
        }
    }
    if (!prim) tool_fatalf("aucune primitive triangulaire indexée dans le modèle");

    const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *jts = NULL, *wts = NULL;
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        const cgltf_attribute *at = &prim->attributes[i];
        if (at->type == cgltf_attribute_type_position) pos = at->data;
        else if (at->type == cgltf_attribute_type_normal) nrm = at->data;
        else if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uv = at->data;
        else if (at->type == cgltf_attribute_type_joints && at->index == 0) jts = at->data;
        else if (at->type == cgltf_attribute_type_weights && at->index == 0) wts = at->data;
    }
    /* Sans UV il n'y a rien à peindre, et c'est le seul cas où cet outil n'a
     * aucun repli : c'est le constat qui déciderait de refaire le personnage
     * plutôt que de l'habiller. On le DIT donc explicitement. */
    if (!pos) tool_fatalf("le modèle n'a pas de positions");
    if (!uv)  tool_fatalf("le modèle n'a PAS d'UV : impossible de l'habiller par texture, "
                          "il faudrait le déplier ou le remplacer");

    m->vcount = (uint32_t)pos->count;
    m->icount = (uint32_t)prim->indices->count;
    m->v = (sommet *)calloc(m->vcount, sizeof(sommet));
    m->idx = (uint32_t *)calloc(m->icount ? m->icount : 1, sizeof(uint32_t));
    if (!m->v || !m->idx) tool_fatalf("mémoire épuisée (%u sommets)", m->vcount);

    int os_max = 0;
    for (uint32_t i = 0; i < m->vcount; ++i) {
        sommet *s = &m->v[i];
        (void)cgltf_accessor_read_float(pos, i, s->p, 3);
        if (nrm) (void)cgltf_accessor_read_float(nrm, i, s->n, 3);
        else s->n[2] = 1.0f;
        (void)cgltf_accessor_read_float(uv, i, s->uv, 2);
        s->os = -1;
        if (jts && wts) {
            float w[4] = { 0, 0, 0, 0 }, jf[4] = { 0, 0, 0, 0 };
            (void)cgltf_accessor_read_float(wts, i, w, 4);
            (void)cgltf_accessor_read_float(jts, i, jf, 4);
            int best = 0;
            for (int k = 1; k < 4; ++k) if (w[k] > w[best]) best = k;
            s->os = (int)(jf[best] + 0.5f);
            if (s->os + 1 > os_max) os_max = s->os + 1;
        }
    }
    m->os_count = os_max;
    for (uint32_t i = 0; i < m->icount; ++i) {
        m->idx[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
    }
}

/* --------------------------------------------------------------- collecte */

typedef struct tampon { unsigned char *d; size_t n, cap; } tampon;

static void tampon_ecrire(void *ctx, void *data, int size)
{
    tampon *t = (tampon *)ctx;
    if (t->n + (size_t)size > t->cap) {
        t->cap = (t->n + (size_t)size) * 2 + 4096;
        t->d = (unsigned char *)realloc(t->d, t->cap);
        if (!t->d) tool_fatalf("mémoire épuisée (encodage JPEG)");
    }
    memcpy(t->d + t->n, data, (size_t)size);
    t->n += (size_t)size;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    const char *entree = NULL, *sortie = NULL, *atlas = NULL;
    int taille = 1024, qualite = 92, dilatation = 6;
    bool mesures = false;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--taille=", 9) == 0) taille = atoi(argv[i] + 9);
        else if (strncmp(argv[i], "--qualite=", 10) == 0) qualite = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--dilatation=", 13) == 0) dilatation = atoi(argv[i] + 13);
        else if (strncmp(argv[i], "--sortie=", 9) == 0) sortie = argv[i] + 9;
        else if (strncmp(argv[i], "--atlas=", 8) == 0) atlas = argv[i] + 8;
        else if (strcmp(argv[i], "--mesures") == 0) mesures = true;
        else if (!entree) entree = argv[i];
    }
    if (!entree) {
        fprintf(stderr,
            "skinart — habille le personnage : peint sa texture dans l'espace UV\n"
            "usage : %s [options] <personnage.glb>\n"
            "  --sortie=CHEMIN    écrit ailleurs que sur l'entrée\n"
            "  --atlas=CHEMIN     écrit aussi la planche en PNG (contrôle visuel)\n"
            "  --taille=N         côté de la planche (défaut 1024)\n"
            "  --qualite=N        qualité JPEG, 1 à 100 (défaut 92)\n"
            "  --dilatation=N     texels de débord hors des îlots (défaut 6)\n"
            "  --mesures          n'écrit rien, dit seulement ce que le modèle contient\n",
            argv[0]);
        return 2;
    }
    if (taille < 64 || taille > 4096) tool_fatalf("taille de planche invalide : %d", taille);
    if (qualite < 1 || qualite > 100) tool_fatalf("qualité JPEG invalide : %d", qualite);
    if (!sortie) sortie = entree;

    cgltf_options opt;
    memset(&opt, 0, sizeof opt);
    cgltf_data *d = NULL;
    if (cgltf_parse_file(&opt, entree, &d) != cgltf_result_success) {
        tool_fatalf("« %s » : glTF illisible", entree);
    }
    if (cgltf_load_buffers(&opt, d, entree) != cgltf_result_success) {
        tool_fatalf("« %s » : tampons illisibles", entree);
    }

    maillage m;
    memset(&m, 0, sizeof m);
    lire_maillage(d, &m);

    anatomie a;
    mesurer(&m, &a);

    carte c;
    carte_init(&c, taille);
    uint32_t recouverts = 0;
    rasteriser(&c, &m, &recouverts);

    size_t couverts = 0;
    for (size_t i = 0; i < (size_t)taille * (size_t)taille; ++i) if (c.plein[i]) ++couverts;

    tool_infof("modèle : %u sommets, %u triangles, %d os",
               m.vcount, m.icount / 3u, m.os_count);
    tool_infof("mesuré : %.3f de haut, envergure %.3f, tête = os %d (menton à %.3f), "
               "poignet à %.0f %% de l'envergure, haut du pied à %.0f %% de la hauteur",
               a.haut, a.envergure, a.os_tete, a.menton,
               100.0 * (double)(a.poignet / a.envergure),
               100.0 * (double)((a.cou_pied - a.sol) / a.haut));
    tool_infof("UV : %.1f %% de la planche couverts, %u texel(s) réclamé(s) deux fois",
               100.0 * (double)couverts / ((double)taille * (double)taille), recouverts);
    if (recouverts > (uint32_t)((double)couverts * 0.01)) {
        tool_warnf("les îlots UV se CHEVAUCHENT : ce qui est peint à un endroit "
                   "ressortira aussi ailleurs sur le corps");
    }
    if (mesures) { carte_free(&c); free(m.v); free(m.idx); cgltf_free(d); return 0; }

    dilater(&c, dilatation);

    unsigned char *px = (unsigned char *)malloc((size_t)taille * (size_t)taille * 3);
    if (!px) tool_fatalf("mémoire épuisée (planche %dx%d)", taille, taille);
    for (int y = 0; y < taille; ++y) {
        for (int x = 0; x < taille; ++x) {
            const size_t o = (size_t)y * (size_t)taille + (size_t)x;
            rvb col;
            if (c.plein[o]) {
                float n[3] = { c.n[o * 3], c.n[o * 3 + 1], c.n[o * 3 + 2] };
                const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 1e-6f) { n[0] /= l; n[1] /= l; n[2] /= l; }
                col = peindre(&a, &c.p[o * 3], n, x, y);
            } else {
                /* Ce qui n'est couvert par personne ne se voit jamais. On y met
                 * quand même la couleur de la veste plutôt que du blanc : si un
                 * jour un texel de bord échappe à la dilatation, il ressortira
                 * comme une tache de tissu et pas comme un trou lumineux. */
                col = C_VESTE;
            }
            px[o * 3 + 0] = (unsigned char)(clamp01(col.r) * 255.0f + 0.5f);
            px[o * 3 + 1] = (unsigned char)(clamp01(col.g) * 255.0f + 0.5f);
            px[o * 3 + 2] = (unsigned char)(clamp01(col.b) * 255.0f + 0.5f);
        }
    }

    if (atlas && !stbi_write_png(atlas, taille, taille, 3, px, taille * 3)) {
        tool_fatalf("« %s » : écriture PNG impossible", atlas);
    }

    /* JPEG et non PNG : c'est déjà le format de l'image embarquée, et une
     * planche de mille pixels en PNG pèserait un mégaoctet et demi de plus dans
     * le dépôt pour une différence qu'aucun écran ne montre sur du tissu. */
    tampon jpg;
    memset(&jpg, 0, sizeof jpg);
    if (!stbi_write_jpg_to_func(tampon_ecrire, &jpg, taille, taille, 3, px, qualite)) {
        tool_fatalf("encodage JPEG impossible");
    }

    const size_t avant = (d->images_count && d->images[0].buffer_view)
                       ? d->images[0].buffer_view->size : 0u;
    /* Le modèle est LIBÉRÉ avant la réécriture : si `sortie` et `entree` sont le
     * même fichier — c'est le cas normal — on ne veut pas d'un lecteur encore
     * ouvert dessus pendant qu'on l'écrase. */
    cgltf_free(d);

    glb_image_replace(entree, sortie, jpg.d, jpg.n, "image/jpeg");
    tool_infof("« %s » : image remplacée, %zu Kio (était %zu Kio)",
               sortie, jpg.n / 1024u, avant / 1024u);

    free(jpg.d);
    free(px);
    carte_free(&c);
    free(m.v); free(m.idx);
    return 0;
}
