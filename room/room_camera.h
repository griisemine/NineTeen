/*
 * room_camera.h — caméra de la salle.
 *
 * Deux modes :
 *   - LIBRE  : vol libre, pour le développement et les captures de référence ;
 *   - JOUEUR : à hauteur d'yeux, contrainte par la collision (branchée en M5).
 *
 * La caméra est mise à jour au pas fixe de simulation et interpolée au rendu,
 * comme tout le reste : c'est ce qui la rend fluide indépendamment du framerate.
 */
#ifndef NS_ROOM_CAMERA_H
#define NS_ROOM_CAMERA_H

#include "ns_math.h"
#include "ns_render.h"

typedef enum room_camera_mode {
    ROOM_CAM_PLAYER = 0,
    ROOM_CAM_FREE,
    ROOM_CAM_ORBIT       /* tour lent de la salle : mode démonstration et captures */
} room_camera_mode;

typedef struct room_camera {
    room_camera_mode mode;

    /* État simulé (pas fixe) et état précédent, pour l'interpolation. */
    ns_v3 position, prev_position;
    float yaw, pitch;
    float prev_yaw, prev_pitch;

    ns_v3 velocity;
    float eye_height;
    float speed_walk, speed_run;
    float mouse_sensitivity;
    float fov_y;

    /* Entrées accumulées entre deux pas de simulation. */
    float input_forward, input_strafe, input_up;
    float mouse_dx, mouse_dy;
    bool  running;

    /* Mode orbite */
    ns_v3 orbit_center;
    float orbit_radius, orbit_height, orbit_speed, orbit_angle;
} room_camera;

void room_camera_init(room_camera *c, ns_v3 start, float yaw);

/* Avance la simulation d'un pas fixe. */
void room_camera_tick(room_camera *c, float dt);

/* Construit la caméra de rendu pour l'image courante, `alpha` étant la
 * fraction de pas écoulée depuis le dernier tick. */
ns_camera room_camera_resolve(const room_camera *c, float alpha);

#endif /* NS_ROOM_CAMERA_H */
