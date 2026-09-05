/*
 * ns_viewmodel.h — les bras du joueur, et le jeton qu'ils tiennent.
 *
 * Partage des rôles : le moteur **dessine**, `room/` **décide de la pose**. Le
 * moteur possède la géométrie, le pipeline et la passe ; il reçoit une poignée de
 * matrices et ne sait rien de la machine à états qui les produit.
 *
 * Sept segments rigides, aucune déformation de peau. Chacun porte sa propre
 * matrice, et c'est exactement pour ça qu'ils ne peuvent pas passer par le
 * G-buffer : `gbuffer.vert` n'a pas de matrice de modèle du tout, la scène étant
 * cuite en espace monde au chargement.
 *
 * La passe est **forward, avec son propre tampon de profondeur**. Ce n'est pas un
 * choix esthétique : l'accumulation temporelle du lancer de rayons ne reprojette
 * pas (elle échantillonne l'historique à l'UV du pixel courant), donc écrire les
 * bras dans la profondeur partagée les ferait traîner dans les ombres et
 * l'occlusion à chaque mouvement de main.
 *
 * Les matrices sont en **espace monde**. Le champ de vision, lui, est plus étroit
 * que celui de la scène : c'est la pratique courante, et c'est ce qui empêche les
 * mains d'être déformées quand elles s'approchent des bords du cadre.
 */
#ifndef NS_VIEWMODEL_H
#define NS_VIEWMODEL_H

#include "ns_math.h"
#include "ns_scene.h"

/*
 * Les LONGUEURS des trois segments d'un bras, en mètres, et pourquoi elles sont
 * ici plutôt que dans `room/`.
 *
 * Le maillage est modélisé sur une longueur de 1 le long de −Z, et c'est la
 * pose qui porte la vraie longueur (`length[]`, appliquée par `viewmodel.vert`).
 * Tant qu'un membre était un tube droit, la convention suffisait : les normales
 * d'un prisme sont radiales, un étirement en Z ne les touche pas.
 *
 * Une main à doigts REPLIÉS n'a pas cette chance. Ses normales ont une
 * composante en Z, et un étirement les fausserait — il faudrait une inverse
 * transposée que le shader n'a pas, et qu'il n'a pas pour de bonnes raisons.
 *
 * La sortie est de modéliser en MILLIMÈTRES puis de diviser Z par la longueur
 * du segment : le shader remultiplie par la même valeur, le produit est
 * l'identité, et les normales traversent intactes. Cela ne tient QUE si les
 * deux valeurs sont la même. Elles le sont parce qu'elles sont ici :
 * `room_viewmodel.c` définit ses `VM_*` à partir de ces constantes, et un
 * désaccord ne peut plus s'installer par distraction.
 */
#define NS_VM_UPPER_M  0.320f    /* épaule -> coude */
#define NS_VM_FORE_M   0.270f    /* coude -> poignet */
#define NS_VM_HAND_M   0.135f    /* poignet -> bout du majeur, DOIGTS REPLIÉS */

/*
 * CE QUE PORTE LA COORDONNÉE DE TEXTURE, ET POURQUOI ELLE NE PORTE PAS UN UV.
 *
 * Le viewmodel n'échantillonne AUCUNE texture — `viewmodel.frag` n'a pas de
 * descripteur d'image, et lui en donner un obligerait à inventer un atlas, un
 * dépliage et un outil pour le peindre, pour sept segments dont on ne voit
 * jamais que le dos de la main. Le `uv` du sommet était donc écrit et jamais
 * lu.
 *
 * Il sert maintenant à dire au fragment OÙ IL EST SUR LA MAIN, ce qui est la
 * seule chose dont une peinture procédurale a besoin :
 *
 *   uv.x — la DORSALITÉ : 0 côté paume, 1 côté dos. Elle vaut pour toutes les
 *          pièces parce qu'elles sont toutes balayées vers −Z, donc leur
 *          binormale pointe toujours du même côté ; c'est ce qui permet de
 *          poser un ongle sans savoir de quel doigt il s'agit.
 *   uv.y — le CODE DE PIÈCE (partie entière) et l'AVANCEMENT le long de la
 *          pièce (partie fractionnaire, 0 à la base, 1 au bout).
 *
 * Un code de pièce plutôt qu'une texture par pièce : c'est lui qui distingue
 * une phalange distale — là où va l'ongle — d'un pli de paume, sans qu'aucun
 * sommet n'ait à porter d'attribut supplémentaire.
 */
typedef enum ns_viewmodel_part {
    NS_VM_PART_LIMB = 0,   /* manche et avant-bras : de la toile */
    NS_VM_PART_PALM,
    NS_VM_PART_THENAR,
    NS_VM_PART_FINGER,
    NS_VM_PART_THUMB,
    NS_VM_PART_TOKEN
} ns_viewmodel_part;

/*
 * L'avancement est comprimé dans cette fraction avant d'être ajouté au code de
 * pièce, pour qu'un bout de pièce (avancement 1) ne déborde jamais sur le code
 * suivant. `viewmodel.frag` DIVISE par la même valeur après `fract()` — les
 * deux doivent rester d'accord, et c'est la raison pour laquelle elle est
 * déclarée ici plutôt qu'écrite deux fois.
 */
#define NS_VM_PART_SPAN 0.99f

typedef enum ns_viewmodel_segment {
    NS_VM_SLEEVE_L = 0,   /* manche : de l'épaule au coude */
    NS_VM_FOREARM_L,      /* avant-bras : du coude au poignet */
    NS_VM_HAND_L,
    NS_VM_SLEEVE_R,
    NS_VM_FOREARM_R,
    NS_VM_HAND_R,
    NS_VM_TOKEN,          /* le jeton, dans la main droite */
    NS_VM_SEGMENT_COUNT
} ns_viewmodel_segment;

typedef struct ns_viewmodel_pose {
    /* Espace monde. Rotation et translation SEULEMENT — pas d'échelle : c'est ce
     * qui permet au shader de transformer les normales par la même matrice, sans
     * inverse transposée. */
    ns_m4 segment[NS_VM_SEGMENT_COUNT];
    /* Longueur de chaque segment, en mètres. Le maillage est modélisé sur une
     * longueur de 1 le long de −Z ; c'est le shader qui l'étire. Passée à part
     * plutôt que glissée dans la matrice : la déduire d'une matrice supposerait
     * qu'elle n'a pas d'échelle, hypothèse qu'on ne veut pas avoir à tenir. */
    float length[NS_VM_SEGMENT_COUNT];
    /* Un segment invisible n'est pas dessiné — le jeton disparaît quand il tombe
     * dans la fente, et les bras n'existent pas en caméra libre. */
    bool  draw[NS_VM_SEGMENT_COUNT];
    /* Champ de vision vertical propre au viewmodel, en degrés. 0 = valeur par
     * défaut du moteur. */
    float fov_y_degrees;
} ns_viewmodel_pose;

/*
 * Le bout du MAJEUR dans le repère de la main, doigts repliés — c'est-à-dire le
 * point que l'IK doit amener sur le bouton, sur la fente ou sur la boule.
 *
 * Il est CALCULÉ à partir des cotes du maillage, jamais recopié. La main est
 * modélisée fléchie : son bout de doigt n'est plus sur l'axe du segment mais
 * six centimètres côté paume, et une pose qui viserait « le poignet plus une
 * longueur de main » manquerait sa cible d'autant. C'est exactement ce qui est
 * arrivé la première fois — les deux mains flottaient sous les commandes.
 */
ns_v3 ns_viewmodel_fingertip(bool right);

/* Poser une pose neutre : rien de dessiné. Un `SDL_zero` ferait la même chose,
 * mais l'appelant ne doit pas avoir à le savoir. */
void ns_viewmodel_pose_clear(ns_viewmodel_pose *p);

/*
 * Construit les sept segments dans un seul couple de tampons et renvoie la plage
 * d'indices de chacun. Appelée une fois au démarrage par le renderer : la
 * géométrie ne change jamais, seules les matrices bougent.
 */
void ns_viewmodel_build(ns_vertex *verts, uint32_t vert_cap, uint32_t *out_vert_count,
                        uint32_t *indices, uint32_t index_cap, uint32_t *out_index_count,
                        uint32_t first_index[NS_VM_SEGMENT_COUNT],
                        uint32_t index_count[NS_VM_SEGMENT_COUNT]);

#endif /* NS_VIEWMODEL_H */
