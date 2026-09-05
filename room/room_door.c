/* room_door.c — voir room_door.h pour le raisonnement. */
#include "room_door.h"

#include "ns_core.h"
#include "ns_env.h"
#include "ns_rhi.h"

#include <SDL3/SDL.h>

/*
 * Les portes déclarées, en mètres monde.
 *
 * Elles sont ICI et non dans `salle.room.json` pour une raison précise : le JSON
 * décrit ce qu'on CONSTRUIT — des boîtes, des matériaux, des chanfreins — et
 * `roomgen` le lit pour cuire la géométrie. Il ne sait rien d'une machine à
 * états, d'une course ni d'une zone de déclenchement, et lui apprendre tout ça
 * pour trois nombres reviendrait à le faire dépendre du comportement du jeu. Le
 * lien entre les deux est le NOM du nœud, et il suffit.
 *
 * Les cotes ci-dessous sont mesurées sur le glTF produit, pas devinées :
 * `porte_wc` s'y trouve en x [8,814 ; 8,877], y [0 ; 2,050], z [−1,450 ; −0,400].
 * La baie de `cloison_toilettes` va de z = −2,45 à −1,50 ; fermé, le vantail
 * couvre z [−2,50 ; −1,45], soit 5 cm de recouvrement de chaque côté — ce qu'on
 * attend d'un ouvrant, qui ne s'arrête pas au nu du tableau.
 *
 * La seconde porte du plan de 2020 n'est pas encore déclarée dans la salle ; la
 * table est dimensionnée pour l'accueillir sans rien réécrire, et une entrée dont
 * le nœud n'existe pas est ignorée en silence.
 */
static const room_door_desc g_declared[] = {
    {
        .object       = "porte_wc",
        /* Du fermé vers l'ouvert : +z. Le vantail se range le long du refend. */
        .axis         = { 0.0f, 0.0f, 1.0f },
        .travel       = 1.05f,
        .rest_is_open = true,
        /* Un mètre de part et d'autre du refend, et la baie élargie de 35 cm :
         * on ouvre en s'approchant de la porte, pas en longeant la cloison à
         * trois mètres. La forme est un rectangle et non un rayon parce qu'un
         * passage se garde par un rectangle — c'est déjà ce que faisait
         * `detecterOuvertureToilette` en 2020. */
        .trigger_min_x = 7.95f, .trigger_max_x =  9.95f,
        .trigger_min_z = -2.80f, .trigger_max_z = -1.15f,
    },
};

#define ROOM_DECLARED_DOORS ((uint32_t)(sizeof g_declared / sizeof g_declared[0]))

/*
 * Le tampon de recopie, borné et partagé.
 *
 * Une porte est un panneau : quelques centaines de sommets. `porte_wc` en a 192,
 * comptés dans le glTF. Le plafond est fixé à 2 048 — dix fois la marge — et une
 * porte plus lourde que ça est REFUSÉE avec un message plutôt qu'animée à moitié.
 * Un tableau statique parce qu'il n'est touché que depuis la boucle de rendu,
 * qui est mono-fil, et qu'une allocation par image pour 9 Kio serait du bruit.
 */
#define ROOM_DOOR_MAX_VERTICES 2048u
static ns_vertex g_scratch[ROOM_DOOR_MAX_VERTICES];

/* --------------------------------------------------------------------------
 * Mise en place
 * -------------------------------------------------------------------------- */

/*
 * Retrouve la plage de sommets d'un objet en relisant SES indices.
 *
 * Pourquoi il faut la déduire au lieu de la lire : `ns_scene_object` ne porte
 * qu'une tranche de lots, et un lot ne connaît que ses indices. La plage de
 * sommets n'est écrite nulle part — ce qui est normal, rien n'en avait besoin
 * avant. Elle est retrouvée une fois au démarrage, sur 264 indices pour
 * `porte_wc`, et jamais relue ensuite.
 *
 * La CONTIGUÏTÉ est vérifiée et non supposée. Elle tient parce que l'écrivain
 * glTF émet les nœuds dans l'ordre et qu'un prop occupe une plage d'un seul
 * tenant ; si un jour ce n'était plus vrai, une porte animée par sa plage
 * déplacerait les sommets d'un voisin, ce qui se verrait comme un mur qui fond.
 * Mieux vaut refuser de l'animer.
 */
static bool vertex_range_of(const ns_scene *scene, const ns_scene_object *obj,
                            uint32_t *first, uint32_t *count)
{
    if (!scene->cpu_indices) return false;

    uint32_t lo = UINT32_MAX, hi = 0;
    uint32_t seen = 0;
    for (uint32_t b = 0; b < obj->batch_count; ++b) {
        const ns_draw_batch *batch = &scene->batches[obj->first_batch + b];
        for (uint32_t i = 0; i < batch->index_count; ++i) {
            const uint32_t v = scene->cpu_indices[batch->first_index + i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            seen++;
        }
    }
    if (seen == 0 || lo > hi) return false;

    *first = lo;
    *count = hi - lo + 1;
    return true;
}

void room_doors_init(room_doors *d, const ns_scene *scene)
{
    SDL_zerop(d);

    /*
     * Les cinq réglages. Tous dans `nineteen.env`, tous lus ici, aucun sans
     * lecteur — `ns_env_report_unread` le vérifie au démarrage.
     *
     * La vitesse : 2020 déplaçait le vantail de `(25/IPS) * 0,1` unité par
     * image, soit 2,5 unités par seconde à n'importe quelle cadence, soit
     * **1,21 m/s** à 2,06 unités par mètre. La course de 2,8 unités y prenait
     * donc 1,12 s. On garde ce temps de traversée : 1,05 m en 1,12 s font
     * 0,94 m/s, et c'est ce qu'on écrit — la porte de 2020 mettait ce temps-là à
     * s'ouvrir, et c'est cela qu'on reconnaît, pas le nombre d'unités.
     */
    d->speed        = ns_env_float("porte.vitesse", 0.94f);
    d->hold_seconds = ns_env_float("porte.maintien", 3.0f);   /* les 3 000 ms de 2020 */
    d->trigger_pad  = ns_env_float("porte.marge", 0.0f);
    d->collide      = ns_env_bool("porte.collision", true);

    if (d->speed < 0.05f) d->speed = 0.05f;          /* zéro figerait la porte */
    if (d->hold_seconds < 0.0f) d->hold_seconds = 0.0f;

    for (uint32_t k = 0; k < ROOM_DECLARED_DOORS && d->count < ROOM_MAX_DOORS; ++k) {
        room_door *p = &d->door[d->count];
        SDL_zerop(p);
        p->desc  = g_declared[k];
        p->state = ROOM_DOOR_CLOSED;
        p->openness = 0.0f;

        /*
         * La porte EXISTE même sans géométrie liée.
         *
         * Sans scène — c'est le cas de `tests/test_door.c` — ou sans le nœud
         * correspondant, la machine à états tourne quand même : elle s'ouvre,
         * elle se referme, elle produit ses obstacles. Seul le téléversement des
         * sommets est sauté, `bound` restant faux. C'est ce qui permet de la
         * vérifier sans carte graphique, et c'est aussi ce qui fait qu'une salle
         * à qui il manque un vantail ne perd pas sa porte, seulement son image.
         */
        if (!scene) { d->count++; continue; }
        const ns_scene_object *obj = ns_scene_find_object(scene, p->desc.object);
        if (!obj) {
            /* La salle de 2020 n'a pas ce nœud, et c'est le cas NORMAL sur
             * `salle-legacy`. Rien à signaler bruyamment. */
            NS_INFO("porte « %s » absente de la scène : non animée", p->desc.object);
            d->count++;
            continue;
        }

        uint32_t first = 0, vcount = 0;
        if (!vertex_range_of(scene, obj, &first, &vcount)) {
            NS_WARN("porte « %s » : plage de sommets introuvable", p->desc.object);
            d->count++;
            continue;
        }
        if (vcount > ROOM_DOOR_MAX_VERTICES) {
            NS_WARN("porte « %s » : %u sommets, au-delà des %u admis — non animée",
                    p->desc.object, vcount, ROOM_DOOR_MAX_VERTICES);
            d->count++;
            continue;
        }

        p->bound        = true;
        p->first_vertex = first;
        p->vertex_count = vcount;
        p->first_batch  = obj->first_batch;
        p->batch_count  = obj->batch_count;
        p->rest_min     = obj->bounds.min;
        p->rest_max     = obj->bounds.max;
        p->applied      = ns_v3_make(0.0f, 0.0f, 0.0f);

        NS_INFO("porte « %s » : %u sommets [%u..%u], course %.2f m sur %.2f m/s, "
                "maintien %.1f s, repos %s",
                p->desc.object, vcount, first, first + vcount - 1,
                (double)p->desc.travel, (double)d->speed, (double)d->hold_seconds,
                p->desc.rest_is_open ? "ouvert" : "fermé");
        d->count++;
    }
}

/* --------------------------------------------------------------------------
 * La machine à états
 * -------------------------------------------------------------------------- */

static bool in_trigger(const room_doors *d, const room_door *p, ns_v3 player)
{
    const float pad = d->trigger_pad;
    return player.x >= p->desc.trigger_min_x - pad
        && player.x <= p->desc.trigger_max_x + pad
        && player.z >= p->desc.trigger_min_z - pad
        && player.z <= p->desc.trigger_max_z + pad;
}

/* Le décalage à appliquer aux sommets pour une ouverture donnée. Voir
 * `rest_is_open` : la position CUITE n'est pas forcément la position fermée, et
 * c'est ce terme qui absorbe la différence. */
static ns_v3 offset_for(const room_door *p, float openness)
{
    const float base = p->desc.rest_is_open ? (openness - 1.0f) : openness;
    return ns_v3_scale(p->desc.axis, base * p->desc.travel);
}

void room_doors_tick(room_doors *d, ns_v3 player, float dt)
{
    if (dt <= 0.0f) return;

    d->blocker_count = 0;

    for (uint32_t i = 0; i < d->count; ++i) {
        room_door *p = &d->door[i];
        const float travel = (p->desc.travel > 1e-4f) ? p->desc.travel : 1.0f;
        /* Par SECONDE et non par image : c'est ce que 2020 obtenait avec son
         * `variateurFPSanimation`, en compensant à la main un nombre d'images
         * par seconde qu'il fallait deviner. Ici le pas est fixe. */
        const float d_open = (d->speed / travel) * dt;
        const bool inside = in_trigger(d, p, player);

        switch (p->state) {
        case ROOM_DOOR_CLOSED:
            if (inside) {
                p->state = ROOM_DOOR_OPENING;
                p->want_open_sound = true;
            }
            break;

        case ROOM_DOOR_OPENING:
            p->openness += d_open;
            if (p->openness >= 1.0f) {
                p->openness = 1.0f;
                p->state    = ROOM_DOOR_OPEN;
                p->hold     = d->hold_seconds;
            }
            break;

        case ROOM_DOOR_OPEN:
            /*
             * Le compte à rebours ne court QUE si le joueur est parti.
             *
             * 2020 refermait au bout de trois secondes quoi qu'il arrive, et
             * rouvrait ensuite si le joueur était encore devant — ce qui donne
             * une porte qui bat au visage de quelqu'un qui se lave les mains. Le
             * réarmement tant qu'on est dans la zone est le même geste que la
             * ré-ouverture pendant la fermeture, poussé d'un cran plus tôt.
             */
            if (inside) {
                p->hold = d->hold_seconds;
            } else {
                p->hold -= dt;
                if (p->hold <= 0.0f) {
                    p->hold  = 0.0f;
                    p->state = ROOM_DOOR_CLOSING;
                    p->want_close_sound = true;
                }
            }
            break;

        case ROOM_DOOR_CLOSING:
            /* RE-OUVRIR SI PASSAGE DEVANT LA PORTE DURANT LA FERMETURE — le
             * commentaire de 2020, et le comportement avec. On repart de la
             * position courante, pas de zéro. */
            if (inside) {
                p->state = ROOM_DOOR_OPENING;
                p->want_open_sound = true;
            } else {
                p->openness -= d_open;
                if (p->openness <= 0.0f) {
                    p->openness = 0.0f;
                    p->state    = ROOM_DOOR_CLOSED;
                }
            }
            break;
        }

        if (!p->bound) continue;

        /* Le centre VIVANT du vantail, pour que le son suive le panneau et non
         * la baie : une porte qui coulisse emmène son grincement avec elle. */
        const ns_v3 off = offset_for(p, p->openness);
        p->sound_position = ns_v3_add(ns_v3_scale(ns_v3_add(p->rest_min, p->rest_max), 0.5f), off);

        /*
         * L'obstacle, à la position vivante et sans seuil.
         *
         * Pas de « on ne bloque que si la porte est fermée à plus de 80 % » : le
         * vantail est un panneau plein sur toute sa course, et le joueur doit
         * pouvoir être bousculé par une porte qui se ferme. C'est le
         * comportement d'une porte automatique, et c'est ce qui la rend
         * physique plutôt que décorative.
         */
        if (d->collide && d->blocker_count < ROOM_MAX_DOORS) {
            room_blocker *b = &d->blocker[d->blocker_count++];
            b->box.min = ns_v3_add(p->rest_min, off);
            b->box.max = ns_v3_add(p->rest_max, off);
        }
    }
}

room_blockers room_doors_blockers(room_doors *d)
{
    room_blockers b;
    b.items = d->blocker;
    b.count = d->blocker_count;
    return b;
}

bool room_door_take_open_sound(room_door *p)
{
    const bool v = p->want_open_sound;
    p->want_open_sound = false;
    return v;
}

bool room_door_take_close_sound(room_door *p)
{
    const bool v = p->want_close_sound;
    p->want_close_sound = false;
    return v;
}

/* --------------------------------------------------------------------------
 * Le téléversement
 * -------------------------------------------------------------------------- */

void room_doors_upload(room_doors *d, ns_rhi *rhi, ns_scene *scene)
{
    if (!rhi || !scene || !scene->cpu_vertices) return;

    for (uint32_t i = 0; i < d->count; ++i) {
        room_door *p = &d->door[i];
        if (!p->bound) continue;

        const ns_v3 want = offset_for(p, p->openness);
        const ns_v3 diff = ns_v3_sub(want, p->applied);
        /* Un dixième de millimètre : en deçà, le déplacement ne couvre pas un
         * pixel à un mètre et ne vaut pas 9 Kio sur l'anneau de transfert. Une
         * porte immobile — donc la plupart des images — ne coûte donc rien. */
        if (ns_v3_len_sq(diff) < 1e-8f) continue;

        /* On repart TOUJOURS des sommets de repos, jamais de ceux déjà écrits :
         * accumuler des différences en virgule flottante fait dériver un vantail
         * qui a fait mille allers-retours. */
        const ns_vertex *src = &scene->cpu_vertices[p->first_vertex];
        for (uint32_t v = 0; v < p->vertex_count; ++v) {
            g_scratch[v] = src[v];
            g_scratch[v].position[0] += want.x;
            g_scratch[v].position[1] += want.y;
            g_scratch[v].position[2] += want.z;
            /* Normales et tangentes inchangées : une translation ne tourne rien.
             * C'est aussi ce qui rend ce chemin sûr — la seule passe de rendu
             * concernée reçoit exactement les mêmes attributs qu'avant. */
        }

        const uint32_t bytes  = p->vertex_count * (uint32_t)sizeof(ns_vertex);
        const uint32_t offset = p->first_vertex * (uint32_t)sizeof(ns_vertex);
        if (!ns_rhi_stage_buffer(rhi, &scene->vertices, g_scratch, bytes, offset)) {
            /* L'anneau est plein ou la copie refusée : on ne marque PAS le
             * décalage comme appliqué, et l'image suivante réessaiera. Marquer
             * ici laisserait le vantail figé à mi-course pour toujours. */
            continue;
        }

        /*
         * Les boîtes des lots suivent les sommets.
         *
         * `ns_render_frame` élimine par frustum sur `batch->bounds` (ns_render.c,
         * autour de la ligne 1400). Une boîte restée à la position de repos ferait
         * disparaître le vantail dès qu'il sort du tronc de vision de son ancienne
         * position — c'est-à-dire en le regardant de biais, ce qui est exactement
         * la situation où on le regarde.
         */
        for (uint32_t b = 0; b < p->batch_count; ++b) {
            ns_draw_batch *batch = &scene->batches[p->first_batch + b];
            batch->bounds.min = ns_v3_add(ns_v3_sub(batch->bounds.min, p->applied), want);
            batch->bounds.max = ns_v3_add(ns_v3_sub(batch->bounds.max, p->applied), want);
        }
        /* L'objet nommé garde la boîte du vantail vivant : c'est elle que lisent
         * `ns_scene_find_object` et tout ce qui voudra viser la porte. */
        for (uint32_t o = 0; o < scene->object_count; ++o) {
            if (scene->objects[o].first_batch != p->first_batch) continue;
            scene->objects[o].bounds.min =
                ns_v3_add(ns_v3_sub(scene->objects[o].bounds.min, p->applied), want);
            scene->objects[o].bounds.max =
                ns_v3_add(ns_v3_sub(scene->objects[o].bounds.max, p->applied), want);
            break;
        }

        p->applied = want;
    }
}
