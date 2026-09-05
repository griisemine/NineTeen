/*
 * test_arene.c — le client C de l'arène, du codec au vrai relais.
 *
 * DEUX RÉGIMES, comme `tests/test_lockstep.c`
 * -------------------------------------------
 *   ns_test_arene
 *       Sans relais. LE VERROU — sans URL de serveur configurée, aucune socket
 *       n'est ouverte, et c'est MESURÉ par `ns_arene_sockets()` et non relu —
 *       puis tout le codec : chaque trame aller-retour, les cas limites, les
 *       charges malveillantes, et le découpage d'un flux TCP. C'est ce que fait
 *       la CI, et rien n'y touche au réseau.
 *
 *   ns_test_arene <hôte> <port>
 *       LE SALON ENTIER contre le relais Go : deux clients entrent dans un
 *       salon de deux, reçoivent le MÊME coup d'envoi, et une action de l'un
 *       arrive à l'autre AVEC LA BONNE PLACE ÉMETTRICE.
 *
 *       Le relais se lance seul, sans base :
 *         cd server && go run ./cmd/duelrelay -addr 127.0.0.1:8081 &
 *         ns_test_arene 127.0.0.1 8081
 *
 * POURQUOI LE SECOND RÉGIME EXISTE
 * --------------------------------
 * C'est la leçon fondatrice de ce dépôt : « deux moitiés d'un même projet
 * peuvent être justes chacune et fausses ensemble ». Le protocole de l'arène
 * est écrit DEUX FOIS, une fois en C ici et une fois en Go dans `relay.go`, et
 * un test C qui parlerait à un faux relais écrit en C ne prouverait rien de la
 * couture entre les deux. C'est cette couture qui a coûté trois défauts au
 * classement en ligne.
 *
 * Ce que le premier régime prouve quand même, et il faut le dire : le codec
 * pur, exercé directement, sans passer par une socket. Le découpage y est
 * vérifié sur `ns_arene_trame`, qui est la seule fonction du module à savoir
 * découper — le fil s'en sert, il ne le refait pas.
 */
#include "ns_arene.h"
#include "ns_core.h"
#include "ns_online.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

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

/* -------------------------------------------------------------------------- */
/* LE VERROU : sans URL configurée, aucune socket                              */
/* -------------------------------------------------------------------------- */

/*
 * LA promesse du dossier, et elle est mesurée plutôt qu'affirmée.
 *
 * `ns_arene_sockets()` est incrémenté juste avant le seul `socket()` du module.
 * Le lire avant et après une tentative d'ouverture dit donc EXACTEMENT ce qui
 * s'est passé, là où relire `ns_arene_ouvrir` ne dirait que ce qu'on croit
 * avoir écrit.
 *
 * L'adresse visée est volontairement VALABLE (127.0.0.1:8081, celle du relais
 * de développement) : un refus obtenu parce que l'hôte n'existe pas ne
 * prouverait rien du verrou.
 */
static void test_verrou(void)
{
    ns_arene_config cfg;
    SDL_zero(cfg);
    cfg.hote = "127.0.0.1";
    cfg.port = 8081;
    cfg.salon = 1;
    cfg.place = 0;
    cfg.places = 2;
    cfg.pseudo = "VERROU";
    cfg.delai_ms = 300;

    const uint32_t avant = ns_arene_sockets();
    CHECK(avant == 0, "le module avait déjà ouvert %u socket(s) avant tout test",
          avant);

    char err[160] = { 0 };
    ns_arene *a = ns_arene_ouvrir(&cfg, err, sizeof err);
    CHECK(a == NULL, "une arène s'est ouverte alors que le réseau est inactif");
    CHECK(err[0] != '\0', "le refus n'a produit aucun motif");
    CHECK(ns_arene_sockets() == avant,
          "SANS URL CONFIGURÉE, UNE SOCKET A ÉTÉ OUVERTE (%u -> %u)",
          avant, ns_arene_sockets());
    if (a) ns_arene_fermer(a);

    /* `--offline` par-dessus une URL valable : le verrou est le même, parce
     * qu'il n'y en a qu'un — `ns_online_server_url` rend NULL dans les deux
     * cas, et ce module n'en connaît pas d'autre. */
    ns_online_config oc;
    SDL_zero(oc);
    oc.server_url = "http://127.0.0.1:1";
    oc.locked = true;
    CHECK(!ns_online_init(&oc), "--offline verrouille même avec une URL");
    CHECK(!ns_online_enabled(), "et le réseau reste inactif");

    err[0] = '\0';
    a = ns_arene_ouvrir(&cfg, err, sizeof err);
    CHECK(a == NULL, "une arène s'est ouverte sous --offline");
    CHECK(ns_arene_sockets() == avant,
          "SOUS --offline, UNE SOCKET A ÉTÉ OUVERTE (%u -> %u)",
          avant, ns_arene_sockets());
    if (a) ns_arene_fermer(a);
    ns_online_shutdown();
}

/* -------------------------------------------------------------------------- */
/* Hors ligne : le même chemin de code                                         */
/* -------------------------------------------------------------------------- */

/*
 * Sans arène, le pointeur est nul et TOUT doit continuer de marcher : c'est la
 * promesse « offline, on est toujours place 0 et le chemin de code est le même
 * ». Une seule de ces lignes qui planterait obligerait la salle à écrire un
 * `if (arene)` autour de chaque appel, c'est-à-dire à maintenir deux modes.
 */
static void test_hors_ligne(void)
{
    CHECK(ns_arene_ma_place(NULL) == 0, "hors ligne, on n'est pas la place 0");
    CHECK(ns_arene_arbitre(NULL), "hors ligne, personne n'arbitre");
    CHECK(ns_arene_etat(NULL) == NS_ARENE_OFF, "l'état d'un NULL n'est pas OFF");
    CHECK(ns_arene_graine(NULL) == 0, "la graine d'un NULL n'est pas nulle");
    CHECK(ns_arene_erreur(NULL)[0] == '\0', "l'erreur d'un NULL n'est pas vide");

    /* Les dépôts ne font rien et ne plantent pas. */
    ns_arene_publier(NULL, true, 0, "envol", false, 42, 10, 3, 12);
    ns_arene_agir(NULL, 1, 2);
    ns_arene_verdict(NULL, 1, NS_ARENE_AUCUN_CAMP, 1, 45000);
    ns_arene_effet(NULL, 0, 1, 2, NS_ARENE_ABSORBEE);

    ns_arene_evenement ev;
    CHECK(!ns_arene_prendre(NULL, &ev), "un NULL a rendu un événement");
    ns_arene_place pl[NS_ARENE_MAX_PLACES];
    CHECK(ns_arene_places(NULL, pl, NS_ARENE_MAX_PLACES) == 0,
          "un NULL a rendu des places");
    uint32_t in = 9, out = 9, perdus = 9;
    ns_arene_stats(NULL, &in, &out, &perdus);
    CHECK(in == 0 && out == 0 && perdus == 0,
          "les compteurs d'un NULL ne sont pas nuls");
}

/* -------------------------------------------------------------------------- */
/* QUI ARBITRE — la fonction pure                                              */
/* -------------------------------------------------------------------------- */

static void test_arbitre_de(void)
{
    CHECK(ns_arene_arbitre_de(0xFF) == 0, "salon plein : la place 0 arbitre");
    CHECK(ns_arene_arbitre_de(0x01) == 0, "seule la place 0 : elle arbitre");
    /* LE CAS QUI COMPTE : la place 0 a raccroché. Sans succession, un salon de
     * sept attendrait un couperet que personne n'envoie. */
    CHECK(ns_arene_arbitre_de(0xFE) == 1, "place 0 partie : la place 1 arbitre");
    CHECK(ns_arene_arbitre_de(0xF0) == 4, "places 0 à 3 parties : la place 4");
    CHECK(ns_arene_arbitre_de(0x80) == 7, "seule la place 7 : elle arbitre");
    CHECK(ns_arene_arbitre_de(0x00) == NS_ARENE_AUCUNE_PLACE,
          "un salon vide n'a pas d'arbitre");

    /* Le même résultat que la définition, sur les 256 masques : la règle est
     * « la plus petite place présente », et rien d'autre. */
    for (int m = 0; m < 256; ++m) {
        int attendu = NS_ARENE_AUCUNE_PLACE;
        for (int i = 0; i < NS_ARENE_MAX_PLACES; ++i) {
            if (m & (1 << i)) { attendu = i; break; }
        }
        if (ns_arene_arbitre_de((uint8_t)m) != (uint8_t)attendu) {
            CHECK(false, "masque %02x : arbitre %u au lieu de %d", m,
                  (unsigned)ns_arene_arbitre_de((uint8_t)m), attendu);
            break;
        }
    }
    g_checks++;   /* la boucle ci-dessus compte pour une vérification */
}

/* -------------------------------------------------------------------------- */
/* Le codec : aller-retour et cas limites                                      */
/* -------------------------------------------------------------------------- */

static void test_join(void)
{
    uint8_t p[NS_ARENE_JOIN_OCTETS + 4];
    SDL_memset(p, 0xAA, sizeof p);

    /* UN PSEUDO DE 23 CARACTÈRES : le cas limite exact du champ. Le relais
     * coupe à 23 pour que le champ garde toujours son zéro final ; on doit
     * écrire les 23 ET le zéro. */
    const char *nom23 = "ABCDEFGHIJKLMNOPQRSTUVW";     /* 23 */
    CHECK(SDL_strlen(nom23) == 23, "le pseudo d'essai ne fait pas 23 octets");
    CHECK(ns_arene_ecrire_join(p, sizeof p, 0x0123456789ABCDEFull, 3, 8, nom23)
              == NS_ARENE_JOIN_OCTETS,
          "un JOIN valable n'a pas été écrit");

    /* Le salon est petit-boutiste sur huit octets. */
    CHECK(p[0] == 0xEF && p[7] == 0x01, "le salon n'est pas petit-boutiste");
    CHECK(p[8] == 3, "la place n'est pas à l'offset 8");
    CHECK(p[9] == 8, "les places attendues ne sont pas à l'offset 9");
    CHECK(SDL_memcmp(p + 10, nom23, 23) == 0, "le pseudo n'est pas à l'offset 10");
    CHECK(p[10 + 23] == 0, "le champ de pseudo n'est pas terminé par un zéro");

    /* Un pseudo trop long est COUPÉ, jamais débordé. */
    const char *long_nom = "0123456789012345678901234567890123456789";
    CHECK(ns_arene_ecrire_join(p, sizeof p, 1, 0, 2, long_nom)
              == NS_ARENE_JOIN_OCTETS, "un pseudo long a fait échouer le JOIN");
    CHECK(p[10 + NS_ARENE_PSEUDO - 1] == 0,
          "un pseudo de 40 octets a mangé le zéro final");
    CHECK(SDL_memcmp(p + 10, long_nom, NS_ARENE_PSEUDO - 1) == 0,
          "un pseudo long n'a pas été coupé à 23");

    /* Les refus. Le relais ferme la connexion sur chacun d'eux ; les découvrir
     * ici coûte moins cher que de se faire raccrocher au nez. */
    CHECK(ns_arene_ecrire_join(p, sizeof p, 1, 0, 1, "X") == 0,
          "un salon d'UNE place a été accepté");
    CHECK(ns_arene_ecrire_join(p, sizeof p, 1, 0, 9, "X") == 0,
          "un salon de NEUF places a été accepté");
    CHECK(ns_arene_ecrire_join(p, sizeof p, 1, 4, 4, "X") == 0,
          "la place 4 d'un salon de 4 a été acceptée");
    CHECK(ns_arene_ecrire_join(p, NS_ARENE_JOIN_OCTETS - 1, 1, 0, 2, "X") == 0,
          "un tampon trop petit a été accepté");
    CHECK(ns_arene_ecrire_join(NULL, 64, 1, 0, 2, "X") == 0,
          "un tampon NULL a été accepté");
}

/* Construit la charge d'un tableau des places, comme le relais l'écrit. */
static size_t forger_tableau(uint8_t *out, int n, const char *const *noms)
{
    out[0] = (uint8_t)n;
    for (int i = 0; i < n; ++i) {
        uint8_t *e = out + 1 + (size_t)i * NS_ARENE_ENTREE_OCTETS;
        e[0] = (uint8_t)i;
        SDL_memset(e + 1, 0, NS_ARENE_PSEUDO);
        SDL_strlcpy((char *)e + 1, noms[i], NS_ARENE_PSEUDO);
    }
    return 1 + (size_t)n * NS_ARENE_ENTREE_OCTETS;
}

static void test_tableau(void)
{
    static const char *const noms[NS_ARENE_MAX_PLACES] = {
        "Zoe", "Abdel", "Yasmine", "Bo", "Chloe", "Dimitri", "Eva", "Farid"
    };
    uint8_t charge[1 + NS_ARENE_MAX_PLACES * NS_ARENE_ENTREE_OCTETS + 8];
    ns_arene_entree e[NS_ARENE_MAX_PLACES];

    /* UN TABLEAU DE HUIT, le maximum du protocole : 1 + 8 x 25 = 201 octets. */
    const size_t len = forger_tableau(charge, NS_ARENE_MAX_PLACES, noms);
    CHECK(len == 201, "un tableau de huit fait %zu octets et non 201", len);
    CHECK(ns_arene_lire_tableau(charge, len, e, NS_ARENE_MAX_PLACES)
              == NS_ARENE_MAX_PLACES, "un tableau de huit n'a pas été lu");
    for (int i = 0; i < NS_ARENE_MAX_PLACES; ++i) {
        CHECK(e[i].place == (uint8_t)i, "l'entrée %d porte la place %u", i,
              (unsigned)e[i].place);
        CHECK(SDL_strcmp(e[i].pseudo, noms[i]) == 0,
              "l'entrée %d porte « %s » et non « %s »", i, e[i].pseudo, noms[i]);
    }

    /* UN TABLEAU QUI ANNONCE PLUS DE PLACES QU'IL N'EN PORTE. C'est la charge
     * malveillante de base : croire le compte annoncé, c'est lire au-delà. */
    charge[0] = NS_ARENE_MAX_PLACES;
    CHECK(ns_arene_lire_tableau(charge, 1 + 3 * NS_ARENE_ENTREE_OCTETS, e,
                                NS_ARENE_MAX_PLACES) == -1,
          "un tableau annonçant 8 places et n'en portant que 3 a été lu");

    /* Un compte impossible. */
    charge[0] = 9;
    CHECK(ns_arene_lire_tableau(charge, sizeof charge, e, NS_ARENE_MAX_PLACES) == -1,
          "un tableau de NEUF places a été lu");
    charge[0] = 255;
    CHECK(ns_arene_lire_tableau(charge, sizeof charge, e, NS_ARENE_MAX_PLACES) == -1,
          "un tableau de 255 places a été lu");

    /* Une charge vide, et une entrée dont la place est hors bornes. */
    CHECK(ns_arene_lire_tableau(charge, 0, e, NS_ARENE_MAX_PLACES) == -1,
          "une charge vide a été lue");
    (void)forger_tableau(charge, 2, noms);
    charge[1 + NS_ARENE_ENTREE_OCTETS] = 8;      /* la place de la 2e entrée */
    CHECK(ns_arene_lire_tableau(charge, 1 + 2 * NS_ARENE_ENTREE_OCTETS, e,
                                NS_ARENE_MAX_PLACES) == -1,
          "une entrée de place 8 a été lue");

    /* Un appelant trop étroit reçoit -1 et non la moitié d'un tableau, qu'il
     * croirait complet. */
    (void)forger_tableau(charge, 8, noms);
    CHECK(ns_arene_lire_tableau(charge, 201, e, 4) == -1,
          "un tableau de 8 est entré dans un tampon de 4");

    /* UN PSEUDO SANS ZÉRO FINAL : 24 octets pleins. Le relais promet de couper
     * à 23, mais on ne fait pas confiance à ce qui sort d'une socket. */
    (void)forger_tableau(charge, 1, noms);
    SDL_memset(charge + 2, 'Z', NS_ARENE_PSEUDO);
    CHECK(ns_arene_lire_tableau(charge, 1 + NS_ARENE_ENTREE_OCTETS, e, 8) == 1,
          "un pseudo sans zéro a fait échouer la lecture");
    CHECK(SDL_strlen(e[0].pseudo) == NS_ARENE_PSEUDO - 1,
          "un pseudo sans zéro n'a pas été borné à 23 (%zu)",
          SDL_strlen(e[0].pseudo));
}

static void test_etat(void)
{
    uint8_t charge[1 + NS_ARENE_ETAT_OCTETS];
    charge[0] = 5;                                  /* la place émettrice */

    const char *jeu23 = "abcdefghijklmnopqrstuvw";  /* 23, le cas limite */
    CHECK(ns_arene_ecrire_etat(charge + 1, NS_ARENE_ETAT_OCTETS, true, 3, jeu23,
                               true, -9007199254740993ll, -12345, 7, 999)
              == NS_ARENE_ETAT_OCTETS, "un ÉTAT valable n'a pas été écrit");

    ns_arene_place pl;
    SDL_zero(pl);
    uint8_t place = 0;
    CHECK(ns_arene_lire_etat(charge, sizeof charge, &place, &pl),
          "un ÉTAT valable n'a pas été lu");
    CHECK(place == 5, "la place émettrice vaut %u et non 5", (unsigned)place);
    CHECK(pl.vivante, "la place est rendue morte");
    CHECK(pl.hard, "le régime difficile n'a pas survécu");
    CHECK(pl.camp == 3, "le camp vaut %u et non 3", (unsigned)pl.camp);
    /* UN SCORE NÉGATIF ET SUR 64 BITS : la valeur vient d'un pair, et aucun
     * pair n'est digne de confiance — mais elle doit revenir telle quelle. */
    CHECK(pl.score == -9007199254740993ll, "le score de 64 bits n'a pas survécu");
    CHECK(pl.points == -12345, "des points négatifs n'ont pas survécu (%d)",
          pl.points);
    CHECK(pl.fusibles == 7, "les fusibles valent %d et non 7", pl.fusibles);
    CHECK(pl.valeur == 999, "la valeur vaut %d et non 999", pl.valeur);
    CHECK(SDL_strcmp(pl.jeu, jeu23) == 0, "la borne jouée vaut « %s »", pl.jeu);
    CHECK(pl.etat_recu, "un ÉTAT lu n'est pas marqué reçu");

    /* « Ne joue pas » se dit par un nom vide, comme dans `room_cp_place`. */
    (void)ns_arene_ecrire_etat(charge + 1, NS_ARENE_ETAT_OCTETS, false, 0, NULL,
                               false, 0, 0, 0, 0);
    CHECK(ns_arene_lire_etat(charge, sizeof charge, &place, &pl), "ÉTAT vide");
    CHECK(pl.jeu[0] == '\0', "un jeu NULL n'a pas donné un nom vide");
    CHECK(!pl.vivante && !pl.hard, "les drapeaux nuls n'ont pas survécu");

    /* UNE TRAME TRONQUÉE : un octet de moins que ce qu'il faut. */
    CHECK(!ns_arene_lire_etat(charge, sizeof charge - 1, &place, &pl),
          "un ÉTAT tronqué d'un octet a été lu");
    CHECK(!ns_arene_lire_etat(charge, 0, &place, &pl), "un ÉTAT vide a été lu");

    /* Une place émettrice hors bornes ne peut pas exister : le relais n'en
     * écrit jamais, donc celle-là vient d'ailleurs. */
    charge[0] = 8;
    CHECK(!ns_arene_lire_etat(charge, sizeof charge, &place, &pl),
          "un ÉTAT de la place 8 a été lu");
    charge[0] = 255;
    CHECK(!ns_arene_lire_etat(charge, sizeof charge, &place, &pl),
          "un ÉTAT de la place 255 a été lu");

    /* Un tampon d'écriture trop court. */
    CHECK(ns_arene_ecrire_etat(charge, NS_ARENE_ETAT_OCTETS - 1, true, 0, "x",
                               false, 0, 0, 0, 0) == 0,
          "un ÉTAT est entré dans un tampon trop court");
}

static void test_action(void)
{
    uint8_t charge[1 + NS_ARENE_ACTION_OCTETS];
    charge[0] = 6;                                  /* l'AUTEUR, mis par le relais */
    CHECK(ns_arene_ecrire_action(charge + 1, NS_ARENE_ACTION_OCTETS, 2, 4)
              == NS_ARENE_ACTION_OCTETS, "une ACTION n'a pas été écrite");

    uint8_t auteur = 0, cible = 0, quoi = 0;
    CHECK(ns_arene_lire_action(charge, sizeof charge, &auteur, &cible, &quoi),
          "une ACTION valable n'a pas été lue");
    CHECK(auteur == 6, "l'auteur vaut %u et non 6", (unsigned)auteur);
    CHECK(cible == 2, "la cible vaut %u et non 2", (unsigned)cible);
    CHECK(quoi == 4, "l'action vaut %u et non 4", (unsigned)quoi);

    CHECK(!ns_arene_lire_action(charge, 2, &auteur, &cible, &quoi),
          "une ACTION tronquée a été lue");
    charge[0] = 8;
    CHECK(!ns_arene_lire_action(charge, sizeof charge, &auteur, &cible, &quoi),
          "une ACTION de la place 8 a été lue");
}

static void test_verdict(void)
{
    uint8_t charge[1 + NS_ARENE_VERDICT_OCTETS];
    charge[0] = 0;                                  /* l'arbitre */
    CHECK(ns_arene_ecrire_verdict(charge + 1, NS_ARENE_VERDICT_OCTETS, 3,
                                  NS_ARENE_AUCUN_CAMP, 7, 315000u)
              == NS_ARENE_VERDICT_OCTETS, "un VERDICT n'a pas été écrit");

    uint8_t em = 9, sortie = 0, vainqueur = 0, numero = 0;
    uint32_t horloge = 0;
    CHECK(ns_arene_lire_verdict(charge, sizeof charge, &em, &sortie, &vainqueur,
                                &numero, &horloge),
          "un VERDICT valable n'a pas été lu");
    CHECK(em == 0, "la place émettrice vaut %u et non 0", (unsigned)em);
    CHECK(sortie == 3, "la place sortie vaut %u et non 3", (unsigned)sortie);
    CHECK(vainqueur == NS_ARENE_AUCUN_CAMP, "la manche est déclarée finie");
    CHECK(numero == 7, "le numéro vaut %u et non 7", (unsigned)numero);
    /* 315 s : la durée d'une manche pleine de huit places, 8 x 45 s moins la
     * dernière. Elle doit tenir dans les 32 bits de millisecondes. */
    CHECK(horloge == 315000u, "l'horloge vaut %u et non 315000", horloge);

    /* « Personne n'est tombé » et « le camp 2 a gagné ». */
    (void)ns_arene_ecrire_verdict(charge + 1, NS_ARENE_VERDICT_OCTETS,
                                  NS_ARENE_AUCUNE_PLACE, 2, 20, 900000u);
    CHECK(ns_arene_lire_verdict(charge, sizeof charge, &em, &sortie, &vainqueur,
                                &numero, &horloge), "VERDICT de fin");
    CHECK(sortie == NS_ARENE_AUCUNE_PLACE, "« personne » n'a pas survécu");
    CHECK(vainqueur == 2, "le camp vainqueur vaut %u et non 2", (unsigned)vainqueur);
    /* Le plafond de manche, `ROOM_CP_MANCHE_MAX_S`, vaut 900 s. */
    CHECK(horloge == 900000u, "le plafond de manche n'a pas survécu");

    CHECK(!ns_arene_lire_verdict(charge, sizeof charge - 1, &em, &sortie,
                                 &vainqueur, &numero, &horloge),
          "un VERDICT tronqué d'un octet a été lu");

    /*
     * UN VERDICT VENU DE LA PLACE 1. Le décodeur ne juge que la FORME : il rend
     * la place émettrice, et c'est le module qui refuse — parce qu'il est le
     * seul à connaître le tableau des places, donc le seul à savoir qui a le
     * droit d'arbitrer à cet instant. La règle du refus est `ns_arene_arbitre_de`
     * et elle est vérifiée plus haut, sur les 256 tableaux possibles.
     */
    charge[0] = 1;
    CHECK(ns_arene_lire_verdict(charge, sizeof charge, &em, &sortie, &vainqueur,
                                &numero, &horloge) && em == 1,
          "le décodeur ne rend pas la place émettrice d'un verdict");
    charge[0] = 8;
    CHECK(!ns_arene_lire_verdict(charge, sizeof charge, &em, &sortie, &vainqueur,
                                 &numero, &horloge),
          "un VERDICT de la place 8 a été lu");
}

static void test_effet(void)
{
    uint8_t charge[1 + NS_ARENE_EFFET_OCTETS];
    charge[0] = 0;
    CHECK(ns_arene_ecrire_effet(charge + 1, NS_ARENE_EFFET_OCTETS, 5, 2, 3,
                                NS_ARENE_RENVOYEE) == NS_ARENE_EFFET_OCTETS,
          "un EFFET n'a pas été écrit");

    uint8_t em = 9, auteur = 0, cible = 0, quoi = 0;
    ns_arene_issue issue = NS_ARENE_PASSEE;
    CHECK(ns_arene_lire_effet(charge, sizeof charge, &em, &auteur, &cible, &quoi,
                              &issue), "un EFFET valable n'a pas été lu");
    CHECK(em == 0, "la place émettrice vaut %u et non 0", (unsigned)em);
    /*
     * DEUX OCTETS D'AUTEUR ET CE N'EST PAS UNE REDONDANCE : le premier dit
     * « c'est l'arbitre qui parle », le second « de qui venait l'action ».
     */
    CHECK(auteur == 5, "l'auteur de l'action vaut %u et non 5", (unsigned)auteur);
    CHECK(cible == 2, "la cible vaut %u et non 2", (unsigned)cible);
    CHECK(quoi == 3, "l'action vaut %u et non 3", (unsigned)quoi);
    CHECK(issue == NS_ARENE_RENVOYEE, "l'issue n'a pas survécu");

    CHECK(!ns_arene_lire_effet(charge, sizeof charge - 1, &em, &auteur, &cible,
                               &quoi, &issue), "un EFFET tronqué a été lu");

    /* UNE ISSUE HORS DE L'ÉNUMÉRATION : elle deviendrait un `switch` sans
     * branche chez celui qui affiche. */
    charge[4] = 200;
    CHECK(!ns_arene_lire_effet(charge, sizeof charge, &em, &auteur, &cible,
                               &quoi, &issue), "une issue de 200 a été lue");
}

/* -------------------------------------------------------------------------- */
/* LE DÉCOUPAGE : TCP est un FLUX                                              */
/* -------------------------------------------------------------------------- */

/* Pose une trame complète dans `out` et rend sa taille. */
static size_t forger_trame(uint8_t *out, uint8_t type, const uint8_t *charge,
                           uint16_t len)
{
    out[0] = (uint8_t)(len & 0xFFu);
    out[1] = (uint8_t)(len >> 8);
    out[2] = type;
    if (len && charge) SDL_memcpy(out + 3, charge, len);
    return (size_t)len + 3;
}

static void test_decoupage(void)
{
    uint8_t flux[2048];
    uint8_t type = 0;
    const uint8_t *charge = NULL;
    uint16_t clen = 0;

    /* Une trame entière, d'un coup. */
    uint8_t action[NS_ARENE_ACTION_OCTETS + 1] = { 2, 1, 4 };
    size_t n = forger_trame(flux, NS_ARENE_T_ACTION, action, 3);
    CHECK(ns_arene_trame(flux, n, &type, &charge, &clen) == (int)n,
          "une trame entière n'a pas été consommée d'un coup");
    CHECK(type == NS_ARENE_T_ACTION, "le type n'a pas survécu au découpage");
    CHECK(clen == 3 && charge == flux + 3, "la charge n'est pas au bon endroit");

    /*
     * UNE TRAME QUI ARRIVE EN DEUX MORCEAUX — et en réalité octet par octet,
     * ce qui est le pire cas. Tant qu'elle n'est pas entière, le découpeur doit
     * rendre 0 et NE RIEN consommer. C'est le défaut classique d'un lecteur de
     * socket, et le seul moyen de le voir est de le provoquer.
     */
    for (size_t k = 0; k < n; ++k) {
        CHECK(ns_arene_trame(flux, k, &type, &charge, &clen) == 0,
              "une trame incomplète (%zu octets sur %zu) a été consommée", k, n);
    }
    CHECK(ns_arene_trame(flux, n, &type, &charge, &clen) == (int)n,
          "la trame n'a pas été consommée une fois entière");

    /* DEUX TRAMES DANS UN SEUL PAQUET. */
    uint8_t verdict[NS_ARENE_VERDICT_OCTETS + 1];
    verdict[0] = 0;
    (void)ns_arene_ecrire_verdict(verdict + 1, NS_ARENE_VERDICT_OCTETS, 3,
                                  NS_ARENE_AUCUN_CAMP, 1, 45000u);
    const size_t n1 = forger_trame(flux, NS_ARENE_T_ACTION, action, 3);
    const size_t n2 = forger_trame(flux + n1, NS_ARENE_T_VERDICT, verdict,
                                   1 + NS_ARENE_VERDICT_OCTETS);
    size_t off = 0;
    int pris = ns_arene_trame(flux + off, n1 + n2 - off, &type, &charge, &clen);
    CHECK(pris == (int)n1, "la première des deux trames n'a pas été découpée");
    CHECK(type == NS_ARENE_T_ACTION, "la première trame a changé de type");
    off += (size_t)pris;
    pris = ns_arene_trame(flux + off, n1 + n2 - off, &type, &charge, &clen);
    CHECK(pris == (int)n2, "la seconde des deux trames n'a pas été découpée");
    CHECK(type == NS_ARENE_T_VERDICT, "la seconde trame a changé de type");
    off += (size_t)pris;
    CHECK(off == n1 + n2, "le découpage n'a pas consommé exactement les deux");
    CHECK(ns_arene_trame(flux + off, 0, &type, &charge, &clen) == 0,
          "un flux épuisé a rendu une trame");

    /*
     * UNE CHARGE DE 511 OCTETS — le maximum qu'on ait le droit d'ÉMETTRE, parce
     * que le relais écrit un octet de place devant et que 512 lui ferait fermer
     * la connexion.
     */
    uint8_t gros[NS_ARENE_LIBRE_MAX];
    for (size_t i = 0; i < sizeof gros; ++i) gros[i] = (uint8_t)(i & 0xFFu);
    n = forger_trame(flux, 0x40, gros, (uint16_t)sizeof gros);
    CHECK(n == NS_ARENE_LIBRE_MAX + 3, "une charge de 511 fait %zu octets", n);
    CHECK(ns_arene_trame(flux, n, &type, &charge, &clen) == (int)n,
          "une charge de 511 octets n'a pas été découpée");
    CHECK(clen == NS_ARENE_LIBRE_MAX && charge[510] == gros[510],
          "une charge de 511 octets a été abîmée");

    /* 512, la borne du relais, doit passer À LA LECTURE : c'est ce qu'il a le
     * droit de nous envoyer. */
    n = forger_trame(flux, 0x40, gros, 0);
    flux[0] = 0x00; flux[1] = 0x02;                  /* 512 annoncés */
    SDL_memset(flux + 3, 0x5A, NS_ARENE_CHARGE_MAX);
    CHECK(ns_arene_trame(flux, 3 + NS_ARENE_CHARGE_MAX, &type, &charge, &clen)
              == 3 + NS_ARENE_CHARGE_MAX,
          "une charge de 512 octets a été refusée à la lecture");

    /*
     * UNE TRAME TROP LONGUE : 513 annoncés. Le décodeur doit rendre -1 SANS
     * RIEN ALLOUER ni attendre les octets promis. Croire une longueur annoncée
     * est le défaut de base d'un lecteur de socket, et c'est ce que le relais
     * refuse aussi de son côté.
     */
    flux[0] = 0x01; flux[1] = 0x02;                  /* 513 */
    CHECK(ns_arene_trame(flux, 3, &type, &charge, &clen) == -1,
          "une trame de 513 octets annoncés a été acceptée");
    flux[0] = 0xFF; flux[1] = 0xFF;                  /* 65 535 */
    CHECK(ns_arene_trame(flux, 3, &type, &charge, &clen) == -1,
          "une trame de 65 535 octets annoncés a été acceptée");
    /* Et le refus ne dépend PAS de ce qui suit : trois octets suffisent à le
     * prononcer, ce qui est exactement le point. */
    CHECK(ns_arene_trame(flux, sizeof flux, &type, &charge, &clen) == -1,
          "une trame trop longue a été acceptée quand les octets étaient là");

    /* Moins de trois octets : on ne sait encore rien, donc on n'affirme rien. */
    CHECK(ns_arene_trame(flux, 0, &type, &charge, &clen) == 0, "0 octet");
    CHECK(ns_arene_trame(flux, 1, &type, &charge, &clen) == 0, "1 octet");
    CHECK(ns_arene_trame(flux, 2, &type, &charge, &clen) == 0, "2 octets");
    CHECK(ns_arene_trame(NULL, 8, &type, &charge, &clen) == 0, "un flux NULL");

    /* Une trame de charge NULLE est légale : trois octets, rien derrière. */
    n = forger_trame(flux, NS_ARENE_T_ETAT, NULL, 0);
    CHECK(ns_arene_trame(flux, n, &type, &charge, &clen) == 3 && clen == 0,
          "une trame de charge nulle n'a pas été découpée");
}

/* -------------------------------------------------------------------------- */
/* Contre un VRAI relais                                                       */
/* -------------------------------------------------------------------------- */

/* Attend que les deux arènes atteignent `vise`, ou rend false. */
static bool attendre(ns_arene *a, ns_arene *b, ns_arene_liaison vise, uint32_t ms)
{
    const uint64_t fin = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < fin) {
        if (ns_arene_etat(a) == vise && ns_arene_etat(b) == vise) return true;
        if (ns_arene_etat(a) == NS_ARENE_ERREUR ||
            ns_arene_etat(b) == NS_ARENE_ERREUR) return false;
        SDL_Delay(4);
    }
    return false;
}

/* Attend un événement d'un type donné, et le rend. */
static bool attendre_evt(ns_arene *a, ns_arene_evt type,
                         ns_arene_evenement *out, uint32_t ms)
{
    const uint64_t fin = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < fin) {
        ns_arene_evenement ev;
        while (ns_arene_prendre(a, &ev)) {
            if (ev.type == type) { if (out) *out = ev; return true; }
        }
        SDL_Delay(4);
    }
    return false;
}

static void test_salon(const char *hote, uint16_t port)
{
    ns_arene_config ca, cb;
    SDL_zero(ca);
    ca.hote = hote;
    ca.port = port;
    ca.salon = (uint64_t)SDL_GetTicks() * 1000u + 11u;
    ca.places = 2;
    ca.pseudo = "ARBITRE";
    ca.delai_ms = 2000;
    cb = ca;
    cb.place = 1;
    cb.pseudo = "RIVALE";

    char ea[160] = { 0 }, eb[160] = { 0 };
    const uint32_t sockets_avant = ns_arene_sockets();
    ns_arene *a = ns_arene_ouvrir(&ca, ea, sizeof ea);
    ns_arene *b = ns_arene_ouvrir(&cb, eb, sizeof eb);
    CHECK(a != NULL, "place 0 : %s", ea);
    CHECK(b != NULL, "place 1 : %s", eb);
    if (!a || !b) { ns_arene_fermer(a); ns_arene_fermer(b); return; }

    /* LE COUP D'ENVOI part quand toutes les places sont prises, après le
     * tableau de la dernière arrivée. */
    CHECK(attendre(a, b, NS_ARENE_COURSE, 4000),
          "le salon de deux n'a pas démarré (A=%d « %s », B=%d « %s »)",
          (int)ns_arene_etat(a), ns_arene_erreur(a),
          (int)ns_arene_etat(b), ns_arene_erreur(b));
    if (ns_arene_etat(a) != NS_ARENE_COURSE) {
        ns_arene_fermer(a); ns_arene_fermer(b); return;
    }

    /*
     * Le compteur de sockets se lit ICI et non au retour de `ns_arene_ouvrir` :
     * la socket s'ouvre SUR LE FIL, ce qui est toute la raison d'être de ce
     * module — l'ouverture ne bloque pas la boucle de jeu. La première version
     * du test le vérifiait juste après l'appel et échouait pour cette raison,
     * ce qui était une bonne nouvelle déguisée en mauvaise.
     */
    CHECK(ns_arene_sockets() >= sockets_avant + 2,
          "deux arènes en course n'ont pas compté deux sockets (%u -> %u)",
          sockets_avant, ns_arene_sockets());

    /* LA MÊME GRAINE DES DEUX CÔTÉS, tirée par le relais et jamais proposée par
     * un client : celui qui joue ne choisit pas ce sur quoi il joue. */
    const uint64_t graine = ns_arene_graine(a);
    CHECK(graine != 0, "le relais n'a pas donné de graine");
    CHECK(ns_arene_graine(b) == graine, "les deux places n'ont pas la même graine");
    CHECK((graine >> 63) == 0, "le bit de poids fort de la graine n'est pas effacé");

    /* LE TABLEAU DES PLACES : c'est le seul endroit d'où viennent les pseudos. */
    ns_arene_place pa[NS_ARENE_MAX_PLACES], pb[NS_ARENE_MAX_PLACES];
    CHECK(ns_arene_places(a, pa, NS_ARENE_MAX_PLACES) == NS_ARENE_MAX_PLACES,
          "la table des places n'est pas indexée par place");
    (void)ns_arene_places(b, pb, NS_ARENE_MAX_PLACES);
    CHECK(pa[0].presente && pa[1].presente, "A ne voit pas les deux places");
    CHECK(pb[0].presente && pb[1].presente, "B ne voit pas les deux places");
    CHECK(SDL_strcmp(pa[1].pseudo, "RIVALE") == 0,
          "A voit la place 1 sous le nom « %s »", pa[1].pseudo);
    CHECK(SDL_strcmp(pb[0].pseudo, "ARBITRE") == 0,
          "B voit la place 0 sous le nom « %s »", pb[0].pseudo);
    CHECK(pa[2].presente == false, "une place vide est déclarée présente");

    /* QUI ARBITRE : la place 0, et elle seule. */
    CHECK(ns_arene_arbitre(a), "la place 0 n'arbitre pas");
    CHECK(!ns_arene_arbitre(b), "la place 1 arbitre alors que la 0 est là");

    /* L'ÉTAT traverse : A publie, B le relit avec ses champs intacts. */
    ns_arene_publier(a, true, 0, "dedale", true, 4242, 130, 5, 171);
    ns_arene_evenement ev;
    CHECK(attendre_evt(b, NS_ARENE_EVT_ETAT, &ev, 3000),
          "l'état de la place 0 n'est jamais arrivé à la place 1");
    if (ev.type == NS_ARENE_EVT_ETAT) {
        CHECK(ev.a == 0, "l'état vient de la place %u et non de la 0",
              (unsigned)ev.a);
        (void)ns_arene_places(b, pb, NS_ARENE_MAX_PLACES);
        CHECK(pb[0].etat_recu, "l'état reçu n'est pas marqué reçu");
        CHECK(SDL_strcmp(pb[0].jeu, "dedale") == 0,
              "la borne jouée est « %s » et non « dedale »", pb[0].jeu);
        CHECK(pb[0].hard, "le régime difficile n'a pas traversé");
        CHECK(pb[0].score == 4242, "le score est %lld et non 4242",
              (long long)pb[0].score);
        CHECK(pb[0].points == 130 && pb[0].fusibles == 5 && pb[0].valeur == 171,
              "les compteurs n'ont pas traversé (%d, %d, %d)",
              pb[0].points, pb[0].fusibles, pb[0].valeur);
        CHECK(pb[0].age_ms < 3000, "l'état arrivé est déjà périmé (%u ms)",
              pb[0].age_ms);
        /* NOTRE PROPRE PLACE N'A JAMAIS D'ÉTAT REÇU : le relais ne nous renvoie
         * pas nos trames. */
        CHECK(!pb[1].etat_recu, "B a reçu son propre état par le réseau");
        CHECK(pb[1].age_ms == UINT32_MAX, "l'âge d'un état jamais reçu n'est pas maximal");
    }

    /*
     * L'ACTION, ET LA BONNE PLACE ÉMETTRICE — le contrôle qui compte le plus.
     *
     * B (place 1) vise A (place 0). B n'écrit PAS son propre numéro : c'est le
     * relais qui l'insère. Si l'octet était écrit par l'émetteur, un joueur
     * pourrait saboter « de la part » d'un autre, et tout le mode repose sur
     * qui a envoyé quoi.
     */
    ns_arene_agir(b, 0, 3);
    CHECK(attendre_evt(a, NS_ARENE_EVT_ACTION, &ev, 3000),
          "l'action de la place 1 n'est jamais arrivée à la place 0");
    if (ev.type == NS_ARENE_EVT_ACTION) {
        CHECK(ev.a == 1, "l'action est attribuée à la place %u et non à la 1",
              (unsigned)ev.a);
        CHECK(ev.b == 0, "l'action vise la place %u et non la 0", (unsigned)ev.b);
        CHECK(ev.valeur == 3, "l'action vaut %d et non 3", ev.valeur);
    }

    /* LE VERDICT descend de l'arbitre vers l'autre place. */
    ns_arene_verdict(a, 1, NS_ARENE_AUCUN_CAMP, 2, 90000u);
    CHECK(attendre_evt(b, NS_ARENE_EVT_VERDICT, &ev, 3000),
          "le verdict de l'arbitre n'est jamais arrivé");
    if (ev.type == NS_ARENE_EVT_VERDICT) {
        CHECK(ev.a == 1, "le verdict sort la place %u et non la 1", (unsigned)ev.a);
        CHECK(ev.b == NS_ARENE_AUCUN_CAMP, "le verdict déclare une fin de manche");
        CHECK(ev.valeur == 2, "le verdict porte le numéro %d et non 2", ev.valeur);
        CHECK(ev.horloge_ms == 90000u, "l'horloge du verdict vaut %u",
              ev.horloge_ms);
    }

    /* L'EFFET aussi, avec ses DEUX auteurs : celui que le relais écrit et celui
     * que l'arbitre rapporte. */
    ns_arene_effet(a, 1, 0, 3, NS_ARENE_ABSORBEE);
    CHECK(attendre_evt(b, NS_ARENE_EVT_EFFET, &ev, 3000),
          "l'effet de l'arbitre n'est jamais arrivé");
    if (ev.type == NS_ARENE_EVT_EFFET) {
        CHECK(ev.a == 1, "l'effet porte l'auteur %u et non 1", (unsigned)ev.a);
        CHECK(ev.issue == NS_ARENE_ABSORBEE, "l'issue n'a pas traversé");
    }

    /*
     * UNE PLACE QUI N'ARBITRE PAS N'ENVOIE PAS DE VERDICT, et le refus se voit
     * DU CÔTÉ QUI A TORT : rien ne part sur le fil. On le mesure au compteur de
     * trames émises, faute de quoi on ne prouverait qu'une absence.
     */
    uint32_t out_avant = 0, out_apres = 0;
    ns_arene_stats(b, NULL, &out_avant, NULL);
    ns_arene_verdict(b, 0, 1, 3, 120000u);
    ns_arene_effet(b, 0, 1, 1, NS_ARENE_PASSEE);
    ns_arene_stats(b, NULL, &out_apres, NULL);
    CHECK(out_apres == out_avant,
          "la place 1 a émis %u trame(s) de verdict alors qu'elle n'arbitre pas",
          out_apres - out_avant);

    /*
     * LA REPRISE DE LA LAME. A raccroche ; le relais laisse sa socket à B — un
     * salon à une place n'est plus un salon, mais couper serait indiscernable
     * d'une panne. B doit alors DEVENIR l'arbitre, sans quoi la manche
     * attendrait pour l'éternité un couperet que plus personne n'envoie.
     */
    ns_arene_fermer(a);
    CHECK(attendre_evt(b, NS_ARENE_EVT_DEPART, &ev, 3000),
          "le départ de la place 0 n'a pas été annoncé");
    CHECK(ev.a == 0, "le départ annonce la place %u et non la 0", (unsigned)ev.a);
    /* Le tableau suit le BYE : on apprend qui part, puis on voit la salle. */
    CHECK(attendre_evt(b, NS_ARENE_EVT_TABLEAU, &ev, 2000),
          "aucun tableau n'a suivi le départ");
    CHECK(ev.a == 1, "le tableau d'après-départ porte %u places et non 1",
          (unsigned)ev.a);
    CHECK(ns_arene_arbitre(b),
          "la place 0 est partie et la place 1 n'a pas repris la lame — "
          "la manche s'arrêterait là");

    uint32_t in_b = 0, out_b = 0, perdus_b = 0;
    ns_arene_stats(b, &in_b, &out_b, &perdus_b);
    CHECK(in_b > 0 && out_b > 0, "aucune trame n'a circulé (%u reçues, %u émises)",
          in_b, out_b);
    CHECK(perdus_b == 0, "%u événement(s) perdu(s) sur un salon de deux", perdus_b);
    printf("  salon de 2 : %u trames reçues, %u émises, graine %016llx\n",
           in_b, out_b, (unsigned long long)graine);

    ns_arene_fermer(b);
}

/* Une entrée refusée — place déjà prise — ne doit ni bloquer ni mentir. */
static void test_place_prise(const char *hote, uint16_t port)
{
    ns_arene_config cfg;
    SDL_zero(cfg);
    cfg.hote = hote;
    cfg.port = port;
    cfg.salon = (uint64_t)SDL_GetTicks() * 1000u + 23u;
    cfg.places = 3;
    cfg.place = 0;
    cfg.pseudo = "PREMIERE";
    cfg.delai_ms = 2000;

    char err[160] = { 0 };
    ns_arene *a = ns_arene_ouvrir(&cfg, err, sizeof err);
    CHECK(a != NULL, "la première place : %s", err);
    if (!a) return;

    ns_arene_evenement ev;
    CHECK(attendre_evt(a, NS_ARENE_EVT_TABLEAU, &ev, 3000),
          "aucun tableau n'est arrivé à la première place");

    cfg.pseudo = "INTRUSE";
    ns_arene *c = ns_arene_ouvrir(&cfg, err, sizeof err);
    CHECK(c != NULL, "l'ouverture d'une place prise doit réussir localement : "
                     "c'est le relais qui refuse, pas nous");
    if (c) {
        /* Le relais ferme la connexion. Ça doit se voir comme une fin de
         * liaison AVEC un motif, et jamais comme un salon qui démarre. */
        const uint64_t fin = SDL_GetTicks() + 3000;
        while (SDL_GetTicks() < fin && ns_arene_etat(c) < NS_ARENE_TERMINEE) {
            SDL_Delay(4);
        }
        CHECK(ns_arene_etat(c) >= NS_ARENE_TERMINEE,
              "une place déjà prise a été admise (état %d)", (int)ns_arene_etat(c));
        CHECK(ns_arene_erreur(c)[0] != '\0',
              "une entrée refusée n'a produit aucun motif");
        /* La liaison est morte : on reprend la lame plutôt que d'attendre. */
        CHECK(ns_arene_arbitre(c), "une liaison morte n'a pas rendu la lame");
        printf("  place prise : « %s »\n", ns_arene_erreur(c));
        ns_arene_fermer(c);
    }
    ns_arene_fermer(a);
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    ns_log_set_level(NS_LOG_ERROR);

    /* LE VERROU EN PREMIER, avant que quoi que ce soit n'ait pu activer le
     * réseau : c'est la seule position d'où la mesure veut dire quelque chose. */
    test_verrou();
    test_hors_ligne();
    test_arbitre_de();
    test_join();
    test_tableau();
    test_etat();
    test_action();
    test_verdict();
    test_effet();
    test_decoupage();

    if (argc >= 3) {
        const char *hote = argv[1];
        const int port = SDL_atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            printf("port invalide : %s\n", argv[2]);
            return 2;
        }
        /*
         * LE VERROU EST LEVÉ EN DÉCLARANT UNE URL, et l'URL n'a rien à voir
         * avec le relais : le classement écoute en HTTP, le relais en binaire
         * sur un autre port. C'est exactement ce que le module promet — il
         * hérite du droit d'ouvrir une socket sans hériter de l'adresse.
         */
        ns_online_config oc;
        SDL_zero(oc);
        oc.server_url = "http://127.0.0.1:1";
        CHECK(ns_online_init(&oc), "le réseau démarre même vers un serveur mort");

        printf("salon de deux contre un vrai relais :\n");
        test_salon(hote, (uint16_t)port);
        printf("entrée refusée :\n");
        test_place_prise(hote, (uint16_t)port);

        ns_online_shutdown();
    } else {
        printf("(aucun relais donné : le verrou et le codec seuls sont "
               "vérifiés — lancer « ns_test_arene <hôte> <port> » avec "
               "« go run ./cmd/duelrelay » pour le salon entier)\n");
    }

    printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
