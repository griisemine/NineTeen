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
    float third_distance;    /* recul, en mètres */
    float third_height;      /* hauteur de visée au-dessus des pieds */
    float third_shoulder;    /* décalage latéral : 0 = pile derrière */

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
 */
void room_camera_tick(room_camera *c, const ns_bvh *bvh, float dt);

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

#endif /* NS_ROOM_CAMERA_H */
