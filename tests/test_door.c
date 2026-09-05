/*
 * test_door.c — la machine à états de la porte des toilettes, et le dégagement
 * de la capsule contre un vantail qui bouge.
 *
 * Ce que ce test attrape réellement
 * ---------------------------------
 * Une porte automatique rate de façons qui ne provoquent aucune erreur et qu'on
 * ne voit qu'en jouant, souvent longtemps après :
 *
 *   1. Elle ne se REFERME jamais, parce que le compte à rebours est réarmé par
 *      une condition qui reste vraie — le joueur est parti mais la zone est trop
 *      large, ou le maintien est remis à jour dans le mauvais état.
 *   2. Elle se referme SUR le joueur qui revient sur ses pas, parce que la
 *      ré-ouverture pendant la fermeture a été oubliée. C'est le cas que 2020
 *      traitait explicitement (« RE OUVRIR SI PASSAGE DEVANT LA PORTE DURANT LA
 *      FERMETURE ») et le premier qu'une réécriture perd.
 *   3. Elle repart de ZÉRO en se rouvrant au lieu de repartir d'où elle en est,
 *      ce qui donne un saut visible d'un quart de mètre.
 *   4. Elle bat au visage de quelqu'un qui reste devant, parce que le maintien
 *      s'écoule même en présence.
 *   5. Le vantail fermé se laisse TRAVERSER à la course, parce que le
 *      dégagement choisit l'axe de moindre pénétration et éjecte de l'autre
 *      côté d'un panneau de 4,5 cm. Une porte qu'on traverse est un décor.
 *
 * Aucun de ces cinq ne demande de GPU, de fenêtre ni de fichier : la machine à
 * états de `room_door.c` est du calcul pur, et c'est pour ça qu'elle en est du
 * calcul pur.
 *
 * Le point 5 se vérifie ici sur `room_camera_tick` avec un BVH NUL — la caméra
 * retombe alors sur un déplacement libre, ce qui isole exactement le dégagement
 * des obstacles hors BVH sans qu'un sol ou un mur cuit ne s'en mêle.
 */
#include "ns_core.h"
#include "room_door.h"

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

/* Le pas de simulation du jeu. On teste à la cadence réelle, pas à une cadence
 * commode : c'est celle-là qui doit donner les bonnes durées. */
#define DT (1.0f / 120.0f)

/* Deux points, dans le repère mesuré sur le glTF produit. La zone de
 * déclenchement de `porte_wc` va de x 7,95 à 9,95 et de z −2,80 à −1,15. */
static const ns_v3 DEVANT = { 8.60f, 1.70f, -1.97f };   /* dans la zone */
static const ns_v3 LOIN   = { 4.00f, 1.70f,  3.00f };   /* franchement dehors */

/* Avance la simulation de `seconds` avec le joueur immobile. Rend le nombre de
 * pas effectués, pour que l'appelant puisse raisonner en durées. */
static void run(room_doors *d, ns_v3 player, float seconds)
{
    const int steps = (int)(seconds / DT + 0.5f);
    for (int i = 0; i < steps; ++i) room_doors_tick(d, player, DT);
}

/* Une porte sans scène : `bound` reste faux, mais la machine à états tourne
 * entièrement — c'est elle qu'on teste, pas le téléversement. */
static void setup(room_doors *d)
{
    room_doors_init(d, NULL);
}

int main(void)
{
    ns_log_set_level(NS_LOG_WARN);

    room_doors d;
    setup(&d);
    CHECK(d.count >= 1, "au moins une porte est déclarée (compte %u)", d.count);
    if (d.count == 0) {
        printf("%d vérification(s), %d échec(s)\n", g_checks, g_failures);
        return 1;
    }
    room_door *p = &d.door[0];

    /* --- au repos : fermée, et elle le reste ---------------------------- */
    CHECK(p->state == ROOM_DOOR_CLOSED, "elle démarre fermée");
    CHECK(p->openness == 0.0f, "et à zéro d'ouverture (%.3f)", (double)p->openness);
    run(&d, LOIN, 10.0f);
    CHECK(p->state == ROOM_DOOR_CLOSED,
          "dix secondes loin d'elle ne l'ouvrent pas (état %d)", (int)p->state);

    /* --- 1. l'approche l'ouvre, et par PROXIMITÉ ------------------------ */
    room_doors_tick(&d, DEVANT, DT);
    CHECK(p->state == ROOM_DOOR_OPENING, "s'approcher la met en ouverture");
    CHECK(p->want_open_sound, "et arme le son d'ouverture");
    CHECK(room_door_take_open_sound(p), "le drapeau se consomme");
    CHECK(!room_door_take_open_sound(p), "et ne se consomme qu'une fois");

    /* La durée de traversée : 1,05 m à 0,94 m/s font 1,12 s — le temps que
     * mettait la porte de 2020, qui est ce qu'on reconnaît. */
    const float travel_expected = p->desc.travel / d.speed;
    float elapsed = 0.0f;
    while (p->state == ROOM_DOOR_OPENING && elapsed < 5.0f) {
        room_doors_tick(&d, DEVANT, DT);
        elapsed += DT;
    }
    CHECK(p->state == ROOM_DOOR_OPEN, "elle finit par être ouverte");
    CHECK(fabsf(elapsed - travel_expected) < 0.05f,
          "l'ouverture dure la course sur la vitesse : %.3f s attendu, %.3f s obtenu",
          (double)travel_expected, (double)elapsed);
    CHECK(p->openness == 1.0f, "et l'ouverture sature à 1 (%.4f)", (double)p->openness);

    /* --- 2. le joueur RESTE : elle ne bat pas ---------------------------- */
    run(&d, DEVANT, d.hold_seconds * 4.0f);
    CHECK(p->state == ROOM_DOOR_OPEN,
          "rester devant la garde ouverte, même quatre fois le maintien (état %d)",
          (int)p->state);
    CHECK(!p->want_close_sound, "et n'arme aucun son de fermeture");

    /* --- 3. il s'en va : elle se referme, après le maintien -------------- */
    elapsed = 0.0f;
    while (p->state == ROOM_DOOR_OPEN && elapsed < 30.0f) {
        room_doors_tick(&d, LOIN, DT);
        elapsed += DT;
    }
    CHECK(p->state == ROOM_DOOR_CLOSING, "partir la met en fermeture");
    CHECK(fabsf(elapsed - d.hold_seconds) < 0.05f,
          "le maintien vaut %.2f s (obtenu %.3f s)",
          (double)d.hold_seconds, (double)elapsed);
    CHECK(room_door_take_close_sound(p), "le son de fermeture est armé");

    /* --- 4. il revient PENDANT la fermeture : elle rouvre d'où elle en est */
    run(&d, LOIN, 0.35f);                    /* elle s'est refermée en partie */
    const float caught = p->openness;
    CHECK(p->state == ROOM_DOOR_CLOSING, "elle est toujours en fermeture");
    CHECK(caught > 0.05f && caught < 0.95f,
          "et saisie à mi-course (%.3f)", (double)caught);

    room_doors_tick(&d, DEVANT, DT);
    CHECK(p->state == ROOM_DOOR_OPENING, "revenir la fait repartir en ouverture");
    CHECK(p->want_open_sound, "et rejoue le son d'ouverture");
    /* LE point : elle repart d'où elle en était, pas de zéro. Un pas de plus a
     * été simulé, donc on tolère ce pas. */
    CHECK(p->openness >= caught - 1e-4f,
          "elle repart de sa position courante (%.3f) et non de zéro (%.3f)",
          (double)p->openness, (double)caught);
    (void)room_door_take_open_sound(p);

    /* --- 5. le cycle complet est reproductible --------------------------- */
    run(&d, DEVANT, 3.0f);
    CHECK(p->state == ROOM_DOOR_OPEN, "elle se rouvre entièrement");
    run(&d, LOIN, d.hold_seconds + travel_expected + 0.5f);
    CHECK(p->state == ROOM_DOOR_CLOSED, "puis se referme entièrement (état %d)",
          (int)p->state);
    CHECK(p->openness == 0.0f, "et revient exactement à zéro (%.6f)",
          (double)p->openness);

    /* --- 6. l'ouverture ne sort JAMAIS de [0,1] -------------------------- */
    {
        room_doors e;
        setup(&e);
        room_door *q = &e.door[0];
        float worst_lo = 1.0f, worst_hi = 0.0f;
        /* Un joueur qui entre et sort sans arrêt : c'est le cas qui fait
         * osciller une machine à états mal bornée. */
        for (int i = 0; i < 6000; ++i) {
            room_doors_tick(&e, (i / 37) % 2 ? DEVANT : LOIN, DT);
            if (q->openness < worst_lo) worst_lo = q->openness;
            if (q->openness > worst_hi) worst_hi = q->openness;
        }
        CHECK(worst_lo >= 0.0f && worst_hi <= 1.0f,
              "l'ouverture reste dans [0,1] sur cinquante secondes d'aller-retour "
              "(min %.4f, max %.4f)", (double)worst_lo, (double)worst_hi);
    }

    /* ==================================================================== */
    /* La collision : une porte fermée ARRÊTE le joueur                      */
    /* ==================================================================== */
    /*
     * Sans scène, la porte n'est pas liée et ne produit donc pas de boîte. On en
     * fabrique une à la main, aux cotes MESURÉES du vantail fermé : x [8,814 ;
     * 8,877], y [0 ; 2,050], z [−2,50 ; −1,45]. Quatre centimètres et demi
     * d'épaisseur, ce qui est précisément l'épaisseur qui met en défaut un
     * dégagement naïf.
     */
    {
        room_blocker leaf;
        leaf.box.min = ns_v3_make(8.814f, 0.00f, -2.50f);
        leaf.box.max = ns_v3_make(8.877f, 2.05f, -1.45f);
        const room_blockers fermee = { &leaf, 1 };

        /*
         * Une caméra de joueur qui marche plein est, droit dans la baie.
         *
         * Deux réglages pour isoler ce qu'on mesure : le BVH est NUL — la
         * capsule se déplace alors librement, donc rien d'autre que le vantail
         * ne peut l'arrêter — et la gravité est mise à zéro, sans quoi le joueur
         * tomberait de cinquante mètres en trois secondes et sortirait par le bas
         * de la boîte, ce qui ferait passer le test pour de mauvaises raisons.
         */
        room_camera cam;
        room_camera_init(&cam, ns_v3_make(8.00f, 1.70f, -1.97f), 0.0f);
        cam.mode    = ROOM_CAM_PLAYER;
        cam.gravity = 0.0f;
        cam.grounded = true;
        /* Lacet nul = regard vers +x dans ce moteur ; on avance donc vers la
         * porte en poussant simplement la marche avant. On COURT, parce que
         * c'est la vitesse à laquelle un panneau mince se traverse. */
        cam.yaw = 0.0f;
        cam.running = true;

        const room_camera depart = cam;

        /* --- témoin : sans obstacle, on traverse bel et bien --------------- */
        float reached = 0.0f;
        for (int i = 0; i < 600; ++i) {
            cam.input_forward = 1.0f;
            room_camera_tick(&cam, NULL, NULL, DT);
            if (cam.position.x > reached) reached = cam.position.x;
        }
        const float derriere = leaf.box.max.x + cam.body_radius;
        CHECK(reached > derriere + 0.30f,
              "témoin : sans obstacle la marche franchit le plan du vantail "
              "(atteint x=%.3f, plan à %.3f)", (double)reached, (double)derriere);

        /* --- porte fermée : on est arrêté DEVANT, et on ne passe pas ------- */
        cam = depart;
        float furthest = cam.position.x;
        for (int i = 0; i < 600; ++i) {
            cam.input_forward = 1.0f;
            room_camera_tick(&cam, NULL, &fermee, DT);
            if (cam.position.x > furthest) furthest = cam.position.x;
        }
        const float devant = leaf.box.min.x - cam.body_radius;
        CHECK(furthest <= devant + 1e-3f,
              "porte fermée : le joueur est arrêté devant le vantail "
              "(le plus loin x=%.4f, face avant dégagée à %.4f)",
              (double)furthest, (double)devant);
        CHECK(cam.position.x < leaf.box.min.x,
              "et il ne se retrouve pas de l'autre côté (x=%.4f)",
              (double)cam.position.x);

        /* --- porte ouverte : le passage est libre -------------------------- */
        /* Le vantail rangé le long du refend, c'est-à-dire décalé de sa course
         * selon +z : il ne bouche plus la baie et ne doit plus rien arrêter. */
        room_blocker ouverte_leaf = leaf;
        ouverte_leaf.box.min.z += 1.05f;
        ouverte_leaf.box.max.z += 1.05f;
        const room_blockers ouverte = { &ouverte_leaf, 1 };

        cam = depart;
        for (int i = 0; i < 600; ++i) {
            cam.input_forward = 1.0f;
            room_camera_tick(&cam, NULL, &ouverte, DT);
        }
        CHECK(cam.position.x > derriere + 0.30f,
              "porte ouverte : le passage est libre (x=%.3f)",
              (double)cam.position.x);

        /* --- la porte se ferme SUR le joueur : il est dégagé, pas coincé --- */
        cam = depart;
        cam.position = ns_v3_make(8.845f, 1.70f, -1.97f);   /* dans le plan du vantail */
        cam.input_forward = 0.0f;
        for (int i = 0; i < 240; ++i) room_camera_tick(&cam, NULL, &fermee, DT);
        const bool degage = (cam.position.x <= leaf.box.min.x - cam.body_radius + 1e-3f)
                         || (cam.position.x >= leaf.box.max.x + cam.body_radius - 1e-3f);
        CHECK(degage,
              "une porte qui se ferme sur le joueur le repousse hors du vantail "
              "(x=%.4f, vantail [%.3f ; %.3f])",
              (double)cam.position.x, (double)leaf.box.min.x, (double)leaf.box.max.x);
    }

    printf("%d vérification(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
