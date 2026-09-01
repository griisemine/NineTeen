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

/*
 * OUVRIR LA CONNEXION, pour les deux appelants de ce fichier.
 *
 * La requête en mémoire et le téléchargement en fichier faisaient la même chose
 * ici : résoudre le nom, essayer chaque adresse rendue, poser les délais,
 * couper Nagle et désarmer SIGPIPE. Le second l'a d'abord recopiée, ce qui est
 * la façon la plus sûre d'oublier `SO_NOSIGPIPE` dans une seule des deux copies
 * et de voir le jeu disparaître quand un serveur raccroche pendant un
 * téléchargement de 175 Mio.
 */
static ns_socket ouvrir_connexion(const char *host, uint16_t port,
                                  uint32_t timeout_ms,
                                  char *erreur, size_t erreur_cap)
{
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
        if (erreur) SDL_snprintf(erreur, erreur_cap, "hôte introuvable : %s", host);
        return NS_INVALID_SOCKET;
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
        if (erreur) SDL_snprintf(erreur, erreur_cap, "connexion refusée : %s:%u",
                                 host, (unsigned)port);
        return NS_INVALID_SOCKET;
    }

    const int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    /* Sans ça, un serveur qui raccroche pendant l'émission TUE le processus par
     * SIGPIPE — un score perdu deviendrait un jeu qui disparaît. */
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
    return s;
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

    const ns_socket s = ouvrir_connexion(host, port, timeout_ms,
                                         out->error, sizeof out->error);
    if (s == NS_INVALID_SOCKET) return false;

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
    /*
     * Le code, lu sur TROIS octets et pas un de plus.
     *
     * C'était `SDL_atoi(buf + 9)`, et le commentaire six lignes plus bas dit
     * pourquoi c'était faux : ce tampon n'est jamais terminé par un NUL.
     * `SDL_atoi` s'arrête au premier caractère non chiffre — donc, sur une
     * réponse faite de chiffres, il sort de l'allocation. Et le cas se
     * construit : la capacité double depuis 8 Kio et 8192 × 2⁵ vaut 262 144,
     * exactement la borne, si bien qu'une réponse pleine laisse `len == cap`
     * sans un seul octet de battement après elle.
     *
     * Le `len < 12` juste au-dessus garantit les indices 9, 10 et 11. On exige
     * en plus trois chiffres : un serveur qui n'en envoie pas ne parle pas
     * HTTP, et rendre 0 est plus honnête qu'un nombre tiré d'octets voisins.
     */
    if (!SDL_isdigit((unsigned char)buf[9]) ||
        !SDL_isdigit((unsigned char)buf[10]) ||
        !SDL_isdigit((unsigned char)buf[11])) {
        SDL_free(buf);
        SDL_snprintf(out->error, sizeof out->error, "code de réponse illisible");
        return false;
    }
    out->status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');

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

/* ==========================================================================
 * Téléchargement — voir ns_http.h pour ce qu'il fait et pourquoi il existe
 * ========================================================================== */

/*
 * La taille du fichier déjà là, ou 0. Elle sert de point de reprise : `SDL_
 * GetPathInfo` la donne sans ouvrir le fichier, et un fichier absent est une
 * reprise à zéro, ce qui est exactement le cas du premier téléchargement.
 */
static int64_t taille_deja_recue(const char *chemin)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(chemin, &info) || info.type != SDL_PATHTYPE_FILE) return 0;
    return (info.size > 0) ? (int64_t)info.size : 0;
}

/* Cherche un en-tête dans le bloc reçu et rend sa valeur entière. Le tampon
 * n'est pas terminé par un NUL, donc rien ici ne peut employer `SDL_strstr`. */
static bool entete_entier(const char *entete, size_t len, const char *nom, int64_t *out)
{
    const size_t nom_len = SDL_strlen(nom);
    for (size_t i = 0; i + nom_len < len; ++i) {
        if (SDL_strncasecmp(entete + i, nom, nom_len) != 0) continue;
        size_t j = i + nom_len;
        while (j < len && (entete[j] == ' ' || entete[j] == ':')) ++j;
        int64_t v = 0;
        bool chiffre = false;
        while (j < len && entete[j] >= '0' && entete[j] <= '9') {
            v = v * 10 + (entete[j++] - '0');
            chiffre = true;
        }
        if (chiffre) { *out = v; return true; }
    }
    return false;
}

bool ns_http_telecharger(const char *url, const char *chemin,
                         uint32_t timeout_ms,
                         ns_http_progres progres, void *contexte,
                         int *statut, char *erreur, size_t erreur_cap)
{
    if (statut) *statut = 0;
    if (erreur && erreur_cap) erreur[0] = '\0';
    if (!url || !chemin) return false;

    char host[256], path[1024];
    uint16_t port = 80;
    if (!ns_http_parse_url(url, host, sizeof host, &port, path, sizeof path,
                           erreur, erreur_cap)) {
        return false;
    }

    const int64_t deja = taille_deja_recue(chemin);

    const ns_socket s = ouvrir_connexion(host, port, timeout_ms, erreur, erreur_cap);
    if (s == NS_INVALID_SOCKET) return false;

    char head[1536];
    int n = SDL_snprintf(head, sizeof head,
                         "GET %s HTTP/1.1\r\n"
                         "Host: %s:%u\r\n"
                         "User-Agent: nineteen\r\n"
                         "Connection: close\r\n"
                         "Accept: application/octet-stream\r\n",
                         path, host, (unsigned)port);
    if (deja > 0) {
        n += SDL_snprintf(head + n, sizeof head - (size_t)n,
                          "Range: bytes=%lld-\r\n", (long long)deja);
    }
    n += SDL_snprintf(head + n, sizeof head - (size_t)n, "\r\n");
    if (n <= 0 || (size_t)n >= sizeof head) {
        ns_close_socket(s);
        if (erreur) SDL_snprintf(erreur, erreur_cap, "en-tête trop long");
        return false;
    }
    if (!sock_send_all(s, head, (size_t)n)) {
        ns_close_socket(s);
        if (erreur) SDL_snprintf(erreur, erreur_cap, "émission interrompue");
        return false;
    }

    /*
     * 64 Kio par lecture. Mesuré sur la boucle locale : le paquet de 175 Mio
     * arrive en 2 680 blocs, et la même boucle en 8 Kio en demande 21 400 pour
     * exactement le même octet — c'est du travail que personne ne voit passer
     * mais que la machine fait quand même.
     */
    static const size_t BLOC = 64u * 1024u;
    char *tampon = (char *)SDL_malloc(BLOC);
    if (!tampon) {
        ns_close_socket(s);
        if (erreur) SDL_snprintf(erreur, erreur_cap, "mémoire épuisée");
        return false;
    }

    SDL_IOStream *io = NULL;
    int64_t attendu = 0, ecrit = 0;
    size_t entete_len = 0;
    bool entete_lu = false, ok = false, abandon = false;
    char entete[4096];

    for (;;) {
#if defined(_WIN32)
        const int recu = recv(s, tampon, (int)BLOC, 0);
#else
        const ssize_t recu = recv(s, tampon, BLOC, 0);
#endif
        if (recu <= 0) break;

        size_t debut = 0;
        if (!entete_lu) {
            /*
             * L'en-tête peut arriver en plusieurs morceaux, et le corps peut
             * commencer dans le même paquet que sa dernière ligne. On accumule
             * jusqu'à la ligne vide, puis on écrit ce qui la suit — l'oublier
             * ferait perdre les premiers octets du fichier, et un paquet amputé
             * de son début échoue à la vérification d'empreinte sans que rien
             * ne dise pourquoi.
             */
            const size_t place = sizeof entete - entete_len;
            const size_t pris = ((size_t)recu < place) ? (size_t)recu : place;
            SDL_memcpy(entete + entete_len, tampon, pris);
            entete_len += pris;

            size_t fin = 0;
            for (size_t i = 0; i + 3 < entete_len; ++i) {
                if (entete[i] == '\r' && entete[i + 1] == '\n'
                    && entete[i + 2] == '\r' && entete[i + 3] == '\n') {
                    fin = i + 4;
                    break;
                }
            }
            if (fin == 0) {
                if (entete_len == sizeof entete) {
                    if (erreur) SDL_snprintf(erreur, erreur_cap, "en-tête démesuré");
                    break;
                }
                continue;
            }

            if (entete_len < 12 || SDL_strncmp(entete, "HTTP/1.", 7) != 0
                || !SDL_isdigit((unsigned char)entete[9])
                || !SDL_isdigit((unsigned char)entete[10])
                || !SDL_isdigit((unsigned char)entete[11])) {
                if (erreur) SDL_snprintf(erreur, erreur_cap, "réponse illisible");
                break;
            }
            const int code = (entete[9] - '0') * 100 + (entete[10] - '0') * 10
                           + (entete[11] - '0');
            if (statut) *statut = code;

            /*
             * 206 : le serveur reprend là où on s'était arrêté, on ajoute à la
             * suite. 200 : il recommence tout — soit qu'il ignore `Range`, soit
             * que le fichier ait changé — et il faut alors ÉCRASER, sinon on
             * colle deux versions bout à bout et l'empreinte ne tombe jamais
             * juste. 416 : on en a déjà autant qu'il en a, ce qui veut dire
             * fini. Tout le reste est un échec.
             */
            if (code == 416 && deja > 0) { ok = true; break; }
            if (code != 200 && code != 206) {
                if (erreur) SDL_snprintf(erreur, erreur_cap, "réponse %d", code);
                break;
            }
            const bool reprise = (code == 206);
            (void)entete_entier(entete, fin, "Content-Length", &attendu);
            ecrit = reprise ? deja : 0;
            if (attendu > 0) attendu += ecrit;

            io = SDL_IOFromFile(chemin, reprise ? "ab" : "wb");
            if (!io) {
                if (erreur) SDL_snprintf(erreur, erreur_cap, "écriture impossible : %s",
                                         SDL_GetError());
                break;
            }
            entete_lu = true;

            /* Ce qui suivait la ligne vide DANS ce même paquet. */
            const size_t reste_entete = fin - (entete_len - pris);
            debut = (reste_entete <= (size_t)recu) ? reste_entete : (size_t)recu;
        }

        const size_t utile = (size_t)recu - debut;
        if (utile) {
            if (SDL_WriteIO(io, tampon + debut, utile) != utile) {
                if (erreur) SDL_snprintf(erreur, erreur_cap, "disque plein ou en lecture seule");
                break;
            }
            ecrit += (int64_t)utile;
        }
        if (progres && !progres(contexte, ecrit, attendu)) { abandon = true; break; }
    }

    if (io) {
        SDL_CloseIO(io);
        /* Complet quand le serveur avait annoncé une taille et qu'on l'a
         * atteinte. Sans annonce on ne peut rien affirmer : c'est l'appelant
         * qui tranche, et il a l'empreinte pour ça. */
        if (!abandon && !ok) ok = (attendu > 0) ? (ecrit >= attendu) : (ecrit > 0);
    }
    if (!ok && !abandon && erreur && erreur_cap && !erreur[0]) {
        SDL_snprintf(erreur, erreur_cap, "transfert interrompu");
    }
    SDL_free(tampon);
    ns_close_socket(s);
    return ok;
}
