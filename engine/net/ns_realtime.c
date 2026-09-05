/* ns_realtime.c — voir ns_realtime.h pour les DEUX verrous, qui priment. */
#include "ns_realtime.h"

#include "ns_core.h"
#include "ns_http.h"
#include "ns_json.h"
#include "ns_online.h"

#include <SDL3/SDL.h>

#include <stdarg.h>

/*
 * Le rythme de la présence.
 *
 * 4 Hz, et non les ~20 Hz qu'évoquait le document de conception. La raison est
 * le transport : `ns_http` rouvre une socket à chaque requête — pas de
 * connexion persistante, pas de TLS, c'est un client de deux cent cinquante
 * lignes et c'est délibéré. Vingt allers-retours par seconde et par joueur y
 * coûteraient vingt poignées de main TCP, pour décrire un bonhomme qui marche à
 * 1,4 m/s.
 *
 * Quatre battements par seconde, c'est 35 cm entre deux positions connues, et
 * l'interpolation au rendu comble le reste sans qu'on voie de saut. La mesure
 * est dans `tests/test_duel.c` plutôt que dans une intention.
 *
 * `NS_RT_PERIOD_MS` et `NS_RT_STALE_MS` sont DÉCLARÉS DANS L'EN-TÊTE depuis que
 * les pairs ont un corps : `room_presence.c` interpole sur la première et fond
 * la sortie sur la seconde. Elles restent expliquées ici, à l'endroit où elles
 * décident vraiment de quelque chose.
 *
 * La péremption : après quoi on cesse de croire ce qu'on sait des autres. Trois
 * battements manqués — assez pour absorber une requête lente, trop peu pour
 * laisser un joueur figé dans l'allée quand le serveur tombe.
 */

static struct {
    bool          enabled;
    char          nickname[NS_RT_NAME];
    char          client_id[40];
    char          url[512];
    char          token[256];
    char          status[128];

    SDL_Thread   *thread;
    SDL_Mutex    *lock;
    SDL_AtomicInt quit;

    /* --- Ma position, déposée par la boucle de jeu --- */
    float         me_x, me_y, me_z, me_yaw, me_eye;
    char          me_cabinet[NS_RT_SLUG];
    char          me_game[NS_RT_SLUG];
    int32_t       me_score;
    bool          leaving;

    /* --- Les autres --- */
    ns_realtime_peer peer[NS_RT_MAX_PEERS];
    uint32_t         peer_count;
    uint64_t         peer_at_ms;      /* 0 = jamais reçu */

    /* --- Fantômes --- */
    char             want_ghosts[NS_RT_SLUG];
    char             want_ghosts_diff[16];
    ns_realtime_ghost_info ghost[NS_RT_MAX_GHOSTS];
    uint32_t               ghost_count;

    char             want_fetch[NS_RT_ID];
    char            *ghost_text;      /* le journal téléchargé, ou NULL */
    size_t           ghost_len;
    ns_realtime_ghost_info ghost_info;

    /* Un dépôt en attente. Un seul : on ne finit qu'une partie à la fois. */
    char             up_run[NS_RT_ID];
    char            *up_text;
    size_t           up_len;

    uint32_t         presence_ok, presence_failed, ghosts_sent;
} g;

/*
 * L'échappement JSON des chaînes qu'on émet.
 *
 * Le moteur n'a pas d'écrivain JSON — il n'en a jamais eu besoin, les corps
 * qu'il produit tenant en six champs — et `ns_runlog` a la même fonction en
 * statique pour la même raison. La dupliquer ici plutôt que d'élargir
 * `ns_json.h` : dix lignes contre une fonction publique de plus dans un en-tête
 * que tout le moteur inclut.
 *
 * Ce qu'elle évite concrètement : un pseudo contenant un guillemet ou une
 * contre-oblique produirait un corps JSON invalide, donc un 400, donc une
 * présence qui échoue en silence — pour le seul joueur qui a mis une
 * apostrophe dans son nom, et sans que rien ne dise pourquoi. Les caractères de
 * contrôle sautent : ils n'ont rien à faire dans un pseudo, et le serveur les
 * retirerait de toute façon.
 */
static void json_escape(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 2 < cap; ++p) {
        if (*p == '"' || *p == '\\') { out[n++] = '\\'; out[n++] = *p; }
        else if ((unsigned char)*p >= 0x20) out[n++] = *p;
    }
    out[n] = '\0';
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_LockMutex(g.lock);
    SDL_vsnprintf(g.status, sizeof g.status, fmt, ap);
    SDL_UnlockMutex(g.lock);
    va_end(ap);
}

/*
 * L'identifiant de client : aléatoire, local, sans aucun privilège.
 *
 * Il ne sert qu'à deux choses — ne pas se voir soi-même dans la liste des
 * pairs, et remplacer sa propre ligne plutôt que d'en empiler une nouvelle à
 * chaque battement. Ce n'est pas une identité : le serveur ne lui accorde rien,
 * et le pseudo qu'il porte est marqué non vérifié tant qu'aucun jeton ne
 * l'accompagne.
 *
 * Tiré à chaque démarrage, donc non traçable d'une session à l'autre. C'est
 * gratuit et c'est la bonne valeur par défaut : un jeu d'arcade n'a aucune
 * raison de fabriquer un identifiant persistant pour ses joueurs.
 */
static void make_client_id(char *out, size_t cap)
{
    static const char ALPHA[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    /* Le serveur n'accepte que [A-Za-z0-9_-] et 8 à 64 caractères. */
    const size_t n = 24;
    size_t i = 0;
    for (; i < n && i + 1 < cap; ++i) {
        out[i] = ALPHA[SDL_rand(35)];
    }
    out[i] = '\0';
}

/* ==========================================================================
 * La présence
 * ========================================================================== */

/*
 * Un aller-retour : je dis où je suis, le serveur me dit qui d'autre est là.
 *
 * Une seule requête pour les deux, parce que la présence est le seul échange
 * PÉRIODIQUE du jeu et que la couper en deux doublerait son trafic sans rien
 * apporter.
 */
static void beat_presence(void)
{
    char nick[NS_RT_NAME], cab[NS_RT_SLUG], game[NS_RT_SLUG], cid[40];
    float x, y, z, yaw, eye;
    int32_t score;
    bool leaving;

    SDL_LockMutex(g.lock);
    SDL_snprintf(nick, sizeof nick, "%s", g.nickname);
    SDL_snprintf(cab, sizeof cab, "%s", g.me_cabinet);
    SDL_snprintf(game, sizeof game, "%s", g.me_game);
    SDL_snprintf(cid, sizeof cid, "%s", g.client_id);
    x = g.me_x; y = g.me_y; z = g.me_z; yaw = g.me_yaw; eye = g.me_eye;
    score = g.me_score;
    leaving = g.leaving;
    SDL_UnlockMutex(g.lock);

    char url[640];
    SDL_snprintf(url, sizeof url, "%s/api/v1/presence", g.url);

    /*
     * Le corps est bâti à la main plutôt que par un sérialiseur : c'est six
     * champs, et le moteur n'a pas d'écrivain JSON. Les chaînes qui y entrent
     * sont échappées ci-dessous — un pseudo contenant un guillemet produirait
     * sinon un JSON invalide, donc une présence qui échoue en silence pour le
     * seul joueur qui a mis une apostrophe typographique dans son nom.
     */
    char nick_esc[NS_RT_NAME * 2], cab_esc[NS_RT_SLUG * 2], game_esc[NS_RT_SLUG * 2];
    json_escape(nick, nick_esc, sizeof nick_esc);
    json_escape(cab, cab_esc, sizeof cab_esc);
    json_escape(game, game_esc, sizeof game_esc);

    char body[768];
    const int len = SDL_snprintf(body, sizeof body,
        "{\"clientId\":\"%s\",\"nickname\":\"%s\","
        "\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"yaw\":%.4f,\"eye\":%.3f,"
        "\"cabinet\":\"%s\",\"game\":\"%s\",\"score\":%d%s}",
        cid, nick_esc, (double)x, (double)y, (double)z, (double)yaw, (double)eye,
        cab_esc, game_esc, (int)score,
        leaving ? ",\"leaving\":true" : "");

    ns_http_response r;
    /*
     * Un délai COURT, et c'est un choix de conception. La présence est
     * périodique : une requête qui traîne huit secondes serait déjà remplacée
     * par les suivantes avant d'aboutir. Mieux vaut la perdre vite et repartir
     * au battement d'après.
     */
    if (!ns_http_request("POST", url, "application/json",
                         g.token[0] ? g.token : NULL, body, (size_t)len, 2000, &r)) {
        g.presence_failed++;
        set_status("présence : serveur injoignable");
        ns_http_response_free(&r);
        return;
    }
    if (r.status != 200 || !r.body) {
        g.presence_failed++;
        set_status("présence : %d", r.status);
        ns_http_response_free(&r);
        return;
    }

    ns_arena arena;
    if (!ns_arena_init(&arena, 128u * 1024u, "json présence")) {
        ns_http_response_free(&r);
        return;
    }
    ns_json doc;
    if (ns_json_parse(&doc, r.body, r.length, &arena)) {
        const ns_json_value *root = ns_json_root(&doc);
        const ns_json_value *peers = ns_json_get(&doc, root, "peers");
        const int n = ns_json_array_count(&doc, peers);

        SDL_LockMutex(g.lock);
        g.peer_count = 0;
        for (int i = 0; i < n && g.peer_count < NS_RT_MAX_PEERS; ++i) {
            const ns_json_value *e = ns_json_at(&doc, peers, i);
            ns_realtime_peer *p = &g.peer[g.peer_count];
            SDL_zerop(p);
            ns_json_get_string(&doc, e, "nickname", p->name, sizeof p->name);
            if (!p->name[0]) continue;
            ns_json_get_string(&doc, e, "clientId", p->id, sizeof p->id);
            p->verified = ns_json_get_bool(&doc, e, "verified", false);
            p->x   = ns_json_get_float(&doc, e, "x", 0.0f);
            p->y   = ns_json_get_float(&doc, e, "y", 0.0f);
            p->z   = ns_json_get_float(&doc, e, "z", 0.0f);
            /*
             * LE CAP, ET SON ABSENCE, qui ne se confondent pas.
             *
             * `ns_json_get_float` rend son repli aussi bien pour une clé absente
             * que pour un cap réellement nul, et les deux ne veulent pas dire la
             * même chose : le premier pair doit être tourné par son déplacement,
             * le second regarde vraiment vers +X. On demande donc la CLÉ avant
             * de lire sa valeur — c'est la seule façon de les séparer, et elle
             * ne coûte qu'une recherche déjà faite par la lecture qui suit.
             *
             * Ce que ça achète : un pair d'une version antérieure au champ
             * marche droit devant lui au lieu de glisser de côté en regardant
             * l'est.
             */
            p->has_yaw = (ns_json_get(&doc, e, "yaw") != NULL);
            p->yaw = ns_json_get_float(&doc, e, "yaw", 0.0f);
            /* Zéro = non publiée, ce qui est exactement ce que rend le repli.
             * L'appelant pose alors sa propre hauteur d'œil. */
            p->eye = ns_json_get_float(&doc, e, "eye", 0.0f);
            ns_json_get_string(&doc, e, "cabinet", p->cabinet, sizeof p->cabinet);
            ns_json_get_string(&doc, e, "game", p->game, sizeof p->game);
            p->score = (int32_t)ns_json_get_i64(&doc, e, "score", 0);
            g.peer_count++;
        }
        g.peer_at_ms = SDL_GetTicks();
        SDL_UnlockMutex(g.lock);
        g.presence_ok++;
        set_status("%u joueur(s) dans la salle", g.peer_count);
    }
    ns_arena_free(&arena);
    ns_http_response_free(&r);
}

/* ==========================================================================
 * Les fantômes
 * ========================================================================== */

static void fetch_ghost_list(const char *game, const char *diff)
{
    /* Le serveur interroge par NUMÉRO de créneau ; le moteur ne connaît que des
     * noms. La traduction vit dans `ns_online`, contre la liste que le serveur a
     * réellement renvoyée — c'est le premier des trois défauts qu'avait trouvés
     * le test de bout en bout, et on ne le refait pas ici. */
    const int id = ns_online_game_id(game, diff);
    if (id < 0) {
        set_status("« %s/%s » inconnu du serveur", game, diff ? diff : "normal");
        return;
    }

    char url[700];
    SDL_snprintf(url, sizeof url, "%s/api/v1/ghosts?game=%d&limit=%d",
                 g.url, id, NS_RT_MAX_GHOSTS);

    ns_http_response r;
    if (!ns_http_request("GET", url, NULL, g.token[0] ? g.token : NULL,
                         NULL, 0, 4000, &r)) {
        set_status("fantômes : serveur injoignable");
        ns_http_response_free(&r);
        return;
    }
    if (r.status != 200 || !r.body) {
        set_status("fantômes : %d", r.status);
        ns_http_response_free(&r);
        return;
    }

    ns_arena arena;
    if (!ns_arena_init(&arena, 128u * 1024u, "json fantômes")) {
        ns_http_response_free(&r);
        return;
    }
    ns_json doc;
    if (ns_json_parse(&doc, r.body, r.length, &arena)) {
        const ns_json_value *root = ns_json_root(&doc);
        const ns_json_value *arr = ns_json_get(&doc, root, "ghosts");
        const int n = ns_json_array_count(&doc, arr);

        SDL_LockMutex(g.lock);
        g.ghost_count = 0;
        for (int i = 0; i < n && g.ghost_count < NS_RT_MAX_GHOSTS; ++i) {
            const ns_json_value *e = ns_json_at(&doc, arr, i);
            ns_realtime_ghost_info *gi = &g.ghost[g.ghost_count];
            SDL_zerop(gi);
            ns_json_get_string(&doc, e, "runId", gi->run_id, sizeof gi->run_id);
            if (!gi->run_id[0]) continue;
            ns_json_get_string(&doc, e, "username", gi->name, sizeof gi->name);
            gi->score = ns_json_get_i64(&doc, e, "score", 0);
            /* La graine fait 63 bits : elle se lit en ENTIER. Relue en flottant
             * elle perdrait ses bits de poids faible, et le duel se jouerait sur
             * une autre partie que celle du fantôme — c'est le deuxième des
             * trois défauts trouvés en bout de chaîne, et il se reproduirait
             * ici mot pour mot. */
            gi->seed  = ns_json_get_i64(&doc, e, "seed", 0);
            gi->ticks = (int32_t)ns_json_get_i64(&doc, e, "ticks", 0);
            g.ghost_count++;
        }
        SDL_UnlockMutex(g.lock);
        set_status("%u fantôme(s) disponibles", g.ghost_count);
    }
    ns_arena_free(&arena);
    ns_http_response_free(&r);
}

/*
 * Le JOURNAL d'un fantôme, en texte brut.
 *
 * Pas de JSON autour, et ce n'est pas une simplification gratuite :
 * `ns_json_string` ne DÉSÉCHAPPE pas — il rend les octets bruts entre les
 * guillemets. Un journal encodé en JSON serait donc arrivé ici comme une seule
 * ligne parsemée de « \n » littéraux, et l'analyseur, qui découpe sur les
 * retours à la ligne, en aurait tiré ZÉRO entrée : un fantôme immobile, aucun
 * message d'erreur, et les deux moitiés justes chacune de leur côté.
 *
 * Du texte brut n'a pas d'échappement, donc pas de désaccord possible sur son
 * échappement.
 */
static void fetch_ghost_body(const char *run_id)
{
    char url[700];
    SDL_snprintf(url, sizeof url, "%s/api/v1/ghosts/%s", g.url, run_id);

    ns_http_response r;
    if (!ns_http_request("GET", url, NULL, g.token[0] ? g.token : NULL,
                         NULL, 0, 6000, &r)) {
        set_status("fantôme : serveur injoignable");
        ns_http_response_free(&r);
        return;
    }
    if (r.status != 200 || !r.body || r.length == 0) {
        set_status("fantôme : %d", r.status);
        ns_http_response_free(&r);
        return;
    }

    char *copy = (char *)SDL_malloc(r.length + 1);
    if (!copy) { ns_http_response_free(&r); return; }
    SDL_memcpy(copy, r.body, r.length);
    copy[r.length] = '\0';

    SDL_LockMutex(g.lock);
    SDL_free(g.ghost_text);
    g.ghost_text = copy;
    g.ghost_len  = r.length;
    /* On rattache les métadonnées déjà connues de la liste : c'est là que sont
     * le nom de l'adversaire et son score. Le journal, lui, porte sa propre
     * graine dans sa première ligne — il est autoportant. */
    SDL_zero(g.ghost_info);
    for (uint32_t i = 0; i < g.ghost_count; ++i) {
        if (SDL_strcmp(g.ghost[i].run_id, run_id) == 0) {
            g.ghost_info = g.ghost[i];
            break;
        }
    }
    if (!g.ghost_info.run_id[0]) {
        SDL_snprintf(g.ghost_info.run_id, sizeof g.ghost_info.run_id, "%s", run_id);
    }
    SDL_UnlockMutex(g.lock);

    ns_http_response_free(&r);
    set_status("fantôme téléchargé (%zu octets)", (size_t)g.ghost_len);
}

/* Dépose son propre journal. Voir `ns_realtime_publish_ghost`. */
static void upload_ghost(const char *run_id, const char *text, size_t len)
{
    char url[700];
    SDL_snprintf(url, sizeof url, "%s/api/v1/runs/%s/inputs", g.url, run_id);

    ns_http_response r;
    const bool reached = ns_http_request("POST", url, "text/plain", g.token,
                                         text, len, 6000, &r);
    if (reached && r.status >= 200 && r.status < 300) {
        g.ghosts_sent++;
        set_status("fantôme publié");
    } else {
        /* Un refus n'est pas une panne du jeu : au pire, personne ne pourra
         * affronter cette partie-là. On le dit et on passe. */
        set_status("fantôme refusé (%d)", reached ? r.status : 0);
    }
    ns_http_response_free(&r);
}

/* ==========================================================================
 * Le fil
 * ========================================================================== */

static int SDLCALL worker(void *unused)
{
    (void)unused;
    uint64_t next_beat = 0;

    while (!SDL_GetAtomicInt(&g.quit)) {
        /*
         * Les tâches ponctuelles PASSENT AVANT le battement de présence : un
         * joueur qui vient de choisir un adversaire attend son journal, et lui
         * faire attendre le prochain quart de seconde pour rien serait une
         * latence qu'on s'inflige.
         */
        char want_list[NS_RT_SLUG] = { 0 }, want_diff[16] = { 0 };
        char want_fetch[NS_RT_ID] = { 0 };
        char up_run[NS_RT_ID] = { 0 };
        char  *up_text = NULL;
        size_t up_len = 0;

        SDL_LockMutex(g.lock);
        if (g.want_ghosts[0]) {
            SDL_snprintf(want_list, sizeof want_list, "%s", g.want_ghosts);
            SDL_snprintf(want_diff, sizeof want_diff, "%s", g.want_ghosts_diff);
            g.want_ghosts[0] = '\0';
        }
        if (g.want_fetch[0]) {
            SDL_snprintf(want_fetch, sizeof want_fetch, "%s", g.want_fetch);
            g.want_fetch[0] = '\0';
        }
        if (g.up_run[0] && g.up_text) {
            SDL_snprintf(up_run, sizeof up_run, "%s", g.up_run);
            up_text = g.up_text;      /* le fil en prend la propriété */
            up_len  = g.up_len;
            g.up_run[0] = '\0';
            g.up_text = NULL;
            g.up_len = 0;
        }
        SDL_UnlockMutex(g.lock);

        if (want_list[0]) fetch_ghost_list(want_list, want_diff);
        if (want_fetch[0]) fetch_ghost_body(want_fetch);
        if (up_run[0] && up_text) {
            upload_ghost(up_run, up_text, up_len);
            SDL_free(up_text);
        }

        const uint64_t now = SDL_GetTicks();
        if (now >= next_beat) {
            next_beat = now + NS_RT_PERIOD_MS;
            beat_presence();
        } else if (!want_list[0] && !want_fetch[0] && !up_run[0]) {
            /* Rien à faire : on dort le reste du battement plutôt que de
             * tourner. Borné à 20 ms pour rester réactif à l'arrêt. */
            uint64_t wait = next_beat - now;
            if (wait > 20) wait = 20;
            SDL_Delay((uint32_t)wait);
        }
    }

    /*
     * La politesse du départ. Le TTL du serveur ferait le travail en douze
     * secondes, mais un joueur qui ferme le jeu ne devrait pas continuer à
     * hanter la salle pendant ce temps-là.
     *
     * Un seul essai, court : c'est un au revoir, pas une transaction. S'il
     * échoue, le TTL reprend la main — la sortie du programme ne doit jamais
     * attendre le réseau.
     */
    SDL_LockMutex(g.lock);
    g.leaving = true;
    SDL_UnlockMutex(g.lock);
    beat_presence();
    return 0;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

bool ns_realtime_init(const ns_realtime_config *cfg)
{
    SDL_zero(g);
    SDL_snprintf(g.status, sizeof g.status, "temps réel inactif");

    if (!cfg) return false;

    /*
     * LE SECOND VERROU, et il est vérifié en premier parce que c'est le choix
     * du joueur : le temps réel est inerte tant qu'il ne l'a pas activé, même
     * avec un serveur configuré et joignable. Le classement ne diffuse rien de
     * soi ; la présence publie un pseudo et une position, et ce n'est pas à un
     * réglage de serveur d'en décider.
     */
    if (!cfg->enabled) {
        NS_INFO("temps réel : désactivé (défaut) — ni présence ni duel");
        return false;
    }

    /*
     * LE PREMIER VERROU, hérité et non réimplémenté. Sans URL de serveur, ou
     * sous `--offline`, `ns_online` n'a pas démarré et ne rend aucune URL : il
     * n'y a rien à ouvrir. La garantie « sans URL configurée, aucune socket
     * n'est ouverte » reste écrite à un seul endroit.
     */
    const char *url = ns_online_server_url();
    if (!url) {
        NS_INFO("temps réel : demandé, mais le réseau est inactif "
                "(pas de serveur, ou --offline) — rien ne sera ouvert");
        SDL_snprintf(g.status, sizeof g.status, "temps réel sans serveur");
        return false;
    }

    SDL_snprintf(g.url, sizeof g.url, "%s", url);
    const char *tok = ns_online_session_token();
    if (tok) SDL_snprintf(g.token, sizeof g.token, "%s", tok);

    SDL_snprintf(g.nickname, sizeof g.nickname, "%s",
                 (cfg->nickname && cfg->nickname[0]) ? cfg->nickname : "Anonyme");
    make_client_id(g.client_id, sizeof g.client_id);

    g.lock = SDL_CreateMutex();
    if (!g.lock) return false;

    SDL_SetAtomicInt(&g.quit, 0);
    g.thread = SDL_CreateThread(worker, "nineteen-rt", NULL);
    if (!g.thread) {
        SDL_DestroyMutex(g.lock);
        g.lock = NULL;
        NS_WARN("temps réel : fil indisponible");
        return false;
    }

    g.enabled = true;
    SDL_snprintf(g.status, sizeof g.status, "temps réel actif");
    NS_INFO("temps réel : actif sur « %s » sous le nom « %s »%s",
            g.url, g.nickname, g.token[0] ? " (pseudo vérifié)" : " (pseudo déclaré)");
    return true;
}

void ns_realtime_shutdown(void)
{
    if (g.thread) {
        SDL_SetAtomicInt(&g.quit, 1);
        SDL_WaitThread(g.thread, NULL);
        g.thread = NULL;
    }
    if (g.lock) { SDL_DestroyMutex(g.lock); g.lock = NULL; }
    SDL_free(g.ghost_text);
    g.ghost_text = NULL;
    SDL_free(g.up_text);
    g.up_text = NULL;
    g.enabled = false;
    SDL_snprintf(g.status, sizeof g.status, "temps réel arrêté");
}

bool        ns_realtime_enabled(void) { return g.enabled; }
const char *ns_realtime_status(void)  { return g.status; }

/* ==========================================================================
 * Présence — l'interface de la boucle de jeu
 * ========================================================================== */

void ns_realtime_publish(float x, float y, float z, float yaw, float eye,
                         const char *cabinet, const char *game, int32_t score)
{
    if (!g.enabled) return;
    SDL_LockMutex(g.lock);
    g.me_x = x; g.me_y = y; g.me_z = z; g.me_yaw = yaw; g.me_eye = eye;
    SDL_snprintf(g.me_cabinet, sizeof g.me_cabinet, "%s", cabinet ? cabinet : "");
    SDL_snprintf(g.me_game, sizeof g.me_game, "%s", game ? game : "");
    g.me_score = score;
    SDL_UnlockMutex(g.lock);
}

uint32_t ns_realtime_peers(ns_realtime_peer *out, uint32_t max, uint64_t *at_ms)
{
    if (at_ms) *at_ms = 0;
    if (!g.enabled || !out || !max) return 0;

    SDL_LockMutex(g.lock);
    /*
     * Un état PÉRIMÉ ne se rend pas.
     *
     * Sans ça, un serveur qui tombe laisse les autres joueurs plantés là où ils
     * étaient, pour toujours. Un joueur immobile est un bogue qu'on regarde ;
     * un joueur absent est une déconnexion qu'on comprend.
     */
    const uint64_t now = SDL_GetTicks();
    if (g.peer_at_ms == 0 || now - g.peer_at_ms > NS_RT_STALE_MS) {
        SDL_UnlockMutex(g.lock);
        return 0;
    }
    uint32_t n = (g.peer_count < max) ? g.peer_count : max;
    for (uint32_t i = 0; i < n; ++i) out[i] = g.peer[i];
    /* Sous le MÊME verrou que la table : c'est tout l'intérêt du paramètre.
     * Voir `ns_realtime.h`. */
    if (at_ms) *at_ms = g.peer_at_ms;
    SDL_UnlockMutex(g.lock);
    return n;
}

uint32_t ns_realtime_peers_age_ms(void)
{
    if (!g.enabled) return UINT32_MAX;
    SDL_LockMutex(g.lock);
    const uint64_t at = g.peer_at_ms;
    SDL_UnlockMutex(g.lock);
    if (at == 0) return UINT32_MAX;
    return (uint32_t)(SDL_GetTicks() - at);
}

/* ==========================================================================
 * Fantômes — l'interface de la boucle de jeu
 * ========================================================================== */

void ns_realtime_request_ghosts(const char *game, const char *difficulty)
{
    if (!g.enabled || !game || !game[0]) return;
    SDL_LockMutex(g.lock);
    SDL_snprintf(g.want_ghosts, sizeof g.want_ghosts, "%s", game);
    SDL_snprintf(g.want_ghosts_diff, sizeof g.want_ghosts_diff, "%s",
                 (difficulty && difficulty[0]) ? difficulty : "normal");
    SDL_UnlockMutex(g.lock);
}

uint32_t ns_realtime_ghosts(ns_realtime_ghost_info *out, uint32_t max)
{
    if (!g.enabled || !out || !max) return 0;
    SDL_LockMutex(g.lock);
    uint32_t n = (g.ghost_count < max) ? g.ghost_count : max;
    for (uint32_t i = 0; i < n; ++i) out[i] = g.ghost[i];
    SDL_UnlockMutex(g.lock);
    return n;
}

void ns_realtime_fetch_ghost(const char *run_id)
{
    if (!g.enabled || !run_id || !run_id[0]) return;
    SDL_LockMutex(g.lock);
    SDL_snprintf(g.want_fetch, sizeof g.want_fetch, "%s", run_id);
    SDL_UnlockMutex(g.lock);
}

bool ns_realtime_take_ghost(char **out_text, size_t *out_len,
                            ns_realtime_ghost_info *out_info)
{
    if (!g.enabled || !out_text || !out_len) return false;
    bool got = false;
    SDL_LockMutex(g.lock);
    if (g.ghost_text) {
        *out_text = g.ghost_text;     /* l'appelant en prend la propriété */
        *out_len  = g.ghost_len;
        if (out_info) *out_info = g.ghost_info;
        g.ghost_text = NULL;
        g.ghost_len = 0;
        got = true;
    }
    SDL_UnlockMutex(g.lock);
    return got;
}

void ns_realtime_publish_ghost(const char *run_id, const char *inputs, size_t len)
{
    if (!g.enabled || !run_id || !run_id[0] || !inputs || !len) return;
    /* Sans jeton de session, le dépôt serait refusé (401) : on ne fait pas le
     * voyage pour rien. Voir un fantôme n'exige aucun compte ; en DÉPOSER un
     * suppose une partie authentifiée, donc un compte. */
    if (!g.token[0]) return;

    char *copy = (char *)SDL_malloc(len);
    if (!copy) return;
    SDL_memcpy(copy, inputs, len);

    SDL_LockMutex(g.lock);
    SDL_free(g.up_text);              /* un dépôt en attente est remplacé */
    g.up_text = copy;
    g.up_len  = len;
    SDL_snprintf(g.up_run, sizeof g.up_run, "%s", run_id);
    SDL_UnlockMutex(g.lock);
}

void ns_realtime_stats(uint32_t *presence_ok, uint32_t *presence_failed,
                       uint32_t *ghosts_sent)
{
    if (presence_ok) *presence_ok = g.presence_ok;
    if (presence_failed) *presence_failed = g.presence_failed;
    if (ghosts_sent) *ghosts_sent = g.ghosts_sent;
}
