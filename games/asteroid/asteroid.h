/*
 * asteroid.h — l'Asteroid de 2020, porté.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * Le terrain de **1728 x 972**, le vaisseau et sa physique — rotation de
 * 0,13 rad par image montée par une **rampe de neuf images**, poussée de 0,75
 * montée par une **rampe de cinq**, décélération de 1,015 par image, vitesse
 * plafonnée à 10 — les **six variétés d'astéroïdes** valant de 50 à 500 points,
 * leur **découpage en quartiers de taille** qui multiplie ce score par 0,2 à 1,
 * la **fragmentation** au-dessus de 36 px, la cadence d'apparition qui passe de
 * douze secondes à six et demie, et les **cinq types de missiles** avec leurs
 * tables de fréquence, de vitesse, de dégâts, de rayon et de durée.
 *
 * L'enveloppe du terrain
 * ----------------------
 * Les astéroïdes entrent par les bords et **traversent** ; le vaisseau, lui,
 * **rebondit** sur les murs. C'est ce que fait l'original, et ce n'est pas ce
 * qu'on attend d'un Asteroids classique — où tout s'enroule. On garde la règle
 * de 2020 : c'est elle qui rend le terrain lisible, parce qu'on sait toujours
 * où est son vaisseau.
 *
 * Ce qui n'est PAS porté, et c'est dit
 * ------------------------------------
 * Les **animations d'explosion** (trois planches, jusqu'à dix images chacune),
 * la **roue de munitions** avec ses treize images de rotation, et la **bombe
 * nucléaire** avec sa grille d'animation 4 x 4. Ce sont des centaines de lignes
 * d'affichage pour une mécanique déjà présente : le bonus « bombe » détruit bien
 * tout à l'écran, il ne le fait simplement pas en seize images.
 */
#ifndef NS_ASTEROID_H
#define NS_ASTEROID_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

/* PLAYGROUND_SIZE_W / H de `legacy/include/define.h`. */
#define AST_W 1728.0f
#define AST_H 972.0f

#define AST_LOGICAL_W 1920.0f
#define AST_LOGICAL_H 1080.0f

#define AST_MAX_ROCKS   64
#define AST_MAX_SHOTS   96
#define AST_MAX_PICKUPS 8

/* NB_MISSILES et l'énumération `shots` de config.h. */
typedef enum ast_shot {
    AST_SHOT_NORMAL = 0, AST_SHOT_ZIGZAG, AST_SHOT_HOMING, AST_SHOT_ICE, AST_SHOT_LASER,
    AST_SHOT_COUNT
} ast_shot;

typedef enum ast_phase { AST_READY = 0, AST_PLAYING, AST_DEAD } ast_phase;

typedef struct ast_rock {
    float x, y, vx, vy;
    float radius;        /* px */
    float hp;
    float spin, angle;
    int   kind;          /* 0..5 : la variété, donc le score de base */
    float frozen;        /* secondes de gel restantes */
    bool  alive;
} ast_rock;

typedef struct ast_shot_t {
    float x, y, vx, vy;
    float angle, target_angle;
    float life;          /* secondes restantes */
    float damage;
    float radius;
    int   kind;
    bool  alive;
} ast_shot_t;

typedef struct ast_pickup {
    float x, y;
    int   bonus;         /* `bonus_e` de config.h, plus les quatre munitions */
    float life;
    bool  alive;
} ast_pickup;

typedef struct asteroid {
    ast_phase phase;

    /* --- le vaisseau --- */
    float ship_x, ship_y, ship_vx, ship_vy;
    float ship_angle;
    float turn_ramp;         /* la rampe de RATIO_TURN, en secondes tenues */
    float thrust_ramp;       /* la rampe de RATIO_ACCEL */
    int   turn_dir;          /* -1, 0, +1 */
    bool  thrusting;
    float shield;            /* secondes de bouclier restantes */

    /* --- l'armement --- */
    int   shot_kind;
    int   multi;             /* 1..3 : NB_TIR_MAX */
    float fire_period;       /* secondes entre deux tirs */
    float fire_timer;
    float missile_speed;     /* facteur, plafonné à 2 */
    float damage_bonus;
    float ammo;              /* 0..1 : la jauge de munitions spéciales */
    int   nukes;

    /* --- le champ --- */
    ast_rock   rock[AST_MAX_ROCKS];
    ast_shot_t shot[AST_MAX_SHOTS];
    ast_pickup pickup[AST_MAX_PICKUPS];

    float spawn_timer, spawn_period;
    float pickup_timer;
    float difficulty;        /* START_DIFFICULTE, monte avec le temps */
    uint32_t wave;

    int64_t  score;
    uint32_t best;
    uint32_t rocks_killed;
    bool     hard;

    float time;
    float dead_time;
    bool  held[NS_GAME_BUTTON_COUNT];

    ns_rng rng;

    /* Événements, CONSOMMÉS par `asteroid_events`. */
    bool     fired, died;
    int64_t  pend_rock;      /* points d'astéroïdes, pas encore journalisés */
    int64_t  pend_bonus;     /* points de bonus ramassés */
    uint32_t pend_wave;
} asteroid;

typedef struct asteroid_art {
    ns_texture ship, rocks, shots, pickup, background;
    bool ready;
} asteroid_art;

bool asteroid_art_load(ns_rhi *r, asteroid_art *a);
void asteroid_art_free(ns_rhi *r, asteroid_art *a);

void asteroid_reset(asteroid *g, uint64_t seed, bool hard);
void asteroid_press(asteroid *g, ns_game_button b);
void asteroid_hold(asteroid *g, const bool held[NS_GAME_BUTTON_COUNT]);
void asteroid_tick(asteroid *g, float dt);
void asteroid_draw(ns_sprite *s, const asteroid *g, const asteroid_art *a,
                   float logical_w, float logical_h);
bool asteroid_autopilot(asteroid *g);

/* Exposés pour les tests : ce sont les règles. */
int64_t asteroid_rock_score(const ast_rock *r);
int     asteroid_live_rocks(const asteroid *g);

extern const ns_game_api g_asteroid_api;

#endif /* NS_ASTEROID_H */
