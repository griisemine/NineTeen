/*
 * test_shooter.c — les tables d'abord.
 *
 * Ce qui se perd en portant un shoot'em up, ce ne sont pas les mécanismes,
 * c'est un CHIFFRE dans une table : un rayon, un rechargement, une rafale. Le
 * jeu continue de tourner, il devient seulement injouable — et lentement,
 * discrètement. Ce fichier a d'ailleurs déjà servi à ça : la demi-largeur du
 * missile ennemi de base vaut `12.5 * RATIO_SIZE_MISSILE_3`, et le ratio vaut
 * 0,4. Recopier 12,5 en oubliant le 0,4 fait des balles deux fois et demie trop
 * grosses, ce qu'aucune adresse ne rattrape.
 *
 * L'autre chose vérifiée ici : le barème. `rulesTable["shooter"]` déclare
 * `scaled{"enemy": 15}` depuis M6, et la VALEUR d'un ennemi est ses points de
 * vie. C'est la seule table du serveur que le portage n'a pas eu à changer, et
 * ça se vérifie.
 */
#include "games.h"
#include "ns_core.h"
#include "shooter.h"

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

static void hold_none(shooter *g)
{
    bool h[NS_GAME_BUTTON_COUNT];
    memset(h, 0, sizeof h);
    shooter_hold(g, h);
}

/* -------------------------------------------------------------------------- */

static void test_depart(void)
{
    shooter g;
    shooter_reset(&g, 1234, false);

    CHECK(g.phase == SH_READY, "la partie commence en sursis");
    CHECK(g.score == 0, "le score part de zéro");
    CHECK(g.weapons == 1, "une seule arme au départ (%d)", g.weapons);
    CHECK(g.ammo_kind == SH_ALLY_BASE, "et le canon de base");
    CHECK(fabsf(g.ship_x - SH_W * 0.5f) < 0.5f,
          "le vaisseau part au milieu du couloir (%.1f)", (double)g.ship_x);
    CHECK(g.ship_y > SH_H * 0.7f, "et en bas (%.1f)", (double)g.ship_y);
    CHECK(shooter_live_enemies(&g) == 0, "le couloir est vide au départ");
}

/*
 * `WEAPON_DISPOSITION` : selon le NOMBRE d'armes, lesquels des cinq
 * emplacements s'allument. Deux canons ne sont pas les deux premiers mais le
 * deuxième et le quatrième — donc symétriques autour de la coque. C'est un
 * détail qu'un portage « simplifié » perd, et il se voit tout de suite.
 */
static void test_la_disposition_des_armes(void)
{
    for (int n = 1; n <= SH_MAX_WEAPONS; ++n) {
        int slots[SH_MAX_WEAPONS];
        const int got = shooter_weapon_slots(n, slots);
        CHECK(got == n, "%d arme(s) allument %d emplacement(s)", n, got);

        /* Aucun emplacement en double, et tous dans les bornes. */
        for (int i = 0; i < got; ++i) {
            CHECK(slots[i] >= 0 && slots[i] < SH_MAX_WEAPONS,
                  "l'emplacement %d est valide (%d)", i, slots[i]);
            for (int j = i + 1; j < got; ++j)
                CHECK(slots[i] != slots[j], "pas deux fois le même emplacement (%d)", slots[i]);
        }
    }

    /* Les cas nommés de la table de 2020. */
    int slots[SH_MAX_WEAPONS];
    shooter_weapon_slots(1, slots);
    CHECK(slots[0] == 2, "une arme part du canon CENTRAL (%d)", slots[0]);
    shooter_weapon_slots(2, slots);
    CHECK((slots[0] == 1 && slots[1] == 3),
          "deux armes sont symétriques, pas côte à côte (%d, %d)", slots[0], slots[1]);

    /* Un nombre hors bornes ne fait pas déborder. */
    CHECK(shooter_weapon_slots(0, slots) == 1, "zéro arme retombe sur une");
    CHECK(shooter_weapon_slots(99, slots) == SH_MAX_WEAPONS, "quatre-vingt-dix-neuf sur cinq");
}

/* Le tir : autant de missiles que d'emplacements allumés, et à des abscisses
 * différentes. */
static void test_le_tir(void)
{
    shooter g;
    shooter_reset(&g, 3, false);
    shooter_press(&g, NS_GAME_ACTION);

    int shots = 0;
    for (int i = 0; i < SH_MAX_SHOTS; ++i) if (g.shot[i].alive) shots++;
    CHECK(shots == 1, "une arme tire un missile (%d)", shots);

    shooter_reset(&g, 3, false);
    g.weapons = 5;
    shooter_press(&g, NS_GAME_ACTION);
    shots = 0;
    float xs[SH_MAX_WEAPONS];
    for (int i = 0; i < SH_MAX_SHOTS; ++i)
        if (g.shot[i].alive && shots < SH_MAX_WEAPONS) xs[shots++] = g.shot[i].x;
    CHECK(shots == 5, "cinq armes tirent cinq missiles (%d)", shots);
    int distinct = 0;
    for (int i = 0; i < shots; ++i) {
        bool same = false;
        for (int j = 0; j < i; ++j) if (fabsf(xs[i] - xs[j]) < 0.5f) same = true;
        if (!same) distinct++;
    }
    CHECK(distinct == shots, "et depuis cinq canons distincts (%d)", distinct);

    /* La cadence : deux appuis d'affilée ne tirent qu'une fois. */
    shooter_reset(&g, 3, false);
    shooter_press(&g, NS_GAME_ACTION);
    shooter_press(&g, NS_GAME_ACTION);
    shots = 0;
    for (int i = 0; i < SH_MAX_SHOTS; ++i) if (g.shot[i].alive) shots++;
    CHECK(shots == 1, "la cadence tient (%d missiles)", shots);
}

/*
 * Le barème : quinze points par point de vie. La table du serveur l'écrit
 * depuis M6, et c'est la valeur qui voyage — pas les points.
 */
static void test_le_bareme(void)
{
    static const float HP[SH_ENEMY_KINDS] = { 1.0f, 7.0f, 25.0f, 40.0f, 90.0f };

    for (int kind = 0; kind < SH_ENEMY_KINDS; ++kind) {
        shooter g;
        shooter_reset(&g, 5, false);
        g.phase = SH_PLAYING;
        g.wave_timer = 1e6f;   /* pas de vague pendant la mesure */

        /* Un ennemi posé, et un missile qui l'achève. */
        g.enemy[0].alive = true;
        g.enemy[0].kind = kind;
        g.enemy[0].hp = 0.5f;
        g.enemy[0].hp_max = HP[kind];
        g.enemy[0].x = SH_W * 0.5f;
        g.enemy[0].y = SH_H * 0.30f;
        g.enemy[0].target_y = -1.0f;

        g.shot[0].alive = true;
        g.shot[0].x = g.enemy[0].x;
        g.shot[0].y = g.enemy[0].y;
        g.shot[0].vy = -100.0f;
        g.shot[0].radius = 6.0f;
        g.shot[0].damage = 10.0f;
        g.shot[0].life = 1.0f;
        g.shot[0].hostile = false;

        const int64_t before = g.score;
        hold_none(&g);
        shooter_tick(&g, STEP);

        const int64_t want = (int64_t)(HP[kind] + 0.5f) * 15
                           + ((kind == 4) ? 2000 : 0);
        CHECK(g.score - before == want,
              "l'ennemi %d (%.0f PV) vaut %lld points (%lld)",
              kind, (double)HP[kind], (long long)want, (long long)(g.score - before));
        CHECK(!g.enemy[0].alive, "et il est détruit");
    }
}

/* Abattre le boss améliore l'armement : c'est la récompense qui donne envie
 * d'y retourner. */
static void test_le_boss_recompense(void)
{
    shooter g;
    shooter_reset(&g, 7, false);
    g.phase = SH_PLAYING;
    g.wave_timer = 1e6f;
    const int weapons = g.weapons, ammo = g.ammo_kind;

    g.enemy[0].alive = true;
    g.enemy[0].kind = 4;
    g.enemy[0].hp = 0.5f;
    g.enemy[0].hp_max = 90.0f;
    g.enemy[0].x = SH_W * 0.5f;
    g.enemy[0].y = SH_H * 0.20f;
    g.boss_alive = true;

    g.shot[0].alive = true;
    g.shot[0].x = g.enemy[0].x;
    g.shot[0].y = g.enemy[0].y;
    g.shot[0].vy = -100.0f;
    g.shot[0].radius = 6.0f;
    g.shot[0].damage = 10.0f;
    g.shot[0].life = 1.0f;

    hold_none(&g);
    shooter_tick(&g, STEP);

    CHECK(!g.boss_alive, "le boss est abattu");
    CHECK(g.weapons == weapons + 1, "et donne une arme de plus (%d)", g.weapons);
    CHECK(g.ammo_kind == ammo + 1, "et un missile meilleur (%d)", g.ammo_kind);
}

/* Un tir ennemi tue, et l'invincibilité de départ protège — un temps. */
static void test_la_mort(void)
{
    shooter g;
    shooter_reset(&g, 9, false);
    g.phase = SH_PLAYING;
    g.wave_timer = 1e6f;

    /* Pendant l'invincibilité de départ, un tir ne tue pas. */
    CHECK(g.invuln > 0.0f, "le vaisseau part invincible (%.2f s)", (double)g.invuln);
    g.shot[0].alive = true;
    g.shot[0].hostile = true;
    g.shot[0].x = g.ship_x;
    g.shot[0].y = g.ship_y;
    g.shot[0].vy = 100.0f;
    g.shot[0].radius = 5.0f;
    g.shot[0].life = 1.0f;
    hold_none(&g);
    shooter_tick(&g, STEP);
    CHECK(g.phase == SH_PLAYING, "et le tir ne le tue pas");

    /* Une fois l'invincibilité passée, si. */
    for (int i = 0; i < 120 * 2; ++i) shooter_tick(&g, STEP);
    CHECK(g.invuln <= 0.0f, "l'invincibilité expire");
    g.shot[1].alive = true;
    g.shot[1].hostile = true;
    g.shot[1].x = g.ship_x;
    g.shot[1].y = g.ship_y;
    g.shot[1].vy = 100.0f;
    g.shot[1].radius = 5.0f;
    g.shot[1].life = 1.0f;
    shooter_tick(&g, STEP);
    CHECK(g.phase == SH_DEAD, "et le tir suivant tue");

    const int64_t s = g.score;
    shooter_tick(&g, STEP);
    CHECK(g.score == s, "une partie finie ne se joue plus");
}

/* Le vaisseau reste dans le couloir : c'est la contrainte du terrain étroit. */
static void test_les_bords(void)
{
    shooter g;
    shooter_reset(&g, 11, false);
    g.phase = SH_PLAYING;
    g.wave_timer = 1e6f;

    bool h[NS_GAME_BUTTON_COUNT];
    memset(h, 0, sizeof h);
    h[NS_GAME_LEFT] = true;
    shooter_hold(&g, h);
    for (int i = 0; i < 120 * 6; ++i) shooter_tick(&g, STEP);
    CHECK(g.ship_x >= 0.0f && g.ship_x < SH_W * 0.2f,
          "poussé à gauche, il s'arrête au bord (%.1f)", (double)g.ship_x);

    memset(h, 0, sizeof h);
    h[NS_GAME_RIGHT] = true;
    shooter_hold(&g, h);
    for (int i = 0; i < 120 * 6; ++i) shooter_tick(&g, STEP);
    CHECK(g.ship_x <= SH_W && g.ship_x > SH_W * 0.8f,
          "et pareil à droite (%.1f)", (double)g.ship_x);
}

/* Les vagues arrivent, et le boss revient tous les cinq tours. */
static void test_les_vagues(void)
{
    shooter g;
    shooter_reset(&g, 13, false);
    g.phase = SH_PLAYING;
    hold_none(&g);

    /* On fait tourner longtemps en tuant tout, pour voir arriver les vagues. */
    bool saw_boss = false;
    for (int i = 0; i < 120 * 120; ++i) {
        shooter_tick(&g, STEP);
        for (int k = 0; k < SH_MAX_ENEMIES; ++k) {
            if (!g.enemy[k].alive) continue;
            if (g.enemy[k].kind == 4) saw_boss = true;
            /* On les efface sans passer par le score : ce test porte sur les
             * vagues. */
            g.enemy[k].alive = false;
            if (g.enemy[k].kind == 4) g.boss_alive = false;
        }
        for (int k = 0; k < SH_MAX_SHOTS; ++k) g.shot[k].alive = false;
        g.invuln = 1.0f;
        if (g.wave > 12u) break;
    }
    CHECK(g.wave > 5u, "les vagues s'enchaînent (%u)", g.wave);
    CHECK(saw_boss, "et un boss est apparu");
}

/* -------------------------------------------------------------------------- */

typedef struct ledger { int64_t total; uint32_t shots, dies; } ledger;

static void drain(shooter *g, ledger *l)
{
    ns_game_events ev;
    SDL_zero(ev);
    g_shooter_api.events(g, &ev);
    if (ev.blip) {
        l->shots++;
        if (SDL_strcmp(ev.blip_kind, "shot") != 0) l->total += 999999;
    }
    if (ev.score) {
        /* Le serveur : quinze points par point de vie, 2 000 le boss, 300 la
         * vague. On recalcule comme lui. */
        if (SDL_strcmp(ev.score_kind, "enemy") == 0) l->total += 15 * ev.score_value;
        else if (SDL_strcmp(ev.score_kind, "boss") == 0) l->total += 2000;
        else if (SDL_strcmp(ev.score_kind, "wave") == 0) l->total += 300;
        else l->total += 999999;
    }
    if (ev.die) l->dies++;
}

static void play(shooter *g, uint64_t seed, ledger *l, float *seconds)
{
    memset(l, 0, sizeof *l);
    shooter_reset(g, seed, false);
    float t = 0.0f;
    for (int i = 0; i < 120 * 180 && l->dies == 0; ++i) {
        shooter_autopilot(g);
        shooter_tick(g, STEP);
        t += STEP;
        drain(g, l);
    }
    if (seconds) *seconds = t;
}

/* L'invariant central : le journal vaut le score. */
static void test_le_journal_vaut_le_score(void)
{
    for (uint64_t seed = 0; seed < 8; ++seed) {
        shooter g;
        ledger l;
        play(&g, 600u + seed, &l, NULL);
        if (l.total != g.score) {
            CHECK(false, "graine %llu : le serveur recalculerait %lld pour %lld affichés",
                  (unsigned long long)seed, (long long)l.total, (long long)g.score);
            return;
        }
    }
    CHECK(true, "huit parties : le journal vaut le score, point de vie par point de vie");
}

static void test_la_fin_est_annoncee(void)
{
    int silent = 0, deaths = 0;
    for (uint64_t seed = 0; seed < 8; ++seed) {
        shooter g;
        ledger l;
        play(&g, 1100u + seed, &l, NULL);
        if (g.phase == SH_DEAD) { deaths++; if (l.dies != 1) silent++; }
    }
    CHECK(deaths > 0, "le joueur automatique finit par tomber (%d fois sur 8)", deaths);
    CHECK(silent == 0, "et chaque fin est annoncée une fois (%d muettes)", silent);
}

/* La cadence de tir de `rulesTable["shooter"]` : 40/s. Le joueur automatique
 * tire en continu ; s'il dépasse, toute partie capturée est refusée. */
static void test_les_cadences_du_serveur(void)
{
    for (uint64_t seed = 0; seed < 4; ++seed) {
        shooter g;
        ledger l;
        float sec = 0.0f;
        play(&g, 1700u + seed, &l, &sec);
        if (sec < 2.0f) continue;
        const double rate = (double)l.shots / (double)sec;
        if (rate > 40.0) {
            CHECK(false, "graine %llu : %.1f tir/s", (unsigned long long)seed, rate);
            return;
        }
    }
    CHECK(true, "quatre parties automatiques tiennent la cadence de tir");
}

static void test_determinisme(void)
{
    shooter a, b;
    ledger la, lb;
    play(&a, 2468, &la, NULL);
    play(&b, 2468, &lb, NULL);
    CHECK(a.score == b.score && a.killed == b.killed,
          "même graine, même partie (%lld / %lld)", (long long)a.score, (long long)b.score);

    shooter c;
    ledger lc;
    play(&c, 2469, &lc, NULL);
    CHECK(c.score != a.score || c.killed != a.killed,
          "une autre graine donne une autre partie");
}

/* Rien ne dérive, rien ne fuit hors des tableaux. */
static void test_rien_ne_derive(void)
{
    shooter g;
    ledger l;
    play(&g, 4242, &l, NULL);

    CHECK(g.ship_x == g.ship_x && g.ship_y == g.ship_y, "le vaisseau n'est pas NaN");
    CHECK(g.ship_x >= -1.0f && g.ship_x <= SH_W + 1.0f, "il est dans le couloir");
    int stray = 0;
    for (int i = 0; i < SH_MAX_SHOTS; ++i) {
        if (!g.shot[i].alive) continue;
        if (g.shot[i].x != g.shot[i].x || g.shot[i].y != g.shot[i].y) stray++;
    }
    for (int i = 0; i < SH_MAX_ENEMIES; ++i) {
        if (!g.enemy[i].alive) continue;
        if (g.enemy[i].hp > g.enemy[i].hp_max + 1e-3f) stray++;
    }
    CHECK(stray == 0, "aucun objet en dérive (%d)", stray);
}

/* Le joueur automatique doit ABATTRE : sans ça, le barème n'est jamais
 * exercé. Il ne survit pas longtemps — un couloir de six cent quarante pixels
 * sous un tir en rafale est un jeu dur, et c'est celui de 2020 — mais il
 * marque. */
static void test_le_robot_abat(void)
{
    uint32_t killed = 0;
    float total = 0.0f;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        shooter g;
        ledger l;
        float sec = 0.0f;
        play(&g, 2600u + seed, &l, &sec);
        killed += g.killed;
        total += sec;
    }
    CHECK(killed > 20, "six parties abattent %u ennemis", killed);
    CHECK(total / 6.0f > 5.0f, "et durent %.1f s en moyenne", (double)(total / 6.0f));
}

static void test_le_vocabulaire(void)
{
    shooter g;
    shooter_reset(&g, 5150, false);
    const char *seen[8] = { 0 };
    int count = 0;
    for (int i = 0; i < 120 * 180; ++i) {
        shooter_autopilot(&g);
        shooter_tick(&g, STEP);
        ns_game_events ev;
        SDL_zero(ev);
        g_shooter_api.events(&g, &ev);
        const char *kinds[2] = { ev.blip ? ev.blip_kind : NULL,
                                 ev.score ? ev.score_kind : NULL };
        for (int k = 0; k < 2; ++k) {
            if (!kinds[k]) continue;
            bool known = false;
            for (int j = 0; j < count; ++j) if (SDL_strcmp(seen[j], kinds[k]) == 0) known = true;
            if (!known && count < 8) seen[count++] = kinds[k];
        }
        if (ev.die) break;
    }
    for (int i = 0; i < count; ++i) {
        bool declared = false;
        for (const char *const *k = g_shooter_api.event_kinds; *k; ++k)
            if (SDL_strcmp(*k, seen[i]) == 0) declared = true;
        CHECK(declared, "« %s » est émis : il doit être déclaré", seen[i]);
    }
    CHECK(count >= 2, "au moins shot et enemy sont émis (%d types)", count);
}

int main(void)
{
    test_depart();
    test_la_disposition_des_armes();
    test_le_tir();
    test_le_bareme();
    test_le_boss_recompense();
    test_la_mort();
    test_les_bords();
    test_les_vagues();
    test_le_journal_vaut_le_score();
    test_la_fin_est_annoncee();
    test_les_cadences_du_serveur();
    test_determinisme();
    test_rien_ne_derive();
    test_le_robot_abat();
    test_le_vocabulaire();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
