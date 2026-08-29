/*
 * room_sound.h — la bande-son de la salle.
 *
 * Même partage des rôles que pour les bras : `engine/audio/ns_audio.c` sait
 * mixer, ce fichier sait **ce qu'il y a à entendre**. Il connaît la scène, la
 * caméra et le BVH ; le mixeur n'en sait rien.
 *
 * Ce que ça produit, en une phrase : un fond de salle continu, dix-neuf bornes
 * qui bourdonnent chacune à sa place, un extracteur derrière la porte des
 * toilettes, la rue au sas, et des pas dont le son change selon ce qu'on a sous
 * les pieds — le tout atténué quand un mur s'interpose.
 *
 * Les pas
 * -------
 * La cadence vient de `bob.distance`, la même valeur qui pilote l'oscillation de
 * la vue et le contre-balancement des bras. Un pas correspond donc à une foulée
 * **par construction** : à vitesse moitié, deux fois moins de pas dans la même
 * durée, et personne n'a de réglage à tenir d'accord.
 *
 * Plus précisément : le seuil vaut la DEMI-foulée, qui est exactement la période
 * de la composante verticale de l'oscillation (`bob_offset` prend `sin(phase*2)`
 * dans `room_camera.c`). Le pas tombe donc au bas du mouvement de tête, et pas à
 * côté. C'est aussi pourquoi ce seuil ne change PAS avec l'allure : le faire
 * varier découplerait le son de l'image, ce qui s'entend tout de suite. Courir
 * fait plus de pas par seconde parce qu'on parcourt la distance plus vite, ce
 * qui est la bonne raison.
 *
 * Le matériau vient de `ground_material`, que `ns_bvh_move_capsule` renseigne
 * depuis M5. La salle déclare la classe de chaque matériau (`footstep` dans
 * `salle.room.json`) ; rien n'est deviné d'un nom de fichier.
 *
 * Une banque, enfin
 * -----------------
 * `legacy/room/sounds/walk.wav` est le seul enregistrement de pas de 2020, et
 * B14 le rejouait pour les cinq matériaux en changeant sa HAUTEUR. Ce qui
 * distinguait une moquette d'un carrelage était donc une transposition — et
 * surtout, c'était la même forme d'onde toutes les 0,775 s. L'oreille repère une
 * répétition exacte bien avant un mauvais timbre.
 *
 * `tools/stepgen` synthétise maintenant quatre variantes par matériau (choc,
 * résonance de talon, résonance de corps, frottement de semelle, caisse pour
 * l'estrade). La brillance mesurée sur la banque produite va de 3 800 passages
 * par zéro et par seconde sur la moquette à 16 500 sur le carrelage : c'est un
 * écart de SPECTRE, pas de vitesse de lecture.
 *
 * `walk.wav` reste chargé et reste le recours : si la banque manque — un arbre
 * de build partiel, un paquet incomplet — on retombe sur le comportement de B14
 * plutôt que de marcher en silence.
 */
#ifndef NS_ROOM_SOUND_H
#define NS_ROOM_SOUND_H

#include "ns_audio.h"
#include "ns_scene.h"
#include "room_camera.h"
#include "room_door.h"

#include <stdbool.h>

/* Les cabines dont peut partir une chasse d'eau. Deux dans la salle
 * reconstruite (`cabine_toilettes_1` et `_2`), et la table en accepte quatre —
 * un bloc sanitaire s'agrandit plus souvent qu'il ne rétrécit. */
#define ROOM_MAX_STALLS 4

/* Quatre par matériau. Le motif audible commence à deux répétitions, pas à
 * quatre : avec l'interdiction de rejouer la variante précédente et l'écart de
 * hauteur tiré à chaque pas, quatre suffisent largement. */
#define ROOM_STEP_VARIANTS 4

/*
 * Les deux niveaux réglables que cette couche ajoute.
 *
 * Ce ne sont pas des bus — un bus coûterait une clé de configuration réservée du
 * moteur et une décision qui n'est pas d'ici (`ns_audio.h` le dit). Ce sont deux
 * facteurs appliqués par `room_sound` : l'un au gain de chaque pas joué, l'autre
 * au gain de la nappe de fond. Ils vivent au niveau du MODULE et non dans
 * `room_sound` parce que le menu doit pouvoir les bouger sans tenir l'instance —
 * exactement le motif de `ns_audio_bus_volume`, pour la même raison.
 */
typedef enum room_sound_level {
    ROOM_LEVEL_STEPS = 0,   /* les pas du joueur */
    ROOM_LEVEL_TONE,        /* le fond continu : néons et tubes */
    ROOM_LEVEL_COUNT
} room_sound_level;

void  room_sound_set_level(room_sound_level k, float v);
float room_sound_get_level(room_sound_level k);

/* Les clés de `settings.cfg`.
 *
 * Elles ne sont PAS dans `ns_config.h` avec les autres, et c'est un compromis
 * assumé : rien dans le moteur ne les lit — elles sont écrites par le menu et
 * relues par `room_sound_init`, deux fichiers de la salle. Les monter dans le
 * moteur reviendrait à y réserver des clés sans lecteur, ce que ce dépôt a déjà
 * fait trois fois et défait deux. */
#define ROOM_CFG_VOL_STEPS "audio.footsteps"
#define ROOM_CFG_VOL_TONE  "audio.roomTone"

typedef struct room_sound {
    bool ready;

    /* Sons chargés une fois. -1 si absent : le jeu tourne sans. */
    int clip_walk;                                   /* le recours de 2020 */
    int clip_step[NS_STEP_COUNT][ROOM_STEP_VARIANTS];
    bool bank_ready;                                 /* la banque a été trouvée */
    int clip_ambience;
    int clip_tone, clip_fan, clip_street;
    int clip_cabinet[3];
    int clip_door_open, clip_door_close;
    int clip_flush;

    /*
     * LES CHASSES D'EAU.
     *
     * Ponctuelles, jamais en boucle. Une salle où la chasse tire toutes les dix
     * secondes est une salle hantée : ce qu'on cherche, c'est le bruit qu'on
     * entend une fois en traversant le hall et qui dit qu'il y a quelqu'un
     * derrière la cloison. L'intervalle est donc long et TIRÉ AU SORT entre deux
     * bornes — un intervalle fixe se remarquerait au troisième passage.
     *
     * Le compte à rebours court même quand personne n'écoute, et c'est voulu :
     * une chasse ne doit pas se déclencher à l'instant précis où le joueur entre
     * dans les toilettes, ce qui la ferait passer pour une réaction à sa
     * présence.
     */
    ns_v3    stall_position[ROOM_MAX_STALLS];
    uint32_t stall_count;
    float    flush_countdown;      /* secondes avant la prochaine */
    uint32_t flush_last_stall;     /* jamais deux fois d'affilée la même */
    float    flush_min, flush_max; /* bornes de l'intervalle, en secondes */

    /* Voix persistantes. */
    int voice_ambience;
    int voice_tone;
    int voice_fan, voice_street;
    int voice_cabinet[NS_MAX_CABINETS];
    int cabinet_voices;

    /* Où sont les deux sources d'ambiance placées, pour leur occlusion. Elles
     * sont DÉDUITES des zones sonores de la scène (« toilettes », « sas_entree »)
     * plutôt que déclarées : ces zones existent déjà, elles portent déjà les
     * bonnes boîtes, et en ajouter une description parallèle ferait deux vérités
     * à tenir d'accord. */
    ns_v3 fan_position, street_position;
    bool  has_fan, has_street;

    /* Cadence des pas : on déclenche chaque fois que la distance parcourue
     * franchit un demi-pas de foulée. */
    float  last_step_distance;
    bool   left_foot;
    uint32_t rng;
    /* La variante jouée en dernier, PAR matériau : on s'interdit de la rejouer
     * deux fois de suite. C'est la seule règle qui compte vraiment — deux pas
     * identiques consécutifs s'entendent, deux pas identiques à cinq pas
     * d'intervalle non. */
    int last_variant[NS_STEP_COUNT];

    /* L'atterrissage se détecte ici plutôt que de se lire dans la caméra : elle
     * amortit `bob.land` dès le pas suivant, et le front est ce qu'on veut. */
    bool was_grounded;

    /* Tourniquet d'occlusion : une source par image plutôt que vingt et une.
     * `ns_bvh_occlusion_factor` lance trois rayons, et vingt et une sources par
     * image coûteraient soixante-trois traversées de BVH pour une grandeur qui
     * bouge à la vitesse où l'on marche. */
    uint32_t occlusion_cursor;

    /* L'espace entendu, amorti. Deux flottants plutôt qu'un pointeur de zone :
     * on interpole entre deux pièces, on ne saute pas de l'une à l'autre. */
    float space_wet, space_decay;
} room_sound;

/* Charge les sons et lance les boucles. Sans effet si le mixeur n'a pas démarré :
 * une machine sans carte son doit pouvoir jouer. */
void room_sound_init(room_sound *s, const ns_scene *scene);

/*
 * À appeler une fois par image, après la caméra ET après les portes.
 *
 * `doors` peut être NULL : une salle sans porte animée — celle de 2020 — sonne
 * comme avant. C'est `room_sound` qui joue les deux extraits de porte plutôt que
 * `room_door`, parce que c'est lui qui sait ce qui est chargé et où est
 * l'auditeur ; la porte, elle, ne sait que si elle vient de partir.
 */
void room_sound_update(room_sound *s, const ns_scene *scene, const room_camera *cam,
                       room_doors *doors, float dt);

/* Le geste d'insertion du jeton, déclenché par la machine à états des bras. */
void room_sound_coin(room_sound *s, ns_v3 position);

void room_sound_shutdown(room_sound *s);

#endif /* NS_ROOM_SOUND_H */
