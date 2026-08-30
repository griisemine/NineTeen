/* asteroid.c — voir asteroid.h pour ce qui est repris de 2020 et ce qui ne l'est pas. */
#include "asteroid.h"

#include "ns_core.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Les constantes de 2020, converties
 * ==========================================================================
 * Tout est écrit en IMAGES à 30 Hz dans l'original. Chaque valeur garde ici son
 * nom et sa valeur d'origine, avec le facteur de conversion à côté : c'est ce
 * qui permet de vérifier le portage ligne à ligne contre `config.h`, et ce qui
 * fait que le jeu se comporte pareil quel que soit le pas du moteur.
 * ========================================================================== */

#define FPS30 30.0f

/* --- vaisseau ------------------------------------------------------------ */
#define RAYON_VAISS       25.0f
#define TURN_AMMOUNT      0.13f              /* rad/image  -> 3,9 rad/s        */
#define TURN_RATE         (TURN_AMMOUNT * FPS30)
#define VITESSE           10.0f              /* px/image   -> 300 px/s         */
#define SPEED_MAX         (VITESSE * FPS30)
#define ACCEL             0.75f              /* px/image²                      */
#define ACCEL_RATE        (ACCEL * FPS30 * FPS30)
/* `DECELERATION 1.015` est une DIVISION par image : v /= 1,015 chaque image.
 * En continu, c'est une décroissance exponentielle de taux ln(1,015) par image,
 * donc 30 ln(1,015) par seconde. */
#define DECEL_PER_SECOND  (30.0f * 0.0148886f)
#define BASE_ANGLE        (3.0f * NS_PI / 2.0f)

/* Les deux rampes : la rotation atteint son plein régime en 9 images, la
 * poussée en 5. C'est ce qui donne au vaisseau son inertie de commande — sans
 * elles il tourne comme un curseur. */
#define NB_FRAME_TURN     9
#define NB_FRAME_THRUST   5
static const float RATIO_TURN[NB_FRAME_TURN]      = { 0.0f, 0.1f, 0.2f, 0.3f, 0.6f, 0.8f, 1.0f, 1.0f, 1.0f };
static const float RATIO_ACCEL[NB_FRAME_THRUST + 1] = { 0.0f, 0.3f, 0.6f, 1.0f, 1.0f, 1.0f };

static float ramp(const float *table, int count, float held_seconds)
{
    const float f = held_seconds * FPS30;
    if (f <= 0.0f) return table[0];
    const int i = (int)f;
    if (i >= count - 1) return table[count - 1];
    const float t = f - (float)i;
    return table[i] + (table[i + 1] - table[i]) * t;
}

/* --- astéroïdes ---------------------------------------------------------- */
#define NB_ASTE_TEXTURES        6
static const int SCORE_ASTEROID[NB_ASTE_TEXTURES] = { 50, 100, 200, 300, 400, 500 };
#define NB_TRANCHE_TAILLE       4
static const float MULTI_TAILLE_SCORE[NB_TRANCHE_TAILLE] = { 0.2f, 0.4f, 0.8f, 1.0f };

#define MAX_ASTEROID_SIZE       90.0f
#define TAILLE_MIN_SPLIT        36.0f
#define TAILLE_MIN_ASTEROID     18.0f
#define VITESSE_MAX_ASTEROID    22.0f        /* px/image */
#define VITESSE_MAX_HARDCORE    30.0f
#define VITESSE_MIN_ASTEROID     9.0f
#define PV_BASE                  1.2f
#define DIST_VAISSEAU_ASTEROID 300.0f

#define VITESSE_SPAWN_INIT      12.0f        /* secondes entre deux apparitions */
#define VITESSE_SPAWN_MIN        6.5f

#define START_DIFFICULTE         1.01f
#define RATIO_DIFFICULTE_AUGMENT 0.0027f     /* par image -> par seconde x30 */
#define MAX_DIFF                40.0f

/* --- missiles ------------------------------------------------------------ */
#define DISTANCE_CANON          23.0f
#define FREQUENCE_BASE          (0.5f)       /* FRAMES_PER_SECOND/2 -> 0,5 s */
static const float FREQUENCE_MISSILES[AST_SHOT_COUNT] = { 1.0f, 0.66f, 1.6f, 1.33f, 0.25f };
#define BASE_VITESSE_MISSILE    (15.0f * FPS30)
static const float VITESSE_MISSILES[AST_SHOT_COUNT]  = { 1.2f, 1.4f, 1.0f, 1.3f, 2.0f };
#define BASE_DEGAT_MISSILE      1.5f
static const float DEGAT_MISSILES[AST_SHOT_COUNT]    = { 1.3f, 1.6f, 3.6f, 0.0f, 12.0f / 30.0f };
static const float RAYON_MISSILES[AST_SHOT_COUNT]    = { 6.0f, 10.0f, 14.0f, 10.0f, 8.0f };
#define DUREE_MISSILE_BASE      2.0f         /* 2 * FRAMES_PER_SECOND -> 2 s */
static const float DUREE_MISSILES[AST_SHOT_COUNT]    = { 0.9f, 0.7f, 1.6f, 1.0f, 0.6f };
static const float MUNITIONS_USAGE[AST_SHOT_COUNT]   = { 0.0f, 0.01f, 0.0334f, 0.02f, 1.0f / 14.0f };

#define BASE_ZIGZAG_ANGLE       0.1f
#define ANGLE_TELEGUIDE         0.1f
#define CHAMP_VISION_TELEGUIDE  (NS_PI / 2.0f)
#define GLACE_GEL               2.0f         /* secondes de gel par impact */

/* --- bonus --------------------------------------------------------------- */
enum {
    B_TIR_MULTIPLE = 0, B_BOUCLIER, B_VITESSE_DE_TIR, B_VITESSE_MISSILE, B_DEGAT,
    B_BOMBE, B_POINT_PETIT, B_POINT_MOYEN, B_POINT_GRAND,
    B_MUN_ZIGZAG, B_MUN_HOMING, B_MUN_GLACE, B_MUN_LASER,
    B_COUNT
};
static const int CHANCE_BONUS[B_COUNT] = { 1, 2, 1, 3, 4, 1, 10, 6, 2, 4, 4, 4, 4 };
static const int BONUS_POINT[3] = { 500, 1500, 5000 };
#define NB_TIR_MAX              3
static const float ANGLE_TIR_MULTIPLE[NB_TIR_MAX][NB_TIR_MAX] = {
    { 0.0f, 0.0f, 0.0f },
    { -NS_PI / 25.0f, NS_PI / 25.0f, 0.0f },
    { -NS_PI / 15.0f, 0.0f, NS_PI / 15.0f },
};
#define BONUS_ACCELERATION_MISSILE 1.7f
#define BONUS_FREQUENCE_MISSILE    1.4f
#define FREQUENCE_MISSILE_MIN      (1.0f / 6.0f)
#define DEGAT_MISSILE_MAX          6.0f
#define DEGAT_ADD                  0.75f
#define BOUCLIER_DUREE             8.0f
#define AMMO_GRANT                 (1.0f / 3.0f)
#define MAX_RATIO_AMMO             0.7f
#define CHANCE_SPAWN_PICKUP        8.0f      /* une chance par 8 s */
#define DIST_WALL_PICKUP         140.0f
#define DIST_VAISS_PICKUP         70.0f

/* ==========================================================================
 * Aides
 * ========================================================================== */

static float frandf(ns_rng *r, float lo, float hi)
{
    return lo + (hi - lo) * (float)ns_rng_below(r, 10000) / 10000.0f;
}

static float wrap_angle(float a)
{
    while (a > NS_PI)  a -= 2.0f * NS_PI;
    while (a < -NS_PI) a += 2.0f * NS_PI;
    return a;
}

/* Le score d'un astéroïde : sa variété donne la base, sa taille le quartier.
 * C'est la formule de 2020, et c'est elle qui fait qu'un petit fragment d'une
 * variété rare vaut plus qu'un gros caillou commun. */
int64_t asteroid_rock_score(const ast_rock *r)
{
    const float d = r->radius * 2.0f;
    int slice = (int)(d / MAX_ASTEROID_SIZE * (float)NB_TRANCHE_TAILLE);
    if (slice < 0) slice = 0;
    if (slice >= NB_TRANCHE_TAILLE) slice = NB_TRANCHE_TAILLE - 1;
    return (int64_t)((float)SCORE_ASTEROID[r->kind] * MULTI_TAILLE_SCORE[slice] + 0.5f);
}

int asteroid_live_rocks(const asteroid *g)
{
    int n = 0;
    for (int i = 0; i < AST_MAX_ROCKS; ++i) if (g->rock[i].alive) n++;
    return n;
}

static ast_rock *free_rock(asteroid *g)
{
    for (int i = 0; i < AST_MAX_ROCKS; ++i) if (!g->rock[i].alive) return &g->rock[i];
    return NULL;
}

static ast_shot_t *free_shot(asteroid *g)
{
    for (int i = 0; i < AST_MAX_SHOTS; ++i) if (!g->shot[i].alive) return &g->shot[i];
    return NULL;
}

/* ==========================================================================
 * Les astéroïdes
 * ========================================================================== */

/*
 * Une apparition entre par un BORD, jamais au milieu, et jamais à moins de
 * trois cents pixels du vaisseau : `DIST_VAISSEAU_ASTEROID`. C'est la règle qui
 * empêche la mort qu'on ne pouvait pas voir venir, et c'est la première chose
 * qu'on casse en simplifiant.
 */
static void spawn_rock(asteroid *g)
{
    ast_rock *r = free_rock(g);
    if (!r) return;

    const float diff = g->difficulty;
    const float size_max = MAX_ASTEROID_SIZE;
    float d = frandf(&g->rng, TAILLE_MIN_ASTEROID * 2.0f, size_max);
    if (d > size_max) d = size_max;

    const float vmax = (g->hard ? VITESSE_MAX_HARDCORE : VITESSE_MAX_ASTEROID) * FPS30;
    const float vmin = VITESSE_MIN_ASTEROID * FPS30;
    float speed = frandf(&g->rng, vmin, vmin + (vmax - vmin) * (diff / MAX_DIFF));
    if (speed > vmax) speed = vmax;

    for (int attempt = 0; attempt < 12; ++attempt) {
        const int side = (int)ns_rng_below(&g->rng, 4);
        float x = 0.0f, y = 0.0f;
        switch (side) {
            case 0: x = -d;         y = frandf(&g->rng, 0.0f, AST_H); break;
            case 1: x = AST_W + d;  y = frandf(&g->rng, 0.0f, AST_H); break;
            case 2: x = frandf(&g->rng, 0.0f, AST_W); y = -d;         break;
            default:x = frandf(&g->rng, 0.0f, AST_W); y = AST_H + d;  break;
        }
        const float dx = g->ship_x - x, dy = g->ship_y - y;
        if (dx * dx + dy * dy < DIST_VAISSEAU_ASTEROID * DIST_VAISSEAU_ASTEROID) continue;

        /* Elle vise le terrain, avec un écart : viser le vaisseau serait
         * injouable, viser au hasard la ferait souvent repartir aussitôt. */
        const float tx = frandf(&g->rng, AST_W * 0.25f, AST_W * 0.75f);
        const float ty = frandf(&g->rng, AST_H * 0.25f, AST_H * 0.75f);
        const float len = sqrtf((tx - x) * (tx - x) + (ty - y) * (ty - y));

        SDL_zerop(r);
        r->x = x; r->y = y;
        r->vx = (tx - x) / (len > 1e-3f ? len : 1.0f) * speed;
        r->vy = (ty - y) / (len > 1e-3f ? len : 1.0f) * speed;
        r->radius = d * 0.5f;
        r->hp = PV_BASE * (d / MAX_ASTEROID_SIZE) * diff;
        if (r->hp < 0.3f) r->hp = 0.3f;
        r->kind = (int)ns_rng_below(&g->rng, NB_ASTE_TEXTURES);
        r->spin = frandf(&g->rng, -3.0f, 3.0f);
        r->alive = true;
        return;
    }
}

/*
 * La fragmentation. Au-dessus de `TAILLE_MIN_SPLIT`, un astéroïde détruit en
 * donne deux plus petits qui partent de part et d'autre — c'est ce qui fait que
 * détruire n'est pas toujours la bonne idée.
 */
static void split_rock(asteroid *g, const ast_rock *src)
{
    if (src->radius * 2.0f < TAILLE_MIN_SPLIT) return;
    const float d = src->radius;   /* chaque moitié fait la moitié du diamètre */
    if (d < TAILLE_MIN_ASTEROID) return;

    const float base = atan2f(src->vy, src->vx);
    const float speed = sqrtf(src->vx * src->vx + src->vy * src->vy);
    for (int k = -1; k <= 1; k += 2) {
        ast_rock *r = free_rock(g);
        if (!r) return;
        const float a = base + (float)k * frandf(&g->rng, 0.35f, 0.8f);
        SDL_zerop(r);
        r->x = src->x; r->y = src->y;
        r->vx = cosf(a) * speed * 1.1f;
        r->vy = sinf(a) * speed * 1.1f;
        r->radius = d * 0.5f;
        r->hp = src->hp * 0.5f;
        if (r->hp < 0.2f) r->hp = 0.2f;
        r->kind = src->kind;
        r->spin = frandf(&g->rng, -4.0f, 4.0f);
        r->alive = true;
    }
}

static void kill_rock(asteroid *g, ast_rock *r)
{
    const int64_t pts = asteroid_rock_score(r);
    g->score += pts;
    g->pend_rock += pts;
    g->rocks_killed++;
    r->alive = false;
    split_rock(g, r);
}

/* ==========================================================================
 * Les bonus
 * ========================================================================== */

static int roll_bonus(asteroid *g)
{
    int total = 0;
    for (int i = 0; i < B_COUNT; ++i) total += CHANCE_BONUS[i];
    int pick = (int)ns_rng_below(&g->rng, (uint32_t)total);
    for (int i = 0; i < B_COUNT; ++i) {
        pick -= CHANCE_BONUS[i];
        if (pick < 0) return i;
    }
    return B_POINT_PETIT;
}

static void grant_bonus(asteroid *g, int b)
{
    switch (b) {
        case B_TIR_MULTIPLE:
            if (g->multi < NB_TIR_MAX) g->multi++;
            break;
        case B_BOUCLIER:
            g->shield = BOUCLIER_DUREE;
            break;
        case B_VITESSE_DE_TIR:
            g->fire_period /= BONUS_FREQUENCE_MISSILE;
            if (g->fire_period < FREQUENCE_MISSILE_MIN) g->fire_period = FREQUENCE_MISSILE_MIN;
            break;
        case B_VITESSE_MISSILE:
            g->missile_speed *= BONUS_ACCELERATION_MISSILE;
            if (g->missile_speed > 2.0f) g->missile_speed = 2.0f;
            break;
        case B_DEGAT:
            g->damage_bonus += DEGAT_ADD;
            if (g->damage_bonus > DEGAT_MISSILE_MAX) g->damage_bonus = DEGAT_MISSILE_MAX;
            break;
        case B_BOMBE:
            g->nukes++;
            break;
        case B_POINT_PETIT:
        case B_POINT_MOYEN:
        case B_POINT_GRAND: {
            const int64_t pts = BONUS_POINT[b - B_POINT_PETIT];
            g->score += pts;
            g->pend_bonus += pts;
            break;
        }
        default: {
            /* Les quatre munitions spéciales : elles CHANGENT l'arme et
             * remplissent la jauge. `MAX_RATIO_AMMO` empêche d'en accumuler
             * indéfiniment — c'est ce qui garde le tir normal utile. */
            const int kind = AST_SHOT_ZIGZAG + (b - B_MUN_ZIGZAG);
            g->shot_kind = kind;
            g->ammo += AMMO_GRANT;
            if (g->ammo > MAX_RATIO_AMMO + AMMO_GRANT) g->ammo = MAX_RATIO_AMMO + AMMO_GRANT;
            g->fire_period = FREQUENCE_BASE / FREQUENCE_MISSILES[kind];
            break;
        }
    }
}

static void spawn_pickup(asteroid *g)
{
    for (int i = 0; i < AST_MAX_PICKUPS; ++i) {
        if (g->pickup[i].alive) continue;
        for (int attempt = 0; attempt < 8; ++attempt) {
            const float x = frandf(&g->rng, DIST_WALL_PICKUP, AST_W - DIST_WALL_PICKUP);
            const float y = frandf(&g->rng, DIST_WALL_PICKUP, AST_H - DIST_WALL_PICKUP);
            const float dx = x - g->ship_x, dy = y - g->ship_y;
            if (dx * dx + dy * dy < DIST_VAISS_PICKUP * DIST_VAISS_PICKUP) continue;
            g->pickup[i].x = x;
            g->pickup[i].y = y;
            g->pickup[i].bonus = roll_bonus(g);
            g->pickup[i].life = 12.0f;
            g->pickup[i].alive = true;
            return;
        }
        return;
    }
}

/* ==========================================================================
 * Le tir
 * ========================================================================== */

static void fire_one(asteroid *g, float angle_offset)
{
    ast_shot_t *s = free_shot(g);
    if (!s) return;
    const int k = g->shot_kind;
    const float a = g->ship_angle + angle_offset;

    SDL_zerop(s);
    s->x = g->ship_x + cosf(a) * DISTANCE_CANON;
    s->y = g->ship_y + sinf(a) * DISTANCE_CANON;
    s->angle = a;
    s->target_angle = a;
    const float speed = BASE_VITESSE_MISSILE * VITESSE_MISSILES[k] * g->missile_speed;
    s->vx = cosf(a) * speed;
    s->vy = sinf(a) * speed;
    s->life = DUREE_MISSILE_BASE * DUREE_MISSILES[k];
    s->damage = BASE_DEGAT_MISSILE * DEGAT_MISSILES[k] + g->damage_bonus;
    s->radius = RAYON_MISSILES[k];
    s->kind = k;
    s->alive = true;
}

static void fire(asteroid *g)
{
    const int k = g->shot_kind;
    /* La munition spéciale se consomme ; le tir normal est gratuit et
     * inépuisable — `MUNITIONS_USAGE[0]` vaut zéro, et c'est délibéré : sans
     * arme de repli, une jauge vide serait une mort annoncée. */
    if (k != AST_SHOT_NORMAL) {
        const float cost = MUNITIONS_USAGE[k];
        if (g->ammo < cost) {
            g->shot_kind = AST_SHOT_NORMAL;
            g->fire_period = FREQUENCE_BASE / FREQUENCE_MISSILES[AST_SHOT_NORMAL];
            return;
        }
        g->ammo -= cost;
    }
    const int n = (g->multi < 1) ? 1 : ((g->multi > NB_TIR_MAX) ? NB_TIR_MAX : g->multi);
    for (int i = 0; i < n; ++i) fire_one(g, ANGLE_TIR_MULTIPLE[n - 1][i]);
    g->fired = true;
}

static void detonate_nuke(asteroid *g)
{
    if (g->nukes <= 0) return;
    g->nukes--;
    for (int i = 0; i < AST_MAX_ROCKS; ++i) {
        if (!g->rock[i].alive) continue;
        const int64_t pts = asteroid_rock_score(&g->rock[i]);
        g->score += pts;
        g->pend_rock += pts;
        g->rocks_killed++;
        g->rock[i].alive = false;   /* la bombe ne fragmente pas : elle efface */
    }
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

void asteroid_reset(asteroid *g, uint64_t seed, bool hard)
{
    memset(g, 0, sizeof *g);
    ns_rng_seed(&g->rng, seed, 0xA57E01u);
    g->hard = hard;
    g->phase = AST_READY;

    g->ship_x = AST_W * 0.5f;
    g->ship_y = AST_H * 0.5f;
    /* Normalisé dans (−π, π] tout de suite : `BASE_ANGLE` vaut 3π/2, et le
     * garder tel quel ferait sauter l'angle de 2π à la première image — un
     * écart qui ne change rien au jeu et beaucoup à qui compare deux angles. */
    g->ship_angle = wrap_angle(BASE_ANGLE);

    g->shot_kind = AST_SHOT_NORMAL;
    g->multi = 1;
    g->fire_period = FREQUENCE_BASE / FREQUENCE_MISSILES[AST_SHOT_NORMAL];
    g->missile_speed = 1.0f;
    g->difficulty = START_DIFFICULTE;
    g->spawn_period = VITESSE_SPAWN_INIT;
    g->spawn_timer = 1.0f;
    g->pickup_timer = CHANCE_SPAWN_PICKUP;

    /*
     * Un champ de DÉPART, comme `FRAME_INIT_SPAWN` et les trois `coord_spawn`
     * de 2020. Sans lui, la première apparition tombe à la douzième seconde et
     * le joueur passe un quart de minute devant un écran vide en se demandant
     * si le jeu a démarré.
     */
    for (int i = 0; i < 3; ++i) spawn_rock(g);
}

void asteroid_press(asteroid *g, ns_game_button b)
{
    if (g->phase == AST_DEAD) return;
    if (g->phase == AST_READY) g->phase = AST_PLAYING;
    if (b == NS_GAME_ACTION) {
        /*
         * Le bouton TIRE, et déclenche la bombe s'il n'y a plus rien à tirer.
         * Une borne n'a qu'un bouton d'action : la bombe de 2020 était sur une
         * seconde touche qui n'existe pas sur le panneau. Elle part donc quand
         * on tire alors que la jauge est vide — c'est le moment où l'on en a
         * besoin, et c'est la seule adaptation faite au support.
         */
        if (g->fire_timer <= 0.0f) {
            fire(g);
            g->fire_timer = g->fire_period;
        }
    }
}

void asteroid_hold(asteroid *g, const bool held[NS_GAME_BUTTON_COUNT])
{
    for (int i = 0; i < NS_GAME_BUTTON_COUNT; ++i) g->held[i] = held[i];
}

static void bounce_ship(asteroid *g)
{
    /* Le vaisseau REBONDIT, il ne s'enroule pas : c'est la règle de 2020, et
     * c'est ce qui fait qu'on sait toujours où il est. */
    if (g->ship_x < RAYON_VAISS)          { g->ship_x = RAYON_VAISS;          g->ship_vx = -g->ship_vx * 0.6f; }
    if (g->ship_x > AST_W - RAYON_VAISS)  { g->ship_x = AST_W - RAYON_VAISS;  g->ship_vx = -g->ship_vx * 0.6f; }
    if (g->ship_y < RAYON_VAISS)          { g->ship_y = RAYON_VAISS;          g->ship_vy = -g->ship_vy * 0.6f; }
    if (g->ship_y > AST_H - RAYON_VAISS)  { g->ship_y = AST_H - RAYON_VAISS;  g->ship_vy = -g->ship_vy * 0.6f; }
}

static void die(asteroid *g)
{
    g->phase = AST_DEAD;
    g->dead_time = 0.0f;
    g->died = true;
}

void asteroid_tick(asteroid *g, float dt)
{
    if (g->phase == AST_DEAD) { g->dead_time += dt; return; }
    g->time += dt;
    if (g->phase == AST_READY) return;

    /* --- la difficulté monte, et avec elle la cadence d'apparition --- */
    g->difficulty += RATIO_DIFFICULTE_AUGMENT * FPS30 * dt;
    if (g->difficulty > MAX_DIFF) g->difficulty = MAX_DIFF;
    if (g->shield > 0.0f) g->shield -= dt;
    if (g->fire_timer > 0.0f) g->fire_timer -= dt;

    /* --- le vaisseau --- */
    const bool left = g->held[NS_GAME_LEFT], right = g->held[NS_GAME_RIGHT];
    const int dir = (left && !right) ? -1 : ((right && !left) ? +1 : 0);
    if (dir != 0 && dir == g->turn_dir) g->turn_ramp += dt;
    else { g->turn_ramp = (dir != 0) ? dt : 0.0f; g->turn_dir = dir; }
    if (dir != 0) {
        g->ship_angle += (float)dir * TURN_RATE * ramp(RATIO_TURN, NB_FRAME_TURN, g->turn_ramp) * dt;
        g->ship_angle = wrap_angle(g->ship_angle);
    }

    const bool thrust = g->held[NS_GAME_UP];
    if (thrust) g->thrust_ramp += dt; else g->thrust_ramp = 0.0f;
    if (thrust) {
        const float a = ACCEL_RATE * ramp(RATIO_ACCEL, NB_FRAME_THRUST + 1, g->thrust_ramp);
        g->ship_vx += cosf(g->ship_angle) * a * dt;
        g->ship_vy += sinf(g->ship_angle) * a * dt;
    }
    g->thrusting = thrust;

    /* La décélération : v /= 1,015 par image devient une exponentielle. */
    const float damp = expf(-DECEL_PER_SECOND * dt);
    g->ship_vx *= damp;
    g->ship_vy *= damp;

    const float sp = sqrtf(g->ship_vx * g->ship_vx + g->ship_vy * g->ship_vy);
    if (sp > SPEED_MAX) { g->ship_vx *= SPEED_MAX / sp; g->ship_vy *= SPEED_MAX / sp; }

    g->ship_x += g->ship_vx * dt;
    g->ship_y += g->ship_vy * dt;
    bounce_ship(g);

    /* Le tir maintenu : `press` tire une fois, le maintien enchaîne. */
    if (g->held[NS_GAME_ACTION] && g->fire_timer <= 0.0f) {
        fire(g);
        g->fire_timer = g->fire_period;
    }
    /* La bombe part quand on tire vers le bas, faute de second bouton. */
    if (g->held[NS_GAME_DOWN] && g->nukes > 0) detonate_nuke(g);

    /* --- les apparitions --- */
    /*
     * LE CHAMP NE SE VIDE JAMAIS.
     *
     * La cadence de 2020 — une apparition toutes les douze secondes, jusqu'à
     * six et demie — décrit un champ qui se REMPLIT, pas un champ qu'on vide.
     * Elle suppose qu'on n'arrive pas à tout casser. Un joueur correct y
     * arrive, et alors il ne se passe plus rien : mesuré sur cinq graines, le
     * score du pilote automatique passe de 380 à 6 s à 480 à 12 s — cent points
     * en six secondes, contre trois cent quatre-vingts dans les six premières.
     * La capture en borne à dix secondes le montre en une image : un écran
     * noir, un vaisseau, et rien à faire.
     *
     * Six secondes de vide dans un jeu d'arcade, c'est six secondes pendant
     * lesquelles on lâche le manche.
     *
     * La règle ajoutée ne touche PAS à la cadence : tant que le champ est
     * fourni, l'intervalle de 2020 s'applique tel quel. Elle ne fait que
     * l'avancer quand il ne reste presque rien — ce qui n'arrive que si le
     * joueur a fait le travail, et qui le récompense donc par du jeu plutôt
     * que par de l'attente.
     *
     * Elle n'AVANCE qu'une apparition déjà prévue : la condition
     * `spawn_timer <= spawn_period` la retient quand l'appelant a délibérément
     * garé le compteur hors du cycle — ce que fait `test_le_tir_normal_est_
     * inepuisable`, qui vide le champ pour mesurer les MUNITIONS et pas la
     * survie. Une règle de remplissage n'a pas à créer des astéroïdes là où la
     * cadence n'en prévoit aucun.
     */
    if (asteroid_live_rocks(g) < 2
        && g->spawn_timer > 2.0f && g->spawn_timer <= g->spawn_period) {
        g->spawn_timer = 2.0f;
    }

    g->spawn_timer -= dt;
    if (g->spawn_timer <= 0.0f) {
        /*
         * DEUX astéroïdes par apparition, et l'intervalle qui se resserre avec
         * la difficulté.
         *
         * `FRAME_2ASTEROID (FRAMES_PER_SECOND/2)` de 2020 dit qu'une apparition
         * en amène un second une demi-seconde plus tard ; on les pose ensemble,
         * la nuance ne se voit pas et coûte un état de moins. Et
         * `ACCELERATION_SPAWN 0.03` retranché à un compte de 360 images mettrait
         * cinq mille cinq cents apparitions à passer de douze secondes à six et
         * demie — soit dix-huit heures de jeu. L'intention est claire, la
         * constante ne la sert pas : l'intervalle suit donc la difficulté, qui
         * est la grandeur que l'original fait monter avec le temps, et il
         * s'arrête au plancher déclaré.
         */
        spawn_rock(g);
        spawn_rock(g);
        g->spawn_period = VITESSE_SPAWN_INIT / g->difficulty;
        if (g->spawn_period < VITESSE_SPAWN_MIN) g->spawn_period = VITESSE_SPAWN_MIN;
        g->spawn_timer = g->spawn_period;
        /* Une « vague » toutes les vingt apparitions : c'est ce que le serveur
         * compte, et ça donne un repère au joueur. */
        if (g->rocks_killed && (g->rocks_killed / 20u) > g->wave) {
            g->wave = g->rocks_killed / 20u;
            g->pend_wave++;
        }
    }
    g->pickup_timer -= dt;
    if (g->pickup_timer <= 0.0f) {
        spawn_pickup(g);
        g->pickup_timer = CHANCE_SPAWN_PICKUP;
    }

    /* --- les astéroïdes --- */
    for (int i = 0; i < AST_MAX_ROCKS; ++i) {
        ast_rock *r = &g->rock[i];
        if (!r->alive) continue;
        if (r->frozen > 0.0f) { r->frozen -= dt; continue; }
        r->x += r->vx * dt;
        r->y += r->vy * dt;
        r->angle += r->spin * dt;

        /* Ils TRAVERSENT : sorti du terrain avec une marge, l'astéroïde
         * disparaît. Le faire rebondir remplirait l'écran en une minute. */
        const float m = r->radius + 60.0f;
        if (r->x < -m || r->x > AST_W + m || r->y < -m || r->y > AST_H + m) {
            r->alive = false;
            continue;
        }

        /* Collision avec le vaisseau. Le bouclier absorbe, l'astéroïde meurt. */
        const float dx = r->x - g->ship_x, dy = r->y - g->ship_y;
        const float rr = r->radius + RAYON_VAISS;
        if (dx * dx + dy * dy <= rr * rr) {
            if (g->shield > 0.0f) {
                g->shield = 0.0f;
                kill_rock(g, r);
            } else {
                die(g);
                return;
            }
        }
    }

    /* --- les missiles --- */
    for (int i = 0; i < AST_MAX_SHOTS; ++i) {
        ast_shot_t *s = &g->shot[i];
        if (!s->alive) continue;
        s->life -= dt;
        if (s->life <= 0.0f) { s->alive = false; continue; }

        if (s->kind == AST_SHOT_ZIGZAG) {
            /* Le zigzag : l'angle oscille autour de sa direction de départ. */
            const float a = s->target_angle + BASE_ZIGZAG_ANGLE * sinf(s->life * 18.0f);
            const float speed = sqrtf(s->vx * s->vx + s->vy * s->vy);
            s->vx = cosf(a) * speed;
            s->vy = sinf(a) * speed;
        } else if (s->kind == AST_SHOT_HOMING) {
            /* La tête chercheuse : elle vire de `ANGLE_TELEGUIDE` par image vers
             * la cible la plus proche DANS SON CHAMP — un demi-tour instantané
             * ferait d'elle une arme absolue. */
            float best = 1e30f;
            const ast_rock *target = NULL;
            for (int k = 0; k < AST_MAX_ROCKS; ++k) {
                if (!g->rock[k].alive) continue;
                const float dx = g->rock[k].x - s->x, dy = g->rock[k].y - s->y;
                const float d2 = dx * dx + dy * dy;
                if (d2 >= best) continue;
                const float da = fabsf(wrap_angle(atan2f(dy, dx) - s->angle));
                if (da > CHAMP_VISION_TELEGUIDE) continue;
                best = d2;
                target = &g->rock[k];
            }
            if (target) {
                const float want = atan2f(target->y - s->y, target->x - s->x);
                const float da = wrap_angle(want - s->angle);
                const float step = ANGLE_TELEGUIDE * FPS30 * dt;
                s->angle += (da > step) ? step : ((da < -step) ? -step : da);
                const float speed = sqrtf(s->vx * s->vx + s->vy * s->vy);
                s->vx = cosf(s->angle) * speed;
                s->vy = sinf(s->angle) * speed;
            }
        }

        s->x += s->vx * dt;
        s->y += s->vy * dt;
        if (s->x < -40.0f || s->x > AST_W + 40.0f || s->y < -40.0f || s->y > AST_H + 40.0f) {
            s->alive = false;
            continue;
        }

        for (int k = 0; k < AST_MAX_ROCKS; ++k) {
            ast_rock *r = &g->rock[k];
            if (!r->alive) continue;
            const float dx = r->x - s->x, dy = r->y - s->y;
            const float rr = r->radius + s->radius;
            if (dx * dx + dy * dy > rr * rr) continue;

            if (s->kind == AST_SHOT_ICE) {
                /* La glace ne fait aucun dégât — `DEGAT_MISSILES[3]` vaut zéro —
                 * elle GÈLE. Un astéroïde gelé ne bouge plus et se casse au
                 * coup suivant. */
                r->frozen = GLACE_GEL;
            } else {
                r->hp -= s->damage;
                if (r->hp <= 0.0f) kill_rock(g, r);
            }
            if (s->kind != AST_SHOT_LASER) s->alive = false;
            break;
        }
    }

    /* --- les ramassages --- */
    for (int i = 0; i < AST_MAX_PICKUPS; ++i) {
        ast_pickup *p = &g->pickup[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0.0f) { p->alive = false; continue; }
        const float dx = p->x - g->ship_x, dy = p->y - g->ship_y;
        if (dx * dx + dy * dy <= (RAYON_VAISS + 22.0f) * (RAYON_VAISS + 22.0f)) {
            grant_bonus(g, p->bonus);
            p->alive = false;
        }
    }
}

/* ==========================================================================
 * Le joueur automatique
 * ==========================================================================
 * Il vise la menace la plus PRESSANTE — le rapport de la distance au temps
 * d'impact — tire quand il est aligné, et s'écarte quand elle est trop proche.
 * Il ne prouve pas que le jeu est amusant ; il prouve qu'on peut y jouer des
 * milliers de pas sans NaN, que le score monte, et que rien ne fuit.
 * ========================================================================== */

bool asteroid_autopilot(asteroid *g)
{
    if (g->phase == AST_DEAD) return false;
    if (g->phase == AST_READY) g->phase = AST_PLAYING;

    const ast_rock *threat = NULL;
    float best = 1e30f;
    for (int i = 0; i < AST_MAX_ROCKS; ++i) {
        const ast_rock *r = &g->rock[i];
        if (!r->alive) continue;
        const float dx = r->x - g->ship_x, dy = r->y - g->ship_y;
        const float d = sqrtf(dx * dx + dy * dy);
        /* Une pierre qui s'éloigne n'est pas une menace : on pondère par le
         * rapprochement. */
        const float closing = -(dx * r->vx + dy * r->vy) / (d > 1e-3f ? d : 1.0f);
        const float rank = d - closing * 0.35f;
        if (rank < best) { best = rank; threat = r; }
    }

    bool held[NS_GAME_BUTTON_COUNT];
    memset(held, 0, sizeof held);

    if (threat) {
        const float want = atan2f(threat->y - g->ship_y, threat->x - g->ship_x);
        const float da = wrap_angle(want - g->ship_angle);
        if (da > 0.06f) held[NS_GAME_RIGHT] = true;
        else if (da < -0.06f) held[NS_GAME_LEFT] = true;
        else held[NS_GAME_ACTION] = true;

        /* Trop près : on pousse dans l'autre sens plutôt que de rester planté. */
        const float dx = threat->x - g->ship_x, dy = threat->y - g->ship_y;
        if (dx * dx + dy * dy < 260.0f * 260.0f && fabsf(da) > 2.0f) held[NS_GAME_UP] = true;
    }
    /*
     * Il RAMASSE. Sans ça les bonus restaient posés jusqu'à expiration : le
     * vaisseau ne bougeait que pour viser, et une partie automatique n'exerçait
     * jamais ni le tir multiple, ni le bouclier, ni les munitions spéciales —
     * c'est-à-dire la moitié du jeu, jamais parcourue par les tests.
     *
     * Le ramassage passe APRÈS la menace : on ne va pas chercher un bonus dans
     * la trajectoire d'un caillou.
     */
    const ast_pickup *want_pickup = NULL;
    float pick_best = 520.0f * 520.0f;
    for (int i = 0; i < AST_MAX_PICKUPS; ++i) {
        if (!g->pickup[i].alive) continue;
        const float dx = g->pickup[i].x - g->ship_x, dy = g->pickup[i].y - g->ship_y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < pick_best) { pick_best = d2; want_pickup = &g->pickup[i]; }
    }
    const bool safe = !threat || best > 380.0f;
    if (want_pickup && safe) {
        const float want = atan2f(want_pickup->y - g->ship_y, want_pickup->x - g->ship_x);
        const float da = wrap_angle(want - g->ship_angle);
        held[NS_GAME_LEFT] = held[NS_GAME_RIGHT] = false;
        if (da > 0.10f) held[NS_GAME_RIGHT] = true;
        else if (da < -0.10f) held[NS_GAME_LEFT] = true;
        else held[NS_GAME_UP] = true;
    } else if (g->ship_x < 200.0f || g->ship_x > AST_W - 200.0f
            || g->ship_y < 150.0f || g->ship_y > AST_H - 150.0f) {
        /* Recentrage doux : coincé dans un coin, on ne voit rien venir. */
        const float want = atan2f(AST_H * 0.5f - g->ship_y, AST_W * 0.5f - g->ship_x);
        if (fabsf(wrap_angle(want - g->ship_angle)) < 0.5f) held[NS_GAME_UP] = true;
    }
    if (g->nukes > 0 && asteroid_live_rocks(g) >= 10) held[NS_GAME_DOWN] = true;

    asteroid_hold(g, held);
    return true;
}

/* ==========================================================================
 * Le dessin
 * ========================================================================== */

typedef struct ctx { ns_sprite *s; float ox, oy, scale; } ctx;

/*
 * Un disque en bandes horizontales. La couche 2D ne dessine que des rectangles,
 * et un astéroïde carré se verrait — surtout à dix-neuf exemplaires.
 *
 * La demi-largeur d'une bande est `sqrt(r² - dy²)` prise au bord le plus PROCHE
 * du centre : c'est ce qui inscrit les bandes dans le cercle plutôt que de les
 * en faire déborder. Huit bandes suffisent à ce que la silhouette se lise comme
 * ronde ; le premier jet en employait une formule fausse et rendait des carrés.
 */
static void disc(const ctx *c, float x, float y, float r, const float rgba[4])
{
    const int N = 8;
    for (int i = 0; i < N; ++i) {
        const float y0 = y - r + 2.0f * r * (float)i / (float)N;
        const float y1 = y - r + 2.0f * r * (float)(i + 1) / (float)N;
        const float d0 = y0 - y, d1 = y1 - y;
        const float dy = (fabsf(d0) < fabsf(d1)) ? d0 : d1;
        float hw = r * r - dy * dy;
        hw = (hw > 0.0f) ? sqrtf(hw) : 0.0f;
        if (hw <= 0.0f) continue;
        ns_sprite_rect(c->s, c->ox + (x - hw) * c->scale, c->oy + y0 * c->scale,
                       hw * 2.0f * c->scale, (y1 - y0) * c->scale, rgba);
    }
}

static const float ROCK_TINT[NB_ASTE_TEXTURES][4] = {
    { 0.55f, 0.52f, 0.50f, 1.0f },
    { 0.62f, 0.48f, 0.36f, 1.0f },
    { 0.42f, 0.55f, 0.62f, 1.0f },
    { 0.66f, 0.42f, 0.42f, 1.0f },
    { 0.50f, 0.62f, 0.44f, 1.0f },
    { 0.68f, 0.60f, 0.32f, 1.0f },
};

void asteroid_draw(ns_sprite *s, const asteroid *g, const asteroid_art *a,
                   float logical_w, float logical_h)
{
    const float sx = logical_w / AST_LOGICAL_W, sy = logical_h / AST_LOGICAL_H;
    ctx c;
    c.s = s;
    c.scale = (sx < sy) ? sx : sy;
    c.ox = (logical_w - AST_LOGICAL_W * c.scale) * 0.5f;
    c.oy = (logical_h - AST_LOGICAL_H * c.scale) * 0.5f;

    static const float space[4] = { 0.02f, 0.02f, 0.06f, 1.0f };
    ns_sprite_rect(s, c.ox, c.oy, AST_LOGICAL_W * c.scale, AST_LOGICAL_H * c.scale, space);

    /* Le terrain, centré dans la zone logique. */
    const float fx = (AST_LOGICAL_W - AST_W) * 0.5f, fy = (AST_LOGICAL_H - AST_H) * 0.5f;
    ctx f = c;
    f.ox = c.ox + fx * c.scale;
    f.oy = c.oy + fy * c.scale;

    if (a && a->background.handle) {
        static const float dim[4] = { 0.55f, 0.55f, 0.62f, 1.0f };
        ns_sprite_texture(s, &a->background);
        ns_sprite_quad(s, f.ox, f.oy, AST_W * c.scale, AST_H * c.scale, 0, 0, 1, 1, dim);
    }
    static const float edge[4] = { 0.16f, 0.20f, 0.34f, 1.0f };
    ns_sprite_rect(s, f.ox, f.oy, AST_W * c.scale, 3.0f * c.scale, edge);
    ns_sprite_rect(s, f.ox, f.oy + (AST_H - 3.0f) * c.scale, AST_W * c.scale, 3.0f * c.scale, edge);
    ns_sprite_rect(s, f.ox, f.oy, 3.0f * c.scale, AST_H * c.scale, edge);
    ns_sprite_rect(s, f.ox + (AST_W - 3.0f) * c.scale, f.oy, 3.0f * c.scale, AST_H * c.scale, edge);

    for (int i = 0; i < AST_MAX_ROCKS; ++i) {
        const ast_rock *r = &g->rock[i];
        if (!r->alive) continue;
        const float *tint = ROCK_TINT[r->kind];
        float rgba[4] = { tint[0], tint[1], tint[2], 1.0f };
        if (r->frozen > 0.0f) { rgba[0] *= 0.55f; rgba[1] *= 0.85f; rgba[2] = 1.0f; }
        disc(&f, r->x, r->y, r->radius, rgba);
    }

    static const float shot_tint[AST_SHOT_COUNT][4] = {
        { 1.00f, 0.90f, 0.40f, 1.0f },
        { 1.00f, 0.62f, 0.20f, 1.0f },
        { 0.44f, 0.72f, 0.28f, 1.0f },
        { 0.41f, 0.81f, 0.95f, 1.0f },
        { 0.69f, 0.42f, 0.82f, 1.0f },
    };
    for (int i = 0; i < AST_MAX_SHOTS; ++i) {
        const ast_shot_t *sh = &g->shot[i];
        if (!sh->alive) continue;
        disc(&f, sh->x, sh->y, sh->radius, shot_tint[sh->kind]);
    }

    static const float pk[4] = { 0.66f, 0.60f, 1.00f, 1.0f };
    for (int i = 0; i < AST_MAX_PICKUPS; ++i) {
        if (!g->pickup[i].alive) continue;
        const float pulse = 14.0f + 4.0f * sinf(g->time * 7.0f);
        disc(&f, g->pickup[i].x, g->pickup[i].y, pulse, pk);
    }

    /* Le vaisseau : un triangle, dessiné en bandes. Le bouclier l'entoure. */
    if (g->phase != AST_DEAD) {
        if (g->shield > 0.0f) {
            static const float sh[4] = { 0.99f, 1.00f, 0.22f, 0.30f };
            disc(&f, g->ship_x, g->ship_y, RAYON_VAISS + 10.0f, sh);
        }
        /*
         * Le vaisseau : trois quads TOURNÉS. La couche 2D sait tourner depuis
         * ce portage, et il en avait besoin — la pile de bandes horizontales du
         * premier jet donnait une écharde dès que le vaisseau n'était pas
         * aligné sur un axe.
         */
        static const float hull[4] = { 0.86f, 0.92f, 1.00f, 1.0f };
        static const float trim[4] = { 0.42f, 0.58f, 0.86f, 1.0f };
        const float sa_ = g->ship_angle;
        /* le fuselage */
        ns_sprite_rect_rot(s, f.ox + g->ship_x * c.scale, f.oy + g->ship_y * c.scale,
                           RAYON_VAISS * 2.0f * c.scale, RAYON_VAISS * 0.62f * c.scale,
                           sa_, hull);
        /* les deux ailerons, en arrière */
        for (int k = -1; k <= 1; k += 2) {
            const float bx = g->ship_x - cosf(sa_) * RAYON_VAISS * 0.55f
                           - sinf(sa_) * (float)k * RAYON_VAISS * 0.45f;
            const float by = g->ship_y - sinf(sa_) * RAYON_VAISS * 0.55f
                           + cosf(sa_) * (float)k * RAYON_VAISS * 0.45f;
            ns_sprite_rect_rot(s, f.ox + bx * c.scale, f.oy + by * c.scale,
                               RAYON_VAISS * 0.95f * c.scale, RAYON_VAISS * 0.34f * c.scale,
                               sa_ + (float)k * 0.55f, trim);
        }
        /* le nez, plus étroit : c'est lui qui dit où l'on tire */
        ns_sprite_rect_rot(s, f.ox + (g->ship_x + cosf(sa_) * RAYON_VAISS * 0.70f) * c.scale,
                           f.oy + (g->ship_y + sinf(sa_) * RAYON_VAISS * 0.70f) * c.scale,
                           RAYON_VAISS * 0.85f * c.scale, RAYON_VAISS * 0.26f * c.scale,
                           sa_, hull);
        if (g->thrusting) {
            static const float fire_c[4] = { 1.00f, 0.62f, 0.18f, 0.85f };
            disc(&f, g->ship_x - cosf(g->ship_angle) * (RAYON_VAISS + 8.0f),
                 g->ship_y - sinf(g->ship_angle) * (RAYON_VAISS + 8.0f), 9.0f, fire_c);
        }
    }

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float amber[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    char line[64];
    SDL_snprintf(line, sizeof line, "%lld", (long long)g->score);
    ns_sprite_text(s, c.ox + 40.0f * c.scale, c.oy + 24.0f * c.scale, c.scale * 6.0f, white, line);

    static const char *const SHOT_NAME[AST_SHOT_COUNT] = {
        "NORMAL", "ZIGZAG", "CHERCHEUR", "GLACE", "LASER"
    };
    SDL_snprintf(line, sizeof line, "%s x%d", SHOT_NAME[g->shot_kind], g->multi);
    ns_sprite_text(s, c.ox + 40.0f * c.scale, c.oy + 100.0f * c.scale, c.scale * 5.0f, amber, line);
    if (g->shot_kind != AST_SHOT_NORMAL) {
        static const float bar[4] = { 0.41f, 0.81f, 0.95f, 1.0f };
        ns_sprite_rect(s, c.ox + 40.0f * c.scale, c.oy + 148.0f * c.scale,
                       260.0f * g->ammo / MAX_RATIO_AMMO * c.scale, 16.0f * c.scale, bar);
    }
    if (g->nukes > 0) {
        SDL_snprintf(line, sizeof line, "BOMBE x%d BAS", g->nukes);
        ns_sprite_text(s, c.ox + 40.0f * c.scale, c.oy + 176.0f * c.scale,
                       c.scale * 5.0f, amber, line);
    }

    if (g->phase == AST_READY) {
        const char *msg = "MANCHE POUR PILOTER   BOUTON POUR TIRER";
        const float sc = c.scale * 5.0f;
        ns_sprite_text(s, c.ox + (AST_LOGICAL_W * c.scale
                                  - ns_sprite_text_width(msg, sc)) * 0.5f,
                       c.oy + AST_LOGICAL_H * c.scale * 0.90f, sc, white, msg);
    } else if (g->phase == AST_DEAD) {
        static const float veil[4] = { 0.03f, 0.03f, 0.05f, 0.78f };
        ns_sprite_rect(s, c.ox, c.oy + AST_LOGICAL_H * c.scale * 0.33f,
                       AST_LOGICAL_W * c.scale, AST_LOGICAL_H * c.scale * 0.34f, veil);
        const char *msg = "PERDU";
        const float sc = c.scale * 9.0f;
        ns_sprite_text(s, c.ox + (AST_LOGICAL_W * c.scale
                                  - ns_sprite_text_width(msg, sc)) * 0.5f,
                       c.oy + AST_LOGICAL_H * c.scale * 0.40f, sc, amber, msg);
        SDL_snprintf(line, sizeof line, "MEILLEUR %u", g->best);
        const float sc2 = c.scale * 5.0f;
        ns_sprite_text(s, c.ox + (AST_LOGICAL_W * c.scale
                                  - ns_sprite_text_width(line, sc2)) * 0.5f,
                       c.oy + AST_LOGICAL_H * c.scale * 0.52f, sc2, white, line);
        if (g->dead_time > 0.8f) {
            const char *again = "ESPACE POUR REJOUER";
            ns_sprite_text(s, c.ox + (AST_LOGICAL_W * c.scale
                                      - ns_sprite_text_width(again, sc2)) * 0.5f,
                           c.oy + AST_LOGICAL_H * c.scale * 0.60f, sc2, white, again);
        }
    }
}

/* ==========================================================================
 * Les planches
 * ========================================================================== */

bool asteroid_art_load(ns_rhi *r, asteroid_art *a)
{
    memset(a, 0, sizeof *a);
    /* Le fond seul est chargé : les autres planches de 2020 sont des atlas
     * d'animation dont ce portage n'emploie pas les images. Les déclarer sans
     * les lire donnerait une dépendance qui ment. */
    a->ready = ns_texture_load(r, &a->background, "games/asteroid/background.png", true, false);
    if (!a->ready) NS_WARN("asteroid : fond introuvable, le jeu tournera sans image de fond");
    return a->ready;
}

void asteroid_art_free(ns_rhi *r, asteroid_art *a)
{
    if (!a->ready) return;
    ns_texture_destroy(r, &a->background);
    a->ready = false;
}

/* ==========================================================================
 * L'adaptation à `ns_game_api`
 * ========================================================================== */

static void as_reset(void *g, uint64_t seed, bool hard) { asteroid_reset((asteroid *)g, seed, hard); }
static void as_press(void *g, ns_game_button b) { asteroid_press((asteroid *)g, b); }
static void as_hold(void *g, const bool h[NS_GAME_BUTTON_COUNT]) { asteroid_hold((asteroid *)g, h); }
static void as_tick(void *g, float dt) { asteroid_tick((asteroid *)g, dt); }

static void as_draw(ns_sprite *s, const void *g, const void *a, float w, float h)
{
    asteroid_draw(s, (const asteroid *)g, (const asteroid_art *)a, w, h);
}

static bool as_art_load(ns_rhi *r, void *a) { return asteroid_art_load(r, (asteroid_art *)a); }
static void as_art_free(ns_rhi *r, void *a) { asteroid_art_free(r, (asteroid_art *)a); }

/* Le pilote automatique pose des MAINTIENS, pas des appuis : c'est ainsi qu'on
 * pilote un vaisseau à inertie. Il n'a donc pas besoin de cadence — un maintien
 * relu cent vingt fois par seconde reste un maintien. */
static bool as_autopilot(void *g) { return asteroid_autopilot((asteroid *)g); }

static uint32_t as_score(const void *g)
{
    const int64_t v = ((const asteroid *)g)->score;
    return v > 0 ? (uint32_t)v : 0u;
}
static uint32_t as_best(const void *g) { return ((const asteroid *)g)->best; }
static void     as_set_best(void *g, uint32_t b) { ((asteroid *)g)->best = b; }

static bool as_dead(const void *g, float *dead_time)
{
    const asteroid *a = (const asteroid *)g;
    if (dead_time) *dead_time = a->dead_time;
    return a->phase == AST_DEAD;
}

/* Le vocabulaire de `rulesTable["asteroid"]`. « rock » porte les POINTS, pas le
 * compte : le score d'un astéroïde dépend de sa variété ET de son quartier de
 * taille, ce qu'aucun barème fixe ne reconstitue. */
static const char *const as_kinds[] = { "rock", "bonus", "wave", "shot", "death", NULL };

static void as_events(void *g, ns_game_events *out)
{
    asteroid *a = (asteroid *)g;

    out->blip = a->fired;
    out->blip_kind = "shot";
    a->fired = false;

    if (a->pend_rock) {
        out->score = true;
        out->score_kind = "rock";
        out->score_value = a->pend_rock;
        a->pend_rock = 0;
    } else if (a->pend_bonus) {
        out->score = true;
        out->score_kind = "bonus";
        out->score_value = a->pend_bonus;
        a->pend_bonus = 0;
    } else if (a->pend_wave) {
        out->score = true;
        out->score_kind = "wave";
        out->score_value = 0;
        a->pend_wave--;
    }

    /* La fin après la file, comme partout : `finish_run` scelle le journal. */
    if (a->died && !a->pend_rock && !a->pend_bonus && !a->pend_wave) {
        out->die = true;
        a->died = false;
    }
}

const ns_game_api g_asteroid_api = {
    .id = "asteroid", .title = "ASTEROID", .label = "ASTRO",
    .state_size = sizeof(asteroid), .art_size = sizeof(asteroid_art),
    .sound_blip = "games/asteroid/shoot1.wav",
    .sound_score = "games/asteroid/explo.wav",
    .sound_die = "games/asteroid/big_explo.wav",
    .art_load = as_art_load, .art_free = as_art_free,
    .reset = as_reset, .press = as_press, .hold = as_hold,
    .tick = as_tick, .draw = as_draw, .autopilot = as_autopilot,
    .event_kinds = as_kinds,
    .score = as_score, .best = as_best, .set_best = as_set_best,
    .dead = as_dead, .events = as_events,
};
