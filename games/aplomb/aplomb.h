/*
 * aplomb.h — APLOMB : le jeu d'empilement, porté de 2020 et DÉBAPTISÉ.
 *
 * Le nom, et pourquoi il a changé
 * -------------------------------
 * Ce jeu s'appelait « TETRIS » — `.id`, `.title`, `.label`, l'enseigne de ses
 * deux bornes, sa clé de classement. **TETRIS est une marque déposée de Tetris
 * Holding**, défendue, et le nom seul suffisait à rendre le paquet invendable.
 *
 * Ici, et contrairement au jeu de labyrinthe, il n'y avait RIEN d'autre à
 * changer : la planche employée (`bricks.png`, huit carrés biseautés de huit
 * couleurs) n'est pas un personnage et n'appartient à personne de reconnais-
 * sable — ouverte et regardée, c'est un carré avec un biseau. Faire tomber des
 * pièces de quatre cases et effacer les lignes pleines est une MÉCANIQUE, donc
 * non protégeable. Seul le nom l'était.
 *
 * « Aplomb » : le mot du maçon pour « d'équerre, à la verticale ». Il dit ce
 * qu'on demande au joueur, il n'évoque aucune marque — surtout pas par un
 * suffixe en « -tris », qui serait exactement la faute à éviter — et il tient
 * en six caractères, la largeur de la colonne du classement.
 *
 * Le vocabulaire d'événements (`lines`, `drop`, `rotate`, `death`) n'a pas
 * bougé : il ne nommait déjà pas le jeu. Seule la clé de `rulesTable` et les
 * créneaux de classement (`aplomb-easy` / `aplomb-hard`) ont suivi.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * La grille de **10 x 20** en cases de 38 px, les **sept pièces avec leurs
 * quatre rotations** — recopiées entier par entier depuis `pieces.h`, voir
 * `aplomb_pieces.h` — la courbe de vitesse à quatre temporisations et son
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
#ifndef NS_APLOMB_H
#define NS_APLOMB_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"
#include "aplomb_pieces.h"

#include <stdbool.h>
#include <stdint.h>

#define APL_W 10             /* GRILLE_W */
#define APL_H 20             /* GRILLE_H */
#define APL_CASE 38.0f       /* CASE_SIZE */

/* Le repère logique, comme les autres jeux portés. */
#define APL_LOGICAL_W 1920.0f
#define APL_LOGICAL_H 1080.0f

/* `EMPTY` de 2020. Sinon la case porte l'identifiant de la pièce, ce qui est ce
 * qui permet de reconnaître une ligne d'une seule couleur. */
#define APL_EMPTY (-1)

/* Les bonus qui comptent au score. `NO_BONUS` garde la valeur 0 de l'original. */
typedef enum apl_bonus { APL_NO_BONUS = 0, APL_MULTI, APL_FLAT } apl_bonus;

typedef enum apl_phase { APL_READY = 0, APL_PLAYING, APL_DEAD } apl_phase;

typedef struct apl_piece {
    int id;        /* 0..6 */
    int rota;      /* 0..3 */
    int giant;     /* 0 ou 1 : l'indice de TAILLE dans la table */
    int x, y;      /* coin haut-gauche de la grille 10x10, en cases */
    apl_bonus bonus;
} apl_piece;

typedef struct aplomb {
    apl_phase phase;

    int8_t    cell[APL_H][APL_W];    /* APL_EMPTY, ou l'identifiant de pièce */
    uint8_t   cell_bonus[APL_H][APL_W];

    apl_piece cur, next;
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
    float robot_wait;    /* la cadence du joueur automatique, voir aplomb.c */
    float flash;                     /* l'éclat d'une ligne qui part */

    ns_rng rng;

    /* Événements, CONSOMMÉS par `aplomb_events` — voir games.h. */
    bool     moved, dropped, died;
    int64_t  pend_points;            /* points gagnés, pas encore journalisés */
} aplomb;

typedef struct aplomb_art {
    ns_texture bricks;
    bool ready;
} aplomb_art;

bool aplomb_art_load(ns_rhi *r, aplomb_art *a);
void aplomb_art_free(ns_rhi *r, aplomb_art *a);

void aplomb_reset(aplomb *g, uint64_t seed, bool hard);
void aplomb_press(aplomb *g, ns_game_button b);
void aplomb_hold(aplomb *g, const bool held[NS_GAME_BUTTON_COUNT]);
void aplomb_tick(aplomb *g, float dt);
void aplomb_draw(ns_sprite *s, const aplomb *g, const aplomb_art *a,
                 float logical_w, float logical_h);
bool aplomb_autopilot(aplomb *g);

/* Exposées pour les tests : ce sont les règles, pas des détails. */
bool aplomb_fits(const aplomb *g, const apl_piece *p);
int  aplomb_clear_lines(aplomb *g);

extern const ns_game_api g_aplomb_api;

#endif /* NS_APLOMB_H */
