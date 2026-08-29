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

/*
 * Ce que le menu MONTRE. Trois pages, une seule à la fois.
 *
 * Les crédits et les commandes sont des SOUS-PAGES du menu, et non des touches
 * à part. C'est le seul endroit qu'un joueur cherche quand il veut savoir
 * quelque chose, et c'est aussi ce qui évite d'inventer une touche de plus dont
 * personne ne saurait qu'elle existe — le défaut exact que ce menu a été écrit
 * pour corriger avec `F7` et `F8`.
 *
 * Conséquence de forme, et elle vaut d'être dite : `main.c` n'a rien à savoir
 * de ces pages. Il envoie les mêmes six actions, le menu décide de ce qu'elles
 * veulent dire selon la page ouverte. Ajouter une page ne touche donc pas la
 * boucle de jeu.
 */
typedef enum room_menu_page {
    ROOM_MENU_PAGE_SETTINGS = 0,
    ROOM_MENU_PAGE_CONTROLS,
    ROOM_MENU_PAGE_CREDITS
} room_menu_page;

typedef struct room_menu {
    bool  open;
    int   cursor;
    float time;          /* secondes depuis l'ouverture : anime le curseur */
    room_menu_page page; /* la sous-page ouverte, réglages par défaut */

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
    /*
     * L'interrupteur du TEMPS RÉEL — présence dans la salle et duels.
     *
     * Le menu écrit ce booléen et le persiste ; il ne démarre ni n'arrête le
     * fil réseau, et le changement prend effet au prochain lancement. C'est la
     * règle que ce fichier tient depuis le début — il DESSINE et il règle, il
     * ne possède aucun sous-système — et c'est ce qui le garde vérifiable sans
     * fenêtre, ce que `tests/test_menu.c` exploite.
     */
    bool               *realtime;
} room_menu_ctx;

void room_menu_open(room_menu *m);
void room_menu_close(room_menu *m);
void room_menu_input(room_menu *m, const room_menu_ctx *ctx, room_menu_action a);
void room_menu_update(room_menu *m, float dt);

/* Écrit dans `ns_config` tout ce que le menu pilote. À appeler à la fermeture et
 * à la sortie : `ns_config_save` reste au choix de l'appelant. */
void room_menu_persist(const room_menu_ctx *ctx);

/*
 * L'indice d'une ligne, par son LIBELLÉ.
 *
 * Existe pour les tests, et il existe parce que l'arithmétique sur le nombre de
 * lignes est un piège : `test_menu.c` visait « PAS » par `nombre de lignes - 5`,
 * en s'appuyant sur le fait que quatre lignes la suivaient. Le jour où une ligne
 * « TEMPS REEL » s'est insérée entre SOURIS et REPRENDRE, le test a continué de
 * tourner — sur la MAUVAISE ligne. C'est le pire des cas : ni rouge franc, ni
 * vert honnête.
 *
 * Un joueur ne compte pas les lignes en partant de la fin ; il lit celle qu'il
 * veut. Renvoie -1 si le libellé n'existe pas, ce qui casse le test bruyamment
 * — un libellé renommé DOIT casser quelque chose.
 */
int room_menu_row(const char *label);

/* Dessine dans le lot courant, en repère `ROOM_HUD_W` x `ROOM_HUD_H`. */
void room_menu_draw(ns_sprite *s, const room_menu *m, const room_menu_ctx *ctx);

#endif /* NS_ROOM_MENU_H */
