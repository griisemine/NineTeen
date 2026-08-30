/*
 * test_demineur.c — les règles du Démineur, et l'invariant qui les relie au serveur.
 *
 * Ce qui mérite un test ici n'est pas la grille — on la voit sur une capture —
 * mais **la comptabilité**. Le Démineur est le premier jeu porté où UN geste
 * produit BEAUCOUP de gains : un seul appui ouvre une cascade de cent cases.
 * Or le serveur ne croit pas le score affiché, il le recalcule à partir du
 * journal (`server/internal/runs/runs.go`). Si le journal dit « une case » là
 * où le score en a compté cent, la partie est classée à cinq points et rien,
 * côté client, ne le laisse voir.
 *
 * Trois défauts trouvés en écrivant ce fichier, et qu'il verrouille :
 *   1. une cascade n'émettait qu'un événement, sans son compte ;
 *   2. la victoire écrasait le gain de la cascade qui l'avait produite ;
 *   3. gagner ne levait pas `die`, donc une partie gagnée n'était **jamais**
 *      enregistrée — ni au meilleur score local, ni au classement.
 */
#include "demineur.h"
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

/* Le barème, tel que `rulesTable["demineur"]` l'applique. */
#define PTS_CELL 5
#define PTS_FLAG 2
#define PTS_WIN  500

/* Vider la file d'événements comme le fait `room/main.c`, en recalculant le
 * score À LA MANIÈRE DU SERVEUR : c'est ce total-là qui sera classé. */
typedef struct ledger {
    int64_t  total;
    uint32_t cells, flags, wins, moves, dies;
} ledger;

static void drain(demineur *g, ledger *l)
{
    ns_game_events ev;
    SDL_zero(ev);
    g_demineur_api.events(g, &ev);
    if (ev.blip) {
        l->moves++;
        /* Le nom compte autant que le fait : « move » est muet côté serveur,
         * « cell » vaut cinq points. */
        if (strcmp(ev.blip_kind, "move") != 0) l->total += 999999;
    }
    if (ev.score) {
        if (strcmp(ev.score_kind, "cell") == 0) {
            l->cells += (uint32_t)ev.score_value;
            l->total += (int64_t)PTS_CELL * ev.score_value;   /* `scaled` */
        } else if (strcmp(ev.score_kind, "flag") == 0) {
            l->flags++;
            l->total += PTS_FLAG;
        } else if (strcmp(ev.score_kind, "win") == 0) {
            l->wins++;
            l->total += PTS_WIN;
        } else {
            l->total += 999999;   /* un nom inconnu ferait rejeter la partie */
        }
    }
    if (ev.die) l->dies++;
}

/* Une partie entière menée par le joueur automatique, journal tenu. */
static void play(demineur *g, uint64_t seed, bool hard, ledger *l, float *seconds)
{
    memset(l, 0, sizeof *l);
    demineur_reset(g, seed, hard);
    float t = 0.0f;
    for (int i = 0; i < 120 * 600 && l->dies == 0; ++i) {
        demineur_autopilot(g);
        demineur_tick(g, STEP);
        t += STEP;
        drain(g, l);
    }
    if (seconds) *seconds = t;
}

/* -------------------------------------------------------------------------- */

static void test_depart(void)
{
    demineur g;
    demineur_reset(&g, 1234, false);

    CHECK(g.phase == DEM_READY, "la partie commence en sursis");
    CHECK(g.score == 0 && g.revealed == 0 && g.flags == 0, "tout est à zéro");
    CHECK(!g.placed, "aucune bombe n'est posée avant le premier coup");
    CHECK(g.cursor_r == DEM_ROWS / 2 && g.cursor_c == DEM_COLS / 2,
          "le curseur part au centre (%d, %d)", g.cursor_r, g.cursor_c);

    int bombs = 0;
    for (int r = 0; r < DEM_ROWS; ++r)
        for (int c = 0; c < DEM_COLS; ++c)
            if (g.bomb[r][c]) bombs++;
    CHECK(bombs == 0, "la grille est vide tant qu'on n'a pas joué (%d)", bombs);

    CHECK(DEM_ROWS == 16 && DEM_COLS == 25, "la grille de 2020 : 16 x 25");
    CHECK(DEM_BOMBS_HARD == 100, "cent bombes sur la borne dure, le quart des cases (%d)",
          DEM_BOMBS_HARD);
    CHECK(DEM_BOMBS_EASY == 60, "soixante bombes sur la borne ordinaire, 15 %% (%d)",
          DEM_BOMBS_EASY);
}

/*
 * La règle qui distingue un démineur d'une loterie. Elle se vérifie sur
 * beaucoup de graines, parce qu'une seule pourrait passer par chance.
 */
static void test_premiere_case_toujours_sure(void)
{
    int failures = 0, neighbour_failures = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        demineur g;
        demineur_reset(&g, seed, false);
        const int r = g.cursor_r, c = g.cursor_c;
        demineur_press(&g, NS_GAME_ACTION);

        if (g.phase == DEM_DEAD) failures++;
        if (g.bomb[r][c]) failures++;
        /* Les huit voisines aussi : c'est ce qui garantit une OUVERTURE et pas
         * un « 8 » solitaire au premier coup. */
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc)
                if (r + dr >= 0 && c + dc >= 0 && r + dr < DEM_ROWS && c + dc < DEM_COLS
                    && g.bomb[r + dr][c + dc]) neighbour_failures++;
    }
    CHECK(failures == 0, "le premier coup ne tue jamais (%d échecs sur 200)", failures);
    CHECK(neighbour_failures == 0,
          "il épargne aussi les huit voisines (%d)", neighbour_failures);
}

static void test_cent_bombes_posees(void)
{
    /* Les DEUX difficultés : la borne ordinaire en pose soixante, la dure les
     * cent de 2020. Ne vérifier qu'un seul compte laisserait l'autre libre. */
    for (int hard = 0; hard < 2; ++hard) {
        const int want = hard ? DEM_BOMBS_HARD : DEM_BOMBS_EASY;
        for (uint64_t seed = 0; seed < 40; ++seed) {
            demineur g;
            demineur_reset(&g, seed, hard != 0);
            demineur_press(&g, NS_GAME_ACTION);
            int bombs = 0;
            for (int r = 0; r < DEM_ROWS; ++r)
                for (int c = 0; c < DEM_COLS; ++c)
                    if (g.bomb[r][c]) bombs++;
            if (bombs != want) {
                CHECK(false, "hard=%d graine %llu : %d bombes au lieu de %d",
                      hard, (unsigned long long)seed, bombs, want);
                return;
            }
            CHECK(demineur_bombs(&g) == want,
                  "demineur_bombs annonce le compte de la difficulté (%d)",
                  demineur_bombs(&g));
        }
    }
    CHECK(true, "quarante graines par difficulté posent le bon compte de bombes");
}

/*
 * La cascade. C'est le comportement signature du jeu : une case à zéro voisin
 * ouvre ses voisines, et la plage s'arrête sur les chiffres.
 */
static void test_cascade_ouvre_une_plage(void)
{
    demineur g;
    demineur_reset(&g, 7, false);
    demineur_press(&g, NS_GAME_ACTION);

    CHECK(g.revealed > 1,
          "le premier coup ouvre une plage, pas une case (%u)", g.revealed);
    CHECK(g.score == (int64_t)g.revealed * PTS_CELL,
          "le score suit exactement les cases ouvertes (%lld pour %u)",
          (long long)g.score, g.revealed);

    /* Aucune bombe n'a été dévoilée, et aucune case dévoilée n'est isolée
     * derrière un zéro : la cascade est complète, pas tronquée. */
    int shown_bomb = 0, unfinished = 0;
    for (int r = 0; r < DEM_ROWS; ++r) {
        for (int c = 0; c < DEM_COLS; ++c) {
            if (g.shown[r][c] && g.bomb[r][c]) shown_bomb++;
            if (!g.shown[r][c]) continue;
            int n = 0;
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc)
                    if (!(dr == 0 && dc == 0) && r + dr >= 0 && c + dc >= 0
                        && r + dr < DEM_ROWS && c + dc < DEM_COLS
                        && g.bomb[r + dr][c + dc]) n++;
            if (n != 0) continue;
            /* une case à zéro voisin DOIT avoir ouvert ses huit voisines */
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc)
                    if (r + dr >= 0 && c + dc >= 0 && r + dr < DEM_ROWS
                        && c + dc < DEM_COLS && !g.shown[r + dr][c + dc]) unfinished++;
        }
    }
    CHECK(shown_bomb == 0, "aucune bombe n'est dévoilée par une cascade (%d)", shown_bomb);
    CHECK(unfinished == 0,
          "la cascade va jusqu'au bout : aucune case à zéro voisin ne laisse une "
          "voisine fermée (%d)", unfinished);
}

/*
 * L'invariant central : **le score affiché est exactement celui que le serveur
 * recalcule**. C'est lui qui a fait tomber les trois défauts cités en tête.
 */
static void test_le_journal_vaut_le_score(void)
{
    for (uint64_t seed = 0; seed < 12; ++seed) {
        demineur g;
        ledger l;
        play(&g, 900u + seed, false, &l, NULL);

        if (l.total != g.score) {
            CHECK(false,
                  "graine %llu : le serveur recalculerait %lld pour un score affiché de %lld",
                  (unsigned long long)seed, (long long)l.total, (long long)g.score);
            return;
        }
        if (l.cells != g.revealed) {
            CHECK(false, "graine %llu : %u cases journalisées pour %u ouvertes",
                  (unsigned long long)seed, l.cells, g.revealed);
            return;
        }
        if (l.flags != g.flags) {
            CHECK(false, "graine %llu : %u drapeaux journalisés pour %u posés",
                  (unsigned long long)seed, l.flags, g.flags);
            return;
        }
    }
    CHECK(true, "douze parties : le journal vaut le score, case pour case");
}

/* Une partie finie DOIT lever `die`, sinon `room/main.c` ne la scelle jamais —
 * et un jeu gagné disparaîtrait sans laisser de meilleur score. */
static void test_la_fin_est_toujours_annoncee(void)
{
    int wins = 0, deaths = 0, silent = 0;
    for (uint64_t seed = 0; seed < 24; ++seed) {
        demineur g;
        ledger l;
        play(&g, 4000u + seed, false, &l, NULL);

        if (g.phase == DEM_WON) wins++;
        else if (g.phase == DEM_DEAD) deaths++;
        if (l.dies != 1) silent++;
    }
    CHECK(silent == 0,
          "vingt-quatre parties finissent en annonçant leur fin, une fois exactement (%d muettes)",
          silent);
    CHECK(wins + deaths == 24,
          "elles finissent toutes (%d gagnées, %d perdues)", wins, deaths);
}

/* La victoire est un événement DISTINCT, et il arrive AVANT la fin de partie. */
static void test_la_victoire_ne_mange_pas_la_cascade(void)
{
    /* Une partie gagnée d'office : on dévoile tout sauf les bombes, à la main.
     * C'est le seul moyen fiable d'atteindre la victoire — le joueur
     * automatique doit deviner, et il meurt souvent avant. */
    demineur g;
    demineur_reset(&g, 42, false);
    demineur_press(&g, NS_GAME_ACTION);      /* pose les bombes */

    ledger l;
    memset(&l, 0, sizeof l);
    drain(&g, &l);

    for (int r = 0; r < DEM_ROWS && g.phase != DEM_WON; ++r) {
        for (int c = 0; c < DEM_COLS && g.phase != DEM_WON; ++c) {
            if (g.bomb[r][c] || g.shown[r][c]) continue;
            g.cursor_r = r;
            g.cursor_c = c;
            demineur_press(&g, NS_GAME_ACTION);
            drain(&g, &l);
        }
    }
    CHECK(g.phase == DEM_WON, "toutes les cases sûres ouvertes : la partie est gagnée");

    /* La file peut rester pleine d'un cran ou deux : on la vide. */
    for (int i = 0; i < 8; ++i) drain(&g, &l);

    CHECK(l.wins == 1, "la victoire est annoncée une fois (%u)", l.wins);
    CHECK(l.cells == (uint32_t)(DEM_ROWS * DEM_COLS - demineur_bombs(&g)),
          "les %d cases sûres sont toutes journalisées (%u)",
          DEM_ROWS * DEM_COLS - demineur_bombs(&g), l.cells);
    CHECK(l.dies == 1, "et la fin de partie est annoncée après elle (%u)", l.dies);
    CHECK(l.total == g.score,
          "le serveur recalcule le même score (%lld contre %lld)",
          (long long)l.total, (long long)g.score);
    CHECK(g.score == (int64_t)l.cells * PTS_CELL + PTS_WIN,
          "soit les cases plus la prime de victoire (%lld)", (long long)g.score);
}

/* Sauter sur une mine tue, et cesse la partie. */
static void test_la_bombe_tue(void)
{
    demineur g;
    demineur_reset(&g, 3, false);
    demineur_press(&g, NS_GAME_ACTION);

    int br = -1, bc = -1;
    for (int r = 0; r < DEM_ROWS && br < 0; ++r)
        for (int c = 0; c < DEM_COLS && br < 0; ++c)
            if (g.bomb[r][c]) { br = r; bc = c; }
    CHECK(br >= 0, "une bombe existe quelque part");

    const int64_t before = g.score;
    g.cursor_r = br;
    g.cursor_c = bc;
    demineur_press(&g, NS_GAME_ACTION);

    CHECK(g.phase == DEM_DEAD, "la case minée tue");
    CHECK(g.score == before, "et ne rapporte rien (%lld)", (long long)g.score);
    CHECK(!g.shown[br][bc], "la bombe n'est pas comptée comme une case ouverte");

    /* Après la mort, plus rien ne bouge. */
    const uint32_t revealed = g.revealed;
    demineur_press(&g, NS_GAME_ACTION);
    demineur_press(&g, NS_GAME_UP);
    CHECK(g.revealed == revealed, "une partie finie ne se joue plus");
}

/* Un drapeau protège de soi-même : c'est à quoi il sert. */
static void test_le_drapeau_protege(void)
{
    demineur g;
    demineur_reset(&g, 11, false);
    demineur_press(&g, NS_GAME_ACTION);

    int br = -1, bc = -1;
    for (int r = 0; r < DEM_ROWS && br < 0; ++r)
        for (int c = 0; c < DEM_COLS && br < 0; ++c)
            if (g.bomb[r][c]) { br = r; bc = c; }

    g.flag[br][bc] = true;
    g.cursor_r = br;
    g.cursor_c = bc;
    demineur_press(&g, NS_GAME_ACTION);
    CHECK(g.phase != DEM_DEAD, "une case sous drapeau ne s'ouvre pas par mégarde");
}

/*
 * Le défilement au maintien. Sans lui, traverser vingt-cinq colonnes demanderait
 * vingt-cinq appuis ; avec lui trop rapide, le curseur devient inpointable.
 */
static void test_le_maintien_fait_defiler(void)
{
    demineur g;
    demineur_reset(&g, 5, false);

    bool held[NS_GAME_BUTTON_COUNT];
    memset(held, 0, sizeof held);

    /* Sans maintien, le curseur ne bouge pas tout seul. */
    const int c0 = g.cursor_c;
    demineur_hold(&g, held);
    for (int i = 0; i < 120; ++i) demineur_tick(&g, STEP);
    CHECK(g.cursor_c == c0, "sans maintien, le curseur ne dérive pas");

    /* Avec maintien : un pas immédiat, puis un défilement régulier. */
    held[NS_GAME_RIGHT] = true;
    demineur_hold(&g, held);
    demineur_tick(&g, STEP);
    CHECK(g.cursor_c == c0 + 1, "le premier pas est immédiat (%d)", g.cursor_c);

    int steps = 0;
    const int start = g.cursor_c;
    for (int i = 0; i < 120 && g.cursor_c < DEM_COLS - 1; ++i) demineur_tick(&g, STEP);
    steps = g.cursor_c - start;
    CHECK(steps >= 5 && steps <= 12,
          "une seconde de maintien fait défiler de cinq à douze cases (%d)", steps);

    /* Et il ne sort jamais de la grille. */
    for (int i = 0; i < 600; ++i) demineur_tick(&g, STEP);
    CHECK(g.cursor_c == DEM_COLS - 1, "il s'arrête au bord (%d)", g.cursor_c);
    CHECK(g.cursor_r >= 0 && g.cursor_r < DEM_ROWS, "et reste dans la grille");
}

/* Le déterminisme : c'est la condition pour qu'un journal signé veuille dire
 * quelque chose, et pour qu'un duel soit un jour possible. */
static void test_determinisme(void)
{
    demineur a, b;
    ledger la, lb;
    play(&a, 31337, false, &la, NULL);
    play(&b, 31337, false, &lb, NULL);

    CHECK(a.score == b.score, "même graine, même score (%lld / %lld)",
          (long long)a.score, (long long)b.score);
    CHECK(a.revealed == b.revealed && a.flags == b.flags,
          "même grille parcourue (%u/%u cases, %u/%u drapeaux)",
          a.revealed, b.revealed, a.flags, b.flags);
    CHECK(memcmp(a.bomb, b.bomb, sizeof a.bomb) == 0, "et les mêmes bombes");
    CHECK(la.total == lb.total, "donc le même journal (%lld / %lld)",
          (long long)la.total, (long long)lb.total);

    demineur c;
    ledger lc;
    play(&c, 31338, false, &lc, NULL);
    CHECK(memcmp(a.bomb, c.bomb, sizeof a.bomb) != 0,
          "une autre graine donne une autre grille");
}

/*
 * L'anti-triche du serveur, vérifié ici plutôt que découvert en production.
 *
 * `rulesTable["demineur"]` limite « cell » à 15/s, « flag » à 8/s, « move » à
 * 30/s et « win » à 0,2/s. Le joueur automatique produit les captures : s'il
 * dépasse, toute partie capturée est refusée — et c'est exactement le genre de
 * défaut qu'on ne voit pas sans serveur en face.
 */
static void test_les_cadences_du_serveur(void)
{
    for (uint64_t seed = 0; seed < 6; ++seed) {
        demineur g;
        ledger l;
        float seconds = 0.0f;
        play(&g, 7000u + seed, false, &l, &seconds);
        if (seconds < 1.0f) continue;

        /* `l.cells` porte la QUANTITÉ de cases ouvertes ; la limite du serveur,
         * elle, porte sur le NOMBRE d'événements, donc sur les coups joués. Ce
         * sont les coups qu'on mesure — bornés par la cadence du joueur
         * automatique, qui est justement là pour ça. */
        const double moves = (double)l.moves / (double)seconds;
        const double flags = (double)l.flags / (double)seconds;
        const double wins  = (double)l.wins / (double)seconds;

        if (moves > 30.0 || flags > 8.0 || wins > 0.2) {
            CHECK(false,
                  "graine %llu : %.1f move/s, %.1f flag/s, %.2f win/s en %.1f s",
                  (unsigned long long)seed, moves, flags, wins, (double)seconds);
            return;
        }
    }
    CHECK(true, "six parties automatiques tiennent les cadences de rulesTable");
}

/* La cadence du joueur automatique, mesurée directement. */
static void test_le_robot_joue_a_cadence_humaine(void)
{
    demineur g;
    demineur_reset(&g, 77, false);

    int acted = 0;
    for (int i = 0; i < 120 * 10; ++i) {   /* dix secondes */
        if (demineur_autopilot(&g)) acted++;
        demineur_tick(&g, STEP);
        if (g.phase == DEM_DEAD || g.phase == DEM_WON) demineur_reset(&g, 77 + (uint64_t)i, false);
    }
    const double per_second = acted / 10.0;
    CHECK(per_second <= 15.0 && per_second >= 3.0,
          "le joueur automatique joue %.1f coups/s — humainement plausible, et sous "
          "les quinze de l'anti-triche", per_second);
}

/* Le vocabulaire émis doit tenir dans celui déclaré, sinon le serveur refuse
 * la partie entière avec « événement inconnu ». */
static void test_le_vocabulaire_est_complet(void)
{
    const char *seen[8] = { 0 };
    int count = 0;

    for (uint64_t seed = 0; seed < 8; ++seed) {
        demineur g;
        demineur_reset(&g, 5000u + seed, false);
        for (int i = 0; i < 120 * 120; ++i) {
            demineur_autopilot(&g);
            demineur_tick(&g, STEP);

            ns_game_events ev;
            SDL_zero(ev);
            g_demineur_api.events(&g, &ev);
            const char *kinds[2] = { ev.blip ? ev.blip_kind : NULL,
                                     ev.score ? ev.score_kind : NULL };
            for (int k = 0; k < 2; ++k) {
                if (!kinds[k]) continue;
                bool known = false;
                for (int j = 0; j < count; ++j)
                    if (strcmp(seen[j], kinds[k]) == 0) known = true;
                if (!known && count < 8) seen[count++] = kinds[k];
            }
            if (ev.die) break;
        }
    }

    for (int i = 0; i < count; ++i) {
        bool declared = false;
        for (const char *const *k = g_demineur_api.event_kinds; *k; ++k)
            if (strcmp(*k, seen[i]) == 0) declared = true;
        CHECK(declared, "« %s » est émis : il doit être déclaré dans event_kinds", seen[i]);
    }
    CHECK(count >= 3, "au moins move, cell et flag sont émis (%d types)", count);
}

/* Le joueur automatique déduit : il doit faire mieux que le hasard. Sur une
 * grille au quart minée, ouvrir au hasard donne quelques cases ; déduire en
 * donne des dizaines. */
static void test_le_robot_deduit(void)
{
    uint32_t best = 0, total = 0;
    for (uint64_t seed = 0; seed < 12; ++seed) {
        demineur g;
        ledger l;
        play(&g, 6000u + seed, false, &l, NULL);
        if (g.revealed > best) best = g.revealed;
        total += g.revealed;
    }
    const uint32_t mean = total / 12u;
    CHECK(mean > 20, "il ouvre en moyenne %u cases par partie — il déduit", mean);
    CHECK(best > 50, "sa meilleure partie en ouvre %u", best);
}

/*
 * LA PARTIE PEUT SE GAGNER — ce qui n'allait pas de soi.
 *
 * Avec les cent bombes de 2020 sur quatre cents cases, soit 25 % — plus dense
 * que la grille « expert » de la version de référence — le solveur de
 * `demineur.c` perdait DEUX CENTS parties sur deux cents. Zéro victoire, onze
 * secondes de moyenne. `PTS_WIN`, l'écran « GAGNE » et toute la branche
 * `DEM_WON` étaient du code inatteignable : le jeu n'avait pas de fin heureuse.
 *
 * La borne ordinaire est passée à 15 %, et le même solveur en gagne six sur
 * dix. Ce test garde la propriété qui compte — qu'il existe une manche à
 * gagner — sans exiger un taux précis : un seuil bas suffit à distinguer « on
 * peut gagner » de « on ne peut jamais ».
 */
static void test_la_partie_se_gagne(void)
{
    int gagnees = 0;
    const int parties = 30;
    for (int i = 0; i < parties; ++i) {
        demineur g;
        ledger l;
        play(&g, 9000u + (uint64_t)i * 131u, false, &l, NULL);
        if (g.phase == DEM_WON) gagnees++;
    }
    CHECK(gagnees >= parties / 4,
          "la borne ordinaire se gagne (%d parties sur %d)", gagnees, parties);
}

/* Le score exposé au classement ne descend jamais sous zéro, et il tient dans
 * un `uint32_t`. */
static void test_le_score_expose(void)
{
    demineur g;
    ledger l;
    play(&g, 21, false, &l, NULL);

    const uint32_t s = g_demineur_api.score(&g);
    CHECK((int64_t)s == g.score, "le score exposé vaut le score interne (%u / %lld)",
          s, (long long)g.score);

    g_demineur_api.set_best(&g, 4242u);
    CHECK(g_demineur_api.best(&g) == 4242u, "le meilleur score se pose et se relit");

    float dead_time = -1.0f;
    const bool over = g_demineur_api.dead(&g, &dead_time);
    CHECK(over == (g.phase == DEM_DEAD || g.phase == DEM_WON),
          "la fin de partie est rapportée telle qu'elle est");
    CHECK(dead_time >= 0.0f, "et le temps écoulé depuis (%.2f)", (double)dead_time);
}

int main(void)
{
    test_depart();
    test_premiere_case_toujours_sure();
    test_cent_bombes_posees();
    test_cascade_ouvre_une_plage();
    test_le_journal_vaut_le_score();
    test_la_fin_est_toujours_annoncee();
    test_la_victoire_ne_mange_pas_la_cascade();
    test_la_bombe_tue();
    test_le_drapeau_protege();
    test_le_maintien_fait_defiler();
    test_determinisme();
    test_les_cadences_du_serveur();
    test_le_robot_joue_a_cadence_humaine();
    test_le_vocabulaire_est_complet();
    test_le_robot_deduit();
    test_la_partie_se_gagne();
    test_le_score_expose();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
