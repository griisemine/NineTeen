/*
 * room_sound.h — la bande-son de la salle.
 *
 * Même partage des rôles que pour les bras : `engine/audio/ns_audio.c` sait
 * mixer, ce fichier sait **ce qu'il y a à entendre**. Il connaît la scène, la
 * caméra et le BVH ; le mixeur n'en sait rien.
 *
 * Ce que ça produit, en une phrase : une nappe d'ambiance, dix-neuf bornes qui
 * bourdonnent chacune à sa place, une radio dans son coin, et des pas dont le
 * son change selon ce qu'on a sous les pieds — le tout atténué quand un mur
 * s'interpose.
 *
 * Les pas
 * -------
 * La cadence vient de `bob.distance`, la même valeur qui pilote l'oscillation de
 * la vue et le contre-balancement des bras. Un pas correspond donc à une foulée
 * **par construction** : à vitesse moitié, deux fois moins de pas dans la même
 * durée, et personne n'a de réglage à tenir d'accord.
 *
 * Le matériau vient de `ground_material`, que `ns_bvh_move_capsule` renseigne
 * depuis M5 et que **personne n'avait jamais lu**. La salle déclare la classe de
 * chaque matériau (`footstep` dans `salle.room.json`) ; rien n'est deviné d'un
 * nom de fichier.
 *
 * Un seul WAV de pas
 * ------------------
 * `legacy/room/sounds/walk.wav` est le seul enregistrement de pas de 2020. Les
 * variantes sont donc fabriquées : hauteur et gain tirés au sort dans une plage
 * propre à chaque matériau, et l'alternance gauche/droite décale légèrement le
 * timbre. Ce n'est pas une banque de vingt échantillons, et je ne prétends pas
 * le contraire — mais deux pas consécutifs cessent d'être identiques, ce qui est
 * ce que l'oreille remarque en premier.
 */
#ifndef NS_ROOM_SOUND_H
#define NS_ROOM_SOUND_H

#include "ns_audio.h"
#include "ns_scene.h"
#include "room_camera.h"

#include <stdbool.h>

typedef struct room_sound {
    bool ready;

    /* Sons chargés une fois. -1 si absent : le jeu tourne sans. */
    int clip_walk;
    int clip_ambience;
    int clip_cabinet[3];
    int clip_door_open, clip_door_close;

    /* Voix persistantes. */
    int voice_ambience;
    int voice_cabinet[NS_MAX_CABINETS];
    int cabinet_voices;

    /* Cadence des pas : on déclenche chaque fois que la distance parcourue
     * franchit un demi-pas de foulée. */
    float  last_step_distance;
    bool   left_foot;
    uint32_t rng;

    /* Tourniquet d'occlusion : une borne par image plutôt que dix-neuf.
     * `ns_bvh_occlusion_factor` lance trois rayons, et dix-neuf sources par image
     * coûteraient cinquante-sept traversées de BVH pour une grandeur qui bouge à
     * la vitesse où l'on marche. */
    uint32_t occlusion_cursor;

    /* L'espace entendu, amorti. Deux flottants plutôt qu'un pointeur de zone :
     * on interpole entre deux pièces, on ne saute pas de l'une à l'autre. */
    float space_wet, space_decay;
} room_sound;

/* Charge les sons et lance les boucles. Sans effet si le mixeur n'a pas démarré :
 * une machine sans carte son doit pouvoir jouer. */
void room_sound_init(room_sound *s, const ns_scene *scene);

/* À appeler une fois par image, après la caméra. */
void room_sound_update(room_sound *s, const ns_scene *scene, const room_camera *cam,
                       float dt);

/* Le geste d'insertion du jeton, déclenché par la machine à états des bras. */
void room_sound_coin(room_sound *s, ns_v3 position);

void room_sound_shutdown(room_sound *s);

#endif /* NS_ROOM_SOUND_H */
