/*
 * test_snake.c — les règles de Snake, confrontées à celles de 2020.
 *
 * Ce qui mérite un test ici n'est pas le dessin, ce sont les règles : ce sont
 * elles qu'on prétend avoir reprises, et elles ne se voient pas sur une capture.
 * Le portage convertit tout un jeu écrit en IMAGES à 30 Hz vers un pas fixe en
 * secondes ; une conversion ratée donne un jeu qui a l'air juste et se joue
 * autrement.
 *
 * Le cas qui justifie le fichier à lui seul : **le hardcore est l'inverse du
 * normal**. `RATIO_GET_FRUIT_HARDCORE` vaut −5, donc manger COÛTE et le score
 * vient des fruits qu'on laisse expirer. C'est la règle la plus surprenante de
 * 2020 et exactement celle qu'on « corrigerait » par mégarde.
 */
#include "games.h"
#include "ns_core.h"
#include "snake.h"

#include <SDL3/SDL.h>

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
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define STEP (1.0f / 120.0f)

static void run(snake *g, float seconds)
{
    const int n = (int)(seconds / STEP);
    for (int i = 0; i < n; ++i) snake_tick(g, STEP);
}

/* -------------------------------------------------------------------------- */

static void test_depart(void)
{
    snake g;
    snake_reset(&g, 1234, false);

    CHECK(g.parts == SNAKE_PRE + 30,
          "le serpent part avec 30 segments plus le tuyau de digestion (%u)", g.parts);
    CHECK(fabsf(g.angle - 3.0f * NS_PI / 2.0f) < 1e-4f,
          "il regarde vers le haut, comme BASE_ANGLE (%.4f)", (double)g.angle);
    CHECK(fabsf(g.speed - 7.0f) < 1e-4f, "BASE_SPEED vaut 7 (%.3f)", (double)g.speed);
    CHECK(g.score == 0 && g.phase == SNAKE_READY, "il commence à zéro, en sursis");

    /* La tête au centre du TERRAIN, pas de la fenêtre : 1728 x 972. */
    CHECK(fabsf(g.part[SNAKE_PRE].x - SNAKE_PLAY_W * 0.5f) < 0.5f
       && fabsf(g.part[SNAKE_PRE].y - SNAKE_PLAY_H * 0.5f) < 0.5f,
          "la tête est au centre du terrain (%.1f, %.1f)",
          (double)g.part[SNAKE_PRE].x, (double)g.part[SNAKE_PRE].y);
}

static void test_rotation(void)
{
    snake g;
    snake_reset(&g, 1234, false);
    const float a0 = g.angle;

    /* TURN_AMMOUNT 0,13 rad par image à 30 Hz = 3,9 rad/s. En une demi-seconde,
     * 1,95 rad — et le sens compte : « droite » augmente l'angle, comme
     * `turnRight` de 2020. */
    snake_hold(&g, false, true, false);
    run(&g, 0.5f);
    float delta = g.angle - a0;
    while (delta < -NS_PI) delta += 2.0f * NS_PI;
    while (delta > NS_PI) delta -= 2.0f * NS_PI;
    CHECK(fabsf(delta - 1.95f) < 0.05f,
          "tourner à droite une demi-seconde vaut 1,95 rad (%.3f)", (double)delta);

    snake_reset(&g, 1234, false);
    snake_hold(&g, true, false, false);
    run(&g, 0.5f);
    delta = g.angle - a0;
    while (delta < -NS_PI) delta += 2.0f * NS_PI;
    while (delta > NS_PI) delta -= 2.0f * NS_PI;
    CHECK(fabsf(delta + 1.95f) < 0.05f,
          "et à gauche, l'opposé exact (%.3f)", (double)delta);

    /* Les deux ensemble ne tournent pas : c'est ce qui arrive quand on écrase
     * les deux flèches, et un serpent qui partirait d'un côté serait arbitraire. */
    snake_reset(&g, 1234, false);
    snake_hold(&g, true, true, false);
    run(&g, 0.5f);
    CHECK(fabsf(g.angle - a0) < 1e-3f, "les deux flèches ensemble ne tournent pas");
}

static void test_vitesse_independante_du_pas(void)
{
    /*
     * LE test de la conversion. Le même temps de jeu doit produire le même
     * déplacement, que le moteur tourne à 120 Hz ou à 40 Hz. Sans l'accumulateur
     * de distance, la vitesse dépendrait de la fréquence de tick — le défaut
     * qu'on a passé le projet à retirer.
     */
    snake a, b;
    snake_reset(&a, 77, false);
    snake_reset(&b, 77, false);

    for (int i = 0; i < 240; ++i) snake_tick(&a, 1.0f / 120.0f);   /* 2 s */
    for (int i = 0; i < 80; ++i)  snake_tick(&b, 1.0f / 40.0f);    /* 2 s */

    const float dx = a.part[SNAKE_PRE].x - b.part[SNAKE_PRE].x;
    const float dy = a.part[SNAKE_PRE].y - b.part[SNAKE_PRE].y;
    CHECK(sqrtf(dx * dx + dy * dy) < 6.0f,
          "à 120 Hz et à 40 Hz, le serpent est au même endroit après 2 s (écart %.2f px)",
          (double)sqrtf(dx * dx + dy * dy));

    /* Et la distance parcourue est celle de 2020 : 7 px par image à 30 Hz,
     * soit 210 px/s, donc 420 px en deux secondes — vers le haut. */
    const float moved = SNAKE_PLAY_H * 0.5f - a.part[SNAKE_PRE].y;
    CHECK(fabsf(moved - 420.0f) < 12.0f,
          "il a parcouru 420 px en 2 s, comme BASE_SPEED l'impose (%.1f)", (double)moved);
}

static void test_mort_contre_le_mur(void)
{
    snake g;
    snake_reset(&g, 5, false);

    /* L'invincibilité de départ protège 8 s. On la laisse passer en tournant en
     * rond au centre, sinon le serpent atteint le mur avant la fin du sursis. */
    snake_hold(&g, false, true, false);
    run(&g, 9.0f);
    CHECK(g.phase != SNAKE_DEAD, "il survit à ses neuf premières secondes en tournant");
    CHECK(g.invincible <= 0.0f, "l'invincibilité est retombée (%.2f)", (double)g.invincible);

    /* Puis tout droit : le terrain fait 972 de haut, donc moins de 3 s suffisent
     * pour atteindre un bord depuis n'importe où. */
    snake_hold(&g, false, false, false);
    run(&g, 6.0f);
    CHECK(g.phase == SNAKE_DEAD, "il finit par se tuer contre un mur");
}

static void test_invincibilite_protege_puis_cesse(void)
{
    /* Deux parties identiques, un seul paramètre changé : le temps. C'est la
     * seule façon d'attribuer l'écart à ce qu'on prétend mesurer. */
    snake g;
    snake_reset(&g, 9, false);
    snake_hold(&g, false, false, false);
    run(&g, 2.0f);
    /* Deux secondes tout droit ne peuvent pas atteindre le mur (420 px/2 s pour
     * 486 px de demi-terrain), donc ce test ne dit rien tout seul — ce qu'il
     * vérifie est que le compteur DESCEND et à la bonne vitesse. */
    CHECK(fabsf(g.invincible - 6.0f) < 0.1f,
          "l'invincibilité descend d'une seconde par seconde (%.2f)", (double)g.invincible);

    run(&g, 6.2f);
    CHECK(g.invincible <= 0.0f, "et elle s'épuise après 8 s (%.2f)", (double)g.invincible);
}

static void test_mort_dans_sa_queue(void)
{
    /*
     * La morsure, testée SUR SA RÈGLE plutôt que sur une mise en situation.
     *
     * Premier jet : faire tourner le serpent en rond jusqu'à ce qu'il se morde.
     * Il ne se mord jamais, et c'est CORRECT — le rayon de virage vaut
     * 210 / 3,9 = 54 px, donc un tour fait 338 px, quand un serpent de trente
     * segments espacés de 5 px n'en mesure que 150. Un serpent court tourne
     * librement, exactement comme dans l'original ; il faut manger pour devenir
     * assez long pour se rattraper. Le test mesurait donc la longueur du
     * serpent en croyant mesurer la hitbox.
     *
     * On pose donc un segment à une distance connue de la tête, et on regarde.
     */
    const float R = 35.0f;   /* BODY_DEATH_HITBOX */

    /*
     * Deux précautions, apprises en écrivant :
     *
     * — on remplit PLUSIEURS index consécutifs, parce que chaque pas décale
     *   tout le corps d'une case : un seul segment posé se serait déplacé avant
     *   d'être regardé ;
     * — on avance une dizaine de ticks, parce que la collision se teste par PAS
     *   de 5 px et qu'un tick à 120 Hz n'en parcourt que 1,75.
     */
    #define PLANT(g, X, Y)                                                     \
        do { for (uint32_t k = 45; k < 56 && k < (g).parts; ++k) {             \
                 (g).part[k].x = (X); (g).part[k].y = (Y); } } while (0)

    /* Droit devant, à portée : ça tue. Le serpent part vers le HAUT. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.invincible = 0.0f;
        g.phase = SNAKE_PLAYING;
        CHECK(g.parts > 56, "le corps est assez long pour l'expérience (%u)", g.parts);
        PLANT(g, g.part[SNAKE_PRE].x, g.part[SNAKE_PRE].y - R * 0.7f);
        run(&g, 0.1f);
        CHECK(g.phase == SNAKE_DEAD, "un segment à moins de 35 px de la tête la tue");
        CHECK(g.died, "et la mort est signalée");
    }

    /* Loin sur le côté : ça ne tue pas. Sans ce second cas, une hitbox infinie
     * passerait le premier. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.invincible = 0.0f;
        g.phase = SNAKE_PLAYING;
        PLANT(g, g.part[SNAKE_PRE].x + R * 8.0f, g.part[SNAKE_PRE].y);
        run(&g, 0.1f);
        CHECK(g.phase != SNAKE_DEAD, "un segment à 280 px sur le côté ne tue pas");
    }

    /* Et pendant le sursis, rien ne tue — c'est ce qui laisse le temps de
     * comprendre où l'on est. */
    {
        snake g;
        snake_reset(&g, 11, false);
        g.phase = SNAKE_PLAYING;      /* invincible reste à 8 s */
        PLANT(g, g.part[SNAKE_PRE].x, g.part[SNAKE_PRE].y - R * 0.7f);
        run(&g, 0.1f);
        CHECK(g.phase != SNAKE_DEAD, "pendant l'invincibilité, se mordre ne tue pas");
    }
    #undef PLANT
}

static void test_digestion_allonge(void)
{
    /*
     * Manger allonge — mais PAS tout de suite : le rayon traverse d'abord les
     * 31 cases du tuyau de digestion. C'est ce délai qui fait qu'on voit la
     * bosse descendre, et c'est la signature visuelle du jeu.
     */
    snake g;
    snake_reset(&g, 3, false);
    const uint32_t before = g.parts;

    /* On dépose à la main la courbe d'un fruit copieux, sans dépendre du hasard
     * des apparitions : le test porte sur la digestion, pas sur le tirage. */
    for (int i = 0; i < SNAKE_PRE; ++i) g.part[i].radius += 2.0f;

    run(&g, 0.05f);
    CHECK(g.parts == before, "il ne s'allonge pas dans l'instant (%u -> %u)", before, g.parts);

    run(&g, 6.0f);
    CHECK(g.parts > before, "puis il s'allonge (%u -> %u)", before, g.parts);
    CHECK(g.parts < before + 200, "et pas de façon absurde (%u)", g.parts);
}

static void test_determinisme(void)
{
    /*
     * Même graine, mêmes entrées, même partie. C'est la condition pour que le
     * journal signé veuille dire quelque chose : le serveur rejoue la partie et
     * doit retrouver le même score.
     */
    snake a, b;
    snake_reset(&a, 987654321u, false);
    snake_reset(&b, 987654321u, false);

    for (int i = 0; i < 10000; ++i) {
        const bool left = (i / 37) % 3 == 0;
        const bool right = (i / 53) % 4 == 0;
        snake_hold(&a, left, right, false);
        snake_hold(&b, left, right, false);
        snake_tick(&a, STEP);
        snake_tick(&b, STEP);
    }
    CHECK(a.score == b.score, "même score (%lld contre %lld)",
          (long long)a.score, (long long)b.score);
    CHECK(a.parts == b.parts, "même longueur (%u contre %u)", a.parts, b.parts);
    CHECK(a.part[SNAKE_PRE].x == b.part[SNAKE_PRE].x
       && a.part[SNAKE_PRE].y == b.part[SNAKE_PRE].y, "même position, au bit près");

    /*
     * Et une graine différente donne une partie différente — sinon le
     * déterminisme ne prouverait que l'immobilité.
     *
     * On compare les FRUITS et pas le score : le déplacement du serpent ne tire
     * rien au sort, il ne dépend que des entrées. La graine ne pilote que les
     * apparitions, et c'est donc là qu'il faut regarder. Comparer les scores
     * aurait donné deux zéros identiques et un test qui passe pour rien.
     */
    snake c;
    snake_reset(&c, 123456789u, false);
    snake d;
    snake_reset(&d, 987654321u, false);
    run(&c, 12.0f);
    run(&d, 12.0f);

    bool same = true;
    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) {
        if (c.fruit[i].id != d.fruit[i].id
            || c.fruit[i].x != d.fruit[i].x || c.fruit[i].y != d.fruit[i].y) {
            same = false;
            break;
        }
    }
    CHECK(!same, "une autre graine pose les fruits ailleurs");
}

static void test_autopilote_tient_une_minute(void)
{
    snake g;
    snake_reset(&g, 24680u, false);

    int alive_steps = 0;
    for (int i = 0; i < 120 * 60; ++i) {
        snake_autopilot(&g);
        snake_tick(&g, STEP);
        if (g.phase != SNAKE_DEAD) alive_steps++;

        /* Aucun NaN ne doit s'installer, et le corps reste sur le terrain. */
        const snake_part *h = &g.part[SNAKE_PRE];
        if (!(h->x == h->x) || !(h->y == h->y)) { CHECK(false, "NaN au pas %d", i); break; }
    }
    CHECK(alive_steps > 120 * 20,
          "le joueur automatique tient au moins vingt secondes (%.1f s)",
          (double)alive_steps / 120.0);
    CHECK(g.score > 0, "et il marque (%lld)", (long long)g.score);
    CHECK(g.parts < SNAKE_MAX_PARTS, "sans faire déborder le corps (%u)", g.parts);
}

static void test_le_bareme_correspond_au_score(void)
{
    /*
     * L'invariant que le serveur recalcule : la somme des valeurs émises vaut le
     * score. S'il est faux, TOUTE partie est rejetée — et silencieusement, la
     * soumission étant « au mieux, jamais bloquante ».
     */
    snake g;
    snake_reset(&g, 4242u, false);

    int64_t sum = 0;
    for (int i = 0; i < 120 * 60; ++i) {
        snake_autopilot(&g);
        snake_tick(&g, STEP);

        ns_game_events ev; SDL_zero(ev);
        g_snake_api.events(&g, &ev);
        if (ev.score) {
            /* Le bonus est à barème FIXE côté serveur (50 points) ; sa valeur ne
             * voyage pas, donc on la compte comme le serveur la comptera. */
            sum += (SDL_strcmp(ev.score_kind, "bonus") == 0) ? 50 : ev.score_value;
        }
        if (g.phase == SNAKE_DEAD) break;
    }
    CHECK(sum == g.score, "la somme des gains émis vaut le score (%lld contre %lld)",
          (long long)sum, (long long)g.score);
}

static void test_hardcore_est_l_inverse(void)
{
    /*
     * LA règle qu'on casserait sans s'en rendre compte. En hardcore, manger un
     * fruit COÛTE cinq fois sa valeur — le score vient des fruits qu'on laisse
     * expirer. Le joueur automatique, lui, court après les fruits : en hardcore
     * il doit donc faire PIRE que zéro, là où il marque en normal.
     */
    snake n, h;
    snake_reset(&n, 31337u, false);
    snake_reset(&h, 31337u, true);

    for (int i = 0; i < 120 * 45; ++i) {
        snake_autopilot(&n); snake_tick(&n, STEP);
        snake_autopilot(&h); snake_tick(&h, STEP);
    }

    CHECK(n.score > 0, "en normal, courir après les fruits rapporte (%lld)",
          (long long)n.score);
    CHECK(h.score < n.score,
          "en hardcore, la même stratégie rapporte moins (%lld contre %lld)",
          (long long)h.score, (long long)n.score);

    /* Et le score exposé au classement ne descend jamais sous zéro : on ne doit
     * rien à la salle en sortant. C'est aussi ce que fait le serveur. */
    CHECK(g_snake_api.score(&h) == (h.score > 0 ? (uint32_t)h.score : 0u),
          "le score exposé est borné à zéro (%u pour %lld)",
          g_snake_api.score(&h), (long long)h.score);

    /* Un fruit mangé en hardcore émet bien une valeur NÉGATIVE : c'est ce qui a
     * obligé le serveur à accepter des bornes signées. */
    snake m;
    snake_reset(&m, 555u, true);
    bool saw_negative = false;
    for (int i = 0; i < 120 * 60 && !saw_negative; ++i) {
        snake_autopilot(&m);
        snake_tick(&m, STEP);
        ns_game_events ev; SDL_zero(ev);
        g_snake_api.events(&m, &ev);
        if (ev.score && SDL_strcmp(ev.score_kind, "fruit") == 0 && ev.score_value < 0) {
            saw_negative = true;
        }
    }
    CHECK(saw_negative, "manger en hardcore émet une valeur négative");
}

static void test_bornes_du_serveur(void)
{
    /*
     * Les valeurs émises doivent tenir dans les bornes que `runs.go` déclare
     * pour Snake : −100 000 à 100 000. Le muffin rose vaut 10 000, ×10 en géant,
     * ×(−5) en hardcore — donc −500 000 serait possible si l'on cumulait les
     * deux. On vérifie que ça n'arrive pas.
     */
    for (int hard = 0; hard < 2; ++hard) {
        snake g;
        snake_reset(&g, 8080u + (uint64_t)hard, hard != 0);
        for (int i = 0; i < 120 * 90; ++i) {
            snake_autopilot(&g);
            snake_tick(&g, STEP);
            ns_game_events ev; SDL_zero(ev);
            g_snake_api.events(&g, &ev);
            if (ev.score && ev.score_value != 0) {
                CHECK(ev.score_value >= -100000 && ev.score_value <= 100000,
                      "%s : la valeur émise tient dans les bornes du serveur (%lld)",
                      hard ? "hardcore" : "normal", (long long)ev.score_value);
                if (ev.score_value < -100000 || ev.score_value > 100000) return;
            }
            if (g.phase == SNAKE_DEAD) snake_reset(&g, 8080u + (uint64_t)i, hard != 0);
        }
    }
    CHECK(true, "quatre-vingt-dix secondes de jeu restent dans les bornes");
}

int main(void)
{
    test_depart();
    test_rotation();
    test_vitesse_independante_du_pas();
    test_invincibilite_protege_puis_cesse();
    test_mort_contre_le_mur();
    test_mort_dans_sa_queue();
    test_digestion_allonge();
    test_determinisme();
    test_autopilote_tient_une_minute();
    test_le_bareme_correspond_au_score();
    test_hardcore_est_l_inverse();
    test_bornes_du_serveur();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
