/*
 * spriteart — les planches de sprites des mini-jeux, dessinées.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * Même raison que `posterart`, et elle est du même ordre : les planches
 * livrées dans le paquet étaient des ŒUVRES DE TIERS. Ouvertes une par une,
 * agrandies au plus proche sur un damier pour voir la couche alpha :
 *
 *   1_pacman/enemy.png    88 x 44   HUIT FANTÔMES de PAC-MAN. La silhouette
 *                                   arrondie a jupe ondulee, les deux yeux et
 *                                   leur pupille decalee : le personnage de
 *                                   Namco, pas une inspiration.      Bandai Namco
 *   1_pacman/pacman.png   88 x 22   QUATRE PAC-MAN. Disque jaune, part de
 *                                   camembert retiree, quatre ouvertures.  idem
 *   1_pacman/items.png    66 x 22   les CERISES et la FRAISE de bonus du meme
 *                                   jeu. Non copiee par le build, mais elle
 *                                   etait la.                              idem
 *   3_flappy_bird/birds.png   51 x 36  L'OISEAU de Flappy Bird, ses trois
 *                                   teintes et ses trois battements. Le bec
 *                                   rouge, l'oeil blanc a pupille noire, la
 *                                   joue claire : c'est le sprite d'origine.
 *   3_flappy_bird/pipes.png  104 x 160  ses TUYAUX, vert et orange, avec leur
 *                                   embouchure a rebord. Eux-memes derives
 *                                   d'ailleurs, ce qui n'arrange rien.
 *   3_flappy_bird/backgrounds.png 288 x 256  son CIEL jour et nuit, sa ligne
 *                                   d'immeubles et sa haie.
 *   3_flappy_bird/sol.png    168 x 55  son SOL raye.
 *   3_flappy_bird/medals.png  88 x 22  ses MEDAILLES, l'oiseau grave dessus.
 *   3_flappy_bird/scoreBoard.png 113 x 57  son TABLEAU de fin de partie.
 *   3_flappy_bird/high_score.png 512 x 512  une image de banque d'images, AVEC
 *                                   LE FILIGRANE « ©123RF » ENCORE DESSUS.
 *                                   Ce n'est pas une oeuvre libre mal creditee :
 *                                   c'est l'apercu non paye d'une oeuvre
 *                                   payante, et le filigrane le dit.
 *
 * Aucune rédaction de `LICENSES.md` ne rend ces fichiers distribuables. Une
 * attribution répare une licence non tenue ; elle ne donne pas un droit qu'on
 * n'a pas. Tant qu'ils sont dans le paquet, le jeu ne peut pas être vendu.
 *
 * Ce qui se copie et ce qui ne se copie pas
 * -----------------------------------------
 * Une MÉCANIQUE ne s'approprie pas : manger des pastilles dans un labyrinthe en
 * fuyant quatre poursuivants, empiler des pièces de quatre cases, franchir des
 * ouvertures en battant des ailes — personne ne les possède, et le code de ce
 * dépôt est écrit ici. Un NOM et un PERSONNAGE, si. C'est exactement la ligne
 * que ce fichier trace : les règles restent, les figures changent.
 *
 * Pourquoi dessinées plutôt que rapportées
 * ----------------------------------------
 * Une planche produite par arithmétique n'a AUCUNE licence à démêler, se
 * régénère à l'identique sur les trois plateformes, et appartient au dépôt.
 * C'est déjà l'argument de `sideart`, `panelart`, `marqueeart` et `posterart` ;
 * il ne change pas parce que l'image est petite.
 *
 * La contrainte qui a produit les formes de 1980, et qu'on subit aussi
 * ---------------------------------------------------------------------
 * Un personnage de labyrinthe se lit à SEIZE PIXELS DE HAUT. C'est cette
 * contrainte — pas le goût de 1980 — qui a donné un disque et un dôme : à cette
 * taille, seule une masse pleine à contour franc survit. La contrainte
 * n'impose pas CES deux formes-là ; elle impose UNE forme pleine, sans détail
 * intérieur, dont la silhouette se reconnaisse en négatif.
 *
 * Les motifs sont donc écrits ici en ASCII, seize caractères sur seize lignes,
 * à la taille où ils doivent se lire. On les VOIT dans le source, on les
 * corrige en regardant, et ce qui est écrit est exactement ce qui sort. Une
 * planche encodée en octets se corrige à l'aveugle.
 *
 * L'agrandissement est un multiple ENTIER, au plus proche : un pixel d'auteur
 * devient un carré de k x k, jamais un dégradé. C'est ce qui garde le contour
 * franc quand la borne affiche le sprite à quatre-vingt-dix pixels.
 *
 * La couleur : dans le code pour le labyrinthe, dans la planche pour le reste
 * -----------------------------------------------------------------------------
 * `ns_sprite_quad` MULTIPLIE la texture par une couleur. Les planches du
 * labyrinthe sont donc en niveaux de gris : la forme est ici, la teinte est
 * dans `games/dedale/dedale.c`. Quatre poursuivants de quatre couleurs, un état
 * apeuré et un état mangé ne demandent alors qu'UNE planche — et les quatre
 * couleurs de 1980 peuvent rester, une couleur n'appartient à personne.
 *
 * Les planches d'Envol, elles, sont dessinées EN COULEUR : `envol.c` les
 * affiche sans teinte, et ses découpes sont écrites au pixel près
 * (`SPR_BIRD_W 17`, `SPR_PIPE_W 26`, `SPR_BG_W 144`…). Les remplaçantes ont
 * donc EXACTEMENT les mêmes dimensions et la même grille de cases : le jeu n'a
 * pas une ligne à changer, et c'est voulu — un remplacement d'art qui oblige à
 * rouvrir la logique du jeu est un remplacement qu'on ne refera pas.
 */
#include "tools_common.h"

#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ==========================================================================
 * La toile : RGBA, non prémultipliée, écrite telle quelle
 * ==========================================================================
 * Pas de gamma ici, contrairement à `posterart` : ces planches sont de la
 * COULEUR D'AFFICHAGE, pas de la lumière calculée. Elles sont chargées en sRGB
 * par `ns_texture_load` et affichées ; les valeurs écrites sont celles qu'on
 * veut voir.
 */
typedef struct toile {
    int w, h;
    unsigned char *px;          /* w * h * 4 */
} toile;

static void toile_init(toile *t, int w, int h)
{
    t->w = w; t->h = h;
    t->px = (unsigned char *)calloc((size_t)w * (size_t)h * 4u, 1u);
    if (!t->px) tool_fatalf("mémoire épuisée (%d x %d)", w, h);
}

static void point(toile *t, int x, int y, const unsigned char rgba[4])
{
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) return;
    unsigned char *p = &t->px[((size_t)y * (size_t)t->w + (size_t)x) * 4u];
    p[0] = rgba[0]; p[1] = rgba[1]; p[2] = rgba[2]; p[3] = rgba[3];
}

static void pave(toile *t, int x, int y, int w, int h, const unsigned char rgba[4])
{
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) point(t, x + i, y + j, rgba);
}

static unsigned char borne8(float v)
{
    if (v <= 0.0f) return 0u;
    if (v >= 1.0f) return 255u;
    return (unsigned char)(v * 255.0f + 0.5f);
}

/* ==========================================================================
 * Un motif ASCII, posé sur la toile
 * ==========================================================================
 * `'.'` est TOUJOURS le vide — c'est la seule convention imposée, et elle vaut
 * pour toutes les planches : un motif se relit à condition que le fond se voie
 * du premier coup d'oeil. Le reste des caractères est libre, décrit par la
 * palette que le motif emporte avec lui.
 *
 * `k` est l'agrandissement, entier : chaque caractère devient un carré plein de
 * k x k pixels. `si_vide` ne peint que là où rien n'a encore été écrit — c'est
 * ce qui permet de superposer deux motifs sans que le second efface le premier.
 */
typedef struct teinte { char c; unsigned char rgba[4]; } teinte;

static const unsigned char *teinte_de(const teinte *pal, int n, char c)
{
    for (int i = 0; i < n; ++i) if (pal[i].c == c) return pal[i].rgba;
    tool_fatalf("caractère « %c » absent de la palette du motif", c);
    return NULL;                                  /* inatteignable */
}

static void pose(toile *t, const char *const *motif, int gw, int gh,
                 const teinte *pal, int n, int x0, int y0, int k, bool si_vide)
{
    for (int r = 0; r < gh; ++r) {
        if ((int)strlen(motif[r]) != gw)
            tool_fatalf("ligne %d du motif : %zu caractères pour %d attendus",
                        r, strlen(motif[r]), gw);
        for (int c = 0; c < gw; ++c) {
            const char ch = motif[r][c];
            if (ch == '.') continue;
            if (si_vide) {
                const unsigned char *p =
                    &t->px[((size_t)(y0 + r * k) * (size_t)t->w + (size_t)(x0 + c * k)) * 4u];
                if (p[3] != 0u) continue;
            }
            pave(t, x0 + c * k, y0 + r * k, k, k, teinte_de(pal, n, ch));
        }
    }
}

/* ==========================================================================
 * DÉDALE — le jeu de labyrinthe
 * ==========================================================================
 * Le héros et les poursuivants du labyrinthe, en NIVEAUX DE GRIS : la teinte
 * est appliquée par le jeu (voir l'en-tête). Quatre niveaux suffisent, et c'est
 * une contrainte de lisibilité autant qu'un choix : à seize pixels, un dégradé
 * intérieur ne se voit pas, il salit.
 *
 *   'o'  le contour — sombre, il DÉTACHE la forme du labyrinthe bleu
 *   '#'  le corps — plein
 *   ':'  le demi-ton — une face en retrait, ou une pale filée
 *   '*'  l'éclat — le point le plus clair, un seul par sprite
 */
static const teinte PAL_GRIS[] = {
    { 'o', {  38,  38,  46, 255 } },
    { ':', { 150, 150, 156, 255 } },
    { '#', { 224, 224, 228, 255 } },
    { '*', { 255, 255, 255, 255 } },
};
#define N_GRIS ((int)(sizeof PAL_GRIS / sizeof PAL_GRIS[0]))

/*
 * LE HÉROS : « le rubis », une pierre taillée.
 *
 * Pourquoi pas un disque à part de camembert : parce que c'est le personnage de
 * quelqu'un d'autre. Pourquoi CETTE forme-là plutôt qu'une autre : parce que la
 * silhouette d'une pierre taillée — TABLE PLATE en haut, taille qui se rétrécit
 * jusqu'à une POINTE en bas — est asymétrique verticalement. À seize pixels,
 * c'est ce qui la rend reconnaissable en un coup d'oeil et impossible à
 * confondre avec un rond. Un disque, lui, se confond avec tous les autres
 * ronds de l'écran, à commencer par ses propres pastilles.
 *
 * Elle n'a pas de face : une pierre ne regarde nulle part. La direction se lit
 * à la TRAÎNÉE, que le jeu dessine en aplat derrière elle — un aplat coûte un
 * quad et se pose exactement dans l'axe du déplacement, ce qu'une planche à
 * quatre orientations ferait moins bien pour quatre fois la surface.
 */
static const char *const DD_RUBIS[16] = {
    "................",
    "...oooooooooo...",
    "..o**********o..",     /* la table, plate et claire : c'est elle qui se lit */
    ".o############o.",
    "o##############o",     /* la ceinture, le point le plus large */
    "o::::######::::o",     /* les facettes : un coin clair, des flancs en retrait */
    ".o::::####::::o.",
    "..o:::####:::o..",
    "...o::####::o...",
    "....o:####:o....",
    ".....o####o.....",
    ".....o####o.....",
    "......o##o......",
    "......o##o......",
    ".......oo.......",
    "................",
};

/*
 * LES POURSUIVANTS : « les hélices », un rotor à quatre pales.
 *
 * Pourquoi pas un dôme à jupe ondulée : même raison. Pourquoi une croix :
 * parce que sa silhouette est AJOURÉE — quatre bras et quatre trous — là où le
 * héros est une masse pleine. Les deux ne peuvent pas être confondus, même à
 * seize pixels, même du coin de l'oeil, même en pleine poursuite. C'était le
 * critère : deux formes qui se distinguent, pas deux formes jolies.
 *
 * L'animation est une ROTATION : la croix droite, puis la croix en X, en
 * alternance. Les deux ont des pales de MÊME ÉPAISSEUR — quatre pixels — et
 * c'est ce qui fait la différence entre une rotation et un clignotement : un X
 * à pales fines aurait donné une masse qui grossit et rétrécit, ce que le
 * premier jet montrait sans appel. Le filé en demi-ton a été essayé et retiré
 * pour la même raison mesurée : il remplissait les quatre creux de la croix,
 * et le rotor devenait un carré gris barré d'un X.
 */
static const char *const DD_CROIX[16] = {
    "................",
    ".....oooooo.....",
    ".....o####o.....",
    ".....o####o.....",
    ".....o####o.....",
    "ooooo######ooooo",
    "o##############o",
    "o#####****#####o",     /* le moyeu : c'est lui qui reste quand on la mange */
    "o#####****#####o",
    "o##############o",
    "ooooo######ooooo",
    ".....o####o.....",
    ".....o####o.....",
    ".....o####o.....",
    ".....oooooo.....",
    "................",
};

static const char *const DD_SAUTOIR[16] = {
    "oooo........oooo",
    "o###o......o###o",
    ".o###o....o###o.",
    "..o###o..o###o..",
    "...o###oo###o...",
    "....o######o....",
    ".....o####o.....",
    "......****......",
    "......****......",
    ".....o####o.....",
    "....o######o....",
    "...o###oo###o...",
    "..o###o..o###o..",
    ".o###o....o###o.",
    "o###o......o###o",
    "oooo........oooo",
};

/* Le MOYEU seul : ce qui reste d'une hélice mangée, et qui rentre à l'enclos.
 * Une petite forme, donc — c'est le signal qu'elle n'est plus dangereuse. */
static const char *const DD_MOYEU[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....oooooo.....",
    ".....o****o.....",
    ".....o****o.....",
    ".....o****o.....",
    ".....o****o.....",
    ".....oooooo.....",
    "................",
    "................",
    "................",
    "................",
    "................",
};

/* La case de la planche : seize pixels d'auteur, agrandis quatre fois.
 *
 * Pourquoi quatre et pas un : la case du labyrinthe fait 40 px logiques et le
 * repère du jeu est en 1920 x 1080, si bien qu'une borne en affiche le sprite
 * autour de quatre-vingt-dix pixels. Une planche de seize y serait interpolée
 * six fois et bavocherait ; une planche de soixante-quatre y est presque au
 * rapport 1:1. Et l'agrandissement étant ENTIER et au plus proche, elle reste
 * exactement le dessin de seize pixels — pas un dessin de soixante-quatre qu'on
 * n'aurait pas relu. */
#define DD_K    4
#define DD_CASE (16 * DD_K)

static void planche_dedale_heros(toile *t)
{
    /* Quatre images : l'éclat traverse la couronne de gauche à droite, puis
     * c'est la pointe qui s'allume. C'est la seule animation d'une pierre —
     * elle ne se déforme pas, elle accroche la lumière. */
    static const int ECLAT_X[4] = { 1, 6, 11, -1 };   /* -1 : la pointe s'allume */

    for (int f = 0; f < 4; ++f) {
        char buf[16][17];
        const char *vue[16];
        for (int r = 0; r < 16; ++r) {
            memcpy(buf[r], DD_RUBIS[r], 16);
            buf[r][16] = '\0';
            vue[r] = buf[r];
        }
        if (ECLAT_X[f] >= 0) {
            for (int r = 3; r <= 4; ++r)
                for (int c = ECLAT_X[f]; c < ECLAT_X[f] + 4 && c < 16; ++c)
                    if (buf[r][c] == '#') buf[r][c] = '*';
        } else {
            for (int r = 10; r <= 13; ++r)
                for (int c = 0; c < 16; ++c) if (buf[r][c] == '#') buf[r][c] = '*';
        }
        pose(t, vue, 16, 16, PAL_GRIS, N_GRIS, f * DD_CASE, 0, DD_K, false);
    }
}

static void planche_dedale_creatures(toile *t)
{
    /* Rangée 0 : l'hélice entière, quatre phases — droite, X, droite, X. */
    for (int f = 0; f < 4; ++f) {
        const char *const *pale = (f % 2 == 0) ? DD_CROIX : DD_SAUTOIR;
        pose(t, pale, 16, 16, PAL_GRIS, N_GRIS, f * DD_CASE, 0, DD_K, false);
    }

    /* Rangée 1 : le moyeu seul, mangé. Quatre fois la même image : le jeu
     * indexe la colonne par la même phase, il n'a donc pas de cas particulier
     * à écrire — et si un jour le moyeu s'anime, la place est là. */
    for (int f = 0; f < 4; ++f)
        pose(t, DD_MOYEU, 16, 16, PAL_GRIS, N_GRIS, f * DD_CASE, DD_CASE, DD_K, false);
}

/* ==========================================================================
 * FLAPPY — les planches de 2020, redessinées aux MÊMES cotes
 * ==========================================================================
 * Chaque planche ci-dessous a exactement la taille et le découpage de celle
 * qu'elle remplace, parce que `games/flappy/flappy.c` les découpe au pixel :
 * `SPR_BIRD_W 17`, `SPR_PIPE_W 26`, `SPR_GROUND_W 168`, `SPR_BG_W 144`. Le jeu
 * ne change pas d'une ligne, et le remplacement se vérifie en regardant l'écran
 * plutôt qu'en relisant du code.
 */

/*
 * L'OISEAU devient UN CERF-VOLANT.
 *
 * Le remplacer par un autre oiseau n'aurait rien réglé : c'est l'oiseau qui est
 * reconnaissable, et un « presque oiseau » à dix-sept pixels de large ressemble
 * à celui d'à côté. Un cerf-volant n'a ni bec, ni oeil, ni plumes ; il a un
 * losange et une queue. La silhouette est donc décidée du premier coup d'oeil,
 * et elle est aussi juste pour la mécanique : on tire sur la ficelle, l'engin
 * remonte, puis il redescend. C'est exactement ce que fait le bouton.
 *
 * Trois images : la queue fouette. Elle est la seule chose qui bouge, et à
 * cette taille c'est assez — l'aile de l'oiseau d'origine ne faisait pas plus.
 */
static const teinte PAL_CERF[] = {
    { 'o', {  40,  30,  56, 255 } },     /* le contour, sombre : il détache du ciel */
    { 'r', { 236,  78,  92, 255 } },     /* la voile haute */
    { 'j', { 255, 206,  84, 255 } },     /* la voile basse */
    { 'b', { 250, 250, 252, 255 } },     /* la vergue, claire */
    { 'q', { 236,  78,  92, 255 } },     /* la queue */
};
#define N_CERF ((int)(sizeof PAL_CERF / sizeof PAL_CERF[0]))

/*
 * 17 x 12, l'exacte case de `SPR_BIRD_W` x `SPR_BIRD_H`.
 *
 * Le losange occupe la DROITE et la queue traîne à GAUCHE : dans ce jeu le
 * décor défile vers la gauche, donc le mobile va vers la droite, et une queue
 * qui partirait devant lui se lirait comme une flèche pointée à l'envers.
 * L'oiseau d'origine regardait à droite pour la même raison.
 *
 * Trois images : seule la queue change — elle fouette vers le bas, à plat, puis
 * vers le haut. La voile, elle, ne se déforme pas : c'est une toile tendue.
 */
static const char *const EN_CERF[3][12] = {
    {
        "...........o.....",
        "..........oro....",
        ".........orrro...",
        "........orrrrro..",
        ".......orrrrrrro.",
        ".....qobbbbbbbbbo",
        "...qq..ojjjjjjjo.",
        ".qq.....ojjjjjo..",
        "q........ojjjo...",
        "..........ojo....",
        "...........o.....",
        ".................",
    },
    {
        "...........o.....",
        "..........oro....",
        ".........orrro...",
        "........orrrrro..",
        ".......orrrrrrro.",
        "...qqqobbbbbbbbbo",
        ".qq....ojjjjjjjo.",
        "q.......ojjjjjo..",
        ".........ojjjo...",
        "..........ojo....",
        "...........o.....",
        ".................",
    },
    {
        "...........o.....",
        "..........oro....",
        "q........orrro...",
        ".qq.....orrrrro..",
        "...qq..orrrrrrro.",
        ".....qobbbbbbbbbo",
        ".......ojjjjjjjo.",
        "........ojjjjjo..",
        ".........ojjjo...",
        "..........ojo....",
        "...........o.....",
        ".................",
    },
};

static void planche_envol_cerf(toile *t)
{
    /* La planche fait 51 x 36 : trois colonnes de 17 sur trois rangées de 12.
     * Le jeu n'emploie que la première rangée ; les deux autres existaient
     * pour les deux autres teintes de l'oiseau d'origine. On les remplit de
     * la même image en deux autres voiles plutôt que de les laisser vides —
     * une planche à trous se croit incomplète et se « répare » un jour à
     * tort. */
    static const unsigned char VOILE[3][2][4] = {
        { { 236,  78,  92, 255 }, { 255, 206,  84, 255 } },
        { {  92, 200, 240, 255 }, { 240, 250, 255, 255 } },
        { { 140, 220, 130, 255 }, { 250, 240, 140, 255 } },
    };
    for (int rang = 0; rang < 3; ++rang) {
        teinte pal[N_CERF];
        memcpy(pal, PAL_CERF, sizeof pal);
        memcpy(pal[1].rgba, VOILE[rang][0], 4);   /* 'r' */
        memcpy(pal[2].rgba, VOILE[rang][1], 4);   /* 'j' */
        memcpy(pal[4].rgba, VOILE[rang][0], 4);   /* 'q' : la queue suit la voile */
        for (int f = 0; f < 3; ++f)
            pose(t, EN_CERF[f], 17, 12, pal, N_CERF, f * 17, rang * 12, 1, false);
    }
}

/*
 * LES TUYAUX deviennent des PYLÔNES.
 *
 * Le tuyau à embouchure de Flappy Bird est lui-même emprunté ailleurs, ce qui
 * fait deux raisons plutôt qu'une de ne pas le garder. Un pylône treillis rend
 * le même service — une colonne pleine hauteur avec une pièce d'about qui
 * marque l'ouverture — sans ressembler à quoi que ce soit d'existant, et son
 * treillis donne une texture verticale que le défilement rend lisible.
 *
 * 104 x 160 : quatre colonnes de 26. `envol.c` prend la colonne 0 en mode
 * difficile et la colonne 52 (donc la troisième) en mode normal ; les deux
 * autres restent des variantes, comme dans la planche d'origine.
 *
 * L'ABOUT EST EN HAUT DE LA PLANCHE, et ce n'est pas un détail de goût. Le
 * pylône du bas est dessiné depuis le bord de l'ouverture vers le sol : sa
 * pièce d'about est donc son PREMIER texel. Celui du haut est dessiné avec v
 * inversé, ce qui remet ce même premier texel contre l'ouverture. Une seule
 * planche sert aux deux, à condition que l'about soit à la ligne 0 — le
 * commentaire de `flappy.c` dit « en bas », et la planche de 2020 le contredit :
 * son rebord est bien tout en haut. Mise à l'autre bout, la pièce d'about se
 * retrouve contre le sol et contre le plafond, c'est-à-dire nulle part où on la
 * regarde.
 */
#define FL_ABOUT_H 15

static void pylone(toile *t, int x0, const float corps[3], const float about[3])
{
    const unsigned char C[4] = { borne8(corps[0]), borne8(corps[1]), borne8(corps[2]), 255u };
    const unsigned char A[4] = { borne8(about[0]), borne8(about[1]), borne8(about[2]), 255u };
    const unsigned char OMBRE[4] = { borne8(corps[0] * 0.45f), borne8(corps[1] * 0.45f),
                                     borne8(corps[2] * 0.45f), 255u };
    const unsigned char CLAIR[4] = { borne8(corps[0] + 0.22f), borne8(corps[1] + 0.22f),
                                     borne8(corps[2] + 0.22f), 255u };
    const unsigned char A_CLAIR[4] = { borne8(about[0] + 0.20f), borne8(about[1] + 0.20f),
                                       borne8(about[2] + 0.20f), 255u };
    const unsigned char BORD[4] = { 26u, 24u, 32u, 255u };

    /* Le fût, EN RETRAIT de deux pixels : c'est ce retrait qui fait que la
     * pièce d'about déborde, donc qu'on la lit comme une pièce rapportée et pas
     * comme un changement de couleur. */
    pave(t, x0 + 2, FL_ABOUT_H, 22, 160 - FL_ABOUT_H, C);
    pave(t, x0 + 2, FL_ABOUT_H,  2, 160 - FL_ABOUT_H, BORD);
    pave(t, x0 + 22, FL_ABOUT_H, 2, 160 - FL_ABOUT_H, BORD);
    pave(t, x0 + 4, FL_ABOUT_H,  3, 160 - FL_ABOUT_H, OMBRE);
    pave(t, x0 + 9, FL_ABOUT_H,  4, 160 - FL_ABOUT_H, CLAIR);
    pave(t, x0 + 18, FL_ABOUT_H, 4, 160 - FL_ABOUT_H, OMBRE);

    /* Le treillis : une croix de Saint-André tous les 16 pixels, tracée au pas
     * plutôt que par une texture — un pylône se lit à sa maille, et la maille
     * doit tomber juste dans la largeur du fût. */
    for (int y = FL_ABOUT_H + 4; y + 14 < 160; y += 16) {
        for (int i = 0; i < 14; ++i) {
            pave(t, x0 + 5 + i, y + i, 2, 2, OMBRE);
            pave(t, x0 + 19 - i, y + i, 2, 2, OMBRE);
        }
    }

    /* La pièce d'about, pleine largeur. */
    pave(t, x0, 0, 26, FL_ABOUT_H, A);
    pave(t, x0, 0, 26, 2, BORD);
    pave(t, x0, FL_ABOUT_H - 2, 26, 2, BORD);
    pave(t, x0, 0, 2, FL_ABOUT_H, BORD);
    pave(t, x0 + 24, 0, 2, FL_ABOUT_H, BORD);
    pave(t, x0 + 4, 2, 5, FL_ABOUT_H - 4, A_CLAIR);
    pave(t, x0 + 19, 2, 4, FL_ABOUT_H - 4, OMBRE);
}

static void planche_envol_pylones(toile *t)
{
    static const float ORANGE[3] = { 0.86f, 0.42f, 0.16f };
    static const float OR_ABOUT[3] = { 0.96f, 0.58f, 0.22f };
    static const float ACIER[3] = { 0.42f, 0.52f, 0.60f };
    static const float AC_ABOUT[3] = { 0.56f, 0.66f, 0.74f };
    static const float VERT[3] = { 0.38f, 0.62f, 0.30f };
    static const float VE_ABOUT[3] = { 0.50f, 0.74f, 0.38f };
    static const float VIOLET[3] = { 0.46f, 0.36f, 0.62f };
    static const float VI_ABOUT[3] = { 0.58f, 0.46f, 0.76f };

    pylone(t,  0, ORANGE, OR_ABOUT);   /* mode difficile */
    pylone(t, 26, ACIER,  AC_ABOUT);
    pylone(t, 52, VERT,   VE_ABOUT);   /* mode normal */
    pylone(t, 78, VIOLET, VI_ABOUT);
}

/*
 * LE SOL : une passerelle de chantier, 168 x 55, répétée par défilement.
 *
 * Le raccord est la seule contrainte réelle : le motif doit être cyclique sur
 * 168 pixels, sinon une couture traverse l'écran à chaque tour. Les diagonales
 * sont donc posées à un pas qui divise 168 (24), et non à un pas choisi à
 * l'oeil — c'est le genre de détail qu'on ne voit qu'en jouant.
 */
/* Un pavé qui REBOUCLE sur la largeur de la planche. Indispensable ici et
 * nulle part ailleurs : le sol est la seule planche que le jeu répète, et un
 * trait coupé au bord droit qui ne repart pas du bord gauche produit une
 * couture qui traverse l'écran une fois par tour. */
static void pave_cyclique(toile *t, int x, int y, int w, int h, const unsigned char rgba[4])
{
    for (int i = 0; i < w; ++i) {
        int cx = (x + i) % t->w;
        if (cx < 0) cx += t->w;
        pave(t, cx, y, 1, h, rgba);
    }
}

static void planche_envol_passerelle(toile *t)
{
    static const unsigned char HAUT[4]   = { 206, 196, 150, 255 };
    static const unsigned char BORD[4]   = {  92,  84,  62, 255 };
    static const unsigned char TABLIER[4]= { 176, 164, 122, 255 };
    static const unsigned char RAYE[4]   = { 232, 176,  56, 255 };
    static const unsigned char OMBRE[4]  = { 148, 136,  98, 255 };

    pave(t, 0, 0, 168, 55, TABLIER);
    pave(t, 0, 0, 168, 6, RAYE);
    pave(t, 0, 6, 168, 2, BORD);
    pave(t, 0, 8, 168, 5, HAUT);
    pave(t, 0, 53, 168, 2, BORD);

    /* Les hachures du bandeau et le treillis du tablier tombent tous les 24 :
     * 168 / 24 = 7 exactement, donc le motif se referme sur lui-même. Un pas
     * choisi à l'oeil — 25, 30 — aurait mis une maille tronquée au raccord. */
    for (int x = 0; x < 168; x += 24)
        for (int i = 0; i < 6; ++i)
            pave_cyclique(t, x + i * 2, i, 8, 1, BORD);

    for (int x = 12; x < 168; x += 24) {
        pave_cyclique(t, x, 13, 3, 40, OMBRE);
        for (int i = 0; i < 20; ++i) {
            pave_cyclique(t, x + 3 + i, 15 + i, 2, 2, OMBRE);
            pave_cyclique(t, x + 22 - i, 15 + i, 2, 2, OMBRE);
        }
    }
}

/*
 * LE FOND : 288 x 256, deux ciels de 144. Le jeu n'affiche que le premier ;
 * le second existait pour la nuit et reste dessiné, à l'identique en valeurs
 * froides — supprimer la moitié droite ferait une planche de 144 que
 * `SPR_BG_W * 2.0f` découperait de travers.
 *
 * Ce qui est dessiné : un dégradé, une crête de collines, une rangée de mâts.
 * Ni immeubles ni haie — c'était la ligne d'horizon de l'original, et c'est
 * précisément ce qu'on remplace.
 */
static void ciel(toile *t, int x0, const float haut[3], const float bas[3],
                 const float colline[3], const float mat[3], bool nuit)
{
    for (int y = 0; y < 256; ++y) {
        const float u = (float)y / 255.0f;
        unsigned char c[4] = {
            borne8(haut[0] + (bas[0] - haut[0]) * u),
            borne8(haut[1] + (bas[1] - haut[1]) * u),
            borne8(haut[2] + (bas[2] - haut[2]) * u), 255u };
        pave(t, x0, y, 144, 1, c);
    }

    if (nuit) {
        /* Des étoiles à position FIXE, tirées d'un mélange entier : deux builds
         * doivent produire le même fichier, octet pour octet. `rand()` ne le
         * garantit pas d'une bibliothèque C à l'autre. */
        static const unsigned char BLANC[4] = { 236, 240, 250, 255 };
        uint32_t h = 0x9e3779b9u;
        for (int i = 0; i < 46; ++i) {
            h = h * 1664525u + 1013904223u;
            const int sx = (int)((h >> 8) % 144u);
            h = h * 1664525u + 1013904223u;
            const int sy = (int)((h >> 8) % 150u);
            pave(t, x0 + sx, sy, 1, 1, BLANC);
        }
    }

    /* La crête : deux cosinus de périodes premières entre elles, ce qui évite
     * la vague régulière qu'une seule sinusoïde donnerait. Périodes 144 et 48,
     * donc cycliques sur la largeur : le fond se répète sans couture. */
    const unsigned char CO[4] = { borne8(colline[0]), borne8(colline[1]), borne8(colline[2]), 255u };
    const unsigned char CO2[4] = { borne8(colline[0] * 0.72f), borne8(colline[1] * 0.72f),
                                   borne8(colline[2] * 0.72f), 255u };
    for (int x = 0; x < 144; ++x) {
        const float a = 6.2831853f * (float)x / 144.0f;
        const int y1 = 196 + (int)(9.0f * cosf(a) + 5.0f * cosf(a * 3.0f));
        const int y2 = 214 + (int)(6.0f * cosf(a * 2.0f + 1.1f));
        pave(t, x0 + x, y1, 1, 256 - y1, CO2);
        pave(t, x0 + x, y2, 1, 256 - y2, CO);
    }

    /* Les poteaux et leur ligne : la ligne d'horizon TECHNIQUE de cette salle
     * d'arcade, et pas une silhouette de ville — c'était justement la sienne
     * qu'on remplace. Pas de 24 : 144 / 24 = 6, donc cycliques.
     *
     * UNE traverse, tout en haut, et un fil qui pend entre deux poteaux. Deux
     * traverses au milieu du mât donnaient des croix plantées sur une colline,
     * ce que la première capture montrait sans qu'on l'ait voulu. */
    const unsigned char MA[4] = { borne8(mat[0]), borne8(mat[1]), borne8(mat[2]), 255u };
    for (int x = 6; x < 144; x += 24) {
        pave(t, x0 + x, 174, 2, 28, MA);
        pave(t, x0 + x - 4, 175, 10, 2, MA);
        /* Le fil, en chaînette approchée par une parabole : il RELIE les
         * poteaux, ce qui les fait lire comme une ligne et non comme des
         * piquets isolés. */
        for (int i = 0; i < 24; ++i) {
            const float u = (float)i / 24.0f - 0.5f;
            const int y = 178 + (int)(5.0f * (0.25f - u * u) * 4.0f);
            if (x + i < 144) pave(t, x0 + x + i, y, 1, 1, MA);
        }
    }
}

static void planche_envol_ciels(toile *t)
{
    static const float J_HAUT[3] = { 0.30f, 0.62f, 0.78f };
    static const float J_BAS[3]  = { 0.72f, 0.86f, 0.86f };
    static const float J_COL[3]  = { 0.30f, 0.52f, 0.44f };
    static const float J_MAT[3]  = { 0.20f, 0.34f, 0.36f };

    static const float N_HAUT[3] = { 0.04f, 0.07f, 0.17f };
    static const float N_BAS[3]  = { 0.14f, 0.22f, 0.36f };
    static const float N_COL[3]  = { 0.08f, 0.16f, 0.22f };
    static const float N_MAT[3]  = { 0.05f, 0.09f, 0.14f };

    ciel(t,   0, J_HAUT, J_BAS, J_COL, J_MAT, false);
    ciel(t, 144, N_HAUT, N_BAS, N_COL, N_MAT, true);
}

/* ==========================================================================
 * Le catalogue, et l'écriture
 * ========================================================================== */
typedef struct planche {
    const char *nom;
    int w, h;
    void (*dessine)(toile *t);
    const char *quoi;
} planche;

static const planche G_PLANCHES[] = {
    { "dedale_heros",     4 * DD_CASE, DD_CASE,     planche_dedale_heros,
      "le rubis, quatre eclats" },
    { "dedale_creatures", 4 * DD_CASE, 2 * DD_CASE, planche_dedale_creatures,
      "les helices, quatre phases + le moyeu mange" },
    { "envol_cerf",   51,  36, planche_envol_cerf, "le cerf-volant, 3 x 3 cases de 17 x 12" },
    { "envol_pylones",  104, 160, planche_envol_pylones, "quatre pylones de 26 x 160" },
    { "envol_passerelle", 168, 55, planche_envol_passerelle, "la passerelle, cyclique sur 168" },
    { "envol_ciels",   288, 256, planche_envol_ciels,  "deux ciels de 144 x 256, jour et nuit" },
};
#define N_PLANCHES ((int)(sizeof G_PLANCHES / sizeof G_PLANCHES[0]))

static void ecrire(const toile *t, const char *chemin)
{
    if (!stbi_write_png(chemin, t->w, t->h, 4, t->px, t->w * 4)) {
        tool_fatalf("écriture impossible : %s", chemin);
    }
}

int main(int argc, char **argv)
{
    const char *nom = NULL, *sortie = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--planche=", 10) == 0) nom = argv[i] + 10;
        else if (!sortie) sortie = argv[i];
        else tool_fatalf("argument inattendu : %s", argv[i]);
    }

    if (!nom || !sortie) {
        fprintf(stderr,
            "spriteart — dessine une planche de sprites de mini-jeu\n"
            "usage : %s --planche=NOM <sortie.png>\n", argv[0]);
        for (int i = 0; i < N_PLANCHES; ++i)
            fprintf(stderr, "  %-18s %3d x %3d  %s\n", G_PLANCHES[i].nom,
                    G_PLANCHES[i].w, G_PLANCHES[i].h, G_PLANCHES[i].quoi);
        return 2;
    }

    for (int i = 0; i < N_PLANCHES; ++i) {
        if (strcmp(nom, G_PLANCHES[i].nom) != 0) continue;
        toile t;
        toile_init(&t, G_PLANCHES[i].w, G_PLANCHES[i].h);
        G_PLANCHES[i].dessine(&t);
        ecrire(&t, sortie);
        printf("spriteart %s : %dx%d\n", nom, t.w, t.h);
        tool_infof("%s", sortie);
        free(t.px);
        return 0;
    }
    tool_fatalf("planche inconnue : « %s »", nom);
    return 1;
}
