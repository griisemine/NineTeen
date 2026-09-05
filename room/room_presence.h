/*
 * room_presence.h — donner un CORPS aux autres joueurs.
 *
 * Le défaut qu'il corrige
 * -----------------------
 * La présence marchait déjà de bout en bout : chacun publiait sa position à
 * 4 Hz, le serveur la redistribuait, et `draw_presence` peignait chaque pair
 * comme une ÉTIQUETTE 2D flottant à hauteur de tête. C'est tout ce qu'il y
 * avait. On ne CROISE pas une étiquette : elle n'a pas de dos, elle ne marche
 * pas, elle ne passe pas derrière une borne, et deux joueurs qui se suivent dans
 * l'allée ne se voient pas se suivre. Ce fichier transforme les positions reçues
 * en corps articulés — le même `ns_skin` que le joueur en troisième personne.
 *
 * Pourquoi c'est dans `room/` et pas dans `engine/`
 * ------------------------------------------------
 * Parce que c'est de la PRÉSENTATION, pas du transport : rien ici n'ouvre de
 * socket, ne connaît le serveur, ni ne touche aux deux verrous du temps réel.
 * C'est aussi ce qui le rend vérifiable — `tests/test_presence.c` le compile
 * directement, sans GPU, sans fichier et sans réseau, exactement comme
 * `room_camera.c` et `room_door.c` le sont déjà.
 *
 * CE QUI EST INTERPOLÉ, ET CE QUI EST DÉDUIT
 * ------------------------------------------
 * Trois choses arrivent du réseau : une position, un cap, une identité. Une
 * quatrième n'arrive PAS et ne doit pas arriver : la phase du cycle de marche.
 *
 *   * LA POSITION s'INTERPOLE, jamais ne s'extrapole. Les positions tombent
 *     toutes les 250 ms et le rendu tourne à 60 images/s : entre deux positions
 *     connues il faut en fabriquer une quinzaine. Extrapoler donnerait un corps
 *     qui continue tout droit puis RECULE quand la position réelle arrive — le
 *     défaut classique, et il est le plus voyant précisément quand un joueur
 *     s'arrête, c'est-à-dire devant une borne, c'est-à-dire là où on le regarde.
 *     Le prix est un RETARD : le corps montré est celui d'il y a une période,
 *     soit 250 ms nominales — le détail du calcul est sur
 *     `room_presence_step`.
 *
 *   * LE CAP vient du réseau quand le pair le publie. Quand il ne le publie pas
 *     — une version antérieure au champ — il est DÉDUIT du déplacement. Un
 *     personnage qui glisse de côté sans tourner est pire qu'une étiquette : il
 *     dit au joueur que quelque chose est cassé.
 *
 *   * LA PHASE DE MARCHE est déduite de la DISTANCE PARCOURUE, et c'est le seul
 *     moyen que les pieds ne patinent pas. La publier serait à la fois plus cher
 *     (un flottant de plus, quatre fois par seconde et par joueur) et FAUX :
 *     entre deux positions reçues le corps montré parcourt la distance
 *     interpolée, pas la distance réelle, et une phase venue du réseau
 *     décrirait la seconde. C'est le même raisonnement que pour le joueur
 *     lui-même — voir la pose du personnage dans `room/main.c` — appliqué à un
 *     corps dont on ne connaît la vitesse qu'après coup.
 *
 * LES PAIRS NE SONT PAS DES OBSTACLES, et c'est délibéré
 * ------------------------------------------------------
 * Aucune collision n'est déclarée ici, et il ne faut pas en ajouter. La raison
 * est le RETARD mesuré ci-dessus : le corps qu'on voit est celui d'il y a 250 ms
 * plus le trajet réseau. Faire de ce corps un mur donnerait un joueur bloqué par
 * quelqu'un qui n'est plus là, et — bien pire — poussé par un pair dont la
 * position saute d'un battement à l'autre sur une liaison qui hoquette. Le duel
 * lui-même est EN DIFFÉRÉ dans ce projet, pour la même raison de fond : ce dépôt
 * ne s'appuie pas sur des positions distantes pour décider de quelque chose qui
 * se voit. On se traverse donc, comme dans toutes les salles où la présence est
 * décorative, et c'est écrit ici plutôt que découvert.
 */
#ifndef ROOM_PRESENCE_H
#define ROOM_PRESENCE_H

#include "ns_math.h"
#include "ns_realtime.h"

#include <stdbool.h>
#include <stdint.h>

/* Autant de corps que le réseau peut rapporter de pairs. Ce n'est pas un
 * plafond de rendu : `NS_MAX_CHARACTERS` vaut ce nombre plus le joueur, et le
 * commentaire qui l'accompagne mesure ce que ça coûte. */
#define ROOM_PRESENCE_MAX NS_RT_MAX_PEERS

/*
 * LA BORNE DE VRAISEMBLANCE D'UNE COORDONNÉE, et elle se calcule.
 *
 * L'emprise de circulation déclarée par la salle mesure 18,97 x 19,68 m, soit
 * 27,3 m de diagonale. 4 096 m, c'est cent cinquante fois cette diagonale : on
 * n'écarte donc aucune position que le jeu puisse produire, même en repoussant
 * un jour les murs.
 *
 * Le choix de la valeur HAUTE, lui, vient du flottant et non du décor. À
 * 4 096 m — une puissance de deux exacte — l'écart entre deux `float`
 * consécutifs vaut 2^-11, c'est-à-dire un demi-millimètre : une interpolation y
 * garde tout son sens. À 10^9, cet écart vaut 64 MÈTRES ; interpoler entre deux
 * telles positions ne produit plus que du bruit, et une distance parcourue
 * calculée dessus ferait tourner les jambes n'importe comment. La borne sépare
 * donc les positions sur lesquelles on sait calculer de celles sur lesquelles on
 * ne sait pas — ce qui est une raison, pas un chiffre rond.
 */
#define ROOM_PRESENCE_MAX_COORD 4096.0f

/*
 * EN DESSOUS DE QUOI UN PAIR EST À L'ARRÊT, en m/s.
 *
 * Mesuré sur le bruit du transport, pas choisi. Les coordonnées voyagent en
 * `%.3f` — le millimètre est le pas de quantification — et un battement dure
 * 250 ms : un pair parfaitement immobile peut donc « bouger » d'au plus 1 mm par
 * battement, soit 4 mm/s. Le seuil est à douze fois ce bruit, et à moins d'un
 * trentième de la marche (1,4 m/s) : il ne peut ni s'allumer sur du bruit, ni
 * s'éteindre sur un joueur qui marche.
 */
#define ROOM_PRESENCE_WALK_MIN 0.05f

/*
 * Ce que le module a besoin de savoir du personnage — et rien de plus.
 *
 * Des flottants et pas un `ns_skin *` : ce fichier ne pose aucun squelette, il
 * dit seulement À QUEL INSTANT du cycle chaque corps doit être échantillonné.
 * C'est ce qui lui permet d'être vérifié sans charger le modèle, donc sans
 * fichier ni GPU — et c'est `room/main.c` qui remplit ces cinq valeurs depuis
 * les mesures que `ns_skin` a prises sur le modèle réel.
 */
typedef struct room_presence_config {
    float stride;       /* la foulée, en mètres par cycle complet */
    float cycle;        /* la durée du cycle d'animation, en secondes */
    float stand_time;   /* l'instant du cycle où le personnage est debout, en s */
    float height;       /* la hauteur du personnage debout, en mètres */
    /*
     * La hauteur d'œil à SUPPOSER quand un pair ne publie pas la sienne.
     *
     * Un pair d'une version antérieure au champ `eye` publie une position d'œil
     * sans dire à quelle hauteur elle est. Sans repli, son corps flotterait à
     * hauteur de regard, pieds dans le vide. Avec, il est posé au sol tant qu'il
     * reste debout — et enfoncé de la différence s'il s'accroupit, ce qu'aucune
     * constante ne peut éviter et ce que le champ `eye` corrige dès que les deux
     * côtés l'ont.
     */
    float eye_default;
} room_presence_config;

/*
 * Un corps, prêt à poser : tout ce dont `room/main.c` a besoin pour remplir un
 * `ns_character_draw` et une étiquette, sans rien recalculer.
 */
typedef struct room_presence_body {
    char    name[NS_RT_NAME];
    char    game[NS_RT_SLUG];
    bool    verified;
    int32_t score;

    ns_v3   feet;        /* les PIEDS, au sol, dans le repère de la salle */
    float   yaw;         /* le cap rendu, en radians, convention du jeu */
    float   cycle_time;  /* l'instant du cycle à échantillonner, en secondes */
    float   speed;       /* la vitesse rendue, en m/s — sert au journal et aux tests */
    float   opacity;     /* 0 invisible, 1 plein : c'est le fondu d'entrée et de sortie */
    float   label_y;     /* la hauteur MONDE de l'étiquette, au-dessus de la tête */
} room_presence_body;

/*
 * L'état d'un pair suivi d'un battement au suivant.
 *
 * Exposé plutôt qu'opaque, comme `room_camera` et `room_door` : la structure
 * entière se pose sur la pile de l'appelant, il n'y a rien à allouer ni à
 * libérer, et un test peut regarder ce qu'il vérifie.
 */
typedef struct room_presence_track {
    bool     used;
    char     id[NS_RT_ID];      /* la clé de suivi : voir `ns_realtime_peer.id` */

    /* Le segment en cours d'interpolation, et ses deux dates. */
    ns_v3    from, to;
    uint64_t from_ms, to_ms;
    float    yaw_from, yaw_to;
    bool     has_yaw;           /* le pair publie-t-il vraiment un cap */

    ns_v3    shown;             /* la position rendue à l'image précédente */
    bool     shown_ok;
    float    phase;             /* la position dans le cycle, dans [0,1) */
    float    yaw_shown;         /* le cap rendu, amorti */
    bool     yaw_shown_ok;

    uint64_t born_ms;           /* première apparition : sert au fondu d'entrée */
    uint64_t last_ms;           /* dernier lot où ce pair figurait : au fondu de sortie */

    /* La part d'affichage, recopiée du dernier lot. */
    char     name[NS_RT_NAME];
    char     game[NS_RT_SLUG];
    bool     verified;
    int32_t  score;
    float    eye;
} room_presence_track;

typedef struct room_presence {
    room_presence_config cfg;
    /*
     * La date du dernier lot INTÉGRÉ.
     *
     * `ns_realtime_peers` rend la même table tant qu'aucune réponse n'est
     * arrivée, et la boucle de jeu la relit à chaque image. Sans cette date, un
     * lot serait intégré soixante fois par seconde : `from` et `to` finiraient
     * égaux, l'interpolation n'aurait plus rien à interpoler et les corps
     * sauteraient de 35 cm quatre fois par seconde. C'est elle qui fait de
     * `room_presence_sample` un appel idempotent, appelable à chaque image.
     */
    uint64_t last_batch_ms;

    room_presence_track track[ROOM_PRESENCE_MAX];

    uint32_t            body_count;
    room_presence_body  body[ROOM_PRESENCE_MAX];
} room_presence;

/* Remet tout à zéro et retient la configuration. `cfg` nul : des valeurs
 * inertes, et aucun corps ne sortira jamais — c'est ce qu'on veut d'un appelant
 * qui n'a pas réussi à charger le personnage. */
void room_presence_init(room_presence *pr, const room_presence_config *cfg);

/*
 * Intègre un lot de pairs daté `at_ms`, en millisecondes depuis le démarrage.
 *
 * Une date NULLE veut dire « rien reçu » — c'est ce que rend `ns_realtime_peers`
 * tant qu'aucune réponse n'est arrivée — et ne fait rien.
 *
 * Appelable à chaque image : un lot déjà vu — même `at_ms` — ne fait rien. Les
 * pairs absents du lot ne sont PAS supprimés, ils cessent seulement d'être
 * rafraîchis ; c'est `room_presence_step` qui les fait disparaître en fondu au
 * bout de `NS_RT_STALE_MS`.
 *
 * Les échantillons ABSURDES sont refusés un par un, pas en bloc : une
 * coordonnée non finie ou hors de `ROOM_PRESENCE_MAX_COORD` laisse le pair sur
 * son segment précédent, plutôt que de l'envoyer à l'infini ou de faire
 * disparaître les quinze autres avec lui.
 */
void room_presence_sample(room_presence *pr, const ns_realtime_peer *peers,
                          uint32_t count, uint64_t at_ms);

/*
 * Fait avancer les corps jusqu'à `now_ms` et remplit `pr->body`. Renvoie leur
 * nombre.
 *
 * `dt` est le pas d'AFFICHAGE en secondes — celui de la boucle de rendu, pas
 * celui de la simulation : tout ce qui se calcule ici (l'amorti du cap, le
 * retour à la pose debout) vit à la cadence de l'écran.
 */
uint32_t room_presence_step(room_presence *pr, uint64_t now_ms, float dt);

/*
 * LA SALLE PEUPLÉE SANS SERVEUR — et pourquoi ça existe.
 *
 * Fabrique `count` pairs qui marchent dans l'allée centrale, à la date `t`.
 * C'est une SOURCE d'échantillons, à passer à `room_presence_sample` comme s'ils
 * venaient du réseau : rien ici ne contourne les deux verrous du temps réel, et
 * `ns_realtime` n'en sait rien.
 *
 * À quoi ça sert : montrer et vérifier le rendu des corps demande trois ou
 * quatre joueurs dans la salle en même temps, ce qu'aucune machine
 * d'intégration continue ne saura fournir et ce qu'une capture ne peut pas
 * attendre. Le drapeau qui l'allume (`--pairs-demo=`) est INERTE par défaut,
 * comme tout le reste du temps réel.
 *
 * Les pairs fabriqués publient leur cap SAUF UN : celui d'indice 1 ne le publie
 * pas, et se retrouve donc orienté par son déplacement. C'est le chemin de
 * compatibilité, et on ne veut pas qu'il ne soit emprunté que par un test.
 */
uint32_t room_presence_demo(ns_realtime_peer *out, uint32_t max,
                            uint32_t count, double t_seconds);

#endif /* ROOM_PRESENCE_H */
