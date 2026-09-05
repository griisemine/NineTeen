/*
 * dedale.h — DÉDALE : le jeu de labyrinthe, porté de 2020, terminé, et
 * DÉBAPTISÉ.
 *
 * Le nom, et pourquoi il a changé
 * -------------------------------
 * Ce jeu s'appelait « PAC-MAN » — `.id`, `.title`, `.label`, l'enseigne de ses
 * trois bornes, sa clé de classement. **PAC-MAN est une marque déposée de
 * Bandai Namco**, et son héros comme ses fantômes sont des personnages sous
 * droit. Le jeu portait le nom ET les planches : `pacman.png` et `enemy.png`
 * étaient les sprites de la borne de 1980, livrés dans le paquet.
 *
 * La distinction qui décide de tout : une MÉCANIQUE ne s'approprie pas. Manger
 * des pastilles dans un labyrinthe en fuyant quatre poursuivants qui alternent
 * dispersion et poursuite, c'est un système de règles, et un système de règles
 * n'est pas protégeable. Un NOM et un PERSONNAGE, si. Les règles restent donc
 * exactement ce qu'elles étaient ; le nom et les figures changent.
 *
 * « Dédale » : le nom commun français du labyrinthe. Il dit ce que le jeu EST,
 * il n'évoque aucune marque, et il tient en six caractères — la largeur de la
 * colonne du classement.
 *
 * Ce qui a remplacé les personnages
 * ---------------------------------
 * Le héros est **le rubis**, une pierre taillée : table plate en haut, taille
 * qui se rétrécit jusqu'à une pointe. Les poursuivants sont **les hélices**,
 * un rotor à quatre pales. Les deux sont dessinés par `tools/spriteart`, qui
 * explique la contrainte : à seize pixels de haut, seule une masse pleine à
 * contour franc se lit — mais rien n'oblige à ce que ce soit CETTE masse-là.
 * Une pierre pleine et un rotor ajouré ne se confondent ni entre eux, ni avec
 * ce qu'ils remplacent.
 *
 * Ce qu'il faut dire ensuite
 * --------------------------
 * **Le jeu de labyrinthe de 2020 n'était pas un jeu.** Ses 251 lignes tiennent
 * une grille de 20 x 20 pastilles, un labyrinthe dont `carte1()` ne trace QUE
 * le bord, un personnage qui se déplace en ligne droite, et rien d'autre : pas
 * de score, pas de mort, pas de niveau — et **aucun poursuivant**, alors que
 * `enemy.png` était là, chargé par personne. C'est une boîte vide dans laquelle
 * on mange des points.
 *
 * Le porter « à l'identique » aurait donné une borne sur laquelle il n'y a rien
 * à faire, et la table du serveur le disait déjà : `rulesTable["dedale"]`
 * attend `pellet`, `power`, `ghost` et `level`, c'est-à-dire un jeu de
 * labyrinthe complet. Elle a été écrite pour le jeu qu'il devait être.
 *
 * Le vocabulaire d'événements n'a PAS été renommé avec le jeu, et c'est
 * délibéré : « ghost » y est un nom commun qui désigne un poursuivant mangé,
 * pas une marque, et c'est un PROTOCOLE — le renommer invaliderait tout journal
 * de partie déjà scellé côté serveur. Seule la clé de la table a suivi.
 *
 * Ce qui est repris tel quel de 2020
 * ----------------------------------
 * La grille de **20 x 20** cases de 40 px et la vitesse de **2 px par image à
 * 60 Hz** (`VITESSE_DEPLACEMENT / (FPS/30)`). Le découpage des planches, lui,
 * ne pouvait pas être repris : les planches ne sont plus les mêmes.
 *
 * Ce qui est ajouté, et pourquoi
 * ------------------------------
 * Un **labyrinthe** — écrit en clair dans le source, symétrique, avec ses deux
 * tunnels latéraux ; **quatre poursuivants** avec quatre comportements
 * (poursuite, embuscade, dispersion, aléatoire) et leur alternance dispersion /
 * poursuite ; les **super-pastilles** qui les rendent mangeables ; le **score**
 * et les **niveaux**. Rien de tout ça n'est une invention : c'est ce que fait
 * n'importe quel jeu de labyrinthe depuis 1980, c'est ce que la table du
 * serveur attend, et c'est ce que l'auteur de 2020 avait manifestement
 * l'intention d'écrire.
 */
#ifndef NS_DEDALE_H
#define NS_DEDALE_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

#define DD_COLS 21
#define DD_ROWS 21
#define DD_CELL 40.0f          /* WINDOW_L / SIZE_TABLEAU_PIECE de 2020 */

#define DD_LOGICAL_W 1920.0f
#define DD_LOGICAL_H 1080.0f

#define DD_ROTORS 4

/* TROIS VIES, comme tout jeu de labyrinthe depuis 1980 — et le portage n'en
 * donnait qu'UNE. Ce n'est pas un détail d'équilibrage : avec une seule vie, la
 * première erreur termine la partie, on n'apprend jamais le plan, et le score
 * ne dépasse pas ce qu'on ramasse avant la première rencontre. Mesuré par la
 * recette : mort à trente-neuf secondes, à chaque partie. */
#define DD_LIVES 3

/* La pause après une prise : le joueur DOIT voir ce qui l'a eu. Sans elle, la
 * partie reprend au centre du labyrinthe sans qu'on ait compris. */
#define DD_CAUGHT_TIME 1.4f

typedef enum dd_tile { DD_WALL = 0, DD_EMPTY, DD_PELLET, DD_POWER } dd_tile;
/* `DD_CAUGHT` : pris, mais il reste une vie. Le temps s'arrête, les hélices
 * regagnent l'enclos et la manche repart. */
typedef enum dd_phase { DD_READY = 0, DD_PLAYING, DD_CAUGHT, DD_DEAD } dd_phase;

/* `enum direction {DROIT, HAUT, GAUCHE, BAS}` de 2020, dans cet ordre : c'est
 * lui qui indexe les colonnes de `dedale.png`. */
typedef enum dd_dir { DD_RIGHT = 0, DD_UP, DD_LEFT, DD_DOWN, DD_DIR_COUNT } dd_dir;

typedef struct dd_rotor {
    float  x, y;
    dd_dir dir;
    int    kind;         /* 0..3 : le comportement ET la couleur */
    float  frightened;   /* secondes de fuite restantes */
    bool   eaten;        /* mangé : il rentre à la maison */
    float  respawn;
} dd_rotor;

typedef struct dedale {
    dd_phase phase;

    uint8_t tile[DD_ROWS][DD_COLS];
    uint32_t pellets_left;

    float  x, y;             /* en pixels, dans la grille */
    dd_dir dir, want;
    float  eclat;            /* phase de l'éclat du rubis, en secondes */

    dd_rotor rotor[DD_ROTORS];
    float  scatter_timer;
    bool   scattering;
    uint32_t chain;          /* hélices mangées d'affilée : 200, 400, 800, 1600 */

    uint32_t level;
    uint32_t lives;          /* il en reste combien, celle en cours comprise */
    int64_t  score;
    uint32_t best;
    bool     hard;

    float time;
    float caught_time;       /* secondes restantes de la pause après une prise */
    float dead_time;
    bool  held[NS_GAME_BUTTON_COUNT];

    ns_rng rng;

    /* Événements, CONSOMMÉS par `dedale_events`. */
    bool     turned, died;
    uint32_t pend_pellet, pend_power, pend_rotor, pend_level;
} dedale;

typedef struct dedale_art {
    ns_texture heros, helices;
    bool ready;
} dedale_art;

bool dedale_art_load(ns_rhi *r, dedale_art *a);
void dedale_art_free(ns_rhi *r, dedale_art *a);

void dedale_reset(dedale *g, uint64_t seed, bool hard);
void dedale_press(dedale *g, ns_game_button b);
void dedale_hold(dedale *g, const bool held[NS_GAME_BUTTON_COUNT]);
void dedale_tick(dedale *g, float dt);
void dedale_draw(ns_sprite *s, const dedale *g, const dedale_art *a,
                 float logical_w, float logical_h);
bool dedale_autopilot(dedale *g);

/* Exposés pour les tests : ce sont les règles. */
bool dedale_walkable(const dedale *g, int col, int row);
int  dedale_count_pellets(const dedale *g);

extern const ns_game_api g_dedale_api;

#endif /* NS_DEDALE_H */
