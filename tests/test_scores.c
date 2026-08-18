/*
 * test_scores.c — le classement local, et le journal scellé.
 *
 * Deux sujets dans un fichier parce qu'ils forment une seule chaîne : on joue,
 * le score entre dans le classement local, et la partie est scellée pour un
 * serveur qui la revérifiera. Les deux tournent sans réseau, sans GPU et sans
 * base de données.
 *
 * Ce que ce test attrape vraiment
 * -------------------------------
 * Pour le classement : un fichier corrompu qui ferait perdre tous les scores,
 * une insertion qui déclasse un ancien record à score égal, un tableau qui
 * déborde.
 *
 * Pour le journal : **une divergence d'un octet avec le serveur**. Le format
 * canonique et le sceau sont écrits deux fois — en Go dans
 * `server/internal/runs/runs.go` et en C ici — et une différence invaliderait
 * TOUTES les parties, avec pour seul symptôme un « sceau invalide » du côté
 * serveur. Les vecteurs ci-dessous sont donc produits par le code Go lui-même,
 * avec son propre contexte de test, et recopiés ici.
 */
#include "ns_runlog.h"
#include "ns_scores.h"
#include "ns_core.h"

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

static char g_tmp_scores[512];
static char g_tmp_queue[512];

/* ========================================================================== */
/* SHA-256 et HMAC : les vecteurs des RFC                                     */
/* ========================================================================== */

static void hex(const uint8_t *b, size_t n, char *out)
{
    static const char *H = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) { out[i * 2] = H[b[i] >> 4]; out[i * 2 + 1] = H[b[i] & 15]; }
    out[n * 2] = '\0';
}

static void test_sha256_vectors(void)
{
    /*
     * FIPS 180-4, les deux vecteurs canoniques, plus le cas qui attrape la faute
     * de remplissage la plus commune : un message de 55 octets tient dans le
     * bloc avec sa longueur, un de 56 n'y tient pas et en demande un second.
     */
    struct { const char *in; const char *want; } v[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
        { "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
        { "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
    };
    for (size_t i = 0; i < SDL_arraysize(v); ++i) {
        uint8_t d[32]; char got[65];
        ns_sha256(v[i].in, strlen(v[i].in), d);
        hex(d, 32, got);
        CHECK(strcmp(got, v[i].want) == 0, "SHA-256 de %zu octets : %s", strlen(v[i].in), got);
    }
}

static void test_hmac_vectors(void)
{
    /* RFC 4231, cas 1, 2 et 4 — celui-ci a une clé de 131 octets, qui dépasse le
     * bloc et doit donc être condensée avant usage. C'est la faute qui ne se voit
     * jamais en test et toujours en production, parce que les clés courtes
     * marchent quand même. */
    const uint8_t k1[20] = { 0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
                             0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b };
    uint8_t d[32]; char got[65];

    ns_hmac_sha256(k1, sizeof k1, "Hi There", 8, d); hex(d, 32, got);
    CHECK(strcmp(got, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") == 0,
          "HMAC cas 1 : %s", got);

    ns_hmac_sha256((const uint8_t *)"Jefe", 4, "what do ya want for nothing?", 28, d);
    hex(d, 32, got);
    CHECK(strcmp(got, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") == 0,
          "HMAC cas 2 : %s", got);

    uint8_t k3[131];
    memset(k3, 0xaa, sizeof k3);
    ns_hmac_sha256(k3, sizeof k3, "Test Using Larger Than Block-Size Key - Hash Key First", 54, d);
    hex(d, 32, got);
    CHECK(strcmp(got, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") == 0,
          "HMAC cas 4 (clé de 131 octets) : %s", got);
}

static void test_base64_raw(void)
{
    /* Sans remplissage : `base64.RawStdEncoding`. Avec les « = » finaux le sceau
     * ne correspond plus, et le serveur ne dit pas pourquoi. */
    struct { const char *in; const char *want; } v[] = {
        { "",       ""       },
        { "f",      "Zg"     },
        { "fo",     "Zm8"    },
        { "foo",    "Zm9v"   },
        { "foob",   "Zm9vYg" },
        { "fooba",  "Zm9vYmE" },
        { "foobar", "Zm9vYmFy" },
    };
    for (size_t i = 0; i < SDL_arraysize(v); ++i) {
        char out[32];
        ns_base64_raw((const uint8_t *)v[i].in, strlen(v[i].in), out, sizeof out);
        CHECK(strcmp(out, v[i].want) == 0, "base64 « %s » -> « %s »", v[i].in, out);
    }
}

/* ========================================================================== */
/* Le format canonique, confronté au serveur                                  */
/* ========================================================================== */

static void test_canonical_matches_server(void)
{
    /*
     * Vecteur produit par `server/internal/runs`, avec son propre contexte de
     * test : graine 1234567890, secret « secret-de-partie-32-octets-exact »,
     * quatre événements de Tetris pour 701 points.
     */
    static const char *WANT_PAYLOAD =
        "v1|1234567890|30000|701|4|2000:drop:0|4000:lines:1|9000:lines:2|15000:tetris:0";
    static const char *WANT_SEAL = "Nk5QOOcz3saq5cxQ/CKoofm+6ndc4iZozT5+Pqv4+IQ";
    static const char *SECRET = "secret-de-partie-32-octets-exact";

    ns_runlog *r = ns_runlog_create(64);
    CHECK(r != NULL, "le journal se crée");
    if (!r) return;

    ns_runlog_begin(r, "tetris", "hard", 1234567890,
                    (const uint8_t *)SECRET, strlen(SECRET));
    ns_runlog_event(r,  2000, "drop",   0);
    ns_runlog_event(r,  4000, "lines",  1);
    ns_runlog_event(r,  9000, "lines",  2);
    ns_runlog_event(r, 15000, "tetris", 0);
    ns_runlog_end(r, 30000, 701);

    char payload[512];
    const size_t n = ns_runlog_canonical(r, payload, sizeof payload);
    printf("  charge : %s\n", payload);
    CHECK(n == strlen(WANT_PAYLOAD), "longueur %zu, attendue %zu", n, strlen(WANT_PAYLOAD));
    CHECK(strcmp(payload, WANT_PAYLOAD) == 0, "la charge canonique est celle du serveur");

    char seal[64];
    CHECK(ns_runlog_seal(r, seal, sizeof seal), "le sceau se calcule");
    printf("  sceau  : %s\n", seal);
    CHECK(strcmp(seal, WANT_SEAL) == 0, "le sceau est celui du serveur (%s)", seal);

    /* La longueur voulue est rendue même sans tampon : c'est ce qui permet de
     * dimensionner en un appel plutôt que de deviner. */
    CHECK(ns_runlog_canonical(r, NULL, 0) == n, "la longueur se demande à vide");

    ns_runlog_destroy(r);
}

static void test_canonical_empty(void)
{
    /* Une partie sans un seul événement doit produire la même chaîne des deux
     * côtés, elle aussi — c'est le cas d'une partie perdue immédiatement. */
    ns_runlog *r = ns_runlog_create(8);
    if (!r) { CHECK(false, "création"); return; }
    ns_runlog_begin(r, "flappy", "normal", 7, (const uint8_t *)"k", 1);
    ns_runlog_end(r, 0, 0);

    char payload[64], seal[64];
    ns_runlog_canonical(r, payload, sizeof payload);
    CHECK(strcmp(payload, "v1|7|0|0|0") == 0, "partie vide : « %s »", payload);
    CHECK(ns_runlog_seal(r, seal, sizeof seal), "sceau");
    CHECK(strcmp(seal, "oW+iD5YE/ssnmo4YU//r7NzQKw+HqelO3b162K5ARQM") == 0,
          "sceau de la partie vide (%s)", seal);
    ns_runlog_destroy(r);
}

static void test_unauthenticated_is_not_queued(void)
{
    /*
     * Hors ligne, il n'y a pas de secret : la partie ne peut pas être scellée
     * valablement. On refuse de la mettre en file plutôt que d'y écrire ce que
     * le serveur rejettera — et le joueur garde quand même son score, par le
     * classement local.
     */
    ns_runlog *r = ns_runlog_create(8);
    if (!r) { CHECK(false, "création"); return; }
    ns_runlog_begin(r, "flappy", "normal", 42, NULL, 0);
    ns_runlog_event(r, 100, "score", 1);
    ns_runlog_end(r, 5000, 1);

    CHECK(!ns_runlog_authenticated(r), "une partie hors ligne n'est pas authentifiée");
    char seal[64];
    CHECK(!ns_runlog_seal(r, seal, sizeof seal), "et elle ne peut pas être scellée");
    CHECK(!ns_runlog_enqueue(r), "et elle n'entre pas dans la file");
    ns_runlog_destroy(r);
}

static void test_queue_writes_one_file(void)
{
    ns_runlog_set_queue_dir(g_tmp_queue);
    const uint32_t before = ns_runlog_pending();

    ns_runlog *r = ns_runlog_create(16);
    if (!r) { CHECK(false, "création"); return; }
    ns_runlog_begin(r, "flappy", "hard", 99, (const uint8_t *)"secret", 6);
    ns_runlog_event(r, 1200, "score", 1);
    ns_runlog_event(r, 3400, "death", 0);
    ns_runlog_end(r, 4000, 1);

    CHECK(ns_runlog_enqueue(r), "la partie authentifiée entre dans la file");
    CHECK(ns_runlog_pending() == before + 1, "un fichier de plus (%u)", ns_runlog_pending());
    ns_runlog_destroy(r);
}

static void test_event_overflow(void)
{
    /* Le journal plein ne doit ni déborder, ni compter au-delà de sa capacité —
     * `ns_runlog_end` remet le compte d'aplomb après le message d'alerte. */
    ns_runlog *r = ns_runlog_create(4);
    if (!r) { CHECK(false, "création"); return; }
    ns_runlog_begin(r, "flappy", "normal", 1, (const uint8_t *)"k", 1);
    for (int i = 0; i < 100; ++i) ns_runlog_event(r, i * 10, "flap", 0);
    ns_runlog_end(r, 1000, 0);
    CHECK(ns_runlog_event_count(r) == 4, "le journal s'arrête à sa capacité (%u)",
          ns_runlog_event_count(r));

    char payload[256];
    ns_runlog_canonical(r, payload, sizeof payload);
    CHECK(strstr(payload, "|4|") != NULL, "et l'annonce dans la charge : %s", payload);
    ns_runlog_destroy(r);
}

/* ========================================================================== */
/* Le classement local                                                        */
/* ========================================================================== */

static void test_scores_basics(void)
{
    ns_scores_set_path(g_tmp_scores);
    ns_scores_load();

    CHECK(ns_scores_best("flappy", "hard") == 0, "un classement vierge n'a pas de record");

    CHECK(ns_scores_record("flappy", "hard", 12, 30000, "Nine") == 1, "premier : rang 1");
    CHECK(ns_scores_record("flappy", "hard", 40, 90000, "Nine") == 1, "meilleur : rang 1");
    CHECK(ns_scores_record("flappy", "hard", 25, 60000, NULL)   == 2, "entre les deux : rang 2");
    CHECK(ns_scores_best("flappy", "hard") == 40, "le record suit");

    /*
     * À score égal, l'ancien reste devant. Un tri naïf ferait remonter la
     * nouvelle entrée et déclasserait un record que le joueur n'a pas battu.
     */
    CHECK(ns_scores_record("flappy", "hard", 40, 88000, "Autre") == 2,
          "à score égal, le nouveau passe DERRIÈRE");

    /* Les difficultés sont deux classements distincts. */
    CHECK(ns_scores_record("flappy", "normal", 5, 10000, NULL) == 1, "autre difficulté");
    CHECK(ns_scores_best("flappy", "normal") == 5, "et son propre record");
    CHECK(ns_scores_best("flappy", "hard") == 40, "sans toucher à l'autre");

    /* Une difficulté absente vaut « normal » : sinon la même borne écrirait dans
     * deux tableaux selon que le champ est renseigné. */
    CHECK(ns_scores_best("flappy", NULL) == 5, "difficulté absente = normal");
    CHECK(ns_scores_best("flappy", "") == 5, "difficulté vide aussi");
}

static void test_scores_full_board(void)
{
    ns_scores_set_path(g_tmp_scores);
    ns_scores_load();
    for (int i = 0; i < NS_SCORE_SLOTS + 5; ++i) {
        ns_scores_record("snake", "normal", (uint32_t)(100 + i * 10), 1000, "x");
    }
    const ns_score_board *b = ns_scores_board("snake", "normal");
    CHECK(b != NULL, "le tableau existe");
    if (!b) return;
    CHECK(b->count == NS_SCORE_SLOTS, "il garde %d entrées (%u)", NS_SCORE_SLOTS, b->count);
    CHECK(b->entry[0].score == 240, "la meilleure est en tête (%u)", b->entry[0].score);
    for (uint32_t i = 1; i < b->count; ++i) {
        CHECK(b->entry[i - 1].score >= b->entry[i].score, "l'ordre tient à l'entrée %u", i);
    }
    /* Un score trop faible pour entrer rend 0, et ne déloge personne. */
    CHECK(ns_scores_record("snake", "normal", 1, 1000, "x") == 0, "trop faible : rang 0");
    CHECK(b->count == NS_SCORE_SLOTS, "et le tableau ne bouge pas");
}

static void test_scores_round_trip(void)
{
    ns_scores_set_path(g_tmp_scores);
    ns_scores_load();
    ns_scores_clear();
    ns_scores_record("tetris", "normal", 7777, 123456, "Jo|hn\nDoe");
    ns_scores_record("tetris", "normal", 100, 2000, "");
    CHECK(ns_scores_save(), "l'écriture réussit");

    /* Relecture depuis zéro : c'est le seul contrôle qui prouve que le format
     * écrit est celui qu'on sait relire. */
    ns_scores_clear();
    ns_scores_load();
    const ns_score_board *b = ns_scores_board("tetris", "normal");
    CHECK(b != NULL && b->count == 2, "deux entrées relues");
    if (!b || b->count < 2) return;
    CHECK(b->entry[0].score == 7777, "le score survit (%u)", b->entry[0].score);
    CHECK(b->entry[0].duration_ms == 123456, "la durée aussi");
    /* Le séparateur et le saut de ligne sont retirés du nom : sans ça, un joueur
     * qui s'appelle « a|b » casserait la ligne suivante du fichier. */
    CHECK(strchr(b->entry[0].name, '|') == NULL, "le nom ne contient pas de séparateur");
    CHECK(strchr(b->entry[0].name, '\n') == NULL, "ni de saut de ligne");
    printf("  nom relu : « %s »\n", b->entry[0].name);
}

static void test_scores_survives_corruption(void)
{
    /*
     * Un fichier abîmé ne doit pas coûter TOUT le classement. C'est la raison du
     * format ligne à ligne : une ligne illisible est sautée, les autres passent.
     */
    ns_scores_set_path(g_tmp_scores);
    SDL_IOStream *io = SDL_IOFromFile(g_tmp_scores, "w");
    CHECK(io != NULL, "fichier de test ouvert");
    if (!io) return;
    static const char *junk =
        "v1\n"
        "flappy/hard|500|1000|0|Ok\n"
        "ligne completement cassee sans separateurs\n"
        "flappy/hard|300|abc|0|Aussi\n"
        "flappy/hard|200|1000|0|Bien\n";
    SDL_WriteIO(io, junk, strlen(junk));
    SDL_CloseIO(io);

    ns_scores_clear();
    ns_scores_load();
    const ns_score_board *b = ns_scores_board("flappy", "hard");
    CHECK(b != NULL, "le tableau est relu malgré les lignes cassées");
    if (!b) return;
    CHECK(b->count >= 2, "les lignes valides survivent (%u)", b->count);
    CHECK(b->entry[0].score == 500, "et la meilleure est correcte (%u)", b->entry[0].score);
}

/* ========================================================================== */

int main(void)
{
    ns_log_set_level(NS_LOG_ERROR);   /* les WARN de corruption sont attendus */

    const char *base = SDL_getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    SDL_snprintf(g_tmp_scores, sizeof g_tmp_scores, "%s/ns_test_scores.txt", base);
    SDL_snprintf(g_tmp_queue, sizeof g_tmp_queue, "%s/ns_test_runs", base);
    (void)SDL_RemovePath(g_tmp_scores);

    test_sha256_vectors();
    test_hmac_vectors();
    test_base64_raw();
    test_canonical_matches_server();
    test_canonical_empty();
    test_unauthenticated_is_not_queued();
    test_queue_writes_one_file();
    test_event_overflow();
    test_scores_basics();
    test_scores_full_board();
    test_scores_round_trip();
    test_scores_survives_corruption();

    (void)SDL_RemovePath(g_tmp_scores);

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
