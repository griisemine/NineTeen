/*
 * test_jeton.c — le FRONT du jeton, et rien d'autre.
 *
 * Ce que ce test attrape, plutôt que ce qu'il a l'air de vérifier
 * ---------------------------------------------------------------
 * Il ne vérifie pas qu'un bruit de pièce est joli — aucun test ne saura jamais
 * ça. Il vérifie la seule chose qui rende ce bruit AUDIBLE au bon moment, et
 * c'est du calcul pur : un front levé une fois, à l'instant où l'image le
 * justifie, et jamais ailleurs.
 *
 * Le défaut que ça attrape est précis et il a déjà eu lieu une fois dans ce
 * dépôt, pour le coup de poing : un front testé comme un SEUIL. À 120 Hz un pas
 * fait 8,3 ms ; « `elapsed` a dépassé le sommet » reste vrai pendant tout le
 * reste de l'état, soit trente pas pour l'insertion. Le son partirait trente
 * fois, et l'oreille entendrait une rafale de pièces là où il n'y en a qu'une.
 * Ce défaut-là ne se voit sur aucune capture et ne casse aucun autre test.
 *
 * Les trois refus sont aussi importants que le déclenchement, et pour la même
 * raison : un jeton qui sonne quand on cogne la machine ferait croire qu'on
 * vient de payer.
 *
 * Ni GPU, ni fenêtre, ni asset : `room_viewmodel.c` est du calcul pur et il
 * l'est précisément pour pouvoir être vérifié ici.
 */
#include "ns_core.h"
#include "room_camera.h"
#include "room_viewmodel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/*
 * Le pas de simulation du jeu. 120 Hz, et le chiffre compte pour ce test : il
 * est ce qui rend un seuil indiscernable d'un front sur une seule image, et
 * discernable sur trente.
 */
#define DT (1.0f / 120.0f)

static room_camera make_player(void)
{
    room_camera c;
    room_camera_init(&c, ns_v3_make(0.0f, 1.70f, 0.0f), 0.0f);
    c.mode = ROOM_CAM_PLAYER;
    return c;
}

/*
 * La borne, aux mêmes cotes que `test_ik.c` et pour la même raison : lacet nul
 * regarde vers +X, donc la borne est en +X et sa normale d'écran pointe vers le
 * joueur. Les cotes locales viennent de `build_cabinet` — fente à y 0,65 et
 * 0,452 m devant le centre, boutons à y 0,968 et 0,5225 m devant.
 */
static ns_cabinet make_cabinet(void)
{
    ns_cabinet cab;
    memset(&cab, 0, sizeof cab);
    SDL_strlcpy(cab.name, "borne_test", sizeof cab.name);
    SDL_strlcpy(cab.game, "envol", sizeof cab.game);
    cab.screen_normal = ns_v3_make(-1.0f, 0.0f, 0.0f);
    cab.coin_slot    = ns_v3_make(1.01f - 0.452f,  0.650f, 0.0f);
    cab.panel_centre = ns_v3_make(1.01f - 0.5225f, 0.968f, 0.0575f);
    cab.stick_top    = ns_v3_make(1.01f - 0.5225f, 1.072f, -0.1425f);
    cab.screen_material = -1;
    return cab;
}

/*
 * Avance de `steps` pas en comptant les fronts, et retient l'instant du
 * PREMIER. C'est le seul instrument de ce fichier : tous les tests qui suivent
 * ne font qu'appeler celui-ci et regarder ce qu'il rend.
 */
typedef struct compte {
    int   fronts;
    float premier;     /* secondes depuis le début de l'avance, −1 si aucun */
    ns_v3 ou;          /* la fente rendue par le front */
} compte;

static compte avance(room_viewmodel *vm, const room_camera *cam, int steps)
{
    compte c = { 0, -1.0f, ns_v3_zero() };
    for (int i = 0; i < steps; ++i) {
        room_viewmodel_tick(vm, cam, DT);
        ns_v3 at = ns_v3_zero();
        if (room_viewmodel_take_token(vm, &at)) {
            if (c.fronts == 0) { c.premier = (float)(i + 1) * DT; c.ou = at; }
            c.fronts++;
        }
    }
    return c;
}

/* ==========================================================================
 * 1. La séquence complète : UN front, et au bon endroit
 * ========================================================================== */

static void test_sequence_leve_un_seul_front(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, DT);

    const ns_cabinet cab = make_cabinet();
    CHECK(room_viewmodel_interact(&vm, &cab), "le geste démarre");
    CHECK(!room_viewmodel_take_token(&vm, NULL),
          "rien n'est levé au démarrage : la pièce est encore dans la main");

    /* Deux secondes couvrent largement les 1,31 s de REACH + INSERT + PRESS,
     * et surtout la totalité de l'état d'insertion : c'est cette marge qui
     * ferait exploser le compte si le front était un seuil. */
    const compte c = avance(&vm, &cam, 240);

    printf("  séquence complète : %d front(s), le premier à %.0f ms\n",
           c.fronts, (double)c.premier * 1000.0);

    CHECK(c.fronts == 1, "le front est levé UNE seule fois (%d)", c.fronts);

    /*
     * L'INSTANT, et il est dérivé plutôt que recopié : REACH dure 420 ms, la
     * poussée culmine à 55 % des 550 ms d'INSERT, soit 302,5 ms plus loin.
     * 722,5 ms. La tolérance est d'un pas de simulation, parce que le front
     * tombe sur le pas qui FRANCHIT le sommet et non exactement dessus.
     *
     * Refaire la dérivation ici plutôt que d'écrire 0,7225 est ce qui fait que
     * le test attrape une durée changée d'un côté sans l'autre — le contraire
     * d'un test qui recopie la constante qu'il vérifie.
     */
    const float attendu = 0.42f + 0.55f * 0.55f;
    CHECK(fabsf(c.premier - attendu) <= DT + 1e-4f,
          "le front tombe au sommet de la poussée : %.1f ms, attendu %.1f (± %.1f)",
          (double)c.premier * 1000.0, (double)attendu * 1000.0, (double)DT * 1000.0);

    /* La fente rendue est bien celle de la borne visée : c'est ce qui permet à
     * l'appelant de placer le son sans retrouver la borne lui-même. */
    CHECK(ns_v3_dist(c.ou, cab.coin_slot) < 1e-4f,
          "le front rend la fente visée (écart %.4f m)",
          (double)ns_v3_dist(c.ou, cab.coin_slot));

    /* Et le jeton a quitté la main À CET INSTANT, pas à la fin de l'état : le
     * voir encore pincé alors qu'on vient de l'entendre tomber est exactement
     * le décalage que ce front existe pour supprimer. */
    CHECK(!vm.token_visible, "la pièce a quitté la main au moment du front");
}

/* ==========================================================================
 * 2. Une pose figée ne paie pas
 * ========================================================================== */

static void test_pose_figee_ne_leve_rien(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);

    CHECK(room_viewmodel_set_forced_pose(&vm, "insert"), "« insert » est une pose connue");

    /*
     * `--pose=insert` sert aux CAPTURES : elle replace l'état à 82 % de sa durée
     * à chaque image et ne fait donc avancer aucun temps. Trois secondes — 360
     * pas — sans qu'une seule pièce ne tombe.
     *
     * Ce n'est pas une précaution théorique : la pose est figée APRÈS le sommet
     * de la poussée. Un front écrit comme « `elapsed` dépasse le sommet »
     * plutôt que « le pas qui le franchit » sonnerait ici trois cent soixante
     * fois, sur une capture, sans que personne ne l'entende jamais.
     */
    const compte c = avance(&vm, &cam, 360);
    printf("  pose figée « insert » : %d front(s) sur 360 pas\n", c.fronts);
    CHECK(c.fronts == 0, "une pose figée ne fait tomber aucun jeton (%d)", c.fronts);
}

/* ==========================================================================
 * 3. Cogner la machine ne fait pas tomber de jeton
 * ========================================================================== */

static void test_cogner_ne_paie_pas(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, DT);

    const ns_cabinet cab = make_cabinet();
    CHECK(room_viewmodel_frappe(&vm, &cab), "le coup part");

    /* Une seconde : le geste dure 500 ms, donc on couvre aussi le retour au
     * repos. */
    const compte c = avance(&vm, &cam, 120);
    printf("  coup sur la borne : %d front(s) de jeton\n", c.fronts);

    CHECK(c.fronts == 0, "cogner ne fait tomber aucun jeton (%d)", c.fronts);

    /*
     * ET LE CONTRÔLE INVERSE, sans lequel le précédent ne vaut rien : le coup a
     * bien levé SON front à lui. Sans cette ligne, un `tick` qui ne ferait plus
     * rien du tout passerait le test précédent avec les honneurs.
     */
    room_viewmodel vm2;
    room_viewmodel_init(&vm2);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm2, &cam, DT);
    CHECK(room_viewmodel_frappe(&vm2, &cab), "le coup part (second sujet)");
    int impacts = 0;
    for (int i = 0; i < 120; ++i) {
        room_viewmodel_tick(&vm2, &cam, DT);
        if (room_viewmodel_take_impact(&vm2, NULL, NULL)) impacts++;
    }
    CHECK(impacts == 1, "le coup lève bien son propre front, une fois (%d)", impacts);
}

/* ==========================================================================
 * 4. La relance : un geste court, de la seule main droite
 * ========================================================================== */

static void test_relance(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, DT);

    const ns_cabinet cab = make_cabinet();

    /* Hors partie, la relance n'a aucun sens : le geste part des commandes. */
    CHECK(!room_viewmodel_relance(&vm), "la relance est refusée les mains vides");

    room_viewmodel_start_playing(&vm, &cab);
    /* Une seconde de jeu pour que les poignets soient arrivés sur les commandes :
     * ils sont amortis, et les mesurer avant qu'ils n'aient convergé comparerait
     * un geste à une transition. */
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, DT);

    const ns_v3 main_g = vm.wrist_l, main_d = vm.wrist_r;

    CHECK(room_viewmodel_relance(&vm), "la relance part depuis les commandes");
    CHECK(vm.state == ROOM_VM_RELANCE, "on est dans l'état de relance");
    CHECK(vm.token_visible, "la pièce est revenue dans la main droite");
    CHECK(!room_viewmodel_relance(&vm), "un geste à la fois");

    /* Jusqu'au sommet de l'aller : 220 ms, soit 27 pas à 120 Hz. */
    const compte c = avance(&vm, &cam, 27);
    const float dg = ns_v3_dist(vm.wrist_l, main_g);
    const float dd = ns_v3_dist(vm.wrist_r, main_d);

    printf("  relance : %d front(s) à %.0f ms ; la main gauche a bougé de %.1f cm, "
           "la droite de %.1f cm\n",
           c.fronts, (double)c.premier * 1000.0,
           (double)dg * 100.0, (double)dd * 100.0);

    CHECK(c.fronts == 1, "la relance lève le front une seule fois (%d)", c.fronts);
    CHECK(fabsf(c.premier - 0.220f) <= DT + 1e-4f,
          "le front tombe au bout de l'aller : %.1f ms, attendu 220 (± %.1f)",
          (double)c.premier * 1000.0, (double)DT * 1000.0);

    /*
     * LA MAIN GAUCHE NE QUITTE PAS LE MANCHE, et c'est toute la différence
     * entre ce geste et la séquence complète. Le seuil n'est pas nul : les
     * épaules respirent et le tremblement de jeu ajoute quelques millimètres.
     * Il est posé bien au-dessous de ce que produirait un retour au repos, qui
     * ramènerait la gauche à plus de vingt centimètres.
     *
     * Mesuré : 0,4 cm à gauche, 26,5 à droite. Et 26,5 et non 40,8, qui est la
     * distance de la CIBLE : le poignet est amorti, il n'a donc pas fini son
     * chemin au moment où la pièce tombe. C'est voulu et c'est le sujet de tout
     * le fichier d'à côté — la machine à états donne une cible par étape, jamais
     * une trajectoire. Les 40,8 cm restent la bonne base pour dériver la
     * vitesse du geste ; ils ne sont pas ce que le poignet a parcouru.
     */
    CHECK(dg < 0.03f, "la main gauche reste sur le manche (%.1f cm)", (double)dg * 100.0);
    CHECK(dd > 0.10f, "la main droite, elle, est partie chercher la fente (%.1f cm)",
          (double)dd * 100.0);

    /* Le reste du geste, puis le retour aux commandes. 400 ms en tout, donc 48
     * pas ; on en donne quelques-uns de plus pour laisser la transition tomber. */
    const compte fin = avance(&vm, &cam, 30);
    CHECK(fin.fronts == 0, "le retour ne fait pas tomber un second jeton (%d)", fin.fronts);
    CHECK(vm.state == ROOM_VM_PLAY, "les mains sont revenues aux commandes");
    CHECK(!vm.token_visible, "et la pièce n'est plus là");
}

/* ==========================================================================
 * 5. La mesure qui a décidé du geste court
 * ========================================================================== */

/*
 * Ce test ne vérifie pas un comportement : il vérifie un ARGUMENT.
 *
 * `room_viewmodel.h` écrit noir sur blanc que la relance ne rejoue pas la
 * séquence complète parce que celle-ci dure 1,31 s, et qu'imposer plus de huit
 * dixièmes de seconde à un joueur qui a déjà appuyé sur relancer, c'est le
 * faire attendre. Ce chiffre est la raison d'être de `ROOM_VM_RELANCE` ; s'il
 * change sans que personne ne s'en aperçoive, le commentaire devient un
 * mensonge et l'état de plus, une complication gratuite.
 *
 * On le MESURE donc, en pas de simulation, plutôt que de le recopier.
 */
static void test_les_deux_durees(void)
{
    room_camera cam = make_player();
    const ns_cabinet cab = make_cabinet();

    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, DT);
    CHECK(room_viewmodel_interact(&vm, &cab), "le geste démarre");

    /* Du premier pas jusqu'à la sortie de PRESS : c'est ce que coûterait une
     * relance qui rejouerait tout. */
    int pas = 0;
    while (pas < 600
           && (vm.state == ROOM_VM_REACH || vm.state == ROOM_VM_INSERT
               || vm.state == ROOM_VM_PRESS)) {
        room_viewmodel_tick(&vm, &cam, DT);
        pas++;
    }
    const float complete = (float)pas * DT;

    room_viewmodel vm2;
    room_viewmodel_init(&vm2);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm2, &cam, DT);
    room_viewmodel_start_playing(&vm2, &cab);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm2, &cam, DT);
    CHECK(room_viewmodel_relance(&vm2), "la relance part");
    pas = 0;
    while (pas < 600 && vm2.state == ROOM_VM_RELANCE) {
        room_viewmodel_tick(&vm2, &cam, DT);
        pas++;
    }
    const float courte = (float)pas * DT;

    printf("  durées mesurées : séquence complète %.0f ms, relance %.0f ms "
           "(%.0f %% de moins)\n",
           (double)complete * 1000.0, (double)courte * 1000.0,
           100.0 * (1.0 - (double)courte / (double)complete));

    CHECK(fabsf(complete - 1.31f) <= 2.0f * DT,
          "la séquence complète dure bien 1,31 s (%.0f ms)", (double)complete * 1000.0);
    CHECK(complete > 0.80f,
          "elle dépasse les huit dixièmes de seconde qui justifient le geste court "
          "(%.0f ms)", (double)complete * 1000.0);
    CHECK(courte < 0.80f,
          "le geste court, lui, reste sous ce seuil (%.0f ms)", (double)courte * 1000.0);
}

/* ========================================================================== */

int main(void)
{
    printf("== le jeton : le geste et son instant ==\n");
    test_sequence_leve_un_seul_front();
    test_pose_figee_ne_leve_rien();
    test_cogner_ne_paie_pas();
    test_relance();
    test_les_deux_durees();

    printf("\n%d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
