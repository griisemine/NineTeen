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
 * Échantillonne l'animation à `time` secondes (bouclée sur la durée) et écrit
 * les matrices d'os dans `out`.
 *
 * `out` doit tenir `ns_skin_joint_count` matrices. Elles vont directement au
 * shader : chacune est déjà le produit de la transformation monde de l'os par
 * sa matrice de liaison inverse, c'est-à-dire « ce qu'il faut appliquer à un
 * sommet au repos ».
 */
void ns_skin_pose(const ns_skin *s, float time, ns_m4 *out, int max);

#endif /* NS_SKIN_H */
