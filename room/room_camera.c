/* room_camera.c — caméra de la salle, simulée à pas fixe. */
#include "room_camera.h"

#include <SDL3/SDL.h>

/* Limite du tangage : juste en deçà de la verticale, sinon la base de la
 * caméra devient dégénérée et l'image se retourne. */
#define PITCH_LIMIT (89.0f * NS_DEG2RAD)

void room_camera_init(room_camera *c, ns_v3 start, float yaw)
{
    SDL_zerop(c);
    c->mode = ROOM_CAM_FREE;
    c->position = c->prev_position = start;
    c->yaw = c->prev_yaw = yaw;
    c->pitch = c->prev_pitch = 0.0f;

    c->eye_height = 1.68f;            /* hauteur d'yeux d'un adulte debout */
    c->speed_walk = 2.6f;
    c->speed_run  = 5.2f;
    c->mouse_sensitivity = 0.0022f;
    c->fov_y = 62.0f;

    c->orbit_radius = 13.0f;
    c->orbit_height = 4.2f;
    c->orbit_speed  = 0.11f;
}

static ns_v3 forward_from_angles(float yaw, float pitch)
{
    const float cp = cosf(pitch);
    return ns_v3_make(cosf(yaw) * cp, sinf(pitch), sinf(yaw) * cp);
}

void room_camera_tick(room_camera *c, float dt)
{
    c->prev_position = c->position;
    c->prev_yaw = c->yaw;
    c->prev_pitch = c->pitch;

    if (c->mode == ROOM_CAM_ORBIT) {
        c->orbit_angle += c->orbit_speed * dt;
        c->position = ns_v3_make(
            c->orbit_center.x + cosf(c->orbit_angle) * c->orbit_radius,
            c->orbit_center.y + c->orbit_height,
            c->orbit_center.z + sinf(c->orbit_angle) * c->orbit_radius);

        /* Toujours tourné vers le centre de la salle. */
        const ns_v3 to_center = ns_v3_sub(c->orbit_center, c->position);
        c->yaw = atan2f(to_center.z, to_center.x);
        c->pitch = atan2f(to_center.y, sqrtf(to_center.x * to_center.x + to_center.z * to_center.z));
        return;
    }

    /* --- orientation --- */
    c->yaw   += c->mouse_dx * c->mouse_sensitivity;
    c->pitch -= c->mouse_dy * c->mouse_sensitivity;
    c->pitch = ns_clampf(c->pitch, -PITCH_LIMIT, PITCH_LIMIT);
    c->mouse_dx = c->mouse_dy = 0.0f;

    /* Garder le lacet borné évite la perte de précision des flottants après
     * plusieurs minutes de rotation continue. */
    if (c->yaw >  NS_TAU) c->yaw -= NS_TAU;
    if (c->yaw < -NS_TAU) c->yaw += NS_TAU;

    /* --- déplacement --- */
    const ns_v3 forward = forward_from_angles(c->yaw, c->pitch);
    const ns_v3 flat_forward = ns_v3_norm(ns_v3_make(forward.x, 0.0f, forward.z));
    const ns_v3 right = ns_v3_norm(ns_v3_cross(flat_forward, ns_v3_make(0, 1, 0)));

    ns_v3 wish = ns_v3_zero();
    if (c->mode == ROOM_CAM_FREE) {
        /* En vol libre, avancer suit le regard, y compris vers le haut. */
        wish = ns_v3_add(wish, ns_v3_scale(forward, c->input_forward));
    } else {
        wish = ns_v3_add(wish, ns_v3_scale(flat_forward, c->input_forward));
    }
    wish = ns_v3_add(wish, ns_v3_scale(right, c->input_strafe));
    wish = ns_v3_add(wish, ns_v3_make(0.0f, c->input_up, 0.0f));

    /* Normaliser empêche la diagonale d'être plus rapide que la ligne droite —
     * un défaut classique, présent dans la V1. */
    if (ns_v3_len_sq(wish) > 1e-6f) wish = ns_v3_norm(wish);

    const float speed = c->running ? c->speed_run : c->speed_walk;
    const ns_v3 target_velocity = ns_v3_scale(wish, speed);

    /* Accélération amortie plutôt qu'instantanée : le déplacement a du poids,
     * et l'amortissement est indépendant du pas de temps. */
    c->velocity.x = ns_damp(c->velocity.x, target_velocity.x, 14.0f, dt);
    c->velocity.y = ns_damp(c->velocity.y, target_velocity.y, 14.0f, dt);
    c->velocity.z = ns_damp(c->velocity.z, target_velocity.z, 14.0f, dt);

    c->position = ns_v3_add(c->position, ns_v3_scale(c->velocity, dt));
}

ns_camera room_camera_resolve(const room_camera *c, float alpha)
{
    ns_camera cam;
    SDL_zero(cam);

    cam.position = ns_v3_lerp(c->prev_position, c->position, alpha);

    /* Interpoler les angles, pas les vecteurs : interpoler deux directions
     * opposées donnerait un vecteur nul au milieu. Le passage par ±π est
     * traité en ramenant l'écart dans [-π, π]. */
    float dyaw = c->yaw - c->prev_yaw;
    while (dyaw >  NS_PI) dyaw -= NS_TAU;
    while (dyaw < -NS_PI) dyaw += NS_TAU;
    const float yaw = c->prev_yaw + dyaw * alpha;
    const float pitch = ns_lerpf(c->prev_pitch, c->pitch, alpha);

    cam.forward = forward_from_angles(yaw, pitch);
    cam.up = ns_v3_make(0.0f, 1.0f, 0.0f);
    cam.fov_y_degrees = c->fov_y;

    /* Plan proche généreux : la salle fait une trentaine de mètres, et avec le
     * reverse-Z la précision de profondeur reste excellente même à 5 cm. */
    cam.znear = 0.05f;
    cam.zfar  = 160.0f;
    return cam;
}
