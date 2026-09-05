/*
 * ns_lockstep.c — la liaison persistante d'un duel en direct.
 *
 * Le protocole, en entier
 * -----------------------
 * Binaire, petit-boutiste, chaque trame précédée de sa longueur :
 *
 *     uint16 longueur de la charge | uint8 type | charge
 *
 * La longueur ne couvre pas les trois octets d'en-tête, et elle est bornée à
 * `FRAME_MAX` : un pair qui annonce plus est un pair qu'on ferme, sans allouer.
 * C'est la première règle d'un décodeur qui lit une socket.
 *
 *   0x01 HELLO   uint64 duel | uint8 slot | char jeu[16] | char diff[8]
 *   0x02 START   uint64 graine            (relais -> les deux)
 *   0x03 INPUT   int32 pas | uint8 tenus | uint8 appuyés
 *   0x04 HASH    int32 pas | uint64 empreinte
 *   0x05 BYE     uint8 motif
 *   0x06 DESYNC  int32 pas
 *
 * Six types, trois octets d'en-tête, dix octets pour l'entrée d'un pas : à
 * 120 Hz cela fait 1,2 kio/s par joueur. Un protocole texte aurait triplé ça
 * pour rien.
 *
 * Le tampon d'entrées
 * -------------------
 * Un anneau indexé par pas modulo `RING`. `RING` vaut 256, soit deux secondes à
 * 120 Hz : bien plus que le retard de huit pas, et assez pour absorber une
 * rafale. Chaque case porte le pas qu'elle contient, ce qui rend une case
 * périmée indiscernable d'une case vide — sans ce champ, un anneau qui a fait
 * un tour rend l'entrée d'il y a deux secondes en la croyant fraîche.
 */
#include "ns_lockstep.h"
#include "ns_core.h"

#include <SDL3/SDL.h>

#include <string.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET ns_lsock;
    #define NS_LS_INVALID INVALID_SOCKET
    #define ns_ls_close closesocket
    #define ns_ls_would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
    typedef int ns_lsock;
    #define NS_LS_INVALID (-1)
    #define ns_ls_close close
    #define ns_ls_would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

#define FRAME_MAX   512
#define RING        256

#define F_HELLO  0x01u
#define F_START  0x02u
#define F_INPUT  0x03u
#define F_HASH   0x04u
#define F_BYE    0x05u
#define F_DESYNC 0x06u

typedef struct slot_input {
    int32_t tick;        /* le pas que porte la case, ou -1 */
    uint8_t held, pressed;
} slot_input;

typedef struct slot_hash {
    int32_t  tick;
    uint64_t hash;
} slot_hash;

struct ns_lockstep {
    ns_lsock          sock;
    ns_lockstep_state state;
    char              error[160];

    uint64_t seed;
    int32_t  desync_tick;

    slot_input peer_in[RING];
    slot_hash  peer_hash[RING];
    slot_hash  own_hash[RING];

    /* Tampon de réception : une trame peut arriver en deux morceaux. TCP est un
     * flux, pas une file de messages — l'oublier est le défaut classique. */
    uint8_t  rx[FRAME_MAX * 4];
    size_t   rx_len;

    uint32_t frames_in, frames_out, stalls;
};

/* ==========================================================================
 * Petit-boutiste explicite
 * ==========================================================================
 * Écrit et lu octet par octet plutôt que par `memcpy` d'un entier : le duel
 * peut opposer deux machines d'ordre différent, et c'est justement le genre de
 * détail qui ne se voit qu'une fois qu'un joueur perd une partie.
 */
static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* ==========================================================================
 * Socket
 * ========================================================================== */

static void set_nonblocking(ns_lsock s)
{
#if defined(_WIN32)
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    if (fl >= 0) fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* `TCP_NODELAY` n'est pas une optimisation ici, c'est une CONDITION. L'algorithme
 * de Nagle retient un petit envoi jusqu'à ce que l'acquittement du précédent
 * arrive : sur des trames de dix octets à 120 Hz, il ajouterait des dizaines de
 * millisecondes de latence à chaque pas, c'est-à-dire exactement ce que le
 * retard de huit pas est censé absorber. */
static void set_nodelay(ns_lsock s)
{
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
}

static bool send_all(ns_lockstep *ls, const uint8_t *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
#if defined(_WIN32)
        const int n = send(ls->sock, (const char *)data + sent, (int)(len - sent), 0);
#else
        const ssize_t n = send(ls->sock, data + sent, len - sent, 0);
#endif
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && ns_ls_would_block()) {
            /* Le tampon d'émission est plein : on attend un instant plutôt que
             * de tourner à vide. Une trame fait dix octets, ça n'arrive que si
             * le pair a cessé de lire — auquel cas le délai suivant tranchera. */
            SDL_Delay(1);
            continue;
        }
        SDL_snprintf(ls->error, sizeof ls->error, "envoi interrompu");
        ls->state = NS_LOCKSTEP_ERROR;
        return false;
    }
    return true;
}

static bool send_frame(ns_lockstep *ls, uint8_t type,
                       const uint8_t *payload, uint16_t len)
{
    if (!ls || ls->sock == NS_LS_INVALID) return false;
    if (len > FRAME_MAX) return false;
    uint8_t head[3];
    put_u16(head, len);
    head[2] = type;
    if (!send_all(ls, head, 3)) return false;
    if (len && !send_all(ls, payload, len)) return false;
    ls->frames_out++;
    return true;
}

/* ==========================================================================
 * Ouverture
 * ========================================================================== */

ns_lockstep *ns_lockstep_connect(const char *host, uint16_t port,
                                 uint64_t duel_id, int slot,
                                 const char *game, const char *difficulty,
                                 uint32_t timeout_ms,
                                 char *error, size_t error_size)
{
    if (error && error_size) error[0] = '\0';
    if (!host || !host[0] || slot < 0 || slot > 1) {
        if (error) SDL_snprintf(error, error_size, "paramètres de duel invalides");
        return NULL;
    }

    char portstr[8];
    SDL_snprintf(portstr, sizeof portstr, "%u", (unsigned)port);

    struct addrinfo hints;
    SDL_zero(hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        if (error) SDL_snprintf(error, error_size, "hôte introuvable : %s", host);
        return NULL;
    }

    ns_lsock s = NS_LS_INVALID;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == NS_LS_INVALID) continue;
        /* La connexion, elle, reste bloquante et bornée : passer en non
         * bloquant AVANT `connect` obligerait à surveiller l'écriture, pour
         * gagner quelques millisecondes une fois par duel. */
#if !defined(_WIN32)
        struct timeval tv;
        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#else
        DWORD ms = timeout_ms;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof ms);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
#endif
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        ns_ls_close(s);
        s = NS_LS_INVALID;
    }
    freeaddrinfo(res);

    if (s == NS_LS_INVALID) {
        if (error) SDL_snprintf(error, error_size, "connexion refusée par %s:%u",
                                host, (unsigned)port);
        return NULL;
    }

    ns_lockstep *ls = (ns_lockstep *)SDL_calloc(1, sizeof *ls);
    if (!ls) { ns_ls_close(s); return NULL; }
    ls->sock = s;
    ls->state = NS_LOCKSTEP_WAITING;
    ls->desync_tick = -1;
    for (int i = 0; i < RING; ++i) {
        ls->peer_in[i].tick = -1;
        ls->peer_hash[i].tick = -1;
        ls->own_hash[i].tick = -1;
    }
    set_nodelay(s);

    uint8_t hello[8 + 1 + 16 + 8];
    SDL_memset(hello, 0, sizeof hello);
    put_u64(hello, duel_id);
    hello[8] = (uint8_t)slot;
    if (game) SDL_strlcpy((char *)hello + 9, game, 16);
    if (difficulty) SDL_strlcpy((char *)hello + 25, difficulty, 8);
    if (!send_frame(ls, F_HELLO, hello, (uint16_t)sizeof hello)) {
        if (error) SDL_snprintf(error, error_size, "%s", ls->error);
        ns_ls_close(s);
        SDL_free(ls);
        return NULL;
    }

    /* Le passage en non bloquant vient APRÈS le HELLO : la boucle de jeu ne
     * doit jamais attendre une socket, mais l'ouverture, elle, a le droit. */
    set_nonblocking(s);
    return ls;
}

void ns_lockstep_close(ns_lockstep *ls)
{
    if (!ls) return;
    if (ls->sock != NS_LS_INVALID) ns_ls_close(ls->sock);
    SDL_free(ls);
}

/* ==========================================================================
 * Réception
 * ========================================================================== */

static void note_desync(ns_lockstep *ls, int32_t tick)
{
    if (ls->state == NS_LOCKSTEP_DESYNC) return;
    ls->state = NS_LOCKSTEP_DESYNC;
    ls->desync_tick = tick;
    SDL_snprintf(ls->error, sizeof ls->error,
                 "les deux parties ont diverge au pas %d", (int)tick);
    NS_WARN("duel : divergence au pas %d — la partie est rompue plutôt que "
            "poursuivie a l'aveugle", (int)tick);
    uint8_t p[4];
    put_u32(p, (uint32_t)tick);
    (void)send_frame(ls, F_DESYNC, p, 4);
}

static void handle_frame(ns_lockstep *ls, uint8_t type,
                         const uint8_t *p, uint16_t len)
{
    ls->frames_in++;
    switch (type) {
        case F_START:
            if (len >= 8) {
                ls->seed = get_u64(p);
                if (ls->state == NS_LOCKSTEP_WAITING) ls->state = NS_LOCKSTEP_RUNNING;
            }
            break;

        case F_INPUT:
            if (len >= 6) {
                const int32_t tick = (int32_t)get_u32(p);
                if (tick >= 0) {
                    slot_input *sl = &ls->peer_in[tick % RING];
                    sl->tick = tick;
                    sl->held = p[4];
                    sl->pressed = p[5];
                }
            }
            break;

        case F_HASH:
            if (len >= 12) {
                const int32_t tick = (int32_t)get_u32(p);
                const uint64_t h = get_u64(p + 4);
                if (tick >= 0) {
                    slot_hash *sl = &ls->peer_hash[tick % RING];
                    sl->tick = tick;
                    sl->hash = h;
                    /* On ne compare RIEN ici : la comparaison appartient à
                     * l'appelant, qui seul sait si l'empreinte reçue doit
                     * ressembler à sa propre partie ou au fantôme qu'il tient
                     * de l'adversaire. Voir `ns_lockstep_verify_peer`. */
                }
            }
            break;

        case F_DESYNC:
            if (len >= 4) note_desync(ls, (int32_t)get_u32(p));
            break;

        case F_BYE:
            if (ls->state == NS_LOCKSTEP_RUNNING || ls->state == NS_LOCKSTEP_WAITING) {
                ls->state = NS_LOCKSTEP_ENDED;
            }
            break;

        default:
            /* Un type inconnu est IGNORÉ, pas fatal : c'est ce qui permettra
             * d'ajouter une trame sans casser les clients déjà installés. Sa
             * longueur est déclarée, donc on sait toujours la sauter. */
            break;
    }
}

void ns_lockstep_poll(ns_lockstep *ls)
{
    if (!ls || ls->sock == NS_LS_INVALID) return;
    if (ls->state == NS_LOCKSTEP_ERROR) return;

    for (;;) {
        if (ls->rx_len >= sizeof ls->rx) {
            /* Impossible tant que FRAME_MAX < sizeof rx, mais le vérifier coûte
             * une comparaison et évite d'écrire hors du tampon si jamais une
             * borne changeait. */
            SDL_snprintf(ls->error, sizeof ls->error, "tampon de reception plein");
            ls->state = NS_LOCKSTEP_ERROR;
            return;
        }
#if defined(_WIN32)
        const int n = recv(ls->sock, (char *)ls->rx + ls->rx_len,
                           (int)(sizeof ls->rx - ls->rx_len), 0);
#else
        const ssize_t n = recv(ls->sock, ls->rx + ls->rx_len,
                               sizeof ls->rx - ls->rx_len, 0);
#endif
        if (n > 0) { ls->rx_len += (size_t)n; continue; }
        if (n == 0) {
            if (ls->state == NS_LOCKSTEP_RUNNING || ls->state == NS_LOCKSTEP_WAITING) {
                ls->state = NS_LOCKSTEP_ENDED;
            }
            break;
        }
        if (ns_ls_would_block()) break;
        SDL_snprintf(ls->error, sizeof ls->error, "liaison interrompue");
        ls->state = NS_LOCKSTEP_ERROR;
        return;
    }

    /* Découpage : TCP est un FLUX. Une trame peut arriver en deux morceaux, et
     * deux trames en un seul. */
    size_t off = 0;
    while (ls->rx_len - off >= 3) {
        const uint16_t len = get_u16(ls->rx + off);
        if (len > FRAME_MAX) {
            SDL_snprintf(ls->error, sizeof ls->error,
                         "trame de %u octets annoncee, maximum %d", len, FRAME_MAX);
            ls->state = NS_LOCKSTEP_ERROR;
            return;
        }
        if (ls->rx_len - off < (size_t)len + 3) break;    /* pas encore entière */
        handle_frame(ls, ls->rx[off + 2], ls->rx + off + 3, len);
        off += (size_t)len + 3;
    }
    if (off) {
        SDL_memmove(ls->rx, ls->rx + off, ls->rx_len - off);
        ls->rx_len -= off;
    }
}

/* ==========================================================================
 * Interface du pas
 * ========================================================================== */

ns_lockstep_state ns_lockstep_status(const ns_lockstep *ls)
{
    return ls ? ls->state : NS_LOCKSTEP_OFF;
}

const char *ns_lockstep_error(const ns_lockstep *ls)
{
    return (ls && ls->error[0]) ? ls->error : "";
}

uint64_t ns_lockstep_seed(const ns_lockstep *ls) { return ls ? ls->seed : 0; }

int32_t ns_lockstep_desync_tick(const ns_lockstep *ls)
{
    return ls ? ls->desync_tick : -1;
}

void ns_lockstep_send_input(ns_lockstep *ls, int32_t tick,
                            uint8_t held, uint8_t pressed)
{
    if (!ls || tick < 0) return;
    uint8_t p[6];
    put_u32(p, (uint32_t)tick);
    p[4] = held;
    p[5] = pressed;
    (void)send_frame(ls, F_INPUT, p, 6);
}

bool ns_lockstep_peer_input(const ns_lockstep *ls, int32_t tick,
                            uint8_t *held, uint8_t *pressed)
{
    if (!ls || tick < 0) return false;
    const slot_input *sl = &ls->peer_in[tick % RING];
    if (sl->tick != tick) return false;      /* absente, ou périmée d'un tour */
    if (held) *held = sl->held;
    if (pressed) *pressed = sl->pressed;
    return true;
}

void ns_lockstep_publish_hash(ns_lockstep *ls, int32_t tick, uint64_t hash)
{
    if (!ls || tick < 0) return;
    slot_hash *mine = &ls->own_hash[tick % RING];
    mine->tick = tick;
    mine->hash = hash;

    uint8_t p[12];
    put_u32(p, (uint32_t)tick);
    put_u64(p + 4, hash);
    (void)send_frame(ls, F_HASH, p, 12);
}

void ns_lockstep_verify_peer(ns_lockstep *ls, int32_t tick, uint64_t expected)
{
    if (!ls || tick < 0) return;
    const slot_hash *peer = &ls->peer_hash[tick % RING];
    /* Une empreinte absente n'est pas une divergence : le paquet peut être en
     * route. On ne conclut jamais d'un silence. */
    if (peer->tick != tick) return;
    if (peer->hash != expected) note_desync(ls, tick);
}

void ns_lockstep_say_bye(ns_lockstep *ls)
{
    if (!ls) return;
    uint8_t p[1] = { 0 };
    (void)send_frame(ls, F_BYE, p, 1);
}

void ns_lockstep_stats(const ns_lockstep *ls, uint32_t *frames_in,
                       uint32_t *frames_out, uint32_t *stalls)
{
    if (frames_in)  *frames_in  = ls ? ls->frames_in : 0;
    if (frames_out) *frames_out = ls ? ls->frames_out : 0;
    if (stalls)     *stalls     = ls ? ls->stalls : 0;
}
