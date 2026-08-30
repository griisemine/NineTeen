/* snake.c — voir snake.h pour ce qui est repris de 2020 et ce qui ne l'est pas. */
#include "snake.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Les cotes de 2020, et leur conversion
 * ==========================================================================
 * L'original tourne à 30 images par seconde et compte TOUT en images. Chaque
 * constante garde ici son nom et sa valeur d'origine ; la conversion en secondes
 * est écrite à côté, une fois, pour qu'on puisse la vérifier au lieu de la
 * croire.
 * ========================================================================== */

#define FPS30 30.0f

/* --- le serpent ------------------------------------------------------------ */
#define BODY_RADIUS        20.0f    /* BODY_RADIUS */
#define INIT_BODY          30       /* INIT_BODY : segments au départ */
#define INIT_DECAL_Y        5.0f    /* INIT_DECAL : le corps part vers le bas */
#define BASE_ANGLE         (3.0f * NS_PI / 2.0f)   /* vers le haut */
#define TURN_RATE          (0.13f * FPS30)         /* 3,9 rad/s */
#define BODY_DEATH_HITBOX  35.0f
#define MIN_BODY_PARTS      5
#define RATIO_RADIUS_BODYADD 6.6f
#define SPEED_DECOMPOSITION  5.0f   /* le pas élémentaire, en pixels */

/* --- la vitesse ------------------------------------------------------------ */
#define BASE_SPEED   7.0f           /* pixels PAR IMAGE : 210 px/s */
#define MIN_SPEED    5.01f
#define SCALE_SPEED  0.0035f

/* --- les fruits ------------------------------------------------------------ */
#define FRUIT_TTL          (14.0f)              /* FRUIT_TTL = 14 s */
#define RATIO_TTL_HARDCORE  0.5f
#define DIST_HEAD_FRUIT    30.0f
#define DIST_WALL_FRUIT    30.0f
#define DIST_CORNER_FRUIT  60.0f
#define DIST_FRUITS        10.0f
#define MAX_TRIES_RAND     100
#define GIANT_SIZE         40.0f
#define GIANT_SCORE        10
#define GIANT_SPEED         2.0f
#define GIANT_CHANCE       200
#define APPEAR_MAX         50.0f
#define HITBOX_SECURITY     1.0f
#define HITBOX_GENTILLE     2.33f

/* CHANCE_SPAWN_FRUIT = 1 * FPS images, soit une tentative par seconde. */
#define SPAWN_PERIOD        1.0f
/* CHANCE_SPAWN_BONUS = 12 * FPS images, soit toutes les douze secondes. */
#define BONUS_PERIOD       12.0f

/* Hardcore : la cadence part de 6 s et descend à 1,6 s en trois minutes. */
#define HC_RATE_INIT        6.0f
#define HC_RATE_MIN         1.6f
#define HC_RATE_DURATION  180.0f

#define RATIO_RADIUS_HARDCORE   0.8f
#define RATIO_SPEED_HARDCORE    5.0f
#define RATIO_GET_FRUIT_HARDCORE (-5)   /* MANGER COÛTE. Voir snake.h. */
#define FRUIT_EATEN_HARDCORE    (-5.0f)
#define FRUIT_TIMEOUT_EATEN_HARDCORE 0.18f
#define FRUIT_TIMEOUT_SCORE_HARDCORE 0.25f

/* --- l'invincibilité et la digestion --------------------------------------- */
#define INVINCIBILITY       8.0f     /* NB_FRAME_INVINCIBILITY = 8 * FPS */
#define GIANT_DIGESTION    15
#define DURATION_POTION     6.0f     /* DURATION_POTION = 180 images */
#define NB_FRAME_CAFE_ACCE  4.0f     /* 4 * FPS */
#define RELATIVE_ACCELERATE_COFEE 0.25f
#define POTION_POISON_ADD  (1.5f)    /* par seconde */
#define FLAT_RM_BOMB       10
#define RELATIVE_RM_BOMB    0.2f
#define RELATIVE_SLOW_FEATHER 0.3f
#define FLAT_REDUCE_SCISOR 10
#define RELATIVE_REDUCE_SCISOR 0.15f
#define CHEST_BOOST_FRUIT_EATEN 5
#define RAINBOW_BOOST_FRUIT_EATEN 5

/* --- les planches ---------------------------------------------------------- */
#define CELL 64.0f                  /* FRUIT_DIM et BODY_DIM valent 64 x 64 */
#define DIGIT_W 12.0f
#define DIGIT_H 18.0f
#define ANIM_FRAMES 4               /* NB_ANIM_SPAWN / NB_ANIM_DEATH */
#define SPAWN_ANIM_TIME 0.4f        /* NB_FRAME_SPAWN_FRUIT = 12 images */
#define DEATH_ANIM_TIME 0.4f

/* Les identifiants, dans l'ordre de l'énumération `FRUITS` de 2020 — qui est
 * aussi l'ordre des tuiles de `fruits.png`. */
enum {
    FRAISE = 0, ORANGE, CITROUILLE, PIMENT, CERISE, POMME, PASTEQUE, CAROTTE,
    ANANAS, TOMATE, FROMAGE, VIANDE, PIZZA, BURGER, HOT_DOG, PANCAKES, SUCETTE,
    GLACE_BATON, GLACE_CONE, GLACE_POT, DONUT, MUFFIN, GATEAU, MUFFIN_ROSE,
    CAFE, PLUME, BOMBE, COFFRE, ARC_EN_CIEL, POTION_HITBOX, POTION_VERTE,
    POTION_JAUNE
};

/* Les six propriétés, dans l'ordre de l'énumération `PROPRIETES`. */
enum { P_ACCE = 0, P_RADIUS, P_PROBA, P_SCORE, P_DIGEST, P_APPEAR };

/*
 * `FRUIT_PROPRIETES`, recopiée intégralement depuis `config.h`.
 *
 * C'est elle qui fait qu'on rejoue au même jeu : l'ordre d'apparition des
 * objets, leur valeur, ce qu'ils font au serpent. La modifier, c'est changer
 * l'équilibre que l'auteur a réglé.
 */
static const float FRUIT_PROP[SNAKE_ITEMS][6] = {
    /*  ACCE    RADIUS  PROBA   SCORE   DIGEST  APPEAR */
    {  0.02f,   20.0f,  2.0f,      20.0f,  5.0f,  0.0f }, /* fraise */
    {  0.05f,   24.0f,  2.0f,      50.0f,  8.0f,  0.0f }, /* orange */
    {  0.09f,   36.0f,  2.0f,     150.0f, 13.0f,  0.0f }, /* citrouille */
    {  0.18f,   18.0f,  3.0f,     200.0f,  6.0f,  5.0f }, /* piment */
    {  0.02f,   18.0f,  3.0f,      41.0f,  4.0f,  8.0f }, /* cerise */
    {  0.05f,   22.0f,  4.0f,     107.0f,  7.0f, 10.0f }, /* pomme */
    {  0.06f,   30.0f,  4.0f,     350.0f, 11.0f, 11.0f }, /* pastèque */
    {  0.03f,   22.0f,  4.0f,     128.0f,  7.0f, 13.0f }, /* carotte */
    {  0.09f,   26.0f,  4.0f,     460.0f, 11.0f, 16.0f }, /* ananas */
    {  0.07f,   22.0f,  5.0f,     132.0f,  8.0f, 18.0f }, /* tomate */
    {  0.10f,   22.0f,  4.0f,     500.0f, 13.0f, 21.0f }, /* fromage */
    {  0.10f,   26.0f,  4.0f,    1000.0f, 11.0f, 24.0f }, /* viande */
    {  0.005f,  28.0f,  5.0f,    1234.0f, 18.0f, 28.0f }, /* pizza */
    {  0.0f,    24.0f,  6.0f,    2400.0f, 19.0f, 30.0f }, /* burger */
    {  0.0f,    24.0f,  5.0f,    3500.0f, 18.0f, 32.0f }, /* hot-dog */
    {  0.0f,    24.0f,  6.0f,    2222.0f, 17.0f, 34.0f }, /* pancakes */
    {  0.05f,   22.0f,  6.0f,    1700.0f, 12.0f, 36.0f }, /* sucette */
    { -0.44f,   24.0f,  5.0f,     500.0f, 14.0f, 42.0f }, /* glace bâton */
    { -0.66f,   24.0f,  5.0f,     600.0f, 14.0f, 43.0f }, /* glace cône */
    { -0.88f,   24.0f,  4.0f,     700.0f, 14.0f, 44.0f }, /* glace pot */
    {  0.08f,   24.0f,  5.0f,    2180.0f, 15.0f, 45.0f }, /* donut */
    {  0.09f,   26.0f,  4.0f,    3700.0f, 16.0f, 46.0f }, /* muffin */
    {  0.08f,   24.0f,  3.0f,    5000.0f, 18.0f, 47.0f }, /* gâteau */
    {  0.06f,   26.0f,  2.0f,   10000.0f, 16.0f, 50.0f }, /* muffin rose */
    {  0.0f,    24.0f,  1.0f,    5000.0f,  3.0f, 15.0f }, /* café */
    { -1.0f,    28.0f,  8.0f,       0.0f,  0.0f,  5.0f }, /* plume */
    {  0.0f,    28.0f,  8.0f,       0.0f,  0.0f,  5.0f }, /* bombe */
    {  0.0f,    28.0f,  9.0f,       0.0f,  0.0f,  0.0f }, /* coffre */
    {  0.0f,    28.0f,  7.0f,       0.0f,  0.0f, 20.0f }, /* arc-en-ciel */
    {  0.0f,    28.0f,  6.0f,       0.0f,  0.0f, 25.0f }, /* potion hitbox */
    {  0.0f,    28.0f,  3.0f,       0.0f,  0.0f, 35.0f }, /* potion verte */
    {  0.0f,    28.0f,  2.0f,   30000.0f,  0.0f, 45.0f }, /* potion jaune */
};

/*
 * Les courbes de digestion, construites une fois par partie par `build_curves`.
 *
 * `FRUIT_ADD_RADIUS[id][i]` est le supplément de rayon déposé dans la case `i`
 * du tuyau de digestion quand on avale l'objet `id`. La courbe est une cloche —
 * d'où la bosse qu'on voit descendre le corps — recalée à droite pour que la
 * digestion commence tout de suite.
 *
 * Le calcul est celui de `snakeInit`, y compris son `1.2 / j` qui divise par
 * zéro au premier tour : en C, `1.2 / 0` en flottant vaut l'infini, `t` vaut
 * moins l'infini, et la branche `t >= 0` range un zéro. Le résultat est donc
 * bien défini et c'est celui de 2020 ; on l'écrit explicitement plutôt que de
 * reproduire la division.
 */
static float g_add_radius[SNAKE_ITEMS][SNAKE_PRE];
static float g_add_radius_giant[SNAKE_ITEMS][SNAKE_PRE];
static bool  g_curves_ready = false;

static void build_one(float out[SNAKE_ITEMS][SNAKE_PRE], float extra)
{
    for (int i = 0; i < SNAKE_ITEMS; ++i) {
        for (int k = 0; k < SNAKE_PRE; ++k) out[i][k] = 0.0f;

        const float digest = FRUIT_PROP[i][P_DIGEST] + extra;
        for (int j = 0; j < SNAKE_PRE / 2; ++j) {
            /* j == 0 donnait −infini dans l'original, donc zéro. */
            const float t = (j == 0) ? -1.0f
                          : ((float)j - 1.2f / (float)j) * digest / (float)(SNAKE_PRE / 2);
            if (t >= 0.0f) {
                out[i][j] = t;
                out[i][SNAKE_PRE - 1 - j] = t;
            }
        }
        out[i][SNAKE_PRE / 2] = digest;

        /* « Coller à droite » : on pousse la courbe vers la fin du tuyau. */
        int zeros = 0;
        for (int j = SNAKE_PRE - 1; j >= 0 && out[i][j] == 0.0f; --j) zeros++;
        if (zeros) {
            for (int k = SNAKE_PRE - 1 - zeros; k >= 0; --k) {
                out[i][k + zeros] = out[i][k];
                out[i][k] = 0.0f;
            }
        }
    }
}

static void build_curves(void)
{
    if (g_curves_ready) return;
    build_one(g_add_radius, 0.0f);
    build_one(g_add_radius_giant, (float)GIANT_DIGESTION);
    g_curves_ready = true;
}

/* ==========================================================================
 * Mise en place
 * ========================================================================== */

static void clear_past(snake *g)
{
    for (int i = 0; i < SNAKE_REMIND; ++i) { g->past_x[i] = g->past_y[i] = -500.0f; }
}

void snake_reset(snake *g, uint64_t seed, bool hard)
{
    build_curves();

    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0x5EEDu);
    g->hard = hard;
    g->phase = SNAKE_READY;

    g->angle = BASE_ANGLE;
    g->speed = BASE_SPEED;
    g->parts = SNAKE_PRE + INIT_BODY;

    /* La tête au centre, le corps qui traîne derrière vers le bas. */
    g->part[SNAKE_PRE].x = SNAKE_PLAY_W * 0.5f;
    g->part[SNAKE_PRE].y = SNAKE_PLAY_H * 0.5f;
    for (uint32_t i = SNAKE_PRE + 1; i < g->parts; ++i) {
        g->part[i].x = g->part[i - 1].x;
        g->part[i].y = g->part[i - 1].y + INIT_DECAL_Y;
    }

    clear_past(g);
    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) g->fruit[i].id = -1;
    g->fruit_slots = 1;
    g->bonus.id = -1;

    g->invincible = INVINCIBILITY;
    g->fruit_timer = SPAWN_PERIOD;
    g->bonus_timer = BONUS_PERIOD;
    g->hardcore_rate = HC_RATE_INIT;
}

/* ==========================================================================
 * Le placement des fruits
 * ========================================================================== */

static bool too_close_wall(float x, float y, float d)
{
    return x < d || y < d || x > SNAKE_PLAY_W - d || y > SNAKE_PLAY_H - d;
}

static bool too_close_corner(float x, float y)
{
    const float d = DIST_CORNER_FRUIT;
    return (x < d || x > SNAKE_PLAY_W - d) && (y < d || y > SNAKE_PLAY_H - d);
}

static bool too_close_head(const snake *g, float x, float y, float radius)
{
    const float dx = x - g->part[SNAKE_PRE].x, dy = y - g->part[SNAKE_PRE].y;
    return sqrtf(dx * dx + dy * dy) < radius + BODY_RADIUS + DIST_HEAD_FRUIT;
}

static bool too_close_fruit(const snake *g, int skip, float x, float y, float radius)
{
    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) {
        if (i == skip || g->fruit[i].id < 0) continue;
        const float dx = x - g->fruit[i].x, dy = y - g->fruit[i].y;
        const float r = radius + FRUIT_PROP[g->fruit[i].id][P_RADIUS] + DIST_FRUITS;
        if (sqrtf(dx * dx + dy * dy) < r) return true;
    }
    return false;
}

/*
 * Tire un objet au sort, pondéré par sa probabilité et débloqué par le nombre de
 * fruits déjà mangés — `MIN_FRUIT_TO_APPEAR` dans la table. C'est ce qui fait
 * qu'une partie commence avec des fraises et finit avec des gâteaux.
 */
static int pick_fruit(snake *g, bool bonus_only)
{
    const int first = bonus_only ? PLUME : 0;
    const int last  = bonus_only ? SNAKE_ITEMS : CAFE + 1;

    float total = 0.0f;
    for (int i = first; i < last; ++i) {
        if (g->fruits_eaten < FRUIT_PROP[i][P_APPEAR]) continue;
        total += FRUIT_PROP[i][P_PROBA];
    }
    if (total <= 0.0f) return bonus_only ? -1 : FRAISE;

    float pick = ns_rng_range(&g->rng, 0.0f, total);
    for (int i = first; i < last; ++i) {
        if (g->fruits_eaten < FRUIT_PROP[i][P_APPEAR]) continue;
        pick -= FRUIT_PROP[i][P_PROBA];
        if (pick <= 0.0f) return i;
    }
    return bonus_only ? PLUME : FRAISE;
}

static void spawn_fruit(snake *g)
{
    int slot = -1;
    for (uint32_t i = 0; i < g->fruit_slots && i < SNAKE_MAX_FRUITS; ++i) {
        if (g->fruit[i].id < 0) { slot = (int)i; break; }
    }
    if (slot < 0) return;

    const int id = pick_fruit(g, false);
    const bool giant = (FRUIT_PROP[id][P_SCORE] > 0.0f)
                    && (ns_rng_below(&g->rng, GIANT_CHANCE) == 0);
    const float radius = FRUIT_PROP[id][P_RADIUS] * (giant ? (GIANT_SIZE / 24.0f) : 1.0f)
                       * (g->hard ? RATIO_RADIUS_HARDCORE : 1.0f);

    /* MAX_TRIES_RAND essais, puis on renonce : mieux vaut ne pas faire
     * apparaître un fruit que de le poser sur la tête du joueur. */
    for (int tries = 0; tries < MAX_TRIES_RAND; ++tries) {
        const float x = ns_rng_range(&g->rng, DIST_WALL_FRUIT, SNAKE_PLAY_W - DIST_WALL_FRUIT);
        const float y = ns_rng_range(&g->rng, DIST_WALL_FRUIT, SNAKE_PLAY_H - DIST_WALL_FRUIT);
        if (too_close_wall(x, y, DIST_WALL_FRUIT + radius)) continue;
        if (too_close_corner(x, y)) continue;
        if (too_close_head(g, x, y, radius)) continue;
        if (too_close_fruit(g, slot, x, y, radius)) continue;

        g->fruit[slot] = (snake_fruit){ x, y, x, y, id, giant, false, 0.0f, 1.0f };
        return;
    }
}

static void spawn_bonus(snake *g)
{
    if (g->bonus.id >= 0) return;
    const int id = pick_fruit(g, true);
    if (id < 0) return;

    const float radius = FRUIT_PROP[id][P_RADIUS];
    for (int tries = 0; tries < MAX_TRIES_RAND; ++tries) {
        const float x = ns_rng_range(&g->rng, DIST_WALL_FRUIT, SNAKE_PLAY_W - DIST_WALL_FRUIT);
        const float y = ns_rng_range(&g->rng, DIST_WALL_FRUIT, SNAKE_PLAY_H - DIST_WALL_FRUIT);
        if (too_close_wall(x, y, DIST_WALL_FRUIT + radius)) continue;
        if (too_close_head(g, x, y, radius)) continue;
        if (too_close_fruit(g, -1, x, y, radius)) continue;
        g->bonus = (snake_fruit){ x, y, x, y, id, false, false, 0.0f, 1.0f };
        return;
    }
}

/* ==========================================================================
 * Le corps : allonger, raccourcir, digérer
 * ========================================================================== */

static void shift_free_space(snake *g)
{
    for (int i = SNAKE_REMIND - 2; i >= 0; --i) {
        g->past_x[i + 1] = g->past_x[i];
        g->past_y[i + 1] = g->past_y[i];
    }
    g->past_x[0] = g->past_y[0] = -500.0f;
}

static void shift_refresh(snake *g, int n)
{
    for (int i = 0; i < SNAKE_REMIND - n; ++i) {
        g->past_x[i] = g->past_x[i + n];
        g->past_y[i] = g->past_y[i + n];
    }
    for (int i = SNAKE_REMIND - n; i < SNAKE_REMIND; ++i) {
        if (i >= 0) { g->past_x[i] = g->past_y[i] = -500.0f; }
    }
}

/* `addBody` : le serpent s'allonge en reprenant les positions que sa queue vient
 * de libérer — sinon il pousserait un segment au hasard, visible aussitôt. */
static int add_body(snake *g)
{
    int n = (int)(g->radius_left / RATIO_RADIUS_BODYADD);
    if (n <= 0) return 0;
    if (g->parts + (uint32_t)n >= SNAKE_MAX_PARTS) n = (int)(SNAKE_MAX_PARTS - g->parts - 1);
    if (n <= 0) return 0;

    g->part[g->parts - 1].radius = 0.0f;

    for (int i = 0; i < n; ++i) {
        const uint32_t k = g->parts + (uint32_t)i;
        if (g->past_x[i] <= -400.0f) {
            /* Rien en mémoire : on prolonge la queue en ligne droite. */
            g->part[k].x = 2.0f * g->part[k - 1].x - g->part[k - 2].x;
            g->part[k].y = 2.0f * g->part[k - 1].y - g->part[k - 2].y;
        } else {
            g->part[k].x = g->past_x[i];
            g->part[k].y = g->past_y[i];
        }
        g->part[k].radius = 0.0f;
    }

    shift_refresh(g, n);
    g->parts += (uint32_t)n;
    g->radius_left -= (float)n * RATIO_RADIUS_BODYADD;
    return n;
}

/* `rmBody` : la bombe et les ciseaux. Ne peut pas tuer — un bonus qui tue serait
 * un malus, et l'original s'en garde explicitement. */
static void remove_body(snake *g, int n)
{
    const uint32_t floor_parts = SNAKE_PRE + MIN_BODY_PARTS;
    if (g->parts <= floor_parts) return;
    if ((uint32_t)n > g->parts - floor_parts) n = (int)(g->parts - floor_parts);
    if (n <= 0) return;

    for (int i = 0; i < n; ++i) {
        if (g->dead_count < SNAKE_MAX_DEAD) {
            const snake_part *p = &g->part[g->parts - (uint32_t)n + (uint32_t)i];
            g->dead[g->dead_count++] = (snake_dead){ p->x, p->y, BODY_RADIUS + p->radius, 0.0f };
        }
    }
    g->parts -= (uint32_t)n;
}

/* `shiftRadius` : la digestion avance d'un cran. C'est ce qui fait descendre la
 * bosse, et c'est appelé une fois par pas de 5 px — deux fois un pas sur deux,
 * comme dans l'original. */
static void shift_radius(snake *g)
{
    g->radius_left += g->part[g->parts - 1].radius;
    if (g->radius_left >= RATIO_RADIUS_BODYADD) (void)add_body(g);

    for (uint32_t i = g->parts - 1; i > 0; --i) g->part[i].radius = g->part[i - 1].radius;
    g->part[0].radius = 0.0f;
}

/* ==========================================================================
 * Manger
 * ========================================================================== */

static void popup(snake *g, float x, float y, int64_t value)
{
    if (g->popup_count >= SNAKE_MAX_POPUPS) return;
    const double mag = (value < 0 ? -(double)value : (double)value);
    float size = 22.0f + 3.0f * (float)log(mag > 1.0 ? mag : 1.0);
    if (size > 45.0f) size = 45.0f;
    g->popup[g->popup_count++] = (snake_popup){ x, y, value, 0.0f, size };
}

static void digest(snake *g, int id, bool giant)
{
    const float (*curve)[SNAKE_PRE] = giant ? g_add_radius_giant : g_add_radius;
    const float ratio = g->hard ? RATIO_RADIUS_HARDCORE : 1.0f;
    for (int i = 0; i < SNAKE_PRE; ++i) g->part[i].radius += ratio * curve[id][i];
}

static void eat(snake *g, snake_fruit *f, bool is_bonus)
{
    const int id = f->id;

    digest(g, id, f->giant);

    g->speed += (g->hard ? RATIO_SPEED_HARDCORE : 1.0f)
              * (f->giant ? GIANT_SPEED : 1.0f) * FRUIT_PROP[id][P_ACCE];
    if (g->speed < MIN_SPEED) g->speed = MIN_SPEED;

    /* Le score. En hardcore il est NÉGATIF pour tout sauf la potion jaune :
     * manger coûte, et c'est la règle du mode. */
    const int64_t base = (int64_t)FRUIT_PROP[id][P_SCORE];
    int64_t gain = base * (f->giant ? GIANT_SCORE : 1);
    if (g->hard && id != POTION_JAUNE) gain *= RATIO_GET_FRUIT_HARDCORE;

    if (base != 0) {
        g->score += gain;
        popup(g, f->x, f->y, gain);
        g->ate = true;
        g->ate_value = gain;
        g->ate_is_bonus = is_bonus;
    }

    g->fruits_eaten += g->hard ? FRUIT_EATEN_HARDCORE : 1.0f;
    if (g->fruits_eaten < 0.0f) g->fruits_eaten = 0.0f;

    /* Les effets particuliers. */
    switch (id) {
        case CAFE:   g->coffee = NB_FRAME_CAFE_ACCE; break;
        case PLUME:  g->speed -= g->speed * RELATIVE_SLOW_FEATHER;
                     if (g->speed < MIN_SPEED) g->speed = MIN_SPEED;
                     break;
        case BOMBE:  remove_body(g, FLAT_RM_BOMB
                                  + (int)((float)(g->parts - SNAKE_PRE) * RELATIVE_RM_BOMB));
                     g->ate = true; g->ate_value = 0; g->ate_is_bonus = true;
                     break;
        case COFFRE: g->fruits_eaten += CHEST_BOOST_FRUIT_EATEN;
                     for (int k = 0; k < 6; ++k) spawn_fruit(g);
                     g->ate = true; g->ate_value = 0; g->ate_is_bonus = true;
                     break;
        case ARC_EN_CIEL: g->fruits_eaten += RAINBOW_BOOST_FRUIT_EATEN;
                     for (int k = 0; k < 12; ++k) spawn_fruit(g);
                     g->ate = true; g->ate_value = 0; g->ate_is_bonus = true;
                     break;
        case POTION_HITBOX: if (g->potions < 5) g->potions++; break;
        case POTION_VERTE:  g->poisoned = DURATION_POTION; break;
        default: break;
    }

    /* Une case de plus s'ouvre de temps en temps : c'est ce qui fait qu'on
     * finit avec un terrain couvert de fruits. */
    if (!is_bonus && g->fruit_slots < SNAKE_MAX_FRUITS
        && (int)g->fruits_eaten / 6 >= (int)g->fruit_slots) {
        g->fruit_slots++;
    }
    f->id = -1;
}

/* ==========================================================================
 * La mort
 * ========================================================================== */

static bool hits_tail(const snake *g)
{
    const snake_part *h = &g->part[SNAKE_PRE];
    /* On saute les premiers segments : ils touchent la tête par construction. */
    for (uint32_t i = SNAKE_PRE + MIN_BODY_PARTS + 8; i < g->parts; ++i) {
        const float dx = h->x - g->part[i].x, dy = h->y - g->part[i].y;
        const float r = BODY_DEATH_HITBOX + g->part[i].radius;
        if (dx * dx + dy * dy < r * r) return true;
    }
    return false;
}

static bool hits_wall(const snake *g)
{
    const snake_part *h = &g->part[SNAKE_PRE];
    const float r = BODY_RADIUS * 0.5f;
    return h->x < r || h->y < r || h->x > SNAKE_PLAY_W - r || h->y > SNAKE_PLAY_H - r;
}

/* ==========================================================================
 * L'avancée
 * ========================================================================== */

void snake_hold(snake *g, bool left, bool right, bool accelerate)
{
    g->turning_left = left;
    g->turning_right = right;
    g->accelerating = accelerate;
}

/* Un pas élémentaire de 5 px, celui de `moveSnake`. */
static void step_once(snake *g)
{
    shift_free_space(g);
    g->past_x[0] = g->part[g->parts - 1].x;
    g->past_y[0] = g->part[g->parts - 1].y;

    for (uint32_t i = g->parts - 1; i > SNAKE_PRE; --i) {
        g->part[i].x = g->part[i - 1].x;
        g->part[i].y = g->part[i - 1].y;
    }
    g->part[SNAKE_PRE].x += cosf(g->angle) * SPEED_DECOMPOSITION;
    g->part[SNAKE_PRE].y += sinf(g->angle) * SPEED_DECOMPOSITION;
}

static float fruit_ttl(const snake *g)
{
    return FRUIT_TTL * (g->hard ? RATIO_TTL_HARDCORE : 1.0f);
}

void snake_tick(snake *g, float dt)
{
    /*
     * Les drapeaux d'événement ne sont PAS remis à zéro ici : c'est
     * `snake_events` qui les consomme, une fois et une seule.
     *
     * Les effacer en tête de `tick` était le défaut trouvé dans Flappy en B12 —
     * `press` est appelée depuis le gestionnaire d'événements, donc avant la
     * boucle de pas fixe, et ce qu'elle lève serait effacé avant d'être lu. Ici
     * s'ajoute une seconde raison : la boucle peut consommer PLUSIEURS pas dans
     * une image, et une mort survenue au premier serait effacée par le second.
     */
    if (g->phase == SNAKE_DEAD) {
        g->dead_time += dt;
        for (uint32_t i = 0; i < g->popup_count; ++i) g->popup[i].age += dt;
        return;
    }

    g->time += dt;
    if (g->phase == SNAKE_READY && g->time > 0.6f) g->phase = SNAKE_PLAYING;

    /* --- la direction --------------------------------------------------- */
    if (g->turning_left != g->turning_right) {
        g->angle += (g->turning_right ? TURN_RATE : -TURN_RATE) * dt;
        if (g->angle < 0.0f) g->angle += 2.0f * NS_PI;
        if (g->angle > 2.0f * NS_PI) g->angle -= 2.0f * NS_PI;
        g->turned = true;
    }

    /* --- les compteurs -------------------------------------------------- */
    if (g->invincible > 0.0f) g->invincible -= dt;
    if (g->coffee > 0.0f) g->coffee -= dt;
    if (g->poisoned > 0.0f) {
        g->poisoned -= dt;
        /* La potion verte fait grossir tout seul : du rayon entre dans le tuyau
         * sans qu'on ait rien mangé. */
        g->part[0].radius += POTION_POISON_ADD * dt;
    }

    /* --- la distance parcourue ------------------------------------------ */
    float speed = g->speed;
    if (g->coffee > 0.0f || g->accelerating) speed += speed * RELATIVE_ACCELERATE_COFEE;
    if (speed < MIN_SPEED) speed = MIN_SPEED;

    /* `speed` est en pixels PAR IMAGE À 30 Hz : on convertit ici, et une seule
     * fois. L'accumulateur rend le résultat indépendant de la fréquence de tick
     * tout en gardant les pas de 5 px de l'original. */
    g->step_carry += speed * FPS30 * dt;

    int guard = 0;
    while (g->step_carry >= SPEED_DECOMPOSITION && guard++ < 64) {
        g->step_carry -= SPEED_DECOMPOSITION;
        step_once(g);
        shift_radius(g);
        g->step_parity ^= 1;
        if (g->step_parity) shift_radius(g);

        /* La collision se teste À CHAQUE PAS et pas une fois par image : à
         * grande vitesse le serpent parcourt plus que sa propre hitbox en une
         * image, et il traverserait sa queue sans la voir. */
        if (g->phase == SNAKE_PLAYING && g->invincible <= 0.0f
            && (hits_wall(g) || hits_tail(g))) {
            g->phase = SNAKE_DEAD;
            g->died = true;
            g->dead_time = 0.0f;
            break;
        }
    }

    if (g->phase == SNAKE_DEAD) return;

    /* --- les fruits ----------------------------------------------------- */
    const float ttl = fruit_ttl(g);
    const float head_r = BODY_RADIUS * (g->potions > 0 ? HITBOX_GENTILLE : HITBOX_SECURITY);

    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) {
        snake_fruit *f = &g->fruit[i];
        if (f->id < 0) continue;
        f->age += dt;

        if (f->age > ttl) {
            /* En hardcore, un fruit qu'on laisse expirer RAPPORTE — c'est là que
             * le score vient, et c'est tout l'inverse du mode normal. */
            if (g->hard) {
                const int64_t gain = (int64_t)(FRUIT_PROP[f->id][P_SCORE]
                                             * FRUIT_TIMEOUT_SCORE_HARDCORE);
                if (gain != 0) {
                    g->score += gain;
                    popup(g, f->x, f->y, gain);
                    g->ate = true;
                    g->ate_value = gain;
                    g->ate_is_bonus = false;
                }
                g->fruits_eaten += FRUIT_TIMEOUT_EATEN_HARDCORE;
            }
            f->id = -1;
            continue;
        }

        const float dx = f->x - g->part[SNAKE_PRE].x, dy = f->y - g->part[SNAKE_PRE].y;
        const float r = head_r + FRUIT_PROP[f->id][P_RADIUS]
                      * (f->giant ? (GIANT_SIZE / 24.0f) : 1.0f);
        if (dx * dx + dy * dy < r * r) {
            if (g->potions > 0) g->potions--;
            eat(g, f, false);
        }
    }

    if (g->bonus.id >= 0) {
        g->bonus.age += dt;
        if (g->bonus.age > ttl) {
            g->bonus.id = -1;
        } else {
            const float dx = g->bonus.x - g->part[SNAKE_PRE].x;
            const float dy = g->bonus.y - g->part[SNAKE_PRE].y;
            const float r = head_r + FRUIT_PROP[g->bonus.id][P_RADIUS];
            if (dx * dx + dy * dy < r * r) eat(g, &g->bonus, true);
        }
    }

    /* --- les apparitions ------------------------------------------------ */
    g->fruit_timer -= dt;
    if (g->fruit_timer <= 0.0f) {
        if (g->hard) {
            /* La cadence s'accélère de 6 s à 1,6 s en trois minutes. */
            const float t = g->time / HC_RATE_DURATION;
            g->hardcore_rate = HC_RATE_INIT + (HC_RATE_MIN - HC_RATE_INIT) * ns_clampf(t, 0.0f, 1.0f);
            g->fruit_timer = g->hardcore_rate;
        } else {
            g->fruit_timer = SPAWN_PERIOD;
        }
        spawn_fruit(g);
    }

    g->bonus_timer -= dt;
    if (g->bonus_timer <= 0.0f) { g->bonus_timer = BONUS_PERIOD; spawn_bonus(g); }

    /* --- les restes ----------------------------------------------------- */
    for (uint32_t i = 0; i < g->dead_count; ) {
        g->dead[i].age += dt;
        if (g->dead[i].age > DEATH_ANIM_TIME) g->dead[i] = g->dead[--g->dead_count];
        else ++i;
    }
    for (uint32_t i = 0; i < g->popup_count; ) {
        g->popup[i].age += dt;
        if (g->popup[i].age > 0.8f) g->popup[i] = g->popup[--g->popup_count];
        else ++i;
    }

    /* --- l'affichage qui suit ------------------------------------------- */
    const float target = (float)g->score;
    g->score_shown += (target - g->score_shown) * ns_clampf(dt * 6.0f, 0.0f, 1.0f);
    g->gauge = ns_clampf(g->fruits_eaten / APPEAR_MAX, 0.0f, 1.0f);

    if (g->score > (int64_t)g->best) g->best = (uint32_t)g->score;
}

/* ==========================================================================
 * Le joueur automatique
 * ========================================================================== */

bool snake_autopilot(snake *g)
{
    if (g->phase == SNAKE_DEAD) return false;

    const snake_part *h = &g->part[SNAKE_PRE];

    /* La cible : le fruit qui rapporte le plus par unité de distance. En
     * hardcore manger coûte, donc il ne vise QUE les bonus — c'est le seul
     * comportement qui fasse monter le score dans ce mode. */
    float best_x = SNAKE_PLAY_W * 0.5f, best_y = SNAKE_PLAY_H * 0.5f;
    float best_gain = -1.0f;
    /*
     * Il court après les fruits DANS LES DEUX MODES, y compris en hardcore où
     * c'est perdant. Ce n'est pas une étourderie : le rôle de ce pilote est
     * d'exercer le code, pas de bien jouer — et faire courir la MÊME stratégie
     * dans les deux modes est précisément ce qui montre que le hardcore est
     * l'inverse du normal. Un pilote qui éviterait les fruits en hardcore
     * n'exercerait jamais le chemin où manger coûte.
     */
    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) {
        if (g->fruit[i].id < 0) continue;
        const float dx = g->fruit[i].x - h->x, dy = g->fruit[i].y - h->y;
        const float d = sqrtf(dx * dx + dy * dy) + 1.0f;
        const float gain = (FRUIT_PROP[g->fruit[i].id][P_SCORE] + 10.0f) / d;
        if (gain > best_gain) { best_gain = gain; best_x = g->fruit[i].x; best_y = g->fruit[i].y; }
    }

    /* Les murs priment sur la gourmandise : à moins de 150 px d'un bord on vise
     * le centre, sinon on meurt en ligne droite sur un fruit posé au bord. */
    const float margin = 150.0f;
    if (h->x < margin || h->y < margin
        || h->x > SNAKE_PLAY_W - margin || h->y > SNAKE_PLAY_H - margin) {
        best_x = SNAKE_PLAY_W * 0.5f;
        best_y = SNAKE_PLAY_H * 0.5f;
    }

    const float want = atan2f(best_y - h->y, best_x - h->x);
    float delta = want - g->angle;
    while (delta > NS_PI) delta -= 2.0f * NS_PI;
    while (delta < -NS_PI) delta += 2.0f * NS_PI;

    const bool left = delta < -0.02f, right = delta > 0.02f;
    snake_hold(g, left, right, false);
    return left || right;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

typedef struct ctx { ns_sprite *s; float ox, oy, scale; } ctx;

static void blit(const ctx *c, const ns_texture *t, float x, float y, float w, float h,
                 float sx, float sy, float sw, float sh, float tw, float th,
                 const float rgba[4])
{
    if (!t || !t->handle) return;
    ns_sprite_texture(c->s, t);
    ns_sprite_quad(c->s, c->ox + x * c->scale, c->oy + y * c->scale,
                   w * c->scale, h * c->scale,
                   sx / tw, sy / th, (sx + sw) / tw, (sy + sh) / th, rgba);
}

static void draw_number(const ctx *c, const snake_art *a, int64_t value, float cx, float y,
                        float scale)
{
    char buf[24];
    const bool neg = value < 0;
    uint64_t v = (uint64_t)(neg ? -value : value);
    int n = 0;
    do { buf[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < 20);

    const float dw = DIGIT_W * scale, dh = DIGIT_H * scale;
    const float total = dw * (float)(n + (neg ? 1 : 0));
    float x = cx - total * 0.5f;

    if (neg) {
        /* La planche n'a pas de signe moins : un trait le fait très bien. */
        static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ns_sprite_texture(c->s, NULL);
        ns_sprite_rect(c->s, c->ox + (x + dw * 0.2f) * c->scale,
                       c->oy + (y + dh * 0.45f) * c->scale,
                       dw * 0.6f * c->scale, dh * 0.12f * c->scale, white);
        x += dw;
    }
    for (int i = n - 1; i >= 0; --i) {
        const float d = (float)(buf[i] - '0');
        blit(c, &a->digits, x, y, dw, dh, d * DIGIT_W, 0.0f, DIGIT_W, DIGIT_H,
             DIGIT_W * 11.0f, DIGIT_H, NULL);
        x += dw;
    }
}

void snake_draw(ns_sprite *s, const snake *g, const snake_art *a,
                float logical_w, float logical_h)
{
    /* Le terrain garde ses proportions et se centre, comme Flappy. */
    const float sx = logical_w / SNAKE_W, sy = logical_h / SNAKE_H;
    ctx c;
    c.s = s;
    c.scale = (sx < sy) ? sx : sy;
    c.ox = (logical_w - SNAKE_W * c.scale) * 0.5f;
    c.oy = (logical_h - SNAKE_H * c.scale) * 0.5f;

    /* --- le fond --------------------------------------------------------
     *
     * LÉGÈREMENT ASSOMBRI — et l'écart utile est ailleurs, ce qui a été mesuré.
     *
     * La planche de 2020 est un vert vif quasi uniforme ; sur la dalle d'une
     * borne — 512 x 288 — la capture montrait un aplat lumineux sur lequel le
     * serpent, vert plus sombre, ne se détachait pas.
     *
     * Assombrir le FOND seul n'y suffit pas, et le compte le dit : mesurée sur
     * la capture, la luminance passait de 190 à 154 pour le terrain mais de 159
     * à 131 pour le corps — l'écart entre les deux TOMBAIT de 31 à 23. Teinter
     * le décor déplace tout le monde ensemble.
     *
     * Ce qui sépare vraiment, c'est de teinter le SERPENT dans l'autre sens :
     * voir `body_tint` plus bas. Le fond ne descend donc que d'un cran, pour le
     * score et les fruits, et le gros de l'écart est pris sur le corps. */
    static const float field_tint[4] = { 0.72f, 0.78f, 0.72f, 1.0f };
    if (a->ready && a->background.handle) {
        blit(&c, &a->background, 0, 0, SNAKE_W, SNAKE_H, 0, 0, SNAKE_W, SNAKE_H,
             SNAKE_W, SNAKE_H, field_tint);
    } else {
        static const float green[4] = { 0.10f, 0.24f, 0.09f, 1.0f };
        ns_sprite_rect(s, c.ox, c.oy, SNAKE_W * c.scale, SNAKE_H * c.scale, green);
    }

    /* Le terrain est centré dans la fenêtre : (1920−1728)/2 = 96 de marge. */
    const float px = (SNAKE_W - SNAKE_PLAY_W) * 0.5f;
    const float py = (SNAKE_H - SNAKE_PLAY_H) * 0.5f;
    const ctx p = { s, c.ox + px * c.scale, c.oy + py * c.scale, c.scale };

    /* --- les restes de queue -------------------------------------------- */
    for (uint32_t i = 0; i < g->dead_count; ++i) {
        const snake_dead *d = &g->dead[i];
        const int frame = (int)(d->age / DEATH_ANIM_TIME * (float)ANIM_FRAMES);
        const float r = d->radius;
        blit(&p, &a->anim, d->x - r, d->y - r, r * 2.0f, r * 2.0f,
             (float)(frame < ANIM_FRAMES ? frame : ANIM_FRAMES - 1) * CELL, CELL,
             CELL, CELL, CELL * ANIM_FRAMES, CELL * 2.0f, NULL);
    }

    /* --- les fruits ----------------------------------------------------- */
    const float ttl = fruit_ttl(g);
    for (int i = 0; i < SNAKE_MAX_FRUITS; ++i) {
        const snake_fruit *f = &g->fruit[i];
        if (f->id < 0) continue;
        const float r = FRUIT_PROP[f->id][P_RADIUS] * (f->giant ? (GIANT_SIZE / 24.0f) : 1.0f);

        /* Il clignote sur la fin : c'est le seul avertissement qu'on ait. */
        const float left = ttl - f->age;
        const bool blink = (left < 3.0f) && (fmodf(left * 6.0f, 1.0f) < 0.5f);

        if (f->age < SPAWN_ANIM_TIME && a->anim.handle) {
            const int frame = (int)(f->age / SPAWN_ANIM_TIME * (float)ANIM_FRAMES);
            blit(&p, &a->anim, f->x - r, f->y - r, r * 2.0f, r * 2.0f,
                 (float)frame * CELL, 0.0f, CELL, CELL, CELL * ANIM_FRAMES, CELL * 2.0f, NULL);
        }
        blit(&p, &a->fruits, f->x - r, f->y - r, r * 2.0f, r * 2.0f,
             (float)f->id * CELL, blink ? CELL : 0.0f, CELL, CELL,
             CELL * SNAKE_ITEMS, CELL * 2.0f, NULL);
    }
    if (g->bonus.id >= 0) {
        const float r = FRUIT_PROP[g->bonus.id][P_RADIUS];
        blit(&p, &a->fruits, g->bonus.x - r, g->bonus.y - r, r * 2.0f, r * 2.0f,
             (float)g->bonus.id * CELL, 0.0f, CELL, CELL,
             CELL * SNAKE_ITEMS, CELL * 2.0f, NULL);
    }

    /* --- le serpent, de la queue vers la tête ---------------------------- */
    const bool invincible = g->invincible > 0.0f;
    const bool blink_body = invincible && g->invincible < 3.0f
                         && fmodf(g->invincible * 6.0f, 1.0f) < 0.5f;
    const float row = invincible ? (blink_body ? 2.0f : 1.0f) : 0.0f;

    /*
     * LE CORPS EST TEINTÉ FROID, et c'est là qu'on gagne la lisibilité.
     *
     * Le terrain est un vert-jaune vif ; le corps sort de la même famille de
     * verts, et sur la dalle d'une borne les deux se confondent — la capture
     * montrait un serpent qu'il fallait chercher. Un bleu ardoise s'oppose au
     * terrain à la fois en TEINTE et en luminance, sans coûter un seul quad de
     * plus : c'est la même planche, avec une couleur de sommet.
     *
     * On ne teinte pas quand l'invincibilité clignote : ces deux lignes-là de
     * la planche existent précisément pour se voir, et les recolorer effacerait
     * l'avertissement.
     */
    static const float body_tint[4] = { 0.38f, 0.52f, 0.85f, 1.0f };
    const float *body_col = invincible ? NULL : body_tint;

    for (uint32_t i = g->parts - 1; i >= SNAKE_PRE; --i) {
        const snake_part *b = &g->part[i];
        const float r = BODY_RADIUS + b->radius;
        blit(&p, &a->body, b->x - r, b->y - r, r * 2.0f, r * 2.0f,
             0.0f, row * CELL, CELL, CELL, CELL * 2.0f, CELL * 3.0f, body_col);
        if (i == SNAKE_PRE) break;   /* uint32_t : pas de i >= 0 possible */
    }
    /* La tête, sur la seconde colonne de la planche.
     *
     * Elle porte désormais un HALO, parce que la capture en borne posait la
     * question qu'un jeu ne doit jamais poser : « lequel de ces ronds est
     * moi ? ». Corps et tête sortent de la même planche, aux mêmes deux
     * couleurs ; à 512 px de large le serpent est un trait uniforme dont on ne
     * voit pas par quel bout il avance — et c'est le bout qu'on pilote.
     *
     * Un seul quad, dessiné DESSOUS : ça ne coûte rien au lot de sprites et ça
     * ne cache pas le dessin d'origine. */
    {
        const snake_part *h = &g->part[SNAKE_PRE];
        const float r = BODY_RADIUS + h->radius;
        const float halo[4] = { 1.0f, 0.94f, 0.35f, invincible ? 0.85f : 0.60f };
        const float hr = r * 1.45f;
        ns_sprite_texture(s, NULL);
        ns_sprite_rect(s, p.ox + (h->x - hr) * p.scale, p.oy + (h->y - hr) * p.scale,
                       hr * 2.0f * p.scale, hr * 2.0f * p.scale, halo);
        blit(&p, &a->body, h->x - r, h->y - r, r * 2.0f, r * 2.0f,
             CELL, row * CELL, CELL, CELL, CELL * 2.0f, CELL * 3.0f, body_col);
    }

    /* --- les scores qui s'envolent --------------------------------------- */
    for (uint32_t i = 0; i < g->popup_count; ++i) {
        const snake_popup *u = &g->popup[i];
        const float k = u->age / 0.8f;
        const float alpha = 1.0f - k;
        const float col[4] = { u->value < 0 ? 1.0f : 0.85f,
                               u->value < 0 ? 0.35f : 1.0f,
                               u->value < 0 ? 0.25f : 0.55f, alpha };
        char buf[24];
        SDL_snprintf(buf, sizeof buf, "%lld", (long long)u->value);
        const float sc = u->size * 0.14f * p.scale;
        ns_sprite_text(s, p.ox + (u->x - 20.0f) * p.scale,
                       p.oy + (u->y - 30.0f - k * 40.0f) * p.scale, sc, col, buf);
    }

    /* --- l'habillage ----------------------------------------------------- */
    if (a->hud.handle) {
        static const float tint[4] = { 0.54f, 0.32f, 0.18f, 1.0f };   /* HUD_COLOR */
        blit(&c, &a->hud, 0, 0, SNAKE_W, SNAKE_H, 0, 0, SNAKE_W, SNAKE_H,
             SNAKE_W, SNAKE_H, tint);
    }

    /* La jauge de progression, à droite. */
    if (a->basket.handle) {
        const float bw = 62.0f, bh = 828.0f;
        const float x = SNAKE_W - bw - 18.0f, y = (SNAKE_H - bh) * 0.5f;
        const float fill = bh * g->gauge;
        blit(&c, &a->basket, x, y + bh - fill, bw, fill,
             0.0f, bh - fill, bw, fill, bw, bh, NULL);
    }

    /* Le score : les chiffres font 12 x 18 dans la planche, donc 26 x 40 px de
     * repère à l'échelle 2,2 — soit 7 x 11 px sur la dalle 512 x 288 d'une
     * borne, ce que la capture montrait comme une tache. Flappy dessine les
     * MÊMES chiffres à 6,4 et se lit. 4,5 met le score de Snake au-dessus du
     * seuil sans mordre sur le terrain. */
    /*
     * UN CARTOUCHE SOUS LE SCORE.
     *
     * Les chiffres sont blancs, le terrain est un vert clair : sur la dalle
     * d'une borne, du blanc sur du vert à 190 de luminance ne se lit pas. Un
     * fond sombre translucide donne au nombre le contraste que le terrain lui
     * refuse, et il coûte un quad.
     *
     * Et il descend à 96 : à 4,5 les chiffres font 81 px de haut, si bien qu'à
     * 18 ils passaient sous le bord haut de la dalle — la capture les montrait
     * coupés par le cadre du tube.
     */
    {
        const int64_t shown = (int64_t)(g->score_shown
                                        + (g->score_shown < 0 ? -0.5f : 0.5f));
        char tmp[24];
        SDL_snprintf(tmp, sizeof tmp, "%lld", (long long)shown);
        const float dw = DIGIT_W * 4.5f * (float)SDL_strlen(tmp);
        const float pad = 22.0f;
        static const float plate[4] = { 0.04f, 0.10f, 0.03f, 0.62f };
        ns_sprite_texture(s, NULL);
        ns_sprite_rect(s, c.ox + (SNAKE_W * 0.5f - dw * 0.5f - pad) * c.scale,
                       c.oy + (96.0f - pad * 0.5f) * c.scale,
                       (dw + pad * 2.0f) * c.scale,
                       (DIGIT_H * 4.5f + pad) * c.scale, plate);
        draw_number(&c, a, shown, SNAKE_W * 0.5f, 96.0f, 4.5f);
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };

    if (g->phase == SNAKE_READY) {
        const float sc = c.scale * 6.0f;
        const char *msg = g->hard ? "HARDCORE : LAISSE LES FRUITS POURRIR"
                                  : "GAUCHE ET DROITE POUR TOURNER";
        const float w = ns_sprite_text_width(msg, sc);
        ns_sprite_text(s, c.ox + (SNAKE_W * c.scale - w) * 0.5f,
                       c.oy + SNAKE_H * c.scale * 0.78f, sc, white, msg);
    } else if (g->phase == SNAKE_DEAD) {
        const float sc = c.scale * 9.0f;
        const char *msg = "PERDU";
        float w = ns_sprite_text_width(msg, sc);
        ns_sprite_text(s, c.ox + (SNAKE_W * c.scale - w) * 0.5f,
                       c.oy + SNAKE_H * c.scale * 0.34f, sc, amber, msg);

        char best[48];
        SDL_snprintf(best, sizeof best, "MEILLEUR %u", g->best);
        const float sc2 = c.scale * 5.0f;
        w = ns_sprite_text_width(best, sc2);
        ns_sprite_text(s, c.ox + (SNAKE_W * c.scale - w) * 0.5f,
                       c.oy + SNAKE_H * c.scale * 0.46f, sc2, white, best);

        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            w = ns_sprite_text_width(again, sc2);
            ns_sprite_text(s, c.ox + (SNAKE_W * c.scale - w) * 0.5f,
                           c.oy + SNAKE_H * c.scale * 0.56f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool snake_art_load(ns_rhi *r, snake_art *a)
{
    memset(a, 0, sizeof *a);
    const bool ok =
        ns_texture_load(r, &a->background, "games/snake/backgroundSnake.png", true, false) &&
        ns_texture_load(r, &a->hud,        "games/snake/hud.png",             true, false) &&
        ns_texture_load(r, &a->body,       "games/snake/snake.png",           true, false) &&
        ns_texture_load(r, &a->fruits,     "games/snake/fruits.png",          true, false) &&
        ns_texture_load(r, &a->anim,       "games/snake/anim.png",            true, false) &&
        ns_texture_load(r, &a->digits,     "games/snake/chiffre.png",         true, false) &&
        ns_texture_load(r, &a->basket,     "games/snake/basket.png",          true, false);
    a->ready = ok;
    if (!ok) NS_WARN("snake : planches introuvables, le jeu tournera sans images");
    return ok;
}

void snake_art_free(ns_rhi *r, snake_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->background);
    ns_texture_destroy(r, &a->hud);
    ns_texture_destroy(r, &a->body);
    ns_texture_destroy(r, &a->fruits);
    ns_texture_destroy(r, &a->anim);
    ns_texture_destroy(r, &a->digits);
    ns_texture_destroy(r, &a->basket);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void sn_reset(void *g, uint64_t seed, bool hard) { snake_reset((snake *)g, seed, hard); }

static void sn_press(void *g, ns_game_button b)
{
    /* Un appui bref compte quand même : sans ça une tape rapide sur la flèche ne
     * ferait rien, le maintien étant lu au pas suivant. */
    snake *s = (snake *)g;
    if (b == NS_GAME_LEFT)  s->turning_left = true;
    if (b == NS_GAME_RIGHT) s->turning_right = true;
    if (b == NS_GAME_ACTION) s->accelerating = true;
}

static void sn_hold(void *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    snake_hold((snake *)g, held[NS_GAME_LEFT], held[NS_GAME_RIGHT], held[NS_GAME_ACTION]);
}

static void sn_tick(void *g, float dt) { snake_tick((snake *)g, dt); }

static void sn_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    snake_draw(s, (const snake *)g, (const snake_art *)a, w, h);
}

static bool sn_art_load(ns_rhi *r, void *a) { return snake_art_load(r, (snake_art *)a); }
static void sn_art_free(ns_rhi *r, void *a) { snake_art_free(r, (snake_art *)a); }
static bool sn_autopilot(void *g)           { return snake_autopilot((snake *)g); }

/* Le score affiché ne descend pas sous zéro : en hardcore le total interne peut
 * être négatif, et le serveur le ramène à zéro de son côté aussi. */
static uint32_t sn_score(const void *g)
{
    const int64_t v = ((const snake *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t sn_best(const void *g)     { return ((const snake *)g)->best; }
static void     sn_set_best(void *g, uint32_t b) { ((snake *)g)->best = b; }

static bool sn_dead(const void *g, float *dead_time)
{
    const snake *s = (const snake *)g;
    if (dead_time) *dead_time = s->dead_time;
    return s->phase == SNAKE_DEAD;
}

/*
 * Le vocabulaire du serveur pour Snake : un fruit vaut sa valeur (barème
 * proportionnel, valeur signée), un bonus vaut 50, tourner et mourir ne valent
 * rien mais sont limités en fréquence. Ce sont les noms de `rulesTable["snake"]`.
 */
static const char *const sn_kinds[] = { "fruit", "bonus", "turn", "death", NULL };

static void sn_events(void *g, ns_game_events *out)
{
    snake *s = (snake *)g;
    out->blip       = s->turned;
    out->blip_kind  = "turn";
    out->score      = s->ate;
    out->score_kind = s->ate_is_bonus ? "bonus" : "fruit";
    /* Un bonus est à barème FIXE côté serveur : sa valeur ne voyage pas. */
    out->score_value = s->ate_is_bonus ? 0 : s->ate_value;
    out->die        = s->died;

    s->turned = s->ate = s->died = false;
}

const ns_game_api g_snake_api = {
    .id = "snake", .title = "SNAKE", .label = "SNAKE",
    .state_size = sizeof(snake), .art_size = sizeof(snake_art),
    .sound_blip = NULL,                         /* tourner ne fait pas de bruit */
    .sound_score = "games/snake/get.wav",
    .sound_die = "games/snake/gameover.wav",
    .art_load = sn_art_load, .art_free = sn_art_free,
    .reset = sn_reset, .press = sn_press, .hold = sn_hold,
    .tick = sn_tick, .draw = sn_draw, .autopilot = sn_autopilot,
    .event_kinds = sn_kinds,
    .score = sn_score, .best = sn_best, .set_best = sn_set_best,
    .dead = sn_dead, .events = sn_events,
};
