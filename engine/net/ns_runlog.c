/* ns_runlog.c — voir ns_runlog.h pour le raisonnement. */
#include "ns_runlog.h"

#include "ns_core.h"
#include "ns_json.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/* SHA-256 (FIPS 180-4)                                                       */
/* ========================================================================== */

typedef struct sha256_ctx {
    uint32_t state[8];
    uint64_t bits;
    uint8_t  buf[64];
    size_t   have;
} sha256_ctx;

static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(sha256_ctx *c, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16)
             | ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
    uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        const uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
    c->state[0] = 0x6a09e667u; c->state[1] = 0xbb67ae85u;
    c->state[2] = 0x3c6ef372u; c->state[3] = 0xa54ff53au;
    c->state[4] = 0x510e527fu; c->state[5] = 0x9b05688cu;
    c->state[6] = 0x1f83d9abu; c->state[7] = 0x5be0cd19u;
    c->bits = 0; c->have = 0;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    c->bits += (uint64_t)len * 8u;
    while (len) {
        const size_t take = (64 - c->have < len) ? (64 - c->have) : len;
        memcpy(c->buf + c->have, p, take);
        c->have += take; p += take; len -= take;
        if (c->have == 64) { sha256_block(c, c->buf); c->have = 0; }
    }
}

static void sha256_final(sha256_ctx *c, uint8_t out[32])
{
    const uint64_t bits = c->bits;
    uint8_t pad = 0x80;
    sha256_update(c, &pad, 1);
    c->bits = bits;                       /* le remplissage ne compte pas */
    pad = 0x00;
    while (c->have != 56) { sha256_update(c, &pad, 1); c->bits = bits; }
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = (uint8_t)(bits >> (56 - i * 8));
    sha256_update(c, len, 8);
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = (uint8_t)(c->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
}

void ns_sha256(const void *data, size_t len, uint8_t out[32])
{
    sha256_ctx c; sha256_init(&c); sha256_update(&c, data, len); sha256_final(&c, out);
}

void ns_hmac_sha256(const uint8_t *key, size_t key_len,
                    const void *data, size_t len, uint8_t out[32])
{
    uint8_t k[64];
    SDL_memset(k, 0, sizeof k);
    /* Une clé plus longue que le bloc est d'abord condensée — c'est la RFC 2104,
     * et l'oublier donne un HMAC qui ne diffère du bon que pour les longues
     * clés, donc jamais pendant les tests et toujours en production. */
    if (key_len > 64) ns_sha256(key, key_len, k);
    else if (key && key_len) memcpy(k, key, key_len);

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

    uint8_t inner[32];
    sha256_ctx c;
    sha256_init(&c); sha256_update(&c, ipad, 64); sha256_update(&c, data, len);
    sha256_final(&c, inner);
    sha256_init(&c); sha256_update(&c, opad, 64); sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

/* Base64 SANS remplissage : `base64.RawStdEncoding` côté Go. Avec les « = »
 * finaux, la comparaison de sceau échoue et le message ne dit pas pourquoi. */
size_t ns_base64_raw(const uint8_t *data, size_t len, char *out, size_t cap)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t need = (len * 8 + 5) / 6;
    if (!out || cap == 0) return need;

    size_t n = 0;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
        const uint32_t c = (i + 2 < len) ? data[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        const int chars = (i + 2 < len) ? 4 : ((i + 1 < len) ? 3 : 2);
        for (int k = 0; k < chars; ++k) {
            if (n + 1 < cap) out[n] = T[(v >> (18 - k * 6)) & 0x3F];
            n++;
        }
    }
    out[(n < cap) ? n : cap - 1] = '\0';
    return need;
}

/* ========================================================================== */
/* Le journal                                                                 */
/* ========================================================================== */

struct ns_runlog {
    char     game[32], difficulty[16];
    int64_t  seed;
    char     run_id[64];
    uint8_t  secret[64];
    size_t   secret_len;

    ns_run_event *event;
    uint32_t      count, capacity;

    /* Le journal d'entrées. Séparé du journal d'événements parce qu'il ne
     * répond pas à la même question : celui-ci rejoue, l'autre authentifie. */
    ns_run_input *input;
    uint32_t      input_count, input_capacity;
    uint8_t       last_held;
    bool          input_started;
    /*
     * Le DERNIER pas vu, qu'il ait produit une ligne ou non.
     *
     * Le journal n'enregistre que les CHANGEMENTS, ce qui est ce qu'il faut —
     * mais il perdait du coup la LONGUEUR de la partie : un joueur qui ne
     * touche plus rien pendant les dix dernières secondes laisse un journal
     * dont la dernière ligne est dix secondes trop tôt. Rejoué, son fantôme
     * s'arrêtait donc dix secondes avant la fin, et deux machines ne voyaient
     * plus la même partie. `tests/test_duel.c` l'a attrapé en comparant les
     * états au bit près après un aller-retour réseau.
     */
    int32_t       last_tick;

    int64_t duration_ms, claimed;
    bool    closed;
};

static char g_queue_dir[1024];
static char g_queue_override[1024];

ns_runlog *ns_runlog_create(uint32_t capacity)
{
    if (capacity == 0 || capacity > NS_RUNLOG_MAX_EVENTS) capacity = 8192;
    ns_runlog *r = (ns_runlog *)SDL_calloc(1, sizeof *r);
    if (!r) return NULL;
    r->event = (ns_run_event *)SDL_calloc(capacity, sizeof(ns_run_event));
    if (!r->event) { SDL_free(r); return NULL; }
    r->capacity = capacity;
    return r;
}

void ns_runlog_destroy(ns_runlog *r)
{
    if (!r) return;
    SDL_free(r->event);
    SDL_free(r->input);
    SDL_free(r);
}

void ns_runlog_begin(ns_runlog *r, const char *game, const char *difficulty,
                     int64_t seed, const uint8_t *secret, size_t secret_len)
{
    /* Une nouvelle partie, un nouveau journal d'entrées : le garder ferait
     * rejouer la précédente collée devant celle-ci. */
    r->input_count = 0;
    r->last_held = 0;
    r->input_started = false;
    r->last_tick = 0;

    if (!r) return;
    r->count = 0; r->duration_ms = 0; r->claimed = 0; r->closed = false;
    r->seed = seed;
    SDL_strlcpy(r->game, game ? game : "", sizeof r->game);
    SDL_strlcpy(r->difficulty, (difficulty && *difficulty) ? difficulty : "normal",
                sizeof r->difficulty);
    r->secret_len = 0;
    r->run_id[0] = '\0';
    SDL_memset(r->secret, 0, sizeof r->secret);
    if (secret && secret_len) {
        r->secret_len = (secret_len > sizeof r->secret) ? sizeof r->secret : secret_len;
        memcpy(r->secret, secret, r->secret_len);
    }
}


void ns_runlog_input(ns_runlog *r, int32_t tick, uint8_t held, uint8_t pressed)
{
    if (!r || r->closed) return;

    /*
     * On n'écrit que les CHANGEMENTS — sauf le tout premier pas, qui pose
     * l'état initial même s'il est nul. Sans cette exception, une partie qui
     * commence touche déjà relâchée n'aurait aucune ligne, et le rejeu ne
     * saurait pas distinguer « rien n'a été enregistré » de « rien n'était
     * pressé ».
     *
     * Les APPUIS, eux, s'écrivent toujours quand il y en a : un appui est un
     * front, pas un état, et deux appuis identiques à deux pas différents sont
     * deux événements distincts.
     */
    const bool changed = (held != r->last_held) || (pressed != 0) || !r->input_started;
    r->last_held = held;
    r->input_started = true;
    /* Noté à CHAQUE pas, changement ou non : c'est la durée de la partie. */
    if (tick > r->last_tick) r->last_tick = tick;
    if (!changed) return;

    if (r->input_count == r->input_capacity) {
        const uint32_t want = r->input_capacity ? r->input_capacity * 2u : 256u;
        /* La même borne que les événements : un journal qui grossit sans fin
         * sur une partie qui tourne mal remplirait la mémoire du joueur. */
        if (want > NS_RUNLOG_MAX_EVENTS) {
            if (r->input_count == NS_RUNLOG_MAX_EVENTS) return;
        }
        ns_run_input *grown = (ns_run_input *)SDL_realloc(r->input, want * sizeof *grown);
        if (!grown) return;
        r->input = grown;
        r->input_capacity = want;
    }
    r->input[r->input_count].tick    = tick;
    r->input[r->input_count].held    = held;
    r->input[r->input_count].pressed = pressed;
    r->input_count++;
}

uint32_t ns_runlog_input_count(const ns_runlog *r) { return r ? r->input_count : 0u; }

const ns_run_input *ns_runlog_inputs(const ns_runlog *r) { return r ? r->input : NULL; }

/*
 * Le journal d'entrées, en mémoire.
 *
 * Rendue publique parce qu'un fantôme s'ENVOIE : le corps d'une requête HTTP se
 * construit en RAM, et passer par un fichier temporaire pour des données qu'on
 * tient déjà serait un aller-retour disque gratuit. L'écriture sur disque
 * (`ns_runlog_write_inputs`) formate désormais par ici, donc il n'existe qu'UN
 * producteur de ce format — celui qu'un fichier `.txt` et un corps HTTP
 * partagent au caractère près.
 */
size_t ns_runlog_format_inputs(const ns_runlog *r, char *out, size_t cap)
{
    if (!r) return 0;

    size_t total = 0;
    char line[160];

    /*
     * Un QUATRIÈME champ dans l'en-tête : le dernier pas de la partie.
     *
     * Il est ajouté, et pas substitué : un journal « v1 <jeu> <difficulté>
     * <graine> » sans lui reste lisible, et `tests/data/rejeu-demineur.txt`
     * n'est pas à réécrire. C'est ce que le lecteur traite comme facultatif.
     *
     * Pourquoi il manquait, et ce que ça cassait : le journal n'écrit que les
     * CHANGEMENTS de commandes — c'est ce qui le garde à quelques kilo-octets —
     * mais du coup un joueur qui lâche les commandes avant de mourir laisse un
     * journal dont la dernière ligne précède la fin de la partie. Le rejeu,
     * qui déduisait la durée de cette dernière ligne, s'arrêtait donc trop tôt.
     * Le score pouvait quand même coïncider ; l'ÉTAT non, et pour un duel les
     * deux machines doivent voir la même partie et pas seulement le même
     * nombre.
     */
    int n = SDL_snprintf(line, sizeof line, "v1 %s %s %lld %d\n",
                         r->game[0] ? r->game : "?",
                         r->difficulty[0] ? r->difficulty : "normal",
                         (long long)r->seed,
                         r->last_tick);
    if (n > 0) {
        if (out && total + (size_t)n < cap) SDL_memcpy(out + total, line, (size_t)n);
        total += (size_t)n;
    }

    for (uint32_t i = 0; i < r->input_count; ++i) {
        n = SDL_snprintf(line, sizeof line, "%d %u %u\n",
                         r->input[i].tick,
                         (unsigned)r->input[i].held,
                         (unsigned)r->input[i].pressed);
        if (n <= 0) continue;
        if (out && total + (size_t)n < cap) SDL_memcpy(out + total, line, (size_t)n);
        total += (size_t)n;
    }

    /* Comme `snprintf` : on termine toujours, même tronqué. */
    if (out && cap) out[(total < cap) ? total : cap - 1] = '\0';
    return total;
}

bool ns_runlog_write_inputs(const ns_runlog *r, const char *path)
{
    if (!r || !path) return false;

    const size_t need = ns_runlog_format_inputs(r, NULL, 0) + 1;
    char *text = (char *)SDL_malloc(need);
    if (!text) return false;
    const size_t len = ns_runlog_format_inputs(r, text, need);

    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (!io) {
        NS_WARN("journal d'entrées : écriture impossible (%s) : %s", path, SDL_GetError());
        SDL_free(text);
        return false;
    }
    SDL_WriteIO(io, text, len);
    SDL_CloseIO(io);
    SDL_free(text);
    NS_INFO("journal d'entrées : %u changement(s) écrits dans « %s »", r->input_count, path);
    return true;
}

void ns_runlog_event(ns_runlog *r, int64_t at_ms, const char *kind, int64_t value)
{
    if (!r || r->closed || !kind || !*kind) return;
    if (r->count >= r->capacity) {
        /* On ne dépasse pas, et on le DIT une fois. Tronquer en silence
         * produirait un journal cohérent mais faux, que le serveur accepterait
         * en recalculant un score plus bas que celui affiché au joueur. */
        if (r->count == r->capacity) {
            NS_WARN("journal de partie plein (%u événements) : la suite est perdue",
                    r->capacity);
            r->count++;   /* pour ne le dire qu'une fois */
        }
        return;
    }
    ns_run_event *e = &r->event[r->count++];
    e->at_ms = at_ms;
    e->value = value;
    SDL_strlcpy(e->kind, kind, sizeof e->kind);
    /* Le séparateur du format canonique ne peut pas apparaître dans un genre. */
    for (char *p = e->kind; *p; ++p) if (*p == '|' || *p == ':') *p = '_';
}

void ns_runlog_end(ns_runlog *r, int64_t duration_ms, int64_t claimed_score)
{
    if (!r) return;
    if (r->count > r->capacity) r->count = r->capacity;   /* le +1 du message */
    r->duration_ms = duration_ms;
    r->claimed = claimed_score;
    r->closed = true;
}

size_t ns_runlog_canonical(const ns_runlog *r, char *out, size_t cap)
{
    if (!r) return 0;
    /*
     * `SDL_snprintf` renvoie la longueur voulue même quand elle dépasse, comme
     * `snprintf` : on avance donc un curseur logique `n` qui peut dépasser `cap`
     * et on n'écrit que tant qu'il reste de la place. C'est ce qui permet à
     * l'appelant de dimensionner son tampon en un seul appel à vide.
     */
    size_t n = 0;
    #define APPEND(...)                                                        \
        do {                                                                   \
            const size_t room = (n < cap) ? (cap - n) : 0;                      \
            n += (size_t)SDL_snprintf(room ? out + n : NULL, room, __VA_ARGS__); \
        } while (0)

    APPEND("v1|%lld|%lld|%lld|%u", (long long)r->seed, (long long)r->duration_ms,
           (long long)r->claimed, r->count);
    for (uint32_t i = 0; i < r->count; ++i) {
        APPEND("|%lld:%s:%lld", (long long)r->event[i].at_ms, r->event[i].kind,
               (long long)r->event[i].value);
    }
    #undef APPEND
    return n;
}

void ns_runlog_set_run_id(ns_runlog *r, const char *run_id)
{
    if (!r) return;
    SDL_snprintf(r->run_id, sizeof r->run_id, "%s", run_id ? run_id : "");
}

const char *ns_runlog_run_id(const ns_runlog *r) { return r ? r->run_id : ""; }

bool ns_runlog_authenticated(const ns_runlog *r) { return r && r->secret_len > 0; }
uint32_t ns_runlog_event_count(const ns_runlog *r) { return r ? r->count : 0u; }

bool ns_runlog_seal(const ns_runlog *r, char *out, size_t cap)
{
    if (!r || !out || cap < 45) return false;      /* 32 octets -> 43 caractères */
    if (r->secret_len == 0) return false;

    const size_t need = ns_runlog_canonical(r, NULL, 0) + 1;
    char *payload = (char *)SDL_malloc(need);
    if (!payload) return false;
    ns_runlog_canonical(r, payload, need);

    uint8_t mac[32];
    ns_hmac_sha256(r->secret, r->secret_len, payload, SDL_strlen(payload), mac);
    SDL_free(payload);

    ns_base64_raw(mac, sizeof mac, out, cap);
    return true;
}

/* -------------------------------------------------------------------------- */

const char *ns_runlog_queue_dir(void)
{
    if (g_queue_override[0]) return g_queue_override;
    if (!g_queue_dir[0]) {
        const char *dir = ns_path_user_dir();
        SDL_snprintf(g_queue_dir, sizeof g_queue_dir, "%sruns", dir ? dir : "");
    }
    return g_queue_dir;
}

void ns_runlog_set_queue_dir(const char *dir)
{
    if (dir && *dir) SDL_strlcpy(g_queue_override, dir, sizeof g_queue_override);
    else             g_queue_override[0] = '\0';
}

static void json_escape(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 2 < cap; ++p) {
        if (*p == '"' || *p == '\\') { out[n++] = '\\'; out[n++] = *p; }
        else if ((unsigned char)*p >= 0x20) out[n++] = *p;
    }
    out[n] = '\0';
}


/*
 * L'analyseur du journal d'entrées, et le SEUL.
 *
 * Il lit maintenant deux sources qui n'ont pas le même niveau de confiance :
 * un fichier que le joueur a sous la main, et le corps d'une réponse HTTP —
 * c'est-à-dire le fantôme de quelqu'un d'autre, servi par un serveur qui peut
 * mentir. Il est donc écrit pour la seconde, ce qui le rend correct pour la
 * première par la même occasion :
 *
 *  - la LONGUEUR fait foi, jamais un octet nul final que rien ne garantit dans
 *    un corps HTTP ;
 *  - pas de `strtok`, qui écrit dans son entrée — on ne modifie pas le tampon
 *    reçu, et deux appels concurrents ne se marchent pas dessus ;
 *  - `%31s` sur `g[32]` et `%15s` sur `d[16]`, sur une copie de ligne bornée,
 *    parce que `sscanf` sans largeur est la faute de NIN-09 ;
 *  - un plafond de lignes, sans quoi un fantôme de 256 Kio n'aurait de limite
 *    que la mémoire de la machine ;
 *  - une ligne illisible est SAUTÉE, pas fatale : un journal tronqué rejoue ce
 *    qu'il a. C'est le comportement voulu — on préfère un duel court à un duel
 *    qui n'a pas lieu.
 */
bool ns_runlog_parse_inputs(const char *text, size_t len,
                            char *game, size_t game_cap,
                            char *difficulty, size_t difficulty_cap,
                            int64_t *seed,
                            ns_run_input **out_input, uint32_t *out_count)
{
    if (out_input) *out_input = NULL;
    if (out_count) *out_count = 0;
    if (!text || len == 0 || !out_input || !out_count) return false;

    /*
     * Une borne dure sur le nombre d'entrées. Une partie de trois minutes fait
     * 21 600 pas et n'enregistre qu'un CHANGEMENT par ligne ; 200 000 couvre
     * une partie de vingt-cinq minutes où l'on changerait de commande à chaque
     * pas. Au-delà, ce n'est plus une partie, c'est un tampon qu'on nous
     * demande d'allouer.
     */
    const uint32_t MAX_INPUTS = 200000u;

    /* La ligne d'en-tête, copiée bornée pour être terminée proprement. */
    size_t head = 0;
    while (head < len && text[head] != '\n') head++;

    char header[128];
    const size_t hcopy = (head < sizeof header - 1) ? head : sizeof header - 1;
    SDL_memcpy(header, text, hcopy);
    header[hcopy] = '\0';

    char g[32] = { 0 }, d[16] = { 0 };
    long long sd = 0;
    int hdr_last_tick = -1;
    /*
     * L'en-tête est lu AVANT tout le reste, et son échec est fatal : rejouer un
     * journal dont on ne connaît ni le jeu ni la graine ne rejouerait rien —
     * ça jouerait une partie neuve avec de vieux boutons.
     *
     * Le quatrième champ — le dernier pas de la partie — est FACULTATIF : les
     * journaux écrits avant qu'il existe n'en ont pas, et ils doivent continuer
     * à se relire. `sscanf` rend le nombre de champs convertis, ce qui distingue
     * les deux sans avoir à renifler la ligne.
     */
    const int fields = SDL_sscanf(header, "v1 %31s %15s %lld %d", g, d, &sd, &hdr_last_tick);
    if (fields < 3) {
        NS_ERROR("journal d'entrées : en-tête « v1 <jeu> <difficulté> <graine> » attendu");
        return false;
    }
    if (fields < 4) hdr_last_tick = -1;
    if (game && game_cap) SDL_snprintf(game, game_cap, "%s", g);
    if (difficulty && difficulty_cap) SDL_snprintf(difficulty, difficulty_cap, "%s", d);
    if (seed) *seed = (int64_t)sd;

    /* Une passe pour compter, une pour lire : quelques kilo-octets de texte, et
     * deux passes valent mieux qu'un tableau qui grandit à tâtons. */
    uint32_t lines = 0;
    for (size_t i = head; i < len; ++i) if (text[i] == '\n') lines++;
    /* Une dernière ligne sans retour final compte aussi : sans ce +1, le
     * dernier changement d'un journal qui ne finit pas par « \n » était perdu. */
    if (len > head && text[len - 1] != '\n') lines++;
    if (lines > MAX_INPUTS) lines = MAX_INPUTS;

    /* Une place de plus pour la ligne de FIN qu'on peut avoir à synthétiser. */
    ns_run_input *arr = (ns_run_input *)SDL_calloc(lines + 1u, sizeof *arr);
    if (!arr) return false;

    uint32_t n = 0;
    size_t i = head;
    while (i < len && n < lines) {
        if (text[i] == '\n') { i++; continue; }

        size_t end = i;
        while (end < len && text[end] != '\n') end++;

        char line[64];
        const size_t lcopy = ((end - i) < sizeof line - 1) ? (end - i) : sizeof line - 1;
        SDL_memcpy(line, text + i, lcopy);
        line[lcopy] = '\0';

        int tick = 0; unsigned held = 0, pressed = 0;
        if (SDL_sscanf(line, "%d %u %u", &tick, &held, &pressed) == 3) {
            arr[n].tick = tick;
            /* Les masques ne portent que NS_GAME_BUTTON_COUNT bits utiles. Un
             * octet reçu du réseau peut en porter huit ; les tronquer ici évite
             * qu'un bit inventé atteigne un jeu. */
            arr[n].held    = (uint8_t)(held    & 0x1Fu);
            arr[n].pressed = (uint8_t)(pressed & 0x1Fu);
            n++;
        }
        i = end;
    }

    /*
     * La ligne de FIN, synthétisée.
     *
     * Si l'en-tête dit que la partie a duré jusqu'au pas N et que le dernier
     * changement de commandes est antérieur, on ajoute une entrée à N qui
     * REPREND les maintiens en cours, sans aucun appui. C'est exactement ce que
     * le joueur a fait pendant ce temps-là : rien.
     *
     * Elle est ajoutée ICI plutôt que dans chaque rejoueur, et c'est le point :
     * tous les consommateurs — `--rejouer=`, le fantôme d'un duel, les tests —
     * déduisent la durée du dernier élément du tableau. Leur donner la bonne
     * dernière entrée corrige les trois d'un coup, sans changer une signature
     * ni demander à personne de se souvenir d'une règle de plus.
     */
    if (hdr_last_tick >= 0 && n < lines + 1u
        && (n == 0 || arr[n - 1].tick < hdr_last_tick)) {
        arr[n].tick    = hdr_last_tick;
        arr[n].held    = (n > 0) ? arr[n - 1].held : 0u;
        arr[n].pressed = 0u;
        n++;
    }

    *out_input = arr;
    *out_count = n;
    NS_INFO("journal d'entrées : %u changement(s) relus (%s/%s, graine %lld)", n, g, d, sd);
    return true;
}

bool ns_runlog_read_inputs(const char *path,
                           char *game, size_t game_cap,
                           char *difficulty, size_t difficulty_cap,
                           int64_t *seed,
                           ns_run_input **out_input, uint32_t *out_count)
{
    if (out_input) *out_input = NULL;
    if (out_count) *out_count = 0;
    if (!path) return false;

    size_t size = 0;
    void *file = SDL_LoadFile(path, &size);
    if (!file || size == 0) {
        NS_ERROR("journal d'entrées illisible : %s", path);
        SDL_free(file);
        return false;
    }

    const bool ok = ns_runlog_parse_inputs((const char *)file, size,
                                           game, game_cap,
                                           difficulty, difficulty_cap,
                                           seed, out_input, out_count);
    SDL_free(file);
    if (!ok) NS_ERROR("« %s » : journal d'entrées illisible", path);
    return ok;
}

bool ns_runlog_enqueue(const ns_runlog *r)
{
    if (!r || !r->closed) return false;
    if (!ns_runlog_authenticated(r)) {
        /* Hors ligne : la partie est gardée par `ns_scores`, et c'est tout. La
         * mettre en file la condamnerait à un « sceau invalide » à la première
         * tentative d'envoi, et remplirait le disque de parties irrecevables. */
        return false;
    }
    if (!r->run_id[0]) {
        /* Un sceau sans identifiant n'a aucune adresse : `/runs/{id}/submit`
         * demande les deux. Ça ne devrait pas arriver — un secret vient
         * toujours avec un identifiant — mais le vérifier ici coûte une ligne
         * et évite d'écrire un fichier que rien ne saura envoyer. */
        NS_WARN("file d'attente : partie scellée sans identifiant, ignorée");
        return false;
    }

    const char *dir = ns_runlog_queue_dir();
    if (!SDL_CreateDirectory(dir)) {
        NS_WARN("file d'attente : « %s » inaccessible : %s", dir, SDL_GetError());
        return false;
    }

    char seal[64];
    if (!ns_runlog_seal(r, seal, sizeof seal)) return false;

    /*
     * Un fichier par partie, nommé par l'horloge à la nanoseconde. Deux parties
     * ne peuvent donc pas s'écraser, une écriture interrompue n'abîme que la
     * sienne, et l'ordre alphabétique est l'ordre chronologique — ce qui donne
     * l'ordre d'envoi sans avoir à lire les fichiers.
     */
    char path[1152];
    /* La barre oblique convient aussi sur Windows : SDL et l'API Win32 
     * l'acceptent toutes deux, et mélanger les séparateurs selon la plateforme
     * n'apporterait ici qu'une occasion de se tromper. */
    SDL_Time now = 0;
    (void)SDL_GetCurrentTime(&now);
    SDL_snprintf(path, sizeof path, "%s/%020lld.json", dir, (long long)now);

    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (!io) {
        NS_WARN("file d'attente : écriture impossible (%s) : %s", path, SDL_GetError());
        return false;
    }

    char buf[1024], esc[64];
    int len = SDL_snprintf(buf, sizeof buf, "{\"game\":\"");
    SDL_WriteIO(io, buf, (size_t)len);
    json_escape(r->game, esc, sizeof esc);
    SDL_WriteIO(io, esc, SDL_strlen(esc));
    len = SDL_snprintf(buf, sizeof buf, "\",\"difficulty\":\"");
    SDL_WriteIO(io, buf, (size_t)len);
    json_escape(r->difficulty, esc, sizeof esc);
    SDL_WriteIO(io, esc, SDL_strlen(esc));

    /*
     * UNE ENVELOPPE, et à l'intérieur la soumission telle que le serveur
     * l'attend — pas l'inverse.
     *
     * La première version mettait tout à plat : `game`, `difficulty`, `runId`,
     * `seed` à côté de `events`, `claimedScore`, `durationMs` et `seal`. Ça
     * paraissait plus simple, et ça ne pouvait pas marcher : `decodeJSON`
     * appelle `dec.DisallowUnknownFields()` (`server/internal/api/api.go:701`),
     * donc les quatre premiers champs auraient fait répondre 400 — un 4xx, donc
     * un fichier supprimé. Toutes les parties de la file auraient été jetées
     * une par une, sans qu'aucune n'arrive, et le journal aurait seulement dit
     * « partie refusée (400) ».
     *
     * Le contenu de `submission` est donc EXACTEMENT `runs.Submission`, et il
     * part tel quel : `ns_runlog_queue_read` en rend les octets sans les
     * reconstruire. Le reste — l'identifiant qui forme l'URL, la graine, le jeu
     * — est de l'enveloppe : ce dont le transport a besoin pour savoir où
     * envoyer, et ce dont un humain a besoin pour lire le fichier.
     */
    len = SDL_snprintf(buf, sizeof buf,
                       "\",\"runId\":\"%s\",\"seed\":%lld,\"submission\":{"
                       "\"claimedScore\":%lld,\"durationMs\":%lld,"
                       "\"seal\":\"%s\",\"events\":[",
                       r->run_id, (long long)r->seed, (long long)r->claimed,
                       (long long)r->duration_ms, seal);
    SDL_WriteIO(io, buf, (size_t)len);

    for (uint32_t i = 0; i < r->count; ++i) {
        json_escape(r->event[i].kind, esc, sizeof esc);
        len = SDL_snprintf(buf, sizeof buf, "%s{\"t\":%lld,\"k\":\"%s\",\"v\":%lld}",
                           i ? "," : "", (long long)r->event[i].at_ms, esc,
                           (long long)r->event[i].value);
        SDL_WriteIO(io, buf, (size_t)len);
    }
    len = SDL_snprintf(buf, sizeof buf, "]}}\n");
    SDL_WriteIO(io, buf, (size_t)len);
    SDL_CloseIO(io);

    NS_INFO("partie mise en file : %u événement(s), score %lld — « %s »",
            r->count, (long long)r->claimed, path);
    return true;
}

uint32_t ns_runlog_pending(void)
{
    int count = 0;
    char **files = SDL_GlobDirectory(ns_runlog_queue_dir(), "*.json", 0, &count);
    if (!files) return 0;
    SDL_free(files);
    return (uint32_t)(count < 0 ? 0 : count);
}

/* ==========================================================================
 * Relecture d'un fichier de file
 * ==========================================================================
 * Le transport a besoin de deux choses : l'IDENTIFIANT, pour former l'URL, et
 * le CORPS, à poster tel quel.
 *
 * Le corps n'est pas le fichier : c'est la valeur de `submission`, et rien
 * d'autre. On la découpe par ses bornes d'octets plutôt que de la réécrire —
 * un sceau porte sur des octets précis, et re-sérialiser serait le seul moyen
 * d'en changer un sans s'en apercevoir.
 * ========================================================================== */

bool ns_runlog_queue_read(const char *path, char *run_id, size_t run_id_cap,
                          char **body, size_t *body_len)
{
    if (run_id && run_id_cap) run_id[0] = '\0';
    if (body) *body = NULL;
    if (body_len) *body_len = 0;
    if (!path || !body || !body_len) return false;

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data || size == 0) {
        SDL_free(data);
        return false;
    }

    ns_arena arena;
    if (!ns_arena_init(&arena, 256u * 1024u, "json file d'attente")) {
        SDL_free(data);
        return false;
    }

    bool ok = false;
    ns_json doc;
    if (ns_json_parse(&doc, (const char *)data, size, &arena)) {
        const ns_json_value *root = ns_json_root(&doc);
        char id[64] = { 0 };
        ns_json_get_string(&doc, root, "runId", id, sizeof id);

        size_t from = 0, to = 0;
        const ns_json_value *sub = ns_json_get(&doc, root, "submission");
        if (id[0] && ns_json_span(&doc, sub, &from, &to) && to > from) {
            const size_t n = to - from;
            char *copy = (char *)SDL_malloc(n + 1);
            if (copy) {
                SDL_memcpy(copy, (const char *)data + from, n);
                copy[n] = '\0';
                if (run_id && run_id_cap) {
                    SDL_snprintf(run_id, run_id_cap, "%s", id);
                }
                *body = copy;
                *body_len = n;
                ok = true;
            }
        }
    }

    ns_arena_free(&arena);
    SDL_free(data);
    return ok;
}
