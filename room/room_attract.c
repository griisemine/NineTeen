/* Voir room_attract.h pour ce que fait ce module et ce qu'il coûte. */
#include "room_attract.h"

#include "games.h"
#include "ns_core.h"

#include <SDL3/SDL.h>

/* Le temps qu'une partie morte reste à l'écran avant d'être relancée. Assez
 * pour qu'on voie ce qui s'est passé en passant, pas assez pour qu'une borne
 * reste sur une image fixe — ce qui serait revenu au défaut de départ. */
#define ATTRACT_RESPAWN_S 2.2f

/* Les planches d'un jeu, chargées UNE fois et partagées par toutes les bornes
 * qui y jouent. Trois bornes portent Flappy : trois copies des mêmes textures
 * seraient trois fois la mémoire pour exactement la même image. */
typedef struct attract_art {
    const ns_game_api *api;
    void              *art;
} attract_art;

typedef struct attract_demo {
    const ns_game_api *api;
    void              *state;
    ns_texture         target;
    int32_t            material;
    uint64_t           seed;
    float              dead_for;
    bool               hard;
    /* La partie de quelqu'un d'autre, posée pour la durée d'une image. Voir
     * `room_attract_substituer` dans l'en-tête. */
    const ns_game_api *sub_api;
    const void        *sub_state;
} attract_demo;

struct room_attract {
    attract_demo demo[NS_MAX_CABINETS];
    int          count;
    /* Un emplacement par jeu porté. Borné par NS_MAX_CABINETS, qui majore
     * forcément le nombre de jeux DISTINCTS que dix-neuf bornes peuvent
     * déclarer : une table plus fine devrait suivre ns_game_count() à la
     * compilation, ce que le C ne permet pas pour un tableau membre. */
    attract_art  art[NS_MAX_CABINETS];
    int          art_count;
    int          cursor;        /* le tour de rôle */
};

/*
 * Une graine par borne, tirée du nom plutôt que d'une horloge.
 *
 * Deux bornes du même jeu doivent montrer des parties DIFFÉRENTES — sinon
 * l'allée présente deux fois la même image, ce qui se remarque immédiatement.
 * Mais une capture doit rester reproductible : c'est une règle de ce dépôt, et
 * `--screenshot` en dépend. Le nom de la borne donne les deux à la fois.
 */
static uint64_t attract_seed(const char *name, uint32_t salt)
{
    uint64_t h = 1469598103934665603ull;
    for (const char *p = name; p && *p; ++p) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    h ^= (uint64_t)salt * 2654435761ull;
    h *= 1099511628211ull;
    return h & 0x7fffffffffffffffull;   /* jamais le bit de signe : voir le relais */
}

static void *art_for(ns_rhi *rhi, room_attract *a, const ns_game_api *api)
{
    for (int i = 0; i < a->art_count; ++i) {
        if (a->art[i].api == api) return a->art[i].art;
    }
    if (a->art_count >= (int)(sizeof a->art / sizeof a->art[0])) return NULL;

    void *art = SDL_calloc(1, api->art_size ? api->art_size : 1);
    if (!art) return NULL;
    if (!api->art_load(rhi, art)) {
        /* Pas fatal : le jeu tournera sans image, comme dans `load_game`. Une
         * démo sans planche vaut mieux qu'une borne qui redevient une affiche. */
        NS_WARN("attract « %s » : planches indisponibles", api->id);
    }
    a->art[a->art_count].api = api;
    a->art[a->art_count].art = art;
    a->art_count++;
    return art;
}

room_attract *room_attract_create(ns_rhi *rhi, const ns_scene *scene)
{
    if (!rhi || !scene || scene->cabinet_count == 0) return NULL;

    room_attract *a = SDL_calloc(1, sizeof *a);
    if (!a) return NULL;

    for (uint32_t i = 0; i < scene->cabinet_count; ++i) {
        const ns_cabinet *c = &scene->cabinets[i];
        if (!c->attract || c->screen_material < 0) continue;

        const ns_game_api *api = ns_game_find(c->game);
        if (!api || !api->autopilot) continue;

        if (a->count >= (int)(sizeof a->demo / sizeof a->demo[0])) break;
        attract_demo *d = &a->demo[a->count];

        d->state = SDL_calloc(1, api->state_size);
        if (!d->state) continue;

        ns_texture_desc sd;
        SDL_zero(sd);
        sd.width = 512; sd.height = 288;
        sd.format = ns_rhi_swapchain_format(rhi);
        sd.render_target = true;
        sd.sampled = true;
        sd.name = "attract";
        if (!ns_texture_create(rhi, &d->target, &sd)) {
            SDL_free(d->state);
            SDL_zero(*d);
            continue;
        }

        d->api      = api;
        d->material = c->screen_material;
        d->seed     = attract_seed(c->name, 0);
        d->hard     = (SDL_strcmp(c->difficulty, "hard") == 0);
        api->reset(d->state, d->seed, d->hard);
        /*
         * Le RECORD est mis à zéro, et ce n'est pas cosmétique : `set_best`
         * écrit dans l'état du jeu, donc un record hérité rendrait deux bornes
         * du même jeu divergentes pour une raison invisible. Une démo n'a pas
         * de record — personne ne la joue.
         */
        if (api->set_best) api->set_best(d->state, 0);

        a->count++;
    }

    if (a->count == 0) { SDL_free(a); return NULL; }

    /* Les planches, après coup : inutile d'en charger une si aucune borne du
     * jeu n'a pu obtenir sa cible. */
    for (int i = 0; i < a->count; ++i) (void)art_for(rhi, a, a->demo[i].api);

    NS_INFO("attract : %d bornes jouent toutes seules (%d jeux, %d dalles par image)",
            a->count, a->art_count, ROOM_ATTRACT_PER_FRAME);
    return a;
}

void room_attract_destroy(ns_rhi *rhi, room_attract *a)
{
    if (!a) return;
    for (int i = 0; i < a->count; ++i) {
        ns_texture_destroy(rhi, &a->demo[i].target);
        SDL_free(a->demo[i].state);
    }
    for (int i = 0; i < a->art_count; ++i) {
        if (a->art[i].art) a->art[i].api->art_free(rhi, a->art[i].art);
        SDL_free(a->art[i].art);
    }
    SDL_free(a);
}

int room_attract_count(const room_attract *a) { return a ? a->count : 0; }

static void *art_of(const room_attract *a, const ns_game_api *api)
{
    for (int i = 0; i < a->art_count; ++i) {
        if (a->art[i].api == api) return a->art[i].art;
    }
    return NULL;
}

void room_attract_tick(room_attract *a, float dt, int32_t skip_material)
{
    if (!a) return;

    /* La moitié bon marché : un état de jeu est une valeur pure, un pas est une
     * poignée d'opérations. Dix-neuf à 120 Hz ne se mesurent pas. */
    for (int i = 0; i < a->count; ++i) {
        attract_demo *d = &a->demo[i];
        if (d->material == skip_material) continue;

        float dead_time = 0.0f;
        if (d->api->dead && d->api->dead(d->state, &dead_time)) {
            d->dead_for += dt;
            if (d->dead_for >= ATTRACT_RESPAWN_S) {
                d->seed = attract_seed(d->api->id, (uint32_t)(d->seed & 0xffffffffu));
                d->api->reset(d->state, d->seed, d->hard);
                if (d->api->set_best) d->api->set_best(d->state, 0);
                d->dead_for = 0.0f;
            }
            continue;   /* une partie morte ne se joue plus */
        }
        d->dead_for = 0.0f;
        d->api->autopilot(d->state);
        d->api->tick(d->state, dt);
    }
}

void room_attract_substituer(room_attract *a, int32_t material,
                             const ns_game_api *api, const void *state)
{
    if (!a || material < 0) return;
    for (int i = 0; i < a->count; ++i) {
        if (a->demo[i].material != material) continue;
        a->demo[i].sub_api   = api;
        a->demo[i].sub_state = api ? state : NULL;
        return;
    }
}

void room_attract_liberer(room_attract *a)
{
    if (!a) return;
    for (int i = 0; i < a->count; ++i) {
        a->demo[i].sub_api = NULL;
        a->demo[i].sub_state = NULL;
    }
}

void room_attract_draw(ns_rhi *rhi, ns_sprite *sprites, room_attract *a,
                       ns_renderer *rd, int32_t skip_material)
{
    if (!a || !rhi || !sprites || !rd) return;

    /* La moitié qui coûte : chaque dalle est une passe de rendu. On en fait son
     * tour, et le tour de rôle ne se voit pas — voir l'en-tête. */
    for (int n = 0; n < ROOM_ATTRACT_PER_FRAME && n < a->count; ++n) {
        attract_demo *d = &a->demo[a->cursor];
        a->cursor = (a->cursor + 1) % a->count;
        if (d->material == skip_material) continue;

        /* La partie du rival plutôt que la démo, si quelqu'un tient cette
         * borne. Les planches passent par `art_for` et non par `art_of` : un
         * rival peut jouer un jeu dont aucune démo n'existe, et il faut alors
         * les charger — c'est la seule allocation que cette passe puisse
         * déclencher, et elle n'arrive qu'une fois par jeu. */
        const ns_game_api *api = d->sub_api ? d->sub_api : d->api;
        const void *etat = d->sub_api ? d->sub_state : d->state;
        void *planches = d->sub_api ? art_for(rhi, a, api) : art_of(a, api);
        if (!etat) { api = d->api; etat = d->state; planches = art_of(a, api); }

        ns_sprite_begin(sprites, 512.0f, 288.0f);
        api->draw(sprites, etat, planches, 512.0f, 288.0f);
        static const float off[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        ns_sprite_end(rhi, sprites, d->target.handle, 512, 288, off);
    }

    /* Déclarer les textures. TOUTES, à chaque image — y compris celles qu'on
     * n'a pas redessinées : ce qui compte est que le matériau pointe sur SA
     * cible, pas qu'elle vienne d'être remplie. */
    for (int i = 0; i < a->count; ++i) {
        const attract_demo *d = &a->demo[i];
        if (d->material == skip_material) continue;
        ns_renderer_set_screen(rd, d->material, d->target.handle);
    }
}
