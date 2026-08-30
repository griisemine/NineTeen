/*
 * test_piano.c — la partition, et la règle qui fait le jeu.
 *
 * Le Piano de 2020 n'avait **aucun score** : pas un point dans ses 308 lignes.
 * Ce qu'il avait, et qu'il fallait garder, c'est sa règle : **frapper une voie
 * vide termine la partie**. Elle est ce qui empêche de marteler les quatre
 * touches, donc ce qui fait qu'il y a un jeu.
 *
 * Le reste — le barème `note` 5 / `combo` 25 — vient de `rulesTable["piano"]`,
 * écrite en M6 pour un jeu qui n'existait pas encore.
 */
#include "games.h"
#include "ns_core.h"
#include "piano.h"

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

static const ns_game_button LANE_KEY[PN_LANES] = {
    NS_GAME_LEFT, NS_GAME_UP, NS_GAME_DOWN, NS_GAME_RIGHT
};

/* -------------------------------------------------------------------------- */

static void test_la_partition(void)
{
    piano g;
    piano_reset(&g, 1, false);

    CHECK(g.phase == PN_READY, "la partie commence en sursis");
    CHECK(g.score == 0 && g.combo == 0, "tout est à zéro");
    CHECK(g.note_count > 0, "la partition est chargée (%u notes)", g.note_count);

    /*
     * `musique.txt` contient huit lignes portant une note sur treize, et une
     * seule voie à la fois — c'est un morceau à une main. La recopie doit le
     * conserver : deux notes simultanées voudraient dire qu'on a lu de travers.
     */
    int simultaneous = 0;
    for (uint32_t i = 0; i < g.note_count; ++i) {
        for (uint32_t j = i + 1; j < g.note_count; ++j) {
            if (fabsf(g.note[i].start - g.note[j].start) < 1e-4f) simultaneous++;
        }
    }
    CHECK(simultaneous == 0, "aucune note simultanée, comme dans musique.txt (%d)",
          simultaneous);

    /* Les quatre voies sont employées : une partition qui n'en jouerait qu'une
     * signalerait une colonne perdue à la recopie. */
    bool used[PN_LANES] = { false, false, false, false };
    for (uint32_t i = 0; i < g.note_count; ++i) used[g.note[i].lane] = true;
    for (int l = 0; l < PN_LANES; ++l) CHECK(used[l], "la voie %d est jouée", l);

    /* Les notes sont ordonnées, et aucune n'est déjà passée. */
    int disordered = 0;
    for (uint32_t i = 1; i < g.note_count; ++i)
        if (g.note[i].start < g.note[i - 1].start) disordered++;
    CHECK(disordered == 0, "les notes arrivent dans l'ordre (%d inversions)", disordered);
    CHECK(g.note[0].start > 0.0f, "la première laisse le temps de la voir (%.2f s)",
          (double)g.note[0].start);
}

/* La règle de 2020 : frapper une voie vide termine la partie. */
static void test_la_fausse_note_termine(void)
{
    piano g;
    piano_reset(&g, 1, false);
    piano_press(&g, NS_GAME_ACTION);
    CHECK(g.phase == PN_PLAYING, "le bouton démarre");

    /* Au démarrage, aucune note n'est dans la fenêtre : n'importe quelle voie
     * est une fausse note. */
    int empty = 0;
    for (int l = 0; l < PN_LANES; ++l) if (piano_note_at(&g, l) < 0) empty++;
    CHECK(empty == PN_LANES, "les quatre voies sont vides au démarrage (%d)", empty);

    piano_press(&g, NS_GAME_LEFT);
    CHECK(g.phase == PN_DEAD, "frapper une voie vide termine la partie");
    CHECK(g.score == 0, "et ne rapporte rien (%lld)", (long long)g.score);

    /* Et plus rien ne se joue. */
    piano_press(&g, NS_GAME_UP);
    CHECK(g.score == 0, "une partie finie ne se joue plus");
}

/* Une note frappée dans la fenêtre vaut 5 ; un palier de combo vaut 25 de plus
 * toutes les dix notes. */
static void test_le_bareme(void)
{
    piano g;
    piano_reset(&g, 1, false);
    piano_press(&g, NS_GAME_ACTION);

    /* On avance jusqu'à la première note et on la frappe. */
    while (g.phase == PN_PLAYING && piano_note_at(&g, g.note[0].lane) < 0) {
        piano_tick(&g, STEP);
    }
    const int lane = g.note[0].lane;
    CHECK(piano_note_at(&g, lane) >= 0, "la première note devient frappable");
    piano_press(&g, LANE_KEY[lane]);

    CHECK(g.phase == PN_PLAYING, "la frappe juste ne tue pas");
    CHECK(g.score == 5, "et vaut cinq points (%lld)", (long long)g.score);
    CHECK(g.combo == 1, "le combo démarre (%u)", g.combo);

    /* Dix notes d'affilée : cinquante points, plus vingt-cinq de palier. */
    piano_reset(&g, 1, false);
    piano_press(&g, NS_GAME_ACTION);
    int played = 0;
    for (int i = 0; i < 120 * 60 && played < 10 && g.phase == PN_PLAYING; ++i) {
        for (int l = 0; l < PN_LANES; ++l) {
            if (piano_note_at(&g, l) < 0) continue;
            piano_press(&g, LANE_KEY[l]);
            played++;
            break;
        }
        piano_tick(&g, STEP);
    }
    CHECK(played == 10, "dix notes jouées (%d)", played);
    CHECK(g.combo == 10, "dix d'affilée (%u)", g.combo);
    CHECK(g.score == 10 * 5 + 25, "soit 75 points (%lld)", (long long)g.score);
}

/* Laisser passer une note casse le combo — mais ne tue pas. C'est la
 * dissymétrie de 2020 : la faute punie est la FAUSSE note, pas l'oubli. */
static void test_l_oubli_ne_tue_pas(void)
{
    piano g;
    piano_reset(&g, 1, false);
    piano_press(&g, NS_GAME_ACTION);

    /* On joue deux notes, puis on en laisse passer une. */
    int played = 0;
    for (int i = 0; i < 120 * 60 && played < 2 && g.phase == PN_PLAYING; ++i) {
        for (int l = 0; l < PN_LANES; ++l) {
            if (piano_note_at(&g, l) < 0) continue;
            piano_press(&g, LANE_KEY[l]);
            played++;
            break;
        }
        piano_tick(&g, STEP);
    }
    CHECK(g.combo == 2, "deux notes enchaînées (%u)", g.combo);

    const uint32_t misses = g.misses;
    for (int i = 0; i < 120 * 10 && g.misses == misses; ++i) piano_tick(&g, STEP);
    CHECK(g.misses > misses, "une note passe (%u ratées)", g.misses);
    CHECK(g.phase == PN_PLAYING, "et l'oubli ne tue pas");
    CHECK(g.combo == 0, "mais il casse le combo (%u)", g.combo);
}

/*
 * Mais TROP d'oublis tuent — dans les deux modes, et c'est nouveau.
 *
 * Le mode normal n'avait aucune condition de défaite : `g->hard &&` gardait la
 * limite pour la seule borne difficile. Mesuré, cinq graines : le pilote
 * automatique tenait 300 s sans mourir, et un joueur qui ne touchait à RIEN en
 * faisait autant, indéfiniment, avec zéro point. Un jeu qu'on ne peut pas
 * perdre n'a pas de score qui compte.
 *
 * Ce test tient la règle par son bout le plus simple : on ne joue pas, et la
 * partie doit finir — quelle que soit la borne.
 */
static void test_ne_rien_jouer_finit_par_tuer(void)
{
    for (int hard = 0; hard < 2; ++hard) {
        piano g;
        piano_reset(&g, 3, hard != 0);
        piano_press(&g, NS_GAME_ACTION);

        int steps = 0;
        while (g.phase != PN_DEAD && steps < 120 * 120) { piano_tick(&g, STEP); steps++; }

        CHECK(g.phase == PN_DEAD,
              "%s : ne rien jouer termine la partie (%.1f s)",
              hard ? "hard  " : "normal", (double)steps / 120.0);
        CHECK(g.fail_reason == PN_FAIL_TOO_MANY_MISSES,
              "%s : et c'est bien l'oubli qui l'a terminée (%d)",
              hard ? "hard  " : "normal", (int)g.fail_reason);
        CHECK(g.misses == (uint32_t)(hard ? 3 : 12),
              "%s : la limite est celle de la borne (%u oublis)",
              hard ? "hard  " : "normal", g.misses);
    }
}

/* La partition boucle et accélère : sans ça, treize notes font cinq secondes de
 * jeu et le score ne veut rien dire. */
static void test_la_partition_boucle_et_accelere(void)
{
    piano g;
    piano_reset(&g, 1, false);
    piano_press(&g, NS_GAME_ACTION);
    const float speed0 = g.speed;

    for (int i = 0; i < 120 * 60; ++i) {
        piano_autopilot(&g);
        piano_tick(&g, STEP);
        if (g.phase == PN_DEAD) break;
    }
    CHECK(g.loops > 0, "la partition a bouclé (%u tours)", g.loops);
    CHECK(g.speed > speed0, "et elle accélère (%.2f -> %.2f)",
          (double)speed0, (double)g.speed);
    CHECK(g.note_count <= PN_MAX_NOTES,
          "sans jamais déborder du tableau (%u)", g.note_count);
    CHECK(g.hits > 20, "et le joueur automatique tient (%u notes)", g.hits);
}

/* La borne « hard » démarre lancée, et trois oublis y terminent la partie. */
static void test_le_hardcore(void)
{
    piano n, h;
    piano_reset(&n, 1, false);
    piano_reset(&h, 1, true);
    CHECK(h.speed > n.speed, "la borne hard démarre plus vite (%.2f contre %.2f)",
          (double)h.speed, (double)n.speed);

    piano_press(&h, NS_GAME_ACTION);
    for (int i = 0; i < 120 * 60 && h.phase == PN_PLAYING; ++i) piano_tick(&h, STEP);
    CHECK(h.phase == PN_DEAD, "en hard, ne rien jouer finit la partie");
    CHECK(h.misses >= 3, "après trois notes manquées (%u)", h.misses);
}

/* -------------------------------------------------------------------------- */

typedef struct ledger { int64_t total; uint32_t dies; } ledger;

static void drain(piano *g, ledger *l)
{
    ns_game_events ev;
    SDL_zero(ev);
    g_piano_api.events(g, &ev);
    if (ev.score) {
        if (SDL_strcmp(ev.score_kind, "note") == 0) l->total += 5;
        else if (SDL_strcmp(ev.score_kind, "combo") == 0) l->total += 25;
        else l->total += 999999;
    }
    if (ev.die) l->dies++;
}

/* L'invariant central : le journal vaut le score. */
static void test_le_journal_vaut_le_score(void)
{
    for (uint64_t seed = 0; seed < 4; ++seed) {
        piano g;
        ledger l;
        memset(&l, 0, sizeof l);
        piano_reset(&g, 200u + seed, false);
        for (int i = 0; i < 120 * 90; ++i) {
            piano_autopilot(&g);
            piano_tick(&g, STEP);
            drain(&g, &l);
            if (l.dies) break;
        }
        for (int i = 0; i < 8; ++i) drain(&g, &l);
        if (l.total != g.score) {
            CHECK(false, "graine %llu : journal %lld, score %lld",
                  (unsigned long long)seed, (long long)l.total, (long long)g.score);
            return;
        }
    }
    CHECK(true, "quatre parties : le journal vaut le score, note pour note");
}

/* Toute partie finie l'annonce, une fois — construite plutôt qu'attendue : le
 * joueur automatique lit la partition sans faute et ne meurt pas. */
static void test_la_fin_est_annoncee(void)
{
    piano g;
    ledger l;
    memset(&l, 0, sizeof l);
    piano_reset(&g, 5, false);
    piano_press(&g, NS_GAME_ACTION);

    /* Quelques notes jouées, pour que la file ne soit pas vide au moment de la
     * faute : c'est ce cas-là qui vérifie que `die` attend son tour. */
    int played = 0;
    for (int i = 0; i < 120 * 60 && played < 3; ++i) {
        for (int lane = 0; lane < PN_LANES; ++lane) {
            if (piano_note_at(&g, lane) < 0) continue;
            piano_press(&g, LANE_KEY[lane]);
            played++;
            break;
        }
        piano_tick(&g, STEP);
    }
    /* La fausse note : une voie qu'on sait vide. */
    int empty = -1;
    for (int lane = 0; lane < PN_LANES; ++lane) if (piano_note_at(&g, lane) < 0) empty = lane;
    CHECK(empty >= 0, "une voie vide existe");
    piano_press(&g, LANE_KEY[empty]);
    CHECK(g.phase == PN_DEAD, "la fausse note termine");

    for (int i = 0; i < 10; ++i) drain(&g, &l);
    CHECK(l.dies == 1, "la fin est annoncée une fois (%u)", l.dies);
    CHECK(l.total == g.score, "et après la file de gains (%lld / %lld)",
          (long long)l.total, (long long)g.score);
}

static void test_determinisme(void)
{
    piano a, b;
    ledger la, lb;
    memset(&la, 0, sizeof la);
    memset(&lb, 0, sizeof lb);
    piano_reset(&a, 999, false);
    piano_reset(&b, 999, false);
    for (int i = 0; i < 120 * 45; ++i) {
        piano_autopilot(&a); piano_tick(&a, STEP); drain(&a, &la);
        piano_autopilot(&b); piano_tick(&b, STEP); drain(&b, &lb);
    }
    CHECK(a.score == b.score && a.hits == b.hits,
          "même graine, même partie (%lld / %lld)", (long long)a.score, (long long)b.score);
}

static void test_le_vocabulaire(void)
{
    piano g;
    piano_reset(&g, 4242, false);
    const char *seen[8] = { 0 };
    int count = 0;
    for (int i = 0; i < 120 * 90; ++i) {
        piano_autopilot(&g);
        piano_tick(&g, STEP);
        ns_game_events ev;
        SDL_zero(ev);
        g_piano_api.events(&g, &ev);
        if (ev.score) {
            bool known = false;
            for (int j = 0; j < count; ++j)
                if (SDL_strcmp(seen[j], ev.score_kind) == 0) known = true;
            if (!known && count < 8) seen[count++] = ev.score_kind;
        }
        if (ev.die) break;
    }
    for (int i = 0; i < count; ++i) {
        bool declared = false;
        for (const char *const *k = g_piano_api.event_kinds; *k; ++k)
            if (SDL_strcmp(*k, seen[i]) == 0) declared = true;
        CHECK(declared, "« %s » est émis : il doit être déclaré", seen[i]);
    }
    CHECK(count >= 2, "note et combo sont émis (%d types)", count);
}

int main(void)
{
    test_la_partition();
    test_la_fausse_note_termine();
    test_le_bareme();
    test_l_oubli_ne_tue_pas();
    test_ne_rien_jouer_finit_par_tuer();
    test_la_partition_boucle_et_accelere();
    test_le_hardcore();
    test_le_journal_vaut_le_score();
    test_la_fin_est_annoncee();
    test_determinisme();
    test_le_vocabulaire();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
