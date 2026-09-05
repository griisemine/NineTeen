/*
 * test_ik.c — le solveur à deux os, et la pose des bras.
 *
 * Deux sujets, un seul binaire parce qu'ils ne se testent pas séparément : la
 * pose ne fait rien d'autre qu'appeler l'IK, et l'IK n'a d'intérêt que pour la
 * pose.
 *
 * Ce que ces tests attrapent réellement, plutôt que ce qu'ils ont l'air de
 * vérifier : un NaN. Une cible hors d'atteinte, une longueur d'os nulle, un pôle
 * colinéaire — chacun de ces trois cas passe par une division ou un `acosf` qui
 * peut sortir du domaine, et un NaN dans une matrice de pose ne produit ni
 * erreur ni message : le bras disparaît, tout simplement, et on cherche du côté
 * du rendu pendant une heure.
 *
 * Ni GPU, ni fenêtre, ni asset.
 */
#include "ns_core.h"
#include "ns_ik.h"
#include "room_camera.h"
#include "room_viewmodel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            fprintf(stderr, "ÉCHEC %s:%d — ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
        }                                                                     \
    } while (0)

#define CHECK_NEAR(got, want, tol)                                            \
    CHECK(fabsf((float)(got) - (float)(want)) <= (float)(tol),                \
          "%s = %.6f, attendu %.6f (± %.6f)", #got, (double)(got),            \
          (double)(want), (double)(tol))

static bool finite_v3(ns_v3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool finite_m4(const ns_m4 *m)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!isfinite(m->m[c][r])) return false;
    return true;
}

/* ==========================================================================
 * Le solveur
 * ========================================================================== */

static void test_reachable(void)
{
    const ns_v3 root = ns_v3_make(0.0f, 1.5f, 0.0f);
    const ns_v3 target = ns_v3_make(0.35f, 1.20f, 0.30f);
    const ns_v3 pole = ns_v3_make(0.80f, 1.00f, 0.0f);

    const ns_ik2 ik = ns_ik_two_bone(root, target, pole, 0.30f, 0.27f);

    CHECK(ik.reached, "cible à 0,55 m pour un bras de 0,57 m : elle est atteignable");
    /* Le poignet tombe sur la cible, pas « près de » : c'est la propriété qui
     * définit un solveur analytique, et elle doit tenir à 1e-4 près. */
    CHECK_NEAR(ns_v3_dist(ik.end, target), 0.0f, 1e-4f);

    /* Et les deux os gardent leur longueur — un solveur qui atteint la cible en
     * étirant l'avant-bras est un solveur qui ne sert à rien. */
    CHECK_NEAR(ns_v3_dist(ik.root, ik.joint), 0.30f, 1e-4f);
    CHECK_NEAR(ns_v3_dist(ik.joint, ik.end), 0.27f, 1e-4f);
}

static void test_out_of_reach(void)
{
    const ns_v3 root = ns_v3_make(0.0f, 1.5f, 0.0f);
    const ns_v3 target = ns_v3_make(0.0f, 1.5f, 3.0f);   /* trois mètres */
    const ns_ik2 ik = ns_ik_two_bone(root, target, ns_v3_make(0.6f, 1.0f, 0.0f),
                                     0.30f, 0.27f);

    CHECK(!ik.reached, "une cible à 3 m ne peut pas être atteinte par un bras de 0,57 m");
    CHECK(finite_v3(ik.joint) && finite_v3(ik.end), "bras tendu : aucun NaN");

    /* Le bras se tend : la distance épaule→poignet vaut la somme des os, à la
     * marge d'epsilon près que le solveur s'accorde pour éviter acosf(1,0000001). */
    CHECK_NEAR(ns_v3_dist(ik.root, ik.end), 0.57f, 1e-3f);

    /* Et il pointe bien vers la cible, sinon « tendu » ne veut rien dire. */
    const ns_v3 want = ns_v3_norm(ns_v3_sub(target, root));
    const ns_v3 got = ns_v3_norm(ns_v3_sub(ik.end, root));
    CHECK_NEAR(ns_v3_dot(want, got), 1.0f, 1e-4f);

    /* Les os gardent leur longueur, même tendus. */
    CHECK_NEAR(ns_v3_dist(ik.root, ik.joint), 0.30f, 1e-3f);
    CHECK_NEAR(ns_v3_dist(ik.joint, ik.end), 0.27f, 1e-3f);
}

static void test_too_close(void)
{
    const ns_v3 root = ns_v3_make(1.0f, 1.0f, 1.0f);

    /* Cible confondue avec l'épaule : `dir` n'existe pas, et c'est le cas qui
     * divise par zéro si on ne le traite pas. */
    const ns_ik2 zero = ns_ik_two_bone(root, root, ns_v3_make(2.0f, 0.0f, 1.0f),
                                       0.30f, 0.27f);
    CHECK(!zero.reached, "distance nulle : la chaîne ne peut pas se replier à ce point");
    CHECK(finite_v3(zero.joint) && finite_v3(zero.end), "distance nulle : aucun NaN");
    CHECK_NEAR(ns_v3_dist(zero.root, zero.joint), 0.30f, 1e-3f);
    CHECK_NEAR(ns_v3_dist(zero.joint, zero.end), 0.27f, 1e-2f);

    /* Repli maximal : |l1 − l2| = 3 cm. Toute cible plus proche est refusée de
     * la même façon. */
    const ns_ik2 near = ns_ik_two_bone(root, ns_v3_make(1.0f, 1.0f, 1.01f),
                                       ns_v3_make(2.0f, 0.0f, 1.0f), 0.30f, 0.27f);
    CHECK(!near.reached, "1 cm est en deçà du repli maximal de 3 cm");
    CHECK(finite_v3(near.end), "cible trop proche : aucun NaN");
}

static void test_pole_side(void)
{
    const ns_v3 root = ns_v3_make(0.0f, 0.0f, 0.0f);
    const ns_v3 target = ns_v3_make(0.0f, 0.0f, 0.50f);

    /* Le coude doit se trouver du côté du pôle. C'est toute la raison d'être du
     * pôle : sans lui le coude est libre sur un cercle, et il finit dans le
     * torse une fois sur deux. */
    const ns_ik2 right = ns_ik_two_bone(root, target, ns_v3_make(1.0f, 0.0f, 0.25f),
                                        0.30f, 0.27f);
    CHECK(right.joint.x > 0.05f, "coude poussé vers +X : x = %.4f", (double)right.joint.x);

    const ns_ik2 left = ns_ik_two_bone(root, target, ns_v3_make(-1.0f, 0.0f, 0.25f),
                                       0.30f, 0.27f);
    CHECK(left.joint.x < -0.05f, "coude poussé vers −X : x = %.4f", (double)left.joint.x);

    /* Le poignet, lui, ne bouge pas d'un pôle à l'autre : le pôle choisit la
     * rotation autour de l'axe, pas le point d'arrivée. */
    CHECK_NEAR(ns_v3_dist(right.end, left.end), 0.0f, 1e-4f);

    /* Pôle colinéaire à la chaîne : il ne désigne plus aucune direction. Le
     * solveur doit en inventer une plutôt que de renvoyer un coude à l'infini. */
    const ns_ik2 colinear = ns_ik_two_bone(root, target, ns_v3_make(0.0f, 0.0f, 5.0f),
                                           0.30f, 0.27f);
    CHECK(finite_v3(colinear.joint), "pôle colinéaire : aucun NaN");
    CHECK_NEAR(ns_v3_dist(colinear.root, colinear.joint), 0.30f, 1e-4f);
}

static void test_degenerate_bones(void)
{
    const ns_v3 root = ns_v3_make(0.0f, 1.0f, 0.0f);
    const ns_v3 target = ns_v3_make(0.0f, 1.0f, 0.4f);
    const ns_v3 pole = ns_v3_make(1.0f, 0.5f, 0.2f);

    /* Une longueur nulle n'est pas une erreur fatale — un moteur qui s'arrête
     * parce qu'une animation est mal décrite est pire que le membre fautif —
     * mais elle ne doit produire ni NaN ni chaîne infinie. */
    const ns_ik2 a = ns_ik_two_bone(root, target, pole, 0.0f, 0.27f);
    CHECK(!a.reached && finite_v3(a.joint) && finite_v3(a.end),
          "humérus de longueur nulle : chaîne finie, reached = false");

    const ns_ik2 b = ns_ik_two_bone(root, target, pole, 0.30f, -1.0f);
    CHECK(!b.reached && finite_v3(b.joint) && finite_v3(b.end),
          "longueur négative : chaîne finie, reached = false");

    /* Et tout est resté à l'épaule, plutôt qu'à un endroit arbitraire. */
    CHECK_NEAR(ns_v3_dist(a.root, a.end), 0.0f, 1e-6f);
}

/* ==========================================================================
 * La pose
 * ========================================================================== */

/* La constante de foulée est écrite dans les deux fichiers — la caméra et le
 * viewmodel — parce que le viewmodel n'a aucune raison d'inclure le .c de la
 * caméra pour une valeur. La duplication est donc assumée, mais elle doit être
 * vérifiée : si elle dérive, les bras balancent à contretemps des pas, ce qui
 * est le genre de défaut qu'on voit sans savoir le nommer. */
#define TEST_STRIDE 1.55f

static room_camera make_player(void)
{
    room_camera c;
    room_camera_init(&c, ns_v3_make(0.0f, 1.70f, 0.0f), 0.0f);
    c.mode = ROOM_CAM_PLAYER;
    return c;
}

static void test_pose_finite(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);

    ns_viewmodel_pose pose;
    /* Avant le premier tick, rien n'est dessiné : une pose non initialisée ne
     * doit pas produire de bras à l'origine du monde. */
    room_viewmodel_pose(&vm, &cam, 0.5f, &pose);
    for (int s = 0; s < NS_VM_SEGMENT_COUNT; ++s) {
        CHECK(!pose.draw[s], "segment %d ne doit pas être dessiné avant le premier pas", s);
    }

    for (int i = 0; i < 240; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    room_viewmodel_pose(&vm, &cam, 0.5f, &pose);

    for (int s = 0; s < NS_VM_HAND_R + 1; ++s) {
        CHECK(pose.draw[s], "segment %d dessiné au repos", s);
        CHECK(finite_m4(&pose.segment[s]), "segment %d : matrice finie", s);
        CHECK(pose.length[s] > 0.0f, "segment %d : longueur positive", s);
    }
    CHECK(!pose.draw[NS_VM_TOKEN], "pas de jeton en main au repos");
    CHECK_NEAR(pose.fov_y_degrees, 58.0f, 1e-3f);

    /*
     * Les mains sont devant le joueur, sous les yeux, et de part et d'autre.
     *
     * ATTENTION à la convention : `forward_from_angles` (room_camera.c) rend
     * `(cos lacet, sin tangage, sin lacet)`, donc **un lacet nul regarde vers
     * +X**, pas vers −Z. Le « droite » de la caméra vaut alors
     * `cross(+X, +Y) = +Z`. « Devant » se lit donc en x et « à droite » en z —
     * c'est l'inverse de ce qu'on écrit d'instinct, et c'est exactement le genre
     * de détail qui fait écrire un test qui vérifie le contraire de ce qu'il
     * annonce.
     */
    const ns_m4 *hl = &pose.segment[NS_VM_HAND_L];
    const ns_m4 *hr = &pose.segment[NS_VM_HAND_R];
    CHECK(hl->m[3][1] < 1.70f && hr->m[3][1] < 1.70f, "les mains sont sous les yeux");
    CHECK(hl->m[3][0] > 0.0f && hr->m[3][0] > 0.0f,
          "les mains sont devant : x = %.3f et %.3f",
          (double)hl->m[3][0], (double)hr->m[3][0]);
    CHECK(hl->m[3][2] < 0.0f, "main gauche à gauche : z = %.3f", (double)hl->m[3][2]);
    CHECK(hr->m[3][2] > 0.0f, "main droite à droite : z = %.3f", (double)hr->m[3][2]);

    /* Et les mains sont à portée : un bras de 67 cm partant d'une épaule à
     * 22,5 cm sous l'œil ne peut pas poser sa main à plus de 90 cm de l'œil. */
    const ns_v3 eye = ns_v3_make(0.0f, 1.70f, 0.0f);
    CHECK(ns_v3_dist(ns_v3_make(hr->m[3][0], hr->m[3][1], hr->m[3][2]), eye) < 0.90f,
          "main droite à portée de l'épaule");
}

static void test_walk_swing_follows_distance(void)
{
    /*
     * Le contre-balancement suit `bob.distance`, pas le temps. La vérification
     * est directe : on impose deux distances séparées d'une demi-foulée et on
     * exige que la main droite ait changé de côté. À la foulée entière, elle
     * doit être revenue.
     */
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);

    /*
     * On échantillonne au QUART et aux trois quarts de foulée, pas à la demie :
     * la phase est un sinus, et sin(0) vaut sin(π). Une demi-foulée redonne donc
     * exactement la même position, et le test qui la comparait à zéro ne
     * mesurait rien — il tombait, ce qui est la seule bonne surprise dans
     * l'histoire.
     */
    cam.bob.amount = cam.prev_bob.amount = 1.0f;
    cam.bob.distance = cam.prev_bob.distance = TEST_STRIDE * 0.25f;
    for (int i = 0; i < 400; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    const float z_quarter = vm.wrist_r.z;

    cam.bob.distance = cam.prev_bob.distance = TEST_STRIDE * 0.75f;
    for (int i = 0; i < 400; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    const float z_three = vm.wrist_r.z;

    cam.bob.distance = cam.prev_bob.distance = TEST_STRIDE * 1.25f;
    for (int i = 0; i < 400; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    const float z_next = vm.wrist_r.z;

    /* Une foulée plus loin, la phase a fait un tour complet : même position. Si
     * la constante de foulée dérivait entre la caméra et le viewmodel, ce test
     * tomberait — et c'est exactement ce qu'on lui demande. */
    CHECK_NEAR(z_next, z_quarter, 2e-3f);
    CHECK(fabsf(z_three - z_quarter) > 0.10f,
          "d'un quart aux trois quarts, le balancement doit s'inverser : "
          "%.4f contre %.4f", (double)z_three, (double)z_quarter);

    /*
     * Et les deux bras balancent en OPPOSITION. La mesure ne demande aucune
     * fonction nouvelle : on relève d'abord le repos en imposant une amplitude
     * nulle, puis l'écart à ce repos au quart de foulée, là où le sinus est
     * maximal. Si les bras balançaient ensemble, la somme des deux écarts
     * doublerait au lieu de s'annuler.
     */
    room_viewmodel rest;
    room_viewmodel_init(&rest);
    cam.bob.amount = cam.prev_bob.amount = 0.0f;
    for (int i = 0; i < 400; ++i) room_viewmodel_tick(&rest, &cam, 1.0f / 120.0f);
    const float rest_r = rest.wrist_r.z, rest_l = rest.wrist_l.z;

    room_viewmodel quarter;
    room_viewmodel_init(&quarter);
    cam.bob.amount = cam.prev_bob.amount = 1.0f;
    cam.bob.distance = cam.prev_bob.distance = TEST_STRIDE * 0.25f;
    for (int i = 0; i < 400; ++i) room_viewmodel_tick(&quarter, &cam, 1.0f / 120.0f);

    const float dr = quarter.wrist_r.z - rest_r;
    const float dl = quarter.wrist_l.z - rest_l;
    CHECK(fabsf(dr) > 0.05f, "au quart de foulée le bras droit est franchement sorti du repos "
                             "(%.4f)", (double)dr);
    CHECK(fabsf(dr + dl) < 0.02f,
          "opposition exacte : %.4f et %.4f devraient s'annuler", (double)dr, (double)dl);
}

/*
 * Le bout du doigt d'un segment de main, demandé AU MAILLAGE.
 *
 * Il se calculait ici : « l'origine moins la troisième colonne, mise à
 * l'échelle par la longueur », c'est-à-dire le poignet prolongé d'une longueur
 * de main le long de l'axe. C'était exact tant que la main était plate et ses
 * doigts droits. Elle est maintenant modélisée fléchie, et le bout du majeur
 * est à six centimètres et demi côté paume de cet axe : la formule mesurait un
 * point de l'espace où il n'y a plus de doigt.
 *
 * Un test qui mesure au mauvais endroit est pire qu'un test absent — il a
 * échoué ici en accusant l'IK, dont ce n'était pas la faute. On demande donc au
 * moteur où est son bout de doigt, ce qui laisse au test le seul rôle qu'il
 * doit avoir : vérifier que la pose l'amène sur la commande.
 */
static ns_v3 on_hand(const ns_viewmodel_pose *pose, int hand, ns_v3 p)
{
    const ns_m4 *h = &pose->segment[hand];
    return ns_v3_make(
        h->m[0][0] * p.x + h->m[1][0] * p.y + h->m[2][0] * p.z + h->m[3][0],
        h->m[0][1] * p.x + h->m[1][1] * p.y + h->m[2][1] * p.z + h->m[3][1],
        h->m[0][2] * p.x + h->m[1][2] * p.y + h->m[2][2] * p.z + h->m[3][2]);
}

static ns_v3 fingertip(const ns_viewmodel_pose *pose, int hand)
{
    return on_hand(pose, hand, ns_viewmodel_fingertip(hand == NS_VM_HAND_R));
}

static void test_sequence(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);

    /*
     * Une borne face au joueur, centrée à 1,01 m de lui — la distance à laquelle
     * la capsule de collision (rayon 0,32 m) l'arrête contre un panneau de
     * commande qui déborde de 0,69 m. Ce n'est PAS `player_anchor`, qui est à
     * 1,085 m : l'ancre dit où le joueur devrait se mettre, la capsule dit où il
     * finit réellement, et c'est la seconde qui décide si le bras atteint.
     *
     * Lacet nul regarde vers +X (voir test_pose_finite), donc la borne est en
     * +X et sa normale d'écran pointe vers le joueur, en −X. Les cotes locales
     * viennent de `build_cabinet` : fente à y 0,65 et 0,452 m devant le centre,
     * boutons à y 0,968 et 0,5225 m devant.
     */
    ns_cabinet cab;
    memset(&cab, 0, sizeof cab);
    SDL_strlcpy(cab.name, "borne_test", sizeof cab.name);
    SDL_strlcpy(cab.game, "envol", sizeof cab.game);
    cab.screen_normal = ns_v3_make(-1.0f, 0.0f, 0.0f);
    cab.coin_slot    = ns_v3_make(1.01f - 0.452f,  0.650f, 0.0f);
    cab.panel_centre = ns_v3_make(1.01f - 0.5225f, 0.968f, 0.0575f);

    CHECK(room_viewmodel_interact(&vm, &cab), "le geste démarre");
    CHECK(vm.state == ROOM_VM_REACH, "premier état : tendre le bras");
    CHECK(vm.token_visible, "le jeton est en main dès le départ");

    /* Un second appui ne relance pas le geste au milieu. */
    CHECK(!room_viewmodel_interact(&vm, &cab), "un geste à la fois");

    /* Traversée complète de la séquence. On échantillonne chaque état au
     * passage, et on exige surtout qu'aucune matrice ne devienne NaN — c'est le
     * seul défaut de cette machine qui ne se voit pas dans un journal. */
    bool seen[ROOM_VM_STATE_COUNT];
    memset(seen, 0, sizeof seen);
    float closest_coin = 1e9f, closest_panel = 1e9f;
    float max_press = 0.0f;

    for (int i = 0; i < 400; ++i) {
        room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
        seen[vm.state] = true;
        max_press = ns_maxf(max_press, vm.press_depth);

        ns_viewmodel_pose pose;
        room_viewmodel_pose(&vm, &cam, 1.0f, &pose);
        for (int s = 0; s < NS_VM_SEGMENT_COUNT; ++s) {
            CHECK(finite_m4(&pose.segment[s]), "pas %d, segment %d : matrice finie", i, s);
        }

        /*
         * On mesure le BOUT DU DOIGT, pas le poignet : mesurer au poignet
         * reviendrait à exiger que le poignet touche le bouton, c'est-à-dire à
         * demander une pose fausse. Et on le demande au maillage plutôt que de
         * le recalculer — voir `fingertip()`.
         */
        const ns_v3 tip = fingertip(&pose, NS_VM_HAND_R);
        if (vm.state == ROOM_VM_INSERT) {
            closest_coin = ns_minf(closest_coin, ns_v3_dist(tip, cab.coin_slot));
        }
        if (vm.state == ROOM_VM_PRESS) {
            closest_panel = ns_minf(closest_panel, ns_v3_dist(tip, cab.panel_centre));
        }
        if (vm.state == ROOM_VM_IDLE && seen[ROOM_VM_RETURN]) break;
    }

    CHECK(seen[ROOM_VM_INSERT], "l'insertion a eu lieu");
    CHECK(seen[ROOM_VM_PRESS], "l'appui a eu lieu");
    CHECK(seen[ROOM_VM_RETURN], "le retour a eu lieu");
    CHECK(vm.state == ROOM_VM_IDLE, "on revient au repos");
    CHECK(!vm.has_target, "la cible est relâchée à la fin");
    CHECK(!vm.token_visible, "le jeton a disparu dans la fente");
    CHECK(max_press > 0.5f, "le bouton a été enfoncé (%.2f)", (double)max_press);

    /*
     * Le doigt TOUCHE ses deux cibles, à 3 cm près. Le seuil est serré exprès :
     * il était à 12 cm quand la pose visait avec le poignet, et 12 cm est
     * précisément la distance à laquelle un jeton flotte visiblement devant la
     * fente. C'est l'itération correctrice de `pose_arm` qui l'a ramené à 1,4 cm,
     * et un seuil large ici laisserait la régression repasser sans bruit.
     *
     * Le reliquat vient du bras qui bute sur sa portée : le joueur est arrêté par
     * sa capsule à 1,01 m du meuble, et à cette distance il est tendu.
     */
    printf("  geste : doigt à %.3f m de la fente, %.3f m du bouton\n",
           (double)closest_coin, (double)closest_panel);
    CHECK(closest_coin < 0.03f, "le doigt atteint la fente (%.3f m)", (double)closest_coin);
    CHECK(closest_panel < 0.03f, "le doigt atteint le bouton (%.3f m)", (double)closest_panel);
}

/*
 * Les deux mains sur les commandes, pendant la partie.
 *
 * Ce que ce test attrape, et pourquoi il ne pouvait pas être écrit avant : la
 * portée d'un bras (57 cm), la distance à laquelle la capsule arrête le joueur
 * (1,01 m du meuble) et la position des commandes sur le panneau incliné sont
 * trois cotes réglées dans trois fichiers différents — `room_viewmodel.c`,
 * `ns_bvh.c`, `roomgen.c`. Tant que les bras pendaient le long du corps, elles
 * pouvaient diverger sans que rien ne le dise. Depuis qu'on JOUE sur la borne,
 * une divergence de quatre centimètres suffit à laisser les deux mains tendues
 * juste au-dessus des commandes sans jamais les toucher — c'est exactement ce
 * qui est arrivé, et c'est ce que ce test verrouille.
 */
static void test_play_hands_on_controls(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);

    /* La même borne que `test_sequence`, plus le manche. Les trois ancres
     * désignent une SURFACE : la fente, le dessus des pastilles, le sommet de la
     * boule — 10,2 cm au-dessus de la tôle, cotes de `build_cabinet`. C'est la
     * pose qui ajoute l'épaisseur de la main, et pour le manche l'épaisseur de
     * la boule : on ne touche pas une boule, on la tient, et la tenir se juge
     * sur son centre. */
    ns_cabinet cab;
    memset(&cab, 0, sizeof cab);
    SDL_strlcpy(cab.name, "borne_test", sizeof cab.name);
    SDL_strlcpy(cab.game, "envol", sizeof cab.game);
    cab.screen_normal = ns_v3_make(-1.0f, 0.0f, 0.0f);
    cab.coin_slot    = ns_v3_make(1.01f - 0.452f,  0.650f,  0.0f);
    cab.panel_centre = ns_v3_make(1.01f - 0.5225f, 0.996f,  0.0575f);
    /* Lacet nul regarde vers +X, donc « à gauche du joueur » est −Z. */
    cab.stick_top    = ns_v3_make(1.01f - 0.4400f, 1.072f, -0.2000f);

    room_viewmodel_start_playing(&vm, &cab);
    CHECK(room_viewmodel_is_playing(&vm), "on est en jeu");
    CHECK(!vm.token_visible, "le jeton n'est plus en main : il est dans la machine");

    /*
     * LE CENTRE DE LA BOULE, ET POURQUOI ON NE MESURE PLUS SUR SON SOMMET.
     *
     * L'ancre est le sommet ; la boule est en dessous, d'un rayon. Une prise se
     * juge sur le centre parce que c'est le seul point dont on puisse dire s'il
     * est DEDANS ou DEHORS.
     */
    const ns_v3 boule = ns_v3_sub(cab.stick_top,
                                  ns_v3_make(0.0f, ROOM_VM_BALL_R, 0.0f));

    /* Deux secondes : le penchement est amorti, il lui faut le temps d'arriver. */
    float best_r = 1e9f, max_press = 0.0f;
    ns_viewmodel_pose pose;
    for (int i = 0; i < 240; ++i) {
        room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
        if (i == 150) room_viewmodel_tap(&vm);       /* un battement d'aile */
        max_press = ns_maxf(max_press, vm.press_depth);

        room_viewmodel_pose(&vm, &cam, 1.0f, &pose);
        for (int s = 0; s < NS_VM_SEGMENT_COUNT; ++s) {
            CHECK(finite_m4(&pose.segment[s]), "pas %d, segment %d : matrice finie", i, s);
        }
        CHECK(room_viewmodel_is_playing(&vm), "on reste en jeu tant qu'on ne l'arrête pas");

        best_r = ns_minf(best_r, ns_v3_dist(fingertip(&pose, NS_VM_HAND_R), cab.panel_centre));
    }

    /*
     * LA PRISE, JUGÉE SUR UNE SECONDE DE JEU ÉTABLI, ET AU PIRE INSTANT.
     *
     * Deux pièges évités ici, et le second m'a coûté une version du test.
     *
     * Un minimum pris sur toute la montée dirait « à un moment la paume est
     * passée près de la boule » — ce qu'une main qui la traverse en chemin
     * satisfait aussi bien qu'une main qui s'y pose. On ne mesure donc qu'après
     * les deux secondes d'amortissement.
     *
     * Et une seule image ne suffit pas : les mains en partie portent un
     * tremblement de ±3,5 mm à 11 rad/s, soit une période de 571 ms. Juger sur
     * la dernière image, c'est juger sur une phase tirée au sort, et un seuil
     * réglé dessus se met à dépendre du nombre d'images du test. Cent vingt
     * images couvrent une période entière ; on garde de chacune la valeur la
     * plus défavorable.
     */
    float paume_l = 0.0f, dedans_l = -1.0f, doigt_l = 1e9f, sommet_l = 1e9f;
    for (int i = 0; i < 120; ++i) {
        room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
        room_viewmodel_pose(&vm, &cam, 1.0f, &pose);

        const ns_v3 paume  = on_hand(&pose, NS_VM_HAND_L, ns_viewmodel_palm());
        const ns_v3 majeur = fingertip(&pose, NS_VM_HAND_L);
        const float pd = ns_v3_dist(paume, boule);
        const float td = ns_v3_dist(majeur, boule);
        paume_l = ns_maxf(paume_l, pd);
        doigt_l = ns_minf(doigt_l, td);
        /* Mesurée et imprimée sans être vérifiée : c'est la grandeur que
         * l'ancien critère bornait à 3 cm, et la voir ici dit d'un coup d'œil
         * pourquoi il ne pouvait pas passer sur une main qui tient. */
        sommet_l = ns_minf(sommet_l, ns_v3_dist(majeur, cab.stick_top));
        /*
         * Le COSINUS entre « centre -> paume » et « centre -> bout du majeur ».
         * Négatif : les deux sont de part et d'autre du centre, donc le doigt a
         * fait le tour de la boule. Positif : ils sont du même côté, et la main
         * est posée À CÔTÉ. C'est toute la différence entre tenir et toucher, et
         * un scalaire suffit à la dire — sans repère de main, sans axe, sans
         * convention d'orientation à tenir d'accord avec le maillage.
         */
        if (pd > 1e-6f && td > 1e-6f) {
            dedans_l = ns_maxf(dedans_l,
                               ns_v3_dot(ns_v3_sub(paume, boule),
                                         ns_v3_sub(majeur, boule)) / (pd * td));
        }
    }

    printf("  jeu : paume gauche à %.3f m du centre de la boule (rayon %.3f), "
           "majeur à %.3f m, cosinus %+.2f ; majeur à %.3f m du SOMMET, que "
           "l'ancien critère bornait à 0.030 ; doigt droit à %.3f m des boutons\n",
           (double)paume_l, (double)ROOM_VM_BALL_R, (double)doigt_l,
           (double)dedans_l, (double)sommet_l, (double)best_r);

    /*
     * CE QUE CE CRITÈRE REMPLACE, ET POURQUOI L'ANCIEN NE POUVAIT PAS PASSER.
     *
     * Il exigeait « bout du majeur à moins de 3 cm du SOMMET de la boule », et
     * il l'exigeait sur la seule main qui ne touche pas sa commande mais la
     * TIENT. Aucune prise ne satisfait ça, et ce n'est pas un réglage : la main
     * est modélisée fléchie, 5,6 cm séparent le creux de la paume du bout du
     * majeur, et une paume posée sur une boule de 42 mm laisse donc ce bout à
     * 5,1 cm du sommet — c'est la valeur que la ligne ci-dessus imprime, et
     * elle vaut 1,7 fois le seuil qu'on exigeait. Le seul moyen de le tenir
     * était d'écarter la paume et de tendre le majeur jusqu'à la boule,
     * c'est-à-dire de tâter l'objet au lieu de l'empoigner : la capture
     * montrait la boule DEHORS, effleurée du bout du doigt, le poing refermé à
     * côté.
     *
     * C'est mot pour mot le reproche que ce fichier s'adresse vingt lignes plus
     * haut, à propos du bout du doigt et de l'axe du poignet : un test qui
     * mesure au mauvais endroit est pire qu'un test absent, parce qu'il tient
     * la faute en place.
     *
     * Ce qu'on mesure maintenant est ce qu'on voulait dire : la boule est-elle
     * dans le poing. Trois faits, aucun réglable :
     *
     *   1. LA PAUME EST POSÉE DESSUS. Le creux à un rayon du centre, à 1 cm
     *      près — au-delà, la main flotte au-dessus.
     *   2. LE MAJEUR A FAIT LE TOUR. Cosinus négatif : le bout du doigt est de
     *      l'autre côté du centre que la paume. Une main posée à côté donne un
     *      cosinus franchement positif ; c'est le fait qui distingue les deux
     *      poses et il n'a pas de valeur intermédiaire plausible.
     *   3. LE DOIGT NE TRAVERSE PAS LA BOULE. Au moins un rayon du centre. Un
     *      critère qui ne dirait que 1 et 2 serait tenu par une main qui
     *      broie la boule.
     *
     * VÉRIFIÉ EN REMETTANT L'ANCIENNE POSE, parce qu'un critère qui passe avant
     * comme après ne prouve rien. Bout du majeur visant le sommet de la boule,
     * ce test tombe sur 1 et sur 2 : paume à 86 mm du centre pour 21 de rayon,
     * cosinus +0,84 — la main est franchement À CÔTÉ. Avec la paume qui vise,
     * 25 mm et −0,48. Le 3 passe dans les deux cas, et c'est normal : c'est un
     * garde-fou, pas le discriminant.
     */
    CHECK(paume_l < ROOM_VM_BALL_R + 0.010f,
          "la paume gauche est posée sur la boule (%.3f m du centre pour %.3f de rayon)",
          (double)paume_l, (double)ROOM_VM_BALL_R);
    CHECK(dedans_l < 0.0f,
          "le majeur gauche s'est refermé de l'autre côté de la boule (cosinus %.2f)",
          (double)dedans_l);
    CHECK(doigt_l >= ROOM_VM_BALL_R,
          "et il ne la traverse pas (%.3f m du centre pour %.3f de rayon)",
          (double)doigt_l, (double)ROOM_VM_BALL_R);
    CHECK(best_r < 0.03f, "la main droite couvre les boutons (%.3f m)", (double)best_r);
    CHECK(max_press > 0.4f, "le battement enfonce l'index (%.2f)", (double)max_press);

    /* Le doigt REMONTE : sinon le battement suivant ne se verrait pas. */
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    CHECK(vm.press_depth < 0.05f, "et il remonte entre deux battements (%.2f)",
          (double)vm.press_depth);

    room_viewmodel_stop_playing(&vm);
    CHECK(!room_viewmodel_is_playing(&vm), "on quitte le jeu");
    for (int i = 0; i < 240; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    CHECK(vm.state == ROOM_VM_IDLE, "et les mains reviennent au repos");
}

static void test_forced_pose(void)
{
    room_viewmodel vm;
    room_viewmodel_init(&vm);

    CHECK(room_viewmodel_set_forced_pose(&vm, "reach"), "--pose=reach accepté");
    CHECK(room_viewmodel_set_forced_pose(&vm, "INSERT"), "la casse est ignorée");
    CHECK(room_viewmodel_set_forced_pose(&vm, "marche"), "les noms français aussi");
    CHECK(!room_viewmodel_set_forced_pose(&vm, "marchee"), "une faute de frappe est refusée");
    CHECK(!room_viewmodel_set_forced_pose(&vm, ""), "une chaîne vide est refusée");
    CHECK(!room_viewmodel_set_forced_pose(&vm, NULL), "NULL est refusé");

    /* Une pose forcée tient : elle ne doit pas retomber au repos après sa durée,
     * sinon une capture prise à la huitième image montrerait autre chose. */
    room_camera cam = make_player();
    CHECK(room_viewmodel_set_forced_pose(&vm, "press"), "--pose=press accepté");
    for (int i = 0; i < 600; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    CHECK(vm.state == ROOM_VM_PRESS, "la pose forcée tient dans le temps");
}

/* ==========================================================================
 * Le coup de poing sur une borne
 * ==========================================================================
 *
 * Ce qu'on défend ici tient en deux phrases, et la seconde a coûté 65 cm.
 *
 *   1. LE GESTE EXISTE ET REVIENT. Le poignet part loin devant, puis retrouve
 *      sa place ; le retour est plus lent que l'aller, c'est ce qui fait qu'un
 *      coup se lit comme un coup.
 *   2. L'ÉPAULE NE DÉRIVE PAS. La poussée d'épaule a d'abord été écrite dans
 *      `vm->lean`, APRÈS son amortissement — donc elle s'intégrait d'un pas
 *      sur l'autre. À 9 par seconde et 120 Hz, un pas ne mange que 7,2 % de
 *      l'excès : 7,5 cm de poussée devenaient 65 cm d'épaule projetée en
 *      avant, et le bras sortait du cadre.
 *
 * Le second défaut ne fait rien planter, ne produit aucun message, et ne se
 * voit sur aucune capture fixe — seulement en frappant, et seulement si l'on
 * regarde. C'est exactement le genre de chose qu'un test tient et qu'un œil
 * laisse passer.
 */
static void test_frappe(void)
{
    room_camera cam = make_player();
    room_viewmodel vm;
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);

    ns_viewmodel_pose pose;
    room_viewmodel_pose(&vm, &cam, 1.0f, &pose);
    const ns_v3 epaule_repos = ns_v3_make(pose.segment[NS_VM_SLEEVE_R].m[3][0],
                                          pose.segment[NS_VM_SLEEVE_R].m[3][1],
                                          pose.segment[NS_VM_SLEEVE_R].m[3][2]);
    const ns_v3 poignet_repos = ns_v3_make(pose.segment[NS_VM_HAND_R].m[3][0],
                                           pose.segment[NS_VM_HAND_R].m[3][1],
                                           pose.segment[NS_VM_HAND_R].m[3][2]);

    CHECK(room_viewmodel_frappe(&vm, NULL), "le coup part, même sans borne");
    CHECK(vm.state == ROOM_VM_HIT, "l'état est bien celui du coup");
    CHECK(!room_viewmodel_frappe(&vm, NULL), "un coup à la fois");
    CHECK(room_viewmodel_is_hitting(&vm), "le geste est en cours");

    /*
     * On déroule les 390 ms en suivant DEUX choses à chaque pas : jusqu'où le
     * poignet va, et jusqu'où l'épaule dérive. `make_player` regarde vers +X,
     * donc « devant » est +X.
     */
    float poignet_max = poignet_repos.x;
    float poignet_min = poignet_repos.x;
    float epaule_max  = epaule_repos.x;
    int   impacts = 0;
    float t_impact = -1.0f;
    const int pas = (int)(0.500f * 120.0f) + 2;
    for (int i = 0; i < pas; ++i) {
        room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
        if (room_viewmodel_take_impact(&vm, NULL, NULL)) {
            ++impacts;
            if (t_impact < 0.0f) t_impact = (float)(i + 1) / 120.0f;
        }
        room_viewmodel_pose(&vm, &cam, 1.0f, &pose);
        const float px = pose.segment[NS_VM_HAND_R].m[3][0];
        const float ex = pose.segment[NS_VM_SLEEVE_R].m[3][0];
        if (px > poignet_max) poignet_max = px;
        if (px < poignet_min) poignet_min = px;
        if (ex > epaule_max)  epaule_max  = ex;
    }
    printf("  frappe : poignet de %.3f a %.3f m (repos %.3f), epaule +%.3f m\n",
           (double)poignet_min, (double)poignet_max, (double)poignet_repos.x,
           (double)(epaule_max - epaule_repos.x));

    /*
     * L'IMPACT TOMBE UNE FOIS, ET À 90 ms. Onze pas de simulation remplissent
     * la condition « on a dépassé la fin de l'aller » ; testée comme un seuil
     * plutôt que comme un front, elle ferait partir le son onze fois.
     */
    CHECK(impacts == 1, "l'impact est un FRONT, pas un seuil (%d fois)", impacts);
    CHECK(t_impact > 0.190f && t_impact < 0.212f,
          "l'impact tombe à la fin de l'aller (%.3f s, attendu 0,200)",
          (double)t_impact);

    /*
     * LE POING RECULE PUIS PART, et c'est la COURSE TOTALE qui compte, pas le
     * gain sur la position de repos.
     *
     * C'est le contrôle qui a fait ajouter la phase d'armé. La position de
     * repos porte déjà les mains en avant — 44 cm de l'œil pour 59 de portée —
     * donc un coup lancé de là ne gagnait que CINQ MILLIMÈTRES au poignet : le
     * geste existait dans le code et ne se voyait pas. Avec l'armé, le poing
     * recule d'abord d'une vingtaine de centimètres, ce qui lui laisse de quoi
     * partir.
     */
    CHECK(poignet_repos.x - poignet_min > 0.30f,
          "le poing RECULE d'abord : %.3f m d'armé (mesuré 0,340)",
          (double)(poignet_repos.x - poignet_min));
    CHECK(poignet_max - poignet_min > 0.40f,
          "et la course totale du poing est franche : %.3f m (mesuré 0,450)",
          (double)(poignet_max - poignet_min));

    /*
     * ET IL DÉPASSE LES MAINS QU'ON PORTE DÉJÀ EN AVANT. C'est l'assertion qui
     * attrape le défaut le plus retors des deux qu'on a trouvés ici : la fin de
     * l'aller tombe ENTRE deux pas de simulation, donc le dernier échantillon
     * de la phase valait 82 % de la course. Le poing culminait trois
     * centimètres devant la position de repos — un coup qui n'arrive pas à
     * destination, et que rien ne signale. Il en gagne maintenant onze.
     */
    CHECK(poignet_max > poignet_repos.x + 0.08f,
          "le poing dépasse franchement la position de repos : %.3f m gagnés "
          "(mesuré 0,110)", (double)(poignet_max - poignet_repos.x));

    /*
     * L'ÉPAULE SUIT, MAIS NE PART PAS. La poussée vaut 7,5 cm ; on laisse deux
     * fois la marge pour l'interpolation et le lissage, et on refuse tout ce
     * qui ressemble à une intégration. C'est CE contrôle qui attrape le défaut
     * de l'accumulation, et la borne est très en dessous des 65 cm mesurés.
     */
    const float derive = epaule_max - epaule_repos.x;
    CHECK(derive < 0.16f,
          "l'épaule ne DÉRIVE pas : %.3f m devant sa place (poussée 0,075)",
          (double)derive);
    CHECK(derive > 0.005f,
          "l'épaule entre quand même dans le coup : %.3f m", (double)derive);

    /* LE GESTE SE TERMINE et rend la main. */
    room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    CHECK(!room_viewmodel_is_hitting(&vm), "le coup est fini après 390 ms");
    CHECK(vm.state == ROOM_VM_IDLE, "et les mains sont rendues au repos");

    /* Tout revient : 400 pas plus tard, l'épaule et le poignet ont retrouvé
     * leur place. Une dérive résiduelle se verrait ici même si elle passait
     * sous la borne pendant le geste. */
    for (int i = 0; i < 400; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    room_viewmodel_pose(&vm, &cam, 1.0f, &pose);
    CHECK_NEAR(pose.segment[NS_VM_SLEEVE_R].m[3][0], epaule_repos.x, 0.002f);
    CHECK_NEAR(pose.segment[NS_VM_HAND_R].m[3][0], poignet_repos.x, 0.010f);

    /*
     * L'ALLER EST PLUS COURT QUE LE RETOUR, et c'est ce qui distingue un coup
     * d'un bras qu'on agite. On le lit sur l'avancement lui-même : à 90 ms il
     * vaut 1, et à 90 + 150 ms — la moitié du retour — il doit encore valoir
     * plus de trois dixièmes. Un geste symétrique serait déjà retombé sous un
     * dixième au même instant.
     */
    room_viewmodel_init(&vm);
    for (int i = 0; i < 120; ++i) room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    room_viewmodel_frappe(&vm, NULL);
    for (int i = 0; i < (int)(0.200f * 120.0f) + 1; ++i) {
        room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    }
    const float a_impact = room_viewmodel_frappe_amount(&vm, 1.0f);
    CHECK(a_impact > 0.95f, "le geste est à fond à l'impact (%.3f)",
          (double)a_impact);
    for (int i = 0; i < (int)(0.150f * 120.0f); ++i) {
        room_viewmodel_tick(&vm, &cam, 1.0f / 120.0f);
    }
    const float a_mi_retour = room_viewmodel_frappe_amount(&vm, 1.0f);
    CHECK(a_mi_retour > 0.30f,
          "le retour est LENT : encore %.3f à mi-chemin (un geste symétrique "
          "serait sous 0,10)", (double)a_mi_retour);

    /* Et le choc décroît au lieu de rester. */
    CHECK(room_viewmodel_choc(&vm, 1.0f) < 0.30f,
          "le choc de la dalle s'est calmé (%.3f)",
          (double)room_viewmodel_choc(&vm, 1.0f));
}

int main(void)
{
    test_reachable();
    test_out_of_reach();
    test_too_close();
    test_pole_side();
    test_degenerate_bones();
    test_pose_finite();
    test_walk_swing_follows_distance();
    test_sequence();
    test_play_hands_on_controls();
    test_forced_pose();
    test_frappe();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
