/*
 * test_dedale.c — le labyrinthe d'abord, les règles ensuite.
 *
 * Ce fichier existe surtout pour UNE raison : **un labyrinthe relu à l'œil
 * ment**. Le premier jet du plan avait ses vingt et une lignes de la bonne
 * longueur, toutes ses pastilles atteignables — et l'enclos des hélices fermé
 * de tous les côtés. Les quatre hélices naissaient dans une poche murée et n'en
 * sortaient jamais. Ça se voyait sur une capture comme « un Dédale tranquille »,
 * pas comme un défaut.
 *
 * Les trois propriétés vérifiées ici sont donc structurelles : chaque ligne fait
 * vingt et une colonnes, toutes les pastilles sont atteignables depuis le départ
 * — sinon le niveau ne se termine jamais — et l'enclos communique avec le
 * labyrinthe.
 */
#include "games.h"
#include "ns_core.h"
#include "dedale.h"

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

/* Un parcours en largeur depuis une case, à travers les tunnels. */
static void flood(const dedale *g, int c0, int r0, bool seen[DD_ROWS][DD_COLS])
{
    memset(seen, 0, sizeof(bool) * DD_ROWS * DD_COLS);
    int16_t queue[DD_ROWS * DD_COLS][2];
    int head = 0, tail = 0;
    seen[r0][c0] = true;
    queue[tail][0] = (int16_t)c0; queue[tail][1] = (int16_t)r0; tail++;

    static const int DX[4] = { 1, -1, 0, 0 };
    static const int DY[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        const int c = queue[head][0], r = queue[head][1];
        head++;
        for (int d = 0; d < 4; ++d) {
            const int nc = ((c + DX[d]) + DD_COLS) % DD_COLS;
            const int nr = r + DY[d];
            if (nr < 0 || nr >= DD_ROWS) continue;
            if (!dedale_walkable(g, nc, nr)) continue;
            if (seen[nr][nc]) continue;
            seen[nr][nc] = true;
            queue[tail][0] = (int16_t)nc; queue[tail][1] = (int16_t)nr; tail++;
        }
    }
}

static void test_le_labyrinthe_tient(void)
{
    dedale g;
    dedale_reset(&g, 1, false);

    /* Le départ, tel que le plan le déclare. */
    const int sc = (int)(g.x / DD_CELL), sr = (int)(g.y / DD_CELL);
    CHECK(dedale_walkable(&g, sc, sr), "le départ n'est pas dans un mur (%d, %d)", sc, sr);

    static bool seen[DD_ROWS][DD_COLS];
    flood(&g, sc, sr, seen);

    int unreachable = 0;
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if ((g.tile[r][c] == DD_PELLET || g.tile[r][c] == DD_POWER) && !seen[r][c])
                unreachable++;
    CHECK(unreachable == 0,
          "toutes les pastilles sont atteignables — sinon le niveau ne finit "
          "jamais (%d hors d'atteinte)", unreachable);

    /* L'enclos : c'est le défaut que ce test a trouvé. */
    int caged = 0;
    for (int i = 0; i < DD_ROTORS; ++i) {
        const int c = (int)(g.rotor[i].x / DD_CELL), r = (int)(g.rotor[i].y / DD_CELL);
        if (!dedale_walkable(&g, c, r)) {
            CHECK(false, "l'hélice %d naît DANS un mur (%d, %d)", i, c, r);
            return;
        }
        if (!seen[r][c]) caged++;
    }
    CHECK(caged == 0,
          "les quatre hélices peuvent sortir de l'enclos (%d emmurés)", caged);

    /* Un labyrinthe symétrique : c'est ce qui le rend lisible, et c'est
     * vérifiable. */
    int asym = 0;
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if (g.tile[r][c] != g.tile[r][DD_COLS - 1 - c]) asym++;
    /* Le départ du joueur est au centre : il ne casse pas la symétrie. */
    CHECK(asym == 0, "le labyrinthe est symétrique gauche-droite (%d écarts)", asym);

    CHECK(dedale_count_pellets(&g) > 120,
          "il y a de quoi jouer : %d pastilles", dedale_count_pellets(&g));

    /* Quatre super-pastilles, une par coin, comme la borne de 1980. */
    int powers = 0;
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c) if (g.tile[r][c] == DD_POWER) powers++;
    CHECK(powers == 4, "quatre super-pastilles (%d)", powers);
}

/* Les tunnels : sortir par la gauche revient par la droite. */
static void test_les_tunnels(void)
{
    dedale g;
    dedale_reset(&g, 1, false);
    g.phase = DD_PLAYING;

    /* La rangée du tunnel est celle où la colonne 0 est praticable. */
    int tunnel_row = -1;
    for (int r = 0; r < DD_ROWS; ++r) if (g.tile[r][0] != DD_WALL) tunnel_row = r;
    CHECK(tunnel_row >= 0, "il existe une rangée de tunnel (%d)", tunnel_row);
    if (tunnel_row < 0) return;

    g.x = 0.5f * DD_CELL;
    g.y = ((float)tunnel_row + 0.5f) * DD_CELL;
    g.dir = g.want = DD_LEFT;
    for (int i = 0; i < 120; ++i) dedale_tick(&g, STEP);

    CHECK(g.x > (float)(DD_COLS - 4) * DD_CELL,
          "sorti par la gauche, on revient par la droite (%.1f)", (double)g.x);
}

/* Le barème de `rulesTable["dedale"]` : pastille 10, super-pastille 50. */
static void test_le_bareme(void)
{
    dedale g;
    dedale_reset(&g, 1, false);
    g.phase = DD_PLAYING;
    /* On écarte les hélices : ce test porte sur le score, pas sur la survie. */
    for (int i = 0; i < DD_ROTORS; ++i) g.rotor[i].respawn = 1e6f;

    const uint32_t before = g.pellets_left;
    const int64_t s0 = g.score;
    for (int i = 0; i < 120 * 6; ++i) dedale_tick(&g, STEP);

    const uint32_t eaten = before - g.pellets_left;
    CHECK(eaten > 0, "il mange en avançant (%u pastilles)", eaten);
    /* Chaque case vaut 10 ou 50 : le score est un multiple de 10. */
    CHECK((g.score - s0) % 10 == 0, "et le score suit le barème (%lld)",
          (long long)(g.score - s0));
}

/* Une super-pastille rend les hélices mangeables, et manger une hélice
 * rapporte 200 — puis 400, 800, 1 600 dans la même vague. */
static void test_la_super_pastille(void)
{
    dedale g;
    dedale_reset(&g, 1, false);
    g.phase = DD_PLAYING;
    for (int i = 0; i < DD_ROTORS; ++i) g.rotor[i].respawn = 1e6f;

    /* On se pose sur une super-pastille. */
    int pc = -1, pr = -1;
    for (int r = 0; r < DD_ROWS && pr < 0; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if (g.tile[r][c] == DD_POWER) { pc = c; pr = r; break; }
    CHECK(pc >= 0, "une super-pastille existe");
    if (pc < 0) return;

    g.x = ((float)pc + 0.5f) * DD_CELL;
    g.y = ((float)pr + 0.5f) * DD_CELL;
    for (int i = 0; i < DD_ROTORS; ++i) g.rotor[i].respawn = 0.0f;
    dedale_tick(&g, STEP);

    int frightened = 0;
    for (int i = 0; i < DD_ROTORS; ++i) if (g.rotor[i].frightened > 0.0f) frightened++;
    CHECK(frightened == DD_ROTORS, "les quatre hélices prennent peur (%d)", frightened);

    /* La première hélice mangée vaut 200, la deuxième 400. */
    const int64_t s0 = g.score;
    g.rotor[0].x = g.x; g.rotor[0].y = g.y;
    dedale_tick(&g, STEP);
    CHECK(g.score - s0 == 200, "la première hélice vaut 200 (%lld)",
          (long long)(g.score - s0));
    CHECK(g.rotor[0].eaten, "et il rentre chez lui");

    const int64_t s1 = g.score;
    g.rotor[1].x = g.x; g.rotor[1].y = g.y;
    dedale_tick(&g, STEP);
    CHECK(g.score - s1 == 400, "le deuxième en vaut 400 (%lld)",
          (long long)(g.score - s1));
}

/* Une hélice non apeurée coûte une vie, et la TROISIÈME termine la partie.
 *
 * Le portage n'en donnait qu'UNE, ce qui n'était pas un réglage mais un oubli :
 * la première rencontre finissait la partie, la recette mesurait trente-neuf
 * secondes à chaque essai, et le score ne dépassait jamais ce qu'on ramasse
 * avant de croiser quelqu'un. */
static void percute(dedale *g)
{
    for (int i = 1; i < DD_ROTORS; ++i) g->rotor[i].respawn = 1e6f;
    g->rotor[0].respawn = 0.0f;
    g->rotor[0].frightened = 0.0f;
    g->rotor[0].eaten = false;
    g->rotor[0].x = g->x;
    g->rotor[0].y = g->y;
    dedale_tick(g, STEP);
}

static void test_l_helice_tue(void)
{
    dedale g;
    dedale_reset(&g, 1, false);
    g.phase = DD_PLAYING;
    CHECK(g.lives == DD_LIVES, "on démarre avec %d vies (%u)", DD_LIVES, g.lives);

    /* On joue d'abord six secondes, hélices garées : il faut du score et un
     * labyrinthe entamé pour pouvoir vérifier qu'ils SURVIVENT à la prise. */
    for (int i = 0; i < DD_ROTORS; ++i) g.rotor[i].respawn = 1e6f;
    for (int i = 0; i < 120 * 6; ++i) dedale_tick(&g, STEP);
    const int64_t score_avant = g.score;
    const int pastilles_avant = dedale_count_pellets(&g);
    CHECK(score_avant > 0 && pastilles_avant < 189,
          "il a joué avant d'être pris (%lld pts, %d pastilles restantes)",
          (long long)score_avant, pastilles_avant);

    /* Les deux premières prises coûtent une vie et rendent la main. */
    for (uint32_t v = 1; v < DD_LIVES; ++v) {
        percute(&g);
        CHECK(g.phase == DD_CAUGHT, "prise %u : la manche s'arrête, pas la partie", v);
        CHECK(g.lives == DD_LIVES - v, "il reste %u vie(s) (%u)", DD_LIVES - v, g.lives);

        /* La pause s'écoule, puis la manche repart au départ du plan. */
        for (int i = 0; i < 400 && g.phase == DD_CAUGHT; ++i) dedale_tick(&g, STEP);
        CHECK(g.phase == DD_PLAYING, "la manche repart d'elle-même");
        /* Et elle repart AU DÉPART, pas là où on s'est fait prendre. */
        const int col = (int)(g.x / DD_CELL), row = (int)(g.y / DD_CELL);
        CHECK(dedale_walkable(&g, col, row), "elle repart sur une case libre");
    }

    /* LE POINT QUI FAIT QUE TROIS VIES VALENT MIEUX QU'UNE : le score et le
     * labyrinthe entamé survivent. Un `load_maze` dans la reprise de manche les
     * effacerait, et perdre une vie reviendrait à tout perdre. */
    CHECK(g.score >= score_avant, "le score survit à la prise (%lld -> %lld)",
          (long long)score_avant, (long long)g.score);
    CHECK(dedale_count_pellets(&g) <= pastilles_avant,
          "et le labyrinthe reste entamé (%d -> %d)",
          pastilles_avant, dedale_count_pellets(&g));

    percute(&g);
    CHECK(g.phase == DD_DEAD, "la dernière prise termine la partie");
    CHECK(g.lives == 0, "et il ne reste plus de vie (%u)", g.lives);

    const int64_t s = g.score;
    dedale_tick(&g, STEP);
    CHECK(g.score == s, "et une partie finie ne se joue plus");
}


/* Vider le labyrinthe fait passer au niveau suivant, et le remplit à nouveau. */
static void test_le_niveau_suivant(void)
{
    dedale g;
    dedale_reset(&g, 1, false);
    g.phase = DD_PLAYING;
    for (int i = 0; i < DD_ROTORS; ++i) g.rotor[i].respawn = 1e6f;

    /* On mange tout sauf la case sous le joueur, puis on l'y fait passer. */
    int px, py;
    px = (int)(g.x / DD_CELL);
    py = (int)(g.y / DD_CELL);
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if (g.tile[r][c] == DD_PELLET || g.tile[r][c] == DD_POWER) {
                if (r == py && c == px) continue;
                g.tile[r][c] = DD_EMPTY;
            }
    g.pellets_left = (uint32_t)dedale_count_pellets(&g);
    if (g.pellets_left == 0) { g.tile[py][px] = DD_PELLET; g.pellets_left = 1; }

    const uint32_t level = g.level;
    dedale_tick(&g, STEP);
    CHECK(g.level == level + 1, "le labyrinthe vidé fait monter d'un niveau (%u)", g.level);
    CHECK(g.pellets_left > 100, "et il se remplit (%u pastilles)", g.pellets_left);
}

/* -------------------------------------------------------------------------- */

typedef struct ledger {
    int64_t  total;
    uint32_t turns, dies;
} ledger;

static void drain(dedale *g, ledger *l)
{
    ns_game_events ev;
    SDL_zero(ev);
    g_dedale_api.events(g, &ev);
    if (ev.blip) {
        l->turns++;
        if (SDL_strcmp(ev.blip_kind, "turn") != 0) l->total += 999999;
    }
    if (ev.score) {
        if (SDL_strcmp(ev.score_kind, "pellet") == 0) l->total += 10;
        else if (SDL_strcmp(ev.score_kind, "power") == 0) l->total += 50;
        else if (SDL_strcmp(ev.score_kind, "ghost") == 0) l->total += 200;
        else if (SDL_strcmp(ev.score_kind, "level") == 0) l->total += 1000;
        else l->total += 999999;
    }
    if (ev.die) l->dies++;
}

static void play(dedale *g, uint64_t seed, ledger *l, float *seconds)
{
    memset(l, 0, sizeof *l);
    dedale_reset(g, seed, false);
    float t = 0.0f;
    for (int i = 0; i < 120 * 180 && l->dies == 0; ++i) {
        dedale_autopilot(g);
        dedale_tick(g, STEP);
        t += STEP;
        drain(g, l);
    }
    if (seconds) *seconds = t;
}

/* L'invariant central : le journal vaut le score. */
static void test_le_journal_vaut_le_score(void)
{
    for (uint64_t seed = 0; seed < 6; ++seed) {
        dedale g;
        ledger l;
        play(&g, 300u + seed, &l, NULL);
        if (l.total != g.score) {
            CHECK(false, "graine %llu : le serveur recalculerait %lld pour %lld affichés",
                  (unsigned long long)seed, (long long)l.total, (long long)g.score);
            return;
        }
    }
    CHECK(true, "six parties : le journal vaut le score, pastille pour pastille");
}

static void test_la_fin_est_annoncee(void)
{
    int silent = 0, deaths = 0;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        dedale g;
        ledger l;
        play(&g, 700u + seed, &l, NULL);
        if (g.phase == DD_DEAD) { deaths++; if (l.dies != 1) silent++; }
    }
    CHECK(deaths > 0, "le joueur automatique se fait attraper (%d fois sur 6)", deaths);
    CHECK(silent == 0, "et chaque mort est annoncée une fois (%d muettes)", silent);
}

static void test_determinisme(void)
{
    dedale a, b;
    ledger la, lb;
    play(&a, 8080, &la, NULL);
    play(&b, 8080, &lb, NULL);
    CHECK(a.score == b.score, "même graine, même score (%lld / %lld)",
          (long long)a.score, (long long)b.score);
    CHECK(memcmp(a.tile, b.tile, sizeof a.tile) == 0, "et le même labyrinthe consommé");
}

/* Le joueur automatique doit JOUER : manger, et survivre plus que quelques
 * secondes. C'est ce chiffre qui a montré que les hélices étaient trop
 * rapides — six parties en soixante secondes pour quarante points. */
/*
 * LA GRAINE DOIT SE VOIR. Deux graines différentes doivent donner deux parties
 * différentes — c'est la propriété qui manquait : la recette a mesuré cinq
 * graines, cinq parties identiques, et la même mort à 39,64 s.
 *
 * Le critère est volontairement faible — deux issues distinctes sur cinq
 * suffisent — parce qu'un joueur automatique déterministe dans un labyrinthe
 * fixe PEUT retomber sur le même résultat de temps en temps. Ce qu'on refuse,
 * c'est que les cinq soient identiques, ce qui prouverait que la graine n'entre
 * nulle part.
 */
static void test_la_graine_change_la_partie(void)
{
    int64_t scores[5];
    float   vies[5];
    for (int i = 0; i < 5; ++i) {
        dedale g;
        ledger l;
        float sec = 0.0f;
        play(&g, 9000u + (uint64_t)i, &l, &sec);
        scores[i] = g.score;
        vies[i] = sec;
    }
    int distincts = 1;
    for (int i = 1; i < 5; ++i) {
        bool neuf = true;
        for (int j = 0; j < i; ++j)
            if (scores[j] == scores[i] && vies[j] == vies[i]) neuf = false;
        if (neuf) distincts++;
    }
    /* Imprimé même quand ça passe : c'est la MESURE qui a motivé le correctif,
     * et un chiffre qu'on ne voit plus est un chiffre qui redevient faux sans
     * qu'on le sache. */
    for (int i = 0; i < 5; ++i)
        printf("  graine %d : %lld pts en %.2f s\n", 9000 + i,
               (long long)scores[i], (double)vies[i]);
    CHECK(distincts >= 2,
          "cinq graines donnent %d issue(s) distincte(s) — la graine entre "
          "dans le déroulement", distincts);
}

static void test_le_robot_joue(void)
{
    uint32_t eaten = 0;
    float total = 0.0f;
    for (uint64_t seed = 0; seed < 6; ++seed) {
        dedale g;
        ledger l;
        float sec = 0.0f;
        play(&g, 1500u + seed, &l, &sec);
        eaten += (uint32_t)(dedale_count_pellets(&g) > 0
                            ? (189u - (uint32_t)dedale_count_pellets(&g)) : 189u);
        total += sec;
    }
    CHECK(total / 6.0f > 8.0f,
          "il survit en moyenne %.1f s — les hélices laissent une chance",
          (double)(total / 6.0f));
    CHECK(eaten > 60, "et il mange (%u pastilles sur six parties)", eaten);
}

static void test_le_vocabulaire(void)
{
    dedale g;
    dedale_reset(&g, 4242, false);

    const char *seen[8] = { 0 };
    int count = 0;
    for (int i = 0; i < 120 * 180; ++i) {
        dedale_autopilot(&g);
        dedale_tick(&g, STEP);
        ns_game_events ev;
        SDL_zero(ev);
        g_dedale_api.events(&g, &ev);
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
        for (const char *const *k = g_dedale_api.event_kinds; *k; ++k)
            if (SDL_strcmp(*k, seen[i]) == 0) declared = true;
        CHECK(declared, "« %s » est émis : il doit être déclaré", seen[i]);
    }
    CHECK(count >= 2, "au moins turn et pellet sont émis (%d types)", count);
}

int main(void)
{
    test_le_labyrinthe_tient();
    test_les_tunnels();
    test_le_bareme();
    test_la_super_pastille();
    test_l_helice_tue();
    test_le_niveau_suivant();
    test_le_journal_vaut_le_score();
    test_la_fin_est_annoncee();
    test_determinisme();
    test_la_graine_change_la_partie();
    test_le_robot_joue();
    test_le_vocabulaire();

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
