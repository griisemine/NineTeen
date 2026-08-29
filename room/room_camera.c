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

/* Longueur d'une foulée complète (deux pas). L'oscillation verticale a deux
 * maxima par foulée — un par pied — et l'oscillation latérale un seul. */
#define STRIDE_METRES 1.55f

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
     * `room/main.c` remplace ces valeurs par les mêmes converties à l'échelle du
     * décor chargé — l'ancienne salle est en unités Blender, pas en mètres.
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

    c->orbit_radius = 13.0f;
    c->orbit_height = 4.2f;
    c->orbit_speed  = 0.11f;
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

    /* Roulis en virage : piloté par l'entrée latérale, pas par la vitesse —
     * l'intention se lit avant que le corps ne bouge. */
    const float roll_target = -c->input_strafe * 0.9f * NS_DEG2RAD * (c->running ? 1.6f : 1.0f);
    c->bob.roll = ns_damp(c->bob.roll, roll_target, 6.0f, dt);

    /* Respiration : la seule composante pilotée par le temps, et pour cause. */
    c->bob_time += dt;
    c->bob.breath = c->bob_time;
}

/* Déplacement de l'œil dû à l'oscillation, à une phase donnée. */
static ns_v3 bob_offset(const room_view_bob *b, float eye_height, ns_v3 right)
{
    const float phase = (b->distance / STRIDE_METRES) * NS_TAU;
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
    const float pitch = ns_lerpf(c->prev_pitch, c->pitch, alpha);

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
        const room_view_bob b = bob_lerp(&c->prev_bob, &c->bob, alpha);
        const float eye = ns_lerpf(c->prev_eye_height, c->eye_height, alpha);
        const ns_v3 flat_forward = ns_v3_norm(ns_v3_make(cam.forward.x, 0.0f, cam.forward.z));
        const ns_v3 right = ns_v3_norm(ns_v3_cross(flat_forward, ns_v3_make(0, 1, 0)));

        cam.position = ns_v3_add(cam.position, bob_offset(&b, eye, right));

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

        ns_v3 want = ns_v3_add(pivot, ns_v3_scale(right, c->third_shoulder));
        const ns_v3 back = ns_v3_scale(cam.forward, -1.0f);

        /*
         * La caméra ne traverse pas les murs, et c'est un rayon qui le dit.
         *
         * Sans lui, reculer de 2,60 m dans une allée de 1,80 m met le point de
         * vue DANS la borne d'en face : on voit l'intérieur du meuble, ou pire,
         * on voit à travers lui le reste de la salle. Le rayon part du pivot —
         * qui est toujours dans le vide, puisque le personnage y tient — et
         * s'arrête à ce qu'il touche, moins une marge.
         */
        float dist = c->third_distance;
        const ns_ray_hit hit = ns_bvh_raycast(bvh, want, back, dist + 0.25f);
        /* `back` est normalisé, donc `t` est bien une distance en mètres. */
        if (hit.hit && hit.t < dist + 0.25f) {
            dist = ns_maxf(0.35f, hit.t - 0.25f);
        }
        cam.position = ns_v3_add(want, ns_v3_scale(back, dist));
    }

    /* Plan proche généreux : la salle fait une trentaine de mètres, et avec le
     * reverse-Z la précision de profondeur reste excellente même à 5 cm. */
    cam.znear = 0.05f;
    cam.zfar  = 160.0f;
    return cam;
}

room_view_bob room_camera_bob(const room_camera *c, float alpha)
{
    return bob_lerp(&c->prev_bob, &c->bob, alpha);
}
