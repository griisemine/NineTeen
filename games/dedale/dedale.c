/* dedale.c — voir dedale.h pour ce qui vient de 2020 et ce qui a dû être écrit. */
#include "dedale.h"

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
 * mi-hauteur et l'enclos des hélices au centre.
 *
 * Trois propriétés que `tests/test_dedale.c` vérifie, parce qu'un labyrinthe
 * relu à l'œil ment : toutes les lignes font vingt et une colonnes, **toutes
 * les pastilles sont atteignables** depuis le départ — sinon le niveau ne se
 * termine jamais — et **l'enclos communique avec le labyrinthe**. Le premier
 * jet échouait sur le troisième point : les hélices naissaient dans une poche
 * fermée et n'en sortaient jamais.
 * ========================================================================== */

static const char *const DD_MAZE[DD_ROWS] = {
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

/* La maison des hélices : le centre du labyrinthe. */
#define DD_HOME_COL 10
#define DD_HOME_ROW 10

/* `VITESSE_DEPLACEMENT / (FPS/30)` = 2 px par image à 60 Hz = 120 px/s.
 * Une case fait 40 px : trois cases par seconde. */
#define DD_SPEED       120.0f
/*
 * 75 % de la vitesse du joueur, comme la borne de 1980 aux premiers niveaux.
 *
 * À 90 % la partie ne durait pas quinze secondes : quatre poursuivants presque
 * aussi rapides que soi, dans un labyrinthe de vingt et une cases, ne laissent
 * aucune fuite. Mesuré avec le joueur automatique — neuf parties en deux
 * minutes, cent soixante points. C'est la marge de vitesse qui FAIT le jeu :
 * sans elle, il n'y a pas de poursuite, il y a une exécution.
 */
#define DD_ROTOR_SPEED 90.0f
#define DD_FRIGHT_SPEED 62.0f
#define DD_EATEN_SPEED 240.0f     /* les yeux rentrent vite */

/* Le barème de `rulesTable["dedale"]`. */
#define PTS_PELLET 10
#define PTS_POWER  50
#define PTS_ROTOR  200
#define PTS_LEVEL  1000

#define DD_FRIGHT_TIME 7.0f
/* L'alternance dispersion / poursuite du jeu d'arcade : les hélices lâchent
 * régulièrement leur proie et repartent dans leur coin. Sans elle, quatre
 * poursuivants convergent et il n'y a plus de jeu. */
#define DD_SCATTER_ON  7.0f   /* la borne de 1980 commence par sept secondes */
#define DD_SCATTER_OFF 20.0f

/* ==========================================================================
 * La grille
 * ========================================================================== */

bool dedale_walkable(const dedale *g, int col, int row)
{
    if (row < 0 || row >= DD_ROWS) return false;
    /* Les tunnels : sortir par la gauche revient par la droite. */
    if (col < 0 || col >= DD_COLS) return true;
    return g->tile[row][col] != DD_WALL;
}

int dedale_count_pellets(const dedale *g)
{
    int n = 0;
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if (g->tile[r][c] == DD_PELLET || g->tile[r][c] == DD_POWER) n++;
    return n;
}

static void cell_of(float x, float y, int *col, int *row)
{
    *col = (int)floorf(x / DD_CELL);
    *row = (int)floorf(y / DD_CELL);
}

static void centre_of(int col, int row, float *x, float *y)
{
    *x = ((float)col + 0.5f) * DD_CELL;
    *y = ((float)row + 0.5f) * DD_CELL;
}

static const int DIR_DX[DD_DIR_COUNT] = { +1, 0, -1, 0 };
static const int DIR_DY[DD_DIR_COUNT] = { 0, -1, 0, +1 };

static float wrap_x(float x)
{
    const float w = DD_COLS * DD_CELL;
    while (x < 0.0f) x += w;
    while (x >= w) x -= w;
    return x;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

static void place_rotors(dedale *g)
{
    /* Les quatre emplacements de l'enclos : les trois cases intérieures et la
     * porte. Le premier jet en posait un sur la case DU DESSOUS, qui est un
     * mur — il y restait bloqué toute la partie, et rien ne le signalait. */
    static const int SPOT_DX[DD_ROTORS] = { -1, 0, +1, 0 };
    static const int SPOT_DY[DD_ROTORS] = { 0, 0, 0, -1 };
    for (int i = 0; i < DD_ROTORS; ++i) {
        centre_of(DD_HOME_COL + SPOT_DX[i], DD_HOME_ROW + SPOT_DY[i],
                  &g->rotor[i].x, &g->rotor[i].y);
        g->rotor[i].dir = (i & 1) ? DD_LEFT : DD_UP;
        g->rotor[i].kind = i;
        g->rotor[i].frightened = 0.0f;
        g->rotor[i].eaten = false;
        /* Ils sortent l'un après l'autre : quatre hélices lâchées ensemble sur
         * un joueur qui démarre, c'est une mort et pas une partie. */
        g->rotor[i].respawn = (float)i * 4.0f;
    }
}

static void load_maze(dedale *g)
{
    for (int r = 0; r < DD_ROWS; ++r) {
        for (int c = 0; c < DD_COLS; ++c) {
            const char ch = DD_MAZE[r][c];
            switch (ch) {
                case '#': g->tile[r][c] = DD_WALL;   break;
                case '.': g->tile[r][c] = DD_PELLET; break;
                case 'o': g->tile[r][c] = DD_POWER;  break;
                default:  g->tile[r][c] = DD_EMPTY;  break;
            }
        }
    }
    g->pellets_left = (uint32_t)dedale_count_pellets(g);
}

void dedale_reset(dedale *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0x9ACu);
    g->hard = hard;
    g->phase = DD_READY;
    g->level = 1;

    load_maze(g);
    /* Le 'P' du plan donne le départ, pour que la position soit LUE et non
     * recopiée à côté du labyrinthe qu'elle doit suivre. */
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if (DD_MAZE[r][c] == 'P') centre_of(c, r, &g->x, &g->y);

    g->dir = g->want = DD_LEFT;
    g->scattering = true;
    g->scatter_timer = DD_SCATTER_ON;
    place_rotors(g);
}

static void next_level(dedale *g)
{
    g->level++;
    g->score += PTS_LEVEL;
    g->pend_level++;
    load_maze(g);
    for (int r = 0; r < DD_ROWS; ++r)
        for (int c = 0; c < DD_COLS; ++c)
            if (DD_MAZE[r][c] == 'P') centre_of(c, r, &g->x, &g->y);
    g->dir = g->want = DD_LEFT;
    place_rotors(g);
}

void dedale_press(dedale *g, ns_game_button b)
{
    if (g->phase == DD_DEAD) return;
    if (g->phase == DD_READY) g->phase = DD_PLAYING;
    switch (b) {
        case NS_GAME_RIGHT: g->want = DD_RIGHT; g->turned = true; break;
        case NS_GAME_UP:    g->want = DD_UP;    g->turned = true; break;
        case NS_GAME_LEFT:  g->want = DD_LEFT;  g->turned = true; break;
        case NS_GAME_DOWN:  g->want = DD_DOWN;  g->turned = true; break;
        default: break;
    }
}

void dedale_hold(dedale *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    for (int i = 0; i < NS_GAME_BUTTON_COUNT; ++i) g->held[i] = held[i];
    /* Le maintien vaut intention : sur une borne, on tient la direction jusqu'à
     * ce que le virage soit possible. C'est ce qui rend les angles jouables. */
    if (held[NS_GAME_RIGHT]) g->want = DD_RIGHT;
    else if (held[NS_GAME_UP]) g->want = DD_UP;
    else if (held[NS_GAME_LEFT]) g->want = DD_LEFT;
    else if (held[NS_GAME_DOWN]) g->want = DD_DOWN;
}

/*
 * Un déplacement sur grille avec un CENTRE de case.
 *
 * Le mobile n'avance que le long de sa direction ; il ne peut tourner qu'au
 * centre d'une case, et seulement si la case visée est libre. C'est ce qui
 * empêche de couper les murs en diagonale, et c'est la seule façon d'avoir des
 * virages qui « collent » comme dans l'original.
 */
static void step_actor(const dedale *g, float *x, float *y, dd_dir *dir,
                       dd_dir want, float speed, float dt)
{
    int col, row;
    cell_of(*x, *y, &col, &row);
    float cx, cy;
    centre_of(col, row, &cx, &cy);

    const bool at_centre = (fabsf(*x - cx) < 1.0f && fabsf(*y - cy) < 1.0f);
    if (at_centre) {
        if (want != *dir && dedale_walkable(g, col + DIR_DX[want], row + DIR_DY[want])) {
            *dir = want;
            *x = cx; *y = cy;
        }
        if (!dedale_walkable(g, col + DIR_DX[*dir], row + DIR_DY[*dir])) {
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
    if (!dedale_walkable(g, col + DIR_DX[*dir], row + DIR_DY[*dir])) {
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
static void rotor_target(const dedale *g, const dd_rotor *rt, int *tc, int *tr)
{
    int pc, pr;
    cell_of(g->x, g->y, &pc, &pr);

    if (rt->eaten) { *tc = DD_HOME_COL; *tr = DD_HOME_ROW; return; }

    static const int CORNER_C[DD_ROTORS] = { DD_COLS - 2, 1, DD_COLS - 2, 1 };
    static const int CORNER_R[DD_ROTORS] = { 1, 1, DD_ROWS - 2, DD_ROWS - 2 };

    if (rt->frightened > 0.0f || (g->scattering && rt->kind != 3)) {
        *tc = CORNER_C[rt->kind];
        *tr = CORNER_R[rt->kind];
        return;
    }
    switch (rt->kind) {
        case 0: *tc = pc; *tr = pr; break;
        case 1: *tc = pc + DIR_DX[g->dir] * 4; *tr = pr + DIR_DY[g->dir] * 4; break;
        case 2: *tc = CORNER_C[2]; *tr = CORNER_R[2]; break;
        default: *tc = -1; *tr = -1; break;   /* aléatoire : pas de cible */
    }
}

static void rotor_choose(dedale *g, dd_rotor *rt)
{
    int col, row;
    cell_of(rt->x, rt->y, &col, &row);
    float cx, cy;
    centre_of(col, row, &cx, &cy);
    if (fabsf(rt->x - cx) > 1.0f || fabsf(rt->y - cy) > 1.0f) return;

    int tc, tr;
    rotor_target(g, rt, &tc, &tr);

    /* Une hélice ne fait JAMAIS demi-tour, sauf s'il n'a pas le choix. C'est la
     * règle qui les empêche de vibrer sur place, et elle est d'origine. */
    const dd_dir back = (dd_dir)((rt->dir + 2) % DD_DIR_COUNT);

    dd_dir best = rt->dir;
    float best_d = 1e30f;
    int options = 0;
    dd_dir first = rt->dir;

    for (int d = 0; d < DD_DIR_COUNT; ++d) {
        if ((dd_dir)d == back) continue;
        const int nc = col + DIR_DX[d], nr = row + DIR_DY[d];
        if (!dedale_walkable(g, nc, nr)) continue;
        options++;
        first = (dd_dir)d;
        if (tc < 0) continue;
        const float dx = (float)(nc - tc), dy = (float)(nr - tr);
        const float dist = dx * dx + dy * dy;
        if (dist < best_d) { best_d = dist; best = (dd_dir)d; }
    }
    if (options == 0) { rt->dir = back; return; }
    if (tc < 0) {
        /* Le hasard : on retire une des sorties possibles. */
        int pick = (int)ns_rng_below(&g->rng, (uint32_t)options);
        for (int d = 0; d < DD_DIR_COUNT; ++d) {
            if ((dd_dir)d == back) continue;
            if (!dedale_walkable(g, col + DIR_DX[d], row + DIR_DY[d])) continue;
            if (pick-- == 0) { rt->dir = (dd_dir)d; return; }
        }
        rt->dir = first;
        return;
    }
    rt->dir = best;
}

static void eat_tile(dedale *g)
{
    int col, row;
    cell_of(g->x, g->y, &col, &row);
    if (col < 0 || col >= DD_COLS || row < 0 || row >= DD_ROWS) return;

    if (g->tile[row][col] == DD_PELLET) {
        g->tile[row][col] = DD_EMPTY;
        g->pellets_left--;
        g->score += PTS_PELLET;
        g->pend_pellet++;
    } else if (g->tile[row][col] == DD_POWER) {
        g->tile[row][col] = DD_EMPTY;
        g->pellets_left--;
        g->score += PTS_POWER;
        g->pend_power++;
        g->chain = 0;
        for (int i = 0; i < DD_ROTORS; ++i) {
            if (g->rotor[i].eaten) continue;
            g->rotor[i].frightened = DD_FRIGHT_TIME;
            /* Ils font demi-tour : c'est le signal visuel qui dit qu'on a la
             * main. */
            g->rotor[i].dir = (dd_dir)((g->rotor[i].dir + 2) % DD_DIR_COUNT);
        }
    }
    if (g->pellets_left == 0) next_level(g);
}

void dedale_tick(dedale *g, float dt)
{
    if (g->phase == DD_DEAD) { g->dead_time += dt; return; }
    g->time += dt;
    if (g->phase == DD_READY) return;

    g->eclat += dt * 9.0f;

    /* L'alternance dispersion / poursuite. */
    g->scatter_timer -= dt;
    if (g->scatter_timer <= 0.0f) {
        g->scattering = !g->scattering;
        g->scatter_timer = g->scattering ? DD_SCATTER_ON : DD_SCATTER_OFF;
    }

    step_actor(g, &g->x, &g->y, &g->dir, g->want, DD_SPEED, dt);
    eat_tile(g);

    for (int i = 0; i < DD_ROTORS; ++i) {
        dd_rotor *rt = &g->rotor[i];
        if (rt->respawn > 0.0f) { rt->respawn -= dt; continue; }
        if (rt->frightened > 0.0f) rt->frightened -= dt;

        float speed = DD_ROTOR_SPEED;
        if (rt->eaten) speed = DD_EATEN_SPEED;
        else if (rt->frightened > 0.0f) speed = DD_FRIGHT_SPEED;
        if (g->hard && !rt->eaten && rt->frightened <= 0.0f) speed *= 1.15f;

        rotor_choose(g, rt);
        dd_dir keep = rt->dir;
        step_actor(g, &rt->x, &rt->y, &rt->dir, keep, speed, dt);

        if (rt->eaten) {
            const float dx = rt->x - ((float)DD_HOME_COL + 0.5f) * DD_CELL;
            const float dy = rt->y - ((float)DD_HOME_ROW + 0.5f) * DD_CELL;
            if (dx * dx + dy * dy < 12.0f * 12.0f) {
                rt->eaten = false;
                rt->frightened = 0.0f;
                rt->respawn = 1.5f;
            }
            continue;
        }

        const float dx = rt->x - g->x, dy = rt->y - g->y;
        if (dx * dx + dy * dy > 20.0f * 20.0f) continue;

        if (rt->frightened > 0.0f) {
            /* 200, 400, 800, 1 600 : la chaîne du jeu d'arcade. Le serveur
             * compte chaque hélice à 200, donc on émet autant d'événements que
             * la valeur le demande — c'est ce qui garde le journal et le score
             * d'accord. */
            g->chain++;
            const uint32_t times = (g->chain > 4) ? 8u : (1u << (g->chain - 1));
            g->score += (int64_t)PTS_ROTOR * times;
            g->pend_rotor += times;
            rt->eaten = true;
            rt->frightened = 0.0f;
        } else {
            g->phase = DD_DEAD;
            g->dead_time = 0.0f;
            g->died = true;
            return;
        }
    }
}

/* ==========================================================================
 * Le joueur automatique
 * ==========================================================================
 * Un parcours en largeur depuis la case du joueur, avec les hélices marquées
 * infranchissables tant qu'ils ne sont pas mangeables. Il vise la pastille la
 * plus proche, ou l'hélice la plus proche quand ils fuient.
 * ========================================================================== */

bool dedale_autopilot(dedale *g)
{
    if (g->phase == DD_DEAD) return false;
    if (g->phase == DD_READY) g->phase = DD_PLAYING;

    int sc, sr;
    cell_of(g->x, g->y, &sc, &sr);
    if (sc < 0 || sc >= DD_COLS || sr < 0 || sr >= DD_ROWS) return true;

    /* Les cases dangereuses : la position d'une hélice et ses voisines. */
    bool danger[DD_ROWS][DD_COLS];
    memset(danger, 0, sizeof danger);
    bool hunting = false;
    for (int i = 0; i < DD_ROTORS; ++i) {
        const dd_rotor *rt = &g->rotor[i];
        if (rt->respawn > 0.0f || rt->eaten) continue;
        if (rt->frightened > 0.35f) { hunting = true; continue; }
        int c, r;
        cell_of(rt->x, rt->y, &c, &r);
        /*
         * La case de l'hélice, et les DEUX qu'elle a devant lui. Pas les huit
         * voisines : dans un couloir d'une case de large, une croix de trois
         * sur trois autour de quatre hélices bouche à peu près tout, le
         * parcours en largeur ne trouve plus rien, et le repli fonce droit
         * dans le décor. Mesuré : six parties en soixante secondes, quarante
         * points. Ce qui est dangereux, c'est là où il va.
         */
        for (int k = 0; k <= 2; ++k) {
            const int cc = ((c + DIR_DX[rt->dir] * k) + DD_COLS) % DD_COLS;
            const int rr = r + DIR_DY[rt->dir] * k;
            if (rr < 0 || rr >= DD_ROWS) continue;
            danger[rr][cc] = true;
        }
    }

    /* Parcours en largeur : `from[]` garde la première direction empruntée. */
    int8_t first[DD_ROWS][DD_COLS];
    memset(first, -1, sizeof first);
    int16_t queue[DD_ROWS * DD_COLS][2];
    int head = 0, tail = 0;

    for (int d = 0; d < DD_DIR_COUNT; ++d) {
        const int nc = ((sc + DIR_DX[d]) + DD_COLS) % DD_COLS;
        const int nr = sr + DIR_DY[d];
        if (!dedale_walkable(g, nc, nr)) continue;
        if (nr < 0 || nr >= DD_ROWS) continue;
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
        bool want = (t == DD_PELLET || t == DD_POWER);
        if (hunting) {
            for (int i = 0; i < DD_ROTORS; ++i) {
                if (g->rotor[i].frightened <= 0.35f || g->rotor[i].eaten) continue;
                int gc, gr;
                cell_of(g->rotor[i].x, g->rotor[i].y, &gc, &gr);
                if (gc == c && gr == r) want = true;
            }
        }
        if (want) { goal_dir = first[r][c]; break; }

        for (int d = 0; d < DD_DIR_COUNT; ++d) {
            const int nc = ((c + DIR_DX[d]) + DD_COLS) % DD_COLS;
            const int nr = r + DIR_DY[d];
            if (nr < 0 || nr >= DD_ROWS) continue;
            if (!dedale_walkable(g, nc, nr)) continue;
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
         * Cerné : on prend la sortie qui ÉLOIGNE le plus de l'hélice la plus
         * proche, pas la première venue. Un héros immobile est un héros mort,
         * mais un héros qui fonce dans le premier couloir libre l'est tout
         * autant — et c'est ce que faisait le premier jet.
         */
        float best_gap = -1.0f;
        for (int d = 0; d < DD_DIR_COUNT; ++d) {
            const int nc = ((sc + DIR_DX[d]) + DD_COLS) % DD_COLS;
            const int nr = sr + DIR_DY[d];
            if (nr < 0 || nr >= DD_ROWS) continue;
            if (!dedale_walkable(g, nc, nr)) continue;
            float gap = 1e30f;
            for (int i = 0; i < DD_ROTORS; ++i) {
                const dd_rotor *rt = &g->rotor[i];
                if (rt->respawn > 0.0f || rt->eaten || rt->frightened > 0.0f) continue;
                const float dx = rt->x - ((float)nc + 0.5f) * DD_CELL;
                const float dy = rt->y - ((float)nr + 0.5f) * DD_CELL;
                const float d2 = dx * dx + dy * dy;
                if (d2 < gap) gap = d2;
            }
            if (gap > best_gap) { best_gap = gap; goal_dir = d; }
        }
    }
    if (goal_dir >= 0) dedale_press(g, (goal_dir == DD_RIGHT) ? NS_GAME_RIGHT
                                     : (goal_dir == DD_UP)    ? NS_GAME_UP
                                     : (goal_dir == DD_LEFT)  ? NS_GAME_LEFT
                                                              : NS_GAME_DOWN);
    return true;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

void dedale_draw(ns_sprite *s, const dedale *g, const dedale_art *a,
                 float logical_w, float logical_h)
{
    const float board_w = DD_COLS * DD_CELL, board_h = DD_ROWS * DD_CELL;
    const float sx = logical_w / DD_LOGICAL_W, sy = logical_h / DD_LOGICAL_H;
    const float base = (sx < sy) ? sx : sy;
    /* Le plateau prend la hauteur disponible : sur une dalle de borne, un
     * plateau de 840 px dans 1080 laisserait un quart de l'écran vide. */
    const float scale = base * (DD_LOGICAL_H * 0.94f) / board_h;
    const float ox = (logical_w - board_w * scale) * 0.5f;
    const float oy = (logical_h - board_h * scale) * 0.5f;

    static const float back[4] = { 0.02f, 0.02f, 0.06f, 1.0f };
    ns_sprite_rect(s, 0.0f, 0.0f, logical_w, logical_h, back);

    /* Les murs occupent la case ENTIÈRE : à trois pixels de retrait ils se
     * lisaient comme une grille de blocs séparés au lieu d'un labyrinthe. */
    static const float wall[4]   = { 0.11f, 0.14f, 0.52f, 1.0f };
    static const float pellet[4] = { 1.00f, 0.86f, 0.62f, 1.0f };
    static const float power[4]  = { 1.00f, 0.62f, 0.24f, 1.0f };

    for (int r = 0; r < DD_ROWS; ++r) {
        for (int c = 0; c < DD_COLS; ++c) {
            const float x = ox + (float)c * DD_CELL * scale;
            const float y = oy + (float)r * DD_CELL * scale;
            if (g->tile[r][c] == DD_WALL) {
                ns_sprite_rect(s, x, y, DD_CELL * scale, DD_CELL * scale, wall);
            } else if (g->tile[r][c] == DD_PELLET) {
                ns_sprite_rect(s, x + (DD_CELL * 0.5f - 3.0f) * scale,
                               y + (DD_CELL * 0.5f - 3.0f) * scale,
                               6.0f * scale, 6.0f * scale, pellet);
            } else if (g->tile[r][c] == DD_POWER) {
                const float p = 8.0f + 3.0f * sinf(g->time * 6.0f);
                ns_sprite_rect(s, x + (DD_CELL * 0.5f - p) * scale,
                               y + (DD_CELL * 0.5f - p) * scale,
                               p * 2.0f * scale, p * 2.0f * scale, power);
            }
        }
    }

    /*
     * LES HÉLICES.
     *
     * La planche est en NIVEAUX DE GRIS et `ns_sprite_quad` la MULTIPLIE par
     * une couleur : une seule image porte donc les quatre poursuivants, l'état
     * apeuré et l'état mangé. C'est ce qui permet de garder les quatre teintes
     * de 1980 — une couleur n'appartient à personne, contrairement à la forme
     * qu'elle habillait.
     */
    static const float HELICE[DD_ROTORS][4] = {
        { 1.00f, 0.24f, 0.20f, 1.0f },
        { 1.00f, 0.65f, 0.82f, 1.0f },
        { 0.36f, 0.90f, 0.95f, 1.0f },
        { 1.00f, 0.72f, 0.28f, 1.0f },
    };
    /*
     * Une hélice apeurée doit se DISTINGUER du mur. Le bleu sombre des bornes
     * de cette époque marchait sur un labyrinthe bleu clair ; ici les murs sont
     * bleus, et quatre hélices bleues dessus étaient tout simplement invisibles
     * — la capture le montrait sans appel. Blanc bleuté, donc, et clignotant
     * sur la dernière seconde et demie : le joueur doit voir que ça se termine.
     */
    static const float fright[4] = { 0.88f, 0.94f, 1.00f, 1.0f };
    static const float fright2[4] = { 0.30f, 0.42f, 1.00f, 1.0f };
    static const float moyeu[4]  = { 0.86f, 0.92f, 1.00f, 1.0f };

    /* La planche : 4 colonnes de rotation sur 2 rangées — l'hélice entière,
     * puis le moyeu seul. Voir `tools/spriteart.c`, planche
     * « dedale_creatures ». */
    const float CASE_U = 1.0f / 4.0f, CASE_V = 1.0f / 2.0f;

    if (a && a->ready) ns_sprite_texture(s, &a->helices);
    for (int i = 0; i < DD_ROTORS; ++i) {
        const dd_rotor *rt = &g->rotor[i];
        if (rt->respawn > 0.0f) continue;
        const float *col = HELICE[rt->kind];
        if (rt->eaten) col = moyeu;
        else if (rt->frightened > 0.0f) {
            const bool blink = (rt->frightened < 1.5f)
                            && (((int)(rt->frightened * 8.0f)) & 1);
            col = blink ? fright2 : fright;
        }
        /* La rotation est indexée sur le TEMPS et décalée par l'indice : quatre
         * hélices qui tourneraient en phase se liraient comme un seul objet. */
        const int frame = ((int)(g->time * 12.0f) + i) & 3;
        const float sz = 34.0f;
        const float px = ox + (rt->x - sz * 0.5f) * scale;
        const float py = oy + (rt->y - sz * 0.5f) * scale;
        if (a && a->ready) {
            ns_sprite_quad(s, px, py, sz * scale, sz * scale,
                           (float)frame * CASE_U, rt->eaten ? CASE_V : 0.0f,
                           (float)(frame + 1) * CASE_U, rt->eaten ? 1.0f : CASE_V, col);
        } else {
            /* Sans planche, le jeu reste jouable : une croix en trois aplats.
             * `NS_WARN` l'a déjà dit une fois au chargement. */
            const float b = sz * 0.34f;
            ns_sprite_rect(s, px + (sz * scale - b * scale) * 0.5f, py, b * scale, sz * scale, col);
            ns_sprite_rect(s, px, py + (sz * scale - b * scale) * 0.5f, sz * scale, b * scale, col);
        }
    }

    /*
     * LE HÉROS : le rubis.
     *
     * Quatre images d'éclat, prises sur le temps — une pierre ne se déforme
     * pas, elle accroche la lumière. C'est TOUTE son animation, et c'est
     * assez : elle est la seule forme pleine et claire de l'écran.
     *
     * PAS DE TRAÎNÉE DERRIÈRE ELLE, et c'est mesuré, pas décidé. Le disque de
     * 1980 montrait sa direction par sa bouche ; une pierre n'en a pas, et deux
     * façons de la remplacer ont été essayées puis retirées sur capture :
     *
     *   - un aplat étiré dans l'axe du déplacement donnait une TIGE. La pierre
     *     avait l'air posée sur un pied — un champignon, pas un mobile ;
     *   - trois, puis deux taches décroissantes donnaient la même tige en
     *     escalier, et le mélange alpha sur un couloir noir les rendait brunes,
     *     c'est-à-dire de la salissure.
     *
     * Le fait derrière l'échec : le couloir fait quarante pixels et la pierre
     * trente-deux. Il n'y a PAS DE PLACE pour une traînée lisible — tout ce
     * qu'on met derrière touche la pierre. Or la direction n'a pas besoin
     * d'être affichée : c'est le joueur qui la donne, et il la connaît. Ce sont
     * les quatre poursuivants qu'il doit regarder, pas lui-même.
     */
    if (g->phase != DD_DEAD) {
        static const float RUBIS[4] = { 1.00f, 0.86f, 0.30f, 1.0f };
        const float sz = 32.0f;
        const float px = ox + (g->x - sz * 0.5f) * scale;
        const float py = oy + (g->y - sz * 0.5f) * scale;
        if (a && a->ready) {
            const int frame = ((int)(g->eclat * 2.2f)) & 3;
            ns_sprite_texture(s, &a->heros);
            ns_sprite_quad(s, px, py, sz * scale, sz * scale,
                           (float)frame * CASE_U, 0.0f, (float)(frame + 1) * CASE_U, 1.0f,
                           RUBIS);
        } else {
            ns_sprite_rect(s, px, py, sz * scale, sz * scale, RUBIS);
        }
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    char line[64];
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    ns_sprite_text(s, 30.0f * base, 24.0f * base, base * 6.0f, white, line);
    SDL_snprintf(line, sizeof line, "NIVEAU %u", g->level);
    ns_sprite_text(s, 30.0f * base, 104.0f * base, base * 5.0f, amber, line);

    if (g->phase == DD_READY) {
        const char *msg = "MANCHE POUR SE DIRIGER";
        const float sc = base * 3.6f;
        ns_sprite_text(s, (logical_w - ns_sprite_text_width(msg, sc)) * 0.5f,
                       logical_h * 0.94f, sc, white, msg);
    } else if (g->phase == DD_DEAD) {
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

bool dedale_art_load(ns_rhi *r, dedale_art *a)
{
    memset(a, 0, sizeof *a);
    /*
     * Les DEUX PLANCHES QU'ON A DESSINÉES, et pas celles de 2020.
     *
     * `pacman.png` et `enemy.png` étaient les sprites de la borne de 1980 :
     * quatre disques à part de camembert, huit fantômes à jupe ondulée. Elles
     * ne sont plus copiées dans le paquet — voir le bloc « LES PLANCHES DES
     * MINI-JEUX » d'`assets/CMakeLists.txt` — et `tools/spriteart` les remplace
     * par un rubis et une hélice, dessinés à seize pixels puis agrandis quatre
     * fois au plus proche.
     *
     * Elles étaient d'ailleurs chargées SANS ÊTRE AFFICHÉES : le dessin se
     * faisait entièrement en aplats, et la planche ne servait qu'à occuper de
     * la mémoire vidéo. C'est maintenant l'inverse — les planches portent les
     * personnages, et l'aplat n'est plus qu'un repli si elles manquent.
     */
    const bool ok =
        ns_texture_load(r, &a->heros,   "games/dedale/heros.png",     true, false) &&
        ns_texture_load(r, &a->helices, "games/dedale/creatures.png", true, false);
    a->ready = ok;
    if (!ok) NS_WARN("dedale : planches introuvables, le jeu tournera en aplats");
    return ok;
}

void dedale_art_free(ns_rhi *r, dedale_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->heros);
    ns_texture_destroy(r, &a->helices);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void dd_reset(void *g, uint64_t seed, bool hard) { dedale_reset((dedale *)g, seed, hard); }
static void dd_press(void *g, ns_game_button b) { dedale_press((dedale *)g, b); }
static void dd_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { dedale_hold((dedale *)g, h); }
static void dd_tick(void *g, float dt) { dedale_tick((dedale *)g, dt); }

static void dd_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    dedale_draw(s, (const dedale *)g, (const dedale_art *)a, w, h);
}

static bool dd_art_load(ns_rhi *r, void *a) { return dedale_art_load(r, (dedale_art *)a); }
static void dd_art_free(ns_rhi *r, void *a) { dedale_art_free(r, (dedale_art *)a); }
static bool dd_autopilot(void *g) { return dedale_autopilot((dedale *)g); }

static uint32_t dd_score(const void *g)
{
    const int64_t v = ((const dedale *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t dd_best(const void *g) { return ((const dedale *)g)->best; }
static void     dd_set_best(void *g, uint32_t b) { ((dedale *)g)->best = b; }

static bool dd_dead(const void *g, float *dead_time)
{
    const dedale *p = (const dedale *)g;
    if (dead_time) *dead_time = p->dead_time;
    return p->phase == DD_DEAD;
}

/* Le vocabulaire de `rulesTable["dedale"]`, tel qu'il a toujours été écrit :
 * pastille 10, super-pastille 50, hélice 200, niveau 1 000. La table attendait
 * un Dédale complet ; c'est maintenant le cas. */
static const char *const dd_kinds[] = { "pellet", "power", "ghost", "level", "turn", "death", NULL };

static void dd_events(void *g, ns_game_events *out)
{
    dedale *p = (dedale *)g;

    out->blip = p->turned;
    out->blip_kind = "turn";
    p->turned = false;

    if (p->pend_pellet)     { out->score = true; out->score_kind = "pellet"; p->pend_pellet--; }
    else if (p->pend_power) { out->score = true; out->score_kind = "power";  p->pend_power--; }
    else if (p->pend_rotor) { out->score = true; out->score_kind = "ghost";  p->pend_rotor--; }
    else if (p->pend_level) { out->score = true; out->score_kind = "level";  p->pend_level--; }
    out->score_value = 0;   /* barème FIXE côté serveur : la valeur ne voyage pas */

    if (p->died && !p->pend_pellet && !p->pend_power && !p->pend_rotor && !p->pend_level) {
        out->die = true;
        p->died = false;
    }
}

const ns_game_api g_dedale_api = {
    .id = "dedale", .title = "DEDALE", .label = "DEDALE",
    .state_size = sizeof(dedale), .art_size = sizeof(dedale_art),
    .sound_blip = NULL,
    .sound_score = "games/flappy/score.wav",
    .sound_die = "games/snake/gameover.wav",
    .art_load = dd_art_load, .art_free = dd_art_free,
    .reset = dd_reset, .press = dd_press, .hold = dd_hold,
    .tick = dd_tick, .draw = dd_draw, .autopilot = dd_autopilot,
    .event_kinds = dd_kinds,
    .score = dd_score, .best = dd_best, .set_best = dd_set_best,
    .dead = dd_dead, .events = dd_events,
};
