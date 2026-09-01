/*
 * test_maj.c — la mise à jour, et surtout ce qu'elle a le droit de faire.
 *
 * Écrire un mécanisme de mise à jour, c'est réécrire la chose qui a tué la V1 :
 * un contrôle de version en ligne dont dépendait le démarrage. Ce test vérifie
 * d'abord les trois règles de `ns_maj.h` — rien n'attend, rien n'échoue, rien
 * ne s'installe tout seul — puis la chaîne complète contre le bouchon HTTP.
 *
 * Le reste est du calcul pur, et c'est là que se cachent les fautes muettes :
 * une comparaison de versions qui met 17.10.0 avant 17.9.0, un vocabulaire
 * d'architecture qui diffère d'une lettre de celui du serveur. Ni l'une ni
 * l'autre ne produit d'erreur — seulement un joueur à qui l'on ne propose plus
 * jamais rien.
 */
#include "ns_core.h"
#include "ns_maj.h"
#include "ns_runlog.h"
#include "stub_serveur.h"

#include <SDL3/SDL.h>

#include <stdio.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define ATTENDRE(cond, limite_ms, ou)                                         \
    do {                                                                      \
        (ou) = false;                                                         \
        for (int _i = 0; _i * 20 < (limite_ms); ++_i) {                       \
            if (cond) { (ou) = true; break; }                                 \
            SDL_Delay(20);                                                    \
        }                                                                     \
    } while (0)

/* -------------------------------------------------------------------------- */

static void test_comparaison(void)
{
    CHECK(ns_maj_comparer("17.0.0", "17.0.0") == 0, "deux fois la même version");
    CHECK(ns_maj_comparer("17.0.0", "17.0.1") < 0, "le correctif compte");
    CHECK(ns_maj_comparer("17.1.0", "17.0.9") > 0, "la mineure prime sur le correctif");
    CHECK(ns_maj_comparer("18.0.0", "17.99.99") > 0, "la majeure prime sur tout");

    /*
     * LE PIÈGE, et il n'est pas théorique : « 17.10.0 » est plus récent que
     * « 17.9.0 », alors qu'en texte « 17.1… » vient avant « 17.9 ». Le jour où
     * la dixième version mineure sort, une comparaison de chaînes ferait cesser
     * les mises à jour sans un mot.
     */
    CHECK(ns_maj_comparer("17.10.0", "17.9.0") > 0,
          "17.10.0 vient APRÈS 17.9.0 (%d)", ns_maj_comparer("17.10.0", "17.9.0"));
    CHECK(ns_maj_comparer("17.9.0", "17.10.0") < 0, "et la réciproque");

    /* Un champ absent vaut zéro : « 17 » et « 17.0.0 » sont la même version. */
    CHECK(ns_maj_comparer("17", "17.0.0") == 0, "« 17 » vaut « 17.0.0 »");
    CHECK(ns_maj_comparer("17.1", "17.0.5") > 0, "et la comparaison reste juste");

    /* Un suffixe non numérique arrête la lecture du champ, sans inventer
     * d'ordre entre « rc2 » et « beta ». */
    CHECK(ns_maj_comparer("17.1.0-rc2", "17.1.0") == 0, "un suffixe ne classe pas");

    /* NULL est traité comme la version zéro plutôt que de déréférencer. */
    CHECK(ns_maj_comparer(NULL, "17.0.0") < 0, "NULL précède tout");
    CHECK(ns_maj_comparer(NULL, NULL) == 0, "et deux NULL sont égaux");
}

/*
 * LE VOCABULAIRE DU SERVEUR, recopié de `Classer` dans
 * `server/internal/telechargements/telechargements.go`. Une lettre d'écart et
 * plus aucun paquet n'est jamais proposé.
 */
static void test_choix_du_paquet(void)
{
    const char *nous = ns_maj_plateforme();
    CHECK(SDL_strcmp(nous, "windows") == 0 || SDL_strcmp(nous, "macos") == 0
       || SDL_strcmp(nous, "linux") == 0,
          "la plateforme se nomme comme chez le serveur (« %s »)", nous);

    CHECK(!ns_maj_convient("plan9", ""), "une autre plateforme est écartée");
    CHECK(ns_maj_convient(nous, ""), "une architecture absente convient partout");
    CHECK(ns_maj_convient(nous, "universel"),
          "« universel », le mot du serveur, convient partout");
    CHECK(!ns_maj_convient(nous, "sparc"), "une architecture étrangère est écartée");

    const char *arch = ns_maj_arch();
    if (arch[0]) {
        CHECK(SDL_strcmp(arch, "arm64") == 0 || SDL_strcmp(arch, "x86_64") == 0,
              "l'architecture se nomme comme chez le serveur (« %s »)", arch);
        CHECK(ns_maj_convient(nous, arch), "la nôtre convient");
        CHECK(!ns_maj_convient(nous, SDL_strcmp(arch, "arm64") == 0 ? "x86_64" : "arm64"),
              "l'autre non — un paquet qui ne démarrerait pas ne se propose pas");
    }

    /*
     * LA VERSION DANS LE NOM. Ce contrôle vient d'une mesure : deux `.dmg` dans
     * la même liste, le client prenait le premier — il annonçait « 17.1.0
     * disponible (175 Mio) » et s'apprêtait à installer le paquet 17.0.0 dont
     * il partait.
     */
    CHECK(ns_maj_porte_version("Nineteen-17.1.0-macOS-universal.dmg", "17.1.0"),
          "le paquet de la version annoncée est reconnu");
    CHECK(!ns_maj_porte_version("Nineteen-17.0.0-macOS-universal.dmg", "17.1.0"),
          "celui d'une autre version ne l'est pas");
    /* Bornée à droite, sinon « 17.1 » se reconnaîtrait dans « 17.10 » et le
     * jeu installerait une version qu'il n'a pas demandée. */
    CHECK(!ns_maj_porte_version("Nineteen-17.10.0.dmg", "17.1"),
          "« 17.1 » ne se reconnaît pas dans « 17.10 »");
    CHECK(!ns_maj_porte_version("Nineteen-17.1.01.dmg", "17.1.0"),
          "ni « 17.1.0 » dans « 17.1.01 »");
    CHECK(!ns_maj_porte_version("Nineteen.dmg", "17.1.0"), "un nom sans version");
    CHECK(!ns_maj_porte_version("Nineteen-17.1.0.dmg", ""), "ni une version vide");

    /* Le rang : le meilleur paquet est celui qui demande le moins au joueur. */
    CHECK(ns_maj_rang_paquet("truc.txt") < 0, "un fichier qui n'est pas un paquet");
    CHECK(ns_maj_rang_paquet("") < 0, "ni la chaîne vide");
#if defined(__linux__)
    CHECK(ns_maj_rang_paquet("Nineteen-17.0.0-x86_64.AppImage")
            < ns_maj_rang_paquet("nineteen_17.0.0_amd64.deb"),
          "l'AppImage passe avant le .deb : ni root ni gestionnaire de paquets");
    CHECK(ns_maj_rang_paquet("nineteen-17.0.0-linux.tar.gz") > 0, "l'archive en dernier");
#elif defined(__APPLE__)
    CHECK(ns_maj_rang_paquet("Nineteen-17.0.0-macOS-universal.dmg") == 0, "le .dmg d'abord");
    CHECK(ns_maj_rang_paquet("Nineteen-17.0.0.pkg") == 1, "puis le .pkg");
    CHECK(ns_maj_rang_paquet("nineteen_17.0.0_arm64.deb") < 0,
          "et un paquet Linux n'est pas un paquet d'ici");
#elif defined(_WIN32)
    CHECK(ns_maj_rang_paquet("Nineteen-17.0.0-Setup.exe") == 0, "l'installateur d'abord");
    CHECK(ns_maj_rang_paquet("Nineteen-17.0.0.zip") > 0, "l'archive après");
#endif
}

/*
 * LA RÈGLE QUI PRIME SUR TOUT : sans serveur, sans réseau, ou sous verrou, la
 * mise à jour n'existe pas et le jeu ne s'en aperçoit pas.
 */
static void test_rien_sans_serveur(void)
{
    ns_maj_config c;
    SDL_zero(c);
    CHECK(!ns_maj_init(&c), "aucune adresse : aucun fil, aucune socket");
    CHECK(ns_maj_etat_courant() == NS_MAJ_INACTIVE, "et l'état le dit");

    c.server_url = "http://127.0.0.1:9";
    c.version = "17.0.0";
    c.locked = true;
    CHECK(!ns_maj_init(&c), "--offline verrouille même avec une adresse");
    CHECK(ns_maj_etat_courant() == NS_MAJ_INACTIVE, "et l'état le dit encore");

    CHECK(!ns_maj_installer(), "rien à installer, et ça ne casse pas");
    CHECK(ns_maj_message()[0] != '\0', "il y a toujours une phrase à afficher");
    CHECK(ns_maj_avancement() == 0.0f, "et aucun avancement à montrer");
}

/* Un serveur mort ne doit produire qu'un échec nommé, jamais une attente. */
static void test_serveur_mort(void)
{
    ns_maj_config c;
    SDL_zero(c);
    c.server_url = "http://127.0.0.1:9";
    c.version = "17.0.0";
    const Uint64 avant = SDL_GetTicks();
    CHECK(ns_maj_init(&c), "le fil démarre vers un serveur mort");
    const Uint64 apres = SDL_GetTicks();
    CHECK(apres - avant < 500,
          "et il RENVOIE LA MAIN TOUT DE SUITE (%llu ms) — c'est la leçon de la V1",
          (unsigned long long)(apres - avant));

    bool vu = false;
    ATTENDRE(ns_maj_etat_courant() == NS_MAJ_ECHEC, 8000, vu);
    CHECK(vu, "l'échec est constaté et nommé (%s)", ns_maj_message());
    ns_maj_shutdown();
}

/* -------------------------------------------------------------------------- */

static const char *CONTENU = "ceci est un paquet d'essai, et rien de plus";

/* L'empreinte du contenu ci-dessus, calculée ici plutôt qu'écrite à la main :
 * une constante recopiée est une constante qui se démode. */
static void empreinte(const char *texte, char out[65])
{
    uint8_t brut[32];
    ns_sha256(texte, SDL_strlen(texte), brut);
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[i * 2]     = HEX[brut[i] >> 4];
        out[i * 2 + 1] = HEX[brut[i] & 15];
    }
    out[64] = '\0';
}

static const char *nom_de_paquet(void)
{
#if defined(_WIN32)
    return "Nineteen-17.1.0-Setup.exe";
#elif defined(__APPLE__)
    return "Nineteen-17.1.0-macOS-universal.dmg";
#else
    return "Nineteen-17.1.0-universal.AppImage";
#endif
}

static void repertoire_d_essai(char *out, size_t cap)
{
    char *cwd = SDL_GetCurrentDirectory();
    SDL_snprintf(out, cap, "%stest-maj", cwd ? cwd : "./");
    SDL_free(cwd);
}

/* Rien de plus récent : le jeu est à jour, et il ne se passe rien. */
static void test_a_jour(const char *url)
{
    char somme[65];
    empreinte(CONTENU, somme);
    stub_poser_maj("17.0.0", nom_de_paquet(), CONTENU, somme);

    ns_maj_config c;
    SDL_zero(c);
    c.server_url = url;
    c.version = "17.0.0";
    CHECK(ns_maj_init(&c), "le fil démarre");
    bool vu = false;
    ATTENDRE(ns_maj_etat_courant() == NS_MAJ_A_JOUR, 5000, vu);
    CHECK(vu, "le serveur ne propose rien de plus récent (%s)", ns_maj_message());
    ns_maj_shutdown();
}

/*
 * LA CHAÎNE : une version plus récente, le paquet de cette machine, le
 * transfert, l'empreinte, et le paquet posé sur le disque.
 */
static void test_chaine(const char *url)
{
    char dossier[1024];
    repertoire_d_essai(dossier, sizeof dossier);
    ns_maj_set_repertoire(dossier);

    char somme[65];
    empreinte(CONTENU, somme);
    stub_poser_maj("17.1.0", nom_de_paquet(), CONTENU, somme);
    /* L'ANCIENNE VERSION EST DANS LA LISTE, en tête, comme dans un vrai
     * répertoire de téléchargement. Elle a la même extension et la même
     * plateforme : seul son numéro la distingue. */
    stub_poser_maj_voisin(
#if defined(_WIN32)
        "Nineteen-17.0.0-Setup.exe"
#elif defined(__APPLE__)
        "Nineteen-17.0.0-macOS-universal.dmg"
#else
        "Nineteen-17.0.0-universal.AppImage"
#endif
    );

    ns_maj_config c;
    SDL_zero(c);
    c.server_url = url;
    c.version = "17.0.0";
    CHECK(ns_maj_init(&c), "le fil démarre");

    bool vu = false;
    ATTENDRE(ns_maj_etat_courant() == NS_MAJ_DISPONIBLE, 5000, vu);
    CHECK(vu, "une version plus récente est annoncée (%s)", ns_maj_message());
    CHECK(SDL_strcmp(ns_maj_version_offerte(), "17.1.0") == 0,
          "et c'est bien 17.1.0 (« %s »)", ns_maj_version_offerte());

    /* RIEN NE SE TÉLÉCHARGE TOUT SEUL : 175 Mio sur la ligne de quelqu'un qui
     * voulait jouer dix minutes n'est pas à nous de le décider. */
    SDL_Delay(300);
    CHECK(ns_maj_etat_courant() == NS_MAJ_DISPONIBLE,
          "et rien ne part avant qu'on le demande");
    CHECK(ns_maj_paquet()[0] == '\0', "aucun paquet posé sur le disque");

    ns_maj_telecharger();
    ATTENDRE(ns_maj_etat_courant() == NS_MAJ_PRETE, 8000, vu);
    CHECK(vu, "le paquet arrive et son empreinte est bonne (%s)", ns_maj_message());
    CHECK(ns_maj_paquet()[0] != '\0', "le chemin du paquet est connu");

    /* Le fichier est là, entier, et c'est bien le nôtre. */
    size_t taille = 0;
    void *lu = SDL_LoadFile(ns_maj_paquet(), &taille);
    CHECK(lu != NULL && taille == SDL_strlen(CONTENU),
          "le fichier fait sa taille (%zu attendus, %zu écrits)",
          SDL_strlen(CONTENU), taille);
    if (lu && taille == SDL_strlen(CONTENU)) {
        CHECK(SDL_memcmp(lu, CONTENU, taille) == 0, "et son contenu est le bon");
    }
    SDL_free(lu);

    CHECK(SDL_strstr(ns_maj_paquet(), "17.1.0") != NULL,
          "et c'est le paquet de 17.1.0, pas celui d'à côté (« %s »)", ns_maj_paquet());

    ns_maj_shutdown();
    SDL_RemovePath(ns_maj_paquet());
    stub_poser_maj_voisin(NULL);
    ns_maj_set_repertoire(NULL);
}

/*
 * UN PAQUET DONT L'EMPREINTE NE TOMBE PAS JUSTE EST ÉCARTÉ, ET EFFACÉ.
 *
 * C'est la seule vérification qui protège d'un installeur tronqué, et un
 * installeur tronqué est la seule façon de casser une machine avec une mise à
 * jour. Le fichier doit disparaître : le laisser ferait reprendre le transfert
 * sur des octets qu'on vient de juger faux.
 */
static void test_empreinte_fausse(const char *url)
{
    char dossier[1024];
    repertoire_d_essai(dossier, sizeof dossier);
    ns_maj_set_repertoire(dossier);

    stub_poser_maj("17.1.0", nom_de_paquet(), CONTENU,
                   "0000000000000000000000000000000000000000000000000000000000000000");

    ns_maj_config c;
    SDL_zero(c);
    c.server_url = url;
    c.version = "17.0.0";
    c.auto_transfert = true;   /* on va droit au but */
    CHECK(ns_maj_init(&c), "le fil démarre");

    bool vu = false;
    ATTENDRE(ns_maj_etat_courant() == NS_MAJ_ECHEC, 8000, vu);
    CHECK(vu, "le paquet est refusé (%s)", ns_maj_message());
    CHECK(ns_maj_paquet()[0] == '\0', "et il n'est proposé à personne");

    char chemin[1200];
    SDL_snprintf(chemin, sizeof chemin, "%s/%s", dossier, nom_de_paquet());
    SDL_PathInfo info;
    CHECK(!SDL_GetPathInfo(chemin, &info),
          "le fichier a été effacé — sinon une reprise repartirait de ces octets");

    ns_maj_shutdown();
    ns_maj_set_repertoire(NULL);
}

/* Une version plus récente sans paquet pour cette machine : on le DIT, au lieu
 * de laisser croire qu'on est à jour. */
static void test_pas_de_paquet_ici(const char *url)
{
    char somme[65];
    empreinte(CONTENU, somme);
    /* Un nom d'une autre plateforme que celle-ci — l'extension décide. */
#if defined(__APPLE__)
    stub_poser_maj("17.1.0", "nineteen_17.1.0_arm64.deb", CONTENU, somme);
#else
    stub_poser_maj("17.1.0", "Nineteen-17.1.0-macOS-universal.dmg", CONTENU, somme);
#endif

    ns_maj_config c;
    SDL_zero(c);
    c.server_url = url;
    c.version = "17.0.0";
    CHECK(ns_maj_init(&c), "le fil démarre");
    bool vu = false;
    ATTENDRE(ns_maj_etat_courant() == NS_MAJ_A_JOUR, 5000, vu);
    CHECK(vu, "on ne propose rien qu'on ne puisse installer (%s)", ns_maj_message());
    CHECK(SDL_strstr(ns_maj_message(), "aucun paquet") != NULL,
          "et le message dit pourquoi (« %s »)", ns_maj_message());
    ns_maj_shutdown();
}

int main(void)
{
    if (!SDL_Init(0)) {
        printf("SDL_Init a échoué : %s\n", SDL_GetError());
        return 1;
    }
    ns_log_set_level(NS_LOG_ERROR);

    printf("=== mise à jour ===\n\n");

    test_comparaison();
    test_choix_du_paquet();
    test_rien_sans_serveur();
    test_serveur_mort();

    char bouchon[64];
    if (stub_demarrer(bouchon, sizeof bouchon)) {
        printf("  bouchon HTTP sur %s\n", bouchon);
        test_a_jour(bouchon);
        test_chaine(bouchon);
        test_empreinte_fausse(bouchon);
        test_pas_de_paquet_ici(bouchon);
        stub_arreter();
    } else {
        printf("  (pile réseau indisponible : le bouchon est sauté)\n");
    }

    printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
