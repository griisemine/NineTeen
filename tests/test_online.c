/*
 * test_online.c — le client HTTP et la couche de classement, sans deviner.
 *
 * Ce test ouvre de VRAIES sockets. C'est délibéré : le reste du moteur se
 * vérifie sans périphérique, mais un client réseau qu'on ne fait jamais parler
 * à personne est un client dont on ne sait rien.
 *
 * Trois régimes, selon les arguments — et CTest ne lance AUCUN serveur, contre
 * ce que disait cette entête jusqu'ici :
 *
 *   ns_test_online
 *       découpage d'URL, ORDRE DE PRÉSÉANCE des quatre sources d'adresse,
 *       verrou `--offline`, serveur mort. C'est ce que fait la CI, et ça ne
 *       demande rien — pas même une socket, pour la partie préséance, qui est
 *       du calcul pur.
 *
 *   ns_test_online <url>
 *       + le classement mondial, contre un serveur qui répond comme le vrai.
 *
 *   ns_test_online <url> <jeton-de-session>
 *       + LA CHAÎNE ENTIÈRE contre le vrai `nineteend` et sa base : billet,
 *       partie, sceau, file, envoi, classement. C'est le seul régime qui
 *       confronte les deux moitiés écrites en C et en Go ; il se lance à la
 *       main, avec un serveur de développement :
 *
 *         NINETEEN_DB_URL=… go run ./cmd/nineteend &
 *         jeton=$(curl -s -X POST …/api/v1/auth/login … | …)
 *         ns_test_online http://127.0.0.1:8080 "$jeton"
 *
 * Ce qui est vérifié avant tout dans le régime sans argument : **le verrou**.
 * `--offline` et l'absence d'URL doivent empêcher toute connexion, et ça se
 * teste en demandant un classement puis en constatant qu'aucune réponse
 * n'arrive jamais.
 */
#include "ns_core.h"
#include "ns_http.h"
#include "ns_online.h"
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

/* -------------------------------------------------------------------------- */

static void test_parse_url(void)
{
    char host[64], path[128], err[128];
    uint16_t port = 0;

    CHECK(ns_http_parse_url("http://example.org/api/v1/games", host, sizeof host,
                            &port, path, sizeof path, err, sizeof err),
          "une URL simple se découpe");
    CHECK(SDL_strcmp(host, "example.org") == 0, "hôte (%s)", host);
    CHECK(port == 80, "port par défaut (%u)", port);
    CHECK(SDL_strcmp(path, "/api/v1/games") == 0, "chemin (%s)", path);

    CHECK(ns_http_parse_url("http://127.0.0.1:8642/x", host, sizeof host,
                            &port, path, sizeof path, err, sizeof err),
          "un port explicite se lit");
    CHECK(port == 8642, "port (%u)", port);

    /* Sans chemin, c'est la racine — et pas une chaîne vide, qui donnerait une
     * requête « GET  HTTP/1.1 » que personne ne comprend. */
    CHECK(ns_http_parse_url("http://serveur", host, sizeof host, &port,
                            path, sizeof path, err, sizeof err),
          "une URL sans chemin se découpe");
    CHECK(SDL_strcmp(path, "/") == 0, "chemin par défaut (%s)", path);

    /*
     * `https` est REFUSÉ, pas tenté en clair. Le traiter comme du HTTP
     * enverrait le jeton de session en clair sur un port qui ne le comprendra
     * pas : mieux vaut échouer bruyamment.
     */
    CHECK(!ns_http_parse_url("https://example.org/", host, sizeof host, &port,
                             path, sizeof path, err, sizeof err),
          "https est refusé");
    CHECK(err[0] != '\0', "et la raison est donnée (%s)", err);

    CHECK(!ns_http_parse_url("http://:8080/x", host, sizeof host, &port,
                             path, sizeof path, err, sizeof err),
          "une URL sans hôte est refusée");
}

/*
 * L'ORDRE DE PRESEANCE DES QUATRE SOURCES D'ADRESSE.
 *
 *   défaut compilé  <  config  <  environnement  <  --server=
 *
 * Pourquoi ça mérite un test plutôt qu'une relecture : la cascade est le genre
 * de code qui a l'air juste dans les deux sens. Une inversion entre
 * l'environnement et la config ne casse RIEN de visible — le jeu démarre, parle
 * à un serveur, affiche un classement — elle envoie simplement les scores
 * ailleurs. C'est exactement la panne muette que ce dépôt traque.
 *
 * Les seize combinaisons de présence/absence sont couvertes, et pas seulement
 * les quatre cas « une seule source » : c'est le passage d'un niveau à l'autre
 * qui se trompe, pas le niveau isolé.
 */
static void test_preseance_url(void)
{
    static const char *C = "http://compilee";   /* défaut cuit au build */
    static const char *G = "http://config";     /* settings.cfg */
    static const char *E = "http://environ";    /* NINETEEN_SERVER_URL */
    static const char *L = "http://ligne";      /* --server= */

    /* Les seize combinaisons, dans l'ordre du masque (bit 0 = compilée,
     * 1 = config, 2 = environnement, 3 = ligne de commande). Le gagnant attendu
     * est le bit le plus haut qui soit posé. */
    for (int mask = 0; mask < 16; ++mask) {
        const char *c = (mask & 1) ? C : NULL;
        const char *g = (mask & 2) ? G : NULL;
        const char *e = (mask & 4) ? E : NULL;
        const char *l = (mask & 8) ? L : NULL;

        const char *attendu_url = NULL;
        ns_online_source attendu_src = NS_ONLINE_SRC_AUCUNE;
        if (mask & 8)      { attendu_url = L; attendu_src = NS_ONLINE_SRC_LIGNE_COMMANDE; }
        else if (mask & 4) { attendu_url = E; attendu_src = NS_ONLINE_SRC_ENVIRONNEMENT; }
        else if (mask & 2) { attendu_url = G; attendu_src = NS_ONLINE_SRC_CONFIG; }
        else if (mask & 1) { attendu_url = C; attendu_src = NS_ONLINE_SRC_COMPILEE; }

        const ns_online_url r = ns_online_resolve_url(c, g, e, l);
        if (attendu_url == NULL) {
            CHECK(r.url == NULL, "combinaison %d : rien nulle part, aucune URL", mask);
        } else {
            CHECK(r.url != NULL && SDL_strcmp(r.url, attendu_url) == 0,
                  "combinaison %d : « %s » l'emporte (obtenu « %s »)",
                  mask, attendu_url, r.url ? r.url : "(null)");
        }
        CHECK(r.source == attendu_src,
              "combinaison %d : la source retenue est « %s » (obtenu « %s »)",
              mask, ns_online_source_nom(attendu_src), ns_online_source_nom(r.source));
    }

    /*
     * LE CAS QUI COMPTE LE PLUS : les quatre vides. C'est le dépôt fraîchement
     * cloné et bâti sans rien demander, et la promesse de ce fichier veut qu'il
     * n'ouvre aucune socket. `""` et non NULL : c'est ce que rendent réellement
     * `NINETEEN_SERVER_URL` non défini au build et `ns_config_get_str(…, "")`.
     */
    {
        const ns_online_url r = ns_online_resolve_url("", "", NULL, NULL);
        CHECK(r.url == NULL, "quatre sources vides : aucune URL, donc aucune socket");
        CHECK(r.source == NS_ONLINE_SRC_AUCUNE, "et aucune source");
        /* Et la chaîne vide ne doit jamais ressortir telle quelle : un appelant
         * qui teste `url != NULL` et un autre qui teste `url[0]` doivent tomber
         * d'accord. */
        ns_online_config cfg;
        SDL_zero(cfg);
        cfg.server_url = r.url;
        cfg.source = r.source;
        CHECK(!ns_online_init(&cfg), "et ns_online_init refuse de démarrer");
        ns_online_shutdown();
    }

    /* Une source faite de BLANCS compte pour absente : `NINETEEN_SERVER_URL=`
     * dans un .env, ou `-e NINETEEN_SERVER_URL=" "`, ne doivent pas produire une
     * URL d'un caractère qui échouerait plus loin sans dire d'où elle vient. */
    {
        const ns_online_url r = ns_online_resolve_url(C, "  ", " \t ", "");
        CHECK(r.url != NULL && SDL_strcmp(r.url, C) == 0,
              "les blancs ne comptent pas : le défaut compilé reste (« %s »)",
              r.url ? r.url : "(null)");
        CHECK(r.source == NS_ONLINE_SRC_COMPILEE, "et sa source est nommée comme telle");
    }

    /*
     * `--offline` n'est PAS un cinquième niveau : il ne change pas la source
     * retenue, il interdit d'en faire quoi que ce soit. Le vérifier ici plutôt
     * que de le supposer — c'est ce qui garantit que le verrou tient QUELLE QUE
     * SOIT la source, et pas seulement pour celle qu'on avait en tête.
     */
    for (int mask = 1; mask < 16; ++mask) {
        const ns_online_url r = ns_online_resolve_url(
            (mask & 1) ? C : NULL, (mask & 2) ? G : NULL,
            (mask & 4) ? E : NULL, (mask & 8) ? L : NULL);
        ns_online_config cfg;
        SDL_zero(cfg);
        cfg.server_url = r.url;
        cfg.source = r.source;
        cfg.locked = true;
        CHECK(!ns_online_init(&cfg),
              "combinaison %d : --offline verrouille, quelle que soit la source", mask);
        CHECK(!ns_online_enabled(), "combinaison %d : et le réseau reste inactif", mask);
        ns_online_shutdown();
    }

    /* Le nom de chaque source est imprimable et distinct : c'est ce que le
     * journal de démarrage montre, et deux sources qui s'appelleraient pareil ne
     * diagnostiqueraient rien. */
    {
        const ns_online_source tous[] = {
            NS_ONLINE_SRC_AUCUNE, NS_ONLINE_SRC_COMPILEE, NS_ONLINE_SRC_CONFIG,
            NS_ONLINE_SRC_ENVIRONNEMENT, NS_ONLINE_SRC_LIGNE_COMMANDE,
        };
        const int n = (int)(sizeof tous / sizeof tous[0]);
        for (int i = 0; i < n; ++i) {
            const char *ni = ns_online_source_nom(tous[i]);
            CHECK(ni != NULL && ni[0] != '\0', "source %d : elle a un nom", (int)tous[i]);
            for (int j = i + 1; j < n; ++j) {
                CHECK(SDL_strcmp(ni, ns_online_source_nom(tous[j])) != 0,
                      "sources %d et %d : des noms distincts (« %s »)",
                      (int)tous[i], (int)tous[j], ni);
            }
        }
    }
}

static void test_verrou_hors_ligne(void)
{
    /*
     * LE test qui compte. Sans URL, et sous `--offline`, aucune socket ne doit
     * s'ouvrir — ce qui se constate en demandant un classement et en vérifiant
     * qu'il n'arrive jamais.
     */
    ns_online_config cfg;
    SDL_zero(cfg);
    CHECK(!ns_online_init(&cfg), "sans URL, le réseau ne démarre pas");
    CHECK(!ns_online_enabled(), "et il se déclare inactif");
    ns_online_request_board("snake", "normal");
    ns_online_board board;
    CHECK(!ns_online_board_get("snake", "normal", &board),
          "une demande sans serveur ne rend jamais rien");
    ns_online_shutdown();

    SDL_zero(cfg);
    cfg.server_url = "http://127.0.0.1:8642";
    cfg.locked = true;
    CHECK(!ns_online_init(&cfg), "--offline verrouille MÊME avec une URL valide");
    CHECK(!ns_online_enabled(), "et le réseau reste inactif");
    ns_online_request_board("snake", "normal");
    SDL_Delay(200);
    CHECK(!ns_online_board_get("snake", "normal", &board),
          "verrouillé, aucun classement n'arrive");
    ns_online_shutdown();
}

static void test_classement_en_ligne(const char *url)
{
    ns_online_config cfg;
    SDL_zero(cfg);
    cfg.server_url = url;

    if (!ns_online_init(&cfg)) {
        CHECK(false, "le réseau démarre sur « %s »", url);
        return;
    }
    CHECK(ns_online_enabled(), "le réseau est actif");

    ns_online_request_board("snake", "normal");

    /* On attend la réponse, mais PAS indéfiniment : un test qui pend est un
     * test qui bloque l'intégration continue. */
    ns_online_board board;
    bool got = false;
    for (int i = 0; i < 100 && !got; ++i) {
        SDL_Delay(50);
        got = ns_online_board_get("snake", "normal", &board);
    }

    CHECK(got, "le classement mondial arrive (statut : %s)", ns_online_status());
    if (got) {
        CHECK(board.count == 3, "trois entrées (%u)", board.count);
        CHECK(SDL_strcmp(board.row[0].name, "ADA") == 0, "premier (%s)", board.row[0].name);
        CHECK(board.row[0].score == 4820, "son score (%u)", board.row[0].score);
        CHECK(board.row[2].score == 990, "et le dernier (%u)", board.row[2].score);
        /* Le classement doit être DÉCROISSANT : un tableau qui remonte ne se lit
         * pas comme un classement. */
        CHECK(board.row[0].score >= board.row[1].score
              && board.row[1].score >= board.row[2].score, "et il décroît");
    }

    /* Un jeu que le serveur ne connaît pas ne doit ni planter ni inventer. */
    ns_online_request_board("inexistant", "normal");
    SDL_Delay(400);
    ns_online_board none;
    CHECK(!ns_online_board_get("inexistant", "normal", &none),
          "un jeu inconnu du serveur ne rend rien");

    ns_online_shutdown();
}

static void test_serveur_mort(void)
{
    /*
     * Un serveur qui ne répond pas ne doit RIEN casser : c'est toute la
     * promesse « au mieux, jamais bloquant ». Le port choisi n'écoute personne.
     */
    ns_online_config cfg;
    SDL_zero(cfg);
    cfg.server_url = "http://127.0.0.1:9";
    CHECK(ns_online_init(&cfg), "le réseau démarre même vers un serveur mort");
    ns_online_request_board("snake", "normal");
    SDL_Delay(600);
    ns_online_board b;
    CHECK(!ns_online_board_get("snake", "normal", &b), "aucun classement n'arrive");
    CHECK(ns_online_status()[0] != '\0', "et le statut le dit (%s)", ns_online_status());
    ns_online_shutdown();
    CHECK(true, "l'arrêt se passe bien malgré l'échec");
}

/*
 * La chaîne COMPLÈTE, contre un vrai serveur : billet → partie → sceau → file
 * → envoi → classement.
 *
 * Ne tourne que si un jeton de session est fourni — c'est-à-dire face au vrai
 * `nineteend` avec sa base, pas face au bouchon de CTest. C'est le seul test qui
 * mette bout à bout les deux moitiés écrites de part et d'autre (C et Go), et
 * les deux défauts qu'il attrape sont ceux qui ont réellement eu lieu :
 *
 *  - une graine relue en `float` : le client joue une AUTRE partie que celle
 *    ouverte, et le serveur refuse le sceau ;
 *  - une enveloppe postée telle quelle : `DisallowUnknownFields` répond 400,
 *    donc le fichier est supprimé et la partie perdue en croyant l'envoyer.
 *
 * Les deux se voient ici, et nulle part ailleurs.
 */
static void test_partie_en_ligne(const char *url, const char *token)
{
    char queue[1024];
    char *cwd = SDL_GetCurrentDirectory();   /* rendu sur le tas : à libérer */
    SDL_snprintf(queue, sizeof queue, "%stest-online-queue", cwd ? cwd : "./");
    SDL_free(cwd);
    ns_runlog_set_queue_dir(queue);

    ns_online_config cfg;
    SDL_zero(cfg);
    cfg.server_url = url;
    cfg.token = token;
    if (!ns_online_init(&cfg)) { CHECK(false, "le réseau démarre"); return; }

    /* Le billet arrive quand il arrive — mais pas indéfiniment. */
    ns_online_ticket t;
    bool got = false;
    for (int i = 0; i < 120 && !got; ++i) {
        got = ns_online_take_ticket("envol", "normal", &t);
        if (!got) SDL_Delay(50);
    }
    CHECK(got, "le serveur délivre un billet (statut : %s)", ns_online_status());
    if (!got) { ns_online_shutdown(); return; }

    CHECK(t.run_id[0] != '\0', "avec un identifiant (« %s »)", t.run_id);
    CHECK(t.secret_len == 32, "un secret de 32 octets (%zu)", t.secret_len);
    /*
     * La graine est tirée sur 63 bits : la voir tenir sur 24 serait le symptôme
     * exact d'une relecture en flottant. Une graine de moins de 2^40 est
     * possible mais improbable (une chance sur 8 millions) ; ce qui ne l'est
     * pas, c'est qu'elle soit un multiple d'une grande puissance de deux, ce
     * qu'un arrondi de `float` produirait à tous les coups.
     */
    CHECK(t.seed > 0, "une graine positive (%lld)", (long long)t.seed);
    CHECK((t.seed & 0xFFFFFFull) != 0 || t.seed < (1LL << 40),
          "et qui a gardé ses bits de poids faible (%lld)", (long long)t.seed);

    /* Une partie qu'un serveur acceptera : le barème d'Envol compte 1 par
     * tuyau, donc quatre passages valent 4. */
    ns_runlog *r = ns_runlog_create(32);
    if (!r) { CHECK(false, "journal"); ns_online_shutdown(); return; }
    ns_runlog_begin(r, "envol", "normal", t.seed, t.secret, t.secret_len);
    ns_runlog_set_run_id(r, t.run_id);
    for (int i = 0; i < 4; ++i) ns_runlog_event(r, 2000 + i * 1500, "pipe", 0);
    ns_runlog_event(r, 8000, "death", 0);
    ns_runlog_end(r, 8000, 4);

    CHECK(ns_runlog_enqueue(r), "la partie entre dans la file");
    ns_runlog_destroy(r);

    uint32_t sent = 0, failed = 0;
    ns_online_flush_queue();
    for (int i = 0; i < 120; ++i) {
        SDL_Delay(50);
        ns_online_stats(&sent, &failed);
        if (sent || failed) break;
    }
    CHECK(sent == 1 && failed == 0,
          "elle est ACCEPTÉE par le serveur (%u envoyée(s), %u refusée(s), %s)",
          sent, failed, ns_online_status());
    CHECK(ns_runlog_pending() == 0, "et la file est vide (%u)", ns_runlog_pending());

    /* Et le score ressort par la porte d'à côté : le classement mondial. */
    ns_online_request_board("envol", "normal");
    ns_online_board board;
    bool seen = false;
    for (int i = 0; i < 100 && !seen; ++i) {
        SDL_Delay(50);
        seen = ns_online_board_get("envol", "normal", &board);
    }
    CHECK(seen && board.count >= 1, "le score reparaît au classement mondial");
    if (seen && board.count >= 1) {
        CHECK(board.row[0].score == 4, "avec sa valeur recalculée (%u)", board.row[0].score);
    }

    ns_online_shutdown();
}

int main(int argc, char **argv)
{
    ns_log_set_level(NS_LOG_ERROR);

    test_parse_url();
    test_preseance_url();
    test_verrou_hors_ligne();
    test_serveur_mort();

    if (argc > 2)      test_partie_en_ligne(argv[1], argv[2]);
    else if (argc > 1) test_classement_en_ligne(argv[1]);
    else printf("  (pas d'URL fournie : le test en ligne est sauté)\n");

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
