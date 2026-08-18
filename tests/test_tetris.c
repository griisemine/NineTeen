/*
 * test_tetris.c — les règles de Tetris, confrontées à celles de 2020.
 *
 * Le cas qui justifie le fichier à lui seul : **la table des pièces**. Onze
 * mille deux cents entiers écrits à la main en 2020, dont
 * `tools/extract_tetris_pieces.py` fait une copie. Une rotation fausse sur une
 * pièce sur vingt-huit ne se voit pas sur une capture — elle se découvre en
 * jouant, longtemps après. Le condensé du fichier source est donc vérifié ici :
 * si `pieces.h` change, le test le dit et on relance le script.
 *
 * L'autre invariant qui compte est celui que le Démineur a rendu explicite : le
 * score affiché doit valoir exactement ce que le serveur recalcule à partir du
 * journal. Le barème de 2020 double à chaque ligne simultanée et multiplie par
 * dix une ligne d'une seule couleur ; rien de tout ça ne se déduit du nombre
 * d'événements, donc « lines » porte les points.
 */
#include "games.h"
#include "ns_core.h"
#include "ns_runlog.h"
#include "tetris.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* -------------------------------------------------------------------------- */

/*
 * La table des pièces est une COPIE. Ce test est ce qui garantit qu'elle le
 * reste : il relit le fichier de 2020 et compare son condensé à celui que le
 * script a inscrit dans l'en-tête généré.
 */
static void test_table_fidele_a_2020(void)
{
    const char *path = TETRIS_LEGACY_PIECES;   /* défini par CMake */
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "le fichier de 2020 est lisible : %s", path);
    if (!f) return;

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    CHECK(buf != NULL, "mémoire pour %ld octets", size);
    if (!buf) { fclose(f); return; }
    const size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    CHECK(got == (size_t)size, "lecture complète (%zu / %ld)", got, size);

    uint8_t digest[32];
    ns_sha256(buf, got, digest);
    free(buf);

    char hex[65];
    for (int i = 0; i < 32; ++i) SDL_snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';

    CHECK(SDL_strcmp(hex, TET_PIECES_SOURCE_SHA256) == 0,
          "la table vient bien de ce pieces.h — sinon relancer "
          "tools/extract_tetris_pieces.py (lu %s, attendu %s)",
          hex, TET_PIECES_SOURCE_SHA256);
}

/* Chaque rotation d'une pièce a le MÊME nombre de cases : une rotation qui en
 * perd ou en gagne est une faute de recopie, et c'est la seule qui se voie
 * automatiquement. */
static void test_rotations_conservent_les_cases(void)
{
    int faults = 0;
    for (int d = 0; d < TET_DIFFICULTIES; ++d) {
        for (int s = 0; s < TET_SIZES; ++s) {
            for (int p = 0; p < TET_PIECES; ++p) {
                int ref = -1;
                for (int r = 0; r < TET_ROTATIONS; ++r) {
                    int n = 0;
                    for (int row = 0; row < TET_GRID; ++row) {
                        uint16_t bits = TET_SHAPE[d][s][p][r][row];
                        while (bits) { n += (bits & 1u); bits >>= 1; }
                    }
                    if (ref < 0) ref = n;
                    else if (n != ref) faults++;
                }
                if (ref == 0) faults++;   /* une pièce vide n'existe pas */
            }
        }
    }
    CHECK(faults == 0, "les 112 formes gardent leur nombre de cases (%d écarts)", faults);

    /* Les sept pièces normales sont des tétrominos : quatre cases. */
    for (int p = 0; p < TET_PIECES; ++p) {
        int n = 0;
        for (int row = 0; row < TET_GRID; ++row) {
            uint16_t bits = TET_SHAPE[0][0][p][0][row];
            while (bits) { n += (bits & 1u); bits >>= 1; }
        }
        CHECK(n == 4, "la pièce %d est un tétromino (%d cases)", p, n);
    }
}

/*
 * Les deux constats qui interdisent tout raccourci, VÉRIFIÉS — et le premier
 * jet de ce fichier affirmait le contraire du second. C'est ce test qui l'a dit.
 */
static void test_les_raccourcis_sont_faux(void)
{
    /* Aucune forme commune entre les deux difficultés : 56 sur 56 diffèrent. */
    int shared = 0;
    for (int s = 0; s < TET_SIZES; ++s) {
        for (int p = 0; p < TET_PIECES; ++p) {
            for (int r = 0; r < TET_ROTATIONS; ++r) {
                bool identical = true;
                for (int row = 0; row < TET_GRID; ++row) {
                    if (TET_SHAPE[0][s][p][r][row] != TET_SHAPE[1][s][p][r][row]) {
                        identical = false;
                        break;
                    }
                }
                if (identical) shared++;
            }
        }
    }
    CHECK(shared == 0,
          "les deux difficultés n'ont aucune forme en commun (%d partagées)", shared);

    /*
     * La FORME géante EST bien la forme normale doublée — les 56 le sont. C'est
     * l'inverse de ce que ce fichier affirmait d'abord, et c'est la mesure qui
     * a tranché.
     */
    int doubled = 0, pivot_doubled = 0;
    for (int d = 0; d < TET_DIFFICULTIES; ++d) {
        for (int p = 0; p < TET_PIECES; ++p) {
            for (int r = 0; r < TET_ROTATIONS; ++r) {
                bool ok = true;
                for (int row = 0; row < TET_GRID / 2 && ok; ++row) {
                    const uint16_t src = TET_SHAPE[d][0][p][r][row];
                    uint16_t want = 0;
                    for (int c = 0; c < TET_GRID / 2; ++c)
                        if (src & (1u << c)) want |= (uint16_t)(3u << (c * 2));
                    if (TET_SHAPE[d][1][p][r][row * 2] != want) ok = false;
                    if (TET_SHAPE[d][1][p][r][row * 2 + 1] != want) ok = false;
                }
                if (ok) doubled++;
                if (TET_PIVOT[d][1][p][r][0] == TET_PIVOT[d][0][p][r][0] * 2
                 && TET_PIVOT[d][1][p][r][1] == TET_PIVOT[d][0][p][r][1] * 2) pivot_doubled++;
            }
        }
    }
    CHECK(doubled == TET_DIFFICULTIES * TET_PIECES * TET_ROTATIONS,
          "la forme géante est la forme normale doublée, partout (%d / 56)", doubled);
    /*
     * Mais le PIVOT ne l'est pas, dans 42 cas sur 56. C'est ce qui interdit de
     * dériver les pièces géantes : on obtiendrait la bonne forme et la mauvaise
     * rotation — une pièce longue qui se déplace d'une case à chaque quart de
     * tour, ce qui a l'air juste sur une capture et se découvre en jouant.
     */
    CHECK(pivot_doubled == 14,
          "et son pivot ne l'est que dans 14 cas sur 56 (%d)", pivot_doubled);
}

/* -------------------------------------------------------------------------- */

static void test_depart(void)
{
    tetris g;
    tetris_reset(&g, 1234, false);

    CHECK(g.phase == TET_READY, "la partie commence en sursis");
    CHECK(g.score == 0 && g.lines == 0, "tout est à zéro");
    CHECK(g.pieces == 1, "une pièce est entrée (%u)", g.pieces);

    int filled = 0;
    for (int y = 0; y < TET_H; ++y)
        for (int x = 0; x < TET_W; ++x)
            if (g.cell[y][x] != TET_EMPTY) filled++;
    CHECK(filled == 0, "le plateau est vide (%d cases)", filled);

    /* La courbe de vitesse part de FRAME_MAX[DOWN] = 20 images à 30 Hz. */
    CHECK(fabsf(g.fall_period - 20.0f / 30.0f) < 1e-4f,
          "la descente part à 20 images par ligne, soit %.3f s (%.3f)",
          20.0 / 30.0, (double)g.fall_period);
}

/* La vitesse décroît, et elle décroît selon la courbe de 2020 — pas selon une
 * rampe qu'on aurait trouvée jolie. */
static void test_la_vitesse_suit_la_courbe(void)
{
    tetris g;
    tetris_reset(&g, 7, false);
    tetris_press(&g, NS_GAME_LEFT);      /* démarre la partie */

    const float p0 = g.fall_period;
    for (int i = 0; i < 120 * 60; ++i) tetris_tick(&g, STEP);   /* une minute */
    const float p60 = g.fall_period;

    /* FRAME_MAX * 0.99976^(30*60) = 20 * 0.99976^1800 = 12,97 images. */
    const float want = 20.0f * powf(0.99976f, 30.0f * 60.0f) / 30.0f;
    CHECK(p60 < p0, "la descente accélère (%.4f -> %.4f)", (double)p0, (double)p60);
    CHECK(fabsf(p60 - want) < 2e-3f,
          "et elle suit la courbe de 2020 à la minute (%.4f, attendu %.4f)",
          (double)p60, (double)want);

    /* Le plancher : FRAME_MIN[DOWN] = 1 image. */
    for (int i = 0; i < 120 * 900; ++i) tetris_tick(&g, STEP);
    CHECK(g.fall_period >= 1.0f / 30.0f - 1e-5f,
          "elle ne descend jamais sous une image par ligne (%.4f)", (double)g.fall_period);
}

/* Le pas du moteur ne doit pas changer le jeu : c'est toute la raison du pas
 * fixe, et c'est ce qu'on casserait sans s'en apercevoir. */
static void test_independant_du_pas(void)
{
    tetris a, b;
    tetris_reset(&a, 99, false);
    tetris_reset(&b, 99, false);
    tetris_press(&a, NS_GAME_LEFT);
    tetris_press(&b, NS_GAME_LEFT);

    for (int i = 0; i < 120 * 30; ++i) tetris_tick(&a, 1.0f / 120.0f);
    for (int i = 0; i < 40 * 30; ++i)  tetris_tick(&b, 1.0f / 40.0f);

    CHECK(fabsf(a.fall_period - b.fall_period) < 1e-4f,
          "la courbe de vitesse est la même à 120 Hz et à 40 Hz (%.5f / %.5f)",
          (double)a.fall_period, (double)b.fall_period);
}

/* -------------------------------------------------------------------------- */

/* Remplit une ligne, sauf une case, avec une couleur donnée. */
static void fill_row(tetris *g, int y, int id, int hole)
{
    for (int x = 0; x < TET_W; ++x) {
        g->cell[y][x] = (x == hole) ? TET_EMPTY : (int8_t)id;
        g->cell_bonus[y][x] = 0;
    }
}

typedef struct ledger {
    int64_t  total;
    uint32_t drops, rotates, dies;
} ledger;

static void drain(tetris *g, ledger *l)
{
    ns_game_events ev;
    SDL_zero(ev);
    g_tetris_api.events(g, &ev);
    if (ev.blip) {
        l->rotates++;
        if (SDL_strcmp(ev.blip_kind, "rotate") != 0) l->total += 999999;
    }
    if (ev.score) {
        if (SDL_strcmp(ev.score_kind, "lines") == 0) l->total += ev.score_value;
        else if (SDL_strcmp(ev.score_kind, "drop") == 0) l->drops++;
        else l->total += 999999;
    }
    if (ev.die) l->dies++;
}

static void play(tetris *g, uint64_t seed, ledger *l, float *seconds)
{
    memset(l, 0, sizeof *l);
    tetris_reset(g, seed, false);
    float t = 0.0f;
    /* Trois minutes suffisent : le joueur automatique survit au-delà, et
     * simuler quinze minutes par graine ne prouverait rien de plus. */
    for (int i = 0; i < 120 * 180 && l->dies == 0; ++i) {
        tetris_autopilot(g);
        tetris_tick(g, STEP);
        t += STEP;
        drain(g, l);
    }
    if (seconds) *seconds = t;
}

static void test_une_ligne_vaut_cent(void)
{
    tetris g;
    tetris_reset(&g, 1, false);
    /* Deux couleurs différentes : sinon c'est une ligne de couleur unique, qui
     * vaut dix fois plus — et le test mesurerait autre chose. */
    fill_row(&g, TET_H - 1, 0, -1);
    g.cell[TET_H - 1][0] = 1;

    const int n = tetris_clear_lines(&g);
    CHECK(n == 1, "une ligne pleine part (%d)", n);
    CHECK(g.score == 100, "et vaut SCORE_BASE (%lld)", (long long)g.score);
    CHECK(g.lines == 1, "elle est comptée (%u)", g.lines);
    for (int x = 0; x < TET_W; ++x)
        CHECK(g.cell[TET_H - 1][x] == TET_EMPTY, "la ligne est vidée en %d", x);
}

/*
 * Le barème de 2020, et il n'est PAS celui d'un Tetris standard : chaque ligne
 * simultanée vaut le DOUBLE de la précédente. Un quadruple vaut donc
 * 100 + 200 + 400 + 800 = 1 500.
 */
static void test_les_lignes_simultanees_doublent(void)
{
    tetris g;
    tetris_reset(&g, 1, false);
    for (int k = 0; k < 4; ++k) {
        fill_row(&g, TET_H - 1 - k, 0, -1);
        g.cell[TET_H - 1 - k][0] = 1;    /* casser la couleur unique */
    }
    const int n = tetris_clear_lines(&g);
    CHECK(n == 4, "quatre lignes partent ensemble (%d)", n);
    CHECK(g.score == 100 + 200 + 400 + 800,
          "et valent 1 500, pas 400 (%lld)", (long long)g.score);
}

/* La règle la plus surprenante de 2020, et celle qu'on « corrigerait » par
 * mégarde : une ligne d'une SEULE couleur vaut dix fois plus. Viser la couleur
 * rapporte davantage que viser le quadruple. */
static void test_la_couleur_unique_vaut_dix_fois(void)
{
    tetris g;
    tetris_reset(&g, 1, false);
    fill_row(&g, TET_H - 1, 3, -1);      /* dix cases de la même pièce */

    const int n = tetris_clear_lines(&g);
    CHECK(n == 1, "la ligne part (%d)", n);
    CHECK(g.score == 100 * 10,
          "et vaut dix fois cent (%lld)", (long long)g.score);
}

/* Les lignes au-dessus descendent, et rien ne se perd en route. */
static void test_la_pile_descend(void)
{
    tetris g;
    tetris_reset(&g, 1, false);
    fill_row(&g, TET_H - 1, 0, -1);
    g.cell[TET_H - 1][0] = 1;
    /* Un témoin juste au-dessus. */
    g.cell[TET_H - 2][4] = 5;

    (void)tetris_clear_lines(&g);
    CHECK(g.cell[TET_H - 1][4] == 5,
          "la case au-dessus est descendue d'une ligne (%d)", g.cell[TET_H - 1][4]);
    CHECK(g.cell[TET_H - 2][4] == TET_EMPTY, "et a quitté sa place");
}

/* -------------------------------------------------------------------------- */

static void test_collisions(void)
{
    tetris g;
    tetris_reset(&g, 5, false);

    /* Poussée à gauche jusqu'au mur : elle s'arrête, elle ne sort pas. */
    for (int i = 0; i < 40; ++i) tetris_press(&g, NS_GAME_LEFT);
    CHECK(tetris_fits(&g, &g.cur), "la pièce collée au mur gauche est valide");
    tet_piece p = g.cur;
    p.x--;
    CHECK(!tetris_fits(&g, &p), "et un pas de plus la ferait sortir");

    for (int i = 0; i < 40; ++i) tetris_press(&g, NS_GAME_RIGHT);
    CHECK(tetris_fits(&g, &g.cur), "idem à droite");
    p = g.cur;
    p.x++;
    CHECK(!tetris_fits(&g, &p), "et un pas de plus la ferait sortir");

    /* Le fond. */
    tetris_reset(&g, 5, false);
    while (true) {
        p = g.cur;
        p.y++;
        if (!tetris_fits(&g, &p)) break;
        g.cur = p;
    }
    CHECK(tetris_fits(&g, &g.cur), "la pièce posée au fond est valide");
}

/* La descente maximale pose la pièce, tout de suite. */
static void test_la_descente_maximale(void)
{
    tetris g;
    tetris_reset(&g, 5, false);
    const uint32_t before = g.pieces;
    tetris_press(&g, NS_GAME_ACTION);
    CHECK(g.pieces == before + 1, "une nouvelle pièce est entrée (%u)", g.pieces);

    int filled = 0;
    for (int y = 0; y < TET_H; ++y)
        for (int x = 0; x < TET_W; ++x) if (g.cell[y][x] != TET_EMPTY) filled++;
    CHECK(filled >= 4, "la pièce posée est dans le plateau (%d cases)", filled);
}

/* La partie finit quand une pièce ne peut plus entrer, et pas avant. */
static void test_la_defaite(void)
{
    tetris g;
    tetris_reset(&g, 5, false);
    /*
     * Plein SAUF la dernière colonne. Un plateau vraiment plein verrait ses
     * vingt lignes disparaître à la première pose, et la partie continuerait —
     * ce que le premier jet de ce test n'avait pas prévu. Une colonne libre
     * empêche toute ligne de se compléter tout en ne laissant aucune place à
     * une pièce.
     */
    for (int y = 0; y < TET_H; ++y)
        for (int x = 0; x < TET_W - 1; ++x) g.cell[y][x] = 0;

    for (int i = 0; i < 40 && g.phase != TET_DEAD; ++i) tetris_press(&g, NS_GAME_ACTION);
    CHECK(g.phase == TET_DEAD, "un plateau sans place finit la partie");

    /* Et plus rien ne bouge. */
    const uint32_t pieces = g.pieces;
    tetris_press(&g, NS_GAME_ACTION);
    tetris_press(&g, NS_GAME_LEFT);
    CHECK(g.pieces == pieces, "une partie finie ne se joue plus");
}

/* -------------------------------------------------------------------------- */

/* L'invariant central : le score affiché est exactement celui que le serveur
 * recalcule. */
static void test_le_journal_vaut_le_score(void)
{
    for (uint64_t seed = 0; seed < 8; ++seed) {
        tetris g;
        ledger l;
        play(&g, 400u + seed, &l, NULL);
        if (l.total != g.score) {
            CHECK(false, "graine %llu : le serveur recalculerait %lld pour %lld affichés",
                  (unsigned long long)seed, (long long)l.total, (long long)g.score);
            return;
        }
    }
    CHECK(true, "huit parties : le journal vaut le score, point pour point");
}

/*
 * Toute partie finie annonce sa fin, une fois — et APRÈS avoir vidé sa file de
 * gains. `finish_run` scelle le journal : un gain publié après lui n'existe pas.
 *
 * Ce test ne passe pas par le joueur automatique, et c'est délibéré : il survit
 * indéfiniment, ce qui est une bonne nouvelle pour lui et une mauvaise manière
 * de provoquer une fin de partie. On construit donc la situation directement.
 */
static void test_la_fin_est_annoncee(void)
{
    tetris g;
    tetris_reset(&g, 5, false);
    for (int y = 0; y < TET_H; ++y)
        for (int x = 0; x < TET_W - 1; ++x) g.cell[y][x] = 0;

    ledger l;
    memset(&l, 0, sizeof l);
    for (int i = 0; i < 40 && g.phase != TET_DEAD; ++i) {
        tetris_press(&g, NS_GAME_ACTION);
        drain(&g, &l);
    }
    CHECK(g.phase == TET_DEAD, "la partie est finie");

    for (int i = 0; i < 8; ++i) drain(&g, &l);
    CHECK(l.dies == 1, "et elle l'annonce une fois exactement (%u)", l.dies);
    CHECK(l.total == g.score, "le journal vaut le score (%lld / %lld)",
          (long long)l.total, (long long)g.score);
}

/*
 * Les cadences de `rulesTable["tetris"]` : « lines » 4/s, « drop » 6/s,
 * « rotate » 30/s. Le joueur automatique produit les captures ; s'il dépasse,
 * toute partie capturée est refusée par l'anti-triche — et c'est le genre de
 * défaut qu'on ne voit pas sans serveur en face.
 */
static void test_les_cadences_du_serveur(void)
{
    for (uint64_t seed = 0; seed < 4; ++seed) {
        tetris g;
        ledger l;
        float seconds = 0.0f;
        play(&g, 1200u + seed, &l, &seconds);
        if (seconds < 2.0f) {
            CHECK(false, "graine %llu : partie de %.2f s, sous le plancher du serveur",
                  (unsigned long long)seed, (double)seconds);
            return;
        }
        const double drops = (double)l.drops / (double)seconds;
        const double rot   = (double)l.rotates / (double)seconds;
        if (drops > 6.0 || rot > 30.0) {
            CHECK(false, "graine %llu : %.1f drop/s, %.1f rotate/s",
                  (unsigned long long)seed, drops, rot);
            return;
        }
    }
    CHECK(true, "quatre parties automatiques tiennent les cadences de rulesTable");
}

static void test_determinisme(void)
{
    tetris a, b;
    ledger la, lb;
    play(&a, 31337, &la, NULL);
    play(&b, 31337, &lb, NULL);

    CHECK(a.score == b.score, "même graine, même score (%lld / %lld)",
          (long long)a.score, (long long)b.score);
    CHECK(a.lines == b.lines && a.pieces == b.pieces,
          "même partie (%u/%u lignes, %u/%u pièces)", a.lines, b.lines, a.pieces, b.pieces);
    CHECK(memcmp(a.cell, b.cell, sizeof a.cell) == 0, "et le même plateau");

    tetris c;
    ledger lc;
    play(&c, 31338, &lc, NULL);
    CHECK(memcmp(a.cell, c.cell, sizeof a.cell) != 0,
          "une autre graine donne une autre partie");
}

/* Le joueur automatique doit jouer, pas seulement survivre : il empile sans
 * NaN, fait des lignes, et le plateau reste cohérent. */
static void test_le_robot_joue(void)
{
    uint32_t lines = 0;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        tetris g;
        ledger l;
        play(&g, 2000u + seed, &l, NULL);
        lines += g.lines;

        /* Aucune case ne porte un identifiant impossible. */
        for (int y = 0; y < TET_H; ++y)
            for (int x = 0; x < TET_W; ++x)
                if (g.cell[y][x] != TET_EMPTY
                    && (g.cell[y][x] < 0 || g.cell[y][x] >= TET_PIECES)) {
                    CHECK(false, "case (%d,%d) = %d", x, y, g.cell[y][x]);
                    return;
                }
    }
    CHECK(lines > 20, "six parties font %u lignes au total — il joue", lines);
}

/* Le vocabulaire émis tient dans celui déclaré. */
static void test_le_vocabulaire(void)
{
    tetris g;
    ledger l;
    (void)l;
    tetris_reset(&g, 4242, false);

    const char *seen[8] = { 0 };
    int count = 0;
    for (int i = 0; i < 120 * 300; ++i) {
        tetris_autopilot(&g);
        tetris_tick(&g, STEP);
        ns_game_events ev;
        SDL_zero(ev);
        g_tetris_api.events(&g, &ev);
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
        for (const char *const *k = g_tetris_api.event_kinds; *k; ++k)
            if (SDL_strcmp(*k, seen[i]) == 0) declared = true;
        CHECK(declared, "« %s » est émis : il doit être déclaré", seen[i]);
    }
    CHECK(count >= 2, "au moins rotate et drop sont émis (%d types)", count);
}

int main(void)
{
    test_table_fidele_a_2020();
    test_rotations_conservent_les_cases();
    test_les_raccourcis_sont_faux();
    test_depart();
    test_la_vitesse_suit_la_courbe();
    test_independant_du_pas();
    test_une_ligne_vaut_cent();
    test_les_lignes_simultanees_doublent();
    test_la_couleur_unique_vaut_dix_fois();
    test_la_pile_descend();
    test_collisions();
    test_la_descente_maximale();
    test_la_defaite();
    test_le_journal_vaut_le_score();
    test_la_fin_est_annoncee();
    test_les_cadences_du_serveur();
    test_determinisme();
    test_le_robot_joue();
    test_le_vocabulaire();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
