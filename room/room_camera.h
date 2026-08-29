/*
 * room_camera.h — caméra de la salle.
 *
 * Trois modes :
 *   - JOUEUR : à hauteur d'yeux, avec gravité, collision en capsule contre la
 *     géométrie réelle, accroupissement et oscillation de marche ;
 *   - LIBRE  : vol libre, pour le développement et les captures de référence ;
 *   - ORBITE : tour lent de la salle, mode démonstration.
 *
 * La caméra est mise à jour au pas fixe de simulation et interpolée au rendu,
 * comme tout le reste : c'est ce qui la rend fluide indépendamment du framerate.
 *
 * Convention de position
 * ----------------------
 * `position` est **l'œil**, dans les trois modes. La base de la capsule s'en
 * déduit en retranchant `eye_height`, et cette conversion ne vit qu'à l'intérieur
 * du pas de simulation. C'est délibéré : les points de vue déclarés par la scène,
 * `--pos=` et l'interpolation de rendu manipulent tous une position d'œil, et
 * faire cohabiter deux sens du même champ selon le mode est la façon la plus
 * sûre de se tromper d'un mètre soixante-dix.
 */
#ifndef NS_ROOM_CAMERA_H
#define NS_ROOM_CAMERA_H

#include "ns_bvh.h"
#include "ns_math.h"
#include "ns_render.h"

typedef enum room_camera_mode {
    ROOM_CAM_PLAYER = 0,
    ROOM_CAM_FREE,
    ROOM_CAM_ORBIT       /* tour lent de la salle : mode démonstration et captures */
} room_camera_mode;

/* ==========================================================================
 * Les obstacles qui ne sont PAS dans le BVH
 * ==========================================================================
 * Le BVH est cuit une fois pour toutes par `tools/bvhbake` : il connaît la salle
 * telle qu'elle sort du glTF, et il ne saura jamais qu'un vantail a coulissé
 * d'un mètre. Tout ce qui bouge et doit arrêter le joueur passe donc par ici —
 * une boîte, relue à sa position VIVANTE à chaque pas.
 *
 * C'est exactement ce que faisait la version de 2020, où la collision de la
 * porte des toilettes était une règle lisant `toiletteFemme.x`, et non de la
 * géométrie. Le refaire autrement demanderait de reconstruire le BVH par image.
 *
 * Le type reste NEUTRE — une boîte, rien d'autre. La caméra n'a pas à connaître
 * les portes : `room_door.c` remplit ces boîtes, `room_camera.c` s'en écarte, et
 * ni l'un ni l'autre n'inclut l'en-tête de l'autre.
 */
typedef struct room_blocker {
    ns_aabb box;
} room_blocker;

typedef struct room_blockers {
    const room_blocker *items;
    uint32_t            count;
} room_blockers;

/*
 * État d'animation de la vue subjective, simulé au pas fixe et interpolé au
 * rendu. Groupé dans une structure parce qu'il DOIT être interpolé en entier :
 * un seul champ oublié dans `room_camera_resolve` fait saccader l'oscillation à
 * la fréquence de simulation, ce qui se voit immédiatement et se diagnostique
 * mal.
 */
typedef struct room_view_bob {
    float distance;   /* mètres parcourus au sol — c'est CETTE valeur qui pilote
                       * la phase, et non le temps : un pas doit correspondre à
                       * une foulée, pas à une horloge. */
    float amount;     /* amplitude, 0 à l'arrêt, 1 en course */
    float land;       /* impulsion d'atterrissage, décroissante */
    float roll;       /* roulis en virage, radians */
    float breath;     /* phase de respiration à l'arrêt */
} room_view_bob;

typedef struct room_camera {
    room_camera_mode mode;

    /* État simulé (pas fixe) et état précédent, pour l'interpolation. */
    ns_v3 position, prev_position;          /* l'œil */
    float yaw, pitch;
    float prev_yaw, prev_pitch;

    ns_v3 velocity;

    /* --- corps --- */
    float eye_height, prev_eye_height;      /* courant, amorti vers la cible */
    float eye_height_stand;
    float eye_height_crouch;
    float body_radius;
    float body_height_stand;
    float body_height_crouch;
    float step_height;                      /* obstacle gravi sans saut */
    float gravity;                          /* positif, appliqué vers le bas */
    float jump_speed;

    float speed_walk, speed_run, speed_crouch;
    float mouse_sensitivity;
    float fov_y;

    /* --- animation --- */
    room_view_bob bob, prev_bob;
    float bob_time;                         /* uniquement pour la respiration */

    /* --- contact --- */
    bool     grounded;
    ns_v3    ground_normal;
    uint32_t ground_material;

    /* Entrées accumulées entre deux pas de simulation. */
    float input_forward, input_strafe, input_up;
    float mouse_dx, mouse_dy;
    bool  running;
    bool  crouch_held;
    bool  jump_requested;                   /* consommé par le pas suivant */

    /*
     * LA TROISIÈME PERSONNE.
     *
     * Ce n'est PAS un mode de caméra à part, et c'est le choix qui rend la
     * chose petite : le joueur continue de se déplacer exactement comme avant,
     * même capsule, même collision, même hauteur d'yeux. Seul le POINT DE VUE
     * recule. Un mode séparé aurait dupliqué le déplacement, et deux
     * déplacements finissent toujours par diverger — c'est déjà l'argument qui
     * a fait garder une seule table de matériaux pour dix-neuf bornes.
     *
     * Conséquence à connaître : la portée des bras, le ramassage et le contrôle
     * de portée continuent de partir de l'ŒIL, pas de la caméra. C'est ce qu'on
     * veut — on interagit avec ce que le personnage atteint, pas avec ce que la
     * caméra survole.
     */
    bool  third_person;
    float third_distance;    /* recul VOULU, en mètres */
    float third_height;      /* hauteur de visée au-dessus des pieds */
    float third_shoulder;    /* décalage latéral VOULU : 0 = pile derrière */

    /*
     * LE BRAS DE CAMÉRA, simulé au pas fixe comme tout le reste.
     *
     * Il vivait dans `room_camera_resolve`, recalculé par image d'affichage.
     * Deux raisons de l'avoir remonté ici :
     *
     *   - il est maintenant AMORTI, et un amortissement a besoin d'un état et
     *     d'un pas de temps. Sans lui, le bras saute dès qu'un rayon accroche
     *     puis lâche une arête, et une caméra qui saute est le défaut qu'on
     *     remarque avant tous les autres ;
     *   - il coûte neuf lancers de rayon. À 120 Hz de simulation c'est un
     *     budget fixe ; par image d'affichage, il triplait sur un écran à
     *     360 Hz, c'est-à-dire exactement là où on a le moins de marge.
     *
     * `third_arm` est le recul obtenu, `third_side` le décalage d'épaule
     * obtenu — l'un et l'autre peuvent être plus courts que ce qui est demandé,
     * et l'épaule aussi : elle est balayée elle aussi, parce que le point d'où
     * part le bras peut se trouver DANS un mur quand on longe une cloison.
     */
    float third_arm,  prev_third_arm;
    float third_side, prev_third_side;

    /*
     * LE PERSONNAGE, tel que la caméra a besoin de le connaître.
     *
     * Trois cotes, toutes MESURÉES sur le modèle par `ns_skin` et posées ici par
     * `room/main.c` — la caméra ne charge rien et n'inclut pas `ns_skin.h`.
     * `actor_half_width` à zéro veut dire « aucun personnage » : les seuils
     * d'effacement sont alors inertes et l'opacité vaut toujours 1, ce qui est
     * le comportement voulu quand le fichier de modèle manque.
     */
    float actor_half_width;   /* demi-largeur en travers, mètres */
    float actor_sweep_radius; /* rayon du cylindre balayé, mètres */
    float stride;             /* foulée, mètres — voir room_camera_stride */

    /* Les réglages du recul. Voir `nineteen.env` pour ce que chacun coûte. */
    float probe_radius;       /* rayon de la sonde de caméra, mètres */
    float return_rate;        /* vitesse de retour du bras, par seconde */
    float fade_full;          /* couverture d'écran en deçà de laquelle il est plein */
    float fade_none;          /* couverture d'écran au-delà de laquelle il est effacé */

    /* Mode orbite */
    ns_v3 orbit_center;
    float orbit_radius, orbit_height, orbit_speed, orbit_angle;
} room_camera;

void room_camera_init(room_camera *c, ns_v3 start, float yaw);

/*
 * Avance la simulation d'un pas fixe.
 *
 * `bvh` peut être NULL (ou non chargé) : le mode joueur retombe alors sur un
 * déplacement libre sans gravité, ce qui garde le jeu jouable sur une salle sans
 * fichier .nsbvh plutôt que de le clouer au sol.
 *
 * `blockers` peut être NULL, et c'est le cas courant : une salle sans pièce
 * mobile n'en a aucun. Ils sont résolus APRÈS le BVH, sur la position déjà
 * corrigée — voir `room_camera.c` pour la règle de dégagement, qui n'est pas
 * la naïve.
 */
void room_camera_tick(room_camera *c, const ns_bvh *bvh,
                      const room_blockers *blockers, float dt);

/* Construit la caméra de rendu pour l'image courante, `alpha` étant la
 * fraction de pas écoulée depuis le dernier tick. */
/*
 * La caméra de rendu, à l'instant `alpha` entre deux pas.
 *
 * `bvh` NUL veut dire « donne-moi l'ŒIL » : pas de recul de troisième personne,
 * et donc pas besoin de collision. Ce n'est pas une commodité, c'est la
 * distinction qui compte — les bras et l'écoute appartiennent au CORPS, pas au
 * point de vue. Un viewmodel posé depuis une caméra reculée de deux mètres
 * flotterait devant le personnage, et le son se mettrait à venir de derrière
 * lui.
 */
ns_camera room_camera_resolve(const room_camera *c, const ns_bvh *bvh, float alpha);

/*
 * L'état d'oscillation interpolé, pour qui a besoin de s'y accrocher — les bras
 * en premier lieu, dont le contre-balancement doit tomber sur les mêmes pas que
 * la vue. Exposé plutôt que recopié : deux interpolations du même état finiraient
 * par se désynchroniser, et un bras qui balance à contretemps se voit tout de
 * suite sans qu'on sache pourquoi.
 */
room_view_bob room_camera_bob(const room_camera *c, float alpha);

/* ==========================================================================
 * La troisième personne : le bras, la foulée, l'effacement
 * ========================================================================== */

/*
 * Déclare les cotes MESURÉES du personnage. À appeler une fois, après le
 * chargement du modèle ; ne pas l'appeler laisse la caméra se comporter comme
 * s'il n'y avait pas de personnage, ce qui est le cas quand le fichier manque.
 *
 * `stride` à zéro ou négatif garde la foulée par défaut. C'est ce qui fait
 * qu'un test qui construit une caméra sans modèle — `tests/test_ik.c` — retrouve
 * exactement la valeur historique.
 */
void room_camera_set_actor(room_camera *c, float half_width, float sweep_radius,
                           float stride);

/*
 * LA FOULÉE, en mètres : la distance parcourue en un cycle d'animation complet.
 *
 * C'est la valeur qui relie la distance parcourue à la phase — des jambes, des
 * bras, de l'oscillation de la tête et des bruits de pas. Elle est écrite en dur
 * à 1,55 m dans QUATRE fichiers : ici, `room_viewmodel.c`, `room_sound.c` et,
 * jusqu'ici, `room/main.c`. Une constante recopiée quatre fois est une constante
 * qui finira par diverger, et les quatre doivent lire celle-ci.
 *
 * Ce qu'elle coûte quand elle est fausse : les pieds patinent. La phase avance
 * de `distance / foulée` ; si la foulée employée diffère de celle du cycle, le
 * pied d'appui glisse au sol d'autant.
 *
 * Elle reste réglable et non mesurée par défaut, et la raison est écrite dans
 * `room_camera.c` : le cycle livré n'a pas de pied cloué au sol, donc aucune
 * mesure ne fait autorité. `personnage.foulee` la fixe à la main ; le jeu
 * COMPARE la valeur employée à la mesure et avertit si elles s'écartent trop.
 */
float room_camera_stride(const room_camera *c);

/*
 * Le recul EFFECTIF du point de vue à l'instant `alpha`, en mètres — celui que
 * `room_camera_resolve` vient d'employer, obstacles compris.
 *
 * Exposé parce que c'est de LUI que dépend l'effacement du personnage, et qu'on
 * ne veut pas que l'appelant le recalcule : deux calculs du même recul se
 * désaccorderaient d'une image, et le personnage clignoterait.
 */
float room_camera_third_arm(const room_camera *c, float alpha);

/*
 * L'OPACITÉ du personnage pour un bras de caméra de `arm` mètres : 1 plein,
 * 0 effacé.
 *
 * Fonction PURE — elle ne lit que les cotes du personnage et le champ de vision
 * — et c'est délibéré : c'est ce qui la rend vérifiable sans GPU, sans salle et
 * sans fenêtre. Voir `tests/test_camera.c`, et `room_camera.c` pour la
 * dérivation des deux seuils, qui ne sont pas choisis mais calculés.
 */
float room_camera_actor_opacity(const room_camera *c, float arm);

#endif /* NS_ROOM_CAMERA_H */
