/*
 * test_economie.c — la boucle JETON -> PARTIE -> TICKETS -> LOT, sans écran.
 *
 * Ce que ce test attrape vraiment
 * -------------------------------
 * Quatre familles de défauts, et aucune ne produit d'erreur à l'exécution :
 *
 *   1. UN BARÈME QUI MENT. Le taux d'un jeu écrit au jugé, ou laissé en place
 *      après qu'on a changé l'équilibrage du jeu. Le symptôme est qu'un jeu
 *      paie dix fois les autres, et personne ne s'en aperçoit avant d'avoir
 *      joué aux huit. Le contrôle est mécanique : chaque diviseur est confronté
 *      à la MÉDIANE MESURÉE gardée à côté de lui dans `room_bareme.h`, et doit
 *      rendre la cible. Un taux qu'on modifie sans remesurer casse le build.
 *
 *   2. UNE AFFICHE QUI MENT. C'est la demande explicite du propriétaire. Le
 *      test confronte la table du barème à la liste des jeux réellement portés
 *      (`ns_game_at`) : un jeu porté sans ligne de barème, ou une ligne sans
 *      jeu, arrête la construction. Sans ça, l'affiche « jetons » afficherait
 *      un taux pour un jeu qui n'existe pas, ou tairait un jeu qui existe.
 *
 *   3. UN SOLDE QUI PASSE SOUS ZÉRO, ou qui déborde. Un portefeuille en
 *      `unsigned` qui se décrémente de trop devient une fortune ; ici il est
 *      signé, et c'est `room_eco_valide` qui doit le dire.
 *
 *   4. UNE DATE QUI GLISSE. Le tournoi du jour et la série tiennent à une
 *      seule division. Un jour de décalage au passage de minuit, et deux
 *      joueurs du même tournoi jouent des parties différentes en croyant
 *      s'affronter. Le défaut est INVISIBLE : les deux parties ont l'air
 *      normales.
 *
 * Tout tourne sans GPU, sans fenêtre et sans réseau : c'est la raison d'être de
 * `room_economie.c`, qui n'est qu'un état et des règles.
 */
#include "room_economie.h"
#include "games.h"
#include "ns_core.h"

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
            fprintf(stderr, "ÉCHEC %s:%d — ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
        }                                                                     \
    } while (0)

static char g_tmp[512];

/* La table, redépliée ici pour pouvoir la parcourir : c'est la MÊME liste que
 * celle du jeu et celle des affiches, donc la parcourir revient à les vérifier
 * toutes les trois. */
typedef struct ligne_attendue {
    const char *jeu;
    int32_t     mediane_n, div_n, mediane_h, div_h;
} ligne_attendue;

#define ECO_L(id, mn, dn, mh, dh) { #id, (mn), (dn), (mh), (dh) },
static const ligne_attendue g_table[] = { ROOM_ECO_BAREME(ECO_L) };
#undef ECO_L

/* ========================================================================== */
/* 1. LE BARÈME : calibré, et calibré sur ce qui a été mesuré                  */
/* ========================================================================== */

/*
 * LA PROPRIÉTÉ QUI JUSTIFIE TOUT LE BARÈME : une partie médiane rend le même
 * nombre de tickets sur les huit jeux et les deux régimes.
 *
 * Sans elle, un barème par jeu n'est qu'une liste de nombres arbitraires. Avec
 * elle, c'est une CALIBRATION, et on peut dire pourquoi chaque nombre vaut ce
 * qu'il vaut : il est la médiane mesurée divisée par la cible.
 *
 * La tolérance est d'un ticket vers le bas et zéro vers le haut, parce que le
 * diviseur est un entier arrondi : `38 / 4` vaut 9 et non 9,5. Un intervalle
 * plus large laisserait passer un taux faux de 20 %, qui est exactement l'écart
 * qu'on cherche à interdire.
 */
static void test_bareme_calibre(void)
{
    for (size_t i = 0; i < SDL_arraysize(g_table); ++i) {
        const ligne_attendue *l = &g_table[i];

        for (int mode = 0; mode < 2; ++mode) {
            const bool    hard = (mode == 1);
            const int32_t med  = hard ? l->mediane_h : l->mediane_n;
            const int32_t div  = hard ? l->div_h     : l->div_n;

            CHECK(div > 0, "%s/%s : diviseur nul", l->jeu, hard ? "hard" : "normal");
            CHECK(med > 0, "%s/%s : médiane nulle", l->jeu, hard ? "hard" : "normal");
            if (div <= 0 || med <= 0) continue;

            CHECK(room_eco_diviseur(l->jeu, hard) == div,
                  "%s/%s : le module rend %d, la table dit %d",
                  l->jeu, hard ? "hard" : "normal",
                  room_eco_diviseur(l->jeu, hard), div);
            CHECK(room_eco_mediane(l->jeu, hard) == med,
                  "%s/%s : médiane %d au lieu de %d", l->jeu,
                  hard ? "hard" : "normal", room_eco_mediane(l->jeu, hard), med);

            /* Le diviseur DÉCOULE de la médiane : il en est le quotient par la
             * cible, arrondi. C'est ce contrôle-ci qui interdit d'écrire un
             * taux sans l'avoir mesuré. */
            const int32_t attendu = (med + ROOM_ECO_CIBLE_TICKETS / 2) / ROOM_ECO_CIBLE_TICKETS;
            CHECK(div == attendu || (attendu == 0 && div == 1),
                  "%s/%s : diviseur %d, or la médiane %d en impose %d",
                  l->jeu, hard ? "hard" : "normal", div, med, attendu);

            /* Et la propriété visible : la médiane rend la cible, à un ticket
             * près. Série nulle, pour ne mesurer que le barème. */
            const int32_t t = room_eco_tickets_pour(l->jeu, hard, med, 0);
            const int32_t base = hard
                ? (ROOM_ECO_CIBLE_TICKETS * (100 + ROOM_ECO_PRIME_DIFFICILE_PCT)) / 100
                : ROOM_ECO_CIBLE_TICKETS;
            CHECK(t <= base && t >= base - 2,
                  "%s/%s : une partie médiane (%d points) rend %d tickets, "
                  "attendu %d ou juste en dessous",
                  l->jeu, hard ? "hard" : "normal", med, t, base);
        }
    }
}

/*
 * LA TABLE ET LES JEUX PORTÉS SE CORRESPONDENT — le contrôle qui empêche
 * l'affiche de mentir par omission.
 *
 * Porter un neuvième jeu sans lui donner de barème le ferait payer ZÉRO ticket,
 * silencieusement : `room_eco_tickets_pour` rend 0 sur un jeu inconnu, ce qui
 * est le bon comportement en jeu et le pire des symptômes pour un développeur.
 * Ici, ça arrête la construction.
 */
static void test_bareme_couvre_les_jeux(void)
{
    const int n = ns_game_count();
    CHECK((size_t)n == SDL_arraysize(g_table),
          "%d jeu(x) porté(s) pour %zu ligne(s) de barème",
          n, SDL_arraysize(g_table));

    for (int i = 0; i < n; ++i) {
        const ns_game_api *api = ns_game_at(i);
        CHECK(api != NULL, "jeu %d nul", i);
        if (!api) continue;
        CHECK(room_eco_diviseur(api->id, false) > 0,
              "le jeu porté « %s » n'a pas de barème normal", api->id);
        CHECK(room_eco_diviseur(api->id, true) > 0,
              "le jeu porté « %s » n'a pas de barème difficile", api->id);
    }

    for (size_t i = 0; i < SDL_arraysize(g_table); ++i) {
        CHECK(ns_game_find(g_table[i].jeu) != NULL,
              "le barème tarife « %s », qui n'est pas un jeu porté", g_table[i].jeu);
    }
}

/* Les entrées absurdes. Aucune ne doit produire de solde négatif ni de
 * débordement — ce sont celles qui viennent d'un fichier ou d'un serveur. */
static void test_bareme_entrees_absurdes(void)
{
    CHECK(room_eco_tickets_pour("envol", false, -1, 0) == 0, "score -1 rapporte");
    CHECK(room_eco_tickets_pour("envol", false, INT64_MIN, 0) == 0, "score minimal rapporte");
    CHECK(room_eco_tickets_pour(NULL, false, 1000, 0) == 0, "jeu NULL rapporte");
    CHECK(room_eco_tickets_pour("", false, 1000, 0) == 0, "jeu vide rapporte");
    CHECK(room_eco_tickets_pour("pacman", false, 1000, 0) == 0,
          "un jeu non porté rapporte");

    /* Le plafond par partie : un score énorme ne doit pas rendre une fortune,
     * et surtout ne doit pas déborder. */
    const int32_t plafond = ROOM_ECO_CIBLE_TICKETS * ROOM_ECO_PLAFOND_PARTIE;
    const int32_t enorme = room_eco_tickets_pour("envol", false, INT64_MAX, 0);
    CHECK(enorme == plafond, "score maximal rend %d tickets, plafond %d", enorme, plafond);
    const int32_t enorme_h = room_eco_tickets_pour("aplomb", true, INT64_MAX, 99);
    CHECK(enorme_h == plafond + ROOM_ECO_SERIE_MAX,
          "score maximal + série rend %d, attendu %d",
          enorme_h, plafond + ROOM_ECO_SERIE_MAX);

    /* Le régime difficile paie davantage à score égal, sur toute la table. */
    for (size_t i = 0; i < SDL_arraysize(g_table); ++i) {
        const ligne_attendue *l = &g_table[i];
        const int32_t tn = room_eco_tickets_pour(l->jeu, false, l->mediane_n, 0);
        const int32_t th = room_eco_tickets_pour(l->jeu, true, l->mediane_h, 0);
        CHECK(th >= tn, "%s : la médiane difficile rend %d, la normale %d",
              l->jeu, th, tn);
    }
}

/* ========================================================================== */
/* 2. LE PORTEFEUILLE : jamais sous zéro, jamais bloqué                       */
/* ========================================================================== */

static void test_jamais_sous_zero(void)
{
    room_eco e;
    room_eco_reset(&e);
    CHECK(room_eco_valide(&e), "un portefeuille neuf est invalide");
    CHECK(e.jetons == 0 && e.tickets == 0, "un portefeuille neuf n'est pas vide");

    /* Sans jeton, on ne joue pas — et on ne descend pas sous zéro. */
    CHECK(!room_eco_inserer(&e), "on a pu insérer un jeton qu'on n'avait pas");
    CHECK(e.jetons == 0, "le solde est passé à %d", e.jetons);

    for (int i = 0; i < 50; ++i) (void)room_eco_inserer(&e);
    CHECK(e.jetons == 0 && room_eco_valide(&e),
          "cinquante insertions à vide ont laissé %d jetons", e.jetons);

    /* Le monnayeur relève au plancher, et c'est la garantie de ne jamais être
     * bloqué : sans lui, le paragraphe précédent serait une fin de partie. */
    const int32_t rendu = room_eco_monnayeur(&e);
    CHECK(rendu == ROOM_ECO_PLANCHER_ACCUEIL, "le monnayeur a rendu %d jetons", rendu);
    CHECK(e.jetons == ROOM_ECO_PLANCHER_ACCUEIL, "solde %d après monnayeur", e.jetons);

    /* Et il le refait, indéfiniment, sans minuterie. C'est la propriété qui
     * rend la salle injouable-proof : on peut toujours reprendre un jeton. */
    for (int i = 0; i < ROOM_ECO_PLANCHER_ACCUEIL; ++i) {
        CHECK(room_eco_inserer(&e), "insertion %d refusée", i);
    }
    CHECK(e.jetons == 0, "solde %d après avoir tout dépensé", e.jetons);
    CHECK(room_eco_monnayeur(&e) == ROOM_ECO_PLANCHER_ACCUEIL,
          "le monnayeur ne recharge pas une deuxième fois");

    /* Un monnayeur actionné alors qu'on est au-dessus du plancher ne retire
     * rien : il complète, il ne nivelle pas. */
    e.jetons = 40;
    CHECK(room_eco_monnayeur(&e) == 0, "le monnayeur a bougé un solde suffisant");
    CHECK(e.jetons == 40, "le monnayeur a ramené 40 jetons à %d", e.jetons);

    /*
     * LE CRÉDIT D'ACCUEIL NE MANGE PAS LES TICKETS.
     *
     * C'est le contrôle qui fixe le défaut que `test_session` a chiffré : le
     * monnayeur changeait les tickets en jetons avant de compléter, ce qui
     * faisait payer dix tickets un jeton qu'on obtenait gratuitement juste
     * après. Vingt parties médianes rapportaient alors 57 tickets au lieu de
     * 200, et la vitrine restait inatteignable sans qu'un seul message le dise.
     */
    room_eco_reset(&e);
    e.tickets = 431;
    CHECK(room_eco_monnayeur(&e) == ROOM_ECO_PLANCHER_ACCUEIL,
          "le crédit d'accueil n'a pas rempli");
    CHECK(e.tickets == 431, "le crédit d'accueil a consommé des tickets (%d)", e.tickets);

    /* LE CHANGE, lui, est demandé — et il est exact. */
    const int32_t change = room_eco_changer(&e, 3);
    CHECK(change == 3, "%d jetons rendus pour 3 demandés", change);
    CHECK(e.tickets == 431 - 3 * ROOM_ECO_TICKETS_PAR_JETON,
          "reste %d tickets", e.tickets);
    CHECK(e.jetons == ROOM_ECO_PLANCHER_ACCUEIL + 3, "solde %d après change", e.jetons);
    CHECK(room_eco_valide(&e), "portefeuille invalide après change");

    /* On ne change pas ce qu'on n'a pas, et rien n'est débité dans ce cas. */
    room_eco_reset(&e);
    e.tickets = ROOM_ECO_TICKETS_PAR_JETON - 1;
    CHECK(room_eco_changer(&e, 1) == 0, "un change s'est fait à découvert");
    CHECK(e.tickets == ROOM_ECO_TICKETS_PAR_JETON - 1, "le change a débité (%d)", e.tickets);
    CHECK(room_eco_changer(&e, 0) == 0, "un change de zéro a rendu quelque chose");
    CHECK(room_eco_changer(&e, -5) == 0, "un change négatif a rendu quelque chose");
    CHECK(room_eco_valide(&e), "portefeuille invalide après un change refusé");

    /* Une demande plus grande que le portefeuille rend ce qu'on peut, pas plus. */
    e.tickets = 25;
    CHECK(room_eco_changer(&e, 99) == 2, "le change n'a pas rendu les 2 jetons possibles");
    CHECK(e.tickets == 5, "reste %d tickets au lieu de 5", e.tickets);
}

/* ========================================================================== */
/* 3. LE TOURNOI DU JOUR                                                      */
/* ========================================================================== */

/*
 * LA MÊME GRAINE LE MÊME JOUR, UNE AUTRE LE LENDEMAIN.
 *
 * Le passage de minuit et le changement d'année sont testés explicitement parce
 * que ce sont les deux endroits où une règle de date se trompe d'un jour, et
 * qu'aucun des deux ne produit d'erreur : le tournoi bascule simplement au
 * mauvais moment, ou ne bascule pas.
 */
static void test_tournoi(void)
{
    /* 2026-08-30 12:00:00 UTC. */
    const int64_t midi = 1787054400ll;
    room_eco_set_horloge(midi);
    const int64_t j = room_eco_jour();

    /* Deux appels le même jour, à des heures différentes : même graine. */
    const uint64_t g_midi = room_eco_graine_du_jour("envol");
    room_eco_set_horloge(midi - 11 * 3600);          /* 01:00 le même jour */
    CHECK(room_eco_jour() == j, "01:00 n'est pas le même jour que midi");
    CHECK(room_eco_graine_du_jour("envol") == g_midi,
          "la graine a changé entre 01:00 et 12:00");
    room_eco_set_horloge(midi + 11 * 3600 + 3599);   /* 23:59:59 */
    CHECK(room_eco_jour() == j, "23:59:59 n'est pas le même jour que midi");
    CHECK(room_eco_graine_du_jour("envol") == g_midi,
          "la graine a changé entre 12:00 et 23:59:59");

    /* LE PASSAGE DE MINUIT : une seconde plus tard, c'est un autre jour. */
    room_eco_set_horloge(midi + 11 * 3600 + 3600);   /* 00:00:00 le lendemain */
    CHECK(room_eco_jour() == j + 1, "minuit n'a pas fait avancer le jour");
    CHECK(room_eco_graine_du_jour("envol") != g_midi,
          "la graine du lendemain est celle de la veille");

    /* LE CHANGEMENT D'ANNÉE : 2026-12-31 23:59:59 UTC puis 2027-01-01. */
    const int64_t saint_sylvestre = 1798761599ll;
    room_eco_set_horloge(saint_sylvestre);
    const int64_t jd = room_eco_jour();
    const uint64_t g31 = room_eco_graine_du_jour("snake");
    room_eco_set_horloge(saint_sylvestre + 1);
    CHECK(room_eco_jour() == jd + 1, "le 1er janvier n'a pas fait avancer le jour");
    CHECK(room_eco_graine_du_jour("snake") != g31,
          "la graine du 1er janvier est celle du 31 décembre");

    /* La graine PROPAGE aux huit jeux, et elle diffère pour chacun : sans ça,
     * le tournoi ferait jouer huit fois la même grille. */
    uint64_t vues[32];
    const int n = ns_game_count();
    CHECK(n > 0 && n <= 32, "%d jeux", n);
    for (int i = 0; i < n && i < 32; ++i) {
        const ns_game_api *api = ns_game_at(i);
        vues[i] = room_eco_graine_tournoi(j, api->id);
        CHECK(vues[i] != 0, "graine nulle pour « %s »", api->id);
        for (int k = 0; k < i; ++k) {
            CHECK(vues[i] != vues[k], "« %s » et « %s » tirent la même graine",
                  api->id, ns_game_at(k)->id);
        }
    }

    /* Et elle EST rejouable : le même jour et le même jeu redonnent le même
     * état de départ sur les huit jeux. C'est la propriété que `test_replay.c`
     * établit au bit près ; on vérifie ici que le tournoi s'y branche. */
    for (int i = 0; i < n; ++i) {
        const ns_game_api *api = ns_game_at(i);
        const uint64_t graine = room_eco_graine_tournoi(j, api->id);
        void *a = SDL_calloc(1, api->state_size);
        void *b = SDL_calloc(1, api->state_size);
        CHECK(a && b, "mémoire épuisée pour « %s »", api->id);
        if (!a || !b) { SDL_free(a); SDL_free(b); continue; }
        api->reset(a, graine, false);
        api->reset(b, graine, false);
        CHECK(ns_game_state_hash(api, a) == ns_game_state_hash(api, b),
              "« %s » : deux parties de la même graine du jour diffèrent", api->id);

        /* Et le lendemain, ce n'est pas la même partie. Un jeu dont l'état de
         * départ ne dépend pas de la graine ferait un tournoi immuable — le
         * défaut serait invisible, tout le monde jouant « la partie du jour »
         * qui ne change jamais. */
        api->reset(b, room_eco_graine_tournoi(j + 1, api->id), false);
        CHECK(ns_game_state_hash(api, a) != ns_game_state_hash(api, b),
              "« %s » : la partie de demain est celle d'aujourd'hui", api->id);
        SDL_free(a);
        SDL_free(b);
    }

    /* Avant l'epoch, la division doit rester euclidienne : `-1 / 86400` vaut 0
     * en C, ce qui mettrait la dernière seconde de 1969 le même jour que la
     * première de 1970. */
    CHECK(room_eco_jour_de(-1) == -1, "la seconde avant l'epoch tombe le jour %lld",
          (long long)room_eco_jour_de(-1));
    CHECK(room_eco_jour_de(0) == 0, "l'epoch n'est pas le jour 0");
    CHECK(room_eco_jour_de(86399) == 0, "la fin du premier jour a débordé");
    CHECK(room_eco_jour_de(86400) == 1, "le deuxième jour n'est pas 1");

    room_eco_set_horloge(0);
}

/* ========================================================================== */
/* 4. LA SÉRIE                                                                */
/* ========================================================================== */

static void test_serie(void)
{
    const int64_t j0 = 1787054400ll;         /* un midi quelconque */
    const int64_t jour = 86400;

    room_eco e;
    room_eco_reset(&e);

    /* Trois jours consécutifs : la série monte d'un par jour. */
    for (int d = 0; d < 3; ++d) {
        room_eco_set_horloge(j0 + jour * d);
        (void)room_eco_fin_partie(&e, "envol", false, 38);
        (void)room_eco_encaisser(&e);
        CHECK(e.serie == d + 1, "jour %d : série %d au lieu de %d", d, e.serie, d + 1);
    }
    CHECK(e.serie_record == 3, "record %d au lieu de 3", e.serie_record);

    /* Deux parties le MÊME jour ne comptent qu'une fois : sinon la série
     * mesurerait l'acharnement d'une soirée et non l'assiduité. */
    (void)room_eco_fin_partie(&e, "envol", false, 38);
    (void)room_eco_encaisser(&e);
    CHECK(e.serie == 3, "deux parties le même jour ont porté la série à %d", e.serie);

    /* UN JOUR SAUTÉ ROMPT LA SÉRIE, et la remet à 1 — pas à 0 : le joueur vient
     * de jouer, il est au premier jour d'une nouvelle série. */
    room_eco_set_horloge(j0 + jour * 5);
    (void)room_eco_fin_partie(&e, "envol", false, 38);
    (void)room_eco_encaisser(&e);
    CHECK(e.serie == 1, "un jour sauté a laissé la série à %d", e.serie);
    CHECK(e.serie_record == 3, "le record est tombé à %d", e.serie_record);

    /* Ce que la série RAPPORTE, et son plafond. */
    const int32_t sans = room_eco_tickets_pour("envol", false, 38, 0);
    const int32_t avec = room_eco_tickets_pour("envol", false, 38, 5);
    CHECK(avec == sans + 5, "série 5 ajoute %d tickets au lieu de 5", avec - sans);
    const int32_t plein  = room_eco_tickets_pour("envol", false, 38, ROOM_ECO_SERIE_MAX);
    const int32_t deborde = room_eco_tickets_pour("envol", false, 38, 900);
    CHECK(plein == deborde, "la série n'est pas plafonnée : %d puis %d", plein, deborde);
    CHECK(plein == sans + ROOM_ECO_SERIE_MAX, "le plafond ajoute %d", plein - sans);

    /* Une horloge reculée ne récompense pas : la série repart à 1. */
    room_eco_set_horloge(j0);
    (void)room_eco_fin_partie(&e, "envol", false, 38);
    (void)room_eco_encaisser(&e);
    CHECK(e.serie == 1, "reculer l'horloge a porté la série à %d", e.serie);
    CHECK(room_eco_valide(&e), "portefeuille invalide après manipulation d'horloge");

    room_eco_set_horloge(0);
}

/* ========================================================================== */
/* 5. QUITTE OU DOUBLE — les deux issues                                      */
/* ========================================================================== */

static void test_quitte_ou_double(void)
{
    room_eco_set_horloge(1787054400ll);

    room_eco e;
    room_eco_reset(&e);

    /* Une partie pose sa mise sur la table SANS la verser. */
    const int32_t mise = room_eco_fin_partie(&e, "shooter", false, 3105);
    CHECK(mise > 0, "une partie médiane n'a rien mis en jeu");
    CHECK(e.tickets == 0, "les tickets ont été versés d'office (%d)", e.tickets);
    CHECK(e.mise == mise, "mise %d au lieu de %d", e.mise, mise);
    CHECK(room_eco_mise_a_battre(&e) == 3105, "score à battre %d",
          room_eco_mise_a_battre(&e));

    /* LE REFUS : encaisser. Aussi simple qu'accepter, et c'est le point — un
     * refus qui demanderait un geste de plus serait une pression. */
    const int32_t verse = room_eco_encaisser(&e);
    CHECK(verse == mise, "l'encaissement a versé %d au lieu de %d", verse, mise);
    CHECK(e.tickets == mise, "solde %d après encaissement", e.tickets);
    CHECK(e.mise == 0, "la mise est restée à %d", e.mise);
    CHECK(room_eco_encaisser(&e) == 0, "on a encaissé deux fois la même mise");

    /* L'ISSUE GAGNANTE : battre son score double la mise. */
    room_eco_reset(&e);
    const int32_t m2 = room_eco_fin_partie(&e, "shooter", false, 3105);
    const int32_t gagne = room_eco_doubler(&e, 3106);
    CHECK(gagne == m2 * 2, "gagné : %d versés pour une mise de %d", gagne, m2);
    CHECK(e.tickets == m2 * 2, "solde %d après un double gagné", e.tickets);
    CHECK(e.mise == 0, "la mise gagnée est restée sur la table");

    /* L'ISSUE PERDANTE : ne pas battre son score perd tout ce qui était en jeu,
     * et RIEN d'autre. Le solde acquis avant la partie ne bouge pas. */
    room_eco_reset(&e);
    e.tickets = 250;
    const int32_t m3 = room_eco_fin_partie(&e, "shooter", false, 3105);
    CHECK(m3 > 0, "rien en jeu");
    const int32_t perdu = room_eco_doubler(&e, 3105);   /* égalité = perdu */
    CHECK(perdu == 0, "l'égalité a versé %d", perdu);
    CHECK(e.tickets == 250, "le solde acquis est tombé à %d", e.tickets);
    CHECK(e.mise == 0, "la mise perdue est restée sur la table");
    CHECK(room_eco_valide(&e), "portefeuille invalide après une perte");

    /* Doubler sans mise ne fabrique rien. */
    CHECK(room_eco_doubler(&e, 999999) == 0, "un double sans mise a versé");
    CHECK(e.tickets == 250, "le solde a bougé sans mise (%d)", e.tickets);

    /* Une partie lancée alors qu'une mise traîne ENCAISSE la précédente : un
     * joueur qui repart jouer sans répondre a refusé, il ne perd rien. */
    room_eco_reset(&e);
    const int32_t m4 = room_eco_fin_partie(&e, "envol", false, 38);
    (void)room_eco_fin_partie(&e, "envol", false, 38);
    CHECK(e.tickets == m4, "la mise abandonnée n'a pas été versée (%d)", e.tickets);

    room_eco_set_horloge(0);
}

/* ========================================================================== */
/* 6. LES LOTS                                                                */
/* ========================================================================== */

static void test_lots(void)
{
    room_eco e;
    room_eco_reset(&e);

    CHECK(ROOM_ECO_LOT_COUNT > 0, "aucun lot déclaré");
    for (int i = 0; i < ROOM_ECO_LOT_COUNT; ++i) {
        const room_eco_lot l = (room_eco_lot)i;
        CHECK(room_eco_lot_prix(l) > 0, "lot %d gratuit", i);
        CHECK(room_eco_lot_titre(l)[0] != '\0', "lot %d sans titre", i);
        CHECK(room_eco_lot_quoi(l)[0] != '\0', "lot %d sans description", i);
        CHECK(!room_eco_lot_acquis(&e, l), "lot %d acquis d'avance", i);
    }

    /* Sans tickets, rien ne s'achète — et rien n'est débité. */
    const room_eco_lot premier = (room_eco_lot)0;
    CHECK(!room_eco_acheter(&e, premier), "un lot s'est acheté à crédit");
    CHECK(e.tickets == 0, "le solde est passé à %d", e.tickets);
    CHECK(room_eco_valide(&e), "portefeuille invalide après un achat refusé");

    /* Avec juste assez, il s'achète une fois et une seule. */
    e.tickets = room_eco_lot_prix(premier);
    CHECK(room_eco_acheter(&e, premier), "l'achat au prix exact a échoué");
    CHECK(e.tickets == 0, "reste %d tickets", e.tickets);
    CHECK(room_eco_lot_acquis(&e, premier), "le lot acheté n'est pas acquis");
    e.tickets = 10000;
    CHECK(!room_eco_acheter(&e, premier), "le même lot s'est acheté deux fois");
    CHECK(e.tickets == 10000, "le second achat a débité (%d)", e.tickets);

    /* Un lot hors table ne s'achète pas et ne débite rien. */
    CHECK(!room_eco_acheter(&e, (room_eco_lot)ROOM_ECO_LOT_COUNT), "lot hors table acheté");
    CHECK(!room_eco_acheter(&e, (room_eco_lot)-1), "lot négatif acheté");
    CHECK(e.tickets == 10000, "un lot hors table a débité (%d)", e.tickets);

    /* CE QUE LA VITRINE MONTRE. Sans un sou, c'est le moins cher qui manque —
     * une vitrine qui montrerait l'inatteignable ne donne envie de rien. */
    room_eco e2;
    room_eco_reset(&e2);
    const room_eco_lot vue = room_eco_lot_en_vue(&e2);
    CHECK(vue != ROOM_ECO_LOT_COUNT, "la vitrine ne montre rien à un joueur neuf");
    for (int i = 0; i < ROOM_ECO_LOT_COUNT; ++i) {
        CHECK(room_eco_lot_prix(vue) <= room_eco_lot_prix((room_eco_lot)i),
              "la vitrine montre le lot %d, plus cher que le lot %d", vue, i);
    }

    /* Tout acquis : elle ne montre plus rien, et le dit. */
    for (int i = 0; i < ROOM_ECO_LOT_COUNT; ++i) e2.lots |= (1u << (unsigned)i);
    CHECK(room_eco_lot_en_vue(&e2) == ROOM_ECO_LOT_COUNT,
          "la vitrine montre encore quelque chose une fois tout acquis");
}

/* ========================================================================== */
/* 7. LA PERSISTANCE — aller-retour, et fichiers abîmés                       */
/* ========================================================================== */

static void ecrire_fichier(const char *chemin, const char *contenu)
{
    SDL_IOStream *io = SDL_IOFromFile(chemin, "w");
    if (!io) return;
    SDL_WriteIO(io, contenu, SDL_strlen(contenu));
    SDL_CloseIO(io);
}

static void test_persistance(void)
{
    room_eco_set_chemin(g_tmp);

    room_eco a;
    room_eco_reset(&a);
    a.jetons = 7;
    a.tickets = 431;
    a.serie = 4;
    a.serie_record = 9;
    a.dernier_jour = 20330;
    a.lots = 0b0101u & ((1u << ROOM_ECO_LOT_COUNT) - 1u);
    a.parties = 512;
    a.tickets_gagnes = 8123;
    CHECK(room_eco_sauver(&a), "sauvegarde impossible dans « %s »", g_tmp);

    room_eco b;
    room_eco_charger(&b);
    CHECK(b.jetons == a.jetons, "jetons %d au lieu de %d", b.jetons, a.jetons);
    CHECK(b.tickets == a.tickets, "tickets %d au lieu de %d", b.tickets, a.tickets);
    CHECK(b.serie == a.serie, "série %d au lieu de %d", b.serie, a.serie);
    CHECK(b.serie_record == a.serie_record, "record %d au lieu de %d",
          b.serie_record, a.serie_record);
    CHECK(b.dernier_jour == a.dernier_jour, "jour %lld au lieu de %lld",
          (long long)b.dernier_jour, (long long)a.dernier_jour);
    CHECK(b.lots == a.lots, "lots %u au lieu de %u", b.lots, a.lots);
    CHECK(b.parties == a.parties, "parties %lld au lieu de %lld",
          (long long)b.parties, (long long)a.parties);
    CHECK(b.tickets_gagnes == a.tickets_gagnes, "gagnés %lld au lieu de %lld",
          (long long)b.tickets_gagnes, (long long)a.tickets_gagnes);
    CHECK(room_eco_valide(&b), "l'aller-retour a produit un état invalide");

    /* La mise N'EST PAS persistée : une table laissée ouverte au moment où l'on
     * quitte le jeu est un refus, pas une dette. */
    a.mise = 40;
    SDL_strlcpy(a.mise_jeu, "envol", sizeof a.mise_jeu);
    (void)room_eco_sauver(&a);
    room_eco_charger(&b);
    CHECK(b.mise == 0, "la mise a survécu au redémarrage (%d)", b.mise);

    /* UN FICHIER ABSENT n'est pas une erreur : c'est le premier lancement. */
    room_eco_set_chemin("/nexistepas/portefeuille.txt");
    room_eco_charger(&b);
    CHECK(b.jetons == 0 && b.tickets == 0 && room_eco_valide(&b),
          "un fichier absent n'a pas donné un portefeuille neuf");

    /* UN FICHIER TRONQUÉ : ce qui a été lu est gardé, le reste vaut zéro. */
    room_eco_set_chemin(g_tmp);
    ecrire_fichier(g_tmp, "v1\njetons=3\ntickets=99\nseri");
    room_eco_charger(&b);
    CHECK(b.jetons == 3, "tronqué : jetons %d", b.jetons);
    CHECK(b.tickets == 99, "tronqué : tickets %d", b.tickets);
    CHECK(room_eco_valide(&b), "tronqué : état invalide");

    /* UN FICHIER CORROMPU : des lignes illisibles, des valeurs absurdes, des
     * champs inconnus. Rien de tout ça ne doit produire un état invalide — la
     * règle est qu'on répare et qu'on le dit, pas qu'on perd le portefeuille. */
    ecrire_fichier(g_tmp,
        "v1\n"
        "jetons=-40\n"                 /* négatif : borné à 0 */
        "tickets=999999999999999\n"    /* énorme : borné */
        "serie=50\nrecord=2\n"         /* incohérent : le record doit suivre */
        "lots=4294967295\n"            /* tous les bits : les inconnus tombent */
        "n'importe quoi\n"
        "inconnu=12\n"
        "parties=-9\n");
    room_eco_charger(&b);
    CHECK(b.jetons >= 0, "corrompu : jetons %d", b.jetons);
    CHECK(b.tickets >= 0, "corrompu : tickets %d", b.tickets);
    CHECK(b.parties >= 0, "corrompu : parties %lld", (long long)b.parties);
    CHECK(b.serie_record >= b.serie, "corrompu : record %d < série %d",
          b.serie_record, b.serie);
    const uint32_t masque = (1u << ROOM_ECO_LOT_COUNT) - 1u;
    CHECK((b.lots & ~masque) == 0u, "corrompu : lot inconnu gardé (%u)", b.lots);
    CHECK(room_eco_valide(&b), "corrompu : état invalide après réparation");

    /* UN FICHIER D'UNE AUTRE VERSION : on repart de zéro plutôt que de lire
     * des champs qui ne veulent plus dire la même chose. */
    ecrire_fichier(g_tmp, "v9\njetons=1000\n");
    room_eco_charger(&b);
    CHECK(b.jetons == 0, "une version inconnue a été lue (%d jetons)", b.jetons);

    /* UN FICHIER VIDE. */
    ecrire_fichier(g_tmp, "");
    room_eco_charger(&b);
    CHECK(b.jetons == 0 && room_eco_valide(&b), "un fichier vide a cassé le chargement");

    (void)SDL_RemovePath(g_tmp);
    room_eco_set_chemin(NULL);
}

/* ========================================================================== */
/* 8. LA BOUCLE ENTIÈRE, sur une session                                      */
/* ========================================================================== */

/*
 * Le contrôle qui vaut pour tous les autres : une session de vingt parties
 * jouées au niveau MÉDIAN ne doit jamais bloquer le joueur, et doit lui faire
 * gagner du terrain plutôt que d'en perdre.
 *
 * C'est la propriété que l'énoncé demandait de mesurer « sur une session
 * réelle », et c'est la seule façon honnête de justifier le plancher d'accueil
 * et le taux de change : ils sont réglés pour que ceci soit vrai.
 */
static void test_session(void)
{
    room_eco_set_horloge(1787054400ll);

    room_eco e;
    room_eco_reset(&e);
    (void)room_eco_monnayeur(&e);

    int retours_monnayeur = 0;
    for (int i = 0; i < 20; ++i) {
        if (!room_eco_inserer(&e)) {
            (void)room_eco_monnayeur(&e);
            retours_monnayeur++;
            CHECK(room_eco_inserer(&e), "bloqué après un passage au monnayeur");
        }
        const ns_game_api *api = ns_game_at(i % ns_game_count());
        (void)room_eco_fin_partie(&e, api->id, false, room_eco_mediane(api->id, false));
        (void)room_eco_encaisser(&e);
        CHECK(room_eco_valide(&e), "partie %d : portefeuille invalide", i);
    }

    /* Vingt parties médianes rendent environ vingt fois la cible : de quoi
     * s'offrir le premier lot, et loin du dernier. C'est le rythme voulu. */
    CHECK(e.tickets_gagnes >= 20 * ROOM_ECO_CIBLE_TICKETS,
          "vingt parties médianes n'ont rapporté que %lld tickets",
          (long long)e.tickets_gagnes);
    CHECK(e.parties == 20, "%lld parties comptées", (long long)e.parties);
    /* Le joueur n'est jamais resté coincé : au pire il a marché jusqu'au
     * monnayeur, ce qui est un geste de salle et non une fin de partie. */
    CHECK(retours_monnayeur == 3,
          "%d retours au monnayeur pour vingt parties, 3 attendus avec un "
          "plancher de %d", retours_monnayeur, ROOM_ECO_PLANCHER_ACCUEIL);

    /*
     * ET LE TERRAIN GAGNÉ : vingt parties médianes doivent payer le premier lot.
     *
     * C'est ce contrôle qui a démasqué le change automatique du monnayeur — il
     * échouait à 57 tickets pour un premier lot à 60. Le garder chiffré plutôt
     * que qualitatif est ce qui fait la différence entre « la boucle a l'air de
     * tourner » et « la vitrine est atteignable ».
     */
    const int32_t moins_cher = room_eco_lot_prix((room_eco_lot)0);
    CHECK(e.tickets >= moins_cher,
          "vingt parties rendent %d tickets, le premier lot en coûte %d",
          e.tickets, moins_cher);
    CHECK(room_eco_acheter(&e, (room_eco_lot)0),
          "vingt parties ne suffisent pas à acheter le premier lot");

    room_eco_set_horloge(0);
}

/* ========================================================================== */

int main(void)
{
    /* Les WARN de corruption sont ATTENDUS : ce test écrit exprès des fichiers
     * abîmés pour vérifier qu'ils se réparent. Même réglage que
     * `ns_test_scores`, pour la même raison. */
    ns_log_set_level(NS_LOG_ERROR);

    const char *base = SDL_getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    SDL_snprintf(g_tmp, sizeof g_tmp, "%s/ns_test_portefeuille.txt", base);
    (void)SDL_RemovePath(g_tmp);

    test_bareme_calibre();
    test_bareme_couvre_les_jeux();
    test_bareme_entrees_absurdes();
    test_jamais_sous_zero();
    test_tournoi();
    test_serie();
    test_quitte_ou_double();
    test_lots();
    test_persistance();
    test_session();

    printf("économie : %d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
