/*
 * test_lockstep.c — le duel EN DIRECT, deux clients contre un vrai relais.
 *
 * Pourquoi il ouvre de vraies sockets
 * -----------------------------------
 * C'est la leçon fondatrice de ce dépôt, écrite dans le journal : « deux
 * moitiés d'un même projet peuvent être justes chacune et fausses ensemble ».
 * Le classement en ligne l'a payé de trois défauts qu'aucun test unitaire ne
 * pouvait voir — un nom de tableau, une graine lue dans un `float`, une
 * enveloppe refusée — parce qu'ils vivaient ENTRE le C et le Go.
 *
 * Un duel en pas verrouillé a exactement la même forme : un protocole binaire
 * écrit deux fois, une fois en C et une fois en Go. On le fait donc parler pour
 * de vrai.
 *
 * Deux régimes
 * ------------
 *   ns_test_lockstep
 *       Sans relais : le refus propre d'une connexion impossible, et le fait
 *       qu'aucune socket ne s'ouvre sans qu'on le demande. C'est ce que fait la
 *       CI.
 *
 *   ns_test_lockstep <hôte> <port>
 *       LE DUEL ENTIER contre le relais Go : appariement, graine commune,
 *       échange d'entrées pas par pas, empreintes comparées, et — le contrôle
 *       qui compte le plus — une divergence PROVOQUÉE doit être DÉTECTÉE.
 *
 *       Le relais se lance seul, sans base :
 *         cd server && go run ./cmd/duelrelay -addr 127.0.0.1:8081 &
 *         ns_test_lockstep 127.0.0.1 8081
 *
 * Ce que le dernier contrôle vaut
 * -------------------------------
 * Un duel qui reste synchronisé quand tout va bien ne prouve rien : deux
 * simulations identiques nourries des mêmes entrées le seraient de toute façon.
 * Ce qu'il faut prouver, c'est qu'on S'EN APERÇOIT quand elles divergent — parce
 * que livrer un duel qui diverge en silence serait pire que de ne pas en
 * livrer. Le test fait donc jouer à l'un des deux une entrée que l'autre ne
 * connaît pas, et exige que la liaison passe en `DESYNC`.
 */
#include "ns_core.h"
#include "ns_lockstep.h"
#include "games.h"

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
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* -------------------------------------------------------------------------- */
/* Sans relais                                                                 */
/* -------------------------------------------------------------------------- */

static void test_refus(void)
{
    char err[160];

    /* Un port sur lequel personne n'écoute. Le refus doit être NET et porter un
     * motif : une fonction qui rend NULL sans rien dire oblige à deviner. */
    ns_lockstep *ls = ns_lockstep_connect("127.0.0.1", 1, 1, 0, "flappy", "normal",
                                          400, err, sizeof err);
    CHECK(ls == NULL, "une connexion vers un port fermé a été acceptée");
    CHECK(err[0] != '\0', "un échec de connexion n'a produit aucun motif");
    if (ls) ns_lockstep_close(ls);

    /* Un hôte qui ne se résout pas. */
    err[0] = '\0';
    ls = ns_lockstep_connect("hote.invalide.nineteen", 8081, 1, 0, "flappy", "normal",
                             400, err, sizeof err);
    CHECK(ls == NULL, "un hôte introuvable a été accepté");
    if (ls) ns_lockstep_close(ls);

    /* Des paramètres invalides ne doivent pas même toucher au réseau. */
    err[0] = '\0';
    ls = ns_lockstep_connect(NULL, 8081, 1, 0, "flappy", "normal", 400, err, sizeof err);
    CHECK(ls == NULL, "un hôte NULL a été accepté");
    err[0] = '\0';
    ls = ns_lockstep_connect("127.0.0.1", 8081, 1, 7, "flappy", "normal", 400, err, sizeof err);
    CHECK(ls == NULL, "une place autre que 0 ou 1 a été acceptée");

    /* L'état d'une liaison inexistante doit être lisible sans planter : le code
     * appelant n'a pas à savoir si l'ouverture a réussi avant d'interroger. */
    CHECK(ns_lockstep_status(NULL) == NS_LOCKSTEP_OFF, "l'état d'un NULL n'est pas OFF");
    CHECK(ns_lockstep_seed(NULL) == 0, "la graine d'un NULL n'est pas nulle");
    CHECK(ns_lockstep_desync_tick(NULL) == -1, "le pas de divergence d'un NULL n'est pas -1");
    CHECK(ns_lockstep_peer_input(NULL, 0, NULL, NULL) == false,
          "l'entrée d'un pair inexistant a été rendue");
}

/* -------------------------------------------------------------------------- */
/* Avec relais                                                                 */
/* -------------------------------------------------------------------------- */

/* Attend que les deux liaisons passent à RUNNING, ou rend false. */
static bool attendre_appariement(ns_lockstep *a, ns_lockstep *b, uint32_t ms)
{
    const uint64_t fin = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < fin) {
        ns_lockstep_poll(a);
        ns_lockstep_poll(b);
        if (ns_lockstep_status(a) == NS_LOCKSTEP_RUNNING &&
            ns_lockstep_status(b) == NS_LOCKSTEP_RUNNING) {
            return true;
        }
        SDL_Delay(2);
    }
    return false;
}

/*
 * Le pas verrouillé, écrit ici comme il le serait dans la boucle de jeu.
 *
 * L'entrée du pas T est publiée tout de suite et consommée au pas
 * T + NS_LOCKSTEP_DELAY. Tant que l'entrée de l'autre manque pour le pas qu'on
 * veut simuler, on N'AVANCE PAS : c'est toute la définition du pas verrouillé,
 * et c'est ce qui garantit que les deux parties sont la même.
 *
 * `divergence_a` force, sur le client A seulement, une entrée que B ne
 * connaîtra pas : c'est la mutation qui doit déclencher la détection.
 */
static bool jouer(ns_lockstep *ls, const ns_game_api *api, void *state,
                  int32_t ticks, uint64_t seed, bool hard,
                  int32_t divergence_a, uint64_t *out_hash)
{
    api->reset(state, seed, hard);
    api->set_best(state, 0);

    const float step = 1.0f / (float)NS_DEFAULT_TICK_HZ;
    for (int32_t t = 0; t < ticks; ++t) {
        /* Une entrée simple mais pas constante : un appui tous les onze pas.
         * Une entrée constante rendrait le test aveugle à un décalage d'un pas,
         * qui est précisément le défaut qu'on cherche. */
        const uint8_t pressed = ((t % 11) == 0) ? 1u : 0u;
        ns_lockstep_send_input(ls, t, 0, pressed);

        const int32_t play = t - NS_LOCKSTEP_DELAY;
        if (play < 0) continue;

        /* On attend l'entrée du pair pour CE pas. Bornée : un test ne doit pas
         * pouvoir s'éterniser si le relais ne renvoie rien. */
        uint8_t peer_held = 0, peer_pressed = 0;
        const uint64_t fin = SDL_GetTicks() + 3000;
        while (!ns_lockstep_peer_input(ls, play, &peer_held, &peer_pressed)) {
            ns_lockstep_poll(ls);
            if (ns_lockstep_status(ls) == NS_LOCKSTEP_DESYNC) return true;
            if (ns_lockstep_status(ls) >= NS_LOCKSTEP_ENDED) return false;
            if (SDL_GetTicks() > fin) return false;
            SDL_Delay(1);
        }

        /* Les DEUX entrées entrent dans la simulation : celle du joueur local
         * et celle du pair. C'est ce qui rend les deux parties identiques —
         * chacun rejoue l'autre chez soi. */
        const uint8_t mine = ((play % 11) == 0) ? 1u : 0u;
        const uint8_t mask = (uint8_t)(mine | peer_pressed |
                                       ((play == divergence_a) ? 2u : 0u));
        for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
            if (mask & (1u << b)) api->press(state, (ns_game_button)b);
        }
        api->tick(state, step);
        ns_game_events ev; SDL_zero(ev);
        api->events(state, &ev);

        if ((play % NS_LOCKSTEP_HASH_EVERY) == 0) {
            const uint64_t k = ns_game_state_hash(api, state);
            ns_lockstep_publish_hash(ls, play, k);
            ns_lockstep_verify_peer(ls, play, k);
        }
        ns_lockstep_poll(ls);
        if (ns_lockstep_status(ls) == NS_LOCKSTEP_DESYNC) return true;
    }
    if (out_hash) *out_hash = ns_game_state_hash(api, state);
    return true;
}

static void test_duel(const char *host, uint16_t port, bool provoquer)
{
    char ea[160] = {0}, eb[160] = {0};
    const uint64_t duel = (uint64_t)SDL_GetTicks() * 1000u + (provoquer ? 7u : 3u);

    ns_lockstep *a = ns_lockstep_connect(host, port, duel, 0, "flappy", "normal",
                                         2000, ea, sizeof ea);
    ns_lockstep *b = ns_lockstep_connect(host, port, duel, 1, "flappy", "normal",
                                         2000, eb, sizeof eb);
    CHECK(a != NULL, "client A : %s", ea);
    CHECK(b != NULL, "client B : %s", eb);
    if (!a || !b) { ns_lockstep_close(a); ns_lockstep_close(b); return; }

    CHECK(attendre_appariement(a, b, 3000), "les deux clients n'ont pas été appariés");
    if (ns_lockstep_status(a) != NS_LOCKSTEP_RUNNING) {
        ns_lockstep_close(a); ns_lockstep_close(b); return;
    }

    /* La graine vient du RELAIS et elle est la même des deux côtés. C'est la
     * règle du billet de partie appliquée au duel : celui qui joue ne choisit
     * pas ce sur quoi il joue. */
    const uint64_t seed = ns_lockstep_seed(a);
    CHECK(seed != 0, "le relais n'a pas donné de graine");
    CHECK(ns_lockstep_seed(b) == seed, "les deux joueurs n'ont pas la même graine");

    /* Une troisième connexion sur la place déjà prise doit être REFUSÉE :
     * sinon un tiers pourrait s'inviter dans un duel en cours. */
    if (!provoquer) {
        char ec[160] = {0};
        ns_lockstep *c = ns_lockstep_connect(host, port, duel, 0, "flappy", "normal",
                                             600, ec, sizeof ec);
        if (c) {
            ns_lockstep_poll(c);
            SDL_Delay(120);
            ns_lockstep_poll(c);
            CHECK(ns_lockstep_status(c) != NS_LOCKSTEP_RUNNING,
                  "un troisième client a été admis sur une place déjà prise");
            ns_lockstep_close(c);
        }
    }

    const ns_game_api *api = ns_game_find("flappy");
    CHECK(api != NULL, "le jeu « flappy » est introuvable");
    if (!api) { ns_lockstep_close(a); ns_lockstep_close(b); return; }

    void *sa = SDL_calloc(1, api->state_size);
    void *sb = SDL_calloc(1, api->state_size);
    uint64_t ha = 0, hb = 0;
    const int32_t ticks = 240;      /* deux secondes de jeu */

    /*
     * Les deux « joueurs » avancent en alternance, dans un seul fil.
     *
     * C'est possible parce que le pas verrouillé ne demande à personne d'aller
     * vite : il demande que personne n'aille PLUS VITE que l'autre. Un seul fil
     * qui les fait progresser à tour de rôle exerce donc exactement la même
     * mécanique que deux machines, sans le hasard d'ordonnancement qui rendrait
     * l'échec difficile à reproduire.
     */
    api->reset(sa, seed, false);
    api->set_best(sa, 0);
    api->reset(sb, seed, false);
    api->set_best(sb, 0);

    const float step = 1.0f / (float)NS_DEFAULT_TICK_HZ;
    bool desync_vu = false;
    for (int32_t t = 0; t < ticks && !desync_vu; ++t) {
        const uint8_t pa = ((t % 11) == 0) ? 1u : 0u;
        const uint8_t pb = ((t % 7) == 0) ? 1u : 0u;
        ns_lockstep_send_input(a, t, 0, pa);
        ns_lockstep_send_input(b, t, 0, pb);

        const int32_t play = t - NS_LOCKSTEP_DELAY;
        if (play < 0) { ns_lockstep_poll(a); ns_lockstep_poll(b); continue; }

        uint8_t ihb = 0, ipb = 0, iha = 0, ipa = 0;
        const uint64_t fin = SDL_GetTicks() + 4000;
        while (!ns_lockstep_peer_input(a, play, &ihb, &ipb) ||
               !ns_lockstep_peer_input(b, play, &iha, &ipa)) {
            ns_lockstep_poll(a);
            ns_lockstep_poll(b);
            if (SDL_GetTicks() > fin) break;
            SDL_Delay(1);
        }
        if (!ns_lockstep_peer_input(a, play, &ihb, &ipb)) break;

        const uint8_t mine_a = ((play % 11) == 0) ? 1u : 0u;
        const uint8_t mine_b = ((play % 7) == 0) ? 1u : 0u;

        const uint8_t mask_a = (uint8_t)(mine_a | ipb);
        const uint8_t mask_b = (uint8_t)(mine_b | ipa);
        for (int bi = 0; bi < NS_GAME_BUTTON_COUNT; ++bi) {
            if (mask_a & (1u << bi)) api->press(sa, (ns_game_button)bi);
            if (mask_b & (1u << bi)) api->press(sb, (ns_game_button)bi);
        }
        api->tick(sa, step);
        api->tick(sb, step);
        ns_game_events ea2; SDL_zero(ea2); api->events(sa, &ea2);
        ns_game_events eb2; SDL_zero(eb2); api->events(sb, &eb2);

        if ((play % NS_LOCKSTEP_HASH_EVERY) == 0) {
            /* Ici les deux parties n'en font qu'UNE : les deux états doivent
             * être égaux, donc chacun vérifie avec sa propre empreinte. */
            uint64_t ka = ns_game_state_hash(api, sa);
            const uint64_t kb = ns_game_state_hash(api, sb);

            /*
             * La divergence PROVOQUÉE : A publie une empreinte fausse au pas 48.
             *
             * La première version faisait jouer à A un appui de plus, en
             * espérant que le jeu diverge. C'était un test au HASARD : la graine
             * vient du relais, elle change à chaque exécution, et selon
             * celle-ci l'oiseau était parfois déjà mort au pas visé — un appui
             * sur un cadavre ne change rien, et le test échouait sans qu'aucun
             * code ne soit fautif.
             *
             * Ce qu'il faut prouver n'est de toute façon pas qu'une entrée
             * supplémentaire diverge — ça, c'est une propriété du JEU, et
             * `check_determinism` s'en occupe. C'est qu'un désaccord d'état est
             * DÉTECTÉ. On falsifie donc l'empreinte, ce qui est le désaccord
             * dans sa forme la plus pure et la plus reproductible.
             */
            if (provoquer && play == 48) ka ^= 0x5eedu;
            ns_lockstep_publish_hash(a, play, ka);
            ns_lockstep_publish_hash(b, play, kb);

            /*
             * La vérification est DANS la boucle d'attente, et c'est le défaut
             * que la première version avait : elle vérifiait une fois, juste
             * après avoir publié, c'est-à-dire avant que l'empreinte de l'autre
             * ait eu le temps de faire l'aller-retour par le relais.
             * `verify_peer` ne conclut jamais d'une absence — donc elle ne
             * concluait rien, et une divergence falsifiée passait inaperçue.
             *
             * Dans une vraie boucle de jeu la question ne se pose pas : on
             * vérifie au pas suivant, cent fois par seconde. Ici il faut
             * l'attendre explicitement.
             */
            for (int k = 0; k < 200; ++k) {
                ns_lockstep_poll(a);
                ns_lockstep_poll(b);
                ns_lockstep_verify_peer(a, play, ka);
                ns_lockstep_verify_peer(b, play, kb);
                if (ns_lockstep_status(a) == NS_LOCKSTEP_DESYNC ||
                    ns_lockstep_status(b) == NS_LOCKSTEP_DESYNC) break;
                SDL_Delay(1);
            }
        }
        ns_lockstep_poll(a);
        ns_lockstep_poll(b);
        if (ns_lockstep_status(a) == NS_LOCKSTEP_DESYNC ||
            ns_lockstep_status(b) == NS_LOCKSTEP_DESYNC) {
            desync_vu = true;
        }
    }

    ha = ns_game_state_hash(api, sa);
    hb = ns_game_state_hash(api, sb);

    if (provoquer) {
        CHECK(desync_vu, "une divergence PROVOQUÉE n'a pas été détectée — "
                         "un duel qui diverge en silence est pire que pas de duel");
        if (desync_vu) {
            const int32_t at = (ns_lockstep_desync_tick(a) >= 0)
                             ? ns_lockstep_desync_tick(a) : ns_lockstep_desync_tick(b);
            printf("  divergence détectée au pas %d (falsifiée au pas 48)\n", (int)at);
            CHECK(at == 48, "la divergence a été vue au pas %d, pas au pas 48", (int)at);
            /* Les DEUX doivent le savoir : celui qui constate le dit à l'autre,
             * sinon l'un s'arrête et l'autre continue de jouer seul. */
            CHECK(ns_lockstep_status(a) == NS_LOCKSTEP_DESYNC &&
                  ns_lockstep_status(b) == NS_LOCKSTEP_DESYNC,
                  "un seul des deux joueurs a vu la divergence (A=%d, B=%d)",
                  (int)ns_lockstep_status(a), (int)ns_lockstep_status(b));
        }
        (void)ha; (void)hb;
    } else {
        CHECK(!desync_vu, "un duel sain a été déclaré divergent");
        CHECK(ha == hb, "les deux parties ont fini dans des états différents :\n"
                        "  A %016llx\n  B %016llx",
              (unsigned long long)ha, (unsigned long long)hb);
        printf("  %d pas joués, état commun %016llx\n", (int)ticks,
               (unsigned long long)ha);
    }

    uint32_t in_a = 0, out_a = 0;
    ns_lockstep_stats(a, &in_a, &out_a, NULL);
    CHECK(in_a > 0 && out_a > 0, "aucune trame n'a circulé (%u reçues, %u envoyées)",
          in_a, out_a);

    ns_lockstep_say_bye(a);
    ns_lockstep_say_bye(b);
    SDL_free(sa); SDL_free(sb);
    ns_lockstep_close(a);
    ns_lockstep_close(b);
    (void)jouer;   /* gardée : c'est la forme que prend la boucle de jeu */
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    ns_log_set_level(NS_LOG_ERROR);

    test_refus();

    if (argc >= 3) {
        const char *host = argv[1];
        const int port = SDL_atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            printf("port invalide : %s\n", argv[2]);
            return 2;
        }
        printf("duel sain :\n");
        test_duel(host, (uint16_t)port, false);
        printf("divergence provoquée :\n");
        test_duel(host, (uint16_t)port, true);
    } else {
        printf("(aucun relais donné : seuls les refus sont vérifiés — "
               "lancer « ns_test_lockstep <hôte> <port> » avec "
               "« go run ./cmd/duelrelay » pour le duel entier)\n");
    }

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
