/*
 * test_bvh.c — la traversée du BVH, la collision en capsule, et la caméra.
 *
 * Ces quatre fonctions — `ns_bvh_raycast`, `ns_bvh_occluded`,
 * `ns_bvh_occlusion_factor`, `ns_bvh_move_capsule` — portent la collision du
 * joueur et l'occlusion audio. Aucune n'était testée, et `docs/CHANGELOG-V15.md`
 * affirmait pourtant que l'occlusion audio l'était. La phrase est corrigée, et
 * voici les tests qui manquaient.
 *
 * Le BVH est construit à la main, en mémoire : pas de fichier, pas de GPU, pas
 * d'asset. Un nœud racine unique contenant tous les triangles est un BVH valide
 * — dégénéré, mais la traversée ne fait pas la différence, et ce qu'on teste ici
 * est l'intersection et la réponse, pas la qualité de la partition.
 */
#include "ns_bvh.h"
#include "ns_core.h"
#include "room_camera.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            fprintf(stderr, "ÉCHEC %s:%d — ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
        }                                                                     \
    } while (0)

#define CHECK_NEAR(got, want, tol)                                            \
    CHECK(fabsf((float)(got) - (float)(want)) <= (float)(tol),                \
          "%s = %.5f, attendu %.5f (± %.5f)", #got, (double)(got),            \
          (double)(want), (double)(tol))

/* ==========================================================================
 * Construction d'un BVH à la main
 * ========================================================================== */

#define MAX_TRIS 64

typedef struct scratch_bvh {
    ns_bvh          bvh;
    ns_bvh_node     node;
    ns_bvh_tri      tris[MAX_TRIS];
    ns_bvh_material material;
} scratch_bvh;

static void put3(float *dst, ns_v3 v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }

static void add_tri(scratch_bvh *s, ns_v3 a, ns_v3 b, ns_v3 c, ns_v3 normal, uint32_t material)
{
    if (s->bvh.tri_count >= MAX_TRIS) { fprintf(stderr, "trop de triangles\n"); exit(2); }
    ns_bvh_tri *t = &s->tris[s->bvh.tri_count++];
    memset(t, 0, sizeof *t);
    put3(t->v0, a);
    put3(t->e1, ns_v3_sub(b, a));
    put3(t->e2, ns_v3_sub(c, a));
    put3(t->normal, ns_v3_norm(normal));
    t->material = material;
}

/* Un quad plan, donné par un coin et deux vecteurs de côté. La normale est
 * fournie explicitement : l'enroulement ne porte rien ici (l'intersection ne
 * fait pas de culling), mais la normale sert au glissement. */
static void add_quad(scratch_bvh *s, ns_v3 origin, ns_v3 u, ns_v3 v, ns_v3 normal, uint32_t mat)
{
    const ns_v3 a = origin;
    const ns_v3 b = ns_v3_add(origin, u);
    const ns_v3 c = ns_v3_add(ns_v3_add(origin, u), v);
    const ns_v3 d = ns_v3_add(origin, v);
    add_tri(s, a, b, c, normal, mat);
    add_tri(s, a, c, d, normal, mat);
}

static void finish(scratch_bvh *s)
{
    ns_aabb bounds;
    bounds.min = ns_v3_make(1e30f, 1e30f, 1e30f);
    bounds.max = ns_v3_make(-1e30f, -1e30f, -1e30f);
    for (uint32_t i = 0; i < s->bvh.tri_count; ++i) {
        const ns_bvh_tri *t = &s->tris[i];
        const ns_v3 v0 = ns_v3_make(t->v0[0], t->v0[1], t->v0[2]);
        const ns_v3 v1 = ns_v3_add(v0, ns_v3_make(t->e1[0], t->e1[1], t->e1[2]));
        const ns_v3 v2 = ns_v3_add(v0, ns_v3_make(t->e2[0], t->e2[1], t->e2[2]));
        const ns_v3 all[3] = { v0, v1, v2 };
        for (int k = 0; k < 3; ++k) {
            bounds.min = ns_v3_min(bounds.min, all[k]);
            bounds.max = ns_v3_max(bounds.max, all[k]);
        }
    }
    /* Marge : une boîte exactement rasante rejette les rayons tangents. */
    bounds.min = ns_v3_sub(bounds.min, ns_v3_make(0.01f, 0.01f, 0.01f));
    bounds.max = ns_v3_add(bounds.max, ns_v3_make(0.01f, 0.01f, 0.01f));

    put3(s->node.bmin, bounds.min);
    put3(s->node.bmax, bounds.max);
    s->node.left_first = 0;
    s->node.tri_count = s->bvh.tri_count;

    s->material.albedo[0] = s->material.albedo[1] = s->material.albedo[2] = 0.5f;
    s->material.roughness = 0.8f;

    s->bvh.nodes = &s->node;
    s->bvh.tris = s->tris;
    s->bvh.materials = &s->material;
    s->bvh.node_count = 1;
    s->bvh.material_count = 1;
    s->bvh.bounds = bounds;
    s->bvh.max_depth = 1;
    s->bvh.loaded = true;
}

/*
 * La salle d'essai :
 *   - un sol de 10 × 10 m à y = 0 ;
 *   - un mur plein à x = +2, hauteur 3 m, normale vers −x ;
 *   - une marche de 20 cm (x de −3 à −1, z de −1 à 1), franchissable ;
 *   - un bloc de 60 cm (x de −3 à −1, z de 2 à 4), infranchissable.
 */
static void build_room(scratch_bvh *s)
{
    memset(s, 0, sizeof *s);

    add_quad(s, ns_v3_make(-5, 0, -5), ns_v3_make(10, 0, 0), ns_v3_make(0, 0, 10),
             ns_v3_make(0, 1, 0), 0);

    add_quad(s, ns_v3_make(2, 0, -5), ns_v3_make(0, 3, 0), ns_v3_make(0, 0, 10),
             ns_v3_make(-1, 0, 0), 0);

    /* Marche : le dessus, et la contremarche côté +x. */
    add_quad(s, ns_v3_make(-3, 0.20f, -1), ns_v3_make(2, 0, 0), ns_v3_make(0, 0, 2),
             ns_v3_make(0, 1, 0), 0);
    add_quad(s, ns_v3_make(-1, 0, -1), ns_v3_make(0, 0.20f, 0), ns_v3_make(0, 0, 2),
             ns_v3_make(1, 0, 0), 0);

    /* Bloc haut : le dessus, et la face côté −z, celle qu'on aborde. */
    add_quad(s, ns_v3_make(-3, 0.60f, 2), ns_v3_make(2, 0, 0), ns_v3_make(0, 0, 2),
             ns_v3_make(0, 1, 0), 0);
    add_quad(s, ns_v3_make(-3, 0, 2), ns_v3_make(2, 0, 0), ns_v3_make(0, 0.60f, 0),
             ns_v3_make(0, 0, -1), 0);

    finish(s);
}

/* ==========================================================================
 * Rayons
 * ========================================================================== */

static void test_raycast(const ns_bvh *b)
{
    /* Depuis 2 m au-dessus du sol, vers le bas : contact à 2 m, normale en haut. */
    ns_ray_hit h = ns_bvh_raycast(b, ns_v3_make(0, 2, 0), ns_v3_make(0, -1, 0), 10.0f);
    CHECK(h.hit, "le sol devrait être touché");
    CHECK_NEAR(h.t, 2.0f, 1e-3f);
    CHECK_NEAR(h.position.y, 0.0f, 1e-3f);
    CHECK_NEAR(h.normal.y, 1.0f, 1e-3f);

    /* Vers le haut : il n'y a rien. */
    h = ns_bvh_raycast(b, ns_v3_make(0, 1, 0), ns_v3_make(0, 1, 0), 10.0f);
    CHECK(!h.hit, "rien ne devrait être touché vers le haut");

    /* Le mur, à 2 m devant. */
    h = ns_bvh_raycast(b, ns_v3_make(0, 1, 0), ns_v3_make(1, 0, 0), 10.0f);
    CHECK(h.hit, "le mur devrait être touché");
    CHECK_NEAR(h.t, 2.0f, 1e-3f);
    CHECK_NEAR(h.normal.x, -1.0f, 1e-3f);

    /* Distance maximale respectée : le mur est à 2 m, on cherche jusqu'à 1 m. */
    h = ns_bvh_raycast(b, ns_v3_make(0, 1, 0), ns_v3_make(1, 0, 0), 1.0f);
    CHECK(!h.hit, "un contact au-delà de la distance maximale ne doit pas compter");

    /* Le mur s'arrête à 3 m de haut. */
    h = ns_bvh_raycast(b, ns_v3_make(0, 3.5f, 0), ns_v3_make(1, 0, 0), 10.0f);
    CHECK(!h.hit, "au-dessus du mur, le rayon passe");
}

static void test_occluded(const ns_bvh *b)
{
    CHECK(ns_bvh_occluded(b, ns_v3_make(0, 1, 0), ns_v3_make(1, 0, 0), 10.0f),
          "le mur devrait occulter");
    CHECK(!ns_bvh_occluded(b, ns_v3_make(0, 1, 0), ns_v3_make(-1, 0, 0), 1.0f),
          "rien ne devrait occulter vers −x sur 1 m");

    /* Doit coïncider avec le raycast sur toute une série de directions : deux
     * implémentations de la même question qui divergeraient donneraient des
     * ombres et un audio en désaccord avec la collision. */
    for (int i = 0; i < 32; ++i) {
        const float a = (float)i / 32.0f * NS_TAU;
        const ns_v3 dir = ns_v3_make(cosf(a), 0.0f, sinf(a));
        const ns_ray_hit h = ns_bvh_raycast(b, ns_v3_make(0, 1, 0), dir, 6.0f);
        CHECK(h.hit == ns_bvh_occluded(b, ns_v3_make(0, 1, 0), dir, 6.0f),
              "raycast et occluded en désaccord à l'angle %.0f°", (double)(a * NS_RAD2DEG));
    }
}

static void test_occlusion_factor(const ns_bvh *b)
{
    /* Trajet dégagé : aucune atténuation. */
    const float clear = ns_bvh_occlusion_factor(b, ns_v3_make(0, 1, 0), ns_v3_make(-3, 1, 0));
    CHECK_NEAR(clear, 1.0f, 1e-4f);

    /* Derrière le mur : atténué, jamais coupé. Le plancher de 0,18 est
     * intentionnel — une cloison n'est pas un isolant parfait, et couper à zéro
     * s'entend comme un défaut. */
    const float blocked = ns_bvh_occlusion_factor(b, ns_v3_make(0, 1, 0), ns_v3_make(4, 1, 0));
    CHECK(blocked < 1.0f, "une source derrière le mur devrait être atténuée (%.3f)", (double)blocked);
    CHECK(blocked >= 0.18f - 1e-4f, "l'atténuation ne doit jamais couper le son (%.3f)", (double)blocked);

    /* Sans BVH, pas d'occlusion : le repli doit être audible, pas muet. */
    ns_bvh empty;
    memset(&empty, 0, sizeof empty);
    CHECK_NEAR(ns_bvh_occlusion_factor(&empty, ns_v3_make(0, 1, 0), ns_v3_make(9, 1, 0)), 1.0f, 1e-6f);
}

/* ==========================================================================
 * Capsule
 * ========================================================================== */

static ns_capsule_move move(const ns_bvh *b, ns_v3 feet, ns_v3 motion, bool grounded)
{
    ns_capsule_move m;
    memset(&m, 0, sizeof m);
    m.feet = feet;
    m.motion = motion;
    m.radius = 0.32f;
    m.height = 1.82f;
    m.step_height = 0.35f;
    m.was_grounded = grounded;
    ns_bvh_move_capsule(b, &m);
    return m;
}

static void test_capsule(const ns_bvh *b)
{
    /* --- un mur ne se traverse pas --- */
    {
        ns_v3 feet = ns_v3_make(0, 0, 0);
        for (int i = 0; i < 200; ++i) {
            const ns_capsule_move m = move(b, feet, ns_v3_make(0.05f, -0.01f, 0.0f), true);
            feet = m.position;
        }
        CHECK(feet.x <= 2.0f - 0.32f + 1e-2f,
              "la capsule a traversé le mur : x = %.4f (limite %.4f)",
              (double)feet.x, (double)(2.0f - 0.32f));
        CHECK(feet.x > 1.0f, "la capsule n'a pas avancé jusqu'au mur : x = %.4f", (double)feet.x);
    }

    /* --- glissement : une approche en biais garde la composante tangente --- */
    {
        const ns_capsule_move m = move(b, ns_v3_make(1.5f, 0, 0),
                                       ns_v3_make(0.5f, 0.0f, 0.5f), true);
        CHECK(m.touched_wall, "le mur aurait dû être touché");
        CHECK(m.position.z > 0.2f,
              "le glissement devrait conserver le déplacement le long du mur (z = %.4f)",
              (double)m.position.z);
    }

    /* --- une marche de 20 cm se franchit --- */
    {
        ns_v3 feet = ns_v3_make(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 60; ++i) {
            const ns_capsule_move m = move(b, feet, ns_v3_make(-0.03f, -0.005f, 0.0f), true);
            feet = m.position;
        }
        CHECK_NEAR(feet.y, 0.20f, 1e-2f);
        CHECK(feet.x < -1.0f, "on aurait dû monter sur la marche (x = %.3f)", (double)feet.x);
    }

    /* --- un bloc de 60 cm ne se franchit pas --- */
    {
        ns_v3 feet = ns_v3_make(-2.0f, 0.0f, 0.0f);
        for (int i = 0; i < 200; ++i) {
            const ns_capsule_move m = move(b, feet, ns_v3_make(0.0f, -0.005f, 0.03f), true);
            feet = m.position;
        }
        CHECK(feet.y < 0.05f, "un bloc de 60 cm ne doit pas se gravir (y = %.3f)", (double)feet.y);
        CHECK(feet.z <= 2.0f - 0.32f + 1e-2f,
              "le bloc n'a pas arrêté la capsule (z = %.3f)", (double)feet.z);
    }

    /* --- la chute se pose sur le sol --- */
    {
        ns_v3 feet = ns_v3_make(0, 3.0f, 0);
        bool grounded = false;
        float vy = 0.0f;
        for (int i = 0; i < 600; ++i) {
            vy -= 9.81f * (1.0f / 120.0f);
            const ns_capsule_move m = move(b, feet, ns_v3_make(0, vy / 120.0f, 0), grounded);
            feet = m.position;
            grounded = m.grounded;
            if (grounded) vy = 0.0f;
        }
        CHECK(grounded, "la capsule devrait reposer sur le sol");
        CHECK_NEAR(feet.y, 0.0f, 1e-3f);
    }

    /* --- un saut n'est pas annulé par le recollement au sol ---
     *
     * C'est le piège de cette fonction : au premier pas d'un saut, les pieds
     * n'ont quitté le sol que de quelques millimètres. Recoller sans regarder le
     * signe du déplacement vertical ramènerait le joueur au sol à chaque fois, et
     * le saut serait « sans effet » sans qu'aucun message ne l'explique. */
    {
        const ns_capsule_move m = move(b, ns_v3_make(0, 0, 0),
                                       ns_v3_make(0, 3.0f / 120.0f, 0), true);
        CHECK(!m.grounded, "le premier pas d'un saut ne doit pas être recollé au sol");
        CHECK(m.position.y > 0.02f, "le saut n'a pas décollé (y = %.4f)", (double)m.position.y);
    }

    /* --- sans BVH, on se déplace librement plutôt que d'être cloué --- */
    {
        ns_bvh empty;
        memset(&empty, 0, sizeof empty);
        const ns_capsule_move m = move(&empty, ns_v3_make(0, 0, 0), ns_v3_make(1, 2, 3), true);
        CHECK_NEAR(m.position.x, 1.0f, 1e-6f);
        CHECK_NEAR(m.position.y, 2.0f, 1e-6f);
        CHECK_NEAR(m.position.z, 3.0f, 1e-6f);
    }
}

/* ==========================================================================
 * Caméra
 * ========================================================================== */

static void run_camera(const ns_bvh *b, room_camera *c, ns_v3 start, float yaw,
                       float speed, int ticks)
{
    room_camera_init(c, start, yaw);
    c->mode = ROOM_CAM_PLAYER;
    c->speed_walk = speed;
    c->speed_run = speed;
    c->grounded = true;
    for (int i = 0; i < ticks; ++i) {
        c->input_forward = 1.0f;
        room_camera_tick(c, b, 1.0f / 120.0f);
    }
}

/* Un couloir dégagé : à z = −3 il n'y a ni marche ni bloc, et le sol s'étend
 * jusqu'à x = −5. Marcher vers −x depuis l'origine y est sans obstacle. */
#define CLEAR_START ns_v3_make(0.0f, 1.70f, -3.0f)
#define CLEAR_YAW   NS_PI

static void test_camera(const ns_bvh *b)
{
    /* --- déterminisme : même entrée, même trajectoire, au bit près --- */
    {
        room_camera a, d;
        run_camera(b, &a, CLEAR_START, CLEAR_YAW, 1.4f, 10000);
        run_camera(b, &d, CLEAR_START, CLEAR_YAW, 1.4f, 10000);
        CHECK(memcmp(&a.position, &d.position, sizeof a.position) == 0,
              "10 000 pas identiques devraient donner la même position "
              "(%.6f,%.6f,%.6f) vs (%.6f,%.6f,%.6f)",
              (double)a.position.x, (double)a.position.y, (double)a.position.z,
              (double)d.position.x, (double)d.position.y, (double)d.position.z);
        CHECK(memcmp(&a.bob, &d.bob, sizeof a.bob) == 0,
              "l'état d'animation devrait être identique lui aussi");
    }

    /*
     * --- l'oscillation avance avec la distance, pas avec le temps ---
     *
     * À vitesse moitié, sur la même durée, on doit faire deux fois moins de pas.
     * Piloter la phase par une horloge — l'erreur classique — donnerait un
     * rapport de 1 : le joueur marcherait au pas cadencé quelle que soit sa
     * vitesse, ce qui se voit et s'entend dès qu'on y branche les bruits de pas.
     */
    {
        room_camera fast, slow;
        run_camera(b, &fast, CLEAR_START, CLEAR_YAW, 1.4f, 300);
        run_camera(b, &slow, CLEAR_START, CLEAR_YAW, 0.7f, 300);

        CHECK(fast.bob.distance > 2.0f, "la distance parcourue devrait être notable (%.3f)",
              (double)fast.bob.distance);
        const float ratio = fast.bob.distance / ns_maxf(slow.bob.distance, 1e-6f);
        CHECK_NEAR(ratio, 2.0f, 0.02f);

        /* Et la distance d'oscillation doit être la distance réelle : on marche
         * vers −x depuis x = 0. */
        CHECK_NEAR(fast.bob.distance, -fast.position.x, 0.05f);
    }

    /* --- la hauteur d'yeux est enfin lue : accroupi, on descend --- */
    {
        room_camera c;
        run_camera(b, &c, CLEAR_START, CLEAR_YAW, 1.4f, 10);
        const float standing = c.position.y;
        c.crouch_held = true;
        for (int i = 0; i < 240; ++i) {
            c.input_forward = 0.0f;
            room_camera_tick(&c, b, 1.0f / 120.0f);
        }
        CHECK(c.position.y < standing - 0.3f,
              "s'accroupir devrait baisser l'œil (%.3f -> %.3f)",
              (double)standing, (double)c.position.y);
        CHECK_NEAR(c.eye_height, 1.31f, 0.01f);
    }

    /* --- le joueur ne traverse pas le mur en marchant dessus --- */
    {
        room_camera c;
        run_camera(b, &c, ns_v3_make(0.0f, 1.70f, -3.0f), 0.0f, 3.0f, 1200);
        CHECK(c.position.x <= 2.0f - 0.32f + 1e-2f,
              "le joueur a traversé le mur (x = %.4f)", (double)c.position.x);

        /* Et plaqué contre lui, la tête cesse d'osciller : l'oscillation suit la
         * distance réellement parcourue, pas la vitesse demandée. */
        CHECK(c.bob.amount < 0.05f,
              "marcher contre un mur ne doit pas faire osciller la tête (%.3f)",
              (double)c.bob.amount);
    }

    /* --- le mode libre ignore la collision, c'est son rôle --- */
    {
        room_camera c;
        room_camera_init(&c, ns_v3_make(0.0f, 1.70f, -3.0f), 0.0f);
        c.mode = ROOM_CAM_FREE;
        c.speed_walk = c.speed_run = 3.0f;
        for (int i = 0; i < 600; ++i) {
            c.input_forward = 1.0f;
            room_camera_tick(&c, b, 1.0f / 120.0f);
        }
        CHECK(c.position.x > 3.0f,
              "le vol libre devrait traverser le mur (x = %.3f)", (double)c.position.x);
    }
}

/* ========================================================================== */

int main(void)
{
    printf("BVH, collision et caméra\n");

    scratch_bvh s;
    build_room(&s);

    test_raycast(&s.bvh);
    test_occluded(&s.bvh);
    test_occlusion_factor(&s.bvh);
    test_capsule(&s.bvh);
    test_camera(&s.bvh);

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
