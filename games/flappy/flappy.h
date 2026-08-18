/*
 * flappy.h — Flappy Bird, porté depuis les 1 402 lignes de 2020.
 *
 * Ce qui est repris tel quel, et ce qui ne l'est pas
 * -------------------------------------------------
 * **Les planches de sprites et les cotes de découpe** sont celles de 2020, au
 * pixel près : oiseau 17 x 12 en trois images, tuyau 26 x 160, sol 168 x 55,
 * chiffre 12 x 18, le tout à l'échelle 4 comme le faisait `SCALE_TO_FIT`. C'est
 * ce qui fait qu'on rejoue au même jeu et pas à une imitation.
 *
 * **Les cotes du terrain** aussi, et elles viennent des constantes du fichier
 * d'origine : écart vertical entre les tuyaux `DISTANCE_BETWEEN_OBSTACLE = 49`,
 * écart horizontal `DISTANCE_UNDER_OBSTACLE = 100`, cinq hauteurs de passage
 * tirées dans une tranche de 550, vitesse de défilement 4 pixels par image à
 * 60 Hz — toutes multipliées par 4, et documentées à leur place ci-dessous.
 *
 * **La physique, elle, est réécrite en flottant.** L'original intègre en entiers
 * et en nombre d'images : `vitesseGraviter` s'incrémente une image sur deux, la
 * montée est une boucle de huit images pendant laquelle `y -= (30 - upper)/2`.
 * Rejouer ça au pas fixe du moteur donnerait une chute deux fois trop rapide, et
 * une simulation dont le comportement dépendrait de la fréquence d'affichage —
 * exactement le défaut qu'on a passé le projet à retirer. La gravité et
 * l'impulsion sont donc exprimées en pixels par seconde carrée et calées sur la
 * hauteur de saut mesurée dans l'original (~100 px). Le chiffre est donné dans
 * le .c, et le test le vérifie.
 *
 * Aucun appel SDL, aucun accès disque, aucun état global : la partie est une
 * structure qu'on avance d'un pas. C'est ce qui la rend rejouable à l'identique
 * — donc scriptable pour les captures, et vérifiable sans écran.
 */
#ifndef NS_FLAPPY_H
#define NS_FLAPPY_H

#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Le terrain de jeu, en pixels d'origine (1920 x 1080 à l'échelle 4). Toute la
 * logique vit dans ce repère ; l'affichage le met à l'échelle de la cible, quelle
 * qu'elle soit — l'écran d'une borne ou le plein écran.
 */
#define FLAPPY_W 1920.0f
#define FLAPPY_H 1080.0f

#define FLAPPY_PIPES 8          /* PRELOAD_POS_OBSTACLE de 2020 */

typedef enum flappy_phase {
    FLAPPY_READY = 0,   /* en attente du premier battement */
    FLAPPY_PLAYING,
    FLAPPY_DEAD
} flappy_phase;

typedef struct flappy_pipe {
    float position;     /* x du bord gauche */
    int   slot;         /* 0..4, la hauteur de passage */
    bool  scored;
} flappy_pipe;

typedef struct flappy {
    flappy_phase phase;

    float bird_y;
    float bird_vy;
    float bird_angle;       /* degrés, −30 en montée, +90 en piqué */

    flappy_pipe pipes[FLAPPY_PIPES];
    float ground_scroll;
    float wing_time;        /* anime les trois images de l'oiseau */
    float dead_time;

    uint32_t score;
    uint32_t best;
    bool     hard;          /* la borne « hard » resserre l'écart */

    ns_rng rng;

    /* Événements de l'image : l'appelant les lit pour jouer un son, puis ils
     * sont remis à zéro au pas suivant. Le jeu ne connaît pas le mixeur. */
    bool flapped, scored_now, died_now;
} flappy;

/* Les planches de 2020, chargées une fois. */
typedef struct flappy_art {
    ns_texture background, birds, pipes, ground, digits;
    bool ready;
} flappy_art;

bool flappy_art_load(ns_rhi *r, flappy_art *a);
void flappy_art_free(ns_rhi *r, flappy_art *a);

void flappy_reset(flappy *g, uint64_t seed, bool hard);

/* Un battement d'aile. Sans effet après la mort — c'est ce qui empêche de
 * « rejouer » en maintenant la touche pendant l'écran de fin. */
void flappy_flap(flappy *g);

/* Avance d'un pas FIXE. `dt` doit être constant d'un appel à l'autre : c'est ce
 * qui rend la partie reproductible à la graine près. */
void flappy_tick(flappy *g, float dt);

/* Dessine dans le repère logique courant de `s`, en s'y adaptant : le terrain de
 * 1920 x 1080 est mis à l'échelle et centré, donc la même fonction remplit
 * l'écran d'une borne et le plein écran. */
void flappy_draw(ns_sprite *s, const flappy *g, const flappy_art *a,
                 float logical_w, float logical_h);

/*
 * Un joueur automatique, pour les captures et l'intégration continue.
 *
 * Il ne sert PAS à démontrer que le jeu est amusant — une machine qui vise le
 * centre du trou n'apprend rien là-dessus. Il sert à ce qu'un enchaînement de
 * plusieurs milliers de pas puisse tourner sans écran ni clavier : c'est ce qui
 * prouve qu'on peut franchir un tuyau, que le score monte, que le recyclage des
 * tuyaux ne finit pas par les empiler, et qu'aucun NaN ne s'installe.
 *
 * À appeler juste avant `flappy_tick`. Renvoie true s'il a battu des ailes.
 */
bool flappy_autopilot(flappy *g);

#endif /* NS_FLAPPY_H */
