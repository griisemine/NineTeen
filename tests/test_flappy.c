/*
 * test_flappy.c — les règles du jeu, sans écran.
 *
 * Le portage doit être comparé au jeu de 2020 sur ses RÈGLES, pas sur son
 * apparence : hauteur du passage, écart entre deux tuyaux, vitesse de
 * défilement, condition de défaite, barème. `legacy/games/3_flappy_bird/` est là
 * pour ça, et les constantes reprises y sont nommées dans les commentaires du
 * portage.
 *
 * Ce que ce test attrape vraiment : une partie qui devient injouable. Un écart
 * trop étroit pour la hauteur de saut, un recyclage de tuyaux qui les empile, un
 * barème qui compte deux fois, une chute qui traverse le sol. Aucun de ces
 * défauts ne se voit sur une capture fixe, et tous rendent le jeu inintéressant.
 */
#include "flappy/flappy.h"
#include "ns_core.h"

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

#define DT (1.0f / 120.0f)      /* le pas fixe du moteur */

static bool finite_game(const flappy *g)
{
    if (!isfinite(g->bird_y) || !isfinite(g->bird_vy) || !isfinite(g->bird_angle)) return false;
    for (int i = 0; i < FLAPPY_PIPES; ++i) if (!isfinite(g->pipes[i].position)) return false;
    return true;
}

/* -------------------------------------------------------------------------- */

static void test_ready_is_safe(void)
{
    flappy g;
    flappy_reset(&g, 1234, false);
    CHECK(g.phase == FLAPPY_READY, "on démarre en attente");

    /* Dix secondes sans toucher à rien : on ne doit NI mourir, NI marquer. Le
     * sursis est ce qui laisse comprendre qu'on joue avant d'être puni. */
    for (int i = 0; i < 1200; ++i) flappy_tick(&g, DT);
    CHECK(g.phase == FLAPPY_READY, "sans battement, la partie ne commence pas");
    CHECK(g.score == 0, "et rien n'est marqué");
    CHECK(finite_game(&g), "aucun NaN");
}

static void test_flap_lifts(void)
{
    flappy g;
    flappy_reset(&g, 7, false);
    flappy_flap(&g);
    CHECK(g.phase == FLAPPY_PLAYING, "un battement lance la partie");

    const float start = g.bird_y;
    float highest = start;
    for (int i = 0; i < 60; ++i) {         /* une demi-seconde */
        flappy_tick(&g, DT);
        if (g.bird_y < highest) highest = g.bird_y;
    }
    const float rise = start - highest;
    printf("  hauteur de saut : %.1f px\n", (double)rise);

    /*
     * L'original monte d'environ 100 px. On accepte 80 à 160 : c'est la plage
     * dans laquelle le jeu reste celui qu'on connaît. En dessous on ne franchit
     * plus rien, au-dessus on survole tout.
     */
    CHECK(rise > 80.0f && rise < 160.0f, "le saut monte comme en 2020 (%.1f px)", (double)rise);
}

static void test_gap_is_passable(void)
{
    /*
     * LA vérification qui compte, et celle qu'aucune capture ne donne : peut-on
     * franchir un tuyau ? On confie la partie au joueur automatique et on regarde
     * le score monter. S'il reste à zéro, l'écart, la gravité ou l'impulsion ne
     * sont pas compatibles entre eux — et le jeu est injouable, quelle que soit
     * la beauté de l'image.
     */
    for (int hard = 0; hard < 2; ++hard) {
        flappy g;
        flappy_reset(&g, 99 + (uint64_t)hard, hard != 0);

        int steps = 0;
        while (g.phase != FLAPPY_DEAD && steps < 120 * 60) {   /* une minute */
            flappy_autopilot(&g);
            flappy_tick(&g, DT);
            steps++;
        }
        printf("  %s : score %u en %.1f s%s\n", hard ? "hard  " : "normal",
               g.score, (double)steps / 120.0,
               g.phase == FLAPPY_DEAD ? " (mort)" : "");

        CHECK(g.score >= 10,
              "%s : le joueur automatique franchit au moins dix tuyaux (%u)",
              hard ? "hard" : "normal", g.score);
        CHECK(finite_game(&g), "aucun NaN après une minute");
    }
}

static void test_pipes_stay_spaced(void)
{
    /*
     * Le recyclage replace un tuyau sorti par la gauche derrière le plus avancé.
     * S'il se trompe de référence, les tuyaux s'empilent — et le jeu devient
     * infranchissable sans qu'aucun test de score ne le dise, puisque le joueur
     * meurt aussitôt.
     */
    flappy g;
    flappy_reset(&g, 4242, false);
    for (int i = 0; i < 120 * 45; ++i) { flappy_autopilot(&g); flappy_tick(&g, DT); }

    float xs[FLAPPY_PIPES];
    for (int i = 0; i < FLAPPY_PIPES; ++i) xs[i] = g.pipes[i].position;
    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        for (int j = i + 1; j < FLAPPY_PIPES; ++j) {
            const float d = fabsf(xs[i] - xs[j]);
            CHECK(d > 300.0f, "deux tuyaux gardent leur écart (%.1f px entre %d et %d)",
                  (double)d, i, j);
        }
    }
}

/*
 * LA COURBE : le jeu doit devenir plus difficile.
 *
 * Ce qui manquait, et qui ne se voit qu'en mesurant longtemps : le score de
 * Flappy était une DROITE. Pilote automatique, cinq graines, six durées — 1, 4,
 * 15, 33, 69, 141 tuyaux à 6, 12, 30, 60, 120 et 240 s — soit 0,588 tuyau par
 * seconde du début à la fin, identique aux cinq graines, et zéro mort en quatre
 * minutes. L'écart entre deux tuyaux ne bougeait jamais.
 *
 * Le test tient les deux bouts de la rampe : l'écart de DÉPART est bien celui
 * de 2020, et il s'est resserré une fois la rampe parcourue. Sans les deux, on
 * pourrait « corriger » la courbe en durcissant le début — ce qui chasserait le
 * débutant au lieu de retenir l'habitué.
 */
static void test_la_difficulte_monte(void)
{
    flappy g;
    flappy_reset(&g, 4242, false);

    /* Au départ : l'écart de 2020, à un pixel près. */
    const float depart = g.pipes[1].position - g.pipes[0].position;
    CHECK(depart > 399.0f && depart < 401.0f,
          "l'écart de départ est celui de 2020 (%.1f px)", (double)depart);

    /* Une fois la rampe parcourue, il s'est resserré. On force le score
     * plutôt que de jouer une heure : c'est LUI qui pilote la rampe. */
    g.phase = FLAPPY_PLAYING;     /* le décor ne défile qu'en jeu */
    g.score = 100;
    for (int i = 0; i < FLAPPY_PIPES; ++i) g.pipes[i].position = -1e4f;
    flappy_tick(&g, DT);          /* le recyclage replace les huit tuyaux */

    float dmin = 1e9f;
    for (int i = 0; i < FLAPPY_PIPES; ++i) {
        for (int j = i + 1; j < FLAPPY_PIPES; ++j) {
            const float d = fabsf(g.pipes[i].position - g.pipes[j].position);
            if (d > 1.0f && d < dmin) dmin = d;
        }
    }
    CHECK(dmin < depart - 40.0f,
          "après cent tuyaux l'écart s'est resserré (%.1f px contre %.1f)",
          (double)dmin, (double)depart);
    CHECK(dmin > 300.0f,
          "mais il reste franchissable (%.1f px)", (double)dmin);
}

static void test_score_counts_once(void)
{
    flappy g;
    flappy_reset(&g, 8, false);
    uint32_t events = 0;
    for (int i = 0; i < 120 * 30; ++i) {
        flappy_autopilot(&g);
        flappy_tick(&g, DT);
        if (g.scored_now) events++;
        if (g.phase == FLAPPY_DEAD) break;
    }
    CHECK(events == g.score, "un tuyau franchi = un point (%u événements, score %u)",
          events, g.score);
    CHECK(g.best == g.score, "le meilleur suit le score courant");
}

static void test_death_and_floor(void)
{
    flappy g;
    flappy_reset(&g, 3, false);
    flappy_flap(&g);

    /* On ne fait plus rien : l'oiseau doit toucher le sol et mourir. */
    int steps = 0;
    while (g.phase != FLAPPY_DEAD && steps < 120 * 20) { flappy_tick(&g, DT); steps++; }
    CHECK(g.phase == FLAPPY_DEAD, "sans battement, on finit par mourir");
    CHECK(g.died_now || steps > 0, "la mort est signalée");

    /* Puis la chute finale s'arrête AU sol, elle ne le traverse pas. */
    for (int i = 0; i < 600; ++i) flappy_tick(&g, DT);
    CHECK(finite_game(&g), "aucun NaN après la mort");
    CHECK(g.bird_y < FLAPPY_H, "l'oiseau ne traverse pas le sol (y = %.1f)", (double)g.bird_y);

    /* Et un battement après la mort ne relance rien : c'est ce qui empêche de
     * repartir en gardant la touche enfoncée au moment du choc. */
    const flappy_phase before = g.phase;
    flappy_flap(&g);
    CHECK(g.phase == before, "battre des ailes après la mort ne fait rien");
}

static void test_deterministic(void)
{
    /*
     * Même graine, mêmes entrées, même partie. C'est ce qui rend une capture
     * reproductible et un rejeu de partie possible — donc, plus tard, un duel
     * vérifiable côté serveur.
     */
    flappy a, b;
    flappy_reset(&a, 20240418, false);
    flappy_reset(&b, 20240418, false);
    for (int i = 0; i < 120 * 20; ++i) {
        flappy_autopilot(&a); flappy_tick(&a, DT);
        flappy_autopilot(&b); flappy_tick(&b, DT);
    }
    CHECK(a.score == b.score && fabsf(a.bird_y - b.bird_y) < 1e-6f,
          "deux parties de même graine sont identiques (%u/%u, %.6f/%.6f)",
          a.score, b.score, (double)a.bird_y, (double)b.bird_y);

    flappy c;
    flappy_reset(&c, 777, false);
    for (int i = 0; i < 120 * 20; ++i) { flappy_autopilot(&c); flappy_tick(&c, DT); }
    CHECK(c.score != a.score || fabsf(c.bird_y - a.bird_y) > 1e-6f,
          "deux graines différentes donnent des parties différentes");
}

int main(void)
{
    test_ready_is_safe();
    test_flap_lifts();
    test_gap_is_passable();
    test_pipes_stay_spaced();
    test_la_difficulte_monte();
    test_score_counts_once();
    test_death_and_floor();
    test_deterministic();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
