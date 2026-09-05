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
 * l'AVANT (`to_world` projette sur `camera.forward` ; le commentaire disait
 * « l'arrière », et une épaule à `z = -0,075` est bien 7,5 cm derrière l'œil) —
 * puis converties en monde à la toute fin. C'est le seul repère où
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
    /*
     * La partie est lancée : les DEUX mains sont sur les commandes — la gauche
     * empoigne le manche, la droite couvre les boutons — et elles y restent.
     *
     * C'est le seul état qui ne se termine pas tout seul : il dure ce que dure
     * la partie, et c'est `room_viewmodel_stop_playing()` qui en sort. En faire
     * un état à durée fixe reviendrait à retirer les mains des commandes au
     * bout d'une demi-seconde de jeu.
     *
     * Pourquoi il fallait l'ajouter : sans lui, on lançait une partie sur une
     * borne et les bras retombaient aussitôt le long du corps, hors du cadre.
     * On voyait la partie tourner dans la dalle sans personne pour la jouer.
     */
    ROOM_VM_PLAY,
    ROOM_VM_RETURN,       /* retour au repos */
    /*
     * LE COUP SUR LA MACHINE.
     *
     * Ajouté EN DERNIER dans l'énumération, et ce n'est pas de la paresse : les
     * cinq valeurs qui précèdent sont lues par `--pose=` et par la table de
     * `room_viewmodel_set_forced_pose`, et les décaler d'un rang changerait en
     * silence ce qu'une capture de référence produit.
     *
     * C'est le seul état qui puisse INTERROMPRE `ROOM_VM_PLAY` et y revenir. Il
     * ne s'insère donc pas dans la séquence du jeton : on ne frappe pas au
     * milieu d'une insertion, on frappe soit les mains vides, soit en pleine
     * partie — c'est-à-dire quand on rage.
     */
    ROOM_VM_HIT,
    /*
     * LA RELANCE : le jeton de la partie suivante, sans se relever.
     *
     * Ajouté APRÈS `ROOM_VM_HIT` pour la raison exacte qui a fait ajouter
     * celui-ci en dernier : les valeurs qui précèdent sont lues par `--pose=`
     * et par la table de `room_viewmodel_set_forced_pose`, et les décaler d'un
     * rang changerait en silence ce qu'une capture de référence produit.
     *
     * POURQUOI IL EXISTE, ET POURQUOI CE N'EST PAS LA SÉQUENCE COMPLÈTE.
     * Relancer une partie coûte un jeton — `room/main.c` le débite déjà — et ce
     * jeton ne s'entendait ni ne se voyait : les deux mains restaient sur les
     * commandes et une partie neuve apparaissait. Rejouer `REACH` → `INSERT` →
     * `PRESS` corrigerait ça et coûterait **1,31 s** (0,42 + 0,55 + 0,34),
     * pendant lesquelles le joueur qui vient de mourir et qui a déjà appuyé
     * attend. Cet état-ci fait le même geste en **0,40 s**, avec la seule main
     * droite : la gauche ne quitte pas le manche, le corps ne se redresse pas,
     * et on repart des commandes où l'on était.
     *
     * Il ne s'insère donc pas dans la séquence du jeton, exactement comme le
     * coup : il INTERROMPT `ROOM_VM_PLAY` et y revient.
     */
    ROOM_VM_RELANCE,
    ROOM_VM_STATE_COUNT
} room_vm_state;

typedef struct room_viewmodel {
    room_vm_state state;
    float         elapsed;        /* secondes dans l'état courant */
    /*
     * L'instant précédent DANS L'ÉTAT, pour interpoler au rendu.
     *
     * Le coup est la première animation de ce fichier dont la valeur n'est pas
     * une position lissée mais une COURBE du temps. On interpole donc le temps
     * et on rééchantillonne la courbe, plutôt que d'interpoler entre deux
     * valeurs de la courbe : la corde d'une parabole rabote son sommet, et le
     * sommet est ici l'instant de l'impact.
     */
    float         prev_elapsed;

    /* La borne visée pendant la séquence. Copiée et non pointée : la scène peut
     * être rechargée pendant qu'un geste est en cours (F5), et un pointeur
     * survivrait à l'objet qu'il désigne. */
    ns_v3 target_coin;
    ns_v3 target_panel;
    ns_v3 target_stick;
    ns_v3 target_normal;
    bool  has_target;

    /* Le battement du jeu, répercuté sur l'index droit. Mis à 1 par
     * `room_viewmodel_tap()`, il retombe tout seul : le geste doit être plus
     * court que l'intervalle entre deux battements, sinon le doigt reste enfoncé
     * et l'animation disparaît au moment précis où l'on joue le plus vite. */
    float tap, prev_tap;

    /* Le contrôle de portée n'est fait qu'une fois par partie : à chaque image
     * il produirait le même avertissement soixante fois par seconde. */
    bool  reach_checked;

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

    /*
     * LE CHAMP DE VISION DES BRAS, qui n'est pas constant : il s'ouvre pendant
     * une partie et se referme après. Le raisonnement complet — et les 32,0
     * degrés mesurés qui le justifient — est sur `VM_FOV_JEU`, dans le .c.
     *
     * Doublé d'un état précédent comme tout le reste de cette structure : il est
     * simulé au pas fixe et interpolé au rendu. Un champ recalculé par image
     * d'affichage à partir d'un état simulé toutes les 8 ms ferait respirer le
     * cadre par paliers, et un zoom qui saccade se voit bien plus qu'un zoom.
     */
    float fov, prev_fov;
    bool  primed;                 /* faux avant la première mise à jour */

    bool  token_visible;
    /*
     * LE JETON QUI BASCULE : un front, posé à l'instant précis où la pièce
     * quitte les doigts pour le mécanisme, et consommé par
     * `room_viewmodel_take_token`.
     *
     * Le MÊME motif que `hit_impact`, et pour le même argument, qui est écrit
     * un peu plus bas : un bruit calculé séparément depuis `elapsed` se
     * décalerait d'un pas de simulation de l'image qui le justifie, et un choc
     * dont le bruit arrive huit millisecondes après l'image ne se lit plus
     * comme un choc. Un jeton n'y échappe pas — c'est un choc, simplement plus
     * petit.
     *
     * L'instant est le SOMMET de `insert_push`, c'est-à-dire le moment où la
     * pièce cesse d'avancer dans la fente et où la main commence à se retirer.
     * C'est là qu'elle bascule, et c'est aussi là qu'elle disparaît de la main.
     */
    bool  token_drop;
    float press_depth, prev_press_depth;   /* 0 à 1, enfoncement de l'index */
    float insert_push, prev_insert_push;   /* 0 à 1, avancée du jeton dans la fente */

    /*
     * LE COUP, et les trois choses qu'il laisse derrière lui.
     *
     * `hit_from_play` : on revient aux commandes après le geste plutôt qu'au
     * repos. Sans lui, cogner une borne pendant une partie rendait les bras au
     * corps et la partie continuait sans personne devant — le défaut exact que
     * `ROOM_VM_PLAY` avait été ajouté pour corriger.
     *
     * `hit_impact` : un front, posé à l'instant précis où le poing arrive et
     * consommé par `room_viewmodel_take_impact`. C'est ce qui fait partir le
     * son, la secousse de la vue et le déraillement de la dalle EN MÊME TEMPS.
     * Les trois calculés séparément depuis `elapsed` se décaleraient d'un pas
     * de simulation les uns des autres, et un choc dont le bruit arrive huit
     * millisecondes après l'image ne se lit plus comme un choc.
     *
     * `choc` : ce qui reste du coup, décroissant. Doublé de son état précédent
     * comme tout le reste de ce fichier — il est lu au rendu, donc il doit être
     * interpolé, sinon la dalle déraille par paliers de 8 ms.
     */
    bool  hit_from_play;
    bool  hit_impact;
    ns_v3 hit_point;              /* le centre de la dalle frappée, en monde */
    int32_t hit_material;         /* le matériau de cette dalle, -1 si inconnu */
    float choc, prev_choc;

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

/*
 * Pose les mains sur les commandes de `cab` et les y laisse. À appeler quand la
 * partie démarre — y compris quand elle démarre sans geste préalable, comme avec
 * `--play-at=`, où personne n'a inséré de jeton.
 */
void room_viewmodel_start_playing(room_viewmodel *vm, const ns_cabinet *cab);

/* Rend les mains au repos. Sans effet si on ne jouait pas. */
void room_viewmodel_stop_playing(room_viewmodel *vm);

/* Un appui : l'index droit descend puis remonte. Le jeu ne connaît pas les bras,
 * c'est l'appelant qui relaie son événement de battement. */
void room_viewmodel_tap(room_viewmodel *vm);

/*
 * LE GESTE COURT DE LA RELANCE : la main droite seule va chercher la fente et
 * revient, en 0,40 s. Refuse — et rend false — si l'on ne joue pas.
 *
 * Le choix entre ce geste et la séquence complète a été fait sur une DURÉE
 * mesurée, pas sur un goût. `REACH` + `INSERT` + `PRESS` valent 1,31 s ; le
 * poignet droit parcourt 40,8 cm du dessus du panneau à la fente sur les
 * dix-neuf bornes — la cote est identique partout, les ancres étant dérivées de
 * la même géométrie. Ce geste-ci fait l'aller en 220 ms (1,85 m/s) et le retour
 * en 180 (2,27 m/s) : on revient plus VITE qu'on ne part, parce qu'à ce
 * moment-là la partie est déjà lancée et que les boutons servent.
 *
 * C'est l'inverse du coup de poing, dont le retour dure trois fois et demie
 * l'aller. Un coup s'achève ; une insertion, non — elle est le début d'autre
 * chose.
 */
bool room_viewmodel_relance(room_viewmodel *vm);

/* ==========================================================================
 * COGNER LA MACHINE
 * ==========================================================================
 *
 * Le geste, et pourquoi ses trois durées ne sont pas la même
 * ----------------------------------------------------------
 * Un coup n'est pas symétrique, et c'est ce qui le distingue d'un bras qu'on
 * agite. Il a trois temps, et chacun a sa durée MESURÉE — par la distance sur
 * le temps, pas par le goût :
 *
 *   ARMÉ    **110 ms**. Le poing quitte la position de repos (24,6 cm à
 *           droite, 20 sous l'œil, 44 devant) pour l'armé (30 à droite, 17
 *           sous l'œil, 10 devant), soit **34,6 cm** — donc **3,1 m/s**. On
 *           ramène le poing moins vite qu'on ne le lance : c'est une
 *           préparation, pas un geste de force.
 *
 *           CETTE PHASE A ÉTÉ AJOUTÉE APRÈS MESURE, et ce n'est pas un
 *           ornement. La position de repos porte DÉJÀ les mains en avant — 44
 *           cm de l'œil pour 59 de portée — donc un coup lancé de là n'avait
 *           que sept centimètres à gagner. Mesuré au poignet, il en gagnait
 *           CINQ MILLIMÈTRES : le geste existait dans le code et ne se voyait
 *           pas à l'écran.
 *
 *   ALLER   **90 ms**. De l'armé au coup porté (13 cm à droite, 6 sous l'œil,
 *           55 devant), soit **49,3 cm**, donc **5,5 m/s** de moyenne : la
 *           plage d'un vrai coup de poing, que la littérature situe entre 5 et
 *           9 m/s à l'impact selon l'entraînement. À 150 ms on tomberait à
 *           3,3 m/s, la vitesse d'un geste qu'on POSE.
 *
 *   RETOUR  **300 ms**, trois fois et demie l'aller. C'est ce qui donne le
 *           poids : un retour aussi vif que l'aller se lit comme un ressort,
 *           pas comme un bras.
 *
 * Le geste dure donc 500 ms en tout, l'impact tombe à 200, et cette demi-
 * seconde est aussi le PRIX du coup — voir `room_viewmodel_is_hitting`.
 */

/*
 * Déclenche le coup vers `cab`. Refuse — et rend false — si un geste est déjà
 * en cours ou si l'on est au milieu de la séquence du jeton : cogner pendant
 * qu'on insère une pièce n'a pas de sens, et laisser les deux se superposer
 * ferait viser deux points à la fois au même poignet.
 *
 * `cab` peut être NULL : on cogne alors dans le vide, ce qui est le
 * comportement voulu quand on tape à côté d'une borne. Le geste a lieu, le son
 * et la secousse aussi, mais aucune dalle ne déraille.
 */
bool room_viewmodel_frappe(room_viewmodel *vm, const ns_cabinet *cab);

/*
 * Vrai pendant les 500 ms du geste.
 *
 * C'EST LA CONSÉQUENCE DE JEU, et elle tient dans cette fonction : pendant que
 * la main droite est sur la machine, elle n'est pas sur les boutons. L'appelant
 * s'en sert pour ne pas transmettre les appuis au jeu — la partie continue,
 * elle, et c'est le seul point qui compte.
 *
 * Pourquoi ce prix-là et pas un jeton retiré : le barème de la salle garantit
 * un PLANCHER de cinq jetons au monnayeur, sans condition et sans attente
 * (`room_bareme.h`). Un jeton retiré n'est donc pas une perte, c'est un
 * aller-retour ; et `room_economie.h` écrit noir sur blanc qu'il n'y a « pas de
 * minuterie qui punit ». Le seul bien qu'on puisse vraiment perdre dans cette
 * salle est le SCORE de la partie en cours, parce que c'est lui qui fait les
 * tickets. C'est donc lui qu'on paie.
 */
bool room_viewmodel_is_hitting(const room_viewmodel *vm);

/*
 * Le FRONT d'impact : vrai UNE fois, au pas de simulation où le poing arrive.
 * Consommé par l'appel, comme `jump_requested` l'est par la caméra.
 *
 * `point` reçoit le centre de la dalle frappée et `material` son matériau, ou
 * -1 : c'est ce qui permet à l'appelant de placer le son dans la salle et de
 * faire dérailler la bonne dalle sans avoir à retrouver la borne lui-même.
 */
bool room_viewmodel_take_impact(room_viewmodel *vm, ns_v3 *point, int32_t *material);

/*
 * Le FRONT du jeton : vrai UNE fois, au pas de simulation où la pièce bascule
 * dans le mécanisme. Consommé par l'appel, comme `room_viewmodel_take_impact`.
 *
 * Il est levé par les DEUX gestes qui insèrent une pièce — la séquence
 * complète et la relance — et par aucun autre. Il ne l'est pas par une pose
 * figée de `--pose=insert`, qui ne fait pas avancer le temps, ni par le coup de
 * poing, qui a son propre front : on ne paie pas en cognant.
 *
 * `at` reçoit la fente visée, en monde. C'est ce qui permet à l'appelant de
 * placer le son sur la borne sans avoir à retrouver celle-ci lui-même — même
 * service que `point` pour l'impact.
 */
bool room_viewmodel_take_token(room_viewmodel *vm, ns_v3 *at);

/*
 * Ce qui reste du choc à l'instant `alpha`, de 1 à 0.
 *
 * Sert à deux choses qui doivent rester d'accord : le déraillement de l'image
 * de la dalle, et la pose du bras du personnage en troisième personne. Les
 * faire décroître séparément les désynchroniserait, et on verrait l'écran se
 * calmer avant le bras.
 */
float room_viewmodel_choc(const room_viewmodel *vm, float alpha);

/*
 * L'avancement du geste, de 0 à 1, tel que `ns_skin_allure.frappe` l'attend :
 * 0 bras au repos, 1 coup porté. Monte en 90 ms et redescend en 300.
 *
 * C'est la MÊME horloge que le geste de la première personne, et c'est
 * délibéré : F10 bascule d'une vue à l'autre en pleine partie, et deux gestes
 * qui ne dureraient pas pareil se verraient au basculement.
 */
float room_viewmodel_frappe_amount(const room_viewmodel *vm, float alpha);

/* Vrai tant que les mains sont sur les commandes. */
bool room_viewmodel_is_playing(const room_viewmodel *vm);

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

/*
 * Relit les cotes de bras dans `nineteen.env`. À appeler UNE fois au démarrage,
 * avant toute pose — voir le commentaire de `g_vm_upper`.
 */
void room_viewmodel_read_env(void);

#endif /* NS_ROOM_VIEWMODEL_H */
