/*
 * room_viewmodel.h — la machine à états qui pose les bras.
 *
 * Le partage des rôles, répété ici parce que c'est lui qui rend le fichier
 * court : `engine/render/ns_viewmodel.c` possède la géométrie, le pipeline et la
 * passe de rendu ; ce fichier-ci décide seulement d'où sont les mains. Il ne
 * touche à aucune ressource GPU et se teste sans périphérique.
 *
 * Espace de travail
 * -----------------
 * Les ancres sont écrites en **espace caméra** — X à droite, Y en haut, Z vers
 * l'arrière — puis converties en monde à la toute fin. C'est le seul repère où
 * « l'épaule est vingt centimètres sous l'œil et dix-huit à droite » veut dire
 * quelque chose. Les écrire en monde obligerait à les recomposer à chaque
 * changement d'orientation, et à se tromper de signe une fois sur deux.
 *
 * La cible d'un geste, elle, est en monde : le bouton d'une borne ne bouge pas
 * quand le joueur tourne la tête. C'est la conversion inverse qui la ramène en
 * espace caméra, et c'est ce qui fait que le bras suit le bouton du regard.
 *
 * Ce qui n'est PAS ici
 * --------------------
 * L'invite affichée à l'écran (« E pour jouer ») demande une couche 2D, qui
 * n'existe pas encore — c'est A9. La logique de proximité, elle, est là et
 * fonctionne : `room_viewmodel_target()` dit quelle borne est à portée.
 */
#ifndef NS_ROOM_VIEWMODEL_H
#define NS_ROOM_VIEWMODEL_H

#include "ns_math.h"
#include "ns_scene.h"
#include "ns_viewmodel.h"
#include "room_camera.h"

/*
 * Les états, dans l'ordre où ils s'enchaînent pendant une interaction.
 *
 * Repos, marche et course ne sont pas trois états mais **un seul** : le passage
 * de l'un à l'autre est continu, piloté par l'amplitude d'oscillation, et en
 * faire des états distincts imposerait des transitions à durée nulle dont le
 * seul effet serait de saccader. La séquence d'interaction, elle, est bien une
 * suite d'étapes : chacune a une durée, une cible et une suivante.
 */
typedef enum room_vm_state {
    ROOM_VM_IDLE = 0,     /* repos, marche, course — un continuum */
    ROOM_VM_REACH,        /* la main droite part vers la fente */
    ROOM_VM_INSERT,       /* le jeton entre ; il disparaît à la fin */
    ROOM_VM_PRESS,        /* l'index descend sur le bouton */
    ROOM_VM_RETURN,       /* retour au repos */
    ROOM_VM_STATE_COUNT
} room_vm_state;

typedef struct room_viewmodel {
    room_vm_state state;
    float         elapsed;        /* secondes dans l'état courant */

    /* La borne visée pendant la séquence. Copiée et non pointée : la scène peut
     * être rechargée pendant qu'un geste est en cours (F5), et un pointeur
     * survivrait à l'objet qu'il désigne. */
    ns_v3 target_coin;
    ns_v3 target_panel;
    ns_v3 target_normal;
    bool  has_target;

    /* Poignets et épaules lissés, en espace caméra. C'est ce lissage qui fait la
     * différence entre un bras qui se tend et un bras qui se téléporte : la
     * machine à états donne une cible par étape, jamais une trajectoire.
     *
     * Doublés d'un état précédent, comme la caméra : ils sont simulés au pas
     * fixe et interpolés au rendu. Sans ça les mains avanceraient par paliers de
     * 8 ms pendant que la vue, elle, serait fluide — et c'est exactement le genre
     * de saccade qu'on ne voit pas sur une capture. */
    ns_v3 wrist_l, wrist_r, prev_wrist_l, prev_wrist_r;
    ns_v3 lean, prev_lean;        /* décalage des deux épaules : se pencher */
    bool  primed;                 /* faux avant la première mise à jour */

    bool  token_visible;
    float press_depth, prev_press_depth;   /* 0 à 1, enfoncement de l'index */
    float insert_push, prev_insert_push;   /* 0 à 1, avancée du jeton dans la fente */

    /* Pose forcée par `--pose=`, pour les captures. -1 = pas de forçage. */
    int   forced_pose;
    float clock;                  /* horloge propre, seulement pour `--pose=walk` */
} room_viewmodel;

void room_viewmodel_init(room_viewmodel *vm);

/*
 * Force un état pour les captures. Renvoie false — sans rien changer — si le nom
 * est inconnu, à charge de l'appelant de le dire à l'utilisateur : un
 * `--pose=marchee` mal orthographié qui produit silencieusement un bras au repos
 * ferait perdre plus de temps que l'erreur ne vaut.
 */
bool room_viewmodel_set_forced_pose(room_viewmodel *vm, const char *name);

/*
 * Déclenche la séquence vers `cab`. Sans effet si une séquence est déjà en
 * cours : appuyer deux fois sur E ne doit pas relancer le geste au milieu.
 * Renvoie true si le geste a démarré.
 */
bool room_viewmodel_interact(room_viewmodel *vm, const ns_cabinet *cab);

/* La borne à portée de main, ou NULL. Simple relais vers
 * `ns_scene_nearest_cabinet`, écrite depuis M4 et jamais appelée jusqu'ici. */
const ns_cabinet *room_viewmodel_target(const ns_scene *scene, const room_camera *cam);

/* Avance la machine à états d'un pas fixe. */
void room_viewmodel_tick(room_viewmodel *vm, const room_camera *cam, float dt);

/*
 * Produit la pose de l'image courante. `alpha` est la fraction de pas écoulée,
 * comme pour la caméra : les bras sont accrochés à la vue, donc ils doivent être
 * interpolés avec elle, sinon ils flottent d'un pas de simulation.
 */
void room_viewmodel_pose(const room_viewmodel *vm, const room_camera *cam,
                         float alpha, ns_viewmodel_pose *out);

#endif /* NS_ROOM_VIEWMODEL_H */
