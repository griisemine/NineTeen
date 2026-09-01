/* room_camera.c — caméra de la salle, simulée à pas fixe. */
#include "room_camera.h"

#include "ns_bvh.h"
#include "ns_env.h"

#include <SDL3/SDL.h>

/* Limite du tangage : juste en deçà de la verticale, sinon la base de la
 * caméra devient dégénérée et l'image se retourne. */
#define PITCH_LIMIT (89.0f * NS_DEG2RAD)

/* --------------------------------------------------------------------------
 * Les obstacles hors BVH
 * --------------------------------------------------------------------------
 * Le BVH est cuit une fois pour toutes et ne connaîtra jamais le vantail qui
 * coulisse. Ce qui bouge et doit arrêter le joueur passe donc par une boîte
 * relue à sa position vivante, et cette fonction l'en écarte.
 *
 * On raisonne sur la boîte DILATÉE du rayon de la capsule (somme de Minkowski) :
 * le joueur redevient un point, et le problème passe de « cylindre contre
 * boîte » à « point dans un rectangle », qui n'a pas de cas particulier aux
 * coins.
 *
 * Le dégagement par l'axe de moindre pénétration, et pourquoi il SUFFIT
 * ---------------------------------------------------------------------
 * C'est la règle naïve, et on la soupçonne à juste titre de laisser traverser un
 * panneau mince : à mi-course dans une cloison, le plus court chemin dehors
 * désigne l'autre côté. Ce soupçon a été VÉRIFIÉ ici, et il ne tient pas — pour
 * une raison qu'on ne voit qu'en posant les chiffres.
 *
 * Le vantail fait 4,5 cm d'épaisseur, mais ce n'est pas contre lui qu'on teste :
 * c'est contre sa dilatation par le rayon de la capsule, 32 cm de chaque côté.
 * La boîte effective fait donc **70 cm** d'épaisseur, pas 4,5. À la course —
 * 3,3 m/s au pas fixe — le joueur en franchit 2,75 cm par pas : il faudrait
 * qu'il en franchisse plus de la MOITIÉ, soit 35 cm, pour que le mauvais côté
 * devienne le plus proche. C'est un facteur douze de marge, et il ne dépend ni de
 * la finesse du panneau ni du nombre d'images par seconde, seulement du rayon du
 * joueur et du pas de simulation.
 *
 * Une version antérieure de cette fonction repoussait « du côté d'où l'on
 * venait », en gardant la position précédente pour trancher. Elle a été retirée :
 * mise à l'épreuve du même test, elle ne rattrape aucun cas que celle-ci laisse
 * passer. Un raffinement qu'on ne peut pas faire échouer n'est pas un
 * raffinement, c'est une branche de plus à lire.
 *
 * Ce qui la mettrait en défaut, si un jour on y touche : un obstacle plus mince
 * que le rayon du joueur ET un pas de simulation assez gros pour en franchir la
 * moitié d'un coup. Aucun des deux n'est le cas ici, et `tests/test_door.c`
 * mesure la marge plutôt que de la supposer.
 */
static void push_out_blockers(const room_blockers *b, float radius,
                              float height, ns_v3 *feet)
{
    if (!b || !b->items || b->count == 0) return;

    for (uint32_t i = 0; i < b->count; ++i) {
        const ns_aabb box = b->items[i].box;
        if (!ns_aabb_valid(box)) continue;

        /* En hauteur, on ne dilate pas : la capsule est verticale, et une porte
         * qui s'arrête à 2,05 m ne doit pas gêner par-dessus. */
        if (feet->y >= box.max.y || feet->y + height <= box.min.y) continue;

        const float min_x = box.min.x - radius, max_x = box.max.x + radius;
        const float min_z = box.min.z - radius, max_z = box.max.z + radius;
        if (feet->x <= min_x || feet->x >= max_x) continue;
        if (feet->z <= min_z || feet->z >= max_z) continue;

        /* Les quatre dégagements possibles, tous positifs. Le plus court gagne —
         * c'est aussi celui qui déplace le joueur le moins visiblement, ce qui
         * compte quand c'est la porte qui vient à lui et non l'inverse. */
        const float out_neg_x = feet->x - min_x, out_pos_x = max_x - feet->x;
        const float out_neg_z = feet->z - min_z, out_pos_z = max_z - feet->z;

        float best = out_neg_x; int axis = 0; float delta = -out_neg_x;
        if (out_pos_x < best) { best = out_pos_x; axis = 0; delta =  out_pos_x; }
        if (out_neg_z < best) { best = out_neg_z; axis = 2; delta = -out_neg_z; }
        if (out_pos_z < best) {              axis = 2; delta =  out_pos_z; }
        if (axis == 0) feet->x += delta; else feet->z += delta;
    }
}

/*
 * Longueur d'une foulée complète (deux pas), PAR DÉFAUT. L'oscillation verticale
 * a deux maxima par foulée — un par pied — et l'oscillation latérale un seul.
 *
 * Ce n'est plus une constante mais un DÉFAUT : `room_camera_set_actor` peut la
 * remplacer, et `room_camera_stride` la rend. La valeur historique reste ici, et
 * elle reste le défaut, pour deux raisons.
 *
 * La première est qu'elle doit s'appliquer telle quelle quand il n'y a pas de
 * personnage : un test qui construit une caméra nue — `tests/test_ik.c` — doit
 * retrouver le comportement d'avant, au bit près.
 *
 * La seconde est qu'on a essayé de la remplacer par la mesure, et qu'on ne l'a
 * pas fait : voir `ns_skin_stride_length`. Le cycle du personnage livré n'a pas
 * de pied cloué au sol, quatre mesures également défendables de sa foulée
 * s'étalent de 1,02 à 2,06 m, et 1,55 tombe au milieu. Substituer une mesure
 * aussi dispersée à une valeur réglée à l'œil aurait été un changement de
 * comportement sans preuve — et il aurait désaccordé les jambes des bruits de
 * pas, qui lisent la même foulée.
 */
#define STRIDE_DEFAULT 1.55f

/* Amplitudes, en fraction de la hauteur d'yeux. Volontairement discrètes : une
 * oscillation qu'on remarque est une oscillation trop forte. */
#define BOB_VERTICAL 0.021f
#define BOB_LATERAL  0.014f

/* Vitesse verticale au-delà de laquelle on ne tombe plus plus vite. Sans
 * plafond, une chute longue finirait par franchir plus d'une hauteur de marche
 * par pas et traverserait le sol. */
#define TERMINAL_FALL 24.0f

void room_camera_init(room_camera *c, ns_v3 start, float yaw)
{
    SDL_zerop(c);
    c->mode = ROOM_CAM_FREE;
    c->position = c->prev_position = start;
    c->yaw = c->prev_yaw = yaw;
    c->pitch = c->prev_pitch = 0.0f;

    /*
     * Proportions de l'auteur de 2020, en mètres : il avait écrit
     * HAUTEUR_CAMERA_DEBOUT 3.5F et HAUTEUR_CAMERA_ACCROUPI 2.7F
     * (legacy/room/room.c:90-91), soit un rapport de 0,771. On garde ce rapport.
     * `room/main.c` MET CES VALEURS À L'ÉCHELLE du décor chargé — l'ancienne
     * salle est en unités Blender, pas en mètres. Il les met à l'échelle et ne
     * les remplace pas : il l'a fait, et le fichier de réglages était alors
     * sans effet sans que rien ne le dise.
     */
    /*
     * TOUTES CES COTES SONT RÉGLABLES SANS RECOMPILER, par `nineteen.env`.
     *
     * Ce ne sont pas des constantes de moteur : ce sont les proportions et les
     * allures d'un PERSONNAGE, c'est-à-dire exactement ce qu'on règle par
     * essais successifs. Les avoir en dur imposait trois minutes de compilation
     * par essai, ce qui revient à ne pas les régler du tout.
     *
     * Les valeurs écrites ici restent les DÉFAUTS, et elles sont toujours
     * justifiées : le fichier absent, le jeu se comporte exactement comme
     * avant. Voir `nineteen.env` pour ce que chacune veut dire.
     */
    c->eye_height_stand  = ns_env_float("personnage.tailleOeil",        1.70f);
    c->eye_height_crouch = ns_env_float("personnage.tailleOeilAccroupi", 1.31f);
    c->eye_height = c->prev_eye_height = c->eye_height_stand;

    c->body_radius        = ns_env_float("personnage.rayon",          0.32f);
    /* Le sommet du crâne, pas les yeux : c'est lui qui cogne les linteaux. */
    c->body_height_stand  = ns_env_float("personnage.taille",         1.82f);
    c->body_height_crouch = ns_env_float("personnage.tailleAccroupi", 1.42f);
    /* Une marche d'escalier confortable. */
    c->step_height        = ns_env_float("personnage.marche",         0.35f);
    c->gravity            = ns_env_float("personnage.gravite",        9.81f);
    c->jump_speed         = ns_env_float("personnage.saut",           3.0f);   /* ~46 cm */

    c->speed_walk   = ns_env_float("personnage.vitesseMarche",    1.4f);
    c->speed_run    = ns_env_float("personnage.vitesseCourse",    3.3f);
    c->speed_crouch = ns_env_float("personnage.vitesseAccroupi",  0.75f);
    c->mouse_sensitivity = ns_env_float("vue.sensibiliteSouris",  0.0022f);
    c->fov_y = ns_env_float("vue.champVertical", 62.0f);
    /*
     * LA SECOUSSE D'UN COUP, en degrés de tangage.
     *
     * 1,6 par défaut, et c'est le SEUL chiffre de tout le geste qui ne se
     * mesure pas — comme `penche_buste` pour l'accroupi, et pour la même
     * raison : il arbitre entre deux défauts opposés dont aucun n'a de seuil
     * objectif. À 0,5 degré on ne voit rien et le coup n'a pas de poids ; à
     * 5 degrés la vue se met à sauter et on le paie en confort.
     *
     * Ce qu'on PEUT dire de 1,6 : le champ vertical vaut 62 degrés, donc une
     * demi-ouverture de 31 ; l'image saute de 5 % de la demi-hauteur de
     * l'écran. C'est du même ordre que ce que l'oscillation de course fait
     * déjà, mais en une seule fois — ce qui est exactement la différence entre
     * marcher et encaisser.
     */
    c->hit_pitch = ns_env_float("vue.secousseFrappe", 1.6f) * NS_DEG2RAD;

    c->grounded = false;
    c->ground_normal = ns_v3_make(0.0f, 1.0f, 0.0f);

    /*
     * La troisième personne, éteinte par défaut : ce jeu se joue devant une
     * borne, et une borne se regarde de près. Elle s'allume dans `nineteen.env`
     * ou par la touche F10.
     */
    c->third_person   = ns_env_bool ("vue.troisiemePersonne", false);
    c->third_distance = ns_env_float("vue.distance",  2.60f);
    c->third_height   = ns_env_float("vue.hauteur",   1.55f);
    c->third_shoulder = ns_env_float("vue.epaule",    0.45f);

    /*
     * LE BRAS DE CAMÉRA et L'EFFACEMENT DU PERSONNAGE.
     *
     * Les quatre réglages ci-dessous sont les seuls que la troisième personne
     * ajoute, et aucun n'est un nombre choisi : voir `third_arm_tick` et
     * `room_camera_actor_opacity` pour d'où sortent les valeurs, et
     * `nineteen.env` pour ce que les changer coûte.
     */
    c->probe_radius = ns_env_float("vue.rayonCamera",      0.16f);
    c->return_rate  = ns_env_float("vue.retourCamera",     6.0f);
    c->fade_full    = ns_env_float("vue.effacementPlein",  0.45f);
    c->fade_none    = ns_env_float("vue.effacementNul",    1.00f);

    /* Sans personnage déclaré, l'opacité vaut toujours 1 et la foulée reste
     * l'historique. `room_camera_set_actor` remplace les trois. */
    c->actor_half_width = c->actor_sweep_radius = 0.0f;
    c->stride = STRIDE_DEFAULT;

    /* Le bras part DÉPLOYÉ. Le contraire ferait rentrer la caméra depuis le
     * personnage à la première image, ce qui se voit sur une capture de départ
     * et sur elle seule — donc jamais, jusqu'au jour où quelqu'un la regarde. */
    c->third_arm  = c->prev_third_arm  = c->third_distance;
    c->third_side = c->prev_third_side = c->third_shoulder;

    c->orbit_radius = 13.0f;
    c->orbit_height = 4.2f;
    c->orbit_speed  = 0.11f;
}

void room_camera_set_actor(room_camera *c, float half_width, float sweep_radius,
                           float stride)
{
    if (!c) return;
    c->actor_half_width   = ns_maxf(half_width,   0.0f);
    c->actor_sweep_radius = ns_maxf(sweep_radius, 0.0f);
    if (stride > 1e-3f) c->stride = stride;
}

float room_camera_stride(const room_camera *c)
{
    return (c && c->stride > 1e-3f) ? c->stride : STRIDE_DEFAULT;
}

static ns_v3 forward_from_angles(float yaw, float pitch)
{
    const float cp = cosf(pitch);
    return ns_v3_make(cosf(yaw) * cp, sinf(pitch), sinf(yaw) * cp);
}

/* Peut-on se redresser ici ? Un joueur accroupi sous une table doit y rester
 * tant qu'il n'en est pas sorti — sinon il traverse le plateau en se levant. */
static bool has_headroom(const ns_bvh *bvh, ns_v3 feet, float from_height, float to_height,
                         float radius)
{
    if (!bvh || !bvh->loaded || to_height <= from_height) return true;
    const ns_v3 origin = ns_v3_add(feet, ns_v3_make(0.0f, from_height - radius, 0.0f));
    const float distance = (to_height - from_height) + radius;
    return !ns_bvh_occluded(bvh, origin, ns_v3_make(0.0f, 1.0f, 0.0f), distance);
}

/* --------------------------------------------------------------------------
 * Le bras de caméra de la troisième personne
 * --------------------------------------------------------------------------
 *
 * Un RAYON ne suffit pas, et c'est mesurable
 * ------------------------------------------
 * La version précédente tirait un rayon unique du pivot vers l'arrière. Un
 * rayon est une droite sans épaisseur : il passe à côté d'une arête de mur, ne
 * rapporte rien, et la caméra — qui a, elle, un volume, celui de son plan
 * proche — traverse le coin. Le symptôme est classique et se voit une image sur
 * dix en longeant un angle : la salle disparaît, on est dans le mur.
 *
 * Ce qu'on veut balayer est donc un VOLUME. `ns_bvh` n'en propose pas : sa
 * seule fonction volumique, `ns_bvh_move_capsule`, n'est pas non plus un vrai
 * balayage — elle échantillonne la silhouette de la capsule par neuf rayons,
 * trois hauteurs sur trois abscisses de flanc, et retranche le rayon de la
 * capsule. On fait ici la même chose en mieux adapté à un objet ROND : un
 * FAISCEAU de neuf rayons parallèles, le centre plus huit répartis sur un
 * cercle de rayon `vue.rayonCamera` perpendiculaire à la direction, et on garde
 * le plus court. Le volume réellement balayé est le cylindre qui les contient.
 *
 * Ce que ça couvre, et ce que ça ne couvre PAS
 * --------------------------------------------
 * Couvert : les arêtes et les coins, qui sont le cas réel. Un montant de porte,
 * l'angle d'une cloison, le coin d'une borne — tout obstacle plus large que le
 * pas angulaire du faisceau est vu par au moins un rayon.
 *
 * Pas couvert : un obstacle plus MINCE que l'écart entre deux rayons voisins et
 * qui passerait pile entre eux. À 0,16 m de rayon et huit rayons, deux voisins
 * sont à 0,122 m l'un de l'autre au plus loin ; un poteau de moins de douze
 * centimètres de large, abordé sous le bon angle, peut donc encore se glisser
 * entre deux sondes. Un vrai balayage de sphère l'attraperait. On l'accepte
 * parce que la salle n'a rien de si fin sur le trajet d'une caméra — le plus
 * mince est le pied d'une borne, à 4 cm, mais il est sous le pivot — et parce
 * qu'écrire un balayage de sphère contre triangles dans `ns_bvh` est un travail
 * de moteur qu'on ne fait pas pour corriger une caméra.
 *
 * Le retrait du rayon à la fin, et pourquoi il est CONSERVATEUR : un rayon
 * décentré qui touche à `t` dit que le cylindre y arrive ; on recule le centre
 * de `radius` de plus. Pour un mur vu de face, c'est exact. Pour une arête
 * rasante, ça s'arrête un peu trop tôt — quelques centimètres — et c'est le bon
 * sens de l'erreur.
 *
 * L'ÉPAULE aussi est balayée, et ce n'est pas du zèle : le bras ne part pas du
 * pivot mais de `pivot + droite x vue.epaule`, soit 45 cm de côté. En longeant
 * une cloison, ce point-là est DANS le mur, et tout ce qu'on lance depuis
 * l'intérieur d'un mur est faux. On balaie donc d'abord le pas de côté depuis le
 * pivot — qui, lui, est toujours dégagé puisque le joueur y tient — puis le
 * recul depuis le point de côté obtenu.
 */

/* Le plan proche. Déclaré ici parce qu'il sert DEUX fois : à la caméra de rendu,
 * et à la dérivation du seuil d'effacement, qui doit savoir de combien le
 * personnage peut s'approcher avant d'être tranché. */
#define NEAR_PLANE 0.05f

/* Huit rayons sur le cercle, plus le centre. Huit et pas quatre : à quatre, le
 * pas angulaire est de 90 degrés et l'écart entre deux sondes voisines vaut
 * 1,41 fois le rayon, soit 23 cm — la largeur d'un montant de porte, qu'on
 * cherche précisément à ne pas rater. */
#define PROBE_RAYS 8

/*
 * Balaie `wanted` mètres depuis `origin` dans `dir` (unitaire) avec un cylindre
 * de rayon `radius`, et rend la distance réellement disponible, dans [0, wanted].
 */
static float sweep_volume(const ns_bvh *bvh, ns_v3 origin, ns_v3 dir,
                          float wanted, float radius)
{
    if (wanted <= 0.0f) return 0.0f;
    if (!bvh || !bvh->loaded) return wanted;

    /* Une base perpendiculaire à `dir`. Le vecteur de départ est choisi non
     * colinéaire : viser droit en haut avec un « haut » de référence vertical
     * donnerait un produit vectoriel nul, donc une base dégénérée et huit rayons
     * confondus avec le centre. */
    const ns_v3 seed = (SDL_fabsf(dir.y) > 0.9f) ? ns_v3_make(1.0f, 0.0f, 0.0f)
                                                 : ns_v3_make(0.0f, 1.0f, 0.0f);
    const ns_v3 u = ns_v3_norm(ns_v3_cross(dir, seed));
    const ns_v3 v = ns_v3_cross(dir, u);

    /* On sonde `wanted + radius` : au-delà, l'obstacle ne peut plus arrêter le
     * centre en deçà de `wanted`, et le rayon coûterait pour rien. */
    const float reach = wanted + radius;
    float nearest = reach;

    const ns_ray_hit centre = ns_bvh_raycast(bvh, origin, dir, reach);
    if (centre.hit && centre.t < nearest) nearest = centre.t;

    for (int i = 0; i < PROBE_RAYS; ++i) {
        const float a = NS_TAU * (float)i / (float)PROBE_RAYS;
        const ns_v3 off = ns_v3_add(ns_v3_scale(u, cosf(a) * radius),
                                    ns_v3_scale(v, sinf(a) * radius));
        const ns_ray_hit h = ns_bvh_raycast(bvh, ns_v3_add(origin, off), dir, reach);
        if (h.hit && h.t < nearest) nearest = h.t;
    }
    return ns_clampf(nearest - radius, 0.0f, wanted);
}

/*
 * Le bras et l'épaule du pas courant.
 *
 * L'amortissement est ASYMÉTRIQUE, et c'est la règle qui rend une caméra de
 * troisième personne supportable : elle RENTRE d'un coup — un mur qui apparaît
 * ne doit jamais avoir le temps de passer devant l'objectif — et elle RESSORT
 * amortie. Amortir la rentrée laisserait voir l'intérieur du mur pendant la
 * dizaine d'images de la transition ; ne pas amortir la sortie ferait sauter le
 * point de vue de deux mètres dès qu'on dépasse le montant d'une porte.
 */
static void third_arm_tick(room_camera *c, const ns_bvh *bvh, float dt)
{
    if (!c->third_person) {
        /* Éteinte : on garde le bras déployé, pour que l'allumer par F10 ne
         * commence pas par un dépliage d'une seconde. */
        c->third_arm  = c->third_distance;
        c->third_side = c->third_shoulder;
        return;
    }

    const ns_v3 forward = forward_from_angles(c->yaw, c->pitch);
    const ns_v3 right   = ns_v3_norm(ns_v3_cross(forward, ns_v3_make(0.0f, 1.0f, 0.0f)));
    const ns_v3 pivot   = ns_v3_make(c->position.x,
                                     c->position.y - c->eye_height + c->third_height,
                                     c->position.z);
    const float radius  = ns_maxf(c->probe_radius, 0.01f);

    /* 1. Le pas de côté, depuis le pivot — le seul point dont on SAIT qu'il est
     *    dégagé : le joueur s'y tient, et sa capsule fait 32 cm de rayon, donc
     *    plus que la sonde. */
    float side = c->third_shoulder;
    if (side > 1e-4f) {
        side = sweep_volume(bvh, pivot, right, side, radius);
    } else if (side < -1e-4f) {
        side = -sweep_volume(bvh, pivot, ns_v3_scale(right, -1.0f), -side, radius);
    }

    /* 2. Le recul, depuis le point de côté effectivement atteint. */
    const ns_v3 from = ns_v3_add(pivot, ns_v3_scale(right, side));
    const float arm = sweep_volume(bvh, from, ns_v3_scale(forward, -1.0f),
                                   c->third_distance, radius);

    c->third_arm = (arm < c->third_arm) ? arm
                 : ns_damp(c->third_arm, arm, c->return_rate, dt);
    c->third_side = (SDL_fabsf(side) < SDL_fabsf(c->third_side)) ? side
                  : ns_damp(c->third_side, side, c->return_rate, dt);
}

float room_camera_third_arm(const room_camera *c, float alpha)
{
    if (!c) return 0.0f;
    return ns_lerpf(c->prev_third_arm, c->third_arm, alpha);
}

/*
 * L'OPACITÉ DU PERSONNAGE, et la DÉRIVATION de ses deux seuils.
 * =============================================================
 *
 * Le défaut de départ, mesuré sur capture : au démarrage, le joueur est dans le
 * sas, dos à la porte extérieure, avec 69 cm derrière lui. Le bras de caméra
 * s'arrêtait donc à 44 cm, l'objectif se retrouvait contre la tempe du
 * personnage, et sa tête et son épaule occupaient le tiers gauche de l'image.
 * Rien ne disait au personnage de s'effacer.
 *
 * De quoi dépend la gêne
 * ----------------------
 * Le personnage est, vu de la caméra, un cylindre vertical de demi-largeur `W`
 * — MESURÉE sur le modèle par `ns_skin`, sur tout le cycle, en travers. En
 * travers et pas en rond : on le regarde de DOS, un bras balancé vers l'avant
 * est caché derrière le corps et ne prend pas un pixel de plus.
 *
 * Sa distance à l'objectif le long du regard est exactement le bras de caméra
 * `d` : la caméra est en `pivot + droite x epaule - regard x d`, et le décalage
 * d'épaule est perpendiculaire au regard, donc il ne compte pas dans la
 * profondeur.
 *
 * Sa largeur à l'écran, RAPPORTÉE À LA HAUTEUR DE L'IMAGE, vaut alors :
 *
 *      couverture(d) = 2W / (2 d tan(champ/2)) = W / (d tan(champ/2))
 *
 * Rapportée à la HAUTEUR et non à la largeur, et c'est important : le champ
 * réglé, `vue.champVertical`, est le vertical. L'horizontal suit le format de
 * la fenêtre. Un seuil exprimé en largeur d'image ferait donc disparaître le
 * personnage à une distance différente sur un 21/9 et sur un 4/3, ce qui est un
 * réglage qui change tout seul.
 *
 * Les deux seuils
 * ---------------
 * PLEIN tant que la couverture reste sous `vue.effacementPlein` (0,45) :
 *
 *      d_plein = W / (0,45 x tan(31°))
 *              = 0,426 / (0,45 x 0,6009) = 1,58 m
 *
 * EFFACÉ dès qu'elle atteint `vue.effacementNul` (1,00), c'est-à-dire dès que le
 * personnage est aussi large que l'image est haute — il n'y a alors plus d'image
 * autour de lui, seulement lui :
 *
 *      d_nul = W / (1,00 x tan(31°)) = 0,426 / 0,6009 = 0,71 m
 *
 * Et un PLANCHER GÉOMÉTRIQUE, qui n'a rien d'un goût : la caméra ne doit jamais
 * entrer dans le volume que le personnage balaie, plan proche compris. Le rayon
 * balayé du modèle vaut 0,656 m, le plan proche 0,05 :
 *
 *      d_plancher = 0,656 + 0,05 = 0,71 m
 *
 * Les deux tombent au même endroit à trois millimètres près, ce qui n'est pas
 * une coïncidence heureuse mais la même chose dite deux fois : un personnage
 * aussi large que l'image est haute est un personnage dans lequel on est. On
 * garde le plus grand des deux, parce que sur un AUTRE modèle — plus large que
 * profond, ou l'inverse — ils divergeraient.
 *
 * Vérification sur le défaut d'origine : à 0,44 m de bras, la couverture vaut
 * 0,426 / (0,44 x 0,6009) = 1,61. Bien au-delà de 1,00 : effacé.
 * Au recul nominal de 2,60 m elle vaut 0,27, bien en deçà de 0,45 : plein.
 *
 * FONDU plutôt qu'ESCAMOTAGE, et pourquoi
 * ----------------------------------------
 * Un escamotage net aurait été plus simple — un booléen, aucun mélange alpha,
 * aucun tri. On ne l'a pas pris, et la raison est la fréquence à laquelle le
 * seuil est franchi : ce jeu se joue dans un couloir de neuf mètres et des
 * allées de moins de deux, c'est-à-dire là où le bras de caméra se raccourcit
 * en permanence. Un joueur qui recule d'un pas contre un mur ferait CLIGNOTER un
 * corps couvrant le quart de l'écran, plusieurs fois par minute. Le fondu, lui,
 * passe inaperçu — ce qui est exactement le cahier des charges d'un effacement.
 *
 * Le coût est réel et il est écrit dans `ns_render.c` : le mélange n'est pas
 * trié, donc pendant le fondu la zone où le bras croise le torse sort un peu
 * plus dense. Un escamotage n'aurait pas eu ce défaut-là. Il en aurait eu un
 * pire, et permanent.
 */
float room_camera_actor_opacity(const room_camera *c, float arm)
{
    if (!c) return 1.0f;
    /* Pas de personnage déclaré : rien à effacer. C'est le cas quand le modèle
     * manque, et le jeu doit alors se comporter comme avant. */
    if (c->actor_half_width <= 1e-4f) return 1.0f;

    const float half_fov = ns_clampf(c->fov_y, 10.0f, 170.0f) * 0.5f * NS_DEG2RAD;
    const float t = tanf(half_fov);
    if (t < 1e-4f) return 1.0f;

    const float k_full = ns_maxf(c->fade_full, 1e-3f);
    const float k_none = ns_maxf(c->fade_none, k_full + 1e-3f);

    const float d_full = c->actor_half_width / (k_full * t);
    const float d_none = ns_maxf(c->actor_half_width / (k_none * t),
                                 c->actor_sweep_radius + NEAR_PLANE);

    if (d_full <= d_none) return (arm >= d_full) ? 1.0f : 0.0f;
    const float x = ns_clampf((arm - d_none) / (d_full - d_none), 0.0f, 1.0f);
    /* Lissée aux deux bouts : une rampe linéaire a un coude au départ et à
     * l'arrivée, et un coude dans une opacité se voit comme un à-coup. */
    return x * x * (3.0f - 2.0f * x);
}

/* --------------------------------------------------------------------------
 * Animation de la vue
 * -------------------------------------------------------------------------- */

static void bob_tick(room_camera *c, float travelled, float dt, bool just_landed,
                     float landing_speed)
{
    /*
     * La phase avance avec la DISTANCE RÉELLEMENT parcourue, pas avec le temps
     * ni avec la vitesse voulue. Deux conséquences, toutes deux vérifiées par un
     * test :
     *
     *   - à vitesse moitié, on fait deux fois moins de pas sur la même durée —
     *     un pas correspond à une foulée et non à une horloge ;
     *   - plaqué contre un mur, la tête cesse de bouger. Piloter par la vitesse
     *     *souhaitée* laissait le joueur marcher sur place indéfiniment, ce qui
     *     se verra encore plus quand les bruits de pas seront branchés dessus.
     */
    if (c->grounded) {
        c->bob.distance += travelled;
    }

    /* Amplitude : proportionnelle à la vitesse réelle rapportée à la marche,
     * amortie pour qu'un arrêt net ne coupe pas l'oscillation au milieu d'un
     * pas. */
    const float speed = (dt > 1e-6f) ? travelled / dt : 0.0f;
    const float reference = ns_maxf(c->speed_walk, 1e-3f);
    float target = c->grounded ? ns_clampf(speed / reference, 0.0f, 1.6f) : 0.0f;
    c->bob.amount = ns_damp(c->bob.amount, target, 8.0f, dt);

    /* Impulsion d'atterrissage : le corps encaisse. */
    if (just_landed) {
        const float impulse = ns_clampf(landing_speed / 6.0f, 0.0f, 1.0f);
        if (impulse > c->bob.land) c->bob.land = impulse;
    }
    c->bob.land = ns_damp(c->bob.land, 0.0f, 9.0f, dt);

    /* Le contrecoup du poing. 11 par seconde : il ne reste 5 % qu'au bout de
     * 270 ms, donc la vue s'est calmée avant que le bras soit revenu, qui met
     * 500 ms. Plus lent, la secousse survivrait au geste et se lirait comme une
     * panne ; plus rapide, elle passerait sous le seuil de ce qu'on perçoit. */
    c->bob.frappe = ns_damp(c->bob.frappe, 0.0f, 11.0f, dt);

    /* Roulis en virage : piloté par l'entrée latérale, pas par la vitesse —
     * l'intention se lit avant que le corps ne bouge. */
    const float roll_target = -c->input_strafe * 0.9f * NS_DEG2RAD * (c->running ? 1.6f : 1.0f);
    c->bob.roll = ns_damp(c->bob.roll, roll_target, 6.0f, dt);

    /* Respiration : la seule composante pilotée par le temps, et pour cause. */
    c->bob_time += dt;
    c->bob.breath = c->bob_time;
}

/* Déplacement de l'œil dû à l'oscillation, à une phase donnée. */
static ns_v3 bob_offset(const room_view_bob *b, float eye_height, ns_v3 right,
                        float stride)
{
    const float phase = (b->distance / stride) * NS_TAU;
    const float vertical = sinf(phase * 2.0f) * BOB_VERTICAL * b->amount * eye_height;
    const float lateral  = sinf(phase) * BOB_LATERAL * b->amount * eye_height;

    /* Respiration : imperceptible en marchant, visible à l'arrêt. */
    const float rest = 1.0f - ns_clampf(b->amount, 0.0f, 1.0f);
    const float breath = sinf(b->breath * 1.9f) * 0.006f * rest * eye_height;

    ns_v3 offset = ns_v3_make(0.0f, vertical + breath - b->land * 0.11f * eye_height, 0.0f);
    return ns_v3_add(offset, ns_v3_scale(right, lateral));
}

static room_view_bob bob_lerp(const room_view_bob *a, const room_view_bob *b, float t)
{
    room_view_bob o;
    o.distance = ns_lerpf(a->distance, b->distance, t);
    o.amount   = ns_lerpf(a->amount, b->amount, t);
    o.land     = ns_lerpf(a->land, b->land, t);
    o.roll     = ns_lerpf(a->roll, b->roll, t);
    o.breath   = ns_lerpf(a->breath, b->breath, t);
    o.frappe   = ns_lerpf(a->frappe, b->frappe, t);
    return o;
}

/* --------------------------------------------------------------------------
 * Pas de simulation
 * -------------------------------------------------------------------------- */

void room_camera_tick(room_camera *c, const ns_bvh *bvh,
                      const room_blockers *blockers, float dt)
{
    c->prev_position = c->position;
    c->prev_yaw = c->yaw;
    c->prev_pitch = c->pitch;
    c->prev_eye_height = c->eye_height;
    c->prev_bob = c->bob;
    c->prev_third_arm = c->third_arm;
    c->prev_third_side = c->third_side;

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
        c->jump_requested = false;
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

    /* --- direction souhaitée --- */
    const ns_v3 forward = forward_from_angles(c->yaw, c->pitch);
    const ns_v3 flat_forward = ns_v3_norm(ns_v3_make(forward.x, 0.0f, forward.z));
    const ns_v3 right = ns_v3_norm(ns_v3_cross(flat_forward, ns_v3_make(0, 1, 0)));

    if (c->mode == ROOM_CAM_FREE) {
        /* Vol libre : aucune gravité, aucune collision. C'est un outil de
         * développement et de cadrage, pas un mode de jeu. */
        ns_v3 wish = ns_v3_scale(forward, c->input_forward);
        wish = ns_v3_add(wish, ns_v3_scale(right, c->input_strafe));
        wish = ns_v3_add(wish, ns_v3_make(0.0f, c->input_up, 0.0f));
        if (ns_v3_len_sq(wish) > 1e-6f) wish = ns_v3_norm(wish);

        const float speed = c->running ? c->speed_run : c->speed_walk;
        const ns_v3 target_velocity = ns_v3_scale(wish, speed);
        c->velocity.x = ns_damp(c->velocity.x, target_velocity.x, 14.0f, dt);
        c->velocity.y = ns_damp(c->velocity.y, target_velocity.y, 14.0f, dt);
        c->velocity.z = ns_damp(c->velocity.z, target_velocity.z, 14.0f, dt);
        c->position = ns_v3_add(c->position, ns_v3_scale(c->velocity, dt));

        c->jump_requested = false;
        c->grounded = false;
        c->bob.amount = ns_damp(c->bob.amount, 0.0f, 8.0f, dt);
        c->bob.roll   = ns_damp(c->bob.roll, 0.0f, 6.0f, dt);
        return;
    }

    /* ------------------------------------------------------------------ joueur */

    /*
     * Accroupissement. Se relever demande de la place au-dessus : sans ce test,
     * on traverse le plateau d'une table en se levant dessous. La hauteur d'œil
     * est amortie, donc la transition est continue et l'interpolation de rendu
     * la voit.
     */
    const bool want_crouch = c->crouch_held;
    const float feet_y_before = c->position.y - c->eye_height;
    bool crouched = want_crouch;
    if (!want_crouch && c->eye_height < c->eye_height_stand - 1e-3f) {
        const ns_v3 feet = ns_v3_make(c->position.x, feet_y_before, c->position.z);
        if (!has_headroom(bvh, feet, c->body_height_crouch, c->body_height_stand, c->body_radius)) {
            crouched = true;
        }
    }
    const float eye_target  = crouched ? c->eye_height_crouch : c->eye_height_stand;
    const float body_target = crouched ? c->body_height_crouch : c->body_height_stand;
    c->eye_height = ns_damp(c->eye_height, eye_target, 12.0f, dt);

    /* --- vitesse horizontale souhaitée --- */
    ns_v3 wish = ns_v3_scale(flat_forward, c->input_forward);
    wish = ns_v3_add(wish, ns_v3_scale(right, c->input_strafe));
    /* Normaliser empêche la diagonale d'être plus rapide que la ligne droite —
     * un défaut classique, présent dans la V1. */
    if (ns_v3_len_sq(wish) > 1e-6f) wish = ns_v3_norm(wish);

    float speed = c->running ? c->speed_run : c->speed_walk;
    if (crouched) speed = c->speed_crouch;
    /* En l'air on garde de l'autorité, mais réduite : on ne change pas de
     * direction en plein saut comme sur un rail. */
    const float control = c->grounded ? 14.0f : 3.0f;
    const ns_v3 target_velocity = ns_v3_scale(wish, speed);
    c->velocity.x = ns_damp(c->velocity.x, target_velocity.x, control, dt);
    c->velocity.z = ns_damp(c->velocity.z, target_velocity.z, control, dt);

    /* --- vertical --- */
    if (c->jump_requested && c->grounded && !crouched) {
        c->velocity.y = c->jump_speed;
        c->grounded = false;
    }
    c->jump_requested = false;
    c->velocity.y -= c->gravity * dt;
    if (c->velocity.y < -TERMINAL_FALL) c->velocity.y = -TERMINAL_FALL;

    /* --- résolution --- */
    const bool was_grounded = c->grounded;
    const float fall_speed = -c->velocity.y;

    ns_capsule_move mv;
    SDL_zero(mv);
    mv.feet = ns_v3_make(c->position.x, feet_y_before, c->position.z);
    mv.motion = ns_v3_scale(c->velocity, dt);
    mv.radius = c->body_radius;
    mv.height = body_target;
    mv.step_height = c->step_height;
    mv.was_grounded = was_grounded;
    ns_bvh_move_capsule(bvh, &mv);

    /*
     * Les pièces mobiles, APRÈS le BVH et pas avant.
     *
     * Le BVH vient de résoudre le glissement contre les murs ; le dégagement
     * d'un vantail se fait donc sur une position déjà valide, et ne peut pas
     * réintroduire une pénétration dans un mur — au pire, il la laisse, et le
     * pas suivant la reprend. L'inverse (dégager puis laisser le BVH glisser)
     * rendrait le vantail traversable dès qu'un mur est proche, ce qui est
     * précisément la situation d'une porte dans son huisserie.
     */
    push_out_blockers(blockers, mv.radius, mv.height, &mv.position);

    const bool just_landed = (!was_grounded && mv.grounded);
    c->grounded = mv.grounded;
    c->ground_normal = mv.ground_normal;
    c->ground_material = mv.ground_material;

    if (mv.grounded && c->velocity.y < 0.0f) c->velocity.y = 0.0f;
    /* Tête cognée : la capsule a été arrêtée avant d'avoir monté ce qu'on
     * demandait. Garder la vitesse ferait « coller » au plafond. */
    if (c->velocity.y > 0.0f && mv.position.y < mv.feet.y + mv.motion.y - 1e-4f) {
        c->velocity.y = 0.0f;
    }

    c->position = ns_v3_make(mv.position.x, mv.position.y + c->eye_height, mv.position.z);

    /* --- animation --- */
    const float dx = mv.position.x - mv.feet.x;
    const float dz = mv.position.z - mv.feet.z;
    bob_tick(c, sqrtf(dx * dx + dz * dz), dt, just_landed, fall_speed);

    /* Le bras de caméra EN DERNIER : il part du corps résolu, pas de celui du
     * pas précédent. Le calculer avant ferait traîner le point de vue d'un pas
     * derrière le personnage, ce qui est un flottement d'un centimètre à la
     * marche et de trois à la course — assez pour se voir, pas assez pour qu'on
     * trouve pourquoi. */
    third_arm_tick(c, bvh, dt);
}

/* --------------------------------------------------------------------------
 * Résolution pour le rendu
 * -------------------------------------------------------------------------- */

ns_camera room_camera_resolve(const room_camera *c, const ns_bvh *bvh, float alpha)
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
    float pitch = ns_lerpf(c->prev_pitch, c->pitch, alpha);

    /*
     * L'ÉTAT D'OSCILLATION, résolu ICI et non plus dans le seul bloc joueur :
     * le contrecoup de frappe agit sur le TANGAGE, donc avant que la direction
     * du regard soit construite. Le recalculer plus bas obligerait à refaire
     * `forward_from_angles`, et deux directions de regard dans la même image
     * sont exactement le genre de chose qui finit par diverger.
     */
    const room_view_bob b = bob_lerp(&c->prev_bob, &c->bob, alpha);

    /*
     * LE CONTRECOUP, et pourquoi il n'entre PAS dans le lacet ni dans la
     * position simulée.
     *
     * C'est un effet de RENDU, comme l'oscillation de marche et comme `land` :
     * il ne touche ni `c->pitch`, ni la capsule, ni la portée des bras. Un
     * joueur qui frappe ne se met pas à viser ailleurs — sa tête bouge, sa
     * visée non. Écrit dans l'état simulé, le coup ferait dériver l'orientation
     * à chaque fois, et cinquante coups la mettraient au plafond.
     */
    if (c->mode == ROOM_CAM_PLAYER && b.frappe > 0.0f) {
        pitch = ns_clampf(pitch + b.frappe * c->hit_pitch, -PITCH_LIMIT, PITCH_LIMIT);
    }

    cam.forward = forward_from_angles(yaw, pitch);
    cam.up = ns_v3_make(0.0f, 1.0f, 0.0f);
    cam.fov_y_degrees = c->fov_y;

    if (c->mode == ROOM_CAM_PLAYER) {
        /*
         * L'oscillation est appliquée ICI, sur l'état interpolé, et non dans le
         * pas de simulation : ajoutée côté simulation elle serait échantillonnée
         * à 120 Hz puis interpolée linéairement entre deux échantillons, ce qui
         * aplatit les sommets de la sinusoïde. Appliquée après, elle est exacte
         * à la fréquence d'affichage.
         */
        const float eye = ns_lerpf(c->prev_eye_height, c->eye_height, alpha);
        const ns_v3 flat_forward = ns_v3_norm(ns_v3_make(cam.forward.x, 0.0f, cam.forward.z));
        const ns_v3 right = ns_v3_norm(ns_v3_cross(flat_forward, ns_v3_make(0, 1, 0)));

        cam.position = ns_v3_add(cam.position, bob_offset(&b, eye, right,
                                                          room_camera_stride(c)));

        /* Roulis : incliner le vecteur « haut » plutôt que tourner la vue.
         * C'est ce que fait l'oreille interne, et ça ne perturbe pas la visée. */
        cam.up = ns_v3_norm(ns_v3_add(cam.up, ns_v3_scale(right, sinf(b.roll))));
    }

    /*
     * LE RECUL de la troisième personne, appliqué en tout dernier — après
     * l'oscillation, qui doit rester celle du CORPS et non celle de la caméra.
     *
     * Le point visé est à `third_height` au-dessus des pieds ; la caméra s'en
     * écarte à reculons le long du regard, plus un décalage d'épaule. Sans ce
     * décalage on vise exactement là où le personnage se tient, et il masque
     * ce qu'on regarde.
     */
    if (c->mode == ROOM_CAM_PLAYER && c->third_person && bvh) {
        const float eye = ns_lerpf(c->prev_eye_height, c->eye_height, alpha);
        const ns_v3 feet = ns_v3_make(cam.position.x, cam.position.y - eye, cam.position.z);
        const ns_v3 pivot = ns_v3_make(feet.x, feet.y + c->third_height, feet.z);
        const ns_v3 right = ns_v3_norm(ns_v3_cross(cam.forward, ns_v3_make(0, 1, 0)));

        /*
         * Le bras et l'épaule sont SIMULÉS, pas recalculés ici : c'est
         * `third_arm_tick` qui balaie le volume, au pas fixe, et on ne fait plus
         * qu'interpoler ses deux longueurs — comme la position et les angles.
         *
         * Ce qui a disparu avec lui : le rayon unique, sa marge de 25 cm, et
         * surtout le plancher de 0,35 m. Ce plancher n'était pas le bug, il en
         * était la conséquence — il gardait un recul « pour qu'il reste quelque
         * chose à voir », et ce quelque chose était l'intérieur du crâne. Le
         * bras peut maintenant tomber à zéro, et c'est la bonne dégradation :
         * le point de vue rejoint l'épaule du personnage, qui est à ce moment-là
         * complètement effacé. On retombe donc sur une vue subjective décalée,
         * ce qui est jouable, plutôt que sur un plan fixe de nuque.
         */
        const ns_v3 want = ns_v3_add(pivot,
                                     ns_v3_scale(right, ns_lerpf(c->prev_third_side,
                                                                 c->third_side, alpha)));
        const ns_v3 back = ns_v3_scale(cam.forward, -1.0f);
        const float dist = room_camera_third_arm(c, alpha);
        cam.position = ns_v3_add(want, ns_v3_scale(back, dist));
    }

    /* Plan proche généreux : la salle fait une trentaine de mètres, et avec le
     * reverse-Z la précision de profondeur reste excellente même à 5 cm. */
    cam.znear = NEAR_PLANE;
    cam.zfar  = 160.0f;
    return cam;
}

room_view_bob room_camera_bob(const room_camera *c, float alpha)
{
    return bob_lerp(&c->prev_bob, &c->bob, alpha);
}

void room_camera_frappe(room_camera *c)
{
    if (!c) return;
    /* On POSE à 1 plutôt que d'ajouter : deux coups coup sur coup ne doivent
     * pas empiler deux secousses, ce qui doublerait l'amplitude et sortirait du
     * réglage. C'est la même règle que `land`, qui garde le plus fort des deux
     * plutôt que leur somme. */
    c->bob.frappe = 1.0f;
}
