/* demineur.c — voir demineur.h pour ce qui est repris de 2020 et ce qui ne l'est pas. */
#include "demineur.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Le découpage de `demineur.png`
 * ==========================================================================
 * Tuiles de 54 x 54 dans une planche de 540 x 270, soit 10 x 5. Les positions
 * sont celles que `afficher_grille` allait chercher — y compris leur désordre :
 * les chiffres ne se suivent pas dans la planche, et les recopier dans l'ordre
 * « logique » donnerait un 3 là où il faut un 5.
 * ========================================================================== */

#define TILE 54.0f
#define ATLAS_W (TILE * 10.0f)
#define ATLAS_H (TILE * 5.0f)

typedef struct tile { float col, row; } tile;

static const tile T_MASKED = { 3, 1 };
static const tile T_BOMB   = { 5, 1 };
static const tile T_FLAG   = { 0, 2 };

/* Un chiffre par nombre de bombes voisines, dans l'ordre de la planche. */
static const tile T_DIGIT[9] = {
    { 0, 0 },   /* 0 : la case libre */
    { 3, 0 },   /* 1 */
    { 4, 0 },   /* 2 */
    { 1, 1 },   /* 3 */
    { 2, 1 },   /* 4 */
    { 4, 1 },   /* 5 */
    { 4, 3 },   /* 6 */
    { 3, 3 },   /* 7 */
    { 1, 3 },   /* 8 */
};

/* Le barème du serveur : `rulesTable["demineur"]`. */
#define PTS_CELL 5
#define PTS_FLAG 2
#define PTS_WIN  500

/* Le défilement au maintien : un pas tout de suite, puis tous les 90 ms. */
#define REPEAT_FIRST 0.28f
#define REPEAT_NEXT  0.09f

/* ==========================================================================
 * La grille
 * ========================================================================== */

static int neighbours(const demineur *g, int r, int c)
{
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (!dr && !dc) continue;
            const int rr = r + dr, cc = c + dc;
            if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
            if (g->bomb[rr][cc]) n++;
        }
    }
    return n;
}

/*
 * Les bombes sont posées APRÈS le premier dévoilement, en épargnant la case
 * jouée ET ses huit voisines. C'est ce que fait `premier_click` dans
 * l'original, et c'est la règle qui distingue un démineur d'une loterie : le
 * premier coup ouvre toujours une zone.
 */
int demineur_bombs(const demineur *g)
{
    return g->hard ? DEM_BOMBS_HARD : DEM_BOMBS_EASY;
}

static void place_bombs(demineur *g, int safe_r, int safe_c)
{
    const int want = demineur_bombs(g);
    int placed = 0, guard = 0;
    while (placed < want && guard++ < DEM_ROWS * DEM_COLS * 40) {
        const int r = (int)ns_rng_below(&g->rng, DEM_ROWS);
        const int c = (int)ns_rng_below(&g->rng, DEM_COLS);
        if (g->bomb[r][c]) continue;
        if (r >= safe_r - 1 && r <= safe_r + 1 && c >= safe_c - 1 && c <= safe_c + 1) continue;
        g->bomb[r][c] = true;
        placed++;
    }
    g->placed = true;
}

/*
 * Le dévoilement en cascade, sans récursion.
 *
 * L'original descend récursivement ; sur une grille de 400 cases c'est sans
 * danger, mais une pile explicite coûte le même code et ne dépend pas de la
 * profondeur. Les cases à zéro voisin ouvrent leurs voisines, les autres
 * s'arrêtent — c'est ce qui donne les grandes plages qui s'ouvrent d'un coup.
 */
/*
 * La pile est LOCALE, et dimensionnée pour ne jamais déborder : une case n'est
 * empilée que par ses huit voisines, donc au plus huit fois, et il y a
 * `DEM_ROWS * DEM_COLS` cases. Un tableau statique aurait violé la règle 2 de
 * `games.h` — aucun état global — et une pile trop courte aurait tronqué la
 * cascade en silence, laissant des cases fermées au milieu d'une plage ouverte.
 */
#define REVEAL_STACK (DEM_ROWS * DEM_COLS * 8)

static void reveal(demineur *g, int r0, int c0)
{
    if (g->shown[r0][c0] || g->flag[r0][c0]) return;

    int16_t stack[REVEAL_STACK][2];
    int top = 0;
    stack[top][0] = (int16_t)r0; stack[top][1] = (int16_t)c0; top++;

    while (top > 0) {
        top--;
        const int r = stack[top][0], c = stack[top][1];
        if (r < 0 || c < 0 || r >= DEM_ROWS || c >= DEM_COLS) continue;
        if (g->shown[r][c] || g->flag[r][c]) continue;

        g->shown[r][c] = true;
        g->revealed++;
        g->score += PTS_CELL;
        g->pend_cells++;    /* le COMPTE, pas un drapeau : voir demineur.h */

        if (neighbours(g, r, c) != 0) continue;

        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (!dr && !dc) continue;
                if (top < REVEAL_STACK) {
                    stack[top][0] = (int16_t)(r + dr);
                    stack[top][1] = (int16_t)(c + dc);
                    top++;
                }
            }
        }
    }
}

/*
 * Gagner est une FIN DE PARTIE, au même titre que sauter sur une mine.
 *
 * Ça n'allait pas de soi et c'est pourtant ce qui compte : `room/main.c` scelle
 * le journal, enregistre le meilleur score et met la partie en file d'attente
 * sur le seul événement `die`. Une victoire qui ne le levait pas était une
 * partie gagnée que personne n'enregistrait — le meilleur score local ne
 * bougeait pas, et le classement mondial ne la voyait jamais.
 */
static void check_won(demineur *g)
{
    if (g->phase == DEM_WON) return;
    if (g->revealed >= (uint32_t)(DEM_ROWS * DEM_COLS - demineur_bombs(g))) {
        g->phase = DEM_WON;
        g->score += PTS_WIN;
        g->pend_win = true;
        g->over = true;
    }
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

void demineur_reset(demineur *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0xDE31u);
    g->hard = hard;
    g->phase = DEM_READY;
    g->cursor_r = DEM_ROWS / 2;
    g->cursor_c = DEM_COLS / 2;
}

static void step_cursor(demineur *g, int dr, int dc)
{
    const int r = g->cursor_r + dr, c = g->cursor_c + dc;
    if (r < 0 || c < 0 || r >= DEM_ROWS || c >= DEM_COLS) return;
    g->cursor_r = r;
    g->cursor_c = c;
    g->moved = true;
}

void demineur_press(demineur *g, ns_game_button b)
{
    if (g->phase == DEM_DEAD || g->phase == DEM_WON) return;

    switch (b) {
        case NS_GAME_UP:    step_cursor(g, -1, 0); break;
        case NS_GAME_DOWN:  step_cursor(g, +1, 0); break;
        case NS_GAME_LEFT:  step_cursor(g, 0, -1); break;
        case NS_GAME_RIGHT: step_cursor(g, 0, +1); break;
        case NS_GAME_ACTION: {
            const int r = g->cursor_r, c = g->cursor_c;
            if (g->shown[r][c]) break;

            if (g->phase == DEM_READY) g->phase = DEM_PLAYING;
            if (!g->placed) place_bombs(g, r, c);

            if (g->flag[r][c]) break;   /* un drapeau protège de soi-même */

            if (g->bomb[r][c]) {
                g->phase = DEM_DEAD;
                g->dead_time = 0.0f;
                g->over = true;
                break;
            }
            reveal(g, r, c);
            check_won(g);
            break;
        }
        default: break;
    }
}

/*
 * Le maintien, pour le défilement du curseur.
 *
 * Les drapeaux, eux, ne sont PAS posables à la main dans ce portage : une borne
 * n'a qu'un bouton d'action, et le second bouton qu'il faudrait n'existe pas sur
 * le panneau. Le joueur automatique en pose (c'est ainsi qu'il déduit), et le
 * barème du serveur les compte — mais un joueur humain ouvre des cases. C'est
 * une limite du support, elle est dite plutôt que masquée, et elle se lèvera le
 * jour où le panneau aura deux boutons déclarés.
 */
void demineur_hold(demineur *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    for (int i = 0; i < NS_GAME_BUTTON_COUNT; ++i) g->held[i] = held[i];
}

void demineur_tick(demineur *g, float dt)
{
    if (g->phase == DEM_DEAD || g->phase == DEM_WON) {
        g->dead_time += dt;
        return;
    }
    g->time += dt;
    if (g->robot_wait > 0.0f) g->robot_wait -= dt;

    /* Le défilement au maintien : sans lui, traverser vingt-cinq colonnes
     * demanderait vingt-cinq appuis. */
    const bool any = g->held[NS_GAME_UP] || g->held[NS_GAME_DOWN]
                  || g->held[NS_GAME_LEFT] || g->held[NS_GAME_RIGHT];
    if (!any) {
        g->repeat = 0.0f;
    } else {
        g->repeat -= dt;
        if (g->repeat <= 0.0f) {
            g->repeat = (g->repeat < -REPEAT_FIRST) ? REPEAT_FIRST : REPEAT_NEXT;
            if (g->held[NS_GAME_UP])    step_cursor(g, -1, 0);
            if (g->held[NS_GAME_DOWN])  step_cursor(g, +1, 0);
            if (g->held[NS_GAME_LEFT])  step_cursor(g, 0, -1);
            if (g->held[NS_GAME_RIGHT]) step_cursor(g, 0, +1);
        }
    }
}

/* ==========================================================================
 * Le joueur automatique
 * ==========================================================================
 * Il joue en DÉDUISANT, pas au hasard : une case dévoilée dont le nombre égale
 * le nombre de voisines masquées permet de les marquer toutes ; une case dont
 * tous les drapeaux sont posés permet d'ouvrir le reste. Quand il ne déduit
 * rien, il ouvre la case masquée la plus sûre qu'il connaisse.
 *
 * Il ne prouve pas que le jeu est amusant. Il prouve qu'on peut enchaîner des
 * milliers de pas, que le score monte, et que la cascade ne boucle pas.
 * ========================================================================== */

static int masked_around(const demineur *g, int r, int c, int *fr, int *fc)
{
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (!dr && !dc) continue;
            const int rr = r + dr, cc = c + dc;
            if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
            if (g->shown[rr][cc]) continue;
            n++;
            if (fr) *fr = rr;
            if (fc) *fc = cc;
        }
    }
    return n;
}

/* Poser le curseur ailleurs EST un geste : le signaler garde le journal de
 * partie fidèle à ce qui s'est passé, et c'est ce que le serveur limite en
 * fréquence. */
static void jump_cursor(demineur *g, int r, int c)
{
    if (g->cursor_r != r || g->cursor_c != c) g->moved = true;
    g->cursor_r = r;
    g->cursor_c = c;
}

/* Les voisines encore fermées ET sans drapeau : le dénominateur d'une
 * probabilité, là où `masked_around` compte le numérateur d'une déduction. */
static int open_masked_around(const demineur *g, int r, int c)
{
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (!dr && !dc) continue;
            const int rr = r + dr, cc = c + dc;
            if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
            if (!g->shown[rr][cc] && !g->flag[rr][cc]) n++;
        }
    }
    return n;
}

static int flags_around(const demineur *g, int r, int c)
{
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (!dr && !dc) continue;
            const int rr = r + dr, cc = c + dc;
            if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
            if (g->flag[rr][cc]) n++;
        }
    }
    return n;
}

/*
 * La cadence, et ce qu'elle protège.
 *
 * `room/main.c` appelle `autopilot` à CHAQUE pas, soit 120 fois par seconde.
 * Pour Flappy et Snake c'est sans conséquence — un appel oriente le vol ou le
 * virage. Ici un appel joue un COUP entier : sans frein, le joueur automatique
 * ouvrirait cent vingt cases par seconde, très au-delà des quinze que
 * `rulesTable["demineur"]` accepte, et toute partie capturée serait refusée par
 * l'anti-triche pour excès de fréquence.
 *
 * Un coup toutes les 150 ms, donc : 6,7 par seconde, sous les deux plafonds
 * (« cell » 15/s, « flag » 8/s), et une cascade qu'on voit s'ouvrir au lieu
 * d'une grille qui se résout d'un bloc entre deux images.
 */
#define ROBOT_PERIOD 0.15f

bool demineur_autopilot(demineur *g)
{
    if (g->phase == DEM_DEAD || g->phase == DEM_WON) return false;
    if (g->robot_wait > 0.0f) return false;
    g->robot_wait = ROBOT_PERIOD;

    if (!g->placed) {
        jump_cursor(g, DEM_ROWS / 2, DEM_COLS / 2);
        demineur_press(g, NS_GAME_ACTION);
        return true;
    }

    /* 1. Une case dont le nombre égale ses voisines masquées : toutes minées. */
    for (int r = 0; r < DEM_ROWS; ++r) {
        for (int c = 0; c < DEM_COLS; ++c) {
            if (!g->shown[r][c]) continue;
            const int n = neighbours(g, r, c);
            if (n == 0) continue;
            int fr = -1, fc = -1;
            if (masked_around(g, r, c, &fr, &fc) == n && flags_around(g, r, c) < n) {
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int rr = r + dr, cc = c + dc;
                        if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
                        if (g->shown[rr][cc] || g->flag[rr][cc]) continue;
                        jump_cursor(g, rr, cc);
                        g->flag[rr][cc] = true;
                        g->flags++;
                        g->score += PTS_FLAG;
                        g->pend_flags++;
                        return true;
                    }
                }
            }
        }
    }

    /* 2. Une case dont tous les drapeaux sont posés : le reste est sûr. */
    for (int r = 0; r < DEM_ROWS; ++r) {
        for (int c = 0; c < DEM_COLS; ++c) {
            if (!g->shown[r][c]) continue;
            const int n = neighbours(g, r, c);
            if (n == 0 || flags_around(g, r, c) != n) continue;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    const int rr = r + dr, cc = c + dc;
                    if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
                    if (g->shown[rr][cc] || g->flag[rr][cc]) continue;
                    jump_cursor(g, rr, cc);
                    demineur_press(g, NS_GAME_ACTION);
                    check_won(g);
                    return true;
                }
            }
        }
    }

    /*
     * 3. Plus rien à déduire : il faut DEVINER. Mais deviner n'est pas tirer au
     *    sort, et la différence se mesure.
     *
     *    Ouvrir bêtement la première case fermée — donc le coin haut-gauche —
     *    revient à jouer le point de la grille où l'on sait le moins de choses,
     *    au moment précis où l'on a besoin de savoir. Mesuré sur la graine des
     *    captures : mort en 0,3 s, deux coups joués.
     *
     *    On estime donc le risque de chaque case fermée : pour chacune de ses
     *    voisines dévoilées, la proportion de bombes qui lui restent parmi ses
     *    propres cases fermées, et l'on garde la PIRE — un chiffre qui accuse
     *    suffit à condamner. Une case qui ne touche aucun chiffre hérite de la
     *    densité globale, c'est-à-dire de ce qu'on sait sans rien savoir. On
     *    joue le minimum, et à risque égal la première rencontrée, pour que la
     *    partie reste rejouable à la graine près.
     */
    {
        int masked_total = 0;
        for (int r = 0; r < DEM_ROWS; ++r)
            for (int c = 0; c < DEM_COLS; ++c)
                if (!g->shown[r][c] && !g->flag[r][c]) masked_total++;
        if (masked_total == 0) return false;

        const int left = demineur_bombs(g) - (int)g->flags;
        float base = (left > 0) ? (float)left / (float)masked_total : 0.0f;

        int br = -1, bc = -1;
        float best_risk = 2.0f;

        for (int r = 0; r < DEM_ROWS; ++r) {
            for (int c = 0; c < DEM_COLS; ++c) {
                if (g->shown[r][c] || g->flag[r][c]) continue;

                float risk = base;
                bool informed = false;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (!dr && !dc) continue;
                        const int rr = r + dr, cc = c + dc;
                        if (rr < 0 || cc < 0 || rr >= DEM_ROWS || cc >= DEM_COLS) continue;
                        if (!g->shown[rr][cc]) continue;
                        const int m = open_masked_around(g, rr, cc);
                        if (m <= 0) continue;
                        const int rest = neighbours(g, rr, cc) - flags_around(g, rr, cc);
                        const float p = (float)rest / (float)m;
                        if (!informed) { risk = p; informed = true; }
                        else if (p > risk) { risk = p; }
                    }
                }
                if (risk < best_risk) { best_risk = risk; br = r; bc = c; }
            }
        }
        if (br < 0) return false;

        jump_cursor(g, br, bc);
        demineur_press(g, NS_GAME_ACTION);
        check_won(g);
        return true;
    }
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

typedef struct ctx { ns_sprite *s; float ox, oy, scale; } ctx;

static void blit_tile(const ctx *c, const ns_texture *t, tile tl,
                      float x, float y, float w, float h, const float rgba[4])
{
    if (!t || !t->handle) return;
    ns_sprite_texture(c->s, t);
    ns_sprite_quad(c->s, c->ox + x * c->scale, c->oy + y * c->scale,
                   w * c->scale, h * c->scale,
                   tl.col * TILE / ATLAS_W, tl.row * TILE / ATLAS_H,
                   (tl.col + 1.0f) * TILE / ATLAS_W, (tl.row + 1.0f) * TILE / ATLAS_H,
                   rgba);
}

void demineur_draw(ns_sprite *s, const demineur *g, const demineur_art *a,
                   float logical_w, float logical_h)
{
    const float sx = logical_w / DEM_W, sy = logical_h / DEM_H;
    ctx c;
    c.s = s;
    c.scale = (sx < sy) ? sx : sy;
    c.ox = (logical_w - DEM_W * c.scale) * 0.5f;
    c.oy = (logical_h - DEM_H * c.scale) * 0.5f;

    static const float back[4] = { 0.09f, 0.10f, 0.13f, 1.0f };
    ns_sprite_rect(s, c.ox, c.oy, DEM_W * c.scale, DEM_H * c.scale, back);

    /* La grille centrée : 25 x 50 = 1250 de large, 16 x 50 = 800 de haut. */
    const float gw = DEM_COLS * DEM_CELL, gh = DEM_ROWS * DEM_CELL;
    const float gx = (DEM_W - gw) * 0.5f, gy = (DEM_H - gh) * 0.5f + 20.0f;

    for (int r = 0; r < DEM_ROWS; ++r) {
        for (int col = 0; col < DEM_COLS; ++col) {
            const float x = gx + (float)col * DEM_CELL;
            const float y = gy + (float)r * DEM_CELL;

            tile t = T_MASKED;
            if (g->flag[r][col] && !g->shown[r][col]) {
                t = T_FLAG;
            } else if (g->shown[r][col]) {
                t = T_DIGIT[neighbours(g, r, col)];
            } else if (g->phase == DEM_DEAD && g->bomb[r][col]) {
                /* À la mort, toutes les bombes se montrent — c'est ce que fait
                 * l'original en passant les MASQUE_AVEC_BOMBES en BOMBE. */
                t = T_BOMB;
            }
            blit_tile(&c, &a->tiles, t, x, y, DEM_CELL, DEM_CELL, NULL);
        }
    }

    /* Le curseur : un cadre, pas une case pleine — on doit voir ce qu'il y a
     * dessous. Il respire pour se trouver d'un coup d'œil.
     *
     * L'ÉPAISSEUR EST UNE MESURE, pas un goût. Le jeu dessine dans un repère de
     * 1920 de large ; la dalle d'une borne est une cible de 512 x 288
     * (`room/main.c`), donc tout est divisé par 3,75 avant même d'être projeté
     * sur un quad déformé en barillet. Le trait de 4 px d'origine arrivait à
     * 1,07 px sur la dalle, et à 0,8 px à l'écran : la capture en borne ne
     * montrait AUCUN curseur. Sur un démineur qui se joue au manche, ne pas
     * voir où l'on est n'est pas une gêne, c'est l'impossibilité de jouer.
     *
     * 9 px donnent 2,4 px sur la dalle, et le fond translucide donne à la case
     * une masse qu'on repère sans chercher le trait. Le battement ne descend
     * plus sous 0,70 : à 0,55 il disparaissait la moitié du temps.
     */
    if (g->phase == DEM_READY || g->phase == DEM_PLAYING) {
        const float pulse = 0.70f + 0.30f * (0.5f + 0.5f * sinf(g->time * 6.0f));
        const float col[4] = { 1.0f, 0.85f, 0.30f, pulse };
        const float x = gx + (float)g->cursor_c * DEM_CELL;
        const float y = gy + (float)g->cursor_r * DEM_CELL;
        const float th = 9.0f;

        /* Le voile intérieur : c'est lui qui se voit de loin, le cadre ne fait
         * que dire exactement quelle case est visée. */
        const float wash[4] = { 1.0f, 0.85f, 0.30f, 0.22f * pulse };
        ns_sprite_rect(s, c.ox + x * c.scale, c.oy + y * c.scale,
                       DEM_CELL * c.scale, DEM_CELL * c.scale, wash);
        ns_sprite_rect(s, c.ox + x * c.scale, c.oy + y * c.scale,
                       DEM_CELL * c.scale, th * c.scale, col);
        ns_sprite_rect(s, c.ox + x * c.scale, c.oy + (y + DEM_CELL - th) * c.scale,
                       DEM_CELL * c.scale, th * c.scale, col);
        ns_sprite_rect(s, c.ox + x * c.scale, c.oy + y * c.scale,
                       th * c.scale, DEM_CELL * c.scale, col);
        ns_sprite_rect(s, c.ox + (x + DEM_CELL - th) * c.scale, c.oy + y * c.scale,
                       th * c.scale, DEM_CELL * c.scale, col);
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };

    /*
     * LA TAILLE DU TEXTE EST UNE MESURE. La fonte fait 7 px de haut, le repère
     * 1920 de large, la dalle d'une borne 512 : un texte écrit à `scale * k`
     * arrive sur la dalle à 7 x k x (512/1920) = 1,87 x k pixels. La recette en
     * borne dit où passe le seuil : à k = 6 (11 px) le score se lit ; à k = 4
     * (7,5 px) la capture ne montre qu'une tache — le score du démineur y était
     * illisible, et c'est la seule chose que le joueur vient chercher.
     *
     * Le plancher retenu est donc k = 6 pour le score et k = 5 pour les
     * consignes, sur les huit jeux.
     */
    char line[64];
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    const float ts = c.scale * 6.0f;
    ns_sprite_text(s, c.ox + (DEM_W * c.scale - ns_sprite_text_width(line, ts)) * 0.5f,
                   c.oy + 22.0f * c.scale, ts, white, line);

    if (g->phase == DEM_READY) {
        const char *msg = "MANCHE POUR VISER   BOUTON POUR OUVRIR";
        const float sc = c.scale * 5.0f;
        ns_sprite_text(s, c.ox + (DEM_W * c.scale - ns_sprite_text_width(msg, sc)) * 0.5f,
                       c.oy + DEM_H * c.scale * 0.92f, sc, white, msg);
    } else if (g->phase == DEM_DEAD || g->phase == DEM_WON) {
        /*
         * Un bandeau sombre SOUS le texte de fin. Sans lui, « PERDU » se pose
         * sur une grille de chiffres rouges, verts et bleus et devient illisible
         * — la seule information qui compte à ce moment-là est celle qu'on ne
         * lit plus. Il est translucide : on veut voir où les bombes étaient.
         */
        static const float veil[4] = { 0.03f, 0.03f, 0.05f, 0.78f };
        ns_sprite_rect(s, c.ox, c.oy + DEM_H * c.scale * 0.33f,
                       DEM_W * c.scale, DEM_H * c.scale * 0.34f, veil);

        const char *msg = (g->phase == DEM_WON) ? "GAGNE" : "PERDU";
        const float sc = c.scale * 9.0f;
        ns_sprite_text(s, c.ox + (DEM_W * c.scale - ns_sprite_text_width(msg, sc)) * 0.5f,
                       c.oy + DEM_H * c.scale * 0.40f, sc, amber, msg);

        SDL_snprintf(line, sizeof line, "MEILLEUR %u", g->best);
        const float sc2 = c.scale * 5.0f;
        ns_sprite_text(s, c.ox + (DEM_W * c.scale - ns_sprite_text_width(line, sc2)) * 0.5f,
                       c.oy + DEM_H * c.scale * 0.52f, sc2, white, line);

        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            ns_sprite_text(s, c.ox + (DEM_W * c.scale
                                      - ns_sprite_text_width(again, sc2)) * 0.5f,
                           c.oy + DEM_H * c.scale * 0.60f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool demineur_art_load(ns_rhi *r, demineur_art *a)
{
    memset(a, 0, sizeof *a);
    a->ready = ns_texture_load(r, &a->tiles, "games/demineur/demineur.png", true, false);
    if (!a->ready) NS_WARN("démineur : planche introuvable, le jeu tournera sans images");
    return a->ready;
}

void demineur_art_free(ns_rhi *r, demineur_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->tiles);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void dm_reset(void *g, uint64_t seed, bool hard) { demineur_reset((demineur *)g, seed, hard); }
static void dm_press(void *g, ns_game_button b) { demineur_press((demineur *)g, b); }
static void dm_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { demineur_hold((demineur *)g, h); }
static void dm_tick(void *g, float dt) { demineur_tick((demineur *)g, dt); }

static void dm_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    demineur_draw(s, (const demineur *)g, (const demineur_art *)a, w, h);
}

static bool dm_art_load(ns_rhi *r, void *a) { return demineur_art_load(r, (demineur_art *)a); }
static void dm_art_free(ns_rhi *r, void *a) { demineur_art_free(r, (demineur_art *)a); }
static bool dm_autopilot(void *g)           { return demineur_autopilot((demineur *)g); }

static uint32_t dm_score(const void *g)
{
    const int64_t v = ((const demineur *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t dm_best(const void *g) { return ((const demineur *)g)->best; }
static void     dm_set_best(void *g, uint32_t b) { ((demineur *)g)->best = b; }

static bool dm_dead(const void *g, float *dead_time)
{
    const demineur *d = (const demineur *)g;
    if (dead_time) *dead_time = d->dead_time;
    return d->phase == DEM_DEAD || d->phase == DEM_WON;
}

/* Le vocabulaire de `rulesTable["demineur"]` : une case ouverte vaut 5, un
 * drapeau 2, la victoire 500. Ce sont les noms du serveur, pas les nôtres. */
static const char *const dm_kinds[] = { "cell", "flag", "win", "move", "death", NULL };

/*
 * Un événement par appel, dans l'ordre où ils se sont produits, et la fin de
 * partie en dernier.
 *
 * Les trois règles que ça tient, et qu'un booléen ne tenait pas :
 *
 * 1. **« cell » porte son compte.** Une cascade est un seul coup qui ouvre N
 *    cases ; le serveur multiplie (`scaled`), il ne compte pas les événements.
 * 2. **La victoire est un événement distinct**, qui ne peut pas écraser les
 *    cases de la cascade qui l'a produite.
 * 3. **`die` attend que la file soit vide.** `finish_run` scelle le journal :
 *    tout gain annoncé après lui n'existe pas.
 */
static void dm_events(void *g, ns_game_events *out)
{
    demineur *d = (demineur *)g;

    out->blip = d->moved;
    /*
     * « move », PAS « cell ». Une case ouverte vaut cinq points côté serveur ;
     * journaliser chaque déplacement de curseur sous ce nom aurait gonflé le
     * score recalculé et fait rejeter toutes les parties. « move » est déclaré
     * muet dans `rulesTable["demineur"]` : compté pour l'anti-triche, sans valeur.
     */
    out->blip_kind = "move";
    d->moved = false;

    if (d->pend_cells) {
        out->score = true;
        out->score_kind = "cell";
        out->score_value = (int64_t)d->pend_cells;
        d->pend_cells = 0;
    } else if (d->pend_flags) {
        out->score = true;
        out->score_kind = "flag";
        out->score_value = 0;   /* barème fixe : deux points, quoi qu'il arrive */
        d->pend_flags--;
    } else if (d->pend_win) {
        out->score = true;
        out->score_kind = "win";
        out->score_value = 0;
        d->pend_win = false;
    } else if (d->over) {
        /* La file est vide : la partie peut être scellée. */
        out->die = true;
        d->over = false;
    }
}

const ns_game_api g_demineur_api = {
    .id = "demineur", .title = "DEMINEUR", .label = "MINES",
    .state_size = sizeof(demineur), .art_size = sizeof(demineur_art),
    /* Le Démineur de 2020 n'avait AUCUN son — son dossier ne contient que la
     * planche et deux polices. On lui en prête donc deux, comme `config.h`
     * empruntait déjà ses bruitages d'un jeu à l'autre, et on laisse le
     * déplacement du curseur muet : un clic à chaque case traversée serait
     * insupportable au bout de vingt-cinq colonnes. */
    .sound_blip = NULL,
    .sound_score = "games/flappy/score.wav",
    .sound_die = "games/snake/gameover.wav",
    .art_load = dm_art_load, .art_free = dm_art_free,
    .reset = dm_reset, .press = dm_press, .hold = dm_hold,
    .tick = dm_tick, .draw = dm_draw, .autopilot = dm_autopilot,
    .event_kinds = dm_kinds,
    .score = dm_score, .best = dm_best, .set_best = dm_set_best,
    .dead = dm_dead, .events = dm_events,
};
