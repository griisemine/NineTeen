/*
 * piano.h — le Piano de 2020, porté et terminé.
 *
 * Ce que 2020 avait, et ce qu'il n'avait pas
 * ------------------------------------------
 * Il avait la **partition** — `musique.txt`, treize lignes de
 * `touche1 touche2 touche3 touche4 longueur` — les **quatre voies**, le
 * défilement, et la règle qui fait tout le jeu : **appuyer sur une voie vide
 * termine la partie** (`touche = -1`, et `return (touche1 == -1 || ...)`).
 *
 * Il n'avait **aucun score**. Pas un point, nulle part dans ses 308 lignes.
 * Or `rulesTable["piano"]` attend `note` et `combo` depuis M6 : la table
 * décrivait le jeu qu'il devait être. C'est maintenant le jeu qui existe.
 *
 * La partition est RECOPIÉE, pas relue
 * ------------------------------------
 * Treize lignes tiennent dans le source, et un mini-jeu n'a pas le droit de
 * toucher au disque hors de ses planches — c'est la règle 1 de `games.h`, et
 * c'est ce qui rend un jeu vérifiable sans écran ni système de fichiers. Les
 * valeurs sont celles de `legacy/games/12_piano/musique.txt`, à l'entier près.
 *
 * L'adaptation à la borne
 * -----------------------
 * Quatre voies demandent quatre entrées. Un panneau d'arcade a un manche à
 * quatre directions : **gauche, haut, bas, droite** sont les quatre touches, et
 * c'est le seul appariement possible. Le bouton d'action ne sert qu'à démarrer.
 */
#ifndef NS_PIANO_H
#define NS_PIANO_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

#define PN_LANES 4
#define PN_MAX_NOTES 256

#define PN_LOGICAL_W 1920.0f
#define PN_LOGICAL_H 1080.0f

typedef enum pn_phase { PN_READY = 0, PN_PLAYING, PN_DEAD } pn_phase;

typedef struct pn_note {
    int   lane;
    float start;      /* secondes avant qu'elle atteigne la ligne de frappe */
    float length;     /* secondes de tenue */
    bool  hit, missed;
} pn_note;

typedef struct piano {
    pn_phase phase;

    pn_note note[PN_MAX_NOTES];
    uint32_t note_count;
    float    time;          /* temps de partition écoulé */
    float    speed;         /* multiplicateur, monte à chaque tour */
    uint32_t loops;

    uint32_t combo, best_combo;
    float    lane_flash[PN_LANES];
    bool     held[PN_LANES];

    int64_t  score;
    uint32_t best;
    uint32_t hits, misses;
    bool     hard;

    float dead_time;
    const char *fail_reason;

    ns_rng rng;

    /* Événements, CONSOMMÉS par `piano_events`. */
    bool     tapped, died;
    uint32_t pend_note, pend_combo;
} piano;

typedef struct piano_art { bool ready; } piano_art;

bool piano_art_load(ns_rhi *r, piano_art *a);
void piano_art_free(ns_rhi *r, piano_art *a);

void piano_reset(piano *g, uint64_t seed, bool hard);
void piano_press(piano *g, ns_game_button b);
void piano_hold(piano *g, const bool held[NS_GAME_BUTTON_COUNT]);
void piano_tick(piano *g, float dt);
void piano_draw(ns_sprite *s, const piano *g, const piano_art *a,
                float logical_w, float logical_h);
bool piano_autopilot(piano *g);

/* Exposé pour les tests : la note frappable d'une voie, ou -1. */
int piano_note_at(const piano *g, int lane);

extern const ns_game_api g_piano_api;

#endif /* NS_PIANO_H */
