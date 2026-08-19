/*
 * pacman.h — le Pac-Man de 2020, porté ET terminé.
 *
 * Ce qu'il faut dire d'abord
 * --------------------------
 * **Le Pac-Man de 2020 n'était pas un jeu.** Ses 251 lignes tiennent une grille
 * de 20 x 20 pastilles, un labyrinthe dont `carte1()` ne trace QUE le bord, un
 * personnage qui se déplace en ligne droite, et rien d'autre : pas de score,
 * pas de mort, pas de niveau — et **aucun fantôme**, alors que `enemy.png` est
 * là, chargé par personne. C'est une boîte vide dans laquelle on mange des
 * points.
 *
 * Le porter « à l'identique » aurait donné une borne sur laquelle il n'y a rien
 * à faire, et la table du serveur le disait déjà : `rulesTable["pacman"]`
 * attend `pellet`, `power`, `ghost` et `level`, c'est-à-dire un Pac-Man
 * complet. Elle a été écrite pour le jeu qu'il devait être.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * La grille de **20 x 20** cases de 40 px, la vitesse de **2 px par image à
 * 60 Hz** (`VITESSE_DEPLACEMENT / (FPS/30)`), le découpage de `pacman.png` en
 * quatre directions x quatre ouvertures de bouche, et `enemy.png` avec ses
 * quatre fantômes.
 *
 * Ce qui est ajouté, et pourquoi
 * ------------------------------
 * Un **labyrinthe** — écrit en clair dans le source, symétrique, avec ses deux
 * tunnels latéraux ; **quatre fantômes** avec les quatre comportements du jeu
 * d'arcade d'origine (poursuite, embuscade, dispersion, aléatoire) et leur
 * alternance dispersion / poursuite ; les **super-pastilles** qui les rendent
 * mangeables ; le **score** et les **niveaux**. Rien de tout ça n'est une
 * invention : c'est ce que le Pac-Man de 1980 fait, ce que la table du serveur
 * attend, et ce que l'auteur de 2020 avait manifestement l'intention d'écrire.
 */
#ifndef NS_PACMAN_H
#define NS_PACMAN_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

#define PM_COLS 21
#define PM_ROWS 21
#define PM_CELL 40.0f          /* WINDOW_L / SIZE_TABLEAU_PIECE de 2020 */

#define PM_LOGICAL_W 1920.0f
#define PM_LOGICAL_H 1080.0f

#define PM_GHOSTS 4

typedef enum pm_tile { PM_WALL = 0, PM_EMPTY, PM_PELLET, PM_POWER } pm_tile;
typedef enum pm_phase { PM_READY = 0, PM_PLAYING, PM_DEAD } pm_phase;

/* `enum direction {DROIT, HAUT, GAUCHE, BAS}` de 2020, dans cet ordre : c'est
 * lui qui indexe les colonnes de `pacman.png`. */
typedef enum pm_dir { PM_RIGHT = 0, PM_UP, PM_LEFT, PM_DOWN, PM_DIR_COUNT } pm_dir;

typedef struct pm_ghost {
    float  x, y;
    pm_dir dir;
    int    kind;         /* 0..3 : le comportement ET la couleur */
    float  frightened;   /* secondes de fuite restantes */
    bool   eaten;        /* mangé : il rentre à la maison */
    float  respawn;
} pm_ghost;

typedef struct pacman {
    pm_phase phase;

    uint8_t tile[PM_ROWS][PM_COLS];
    uint32_t pellets_left;

    float  x, y;             /* en pixels, dans la grille */
    pm_dir dir, want;
    float  mouth;            /* phase d'ouverture de la bouche */

    pm_ghost ghost[PM_GHOSTS];
    float  scatter_timer;
    bool   scattering;
    uint32_t chain;          /* fantômes mangés d'affilée : 200, 400, 800, 1600 */

    uint32_t level;
    int64_t  score;
    uint32_t best;
    bool     hard;

    float time;
    float dead_time;
    bool  held[NS_GAME_BUTTON_COUNT];

    ns_rng rng;

    /* Événements, CONSOMMÉS par `pacman_events`. */
    bool     turned, died;
    uint32_t pend_pellet, pend_power, pend_ghost, pend_level;
} pacman;

typedef struct pacman_art {
    ns_texture hero, enemy;
    bool ready;
} pacman_art;

bool pacman_art_load(ns_rhi *r, pacman_art *a);
void pacman_art_free(ns_rhi *r, pacman_art *a);

void pacman_reset(pacman *g, uint64_t seed, bool hard);
void pacman_press(pacman *g, ns_game_button b);
void pacman_hold(pacman *g, const bool held[NS_GAME_BUTTON_COUNT]);
void pacman_tick(pacman *g, float dt);
void pacman_draw(ns_sprite *s, const pacman *g, const pacman_art *a,
                 float logical_w, float logical_h);
bool pacman_autopilot(pacman *g);

/* Exposés pour les tests : ce sont les règles. */
bool pacman_walkable(const pacman *g, int col, int row);
int  pacman_count_pellets(const pacman *g);

extern const ns_game_api g_pacman_api;

#endif /* NS_PACMAN_H */
