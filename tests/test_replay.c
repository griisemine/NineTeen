/*
 * test_replay.c — un journal d'ENTRÉES rejoue-t-il vraiment une partie ?
 *
 * Pourquoi ce test existe
 * -----------------------
 * J'ai affirmé pendant tout le projet que « le journal de partie est déjà une
 * suite d'entrées horodatées au pas fixe, donc exactement ce qu'il faut pour
 * rejouer une partie chez quelqu'un d'autre, donc pour un duel ». C'était faux :
 * `ns_runlog` enregistre des CONSÉQUENCES (`pipe`, `score`, `death`), pas des
 * appuis sur des boutons. Il authentifie un score ; il ne rejoue pas une partie.
 *
 * La question restait donc entière, et c'est celle qui décide si un duel est
 * seulement possible : **une suite de masques de boutons, un par pas fixe,
 * suffit-elle à reproduire une partie au score près ?** Ce test y répond, jeu
 * par jeu, pour les huit.
 *
 * Ce qu'il ne fait pas
 * --------------------
 * Il n'implémente aucun réseau, aucun protocole, aucun duel. C'est une décision
 * prise avec l'auteur du projet : le temps réel se conçoit avant de s'écrire. Ce
 * test mesure la PROPRIÉTÉ dont dépendrait n'importe laquelle des formes
 * envisagées (`docs/RESEAU-TEMPS-REEL.md`), pour que la conception se fasse sur
 * un fait plutôt que sur une supposition — la mienne, qui était fausse.
 *
 * Ce qu'il attrape, dès aujourd'hui et sans duel
 * ---------------------------------------------
 * Un jeu qui lirait une horloge, un `rand()` non semé, ou un état résiduel entre
 * deux parties. Trois défauts qui ne se voient pas en jouant et qui rendent tout
 * rapport de bug irreproductible.
 *
 * La différence avec les `test_determinisme` de chaque jeu : ceux-là rejouent le
 * PILOTE AUTOMATIQUE deux fois, et le pilote appelle les fonctions internes du
 * jeu directement (`envol_flap`, par exemple), sans passer par `press`/`hold`.
 * Ils prouvent que le jeu est déterministe ; ils ne prouvent pas que le CHEMIN
 * DES BOUTONS suffit à le piloter. C'est pourtant le seul chemin dont un duel
 * disposerait.
 */
#include "games.h"
#include "ns_core.h"
#include "ns_math.h"

#include <SDL3/SDL.h>

#include <stdio.h>
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

/* Le pas fixe du moteur, celui auquel les jeux avancent réellement. */
#define STEP      (1.0f / 120.0f)
#define TICKS     3600            /* trente secondes de jeu */

/*
 * Le journal d'entrées, tel qu'il devrait exister.
 *
 * Un masque de cinq bits par pas fixe. On le garde ici en clair — un octet par
 * tic — parce que ce test mesure une propriété, pas un format : la compression
 * (n'écrire que les CHANGEMENTS) est une question de transport, et la trancher
 * maintenant reviendrait à commencer l'implémentation qu'on a décidé de ne pas
 * commencer.
 */
typedef struct track {
    uint8_t held[TICKS];      /* masque des maintiens */
    uint8_t pressed[TICKS];   /* masque des appuis (front montant) */
} track;

/*
 * Une suite d'entrées PLAUSIBLE et reproductible.
 *
 * Pas un bruit blanc : un joueur tient une direction quelques dixièmes de
 * seconde et tape le bouton d'action par à-coups. Un masque tiré au hasard à
 * chaque tic ne ferait rien bouger dans un jeu où l'on tourne en tenant une
 * touche — Snake, Pac-Man — et le test passerait sur des parties immobiles.
 */
static void make_track(track *t, uint64_t seed)
{
    ns_rng rng;
    ns_rng_seed(&rng, seed, 0x51ED2701u);

    uint8_t cur = 0;
    int remaining = 0;
    for (int i = 0; i < TICKS; ++i) {
        if (remaining <= 0) {
            /* Une direction tenue entre 1/12e et 1/2 seconde. */
            remaining = 10 + (int)(ns_rng_below(&rng, 50u));
            const uint32_t pick = ns_rng_below(&rng, 6u);
            cur = (pick < 4u) ? (uint8_t)(1u << pick) : 0u;
        }
        remaining--;

        uint8_t press = 0;
        /* Le bouton d'action, tapé environ trois fois par seconde. */
        if (ns_rng_below(&rng, 40u) == 0u) press = (uint8_t)(1u << NS_GAME_ACTION);

        t->held[i] = cur;
        t->pressed[i] = press;
    }
}

/*
 * Joue une partie ENTIÈREMENT pilotée par le journal, et rend ce qu'il faut
 * pour comparer : le score, le nombre de pas réellement joués, et la somme des
 * gains vus par `events` — trois grandeurs qui divergent pour des raisons
 * différentes, ce qui rend un écart plus facile à situer.
 */
static void play_track(const ns_game_api *api, void *state, const track *t,
                       uint64_t seed, bool hard,
                       uint32_t *out_score, int *out_ticks, int64_t *out_gain)
{
    api->reset(state, seed, hard);
    api->set_best(state, 0);

    int64_t gain = 0;
    int played = 0;

    for (int i = 0; i < TICKS; ++i) {
        /* Les appuis d'abord : dans le moteur ils arrivent du gestionnaire
         * d'événements, donc AVANT le pas. Inverser l'ordre ici mesurerait un
         * jeu que personne ne joue. */
        for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
            if (t->pressed[i] & (1u << b)) api->press(state, (ns_game_button)b);
        }
        if (api->hold) {
            bool held[NS_GAME_BUTTON_COUNT];
            for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
                held[b] = (t->held[i] & (1u << b)) != 0;
            }
            api->hold(state, held);
        }
        api->tick(state, STEP);
        played++;

        /* `events` CONSOMME : ne pas le vider changerait le comportement du jeu
         * d'une exécution à l'autre selon qu'on l'appelle ou non. Le rejeu doit
         * faire exactement ce que fait la boucle de jeu. */
        ns_game_events ev;
        SDL_zero(ev);
        api->events(state, &ev);
        if (ev.score) gain += ev.score_value;
    }

    *out_score = api->score(state);
    *out_ticks = played;
    *out_gain = gain;
}

int main(void)
{
    if (!SDL_Init(0)) {
        printf("SDL_Init a échoué : %s\n", SDL_GetError());
        return 1;
    }
    ns_log_set_level(NS_LOG_ERROR);

    printf("=== un journal d'entrées rejoue-t-il une partie ? ===\n\n");

    const int count = ns_game_count();
    CHECK(count > 0, "il y a des jeux à vérifier (%d)", count);

    for (int g = 0; g < count; ++g) {
        const ns_game_api *api = ns_game_at(g);
        if (!api) { CHECK(false, "jeu %d introuvable", g); continue; }

        void *a = SDL_calloc(1, api->state_size);
        void *b = SDL_calloc(1, api->state_size);
        if (!a || !b) { CHECK(false, "mémoire pour « %s »", api->id); SDL_free(a); SDL_free(b); continue; }

        track t;
        make_track(&t, 0xA11CEull + (uint64_t)g);

        uint32_t sa = 0, sb = 0;
        int ta = 0, tb = 0;
        int64_t ga = 0, gb = 0;

        play_track(api, a, &t, 20240418ull + (uint64_t)g, false, &sa, &ta, &ga);
        play_track(api, b, &t, 20240418ull + (uint64_t)g, false, &sb, &tb, &gb);

        CHECK(sa == sb, "%s : même journal, même score (%u / %u)", api->id, sa, sb);
        CHECK(ta == tb, "%s : même nombre de pas (%d / %d)", api->id, ta, tb);
        CHECK(ga == gb, "%s : mêmes gains cumulés (%lld / %lld)", api->id,
              (long long)ga, (long long)gb);

        /*
         * L'ÉTAT COMPLET, pas seulement le score.
         *
         * Deux parties peuvent finir sur le même score en ayant divergé — un
         * astéroïde ailleurs, un fantôme sur une autre case. Comparer les octets
         * de la structure attrape ça, et c'est la seule comparaison qui vaille
         * pour un duel, où les deux machines doivent voir LA MÊME partie et pas
         * seulement le même nombre.
         *
         * Les jeux n'ont ni pointeur ni remplissage non initialisé : `reset` est
         * appelée sur une structure mise à zéro par `SDL_calloc`, donc la
         * comparaison est licite. Si un jeu venait à contenir un pointeur, ce
         * test le signalerait immédiatement — et ce serait une bonne nouvelle,
         * parce qu'un pointeur dans un état de jeu rend le rejeu impossible.
         */
        CHECK(memcmp(a, b, api->state_size) == 0,
              "%s : et le même ÉTAT au bit près, pas seulement le même score", api->id);

        /* Une AUTRE suite d'entrées doit donner une autre partie : sans ça, le
         * test ci-dessus passerait sur un jeu qui ignore ses commandes. */
        track other;
        make_track(&other, 0xB0B0ull + (uint64_t)g);
        uint32_t sc = 0; int tc = 0; int64_t gc = 0;
        void *c = SDL_calloc(1, api->state_size);
        if (c) {
            play_track(api, c, &other, 20240418ull + (uint64_t)g, false, &sc, &tc, &gc);
            const bool differs = (memcmp(a, c, api->state_size) != 0);
            CHECK(differs, "%s : une autre suite d'entrées donne une autre partie", api->id);
            SDL_free(c);
        }

        /*
         * LE TRACK A-T-IL FAIT QUELQUE CHOSE ?
         *
         * Quatre jeux finissent la suite d'entrées à zéro point — c'est normal,
         * un joueur qui tape au hasard meurt vite à Envol — mais un score nul
         * peut aussi vouloir dire que le jeu n'a rien reçu du tout. Et une
         * partie qui n'a pas bougé se « rejoue à l'identique » trivialement :
         * le test passerait en ne mesurant rien.
         *
         * On compare donc l'état final à un état FRAÎCHEMENT remis à zéro, avec
         * la même graine. S'ils sont identiques, la suite d'entrées n'a pas
         * touché le jeu, et c'est le test lui-même qui est en défaut.
         */
        void *fresh = SDL_calloc(1, api->state_size);
        if (fresh) {
            api->reset(fresh, 20240418ull + (uint64_t)g, false);
            api->set_best(fresh, 0);
            CHECK(memcmp(a, fresh, api->state_size) != 0,
                  "%s : la suite d'entrées a fait AVANCER la partie", api->id);
            SDL_free(fresh);
        }

        float dead_at = 0.0f;
        const bool died = api->dead(a, &dead_at);
        printf("  %-9s score %6u, gains %6lld — rejoué à l'identique%s\n",
               api->id, sa, (long long)ga,
               died ? " (partie terminée avant la fin du journal)" : "");

        SDL_free(a);
        SDL_free(b);
    }

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures == 0 ? 0 : 1;
}
