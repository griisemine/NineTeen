/*
 * room_poste.h — LE POSTE DE JEU : la vue s'approche de la dalle.
 *
 * Le défaut, mesuré
 * -----------------
 * On jouait à 8 % de l'écran. Debout sur l'ancre que la salle déclare, l'œil est
 * à 0,870 m du centre de la dalle ; celle-ci fait 0,5333 x 0,300 m, elle
 * sous-tend donc 34,1° de large et 19,6° de haut pour un champ vertical de 62°
 * (et 93,8° horizontal en 16/9) — soit 11,5 % de l'aire du cadre en géométrie
 * pure, et 8,1 % comptés sur la capture, la dalle étant inclinée de 20°.
 *
 * Le reste du cadre est occupé par le marquee, le sol, et les deux bornes
 * voisines. C'est joli et c'est le but — « on reste EN 3D, la tête reste
 * libre » — mais le JEU y est un timbre-poste.
 *
 * Ce que ce module fait, et ce qu'il ne fait pas
 * ---------------------------------------------
 * IL FAIT avancer le POINT DE VUE, et lui seul. Le corps ne bouge pas : même
 * capsule, même collision, même ancre, mêmes mains sur les commandes. C'est
 * exactement le partage que `room_camera` a établi pour la troisième personne —
 * « le joueur continue de se déplacer exactement comme avant, seul le POINT DE
 * VUE recule » — pris dans l'autre sens.
 *
 * IL NE FAIT PAS un mode de jeu. Il n'y a rien à verrouiller : la tête reste
 * libre, on peut regarder la borne d'à côté, et le premier pas de déplacement
 * rend la vue au corps.
 *
 * Les deux leviers, et pourquoi il en faut DEUX
 * --------------------------------------------
 * Approcher seul ne suffit pas. Pour que la dalle tienne 58 % de la hauteur du
 * cadre à 62° de champ, il faudrait mettre l'œil à 0,455 m du verre — dans
 * l'ouverture du bandeau, la tête à l'intérieur du meuble ; et pour 70 %, à
 * 0,377 m, c'est-à-dire le nez sur la vitre.
 *
 * On prend donc les DEUX leviers, et la table dit ce que chaque couple donne.
 * Elle est calculée en projetant les quatre coins de la dalle par la caméra
 * réelle — pas estimée, et pas lue sur une capture, où le bandeau et les
 * grilles de haut-parleurs empêchent de délimiter la dalle au pixel :
 *
 *     part  champ   distance   avance   part de largeur / hauteur   aire
 *      —      62°    0,870 m    0,000 m        29,7 / 28,2 %         8,4 %   (avant)
 *     0,50    50°    0,677 m    0,244 m        47,5 / 47,5 %        22,6 %
 *     0,54    48°    0,652 m    0,262 m        51,7 / 51,7 %        26,7 %
 *     0,58    46°    0,633 m    0,277 m        55,9 / 55,9 %        31,2 %   (livré)
 *     0,66    42°    0,608 m    0,297 m        64,3 / 64,3 %        41,3 %
 *     0,72    38°    0,616 m    0,290 m        70,7 / 70,7 %        50,0 %
 *
 * Le réglage livré multiplie par 3,7 l'aire occupée par le jeu. Au-delà, la
 * salle disparaît : à 0,72 la capture ne montre plus que le jonc du cadre, et
 * l'idée qu'on tenait — jouer SANS quitter la salle — est perdue. Les deux
 * chiffres sont dans `nineteen.env`, en clair, avec cette table : c'est un
 * arbitrage de mise en scène, il se règle sans recompiler et il se discute.
 *
 * Le second levier n'est PAS physique et il faut le dire : resserrer le champ
 * est un choix de lisibilité, celui que fait tout jeu qui vise. Il est déclaré,
 * il est réversible, et il ne touche qu'à la caméra de rendu.
 *
 * La distance n'est pas écrite : elle est CALCULÉE
 * -----------------------------------------------
 * On déclare la part de hauteur voulue et le champ du poste ; la distance en
 * découle, par dalle :
 *
 *     d = (hauteur_dalle / 2) / tan(part * champ / 2)
 *
 * C'est ce qui fait qu'une borne à plus grande dalle se règle toute seule, et
 * c'est la même discipline que le reste du dépôt : une règle, pas un nombre.
 */
#ifndef NS_ROOM_POSTE_H
#define NS_ROOM_POSTE_H

#include "ns_math.h"
#include "ns_scene.h"

#include <stdbool.h>

typedef struct room_poste {
    bool  pris;              /* la vue est demandée au poste */
    ns_v3 oeil;              /* l'œil VOULU, en monde */
    float tangage;           /* le tangage qui met la dalle au centre, DEPUIS `oeil` */
    float fov;               /* champ vertical au poste, degrés */

    /*
     * La part établie, simulée au pas fixe et interpolée au rendu — comme tout
     * le reste. `prev_part` existe pour la même raison que `prev_position` de la
     * caméra : une valeur lue par image d'affichage et calculée par pas de
     * simulation saccade, et ça se voit d'autant plus qu'ici elle déplace la vue.
     */
    float part, prev_part;
} room_poste;

void room_poste_init(room_poste *p);

/*
 * Demande le poste devant `cab`. Rend false — sans rien changer — si la borne
 * ne déclare pas de dalle exploitable : le jeu se joue alors comme avant, ce qui
 * est le comportement de la salle de 2020, qui n'a ni cotes de dalle ni normale.
 *
 * `oeil_corps` est l'œil du CORPS : il sert à borner l'avancée, jamais à la
 * définir. On ne s'approche pas plus près que le poste calculé, et on ne
 * s'éloigne jamais — une borne dont on serait déjà plus près que le poste (ce
 * qui n'arrive pas sur l'ancre déclarée, mais arriverait sous `--pos=`) ne doit
 * pas faire RECULER la vue.
 */
bool room_poste_prendre(room_poste *p, const ns_cabinet *cab, ns_v3 oeil_corps);

/* Rend la vue au corps. Sans effet si elle y est déjà. */
void room_poste_lacher(room_poste *p);

/* Avance l'établissement d'un pas fixe. Deux vitesses, et elles diffèrent :
 * se pencher vers une machine est plus lent que se redresser. */
void room_poste_tick(room_poste *p, float dt);

/* La part établie à l'instant `alpha` : 0 au corps, 1 au poste. */
float room_poste_part(const room_poste *p, float alpha);

/* L'œil à employer POUR LE RENDU, `oeil_corps` étant celui du corps. */
ns_v3 room_poste_oeil(const room_poste *p, ns_v3 oeil_corps, float alpha);

/* Le champ vertical à employer, `fov_corps` étant celui de la caméra libre. */
float room_poste_fov(const room_poste *p, float fov_corps, float alpha);

/*
 * Le tangage qui met la dalle au centre DEPUIS LE POSTE, en radians.
 *
 * Il n'est pas le même que depuis le corps, et l'ignorer était le piège : sur
 * `borne_arcade_1` la dalle est à −31,0° vue de l'ancre et à −19,9° vue du
 * poste. Poser l'un puis avancer vers l'autre fait dériver l'image vers le haut
 * du cadre pendant toute la transition — c'est-à-dire précisément pendant qu'on
 * la regarde.
 */
float room_poste_tangage(const room_poste *p);

/*
 * L'œil du poste AU PAS DE SIMULATION, sans interpolation de rendu : `courant`
 * vrai pour le pas qui vient d'être calculé, faux pour le précédent.
 *
 * Existe pour les bras. Ils sont posés depuis une `room_camera` dont
 * `room_viewmodel_pose` interpole lui-même les deux pas ; lui donner un œil déjà
 * interpolé le ferait interpoler deux fois, et les bras dériveraient d'une image
 * à chaque changement de vitesse. On lui donne donc les DEUX pas et on le laisse
 * faire son travail.
 */
ns_v3 room_poste_oeil_pas(const room_poste *p, ns_v3 oeil_pas, bool courant);

/* Les réglages du poste, relus dans `nineteen.env`. À appeler une fois au
 * démarrage, avant le premier `room_poste_prendre`. */
void room_poste_read_env(void);

#endif /* NS_ROOM_POSTE_H */
