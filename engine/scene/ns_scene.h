/*
 * ns_scene.h — la salle chargée en mémoire GPU.
 *
 * Charge le glTF produit par obj2gltf, ses matériaux PBR, ses textures et les
 * lumières déduites, puis expose ce qu'il faut pour dessiner : un tampon de
 * sommets, un tampon d'indices, une liste de brouillons de dessin triés par
 * matériau, et la liste des lumières.
 *
 * Différence de fond avec la V1 : là où room.c parcourait la scène assimp à
 * chaque image en émettant un glBegin/glEnd par face, tout est ici téléversé
 * une fois au chargement et dessiné par lots.
 */
#ifndef NS_SCENE_H
#define NS_SCENE_H

#include "ns_core.h"
#include "ns_math.h"
#include "ns_bvh.h"
#include "ns_rhi.h"

#define NS_MAX_LIGHTS    128
#define NS_MAX_CABINETS  24

/*
 * Échelle du modèle de 2020, en unités par mètre.
 *
 * Établie depuis les constantes de l'auteur lui-même — `HAUTEUR_CAMERA_DEBOUT
 * 3.5F` et `HAUTEUR_CAMERA_ACCROUPI 2.7F` (legacy/room/room.c:90-91) pour un œil
 * debout et accroupi — et confirmée par tout le reste du décor : à ce facteur, le
 * plafond tombe à 2,92 m, les appliques à 2,3 m, les piliers à 0,48 m de côté, le
 * comptoir à 1,25 m de haut et les affiches à 0,38 × 0,66 m.
 */
#define NS_LEGACY_UNITS_PER_METRE 2.06f

/* ========================================================================== */
/* Format de sommet                                                           */
/* ========================================================================== */
/*
 * 48 octets, entrelacés. L'entrelacement est volontaire : le G-buffer lit les
 * quatre attributs du même sommet, donc les garder contigus évite quatre accès
 * mémoire dispersés par sommet.
 */
typedef struct ns_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4];
} ns_vertex;

/* ========================================================================== */
/* Matériaux                                                                  */
/* ========================================================================== */
/*
 * Aligné pour un storage buffer std430 : les vec3 y sont alignés sur 16 octets,
 * donc on complète explicitement plutôt que de laisser le compilateur C et le
 * compilateur GLSL se mettre d'accord tout seuls — ils ne le font pas.
 */
typedef struct ns_material_gpu {
    float base_color[4];
    float emissive[3];
    float emissive_strength;
    float metallic;
    float roughness;
    int32_t albedo_texture;      /* -1 si absent */
    int32_t normal_texture;
    int32_t orm_texture;
    int32_t _pad[3];
} ns_material_gpu;

/* ========================================================================== */
/* Lumières                                                                   */
/* ========================================================================== */

typedef enum ns_light_type {
    NS_LIGHT_POINT = 0,
    NS_LIGHT_SPOT,
    NS_LIGHT_DIRECTIONAL
} ns_light_type;

typedef struct ns_light_gpu {
    float position[3];
    float range;
    float color[3];
    float intensity;
    float direction[3];
    float spot_cos;
    int32_t type;
    int32_t shadow_index;        /* -1 si la lumière ne projette pas d'ombre */
    /*
     * Rayon apparent de la source, en mètres. Il borne la décroissance en 1/d² :
     * sans lui, une source ponctuelle placée à 5 cm d'une surface l'éclaire
     * 3 600 fois plus qu'à 3 m, et la brûle.
     *
     * C'était une constante globale de 0,22 m dans `lighting.frag`, dimensionnée
     * pour l'ampoule d'une applique. Un pavé lumineux de faux plafond fait 1,20 m :
     * traité comme une ampoule, il carbonisait les dalles voisines tout en
     * n'éclairant presque rien à trois mètres. D'où un champ par lumière — la
     * grandeur est une propriété du luminaire, pas du moteur.
     */
    float   source_radius;
    float   _pad;
} ns_light_gpu;

/* Données CPU associées : ce qui anime la lumière mais n'a pas à monter au GPU. */
typedef struct ns_light_anim {
    bool  flicker;
    bool  screen;      /* écran de borne : pulsation douce plutôt que grésillement */
    float phase;
    float base_intensity;
} ns_light_anim;

/* ========================================================================== */
/* Bornes                                                                     */
/* ========================================================================== */

/*
 * Points d'intérêt du décor. Le modèle contient un billard, un canapé, un bar
 * d'accueil, des radios et des toilettes que la V1 n'utilisait pas : ce n'était
 * que du décor. Les repérer permet d'y accrocher une interaction, une source
 * sonore positionnelle, ou une zone de réverbération distincte.
 */
typedef enum ns_poi_kind {
    NS_POI_NONE = 0,
    NS_POI_BILLIARD,
    NS_POI_SOFA,
    NS_POI_BAR,
    NS_POI_RADIO,
    NS_POI_TOILETS,
    NS_POI_EXIT,
    NS_POI_LEADERBOARD,
    NS_POI_TOKENS,          /* le monnayeur : la ou l'on prend ses jetons */
    NS_POI_PRIZES,          /* la vitrine a lots : la ou l'on depense ses tickets */
    NS_POI_KIND_COUNT
} ns_poi_kind;

typedef struct ns_poi {
    char        name[64];
    ns_poi_kind kind;
    ns_aabb     bounds;
    ns_v3       anchor;       /* où se place le joueur pour interagir */
} ns_poi;

#define NS_MAX_POI 32

/*
 * Une zone d'air chargé, DÉCLARÉE par la salle. Le format est celui de
 * `ns_particle_zone` en tout point sauf le type, et c'est voulu : `engine/scene`
 * ne doit pas dépendre de `engine/fx`, sinon charger une salle imposerait
 * d'avoir un moteur de rendu. La conversion est une recopie, faite par
 * l'appelant.
 */
typedef struct ns_dust_zone {
    char    name[48];
    ns_aabb bounds;
    float   density;
    float   drift[3];
    float   size;
    float   color[3];
    float   brightness;
} ns_dust_zone;

#define NS_MAX_DUST_ZONES 8

/*
 * Une zone de RÉVERBÉRATION, déclarée par la salle.
 *
 * Ce qu'elle porte est ce qu'on entend quand on est DEDANS : un bloc sanitaire
 * carrelé rend, une salle en moquette avale. C'est une propriété du volume où se
 * trouve l'oreille, pas de celui où se trouve la source — une radio derrière une
 * cloison sonne comme la pièce où l'on est. Ce qui change avec le mur, c'est
 * l'occlusion, traitée par ailleurs et par voix.
 *
 * Hors de toute zone, le mixage est sec. Une salle qui n'en déclare aucune sonne
 * donc exactement comme avant.
 */
typedef struct ns_sound_zone {
    char    name[48];
    ns_aabb bounds;
    float   wet;      /* part de queue, 0 à 0,9 */
    float   decay;    /* contre-réaction, 0 à 0,85 */
} ns_sound_zone;

#define NS_MAX_SOUND_ZONES 8

/*
 * Classe de pas d'un matériau, DÉCLARÉE par la salle.
 *
 * `ns_bvh_move_capsule` renvoie depuis M5 l'index du matériau sous les pieds,
 * dans `ground_material` — et ce champ n'avait jamais eu de lecteur. Il en a un :
 * cette table, indexée par le même numéro, dit sur quoi on vient de poser le
 * pied. Un index plutôt qu'un nom, parce que c'est lu à chaque foulée.
 */
typedef enum ns_footstep {
    NS_STEP_NONE = 0,     /* on ne marche pas dessus (un mur, un plafond) */
    NS_STEP_MOQUETTE,
    NS_STEP_CARRELAGE,
    NS_STEP_BOIS,
    NS_STEP_BETON,
    NS_STEP_ESTRADE,
    NS_STEP_COUNT
} ns_footstep;

const char *ns_footstep_label(ns_footstep k);

typedef struct ns_cabinet {
    char    name[64];
    char    game[32];
    char    difficulty[16];
    int     slot;
    ns_aabb bounds;
    ns_v3   screen_center;       /* point d'où part la lumière de l'écran */
    ns_v3   screen_normal;       /* vers où la borne regarde */
    float   screen_width, screen_height;  /* dalle utile, en mètres */
    ns_v3   player_anchor;       /* où se place le joueur pour jouer */
    /*
     * Les deux points que la main vise pendant la séquence d'interaction. Ils
     * sont **déclarés par la salle**, pas dérivés de la boîte englobante : ils
     * vivent dans `build_cabinet()`, à trois lignes des boîtes qu'ils désignent.
     * Les redériver ici en ferait une seconde copie, et une seconde copie d'un
     * chiffre est une copie qui dérive — c'est exactement ce qui plaçait le
     * centre d'écran 31 cm trop bas.
     *
     * Pour la salle de 2020, qui ne les déclare pas, `load_cabinet_assignment`
     * pose un repli à partir des cotes du meuble et le dit dans son commentaire.
     */
    /* Index du matériau de la dalle, DÉCLARÉ par la salle : c'est lui qu'on
     * remplace pour faire tourner un jeu dans l'écran. Le deviner en cherchant
     * quel lot d'une borne est son écran redonnerait exactement le genre
     * d'heuristique qu'on a passé le projet à retirer. −1 pour la salle de 2020,
     * qui ne le déclare pas. */
    int32_t screen_material;
    ns_v3   panel_centre;        /* dessus de la grappe de boutons */
    ns_v3   coin_slot;           /* fente à jetons, sur la face avant */
    ns_v3   stick_top;           /* là où la main gauche empoigne le manche */
    bool    attract;
} ns_cabinet;

/* ========================================================================== */
/* Lots de dessin                                                             */
/* ========================================================================== */

typedef struct ns_draw_batch {
    uint32_t first_index;
    uint32_t index_count;
    int32_t  material;
    /*
     * Objet auquel ce lot appartient, ou -1. C'était la lacune qui empêchait de
     * relier une borne à sa géométrie : un lot ne portait que son matériau, et
     * les matériaux sont partagés — `ecran_demineur` habille deux bornes,
     * `ecran_shooter.001` en habille trois. Une surcharge indexée par matériau
     * piloterait donc sept bornes à l'unisson.
     *
     * Un `int32_t` et non un nom : ce champ est parcouru une fois par lot à
     * chaque image dans la boucle d'élimination, et 48 octets de nom y coûteraient
     * plus de cache que la fonctionnalité ne vaut.
     */
    int32_t  object;
    ns_aabb  bounds;             /* pour l'élimination par frustum */
} ns_draw_batch;

/* ========================================================================== */
/* Objets nommés                                                              */
/* ========================================================================== */
/*
 * La géométrie est un seul grand tampon découpé en lots ; cette table redonne un
 * nom et une nature à chaque tranche. C'est ce qui permet de désigner « l'écran
 * de la borne 4 » sans le deviner à partir d'une fraction de sa boîte englobante.
 *
 * Les noms viennent des nœuds du glTF. Sur le modèle de 2020 ils ne servent pas à
 * grand-chose — la moitié du décor s'appelle `Cube.0XX` et les quinze bornes
 * portent toutes le même nom — mais la salle reconstruite les rend exploitables,
 * et la table fonctionne pour les deux.
 */
typedef enum ns_object_kind {
    NS_OBJ_DECOR = 0,
    NS_OBJ_CABINET,
    NS_OBJ_SCREEN,
    NS_OBJ_FIXTURE,
    NS_OBJ_DOOR,
    NS_OBJ_PROP,
    NS_OBJ_KIND_COUNT
} ns_object_kind;

typedef struct ns_scene_object {
    char           name[64];
    ns_object_kind kind;
    ns_aabb        bounds;
    uint32_t       first_batch, batch_count;   /* tranche contiguë de `batches` */
} ns_scene_object;

#define NS_MAX_OBJECTS 1024

/* ========================================================================== */
/* Points de vue nommés                                                       */
/* ========================================================================== */
/*
 * Pourquoi des noms et pas des coordonnées : la salle de 2020 est modélisée à
 * 2,06 unités par mètre, la salle reconstruite à 1. La même coordonnée y désigne
 * donc deux endroits différents, et `--pos=0,1.68,8` ne compare rien. Chaque
 * salle déclare en revanche les mêmes points de vue *nommés* dans son propre
 * référentiel, ce qui rend les captures comparables.
 */
typedef struct ns_viewpoint {
    char  name[32];
    ns_v3 position;
    float yaw, pitch;        /* radians */
    bool  orbit;             /* si vrai, position sert de centre */
    float orbit_radius, orbit_height;
} ns_viewpoint;

#define NS_MAX_VIEWPOINTS 16

/* ========================================================================== */
/* Scène                                                                      */
/* ========================================================================== */

typedef struct ns_scene {
    ns_buffer vertices;
    ns_buffer indices;
    ns_buffer materials;         /* storage buffer de ns_material_gpu */

    ns_draw_batch *batches;
    uint32_t       batch_count;

    ns_scene_object *objects;
    uint32_t         object_count;

    ns_texture *textures;
    uint32_t    texture_count;

    ns_material_gpu *material_data;
    uint32_t         material_count;
    /* Une entrée par matériau, dans le même ordre. Vide si la salle n'en déclare
     * pas — la salle de 2020, notamment, où l'on marche sur de la moquette par
     * défaut faute de mieux. */
    ns_footstep     *material_footstep;

    ns_dust_zone     dust[NS_MAX_DUST_ZONES];
    uint32_t         dust_count;

    ns_sound_zone    sound_zone[NS_MAX_SOUND_ZONES];
    uint32_t         sound_zone_count;

    ns_light_gpu   lights[NS_MAX_LIGHTS];
    ns_light_anim  light_anim[NS_MAX_LIGHTS];
    uint32_t       light_count;

    ns_cabinet cabinets[NS_MAX_CABINETS];
    uint32_t   cabinet_count;

    /*
     * Le TABLEAU DU BAR : l'indice du matériau que le moteur pilote comme un
     * écran vivant, ou −1. DÉCLARÉ par la salle, jamais deviné — c'est le seul
     * moyen de savoir lequel des cent quatre-vingt-dix matériaux est un écran
     * sans revenir à une heuristique sur les noms.
     *
     * Distinct des dalles de bornes : celles-ci reçoivent le traitement de
     * tube, celui-là non. Une dalle plate accrochée au mur d'un bar n'a ni
     * courbure ni lignes de balayage.
     */
    int32_t    scoreboard_material;

    ns_poi     pois[NS_MAX_POI];
    /*
     * LES LIEUX NOMMES, charges et pas encore employes.
     *
     * `ns_scene_load` remplit cette liste depuis la description de la salle, et
     * RIEN dans le depot ne la lit — verifie. Ce n'est pas un oubli a corriger
     * en la supprimant : elle est le seul endroit ou la salle dise « le billard
     * est ICI », ce dont auront besoin une signaletique, un guidage de camera ou
     * un mode attraction. C'est ecrit ici pour que le prochain ne croie pas
     * qu'elle pilote deja quelque chose.
     */
    uint32_t   poi_count;

    ns_aabb  bounds;        /* toute la géométrie, décor lointain compris */
    /*
     * Emprise de la salle jouable, déduite des bornes et des lumières.
     * Distincte de `bounds` : le modèle d'origine contient du décor très
     * éloigné (sol et toit débordants) qui étire la boîte englobante à plus de
     * cinquante mètres, alors que la salle elle-même en fait une vingtaine.
     * Placer une caméra d'après `bounds` la met à l'extérieur, dans le noir.
     */
    ns_aabb  room_bounds;

    /*
     * Échelle du modèle, en unités par mètre.
     *
     * La salle de 2020 vaut 2,06 : l'auteur l'a modélisée en unités Blender, et
     * il le disait lui-même — `HAUTEUR_CAMERA_DEBOUT 3.5F` pour un œil debout
     * (legacy/room/room.c:90). Le moteur V15 plaçait pourtant la caméra à 1,68,
     * soit **82 cm de haut** : le joueur était un enfant au milieu de bornes de
     * 2,4 m. Toutes les grandeurs du joueur — hauteur d'yeux, vitesse, rayon de
     * capsule, amplitude d'oscillation — sont donc exprimées en mètres et
     * multipliées par ce facteur.
     *
     * La salle reconstruite vaut 1,0, et le problème cesse d'exister.
     */
    float units_per_metre;

    /* Point d'apparition déclaré. Absent (w == 0) : déduit de `room_bounds`. */
    ns_v3 player_start;
    float player_yaw;
    bool  has_player_start;
    bool  has_declared_room_bounds;
    /*
     * Vrai quand le fichier de scène décrit lui-même ses bornes et ses lieux. Les
     * heuristiques correspondantes ne tournent alors pas du tout : elles ne sont
     * pas « corrigées » par la donnée déclarée, elles ne s'exécutent pas. C'est ce
     * qui garantit qu'une valeur devinée ne peut pas se glisser dans un champ que
     * personne n'a rempli.
     *
     * Faux pour la salle de 2020, qui n'a que son OBJ à offrir : l'ancien chemin
     * reste vivant derrière ce drapeau, ce qui permet de comparer les deux salles
     * dans le même binaire.
     */
    bool  has_declared_cabinets;
    bool  has_declared_pois;

    ns_viewpoint viewpoints[NS_MAX_VIEWPOINTS];
    uint32_t     viewpoint_count;

    uint32_t vertex_count;
    uint32_t index_count;

    /*
     * Les sommets et les indices, CÔTÉ PROCESSEUR.
     *
     * Ce ne sont pas des copies : ces deux tableaux sont ceux que le chargeur a
     * remplis avant de les téléverser, et ils vivent dans l'arène de la scène,
     * qui n'est libérée qu'au déchargement. Ils étaient donc DÉJÀ résidents pour
     * toute la partie — seize mégaoctets de sommets pour la salle reconstruite —
     * simplement inaccessibles : le chargeur les tenait dans deux variables
     * locales. Les exposer coûte seize octets de structure et zéro octet de
     * mémoire.
     *
     * À quoi ça sert, et pourquoi c'est le chemin choisi
     * --------------------------------------------------
     * La scène est cuite EN ESPACE MONDE : `gbuffer.vert` n'a pas de matrice de
     * modèle, et un objet ne porte qu'un nom, une boîte et une tranche de lots.
     * Faire bouger un vantail de porte demande donc soit une passe de rendu à
     * part avec sa propre matrice — sur le modèle de `pass_character` —, soit de
     * réécrire ses sommets dans le tampon.
     *
     * Mesuré avant de choisir : `porte_wc` occupe 192 sommets, contigus et
     * exclusifs, soit 9 216 octets. Une translation pure de 192 sommets par
     * image passe par `ns_rhi_stage_buffer` sans toucher au shader, au pipeline,
     * ni à `ns_shaders.c` — donc sans rien changer à ce que le contrôle MSL
     * vérifie. Une passe dédiée aurait demandé un pipeline, un jeu d'uniformes et
     * une matrice par objet, pour faire glisser un panneau de 1,05 m.
     *
     * `room_door.c` est le seul consommateur, et il lit les indices UNE FOIS au
     * démarrage pour retrouver la plage de sommets de chaque vantail. Rien ne les
     * écrit à la volée : la position de repos reste la vérité, et le déplacement
     * est toujours calculé depuis elle plutôt que cumulé — un cumul dérive.
     */
    const ns_vertex *cpu_vertices;
    const uint32_t  *cpu_indices;

    /* Substituts utilisés quand une texture manque : évite un test par matériau
     * dans le shader, et surtout évite l'écran noir en cas d'asset absent. */
    ns_texture fallback_white;
    ns_texture fallback_normal;
    ns_texture fallback_orm;

    /* Structure d'accélération : rendu, collision et audio la partagent. */
    ns_bvh bvh;

    ns_arena arena;
} ns_scene;

/*
 * Charge la scène complète : géométrie, matériaux, textures (diffus + cartes
 * générées), lumières déduites, affectation des bornes.
 *
 * `gltf_logical` est un chemin logique ("scene/salle.gltf"). Les fichiers
 * annexes sont trouvés à côté : salle.bin, salle.lights.json, cabinets.json,
 * et les cartes PBR dans materials/.
 */
bool ns_scene_load(ns_rhi *r, ns_scene *out, const char *gltf_logical);
void ns_scene_unload(ns_rhi *r, ns_scene *s);

/* Fait vivre l'éclairage : scintillement des néons, pulsation des écrans. */
void ns_scene_animate_lights(ns_scene *s, double time_seconds);

/* Objet nommé, ou NULL. La comparaison est exacte et sensible à la casse : le
 * fichier annexe désigne les objets par nom plutôt que par indice, ce qui reste
 * valable si l'ordre des nœuds change. */
const ns_scene_object *ns_scene_find_object(const ns_scene *s, const char *name);

/* Point de vue nommé, ou NULL. */
const ns_viewpoint *ns_scene_find_viewpoint(const ns_scene *s, const char *name);

/* Trouve la borne la plus proche d'un point, dans un rayon donné. */
const ns_cabinet *ns_scene_nearest_cabinet(const ns_scene *s, ns_v3 position, float max_distance);

/* Point d'intérêt le plus proche (billard, bar, canapé, radio…). */
const ns_poi *ns_scene_nearest_poi(const ns_scene *s, ns_v3 position, float max_distance);

/* Libellé lisible d'un type de point d'intérêt, pour l'invite d'interaction. */
const char *ns_poi_label(ns_poi_kind kind);

/* Couleur d'écran associée à un jeu : sert à la lumière que la borne projette
 * devant elle, et au halo de son marquee. */
void ns_game_screen_color(const char *game, float out_rgb[3]);

#endif /* NS_SCENE_H */
