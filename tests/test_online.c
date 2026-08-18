/*
 * test_online.c — le client HTTP et la couche de classement, sans deviner.
 *
 * Ce test ouvre de VRAIES sockets vers un serveur local lancé par CTest. C'est
 * délibéré : le reste du moteur se vérifie sans périphérique, mais un client
 * réseau qu'on ne fait jamais parler à personne est un client dont on ne sait
 * rien. Le serveur de test répond exactement ce que répond le vrai sur les deux
 * routes que le client lit.
 *
 * Ce qui est vérifié avant tout : **le verrou**. `--offline` et l'absence d'URL
 * doivent empêcher toute connexion, et ça se teste en demandant un classement
 * puis en constatant qu'aucune réponse n'arrive jamais.
 */
#include "ns_core.h"
#include "ns_http.h"
#include "ns_online.h"

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

int main(int argc, char **argv)
{
    ns_log_set_level(NS_LOG_ERROR);

    test_parse_url();
    test_verrou_hors_ligne();
    test_serveur_mort();

    if (argc > 1) test_classement_en_ligne(argv[1]);
    else printf("  (pas d'URL fournie : le test en ligne est sauté)\n");

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
