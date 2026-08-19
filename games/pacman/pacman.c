/* pacman.c — voir pacman.h pour ce qui vient de 2020 et ce qui a dû être écrit. */
#include "pacman.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Le labyrinthe
 * ==========================================================================
 * Écrit en clair : c'est la seule forme dans laquelle on peut le RELIRE. Un
 * labyrinthe encodé en bits se corrige à l'aveugle ; celui-ci se corrige en
 * regardant.
 *
 * `#` mur, `.` pastille, `o` super-pastille, `-` la porte de l'enclos, `P` le
 * départ du joueur, ` ` couloir vide. Vingt et une colonnes sur vingt et une
 * lignes, **symétrique gauche-droite**, avec les deux tunnels latéraux à
 * mi-hauteur et l'enclos des fantômes au centre.
 *
 * Trois propriétés que `tests/test_pacman.c` vérifie, parce qu'un labyrinthe
 * relu à l'œil ment : toutes les lignes font vingt et une colonnes, **toutes
 * les pastilles sont atteignables** depuis le départ — sinon le niveau ne se
 * termine jamais — et **l'enclos communique avec le labyrinthe**. Le premier
 * jet échouait sur le troisième point : les fantômes naissaient dans une poche
 * fermée et n'en sortaient jamais.
 * ========================================================================== */

static const char *const PM_MAZE[PM_ROWS] = {
    "#####################",
    "#........#.#........#",
    "#o##.###.#.#.###.##o#",
    "#.##.###.#.#.###.##.#",
    "#...................#",
    "#.##.#.#######.#.##.#",
    "#....#...#.#...#....#",
    "####.###.#.#.###.####",
    "   #.#.........#.#   ",
    "####.#.## - ##.#.####",
    "......  #   #  ......",
    "####.#.## # ##.#.####",
    "   #.#.........#.#   ",
    "####.###.#.#.###.####",
    "#....#...#.#...#....#",
    "#.##.#.#######.#.##.#",
    "#o........P........o#",
    "#.###.###.#.###.###.#",
    "#.....#.......#.....#",
    "#.#######.#.#######.#",
    "#####################",
};

/* La maison des fantômes : le centre du labyrinthe. */
#define PM_HOME_COL 10
#define PM_HOME_ROW 10

/* `VITESSE_DEPLACEMENT / (FPS/30)` = 2 px par image à 60 Hz = 120 px/s.
 * Une case fait 40 px : trois cases par seconde. */
#define PM_SPEED       120.0f
/*
 * 75 % de la vitesse du joueur, comme la borne de 1980 aux premiers niveaux.
 *
 * À 90 % la partie ne durait pas quinze secondes : quatre poursuivants presque
 * aussi rapides que soi, dans un labyrinthe de vingt et une cases, ne laissent
 * aucune fuite. Mesuré avec le joueur automatique — neuf parties en deux
 * minutes, cent soixante points. C'est la marge de vitesse qui FAIT le jeu :
 * sans elle, il n'y a pas de poursuite, il y a une exécution.
 */
#define PM_GHOST_SPEED 90.0f
#define PM_FRIGHT_SPEED 62.0f
#define PM_EATEN_SPEED 240.0f     /* les yeux rentrent vite */

/* Le barème de `rulesTable["pacman"]`. */
#define PTS_PELLET 10
#define PTS_POWER  50
#define PTS_GHOST  200
#define PTS_LEVEL  1000

#define PM_FRIGHT_TIME 7.0f
/* L'alternance dispersion / poursuite du jeu d'arcade : les fantômes lâchent
 * régulièrement leur proie et repartent dans leur coin. Sans elle, quatre
 * poursuivants convergent et il n'y a plus de jeu. */
#define PM_SCATTER_ON  7.0f   /* la borne de 1980 commence par sept secondes */
#define PM_SCATTER_OFF 20.0f

/* ==========================================================================
 * La grille
 * ========================================================================== */

bool pacman_walkable(const pacman *g, int col, int row)
{
    if (row < 0 || row >= PM_ROWS) return false;
    /* Les tunnels : sortir par la gauche revient par la droite. */
    if (col < 0 || col >= PM_COLS) return true;
    return g->tile[row][col] != PM_WALL;
}

int pacman_count_pellets(const pacman *g)
{
    int n = 0;
    for (int r = 0; r < PM_ROWS; ++r)
        for (int c = 0; c < PM_COLS; ++c)
            if (g->tile[r][c] == PM_PELLET || g->tile[r][c] == PM_POWER) n++;
    return n;
}

static void cell_of(float x, float y, int *col, int *row)
{
    *col = (int)floorf(x / PM_CELL);
    *row = (int)floorf(y / PM_CELL);
}

static void centre_of(int col, int row, float *x, float *y)
{
    *x = ((float)col + 0.5f) * PM_CELL;
    *y = ((float)row + 0.5f) * PM_CELL;
}

static const int DIR_DX[PM_DIR_COUNT] = { +1, 0, -1, 0 };
static const int DIR_DY[PM_DIR_COUNT] = { 0, -1, 0, +1 };

static float wrap_x(float x)
{
    const float w = PM_COLS * PM_CELL;
    while (x < 0.0f) x += w;
    while (x >= w) x -= w;
    return x;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

static void place_ghosts(pacman *g)
{
    /* Les quatre emplacements de l'enclos : les trois cases intérieures et la
     * porte. Le premier jet en posait un sur la case DU DESSOUS, qui est un
     * mur — il y restait bloqué toute la partie, et rien ne le signalait. */
    static const int SPOT_DX[PM_GHOSTS] = { -1, 0, +1, 0 };
    static const int SPOT_DY[PM_GHOSTS] = { 0, 0, 0, -1 };
    for (int i = 0; i < PM_GHOSTS; ++i) {
        centre_of(PM_HOME_COL + SPOT_DX[i], PM_HOME_ROW + SPOT_DY[i],
                  &g->ghost[i].x, &g->ghost[i].y);
        g->ghost[i].dir = (i & 1) ? PM_LEFT : PM_UP;
        g->ghost[i].kind = i;
        g->ghost[i].frightened = 0.0f;
        g->ghost[i].eaten = false;
        /* Ils sortent l'un après l'autre : quatre fantômes lâchés ensemble sur
         * un joueur qui démarre, c'est une mort et pas une partie. */
        g->ghost[i].respawn = (float)i * 4.0f;
    }
}

static void load_maze(pacman *g)
{
    for (int r = 0; r < PM_ROWS; ++r) {
        for (int c = 0; c < PM_COLS; ++c) {
            const char ch = PM_MAZE[r][c];
            switch (ch) {
                case '#': g->tile[r][c] = PM_WALL;   break;
                case '.': g->tile[r][c] = PM_PELLET; break;
                case 'o': g->tile[r][c] = PM_POWER;  break;
                default:  g->tile[r][c] = PM_EMPTY;  break;
            }
        }
    }
    g->pellets_left = (uint32_t)pacman_count_pellets(g);
}

void pacman_reset(pacman *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0x9ACu);
    g->hard = hard;
    g->phase = PM_READY;
    g->level = 1;

    load_maze(g);
    /* Le 'P' du plan donne le départ, pour que la position soit LUE et non
     * recopiée à côté du labyrinthe qu'elle doit suivre. */
    for (int r = 0; r < PM_ROWS; ++r)
        for (int c = 0; c < PM_COLS; ++c)
            if (PM_MAZE[r][c] == 'P') centre_of(c, r, &g->x, &g->y);

    g->dir = g->want = PM_LEFT;
    g->scattering = true;
    g->scatter_timer = PM_SCATTER_ON;
    place_ghosts(g);
}

static void next_level(pacman *g)
{
    g->level++;
    g->score += PTS_LEVEL;
    g->pend_level++;
    load_maze(g);
    for (int r = 0; r < PM_ROWS; ++r)
        for (int c = 0; c < PM_COLS; ++c)
            if (PM_MAZE[r][c] == 'P') centre_of(c, r, &g->x, &g->y);
    g->dir = g->want = PM_LEFT;
    place_ghosts(g);
}

void pacman_press(pacman *g, ns_game_button b)
{
    if (g->phase == PM_DEAD) return;
    if (g->phase == PM_READY) g->phase = PM_PLAYING;
    switch (b) {
        case NS_GAME_RIGHT: g->want = PM_RIGHT; g->turned = true; break;
        case NS_GAME_UP:    g->want = PM_UP;    g->turned = true; break;
        case NS_GAME_LEFT:  g->want = PM_LEFT;  g->turned = true; break;
        case NS_GAME_DOWN:  g->want = PM_DOWN;  g->turned = true; break;
        default: break;
    }
}

void pacman_hold(pacman *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    for (int i = 0; i < NS_GAME_BUTTON_COUNT; ++i) g->held[i] = held[i];
    /* Le maintien vaut intention : sur une borne, on tient la direction jusqu'à
     * ce que le virage soit possible. C'est ce qui rend les angles jouables. */
    if (held[NS_GAME_RIGHT]) g->want = PM_RIGHT;
    else if (held[NS_GAME_UP]) g->want = PM_UP;
    else if (held[NS_GAME_LEFT]) g->want = PM_LEFT;
    else if (held[NS_GAME_DOWN]) g->want = PM_DOWN;
}

/*
 * Un déplacement sur grille avec un CENTRE de case.
 *
 * Le mobile n'avance que le long de sa direction ; il ne peut tourner qu'au
 * centre d'une case, et seulement si la case visée est libre. C'est ce qui
 * empêche de couper les murs en diagonale, et c'est la seule façon d'avoir des
 * virages qui « collent » comme dans l'original.
 */
static void step_actor(const pacman *g, float *x, float *y, pm_dir *dir,
                       pm_dir want, float speed, float dt)
{
    int col, row;
    cell_of(*x, *y, &col, &row);
    float cx, cy;
    centre_of(col, row, &cx, &cy);

    const bool at_centre = (fabsf(*x - cx) < 1.0f && fabsf(*y - cy) < 1.0f);
    if (at_centre) {
        if (want != *dir && pacman_walkable(g, col + DIR_DX[want], row + DIR_DY[want])) {
            *dir = want;
            *x = cx; *y = cy;
        }
        if (!pacman_walkable(g, col + DIR_DX[*dir], row + DIR_DY[*dir])) {
            *x = cx; *y = cy;
            return;   /* mur devant, et le virage voulu n'est pas praticable */
        }
    }

    *x += (float)DIR_DX[*dir] * speed * dt;
    *y += (float)DIR_DY[*dir] * speed * dt;
    *x = wrap_x(*x);

    /* On ne dépasse jamais le centre d'une case dont la suivante est un mur. */
    cell_of(*x, *y, &col, &row);
    centre_of(col, row, &cx, &cy);
    if (!pacman_walkable(g, col + DIR_DX[*dir], row + DIR_DY[*dir])) {
        if (DIR_DX[*dir] > 0 && *x > cx) *x = cx;
        if (DIR_DX[*dir] < 0 && *x < cx) *x = cx;
        if (DIR_DY[*dir] > 0 && *y > cy) *y = cy;
        if (DIR_DY[*dir] < 0 && *y < cy) *y = cy;
    }
}

/*
 * Les quatre comportements du jeu d'arcade, et ils sont vraiment quatre.
 *
 *   0 — poursuite directe : il vise la case du joueur ;
 *   1 — embuscade : il vise quatre cases DEVANT le joueur ;
 *   2 — dispersion : il vise son coin, toujours ;
 *   3 — aléatoire : il choisit au hasard à chaque intersection.
 *
 * C'est ce mélange qui fait qu'on ne peut pas fuir en ligne droite, et c'est
 * ce qui manquait entièrement en 2020.
 */
static void ghost_target(const pacman *g, const pm_ghost *gh, int *tc, int *tr)
{
    int pc, pr;
    cell_of(g->x, g->y, &pc, &pr);

    if (gh->eaten) { *tc = PM_HOME_COL; *tr = PM_HOME_ROW; return; }

    static const int CORNER_C[PM_GHOSTS] = { PM_COLS - 2, 1, PM_COLS - 2, 1 };
    static const int CORNER_R[PM_GHOSTS] = { 1, 1, PM_ROWS - 2, PM_ROWS - 2 };

    if (gh->frightened > 0.0f || (g->scattering && gh->kind != 3)) {
        *tc = CORNER_C[gh->kind];
        *tr = CORNER_R[gh->kind];
        return;
    }
    switch (gh->kind) {
        case 0: *tc = pc; *tr = pr; break;
        case 1: *tc = pc + DIR_DX[g->dir] * 4; *tr = pr + DIR_DY[g->dir] * 4; break;
        case 2: *tc = CORNER_C[2]; *tr = CORNER_R[2]; break;
        default: *tc = -1; *tr = -1; break;   /* aléatoire : pas de cible */
    }
}

static void ghost_choose(pacman *g, pm_ghost *gh)
{
    int col, row;
    cell_of(gh->x, gh->y, &col, &row);
    float cx, cy;
    centre_of(col, row, &cx, &cy);
    if (fabsf(gh->x - cx) > 1.0f || fabsf(gh->y - cy) > 1.0f) return;

    int tc, tr;
    ghost_target(g, gh, &tc, &tr);

    /* Un fantôme ne fait JAMAIS demi-tour, sauf s'il n'a pas le choix. C'est la
     * règle qui les empêche de vibrer sur place, et elle est d'origine. */
    const pm_dir back = (pm_dir)((gh->dir + 2) % PM_DIR_COUNT);

    pm_dir best = gh->dir;
    float best_d = 1e30f;
    int options = 0;
    pm_dir first = gh->dir;

    for (int d = 0; d < PM_DIR_COUNT; ++d) {
        if ((pm_dir)d == back) continue;
        const int nc = col + DIR_DX[d], nr = row + DIR_DY[d];
        if (!pacman_walkable(g, nc, nr)) continue;
        options++;
        first = (pm_dir)d;
        if (tc < 0) continue;
        const float dx = (float)(nc - tc), dy = (float)(nr - tr);
        const float dist = dx * dx + dy * dy;
        if (dist < best_d) { best_d = dist; best = (pm_dir)d; }
    }
    if (options == 0) { gh->dir = back; return; }
    if (tc < 0) {
        /* Le hasard : on retire une des sorties possibles. */
        int pick = (int)ns_rng_below(&g->rng, (uint32_t)options);
        for (int d = 0; d < PM_DIR_COUNT; ++d) {
            if ((pm_dir)d == back) continue;
            if (!pacman_walkable(g, col + DIR_DX[d], row + DIR_DY[d])) continue;
            if (pick-- == 0) { gh->dir = (pm_dir)d; return; }
        }
        gh->dir = first;
        return;
    }
    gh->dir = best;
}

static void eat_tile(pacman *g)
{
    int col, row;
    cell_of(g->x, g->y, &col, &row);
    if (col < 0 || col >= PM_COLS || row < 0 || row >= PM_ROWS) return;

    if (g->tile[row][col] == PM_PELLET) {
        g->tile[row][col] = PM_EMPTY;
        g->pellets_left--;
        g->score += PTS_PELLET;
        g->pend_pellet++;
    } else if (g->tile[row][col] == PM_POWER) {
        g->tile[row][col] = PM_EMPTY;
        g->pellets_left--;
        g->score += PTS_POWER;
        g->pend_power++;
        g->chain = 0;
        for (int i = 0; i < PM_GHOSTS; ++i) {
            if (g->ghost[i].eaten) continue;
            g->ghost[i].frightened = PM_FRIGHT_TIME;
            /* Ils font demi-tour : c'est le signal visuel qui dit qu'on a la
             * main. */
            g->ghost[i].dir = (pm_dir)((g->ghost[i].dir + 2) % PM_DIR_COUNT);
        }
    }
    if (g->pellets_left == 0) next_level(g);
}

void pacman_tick(pacman *g, float dt)
{
    if (g->phase == PM_DEAD) { g->dead_time += dt; return; }
    g->time += dt;
    if (g->phase == PM_READY) return;

    g->mouth += dt * 9.0f;

    /* L'alternance dispersion / poursuite. */
    g->scatter_timer -= dt;
    if (g->scatter_timer <= 0.0f) {
        g->scattering = !g->scattering;
        g->scatter_timer = g->scattering ? PM_SCATTER_ON : PM_SCATTER_OFF;
    }

    step_actor(g, &g->x, &g->y, &g->dir, g->want, PM_SPEED, dt);
    eat_tile(g);

    for (int i = 0; i < PM_GHOSTS; ++i) {
        pm_ghost *gh = &g->ghost[i];
        if (gh->respawn > 0.0f) { gh->respawn -= dt; continue; }
        if (gh->frightened > 0.0f) gh->frightened -= dt;

        float speed = PM_GHOST_SPEED;
        if (gh->eaten) speed = PM_EATEN_SPEED;
        else if (gh->frightened > 0.0f) speed = PM_FRIGHT_SPEED;
        if (g->hard && !gh->eaten && gh->frightened <= 0.0f) speed *= 1.15f;

        ghost_choose(g, gh);
        pm_dir keep = gh->dir;
        step_actor(g, &gh->x, &gh->y, &gh->dir, keep, speed, dt);

        if (gh->eaten) {
            const float dx = gh->x - ((float)PM_HOME_COL + 0.5f) * PM_CELL;
            const float dy = gh->y - ((float)PM_HOME_ROW + 0.5f) * PM_CELL;
            if (dx * dx + dy * dy < 12.0f * 12.0f) {
                gh->eaten = false;
                gh->frightened = 0.0f;
                gh->respawn = 1.5f;
            }
            continue;
        }

        const float dx = gh->x - g->x, dy = gh->y - g->y;
        if (dx * dx + dy * dy > 20.0f * 20.0f) continue;

        if (gh->frightened > 0.0f) {
            /* 200, 400, 800, 1 600 : la chaîne du jeu d'arcade. Le serveur
             * compte chaque fantôme à 200, donc on émet autant d'événements que
             * la valeur le demande — c'est ce qui garde le journal et le score
             * d'accord. */
            g->chain++;
            const uint32_t times = (g->chain > 4) ? 8u : (1u << (g->chain - 1));
            g->score += (int64_t)PTS_GHOST * times;
            g->pend_ghost += times;
            gh->eaten = true;
            gh->frightened = 0.0f;
        } else {
            g->phase = PM_DEAD;
            g->dead_time = 0.0f;
            g->died = true;
            return;
        }
    }
}

/* ==========================================================================
 * Le joueur automatique
 * ==========================================================================
 * Un parcours en largeur depuis la case du joueur, avec les fantômes marqués
 * infranchissables tant qu'ils ne sont pas mangeables. Il vise la pastille la
 * plus proche, ou le fantôme le plus proche quand ils fuient.
 * ========================================================================== */

bool pacman_autopilot(pacman *g)
{
    if (g->phase == PM_DEAD) return false;
    if (g->phase == PM_READY) g->phase = PM_PLAYING;

    int sc, sr;
    cell_of(g->x, g->y, &sc, &sr);
    if (sc < 0 || sc >= PM_COLS || sr < 0 || sr >= PM_ROWS) return true;

    /* Les cases dangereuses : la position d'un fantôme et ses voisines. */
    bool danger[PM_ROWS][PM_COLS];
    memset(danger, 0, sizeof danger);
    bool hunting = false;
    for (int i = 0; i < PM_GHOSTS; ++i) {
        const pm_ghost *gh = &g->ghost[i];
        if (gh->respawn > 0.0f || gh->eaten) continue;
        if (gh->frightened > 0.35f) { hunting = true; continue; }
        int c, r;
        cell_of(gh->x, gh->y, &c, &r);
        /*
         * La case du fantôme, et les DEUX qu'il a devant lui. Pas les huit
         * voisines : dans un couloir d'une case de large, une croix de trois
         * sur trois autour de quatre fantômes bouche à peu près tout, le
         * parcours en largeur ne trouve plus rien, et le repli fonce droit
         * dans le décor. Mesuré : six parties en soixante secondes, quarante
         * points. Ce qui est dangereux, c'est là où il va.
         */
        for (int k = 0; k <= 2; ++k) {
            const int cc = ((c + DIR_DX[gh->dir] * k) + PM_COLS) % PM_COLS;
            const int rr = r + DIR_DY[gh->dir] * k;
            if (rr < 0 || rr >= PM_ROWS) continue;
            danger[rr][cc] = true;
        }
    }

    /* Parcours en largeur : `from[]` garde la première direction empruntée. */
    int8_t first[PM_ROWS][PM_COLS];
    memset(first, -1, sizeof first);
    int16_t queue[PM_ROWS * PM_COLS][2];
    int head = 0, tail = 0;

    for (int d = 0; d < PM_DIR_COUNT; ++d) {
        const int nc = ((sc + DIR_DX[d]) + PM_COLS) % PM_COLS;
        const int nr = sr + DIR_DY[d];
        if (!pacman_walkable(g, nc, nr)) continue;
        if (nr < 0 || nr >= PM_ROWS) continue;
        if (danger[nr][nc]) continue;
        if (first[nr][nc] >= 0) continue;
        first[nr][nc] = (int8_t)d;
        queue[tail][0] = (int16_t)nc;
        queue[tail][1] = (int16_t)nr;
        tail++;
    }

    int goal_dir = -1;
    while (head < tail && goal_dir < 0) {
        const int c = queue[head][0], r = queue[head][1];
        head++;
        const uint8_t t = g->tile[r][c];
        bool want = (t == PM_PELLET || t == PM_POWER);
        if (hunting) {
            for (int i = 0; i < PM_GHOSTS; ++i) {
                if (g->ghost[i].frightened <= 0.35f || g->ghost[i].eaten) continue;
                int gc, gr;
                cell_of(g->ghost[i].x, g->ghost[i].y, &gc, &gr);
                if (gc == c && gr == r) want = true;
            }
        }
        if (want) { goal_dir = first[r][c]; break; }

        for (int d = 0; d < PM_DIR_COUNT; ++d) {
            const int nc = ((c + DIR_DX[d]) + PM_COLS) % PM_COLS;
            const int nr = r + DIR_DY[d];
            if (nr < 0 || nr >= PM_ROWS) continue;
            if (!pacman_walkable(g, nc, nr)) continue;
            if (danger[nr][nc]) continue;
            if (first[nr][nc] >= 0) continue;
            first[nr][nc] = first[r][c];
            queue[tail][0] = (int16_t)nc;
            queue[tail][1] = (int16_t)nr;
            tail++;
        }
    }

    if (goal_dir < 0) {
        /*
         * Cerné : on prend la sortie qui ÉLOIGNE le plus du fantôme le plus
         * proche, pas la première venue. Un Pac-Man immobile est un Pac-Man
         * mort, mais un Pac-Man qui fonce dans le premier couloir libre l'est
         * tout autant — et c'est ce que faisait le premier jet.
         */
        float best_gap = -1.0f;
        for (int d = 0; d < PM_DIR_COUNT; ++d) {
            const int nc = ((sc + DIR_DX[d]) + PM_COLS) % PM_COLS;
            const int nr = sr + DIR_DY[d];
            if (nr < 0 || nr >= PM_ROWS) continue;
            if (!pacman_walkable(g, nc, nr)) continue;
            float gap = 1e30f;
            for (int i = 0; i < PM_GHOSTS; ++i) {
                const pm_ghost *gh = &g->ghost[i];
                if (gh->respawn > 0.0f || gh->eaten || gh->frightened > 0.0f) continue;
                const float dx = gh->x - ((float)nc + 0.5f) * PM_CELL;
                const float dy = gh->y - ((float)nr + 0.5f) * PM_CELL;
                const float d2 = dx * dx + dy * dy;
                if (d2 < gap) gap = d2;
            }
            if (gap > best_gap) { best_gap = gap; goal_dir = d; }
        }
    }
    if (goal_dir >= 0) pacman_press(g, (goal_dir == PM_RIGHT) ? NS_GAME_RIGHT
                                     : (goal_dir == PM_UP)    ? NS_GAME_UP
                                     : (goal_dir == PM_LEFT)  ? NS_GAME_LEFT
                                                              : NS_GAME_DOWN);
    return true;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

void pacman_draw(ns_sprite *s, const pacman *g, const pacman_art *a,
                 float logical_w, float logical_h)
{
    const float board_w = PM_COLS * PM_CELL, board_h = PM_ROWS * PM_CELL;
    const float sx = logical_w / PM_LOGICAL_W, sy = logical_h / PM_LOGICAL_H;
    const float base = (sx < sy) ? sx : sy;
    /* Le plateau prend la hauteur disponible : sur une dalle de borne, un
     * plateau de 840 px dans 1080 laisserait un quart de l'écran vide. */
    const float scale = base * (PM_LOGICAL_H * 0.94f) / board_h;
    const float ox = (logical_w - board_w * scale) * 0.5f;
    const float oy = (logical_h - board_h * scale) * 0.5f;

    static const float back[4] = { 0.02f, 0.02f, 0.06f, 1.0f };
    ns_sprite_rect(s, 0.0f, 0.0f, logical_w, logical_h, back);

    /* Les murs occupent la case ENTIÈRE : à trois pixels de retrait ils se
     * lisaient comme une grille de blocs séparés au lieu d'un labyrinthe. */
    static const float wall[4]   = { 0.11f, 0.14f, 0.52f, 1.0f };
    static const float pellet[4] = { 1.00f, 0.86f, 0.62f, 1.0f };
    static const float power[4]  = { 1.00f, 0.62f, 0.24f, 1.0f };

    for (int r = 0; r < PM_ROWS; ++r) {
        for (int c = 0; c < PM_COLS; ++c) {
            const float x = ox + (float)c * PM_CELL * scale;
            const float y = oy + (float)r * PM_CELL * scale;
            if (g->tile[r][c] == PM_WALL) {
                ns_sprite_rect(s, x, y, PM_CELL * scale, PM_CELL * scale, wall);
            } else if (g->tile[r][c] == PM_PELLET) {
                ns_sprite_rect(s, x + (PM_CELL * 0.5f - 3.0f) * scale,
                               y + (PM_CELL * 0.5f - 3.0f) * scale,
                               6.0f * scale, 6.0f * scale, pellet);
            } else if (g->tile[r][c] == PM_POWER) {
                const float p = 8.0f + 3.0f * sinf(g->time * 6.0f);
                ns_sprite_rect(s, x + (PM_CELL * 0.5f - p) * scale,
                               y + (PM_CELL * 0.5f - p) * scale,
                               p * 2.0f * scale, p * 2.0f * scale, power);
            }
        }
    }

    /* Les fantômes. Quatre couleurs, plus le bleu de fuite et les yeux seuls. */
    static const float GH[PM_GHOSTS][4] = {
        { 1.00f, 0.24f, 0.20f, 1.0f },
        { 1.00f, 0.65f, 0.82f, 1.0f },
        { 0.36f, 0.90f, 0.95f, 1.0f },
        { 1.00f, 0.72f, 0.28f, 1.0f },
    };
    /*
     * Un fantôme apeuré doit se DISTINGUER du mur. Le bleu sombre de la borne
     * de 1980 marchait sur un labyrinthe bleu clair ; ici les murs sont bleus,
     * et quatre fantômes bleus dessus étaient tout simplement invisibles — la
     * capture le montrait sans appel. Blanc bleuté, donc, et clignotant sur la
     * dernière seconde et demie, comme l'original prévient que ça se termine.
     */
    static const float fright[4] = { 0.88f, 0.94f, 1.00f, 1.0f };
    static const float fright2[4] = { 0.30f, 0.42f, 1.00f, 1.0f };
    static const float eyes[4]   = { 0.86f, 0.92f, 1.00f, 1.0f };
    for (int i = 0; i < PM_GHOSTS; ++i) {
        const pm_ghost *gh = &g->ghost[i];
        if (gh->respawn > 0.0f) continue;
        const float *col = GH[gh->kind];
        if (gh->eaten) col = eyes;
        else if (gh->frightened > 0.0f) {
            const bool blink = (gh->frightened < 1.5f)
                            && (((int)(gh->frightened * 8.0f)) & 1);
            col = blink ? fright2 : fright;
        }
        const float sz = gh->eaten ? 14.0f : 28.0f;
        ns_sprite_rect(s, ox + (gh->x - sz * 0.5f) * scale,
                       oy + (gh->y - sz * 0.5f) * scale, sz * scale, sz * scale, col);
    }

    /* Le héros : un carré dont la bouche s'ouvre, dessinée en retirant un coin
     * du côté où il regarde. La planche de 2020 est un atlas 4 x 4 ; on garde
     * son rythme d'ouverture sans en dépendre. */
    if (g->phase != PM_DEAD) {
        static const float hero[4] = { 1.00f, 0.92f, 0.24f, 1.0f };
        const float sz = 30.0f;
        ns_sprite_rect(s, ox + (g->x - sz * 0.5f) * scale, oy + (g->y - sz * 0.5f) * scale,
                       sz * scale, sz * scale, hero);
        const float open = 0.5f + 0.5f * sinf(g->mouth);
        if (open > 0.15f) {
            const float m = sz * 0.5f * open;
            const float mx = g->x + (float)DIR_DX[g->dir] * sz * 0.30f;
            const float my = g->y + (float)DIR_DY[g->dir] * sz * 0.30f;
            ns_sprite_rect(s, ox + (mx - m * 0.5f) * scale, oy + (my - m * 0.5f) * scale,
                           m * scale, m * scale, back);
        }
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    char line[64];
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    ns_sprite_text(s, 30.0f * base, 24.0f * base, base * 6.0f, white, line);
    SDL_snprintf(line, sizeof line, "NIVEAU %u", g->level);
    ns_sprite_text(s, 30.0f * base, 100.0f * base, base * 3.4f, amber, line);
    (void)a;

    if (g->phase == PM_READY) {
        const char *msg = "MANCHE POUR SE DIRIGER";
        const float sc = base * 3.6f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(msg, sc)) * 0.5f,
                       logical_h * 0.94f, sc, white, msg);
    } else if (g->phase == PM_DEAD) {
        static const float veil[4] = { 0.03f, 0.03f, 0.05f, 0.78f };
        ns_sprite_rect(s, 0.0f, logical_h * 0.33f, logical_w, logical_h * 0.34f, veil);
        const char *msg = "PERDU";
        const float sc = base * 9.0f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(msg, sc)) * 0.5f,
                       logical_h * 0.40f, sc, amber, msg);
        SDL_snprintf(line, sizeof line, "MEILLEUR %u", g->best);
        const float sc2 = base * 5.0f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(line, sc2)) * 0.5f,
                       logical_h * 0.52f, sc2, white, line);
        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            ns_sprite_text(s, (logical_w - ns_sprite_text_width(again, sc2)) * 0.5f,
                           logical_h * 0.60f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool pacman_art_load(ns_rhi *r, pacman_art *a)
{
    memset(a, 0, sizeof *a);
    /*
     * Les planches de 2020 font 88 x 22 et 88 x 44 : quatre images de 22 px de
     * côté. À l'échelle de ce portage — une case de 40 px sur une dalle de
     * borne — elles seraient plus floues que les formes pleines qu'on dessine.
     * Elles sont chargées pour que le jeu puisse les employer le jour où on les
     * agrandira ; l'affichage, lui, n'en dépend pas, et c'est dit plutôt que
     * caché derrière une texture qui ne se voit pas.
     */
    a->ready = ns_texture_load(r, &a->hero, "games/pacman/pacman.png", true, false);
    if (a->ready) ns_texture_load(r, &a->enemy, "games/pacman/enemy.png", true, false);
    return a->ready;
}

void pacman_art_free(ns_rhi *r, pacman_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->hero);
    if (a->enemy.handle) ns_texture_destroy(r, &a->enemy);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void pm_reset(void *g, uint64_t seed, bool hard) { pacman_reset((pacman *)g, seed, hard); }
static void pm_press(void *g, ns_game_button b) { pacman_press((pacman *)g, b); }
static void pm_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { pacman_hold((pacman *)g, h); }
static void pm_tick(void *g, float dt) { pacman_tick((pacman *)g, dt); }

static void pm_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    pacman_draw(s, (const pacman *)g, (const pacman_art *)a, w, h);
}

static bool pm_art_load(ns_rhi *r, void *a) { return pacman_art_load(r, (pacman_art *)a); }
static void pm_art_free(ns_rhi *r, void *a) { pacman_art_free(r, (pacman_art *)a); }
static bool pm_autopilot(void *g) { return pacman_autopilot((pacman *)g); }

static uint32_t pm_score(const void *g)
{
    const int64_t v = ((const pacman *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t pm_best(const void *g) { return ((const pacman *)g)->best; }
static void     pm_set_best(void *g, uint32_t b) { ((pacman *)g)->best = b; }

static bool pm_dead(const void *g, float *dead_time)
{
    const pacman *p = (const pacman *)g;
    if (dead_time) *dead_time = p->dead_time;
    return p->phase == PM_DEAD;
}

/* Le vocabulaire de `rulesTable["pacman"]`, tel qu'il a toujours été écrit :
 * pastille 10, super-pastille 50, fantôme 200, niveau 1 000. La table attendait
 * un Pac-Man complet ; c'est maintenant le cas. */
static const char *const pm_kinds[] = { "pellet", "power", "ghost", "level", "turn", "death", NULL };

static void pm_events(void *g, ns_game_events *out)
{
    pacman *p = (pacman *)g;

    out->blip = p->turned;
    out->blip_kind = "turn";
    p->turned = false;

    if (p->pend_pellet)     { out->score = true; out->score_kind = "pellet"; p->pend_pellet--; }
    else if (p->pend_power) { out->score = true; out->score_kind = "power";  p->pend_power--; }
    else if (p->pend_ghost) { out->score = true; out->score_kind = "ghost";  p->pend_ghost--; }
    else if (p->pend_level) { out->score = true; out->score_kind = "level";  p->pend_level--; }
    out->score_value = 0;   /* barème FIXE côté serveur : la valeur ne voyage pas */

    if (p->died && !p->pend_pellet && !p->pend_power && !p->pend_ghost && !p->pend_level) {
        out->die = true;
        p->died = false;
    }
}

const ns_game_api g_pacman_api = {
    .id = "pacman", .title = "PAC-MAN", .label = "PACMAN",
    .state_size = sizeof(pacman), .art_size = sizeof(pacman_art),
    .sound_blip = NULL,
    .sound_score = "games/flappy/score.wav",
    .sound_die = "games/snake/gameover.wav",
    .art_load = pm_art_load, .art_free = pm_art_free,
    .reset = pm_reset, .press = pm_press, .hold = pm_hold,
    .tick = pm_tick, .draw = pm_draw, .autopilot = pm_autopilot,
    .event_kinds = pm_kinds,
    .score = pm_score, .best = pm_best, .set_best = pm_set_best,
    .dead = pm_dead, .events = pm_events,
};
