/*
 * shooter.h — le Shooter de 2020, porté.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * Le terrain ÉTROIT — un tiers de la largeur de l'écran, `RATIO_WIDTH_PLAYGROUND`
 * — le vaisseau à `SHIP_SPEED 10` px/image avec son amortissement de 0,9, les
 * **cinq emplacements d'armes** et la table `WEAPON_DISPOSITION` qui décide
 * lesquels s'allument selon le nombre d'armes, les **trois missiles alliés**
 * (droit, zigzag, à tête chercheuse), et les **cinq types d'ennemis** avec leurs
 * points de vie `{1, 7, 25, 40, 90}`, leurs vitesses `{5,3 ; 6,3 ; 5 ; 1 ; 0}`
 * et leurs tables de tir : rechargement, rafale, et le type de visée —
 * `AIMED`, `STRAIGHT` ou `VERTICAL`.
 *
 * Le barème, et pourquoi il n'a pas bougé
 * ---------------------------------------
 * `rulesTable["shooter"]` déclare `scaled{"enemy": 15}` depuis M6 : quinze
 * points par unité de valeur. La valeur d'un ennemi, c'est ses POINTS DE VIE —
 * un ennemi à 90 points de vie vaut 1 350, un ennemi à 1 en vaut 15. La table a
 * donc été écrite pour ce barème-là, et c'est celui qu'on émet. Contrairement à
 * Snake, Tetris ou Asteroid, elle n'a rien eu à changer.
 *
 * Ce qui n'est PAS porté, et c'est dit
 * ------------------------------------
 * **Le boss à bras articulés.** Six cents lignes : huit composants avec leurs
 * points de vie propres, douze explosions, une interpolation polynomiale de
 * trajectoire sur quatre points, un laser qui pivote en trente-six images. Le
 * boss existe — c'est l'ennemi 4, celui à 90 points de vie, qui tient le haut
 * de l'écran et tire en rafale — mais il n'a pas ses bras qui se détachent un
 * par un. C'est la pièce d'animation la plus lourde des huit jeux, et la moins
 * mécanique.
 */
#ifndef NS_SHOOTER_H
#define NS_SHOOTER_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

/* Un tiers de 1920 : le couloir vertical de 2020. */
#define SH_W 640.0f
#define SH_H 1080.0f

#define SH_LOGICAL_W 1920.0f
#define SH_LOGICAL_H 1080.0f

#define SH_MAX_ENEMIES 32
#define SH_MAX_SHOTS   192
#define SH_MAX_WEAPONS 5     /* NB_MAX_WEAPON */
#define SH_ENEMY_KINDS 5     /* NB_ENEMY */

typedef enum sh_shot_kind {
    SH_ALLY_BASE = 0, SH_ALLY_ZIGZAG, SH_ALLY_HOMING,   /* NB_ALLY_MISSILES */
    SH_ENEMY_BASE, SH_ENEMY_LASER,                      /* NB_ENEMY_MISSILES */
    SH_SHOT_KINDS
} sh_shot_kind;

typedef enum sh_phase { SH_READY = 0, SH_PLAYING, SH_DEAD } sh_phase;

typedef struct sh_shot {
    float x, y, vx, vy;
    float damage, radius, life;
    int   kind;
    bool  hostile;
    bool  alive;
} sh_shot;

typedef struct sh_enemy {
    float x, y, vx, vy;
    float hp, hp_max;
    int   kind;
    float target_y;          /* les gros descendent puis tiennent leur ligne */
    float reload[7];         /* NB_MAX_WEAPON_ENEMY */
    int   burst[7];
    bool  alive;
    float hit_flash;
} sh_enemy;

typedef struct shooter {
    sh_phase phase;

    float ship_x, ship_y, ship_vx;
    int   weapons;           /* 1..5 */
    int   ammo_kind;         /* SH_ALLY_* */
    float fire_timer, fire_period;
    float invuln;
    int   lives;             /* vies restantes EN PLUS de celle en cours */

    sh_enemy enemy[SH_MAX_ENEMIES];
    sh_shot  shot[SH_MAX_SHOTS];

    float    wave_timer;
    uint32_t wave;
    bool     boss_alive;

    int64_t  score;
    uint32_t best;
    uint32_t killed;
    bool     hard;

    float time;
    float dead_time;
    float scroll;
    bool  held[NS_GAME_BUTTON_COUNT];

    ns_rng rng;

    /* Événements, CONSOMMÉS par `shooter_events`. */
    bool     fired, died;
    int64_t  pend_enemy;     /* points de vie détruits, pas encore journalisés */
    uint32_t pend_boss, pend_wave;
} shooter;

typedef struct shooter_art {
    ns_texture background;
    bool ready;
} shooter_art;

bool shooter_art_load(ns_rhi *r, shooter_art *a);
void shooter_art_free(ns_rhi *r, shooter_art *a);

void shooter_reset(shooter *g, uint64_t seed, bool hard);
void shooter_press(shooter *g, ns_game_button b);
void shooter_hold(shooter *g, const bool held[NS_GAME_BUTTON_COUNT]);
void shooter_tick(shooter *g, float dt);
void shooter_draw(ns_sprite *s, const shooter *g, const shooter_art *a,
                  float logical_w, float logical_h);
bool shooter_autopilot(shooter *g);

/* Exposés pour les tests : ce sont les règles. */
int  shooter_live_enemies(const shooter *g);
int  shooter_weapon_slots(int weapons, int slot_out[SH_MAX_WEAPONS]);

extern const ns_game_api g_shooter_api;

#endif /* NS_SHOOTER_H */
