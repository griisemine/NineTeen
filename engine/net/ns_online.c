/* ns_online.c — voir ns_online.h pour la règle qui prime sur tout le reste. */
#include "ns_online.h"

#include "ns_core.h"
#include "ns_http.h"
#include "ns_json.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <string.h>

#define NS_ONLINE_MAX_BOARDS 8
#define NS_ONLINE_MAX_GAMES  16

typedef struct game_id { char slug[24]; int id; } game_id;

static struct {
    bool        enabled;
    bool        locked;
    char        url[512];
    char        token[256];
    char        status[128];

    SDL_Thread *thread;
    SDL_Mutex  *lock;
    SDL_AtomicInt quit;

    /* Une demande en attente, au plus. Un classement qu'on redemande avant
     * d'avoir la réponse ne sert à rien : on garde la dernière. */
    bool        want_board;
    char        want_game[24];
    char        want_diff[16];

    ns_online_board board[NS_ONLINE_MAX_BOARDS];
    uint32_t        boards;

    game_id     games[NS_ONLINE_MAX_GAMES];
    uint32_t    game_count;
    bool        games_known;

    uint32_t    sent, failed;
} g;

/* ==========================================================================
 * Le fil
 * ========================================================================== */

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_LockMutex(g.lock);
    SDL_vsnprintf(g.status, sizeof g.status, fmt, ap);
    SDL_UnlockMutex(g.lock);
    va_end(ap);
}

/* Résout les identifiants de jeux, une fois. Le classement les demande par
 * NUMÉRO ; le reste du moteur ne connaît que des noms. */
static void fetch_games(void)
{
    char url[640];
    SDL_snprintf(url, sizeof url, "%s/api/v1/games", g.url);

    ns_http_response r;
    if (!ns_http_request("GET", url, NULL, g.token[0] ? g.token : NULL, NULL, 0, 4000, &r)) {
        set_status("serveur injoignable");
        ns_http_response_free(&r);
        return;
    }
    if (r.status != 200 || !r.body) {
        set_status("serveur : %d", r.status);
        ns_http_response_free(&r);
        return;
    }

    /* Une arène jetable : le document ne survit pas à la fonction, et la rendre
     * d'un bloc évite de suivre chaque jeton. */
    ns_arena arena;
    if (!ns_arena_init(&arena, 128u * 1024u, "json réseau")) {
        ns_http_response_free(&r);
        return;
    }
    ns_json doc;
    if (ns_json_parse(&doc, r.body, r.length, &arena)) {
        const ns_json_value *root = ns_json_root(&doc);
        const ns_json_value *games = ns_json_get(&doc, root, "games");
        const int n = ns_json_array_count(&doc, games);
        SDL_LockMutex(g.lock);
        g.game_count = 0;
        for (int i = 0; i < n && g.game_count < NS_ONLINE_MAX_GAMES; ++i) {
            const ns_json_value *e = ns_json_at(&doc, games, i);
            char slug[24] = { 0 };
            ns_json_get_string(&doc, e, "slug", slug, sizeof slug);
            if (!slug[0]) continue;
            game_id *gi = &g.games[g.game_count++];
            SDL_snprintf(gi->slug, sizeof gi->slug, "%s", slug);
            gi->id = (int)ns_json_get_float(&doc, e, "id", 0.0f);
        }
        g.games_known = g.game_count > 0;
        SDL_UnlockMutex(g.lock);
    }
    ns_arena_free(&arena);
    ns_http_response_free(&r);
}

static int game_id_for(const char *slug)
{
    for (uint32_t i = 0; i < g.game_count; ++i) {
        if (SDL_strcasecmp(g.games[i].slug, slug) == 0) return g.games[i].id;
    }
    return -1;
}

static ns_online_board *board_slot(const char *game, const char *diff)
{
    for (uint32_t i = 0; i < g.boards; ++i) {
        if (SDL_strcasecmp(g.board[i].game, game) == 0
            && SDL_strcasecmp(g.board[i].difficulty, diff) == 0) return &g.board[i];
    }
    if (g.boards >= NS_ONLINE_MAX_BOARDS) return NULL;
    ns_online_board *b = &g.board[g.boards++];
    SDL_zerop(b);
    SDL_snprintf(b->game, sizeof b->game, "%s", game);
    SDL_snprintf(b->difficulty, sizeof b->difficulty, "%s", diff);
    return b;
}

static void fetch_board(const char *game, const char *diff)
{
    if (!g.games_known) fetch_games();

    SDL_LockMutex(g.lock);
    const int id = game_id_for(game);
    SDL_UnlockMutex(g.lock);
    if (id < 0) {
        set_status("« %s » inconnu du serveur", game);
        return;
    }

    char url[700];
    SDL_snprintf(url, sizeof url, "%s/api/v1/leaderboard?game=%d&limit=%d",
                 g.url, id, NS_ONLINE_MAX_ROWS);

    ns_http_response r;
    if (!ns_http_request("GET", url, NULL, g.token[0] ? g.token : NULL, NULL, 0, 4000, &r)) {
        set_status("serveur injoignable");
        ns_http_response_free(&r);
        return;
    }
    if (r.status != 200 || !r.body) {
        set_status("classement : %d", r.status);
        ns_http_response_free(&r);
        return;
    }

    ns_arena arena;
    if (!ns_arena_init(&arena, 256u * 1024u, "json classement")) {
        ns_http_response_free(&r);
        return;
    }
    ns_json doc;
    if (ns_json_parse(&doc, r.body, r.length, &arena)) {
        const ns_json_value *root = ns_json_root(&doc);
        const ns_json_value *entries = ns_json_get(&doc, root, "entries");
        const int n = ns_json_array_count(&doc, entries);

        SDL_LockMutex(g.lock);
        ns_online_board *b = board_slot(game, diff);
        if (b) {
            b->count = 0;
            for (int i = 0; i < n && b->count < NS_ONLINE_MAX_ROWS; ++i) {
                const ns_json_value *e = ns_json_at(&doc, entries, i);
                ns_online_row *row = &b->row[b->count];
                row->name[0] = '\0';
                ns_json_get_string(&doc, e, "username", row->name, sizeof row->name);
                if (!row->name[0]) continue;
                const float sc = ns_json_get_float(&doc, e, "score", 0.0f);
                row->score = (sc > 0.0f) ? (uint32_t)sc : 0u;
                b->count++;
            }
            b->fresh = true;
        }
        SDL_UnlockMutex(g.lock);
        set_status("classement mondial à jour");
    }
    ns_arena_free(&arena);
    ns_http_response_free(&r);
}

static int SDLCALL worker(void *unused)
{
    (void)unused;
    while (!SDL_GetAtomicInt(&g.quit)) {
        bool work = false;
        char game[24], diff[16];

        SDL_LockMutex(g.lock);
        if (g.want_board) {
            g.want_board = false;
            SDL_snprintf(game, sizeof game, "%s", g.want_game);
            SDL_snprintf(diff, sizeof diff, "%s", g.want_diff);
            work = true;
        }
        SDL_UnlockMutex(g.lock);

        if (work) fetch_board(game, diff);
        else SDL_Delay(60);
    }
    return 0;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

bool ns_online_init(const ns_online_config *cfg)
{
    SDL_zero(g);
    SDL_snprintf(g.status, sizeof g.status, "hors ligne");

    if (!cfg) return false;
    g.locked = cfg->locked;

    if (cfg->locked) {
        /*
         * `--offline` est un VERROU, pas un repli : on ne démarre pas le fil,
         * donc aucune socket ne peut être ouverte, même si une URL est
         * configurée. C'est ce qui rend la garantie vérifiable et pas seulement
         * probable.
         */
        SDL_snprintf(g.status, sizeof g.status, "hors ligne (verrouillé)");
        NS_INFO("réseau : verrouillé par --offline, aucune connexion ne sera tentée");
        return false;
    }
    if (!cfg->server_url || !cfg->server_url[0]) {
        NS_INFO("réseau : aucun serveur configuré, le classement restera local");
        return false;
    }

    SDL_snprintf(g.url, sizeof g.url, "%s", cfg->server_url);
    /* Une barre oblique finale doublerait les séparateurs des chemins d'API. */
    size_t n = SDL_strlen(g.url);
    while (n > 0 && g.url[n - 1] == '/') g.url[--n] = '\0';

    if (cfg->token && cfg->token[0]) {
        SDL_snprintf(g.token, sizeof g.token, "%s", cfg->token);
    }

    g.lock = SDL_CreateMutex();
    if (!g.lock) return false;

    SDL_SetAtomicInt(&g.quit, 0);
    g.thread = SDL_CreateThread(worker, "nineteen-net", NULL);
    if (!g.thread) {
        SDL_DestroyMutex(g.lock);
        g.lock = NULL;
        NS_WARN("réseau : fil indisponible, le classement restera local");
        return false;
    }

    g.enabled = true;
    SDL_snprintf(g.status, sizeof g.status, "connexion...");
    NS_INFO("réseau : actif sur « %s »%s", g.url,
            g.token[0] ? " (avec jeton)" : " (lecture seule)");
    return true;
}

void ns_online_shutdown(void)
{
    if (g.thread) {
        SDL_SetAtomicInt(&g.quit, 1);
        SDL_WaitThread(g.thread, NULL);
        g.thread = NULL;
    }
    if (g.lock) { SDL_DestroyMutex(g.lock); g.lock = NULL; }
    g.enabled = false;
}

bool ns_online_enabled(void) { return g.enabled; }

const char *ns_online_status(void) { return g.status; }

void ns_online_request_board(const char *game, const char *difficulty)
{
    if (!g.enabled || !game || !difficulty) return;
    SDL_LockMutex(g.lock);
    SDL_snprintf(g.want_game, sizeof g.want_game, "%s", game);
    SDL_snprintf(g.want_diff, sizeof g.want_diff, "%s", difficulty);
    g.want_board = true;
    SDL_UnlockMutex(g.lock);
}

bool ns_online_board_get(const char *game, const char *difficulty, ns_online_board *out)
{
    if (!g.enabled || !out || !game || !difficulty) return false;
    bool found = false;
    SDL_LockMutex(g.lock);
    for (uint32_t i = 0; i < g.boards; ++i) {
        if (SDL_strcasecmp(g.board[i].game, game) == 0
            && SDL_strcasecmp(g.board[i].difficulty, difficulty) == 0
            && g.board[i].fresh) {
            *out = g.board[i];
            found = true;
            break;
        }
    }
    SDL_UnlockMutex(g.lock);
    return found;
}

void ns_online_flush_queue(void)
{
    /*
     * Rien à envoyer, et ce n'est PAS une lacune : c'est la conséquence directe
     * de l'anti-triche de M6.
     *
     * Le serveur tire la graine ET le secret d'une partie AVANT qu'elle soit
     * jouée ; le score n'est plus une valeur que le client annonce mais une
     * conséquence que le serveur recalcule à partir d'un journal scellé avec CE
     * secret. Une partie jouée hors ligne n'a ni l'un ni l'autre : elle n'est
     * donc pas soumettable, et la rendre soumettable reviendrait à accepter des
     * scores non vérifiables — c'est-à-dire à revenir exactement à la V1, où le
     * client annonçait son score et le serveur le croyait.
     *
     * Les parties hors ligne restent donc locales, définitivement, et le
     * classement local les affiche comme telles. C'est un choix de conception,
     * pas un travail qui reste à faire.
     */
    if (!g.enabled) return;
}

void ns_online_stats(uint32_t *sent, uint32_t *failed)
{
    if (sent) *sent = g.sent;
    if (failed) *failed = g.failed;
}
