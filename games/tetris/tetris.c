/* tetris.c — voir tetris.h pour ce qui est repris de 2020 et ce qui ne l'est pas. */
#include "tetris.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Le temps de 2020, converti
 * ==========================================================================
 * `updateDistances` recalcule à chaque image :
 *
 *     frame[i] = clamp(FRAME_MAX[i] * pow(GROW_RATE[i], t), FRAME_MIN[i], FRAME_MAX[i])
 *
 * où `t` est le nombre d'images écoulées. Le résultat est un nombre d'IMAGES
 * entre deux pas ; à 30 Hz, une période en secondes vaut `frame / 30`.
 *
 * On garde donc la formule telle quelle, en exprimant `t` en images de 30 Hz
 * calculées depuis le temps réel — ce qui rend la courbe identique quel que soit
 * le pas du moteur, alors que rejouer des images la ferait dépendre de
 * l'affichage.
 * ========================================================================== */

#define FPS30 30.0f

/* enum{LATERAL, DOWN, TO_GO, STOP} de config.h */
static const float GROW_RATE[4]  = { 0.9999f, 0.99976f, 0.9999f, 0.99988f };
static const float FRAME_MIN[4]  = { 4.0f,  1.0f,  8.0f, 15.0f };
static const float FRAME_MAX[4]  = { 8.0f, 20.0f, 18.0f, 20.0f };
enum { T_LATERAL = 0, T_DOWN, T_TO_GO, T_STOP };

/* Le barème, tel quel. */
#define SCORE_BASE        100
#define RATIO_COMBO_LINE  2
#define NB_FLAT_POINT     500
#define RATIO_MULTI_POINT 2
#define RATIO_SAME_COLOR  10

/* `PROBA_BONUS 6` : une pièce sur six porte un bonus. */
#define PROBA_BONUS 6
/* `ACCELERATE 10` : la descente rapide va dix fois plus vite. */
#define ACCELERATE 10.0f

static float period_seconds(int which, float elapsed_seconds)
{
    const float t = elapsed_seconds * FPS30;
    float f = FRAME_MAX[which] * powf(GROW_RATE[which], t);
    if (f < FRAME_MIN[which]) f = FRAME_MIN[which];
    if (f > FRAME_MAX[which]) f = FRAME_MAX[which];
    return f / FPS30;
}

/* ==========================================================================
 * Les pièces
 * ========================================================================== */

static uint16_t shape_row(const tetris *g, const tet_piece *p, int row)
{
    return TET_SHAPE[g->hard ? 1 : 0][p->giant][p->id][p->rota][row];
}

static void shape_pivot(const tetris *g, const tet_piece *p, int *px, int *py)
{
    const uint8_t *v = TET_PIVOT[g->hard ? 1 : 0][p->giant][p->id][p->rota];
    *px = v[0];
    *py = v[1];
}

/*
 * La pièce tient-elle là où elle est ?
 *
 * Une case au-dessus du plateau est ACCEPTÉE — c'est ainsi qu'une pièce entre
 * par le haut. Une case sous le fond ou hors des bords ne l'est pas.
 */
bool tetris_fits(const tetris *g, const tet_piece *p)
{
    for (int row = 0; row < TET_GRID; ++row) {
        const uint16_t bits = shape_row(g, p, row);
        if (!bits) continue;
        for (int col = 0; col < TET_GRID; ++col) {
            if (!(bits & (1u << col))) continue;
            const int x = p->x + col, y = p->y + row;
            if (x < 0 || x >= TET_W || y >= TET_H) return false;
            if (y < 0) continue;                 /* encore au-dessus du plateau */
            if (g->cell[y][x] != TET_EMPTY) return false;
        }
    }
    return true;
}

static void lock_piece(tetris *g, const tet_piece *p)
{
    for (int row = 0; row < TET_GRID; ++row) {
        const uint16_t bits = shape_row(g, p, row);
        if (!bits) continue;
        for (int col = 0; col < TET_GRID; ++col) {
            if (!(bits & (1u << col))) continue;
            const int x = p->x + col, y = p->y + row;
            if (x < 0 || x >= TET_W || y < 0 || y >= TET_H) continue;
            g->cell[y][x] = (int8_t)p->id;
            g->cell_bonus[y][x] = (uint8_t)p->bonus;
        }
    }
}

static void roll_piece(tetris *g, tet_piece *p)
{
    p->id = (int)ns_rng_below(&g->rng, TET_PIECES);
    p->rota = 0;
    /* `RATIO_GIANT` : une pièce géante ne sort qu'en hardcore, une fois sur
     * six comme les bonus. C'est la seule différence de RÈGLE entre les deux
     * bornes ; les formes, elles, diffèrent par la table. */
    p->giant = (g->hard && ns_rng_below(&g->rng, PROBA_BONUS) == 0) ? 1 : 0;
    p->bonus = TET_NO_BONUS;
    if (ns_rng_below(&g->rng, PROBA_BONUS) == 0) {
        p->bonus = (ns_rng_below(&g->rng, 2) == 0) ? TET_MULTI : TET_FLAT;
    }
    /* `spawn` pose x et y : la position d'entrée dépend de l'étendue réelle de
     * la forme, que seule la table connaît. */
    p->x = 0;
    p->y = 0;
}

/* L'étendue réellement occupée par une forme, dans sa grille de 10 x 10. */
static void shape_extent(const tetris *g, const tet_piece *p,
                         int *first_row, int *first_col, int *last_col)
{
    int fr = TET_GRID, fc = TET_GRID, lc = -1;
    for (int row = 0; row < TET_GRID; ++row) {
        const uint16_t bits = shape_row(g, p, row);
        if (!bits) continue;
        if (row < fr) fr = row;
        for (int col = 0; col < TET_GRID; ++col) {
            if (!(bits & (1u << col))) continue;
            if (col < fc) fc = col;
            if (col > lc) lc = col;
        }
    }
    if (fr == TET_GRID) { fr = 0; fc = 0; lc = 0; }
    *first_row = fr; *first_col = fc; *last_col = lc;
}

/*
 * Fait entrer `next`, en tire une nouvelle, et dit si l'entrée est possible.
 *
 * La pièce entre par le HAUT, sa première ligne pleine posée sur la ligne 0, et
 * centrée sur ses colonnes réellement occupées.
 *
 * Le premier jet la faisait apparaître au-dessus du plateau et descendre
 * jusqu'à ce qu'elle « tienne » — et c'était un défaut de fond, trouvé par les
 * tests : une case au-dessus de la ligne 0 est acceptée par construction (c'est
 * ainsi qu'une pièce arrive), donc une pièce entièrement au-dessus du plateau
 * tenait TOUJOURS. Sur un plateau plein, elle restait suspendue dans le vide et
 * la partie ne se terminait jamais. Le joueur automatique a survécu quinze
 * minutes d'affilée sans que rien ne le signale.
 *
 * Entrer à une position DÉFINIE rend la question décidable : ou la pièce y
 * tient, ou la partie est finie. C'est la condition de défaite, et la seule.
 */
static bool spawn(tetris *g)
{
    g->cur = g->next;
    roll_piece(g, &g->next);
    g->pieces++;
    g->spawned = true;
    g->lock = 0.0f;

    int first_row, first_col, last_col;
    shape_extent(g, &g->cur, &first_row, &first_col, &last_col);
    g->cur.y = -first_row;
    g->cur.x = (TET_W - (last_col - first_col + 1)) / 2 - first_col;
    return tetris_fits(g, &g->cur);
}

/* ==========================================================================
 * Les lignes
 * ==========================================================================
 * Le barème d'origine, et il n'est pas celui d'un Tetris standard :
 *
 *   - cent points la première ligne ;
 *   - chaque ligne SUPPLÉMENTAIRE de la même fournée vaut le DOUBLE de la
 *     précédente, ce qui fait qu'un quadruple vaut 100+200+400+800 = 1 500 ;
 *   - une ligne d'une SEULE couleur vaut dix fois ce total ;
 *   - un bonus `MULTI` dans la ligne la double encore, un bonus `FLAT` ajoute
 *     cinq cents points à plat.
 *
 * C'est ce qui rend le jeu de 2020 reconnaissable : viser la couleur unique
 * rapporte davantage que viser le quadruple.
 * ========================================================================== */

int tetris_clear_lines(tetris *g)
{
    int64_t last = 0;
    int cleared = 0;

    for (int y = TET_H - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < TET_W; ++x) {
            if (g->cell[y][x] == TET_EMPTY) { full = false; break; }
        }
        if (!full) continue;

        /* --- le barème de la ligne --- */
        int64_t points = cleared ? last * RATIO_COMBO_LINE : SCORE_BASE;

        int same = g->cell[y][0];
        int64_t multi = 1, flat = 0;
        for (int x = 0; x < TET_W; ++x) {
            if (g->cell[y][x] != same) same = -1;
            if (g->cell_bonus[y][x] == TET_MULTI) multi *= RATIO_MULTI_POINT;
            else if (g->cell_bonus[y][x] == TET_FLAT) flat++;
        }
        if (same >= 0) points *= RATIO_SAME_COLOR;
        points *= multi;
        last = points;
        points += flat * NB_FLAT_POINT;

        g->score += points;
        g->pend_points += points;
        g->lines++;
        cleared++;

        /* --- la descente --- */
        for (int r = y; r > 0; --r) {
            memcpy(g->cell[r], g->cell[r - 1], sizeof g->cell[r]);
            memcpy(g->cell_bonus[r], g->cell_bonus[r - 1], sizeof g->cell_bonus[r]);
        }
        for (int x = 0; x < TET_W; ++x) {
            g->cell[0][x] = TET_EMPTY;
            g->cell_bonus[0][x] = TET_NO_BONUS;
        }
        y++;   /* la ligne descendue prend la place : on la réexamine */
    }
    if (cleared) g->flash = 0.30f;
    return cleared;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

void tetris_reset(tetris *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0x7E7215u);
    g->hard = hard;
    g->phase = TET_READY;

    for (int y = 0; y < TET_H; ++y)
        for (int x = 0; x < TET_W; ++x) g->cell[y][x] = TET_EMPTY;

    roll_piece(g, &g->next);
    (void)spawn(g);

    g->fall_period = period_seconds(T_DOWN, 0.0f);
    g->lateral_period = period_seconds(T_LATERAL, 0.0f);
    g->lock_period = period_seconds(T_STOP, 0.0f);
}

/* Une rotation garde le PIVOT en place : sans ça une pièce longue se déplace
 * d'une case à chaque quart de tour et devient impossible à poser. */
static void rotate(tetris *g, int dir)
{
    tet_piece p = g->cur;
    int px0, py0, px1, py1;
    shape_pivot(g, &p, &px0, &py0);
    p.rota = (p.rota + dir + TET_ROTATIONS) % TET_ROTATIONS;
    shape_pivot(g, &p, &px1, &py1);
    p.x += px0 - px1;
    p.y += py0 - py1;

    /* Décalage au mur : la rotation contre un bord est rattrapée d'une ou deux
     * cases plutôt que refusée. C'est ce que tout joueur attend, et l'original
     * jouait le son `cantRotate` quand ça ne passait pas — donc il essayait. */
    static const int kick[5] = { 0, -1, 1, -2, 2 };
    for (int k = 0; k < 5; ++k) {
        tet_piece q = p;
        q.x += kick[k];
        if (tetris_fits(g, &q)) { g->cur = q; g->moved = true; return; }
    }
}

static void shift(tetris *g, int dx)
{
    tet_piece p = g->cur;
    p.x += dx;
    if (tetris_fits(g, &p)) { g->cur = p; g->moved = true; }
}

/* La pose : verrouille, compte les lignes, fait entrer la suivante. */
static void settle(tetris *g)
{
    lock_piece(g, &g->cur);
    (void)tetris_clear_lines(g);
    g->dropped = true;
    if (!spawn(g)) {
        g->phase = TET_DEAD;
        g->dead_time = 0.0f;
        g->died = true;
    }
}

void tetris_press(tetris *g, ns_game_button b)
{
    if (g->phase == TET_DEAD) return;
    if (g->phase == TET_READY) g->phase = TET_PLAYING;

    switch (b) {
        case NS_GAME_LEFT:  shift(g, -1); break;
        case NS_GAME_RIGHT: shift(g, +1); break;
        case NS_GAME_UP:    rotate(g, +1); break;
        case NS_GAME_ACTION: {
            /*
             * La descente MAXIMALE, sur le bouton. Une borne n'a qu'un bouton
             * d'action : `ESPACE` de 2020 y va, et la rotation passe au manche
             * vers le haut. C'est la seule adaptation, et elle est imposée par
             * le panneau de commande, pas choisie.
             */
            while (true) {
                tet_piece p = g->cur;
                p.y++;
                if (!tetris_fits(g, &p)) break;
                g->cur = p;
            }
            settle(g);
            break;
        }
        default: break;
    }
}

void tetris_hold(tetris *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    for (int i = 0; i < NS_GAME_BUTTON_COUNT; ++i) g->held[i] = held[i];
    g->soft_drop = held[NS_GAME_DOWN];
}

void tetris_tick(tetris *g, float dt)
{
    if (g->phase == TET_DEAD) { g->dead_time += dt; return; }
    g->time += dt;
    if (g->flash > 0.0f) g->flash -= dt;
    if (g->phase == TET_READY) return;

    if (g->robot_wait > 0.0f) g->robot_wait -= dt;

    g->elapsed += dt;
    g->fall_period    = period_seconds(T_DOWN, g->elapsed);
    g->lateral_period = period_seconds(T_LATERAL, g->elapsed);
    g->lock_period    = period_seconds(T_STOP, g->elapsed);

    /* Le maintien latéral : la première pression est traitée par `press`, le
     * maintien fait défiler à la cadence de `frame[LATERAL]`. */
    if (g->held[NS_GAME_LEFT] || g->held[NS_GAME_RIGHT]) {
        g->lateral += dt;
        while (g->lateral >= g->lateral_period) {
            g->lateral -= g->lateral_period;
            shift(g, g->held[NS_GAME_LEFT] ? -1 : +1);
        }
    } else {
        g->lateral = 0.0f;
    }

    /* La chute. `ACCELERATE 10` : maintenir bas va dix fois plus vite. */
    g->fall += dt * (g->soft_drop ? ACCELERATE : 1.0f);
    while (g->fall >= g->fall_period) {
        g->fall -= g->fall_period;
        tet_piece p = g->cur;
        p.y++;
        if (tetris_fits(g, &p)) {
            g->cur = p;
            g->lock = 0.0f;
        } else {
            /*
             * Le délai de pose. Une pièce posée n'est pas verrouillée tout de
             * suite : on garde `frame[STOP]` pour la glisser sous un surplomb.
             * C'est ce qui distingue un Tetris jouable d'un Tetris qui punit.
             */
            g->lock += g->fall_period;
            if (g->lock >= g->lock_period) { settle(g); return; }
        }
    }
}

/* ==========================================================================
 * Le joueur automatique
 * ==========================================================================
 * Il essaie les quarante poses possibles — dix colonnes, quatre rotations — et
 * garde la meilleure selon quatre termes qui sont ceux de tous les solveurs de
 * Tetris : la hauteur cumulée, les trous créés, les lignes faites, et la
 * rugosité de la surface. Il ne prouve pas que le jeu est amusant ; il prouve
 * qu'on peut enchaîner des milliers de pièces sans NaN, que le score monte, et
 * que la descente des lignes ne laisse pas de trou.
 * ========================================================================== */

static float board_cost(const tetris *g, int *lines_out)
{
    int height[TET_W];
    int holes = 0;
    for (int x = 0; x < TET_W; ++x) {
        height[x] = 0;
        bool seen = false;
        for (int y = 0; y < TET_H; ++y) {
            if (g->cell[y][x] != TET_EMPTY) {
                if (!seen) { height[x] = TET_H - y; seen = true; }
            } else if (seen) {
                holes++;
            }
        }
    }
    int lines = 0;
    for (int y = 0; y < TET_H; ++y) {
        bool full = true;
        for (int x = 0; x < TET_W; ++x) if (g->cell[y][x] == TET_EMPTY) { full = false; break; }
        if (full) lines++;
    }
    int agg = 0, bump = 0;
    for (int x = 0; x < TET_W; ++x) {
        agg += height[x];
        if (x) bump += (height[x] > height[x - 1]) ? (height[x] - height[x - 1])
                                                   : (height[x - 1] - height[x]);
    }
    if (lines_out) *lines_out = lines;
    /* Les coefficients classiques de Pierre Dellacherie, arrondis. */
    return -0.51f * (float)agg + 0.76f * (float)lines
           - 0.36f * (float)holes - 0.18f * (float)bump;
}

/*
 * Un geste toutes les 100 ms, et ce n'est pas un confort.
 *
 * `room/main.c` appelle `autopilot` à CHAQUE pas, soit 120 fois par seconde, et
 * ici un appel joue un geste entier : sans frein il posait quarante pièces par
 * seconde — 320 500 points en trente secondes, et surtout six fois la limite de
 * fréquence que `rulesTable["tetris"]` accepte sur « drop ». Toute partie
 * capturée aurait été refusée par l'anti-triche.
 *
 * À dix gestes par seconde il pose environ trois pièces par seconde, ce qui est
 * rapide pour un humain mais plausible, et sous le plafond.
 */
#define ROBOT_PERIOD 0.10f

bool tetris_autopilot(tetris *g)
{
    if (g->phase == TET_DEAD) return false;
    if (g->phase == TET_READY) g->phase = TET_PLAYING;
    if (g->robot_wait > 0.0f) return false;
    g->robot_wait = ROBOT_PERIOD;

    float best = -1e30f;
    int best_rota = g->cur.rota, best_x = g->cur.x;
    bool found = false;

    for (int r = 0; r < TET_ROTATIONS; ++r) {
        for (int x = -TET_GRID; x <= TET_W; ++x) {
            tet_piece p = g->cur;
            p.rota = r;
            p.x = x;
            p.y = g->cur.y;
            if (!tetris_fits(g, &p)) continue;
            while (true) {
                tet_piece q = p;
                q.y++;
                if (!tetris_fits(g, &q)) break;
                p = q;
            }
            /* Évaluer une pose demande de la jouer : on travaille sur une COPIE
             * pour ne pas toucher au plateau réel — c'est ce qui permet à ce
             * joueur d'être un test et pas une seconde implémentation. */
            tetris probe = *g;
            lock_piece(&probe, &p);
            const float cost = board_cost(&probe, NULL);
            if (cost > best) { best = cost; best_rota = r; best_x = x; found = true; }
        }
    }
    if (!found) { tetris_press(g, NS_GAME_ACTION); return true; }

    if (g->cur.rota != best_rota) { rotate(g, +1); return true; }
    if (g->cur.x != best_x)       { shift(g, (best_x > g->cur.x) ? +1 : -1); return true; }
    tetris_press(g, NS_GAME_ACTION);
    return true;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

#define BRICK_TILE 32.0f
#define BRICK_ATLAS_W (BRICK_TILE * 8.0f)

typedef struct ctx { ns_sprite *s; float ox, oy, scale; } ctx;

static void brick(const ctx *c, const ns_texture *t, int id,
                  float x, float y, float size, const float rgba[4])
{
    if (!t || !t->handle || id < 0) return;
    const float u0 = (float)id * BRICK_TILE / BRICK_ATLAS_W;
    const float u1 = (float)(id + 1) * BRICK_TILE / BRICK_ATLAS_W;
    ns_sprite_texture(c->s, t);
    ns_sprite_quad(c->s, c->ox + x * c->scale, c->oy + y * c->scale,
                   size * c->scale, size * c->scale, u0, 0.0f, u1, 1.0f, rgba);
}

void tetris_draw(ns_sprite *s, const tetris *g, const tetris_art *a,
                 float logical_w, float logical_h)
{
    const float sx = logical_w / TET_LOGICAL_W, sy = logical_h / TET_LOGICAL_H;
    ctx c;
    c.s = s;
    c.scale = (sx < sy) ? sx : sy;
    c.ox = (logical_w - TET_LOGICAL_W * c.scale) * 0.5f;
    c.oy = (logical_h - TET_LOGICAL_H * c.scale) * 0.5f;

    static const float back[4] = { 0.05f, 0.05f, 0.09f, 1.0f };
    ns_sprite_rect(s, c.ox, c.oy, TET_LOGICAL_W * c.scale, TET_LOGICAL_H * c.scale, back);

    /*
     * La case fait 38 px en 2020 — mais dans une fenêtre de 1080 de haut ENTOURÉE
     * d'un cadre de HUD dessiné (`hud_grille.png`, 692 x 862) qui occupait le
     * reste. Sans ce cadre, un puits de 760 px de haut laisse trois cents pixels
     * vides en haut et en bas, et sur la dalle d'une borne — soixante-deux
     * centimètres lus à un mètre — le jeu devient illisible.
     *
     * On garde donc la GRILLE de 2020, dix sur vingt, et on lui rend la hauteur
     * disponible. C'est un changement d'affichage, pas de règle : aucune cote de
     * jeu n'en dépend, tout se calcule en cases.
     */
    const float cell = (TET_LOGICAL_H * 0.90f) / (float)TET_H;
    const float gw = TET_W * cell, gh = TET_H * cell;
    const float gx = (TET_LOGICAL_W - gw) * 0.5f, gy = (TET_LOGICAL_H - gh) * 0.5f;

    static const float well[4]  = { 0.10f, 0.11f, 0.16f, 1.0f };
    static const float rule[4]  = { 0.22f, 0.26f, 0.36f, 1.0f };
    ns_sprite_rect(s, c.ox + (gx - 8.0f) * c.scale, c.oy + (gy - 8.0f) * c.scale,
                   (gw + 16.0f) * c.scale, (gh + 16.0f) * c.scale, rule);
    ns_sprite_rect(s, c.ox + gx * c.scale, c.oy + gy * c.scale,
                   gw * c.scale, gh * c.scale, well);

    /* Le plateau. L'éclat d'une ligne qui vient de partir tient trois dixièmes
     * de seconde : sans lui, dix cases disparaissent sans qu'on ait vu
     * pourquoi. */
    const float glow = (g->flash > 0.0f) ? (1.0f + g->flash * 1.8f) : 1.0f;
    for (int y = 0; y < TET_H; ++y) {
        for (int x = 0; x < TET_W; ++x) {
            if (g->cell[y][x] == TET_EMPTY) continue;
            const float rgba[4] = { glow, glow, glow, 1.0f };
            brick(&c, &a->bricks, g->cell[y][x],
                  gx + (float)x * cell, gy + (float)y * cell, cell, rgba);
        }
    }

    /* La pièce en cours, et son ombre au sol — la « pièce fantôme ». Elle
     * n'existait pas en 2020, et c'est le seul ajout de confort : sans souris ni
     * curseur, viser une colonne à l'œil sur vingt lignes de hauteur est une
     * difficulté du support, pas du jeu. */
    if (g->phase != TET_DEAD) {
        tet_piece ghost = g->cur;
        while (true) {
            tet_piece q = ghost;
            q.y++;
            if (!tetris_fits(g, &q)) break;
            ghost = q;
        }
        static const float dim[4] = { 0.30f, 0.30f, 0.34f, 0.55f };
        for (int row = 0; row < TET_GRID; ++row) {
            const uint16_t bits = shape_row(g, &ghost, row);
            for (int col = 0; col < TET_GRID; ++col) {
                if (!(bits & (1u << col))) continue;
                const int x = ghost.x + col, y = ghost.y + row;
                if (x < 0 || x >= TET_W || y < 0 || y >= TET_H) continue;
                brick(&c, &a->bricks, ghost.id,
                      gx + (float)x * cell, gy + (float)y * cell, cell, dim);
            }
        }
        static const float lit[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        for (int row = 0; row < TET_GRID; ++row) {
            const uint16_t bits = shape_row(g, &g->cur, row);
            for (int col = 0; col < TET_GRID; ++col) {
                if (!(bits & (1u << col))) continue;
                const int x = g->cur.x + col, y = g->cur.y + row;
                if (x < 0 || x >= TET_W || y < 0 || y >= TET_H) continue;
                brick(&c, &a->bricks, g->cur.id,
                      gx + (float)x * cell, gy + (float)y * cell, cell, lit);
            }
        }
    }

    /* La pièce suivante, à droite du puits — `SHIFT_RIGHT_NEXT` de 2020. */
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    const float pane = cell * 0.62f;
    const float nx = gx + gw + 70.0f, ny = gy + 90.0f;
    ns_sprite_text(s, c.ox + nx * c.scale, c.oy + (ny - 56.0f) * c.scale,
                   c.scale * 4.0f, amber, "SUIVANTE");
    for (int row = 0; row < TET_GRID; ++row) {
        const uint16_t bits = shape_row(g, &g->next, row);
        for (int col = 0; col < TET_GRID; ++col) {
            if (!(bits & (1u << col))) continue;
            brick(&c, &a->bricks, g->next.id,
                  nx + (float)col * pane, ny + (float)row * pane, pane, white);
        }
    }

    char line[64];
    const float hx = gx - 380.0f;
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    ns_sprite_text(s, c.ox + hx * c.scale, c.oy + (gy + 20.0f) * c.scale,
                   c.scale * 7.0f, white, line);
    SDL_snprintf(line, sizeof line, "%u LIGNES", g->lines);
    ns_sprite_text(s, c.ox + hx * c.scale, c.oy + (gy + 110.0f) * c.scale,
                   c.scale * 4.0f, amber, line);

    if (g->phase == TET_READY) {
        const char *msg = "MANCHE POUR TOURNER ET VISER   BOUTON POUR POSER";
        const float sc = c.scale * 3.4f;
        ns_sprite_text(s, c.ox + (TET_LOGICAL_W * c.scale
                                  - ns_sprite_text_width(msg, sc)) * 0.5f,
                       c.oy + TET_LOGICAL_H * c.scale * 0.93f, sc, white, msg);
    } else if (g->phase == TET_DEAD) {
        static const float veil[4] = { 0.03f, 0.03f, 0.05f, 0.78f };
        ns_sprite_rect(s, c.ox, c.oy + TET_LOGICAL_H * c.scale * 0.33f,
                       TET_LOGICAL_W * c.scale, TET_LOGICAL_H * c.scale * 0.34f, veil);

        const char *msg = "PERDU";
        const float sc = c.scale * 9.0f;
        ns_sprite_text(s, c.ox + (TET_LOGICAL_W * c.scale
                                  - ns_sprite_text_width(msg, sc)) * 0.5f,
                       c.oy + TET_LOGICAL_H * c.scale * 0.40f, sc, amber, msg);

        SDL_snprintf(line, sizeof line, "MEILLEUR %u", g->best);
        const float sc2 = c.scale * 5.0f;
        ns_sprite_text(s, c.ox + (TET_LOGICAL_W * c.scale
                                  - ns_sprite_text_width(line, sc2)) * 0.5f,
                       c.oy + TET_LOGICAL_H * c.scale * 0.52f, sc2, white, line);

        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            ns_sprite_text(s, c.ox + (TET_LOGICAL_W * c.scale
                                      - ns_sprite_text_width(again, sc2)) * 0.5f,
                           c.oy + TET_LOGICAL_H * c.scale * 0.60f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool tetris_art_load(ns_rhi *r, tetris_art *a)
{
    memset(a, 0, sizeof *a);
    a->ready = ns_texture_load(r, &a->bricks, "games/tetris/bricks.png", true, false);
    if (!a->ready) NS_WARN("tetris : planche introuvable, le jeu tournera sans images");
    return a->ready;
}

void tetris_art_free(ns_rhi *r, tetris_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->bricks);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void te_reset(void *g, uint64_t seed, bool hard) { tetris_reset((tetris *)g, seed, hard); }
static void te_press(void *g, ns_game_button b) { tetris_press((tetris *)g, b); }
static void te_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { tetris_hold((tetris *)g, h); }
static void te_tick(void *g, float dt) { tetris_tick((tetris *)g, dt); }

static void te_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    tetris_draw(s, (const tetris *)g, (const tetris_art *)a, w, h);
}

static bool te_art_load(ns_rhi *r, void *a) { return tetris_art_load(r, (tetris_art *)a); }
static void te_art_free(ns_rhi *r, void *a) { tetris_art_free(r, (tetris_art *)a); }
static bool te_autopilot(void *g)           { return tetris_autopilot((tetris *)g); }

static uint32_t te_score(const void *g)
{
    const int64_t v = ((const tetris *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t te_best(const void *g) { return ((const tetris *)g)->best; }
static void     te_set_best(void *g, uint32_t b) { ((tetris *)g)->best = b; }

static bool te_dead(const void *g, float *dead_time)
{
    const tetris *t = (const tetris *)g;
    if (dead_time) *dead_time = t->dead_time;
    return t->phase == TET_DEAD;
}

/* Le vocabulaire de `rulesTable["tetris"]`. « lines » porte les POINTS de la
 * fournée, pas leur nombre : le barème de 2020 double à chaque ligne
 * simultanée et multiplie par dix une ligne d'une seule couleur, ce qu'aucun
 * barème fixe côté serveur ne saurait reconstituer. */
static const char *const te_kinds[] = { "lines", "drop", "rotate", "death", NULL };

static void te_events(void *g, ns_game_events *out)
{
    tetris *t = (tetris *)g;

    /* Un geste : tourner ou glisser. Muet côté serveur, mais compté — c'est sur
     * lui que porte la limite de fréquence. */
    out->blip = t->moved;
    out->blip_kind = "rotate";
    t->moved = false;

    if (t->pend_points) {
        out->score = true;
        out->score_kind = "lines";
        out->score_value = t->pend_points;
        t->pend_points = 0;
    } else if (t->dropped) {
        out->score = true;
        out->score_kind = "drop";
        out->score_value = 0;
        t->dropped = false;
    }

    /* La fin n'est annoncée qu'une fois la file vide : `finish_run` scelle le
     * journal, et un gain publié après lui n'existe pas. Même règle que le
     * Démineur, pour la même raison. */
    if (t->died && !t->pend_points && !t->dropped) {
        out->die = true;
        t->died = false;
    }
}

const ns_game_api g_tetris_api = {
    .id = "tetris", .title = "TETRIS", .label = "TETRIS",
    .state_size = sizeof(tetris), .art_size = sizeof(tetris_art),
    .sound_blip = "games/tetris/rotate.wav",
    .sound_score = "games/tetris/line.wav",
    .sound_die = "games/tetris/gameover.wav",
    .art_load = te_art_load, .art_free = te_art_free,
    .reset = te_reset, .press = te_press, .hold = te_hold,
    .tick = te_tick, .draw = te_draw, .autopilot = te_autopilot,
    .event_kinds = te_kinds,
    .score = te_score, .best = te_best, .set_best = te_set_best,
    .dead = te_dead, .events = te_events,
};
