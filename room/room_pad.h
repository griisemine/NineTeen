/*
 * room_pad.h — la manette, et la seule table qui traduit une entrée en boutons.
 *
 * Le piège que ce fichier existe pour désamorcer
 * ----------------------------------------------
 * `main.c` calculait DEUX FOIS la même chose, à vingt lignes d'intervalle : une
 * fois dans `held[]` pour le jeu, une fois dans `hmask` pour le journal
 * d'entrées. Les deux lisaient le clavier, les deux lisaient les mêmes touches,
 * et rien n'obligeait la seconde à suivre la première.
 *
 * Or `hmask` n'est pas un détail d'implémentation : c'est ce que
 * `--journal-entrees` ÉCRIT, ce que `--rejouer` relit, ce que `tests/test_replay`
 * éprouve et ce qu'un duel en lockstep PUBLIE SUR LE RÉSEAU. Une manette
 * branchée sur `held[]` seulement aurait rendu toute partie jouée à la manette
 * non reproductible et fait diverger les duels — sans un message d'erreur, sans
 * un test rouge, avec pour seul symptôme « l'autre joueur ne voit pas la même
 * chose ».
 *
 * D'où la règle que ce fichier impose : il n'existe qu'UNE fonction qui produit
 * le masque de boutons, `room_pad_mask` pour la manette et `room_keys_mask` pour
 * le clavier. `main.c` les additionne une fois par image et TOUT le reste —
 * `held[]`, `hmask`, le journal, le réseau — descend de cet octet-là.
 *
 * Pourquoi la partie calculée est séparée de la partie SDL
 * --------------------------------------------------------
 * Parce que personne n'a de manette branchée sur les machines qui construisent
 * ce dépôt. `room_pad_sample` interroge un vrai périphérique et ne peut donc
 * s'éprouver qu'à la main ; tout ce qui décide — zone morte, seuils, diagonales,
 * conversion en masque, en déplacement et en regard — est du calcul pur sur une
 * `room_pad_state` qu'un test remplit lui-même. `tests/test_pad.c` s'en sert
 * pour vérifier la propriété qui compte : à geste égal, la manette rend
 * EXACTEMENT le masque du clavier.
 */
#ifndef NS_ROOM_PAD_H
#define NS_ROOM_PAD_H

#include "games.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * L'état BRUT d'une manette à un instant : ce que SDL rapporte, normalisé, et
 * rien de plus. Aucune zone morte n'y est appliquée — c'est justement ce que le
 * test veut pouvoir faire varier.
 *
 * Les axes suivent le repère de SDL : +x à droite, **+y vers le BAS**. Le
 * convertir ici ferait une deuxième convention à retenir, et la première erreur
 * de signe se paierait par un personnage qui recule quand on pousse.
 */
typedef struct room_pad_state {
    /* La croix directionnelle, indexée par `ns_game_button` — donc `dpad[NS_GAME_UP]`.
     * L'ordre de l'énumération est celui des quatre premières entrées, et c'est
     * ce qui permet à `room_pad_mask` de n'être qu'une boucle. */
    bool  dpad[4];
    bool  action;          /* le bouton du bas (A / croix) */
    bool  menu;            /* Start */
    float move_x, move_y;  /* stick gauche, −1 à +1 */
    float look_x, look_y;  /* stick droit, −1 à +1 */
} room_pad_state;

/*
 * Ce qui se règle, et qui doit se régler.
 *
 * Une zone morte ABSENTE rend le personnage ivre : aucun stick ne rend zéro au
 * repos, et une dérive de 3 % suffit à faire tourner la vue toute seule pendant
 * qu'on lit le menu. Une zone morte FIXE est le défaut d'à côté : elle est trop
 * petite pour une manette usée et trop grande pour une neuve, et le joueur n'a
 * aucun recours.
 */
typedef struct room_pad_tuning {
    float deadzone;        /* stick gauche, 0 à 1 */
    float look_deadzone;   /* stick droit — usé différemment, donc réglé à part */
    float look_speed;      /* radians par seconde à fond de course */
} room_pad_tuning;

/* Les valeurs par défaut, puis ce que `nineteen.env` en dit. */
void room_pad_read_env(room_pad_tuning *t);

/*
 * LE MASQUE — l'octet dont dépendent le journal d'entrées et les duels.
 *
 * Un bit par `ns_game_button`. La croix directionnelle et le stick gauche s'y
 * versent tous les deux, parce qu'ils veulent dire la même chose : un joueur qui
 * tient le stick à gauche et un joueur qui tient la croix à gauche jouent le
 * même coup, et le journal doit les enregistrer pareil.
 *
 * Le seuil du stick EST la zone morte. C'est un choix, et il tient en une
 * phrase : deux seuils demanderaient au joueur de comprendre pourquoi son stick
 * bouge la caméra sans bouger le personnage.
 */
uint8_t room_pad_mask(const room_pad_state *s, const room_pad_tuning *t);

/* Le même masque, pour le CLAVIER. `keys` est le tableau rendu par
 * `SDL_GetKeyboardState`, indexé par scancode. Les deux fonctions sont côte à
 * côte pour qu'on ne puisse pas en modifier une en oubliant l'autre. */
uint8_t room_keys_mask(const bool *keys);

/*
 * Le DÉPLACEMENT dans la salle : analogique, lui, parce qu'un stick sait
 * marcher doucement et qu'un clavier ne le sait pas.
 *
 * Zone morte RADIALE, et rééchelonnée : sans le rééchelonnement, la vitesse
 * saute de zéro à un quart dès qu'on sort de la zone, ce qui se sent tout de
 * suite. `forward` est positif vers l'avant, `strafe` positif vers la droite —
 * le repère de `room_camera`, pas celui de SDL.
 */
void room_pad_move(const room_pad_state *s, const room_pad_tuning *t,
                   float *forward, float *strafe);

/* Le REGARD, en radians pour ce pas de simulation. Même zone morte radiale et
 * même rééchelonnement, avec sa propre zone et sa propre vitesse. */
void room_pad_look(const room_pad_state *s, const room_pad_tuning *t, float dt,
                   float *dyaw, float *dpitch);

/* --------------------------------------------------------------------------
 * Le côté SDL — celui qu'aucun test ne peut atteindre sans matériel
 * -------------------------------------------------------------------------- */

/* Lit le périphérique. `g` nul rend un état entièrement au repos, ce qui est
 * exactement ce qu'il faut quand aucune manette n'est branchée. */
void room_pad_sample(SDL_Gamepad *g, room_pad_state *out);

#endif /* NS_ROOM_PAD_H */
