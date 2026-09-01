/* stub_serveur.c — voir stub_serveur.h pour ce que ce bouchon est et n'est pas. */

/*
 * Les mêmes gardes que `engine/net/ns_http.c`, et pour la même raison : le
 * projet compile en `-std=c11` strict, ce qui masque les déclarations POSIX.
 */
#if !defined(_WIN32)
    #define _POSIX_C_SOURCE 200809L
    #if defined(__APPLE__)
        #define _DARWIN_C_SOURCE 1
    #endif
#endif

#include "stub_serveur.h"

#include <SDL3/SDL.h>

#include <string.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET stub_socket;
    #define STUB_INVALID INVALID_SOCKET
    #define stub_close closesocket
#else
    #include <errno.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    typedef int stub_socket;
    #define STUB_INVALID (-1)
    #define stub_close close
#endif

static struct {
    stub_socket    ecoute;
    SDL_Thread    *fil;
    SDL_AtomicInt  quit;
    SDL_AtomicInt  ouvertes;
    SDL_AtomicInt  soumises;
    SDL_Mutex     *verrou;
    char           creneau[32];
    int            score;
    uint32_t       graine;

    /* Ce que la route des téléchargements offre. Voir `stub_poser_maj`. */
    char           maj_version[32];
    char           maj_nom[128];
    char           maj_contenu[256];
    char           maj_somme[80];
    char           maj_voisin[128];
    SDL_AtomicInt  servis;
} s;

/*
 * LA TABLE DES JEUX, recopiée de `server/internal/migrations/0001_initial.sql`.
 *
 * Recopiée et non devinée : c'est elle qui porte le piège. Le moteur nomme un
 * jeu « demineur » avec une difficulté « easy » ou « hard » ; la base en fait
 * un seul créneau par tableau, « demineur-easy », et deux jeux n'ont pas de
 * régime du tout. Un bouchon qui répondrait « demineur » tout court laisserait
 * passer exactement la faute qu'on veut attraper.
 */
static const char *const CRENEAUX[] = {
    "envol-hard", "aplomb-hard", "asteroid-hard", "shooter-hard", "snake-hard",
    "demineur-hard", "demineur-easy", "snake-easy", "shooter-easy",
    "asteroid-easy", "aplomb-easy", "envol-easy", "dedale", "piano",
};
#define STUB_CRENEAUX (int)(sizeof CRENEAUX / sizeof CRENEAUX[0])

/*
 * Le bouchon annonce la plateforme de la machine qui exécute le test, sinon le
 * paquet serait toujours écarté et le test ne vérifierait que le refus.
 */
#if defined(_WIN32)
    #define STUB_PLATEFORME "windows"
#elif defined(__APPLE__)
    #define STUB_PLATEFORME "macos"
#else
    #define STUB_PLATEFORME "linux"
#endif

static int creneau_id(const char *nom)
{
    for (int i = 0; i < STUB_CRENEAUX; ++i) {
        if (SDL_strcmp(CRENEAUX[i], nom) == 0) return i + 1;
    }
    return -1;
}

static void envoyer(stub_socket c, int code, const char *raison, const char *corps)
{
    char entete[256];
    const size_t n = corps ? SDL_strlen(corps) : 0;
    const int len = SDL_snprintf(entete, sizeof entete,
                                 "HTTP/1.1 %d %s\r\n"
                                 "Content-Type: application/json\r\n"
                                 "Content-Length: %zu\r\n"
                                 "Connection: close\r\n\r\n",
                                 code, raison, n);
    (void)send(c, entete, (size_t)len, 0);
    if (n) (void)send(c, corps, n, 0);
}

/* La liste des jeux, dans la forme exacte de `handleGames`. */
static void repondre_jeux(stub_socket c)
{
    /*
     * 4 Kio : les quatorze créneaux occupent 1 320 octets, et un tampon trop
     * juste ne produit pas une erreur mais un JSON TRONQUÉ — donc un client
     * qui ne connaît aucun jeu et un test qui accuse le mauvais coupable. Vu.
     */
    char corps[4096];
    size_t n = (size_t)SDL_snprintf(corps, sizeof corps, "{\"ok\":true,\"games\":[");
    for (int i = 0; i < STUB_CRENEAUX && n < sizeof corps; ++i) {
        const int ecrit = SDL_snprintf(corps + n, sizeof corps - n,
                                       "%s{\"id\":%d,\"slug\":\"%s\",\"name\":\"%s\","
                                       "\"difficulty\":\"normal\",\"multiplier\":1}",
                                       i ? "," : "", i + 1, CRENEAUX[i], CRENEAUX[i]);
        if (ecrit < 0 || (size_t)ecrit >= sizeof corps - n) return;   /* jamais tronquer */
        n += (size_t)ecrit;
    }
    SDL_snprintf(corps + n, sizeof corps - n, "]}");
    envoyer(c, 200, "OK", corps);
}

/*
 * L'ouverture d'une partie. Le créneau demandé est GARDÉ : c'est la seule
 * mesure qui dise sur quel tableau le client croit jouer, et c'est celle qui
 * manquait quand le billet partait sur « demineur-easy » pour une partie que
 * « demineur-hard » allait réclamer.
 */
static void repondre_ouverture(stub_socket c, const char *corps_recu)
{
    char slug[32] = { 0 };
    const char *p = corps_recu ? SDL_strstr(corps_recu, "\"game\":\"") : NULL;
    if (p) {
        p += 8;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof slug) slug[i++] = *p++;
        slug[i] = '\0';
    }
    SDL_LockMutex(s.verrou);
    SDL_strlcpy(s.creneau, slug, sizeof s.creneau);
    SDL_UnlockMutex(s.verrou);

    if (creneau_id(slug) < 0) {
        envoyer(c, 400, "Bad Request", "{\"ok\":false,\"error\":\"jeu inconnu\"}");
        return;
    }

    /* Une graine qui GARDE ses bits de poids faible, comme celle du serveur :
     * une graine relue en flottant se verrait ici, et nulle part ailleurs. */
    s.graine = s.graine * 1664525u + 1013904223u;
    const long long seed = (long long)s.graine * 7919LL + 12345LL;

    char reponse[256];
    SDL_snprintf(reponse, sizeof reponse,
                 "{\"ok\":true,\"runId\":\"stub-%d\",\"seed\":%lld,"
                 /* 32 octets de secret, en base64 sans remplissage. */
                 "\"secret\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8\","
                 "\"game\":\"%s\"}",
                 SDL_GetAtomicInt(&s.ouvertes) + 1, seed, slug);
    SDL_AddAtomicInt(&s.ouvertes, 1);
    envoyer(c, 201, "Created", reponse);
}

/*
 * La soumission. Le bouchon ne VÉRIFIE pas le sceau — il n'a pas à refaire le
 * travail du serveur Go, qui a ses propres tests. Il vérifie que le corps est
 * bien la valeur de `submission` et rien d'autre : un client qui posterait le
 * fichier de file entier passerait ici et se ferait refuser par le vrai
 * serveur, qui interdit les champs inconnus. C'est arrivé une fois.
 */
static void repondre_soumission(stub_socket c, const char *corps_recu)
{
    if (!corps_recu || !SDL_strstr(corps_recu, "\"seal\":\"")
        || !SDL_strstr(corps_recu, "\"events\":[")
        || SDL_strstr(corps_recu, "\"runId\":")) {
        envoyer(c, 400, "Bad Request", "{\"ok\":false,\"error\":\"enveloppe\"}");
        return;
    }
    int score = -1;
    const char *p = SDL_strstr(corps_recu, "\"claimedScore\":");
    if (p) score = SDL_atoi(p + 15);

    SDL_LockMutex(s.verrou);
    s.score = score;
    SDL_UnlockMutex(s.verrou);
    SDL_AddAtomicInt(&s.soumises, 1);

    char reponse[128];
    SDL_snprintf(reponse, sizeof reponse,
                 "{\"ok\":true,\"score\":%d,\"verdict\":\"accepted\"}", score);
    envoyer(c, 200, "OK", reponse);
}

static void repondre_classement(stub_socket c)
{
    SDL_LockMutex(s.verrou);
    const int score = s.score;
    SDL_UnlockMutex(s.verrou);
    if (score < 0) {
        envoyer(c, 200, "OK", "{\"ok\":true,\"entries\":[]}");
        return;
    }
    char corps[192];
    SDL_snprintf(corps, sizeof corps,
                 "{\"ok\":true,\"entries\":[{\"rank\":1,\"username\":\"Bouchon\","
                 "\"score\":%d}]}", score);
    envoyer(c, 200, "OK", corps);
}

/*
 * La liste des paquets, dans la forme exacte de `handleTelechargements` — les
 * clés en minuscules, l'architecture dans le vocabulaire français du serveur
 * (« universel », « arm64 »), et l'URL DONNÉE et non devinée.
 */
static void repondre_telechargements(stub_socket c)
{
    SDL_LockMutex(s.verrou);
    const bool rien = s.maj_version[0] == '\0' || s.maj_nom[0] == '\0';
    char corps[1024];
    if (rien) {
        SDL_snprintf(corps, sizeof corps,
                     "{\"ok\":true,\"version\":\"0.0.0\",\"source\":\"aucune\","
                     "\"serveur\":\"\",\"fichiers\":[],\"manifestes\":[]}");
    } else {
        /* L'architecture suit le nom, comme `Classer` la déduit : « universel »
         * pour un nom qui le dit, sinon rien — ce qui convient partout et
         * permet au test de couvrir les deux chemins. */
        const char *arch = SDL_strstr(s.maj_nom, "universal") ? "universel" : "";
        /* Le voisin est mis EN TÊTE : un client qui prendrait le premier venu
         * se trahit alors tout de suite, au lieu de passer par chance. */
        char voisin[320] = { 0 };
        if (s.maj_voisin[0]) {
            SDL_snprintf(voisin, sizeof voisin,
                         "{\"nom\":\"%s\",\"url\":\"/telechargements/%s\","
                         "\"plateforme\":\"%s\",\"arch\":\"%s\",\"format\":\"essai\","
                         "\"octets\":1,\"sha256\":\"\"},",
                         s.maj_voisin, s.maj_voisin, STUB_PLATEFORME, arch);
        }
        SDL_snprintf(corps, sizeof corps,
                     "{\"ok\":true,\"version\":\"%s\",\"source\":\"locale\","
                     "\"serveur\":\"\",\"fichiers\":[%s{"
                     "\"nom\":\"%s\",\"url\":\"/telechargements/%s\","
                     "\"plateforme\":\"%s\",\"arch\":\"%s\",\"format\":\"essai\","
                     "\"octets\":%zu,\"sha256\":\"%s\"}],\"manifestes\":[]}",
                     s.maj_version, voisin, s.maj_nom, s.maj_nom,
                     STUB_PLATEFORME, arch,
                     SDL_strlen(s.maj_contenu), s.maj_somme);
    }
    SDL_UnlockMutex(s.verrou);
    envoyer(c, 200, "OK", corps);
}

/*
 * Le fichier lui-même, avec la REPRISE : un en-tête `Range: bytes=N-` doit
 * rendre 206 et la suite seule. Sans ce chemin dans le bouchon, la reprise ne
 * serait vérifiée que face à un vrai serveur, c'est-à-dire jamais dans
 * l'intégration continue.
 */
static void repondre_fichier(stub_socket c, const char *requete)
{
    SDL_AddAtomicInt(&s.servis, 1);

    SDL_LockMutex(s.verrou);
    char contenu[256];
    SDL_strlcpy(contenu, s.maj_contenu, sizeof contenu);
    const bool connu = s.maj_nom[0] != '\0'
                    && SDL_strstr(requete, s.maj_nom) != NULL;
    SDL_UnlockMutex(s.verrou);

    if (!connu) { envoyer(c, 404, "Not Found", "{\"ok\":false}"); return; }

    size_t depuis = 0;
    const char *r = SDL_strstr(requete, "Range: bytes=");
    if (r) depuis = (size_t)SDL_atoi(r + 13);

    const size_t total = SDL_strlen(contenu);
    if (depuis >= total) { envoyer(c, 416, "Range Not Satisfiable", ""); return; }

    char entete[256];
    const size_t n = total - depuis;
    const int len = SDL_snprintf(entete, sizeof entete,
                                 "HTTP/1.1 %d %s\r\n"
                                 "Content-Type: application/octet-stream\r\n"
                                 "Content-Length: %zu\r\n"
                                 "Connection: close\r\n\r\n",
                                 depuis ? 206 : 200,
                                 depuis ? "Partial Content" : "OK", n);
    (void)send(c, entete, (size_t)len, 0);
    (void)send(c, contenu + depuis, n, 0);
}

static int SDLCALL boucle(void *inutile)
{
    (void)inutile;
    while (!SDL_GetAtomicInt(&s.quit)) {
        const stub_socket c = accept(s.ecoute, NULL, NULL);
        if (c == STUB_INVALID) {
            if (SDL_GetAtomicInt(&s.quit)) break;
            SDL_Delay(5);
            continue;
        }

        /*
         * Une requête tient dans ce tampon : la plus grosse est une soumission
         * de journal, et `ns_runlog` borne son fichier à quelques kilo-octets
         * pour les parties du test. On lit jusqu'à ce que l'en-tête soit
         * complet ET que le corps annoncé soit là — lire une seule fois
         * marchait sur boucle locale et aurait fini par découper un corps en
         * deux paquets sans qu'on sache pourquoi.
         */
        static char req[65536];
        size_t recu = 0;
        long attendu = -1;
        const char *corps = NULL;
        while (recu + 1 < sizeof req) {
            const long n = (long)recv(c, req + recu, sizeof req - recu - 1, 0);
            if (n <= 0) break;
            recu += (size_t)n;
            req[recu] = '\0';
            if (!corps) {
                const char *fin = SDL_strstr(req, "\r\n\r\n");
                if (fin) {
                    corps = fin + 4;
                    const char *cl = SDL_strstr(req, "Content-Length:");
                    attendu = cl ? SDL_atoi(cl + 15) : 0;
                }
            }
            if (corps && (long)(recu - (size_t)(corps - req)) >= attendu) break;
        }

        const bool autorise = SDL_strstr(req, "Authorization: Bearer " STUB_JETON) != NULL;

        if (SDL_strncmp(req, "GET /api/v1/telechargements", 27) == 0) {
            repondre_telechargements(c);
        } else if (SDL_strncmp(req, "GET /telechargements/", 21) == 0) {
            repondre_fichier(c, req);
        } else if (SDL_strncmp(req, "GET /api/v1/games", 17) == 0) {
            repondre_jeux(c);
        } else if (SDL_strncmp(req, "GET /api/v1/leaderboard", 23) == 0) {
            repondre_classement(c);
        } else if (SDL_strncmp(req, "POST /api/v1/runs/", 18) == 0) {
            if (!autorise) envoyer(c, 401, "Unauthorized", "{\"ok\":false}");
            else           repondre_soumission(c, corps);
        } else if (SDL_strncmp(req, "POST /api/v1/runs", 17) == 0) {
            if (!autorise) envoyer(c, 401, "Unauthorized", "{\"ok\":false}");
            else           repondre_ouverture(c, corps);
        } else {
            envoyer(c, 404, "Not Found", "{\"ok\":false}");
        }
        stub_close(c);
    }
    return 0;
}

bool stub_demarrer(char *url, size_t cap)
{
    if (!url || !cap) return false;
    SDL_zero(s);
    s.score = -1;
    s.graine = 20240418u;

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    s.ecoute = socket(AF_INET, SOCK_STREAM, 0);
    if (s.ecoute == STUB_INVALID) return false;

    struct sockaddr_in adr;
    SDL_zero(adr);
    adr.sin_family = AF_INET;
    adr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    adr.sin_port = 0;   /* le système choisit : deux tests peuvent tourner ensemble */
    if (bind(s.ecoute, (struct sockaddr *)&adr, sizeof adr) != 0
        || listen(s.ecoute, 8) != 0) {
        stub_close(s.ecoute);
        s.ecoute = STUB_INVALID;
        return false;
    }

    struct sockaddr_in lu;
    SDL_zero(lu);
#if defined(_WIN32)
    int taille = (int)sizeof lu;
#else
    socklen_t taille = (socklen_t)sizeof lu;
#endif
    if (getsockname(s.ecoute, (struct sockaddr *)&lu, &taille) != 0) {
        stub_close(s.ecoute);
        s.ecoute = STUB_INVALID;
        return false;
    }
    SDL_snprintf(url, cap, "http://127.0.0.1:%u", (unsigned)ntohs(lu.sin_port));

    s.verrou = SDL_CreateMutex();
    s.fil = SDL_CreateThread(boucle, "stub-http", NULL);
    if (!s.verrou || !s.fil) {
        stub_arreter();
        return false;
    }
    return true;
}

void stub_arreter(void)
{
    SDL_SetAtomicInt(&s.quit, 1);
    if (s.ecoute != STUB_INVALID) {
        /* Fermer la socket d'écoute débloque `accept` : c'est le seul moyen
         * portable de réveiller un fil qui attend une connexion. */
        stub_close(s.ecoute);
        s.ecoute = STUB_INVALID;
    }
    if (s.fil) { SDL_WaitThread(s.fil, NULL); s.fil = NULL; }
    if (s.verrou) { SDL_DestroyMutex(s.verrou); s.verrou = NULL; }
#if defined(_WIN32)
    WSACleanup();
#endif
}

uint32_t stub_parties_ouvertes(void) { return (uint32_t)SDL_GetAtomicInt(&s.ouvertes); }
uint32_t stub_parties_soumises(void) { return (uint32_t)SDL_GetAtomicInt(&s.soumises); }
int      stub_dernier_score(void)    { return s.score; }

const char *stub_dernier_creneau(void)
{
    return s.creneau;
}

void stub_poser_maj(const char *version, const char *nom,
                    const char *contenu, const char *somme_hex)
{
    SDL_LockMutex(s.verrou);
    SDL_strlcpy(s.maj_version, version ? version : "", sizeof s.maj_version);
    SDL_strlcpy(s.maj_nom, nom ? nom : "", sizeof s.maj_nom);
    SDL_strlcpy(s.maj_contenu, contenu ? contenu : "", sizeof s.maj_contenu);
    SDL_strlcpy(s.maj_somme, somme_hex ? somme_hex : "", sizeof s.maj_somme);
    SDL_UnlockMutex(s.verrou);
}

void stub_poser_maj_voisin(const char *nom)
{
    SDL_LockMutex(s.verrou);
    SDL_strlcpy(s.maj_voisin, nom ? nom : "", sizeof s.maj_voisin);
    SDL_UnlockMutex(s.verrou);
}

uint32_t stub_fichiers_servis(void) { return (uint32_t)SDL_GetAtomicInt(&s.servis); }
