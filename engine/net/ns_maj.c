/* ns_maj.c — voir ns_maj.h pour les trois règles qui gouvernent ce fichier. */

#include "ns_maj.h"

#include "ns_config.h"
#include "ns_core.h"
#include "ns_http.h"
#include "ns_json.h"
#include "ns_runlog.h"

#include <SDL3/SDL.h>

#include <stdarg.h>

static struct {
    bool          active;
    char          url[512];
    char          version[32];      /* celle qu'on est */
    bool          auto_transfert;

    SDL_Thread   *fil;
    SDL_Mutex    *verrou;
    SDL_AtomicInt quit;
    SDL_AtomicInt veut_transfert;

    ns_maj_etat   etat;
    char          offerte[32];      /* celle que le serveur propose */
    char          message[192];
    char          nom[128];         /* nom du fichier chez le serveur */
    char          adresse[640];     /* son URL complète */
    char          somme[80];        /* l'empreinte annoncée, en hexadécimal */
    int64_t       octets;
    char          paquet[1024];     /* le chemin local, une fois vérifié */
    float         avancement;
    char          repertoire[1024];
} g;

static char g_repertoire_force[1024];

/* ==========================================================================
 * Le calcul pur : comparer, et choisir. Testable sans une socket.
 * ========================================================================== */

int ns_maj_comparer(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    for (int champ = 0; champ < 4; ++champ) {
        long va = 0, vb = 0;
        while (*a >= '0' && *a <= '9') va = va * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') vb = vb * 10 + (*b++ - '0');
        if (va != vb) return (va < vb) ? -1 : 1;
        /* Un séparateur, et un seul : tout le reste arrête la lecture. Voir
         * l'en-tête sur les pré-versions. */
        if (*a == '.') ++a; else a = "";
        if (*b == '.') ++b; else b = "";
        if (!*a && !*b) break;
    }
    return 0;
}

const char *ns_maj_plateforme(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

const char *ns_maj_arch(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "";
#endif
}

bool ns_maj_convient(const char *plateforme, const char *arch)
{
    if (!plateforme || SDL_strcasecmp(plateforme, ns_maj_plateforme()) != 0) return false;
    if (!arch || !arch[0]) return true;
    if (SDL_strcasecmp(arch, "universel") == 0) return true;
    const char *nous = ns_maj_arch();
    /* Une architecture inconnue de ce binaire ne peut rien confirmer : on
     * refuse plutôt que de proposer un paquet qui ne démarrera pas. */
    return nous[0] && SDL_strcasecmp(arch, nous) == 0;
}

/* Vrai si `nom` se termine par `suffixe`, sans tenir compte de la casse. */
static bool finit_par(const char *nom, const char *suffixe)
{
    const size_t n = SDL_strlen(nom), m = SDL_strlen(suffixe);
    return m <= n && SDL_strcasecmp(nom + n - m, suffixe) == 0;
}

bool ns_maj_porte_version(const char *nom, const char *version)
{
    if (!nom || !version || !version[0]) return false;
    /* Une sous-chaîne suffit, mais elle doit être BORNÉE : sans ça « 17.1.0 »
     * serait trouvé dans « 17.1.01 », et « 17.1 » dans « 17.10 ». Le caractère
     * qui suit ne doit donc pas être un chiffre ni un point. */
    const size_t n = SDL_strlen(version);
    for (const char *p = nom; *p; ++p) {
        if (SDL_strncmp(p, version, n) != 0) continue;
        const char suivant = p[n];
        if ((suivant >= '0' && suivant <= '9') || suivant == '.') continue;
        return true;
    }
    return false;
}

int ns_maj_rang_paquet(const char *nom)
{
    if (!nom || !nom[0]) return -1;
    /* Le même découpage que `Classer` côté serveur, et dans le même ordre : les
     * deux listes doivent se lire ensemble. */
    static const struct { const char *suffixe; int rang; } TABLE[] = {
#if defined(_WIN32)
        { ".exe", 0 }, { ".msi", 1 }, { ".zip", 2 },
#elif defined(__APPLE__)
        { ".dmg", 0 }, { ".pkg", 1 },
#else
        { ".appimage", 0 }, { ".deb", 1 }, { ".rpm", 2 }, { ".tar.gz", 3 },
#endif
    };
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; ++i) {
        if (finit_par(nom, TABLE[i].suffixe)) return TABLE[i].rang;
    }
    return -1;
}

/* ==========================================================================
 * L'état, sous verrou
 * ========================================================================== */

static void poser(ns_maj_etat etat, const char *format, ...) SDL_PRINTF_VARARG_FUNC(2);

static void poser(ns_maj_etat etat, const char *format, ...)
{
    SDL_LockMutex(g.verrou);
    const bool change = (g.etat != etat);
    g.etat = etat;
    va_list ap;
    va_start(ap, format);
    SDL_vsnprintf(g.message, sizeof g.message, format, ap);
    va_end(ap);
    char copie[192];
    SDL_strlcpy(copie, g.message, sizeof copie);
    SDL_UnlockMutex(g.verrou);

    /*
     * CHAQUE CHANGEMENT D'ÉTAT S'ÉCRIT DANS LE JOURNAL, et c'est la seule
     * chose qui reste quand un joueur dit « ça ne se met jamais à jour ». Le
     * bandeau, lui, disparaît avec la fenêtre. Une fois par changement et pas
     * une par tour de boucle : le fil repasse dix fois par seconde.
     */
    if (change) {
        if (etat == NS_MAJ_ECHEC) NS_WARN("mise à jour : %s", copie);
        else                      NS_INFO("mise à jour : %s", copie);
    }
}

const char *ns_maj_repertoire(void)
{
    if (g_repertoire_force[0]) return g_repertoire_force;
    if (!g.repertoire[0]) {
        const char *dir = ns_path_user_dir();
        SDL_snprintf(g.repertoire, sizeof g.repertoire, "%smaj", dir ? dir : "");
    }
    return g.repertoire;
}

void ns_maj_set_repertoire(const char *chemin)
{
    if (chemin && *chemin) SDL_strlcpy(g_repertoire_force, chemin, sizeof g_repertoire_force);
    else                   g_repertoire_force[0] = '\0';
}

/* ==========================================================================
 * Le fil
 * ========================================================================== */

/* L'empreinte du fichier, en minuscules, pour la comparer à celle du serveur. */
static bool empreinte_hex(const char *chemin, char *out, size_t cap)
{
    uint8_t brut[32];
    if (!ns_sha256_fichier(chemin, brut)) return false;
    if (cap < 65) return false;
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[i * 2]     = HEX[brut[i] >> 4];
        out[i * 2 + 1] = HEX[brut[i] & 15];
    }
    out[64] = '\0';
    return true;
}

/*
 * INTERROGER LE SERVEUR : une requête, et tout ce qu'il faut est dedans.
 *
 * `/api/v1/telechargements` rend la version, l'adresse publique et la liste des
 * fichiers avec leur plateforme, leur architecture, leur taille et leur
 * empreinte. C'est la même route que la page web, et c'est voulu : deux sources
 * de vérité pour « qu'est-ce qui est téléchargeable » finiraient par diverger,
 * et c'est déjà arrivé une fois, quand le navigateur bâtissait les liens tout
 * seul à partir du seul numéro de version.
 */
static bool interroger(void)
{
    char url[640];
    SDL_snprintf(url, sizeof url, "%s/api/v1/telechargements", g.url);

    ns_http_response r;
    if (!ns_http_request("GET", url, NULL, NULL, NULL, 0, 4000, &r)) {
        poser(NS_MAJ_ECHEC, "serveur injoignable");
        ns_http_response_free(&r);
        return false;
    }
    if (r.status != 200 || !r.body) {
        poser(NS_MAJ_ECHEC, "le serveur répond %d", r.status);
        ns_http_response_free(&r);
        return false;
    }

    ns_arena arene;
    if (!ns_arena_init(&arene, 128u * 1024u, "json mise à jour")) {
        ns_http_response_free(&r);
        return false;
    }

    bool ok = false;
    ns_json doc;
    if (ns_json_parse(&doc, r.body, r.length, &arene)) {
        const ns_json_value *racine = ns_json_root(&doc);
        char offerte[32] = { 0 };
        ns_json_get_string(&doc, racine, "version", offerte, sizeof offerte);

        if (!offerte[0]) {
            poser(NS_MAJ_ECHEC, "le serveur n'annonce aucune version");
        } else if (ns_maj_comparer(offerte, g.version) <= 0) {
            poser(NS_MAJ_A_JOUR, "à jour (%s)", g.version);
            ok = true;
        } else if (SDL_strcmp(offerte, ns_config_get_str(NS_CFG_MAJ_REFUSEE, "")) == 0) {
            /* Refusée une fois, plus jamais reproposée. */
            poser(NS_MAJ_A_JOUR, "version %s écartée par le joueur", offerte);
            ok = true;
        } else {
            /* Le paquet de CETTE machine, s'il y en a un. */
            const ns_json_value *fichiers = ns_json_get(&doc, racine, "fichiers");
            const int n = ns_json_array_count(&doc, fichiers);
            char nom[128] = { 0 }, chemin[256] = { 0 }, somme[80] = { 0 };
            int64_t octets = 0;
            int meilleur = -1;
            for (int i = 0; i < n; ++i) {
                const ns_json_value *f = ns_json_at(&doc, fichiers, i);
                char plateforme[24] = { 0 }, arch[24] = { 0 }, candidat[128] = { 0 };
                ns_json_get_string(&doc, f, "plateforme", plateforme, sizeof plateforme);
                ns_json_get_string(&doc, f, "arch", arch, sizeof arch);
                if (!ns_maj_convient(plateforme, arch)) continue;
                ns_json_get_string(&doc, f, "nom", candidat, sizeof candidat);
                /*
                 * LE PAQUET DOIT PORTER LA VERSION ANNONCÉE, et cette ligne
                 * vient d'une mesure.
                 *
                 * Un répertoire garde les anciennes versions à côté des
                 * nouvelles — c'est même sa raison d'être, puisqu'on peut
                 * vouloir redescendre. Le serveur annonce alors la plus
                 * récente, mais la liste contient les deux, et deux `.dmg` ont
                 * évidemment la même extension : sans ce contrôle, le premier
                 * de la liste gagnait. Mesuré sur la pile réelle — le jeu
                 * annonçait « 17.1.0 disponible (175 Mio) » et s'apprêtait à
                 * télécharger le paquet 17.0.0, qu'il aurait installé pour
                 * revenir exactement là d'où il partait.
                 */
                if (!ns_maj_porte_version(candidat, offerte)) continue;
                const int rang = ns_maj_rang_paquet(candidat);
                if (rang < 0 || (meilleur >= 0 && rang >= meilleur)) continue;
                meilleur = rang;
                SDL_strlcpy(nom, candidat, sizeof nom);
                ns_json_get_string(&doc, f, "url", chemin, sizeof chemin);
                ns_json_get_string(&doc, f, "sha256", somme, sizeof somme);
                octets = ns_json_get_i64(&doc, f, "octets", 0);
            }

            if (!nom[0] || !chemin[0]) {
                /*
                 * Une version plus récente existe mais pas pour cette machine.
                 * On le DIT, au lieu de se taire : c'est le cas du joueur
                 * Windows devant un serveur qui n'héberge que du Linux, et
                 * lui laisser croire qu'il est à jour serait faux.
                 */
                poser(NS_MAJ_A_JOUR, "%s existe, mais aucun paquet %s sur ce serveur",
                      offerte, ns_maj_plateforme());
                ok = true;
            } else {
                SDL_LockMutex(g.verrou);
                SDL_strlcpy(g.offerte, offerte, sizeof g.offerte);
                SDL_strlcpy(g.nom, nom, sizeof g.nom);
                SDL_strlcpy(g.somme, somme, sizeof g.somme);
                g.octets = octets;
                /* L'adresse vient du serveur, jamais d'une concaténation
                 * devinée : c'est exactement la faute que la page web a
                 * commise, et elle avait produit trois liens morts. */
                if (SDL_strncasecmp(chemin, "http://", 7) == 0
                    || SDL_strncasecmp(chemin, "https://", 8) == 0) {
                    SDL_strlcpy(g.adresse, chemin, sizeof g.adresse);
                } else {
                    SDL_snprintf(g.adresse, sizeof g.adresse, "%s%s", g.url, chemin);
                }
                SDL_UnlockMutex(g.verrou);
                poser(NS_MAJ_DISPONIBLE, "version %s disponible (%lld Mio)",
                      offerte, (long long)((octets + 524288) / 1048576));
                ok = true;
            }
        }
    } else {
        poser(NS_MAJ_ECHEC, "réponse illisible");
    }

    ns_arena_free(&arene);
    ns_http_response_free(&r);
    return ok;
}

static bool avancer(void *contexte, int64_t recu, int64_t total)
{
    (void)contexte;
    SDL_LockMutex(g.verrou);
    g.avancement = (total > 0) ? (float)((double)recu / (double)total) : 0.0f;
    SDL_UnlockMutex(g.verrou);
    /* Fermer le jeu ARRÊTE le transfert. Le fichier partiel reste, et la
     * prochaine fois reprendra où l'on s'est arrêté. */
    return !SDL_GetAtomicInt(&g.quit);
}

static void transferer(void)
{
    char adresse[640], nom[128], somme[80];
    SDL_LockMutex(g.verrou);
    SDL_strlcpy(adresse, g.adresse, sizeof adresse);
    SDL_strlcpy(nom, g.nom, sizeof nom);
    SDL_strlcpy(somme, g.somme, sizeof somme);
    SDL_UnlockMutex(g.verrou);
    if (!adresse[0]) return;

    const char *dir = ns_maj_repertoire();
    if (!SDL_CreateDirectory(dir)) {
        poser(NS_MAJ_ECHEC, "« %s » inaccessible", dir);
        return;
    }

    char chemin[1024];
    SDL_snprintf(chemin, sizeof chemin, "%s/%s", dir, nom);

    poser(NS_MAJ_TRANSFERT, "téléchargement de %s", nom);

    int statut = 0;
    char erreur[128] = { 0 };
    /*
     * Quatre-vingt-dix secondes de délai par lecture, et non les quatre du
     * classement : c'est un délai d'INACTIVITÉ, pas une durée totale, et un
     * paquet de 175 Mio sur une ligne ordinaire prend plusieurs minutes sans
     * qu'aucune lecture ne dure plus d'une seconde. Trop court, on couperait un
     * transfert sain ; c'est la reprise qui protège du reste.
     */
    const bool recu = ns_http_telecharger(adresse, chemin, 90000,
                                          avancer, NULL, &statut,
                                          erreur, sizeof erreur);
    if (SDL_GetAtomicInt(&g.quit)) return;
    if (!recu) {
        poser(NS_MAJ_ECHEC, "transfert : %s", erreur[0] ? erreur : "interrompu");
        return;
    }

    /*
     * L'EMPREINTE, avant de proposer quoi que ce soit.
     *
     * Un paquet tronqué ou altéré ne doit jamais arriver devant le joueur. Sans
     * empreinte annoncée par le serveur on ne peut rien affirmer : on refuse
     * plutôt que de faire semblant, et le fichier est effacé pour qu'une
     * reprise ne s'appuie pas dessus.
     */
    if (!somme[0]) {
        SDL_RemovePath(chemin);
        poser(NS_MAJ_ECHEC, "le serveur ne publie pas l'empreinte de %s", nom);
        return;
    }
    char calculee[80];
    if (!empreinte_hex(chemin, calculee, sizeof calculee)) {
        poser(NS_MAJ_ECHEC, "%s ne se relit pas", nom);
        return;
    }
    if (SDL_strcasecmp(calculee, somme) != 0) {
        SDL_RemovePath(chemin);
        poser(NS_MAJ_ECHEC, "empreinte de %s incorrecte, paquet écarté", nom);
        NS_WARN("mise à jour : empreinte %s attendue, %s calculée — fichier effacé",
                somme, calculee);
        return;
    }

    SDL_LockMutex(g.verrou);
    SDL_strlcpy(g.paquet, chemin, sizeof g.paquet);
    g.avancement = 1.0f;
    SDL_UnlockMutex(g.verrou);
    poser(NS_MAJ_PRETE, "%s prête à installer", g.offerte);
    NS_INFO("mise à jour : « %s » vérifié (%s)", chemin, somme);
}

static int SDLCALL fil_maj(void *inutile)
{
    (void)inutile;

    if (!interroger()) return 0;

    for (;;) {
        if (SDL_GetAtomicInt(&g.quit)) return 0;

        SDL_LockMutex(g.verrou);
        const ns_maj_etat etat = g.etat;
        /*
         * UN ÉCHEC DE TRANSFERT RESTE RATTRAPABLE, et c'est ce qui distingue
         * les deux échecs possibles.
         *
         * Si `interroger` a échoué, on ne sait rien : pas d'adresse, rien à
         * réessayer, le fil s'éteint. Mais un transfert coupé au milieu, lui,
         * a tout ce qu'il faut pour recommencer — et il recommencera là où il
         * s'est arrêté, puisque le fichier partiel est resté. Sans ce cas, une
         * coupure de réseau condamnait la mise à jour jusqu'au prochain
         * lancement du jeu, et la touche ne répondait plus.
         */
        const bool rattrapable = g.adresse[0] != '\0';
        SDL_UnlockMutex(g.verrou);

        const bool demande = SDL_CompareAndSwapAtomicInt(&g.veut_transfert, 1, 0);
        if (etat == NS_MAJ_DISPONIBLE && (demande || g.auto_transfert)) {
            transferer();
        } else if (etat == NS_MAJ_ECHEC && rattrapable && demande) {
            transferer();
        } else if (etat != NS_MAJ_DISPONIBLE
                   && !(etat == NS_MAJ_ECHEC && rattrapable)) {
            return 0;   /* plus rien à faire : le fil s'éteint */
        }
        /*
         * 200 ms. Le fil n'attend qu'un appui sur une touche, et un quart de
         * seconde avant qu'un téléchargement de plusieurs minutes ne démarre ne
         * se voit pas. À 100 ms on réveillait le processeur dix fois par
         * seconde pour toute la durée d'une session, sans rien y gagner.
         */
        SDL_Delay(200);
    }
}

/* ==========================================================================
 * Interface
 * ========================================================================== */

bool ns_maj_init(const ns_maj_config *cfg)
{
    SDL_zero(g);
    g.etat = NS_MAJ_INACTIVE;
    SDL_strlcpy(g.message, "mise à jour : inactive", sizeof g.message);

    if (!cfg || cfg->locked || !cfg->server_url || !cfg->server_url[0]) return false;
    SDL_strlcpy(g.url, cfg->server_url, sizeof g.url);
    SDL_strlcpy(g.version, (cfg->version && cfg->version[0]) ? cfg->version : "0",
                sizeof g.version);
    g.auto_transfert = cfg->auto_transfert;

    /* Une barre oblique finale ferait « http://hôte//api/v1/… ». Deux serveurs
     * sur trois s'en accommodent, ce qui est la pire des situations : ça marche
     * jusqu'au jour où ça ne marche plus. */
    size_t n = SDL_strlen(g.url);
    while (n > 0 && g.url[n - 1] == '/') g.url[--n] = '\0';

    g.verrou = SDL_CreateMutex();
    if (!g.verrou) return false;

    g.active = true;
    g.etat = NS_MAJ_QUESTION;
    SDL_strlcpy(g.message, "mise à jour : question posée", sizeof g.message);

    g.fil = SDL_CreateThread(fil_maj, "nineteen-maj", NULL);
    if (!g.fil) {
        g.active = false;
        g.etat = NS_MAJ_INACTIVE;
        SDL_DestroyMutex(g.verrou);
        g.verrou = NULL;
        return false;
    }
    return true;
}

void ns_maj_shutdown(void)
{
    if (!g.active) return;
    SDL_SetAtomicInt(&g.quit, 1);
    if (g.fil) { SDL_WaitThread(g.fil, NULL); g.fil = NULL; }
    if (g.verrou) { SDL_DestroyMutex(g.verrou); g.verrou = NULL; }
    g.active = false;
    g.etat = NS_MAJ_INACTIVE;
}

ns_maj_etat ns_maj_etat_courant(void)
{
    if (!g.active) return NS_MAJ_INACTIVE;
    SDL_LockMutex(g.verrou);
    const ns_maj_etat e = g.etat;
    SDL_UnlockMutex(g.verrou);
    return e;
}

const char *ns_maj_version_offerte(void) { return g.offerte; }
const char *ns_maj_message(void)         { return g.message; }
const char *ns_maj_paquet(void)          { return g.paquet; }

float ns_maj_avancement(void)
{
    if (!g.active) return 0.0f;
    SDL_LockMutex(g.verrou);
    const float a = g.avancement;
    SDL_UnlockMutex(g.verrou);
    return a;
}

void ns_maj_telecharger(void)
{
    const ns_maj_etat e = ns_maj_etat_courant();
    /* Un échec de transfert se réessaie : voir `fil_maj`. Le fichier partiel
     * est resté, et la reprise repart de là. */
    if (e != NS_MAJ_DISPONIBLE && !(e == NS_MAJ_ECHEC && g.adresse[0])) return;
    SDL_SetAtomicInt(&g.veut_transfert, 1);
}

bool ns_maj_installer(void)
{
    if (ns_maj_etat_courant() != NS_MAJ_PRETE || !g.paquet[0]) return false;

    /*
     * `file://` et non le chemin nu : `SDL_OpenURL` passe la chaîne au système,
     * qui attend une URL. Un chemin macOS contient des espaces — « Application
     * Support » — et un chemin Windows commence par une lettre de lecteur ; les
     * deux se lisent mal sans schéma.
     */
    char lien[1200];
#if defined(_WIN32)
    SDL_snprintf(lien, sizeof lien, "file:///%s", g.paquet);
    for (char *p = lien; *p; ++p) if (*p == '\\') *p = '/';
#else
    SDL_snprintf(lien, sizeof lien, "file://%s", g.paquet);
#endif
    if (!SDL_OpenURL(lien)) {
        NS_WARN("mise à jour : impossible d'ouvrir « %s » : %s", g.paquet, SDL_GetError());
        return false;
    }
    NS_INFO("mise à jour : « %s » confié au système", g.paquet);
    return true;
}

void ns_maj_refuser(void)
{
    if (!g.offerte[0]) return;
    ns_config_set_str(NS_CFG_MAJ_REFUSEE, g.offerte);
    (void)ns_config_save();
    poser(NS_MAJ_A_JOUR, "version %s écartée", g.offerte);
    NS_INFO("mise à jour : %s écartée, elle ne sera plus proposée", g.offerte);
}
