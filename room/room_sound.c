/* room_sound.c — voir room_sound.h pour le raisonnement. */
#include "room_sound.h"

#include "ns_core.h"

#include <string.h>

/* Un pas tous les demi-pas de foulée. `STRIDE_METRES` vaut 1,55 m dans
 * room_camera.c : un cycle complet, donc deux pas. La constante est dupliquée
 * pour la même raison que dans le viewmodel — un module de son n'a pas à inclure
 * le .c d'une caméra — et vérifiée par le test. */
#define RS_STRIDE_METRES 1.55f
#define RS_STEP_METRES   (RS_STRIDE_METRES * 0.5f)

/* Portées. Une borne ne s'entend pas d'un bout à l'autre de la salle : c'est ce
 * qui permet d'en avoir dix-neuf sans que ça devienne une bouillie. */
#define RS_CABINET_RADIUS 0.9f
#define RS_CABINET_MAX    5.5f

typedef struct step_voice {
    float gain_min, gain_max;
    float pitch_min, pitch_max;
} step_voice;

/*
 * Un seul enregistrement de pas existe (`walk.wav`, 2020). Les matériaux se
 * distinguent donc par la hauteur et le gain, pas par l'échantillon — c'est une
 * approximation, elle est dite, et elle suffit à ce que la moquette et le
 * carrelage ne se confondent pas : le carrelage est plus haut et plus fort, la
 * moquette plus sourde et plus faible. Une vraie banque par matériau viendra
 * avec de vrais enregistrements.
 */
static const step_voice g_step_voice[NS_STEP_COUNT] = {
    /* NONE      */ { 0.00f, 0.00f, 1.00f, 1.00f },
    /* MOQUETTE  */ { 0.20f, 0.28f, 0.82f, 0.92f },
    /* CARRELAGE */ { 0.42f, 0.54f, 1.18f, 1.34f },
    /* BOIS      */ { 0.34f, 0.44f, 1.00f, 1.12f },
    /* BETON     */ { 0.38f, 0.48f, 1.06f, 1.20f },
    /* ESTRADE   */ { 0.44f, 0.56f, 0.92f, 1.04f },
};

static float rand_range(ns_rng *r, float lo, float hi)
{
    const float t = (float)(ns_rng_u32(r) >> 8) / (float)(1u << 24);
    return lo + (hi - lo) * t;
}

/* --------------------------------------------------------------------------
 * Mise en place
 * -------------------------------------------------------------------------- */

void room_sound_init(room_sound *s, const ns_scene *scene)
{
    memset(s, 0, sizeof *s);
    s->clip_walk = s->clip_ambience = NS_AUDIO_INVALID;
    s->clip_door_open = s->clip_door_close = NS_AUDIO_INVALID;
    for (int i = 0; i < 3; ++i) s->clip_cabinet[i] = NS_AUDIO_INVALID;
    s->voice_ambience = NS_AUDIO_INVALID;
    for (int i = 0; i < NS_MAX_CABINETS; ++i) s->voice_cabinet[i] = NS_AUDIO_INVALID;
    s->left_foot = true;

    if (!ns_audio_ready()) return;

    s->clip_walk       = ns_audio_load("sounds/walk.wav");
    s->clip_ambience   = ns_audio_load("sounds/background.wav");
    s->clip_cabinet[0] = ns_audio_load("sounds/borne1.wav");
    s->clip_cabinet[1] = ns_audio_load("sounds/borne2.wav");
    s->clip_cabinet[2] = ns_audio_load("sounds/borne3.wav");
    s->clip_door_open  = ns_audio_load("sounds/SF-ouvport.wav");
    s->clip_door_close = ns_audio_load("sounds/SF-fermport.wav");

    /*
     * La nappe d'ambiance n'est PAS spatialisée : elle n'a pas de position, elle
     * est la salle. La spatialiser reviendrait à la faire venir d'un point, et à
     * la voir tourner quand le joueur tourne la tête.
     */
    if (s->clip_ambience >= 0) {
        s->voice_ambience = ns_audio_loop(s->clip_ambience, NS_BUS_AMBIENCE, 0.34f);
    }

    /*
     * Une boucle par borne, à sa position déclarée, en alternant les trois
     * enregistrements. Sans l'alternance, dix-neuf sources jouant le même son en
     * phase produisent un peigne : les mêmes fréquences s'annulent et se
     * renforcent selon l'endroit où l'on se tient, et ça s'entend comme un
     * sifflement qui suit le joueur.
     */
    if (scene) {
        for (uint32_t i = 0; i < scene->cabinet_count && i < NS_MAX_CABINETS; ++i) {
            const int clip = s->clip_cabinet[i % 3];
            if (clip < 0) continue;
            const ns_cabinet *c = &scene->cabinets[i];
            s->voice_cabinet[i] = ns_audio_loop_3d(
                clip, NS_BUS_SFX, c->screen_center,
                0.30f, 0.94f + 0.04f * (float)(i % 4),
                RS_CABINET_RADIUS, RS_CABINET_MAX);
            if (s->voice_cabinet[i] >= 0) s->cabinet_voices++;
        }
    }

    s->ready = true;
    NS_INFO("son : %d borne(s) sonorisée(s), ambiance %s",
            s->cabinet_voices, s->voice_ambience >= 0 ? "en place" : "absente");
}

void room_sound_shutdown(room_sound *s)
{
    if (!s->ready) return;
    ns_audio_stop(s->voice_ambience);
    for (int i = 0; i < NS_MAX_CABINETS; ++i) ns_audio_stop(s->voice_cabinet[i]);
    memset(s, 0, sizeof *s);
}

/* --------------------------------------------------------------------------
 * Par image
 * -------------------------------------------------------------------------- */

static ns_footstep step_under_feet(const ns_scene *scene, const room_camera *cam)
{
    if (!scene->material_footstep) return NS_STEP_MOQUETTE;
    if (cam->ground_material >= scene->material_count) return NS_STEP_MOQUETTE;
    const ns_footstep k = scene->material_footstep[cam->ground_material];
    /* Un matériau sans classe déclarée n'est pas une erreur : on marche dessus
     * comme sur de la moquette, et la salle de 2020 n'en déclare aucune. */
    return (k == NS_STEP_NONE) ? NS_STEP_MOQUETTE : k;
}

void room_sound_update(room_sound *s, const ns_scene *scene, const room_camera *cam, float dt)
{
    if (!s->ready || !ns_audio_ready()) return;

    /* L'auditeur suit l'œil. `cam->position` EST l'œil depuis A6. */
    const ns_camera view = room_camera_resolve(cam, 1.0f);
    ns_audio_set_listener(view.position, view.forward, ns_v3_make(0.0f, 1.0f, 0.0f));

    /* --- les pas ------------------------------------------------------- */
    const room_view_bob bob = room_camera_bob(cam, 1.0f);
    if (cam->mode == ROOM_CAM_PLAYER && cam->grounded && bob.amount > 0.12f) {
        if (bob.distance - s->last_step_distance >= RS_STEP_METRES) {
            s->last_step_distance = bob.distance;
            s->left_foot = !s->left_foot;

            const ns_footstep k = step_under_feet(scene, cam);
            const step_voice *v = &g_step_voice[k];

            ns_rng rng;
            ns_rng_seed(&rng, (uint64_t)(bob.distance * 1000.0f), s->rng++);

            /* Le pied gauche et le pied droit ne sonnent pas pareil : deux corps
             * différents, deux chaussures différentes. Un décalage constant de
             * timbre suffit à ce que l'oreille entende une marche plutôt qu'une
             * répétition. */
            const float foot = s->left_foot ? 0.97f : 1.03f;
            const float gain  = rand_range(&rng, v->gain_min, v->gain_max)
                              * ns_clampf(bob.amount, 0.3f, 1.0f);
            const float pitch = rand_range(&rng, v->pitch_min, v->pitch_max) * foot;

            /* Le pas vient des PIEDS, pas des yeux : à 1,70 m au-dessus, il
             * sonnerait comme si l'on marchait sur les mains. */
            ns_v3 feet = view.position;
            feet.y -= cam->eye_height;
            ns_audio_play_3d(s->clip_walk, NS_BUS_SFX, feet, gain, pitch, 0.6f, 8.0f);
        }
    } else if (bob.amount <= 0.12f) {
        /* À l'arrêt, on réarme : repartir ne doit pas attendre un demi-pas. */
        s->last_step_distance = bob.distance;
    }

    /* --- occlusion, une source par image -------------------------------- */
    /*
     * `ns_bvh_occlusion_factor` lance trois rayons. Dix-neuf sources par image
     * en feraient cinquante-sept, pour une grandeur qui ne change qu'à la vitesse
     * où l'on marche. Un tourniquet suffit : chaque borne est réévaluée cinq fois
     * par seconde à 60 images, et l'amortissement du mixeur lisse le reste.
     */
    if (scene->bvh.loaded && s->cabinet_voices > 0) {
        const uint32_t n = (scene->cabinet_count < NS_MAX_CABINETS)
                         ? scene->cabinet_count : NS_MAX_CABINETS;
        if (n) {
            const uint32_t i = s->occlusion_cursor % n;
            s->occlusion_cursor++;
            if (s->voice_cabinet[i] >= 0) {
                const float f = ns_bvh_occlusion_factor(&scene->bvh, view.position,
                                                        scene->cabinets[i].screen_center);
                ns_audio_voice_occlusion(s->voice_cabinet[i], f);
            }
        }
    }

    ns_audio_update(dt);
}

void room_sound_coin(room_sound *s, ns_v3 position)
{
    if (!s->ready) return;
    /* Le jeton emprunte le son de porte, aigu et sec : c'est le seul « clac »
     * métallique de la banque de 2020. Un vrai son de jeton viendra avec les
     * enregistrements ; en attendant, un geste muet serait pire. */
    if (s->clip_door_close >= 0) {
        ns_audio_play_3d(s->clip_door_close, NS_BUS_SFX, position, 0.5f, 1.6f, 0.5f, 6.0f);
    }
}
