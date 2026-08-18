/*
 * ns_audio.h — le mixeur, sur miniaudio.
 *
 * `third_party/miniaudio` est vendoré depuis M0 et n'a **jamais été lié**. Les
 * quatre clés `audio.master`, `audio.music`, `audio.sfx` et `audio.ambience` de
 * `ns_config.h` sont réservées depuis aussi longtemps et n'ont jamais été lues.
 * Ce fichier est ce qui leur donne un sens.
 *
 * Ce qu'on demande au son, dans une salle d'arcade
 * -----------------------------------------------
 * Rien de spectaculaire, et tout de discret : une nappe d'ambiance, dix-neuf
 * bornes qui bourdonnent chacune à sa place, une radio dans un coin, des pas dont
 * le matériau change quand on passe de la moquette au carrelage des toilettes, et
 * une atténuation quand un mur s'interpose. C'est cette dernière qui donne le
 * volume de la pièce à l'oreille, bien plus que la réverbération.
 *
 * Trois décisions, et leurs raisons
 * ---------------------------------
 * **Tout est décodé en mono, en mémoire, au chargement.** Une source stéréo ne
 * se spatialise pas — miniaudio la jouerait telle quelle, et une borne à trois
 * mètres sur la gauche sortirait des deux enceintes. Le décodage en mémoire évite
 * par ailleurs de tenir un descripteur de fichier par voix, et les sons de 2020
 * pèsent quelques centaines de kilo-octets en tout.
 *
 * **L'occlusion est amortie.** `ns_bvh_occlusion_factor` ne renvoie que quatre
 * valeurs distinctes (0,18 / 0,453 / 0,727 / 1,0) parce qu'elle lance trois
 * rayons. Appliquée crue, on entend le monde changer par paliers en marchant.
 * `ns_damp` la lisse, et c'est non négociable.
 *
 * **Le mixeur sait tourner sans périphérique**, et rendre dans un WAV. C'est ce
 * qui rend l'atténuation par distance et l'occlusion vérifiables en intégration
 * continue, sur une machine sans carte son — donc vérifiées plutôt qu'affirmées.
 */
#ifndef NS_AUDIO_H
#define NS_AUDIO_H

#include "ns_math.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Les quatre bus. Ce sont exactement les quatre clés de configuration réservées
 * depuis M0 ; en ajouter un cinquième demanderait une clé de plus, donc une
 * décision, donc pas ici.
 */
typedef enum ns_audio_bus {
    NS_BUS_MASTER = 0,
    NS_BUS_MUSIC,
    NS_BUS_SFX,
    NS_BUS_AMBIENCE,
    NS_BUS_COUNT
} ns_audio_bus;

#define NS_AUDIO_INVALID (-1)

typedef struct ns_audio_config {
    /* 0 = sortie sur le périphérique par défaut. Sinon, aucun périphérique n'est
     * ouvert et le mixeur n'avance que sur appel de `ns_audio_render`. */
    bool     offline;
    uint32_t sample_rate;      /* 0 = 48 000 */
} ns_audio_config;

/*
 * Démarre le mixeur. Un échec n'est **pas** fatal : une machine sans carte son
 * doit pouvoir jouer. Toutes les fonctions ci-dessous deviennent alors des
 * no-ops silencieux, et c'est délibéré — un jeu qui refuse de démarrer parce
 * qu'il n'a pas trouvé de sortie audio est un jeu cassé.
 */
bool ns_audio_init(const ns_audio_config *cfg);
void ns_audio_shutdown(void);
bool ns_audio_ready(void);

/* Volume d'un bus, 0 à 1. Lu depuis `ns_config` au démarrage, réécrit ici. */
void  ns_audio_set_bus_volume(ns_audio_bus bus, float volume);
float ns_audio_bus_volume(ns_audio_bus bus);

/*
 * Charge un son et renvoie son identifiant, ou `NS_AUDIO_INVALID`. `logical` est
 * un chemin de montage (`sounds/walk.wav`), résolu par `ns_path_resolve` comme
 * tout le reste. Un même chemin chargé deux fois renvoie le même identifiant :
 * les sons sont partagés, seules les voix sont multiples.
 */
int ns_audio_load(const char *logical);

/* L'auditeur. `forward` et `up` doivent être orthonormés — ceux d'`ns_camera`
 * le sont. */
void ns_audio_set_listener(ns_v3 position, ns_v3 forward, ns_v3 up);

/*
 * Joue un son. `gain` est linéaire, `pitch` multiplie la fréquence de lecture
 * (1 = normal). Les variantes `_3d` placent la source dans le monde ; `radius`
 * est la distance à partir de laquelle l'atténuation commence.
 *
 * Renvoie un identifiant de voix pour `ns_audio_stop` et
 * `ns_audio_voice_occlusion`, ou `NS_AUDIO_INVALID`. Une voix ponctuelle se
 * recycle toute seule à la fin du son : on peut ignorer son identifiant.
 */
int ns_audio_play(int clip, ns_audio_bus bus, float gain, float pitch);
int ns_audio_play_3d(int clip, ns_audio_bus bus, ns_v3 position,
                     float gain, float pitch, float radius, float max_distance);
int ns_audio_loop_3d(int clip, ns_audio_bus bus, ns_v3 position,
                     float gain, float pitch, float radius, float max_distance);
int ns_audio_loop(int clip, ns_audio_bus bus, float gain);

void ns_audio_stop(int voice);
void ns_audio_voice_position(int voice, ns_v3 position);

/*
 * Occlusion d'une voix, de 0 (totalement bouchée) à 1 (dégagée). La valeur est
 * **amortie à l'intérieur** : l'appelant passe la mesure brute du BVH à chaque
 * image, le mixeur se charge de ne pas la faire entendre par paliers.
 */
void ns_audio_voice_occlusion(int voice, float visibility);

/* À appeler une fois par image. Fait l'amortissement et le recyclage des voix. */
void ns_audio_update(float dt);

/*
 * Rend `seconds` de mixage dans un WAV 16 bits stéréo, sans périphérique.
 * N'a de sens qu'après `ns_audio_init` avec `offline = true`.
 *
 * C'est le test : on vérifie qu'une source lointaine est plus faible qu'une
 * source proche, et qu'une source occultée est **atténuée sans être coupée** —
 * le plancher de 0,18 de `ns_bvh_occlusion_factor` est intentionnel, un mur ne
 * fait pas taire une radio.
 */
bool ns_audio_render(const char *wav_path, float seconds);

#endif /* NS_AUDIO_H */
