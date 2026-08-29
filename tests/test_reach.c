/*
 * test_reach.c — le remplissage par diffusion de `roomgen`, sur des pièces
 * fabriquées.
 *
 * CE QU'IL EXISTE POUR ATTRAPER. La salle a été livrée avec son joueur ENFERMÉ :
 * un sas déclaré comme contour fermé, sans ouverture vers le hall, et
 * `playerStart` dedans. Le build a réussi, trente-six tests sont passés, le
 * paquet est parti. Le contrôle C-10 (`check_reachable`, tools/roomgen.c) existe
 * depuis pour que ce soit impossible — et un contrôle qu'on n'a pas éprouvé sur
 * une pièce scellée CONNUE est un contrôle dont on ne sait pas s'il verrait la
 * prochaine. C'est tout l'objet de ce fichier.
 *
 * Les quatre cas qui comptent, et ils ne sont pas décoratifs :
 *
 *   - une pièce OUVERTE : le joueur atteint le fond, et il s'arrête à un rayon
 *     du parement — ni plus (le contrôle serait trop sévère et se ferait
 *     retirer), ni moins (il laisserait passer un couloir trop étroit) ;
 *   - une pièce SCELLÉE : exactement le sas. Le départ dedans, les cibles
 *     dehors, et deux poches distinctes ;
 *   - une porte à la largeur EXACTE du joueur : elle doit passer, quel que soit
 *     l'endroit où la grille tombe ;
 *   - une porte TROP ÉTROITE : elle ne doit jamais passer, au même titre.
 *
 * Les deux derniers sont éprouvés sur cinq alignements de grille différents. Un
 * verdict qui dépendrait de l'endroit où la grille tombe serait pire qu'un
 * verdict faux : il serait irreproductible, et personne ne croirait plus le
 * contrôle. C'est ce que la note de `reach_grid.h` promet ; ce test le vérifie.
 *
 * Ni GPU, ni asset, ni salle : de la géométrie posée à la main.
 */
#include "reach_grid.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  ÉCHEC %s:%d — ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((a) - (b)) <= (eps), \
    "%s ≈ %s : %.4f vs %.4f", #a, #b, (double)(a), (double)(b))

/* Les cotes du joueur, celles de `room/room_camera.c` et de `RG_BODY_RADIUS`. Le
 * pas est celui que `roomgen` emploie : le tester à un autre pas testerait autre
 * chose que ce qui tourne. */
#define BODY   0.32f
#define CELL   0.05f

/* Un contour rectangulaire fermé, comme `reach_add_walls` le pose : une jointure
 * à chaque coin, donc un prolongement d'une demi-épaisseur des deux côtés. */
static void closed_rect(reach_grid *g, unsigned char *mask,
                        float x0, float z0, float x1, float z1, float half)
{
    const ns_v2 p[4] = {
        ns_v2_make(x0, z0), ns_v2_make(x1, z0),
        ns_v2_make(x1, z1), ns_v2_make(x0, z1)
    };
    for (int i = 0; i < 4; ++i) {
        reach_mask_segment(g, mask, p[i], p[(i + 1) % 4], half, half, half);
    }
}

/* -------------------------------------------------------------------------- */
/* 1. Une pièce ouverte                                                        */
/* -------------------------------------------------------------------------- */

static void test_piece_ouverte(void)
{
    printf("— une pièce ouverte\n");

    reach_grid g;
    reach_grid_init(&g, -4.2f, -3.2f, 4.2f, 3.2f, CELL);
    unsigned char *mask = reach_mask_new(&g);
    closed_rect(&g, mask, -4.0f, -3.0f, 4.0f, 3.0f, 0.10f);
    reach_grid_add(&g, mask);
    reach_mask_free(mask);
    reach_grid_solve(&g, BODY);

    const int home = reach_grid_label_at(&g, 0.0f, 0.0f);
    CHECK(home >= 0, "le centre d'une pièce vide doit être foulable");

    /* Les quatre coins, à un rayon et demi des deux parements. Une pièce ouverte
     * n'a qu'une poche : si l'un d'eux répondait autre chose, le remplissage
     * s'arrêterait quelque part sans raison. */
    const float corners[4][2] = {
        { -3.4f, -2.4f }, { 3.4f, -2.4f }, { -3.4f, 2.4f }, { 3.4f, 2.4f }
    };
    for (int i = 0; i < 4; ++i) {
        CHECK(reach_grid_label_at(&g, corners[i][0], corners[i][1]) == home,
              "le coin (%.1f, %.1f) doit être dans la même poche que le centre",
              (double)corners[i][0], (double)corners[i][1]);
    }

    /* AUCUNE autre poche : ni anneau autour de la pièce (la grille serre le mur
     * de dix centimètres, moins qu'un rayon), ni îlot oublié. */
    CHECK(g.components == 1, "une pièce vide fait UNE poche, pas %d", g.components);

    /*
     * LE POINT QUI COMPTE : le joueur s'arrête à un rayon du parement.
     *
     * Le parement intérieur est à 3,90 m (médiane 4,00, demi-épaisseur 0,10), et
     * le corps fait 0,32 m de rayon : la dernière cellule foulable doit être à
     * 3,58 m, à un pas de grille près. Plus loin, le contrôle laisserait le
     * joueur entrer dans le mur ; plus près, il refuserait des couloirs
     * parfaitement praticables — et ce contrôle-là serait retiré dans l'heure.
     */
    float min_x, min_z, max_x, max_z;
    reach_grid_extent(&g, home, &min_x, &min_z, &max_x, &max_z);
    CHECK_NEAR(max_x,  3.90f - BODY, CELL);
    CHECK_NEAR(min_x, -3.90f + BODY, CELL);
    CHECK_NEAR(max_z,  2.90f - BODY, CELL);
    CHECK_NEAR(min_z, -2.90f + BODY, CELL);

    /*
     * Et l'aire suit : le rectangle intérieur rentré d'un rayon sur ses quatre
     * côtés. C'est le garde-fou contre la FUITE — un coin d'onglet mal fermé
     * ferait sortir le remplissage, et l'aire doublerait d'un coup.
     *
     * La tolérance vaut un mètre carré et demi parce que le pas de grille rendu
     * au joueur (voir `reach_grid.h`) ajoute une cellule tout autour : sur un
     * périmètre de 25 m, cela fait déjà 1,2 m² de plus, légitimement.
     */
    const float expected = (2.0f * (3.90f - BODY)) * (2.0f * (2.90f - BODY));
    CHECK(fabsf(reach_grid_area(&g, home) - expected) < 1.5f,
          "aire atteignable %.2f m² pour %.2f m² attendus",
          (double)reach_grid_area(&g, home), (double)expected);

    /* Un point DANS le mur n'est pas foulable — c'est le premier des cinq
     * verdicts de C-10, celui du joueur qui naît dans la brique. */
    CHECK(reach_grid_label_at(&g, 4.0f, 0.0f) < 0,
          "un point au milieu du mur ne doit pas être foulable");
    /* Et un point collé au parement non plus : le corps a une largeur. */
    CHECK(reach_grid_label_at(&g, 3.85f, 0.0f) < 0,
          "un point à 5 cm du parement ne peut pas accueillir un corps de 32 cm");

    reach_grid_release(&g);
}

/* -------------------------------------------------------------------------- */
/* 2. Une pièce scellée — le défaut, reproduit                                 */
/* -------------------------------------------------------------------------- */

static void test_piece_scellee(void)
{
    printf("— une pièce scellée dans une autre (le sas)\n");

    reach_grid g;
    reach_grid_init(&g, -4.2f, -3.2f, 4.2f, 3.2f, CELL);
    unsigned char *mask = reach_mask_new(&g);
    closed_rect(&g, mask, -4.0f, -3.0f, 4.0f, 3.0f, 0.10f);
    /* Le sas : un contour fermé de 1,67 x 4,00 m sans la moindre baie, posé dans
     * l'angle. Ce sont les cotes de celui qui est parti en production. */
    closed_rect(&g, mask, 1.60f, -2.80f, 3.27f, 1.20f, 0.10f);
    reach_grid_add(&g, mask);
    reach_mask_free(mask);
    reach_grid_solve(&g, BODY);

    const int home = reach_grid_label_at(&g, 2.43f, -0.80f);   /* dedans */
    const int hall = reach_grid_label_at(&g, -2.00f, 0.00f);   /* dehors */

    CHECK(home >= 0, "le joueur tient DANS le sas — c'est bien le problème");
    CHECK(hall >= 0, "le hall reste foulable");
    CHECK(home != hall,
          "un contour fermé sans baie DOIT séparer deux poches ; le contrôle "
          "rend %d des deux côtés", home);
    CHECK(g.components == 2, "un sas scellé fait 2 poches, pas %d", g.components);

    /* L'aire de la poche du départ : 1,67 x 4,00 m d'axes, donc 1,47 x 3,80 m
     * dedans, rentrés d'un rayon. C'est ce chiffre-là — quelques mètres carrés
     * pour une salle de deux cents — qui doit sauter aux yeux dans le journal. */
    const float expected = (1.47f - 2.0f * BODY) * (3.80f - 2.0f * BODY);
    const float sas_area = reach_grid_area(&g, home);
    const float hall_area = reach_grid_area(&g, hall);
    CHECK(fabsf(sas_area - expected) < 0.5f,
          "poche du sas %.2f m² pour %.2f m² attendus",
          (double)sas_area, (double)expected);
    CHECK(hall_area > 5.0f * sas_area,
          "le hall doit être sans commune mesure avec le sas : %.2f contre %.2f m²",
          (double)hall_area, (double)sas_area);

    reach_grid_release(&g);
}

/* -------------------------------------------------------------------------- */
/* 3 et 4. Deux pièces, une porte                                              */
/* -------------------------------------------------------------------------- */

/*
 * Une cloison percée d'une baie centrée, PAR LE MÊME CHEMIN que `roomgen` : on
 * marque le pan entier, puis on efface la baie dans le masque. Tester deux
 * segments posés de part et d'autre du vide testerait un code que la salle
 * n'emprunte pas.
 *
 * `shift` décale l'origine de la grille : c'est ce qui permet de vérifier que le
 * verdict ne dépend pas de l'endroit où la grille tombe sur la porte.
 */
static bool door_connects(float width, float shift)
{
    reach_grid g;
    reach_grid_init(&g, -4.2f + shift, -3.2f + shift, 4.2f + shift, 3.2f + shift, CELL);

    unsigned char *mask = reach_mask_new(&g);
    closed_rect(&g, mask, -4.0f, -3.0f, 4.0f, 3.0f, 0.10f);
    reach_mask_segment(&g, mask, ns_v2_make(0.0f, -3.0f), ns_v2_make(0.0f, 3.0f),
                       0.10f, 0.10f, 0.10f);
    reach_mask_carve(&g, mask, ns_v2_make(0.0f, -width * 0.5f),
                     ns_v2_make(0.0f, width * 0.5f), 0.10f + CELL);
    reach_grid_add(&g, mask);
    reach_mask_free(mask);
    reach_grid_solve(&g, BODY);

    const int left  = reach_grid_label_at(&g, -2.0f, 0.0f);
    const int right = reach_grid_label_at(&g,  2.0f, 0.0f);
    const bool joined = (left >= 0 && left == right);
    reach_grid_release(&g);
    return joined;
}

static void test_portes(void)
{
    printf("— une porte, à cinq alignements de grille\n");

    /* Cinq décalages, dont trois qui ne sont pas des multiples du pas : c'est
     * exactement là qu'un contrôle mal réglé bascule. */
    static const float shifts[5] = { 0.0f, 0.012f, 0.025f, 0.031f, 0.047f };

    for (int i = 0; i < 5; ++i) {
        /* La largeur EXACTE du joueur : deux rayons. Elle doit passer — c'est la
         * promesse écrite en tête de `reach_grid.h`, et la refuser rendrait le
         * contrôle insupportable sur une salle jouable. */
        CHECK(door_connects(2.0f * BODY, shifts[i]),
              "une porte de %.2f m (deux rayons) doit passer, décalage %.3f m",
              (double)(2.0f * BODY), (double)shifts[i]);

        /* Trop étroite, et de loin : 30 cm pour un corps de 64. Aucun alignement
         * ne doit la laisser passer. */
        CHECK(!door_connects(0.30f, shifts[i]),
              "une porte de 0.30 m ne doit JAMAIS passer, décalage %.3f m",
              (double)shifts[i]);

        /* Une porte confortable passe évidemment ; le cas est là pour qu'un
         * contrôle devenu trop sévère se fasse voir tout de suite. */
        CHECK(door_connects(1.20f, shifts[i]),
              "une porte de 1.20 m doit passer, décalage %.3f m",
              (double)shifts[i]);
    }

    /* Sans la baie du tout, les deux pièces sont séparées. Sinon les trois cas
     * ci-dessus ne prouveraient rien : ils passeraient tous. */
    CHECK(!door_connects(0.0f, 0.0f),
          "une cloison pleine sépare les deux pièces");
}

/* -------------------------------------------------------------------------- */
/* 5. Deux murs superposés : la baie de l'un ne perce pas l'autre              */
/* -------------------------------------------------------------------------- */

/*
 * LE DÉFAUT EXACT DE LA SALLE, dans sa forme la plus courte.
 *
 * Le sas était décrit DEUX fois — un contour fermé percé de deux baies, et une
 * polyligne suivant le même tracé, sans aucune. La seconde rebouchait les baies
 * de la première, et rien ne le disait : chaque mur, pris seul, avait ses
 * ouvertures.
 *
 * D'où le masque par mur dans `reach_add_walls`. Percer directement dans la
 * grille ouvrirait la baie de l'un dans le plein de l'autre, et le contrôle
 * déclarerait la salle traversable — c'est-à-dire qu'il mentirait exactement là
 * où on l'attend.
 */
static void test_masques_separes(void)
{
    printf("— la baie d'un mur ne perce pas le mur qui le double\n");

    reach_grid g;
    reach_grid_init(&g, -4.2f, -3.2f, 4.2f, 3.2f, CELL);

    unsigned char *outer = reach_mask_new(&g);
    closed_rect(&g, outer, -4.0f, -3.0f, 4.0f, 3.0f, 0.10f);
    reach_grid_add(&g, outer);
    reach_mask_free(outer);

    /* La cloison percée d'une porte large. */
    unsigned char *pierced = reach_mask_new(&g);
    reach_mask_segment(&g, pierced, ns_v2_make(0.0f, -3.0f), ns_v2_make(0.0f, 3.0f),
                       0.10f, 0.10f, 0.10f);
    reach_mask_carve(&g, pierced, ns_v2_make(0.0f, -0.60f), ns_v2_make(0.0f, 0.60f),
                     0.10f + CELL);
    reach_grid_add(&g, pierced);
    reach_mask_free(pierced);

    reach_grid_solve(&g, BODY);
    const int open_left  = reach_grid_label_at(&g, -2.0f, 0.0f);
    const int open_right = reach_grid_label_at(&g,  2.0f, 0.0f);
    CHECK(open_left >= 0 && open_left == open_right,
          "seule, la cloison percée laisse passer");

    /* Le doublon, sur le même tracé, sans baie. */
    unsigned char *twin = reach_mask_new(&g);
    reach_mask_segment(&g, twin, ns_v2_make(0.0f, -3.0f), ns_v2_make(0.0f, 3.0f),
                       0.10f, 0.10f, 0.10f);
    reach_grid_add(&g, twin);
    reach_mask_free(twin);
    reach_grid_solve(&g, BODY);

    const int left  = reach_grid_label_at(&g, -2.0f, 0.0f);
    const int right = reach_grid_label_at(&g,  2.0f, 0.0f);
    CHECK(left >= 0 && right >= 0 && left != right,
          "le mur qui double sans baie DOIT refermer le passage — c'est le "
          "défaut qui a enfermé le joueur");

    reach_grid_release(&g);
}

/* -------------------------------------------------------------------------- */
/* 6. Deux pièces mitoyennes — le faux positif, reproduit                      */
/* -------------------------------------------------------------------------- */

/*
 * LE CAS QUI MANQUAIT, et qui a coûté un faux positif.
 *
 * Le hall et le bloc sanitaire de la salle s'aboutent, et leurs lignes MÉDIANES
 * ne coïncident pas : 7,215 pour l'un, 7,279 pour l'autre. La bande de 6,4 cm
 * entre les deux — une cellule et demie au pas de 5 cm — n'est strictement dans
 * AUCUN des deux polygones. La première version de la règle du dehors la
 * bouchait, ce qui refermait la baie de 3,42 m qui relie les deux pièces : C-10
 * annonçait le bloc sanitaire inatteignable alors qu'on y entre de plain-pied.
 *
 * Un faux positif est plus grave qu'un défaut manqué. Un contrôle qui crie faux
 * se fait retirer, et on retombe alors sur le joueur enfermé — c'est-à-dire sur
 * le défaut que ce contrôle existe pour empêcher.
 *
 * Les cotes ci-dessous sont celles de la salle : 6,4 cm d'écart, murs de 20 cm.
 */
static const ns_v2 MITOYEN_HALL[4] = {
    { -3.000f, -2.0f }, { 0.000f, -2.0f }, { 0.000f, 2.0f }, { -3.000f, 2.0f }
};
static const ns_v2 MITOYEN_ANNEXE[4] = {
    { 0.064f, -2.0f }, { 3.0f, -2.0f }, { 3.0f, 2.0f }, { 0.064f, 2.0f }
};

/* `pierce` : les deux murs mitoyens portent chacun leur baie, face à face —
 * exactement comme `baie_sanitaires` et `baie_sanitaires_int`. */
static bool mitoyennes_connected(bool pierce, float *out_min_x)
{
    const reach_contour contours[2] = {
        { MITOYEN_HALL,   4, 0.20f },
        { MITOYEN_ANNEXE, 4, 0.20f }
    };

    reach_grid g;
    reach_grid_init(&g, -3.4f, -2.4f, 3.4f, 2.4f, CELL);

    unsigned char *mask = reach_mask_new(&g);
    closed_rect(&g, mask, -3.0f, -2.0f, 0.0f, 2.0f, 0.10f);
    if (pierce) {
        reach_mask_carve(&g, mask, ns_v2_make(0.0f, -0.60f),
                         ns_v2_make(0.0f, 0.60f), 0.10f + CELL);
    }
    reach_grid_add(&g, mask);
    reach_mask_free(mask);

    mask = reach_mask_new(&g);
    closed_rect(&g, mask, 0.064f, -2.0f, 3.0f, 2.0f, 0.10f);
    if (pierce) {
        reach_mask_carve(&g, mask, ns_v2_make(0.064f, -0.60f),
                         ns_v2_make(0.064f, 0.60f), 0.10f + CELL);
    }
    reach_grid_add(&g, mask);
    reach_mask_free(mask);

    reach_grid_close_outside(&g, contours, 2);
    reach_grid_solve(&g, BODY);

    const int left  = reach_grid_label_at(&g, -1.5f, 0.0f);
    const int right = reach_grid_label_at(&g,  1.5f, 0.0f);
    if (out_min_x) {
        float unused_z, unused_max_x, unused_max_z;
        reach_grid_extent(&g, left, out_min_x, &unused_z, &unused_max_x, &unused_max_z);
    }
    const bool joined = (left >= 0 && left == right);
    reach_grid_release(&g);
    return joined;
}

static void test_pieces_mitoyennes(void)
{
    printf("— deux pièces mitoyennes, médianes décalées de 6,4 cm\n");

    float min_x = 0.0f;
    CHECK(mitoyennes_connected(true, &min_x),
          "deux pièces mitoyennes percées face à face DOIVENT communiquer — la "
          "bande entre leurs médianes est du MUR, pas de la rue");
    CHECK(!mitoyennes_connected(false, NULL),
          "sans baie, elles restent séparées : la tolérance d'épaisseur ne doit "
          "pas ouvrir un passage que la géométrie n'a pas");

    /*
     * ET LA TOLÉRANCE N'A RIEN ÉLARGI D'AUTRE. Le mur ouest du hall n'a pas de
     * baie : il reste plein, et le joueur s'y arrête à un rayon de son parement
     * INTÉRIEUR, à −2,90 m. Accepter l'épaisseur des murs comme « dans le
     * bâtiment » ne doit pas déplacer cette limite d'un centimètre — sans quoi
     * la correction du faux positif aurait ouvert la rue par le mur d'en face.
     */
    CHECK_NEAR(min_x, -2.90f + BODY, 2.0f * CELL);
}

/* -------------------------------------------------------------------------- */
/* 7. La rue ne s'ouvre pas par la porte d'entrée                              */
/* -------------------------------------------------------------------------- */

/*
 * Une salle a une porte sur la rue, et la règle du dehors est tout ce qui
 * empêche le remplissage de sortir par là. S'il sortait, il inonderait le cadre
 * de la grille : l'aire mesurée deviendrait un grand chiffre parfaitement faux,
 * et le seuil de C-10 ne verrait plus jamais un joueur enfermé.
 */
static void test_porte_sur_la_rue(void)
{
    printf("— une porte sur la rue ne laisse pas fuir le remplissage\n");

    static const ns_v2 salle[4] = {
        { -3.0f, -2.0f }, { 3.0f, -2.0f }, { 3.0f, 2.0f }, { -3.0f, 2.0f }
    };
    const reach_contour contours[1] = { { salle, 4, 0.20f } };

    reach_grid g;
    reach_grid_init(&g, -6.0f, -5.0f, 6.0f, 5.0f, CELL);   /* large : de la rue partout */

    unsigned char *mask = reach_mask_new(&g);
    closed_rect(&g, mask, -3.0f, -2.0f, 3.0f, 2.0f, 0.10f);
    /* Une porte de 1,20 m dans le mur sud, grande ouverte sur le trottoir. */
    reach_mask_carve(&g, mask, ns_v2_make(-0.60f, -2.0f), ns_v2_make(0.60f, -2.0f),
                     0.10f + CELL);
    reach_grid_add(&g, mask);
    reach_mask_free(mask);

    reach_grid_close_outside(&g, contours, 1);
    reach_grid_solve(&g, BODY);

    const int home = reach_grid_label_at(&g, 0.0f, 0.0f);
    CHECK(home >= 0, "l'intérieur reste foulable");
    CHECK(reach_grid_label_at(&g, 0.0f, -3.0f) < 0,
          "un mètre au-delà de la porte, c'est la rue : rien de foulable");
    CHECK(reach_grid_label_at(&g, 5.0f, 4.0f) < 0,
          "le coin de la grille, loin de tout mur, ne doit rien accueillir");

    /* L'aire reste celle de la pièce, pas celle du cadre : 12 x 10 m de grille
     * pour 5,8 x 3,8 m de pièce. Une fuite se verrait immédiatement ici. */
    const float expected = (5.80f - 2.0f * BODY) * (3.80f - 2.0f * BODY);
    CHECK(fabsf(reach_grid_area(&g, home) - expected) < 1.5f,
          "aire %.2f m² pour %.2f m² attendus — au-delà, le remplissage a fui "
          "sur le trottoir",
          (double)reach_grid_area(&g, home), (double)expected);

    reach_grid_release(&g);
}

/* -------------------------------------------------------------------------- */
/* 8. Un meuble, au triangle                                                   */
/* -------------------------------------------------------------------------- */

/*
 * Les solides entrent dans la grille par leurs TRIANGLES, et une face verticale
 * se projette en plan sur un SEGMENT. Marquée par sa boîte englobante, elle
 * barrerait un carré de sa longueur de côté — un mur de neuf mètres barrerait
 * quatre-vingts mètres carrés, et la salle serait déclarée injouable sans qu'on
 * comprenne pourquoi.
 */
static void test_triangle_plat(void)
{
    printf("— une face verticale ne barre qu'une ligne\n");

    reach_grid g;
    reach_grid_init(&g, -4.2f, -3.2f, 4.2f, 3.2f, CELL);
    unsigned char *mask = reach_mask_new(&g);
    closed_rect(&g, mask, -4.0f, -3.0f, 4.0f, 3.0f, 0.10f);
    reach_grid_add(&g, mask);
    reach_mask_free(mask);

    /* Un panneau vu de dessus : trois points ALIGNÉS, ce que donne toute face
     * verticale une fois projetée. Il joint deux coins opposés de la pièce. */
    reach_grid_block_triangle(&g, ns_v2_make(-3.90f, -2.90f),
                              ns_v2_make(0.0f, 0.0f), ns_v2_make(3.90f, 2.90f));
    reach_grid_solve(&g, BODY);

    /* Les deux côtés restent foulables, et séparés : le panneau barre bel et
     * bien, mais seulement sur sa ligne. */
    const int north = reach_grid_label_at(&g, -2.5f, 2.0f);
    const int south = reach_grid_label_at(&g,  2.5f, -2.0f);
    CHECK(north >= 0 && south >= 0,
          "les deux côtés du panneau restent foulables (%d, %d) — marqué par sa "
          "BOÎTE, un triangle plat aurait rempli toute la pièce", north, south);
    CHECK(north != south, "un panneau qui joint deux murs sépare la pièce");

    /* Et chaque moitié reste ample. Marquée par sa boîte englobante, la diagonale
     * aurait pris les 45 m² d'un coup. */
    const float area_north = reach_grid_area(&g, north);
    const float area_south = reach_grid_area(&g, south);
    CHECK(area_north > 5.0f && area_south > 5.0f,
          "chaque moitié doit rester ample : %.2f et %.2f m²",
          (double)area_north, (double)area_south);

    reach_grid_release(&g);
}

int main(void)
{
    printf("test_reach — accessibilité de la salle (C-10)\n");
    test_piece_ouverte();
    test_piece_scellee();
    test_portes();
    test_masques_separes();
    test_pieces_mitoyennes();
    test_porte_sur_la_rue();
    test_triangle_plat();

    printf("%d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
