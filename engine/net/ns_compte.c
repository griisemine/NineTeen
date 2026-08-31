/* ns_compte.c — voir ns_compte.h pour ce que ce module refuse de faire. */
#include "ns_compte.h"

#include "ns_core.h"
#include "ns_http.h"
#include "ns_json.h"

#include <SDL3/SDL.h>

/* ==========================================================================
 * La file
 * ========================================================================== */

typedef enum genre {
    G_RIEN = 0,
    G_VERIFIER,      /* /me, avec le jeton gardé d'une session précédente */
    G_INSCRIRE,
    G_CONNECTER,
    G_DECONNECTER,
    G_LISTER,
    G_CREER,
    G_REJOINDRE,
    G_QUITTER,
    G_SUPPRIMER,
    G_BATTRE
} genre;

/*
 * Une requête, telle qu'elle attend son tour.
 *
 * Un seul type pour les onze genres, avec des champs qui ne servent pas
 * partout : la file reste un tableau plat, sans allocation, et l'on ne peut
 * pas oublier de libérer une requête abandonnée à l'arrêt du fil. Le prix est
 * une structure un peu large — mesurée à la compilation, voir le
 * `_Static_assert` plus bas — payée huit fois, une fois pour toutes.
 */
typedef struct requete {
    genre genre;
    char  texte[NS_SALON_NOM_MAX > NS_COMPTE_PSEUDO_MAX ? NS_SALON_NOM_MAX
                                                        : NS_COMPTE_PSEUDO_MAX];
    char  secret[NS_COMPTE_MDP_MAX];   /* mot de passe, effacé après envoi */
    char  code[NS_SALON_CODE_MAX];
    char  borne[32];
    int   a, b, c, d;
    bool  f, h;
} requete;

#define FILE_MAX 8

static struct {
    bool          actif;
    char          url[512];

    SDL_Thread   *fil;
    SDL_Mutex    *verrou;
    SDL_AtomicInt quitte;

    requete  file[FILE_MAX];
    int      tete, queue, en_file;

    /* Ce que l'écran lit. Toujours sous le verrou. */
    ns_compte_etat etat;
    bool           en_vol;
    char           pseudo[NS_COMPTE_PSEUDO_MAX];
    char           jeton[NS_COMPTE_JETON_MAX];
    bool           jeton_change;
    char           message[NS_COMPTE_MESSAGE_MAX];

    ns_salon_resume liste[NS_SALON_MAX_LISTE];
    int             listes;
    uint32_t        liste_a_ms;      /* horodatage de la dernière liste reçue */
    bool            liste_recue;

    ns_salon salon;
    bool     dans_un_salon;

    uint32_t envoyees, echouees;
} g;

/*
 * Le mot de passe pèse à lui seul plus que tout le reste de la requête. Le
 * dire à la compilation évite qu'un champ ajouté un jour fasse grossir la file
 * sans que personne le remarque.
 */
_Static_assert(sizeof(requete) < 1024, "une requête doit rester petite : la file en garde huit");

/* ==========================================================================
 * Petites aides
 * ========================================================================== */

static void dire(const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);

/* Sous le verrou de l'appelant. */
static void dire(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(g.message, sizeof g.message, fmt, ap);
    va_end(ap);
}

/*
 * Efface un secret pour de bon.
 *
 * `SDL_memset` sur un tampon dont plus personne ne se sert est exactement ce
 * qu'un compilateur a le droit de supprimer. Le `volatile` lui retire ce
 * droit. Ce n'est pas de la superstition : le mot de passe reste sinon lisible
 * dans la pile du fil de travail jusqu'à ce que quelque chose l'écrase, et un
 * vidage mémoire l'emporterait.
 */
static void effacer(char *p, size_t n)
{
    volatile char *v = (volatile char *)p;
    while (n--) *v++ = 0;
}

/* Échappe une chaîne pour un littéral JSON. Le corps est fabriqué à la main
 * ici : il tient en quatre champs, et lier un écrivain JSON complet pour ça
 * serait un mécanisme de plus à entretenir. Mais fabriquer du JSON par
 * `snprintf` sans échapper est la faute classique — un pseudo contenant un
 * guillemet casserait le document, et le serveur répondrait « corps illisible »
 * sans que personne comprenne pourquoi. */
static void echapper(const char *src, char *out, size_t taille)
{
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 7 < taille; ++i) {
        const unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c < 0x20) {
            j += (size_t)SDL_snprintf(out + j, taille - j, "\\u%04x", c);
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

/* ==========================================================================
 * Lecture des réponses
 * ========================================================================== */

/*
 * Le motif d'un refus vient du SERVEUR, pas d'ici.
 *
 * Recopier ses règles de ce côté-ci — « douze caractères minimum », « trois à
 * vingt-quatre caractères » — les ferait diverger le jour où elles changent,
 * et c'est la faute que ce dépôt nomme : deux descriptions d'une même chose
 * finissent toujours par se contredire. On affiche ce qu'il dit.
 */
static void motif_depuis_corps(const ns_http_response *r, int status)
{
    if (r->body && r->length) {
        ns_arena arene;
        if (ns_arena_init(&arene, 16u * 1024u, "json compte")) {
            ns_json doc;
            if (ns_json_parse(&doc, r->body, r->length, &arene)) {
                char err[NS_COMPTE_MESSAGE_MAX] = { 0 };
                ns_json_get_string(&doc, ns_json_root(&doc), "error", err, sizeof err);
                if (err[0]) {
                    SDL_snprintf(g.message, sizeof g.message, "%s", err);
                    ns_arena_free(&arene);
                    return;
                }
            }
            ns_arena_free(&arene);
        }
    }
    if (status == 0) dire("serveur injoignable");
    else             dire("serveur : %d", status);
}

static void lire_occupants(const ns_json *doc, const ns_json_value *obj,
                           ns_salon *s)
{
    const ns_json_value *arr = ns_json_get(doc, obj, "occupants");
    const int n = ns_json_array_count(doc, arr);
    s->occupants = 0;
    for (int i = 0; i < n && s->occupants < NS_SALON_MAX_PLACES; ++i) {
        const ns_json_value *e = ns_json_at(doc, arr, i);
        ns_salon_occupant *o = &s->occupant[s->occupants];
        SDL_zerop(o);
        o->place = (int)ns_json_get_i64(doc, e, "place", -1);
        /*
         * Une place hors bornes est jetée ici et pas plus loin. Le tableau fait
         * huit cases et l'écran l'indexe : c'est le seul endroit où la valeur
         * arrive du réseau, donc le seul où la vérifier a un sens.
         */
        if (o->place < 0 || o->place >= NS_SALON_MAX_PLACES) continue;
        ns_json_get_string(doc, e, "pseudo", o->pseudo, sizeof o->pseudo);
        o->camp     = (int)ns_json_get_i64(doc, e, "camp", 0);
        o->points   = (int)ns_json_get_i64(doc, e, "points", 0);
        o->fusibles = (int)ns_json_get_i64(doc, e, "fusibles", 0);
        o->vivante  = ns_json_get_bool(doc, e, "vivante", true);
        ns_json_get_string(doc, e, "borne", o->borne, sizeof o->borne);
        s->occupants++;
    }
}

/* Lit un salon complet — la forme rendue par créer, rejoindre et battre. */
static bool lire_salon(const ns_http_response *r, ns_salon *out)
{
    if (!r->body || !r->length) return false;

    ns_arena arene;
    if (!ns_arena_init(&arene, 64u * 1024u, "json salon")) return false;

    bool ok = false;
    ns_json doc;
    if (ns_json_parse(&doc, r->body, r->length, &arene)) {
        const ns_json_value *root = ns_json_root(&doc);
        ns_json_get_string(&doc, root, "code", out->code, sizeof out->code);
        if (out->code[0]) {
            ns_json_get_string(&doc, root, "nom", out->nom, sizeof out->nom);
            ns_json_get_string(&doc, root, "proprietaire", out->proprietaire,
                               sizeof out->proprietaire);
            ns_json_get_string(&doc, root, "etat", out->etat, sizeof out->etat);
            out->places = (int)ns_json_get_i64(&doc, root, "places", 0);
            out->camps  = (int)ns_json_get_i64(&doc, root, "camps", 1);
            out->prive  = ns_json_get_bool(&doc, root, "prive", false);
            out->place  = (int)ns_json_get_i64(&doc, root, "place", 0);

            /* Les mêmes bornes que côté occupants, pour la même raison. */
            if (out->places < 1 || out->places > NS_SALON_MAX_PLACES)
                out->places = NS_SALON_MAX_PLACES;
            if (out->place < 0 || out->place >= out->places) out->place = 0;
            if (out->camps < 1 || out->camps > 2) out->camps = 1;

            const ns_json_value *rel = ns_json_get(&doc, root, "relais");
            if (rel) {
                ns_json_get_string(&doc, rel, "hote", out->relais_hote,
                                   sizeof out->relais_hote);
                out->relais_port  = (uint16_t)ns_json_get_i64(&doc, rel, "port", 0);
                out->relais_salon = (uint64_t)ns_json_get_i64(&doc, rel, "salon", 0);
            }
            lire_occupants(&doc, root, out);
            ok = true;
        }
    }
    ns_arena_free(&arene);
    return ok;
}

/* ==========================================================================
 * Les requêtes, exécutées sur le fil de travail
 * ========================================================================== */

/* Rend le jeton courant, recopié sous le verrou : le fil ne doit pas lire
 * `g.jeton` pendant qu'une autre requête le remplace. */
static void copier_jeton(char *out, size_t taille)
{
    SDL_LockMutex(g.verrou);
    SDL_snprintf(out, taille, "%s", g.jeton);
    SDL_UnlockMutex(g.verrou);
}

static bool appel(const char *methode, const char *chemin, const char *corps,
                  ns_http_response *r)
{
    char url[640];
    SDL_snprintf(url, sizeof url, "%s%s", g.url, chemin);

    char jeton[NS_COMPTE_JETON_MAX];
    copier_jeton(jeton, sizeof jeton);

    g.envoyees++;
    const bool ok = ns_http_request(methode, url,
                                    corps ? "application/json" : NULL,
                                    jeton[0] ? jeton : NULL,
                                    corps, corps ? SDL_strlen(corps) : 0,
                                    6000, r);
    if (!ok || r->status < 200 || r->status >= 300) g.echouees++;
    effacer(jeton, sizeof jeton);
    return ok;
}

static void poser_jeton(const char *jeton, const char *pseudo)
{
    SDL_LockMutex(g.verrou);
    SDL_snprintf(g.jeton, sizeof g.jeton, "%s", jeton ? jeton : "");
    SDL_snprintf(g.pseudo, sizeof g.pseudo, "%s", pseudo ? pseudo : "");
    g.jeton_change = true;
    g.etat = g.jeton[0] ? NS_COMPTE_CONNECTE : NS_COMPTE_ANONYME;
    SDL_UnlockMutex(g.verrou);
}

/* Inscription et connexion ne diffèrent que par le chemin : même corps, même
 * réponse, mêmes suites. Les écrire deux fois aurait fait diverger la
 * deuxième. */
static void faire_auth(const requete *q, const char *chemin, const char *verbe)
{
    char pseudo[NS_COMPTE_PSEUDO_MAX * 2];
    char mdp[NS_COMPTE_MDP_MAX * 2];
    echapper(q->texte, pseudo, sizeof pseudo);
    echapper(q->secret, mdp, sizeof mdp);

    char corps[NS_COMPTE_MDP_MAX * 2 + NS_COMPTE_PSEUDO_MAX * 2 + 64];
    SDL_snprintf(corps, sizeof corps, "{\"username\":\"%s\",\"password\":\"%s\"}",
                 pseudo, mdp);
    effacer(mdp, sizeof mdp);

    ns_http_response r;
    const bool ok = appel("POST", chemin, corps, &r);
    effacer(corps, sizeof corps);

    if (ok && r.status >= 200 && r.status < 300) {
        char jeton[NS_COMPTE_JETON_MAX] = { 0 };
        char nom[NS_COMPTE_PSEUDO_MAX] = { 0 };
        ns_arena arene;
        if (ns_arena_init(&arene, 16u * 1024u, "json auth")) {
            ns_json doc;
            if (ns_json_parse(&doc, r.body, r.length, &arene)) {
                const ns_json_value *root = ns_json_root(&doc);
                ns_json_get_string(&doc, root, "sessionKey", jeton, sizeof jeton);
                ns_json_get_string(&doc, root, "username", nom, sizeof nom);
            }
            ns_arena_free(&arene);
        }
        if (jeton[0]) {
            poser_jeton(jeton, nom[0] ? nom : q->texte);
            SDL_LockMutex(g.verrou);
            dire("%s : %s", verbe, g.pseudo);
            SDL_UnlockMutex(g.verrou);
        } else {
            /* Le serveur a dit oui sans donner de clé. On ne bascule pas
             * l'écran sur un état qu'on ne peut pas honorer. */
            SDL_LockMutex(g.verrou);
            g.etat = NS_COMPTE_ANONYME;
            dire("réponse du serveur sans clé de session");
            SDL_UnlockMutex(g.verrou);
        }
        effacer(jeton, sizeof jeton);
    } else {
        SDL_LockMutex(g.verrou);
        g.etat = NS_COMPTE_ANONYME;
        motif_depuis_corps(&r, ok ? r.status : 0);
        SDL_UnlockMutex(g.verrou);
    }
    ns_http_response_free(&r);
}

static void faire_verifier(void)
{
    ns_http_response r;
    const bool ok = appel("GET", "/api/v1/me", NULL, &r);
    if (ok && r.status == 200) {
        char nom[NS_COMPTE_PSEUDO_MAX] = { 0 };
        ns_arena arene;
        if (ns_arena_init(&arene, 16u * 1024u, "json me")) {
            ns_json doc;
            if (ns_json_parse(&doc, r.body, r.length, &arene))
                ns_json_get_string(&doc, ns_json_root(&doc), "username", nom, sizeof nom);
            ns_arena_free(&arene);
        }
        SDL_LockMutex(g.verrou);
        SDL_snprintf(g.pseudo, sizeof g.pseudo, "%s", nom);
        g.etat = NS_COMPTE_CONNECTE;
        dire("connecté : %s", g.pseudo);
        SDL_UnlockMutex(g.verrou);
    } else {
        /*
         * Un jeton gardé d'une session précédente peut avoir expiré (trente
         * jours) ou avoir été révoqué. On le JETTE au lieu de le garder : le
         * garder ferait échouer chaque requête suivante sans que le joueur
         * comprenne pourquoi il n'est pas connecté.
         */
        poser_jeton("", "");
        SDL_LockMutex(g.verrou);
        dire("session expirée, reconnectez-vous");
        SDL_UnlockMutex(g.verrou);
    }
    ns_http_response_free(&r);
}

static void faire_deconnecter(void)
{
    ns_http_response r;
    (void)appel("POST", "/api/v1/auth/logout", "{}", &r);
    ns_http_response_free(&r);

    poser_jeton("", "");
    SDL_LockMutex(g.verrou);
    g.dans_un_salon = false;
    SDL_zero(g.salon);
    dire("déconnecté");
    SDL_UnlockMutex(g.verrou);
}

static void faire_lister(void)
{
    ns_http_response r;
    const bool ok = appel("GET", "/api/v1/salons", NULL, &r);
    if (!ok || r.status != 200) {
        SDL_LockMutex(g.verrou);
        motif_depuis_corps(&r, ok ? r.status : 0);
        SDL_UnlockMutex(g.verrou);
        ns_http_response_free(&r);
        return;
    }

    ns_salon_resume tmp[NS_SALON_MAX_LISTE];
    int n = 0;
    ns_arena arene;
    if (ns_arena_init(&arene, 64u * 1024u, "json salons")) {
        ns_json doc;
        if (ns_json_parse(&doc, r.body, r.length, &arene)) {
            const ns_json_value *arr =
                ns_json_get(&doc, ns_json_root(&doc), "salons");
            const int total = ns_json_array_count(&doc, arr);
            for (int i = 0; i < total && n < NS_SALON_MAX_LISTE; ++i) {
                const ns_json_value *e = ns_json_at(&doc, arr, i);
                ns_salon_resume *s = &tmp[n];
                SDL_zerop(s);
                ns_json_get_string(&doc, e, "code", s->code, sizeof s->code);
                if (!s->code[0]) continue;
                ns_json_get_string(&doc, e, "nom", s->nom, sizeof s->nom);
                ns_json_get_string(&doc, e, "proprietaire", s->proprietaire,
                                   sizeof s->proprietaire);
                ns_json_get_string(&doc, e, "etat", s->etat, sizeof s->etat);
                s->places    = (int)ns_json_get_i64(&doc, e, "places", 0);
                s->occupes   = (int)ns_json_get_i64(&doc, e, "occupes", 0);
                s->camps     = (int)ns_json_get_i64(&doc, e, "camps", 1);
                s->depuis_ms = (uint32_t)ns_json_get_i64(&doc, e, "depuisMs", 0);
                n++;
            }
        }
        ns_arena_free(&arene);
    }

    SDL_LockMutex(g.verrou);
    SDL_memcpy(g.liste, tmp, sizeof tmp);
    g.listes      = n;
    g.liste_a_ms  = SDL_GetTicks();
    g.liste_recue = true;
    SDL_UnlockMutex(g.verrou);
    ns_http_response_free(&r);
}

/* Créer et rejoindre rendent la MÊME forme, et posent donc le même salon. */
static void poser_salon(const ns_http_response *r)
{
    ns_salon s;
    SDL_zero(s);
    if (lire_salon(r, &s)) {
        SDL_LockMutex(g.verrou);
        g.salon         = s;
        g.dans_un_salon = true;
        dire("salon %s — %s", s.code, s.nom[0] ? s.nom : "sans nom");
        SDL_UnlockMutex(g.verrou);
    } else {
        SDL_LockMutex(g.verrou);
        dire("réponse de salon illisible");
        SDL_UnlockMutex(g.verrou);
    }
}

static void faire_creer(const requete *q)
{
    char nom[NS_SALON_NOM_MAX * 2];
    echapper(q->texte, nom, sizeof nom);

    char corps[NS_SALON_NOM_MAX * 2 + 96];
    SDL_snprintf(corps, sizeof corps,
                 "{\"nom\":\"%s\",\"places\":%d,\"camps\":%d,\"prive\":%s}",
                 nom, q->a, q->b, q->f ? "true" : "false");

    ns_http_response r;
    const bool ok = appel("POST", "/api/v1/salons", corps, &r);
    if (ok && r.status >= 200 && r.status < 300) {
        poser_salon(&r);
    } else {
        SDL_LockMutex(g.verrou);
        motif_depuis_corps(&r, ok ? r.status : 0);
        SDL_UnlockMutex(g.verrou);
    }
    ns_http_response_free(&r);
}

/*
 * Le code est recopié dans un CHEMIN d'URL. Il ne peut donc pas être pris tel
 * quel : un code contenant une barre oblique ou un point d'interrogation
 * changerait la route appelée. On n'échappe pas — on REFUSE, parce qu'un code
 * légitime est fait de lettres et de chiffres et rien d'autre, et qu'un refus
 * ici est plus honnête qu'une requête partie ailleurs.
 */
static bool code_sain(const char *code)
{
    if (!code || !code[0]) return false;
    size_t n = 0;
    for (; code[n]; ++n) {
        const char c = code[n];
        const bool bon = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                       || (c >= '0' && c <= '9');
        if (!bon) return false;
    }
    return n < NS_SALON_CODE_MAX;
}

static void faire_rejoindre(const requete *q)
{
    char chemin[64];
    SDL_snprintf(chemin, sizeof chemin, "/api/v1/salons/%s/join", q->code);

    ns_http_response r;
    const bool ok = appel("POST", chemin, "{}", &r);
    if (ok && r.status >= 200 && r.status < 300) {
        poser_salon(&r);
    } else {
        SDL_LockMutex(g.verrou);
        /* Les trois refus du contrat méritent chacun leur phrase : « 404 » ne
         * dit pas à un joueur qu'il a mal recopié son code. */
        if (ok && r.status == 404)      dire("aucun salon avec ce code");
        else if (ok && r.status == 409) dire("salon complet");
        else if (ok && r.status == 410) dire("la manche a déjà commencé");
        else                            motif_depuis_corps(&r, ok ? r.status : 0);
        SDL_UnlockMutex(g.verrou);
    }
    ns_http_response_free(&r);
}

static void faire_sortir(const requete *q, bool supprimer)
{
    char chemin[64];
    SDL_snprintf(chemin, sizeof chemin, "/api/v1/salons/%s%s", q->code,
                 supprimer ? "" : "/leave");

    ns_http_response r;
    (void)appel(supprimer ? "DELETE" : "POST", chemin, supprimer ? NULL : "{}", &r);
    ns_http_response_free(&r);

    /*
     * On sort de l'écran QUOI QUE réponde le serveur. S'il a refusé, le
     * faucheur retirera la place au bout du délai d'abandon ; garder le joueur
     * prisonnier d'un salon parce qu'une requête a échoué serait pire que la
     * place fantôme.
     */
    SDL_LockMutex(g.verrou);
    g.dans_un_salon = false;
    SDL_zero(g.salon);
    dire(supprimer ? "salon supprimé" : "salon quitté");
    SDL_UnlockMutex(g.verrou);
}

static void faire_battre(const requete *q)
{
    char borne[64];
    echapper(q->borne, borne, sizeof borne);

    char corps[192];
    SDL_snprintf(corps, sizeof corps,
                 "{\"points\":%d,\"fusibles\":%d,\"vivante\":%s,"
                 "\"borne\":\"%s\",\"camp\":%d,\"commence\":%s}",
                 q->a, q->b, q->f ? "true" : "false", borne, q->c,
                 q->h ? "true" : "false");

    char chemin[64];
    SDL_snprintf(chemin, sizeof chemin, "/api/v1/salons/%s/beat", q->code);

    ns_http_response r;
    const bool ok = appel("POST", chemin, corps, &r);
    if (ok && r.status == 200) {
        ns_salon s;
        SDL_zero(s);
        SDL_LockMutex(g.verrou);
        s = g.salon;             /* on garde le relais : le battement ne le rend pas */
        SDL_UnlockMutex(g.verrou);

        ns_arena arene;
        if (ns_arena_init(&arene, 64u * 1024u, "json battement")) {
            ns_json doc;
            if (ns_json_parse(&doc, r.body, r.length, &arene)) {
                const ns_json_value *root = ns_json_root(&doc);
                ns_json_get_string(&doc, root, "etat", s.etat, sizeof s.etat);
                lire_occupants(&doc, root, &s);
            }
            ns_arena_free(&arene);
        }
        SDL_LockMutex(g.verrou);
        if (g.dans_un_salon) g.salon = s;
        SDL_UnlockMutex(g.verrou);
    } else if (ok && (r.status == 404 || r.status == 410)) {
        /* Le salon a été fermé sous nos pieds — faucheur, ou propriétaire. Le
         * dire une fois et sortir vaut mieux que de battre dans le vide. */
        SDL_LockMutex(g.verrou);
        g.dans_un_salon = false;
        SDL_zero(g.salon);
        dire("le salon a été fermé");
        SDL_UnlockMutex(g.verrou);
    }
    ns_http_response_free(&r);
}

/* ==========================================================================
 * Le fil
 * ========================================================================== */

static int SDLCALL fil_travail(void *inutilise)
{
    (void)inutilise;
    while (!SDL_GetAtomicInt(&g.quitte)) {
        requete q;
        bool prise = false;

        SDL_LockMutex(g.verrou);
        if (g.en_file > 0) {
            q = g.file[g.tete];
            /* La copie prise, on efface l'exemplaire de la file : un mot de
             * passe ne doit pas survivre dans un tableau global le temps que
             * huit autres requêtes le recouvrent. */
            effacer(g.file[g.tete].secret, sizeof g.file[g.tete].secret);
            g.tete = (g.tete + 1) % FILE_MAX;
            g.en_file--;
            g.en_vol = true;
            prise = true;
        }
        SDL_UnlockMutex(g.verrou);

        if (!prise) {
            /* Le même repos que le fil de `ns_online`. Soixante millisecondes
             * de latence sur un clic de menu ne se voient pas ; une attente
             * active sur un fil qui ne fait rien se voit sur la batterie. */
            SDL_Delay(60);
            continue;
        }

        switch (q.genre) {
        case G_VERIFIER:    faire_verifier();                              break;
        case G_INSCRIRE:    faire_auth(&q, "/api/v1/auth/register", "inscrit"); break;
        case G_CONNECTER:   faire_auth(&q, "/api/v1/auth/login", "connecté");   break;
        case G_DECONNECTER: faire_deconnecter();                           break;
        case G_LISTER:      faire_lister();                                break;
        case G_CREER:       faire_creer(&q);                               break;
        case G_REJOINDRE:   faire_rejoindre(&q);                           break;
        case G_QUITTER:     faire_sortir(&q, false);                       break;
        case G_SUPPRIMER:   faire_sortir(&q, true);                        break;
        case G_BATTRE:      faire_battre(&q);                              break;
        case G_RIEN:                                                       break;
        }
        effacer(q.secret, sizeof q.secret);

        SDL_LockMutex(g.verrou);
        g.en_vol = g.en_file > 0;
        SDL_UnlockMutex(g.verrou);
    }
    return 0;
}

/* Enfile, ou dit non. Rend faux si la file est pleine — ce qui ne devrait
 * arriver qu'à force de cliquer, et mérite d'être dit plutôt qu'ignoré. */
static bool enfiler(const requete *q)
{
    if (!g.actif) return false;

    SDL_LockMutex(g.verrou);
    if (g.en_file >= FILE_MAX) {
        dire("trop de demandes à la fois");
        SDL_UnlockMutex(g.verrou);
        return false;
    }
    g.file[g.queue] = *q;
    g.queue = (g.queue + 1) % FILE_MAX;
    g.en_file++;
    g.en_vol = true;
    SDL_UnlockMutex(g.verrou);
    return true;
}

/* ==========================================================================
 * Surface publique
 * ========================================================================== */

bool ns_compte_init(const char *url, const char *jeton)
{
    SDL_zero(g);
    g.etat = NS_COMPTE_ETEINT;
    g.liste_a_ms = 0;

    /* Sans URL, aucune socket. L'invariant du dépôt tient ici comme ailleurs :
     * le fil n'est même pas créé, donc la garantie est vérifiable et pas
     * seulement probable. */
    if (!url || !url[0]) return false;

    SDL_snprintf(g.url, sizeof g.url, "%s", url);
    size_t n = SDL_strlen(g.url);
    while (n > 0 && g.url[n - 1] == '/') g.url[--n] = '\0';

    g.verrou = SDL_CreateMutex();
    if (!g.verrou) return false;

    SDL_SetAtomicInt(&g.quitte, 0);
    g.fil = SDL_CreateThread(fil_travail, "nineteen-compte", NULL);
    if (!g.fil) {
        SDL_DestroyMutex(g.verrou);
        g.verrou = NULL;
        NS_WARN("compte : fil indisponible, l'inscription restera impossible");
        return false;
    }

    g.actif = true;
    g.etat  = NS_COMPTE_ANONYME;
    SDL_snprintf(g.message, sizeof g.message, "pas encore connecté");

    if (jeton && jeton[0]) {
        SDL_snprintf(g.jeton, sizeof g.jeton, "%s", jeton);
        requete q; SDL_zero(q); q.genre = G_VERIFIER;
        (void)enfiler(&q);
    }
    return true;
}

void ns_compte_shutdown(void)
{
    if (g.fil) {
        SDL_SetAtomicInt(&g.quitte, 1);
        SDL_WaitThread(g.fil, NULL);
        g.fil = NULL;
    }
    if (g.verrou) {
        SDL_DestroyMutex(g.verrou);
        g.verrou = NULL;
    }
    /* Le jeton et les mots de passe encore en file ne survivent pas à l'arrêt. */
    effacer(g.jeton, sizeof g.jeton);
    for (int i = 0; i < FILE_MAX; ++i) effacer(g.file[i].secret, sizeof g.file[i].secret);
    SDL_zero(g);
}

ns_compte_etat ns_compte_etat_courant(void)
{
    if (!g.actif) return NS_COMPTE_ETEINT;
    SDL_LockMutex(g.verrou);
    const ns_compte_etat e = g.en_vol && g.etat != NS_COMPTE_CONNECTE
                           ? NS_COMPTE_ATTENTE : g.etat;
    SDL_UnlockMutex(g.verrou);
    return e;
}

bool ns_compte_occupe(void)
{
    if (!g.actif) return false;
    SDL_LockMutex(g.verrou);
    const bool v = g.en_vol;
    SDL_UnlockMutex(g.verrou);
    return v;
}

const char *ns_compte_pseudo(void)
{
    /*
     * Une chaîne statique renvoyée sans verrou serait lue pendant que le fil
     * l'écrit. On recopie sous verrou dans un tampon d'appel — l'appelant est
     * l'écran, qui appelle une fois par image et affiche aussitôt.
     */
    static char copie[NS_COMPTE_PSEUDO_MAX];
    if (!g.actif) return "";
    SDL_LockMutex(g.verrou);
    SDL_snprintf(copie, sizeof copie, "%s", g.pseudo);
    SDL_UnlockMutex(g.verrou);
    return copie;
}

const char *ns_compte_message(void)
{
    static char copie[NS_COMPTE_MESSAGE_MAX];
    if (!g.actif) return "hors ligne : aucun serveur configuré";
    SDL_LockMutex(g.verrou);
    SDL_snprintf(copie, sizeof copie, "%s", g.message);
    SDL_UnlockMutex(g.verrou);
    return copie;
}

const char *ns_compte_jeton(void)
{
    static char copie[NS_COMPTE_JETON_MAX];
    if (!g.actif) return "";
    SDL_LockMutex(g.verrou);
    SDL_snprintf(copie, sizeof copie, "%s", g.jeton);
    SDL_UnlockMutex(g.verrou);
    return copie;
}

bool ns_compte_jeton_change(void)
{
    if (!g.actif) return false;
    SDL_LockMutex(g.verrou);
    const bool v = g.jeton_change;
    g.jeton_change = false;
    SDL_UnlockMutex(g.verrou);
    return v;
}

void ns_compte_inscrire(const char *pseudo, const char *mdp)
{
    requete q; SDL_zero(q); q.genre = G_INSCRIRE;
    SDL_snprintf(q.texte, sizeof q.texte, "%s", pseudo ? pseudo : "");
    SDL_snprintf(q.secret, sizeof q.secret, "%s", mdp ? mdp : "");
    (void)enfiler(&q);
    effacer(q.secret, sizeof q.secret);
}

void ns_compte_connecter(const char *pseudo, const char *mdp)
{
    requete q; SDL_zero(q); q.genre = G_CONNECTER;
    SDL_snprintf(q.texte, sizeof q.texte, "%s", pseudo ? pseudo : "");
    SDL_snprintf(q.secret, sizeof q.secret, "%s", mdp ? mdp : "");
    (void)enfiler(&q);
    effacer(q.secret, sizeof q.secret);
}

void ns_compte_deconnecter(void)
{
    requete q; SDL_zero(q); q.genre = G_DECONNECTER;
    (void)enfiler(&q);
}

void ns_salons_demander_liste(void)
{
    requete q; SDL_zero(q); q.genre = G_LISTER;
    (void)enfiler(&q);
}

int ns_salons_liste(ns_salon_resume *out, int max)
{
    if (!g.actif || !out || max <= 0) return 0;
    SDL_LockMutex(g.verrou);
    const int n = g.listes < max ? g.listes : max;
    for (int i = 0; i < n; ++i) out[i] = g.liste[i];
    SDL_UnlockMutex(g.verrou);
    return n;
}

uint32_t ns_salons_liste_age_ms(void)
{
    if (!g.actif) return UINT32_MAX;
    SDL_LockMutex(g.verrou);
    const bool recue = g.liste_recue;
    const uint32_t a = g.liste_a_ms;
    SDL_UnlockMutex(g.verrou);
    if (!recue) return UINT32_MAX;
    return (uint32_t)(SDL_GetTicks() - a);
}

void ns_salon_creer(const char *nom, int places, int camps, bool prive)
{
    requete q; SDL_zero(q); q.genre = G_CREER;
    SDL_snprintf(q.texte, sizeof q.texte, "%s", nom ? nom : "");
    /* On borne ICI plutôt que de laisser le serveur refuser : ces deux valeurs
     * viennent d'un réglage de l'écran, pas du joueur, et un 409 pour une
     * valeur que l'interface n'aurait jamais dû proposer serait un défaut de
     * ce côté-ci. */
    q.a = places < 2 ? 2 : (places > NS_SALON_MAX_PLACES ? NS_SALON_MAX_PLACES : places);
    q.b = camps  < 1 ? 1 : (camps  > 2 ? 2 : camps);
    q.f = prive;
    (void)enfiler(&q);
}

void ns_salon_rejoindre(const char *code)
{
    if (!code_sain(code)) {
        if (g.actif) {
            SDL_LockMutex(g.verrou);
            dire("code invalide : lettres et chiffres seulement");
            SDL_UnlockMutex(g.verrou);
        }
        return;
    }
    requete q; SDL_zero(q); q.genre = G_REJOINDRE;
    SDL_snprintf(q.code, sizeof q.code, "%s", code);
    (void)enfiler(&q);
}

static bool code_courant(char *out, size_t taille)
{
    if (!g.actif) return false;
    SDL_LockMutex(g.verrou);
    const bool dedans = g.dans_un_salon;
    if (dedans) SDL_snprintf(out, taille, "%s", g.salon.code);
    SDL_UnlockMutex(g.verrou);
    return dedans;
}

void ns_salon_quitter(void)
{
    requete q; SDL_zero(q); q.genre = G_QUITTER;
    if (!code_courant(q.code, sizeof q.code)) return;
    (void)enfiler(&q);
}

void ns_salon_supprimer(void)
{
    requete q; SDL_zero(q); q.genre = G_SUPPRIMER;
    if (!code_courant(q.code, sizeof q.code)) return;
    (void)enfiler(&q);
}

bool ns_salon_courant(ns_salon *out)
{
    if (!g.actif || !out) return false;
    SDL_LockMutex(g.verrou);
    const bool dedans = g.dans_un_salon;
    if (dedans) *out = g.salon;
    SDL_UnlockMutex(g.verrou);
    return dedans;
}

void ns_salon_battre(int points, int fusibles, bool vivante,
                     const char *borne, int camp, bool commence)
{
    requete q; SDL_zero(q); q.genre = G_BATTRE;
    if (!code_courant(q.code, sizeof q.code)) return;

    /*
     * Un battement de plus quand il y en a déjà un en file ne sert à rien : le
     * suivant portera un état plus frais. On le jette silencieusement — c'est
     * le seul genre de requête qui se coalesce, et pour la même raison que le
     * classement de `ns_online` se coalesce.
     */
    SDL_LockMutex(g.verrou);
    for (int i = 0, k = g.tete; i < g.en_file; ++i, k = (k + 1) % FILE_MAX) {
        if (g.file[k].genre == G_BATTRE) {
            SDL_UnlockMutex(g.verrou);
            return;
        }
    }
    SDL_UnlockMutex(g.verrou);

    q.a = points;
    q.b = fusibles;
    q.c = camp;
    q.f = vivante;
    q.h = commence;
    SDL_snprintf(q.borne, sizeof q.borne, "%s", borne ? borne : "");
    (void)enfiler(&q);
}

void ns_compte_stats(uint32_t *envoyees, uint32_t *echouees)
{
    if (envoyees) *envoyees = g.envoyees;
    if (echouees) *echouees = g.echouees;
}
