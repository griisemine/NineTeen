/*
 * ns_arene.c — la liaison persistante d'une arène.
 *
 * Voir `ns_arene.h` pour QUI ARBITRE, pour le vocabulaire des trames octet par
 * octet, et pour le verrou hérité de `ns_online`. Ce fichier-ci ne parle que de
 * la mécanique : le codec, le fil, et ce qui se passe quand le relais tombe.
 *
 * TROIS DIFFÉRENCES AVEC `ns_lockstep.c`, et elles sont toutes assumées
 * ---------------------------------------------------------------------
 * Le duel et l'arène partagent le cadrage, le relais et l'esprit. Ils diffèrent
 * sur trois points, et chacun a une raison :
 *
 *   1. UN FIL DE TRAVAIL, au lieu d'un `poll` appelé par la boucle de jeu. Le
 *      duel est en pas verrouillé : sa boucle DOIT attendre le pair, donc elle
 *      a le droit — et le devoir — de tenir la socket elle-même. L'arène,
 *      elle, ne doit jamais faire attendre personne : la manche continue même
 *      si le relais est mort. Le fil est donc la seule forme correcte, et c'est
 *      celle de `ns_online` et de `ns_realtime`.
 *
 *   2. UNE CONNEXION NON BLOQUANTE, avec `select`. `ns_lockstep_connect`
 *      connecte en bloquant, ce qui est juste chez lui : son appelant a
 *      explicitement demandé l'ouverture d'un duel et a le droit d'attendre.
 *      Ici, `ns_arene_fermer` doit pouvoir arrêter le fil à tout instant — et
 *      un `connect` bloquant ne se laisse pas interrompre. `SO_SNDTIMEO` ne le
 *      borne pas sous Linux, où un hôte injoignable coûte plus d'une minute :
 *      fermer l'arène attendrait alors cette minute-là.
 *
 *   3. UNE FILE D'ÉVÉNEMENTS bornée, au lieu d'un anneau indexé par le pas. Le
 *      duel sait quel pas il veut ; l'arène reçoit des faits datés par leur
 *      arrivée. Quand la file déborde — une salle qui ne dépile pas — on jette
 *      LE PLUS ANCIEN et on le compte. Jeter le plus récent perdrait le verdict
 *      qui vient de tomber pour garder un état périmé de trois secondes.
 *
 * CE QU'ON N'ALLOUE JAMAIS
 * ------------------------
 * Rien qui dépende d'une longueur annoncée par le réseau. La longueur d'une
 * trame est bornée AVANT d'être crue (`NS_ARENE_CHARGE_MAX`), le tableau des
 * places est refusé s'il annonce plus d'entrées qu'il n'en porte, et tous les
 * tampons de ce fichier sont dimensionnés à la compilation. C'est la première
 * règle d'un décodeur qui lit une socket, et c'est aussi celle du relais.
 */
#include "ns_arene.h"

#include "ns_core.h"
#include "ns_online.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <string.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET ns_asock;
    /* Winsock n'a pas `socklen_t` : `getsockopt` y prend un `int *`. */
    typedef int ns_alen;
    #define NS_AR_INVALID INVALID_SOCKET
    #define ns_ar_close closesocket
    #define ns_ar_would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
    #define ns_ar_in_progress() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
    typedef int ns_asock;
    typedef socklen_t ns_alen;
    #define NS_AR_INVALID (-1)
    #define ns_ar_close close
    #define ns_ar_would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
    #define ns_ar_in_progress() (errno == EINPROGRESS)
#endif

/* Les types que le relais se réserve. On les subit, on ne les fabrique pas :
 * il refuse de rediffuser un type inférieur à 0x20 venu d'un client. */
#define F_START  0x02u
#define F_BYE    0x05u
#define F_JOIN   0x10u
#define F_ROSTER 0x11u

/*
 * LE BATTEMENT DU FIL : 4 ms.
 *
 * C'est le pire délai qu'une action déposée par la boucle de jeu attende avant
 * de partir, et le pire retard d'une trame reçue avant d'être découpée. Le
 * battement d'état, lui, est à 250 ms — soixante fois plus.
 *
 * Le prix est 250 réveils par seconde d'un fil qui ne fait rien la plupart du
 * temps. C'est le même ordre de grandeur que le duel, qui traite 120 pas par
 * seconde sur une boucle de la même forme, et c'est ce qu'il faut payer pour
 * qu'un sabotage parte à l'instant où le joueur appuie plutôt qu'au prochain
 * quart de seconde.
 */
#define TIC_MS 4u

/* Le tampon de réception. La plus grosse trame reçue est le tableau des places
 * (3 + 201 = 204 octets) ; quatre charges maximales laissent de quoi absorber
 * une rafale sans jamais avoir à agrandir quoi que ce soit. */
#define RX_MAX (NS_ARENE_CHARGE_MAX * 4)

/* Le tampon d'émission. La plus grosse trame écrite est l'ÉTAT, 49 octets sur
 * le fil : 4 kio en tiennent quatre-vingt-trois. Une file pleine veut dire que
 * le relais ne lit plus — on JETTE la trame au lieu de bloquer, comme le relais
 * jette quand sa propre file est pleine. Un arrêt se voit ; un gel, non. */
#define TX_MAX 4096

/* La file d'événements. Sept places à 4 Hz font 28 états par seconde, et la
 * salle dépile une fois par image, soixante fois par seconde : 64 est très
 * large. Ce qui compte n'est pas la valeur, c'est qu'elle soit BORNÉE. */
#define FILE_MAX 64

/* ==========================================================================
 * Petit-boutiste explicite
 * ==========================================================================
 * Écrit et lu octet par octet plutôt que par `memcpy` d'un entier, pour la
 * raison écrite dans `ns_lockstep.c` : une arène peut mêler des machines
 * d'ordre différent, et c'est le genre de détail qui ne se voit qu'une fois
 * qu'un joueur a perdu une manche.
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

/* Copie un champ de texte de largeur fixe, complété de zéros. */
static void put_texte(uint8_t *p, size_t largeur, const char *s)
{
    SDL_memset(p, 0, largeur);
    if (!s || !s[0]) return;
    /* `SDL_strlcpy` écrit au plus `largeur - 1` caractères et termine : le
     * champ garde donc TOUJOURS son zéro final, ce qui est la propriété dont
     * dépend le lecteur d'en face. */
    SDL_strlcpy((char *)p, s, largeur);
}

/* Relit un champ de texte de largeur fixe. Le résultat est terminé quoi qu'ait
 * envoyé l'autre bout : on ne fait pas confiance à ce qui sort d'une socket,
 * même quand le relais promet de couper à 23. */
static void get_texte(char *out, size_t cap, const uint8_t *p, size_t largeur)
{
    size_t n = (cap - 1 < largeur) ? cap - 1 : largeur;
    size_t i = 0;
    for (; i < n && p[i]; ++i) out[i] = (char)p[i];
    out[i] = '\0';
}

/* ==========================================================================
 * LE CODEC, PUR
 * ==========================================================================
 * Aucune de ces fonctions ne touche à une socket, à une allocation ou à un
 * état : c'est ce qui permet à `tests/test_arene.c` d'en exercer les cas
 * limites sans relais et sans réseau.
 */

int ns_arene_trame(const uint8_t *flux, size_t len, uint8_t *type,
                   const uint8_t **charge, uint16_t *charge_len)
{
    if (!flux || len < 3) return 0;
    const uint16_t n = get_u16(flux);
    /* La longueur est bornée AVANT d'être crue. Un pair qui annonce plus est un
     * pair qu'on ferme, et on n'a rien alloué pour l'apprendre. */
    if (n > NS_ARENE_CHARGE_MAX) return -1;
    if (len < (size_t)n + 3) return 0;      /* pas encore entière */
    if (type) *type = flux[2];
    if (charge) *charge = flux + 3;
    if (charge_len) *charge_len = n;
    return (int)n + 3;
}

size_t ns_arene_ecrire_join(uint8_t *out, size_t cap, uint64_t salon,
                            uint8_t place, uint8_t places, const char *pseudo)
{
    if (!out || cap < NS_ARENE_JOIN_OCTETS) return 0;
    if (places < NS_ARENE_PLACES_MIN || places > NS_ARENE_MAX_PLACES) return 0;
    if (place >= places) return 0;
    put_u64(out, salon);
    out[8] = place;
    out[9] = places;
    put_texte(out + 10, NS_ARENE_PSEUDO, pseudo);
    return NS_ARENE_JOIN_OCTETS;
}

int ns_arene_lire_tableau(const uint8_t *charge, size_t len,
                          ns_arene_entree *out, int max)
{
    if (!charge || !out || max <= 0 || len < 1) return -1;
    const int n = (int)charge[0];
    /*
     * TROIS REFUS, et c'est le cœur de ce que ce décodeur doit garantir :
     *   - un compte plus grand que huit ne peut pas être vrai ;
     *   - un compte plus grand que ce que la charge PORTE est un décodeur qu'on
     *     essaie de faire déborder ;
     *   - un appelant qui n'a pas la place ne doit pas recevoir la moitié d'un
     *     tableau, qu'il croirait complet.
     * Dans les trois cas on n'écrit RIEN et on rend -1.
     */
    if (n > NS_ARENE_MAX_PLACES) return -1;
    if (len < (size_t)1 + (size_t)n * NS_ARENE_ENTREE_OCTETS) return -1;
    if (n > max) return -1;

    for (int i = 0; i < n; ++i) {
        const uint8_t *e = charge + 1 + (size_t)i * NS_ARENE_ENTREE_OCTETS;
        if (e[0] >= NS_ARENE_MAX_PLACES) return -1;
        out[i].place = e[0];
        get_texte(out[i].pseudo, sizeof out[i].pseudo, e + 1, NS_ARENE_PSEUDO);
    }
    return n;
}

size_t ns_arene_ecrire_etat(uint8_t *out, size_t cap, bool vivante, uint8_t camp,
                            const char *jeu, bool hard, int64_t score,
                            int32_t points, int32_t fusibles, int32_t valeur)
{
    if (!out || cap < NS_ARENE_ETAT_OCTETS) return 0;
    out[0] = (uint8_t)((vivante ? 1u : 0u) | (hard ? 2u : 0u));
    out[1] = (uint8_t)(camp & 7u);
    put_u32(out + 2,  (uint32_t)points);
    put_u32(out + 6,  (uint32_t)fusibles);
    put_u32(out + 10, (uint32_t)valeur);
    put_u64(out + 14, (uint64_t)score);
    put_texte(out + 22, NS_ARENE_JEU, jeu);
    return NS_ARENE_ETAT_OCTETS;
}

bool ns_arene_lire_etat(const uint8_t *charge, size_t len,
                        uint8_t *place, ns_arene_place *out)
{
    if (!charge || !out || len < (size_t)1 + NS_ARENE_ETAT_OCTETS) return false;
    if (charge[0] >= NS_ARENE_MAX_PLACES) return false;
    if (place) *place = charge[0];

    const uint8_t *p = charge + 1;
    out->vivante  = (p[0] & 1u) != 0u;
    out->hard     = (p[0] & 2u) != 0u;
    out->camp     = (uint8_t)(p[1] & 7u);
    out->points   = (int32_t)get_u32(p + 2);
    out->fusibles = (int32_t)get_u32(p + 6);
    out->valeur   = (int32_t)get_u32(p + 10);
    out->score    = (int64_t)get_u64(p + 14);
    get_texte(out->jeu, sizeof out->jeu, p + 22, NS_ARENE_JEU);
    out->etat_recu = true;
    return true;
}

size_t ns_arene_ecrire_action(uint8_t *out, size_t cap, uint8_t cible,
                              uint8_t action)
{
    if (!out || cap < NS_ARENE_ACTION_OCTETS) return 0;
    out[0] = cible;
    out[1] = action;
    return NS_ARENE_ACTION_OCTETS;
}

bool ns_arene_lire_action(const uint8_t *charge, size_t len, uint8_t *auteur,
                          uint8_t *cible, uint8_t *action)
{
    if (!charge || len < (size_t)1 + NS_ARENE_ACTION_OCTETS) return false;
    if (charge[0] >= NS_ARENE_MAX_PLACES) return false;
    if (auteur) *auteur = charge[0];
    if (cible)  *cible  = charge[1];
    if (action) *action = charge[2];
    return true;
}

size_t ns_arene_ecrire_verdict(uint8_t *out, size_t cap, uint8_t sortie,
                               uint8_t vainqueur, uint8_t numero,
                               uint32_t horloge_ms)
{
    if (!out || cap < NS_ARENE_VERDICT_OCTETS) return 0;
    out[0] = numero;
    out[1] = sortie;
    out[2] = vainqueur;
    put_u32(out + 3, horloge_ms);
    return NS_ARENE_VERDICT_OCTETS;
}

bool ns_arene_lire_verdict(const uint8_t *charge, size_t len, uint8_t *emetteur,
                           uint8_t *sortie, uint8_t *vainqueur, uint8_t *numero,
                           uint32_t *horloge_ms)
{
    if (!charge || len < (size_t)1 + NS_ARENE_VERDICT_OCTETS) return false;
    /*
     * On ne juge ici que la FORME. L'octet d'identité est rendu tel quel :
     * savoir s'il désigne l'arbitre demande le tableau des places, que ce
     * décodeur pur n'a pas. Le refus vit dans `traiter`, avec
     * `ns_arene_arbitre_de` — une seule description de qui arbitre.
     */
    if (charge[0] >= NS_ARENE_MAX_PLACES) return false;
    if (emetteur)   *emetteur   = charge[0];
    if (numero)     *numero     = charge[1];
    if (sortie)     *sortie     = charge[2];
    if (vainqueur)  *vainqueur  = charge[3];
    if (horloge_ms) *horloge_ms = get_u32(charge + 4);
    return true;
}

uint8_t ns_arene_arbitre_de(uint8_t presentes)
{
    for (uint8_t i = 0; i < NS_ARENE_MAX_PLACES; ++i) {
        if (presentes & (uint8_t)(1u << i)) return i;
    }
    return NS_ARENE_AUCUNE_PLACE;
}

size_t ns_arene_ecrire_effet(uint8_t *out, size_t cap, uint8_t auteur,
                             uint8_t cible, uint8_t action, ns_arene_issue issue)
{
    if (!out || cap < NS_ARENE_EFFET_OCTETS) return 0;
    out[0] = auteur;
    out[1] = cible;
    out[2] = action;
    out[3] = (uint8_t)issue;
    return NS_ARENE_EFFET_OCTETS;
}

bool ns_arene_lire_effet(const uint8_t *charge, size_t len, uint8_t *emetteur,
                         uint8_t *auteur, uint8_t *cible, uint8_t *action,
                         ns_arene_issue *issue)
{
    if (!charge || len < (size_t)1 + NS_ARENE_EFFET_OCTETS) return false;
    if (charge[0] >= NS_ARENE_MAX_PLACES) return false;
    /* Une issue hors de l'énumération deviendrait un `switch` sans branche chez
     * celui qui affiche. On la refuse ici, une fois, plutôt que de demander à
     * chaque lecteur d'y penser. */
    if (charge[4] > (uint8_t)NS_ARENE_REFUSEE) return false;
    if (emetteur) *emetteur = charge[0];
    if (auteur)   *auteur   = charge[1];
    if (cible)    *cible    = charge[2];
    if (action)   *action   = charge[3];
    if (issue)    *issue    = (ns_arene_issue)charge[4];
    return true;
}

/* ==========================================================================
 * L'état d'une arène
 * ========================================================================== */

struct ns_arene {
    /* Touchée par le FIL SEUL, de son ouverture à sa fermeture. */
    ns_asock      sock;
    uint8_t       rx[RX_MAX];
    size_t        rx_len;
    bool          tableau_vu;      /* le relais nous a-t-il jamais répondu ? */

    /* Figés à l'ouverture, jamais réécrits : lisibles sans verrou. */
    char          hote[256];
    uint16_t      port;
    uint64_t      salon;
    uint8_t       ma_place;
    uint8_t       places_attendues;
    char          pseudo[NS_ARENE_PSEUDO];
    uint32_t      delai_ms;

    SDL_Thread   *fil;
    SDL_Mutex    *verrou;
    SDL_AtomicInt quit;
    /*
     * L'état de la liaison est ATOMIQUE et non protégé par le verrou : c'est
     * le champ que la boucle de jeu relit le plus souvent — `ns_arene_arbitre`
     * l'interroge à chaque image — et il n'a aucune raison de faire attendre
     * qui que ce soit.
     */
    SDL_AtomicInt liaison;

    /* --- sous le verrou --- */
    char          erreur[160];
    uint64_t      graine;

    ns_arene_place place[NS_ARENE_MAX_PLACES];
    uint64_t       vu_ms[NS_ARENE_MAX_PLACES];   /* 0 = jamais d'ÉTAT reçu */

    uint8_t       mon_etat[NS_ARENE_ETAT_OCTETS];
    bool          mon_etat_pose;

    ns_arene_evenement file[FILE_MAX];
    uint32_t      tete, n_file;

    uint8_t       tx[TX_MAX];
    size_t        tx_len;

    uint32_t      trames_in, trames_out, perdus;
};

/*
 * LE COMPTEUR DE SOCKETS DU MODULE. Voir `ns_arene_sockets` : il existe pour
 * que la promesse « sans URL configurée, aucune socket n'est ouverte » se
 * MESURE au lieu de se relire. Il est incrémenté juste avant le seul appel à
 * `socket()` du fichier, et jamais remis à zéro.
 *
 * Atomique parce que deux arènes peuvent s'ouvrir en même temps — c'est ce que
 * fait le test, qui en ouvre deux dans un salon de deux.
 */
static SDL_AtomicInt g_sockets;

uint32_t ns_arene_sockets(void) { return (uint32_t)SDL_GetAtomicInt(&g_sockets); }

static void poser_erreur(ns_arene *a, const char *fmt, ...)
{
    va_list ap;
    SDL_LockMutex(a->verrou);
    /*
     * ÉCRITE UNE SEULE FOIS, et jamais ensuite. `ns_arene_erreur` rend un
     * pointeur DANS la structure : le laisser se faire réécrire par le fil
     * pendant que la salle l'affiche serait une course. Le premier motif est
     * de toute façon le bon — les suivants en sont les conséquences.
     */
    if (!a->erreur[0]) {
        va_start(ap, fmt);
        SDL_vsnprintf(a->erreur, sizeof a->erreur, fmt, ap);
        va_end(ap);
    }
    SDL_UnlockMutex(a->verrou);
}

/* Le masque des places présentes. LE VERROU DOIT ÊTRE TENU. */
static uint8_t masque_present(const ns_arene *a)
{
    uint8_t m = 0;
    for (uint8_t i = 0; i < NS_ARENE_MAX_PLACES; ++i) {
        if (a->place[i].presente) m = (uint8_t)(m | (1u << i));
    }
    return m;
}

/*
 * La place dont on accepte un verdict, à cet instant.
 *
 * Tant que le tableau n'est pas arrivé, c'est la place 0 : c'est la règle nue,
 * et le seul verdict qui puisse arriver avant le premier tableau viendrait d'un
 * salon dont on ne sait rien. LE VERROU DOIT ÊTRE TENU.
 */
static uint8_t arbitre_attendu(const ns_arene *a)
{
    if (!a->tableau_vu) return 0u;
    return ns_arene_arbitre_de(masque_present(a));
}

/* Pousse un événement. LE VERROU DOIT ÊTRE TENU. */
static void pousser(ns_arene *a, const ns_arene_evenement *e)
{
    if (a->n_file >= FILE_MAX) {
        /* On jette LE PLUS ANCIEN. Jeter le plus récent garderait un état
         * périmé au prix du verdict qui vient de tomber. */
        a->tete = (a->tete + 1u) % FILE_MAX;
        a->n_file--;
        a->perdus++;
    }
    a->file[(a->tete + a->n_file) % FILE_MAX] = *e;
    a->n_file++;
}

/* Met une trame dans la file de sortie. Prend le verrou. */
static void poster(ns_arene *a, uint8_t type, const uint8_t *charge, uint16_t len)
{
    if (len > NS_ARENE_LIBRE_MAX) return;    /* le relais fermerait : voir l'en-tête */
    SDL_LockMutex(a->verrou);
    if (a->tx_len + 3u + len <= TX_MAX) {
        put_u16(a->tx + a->tx_len, len);
        a->tx[a->tx_len + 2] = type;
        if (len) SDL_memcpy(a->tx + a->tx_len + 3, charge, len);
        a->tx_len += 3u + len;
        a->trames_out++;
    } else {
        /* Le relais ne lit plus. On JETTE plutôt que de bloquer la boucle de
         * jeu : c'est exactement ce que fait `posteVerrouille` de l'autre côté,
         * et pour la même raison. */
        a->perdus++;
    }
    SDL_UnlockMutex(a->verrou);
}

/* ==========================================================================
 * La socket
 * ========================================================================== */

static void set_nonblocking(ns_asock s)
{
#if defined(_WIN32)
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    if (fl >= 0) fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* `TCP_NODELAY` pour la même raison que dans le duel : l'algorithme de Nagle
 * retient un petit envoi jusqu'à l'acquittement du précédent. Sur une trame
 * d'action de cinq octets, ce serait des dizaines de millisecondes entre
 * l'appui du joueur et le brouillage de l'écran d'en face. */
static void set_nodelay(ns_asock s)
{
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
}

/*
 * Attend que la socket devienne inscriptible, ou que le délai passe, ou qu'on
 * demande l'arrêt. Rend vrai si la connexion a abouti.
 *
 * `select` et non `poll` : c'est la seule des deux que Windows fournisse sous
 * ce nom, et l'arène n'a qu'un descripteur à surveiller — le seul défaut connu
 * de `select`, sa borne à 1024 descripteurs, ne peut pas se produire ici.
 */
static bool attendre_connexion(ns_arene *a, ns_asock s, uint32_t delai_ms)
{
    const uint64_t fin = SDL_GetTicks() + delai_ms;
    for (;;) {
        if (SDL_GetAtomicInt(&a->quit)) return false;
        const uint64_t maintenant = SDL_GetTicks();
        if (maintenant >= fin) return false;

        fd_set w;
        FD_ZERO(&w);
        /* #nosec — `s` est notre seul descripteur, créé trois lignes plus haut. */
        FD_SET(s, &w);
        struct timeval tv;
        /* On se réveille au moins tous les TIC_MS pour relire `quit` : c'est ce
         * qui rend `ns_arene_fermer` immédiat même pendant la connexion. */
        tv.tv_sec = 0;
        tv.tv_usec = (int)(TIC_MS * 1000u);
#if defined(_WIN32)
        const int nfds = 0;          /* ignoré par Winsock */
#else
        const int nfds = (int)s + 1;
#endif
        const int r = select(nfds, NULL, &w, NULL, &tv);
        if (r < 0) return false;
        if (r == 0) continue;

        /* Inscriptible ne veut pas dire connecté : `SO_ERROR` tranche. Sans ce
         * contrôle, un refus se présenterait comme une réussite et le JOIN
         * partirait dans le vide. */
        int err = 0;
        ns_alen n = (ns_alen)sizeof err;
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &n) != 0) return false;
        return err == 0;
    }
}

/* Envoie sur une socket non bloquante, borné dans le temps. Sert au seul JOIN :
 * après lui, tout passe par la file de sortie et le fil. */
static bool envoyer_tout(ns_arene *a, const uint8_t *data, size_t len,
                         uint32_t delai_ms)
{
    const uint64_t fin = SDL_GetTicks() + delai_ms;
    size_t envoye = 0;
    while (envoye < len) {
        if (SDL_GetAtomicInt(&a->quit)) return false;
#if defined(_WIN32)
        const int n = send(a->sock, (const char *)data + envoye,
                           (int)(len - envoye), 0);
#else
        const ssize_t n = send(a->sock, data + envoye, len - envoye, 0);
#endif
        if (n > 0) { envoye += (size_t)n; continue; }
        if (n < 0 && ns_ar_would_block()) {
            if (SDL_GetTicks() > fin) return false;
            SDL_Delay(TIC_MS);
            continue;
        }
        return false;
    }
    return true;
}

/*
 * Ouvre la socket et annonce le JOIN. Tourne SUR LE FIL : la boucle de jeu
 * n'attend rien de tout ça.
 */
static bool connecter(ns_arene *a)
{
    char portstr[8];
    SDL_snprintf(portstr, sizeof portstr, "%u", (unsigned)a->port);

    struct addrinfo hints;
    SDL_zero(hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(a->hote, portstr, &hints, &res) != 0 || !res) {
        poser_erreur(a, "hote introuvable : %s", a->hote);
        return false;
    }

    ns_asock s = NS_AR_INVALID;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        /* LE SEUL `socket()` DU FICHIER. Le compteur est incrémenté ici, et
         * c'est ce qui rend la promesse de l'en-tête mesurable. */
        SDL_AddAtomicInt(&g_sockets, 1);
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == NS_AR_INVALID) continue;
        set_nonblocking(s);
        const int r = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        if (r == 0) break;
        if (ns_ar_in_progress() && attendre_connexion(a, s, a->delai_ms)) break;
        ns_ar_close(s);
        s = NS_AR_INVALID;
    }
    freeaddrinfo(res);

    if (s == NS_AR_INVALID) {
        poser_erreur(a, "connexion refusee par %s:%u", a->hote, (unsigned)a->port);
        return false;
    }
    set_nodelay(s);
    a->sock = s;

    uint8_t charge[NS_ARENE_JOIN_OCTETS];
    if (!ns_arene_ecrire_join(charge, sizeof charge, a->salon, a->ma_place,
                              a->places_attendues, a->pseudo)) {
        poser_erreur(a, "parametres de salon invalides");
        return false;
    }
    uint8_t trame[3 + NS_ARENE_JOIN_OCTETS];
    put_u16(trame, (uint16_t)NS_ARENE_JOIN_OCTETS);
    trame[2] = F_JOIN;
    SDL_memcpy(trame + 3, charge, sizeof charge);
    if (!envoyer_tout(a, trame, sizeof trame, a->delai_ms)) {
        poser_erreur(a, "le JOIN n'est pas parti");
        return false;
    }

    SDL_LockMutex(a->verrou);
    a->trames_out++;
    SDL_UnlockMutex(a->verrou);
    SDL_SetAtomicInt(&a->liaison, (int)NS_ARENE_SALON);
    return true;
}

/* ==========================================================================
 * Réception
 * ========================================================================== */

/* Le tableau des places REMPLACE ce qu'on savait : il est complet à chaque
 * envoi, arrivées et départs compris. Reconstruire au lieu de fusionner évite
 * la question « depuis quand cette place n'est-elle plus là ». */
static void appliquer_tableau(ns_arene *a, const uint8_t *p, uint16_t len)
{
    ns_arene_entree e[NS_ARENE_MAX_PLACES];
    const int n = ns_arene_lire_tableau(p, len, e, NS_ARENE_MAX_PLACES);
    if (n < 0) {
        NS_WARN("arene : tableau des places incoherent (%u octets) — ignore",
                (unsigned)len);
        return;
    }

    SDL_LockMutex(a->verrou);
    a->tableau_vu = true;
    for (int i = 0; i < NS_ARENE_MAX_PLACES; ++i) a->place[i].presente = false;
    for (int i = 0; i < n; ++i) {
        ns_arene_place *pl = &a->place[e[i].place];
        pl->presente = true;
        SDL_strlcpy(pl->pseudo, e[i].pseudo, sizeof pl->pseudo);
    }
    /* Une place partie perd son état : le garder afficherait le score d'un
     * absent, et `etat_recu` cesserait de vouloir dire ce qu'il dit. */
    for (int i = 0; i < NS_ARENE_MAX_PLACES; ++i) {
        if (!a->place[i].presente) {
            a->place[i].etat_recu = false;
            a->vu_ms[i] = 0;
        }
    }
    ns_arene_evenement ev;
    SDL_zero(ev);
    ev.type = NS_ARENE_EVT_TABLEAU;
    ev.a = (uint8_t)n;
    pousser(a, &ev);
    SDL_UnlockMutex(a->verrou);
}

/* Vrai si `emetteur` a le droit d'arbitrer, d'après le tableau qu'on connaît. */
static bool accepter_arbitre(ns_arene *a, uint8_t emetteur)
{
    SDL_LockMutex(a->verrou);
    const bool ok = (emetteur == arbitre_attendu(a));
    SDL_UnlockMutex(a->verrou);
    return ok;
}

static void traiter(ns_arene *a, uint8_t type, const uint8_t *p, uint16_t len)
{
    SDL_LockMutex(a->verrou);
    a->trames_in++;
    SDL_UnlockMutex(a->verrou);

    ns_arene_evenement ev;
    SDL_zero(ev);

    switch (type) {
        case F_ROSTER:
            appliquer_tableau(a, p, len);
            return;

        case F_START:
            if (len < 8) return;
            SDL_LockMutex(a->verrou);
            a->graine = get_u64(p);
            ev.type = NS_ARENE_EVT_DEBUT;
            pousser(a, &ev);
            SDL_UnlockMutex(a->verrou);
            /* La graine est posée AVANT l'état : une salle qui voit COURSE doit
             * pouvoir lire une graine, jamais un zéro. */
            SDL_SetAtomicInt(&a->liaison, (int)NS_ARENE_COURSE);
            return;

        case F_BYE:
            /* Un octet : LA PLACE DU PARTANT, écrite par le relais. Un joueur
             * ne peut pas déclarer le départ d'un autre — son propre 0x05 ferme
             * sa connexion et n'est pas rediffusé. */
            if (len < 1 || p[0] >= NS_ARENE_MAX_PLACES) return;
            ev.type = NS_ARENE_EVT_DEPART;
            ev.a = p[0];
            break;

        case NS_ARENE_T_ETAT: {
            uint8_t place = 0;
            ns_arene_place lu;
            SDL_zero(lu);
            if (!ns_arene_lire_etat(p, len, &place, &lu)) return;
            SDL_LockMutex(a->verrou);
            /* Le pseudo et la présence viennent du TABLEAU, pas de l'état : on
             * ne recopie que ce que la trame porte vraiment. */
            const bool presente = a->place[place].presente;
            char nom[NS_ARENE_PSEUDO];
            SDL_strlcpy(nom, a->place[place].pseudo, sizeof nom);
            a->place[place] = lu;
            a->place[place].presente = presente;
            SDL_strlcpy(a->place[place].pseudo, nom, sizeof a->place[place].pseudo);
            a->vu_ms[place] = SDL_GetTicks();
            ev.type = NS_ARENE_EVT_ETAT;
            ev.a = place;
            pousser(a, &ev);
            SDL_UnlockMutex(a->verrou);
            return;
        }

        case NS_ARENE_T_ACTION: {
            uint8_t auteur = 0, cible = 0, quoi = 0;
            if (!ns_arene_lire_action(p, len, &auteur, &cible, &quoi)) return;
            ev.type = NS_ARENE_EVT_ACTION;
            ev.a = auteur;
            ev.b = cible;
            ev.valeur = (int32_t)quoi;
            break;
        }

        case NS_ARENE_T_VERDICT: {
            uint8_t emetteur = 0, sortie = 0, vainqueur = 0, numero = 0;
            uint32_t horloge = 0;
            if (!ns_arene_lire_verdict(p, len, &emetteur, &sortie, &vainqueur,
                                       &numero, &horloge)) {
                return;
            }
            /*
             * LE CONTRÔLE QUE L'OCTET D'IDENTITÉ REND POSSIBLE. Un verdict qui
             * ne vient pas de l'arbitre n'est pas un verdict, c'est une place
             * qui essaie de sortir un rival. Le relais écrit lui-même cet
             * octet, donc personne ne peut le contrefaire, et ce module est le
             * seul en position de faire le refus : il a le tableau des places.
             */
            if (!accepter_arbitre(a, emetteur)) {
                NS_WARN("arene : verdict de la place %u rejete — elle n'arbitre "
                        "pas", (unsigned)emetteur);
                return;
            }
            ev.type = NS_ARENE_EVT_VERDICT;
            ev.a = sortie;
            ev.b = vainqueur;
            ev.valeur = (int32_t)numero;
            ev.horloge_ms = horloge;
            break;
        }

        case NS_ARENE_T_EFFET: {
            uint8_t emetteur = 0, auteur = 0, cible = 0, quoi = 0;
            ns_arene_issue issue = NS_ARENE_PASSEE;
            if (!ns_arene_lire_effet(p, len, &emetteur, &auteur, &cible, &quoi,
                                     &issue)) {
                return;
            }
            if (!accepter_arbitre(a, emetteur)) return;   /* comme le verdict */
            ev.type = NS_ARENE_EVT_EFFET;
            ev.a = auteur;
            ev.b = cible;
            ev.valeur = (int32_t)quoi;
            ev.issue = issue;
            break;
        }

        default:
            /* Un type inconnu est IGNORÉ, pas fatal : c'est ce qui permettra
             * d'ajouter une trame sans casser les clients déjà installés. Sa
             * longueur est déclarée, donc on sait toujours la sauter. */
            return;
    }

    SDL_LockMutex(a->verrou);
    pousser(a, &ev);
    SDL_UnlockMutex(a->verrou);
}

/* Vrai tant que la liaison tient. */
static bool recevoir(ns_arene *a)
{
    for (;;) {
        if (a->rx_len >= sizeof a->rx) {
            /* Impossible tant que la plus grosse trame tient dans RX_MAX, mais
             * le vérifier coûte une comparaison et évite d'écrire hors du
             * tampon si jamais une borne changeait. */
            poser_erreur(a, "tampon de reception plein");
            return false;
        }
#if defined(_WIN32)
        const int n = recv(a->sock, (char *)a->rx + a->rx_len,
                           (int)(sizeof a->rx - a->rx_len), 0);
#else
        const ssize_t n = recv(a->sock, a->rx + a->rx_len,
                               sizeof a->rx - a->rx_len, 0);
#endif
        if (n > 0) { a->rx_len += (size_t)n; continue; }
        if (n == 0) {
            /*
             * Le relais a raccroché. Trois raisons possibles et on ne peut pas
             * les distinguer d'ici : entrée refusée (place prise, salon déjà
             * lancé, places attendues divergentes), fin de salon, ou panne. Le
             * seul indice honnête est de savoir si le relais nous avait jamais
             * répondu.
             */
            if (!a->tableau_vu) {
                poser_erreur(a, "entree refusee : place %u du salon %llu",
                             (unsigned)a->ma_place,
                             (unsigned long long)a->salon);
            }
            SDL_SetAtomicInt(&a->liaison, (int)NS_ARENE_TERMINEE);
            return false;
        }
        if (ns_ar_would_block()) break;
        poser_erreur(a, "liaison interrompue");
        return false;
    }

    /* LE DÉCOUPAGE. TCP est un FLUX : une trame peut arriver en deux morceaux,
     * et deux trames en un seul paquet. `ns_arene_trame` est la seule à savoir
     * le faire, ici comme dans le test. */
    size_t off = 0;
    for (;;) {
        uint8_t type = 0;
        const uint8_t *charge = NULL;
        uint16_t clen = 0;
        const int pris = ns_arene_trame(a->rx + off, a->rx_len - off,
                                        &type, &charge, &clen);
        if (pris == 0) break;
        if (pris < 0) {
            poser_erreur(a, "trame trop longue annoncee, maximum %d",
                         NS_ARENE_CHARGE_MAX);
            return false;
        }
        traiter(a, type, charge, clen);
        off += (size_t)pris;
    }
    if (off) {
        SDL_memmove(a->rx, a->rx + off, a->rx_len - off);
        a->rx_len -= off;
    }
    return true;
}

/* Vide la file de sortie autant que la socket l'accepte. */
static bool emettre(ns_arene *a)
{
    bool vivant = true;
    SDL_LockMutex(a->verrou);
    while (a->tx_len) {
#if defined(_WIN32)
        const int n = send(a->sock, (const char *)a->tx, (int)a->tx_len, 0);
#else
        const ssize_t n = send(a->sock, a->tx, a->tx_len, 0);
#endif
        if (n > 0) {
            a->tx_len -= (size_t)n;
            if (a->tx_len) SDL_memmove(a->tx, a->tx + n, a->tx_len);
            continue;
        }
        if (n < 0 && ns_ar_would_block()) break;   /* on réessaiera au prochain tic */
        vivant = false;
        break;
    }
    SDL_UnlockMutex(a->verrou);
    if (!vivant) poser_erreur(a, "envoi interrompu");
    return vivant;
}

/* ==========================================================================
 * Le fil
 * ========================================================================== */

static int SDLCALL fil_arene(void *ud)
{
    ns_arene *a = (ns_arene *)ud;

    if (!connecter(a)) {
        if (SDL_GetAtomicInt(&a->liaison) == (int)NS_ARENE_CONNEXION) {
            SDL_SetAtomicInt(&a->liaison, (int)NS_ARENE_ERREUR);
        }
        if (a->sock != NS_AR_INVALID) { ns_ar_close(a->sock); a->sock = NS_AR_INVALID; }
        return 0;
    }

    uint64_t prochain = SDL_GetTicks();
    while (!SDL_GetAtomicInt(&a->quit)) {
        if (!recevoir(a)) break;
        if (!emettre(a)) break;

        const uint64_t maintenant = SDL_GetTicks();
        if (maintenant >= prochain) {
            /*
             * LE BATTEMENT D'ÉTAT, à 4 Hz. On ne publie que si la salle a
             * DÉPOSÉ quelque chose : envoyer un état vide pendant que le salon
             * se remplit dirait aux autres qu'on est à zéro point alors qu'on
             * n'a rien dit du tout, et `etat_recu` cesserait de distinguer les
             * deux.
             */
            prochain = maintenant + NS_ARENE_PERIODE_MS;
            uint8_t charge[NS_ARENE_ETAT_OCTETS];
            bool pose;
            SDL_LockMutex(a->verrou);
            pose = a->mon_etat_pose;
            if (pose) SDL_memcpy(charge, a->mon_etat, sizeof charge);
            SDL_UnlockMutex(a->verrou);
            if (pose) poster(a, NS_ARENE_T_ETAT, charge, (uint16_t)sizeof charge);
        }
        SDL_Delay(TIC_MS);
    }

    if (SDL_GetAtomicInt(&a->liaison) != (int)NS_ARENE_TERMINEE &&
        a->erreur[0]) {
        SDL_SetAtomicInt(&a->liaison, (int)NS_ARENE_ERREUR);
    }
    if (a->sock != NS_AR_INVALID) { ns_ar_close(a->sock); a->sock = NS_AR_INVALID; }
    return 0;
}

/* ==========================================================================
 * Cycle de vie
 * ========================================================================== */

ns_arene *ns_arene_ouvrir(const ns_arene_config *cfg, char *erreur, size_t taille)
{
    if (erreur && taille) erreur[0] = '\0';
    if (!cfg || !cfg->hote || !cfg->hote[0] || cfg->port == 0) {
        if (erreur) SDL_snprintf(erreur, taille, "adresse de relais absente");
        return NULL;
    }
    if (cfg->places < NS_ARENE_PLACES_MIN || cfg->places > NS_ARENE_MAX_PLACES ||
        cfg->place >= cfg->places) {
        if (erreur) {
            SDL_snprintf(erreur, taille, "place %u hors d'un salon de %u",
                         (unsigned)cfg->place, (unsigned)cfg->places);
        }
        return NULL;
    }

    /*
     * LE VERROU HÉRITÉ, et c'est la première chose que fait cette fonction.
     *
     * Sans URL de serveur, ou sous `--offline`, `ns_online` n'a pas démarré et
     * ne rend aucune URL : il n'y a rien à ouvrir. La garantie « sans URL
     * configurée, aucune socket n'est ouverte » reste écrite à UN SEUL endroit,
     * `ns_online_init`, et ce module en hérite exactement comme
     * `ns_realtime_init`. Le contrôle est ICI, avant toute allocation et avant
     * tout `socket()` — `ns_arene_sockets()` le mesure.
     */
    if (!ns_online_server_url()) {
        NS_INFO("arene : demandee, mais le reseau est inactif "
                "(pas de serveur, ou --offline) — rien ne sera ouvert");
        if (erreur) {
            SDL_snprintf(erreur, taille,
                         "reseau inactif : aucune socket n'est ouverte");
        }
        return NULL;
    }

    ns_arene *a = (ns_arene *)SDL_calloc(1, sizeof *a);
    if (!a) {
        if (erreur) SDL_snprintf(erreur, taille, "memoire insuffisante");
        return NULL;
    }
    a->sock = NS_AR_INVALID;
    SDL_strlcpy(a->hote, cfg->hote, sizeof a->hote);
    a->port = cfg->port;
    a->salon = cfg->salon;
    a->ma_place = cfg->place;
    a->places_attendues = cfg->places;
    SDL_strlcpy(a->pseudo, cfg->pseudo ? cfg->pseudo : "", sizeof a->pseudo);
    a->delai_ms = cfg->delai_ms ? cfg->delai_ms : 2000u;
    for (int i = 0; i < NS_ARENE_MAX_PLACES; ++i) a->place[i].camp = (uint8_t)i;

    a->verrou = SDL_CreateMutex();
    if (!a->verrou) {
        SDL_free(a);
        if (erreur) SDL_snprintf(erreur, taille, "verrou indisponible");
        return NULL;
    }
    SDL_SetAtomicInt(&a->quit, 0);
    SDL_SetAtomicInt(&a->liaison, (int)NS_ARENE_CONNEXION);

    a->fil = SDL_CreateThread(fil_arene, "nineteen-arene", a);
    if (!a->fil) {
        SDL_DestroyMutex(a->verrou);
        SDL_free(a);
        if (erreur) SDL_snprintf(erreur, taille, "fil indisponible");
        return NULL;
    }
    NS_INFO("arene : salon %llu, place %u sur %u, relais %s:%u",
            (unsigned long long)a->salon, (unsigned)a->ma_place,
            (unsigned)a->places_attendues, a->hote, (unsigned)a->port);
    return a;
}

void ns_arene_fermer(ns_arene *a)
{
    if (!a) return;
    SDL_SetAtomicInt(&a->quit, 1);
    SDL_WaitThread(a->fil, NULL);
    /* La socket est fermée PAR LE FIL, à sa sortie : elle n'appartient qu'à
     * lui, et la fermer ici pendant qu'il lit encore serait la seule course de
     * ce fichier. */
    SDL_DestroyMutex(a->verrou);
    SDL_free(a);
}

ns_arene_liaison ns_arene_etat(ns_arene *a)
{
    return a ? (ns_arene_liaison)SDL_GetAtomicInt(&a->liaison) : NS_ARENE_OFF;
}

const char *ns_arene_erreur(ns_arene *a)
{
    return (a && a->erreur[0]) ? a->erreur : "";
}

uint64_t ns_arene_graine(ns_arene *a)
{
    if (!a) return 0;
    SDL_LockMutex(a->verrou);
    const uint64_t g = a->graine;
    SDL_UnlockMutex(a->verrou);
    return g;
}

uint8_t ns_arene_ma_place(const ns_arene *a) { return a ? a->ma_place : 0u; }

bool ns_arene_arbitre(ns_arene *a)
{
    if (!a) return true;                      /* hors ligne : le même chemin */
    const ns_arene_liaison e = ns_arene_etat(a);
    /*
     * LA REPRISE DE LA LAME, PREMIER CAS. Une liaison morte veut dire que
     * l'arbitre ne parlera plus. Continuer à l'attendre figerait la manche à
     * jamais, ce qui est le seul comportement interdit de ce fichier : on
     * reprend la lame, quelle que soit la place occupée, et on finit avec les
     * rivaux locaux.
     */
    if (e == NS_ARENE_OFF || e == NS_ARENE_TERMINEE || e == NS_ARENE_ERREUR) {
        return true;
    }

    bool oui;
    SDL_LockMutex(a->verrou);
    if (!a->tableau_vu) {
        /* Le tableau n'est pas encore arrivé : on ne sait rien de personne, et
         * la seule réponse possible est la règle nue. */
        oui = (a->ma_place == 0u);
    } else {
        /*
         * LA REPRISE DE LA LAME, SECOND CAS — et c'est celui qui arrive
         * vraiment. La place 0 raccroche au milieu d'une manche : le relais
         * nous laisse notre socket, la liaison est parfaitement vivante, et
         * plus personne n'avance le couperet. Un salon de sept joueurs
         * attendrait alors pour l'éternité un verdict que personne n'envoie.
         *
         * L'arbitre est donc LA PLUS PETITE PLACE PRÉSENTE, et non la place 0.
         * Tout le monde en décide sur le MÊME tableau — le relais le diffuse
         * identique à toutes les places — donc tout le monde tombe d'accord
         * sans qu'aucune négociation soit nécessaire.
         */
        oui = (ns_arene_arbitre_de(masque_present(a)) == a->ma_place);
    }
    SDL_UnlockMutex(a->verrou);
    return oui;
}

/* ==========================================================================
 * Déposer
 * ========================================================================== */

void ns_arene_publier(ns_arene *a, bool vivante, uint8_t camp,
                      const char *jeu, bool hard, int64_t score,
                      int32_t points, int32_t fusibles, int32_t valeur)
{
    if (!a) return;
    uint8_t charge[NS_ARENE_ETAT_OCTETS];
    if (!ns_arene_ecrire_etat(charge, sizeof charge, vivante, camp, jeu, hard,
                              score, points, fusibles, valeur)) {
        return;
    }
    SDL_LockMutex(a->verrou);
    SDL_memcpy(a->mon_etat, charge, sizeof charge);
    a->mon_etat_pose = true;
    SDL_UnlockMutex(a->verrou);
}

void ns_arene_agir(ns_arene *a, uint8_t cible, uint8_t action)
{
    if (!a) return;
    uint8_t charge[NS_ARENE_ACTION_OCTETS];
    if (!ns_arene_ecrire_action(charge, sizeof charge, cible, action)) return;
    poster(a, NS_ARENE_T_ACTION, charge, (uint16_t)sizeof charge);
}

void ns_arene_verdict(ns_arene *a, uint8_t sortie, uint8_t vainqueur,
                      uint8_t numero, uint32_t horloge_ms)
{
    /*
     * DEUX REFUS ET NON UN. `a == NULL` d'abord : hors ligne on EST l'arbitre —
     * `ns_arene_arbitre(NULL)` rend vrai, c'est toute la promesse du chemin de
     * code unique — mais il n'y a personne à qui diffuser. Ne garder que le
     * second test faisait passer le nul jusqu'à la file de sortie.
     */
    if (!a || !ns_arene_arbitre(a)) return;
    uint8_t charge[NS_ARENE_VERDICT_OCTETS];
    if (!ns_arene_ecrire_verdict(charge, sizeof charge, sortie, vainqueur,
                                 numero, horloge_ms)) {
        return;
    }
    poster(a, NS_ARENE_T_VERDICT, charge, (uint16_t)sizeof charge);
}

void ns_arene_effet(ns_arene *a, uint8_t auteur, uint8_t cible,
                    uint8_t action, ns_arene_issue issue)
{
    if (!a || !ns_arene_arbitre(a)) return;   /* hors ligne : personne à qui parler */
    uint8_t charge[NS_ARENE_EFFET_OCTETS];
    if (!ns_arene_ecrire_effet(charge, sizeof charge, auteur, cible, action,
                               issue)) {
        return;
    }
    poster(a, NS_ARENE_T_EFFET, charge, (uint16_t)sizeof charge);
}

/* ==========================================================================
 * Relire
 * ========================================================================== */

uint32_t ns_arene_places(ns_arene *a, ns_arene_place *out, uint32_t max)
{
    if (!a || !out || !max) return 0;
    const uint64_t maintenant = SDL_GetTicks();
    uint32_t n = 0;

    SDL_LockMutex(a->verrou);
    for (uint32_t i = 0; i < NS_ARENE_MAX_PLACES && n < max; ++i) {
        out[n] = a->place[i];
        if (a->vu_ms[i] == 0) {
            out[n].age_ms = UINT32_MAX;
        } else {
            const uint64_t d = maintenant - a->vu_ms[i];
            out[n].age_ms = (d > UINT32_MAX) ? UINT32_MAX : (uint32_t)d;
        }
        n++;
    }
    SDL_UnlockMutex(a->verrou);
    return n;
}

bool ns_arene_prendre(ns_arene *a, ns_arene_evenement *out)
{
    if (!a || !out) return false;
    bool eu = false;
    SDL_LockMutex(a->verrou);
    if (a->n_file) {
        *out = a->file[a->tete];
        a->tete = (a->tete + 1u) % FILE_MAX;
        a->n_file--;
        eu = true;
    }
    SDL_UnlockMutex(a->verrou);
    return eu;
}

void ns_arene_stats(ns_arene *a, uint32_t *trames_recues,
                    uint32_t *trames_envoyees, uint32_t *perdus)
{
    if (trames_recues)   *trames_recues = 0;
    if (trames_envoyees) *trames_envoyees = 0;
    if (perdus)          *perdus = 0;
    if (!a) return;
    SDL_LockMutex(a->verrou);
    if (trames_recues)   *trames_recues = a->trames_in;
    if (trames_envoyees) *trames_envoyees = a->trames_out;
    if (perdus)          *perdus = a->perdus;
    SDL_UnlockMutex(a->verrou);
}
