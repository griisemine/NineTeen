/*
 * tetris.h — le Tetris de 2020, porté.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * La grille de **10 x 20** en cases de 38 px, les **sept pièces avec leurs
 * quatre rotations** — recopiées entier par entier depuis `pieces.h`, voir
 * `tetris_pieces.h` — la courbe de vitesse à quatre temporisations et son
 * amortissement géométrique, et le barème : cent points la ligne, **doublés à
 * chaque ligne simultanée**, **multipliés par dix** quand la ligne est d'une
 * seule couleur.
 *
 * Deux constats qui interdisaient tout raccourci
 * ----------------------------------------------
 * Mesurés sur la table d'origine, pas supposés :
 *
 *   - les **deux difficultés n'ont aucune forme en commun** : les 56 diffèrent ;
 *   - une **pièce géante EST la pièce normale doublée** — les 56 le sont — mais
 *     son **pivot ne l'est pas** : dans 42 cas sur 56 il n'est pas au double de
 *     celui de la pièce normale.
 *
 * Un générateur aurait donc produit la bonne forme et la mauvaise rotation. Le
 * pivot est ce autour de quoi la pièce tourne : le placer une case à côté fait
 * qu'une pièce longue se déplace à chaque quart de tour. C'est le genre de
 * défaut qui a l'air juste sur une capture et qui se découvre en jouant.
 *
 * Le temps
 * --------
 * Tout est écrit en IMAGES à 30 Hz dans l'original, y compris la vitesse de
 * chute. Chaque constante est convertie en secondes avec sa valeur de départ
 * écrite à côté. La descente commence à 20 images par ligne — deux tiers de
 * seconde — et décroît de 0,99976 par image jusqu'à une image par ligne.
 *
 * Ce qui n'est PAS porté, et c'est dit
 * ------------------------------------
 * Les quatre bonus ANIMÉS de 2020 — le laser qui traverse l'écran, le
 * remplissage qui tombe case par case, l'accélération et le ralentissement —
 * représentent huit cents lignes d'animation pour une mécanique qui ne change
 * pas la partie. Les deux bonus qui comptent au SCORE sont là : `MULTI_POINT`
 * (la ligne vaut double) et `FLAT_POINT` (+500). Le reste viendra ou ne viendra
 * pas, mais il ne sera pas annoncé comme présent.
 */
#ifndef NS_TETRIS_H
#define NS_TETRIS_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"
#include "tetris_pieces.h"

#include <stdbool.h>
#include <stdint.h>

#define TET_W 10             /* GRILLE_W */
#define TET_H 20             /* GRILLE_H */
#define TET_CASE 38.0f       /* CASE_SIZE */

/* Le repère logique, comme les autres jeux portés. */
#define TET_LOGICAL_W 1920.0f
#define TET_LOGICAL_H 1080.0f

/* `EMPTY` de 2020. Sinon la case porte l'identifiant de la pièce, ce qui est ce
 * qui permet de reconnaître une ligne d'une seule couleur. */
#define TET_EMPTY (-1)

/* Les bonus qui comptent au score. `NO_BONUS` garde la valeur 0 de l'original. */
typedef enum tet_bonus { TET_NO_BONUS = 0, TET_MULTI, TET_FLAT } tet_bonus;

typedef enum tet_phase { TET_READY = 0, TET_PLAYING, TET_DEAD } tet_phase;

typedef struct tet_piece {
    int id;        /* 0..6 */
    int rota;      /* 0..3 */
    int giant;     /* 0 ou 1 : l'indice de TAILLE dans la table */
    int x, y;      /* coin haut-gauche de la grille 10x10, en cases */
    tet_bonus bonus;
} tet_piece;

typedef struct tetris {
    tet_phase phase;

    int8_t    cell[TET_H][TET_W];    /* TET_EMPTY, ou l'identifiant de pièce */
    uint8_t   cell_bonus[TET_H][TET_W];

    tet_piece cur, next;
    bool      spawned;

    /* Les quatre temporisations de 2020, en SECONDES. */
    float fall, fall_period;
    float lateral, lateral_period;
    float lock, lock_period;         /* FRAME_STOP : le temps collé au fond */
    float elapsed;                   /* pour l'amortissement de la vitesse */

    bool  held[NS_GAME_BUTTON_COUNT];
    bool  soft_drop;

    int64_t  score;
    uint32_t best;
    uint32_t lines;
    uint32_t pieces;
    bool     hard;                   /* la borne « hard » : formes et géants */

    float time;
    float dead_time;
    float robot_wait;    /* la cadence du joueur automatique, voir tetris.c */
    float flash;                     /* l'éclat d'une ligne qui part */

    ns_rng rng;

    /* Événements, CONSOMMÉS par `tetris_events` — voir games.h. */
    bool     moved, dropped, died;
    int64_t  pend_points;            /* points gagnés, pas encore journalisés */
} tetris;

typedef struct tetris_art {
    ns_texture bricks;
    bool ready;
} tetris_art;

bool tetris_art_load(ns_rhi *r, tetris_art *a);
void tetris_art_free(ns_rhi *r, tetris_art *a);

void tetris_reset(tetris *g, uint64_t seed, bool hard);
void tetris_press(tetris *g, ns_game_button b);
void tetris_hold(tetris *g, const bool held[NS_GAME_BUTTON_COUNT]);
void tetris_tick(tetris *g, float dt);
void tetris_draw(ns_sprite *s, const tetris *g, const tetris_art *a,
                 float logical_w, float logical_h);
bool tetris_autopilot(tetris *g);

/* Exposées pour les tests : ce sont les règles, pas des détails. */
bool tetris_fits(const tetris *g, const tet_piece *p);
int  tetris_clear_lines(tetris *g);

extern const ns_game_api g_tetris_api;

#endif /* NS_TETRIS_H */
