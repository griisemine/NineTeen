/*
 * ns_saisie_draw.h — le champ de saisie, dessiné.
 *
 * POURQUOI ICI ET PAS DANS `room_hud.c`
 * -------------------------------------
 * `room_hud.c` dessine l'état de LA SALLE : la borne à portée, le score, le
 * bandeau de réglages, le téléviseur du bar. Un champ de texte n'est l'état de
 * rien de tout ça, et l'écran qui s'en servira d'abord — le comptoir des
 * comptes et des salons annoncé par `ns_compte.h` — n'est pas le HUD de la
 * salle. Le poser là obligerait cet écran à dépendre de `room/` pour dessiner un
 * rectangle et un curseur.
 *
 * Ici, à côté de la couche 2D, parce que c'est elle qui porte la fonte et que
 * le seuil de lisibilité ci-dessous n'a de sens que par rapport à elle. Et dans
 * un fichier séparé de `ns_sprite.h`, parce que cet en-tête refuse les widgets
 * en toutes lettres — il a raison, et la bonne réponse à « pas de widgets dans
 * l'empileur de quads » est un fichier de plus, pas une exception.
 *
 * LE SEUIL DE LISIBILITÉ
 * ----------------------
 * Le dépôt en a un, relevé et appliqué dans `room_hud.c` : ONZE PIXELS de
 * hauteur de glyphe. En dessous, la fonte 5 x 7 devient la « bouillie grise »
 * que `ns_sprite.h` décrit. `ns_saisie_echelle_lisible` le fait respecter au
 * lieu de le rappeler : un champ où l'on tape son mot de passe est le dernier
 * endroit où l'on veut deviner ce qu'on a écrit.
 */
#ifndef NS_SAISIE_DRAW_H
#define NS_SAISIE_DRAW_H

#include "ns_saisie.h"
#include "ns_sprite.h"

/*
 * Le seuil, en pixels de hauteur de glyphe. Il n'est pas choisi ici : c'est
 * celui que `room_hud.c` a relevé et qu'il applique déjà au téléviseur du bar et
 * au tableau des records.
 */
#define NS_SAISIE_SEUIL_PX 11.0f

/*
 * L'échelle réellement employée pour une échelle demandée : la même, ou celle
 * qui atteint le seuil. À appeler AVANT de dimensionner le cadre — un cadre
 * calculé pour une échelle que le dessin va relever est un cadre trop petit.
 */
float ns_saisie_echelle_lisible(float demandee);

/*
 * LA CADENCE DU CURSEUR — une seconde de cycle, allumé les deux premiers tiers.
 *
 * Elle n'est pas choisie ici : c'est celle que `room_comptoir.c` applique déjà
 * à ses lignes de saisie, et deux curseurs qui clignotent à deux rythmes dans le
 * même jeu se remarquent tout de suite. Le seul endroit où le rythme se décide
 * est donc celui-ci, pour que le comptoir puisse s'y ramener d'une ligne.
 *
 * Ce que ces deux tiers achètent : le curseur passe plus de temps allumé
 * qu'éteint, donc un coup d'œil rapide le trouve, sans pour autant devenir un
 * trait fixe qu'on cesse de distinguer du texte.
 */
#define NS_SAISIE_CYCLE_MS  1000u
#define NS_SAISIE_ALLUME_MS  660u

typedef struct ns_saisie_cadre {
    /* Le cadre, dans le repère logique du lot en cours — celui qu'on a passé à
     * `ns_sprite_begin`, donc `ROOM_HUD_W` x `ROOM_HUD_H` pour un écran de la
     * salle. Pas des pixels : c'est ce qui garde la même taille relative d'un
     * portable à un écran de bureau. */
    float x, y, w, h;

    /* Le facteur de la fonte 5 x 7, relevé au seuil s'il est trop bas. */
    float echelle;

    /* Ce qu'on lit quand le champ est vide — « VOTRE PSEUDO », « CODE À SIX
     * LETTRES ». NULL pour rien. Sans elle, un champ vide ne dit pas ce qu'il
     * attend, et l'étiquette d'à côté doit tout porter. */
    const char *invite;

    /* Le champ qui a le focus. Lui seul montre un curseur, et son cadre est plus
     * clair : le clignotement laisse la moitié du temps sans curseur, et il faut
     * bien que le champ reste reconnaissable pendant cette moitié-là. */
    bool actif;

    /* Des secondes, qui font clignoter le curseur. N'importe quelle horloge
     * continue fait l'affaire ; `ns_time_seconds` en est une. */
    double temps;
} ns_saisie_cadre;

/*
 * Dessine dans le lot courant. Le texte plus long que le cadre DÉFILE pour
 * garder le curseur visible.
 */
void ns_saisie_dessiner(ns_sprite *s, const ns_saisie *champ, const ns_saisie_cadre *c);

#endif /* NS_SAISIE_DRAW_H */
