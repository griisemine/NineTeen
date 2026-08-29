/*
 * ns_skin.h — un personnage ARTICULÉ : maillage pesé, squelette, animation.
 *
 * Ce que c'est, et ce que ce n'est pas
 * ------------------------------------
 * C'est le seul endroit du moteur où une géométrie est déformée par un
 * squelette. Tout le reste de la salle est cuit en espace monde au chargement
 * (`ns_scene`), et les bras du joueur sont des segments RIGIDES portant chacun
 * leur matrice (`ns_viewmodel`) — deux choix délibérés, et bons pour ce qu'ils
 * font. Ni l'un ni l'autre ne peut porter un personnage : un coude rigide se
 * disloque, et un maillage cuit ne bouge pas.
 *
 * Il faut donc la troisième forme, la seule qui manquait : le maillage est
 * livré au repos, chaque sommet déclare jusqu'à QUATRE os et leurs poids, et le
 * shader recompose sa position à chaque image. C'est le « linear blend
 * skinning », et c'est ce que fait tout moteur depuis vingt-cinq ans.
 *
 * Pourquoi un personnage IMPORTÉ et non généré
 * --------------------------------------------
 * Les bras du viewmodel sont générés en C, et c'était le bon choix : sept
 * segments, trois cents triangles, aucun format de fichier à inventer. Un
 * personnage entier ne s'écrit pas comme ça — il faut un maillage cousu, des
 * poids par sommet et un cycle de marche, c'est-à-dire du travail d'artiste et
 * d'animateur. On importe donc, et on le dit dans `assets/cc0/LICENSES.md`.
 *
 * Ce que le module fait, et ce qu'il laisse au renderer
 * -----------------------------------------------------
 * Il LIT le fichier et il ÉCHANTILLONNE l'animation. Il ne connaît ni GPU, ni
 * pipeline, ni matériau : il rend des sommets, des indices et un tableau de
 * matrices d'os. C'est le même partage que `room_viewmodel` / `ns_viewmodel` —
 * celui qui pose sait poser, celui qui dessine sait dessiner — et c'est ce qui
 * permet de le tester sans périphérique.
 */
#ifndef NS_SKIN_H
#define NS_SKIN_H

#include "ns_math.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * QUATRE os par sommet. Ce n'est pas une limite du format — glTF en autorise
 * davantage par jeux d'attributs successifs — c'est celle qu'on impose, et pour
 * une raison mesurable : au-delà de quatre, le quatrième poids d'un maillage de
 * personnage est presque toujours sous 2 %, donc invisible, et chaque os
 * supplémentaire coûte une multiplication de matrice PAR SOMMET.
 */
#define NS_SKIN_INFLUENCES 4

/*
 * Plafond d'os. Dix-neuf suffisent au personnage employé ; soixante-quatre
 * couvrent un humanoïde avec des doigts. La borne existe parce que les matrices
 * partent en UNIFORME de shader, dont la taille est bornée : 64 x 64 octets
 * font 4 Kio, ce que toute cible tient.
 */
#define NS_SKIN_MAX_JOINTS 64

typedef struct ns_skin_vertex {
    float   position[3];
    float   normal[3];
    float   uv[2];
    /* Les indices d'os tiennent sur un octet : `NS_SKIN_MAX_JOINTS` vaut 64. */
    uint8_t joints[NS_SKIN_INFLUENCES];
    float   weights[NS_SKIN_INFLUENCES];
} ns_skin_vertex;

typedef struct ns_skin ns_skin;

/*
 * Charge un glTF binaire (.glb) ou texte. `logical` passe par `ns_path_resolve`,
 * donc « models/personnage/personnage.glb » suffit.
 *
 * Rend NULL et le DIT si le fichier n'a pas ce qu'il faut — pas de peau, pas
 * d'animation, trop d'os. Un personnage qui manque n'est pas fatal : le jeu se
 * joue à la première personne, et c'est ce que fait l'appelant.
 */
ns_skin *ns_skin_load(const char *logical);
void     ns_skin_free(ns_skin *s);

const ns_skin_vertex *ns_skin_vertices(const ns_skin *s, uint32_t *count);
const uint32_t       *ns_skin_indices(const ns_skin *s, uint32_t *count);
int                   ns_skin_joint_count(const ns_skin *s);
float                 ns_skin_duration(const ns_skin *s);

/* L'image du personnage, telle qu'elle est dans le fichier (PNG ou JPEG), ou
 * NULL. C'est au renderer de la décoder : ce module ne connaît pas le GPU. */
const void *ns_skin_image(const ns_skin *s, size_t *size);

/*
 * La HAUTEUR du personnage au repos, en unités du fichier. Sert à le mettre à
 * l'échelle du jeu : un personnage importé n'a aucune raison d'être à la taille
 * qu'on veut, et la deviner sur le nom du fichier serait une heuristique.
 */
float ns_skin_rest_height(const ns_skin *s);

/*
 * Les deux LARGEURS du personnage, en unités du fichier, mesurées sur TOUT le
 * cycle et non sur la seule pose de liaison — un bras qui balance sort de la
 * silhouette au repos, et c'est le personnage qui MARCHE qu'on regarde.
 *
 * Toutes deux se mettent à l'échelle exactement comme la hauteur, et c'est ce
 * qui permet de DÉRIVER les distances de caméra au lieu de les choisir : voir
 * `room_camera.c`, qui pose ses deux seuils d'effacement à partir de ces cotes
 * et du champ de vision.
 *
 *   - `sweep_radius` : le rayon du CYLINDRE qui contient tout le personnage en
 *     mouvement, main tendue comprise. Le volume dans lequel une caméra ne doit
 *     jamais entrer, d'où qu'elle vienne.
 *   - `half_width` : la demi-largeur EN TRAVERS, sur l'axe des épaules, lui-même
 *     mesuré (c'est la perpendiculaire au trajet des pieds). C'est elle qui
 *     décide de la place prise à l'écran, parce qu'on regarde le personnage de
 *     DOS : un bras balancé vers l'avant est caché derrière le corps et ne fait
 *     pas un pixel de plus.
 *
 * Les confondre donne un personnage transparent en permanence : sur le modèle
 * employé, le rayon balayé vaut le double de la demi-largeur.
 */
float ns_skin_sweep_radius(const ns_skin *s);
float ns_skin_half_width(const ns_skin *s);

/*
 * La FOULÉE du cycle, en unités du fichier : la distance que le personnage
 * parcourt en un tour complet d'animation.
 *
 * Un cycle de marche est animé SUR PLACE — le bassin ne translate pas, ce sont
 * les pieds qui vont et viennent sous lui. Pendant son APPUI, un pied est cloué
 * au sol : dans le repère du personnage il recule donc exactement de ce dont le
 * corps avance. La foulée est par conséquent la SOMME DES RECULS DU PIED
 * PORTEUR sur un cycle, le porteur étant à chaque instant le plus bas des deux.
 *
 * Ce n'est PAS l'aller-retour d'un seul pied. Un pied part d'un demi-pas devant
 * le corps et finit un demi-pas derrière : son excursion vaut un PAS, soit la
 * moitié de la foulée. Confondre les deux donne un personnage dont les jambes
 * tournent deux fois trop vite — la mesure a d'abord été écrite comme ça, et
 * c'est la capture qui a tranché.
 *
 * À quoi ça sert : c'est la valeur qui décide si le personnage patine. La phase
 * du cycle est pilotée par la distance réellement parcourue ; si la foulée
 * employée pour la convertir n'est pas celle du fichier, les pieds glissent au
 * sol d'autant que les deux diffèrent.
 *
 * CE QUE CETTE MESURE NE PEUT PAS FAIRE, et il faut le savoir avant de s'y fier
 * ----------------------------------------------------------------------------
 * Elle suppose un cycle dont le pied d'appui est CLOUÉ au sol. Beaucoup de
 * cycles ne le sont pas — celui du personnage livré ne l'est pas —, et cela se
 * constate plutôt que se déduire : sur lui, quatre façons également défendables
 * de mesurer la même foulée donnent 1,02 / 1,34 / 1,44 / 2,06 m, et le
 * glissement total du point de contact ne descend au-dessous d'aucune valeur de
 * foulée. Aucun nombre ne supprime le patinage d'un cycle qui patine déjà.
 *
 * La mesure reste utile pour ce qu'elle est : un ORDRE DE GRANDEUR, à confronter
 * à la valeur employée. `room/main.c` fait exactement ça — il compare et il
 * avertit — plutôt que de remplacer en silence une valeur réglée à l'œil par une
 * mesure dont la dispersion vaut un facteur deux.
 */
float ns_skin_stride_length(const ns_skin *s);

/*
 * L'ANGLE DE L'AXE AVANT du modèle, en radians, mesuré dans son plan horizontal
 * et compté depuis +X vers +Z — la même convention que le lacet du jeu.
 *
 * Pourquoi il faut le mesurer, et pourquoi ça a coûté une capture
 * ---------------------------------------------------------------
 * Un personnage importé regarde là où son auteur l'a tourné. glTF recommande
 * +Z, ce que ce modèle-ci respecte, mais « recommande » n'est pas « garantit » —
 * et surtout, poser le personnage demande de composer CETTE direction avec le
 * lacet du joueur, ce qui est exactement l'endroit où l'on se trompe de signe.
 *
 * On s'y est trompé : le code posait le personnage avec une rotation du lacet
 * telle quelle, ce qui envoyait son regard sur le MIROIR de celui de la caméra.
 * À lacet nul le personnage marchait à quatre-vingt-dix degrés de la direction
 * suivie — de profil, en crabe — et la capture le montre sans ambiguïté. Le
 * défaut n'avait jamais été vu parce que la caméra était soit dans le
 * personnage, soit ailleurs.
 *
 * La mesure vient du même balayage que la foulée : le pied porteur recule, donc
 * l'AVANT est l'opposé de ce recul. Aucun nom d'os, aucune convention supposée.
 * Un cycle immobile rend la valeur glTF par défaut, +Z, soit un quart de tour.
 */
float ns_skin_forward_angle(const ns_skin *s);

/*
 * Échantillonne l'animation à `time` secondes (bouclée sur la durée) et écrit
 * les matrices d'os dans `out`.
 *
 * `out` doit tenir `ns_skin_joint_count` matrices. Elles vont directement au
 * shader : chacune est déjà le produit de la transformation monde de l'os par
 * sa matrice de liaison inverse, c'est-à-dire « ce qu'il faut appliquer à un
 * sommet au repos ».
 */
void ns_skin_pose(const ns_skin *s, float time, ns_m4 *out, int max);

/*
 * L'instant du cycle où le personnage est le plus proche de DEBOUT, en secondes.
 *
 * Un cycle de marche n'a pas de pose de repos : il n'a que des poses de marche.
 * Immobiliser le personnage sur la phase où on l'a arrêté le laisse en grand
 * écart, ce qui se voit dès qu'on lâche les commandes.
 *
 * On cherche donc la « position de passage » — le moment où les deux pieds se
 * croisent, jambes rassemblées. Elle est MESURÉE et non devinée : on balaie le
 * cycle et on retient l'instant où les deux os les plus bas sont le plus
 * proches horizontalement. Aucun nom d'os n'entre là-dedans, ce qui évite de
 * dépendre de la convention de nommage d'un exportateur.
 */
float ns_skin_stand_time(const ns_skin *s);

#endif /* NS_SKIN_H */
