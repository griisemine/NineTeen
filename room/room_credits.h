/*
 * room_credits.h — l'écran de crédits, et la page des commandes.
 *
 * Pourquoi ce fichier existe : une obligation, pas une politesse
 * -------------------------------------------------------------
 * Le personnage du joueur est **CesiumMan**, (C) 2017 Cesium, sous **CC BY
 * 4.0**. Cette licence-là n'est pas CC0 : elle autorise l'emploi, y compris
 * commercial, À LA CONDITION que l'auteur soit crédité. `LICENSES.md` le dit
 * depuis le début — « elle doit l'être aussi partout où le jeu est distribué :
 * écran de crédits, page de téléchargement, archive ».
 *
 * L'archive était tenue : `room/CMakeLists.txt` y installe `LICENSES.md`.
 * L'ÉCRAN NE L'ÉTAIT PAS. Il n'y avait aucun écran de crédits dans le jeu, et
 * `grep -rin credit room/ engine/ games/` ne rendait rien. Un fichier Markdown
 * posé à côté d'un binaire est une attribution qu'aucun joueur ne lit ; ce
 * n'est pas la même chose qu'un écran, et la licence demande les deux.
 *
 * Ce n'est donc pas un écran « en plus ». C'est la condition à laquelle on a le
 * droit de se servir du personnage.
 *
 * Pourquoi les crédits sont des DONNÉES, et pas des appels de dessin en vrac
 * -------------------------------------------------------------------------
 * Parce qu'une obligation légale doit être VÉRIFIABLE, et qu'on ne vérifie pas
 * un appel de dessin. La table `room_credits_lines` est publique : une ligne
 * effacée par mégarde fait échouer `tests/test_menu.c`, qui exige que la
 * mention Cesium et « CC BY 4.0 » soient toutes deux présentes. C'est la règle
 * du dépôt — ce qui ne doit pas disparaître en silence casse quelque chose de
 * bruyant.
 *
 * La page des commandes, dans le même fichier
 * -------------------------------------------
 * Elle y est parce qu'elle a la même forme — une liste de paires, dessinée en
 * plein écran, sans état — et parce qu'elle solde le même genre de dette. En
 * arrivant dans le sas, un joueur ne voyait RIEN : ni titre, ni commande, ni
 * indication. Les touches n'étaient écrites que dans `--help` et dans
 * `docs/JOUER.md`, c'est-à-dire nulle part pour qui double-clique sur un
 * paquet. Un jeu dont les commandes ne sont dites que dans un terminal est un
 * jeu qui suppose qu'on l'a compilé.
 *
 * Ces deux pages ne possèdent rien et ne décident de rien : elles dessinent des
 * chaînes. C'est ce qui les rend vérifiables sans fenêtre.
 *
 * Les chaînes affichées sont en ASCII SANS ACCENT, et en majuscules. Ce n'est
 * pas une négligence : la fonte du jeu est un bitmap 5 x 7 qui couvre l'ASCII
 * imprimable et rien d'autre (`engine/sprite/ns_font5x7.h`). Un « é » n'y
 * dessine pas un accent, il dessine un trou — c'est déjà la règle que suit le
 * menu, qui affiche « QUALITE » et « ECHELLE DE RENDU ».
 */
#ifndef NS_ROOM_CREDITS_H
#define NS_ROOM_CREDITS_H

#include "ns_sprite.h"

#include <stdbool.h>

/*
 * Une ligne de crédit. `what` est ce qu'on a employé, `who` qui l'a fait et
 * sous quelle licence.
 *
 * La licence est dans la MÊME chaîne que l'auteur, délibérément : les séparer
 * en deux colonnes invite à en aligner une et à laisser l'autre déborder, et
 * c'est l'auteur ET la licence qui font l'attribution — pas l'un des deux.
 *
 * `heading` marque un intertitre : `who` y est nul et `what` porte le titre de
 * section. C'est un drapeau plutôt qu'un type séparé parce qu'une section n'est
 * qu'une ligne qu'on dessine autrement.
 */
typedef struct room_credit_line {
    const char *what;
    const char *who;
    bool        heading;
} room_credit_line;

/*
 * La table complète, terminée par une ligne dont `what` est nul.
 * `count` reçoit le nombre de lignes utiles s'il n'est pas nul.
 */
const room_credit_line *room_credits_lines(int *count);

/* Idem pour les commandes : `what` est la touche, `who` ce qu'elle fait. */
const room_credit_line *room_controls_lines(int *count);

/*
 * Dessine la page, en repère `ROOM_HUD_W` x `ROOM_HUD_H`, par-dessus ce qui est
 * déjà là. L'appelant possède le lot — c'est la règle de `room_hud` et de
 * `room_menu`, et elle vaut ici pour la même raison.
 */
void room_credits_draw(ns_sprite *s);
void room_controls_draw(ns_sprite *s);

/*
 * L'AIDE D'ARRIVÉE : les quelques commandes qui suffisent à entrer dans la
 * salle.
 *
 * Un bandeau discret, en bas, qui s'efface de lui-même. Il ne remplace pas la
 * page complète — il répond à la seule question qu'on se pose la première
 * minute, « qu'est-ce que je fais ». `alpha` vaut 0 à 1 et vient du minuteur de
 * l'appelant : le fondu est à lui, la mise en page est ici.
 */
void room_credits_draw_intro(ns_sprite *s, float alpha);

#endif /* NS_ROOM_CREDITS_H */
