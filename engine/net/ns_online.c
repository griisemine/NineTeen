/* ns_online.c — voir ns_online.h pour la règle qui prime sur tout le reste. */
#include "ns_online.h"

#include "ns_config.h"
#include "ns_core.h"
#include "ns_http.h"
#include "ns_json.h"
#include "ns_runlog.h"

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

    /*
     * UN billet d'avance, au plus, et le jeu pour lequel il a été tiré. En
     * garder plusieurs ne servirait à rien — on ne joue qu'une partie à la fois
     * — et en garder zéro obligerait à attendre le réseau au moment où le
     * joueur appuie sur le bouton.
     */
    bool             ticket_ready;
    char             ticket_game[24];
    char             ticket_diff[16];
    ns_online_ticket ticket;
    char             want_ticket[24];   /* un billet à tirer, ou vide */
    char             want_ticket_diff[16];
    /*
     * Le dernier créneau pour lequel on a demandé un billet. `want_ticket` est
     * CONSOMMÉ par le fil ; celui-ci ne l'est pas, et c'est ce qui permet de
     * redemander un billet sans que l'appelant ait à répéter le nom du jeu —
     * notamment quand `ns_online_set_duel` jette un billet devenu faux.
     */
    char             last_ticket[24];
    char             last_ticket_diff[16];
    /*
     * L'heure avant laquelle on ne REDEMANDE pas de billet après un échec.
     *
     * Sans elle, la demande restait armée tant qu'aucun billet n'arrivait : un
     * serveur qui répond 401 — le cas normal quand on n'a pas de jeton de
     * session — faisait repartir le fil aussitôt, sans attente, à pleine
     * vitesse. C'est très exactement la boucle de la V1
     * (`while (updateMeilleureScoreStruct(...) == EXIT_FAILURE);`, room.c:1302)
     * que l'audit reproche, et je venais de la réécrire. Un échec coûte
     * maintenant quinze secondes de silence.
     */
    uint64_t         ticket_retry_at_ms;

    bool        want_flush;

    /*
     * Le fantôme que le prochain billet doit affronter, ou vide.
     *
     * Un seul, parce qu'on n'affronte qu'un adversaire à la fois. Il est envoyé
     * au serveur dans le corps de `POST /api/v1/runs`, qui reprend alors la
     * graine de CETTE partie-là au lieu d'en tirer une neuve : c'est la seule
     * chose qui distingue un duel de deux parties sans rapport.
     */
    char        duel_ghost[64];

    uint32_t    sent, failed;
} g;

/*
 * Décodage base64 « brut » — sans remplissage, comme `base64.RawStdEncoding`
 * côté Go. Le secret voyage sous cette forme et doit revenir aux 32 octets
 * exacts : un décodeur qui exigerait les `=` finaux échouerait sur chaque
 * billet, et le seul symptôme serait des parties refusées pour sceau invalide.
 */
static size_t base64_decode(const char *in, uint8_t *out, size_t cap)
{
    static const char *ALPHA =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (const char *p = in; *p; ++p) {
        if (*p == '=') break;
        const char *q = SDL_strchr(ALPHA, *p);
        if (!q) continue;               /* espaces, retours à la ligne */
        acc = (acc << 6) | (uint32_t)(q - ALPHA);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n < cap) out[n++] = (uint8_t)((acc >> bits) & 0xFFu);
        }
    }
    return n;
}

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

/*
 * Le NOM que le serveur donne à ce que le client appelle « flappy » + « hard ».
 *
 * Les deux moitiés n'ont jamais parlé la même langue, et personne ne s'en était
 * aperçu : le moteur porte un identifiant de jeu (`flappy`) et une difficulté
 * (`normal` / `hard`) séparés — c'est ce que déclare chaque borne — tandis que
 * la base du serveur en fait un seul créneau par tableau : `flappy-easy`,
 * `flappy-hard`, et `pacman` tout court là où il n'y a qu'un mode. Le client
 * cherchait « flappy », qui n'existe nulle part côté serveur : `game_id_for`
 * rendait −1, le classement mondial était donc introuvable pour les huit jeux,
 * et l'ouverture de partie répondait « jeu inconnu ». Le test qui existait ne
 * l'a pas vu parce qu'il interroge un bouchon dont le jeu s'appelle « snake ».
 *
 * On résout donc contre la liste que le serveur a RÉELLEMENT renvoyée, dans cet
 * ordre : le créneau de la difficulté demandée, puis le nom nu. Rien n'est
 * codé en dur au-delà du suffixe, et un jeu que le serveur ne connaît pas
 * reste un jeu inconnu — pas un identifiant inventé.
 *
 * Appelée avec `g.lock` tenu.
 */
static int resolve_game(const char *game, const char *diff, char *out, size_t cap)
{
    if (!game || !game[0]) return -1;
    const bool hard = diff && SDL_strcasecmp(diff, "hard") == 0;

    char candidate[24];
    SDL_snprintf(candidate, sizeof candidate, "%s-%s", game, hard ? "hard" : "easy");
    int id = game_id_for(candidate);
    if (id < 0) {
        SDL_snprintf(candidate, sizeof candidate, "%s", game);
        id = game_id_for(candidate);
    }
    if (id >= 0 && out && cap) SDL_snprintf(out, cap, "%s", candidate);
    return id;
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
    const int id = resolve_game(game, diff, NULL, 0);
    SDL_UnlockMutex(g.lock);
    if (id < 0) {
        set_status("« %s/%s » inconnu du serveur", game, diff ? diff : "normal");
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

/*
 * Tire un billet : `POST /api/v1/runs`, qui rend un identifiant, une graine et
 * un secret. Sans jeton de session, le serveur répond 401 et l'on reste hors
 * ligne — ce qui est le comportement voulu, pas une panne : **le jeu ne demande
 * jamais de compte pour jouer**.
 */
#define NS_ONLINE_TICKET_RETRY_MS 15000u

/* Un échec met la demande en sommeil : voir `ticket_retry_at_ms`. */
static void ticket_backoff(void)
{
    SDL_LockMutex(g.lock);
    g.ticket_retry_at_ms = SDL_GetTicks() + NS_ONLINE_TICKET_RETRY_MS;
    SDL_UnlockMutex(g.lock);
}

static void fetch_ticket(const char *game, const char *diff)
{
    if (!g.token[0]) {
        set_status("classement en lecture seule (pas de jeton)");
        ticket_backoff();
        return;
    }

    /* Le serveur ouvre une partie sur SON créneau : « flappy » + « hard » y
     * s'appelle « flappy-hard ». Sans cette traduction il répondait « jeu
     * inconnu », donc 400, pour les huit jeux. */
    if (!g.games_known) fetch_games();
    char slug[24] = { 0 };
    SDL_LockMutex(g.lock);
    const int known = resolve_game(game, diff, slug, sizeof slug);
    SDL_UnlockMutex(g.lock);
    if (known < 0) {
        set_status("« %s/%s » inconnu du serveur", game, diff ? diff : "normal");
        ticket_backoff();
        return;
    }

    char url[640];
    SDL_snprintf(url, sizeof url, "%s/api/v1/runs", g.url);

    /* Le fantôme, s'il y en a un : le serveur reprend alors SA graine. Le champ
     * est omis quand il n'y en a pas — le décodeur du serveur refuse les champs
     * inconnus, mais un champ absent reste un champ absent. */
    char ghost[64];
    SDL_LockMutex(g.lock);
    SDL_snprintf(ghost, sizeof ghost, "%s", g.duel_ghost);
    SDL_UnlockMutex(g.lock);

    char body[192];
    int len;
    if (ghost[0]) {
        len = SDL_snprintf(body, sizeof body, "{\"game\":\"%s\",\"ghost\":\"%s\"}",
                           slug, ghost);
    } else {
        len = SDL_snprintf(body, sizeof body, "{\"game\":\"%s\"}", slug);
    }

    ns_http_response r;
    if (!ns_http_request("POST", url, "application/json", g.token,
                         body, (size_t)len, 4000, &r)) {
        set_status("serveur injoignable");
        ns_http_response_free(&r);
        ticket_backoff();
        return;
    }
    if ((r.status != 200 && r.status != 201) || !r.body) {
        set_status("ouverture de partie : %d", r.status);
        ns_http_response_free(&r);
        ticket_backoff();
        return;
    }

    ns_arena arena;
    if (!ns_arena_init(&arena, 64u * 1024u, "json billet")) {
        ns_http_response_free(&r);
        ticket_backoff();
        return;
    }
    bool got = false;
    ns_json doc;
    if (ns_json_parse(&doc, r.body, r.length, &arena)) {
        const ns_json_value *root = ns_json_root(&doc);
        char run_id[64] = { 0 }, secret[128] = { 0 };
        ns_json_get_string(&doc, root, "runId", run_id, sizeof run_id);
        ns_json_get_string(&doc, root, "secret", secret, sizeof secret);
        /* La graine fait 63 bits et `ns_json_get_float` n'en garde que 24 : elle
         * se lit en entier, sinon on jouerait une AUTRE partie que celle que le
         * serveur vient d'ouvrir, et chaque envoi serait refusé pour sceau
         * invalide sans qu'une seule ligne dise pourquoi. */
        const int64_t seed = ns_json_get_i64(&doc, root, "seed", 0);

        if (run_id[0] && secret[0]) {
            SDL_LockMutex(g.lock);
            SDL_zero(g.ticket);
            SDL_snprintf(g.ticket.run_id, sizeof g.ticket.run_id, "%s", run_id);
            g.ticket.seed = seed;
            g.ticket.secret_len = base64_decode(secret, g.ticket.secret,
                                                sizeof g.ticket.secret);
            SDL_snprintf(g.ticket_game, sizeof g.ticket_game, "%s", game);
            SDL_snprintf(g.ticket_diff, sizeof g.ticket_diff, "%s",
                         diff && diff[0] ? diff : "normal");
            g.ticket_ready = g.ticket.secret_len > 0;
            got = g.ticket_ready;
            SDL_UnlockMutex(g.lock);
            if (got) set_status("partie ouverte sur le serveur");
        }
    }
    ns_arena_free(&arena);
    ns_http_response_free(&r);
    /* Une réponse 200 dont on ne tire pas de billet exploitable est un échec
     * comme un autre : sans ce repli, elle relancerait la demande en boucle. */
    if (!got) ticket_backoff();
}

/*
 * Vide la file : un fichier, un `POST /api/v1/runs/{id}/submit`.
 *
 * Ce qui se supprime et ce qui reste, et c'est la seule décision qui compte
 * ici : un 2xx efface le fichier, un 4xx aussi — le serveur a tranché, le
 * renvoyer donnerait le même refus jusqu'à la fin des temps. Un 5xx ou une
 * absence de réponse le GARDE : c'est une panne, pas un verdict.
 */
static void flush_queue_now(void)
{
    if (!g.token[0]) return;

    int count = 0;
    char **files = SDL_GlobDirectory(ns_runlog_queue_dir(), "*.json", 0, &count);
    if (!files) return;

    for (int i = 0; i < count; ++i) {
        char path[1024];
        SDL_snprintf(path, sizeof path, "%s/%s", ns_runlog_queue_dir(), files[i]);

        char run_id[64];
        char *body = NULL;
        size_t body_len = 0;
        if (!ns_runlog_queue_read(path, run_id, sizeof run_id, &body, &body_len)) {
            /* Illisible ou sans identifiant : rien ne pourra jamais l'envoyer. */
            NS_WARN("file d'attente : « %s » inexploitable, retiré", path);
            SDL_RemovePath(path);
            continue;
        }

        char url[768];
        SDL_snprintf(url, sizeof url, "%s/api/v1/runs/%s/submit", g.url, run_id);

        ns_http_response r;
        const bool reached = ns_http_request("POST", url, "application/json", g.token,
                                             body, body_len, 6000, &r);
        SDL_free(body);

        if (!reached || r.status >= 500 || r.status == 0) {
            ns_http_response_free(&r);
            set_status("envoi différé (serveur indisponible)");
            break;   /* inutile d'insister sur les suivants */
        }
        if (r.status >= 200 && r.status < 300) {
            g.sent++;
            set_status("partie envoyée");
        } else {
            g.failed++;
            NS_WARN("partie refusée par le serveur (%d) : %s", r.status, path);
            set_status("partie refusée (%d)", r.status);
        }
        ns_http_response_free(&r);
        SDL_RemovePath(path);
    }
    SDL_free(files);
}

static int SDLCALL worker(void *unused)
{
    (void)unused;
    while (!SDL_GetAtomicInt(&g.quit)) {
        bool board = false, ticket = false, flush = false;
        char game[24], diff[16], tgame[24], tdiff[16];

        SDL_LockMutex(g.lock);
        if (g.want_board) {
            g.want_board = false;
            SDL_snprintf(game, sizeof game, "%s", g.want_game);
            SDL_snprintf(diff, sizeof diff, "%s", g.want_diff);
            board = true;
        }
        if (g.want_ticket[0] && !g.ticket_ready
            && SDL_GetTicks() >= g.ticket_retry_at_ms) {
            SDL_snprintf(tgame, sizeof tgame, "%s", g.want_ticket);
            SDL_snprintf(tdiff, sizeof tdiff, "%s", g.want_ticket_diff);
            /* La demande est CONSOMMÉE. La laisser armée jusqu'à ce qu'un billet
             * arrive transformait le moindre refus en boucle d'attente active :
             * c'est `ns_online_take_ticket` et `ns_online_prefetch_ticket` qui la
             * réarment, une fois, quand on en a de nouveau besoin. */
            g.want_ticket[0] = '\0';
            ticket = true;
        }
        if (g.want_flush) { g.want_flush = false; flush = true; }
        SDL_UnlockMutex(g.lock);

        if (board) fetch_board(game, diff);
        else if (flush) flush_queue_now();
        else if (ticket) fetch_ticket(tgame, tdiff);
        else SDL_Delay(60);
    }
    return 0;
}

/* ==========================================================================
 * D'ou vient l'adresse — voir ns_online.h pour l'ordre et ses raisons
 * ========================================================================== */

/* Une source vide, ou faite uniquement de blancs, compte pour absente. */
static bool source_dit_quelque_chose(const char *s)
{
    if (!s) return false;
    for (; *s; ++s) {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return true;
    }
    return false;
}

ns_online_url ns_online_resolve_url(const char *compilee,
                                    const char *config,
                                    const char *environnement,
                                    const char *ligne_commande)
{
    ns_online_url r;
    r.url = NULL;
    r.source = NS_ONLINE_SRC_AUCUNE;

    /*
     * Du plus FORT au plus faible, et on s'arrête au premier qui parle. Écrit
     * dans ce sens-là exprès : l'ordre de préséance se lit alors directement
     * dans l'ordre des lignes, et ajouter une source un jour, c'est insérer une
     * ligne au bon endroit plutôt que de réviser une cascade de conditions.
     */
    if (source_dit_quelque_chose(ligne_commande)) {
        r.url = ligne_commande; r.source = NS_ONLINE_SRC_LIGNE_COMMANDE;
    } else if (source_dit_quelque_chose(environnement)) {
        r.url = environnement;  r.source = NS_ONLINE_SRC_ENVIRONNEMENT;
    } else if (source_dit_quelque_chose(config)) {
        r.url = config;         r.source = NS_ONLINE_SRC_CONFIG;
    } else if (source_dit_quelque_chose(compilee)) {
        r.url = compilee;       r.source = NS_ONLINE_SRC_COMPILEE;
    }
    return r;
}

const char *ns_online_source_nom(ns_online_source source)
{
    switch (source) {
    case NS_ONLINE_SRC_LIGNE_COMMANDE: return "--server=";
    case NS_ONLINE_SRC_ENVIRONNEMENT:  return "NINETEEN_SERVER_URL";
    case NS_ONLINE_SRC_CONFIG:         return "config " NS_CFG_SERVER_URL;
    case NS_ONLINE_SRC_COMPILEE:       return "défaut compilé";
    case NS_ONLINE_SRC_AUCUNE:         break;
    }
    /* Zéro veut dire « personne ne l'a dit » : un appelant qui construit sa
     * configuration au `SDL_zero` tombe ici, et le journal ne doit pas lui
     * inventer une origine. */
    return "source non déclarée";
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
        /* On nomme quand même l'adresse qui a été VERROUILLÉE, et sa source.
         * Sans ça, « --offline » et « pas d'URL » produisent le même silence,
         * et l'on ne sait pas si le verrou a servi à quelque chose. */
        if (cfg->server_url && cfg->server_url[0]) {
            NS_INFO("réseau : verrouillé par --offline, aucune connexion ne sera "
                    "tentée (« %s », de %s, est ignorée)",
                    cfg->server_url, ns_online_source_nom(cfg->source));
        } else {
            NS_INFO("réseau : verrouillé par --offline, aucune connexion ne sera tentée");
        }
        return false;
    }
    if (!cfg->server_url || !cfg->server_url[0]) {
        /* On dit OÙ chercher. Quatre sources muettes se ressemblent toutes, et
         * quelqu'un qui vient de bâtir le dépôt n'a aucune raison de deviner
         * laquelle il aurait dû remplir. */
        NS_INFO("réseau : aucun serveur configuré, le classement restera local "
                "(ni --server=, ni NINETEEN_SERVER_URL, ni « %s » en config, "
                "ni défaut compilé -DNINETEEN_SERVER_URL)", NS_CFG_SERVER_URL);
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
    /*
     * LA SOURCE EST DANS LA LIGNE, et c'est le seul moyen de diagnostiquer
     * « pourquoi ça parle au mauvais serveur ». Quatre endroits peuvent fournir
     * l'adresse ; savoir lequel a gagné évite de relire les quatre.
     */
    NS_INFO("réseau : actif sur « %s » (source : %s)%s", g.url,
            ns_online_source_nom(cfg->source),
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

/* Un billet vaut pour UN créneau : « flappy/hard » n'ouvre pas une partie de
 * « flappy/normal ». Les deux tableaux sont distincts côté serveur, et deux
 * bornes voisines de la salle jouent justement le même jeu à deux difficultés. */
static bool ticket_matches(const char *game, const char *diff)
{
    const char *d = (diff && diff[0]) ? diff : "normal";
    return g.ticket_ready
        && SDL_strcasecmp(g.ticket_game, game) == 0
        && SDL_strcasecmp(g.ticket_diff, d) == 0;
}

static void arm_ticket(const char *game, const char *diff)
{
    SDL_snprintf(g.want_ticket, sizeof g.want_ticket, "%s", game);
    SDL_snprintf(g.want_ticket_diff, sizeof g.want_ticket_diff, "%s",
                 (diff && diff[0]) ? diff : "normal");
    SDL_snprintf(g.last_ticket, sizeof g.last_ticket, "%s", g.want_ticket);
    SDL_snprintf(g.last_ticket_diff, sizeof g.last_ticket_diff, "%s", g.want_ticket_diff);
}

void ns_online_prefetch_ticket(const char *game, const char *difficulty)
{
    if (!g.enabled || !game || !game[0]) return;
    SDL_LockMutex(g.lock);
    /* Un billet déjà prêt pour CE créneau : rien à faire. Pour un autre, on
     * demande celui-ci — le joueur a changé de borne, l'ancien ne servira plus.
     * Un seul d'avance, jamais deux : on ne joue qu'une partie à la fois. */
    if (!ticket_matches(game, difficulty)) arm_ticket(game, difficulty);
    SDL_UnlockMutex(g.lock);
}

bool ns_online_take_ticket(const char *game, const char *difficulty, ns_online_ticket *out)
{
    if (!out) return false;
    SDL_zerop(out);
    if (!g.enabled || !game || !game[0]) return false;

    bool got = false;
    SDL_LockMutex(g.lock);
    if (ticket_matches(game, difficulty)) {
        *out = g.ticket;
        g.ticket_ready = false;
        SDL_zero(g.ticket);
        got = true;
    }
    /* Qu'on ait pris un billet ou non, on en redemande un pour CE créneau :
     * c'est ce qui fait qu'il y en a un de prêt à la partie suivante — et une
     * partie suivante, à une borne, c'est dans quelques secondes. */
    arm_ticket(game, difficulty);
    SDL_UnlockMutex(g.lock);
    return got;
}

void ns_online_flush_queue(void)
{
    /*
     * Une partie jouée HORS LIGNE reste locale, définitivement, et ce n'est pas
     * une lacune : le serveur tire la graine ET le secret avant qu'on joue, donc
     * une partie sans billet n'a rien à prouver. `ns_runlog_enqueue` la refuse
     * déjà.
     *
     * Ce qui est en file, en revanche, est scellé ET adressé : ça part. Le fil
     * s'en charge — on ne fait que le lui demander, parce qu'une boucle de jeu
     * n'attend pas une requête HTTP.
     */
    if (!g.enabled) return;
    SDL_LockMutex(g.lock);
    g.want_flush = true;
    SDL_UnlockMutex(g.lock);
}

void ns_online_stats(uint32_t *sent, uint32_t *failed)
{
    if (sent) *sent = g.sent;
    if (failed) *failed = g.failed;
}

/* ==========================================================================
 * Ce que le temps réel emprunte ici — voir ns_online.h
 * ========================================================================== */

/*
 * NULL quand le réseau est inactif, et c'est tout l'intérêt : `ns_realtime` ne
 * peut rien ouvrir que `ns_online_init` n'ait déjà autorisé. Le verrou d'A2b
 * reste écrit une fois.
 */
const char *ns_online_server_url(void)
{
    return (g.enabled && g.url[0]) ? g.url : NULL;
}

const char *ns_online_session_token(void)
{
    return (g.enabled && g.token[0]) ? g.token : NULL;
}

int ns_online_game_id(const char *game, const char *difficulty)
{
    if (!g.enabled || !game || !game[0]) return -1;
    SDL_LockMutex(g.lock);
    const int id = g.games_known ? resolve_game(game, difficulty, NULL, 0) : -1;
    SDL_UnlockMutex(g.lock);
    return id;
}

void ns_online_set_duel(const char *ghost_run_id)
{
    if (!g.enabled) return;
    SDL_LockMutex(g.lock);
    if (ghost_run_id && ghost_run_id[0]) {
        SDL_snprintf(g.duel_ghost, sizeof g.duel_ghost, "%s", ghost_run_id);
    } else {
        g.duel_ghost[0] = '\0';
    }
    /*
     * Un billet déjà prêt a été tiré sur une AUTRE graine que celle du fantôme
     * qu'on vient de choisir : le garder ferait jouer un duel qui n'en est pas
     * un, sans que rien ne le dise. On le jette et on en redemande un.
     *
     * C'est le genre de défaut qui ne se voit pas en jouant — la partie se
     * déroule normalement, le fantôme est simplement... quelqu'un d'autre.
     */
    g.ticket_ready = false;
    SDL_zero(g.ticket);
    /*
     * Et on en REDEMANDE un tout de suite, pour le même créneau. Sans cette
     * ligne le billet jeté n'était jamais remplacé — `want_ticket` ayant été
     * consommé par le fil — et le duel se jouait hors ligne : la partie
     * démarrait sur la graine locale, donc pas sur celle du fantôme, donc
     * contre un adversaire qui jouait à un autre jeu que le sien.
     *
     * L'attente d'après-échec est levée du même coup : ce n'est pas un échec,
     * c'est un changement d'avis du joueur, et il ne doit pas coûter quinze
     * secondes d'attente devant la borne.
     */
    if (g.last_ticket[0]) {
        arm_ticket(g.last_ticket, g.last_ticket_diff);
        g.ticket_retry_at_ms = 0;
    }
    SDL_UnlockMutex(g.lock);
}
