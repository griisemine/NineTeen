/*
 * room_menu.h — le menu de réglages, dessiné.
 *
 * Pourquoi il arrive maintenant, et pas avec B9
 * ---------------------------------------------
 * B9 a livré les réglages sans l'écran : `F7` change le palier de qualité, `F8`
 * l'échelle de rendu, et le choix est gardé d'une session à l'autre. C'était
 * défendable tant que la couche 2D n'existait pas — mais elle existe depuis B6,
 * et depuis B10 elle affiche déjà l'invite, le score et le classement. Deux
 * touches de fonction non documentées à l'écran, c'est un réglage que seuls
 * ceux qui lisent le dépôt savent utiliser.
 *
 * Ce qu'il règle, et ce qu'il ne règle pas
 * ----------------------------------------
 * Uniquement ce qui a déjà un lecteur : le palier, l'échelle de rendu, la
 * densité de poussière, l'exposition, les quatre volumes, les DEUX niveaux de la
 * salle (les pas, le fond continu), la sensibilité de la souris. Aucune entrée
 * n'est ajoutée « pour plus tard » — c'est exactement la faute que `ns_config.h`
 * documente à propos des deux clés réservées qui n'ont jamais rien piloté.
 *
 * Les deux niveaux de la salle passent par `room_sound_set_level` et non par le
 * contexte : ils vivent au niveau du MODULE `room_sound`, comme les volumes de
 * bus vivent au niveau de `ns_audio`. Un curseur de volume n'a pas besoin de
 * l'instance qui joue les sons — et cette instance n'existe pas encore quand le
 * menu se construit.
 *
 * Il ne connaît ni SDL, ni le RHI. L'appelant lui envoie des ACTIONS et relit
 * ensuite les drapeaux `*_dirty` pour appliquer ce qui a changé — c'est ce qui
 * permet de le dessiner et de le tester sans fenêtre.
 */
#ifndef NS_ROOM_MENU_H
#define NS_ROOM_MENU_H

#include "ns_render.h"
#include "ns_sprite.h"

#include <stdbool.h>

typedef enum room_menu_action {
    ROOM_MENU_UP = 0,
    ROOM_MENU_DOWN,
    ROOM_MENU_LEFT,
    ROOM_MENU_RIGHT,
    ROOM_MENU_ACCEPT,
    ROOM_MENU_CANCEL
} room_menu_action;

typedef struct room_menu {
    bool  open;
    int   cursor;
    float time;          /* secondes depuis l'ouverture : anime le curseur */

    /* Ce que l'appelant doit appliquer après un `room_menu_input`. Il les remet
     * à faux lui-même — le menu ne sait pas ce qu'appliquer coûte. */
    bool  render_dirty;  /* `ns_renderer_set_settings` à rappeler */
    bool  close_request;
    bool  quit_request;
} room_menu;

/*
 * Ce sur quoi le menu agit. Des pointeurs plutôt qu'une copie : le menu écrit
 * dans les valeurs vivantes, et il n'y a donc aucune synchronisation à tenir.
 * `mouse_sensitivity` est le multiplicateur utilisateur, pas la sensibilité
 * effective de la caméra — c'est lui qu'on persiste.
 */
typedef struct room_menu_ctx {
    ns_render_settings *rs;
    float              *mouse_sensitivity;
} room_menu_ctx;

void room_menu_open(room_menu *m);
void room_menu_close(room_menu *m);
void room_menu_input(room_menu *m, const room_menu_ctx *ctx, room_menu_action a);
void room_menu_update(room_menu *m, float dt);

/* Écrit dans `ns_config` tout ce que le menu pilote. À appeler à la fermeture et
 * à la sortie : `ns_config_save` reste au choix de l'appelant. */
void room_menu_persist(const room_menu_ctx *ctx);

/* Dessine dans le lot courant, en repère `ROOM_HUD_W` x `ROOM_HUD_H`. */
void room_menu_draw(ns_sprite *s, const room_menu *m, const room_menu_ctx *ctx);

#endif /* NS_ROOM_MENU_H */
