/*
 * test_asteroid.c — les règles d'Asteroid, confrontées à celles de 2020.
 *
 * Ce qui mérite un test ici, ce sont les tables : six variétés d'astéroïdes
 * valant de 50 à 500, quatre quartiers de taille qui multiplient ce score par
 * 0,2 à 1, cinq types de missiles avec leurs fréquences, vitesses, dégâts et
 * durées. Une valeur recopiée de travers ne se voit pas — elle se joue.
 *
 * Et l'invariant que le Démineur a rendu explicite : le score affiché doit
 * valoir exactement ce que le serveur recalcule à partir du journal.
 */
#include "asteroid.h"
#include "games.h"
#include "ns_core.h"

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

/* L'écart entre deux angles, modulo 2π. */
static float wrapped(float a)
{
    while (a > NS_PI)  a -= 2.0f * NS_PI;
    while (a < -NS_PI) a += 2.0f * NS_PI;
    return a;
}

static void hold_none(asteroid *g)
{
    bool h[NS_GAME_BUTTON_COUNT];
    memset(h, 0, sizeof h);
    asteroid_hold(g, h);
}

static void hold_one(asteroid *g, ns_game_button b)
{
    bool h[NS_GAME_BUTTON_COUNT];
    memset(h, 0, sizeof h);
    h[b] = true;
    asteroid_hold(g, h);
}

/* -------------------------------------------------------------------------- */

static void test_depart(void)
{
    asteroid g;
    asteroid_reset(&g, 1234, false);

    CHECK(g.phase == AST_READY, "la partie commence en sursis");
    CHECK(g.score == 0, "le score part de zéro");
    CHECK(fabsf(g.ship_x - AST_W * 0.5f) < 0.5f && fabsf(g.ship_y - AST_H * 0.5f) < 0.5f,
          "le vaisseau est au centre du terrain (%.1f, %.1f)",
          (double)g.ship_x, (double)g.ship_y);
    /* BASE_ANGLE vaut 3π/2, normalisé dans (−π, π] : −π/2, le même cap. */
    CHECK(fabsf(g.ship_angle + NS_PI / 2.0f) < 1e-4f,
          "il regarde vers le haut, comme BASE_ANGLE (%.4f)", (double)g.ship_angle);
    CHECK(g.multi == 1 && g.shot_kind == 0, "arme de base, tir simple");

    /* Le champ de départ : trois astéroïdes, comme les trois `coord_spawn`. */
    CHECK(asteroid_live_rocks(&g) == 3,
          "trois astéroïdes sont déjà là (%d)", asteroid_live_rocks(&g));

    /* Et aucun n'est posé sur le vaisseau : DIST_VAISSEAU_ASTEROID vaut 300. */
    int too_close = 0;
    for (int i = 0; i < AST_MAX_ROCKS; ++i) {
        if (!g.rock[i].alive) continue;
        const float dx = g.rock[i].x - g.ship_x, dy = g.rock[i].y - g.ship_y;
        if (dx * dx + dy * dy < 300.0f * 300.0f) too_close++;
    }
    CHECK(too_close == 0, "aucun n'apparaît à moins de 300 px du vaisseau (%d)", too_close);
}

/*
 * Le barème d'un astéroïde : sa VARIÉTÉ donne la base (50 à 500), son QUARTIER
 * de taille le multiplie (0,2 / 0,4 / 0,8 / 1). C'est ce qui fait qu'un petit
 * fragment d'une variété rare vaut plus qu'un gros caillou commun — et c'est la
 * première chose qu'on simplifierait par mégarde.
 */
static void test_le_bareme_des_asteroides(void)
{
    static const int base[6] = { 50, 100, 200, 300, 400, 500 };
    static const float mult[4] = { 0.2f, 0.4f, 0.8f, 1.0f };

    for (int kind = 0; kind < 6; ++kind) {
        for (int slice = 0; slice < 4; ++slice) {
            ast_rock r;
            SDL_zero(r);
            r.kind = kind;
            /* Un diamètre au milieu du quartier : 90 px de large, quatre
             * tranches, donc les centres tombent à 11,25 / 33,75 / 56,25 / 78,75. */
            const float d = 90.0f * ((float)slice + 0.5f) / 4.0f;
            r.radius = d * 0.5f;
            const int64_t want = (int64_t)((float)base[kind] * mult[slice] + 0.5f);
            const int64_t got = asteroid_rock_score(&r);
            if (got != want) {
                CHECK(false, "variété %d quartier %d : %lld au lieu de %lld",
                      kind, slice, (long long)got, (long long)want);
                return;
            }
        }
    }
    CHECK(true, "les vingt-quatre combinaisons de variété et de taille sont justes");

    /* Un petit fragment rare bat un gros caillou commun : c'est la règle. */
    ast_rock rare = { 0 }, common = { 0 };
    rare.kind = 5;   rare.radius = 90.0f * 0.125f * 0.5f;   /* premier quartier */
    common.kind = 0; common.radius = 90.0f * 0.875f * 0.5f; /* dernier quartier */
    CHECK(asteroid_rock_score(&rare) > asteroid_rock_score(&common),
          "un petit fragment rare (%lld) vaut plus qu'un gros commun (%lld)",
          (long long)asteroid_rock_score(&rare), (long long)asteroid_rock_score(&common));
}

/* La physique du vaisseau : la rotation monte par une rampe de neuf images, la
 * poussée par une rampe de cinq. Sans elles il se pilote comme un curseur. */
static void test_les_rampes(void)
{
    asteroid a, b;
    asteroid_reset(&a, 7, false);
    asteroid_reset(&b, 7, false);
    asteroid_press(&a, NS_GAME_ACTION);
    asteroid_press(&b, NS_GAME_ACTION);
    const float a0 = a.ship_angle;

    /*
     * Une image de rotation, contre neuf. Les écarts se mesurent MODULO 2π :
     * l'angle est normalisé, et comparer deux angles bruts de part et d'autre
     * d'un tour donne 6,28 rad de « rotation » là où il n'y en a aucune. Le
     * premier jet de ce test s'y est fait prendre.
     */
    hold_one(&a, NS_GAME_RIGHT);
    asteroid_tick(&a, 1.0f / 30.0f);
    const float one = fabsf(wrapped(a.ship_angle - a0));

    hold_one(&b, NS_GAME_RIGHT);
    for (int i = 0; i < 9; ++i) asteroid_tick(&b, 1.0f / 30.0f);
    const float nine = fabsf(wrapped(b.ship_angle - a0));

    CHECK(one > 0.0f, "une image de maintien tourne un peu (%.4f)", (double)one);
    CHECK(nine > one * 9.0f * 0.5f,
          "neuf images tournent bien plus que neuf fois la première (%.4f contre %.4f)",
          (double)nine, (double)(one * 9.0f));
    CHECK(nine < 9.0f * 0.13f + 1e-3f,
          "et jamais plus que neuf fois TURN_AMMOUNT (%.4f)", (double)nine);

    /* Relâcher remet la rampe à zéro : c'est ce qui rend une correction fine
     * possible. */
    hold_none(&b);
    asteroid_tick(&b, 0.2f);
    const float before = b.ship_angle;
    hold_one(&b, NS_GAME_RIGHT);
    asteroid_tick(&b, 1.0f / 30.0f);
    CHECK(fabsf(wrapped(b.ship_angle - before)) < one * 1.5f,
          "après un relâchement, la rotation repart doucement (%.4f)",
          (double)fabsf(wrapped(b.ship_angle - before)));
}

/* La décélération : sans poussée, le vaisseau s'arrête — lentement. */
static void test_la_deceleration(void)
{
    asteroid g;
    asteroid_reset(&g, 3, false);
    asteroid_press(&g, NS_GAME_ACTION);

    hold_one(&g, NS_GAME_UP);
    for (int i = 0; i < 60; ++i) asteroid_tick(&g, STEP);
    const float v0 = sqrtf(g.ship_vx * g.ship_vx + g.ship_vy * g.ship_vy);
    CHECK(v0 > 10.0f, "la poussée accélère (%.1f px/s)", (double)v0);

    hold_none(&g);
    for (int i = 0; i < 120; ++i) asteroid_tick(&g, STEP);
    const float v1 = sqrtf(g.ship_vx * g.ship_vx + g.ship_vy * g.ship_vy);
    CHECK(v1 < v0, "et il ralentit sans elle (%.1f -> %.1f)", (double)v0, (double)v1);
    /* `DECELERATION 1.015` par image : en une seconde, v est divisée par
     * 1,015^30 = 1,563. */
    CHECK(fabsf(v1 - v0 / 1.563f) < v0 * 0.08f,
          "au taux de 2020 : 1,015 par image (%.1f, attendu %.1f)",
          (double)v1, (double)(v0 / 1.563f));

    /* Et la vitesse est plafonnée à VITESSE = 10 px/image = 300 px/s. */
    hold_one(&g, NS_GAME_UP);
    for (int i = 0; i < 120 * 20; ++i) asteroid_tick(&g, STEP);
    const float v2 = sqrtf(g.ship_vx * g.ship_vx + g.ship_vy * g.ship_vy);
    CHECK(v2 <= 300.0f + 1.0f, "la vitesse ne dépasse pas 300 px/s (%.1f)", (double)v2);
}

/* Le vaisseau REBONDIT sur les murs, il ne s'enroule pas. C'est la règle de
 * 2020, et ce n'est pas celle d'un Asteroids classique. */
static void test_le_vaisseau_rebondit(void)
{
    asteroid g;
    asteroid_reset(&g, 3, false);
    asteroid_press(&g, NS_GAME_ACTION);
    g.ship_angle = 0.0f;              /* plein est */
    hold_one(&g, NS_GAME_UP);
    for (int i = 0; i < 120 * 20; ++i) asteroid_tick(&g, STEP);

    CHECK(g.ship_x <= AST_W && g.ship_x >= 0.0f,
          "il reste dans le terrain (%.1f)", (double)g.ship_x);
    CHECK(g.ship_x > AST_W * 0.5f,
          "et il est allé au bout, pas revenu par l'autre bord (%.1f)", (double)g.ship_x);
}

/* La fragmentation : au-dessus de TAILLE_MIN_SPLIT, détruire donne deux
 * fragments. En dessous, rien. */
static void test_la_fragmentation(void)
{
    asteroid g;
    asteroid_reset(&g, 11, false);
    for (int i = 0; i < AST_MAX_ROCKS; ++i) g.rock[i].alive = false;

    /* Un gros : il se casse. */
    g.rock[0].alive = true;
    g.rock[0].x = AST_W * 0.5f; g.rock[0].y = 100.0f;
    g.rock[0].vx = 0.0f; g.rock[0].vy = 60.0f;
    g.rock[0].radius = 40.0f;   /* 80 px de diamètre, bien au-dessus de 36 */
    g.rock[0].hp = 0.1f;
    g.rock[0].kind = 2;

    g.phase = AST_PLAYING;
    g.shot[0].alive = true;
    g.shot[0].x = g.rock[0].x; g.shot[0].y = g.rock[0].y;
    g.shot[0].vx = 0.0f; g.shot[0].vy = 1.0f;
    g.shot[0].radius = 6.0f; g.shot[0].damage = 10.0f;
    g.shot[0].life = 1.0f; g.shot[0].kind = 0;
    hold_none(&g);
    asteroid_tick(&g, STEP);

    CHECK(asteroid_live_rocks(&g) == 2, "un gros astéroïde en donne deux (%d)",
          asteroid_live_rocks(&g));
    CHECK(g.score > 0, "et il rapporte (%lld)", (long long)g.score);

    /* Un petit : il disparaît. */
    asteroid_reset(&g, 11, false);
    for (int i = 0; i < AST_MAX_ROCKS; ++i) g.rock[i].alive = false;
    g.rock[0].alive = true;
    g.rock[0].x = AST_W * 0.5f; g.rock[0].y = 100.0f;
    g.rock[0].radius = 10.0f;   /* 20 px, sous les 36 */
    g.rock[0].hp = 0.1f;
    g.phase = AST_PLAYING;
    g.shot[0].alive = true;
    g.shot[0].x = g.rock[0].x; g.shot[0].y = g.rock[0].y;
    g.shot[0].radius = 6.0f; g.shot[0].damage = 10.0f;
    g.shot[0].life = 1.0f;
    asteroid_tick(&g, STEP);
    CHECK(asteroid_live_rocks(&g) == 0, "un petit astéroïde ne se casse pas (%d)",
          asteroid_live_rocks(&g));
}

/* La glace ne fait AUCUN dégât — `DEGAT_MISSILES[3]` vaut zéro — elle gèle.
 * C'est la règle la plus surprenante de l'arsenal de 2020. */
static void test_la_glace_gele_sans_casser(void)
{
    asteroid g;
    asteroid_reset(&g, 13, false);
    for (int i = 0; i < AST_MAX_ROCKS; ++i) g.rock[i].alive = false;
    g.phase = AST_PLAYING;

    g.rock[0].alive = true;
    g.rock[0].x = 500.0f; g.rock[0].y = 300.0f;
    g.rock[0].vx = 200.0f; g.rock[0].vy = 0.0f;
    g.rock[0].radius = 30.0f;
    g.rock[0].hp = 1.0f;

    g.shot[0].alive = true;
    g.shot[0].x = 500.0f; g.shot[0].y = 300.0f;
    g.shot[0].radius = 10.0f; g.shot[0].damage = 0.0f;
    g.shot[0].life = 1.0f; g.shot[0].kind = AST_SHOT_ICE;

    hold_none(&g);
    asteroid_tick(&g, STEP);

    CHECK(g.rock[0].alive, "l'astéroïde survit à la glace");
    CHECK(g.rock[0].frozen > 0.0f, "mais il est gelé (%.2f s)", (double)g.rock[0].frozen);

    /* On relève la position APRÈS l'impact : l'astéroïde a légitimement avancé
     * pendant l'image où il a été touché, le gel ne prend effet qu'ensuite. */
    const float x0 = g.rock[0].x;
    for (int i = 0; i < 60; ++i) asteroid_tick(&g, STEP);
    CHECK(fabsf(g.rock[0].x - x0) < 1.0f,
          "et un astéroïde gelé ne bouge plus (%.2f -> %.2f)", (double)x0, (double)g.rock[0].x);
}

/* Le bouclier absorbe une collision, une seule. */
static void test_le_bouclier(void)
{
    asteroid g;
    asteroid_reset(&g, 17, false);
    for (int i = 0; i < AST_MAX_ROCKS; ++i) g.rock[i].alive = false;
    g.phase = AST_PLAYING;
    g.shield = 5.0f;

    g.rock[0].alive = true;
    g.rock[0].x = g.ship_x; g.rock[0].y = g.ship_y;
    g.rock[0].radius = 20.0f;
    g.rock[0].hp = 1.0f;

    hold_none(&g);
    asteroid_tick(&g, STEP);
    CHECK(g.phase == AST_PLAYING, "le bouclier encaisse");
    CHECK(g.shield <= 0.0f, "et il est consommé (%.2f)", (double)g.shield);

    /* Le suivant tue. */
    g.rock[1].alive = true;
    g.rock[1].x = g.ship_x; g.rock[1].y = g.ship_y;
    g.rock[1].radius = 20.0f;
    g.rock[1].hp = 1.0f;
    asteroid_tick(&g, STEP);
    CHECK(g.phase == AST_DEAD, "le choc suivant tue");
}

/* Le tir normal est GRATUIT : `MUNITIONS_USAGE[0]` vaut zéro. Sans arme de
 * repli, une jauge vide serait une mort annoncée. */
static void test_le_tir_normal_est_inepuisable(void)
{
    asteroid g;
    asteroid_reset(&g, 19, false);
    asteroid_press(&g, NS_GAME_ACTION);
    /* Terrain vide et apparitions repoussées : ce test porte sur les MUNITIONS,
     * pas sur la survie. Le premier jet laissait le vaisseau immobile au milieu
     * du champ trente secondes durant — il mourait, `tick` sortait aussitôt, et
     * le test mesurait le silence d'une partie finie. */
    for (int i = 0; i < AST_MAX_ROCKS; ++i) g.rock[i].alive = false;
    g.spawn_timer = 1e6f;
    hold_one(&g, NS_GAME_ACTION);
    for (int i = 0; i < 120 * 30; ++i) asteroid_tick(&g, STEP);
    CHECK(g.phase == AST_PLAYING, "la partie tourne encore");

    int shots = 0;
    for (int i = 0; i < AST_MAX_SHOTS; ++i) if (g.shot[i].alive) shots++;
    CHECK(g.shot_kind == AST_SHOT_NORMAL, "l'arme reste celle de base");
    CHECK(shots > 0, "et elle tire toujours après trente secondes (%d en vol)", shots);

    /* Une arme spéciale à jauge vide retombe sur le tir normal. */
    g.shot_kind = AST_SHOT_HOMING;
    g.ammo = 0.0f;
    g.fire_timer = 0.0f;
    asteroid_tick(&g, STEP);
    CHECK(g.shot_kind == AST_SHOT_NORMAL,
          "jauge vide : on retombe sur le tir normal (%d)", g.shot_kind);
}

/* -------------------------------------------------------------------------- */

typedef struct ledger {
    int64_t  total;
    uint32_t shots, dies;
} ledger;

static void drain(asteroid *g, ledger *l)
{
    ns_game_events ev;
    SDL_zero(ev);
    g_asteroid_api.events(g, &ev);
    if (ev.blip) {
        l->shots++;
        if (SDL_strcmp(ev.blip_kind, "shot") != 0) l->total += 999999;
    }
    if (ev.score) {
        if (SDL_strcmp(ev.score_kind, "rock") == 0
         || SDL_strcmp(ev.score_kind, "bonus") == 0) l->total += ev.score_value;
        else if (SDL_strcmp(ev.score_kind, "wave") == 0) l->total += 250;
        else l->total += 999999;
    }
    if (ev.die) l->dies++;
}

static void play(asteroid *g, uint64_t seed, ledger *l, float *seconds)
{
    memset(l, 0, sizeof *l);
    asteroid_reset(g, seed, false);
    float t = 0.0f;
    for (int i = 0; i < 120 * 240 && l->dies == 0; ++i) {
        asteroid_autopilot(g);
        asteroid_tick(g, STEP);
        t += STEP;
        drain(g, l);
    }
    if (seconds) *seconds = t;
}

/*
 * L'invariant central. « wave » vaut 250 à plat côté serveur, « rock » et
 * « bonus » portent leurs points : la somme doit valoir le score affiché, aux
 * vagues près qui sont un bonus du serveur et pas du client.
 */
static void test_le_journal_vaut_le_score(void)
{
    for (uint64_t seed = 0; seed < 6; ++seed) {
        asteroid g;
        ledger l;
        play(&g, 500u + seed, &l, NULL);
        /* On retranche les vagues, qui rapportent côté serveur uniquement. */
        const int64_t from_events = l.total - 250 * (int64_t)g.wave;
        if (from_events != g.score) {
            CHECK(false, "graine %llu : le journal donne %lld pour %lld affichés",
                  (unsigned long long)seed, (long long)from_events, (long long)g.score);
            return;
        }
    }
    CHECK(true, "six parties : le journal vaut le score, point pour point");
}

static void test_la_fin_est_annoncee(void)
{
    int silent = 0, deaths = 0;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        asteroid g;
        ledger l;
        play(&g, 900u + seed, &l, NULL);
        if (g.phase == AST_DEAD) deaths++;
        if (g.phase == AST_DEAD && l.dies != 1) silent++;
    }
    CHECK(deaths > 0, "le joueur automatique finit par mourir (%d fois sur 6)", deaths);
    CHECK(silent == 0, "et chaque mort est annoncée une fois (%d muettes)", silent);
}

/* Les cadences de `rulesTable["asteroid"]` : rock 25/s, bonus 2/s, wave 0,5/s,
 * shot 40/s. Le joueur automatique produit les captures ; s'il dépasse, toute
 * partie capturée est refusée. */
static void test_les_cadences_du_serveur(void)
{
    for (uint64_t seed = 0; seed < 4; ++seed) {
        asteroid g;
        ledger l;
        float seconds = 0.0f;
        play(&g, 1300u + seed, &l, &seconds);
        if (seconds < 2.0f) continue;
        const double shots = (double)l.shots / (double)seconds;
        if (shots > 40.0) {
            CHECK(false, "graine %llu : %.1f shot/s", (unsigned long long)seed, shots);
            return;
        }
    }
    CHECK(true, "quatre parties automatiques tiennent la cadence de tir");
}

static void test_determinisme(void)
{
    asteroid a, b;
    ledger la, lb;
    play(&a, 4242, &la, NULL);
    play(&b, 4242, &lb, NULL);
    CHECK(a.score == b.score, "même graine, même score (%lld / %lld)",
          (long long)a.score, (long long)b.score);
    CHECK(fabsf(a.ship_x - b.ship_x) < 1e-3f && fabsf(a.ship_y - b.ship_y) < 1e-3f,
          "et la même position finale");

    asteroid c;
    ledger lc;
    play(&c, 4243, &lc, NULL);
    CHECK(c.score != a.score || fabsf(c.ship_x - a.ship_x) > 1e-3f,
          "une autre graine donne une autre partie");
}

/* Rien ne part en NaN, et rien ne sort du terrain sans être retiré. */
static void test_rien_ne_derive(void)
{
    asteroid g;
    ledger l;
    play(&g, 777, &l, NULL);

    CHECK(g.ship_x == g.ship_x && g.ship_y == g.ship_y, "le vaisseau n'est pas NaN");
    CHECK(g.ship_x >= -1.0f && g.ship_x <= AST_W + 1.0f, "il est dans le terrain");

    int stray = 0;
    for (int i = 0; i < AST_MAX_ROCKS; ++i) {
        if (!g.rock[i].alive) continue;
        if (g.rock[i].x != g.rock[i].x || g.rock[i].y != g.rock[i].y) stray++;
        if (g.rock[i].radius <= 0.0f) stray++;
    }
    for (int i = 0; i < AST_MAX_SHOTS; ++i) {
        if (!g.shot[i].alive) continue;
        if (g.shot[i].x != g.shot[i].x || g.shot[i].y != g.shot[i].y) stray++;
    }
    CHECK(stray == 0, "aucun objet en dérive (%d)", stray);
}

/* Le joueur automatique doit RAMASSER : sans ça la moitié du jeu — tir
 * multiple, bouclier, munitions — n'est jamais exercée par les tests. */
static void test_le_robot_ramasse(void)
{
    int upgraded = 0;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        asteroid g;
        ledger l;
        play(&g, 2100u + seed, &l, NULL);
        if (g.multi > 1 || g.shot_kind != AST_SHOT_NORMAL || g.nukes > 0
            || g.damage_bonus > 0.0f || g.missile_speed > 1.0f) upgraded++;
    }
    CHECK(upgraded > 0, "il ramasse des bonus (%d parties sur 6)", upgraded);
}

static void test_le_vocabulaire(void)
{
    asteroid g;
    asteroid_reset(&g, 5150, false);

    const char *seen[8] = { 0 };
    int count = 0;
    for (int i = 0; i < 120 * 240; ++i) {
        asteroid_autopilot(&g);
        asteroid_tick(&g, STEP);
        ns_game_events ev;
        SDL_zero(ev);
        g_asteroid_api.events(&g, &ev);
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
        for (const char *const *k = g_asteroid_api.event_kinds; *k; ++k)
            if (SDL_strcmp(*k, seen[i]) == 0) declared = true;
        CHECK(declared, "« %s » est émis : il doit être déclaré", seen[i]);
    }
    CHECK(count >= 2, "au moins shot et rock sont émis (%d types)", count);
}

int main(void)
{
    test_depart();
    test_le_bareme_des_asteroides();
    test_les_rampes();
    test_la_deceleration();
    test_le_vaisseau_rebondit();
    test_la_fragmentation();
    test_la_glace_gele_sans_casser();
    test_le_bouclier();
    test_le_tir_normal_est_inepuisable();
    test_le_journal_vaut_le_score();
    test_la_fin_est_annoncee();
    test_les_cadences_du_serveur();
    test_determinisme();
    test_rien_ne_derive();
    test_le_robot_ramasse();
    test_le_vocabulaire();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
