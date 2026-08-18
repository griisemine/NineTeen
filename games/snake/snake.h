/*
 * snake.h — Snake, porté depuis les 1 802 lignes de 2020.
 *
 * Ce n'est PAS le Snake à cases
 * -----------------------------
 * C'est un serpent à **angle libre** qui tourne tant qu'on tient la direction,
 * avec de la **digestion** — la bosse d'un fruit avalé descend visiblement le
 * corps — et une économie de **trente-deux objets** dont chacun a sa propre
 * accélération, sa taille, sa probabilité, son score et son temps de digestion.
 * Confondre les deux et livrer un Snake à cases aurait été livrer un autre jeu.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * La table `FRUIT_PROPRIETES` des 25 fruits et 7 bonus, intégralement, dans son
 * ordre d'origine. Les cotes du terrain (1728 x 972 dans une fenêtre
 * 1920 x 1080), le rayon du corps, la mémoire des 100 positions passées, le
 * seuil d'allongement de 6,6, la hitbox de mort à 35, les 8 secondes
 * d'invincibilité, et le découpage des planches au pixel près.
 *
 * Ce qui change, et pourquoi
 * --------------------------
 * **Le temps.** L'original compte en images à 30 Hz : `TURN_AMMOUNT 0.13` est
 * une rotation *par image*, `FRUIT_TTL` un nombre *d'images*. Rejouer ça au pas
 * fixe du moteur donnerait un jeu dont le comportement dépend de la fréquence
 * d'affichage — le défaut qu'on a passé le projet à retirer. Chaque constante
 * est donc convertie en secondes, et sa valeur d'origine est écrite à côté.
 *
 * **Le mouvement reste décomposé en pas de 5 px**, comme dans l'original, parce
 * que ce n'est pas un détail d'implémentation : c'est ce qui donne sa cadence à
 * la digestion (`shiftRadius` une fois par pas, deux fois un pas sur deux) et
 * donc la vitesse à laquelle la bosse descend le corps. Un accumulateur de
 * distance rend ça indépendant de la fréquence de tick sans rien changer au
 * résultat.
 *
 * **Le hardcore est l'inverse du normal**, et c'est voulu par l'auteur :
 * `RATIO_GET_FRUIT_HARDCORE` vaut **−5**, donc manger COÛTE cinq fois la valeur
 * du fruit, et le score vient des fruits qu'on laisse **expirer**. C'est la
 * règle la plus surprenante de 2020 ; elle est conservée, et c'est elle qui a
 * obligé le serveur à accepter des valeurs négatives (voir `runs.go`).
 *
 * Aucun appel SDL hors `snake_art_load`, aucun état global, `snake_tick` à pas
 * constant : la partie est une structure qu'on avance. C'est ce qui la rend
 * rejouable à la graine près — donc scriptable, vérifiable sans écran, et
 * soumettable au serveur sous forme de journal signé.
 */
#ifndef NS_SNAKE_H
#define NS_SNAKE_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

/* Le terrain, et la fenêtre autour. PLAYGROUND_SIZE_W/H et BASE_WINDOW_W/H de
 * `legacy/include/define.h`. Toute la logique vit dans le repère du TERRAIN ;
 * l'affichage le centre dans la fenêtre. */
#define SNAKE_PLAY_W 1728.0f
#define SNAKE_PLAY_H  972.0f
#define SNAKE_W      1920.0f
#define SNAKE_H      1080.0f

/* NB_FRUITS 25 + NB_BONUSES 7. L'ordre est celui de l'énumération `FRUITS`, et
 * c'est aussi l'ordre des tuiles de `fruits.png`. */
#define SNAKE_ITEMS 32

/* REMIND_BODY : la mémoire des positions libérées par la queue, dans laquelle
 * on repêche où poser les segments quand le serpent s'allonge. */
#define SNAKE_REMIND 100

/* SIZE_PRE_RADIUS : la longueur du tuyau de digestion. Les 31 premières cases du
 * corps n'ont pas de position — elles ne portent qu'un rayon, qui descend d'un
 * cran à chaque pas jusqu'à atteindre la tête. C'est ce délai qui fait qu'on
 * VOIT le fruit descendre. */
#define SNAKE_PRE 31

/* De quoi tenir un serpent très long sans jamais allouer en cours de partie :
 * une partie de dix minutes en fait quelques centaines. */
#define SNAKE_MAX_PARTS 2048
#define SNAKE_MAX_FRUITS 24
#define SNAKE_MAX_DEAD 256
#define SNAKE_MAX_POPUPS 24

typedef enum snake_phase {
    SNAKE_READY = 0,   /* le serpent avance déjà, mais rien ne peut le tuer */
    SNAKE_PLAYING,
    SNAKE_DEAD
} snake_phase;

typedef struct snake_part {
    float x, y;
    float radius;      /* le supplément de rayon dû à la digestion */
} snake_part;

typedef struct snake_fruit {
    float    x, y;
    float    dest_x, dest_y;   /* les bonus glissent vers leur place */
    int      id;               /* -1 = case libre */
    bool     giant;
    bool     hit;
    float    age;              /* secondes depuis l'apparition */
    float    coef_radius;
} snake_fruit;

typedef struct snake_dead {
    float x, y, radius;
    float age;
} snake_dead;

/* Un score qui s'envole, comme dans l'original. */
typedef struct snake_popup {
    float   x, y;
    int64_t value;
    float   age;
    float   size;
} snake_popup;

typedef struct snake {
    snake_phase phase;

    /* Le corps. `part[SNAKE_PRE]` est la TÊTE ; les cases 0..30 n'ont pas de
     * position, elles ne portent que le rayon en cours de digestion. */
    snake_part part[SNAKE_MAX_PARTS];
    uint32_t   parts;

    float  angle;          /* radians, 3π/2 au départ = vers le haut */
    float  speed;          /* pixels par image à 30 Hz, comme l'original */
    float  step_carry;     /* distance pas encore consommée */
    int    step_parity;    /* le pas sur deux qui double la digestion */
    float  radius_left;    /* le rayon accumulé en queue, seuil 6,6 */

    /* Les positions que la queue a libérées, où l'on repose les segments quand
     * le serpent s'allonge. */
    float  past_x[SNAKE_REMIND], past_y[SNAKE_REMIND];

    snake_fruit fruit[SNAKE_MAX_FRUITS];
    uint32_t    fruit_slots;      /* nbFruits : le nombre de cases ouvertes */
    snake_fruit bonus;

    snake_dead  dead[SNAKE_MAX_DEAD];
    uint32_t    dead_count;

    snake_popup popup[SNAKE_MAX_POPUPS];
    uint32_t    popup_count;

    /* Compteurs de 2020, en secondes plutôt qu'en images. */
    float  invincible;      /* NB_FRAME_INVINCIBILITY, 8 s */
    float  fruit_timer;     /* prochaine tentative d'apparition */
    float  bonus_timer;
    float  poisoned;        /* la potion verte, qui fait grossir tout seul */
    float  coffee;          /* l'accélération du café */
    float  hardcore_rate;   /* la cadence d'apparition, qui s'accélère */
    float  fruits_eaten;    /* nbFruitEaten : débloque les objets rares */
    int    potions;         /* les potions de hitbox en réserve */

    float  time;            /* secondes de jeu écoulées */
    float  dead_time;
    float  gauge;           /* la jauge de droite, 0..1 */
    float  score_shown;     /* le score affiché, qui rattrape le vrai */

    int64_t  score;         /* signé : en hardcore il descend */
    uint32_t best;
    bool     hard;
    bool     turning_left, turning_right, accelerating;

    ns_rng rng;

    /* Événements de l'image, CONSOMMÉS par `snake_events` — voir games.h : un
     * drapeau levé par `press` avant la boucle de pas fixe ne doit pas être
     * effacé par le premier `tick`. */
    bool    turned, ate, died;
    int64_t ate_value;      /* signé, et c'est le point de tout le hardcore */
    bool    ate_is_bonus;
} snake;

/* Les planches de 2020, chargées une fois. */
typedef struct snake_art {
    ns_texture background, hud, body, fruits, anim, digits, basket;
    bool ready;
} snake_art;

bool snake_art_load(ns_rhi *r, snake_art *a);
void snake_art_free(ns_rhi *r, snake_art *a);

void snake_reset(snake *g, uint64_t seed, bool hard);

/* Tourner se MAINTIENT — c'est ce qui distingue ce Snake du Snake à cases, et
 * c'est pour lui que `ns_game_api` a gagné son crochet `hold`. */
void snake_hold(snake *g, bool left, bool right, bool accelerate);

/* Avance d'un pas FIXE. `dt` doit être constant d'un appel à l'autre. */
void snake_tick(snake *g, float dt);

void snake_draw(ns_sprite *s, const snake *g, const snake_art *a,
                float logical_w, float logical_h);

/* Un joueur automatique : il vise le fruit le plus proche et évite les murs. Il
 * ne prouve pas que le jeu est amusant — il prouve qu'on peut enchaîner des
 * milliers de pas sans NaN, que le score monte, et que la mémoire des positions
 * ne dérive pas. */
bool snake_autopilot(snake *g);

/* La même chose, vue par `room/` : voir `games/games.h`. */
extern const ns_game_api g_snake_api;

#endif /* NS_SNAKE_H */
