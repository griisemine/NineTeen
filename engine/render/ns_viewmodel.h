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
