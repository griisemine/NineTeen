/*
 * room_door.h — les portes coulissantes des toilettes.
 *
 * Ce qu'on remet en service
 * -------------------------
 * En 2020 la salle avait deux portes de toilettes, une par bloc, et elles
 * s'ouvraient TOUTES SEULES quand on s'en approchait — `detecterOuvertureToilette`
 * (legacy/room/room.c:2271) est une détection de proximité, pas une touche. Le
 * vantail glissait le long de X de 20,2 à 23,0 unités, soit 2,8 unités à 2,06
 * unités par mètre : **1,359 m** de course. Il se refermait tout seul trois
 * secondes après son ouverture complète, avec `SF-ouvport.wav` à l'aller et
 * `SF-fermport.wav` au retour, tous deux réglés en volume par la distance sur une
 * portée de 8 unités — **3,88 m**.
 *
 * Les deux sons sont chargés par `room_sound.c` depuis toujours. **Rien ne les a
 * jamais joués.** C'était un chargement mort, et c'est ce fichier qui lui donne
 * un sens.
 *
 * Les quatre états, repris tels quels
 * -----------------------------------
 * `FERMER / OUVERTURE / OUVERTE / FERMETURE`. Ce n'est pas de la nostalgie : ces
 * quatre-là sont exactement ce qu'il faut, et le cinquième cas qu'on serait tenté
 * d'ajouter — « en train de se rouvrir » — est déjà couvert par le retour de
 * FERMETURE à OUVERTURE, qui est ce que 2020 faisait et qui est juste. Une porte
 * qui se referme sur quelqu'un qui revient doit repartir de là où elle en est,
 * pas d'un état neuf.
 *
 * Ce que ce fichier NE fait pas
 * -----------------------------
 * Il ne dessine rien et n'appelle pas le GPU. `room_door_tick` est du calcul pur
 * — c'est ce qui permet à `tests/test_door.c` de vérifier la machine à états sans
 * carte graphique. Le téléversement des sommets est une fonction à part,
 * `room_door_upload`, appelée dans l'image ; le son est déclenché par des drapeaux
 * que `room_sound` consomme. Trois responsabilités, trois moments.
 *
 * La géométrie, et le choix qui la fait bouger
 * --------------------------------------------
 * La salle est cuite en ESPACE MONDE : pas de matrice de modèle dans
 * `gbuffer.vert`, et `ns_scene_object` ne porte qu'un nom, une nature, une boîte
 * et une tranche de lots. Deux chemins existaient. Celui retenu réécrit la plage
 * de sommets du vantail dans le tampon, par `ns_rhi_stage_buffer` — l'anneau de
 * transfert du chemin chaud, pas le téléversement bloquant du chargement.
 *
 * Le chiffre qui a décidé : `porte_wc` occupe **192 sommets**, contigus et
 * exclusifs — comptés dans le glTF produit, où aucun autre maillage ne les
 * partage —, soit 9 216 octets par image et par porte sur un anneau de 16 Mio.
 * Une passe de rendu dédiée aurait demandé un pipeline de plus, un jeu
 * d'uniformes, une matrice par objet et une modification de `ns_shaders.c` —
 * donc du MSL nouveau, donc le contrôle `ns_test_msl` à reprendre — pour faire
 * glisser un panneau de 1,05 m. Le rapport travail/risque n'était pas discutable.
 *
 * Deux conséquences à connaître, dites franchement :
 *   - les boîtes des lots sont translatées avec les sommets, sans quoi
 *     l'élimination par frustum ferait disparaître le vantail à un bord d'écran ;
 *   - le BVH, lui, est CUIT (`tools/bvhbake.c`) et ne saura jamais que le vantail
 *     a bougé. La collision passe donc par une règle — voir plus bas —, et
 *     l'occlusion sonore de la porte n'existe pas : une porte fermée arrête le
 *     joueur mais n'assourdit pas ce qu'il y a derrière. C'était déjà le cas en
 *     2020.
 *
 * La collision, qui est une RÈGLE et non de la géométrie
 * ------------------------------------------------------
 * `room_door_blockers` rend la boîte du vantail à sa position VIVANTE, et
 * `room_camera_tick` en repousse la capsule. C'est ce que faisait 2020, et c'est
 * la seule façon de le faire ici : un BVH cuit ne peut pas suivre. Sans cette
 * règle, l'animation serait un décor et on traverserait la porte fermée — ce qui
 * la rendrait exactement aussi crédible qu'une image.
 */
#ifndef NS_ROOM_DOOR_H
#define NS_ROOM_DOOR_H

#include "ns_math.h"
#include "ns_scene.h"
#include "room_camera.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * DEUX au moins, et pas une.
 *
 * Le plan de 2020 a deux blocs sanitaires — `toiletteFemme` et `toiletteHomme` —
 * et la salle reconstruite n'en déclare pour l'instant qu'un. Écrire l'API pour
 * une seule porte obligerait à la réécrire au moment d'ajouter la seconde, ce qui
 * est le genre de dette qu'on contracte en trois minutes et qu'on rembourse en
 * trois heures. Quatre : il y a aussi deux portes de cabine dans la scène, qui
 * pourraient un jour vouloir le même traitement.
 */
#define ROOM_MAX_DOORS 4

typedef enum room_door_state {
    ROOM_DOOR_CLOSED = 0,   /* FERMER   : au repos, la baie est bouchée */
    ROOM_DOOR_OPENING,      /* OUVERTURE */
    ROOM_DOOR_OPEN,         /* OUVERTE  : maintenue, le compte à rebours court */
    ROOM_DOOR_CLOSING       /* FERMETURE */
} room_door_state;

/*
 * Une porte, telle qu'elle est DÉCLARÉE.
 *
 * L'axe de glissement est un vecteur unitaire plutôt qu'un choix entre X et Z :
 * `porte_wc` coulisse selon +z, celle de 2020 selon +x, et la prochaine sera
 * peut-être posée de biais. Un booléen aurait à être remplacé le jour où ça
 * arrive.
 */
typedef struct room_door_desc {
    const char *object;     /* le nom du nœud glTF : « porte_wc » */
    ns_v3       axis;       /* unitaire, du repos vers la position OUVERTE */
    float       travel;     /* course en mètres, du fermé à l'ouvert */

    /*
     * Le repos CUIT est-il la position ouverte ?
     *
     * Pour `porte_wc`, oui, et le commentaire du prop dans `salle.room.json`
     * explique pourquoi : le contrôle d'accessibilité du build ne sait pas encore
     * qu'un ouvrant s'ouvre, et verrait un bloc sanitaire scellé si la géométrie
     * cuite montrait la porte fermée. Il arrêterait la construction.
     *
     * Conséquence directe sur ce fichier : le décalage appliqué aux sommets vaut
     * `(ouverture - 1) * travel` et non `ouverture * travel`. Fermée, la porte
     * est donc DÉPLACÉE de −travel par rapport à ce que le glTF contient ; c'est
     * l'état ouvert qui ne coûte aucun téléversement. Le jour où le contrôle
     * saura lire un ouvrant, ce drapeau tombera à faux et rien d'autre ne
     * changera.
     */
    bool        rest_is_open;

    /* La zone de déclenchement, en plan. C'est un rectangle en XZ et pas un
     * rayon : un couloir se garde par un rectangle, et 2020 le faisait déjà
     * ainsi (`x > 18.9 && x < 21.8 && y > 8.5 && y < 10.5`). Les bornes sont en
     * mètres monde ; la hauteur n'entre pas en compte, on n'ouvre pas une porte
     * en sautant par-dessus. */
    float       trigger_min_x, trigger_max_x;
    float       trigger_min_z, trigger_max_z;
} room_door_desc;

typedef struct room_door {
    room_door_desc  desc;
    room_door_state state;

    /* 0 = fermée, 1 = ouverte. C'est la seule variable d'animation : la position
     * des sommets, la boîte de collision et le sens du mouvement s'en déduisent
     * tous. Un état à deux variables finit toujours par se contredire. */
    float           openness;
    float           hold;        /* secondes restantes en position OUVERTE */

    /* --- la géométrie --- */
    bool     bound;              /* l'objet a été trouvé dans la scène */
    uint32_t first_vertex, vertex_count;
    uint32_t first_batch, batch_count;
    /* La position de repos, copiée une fois. C'est la VÉRITÉ : chaque image
     * repart d'elle. Cumuler des deltas sur le tampon dériverait, et une porte
     * qui dérive de deux millimètres par minute finit dans le mur. */
    ns_v3    rest_min, rest_max;         /* boîte du vantail au repos */
    ns_v3    applied;                    /* décalage actuellement en place */

    /* --- les sons à déclencher, consommés par `room_sound` --- */
    bool     want_open_sound, want_close_sound;
    ns_v3    sound_position;             /* centre du vantail, vivant */
} room_door;

typedef struct room_doors {
    room_door door[ROOM_MAX_DOORS];
    uint32_t  count;

    /* --- les réglages, tous lus dans `nineteen.env` --- */
    float speed;          /* course en mètres par seconde */
    float hold_seconds;   /* maintien en position ouverte */
    float trigger_pad;    /* marge ajoutée autour de la zone déclarée */
    bool  collide;        /* une porte fermée arrête-t-elle le joueur */

    /* Les boîtes rendues à la caméra, reconstruites à chaque pas. */
    room_blocker blocker[ROOM_MAX_DOORS];
    uint32_t     blocker_count;
} room_doors;

/*
 * Retrouve les portes déclarées dans la scène et lit les réglages.
 *
 * Une porte absente de la scène n'est pas une erreur : la salle de 2020 n'a pas
 * de `porte_wc`, et le jeu doit y tourner. Elle est simplement ignorée, et le
 * journal le dit.
 */
void room_doors_init(room_doors *d, const ns_scene *scene);

/*
 * Un pas de simulation. Calcul PUR : ni GPU, ni audio, ni horloge globale.
 *
 * `player` est la position de l'œil ; seuls X et Z servent. La proximité rouvre
 * une porte en cours de fermeture, ce qui est le comportement de 2020 et le seul
 * acceptable — une porte qui se referme sur le joueur qui revient est une porte
 * cassée.
 */
void room_doors_tick(room_doors *d, ns_v3 player, float dt);

/*
 * Pousse les sommets déplacés vers le GPU, et translate les boîtes des lots.
 *
 * À appeler DANS l'image (`ns_rhi_stage_buffer` exige une image commencée), et
 * seulement pour les portes qui ont bougé : une porte au repos ne coûte rien.
 * `scene` n'est pas `const` parce que les boîtes des lots sont mises à jour —
 * sans quoi le vantail disparaîtrait au bord de l'écran.
 */
void room_doors_upload(room_doors *d, ns_rhi *rhi, ns_scene *scene);

/* Les obstacles vivants, pour `room_camera_tick`. Vide si la collision est
 * désactivée ou si toutes les portes sont ouvertes. */
room_blockers room_doors_blockers(room_doors *d);

/* Vrai une seule fois par ouverture / fermeture : le drapeau est consommé. C'est
 * `room_sound` qui appelle, parce que c'est lui qui sait où est l'auditeur et
 * quels extraits sont chargés. */
bool room_door_take_open_sound(room_door *p);
bool room_door_take_close_sound(room_door *p);

#endif /* NS_ROOM_DOOR_H */
