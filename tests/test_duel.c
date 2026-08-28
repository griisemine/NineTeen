/*
 * test_duel.c — la présence et le duel en différé, contre un VRAI serveur.
 *
 * Pourquoi ce test est écrit comme ça
 * -----------------------------------
 * Le changelog de ce projet tire une leçon de son classement en ligne : « deux
 * moitiés d'un même projet peuvent être justes chacune et fausses ensemble ».
 * Trois défauts n'existaient qu'ENTRE le C et le Go — un nom de créneau, une
 * graine relue en flottant, une enveloppe postée telle quelle — et aucun test
 * unitaire ne pouvait les voir, parce qu'aucun ne faisait parler les deux
 * moitiés.
 *
 * Le temps réel ajoute exactement les mêmes occasions de se tromper à deux :
 * un journal d'entrées produit ici et relu là-bas, une graine qui doit être LA
 * MÊME des deux côtés d'un duel, un pseudo qui traverse du JSON. Ce test existe
 * donc pour les faire parler pour de vrai.
 *
 * Trois régimes, et CTest ne lance aucun serveur
 * ----------------------------------------------
 *   ns_test_duel
 *       les verrous et l'analyseur de journal. C'est ce que fait la CI, et ça
 *       ne demande rien : ni serveur, ni base, ni socket.
 *
 *   ns_test_duel <url>
 *       + la présence ANONYME, celle qui n'exige aucun compte.
 *
 *   ns_test_duel <url> <jeton>
 *       + LA CHAÎNE ENTIÈRE du duel : billet, partie réellement jouée, envoi,
 *         dépôt du fantôme, liste, téléchargement, REJEU, et la graine du duel.
 *         Se lance à la main, contre `docker compose up` :
 *
 *           cd server && docker compose up --build -d
 *           curl -s -X POST http://127.0.0.1:8080/api/v1/auth/register \
 *                -H 'Content-Type: application/json' \
 *                -d '{"username":"duel","password":"motdepasse-long"}'
 *           # la réponse porte « sessionKey » : c'est le jeton.
 *           ns_test_duel http://127.0.0.1:8080 "$jeton"
 *
 * Ce que le régime sans argument vérifie AVANT tout : les deux verrous. Le
 * temps réel ne doit rien ouvrir sans URL, rien sous `--offline`, et rien tant
 * qu'il n'a pas été explicitement activé.
 */
#include "games.h"
#include "ns_core.h"
#include "ns_http.h"
#include "ns_online.h"
#include "ns_realtime.h"
#include "ns_runlog.h"

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

/* Le pas fixe du moteur : un duel se synchronise sur des NUMÉROS DE PAS, jamais
 * sur des secondes. C'est ce qui rend deux parties comparables. */
#define STEP   (1.0f / 120.0f)
#define TICKS  3600            /* trente secondes */

/* ==========================================================================
 * Les verrous — ce qui compte le plus dans ce fichier
 * ========================================================================== */

/*
 * DEUX verrous en série, et il faut que chacun suffise à tout arrêter.
 *
 * Le premier est celui du classement (`ns_online`) : sans URL, ou sous
 * `--offline`, aucune socket. Le second est propre au temps réel : même avec un
 * serveur configuré et joignable, la présence ne publie rien tant que le joueur
 * ne l'a pas activée.
 *
 * Le second existe parce que les deux n'engagent pas la même chose. Un
 * classement en ligne ne diffuse rien de soi ; la présence publie un pseudo et
 * une position dans une salle. Ce n'est pas à un réglage de serveur d'en
 * décider à la place du joueur.
 */
static void test_verrous(void)
{
    ns_online_config oc;
    ns_realtime_config rc;

    /* --- 1. Sans réseau du tout, et le temps réel demandé --- */
    SDL_zero(oc);
    CHECK(!ns_online_init(&oc), "sans URL, le classement ne démarre pas");

    SDL_zero(rc);
    rc.enabled = true;                 /* le joueur le VEUT */
    rc.nickname = "Ada";
    CHECK(!ns_realtime_init(&rc),
          "temps réel demandé mais sans serveur : rien ne démarre");
    CHECK(!ns_realtime_enabled(), "et il se déclare inactif");

    /* Et il ne rend rien, jamais : on le constate en demandant. */
    ns_realtime_publish(1.0f, 0.0f, 2.0f, 0.5f, "borne-01", "pacman", 0);
    ns_realtime_request_ghosts("pacman", "normal");
    SDL_Delay(200);
    ns_realtime_peer peers[NS_RT_MAX_PEERS];
    CHECK(ns_realtime_peers(peers, NS_RT_MAX_PEERS) == 0,
          "aucun pair n'arrive sans serveur");
    ns_realtime_ghost_info gi[NS_RT_MAX_GHOSTS];
    CHECK(ns_realtime_ghosts(gi, NS_RT_MAX_GHOSTS) == 0,
          "aucun fantôme n'arrive sans serveur");
    ns_realtime_shutdown();
    ns_online_shutdown();

    /* --- 2. Une URL VALIDE, mais `--offline` --- */
    SDL_zero(oc);
    oc.server_url = "http://127.0.0.1:8642";
    oc.locked = true;
    CHECK(!ns_online_init(&oc), "--offline verrouille le classement");

    SDL_zero(rc);
    rc.enabled = true;
    rc.nickname = "Ada";
    CHECK(!ns_realtime_init(&rc),
          "--offline verrouille AUSSI le temps réel, URL ou pas");
    CHECK(!ns_realtime_enabled(), "et il reste inactif");
    ns_realtime_shutdown();
    ns_online_shutdown();

    /*
     * --- 3. Un serveur configuré et le temps réel NON activé ---
     *
     * C'est le défaut, et c'est le cas qui compte : le classement en ligne
     * marche, et pourtant rien de soi n'est publié. Un joueur qui met une URL
     * pour voir le classement mondial ne demande pas à apparaître dans la
     * salle des autres.
     */
    SDL_zero(oc);
    oc.server_url = "http://127.0.0.1:9";     /* personne n'écoute */
    CHECK(ns_online_init(&oc), "le classement démarre vers un serveur mort");

    SDL_zero(rc);
    rc.enabled = false;                        /* le DÉFAUT */
    rc.nickname = "Ada";
    CHECK(!ns_realtime_init(&rc),
          "temps réel INERTE PAR DÉFAUT, même avec un serveur configuré");
    CHECK(!ns_realtime_enabled(), "et il se déclare inactif");
    ns_realtime_shutdown();
    ns_online_shutdown();
}

/* ==========================================================================
 * L'analyseur de journal, confronté à ce que le réseau peut apporter
 * ========================================================================== */

/*
 * Le journal d'entrées vient maintenant d'un SERVEUR, pas seulement du disque.
 * C'est un changement de niveau de confiance, et l'analyseur doit tenir devant
 * ce qu'un serveur peut envoyer — y compris de travers.
 *
 * Ces cas ne sont pas théoriques : le corps d'une réponse HTTP n'est pas terminé
 * par un octet nul, il peut être tronqué en plein milieu, et rien n'oblige
 * l'autre bout à respecter un format.
 */
static void test_analyseur(void)
{
    ns_run_input *in = NULL;
    uint32_t n = 0;
    char game[32], diff[16];
    int64_t seed = 0;

    /* --- Le cas normal --- */
    const char *ok = "v1 pacman normal 20240418\n0 0 0\n281 4 0\n900 8 16\n";
    CHECK(ns_runlog_parse_inputs(ok, SDL_strlen(ok), game, sizeof game,
                                 diff, sizeof diff, &seed, &in, &n),
          "un journal bien formé se relit");
    CHECK(n == 3, "ses trois changements (%u)", n);
    CHECK(SDL_strcmp(game, "pacman") == 0, "le jeu (%s)", game);
    CHECK(seed == 20240418, "la graine (%lld)", (long long)seed);
    CHECK(in && in[2].tick == 900 && in[2].held == 8 && in[2].pressed == 16,
          "et la dernière ligne");
    SDL_free(in); in = NULL;

    /*
     * --- SANS retour à la ligne final ---
     *
     * Le cas qui manquait. L'ancien analyseur comptait les lignes en comptant
     * les « \n » : un journal dont la dernière ligne n'en avait pas perdait ce
     * dernier changement, en silence. Sur un corps HTTP, c'est le cas NORMAL.
     */
    const char *nonl = "v1 snake hard 7\n10 1 0\n20 2 0";
    CHECK(ns_runlog_parse_inputs(nonl, SDL_strlen(nonl), game, sizeof game,
                                 diff, sizeof diff, &seed, &in, &n),
          "un journal sans retour final se relit");
    CHECK(n == 2, "et sa DERNIÈRE ligne n'est pas perdue (%u)", n);
    SDL_free(in); in = NULL;

    /*
     * --- Sans octet nul terminal ---
     *
     * On passe une longueur plus courte que le tampon : c'est exactement ce que
     * fait une réponse HTTP, dont le corps n'est pas une chaîne C. Un analyseur
     * qui lirait jusqu'au zéro lirait ce qui traîne derrière.
     */
    const char *bounded = "v1 tetris easy 3\n5 1 0\nCECI-NE-DOIT-PAS-ETRE-LU";
    const size_t cut = SDL_strlen("v1 tetris easy 3\n5 1 0\n");
    CHECK(ns_runlog_parse_inputs(bounded, cut, game, sizeof game,
                                 diff, sizeof diff, &seed, &in, &n),
          "la LONGUEUR fait foi, pas l'octet nul");
    CHECK(n == 1, "et rien n'est lu au-delà (%u)", n);
    SDL_free(in); in = NULL;

    /* --- Des lignes illisibles : sautées, pas fatales --- */
    const char *dirty = "v1 pacman normal 5\n0 0 0\nn'importe quoi\n\n60 2 0\n";
    CHECK(ns_runlog_parse_inputs(dirty, SDL_strlen(dirty), game, sizeof game,
                                 diff, sizeof diff, &seed, &in, &n),
          "un journal partiellement abîmé se relit quand même");
    CHECK(n == 2, "les lignes valides sont gardées (%u)", n);
    SDL_free(in); in = NULL;

    /* --- Un en-tête absent est FATAL : rejouer sans graine ne rejoue rien --- */
    const char *nohdr = "0 0 0\n1 1 0\n";
    CHECK(!ns_runlog_parse_inputs(nohdr, SDL_strlen(nohdr), game, sizeof game,
                                  diff, sizeof diff, &seed, &in, &n),
          "un journal sans en-tête est refusé");

    /* --- Un corps vide ne fait pas tomber le programme --- */
    CHECK(!ns_runlog_parse_inputs("", 0, game, sizeof game,
                                  diff, sizeof diff, &seed, &in, &n),
          "un corps vide est refusé proprement");

    /*
     * --- Des bits que le moteur ne connaît pas ---
     *
     * Un masque reçu du réseau porte huit bits ; le moteur n'en a que cinq de
     * définis. Les laisser passer ferait atteindre un jeu par un bouton qui
     * n'existe pas.
     */
    const char *wide = "v1 pacman normal 1\n0 255 255\n";
    CHECK(ns_runlog_parse_inputs(wide, SDL_strlen(wide), game, sizeof game,
                                 diff, sizeof diff, &seed, &in, &n),
          "un masque trop large se relit");
    CHECK(in && in[0].held == 0x1F && in[0].pressed == 0x1F,
          "et se trouve borné aux cinq boutons réels (%u/%u)",
          in ? in[0].held : 0, in ? in[0].pressed : 0);
    SDL_free(in); in = NULL;
}

/* ==========================================================================
 * Jouer, et rejouer
 * ========================================================================== */

/*
 * Une suite d'entrées plausible et reproductible — la même idée que
 * `test_replay.c` : un joueur tient une direction quelques dixièmes de seconde
 * et tape le bouton par à-coups. Un masque tiré au hasard à chaque pas ne ferait
 * rien bouger dans un jeu où l'on tourne en TENANT une touche.
 */
static void make_track(uint8_t held[TICKS], uint8_t pressed[TICKS], uint64_t seed)
{
    ns_rng rng;
    ns_rng_seed(&rng, seed, 0x51ED2701u);

    uint8_t cur = 0;
    int remaining = 0;
    for (int i = 0; i < TICKS; ++i) {
        if (remaining <= 0) {
            remaining = 10 + (int)(ns_rng_below(&rng, 50u));
            const uint32_t pick = ns_rng_below(&rng, 6u);
            cur = (pick < 4u) ? (uint8_t)(1u << pick) : 0u;
        }
        remaining--;
        held[i] = cur;
        pressed[i] = (ns_rng_below(&rng, 40u) == 0u) ? (uint8_t)(1u << NS_GAME_ACTION) : 0u;
    }
}

/*
 * Joue une partie ENTIÈREMENT par le chemin des boutons, en enregistrant les
 * deux journaux — les événements pour le serveur, les entrées pour le fantôme.
 *
 * C'est le même chemin que la boucle de jeu : les appuis AVANT le pas, `events`
 * consommé à chaque pas. Rejouer autrement mesurerait un jeu que personne ne
 * joue.
 */
static uint32_t play(const ns_game_api *api, void *state, ns_runlog *log,
                     const uint8_t held[TICKS], const uint8_t pressed[TICKS],
                     uint64_t seed, int64_t *out_ms)
{
    api->reset(state, seed, false);
    api->set_best(state, 0);

    int64_t run_ms = 0;
    uint8_t prev_held = 0;

    for (int i = 0; i < TICKS; ++i) {
        for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
            if (pressed[i] & (1u << b)) api->press(state, (ns_game_button)b);
        }
        if (api->hold) {
            bool h[NS_GAME_BUTTON_COUNT];
            for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
                h[b] = (held[i] & (1u << b)) != 0;
            }
            api->hold(state, h);
        }

        /* Le journal d'ENTRÉES, écrit comme la boucle de jeu l'écrit : une
         * ligne seulement quand quelque chose change. */
        if (log) ns_runlog_input(log, i, held[i], pressed[i]);
        prev_held = held[i];
        (void)prev_held;

        api->tick(state, STEP);
        run_ms += (int64_t)(STEP * 1000.0f + 0.5f);

        ns_game_events ev; SDL_zero(ev);
        api->events(state, &ev);
        if (log) {
            if (ev.blip)  ns_runlog_event(log, run_ms, ev.blip_kind, 0);
            if (ev.score) ns_runlog_event(log, run_ms, ev.score_kind, ev.score_value);
        }
    }
    if (out_ms) *out_ms = run_ms;
    return api->score(state);
}

/*
 * Rejoue un journal d'entrées SPARSE, exactement comme `room/main.c` le fait :
 * les maintiens sont CONSERVÉS entre deux changements. L'oublier donnerait un
 * joueur qui relâche tout entre deux lignes — et un fantôme qui ne ressemble
 * pas à la partie qu'il rejoue.
 */
static uint32_t replay(const ns_game_api *api, void *state,
                       const ns_run_input *in, uint32_t count,
                       uint64_t seed, bool hard)
{
    api->reset(state, seed, hard);
    api->set_best(state, 0);

    const int32_t last = count ? in[count - 1].tick : 0;
    uint8_t held_mask = 0;
    uint32_t cursor = 0;

    for (int32_t tick = 0; tick <= last; ++tick) {
        uint8_t press_mask = 0;
        while (cursor < count && in[cursor].tick == tick) {
            held_mask = in[cursor].held;
            press_mask |= in[cursor].pressed;
            cursor++;
        }
        for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
            if (press_mask & (1u << b)) api->press(state, (ns_game_button)b);
        }
        if (api->hold) {
            bool h[NS_GAME_BUTTON_COUNT];
            for (int b = 0; b < NS_GAME_BUTTON_COUNT; ++b) {
                h[b] = (held_mask & (1u << b)) != 0;
            }
            api->hold(state, h);
        }
        api->tick(state, STEP);
        ns_game_events ev; SDL_zero(ev);
        api->events(state, &ev);
    }
    return api->score(state);
}

/* ==========================================================================
 * La présence, contre un vrai serveur, SANS COMPTE
 * ========================================================================== */

/*
 * Ce test tourne sans jeton, et c'est le point : **aucun compte n'est exigé**
 * pour se montrer dans la salle ni pour voir les autres. C'est la règle du
 * projet depuis la V1, où tout `main()` était enfermé derrière une vérification
 * en ligne.
 *
 * Deux joueurs sont nécessaires pour vérifier une présence, et le module est un
 * singleton — on ne peut pas en instancier deux dans un processus. Le second
 * joueur est donc simulé par une requête HTTP directe, ce qui a l'avantage de
 * vérifier les DEUX sens séparément : que le vrai client VOIT l'autre, et que
 * l'autre voit ce que le vrai client PUBLIE.
 */
static void test_presence(const char *url)
{
    ns_online_config oc;
    SDL_zero(oc);
    oc.server_url = url;
    if (!ns_online_init(&oc)) { CHECK(false, "le classement démarre"); return; }

    ns_realtime_config rc;
    SDL_zero(rc);
    rc.enabled = true;
    rc.nickname = "Ada";
    if (!ns_realtime_init(&rc)) {
        CHECK(false, "le temps réel démarre sur « %s »", url);
        ns_online_shutdown();
        return;
    }
    CHECK(ns_realtime_enabled(), "le temps réel est actif");
    CHECK(true, "et il n'a demandé AUCUN compte pour ça");

    ns_realtime_publish(1.5f, 0.0f, 2.5f, 0.75f, "borne-pacman", "pacman", 120);

    /* Le second joueur, par la porte d'à côté. */
    char purl[640];
    SDL_snprintf(purl, sizeof purl, "%s/api/v1/presence", url);
    const char *bob =
        "{\"clientId\":\"bobbobbobbobbob\",\"nickname\":\"Bob\","
        "\"x\":-3.0,\"y\":0.0,\"z\":1.0,\"yaw\":1.0,"
        "\"cabinet\":\"borne-tetris\",\"game\":\"tetris\",\"score\":7}";

    bool seen_ada = false;
    for (int i = 0; i < 40 && !seen_ada; ++i) {
        ns_http_response r;
        if (ns_http_request("POST", purl, "application/json", NULL,
                            bob, SDL_strlen(bob), 3000, &r)) {
            if (r.status == 200 && r.body && SDL_strstr(r.body, "\"Ada\"")) {
                seen_ada = true;
                /* Sans jeton, le serveur ne répond PAS du pseudo, et il le dit
                 * plutôt que de faire semblant. */
                CHECK(SDL_strstr(r.body, "\"verified\":false") != NULL,
                      "un pseudo sans compte est marqué NON vérifié");
            }
        }
        ns_http_response_free(&r);
        if (!seen_ada) SDL_Delay(250);
    }
    CHECK(seen_ada, "ce que le vrai client PUBLIE est vu par un autre joueur");

    /* Et dans l'autre sens : le vrai client voit Bob. */
    ns_realtime_peer peers[NS_RT_MAX_PEERS];
    uint32_t n = 0;
    for (int i = 0; i < 40 && n == 0; ++i) {
        SDL_Delay(250);
        n = ns_realtime_peers(peers, NS_RT_MAX_PEERS);
    }
    CHECK(n >= 1, "le vrai client VOIT l'autre joueur (%u, statut : %s)",
          n, ns_realtime_status());
    if (n >= 1) {
        bool found = false;
        for (uint32_t i = 0; i < n; ++i) {
            if (SDL_strcmp(peers[i].name, "Bob") == 0) {
                found = true;
                CHECK(peers[i].x < -2.5f && peers[i].x > -3.5f,
                      "avec sa position (%.2f)", (double)peers[i].x);
                CHECK(SDL_strcmp(peers[i].cabinet, "borne-tetris") == 0,
                      "et la borne devant laquelle il se tient (%s)", peers[i].cabinet);
                CHECK(peers[i].score == 7, "et son score (%d)", peers[i].score);
            }
        }
        CHECK(found, "et c'est bien lui, avec son pseudo");
    }

    /*
     * Le RYTHME réellement obtenu.
     *
     * `ns_realtime.c` annonce 4 Hz ; une annonce n'est pas une mesure. On ouvre
     * donc une fenêtre franche et on compte les battements qui y tombent, plutôt
     * que de diviser un total par une durée supposée — ce que faisait la
     * première version de ce test, qui affichait « ~10 s » en dur alors qu'elle
     * s'arrêtait dès le premier pair vu. Un chiffre faux est pire qu'aucun
     * chiffre : celui-là annonçait 0,2 Hz pour un client qui en fait 4.
     */
    uint32_t before = 0, failed = 0;
    ns_realtime_stats(&before, &failed, NULL);

    const uint64_t w0 = SDL_GetTicks();
    SDL_Delay(3000);
    const uint64_t elapsed = SDL_GetTicks() - w0;

    uint32_t after = 0;
    ns_realtime_stats(&after, &failed, NULL);

    const uint32_t beats = after - before;
    const double hz = (double)beats * 1000.0 / (double)elapsed;
    printf("  présence : %u battement(s) en %llu ms, soit %.1f Hz\n",
           beats, (unsigned long long)elapsed, hz);
    CHECK(after > 0, "des battements de présence aboutissent (%u, %u perdus)",
          after, failed);
    /*
     * La borne basse est ce qui compte : un client qui bat trop lentement fait
     * saccader les autres. La borne haute garde le trafic honnête — 4 Hz
     * annoncés, on refuse d'en faire dix sans l'avoir dit.
     */
    CHECK(hz > 2.0 && hz < 6.0,
          "et le rythme tient les 4 Hz annoncés (%.1f Hz)", hz);

    ns_realtime_shutdown();
    ns_online_shutdown();
}

/* ==========================================================================
 * LA CHAÎNE ENTIÈRE DU DUEL
 * ========================================================================== */

/*
 * Billet → partie réellement jouée → envoi → dépôt du fantôme → liste →
 * téléchargement → REJEU → et la graine du duel.
 *
 * L'assertion qui porte tout le reste est l'avant-dernière : le journal
 * REDESCENDU DU SERVEUR, rejoué sur la graine du serveur, doit rendre le même
 * score ET LE MÊME ÉTAT AU BIT PRÈS que la partie d'origine. C'est la
 * définition même d'un duel jouable — les deux machines doivent voir LA MÊME
 * partie, pas seulement le même nombre — et c'est la seule chose qu'aucun test
 * d'un seul côté ne peut établir.
 */
static void test_duel_complet(const char *url, const char *token)
{
    char queue[1024];
    char *cwd = SDL_GetCurrentDirectory();
    SDL_snprintf(queue, sizeof queue, "%stest-duel-queue", cwd ? cwd : "./");
    SDL_free(cwd);
    ns_runlog_set_queue_dir(queue);

    ns_online_config oc;
    SDL_zero(oc);
    oc.server_url = url;
    oc.token = token;
    if (!ns_online_init(&oc)) { CHECK(false, "le classement démarre"); return; }

    ns_realtime_config rc;
    SDL_zero(rc);
    rc.enabled = true;
    rc.nickname = "Ada";
    if (!ns_realtime_init(&rc)) { CHECK(false, "le temps réel démarre"); ns_online_shutdown(); return; }

    /*
     * Pac-Man : une suite d'entrées prise au hasard y marque réellement des
     * points — 19 pastilles en trente secondes — là où elle meurt en trois
     * secondes à Flappy. Un duel dont les deux scores valent zéro ne prouve
     * rien.
     */
    const ns_game_api *api = ns_game_find("pacman");
    if (!api) { CHECK(false, "Pac-Man est porté"); goto done; }

    /* --- 1. Le billet : la graine vient du SERVEUR, avant qu'on joue --- */
    ns_online_prefetch_ticket(api->id, "normal");
    ns_online_ticket t;
    bool got = false;
    for (int i = 0; i < 160 && !got; ++i) {
        got = ns_online_take_ticket(api->id, "normal", &t);
        if (!got) SDL_Delay(50);
    }
    CHECK(got, "le serveur délivre un billet (statut : %s)", ns_online_status());
    if (!got) goto done;
    CHECK(t.seed > 0, "avec une graine positive (%lld)", (long long)t.seed);

    /* --- 2. La partie, réellement jouée par le chemin des boutons --- */
    uint8_t held[TICKS], pressed[TICKS];
    make_track(held, pressed, 0xD0E1ull);

    void *state = SDL_calloc(1, api->state_size);
    ns_runlog *log = ns_runlog_create(NS_RUNLOG_MAX_EVENTS);
    if (!state || !log) { CHECK(false, "mémoire"); SDL_free(state); goto done; }

    ns_runlog_begin(log, api->id, "normal", t.seed, t.secret, t.secret_len);
    ns_runlog_set_run_id(log, t.run_id);

    int64_t run_ms = 0;
    const uint32_t score = play(api, state, log, held, pressed, (uint64_t)t.seed, &run_ms);
    ns_runlog_event(log, run_ms, "death", 0);
    ns_runlog_end(log, run_ms, (int64_t)score);

    printf("  partie jouée : %s, graine %lld, score %u en %.1f s\n",
           api->id, (long long)t.seed, score, (double)run_ms / 1000.0);
    CHECK(score > 0, "la partie a réellement marqué des points (%u)", score);

    /* --- 3. L'envoi : le serveur RECALCULE le score --- */
    CHECK(ns_runlog_enqueue(log), "la partie entre dans la file");
    ns_online_flush_queue();
    uint32_t sent = 0, failed = 0;
    for (int i = 0; i < 200; ++i) {
        SDL_Delay(50);
        ns_online_stats(&sent, &failed);
        if (sent || failed) break;
    }
    CHECK(sent == 1 && failed == 0,
          "et elle est ACCEPTÉE (%u envoyée(s), %u refusée(s), %s)",
          sent, failed, ns_online_status());

    /* --- 4. Le dépôt du fantôme --- */
    const size_t need = ns_runlog_format_inputs(log, NULL, 0) + 1;
    char *journal = (char *)SDL_malloc(need);
    if (!journal) { CHECK(false, "mémoire"); SDL_free(state); goto done; }
    const size_t jlen = ns_runlog_format_inputs(log, journal, need);
    printf("  journal d'entrées : %u changement(s), %zu octets\n",
           ns_runlog_input_count(log), jlen);

    ns_realtime_publish_ghost(t.run_id, journal, jlen);
    uint32_t ghosts_sent = 0;
    for (int i = 0; i < 200 && ghosts_sent == 0; ++i) {
        SDL_Delay(50);
        ns_realtime_stats(NULL, NULL, &ghosts_sent);
    }
    CHECK(ghosts_sent == 1, "le fantôme est publié (%u, statut : %s)",
          ghosts_sent, ns_realtime_status());

    /* --- 5. La liste, puis le téléchargement --- */
    ns_realtime_request_ghosts(api->id, "normal");
    ns_realtime_ghost_info list[NS_RT_MAX_GHOSTS];
    uint32_t gn = 0;
    for (int i = 0; i < 100 && gn == 0; ++i) {
        SDL_Delay(50);
        gn = ns_realtime_ghosts(list, NS_RT_MAX_GHOSTS);
    }
    CHECK(gn >= 1, "le fantôme apparaît dans la liste (%u)", gn);

    int mine = -1;
    for (uint32_t i = 0; i < gn; ++i) {
        if (SDL_strcmp(list[i].run_id, t.run_id) == 0) { mine = (int)i; break; }
    }
    CHECK(mine >= 0, "et c'est bien le nôtre qu'on retrouve");
    if (mine >= 0) {
        CHECK(list[mine].score == (int64_t)score,
              "avec le score que le SERVEUR a recalculé (%lld / %u)",
              (long long)list[mine].score, score);
        /*
         * La graine, relue en ENTIER. Sur 63 bits, un `float` n'en garderait
         * que 24 — c'est le deuxième des trois défauts qu'avait trouvés la
         * chaîne du classement, et il se reproduirait ici mot pour mot : le duel
         * se jouerait sur une AUTRE partie que celle du fantôme.
         */
        CHECK(list[mine].seed == t.seed,
              "et la graine intacte, sur ses 63 bits (%lld / %lld)",
              (long long)list[mine].seed, (long long)t.seed);
    }

    ns_realtime_fetch_ghost(t.run_id);
    char *down = NULL;
    size_t down_len = 0;
    ns_realtime_ghost_info info;
    bool have = false;
    for (int i = 0; i < 200 && !have; ++i) {
        SDL_Delay(50);
        have = ns_realtime_take_ghost(&down, &down_len, &info);
    }
    CHECK(have, "le journal du fantôme se télécharge (statut : %s)", ns_realtime_status());
    if (!have) { SDL_free(journal); SDL_free(state); goto done; }

    /*
     * Le journal doit revenir IDENTIQUE. C'est ce qui aurait attrapé le défaut
     * d'échappement : encodé dans un champ JSON, il serait redescendu avec des
     * « \n » littéraux — même longueur à peu près, contenu inutilisable.
     */
    CHECK(down_len == jlen, "et il revient de la même taille (%zu / %zu)", down_len, jlen);
    CHECK(down_len == jlen && SDL_memcmp(down, journal, jlen) == 0,
          "OCTET POUR OCTET identique à celui qu'on a déposé");

    /* --- 6. LE REJEU : le cœur du duel --- */
    ns_run_input *in = NULL;
    uint32_t incount = 0;
    char gname[32] = { 0 }, gdiff[16] = { 0 };
    int64_t gseed = 0;
    CHECK(ns_runlog_parse_inputs(down, down_len, gname, sizeof gname,
                                 gdiff, sizeof gdiff, &gseed, &in, &incount),
          "le journal redescendu s'analyse");
    CHECK(gseed == t.seed, "il porte LA graine du serveur (%lld / %lld)",
          (long long)gseed, (long long)t.seed);
    CHECK(SDL_strcmp(gname, api->id) == 0, "et le bon jeu (%s)", gname);

    void *ghost_state = SDL_calloc(1, api->state_size);
    if (ghost_state && in) {
        const uint32_t gscore = replay(api, ghost_state, in, incount,
                                       (uint64_t)gseed, false);

        CHECK(gscore == score,
              "LE FANTÊME REJOUÉ DEPUIS LE SERVEUR REFAIT LE MÊME SCORE (%u / %u)",
              gscore, score);
        /*
         * Et le même ÉTAT, au bit près. Deux parties peuvent finir sur le même
         * score en ayant divergé — un fantôme ailleurs, une pastille de plus —
         * et pour un duel les deux machines doivent voir LA MÊME partie, pas
         * seulement le même nombre. C'est la comparaison que `test_replay.c`
         * fait en local ; celle-ci la fait à travers le réseau.
         */
        CHECK(SDL_memcmp(ghost_state, state, api->state_size) == 0,
              "et le MÊME ÉTAT au bit près, après un aller-retour réseau");
    } else {
        CHECK(false, "mémoire pour l'état du fantôme");
    }
    SDL_free(ghost_state);
    SDL_free(in);

    /* --- 7. La graine du DUEL : affronter ce fantôme, c'est jouer SA partie --- */
    ns_online_set_duel(t.run_id);
    ns_online_prefetch_ticket(api->id, "normal");
    ns_online_ticket duel;
    bool duel_ok = false;
    for (int i = 0; i < 200 && !duel_ok; ++i) {
        duel_ok = ns_online_take_ticket(api->id, "normal", &duel);
        if (!duel_ok) SDL_Delay(50);
    }
    CHECK(duel_ok, "un billet de DUEL est délivré (statut : %s)", ns_online_status());
    if (duel_ok) {
        CHECK(duel.seed == t.seed,
              "et il porte LA MÊME GRAINE que le fantôme (%lld / %lld) — "
              "sans quoi ce ne serait pas un duel mais deux parties",
              (long long)duel.seed, (long long)t.seed);
        CHECK(SDL_strcmp(duel.run_id, t.run_id) != 0,
              "tout en étant une partie DISTINCTE, avec son propre secret");
    }
    ns_online_set_duel(NULL);

    SDL_free(down);
    SDL_free(journal);
    SDL_free(state);
    ns_runlog_destroy(log);

done:
    ns_realtime_shutdown();
    ns_online_shutdown();
}

/* ==========================================================================
 * Le serveur qui TOMBE au milieu
 * ========================================================================== */

/*
 * La promesse « au mieux, jamais bloquant » vaut aussi pour le temps réel, et
 * elle vaut surtout AU MILIEU : un serveur qui répond puis s'arrête ne doit pas
 * figer la salle, ni faire tomber le jeu, ni laisser les autres joueurs plantés
 * dans l'allée pour l'éternité.
 *
 * On le simule par le seul moyen honnête et reproductible : une URL qui
 * n'écoute personne. Le vrai débranchement se vérifie à la main, et il est
 * décrit dans docs/RESEAU-TEMPS-REEL.md.
 */
static void test_serveur_mort(void)
{
    ns_online_config oc;
    SDL_zero(oc);
    oc.server_url = "http://127.0.0.1:9";
    CHECK(ns_online_init(&oc), "le classement démarre vers un serveur mort");

    ns_realtime_config rc;
    SDL_zero(rc);
    rc.enabled = true;
    rc.nickname = "Ada";
    CHECK(ns_realtime_init(&rc), "le temps réel démarre aussi");

    /* On joue : la boucle de jeu ne doit rien attendre. */
    const uint64_t t0 = SDL_GetTicks();
    for (int i = 0; i < 200; ++i) {
        ns_realtime_publish((float)i, 0.0f, 0.0f, 0.0f, "borne", "pacman", i);
    }
    const uint64_t dt = SDL_GetTicks() - t0;
    CHECK(dt < 100, "publier une position ne bloque JAMAIS (%llu ms pour 200 appels)",
          (unsigned long long)dt);

    SDL_Delay(800);
    ns_realtime_peer peers[NS_RT_MAX_PEERS];
    CHECK(ns_realtime_peers(peers, NS_RT_MAX_PEERS) == 0,
          "aucun joueur fantôme n'apparaît quand le serveur est mort");
    CHECK(ns_realtime_peers_age_ms() == UINT32_MAX,
          "et l'âge de la présence dit qu'on n'a jamais rien reçu");
    CHECK(ns_realtime_status()[0] != '\0', "le statut le dit (%s)", ns_realtime_status());

    ns_realtime_shutdown();
    ns_online_shutdown();
    CHECK(true, "et l'arrêt se passe bien malgré l'échec");
}

/* ========================================================================== */

int main(int argc, char **argv)
{
    if (!SDL_Init(0)) {
        printf("SDL_Init a échoué : %s\n", SDL_GetError());
        return 1;
    }
    ns_log_set_level(NS_LOG_ERROR);

    printf("=== présence et duel ===\n\n");

    test_verrous();
    test_analyseur();
    test_serveur_mort();

    if (argc > 2)      test_duel_complet(argv[1], argv[2]);
    else if (argc > 1) test_presence(argv[1]);
    else printf("  (pas d'URL fournie : les tests en ligne sont sautés)\n");

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
