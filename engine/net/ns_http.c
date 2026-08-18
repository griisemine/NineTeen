/* ns_http.c — voir ns_http.h pour ce que ce client est et n'est pas. */

/*
 * AVANT tout en-tête : le projet compile en `-std=c11` strict, ce qui masque
 * les déclarations POSIX. Sans ça, `getaddrinfo` et `struct addrinfo` n'existent
 * pas — et en C, une fonction non déclarée qui rend un pointeur est le genre de
 * chose qui compile ailleurs et casse ici.
 */
#if !defined(_WIN32)
    #define _POSIX_C_SOURCE 200809L
    #if defined(__APPLE__)
        #define _DARWIN_C_SOURCE 1
    #endif
#endif

#include "ns_http.h"

#include "ns_core.h"

#include <SDL3/SDL.h>

#include <string.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET ns_socket;
    #define NS_INVALID_SOCKET INVALID_SOCKET
    #define ns_close_socket closesocket
#else
    #include <errno.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
    typedef int ns_socket;
    #define NS_INVALID_SOCKET (-1)
    #define ns_close_socket close
#endif

/* ==========================================================================
 * Découpe d'URL
 * ========================================================================== */

bool ns_http_parse_url(const char *url, char *host, size_t host_size,
                       uint16_t *port, char *path, size_t path_size,
                       char *error, size_t error_size)
{
    if (error && error_size) error[0] = '\0';
    if (!url || !host || !port || !path) return false;

    if (SDL_strncasecmp(url, "https://", 8) == 0) {
        /*
         * REFUSÉ, et pas tenté en clair. Un `https://` traité comme du HTTP
         * enverrait le jeton de session en clair sur un port qui ne le
         * comprendra pas : on échoue bruyamment plutôt que discrètement.
         */
        if (error && error_size) {
            SDL_snprintf(error, error_size,
                         "https non pris en charge : mettre un proxy TLS devant le serveur");
        }
        return false;
    }
    const char *rest = url;
    if (SDL_strncasecmp(rest, "http://", 7) == 0) rest += 7;

    *port = 80;

    const char *slash = SDL_strchr(rest, '/');
    const char *authority_end = slash ? slash : (rest + SDL_strlen(rest));

    const char *colon = NULL;
    for (const char *p = rest; p < authority_end; ++p) if (*p == ':') colon = p;

    const size_t host_len = (size_t)((colon ? colon : authority_end) - rest);
    if (host_len == 0 || host_len + 1 > host_size) {
        if (error && error_size) SDL_snprintf(error, error_size, "hôte absent ou trop long");
        return false;
    }
    SDL_memcpy(host, rest, host_len);
    host[host_len] = '\0';

    if (colon) {
        const int p = SDL_atoi(colon + 1);
        if (p <= 0 || p > 65535) {
            if (error && error_size) SDL_snprintf(error, error_size, "port invalide");
            return false;
        }
        *port = (uint16_t)p;
    }

    SDL_snprintf(path, path_size, "%s", slash ? slash : "/");
    return true;
}

/* ==========================================================================
 * Socket
 * ========================================================================== */

static bool sock_send_all(ns_socket s, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
#if defined(_WIN32)
        const int n = send(s, data + sent, (int)(len - sent), 0);
#else
    #if defined(MSG_NOSIGNAL)
        const ssize_t n = send(s, data + sent, len - sent, MSG_NOSIGNAL);
    #else
        /* macOS n'a pas MSG_NOSIGNAL : c'est `SO_NOSIGPIPE`, posé sur la socket
         * juste après sa création, qui empêche le SIGPIPE. */
        const ssize_t n = send(s, data + sent, len - sent, 0);
    #endif
#endif
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static void set_timeout(ns_socket s, uint32_t ms)
{
#if defined(_WIN32)
    DWORD tv = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
#else
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000u);
    tv.tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

/* ==========================================================================
 * Requête
 * ========================================================================== */

void ns_http_response_free(ns_http_response *r)
{
    if (!r) return;
    SDL_free(r->body);
    r->body = NULL;
    r->length = 0;
}

bool ns_http_request(const char *method, const char *url,
                     const char *content_type, const char *bearer,
                     const void *body, size_t body_len,
                     uint32_t timeout_ms, ns_http_response *out)
{
    if (!out) return false;
    SDL_zerop(out);
    if (!method || !url) {
        SDL_snprintf(out->error, sizeof out->error, "requête incomplète");
        return false;
    }

    char host[256], path[1024];
    uint16_t port = 80;
    if (!ns_http_parse_url(url, host, sizeof host, &port, path, sizeof path,
                           out->error, sizeof out->error)) {
        return false;
    }

#if defined(_WIN32)
    /* Winsock veut être réveillé une fois par processus. `WSAStartup` est
     * réentrant et compté, donc l'appeler ici est sans danger. */
    WSADATA wsa;
    static SDL_AtomicInt started;
    if (SDL_CompareAndSwapAtomicInt(&started, 0, 1)) WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    char port_text[8];
    SDL_snprintf(port_text, sizeof port_text, "%u", (unsigned)port);

    struct addrinfo hints, *res = NULL;
    SDL_zero(hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_text, &hints, &res) != 0 || !res) {
        SDL_snprintf(out->error, sizeof out->error, "hôte introuvable : %s", host);
        return false;
    }

    ns_socket s = NS_INVALID_SOCKET;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == NS_INVALID_SOCKET) continue;
        set_timeout(s, timeout_ms);
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        ns_close_socket(s);
        s = NS_INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (s == NS_INVALID_SOCKET) {
        SDL_snprintf(out->error, sizeof out->error, "connexion refusée : %s:%u", host, port);
        return false;
    }

    {
        const int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    #if !defined(_WIN32) && defined(SO_NOSIGPIPE)
        /* Sans ça, un serveur qui raccroche pendant l'émission TUE le processus
         * par SIGPIPE — un score perdu deviendrait un jeu qui disparaît. */
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
    #endif
    }

    /* --- l'en-tête ------------------------------------------------------- */
    char head[2048];
    int n = SDL_snprintf(head, sizeof head,
                         "%s %s HTTP/1.1\r\n"
                         "Host: %s:%u\r\n"
                         "User-Agent: nineteen\r\n"
                         "Connection: close\r\n"
                         "Accept: application/json\r\n",
                         method, path, host, (unsigned)port);
    if (bearer && bearer[0]) {
        n += SDL_snprintf(head + n, sizeof head - (size_t)n,
                          "Authorization: Bearer %s\r\n", bearer);
    }
    if (body && body_len) {
        n += SDL_snprintf(head + n, sizeof head - (size_t)n,
                          "Content-Type: %s\r\nContent-Length: %zu\r\n",
                          content_type ? content_type : "application/octet-stream", body_len);
    }
    n += SDL_snprintf(head + n, sizeof head - (size_t)n, "\r\n");

    if (n <= 0 || (size_t)n >= sizeof head) {
        ns_close_socket(s);
        SDL_snprintf(out->error, sizeof out->error, "en-tête trop long");
        return false;
    }

    if (!sock_send_all(s, head, (size_t)n)
        || (body && body_len && !sock_send_all(s, (const char *)body, body_len))) {
        ns_close_socket(s);
        SDL_snprintf(out->error, sizeof out->error, "émission interrompue");
        return false;
    }

    /* --- la réponse ------------------------------------------------------ */
    /*
     * Bornée à 256 Kio. Un classement fait quelques kilo-octets ; au-delà, c'est
     * qu'on ne parle pas au serveur qu'on croit, et lire sans limite est le
     * moyen le plus simple de se faire épuiser la mémoire par un tiers.
     */
    const size_t MAX = 256u * 1024u;
    size_t cap = 8192, len = 0;
    char *buf = (char *)SDL_malloc(cap);
    if (!buf) {
        ns_close_socket(s);
        SDL_snprintf(out->error, sizeof out->error, "mémoire épuisée");
        return false;
    }

    for (;;) {
        if (len == cap) {
            if (cap >= MAX) break;
            const size_t next = (cap * 2 > MAX) ? MAX : cap * 2;
            char *bigger = (char *)SDL_realloc(buf, next);
            if (!bigger) break;
            buf = bigger;
            cap = next;
        }
#if defined(_WIN32)
        const int got = recv(s, buf + len, (int)(cap - len), 0);
#else
        const ssize_t got = recv(s, buf + len, cap - len, 0);
#endif
        if (got <= 0) break;
        len += (size_t)got;
    }
    ns_close_socket(s);

    /* --- découpe de la réponse ------------------------------------------- */
    if (len < 12 || SDL_strncmp(buf, "HTTP/1.", 7) != 0) {
        SDL_free(buf);
        SDL_snprintf(out->error, sizeof out->error, "réponse illisible");
        return false;
    }
    out->status = SDL_atoi(buf + 9);

    /* Fin d'en-tête. `SDL_strstr` sur un tampon non terminé serait faux : on
     * cherche à la main sur la longueur connue. */
    size_t header_end = 0;
    for (size_t i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            header_end = i + 4;
            break;
        }
    }
    if (header_end == 0) {
        SDL_free(buf);
        SDL_snprintf(out->error, sizeof out->error, "en-tête de réponse tronqué");
        return false;
    }

    const size_t body_size = len - header_end;
    char *payload = (char *)SDL_malloc(body_size + 1);
    if (!payload) {
        SDL_free(buf);
        SDL_snprintf(out->error, sizeof out->error, "mémoire épuisée");
        return false;
    }
    SDL_memcpy(payload, buf + header_end, body_size);
    payload[body_size] = '\0';
    SDL_free(buf);

    out->body = payload;
    out->length = body_size;
    return true;
}
