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
 * demi ; ce tableau-ci fait 1,80 x 0,90 m en 2:1, il est derrière le comptoir,
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
 * ne s'applique qu'aux dalles déclarées par une borne.
 */
void room_hud_draw_scoreboard(ns_sprite *s, float w, float h, double time_seconds,
                              const char *my_name, const char *my_game,
                              uint32_t my_score);

#endif /* NS_ROOM_HUD_H */
