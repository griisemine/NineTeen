/*
 * room_hud.h — ce que le joueur lit à l'écran.
 *
 * La dette que ce fichier solde
 * -----------------------------
 * Depuis A7, la logique de proximité fonctionne : `ns_scene_nearest_cabinet`
 * dit quelle borne est à portée, `E` déclenche le geste, le bras insère le
 * jeton. Et rien ne le disait au joueur. Le commentaire du code annonçait
 * « l'invite affichée à l'écran demande une couche 2D, qui n'existe pas
 * encore » — elle existe depuis B6. Ce fichier est ce qui manquait.
 *
 * Ce que ça change, et pourquoi ce n'est pas de la décoration : une interaction
 * qu'on ne devine pas n'existe pas. Un joueur qui s'approche d'une borne sans
 * rien voir s'en éloigne.
 *
 * Ce que ce fichier fait, et ce qu'il ne fait pas
 * ----------------------------------------------
 * Il DESSINE. Il ne décide de rien : ni de ce qui est à portée, ni de ce qui se
 * joue, ni de ce qui est réglé. Tout lui arrive en paramètre, ce qui le rend
 * testable sans GPU et surtout le garde à l'écart des états qu'il affiche — un
 * affichage qui possède une donnée finit par la contredire.
 *
 * Il n'y a pas de navigation. Le panneau de réglages s'affiche, il ne se
 * parcourt pas : `F7` et `F8` restent les commandes, et ce panneau dit ce
 * qu'elles ont fait. C'est moins qu'un menu et c'est honnête — un menu demande
 * une sélection, un focus, une remontée d'entrée, et rien de tout ça n'existe.
 */
#ifndef NS_ROOM_HUD_H
#define NS_ROOM_HUD_H

#include "ns_scene.h"
#include "ns_sprite.h"
#include "room_couperet.h"
#include "room_economie.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Le repère logique de l'affichage, en « points ».
 *
 * Fixe et non lié à la résolution : c'est ce qui fait qu'un texte garde la même
 * taille relative sur un portable et sur un écran de bureau. `ns_sprite_begin`
 * met ce repère à l'échelle de la cible.
 */
#define ROOM_HUD_W 1280.0f
#define ROOM_HUD_H 720.0f

typedef struct room_hud_state {
    /* La borne à portée, ou NULL. C'est `room_viewmodel_target` qui le dit. */
    const ns_cabinet *near;
    bool  can_interact;      /* faux pendant un geste : on ne relance pas */

    bool     playing;
    uint32_t score, best;
    bool     dead;

    /* Le bandeau de réglages, affiché quelques secondes après F7 ou F8. */
    float       settings_timer;      /* secondes restantes */
    const char *quality_name;
    float       render_scale;

    /* Le rang obtenu à la dernière partie, 0 si hors classement. Affiché sur
     * l'écran de fin : c'est la seule information qui donne envie de relancer. */
    uint32_t last_rank;

    /*
     * L'AIDE D'ARRIVÉE : secondes restantes, 0 pour ne rien afficher.
     *
     * Elle existe parce qu'on a REGARDÉ le jeu arriver. Au premier plan, le
     * joueur est dans un sas de neuf mètres, il voit deux mains, et l'écran ne
     * porte pas un mot : ni titre, ni commande, ni indication. Les touches
     * n'étaient dites que dans `--help` et dans `docs/JOUER.md` — c'est-à-dire
     * nulle part pour qui a téléchargé un paquet et double-cliqué dessus.
     *
     * Un minuteur plutôt qu'une condition sur le déplacement : la condition
     * demanderait à cet affichage de savoir où est le joueur, donc de posséder
     * un état de la salle, ce que ce fichier ne fait pour rien d'autre. Le
     * bandeau part de lui-même, la page complète reste dans `Échap`.
     */
    float intro_timer;

    /*
     * L'ÉCONOMIE — le solde, le lieu qu'on approche, et ce qui vient de se
     * passer.
     *
     * Tout arrive par POINTEUR sur ce que possède `room_economie`, et rien
     * n'est recopié ici : c'est la règle que ce fichier s'est donnée dès son
     * en-tête — « un affichage qui possède une donnée finit par la
     * contredire ». Un compteur de tickets recopié dans le HUD à chaque image
     * serait exactement ça, et il se désynchroniserait le jour où une image
     * sauterait.
     *
     * `eco` nul veut dire « ne rien afficher », ce qui est le cas de
     * `--no-hud` et des captures d'écran de décor.
     */
    const room_eco *eco;

    /*
     * Le LIEU à portée, et ce qui s'y fait.
     *
     * Deux des huit points d'intérêt déclarés par la salle sont maintenant
     * actionnables — `NS_POI_TOKENS` (le monnayeur) et `NS_POI_PRIZES` (la
     * vitrine à lots). `NS_POI_NONE` pour les autres et pour le vide : les six
     * qui restent — billard, canapé, bar, radio, toilettes, porte — n'ont rien
     * à proposer, et une invite qui s'allumerait devant un canapé apprendrait
     * au joueur à ne plus la lire.
     */
    ns_poi_kind poi;

    /* Le bandeau transitoire, tel que `room_eco_salle_message` le rend. Vide =
     * rien à dire. Le minuteur sert au fondu, comme pour les réglages. */
    const char *eco_message;
    float       eco_message_timer;
} room_hud_state;

/*
 * Dessine l'affichage par-dessus la scène. À appeler entre `ns_sprite_begin` et
 * `ns_sprite_end` — l'appelant possède le lot, parce qu'il peut vouloir y mettre
 * autre chose.
 */
void room_hud_draw(ns_sprite *s, const room_hud_state *st);

/*
 * L'écran de la borne de CLASSEMENT : les meilleurs scores locaux, dessinés
 * dans sa dalle comme Flappy l'est dans la sienne.
 *
 * C'est ce qui donne un sens au reste. Un score qu'on ne peut pas comparer ne
 * donne envie de rien ; affiché sur une borne qu'on croise en entrant, il donne
 * envie de reprendre la main. La borne affichait une image fixe de 2020.
 */
void room_hud_draw_leaderboard(ns_sprite *s, float w, float h, double time_seconds);

/*
 * LE TABLEAU DU BAR : le classement à gauche, les joueurs EN DIRECT à droite.
 *
 * Distinct de la dalle de la borne de classement, et pour une raison de forme
 * autant que de fond. La dalle fait 62 cm en 16:9 et se lit à deux mètres et
 * demi ; ce tableau-ci fait 1,70 x 0,85 m en 2:1, il est derrière le comptoir,
 * on le voit en entrant et de toute la salle. Il a la place de dire ce que la
 * dalle n'a pas la place de dire : QUI joue, à QUOI, et à COMBIEN il en est.
 *
 * Il portait `background_classement.png` — une image peinte de 2020 avec des
 * scores dessinés dessus. Le propriétaire l'a photographiée noire et a demandé
 * qu'elle « affiche les scores lives des joueurs et le classement ». C'est un
 * tableau de bar : sa raison d'être est qu'on lève les yeux et qu'on voie qui
 * est en train de battre quoi.
 *
 * Ce n'est PAS un tube : pas de courbure, pas de lignes de balayage. Une dalle
 * plate accrochée au mur d'un bar est un écran plat, et le traitement de tube
 * ne s'applique qu'aux dalles déclarées par une borne. Depuis que « ecran_bar »
 * est un meuble — patte, caisson, cadre, dalle — la phrase décrit enfin ce que
 * la salle contient : elle portait un panneau nu à 32,7 cm du mur.
 */

/*
 * LA CIBLE OU CE TABLEAU EST DESSINE, et pourquoi elle est ici plutôt que chez
 * son seul appelant : `room_hud_draw_scoreboard` rapporte toutes ses cotes à
 * 640 (`u = w / 640`). Les deux nombres doivent donc rester un MULTIPLE ENTIER
 * de ce repère, et les écrire à deux endroits est la façon la plus sûre de les
 * voir diverger. Le choix de 2560 x 1280 est mesuré dans `room/main.c`, à
 * l'endroit où la cible est créée.
 */
#define ROOM_BAR_RT_W 2560u
#define ROOM_BAR_RT_H 1280u

void room_hud_draw_scoreboard(ns_sprite *s, float w, float h, double time_seconds,
                              const char *my_name, const char *my_game,
                              uint32_t my_score);

/*
 * LA DALLE QUI DÉRAILLE, quand on cogne la borne.
 *
 * Pourquoi c'est ce qui fait la différence entre une animation et un jeu
 * ---------------------------------------------------------------------
 * Un coup de poing dont on ne voit que le bras est une animation. Un coup de
 * poing après lequel la MACHINE répond est un jeu — c'est ce que le
 * propriétaire a demandé quand il a dit « si on rage », et c'est la seule
 * moitié du geste qui parle de la borne plutôt que du joueur.
 *
 * Ce que ça dessine, et pourquoi ce n'est pas une secousse
 * --------------------------------------------------------
 * On ne peut pas DÉPLACER l'image : `ns_sprite_begin` ne prend qu'une taille
 * logique, sans origine, donc le seul décalage possible serait ancré sur le
 * coin haut-gauche — une image qui grandit depuis son coin, ce qui ne ressemble
 * à rien.
 *
 * On dessine donc ce qu'un tube fait VRAIMENT quand on le frappe : il perd sa
 * synchronisation verticale. Une barre sombre traverse l'image de bas en haut,
 * quelques lignes se déchirent, et l'ensemble blanchit brièvement — c'est le
 * « déraillement » qu'on reconnaît sans savoir le nommer. Trois aplats
 * translucides par image, aucune ressource, aucun shader.
 *
 * Ce que ça NE fait PAS, et ce qu'il faudrait pour le faire
 * ---------------------------------------------------------
 * Le vrai sursaut — l'image entière qui saute de quelques lignes dans son cadre
 * — demanderait un décalage d'UV côté matériau, c'est-à-dire un champ de plus
 * dans `ns_render_settings` ou dans l'uniforme `u_screen`. Les deux fichiers
 * qui le porteraient sont tenus par un autre chantier ; c'est écrit dans le
 * rapport, et ce qui est ici tient sans eux.
 *
 * `choc` va de 1 (à l'impact) à 0. `phase` est un temps en secondes, qui fait
 * descendre la barre : il est passé plutôt que dérivé de `choc` parce que deux
 * coups rapprochés remettent `choc` à 1 sans que la barre doive resauter au
 * même endroit.
 */
void room_hud_draw_choc(ns_sprite *s, float w, float h, float choc, float phase);

/* ==========================================================================
 * LE COUPERET — les deux surfaces du mode compétitif
 * ==========================================================================
 *
 * DEUX, et pas une, parce qu'elles répondent à deux questions posées à deux
 * distances différentes.
 *
 * `room_hud_draw_couperet` est par-dessus la vue : « qu'est-ce que je fais
 * MAINTENANT ». Le compte à rebours, qui est visé, ce que je subis, ce que je
 * peux acheter. Elle doit se lire sans quitter la borne des yeux, donc elle
 * tient sur deux bandes — une en haut, une en bas — et laisse le milieu libre.
 *
 * `room_hud_draw_arene` est sur LE TÉLÉVISEUR DU BAR : « où en est la salle ».
 * Le classement complet, les huit places, les camps. On le lit en levant la
 * tête, entre deux parties, à cinq mètres — c'est exactement ce pour quoi ce
 * panneau existe déjà, et c'est pour ça que le mode n'a eu besoin d'aucun
 * écran nouveau. Un tableau de bar dont la raison d'être est qu'on voie « qui
 * est en train de battre quoi » avait déjà la bonne place et la bonne taille.
 *
 * `moi` est la place du joueur local, `ROOM_CP_MAX_PLACES` s'il n'en a pas —
 * auquel cas rien n'est surligné plutôt qu'une ligne au hasard.
 */
void room_hud_draw_couperet(ns_sprite *s, const room_couperet *c, uint8_t moi,
                            uint8_t cible, double time_seconds);

/*
 * LE BROUILLAGE, dessiné DANS la dalle qu'on joue.
 *
 * Pas par-dessus la vue : ce n'est pas le joueur qu'on gêne, c'est SA BORNE.
 * Un voile sur tout l'écran se lirait comme un effet de caméra et gênerait
 * aussi le compte à rebours et les prix des actions — c'est-à-dire tout ce qui
 * permet de répondre à l'attaque. Mis dans la dalle, il ne gêne QUE la partie,
 * ce qui est exactement ce qu'il achète.
 *
 * Il emprunte la voie que `room_hud_draw_choc` a ouverte : dessiné après
 * `game_api->draw` et dans la même passe, donc il recouvre le jeu sans qu'un
 * shader ni une ressource de plus n'existe.
 *
 * `force` va de 0 à 1. `phase` est un temps en secondes : c'est lui qui fait
 * ramper les bandes, et il est passé plutôt que dérivé de `force` parce qu'un
 * second brouillage prolonge le premier sans que le motif doive resauter.
 */
void room_hud_draw_brouillage(ns_sprite *s, float w, float h, float force, float phase);
void room_hud_draw_arene(ns_sprite *s, float w, float h, const room_couperet *c,
                         uint8_t moi, double time_seconds);

#endif /* NS_ROOM_HUD_H */
