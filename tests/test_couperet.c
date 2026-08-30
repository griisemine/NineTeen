/*
 * test_couperet.c — LE COUPERET : la règle, et surtout SON ÉQUILIBRE.
 *
 * Ce test fait deux choses très différentes, et la seconde est la raison d'être
 * du fichier.
 *
 * 1. LA RÈGLE SE CLOUE. Le couperet tombe au bon moment et sur la bonne place,
 *    le leurre passe avant le blindage, un spectre ne joue plus mais agit
 *    encore, une coupure jette la durée, une équipe protège son porteur. Tout
 *    ça est du calcul entier et se vérifie ligne à ligne.
 *
 * 2. L'ÉQUILIBRE SE MESURE, et il ne peut pas se relire. Le mode repose sur un
 *    exposant, `ROOM_CP_K_CENT`, dont l'en-tête dit franchement qu'il est
 *    CHOISI. Ce qui ne doit pas être choisi, c'est ce qu'il produit : si le
 *    pressé gagne toujours, la moitié du mode est morte ; si l'engagé gagne
 *    toujours, l'autre moitié l'est. Le seul moyen de le savoir est de faire
 *    JOUER des manches entières, et c'est ce que fait la seconde partie.
 *
 * D'OÙ VIENNENT LES PARTIES DU TOURNOI
 * ------------------------------------
 * Des vrais jeux. Le test lance les huit AUTOPILOTES du dépôt, aux deux
 * régimes, et constitue un vivier de (durée, score) mesurés — puis tire dedans.
 * Aucune loi de probabilité n'est inventée : la dispersion du tournoi est celle
 * que les jeux produisent réellement, y compris ses queues (`room_bareme.h` la
 * chiffre jusqu'à 1 077 % d'étendue rapportée à la médiane).
 *
 * C'est aussi ce qui rend le test AUTO-ENTRETENU : régler un jeu change son
 * vivier au prochain lancement du test, donc son équilibre dans le mode, sans
 * qu'aucune table recopiée ait à être remise à jour.
 *
 * CE QUE LE TOURNOI NE DIT PAS. L'autopilote n'est pas un joueur : il établit
 * la forme de la distribution, pas le talent. Un humain change les deux
 * stratégies à la fois, dans le même sens, ce qui est la raison pour laquelle
 * un RAPPORT entre elles reste informatif là où un taux absolu ne le serait
 * pas.
 */
#include "games.h"
#include "ns_core.h"
#include "room_couperet.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failures;                                                     \
            printf("ÉCHEC %s:%d — ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* Un générateur à état explicite : le tournoi doit rendre le même verdict à
 * chaque exécution, sinon un échec ne se reproduit pas. */
static uint64_t g_rng = 0x243F6A8885A308D3ull;
static uint32_t alea(uint32_t borne)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return borne ? (uint32_t)(g_rng % borne) : 0u;
}

/* ==========================================================================
 * 1. LA TABLE ET LE BARÈME
 * ========================================================================== */

static void test_table(void)
{
    printf("\n-- la table des durées --\n");

    /* Chaque jeu porté a sa ligne, et chaque ligne a son jeu. C'est le même
     * contrôle croisé que `test_economie` fait sur le barème : un jeu porté
     * sans durée se verrait payer un coefficient nul, donc zéro point, en
     * silence. */
    for (int i = 0; i < ns_game_count(); ++i) {
        const ns_game_api *api = ns_game_at(i);
        CHECK(room_cp_duree_mediane(api->id, false) > 0.0f,
              "« %s » n'a pas de durée médiane en régime normal", api->id);
        CHECK(room_cp_duree_mediane(api->id, true) > 0.0f,
              "« %s » n'a pas de durée médiane en régime difficile", api->id);
    }
    CHECK(room_cp_duree_mediane("nexistepas", false) == 0.0f,
          "un jeu inconnu ne doit pas avoir de durée");

    /* Le coefficient vaut UN à la durée de référence : c'est ce qui donne son
     * sens à la formule, et c'est la seule valeur de la courbe qu'on puisse
     * poser sans mesure. */
    const float tref = (float)ROOM_CP_T_REF_DS * 0.1f;
    CHECK(fabsf(room_cp_engagement(tref) - 1.0f) < 1e-4f,
          "l'engagement à T_REF vaut %.4f au lieu de 1", (double)room_cp_engagement(tref));

    CHECK(room_cp_engagement(0.0f) == 0.0f, "une partie de durée nulle ne rapporte rien");
    CHECK(room_cp_engagement(-5.0f) == 0.0f, "une durée négative ne rapporte rien");

    /* Le plafond est posé là où la mesure s'arrête. Au-delà, plus rien ne
     * monte : c'est ce qui empêche une partie de dix minutes de valoir une
     * manche entière sur la foi d'une extrapolation. */
    const float pmax = (float)ROOM_CP_DUREE_MAX_DS * 0.1f;
    CHECK(fabsf(room_cp_engagement(pmax) - room_cp_engagement(pmax * 3.0f)) < 1e-5f,
          "l'engagement continue de monter au-delà du plafond de mesure");

    /* Monotone : jouer plus longtemps ne doit jamais rapporter moins. */
    float prec = -1.0f;
    for (float s = 0.0f; s <= pmax; s += 1.0f) {
        const float e = room_cp_engagement(s);
        CHECK(e >= prec, "l'engagement redescend à %.0f s (%.4f < %.4f)",
              (double)s, (double)e, (double)prec);
        prec = e;
    }

    /*
     * L'EXPOSANT DOIT ÊTRE > 1, et ce contrôle est le garde-fou de tout le
     * mode : à K = 1 le paiement est plat à la seconde et personne n'a de
     * raison de s'engager. On le vérifie par ce qu'il produit plutôt que par sa
     * valeur — une longue partie doit payer MIEUX À LA SECONDE qu'une courte.
     */
    const float court = 10.0f, longue = 160.0f;
    const float pps_court  = room_cp_engagement(court) / court;
    const float pps_longue = room_cp_engagement(longue) / longue;
    CHECK(pps_longue > pps_court * 2.0f,
          "l'engagement ne paie pas la durée : %.4f contre %.4f par seconde",
          (double)pps_longue, (double)pps_court);
    printf("   engagement : %.0f s -> %.3f | %.0f s -> %.3f "
           "(%.1fx par seconde)\n",
           (double)court, (double)room_cp_engagement(court),
           (double)longue, (double)room_cp_engagement(longue),
           (double)(pps_longue / pps_court));

    /* Un score négatif — celui qui vient d'un pair ou d'un fichier abîmé — ne
     * doit pas rendre un solde négatif. */
    CHECK(room_cp_points_pour("snake", false, -1000) == 0,
          "un score négatif rapporte des points");
    CHECK(room_cp_points_pour("nexistepas", false, 1000) == 0,
          "un jeu inconnu rapporte des points");
}

/* ==========================================================================
 * 2. LE SALON
 * ========================================================================== */

static void test_salon(void)
{
    printf("\n-- le salon --\n");
    room_couperet c;
    room_cp_ouvrir(&c, 4, false);

    CHECK(c.phase == ROOM_CP_SALON, "un salon neuf n'est pas en phase salon");
    CHECK(!room_cp_salon_plein(&c), "un salon vide se dit plein");
    room_cp_lancer(&c, 1);
    CHECK(c.phase == ROOM_CP_SALON, "un salon incomplet a démarré");

    CHECK(room_cp_asseoir(&c, 0, "un", 0), "la place 0 refuse un joueur");
    CHECK(!room_cp_asseoir(&c, 0, "bis", 0), "la place 0 accepte deux joueurs");
    CHECK(!room_cp_asseoir(&c, 4, "hors", 0), "une place hors bornes est acceptée");
    CHECK(c.place[0].jetons == ROOM_CP_JETONS_DEPART,
          "on ne démarre pas avec %d jetons", ROOM_CP_JETONS_DEPART);

    /* Hors mode équipes, le camp EST la place : c'est l'invariant qui permet
     * aux deux modes de partager toute la suite du code. */
    CHECK(c.place[0].camp == 0, "le camp ne suit pas la place en individuel");

    for (int i = 1; i < 4; ++i) CHECK(room_cp_asseoir(&c, (uint8_t)i, "x", 3), "place %d", i);
    CHECK(c.place[2].camp == 2, "le camp demandé a été retenu en individuel");
    CHECK(room_cp_salon_plein(&c), "quatre places prises et le salon n'est pas plein");

    room_cp_lancer(&c, 42);
    CHECK(c.phase == ROOM_CP_COURSE, "le salon plein n'a pas démarré");
    CHECK(c.graine == 42, "la graine n'a pas été retenue");
    CHECK(!room_cp_asseoir(&c, 0, "tard", 0), "on s'assoit après le départ");
    CHECK(room_cp_valide(&c), "l'état d'un salon lancé est invalide");

    /* Quitter en course laisse les points au camp. */
    (void)room_cp_partie_fin(&c, 1, 0);
    room_cp_partie_debut(&c, 1, "snake", false);
    room_cp_avancer(&c, 30.0f);
    const int32_t gagne = room_cp_partie_fin(&c, 1, room_eco_mediane("snake", false));
    CHECK(gagne > 0, "trente secondes de snake ne rapportent rien (%d)", gagne);
    room_cp_lever(&c, 1);
    CHECK(c.place[1].occupee, "quitter en course a libéré la place");
    CHECK(!c.place[1].vivante, "quitter en course n'a pas fait un spectre");
    CHECK(room_cp_points_camp(&c, 1) == gagne,
          "les points d'un partant ont disparu (%d au lieu de %d)",
          room_cp_points_camp(&c, 1), gagne);
}

/* ==========================================================================
 * 3. LE COUPERET
 * ========================================================================== */

static void asseoir_tous(room_couperet *c, uint8_t n, bool equipes)
{
    room_cp_ouvrir(c, n, equipes);
    for (uint8_t i = 0; i < n; ++i) {
        char nom[8];
        snprintf(nom, sizeof nom, "j%u", (unsigned)i);
        (void)room_cp_asseoir(c, i, nom, equipes ? (uint8_t)(i % 2u) : i);
    }
    room_cp_lancer(c, 7);
}

static void test_couperet(void)
{
    printf("\n-- le couperet --\n");
    const float periode = (float)ROOM_CP_PERIODE_DS * 0.1f;

    room_couperet c;
    asseoir_tous(&c, 4, false);

    /* Personne ne sort avant la période. */
    room_cp_avancer(&c, periode - 0.5f);
    CHECK(c.couperets == 0, "un couperet est tombé avant l'heure");
    CHECK(c.prochain > 0.0f && c.prochain <= periode,
          "le compte à rebours vaut %.2f", (double)c.prochain);

    /* Un seul joueur marque : c'est un autre qui sort. */
    room_cp_partie_debut(&c, 2, "demineur", false);
    room_cp_avance(&c, 2, room_eco_mediane("demineur", false));
    const uint8_t menace = room_cp_menace(&c);
    CHECK(menace != 2, "le seul joueur qui avance est menacé");

    room_cp_avancer(&c, 1.0f);
    CHECK(c.couperets == 1, "le couperet n'est pas tombé (%d)", c.couperets);
    CHECK(c.place[menace].vivante == false, "la place menacée n'est pas sortie");
    CHECK(c.place[menace].sortie_a == 1, "le numéro de couperet n'est pas noté");
    CHECK(room_cp_valide(&c), "l'état après un couperet est invalide");

    /* Le jeton du couperet : tous sauf la victime. */
    for (int i = 0; i < 4; ++i) {
        const int32_t attendu = ROOM_CP_JETONS_DEPART + ((i == (int)menace) ? 0 : 1);
        CHECK(c.place[i].jetons == attendu,
              "place %d : %d jetons au lieu de %d", i, c.place[i].jetons, attendu);
    }

    /*
     * LE PAS DE SIMULATION NE DOIT PAS CHANGER LE VERDICT.
     *
     * C'est la propriété qui décide si l'arbitre et les autres voient le même
     * couperet : deux machines qui avancent du même total dans un nombre
     * d'appels différent doivent sortir la même place au même moment.
     */
    const float pas[] = { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f, 0.5f };
    int32_t coups[4];
    uint8_t dernier[4];
    for (int k = 0; k < 4; ++k) {
        room_couperet d;
        asseoir_tous(&d, 8, false);
        const float total = 3.0f * periode + 1.0f;
        float t = 0.0f;
        while (t < total) { room_cp_avancer(&d, pas[k]); t += pas[k]; }
        coups[k] = d.couperets;
        dernier[k] = 0;
        for (int i = 0; i < 8; ++i) if (!d.place[i].vivante) dernier[i < 8 ? 0 : 0] = (uint8_t)i;
        for (int i = 0; i < 8; ++i) if (d.place[i].sortie_a == d.couperets) dernier[k] = (uint8_t)i;
    }
    for (int k = 1; k < 4; ++k) {
        CHECK(coups[k] == coups[0],
              "pas %.4f s : %d couperets au lieu de %d",
              (double)pas[k], coups[k], coups[0]);
        CHECK(dernier[k] == dernier[0],
              "pas %.4f s : la place %u est sortie au lieu de %u",
              (double)pas[k], dernier[k], dernier[0]);
    }
    printf("   quatre pas de simulation, %d couperets et la même dernière place\n",
           coups[0]);

    /* La manche se termine quand il ne reste qu'un camp. */
    room_couperet e;
    asseoir_tous(&e, 4, false);
    for (int i = 0; i < 20; ++i) room_cp_avancer(&e, periode);
    CHECK(e.phase == ROOM_CP_FINI, "la manche ne s'est pas terminée");
    CHECK(e.vainqueur < ROOM_CP_MAX_PLACES, "aucun vainqueur désigné");
    CHECK(room_cp_vivants_camp(&e, e.vainqueur) >= 1,
          "le vainqueur n'a personne debout");
    int vivants = 0;
    for (int i = 0; i < 4; ++i) if (e.place[i].vivante) vivants++;
    CHECK(vivants == 1, "%d places debout à la fin au lieu d'une", vivants);
}

/* ==========================================================================
 * 4. LES ÉQUIPES
 * ========================================================================== */

static void test_equipes(void)
{
    printf("\n-- les équipes --\n");
    room_couperet c;
    asseoir_tous(&c, 4, true);   /* places 0 et 2 en camp 0, 1 et 3 en camp 1 */

    CHECK(c.place[0].camp == 0 && c.place[2].camp == 0, "les camps ne sont pas ceux demandés");
    CHECK(c.place[1].camp == 1 && c.place[3].camp == 1, "les camps ne sont pas ceux demandés");

    /*
     * LE CŒUR DU MODE ÉQUIPE : un porteur qui marque protège son coéquipier qui
     * ne marque pas.
     *
     * Sans cette règle, le rôle de soutien serait suicidaire — celui qui dépense
     * ses jetons en blindages au lieu de jouer serait toujours le dernier au
     * classement individuel, donc toujours le premier sorti, et l'alliance que
     * le mode promet ne pourrait pas exister. Le couperet classe donc les CAMPS,
     * et ne descend au joueur qu'à l'intérieur du camp condamné.
     */
    room_cp_partie_debut(&c, 0, "snake", false);
    room_cp_avancer(&c, 40.0f);
    (void)room_cp_partie_fin(&c, 0, room_eco_mediane("snake", false) * 3);

    CHECK(room_cp_points_camp(&c, 0) > 0, "le camp 0 n'a pas encaissé");
    CHECK(room_cp_points_camp(&c, 1) == 0, "le camp 1 a des points sans avoir joué");

    const uint8_t vise = room_cp_menace(&c);
    CHECK(vise == 1 || vise == 3,
          "le couperet vise la place %u : il devrait viser le camp sans points", vise);

    /* La place 2 n'a rien marqué et n'est pourtant PAS menacée : son camp la
     * couvre. C'est exactement ce qui rend le soutien jouable. */
    CHECK(c.place[2].points == 0, "la place 2 a marqué toute seule");
    CHECK(vise != 2, "le couperet vise le soutien d'un camp qui mène");
    printf("   la place 2 n'a rien marqué et n'est pas menacée : son camp la couvre\n");

    room_cp_avancer(&c, (float)ROOM_CP_PERIODE_DS * 0.1f);
    CHECK(!c.place[vise].vivante, "la place menacée n'est pas sortie");
    CHECK(room_cp_vivants_camp(&c, 0) == 2, "le camp qui mène a perdu quelqu'un");
}

/* ==========================================================================
 * 5. LES ACTIONS
 * ========================================================================== */

static void test_actions(void)
{
    printf("\n-- les six actions --\n");
    room_couperet c;
    asseoir_tous(&c, 4, false);

    /* Les refus, et chacun ferme une façon de gaspiller des jetons. */
    CHECK(!room_cp_agir(&c, 0, 0, ROOM_CP_BROUILLAGE), "on se brouille soi-même");
    CHECK(!room_cp_agir(&c, 0, 9, ROOM_CP_BROUILLAGE), "on vise une place inexistante");
    CHECK(c.place[0].jetons == ROOM_CP_JETONS_DEPART, "un refus a débité des jetons");

    room_cp_partie_debut(&c, 1, "aplomb", false);
    CHECK(!room_cp_agir(&c, 0, 1, ROOM_CP_COUPURE),
          "la coupure passe à %d jetons alors qu'elle en coûte %d",
          ROOM_CP_JETONS_DEPART, room_cp_action_cout(ROOM_CP_COUPURE));
    CHECK(c.place[0].jetons == ROOM_CP_JETONS_DEPART, "une action trop chère a débité");

    /* Le brouillage passe et se PROLONGE. */
    room_cp_partie_debut(&c, 1, "snake", false);
    CHECK(room_cp_agir(&c, 0, 1, ROOM_CP_BROUILLAGE), "le brouillage ne passe pas");
    CHECK(c.place[0].jetons == ROOM_CP_JETONS_DEPART - 1, "le brouillage n'a pas été payé");
    const float un = c.place[1].brouillage;
    CHECK(un > 0.0f, "le brouillage n'a aucun effet");
    CHECK(room_cp_agir(&c, 2, 1, ROOM_CP_BROUILLAGE), "le second brouillage ne passe pas");
    CHECK(c.place[1].brouillage > un * 1.5f,
          "deux brouillages ne prolongent pas (%.1f puis %.1f s)",
          (double)un, (double)c.place[1].brouillage);

    room_cp_avancer(&c, 100.0f);
    CHECK(c.place[1].brouillage == 0.0f, "le brouillage ne s'épuise pas");

    /* LE BLINDAGE ABSORBE, une fois. */
    room_couperet b;
    asseoir_tous(&b, 4, false);
    room_cp_partie_debut(&b, 1, "snake", false);
    b.place[1].jetons = 10;
    CHECK(room_cp_agir(&b, 1, 1, ROOM_CP_BLINDAGE), "on ne peut pas se blinder");
    CHECK(room_cp_agir(&b, 0, 1, ROOM_CP_BROUILLAGE), "le brouillage ne part pas");
    CHECK(b.place[1].brouillage == 0.0f, "le blindage n'a pas absorbé");
    CHECK(!b.place[1].blindage, "le blindage n'a pas été consommé");
    CHECK(room_cp_agir(&b, 2, 1, ROOM_CP_BROUILLAGE), "le second brouillage ne part pas");
    CHECK(b.place[1].brouillage > 0.0f, "le blindage a absorbé deux fois");
    /* On ne tient qu'une plaque : la seconde est refusée sans rien débiter. */
    CHECK(room_cp_agir(&b, 1, 1, ROOM_CP_BLINDAGE), "on ne peut plus se blinder");
    const int32_t avant_bis = b.place[1].jetons;
    CHECK(!room_cp_agir(&b, 1, 1, ROOM_CP_BLINDAGE), "on empile deux blindages");
    CHECK(b.place[1].jetons == avant_bis, "un blindage refusé a débité");

    /* LE LEURRE RENVOIE. */
    room_couperet l;
    asseoir_tous(&l, 4, false);
    room_cp_partie_debut(&l, 0, "snake", false);
    room_cp_partie_debut(&l, 1, "snake", false);
    CHECK(room_cp_agir(&l, 1, 1, ROOM_CP_LEURRE), "on ne peut pas poser de leurre");
    CHECK(room_cp_agir(&l, 0, 1, ROOM_CP_BROUILLAGE), "le brouillage ne part pas");
    CHECK(l.place[1].brouillage == 0.0f, "le leurre n'a pas renvoyé");
    CHECK(l.place[0].brouillage > 0.0f, "l'auteur n'a pas pris son propre brouillage");

    /* Le leurre passe AVANT le blindage : c'est la règle la plus chère qui
     * gagne, sans quoi personne ne l'achète. */
    room_couperet lb;
    asseoir_tous(&lb, 4, false);
    room_cp_partie_debut(&lb, 0, "snake", false);
    room_cp_partie_debut(&lb, 1, "snake", false);
    lb.place[1].jetons = 20;
    CHECK(room_cp_agir(&lb, 1, 1, ROOM_CP_LEURRE), "leurre");
    CHECK(room_cp_agir(&lb, 1, 1, ROOM_CP_BLINDAGE), "blindage");
    CHECK(room_cp_agir(&lb, 0, 1, ROOM_CP_BROUILLAGE), "brouillage");
    CHECK(lb.place[0].brouillage > 0.0f, "le blindage a gagné sur le leurre");
    CHECK(lb.place[1].blindage, "le blindage a été consommé pour rien");

    /* Deux leurres face à face ne bouclent pas : c'est l'auteur qui prend. */
    room_couperet ll;
    asseoir_tous(&ll, 4, false);
    room_cp_partie_debut(&ll, 0, "snake", false);
    room_cp_partie_debut(&ll, 1, "snake", false);
    ll.place[0].jetons = 10;
    CHECK(room_cp_agir(&ll, 0, 0, ROOM_CP_LEURRE), "leurre de l'auteur");
    CHECK(room_cp_agir(&ll, 1, 1, ROOM_CP_LEURRE), "leurre de la cible");
    CHECK(room_cp_agir(&ll, 0, 1, ROOM_CP_BROUILLAGE), "brouillage");
    CHECK(ll.place[0].brouillage > 0.0f, "le renvoi n'a pas atteint l'auteur");
    CHECK(ll.place[1].brouillage == 0.0f, "la cible a pris malgré son leurre");

    /* LA COUPURE JETTE LA DURÉE, et c'est tout son intérêt. */
    room_couperet k;
    asseoir_tous(&k, 4, false);
    k.place[0].jetons = 10;
    room_cp_partie_debut(&k, 1, "aplomb", false);
    /* Sous la période : un couperet qui tomberait ici sortirait la place 1 —
     * seule à ne rien avoir encaissé — et on mesurerait le couperet au lieu de
     * la coupure. */
    room_cp_avancer(&k, 40.0f);
    CHECK(k.place[1].depuis > 39.0f, "la partie n'a pas duré (%.1f s)",
          (double)k.place[1].depuis);
    CHECK(room_cp_agir(&k, 0, 1, ROOM_CP_COUPURE), "la coupure ne passe pas");
    CHECK(k.place[1].jeu[0] == '\0', "la borne coupée joue encore");
    CHECK(k.place[1].annulees == 1, "l'annulation n'est pas comptée");
    CHECK(k.place[1].provisoire == 0, "une borne coupée défend encore du couperet");
    CHECK(room_cp_partie_fin(&k, 1, room_eco_mediane("aplomb", false)) == 0,
          "une partie coupée rapporte encore");
    CHECK(k.place[1].points == 0, "une partie coupée a crédité des points");

    /* LE RELAIS donne un jeton, y compris à un spectre — armer son fantôme est
     * une tactique, pas un bogue. */
    room_couperet r;
    asseoir_tous(&r, 4, false);
    const int32_t avant = r.place[1].jetons;
    CHECK(room_cp_agir(&r, 0, 1, ROOM_CP_RELAIS), "le relais ne passe pas");
    CHECK(r.place[1].jetons == avant + 1, "le relais n'a rien donné");
    CHECK(r.place[0].jetons == ROOM_CP_JETONS_DEPART - room_cp_action_cout(ROOM_CP_RELAIS),
          "le relais n'a pas été payé");

    /* On ne frappe ni un spectre, ni une borne éteinte : ce serait jeter ses
     * jetons, et c'est ce qui rendait le blindage inutile face à plusieurs
     * attaquants. */
    room_couperet s;
    asseoir_tous(&s, 4, false);
    s.place[2].vivante = false;
    CHECK(!room_cp_agir(&s, 0, 2, ROOM_CP_BROUILLAGE), "on brouille un spectre");
    CHECK(!room_cp_agir(&s, 0, 1, ROOM_CP_BROUILLAGE),
          "on brouille quelqu'un qui ne joue pas");
    room_cp_partie_debut(&s, 1, "snake", false);
    CHECK(room_cp_agir(&s, 0, 1, ROOM_CP_BROUILLAGE), "on ne brouille pas un joueur");
    /* Mais un spectre AGIT toujours : c'est ce qui fait qu'être sorti change de
     * métier au lieu de mettre à la porte. */
    room_cp_partie_debut(&s, 0, "snake", false);
    CHECK(room_cp_agir(&s, 2, 0, ROOM_CP_BROUILLAGE), "un spectre ne peut plus agir");
    CHECK(s.place[0].brouillage > 0.0f, "le brouillage d'un spectre n'a rien fait");

    /* En équipe, on ne frappe pas les siens. */
    room_couperet q;
    asseoir_tous(&q, 4, true);
    for (uint8_t i = 0; i < 4; ++i) room_cp_partie_debut(&q, i, "snake", false);
    CHECK(!room_cp_agir(&q, 0, 2, ROOM_CP_BROUILLAGE), "on frappe son propre camp");
    CHECK(room_cp_agir(&q, 0, 1, ROOM_CP_BROUILLAGE), "on ne frappe pas l'autre camp");
    CHECK(room_cp_agir(&q, 0, 2, ROOM_CP_BLINDAGE), "on ne protège pas son camp");

    CHECK(room_cp_valide(&c) && room_cp_valide(&b) && room_cp_valide(&l) &&
          room_cp_valide(&k) && room_cp_valide(&q),
          "un état est invalide après les actions");
}

/* ==========================================================================
 * 6. LE JOURNAL
 * ========================================================================== */

static void test_journal(void)
{
    printf("\n-- le journal --\n");
    room_couperet c;
    asseoir_tous(&c, 2, false);

    room_cp_evenement e;
    int arrivees = 0, debuts = 0;
    while (room_cp_prendre(&c, &e)) {
        if (e.type == ROOM_CP_EVT_ARRIVEE) arrivees++;
        if (e.type == ROOM_CP_EVT_DEBUT) debuts++;
    }
    CHECK(arrivees == 2, "%d arrivées journalisées au lieu de 2", arrivees);
    CHECK(debuts == 1, "%d départs de manche au lieu d'un", debuts);
    CHECK(!room_cp_prendre(&c, &e), "le journal rend un événement de plus");

    room_cp_partie_debut(&c, 0, "demineur", false);
    room_cp_avancer(&c, 20.0f);
    const int32_t pts = room_cp_partie_fin(&c, 0, room_eco_mediane("demineur", false));
    CHECK(room_cp_prendre(&c, &e), "la fin de partie n'est pas journalisée");
    CHECK(e.type == ROOM_CP_EVT_PARTIE && e.a == 0 && e.valeur == pts,
          "l'événement de fin de partie est faux");

    /* Un journal plein jette le plus ancien et ne déborde pas. */
    for (int i = 0; i < ROOM_CP_JOURNAL * 3; ++i) {
        (void)room_cp_agir(&c, 0, 1, ROOM_CP_RELAIS);
        c.place[0].jetons = 10;
    }
    int n = 0;
    while (room_cp_prendre(&c, &e)) n++;
    CHECK(n <= ROOM_CP_JOURNAL, "le journal a rendu %d événements pour %d places",
          n, ROOM_CP_JOURNAL);
    CHECK(n >= ROOM_CP_JOURNAL - 1, "le journal n'a gardé que %d événements", n);
    CHECK(room_cp_valide(&c), "l'état est invalide après le journal");
}

/* ==========================================================================
 * 7. LE TOURNOI — l'équilibre, mesuré
 * ========================================================================== */

#define VIVIER 32      /* parties d'autopilote par ligne */

typedef struct ligne {
    const char *jeu;
    bool        hard;
    float       duree[VIVIER];
    int64_t     score[VIVIER];
    float       med_duree;
    int32_t     med_points;   /* points d'une partie médiane, mode compétitif */
    double      rendement;    /* points par seconde, moyennés sur le vivier */
} ligne;

static ligne g_lignes[16];
static int   g_nlignes = 0;

static int cmp_float(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/*
 * Constitue le vivier en JOUANT. Même protocole que la mesure qui a produit
 * `ROOM_CP_DUREES` : pas fixe à 120 Hz, graines espacées par le nombre d'or
 * 64 bits, plafond à 180 s.
 */
static void remplir_vivier(void)
{
    const float pas = 1.0f / 120.0f;
    const int   plafond = 180 * 120;

    for (int i = 0; i < ns_game_count() && g_nlignes < 16; ++i) {
        const ns_game_api *api = ns_game_at(i);
        for (int h = 0; h < 2; ++h) {
            ligne *l = &g_lignes[g_nlignes++];
            l->jeu = api->id;
            l->hard = (h != 0);
            void *st = calloc(1, api->state_size);
            if (!st) { g_nlignes--; return; }
            for (int r = 0; r < VIVIER; ++r) {
                api->reset(st, (uint64_t)r * 0x9E3779B97F4A7C15ull + 1u, l->hard);
                api->set_best(st, 0);
                int t = 0;
                for (; t < plafond; ++t) {
                    if (api->autopilot) (void)api->autopilot(st);
                    api->tick(st, pas);
                    ns_game_events ev;
                    memset(&ev, 0, sizeof ev);
                    api->events(st, &ev);
                    float mort = 0.0f;
                    if (api->dead(st, &mort)) break;
                }
                l->duree[r] = (float)t / 120.0f;
                l->score[r] = (int64_t)api->score(st);
            }
            free(st);

            float tri[VIVIER];
            memcpy(tri, l->duree, sizeof tri);
            qsort(tri, VIVIER, sizeof tri[0], cmp_float);
            l->med_duree = 0.5f * (tri[VIVIER / 2 - 1] + tri[VIVIER / 2]);
        }
    }
}

typedef enum strategie { PRESSE = 0, ENGAGE } strategie;

/*
 * CE QUE CHAQUE STRATÉGIE JOUE, et il a fallu s'y reprendre.
 *
 * La première version faisait prendre au pressé la ligne la plus COURTE. Le
 * tournoi a montré que c'était une caricature et non une stratégie : la plus
 * courte est le démineur difficile (5,9 s), qui rapporte le plancher, et aucun
 * joueur ne choisirait volontairement la borne qui paie le moins.
 *
 * Les deux stratégies maximisent donc la même chose — les POINTS PAR SECONDE —
 * et ne diffèrent que par une contrainte, qui est exactement celle que le mode
 * met en jeu :
 *
 *   PRESSÉ : seulement des bornes plus COURTES QUE LA PÉRIODE DU COUPERET. Il
 *            encaisse donc au moins une fois entre deux lames, et n'est jamais
 *            pris les mains vides.
 *   ENGAGÉ : toutes les bornes. Il prend le meilleur rendement du tableau et
 *            paie en exposition.
 *
 * C'est la comparaison honnête : deux joueurs qui cherchent la même chose, dont
 * l'un refuse le risque. Si l'exposant est juste, aucun des deux ne doit avoir
 * systématiquement raison.
 */
/*
 * LE RENDEMENT D'UNE BORNE : points par seconde, EN MOYENNE SUR LE VIVIER.
 *
 * Et pas « points d'une partie médiane divisé par la durée médiane », qui est
 * ce que ce fichier calculait d'abord. Le tournoi a montré l'erreur : le pressé
 * jouait envol difficile, annoncé à 0,148 point par seconde sur les médianes,
 * et en réalisait 0,30 — le DOUBLE.
 *
 * La raison est l'inégalité de Jensen, et elle est structurelle ici : le
 * paiement est CONVEXE en durée (c'est la définition même du coefficient
 * d'engagement, exposant > 1), donc la moyenne des paiements dépasse le
 * paiement de la durée moyenne. Envol difficile va de 6,1 à 63,9 s dans le
 * vivier — un facteur dix — et cette dispersion se transforme en prime.
 *
 * Choisir sa borne sur les médianes revient donc à ignorer précisément ce que
 * l'exposant fabrique. Le rendement se mesure sur les VINGT-QUATRE parties
 * réellement jouées : somme des points, divisée par somme des durées.
 */
static void tarifer(void)
{
    for (int i = 0; i < g_nlignes; ++i) {
        ligne *l = &g_lignes[i];
        l->med_points = room_cp_points_pour(l->jeu, l->hard,
                                            room_eco_mediane(l->jeu, l->hard));
        double pts = 0.0, sec = 0.0;
        for (int r = 0; r < VIVIER; ++r) {
            pts += (double)room_cp_points_pour(l->jeu, l->hard, l->score[r]);
            sec += (double)l->duree[r];
        }
        l->rendement = (sec > 0.0) ? pts / sec : 0.0;
    }
}

static int ligne_de(strategie s)
{
    const float periode = room_cp_periode();
    int choix = -1;
    for (int i = 0; i < g_nlignes; ++i) {
        if (s == PRESSE && g_lignes[i].med_duree >= periode) continue;
        if (choix < 0 || g_lignes[i].rendement > g_lignes[choix].rendement) choix = i;
    }
    return (choix < 0) ? 0 : choix;
}

/*
 * Une manche complète. `avec_sabotage` fait dépenser les jetons : le pressé
 * coupe l'engagé dès qu'il en a de quoi, l'engagé se blinde. C'est le
 * comportement le plus simple qui exerce vraiment l'économie des jetons — et
 * c'est le pire cas pour l'engagé, donc celui qui vaut d'être mesuré.
 *
 * Rend la stratégie du camp vainqueur.
 */
typedef struct bilan {
    long points[2];      /* points encaissés, cumulés par stratégie */
    long sortie[2];      /* rang de sortie cumulé : 1 = premier sorti */
    long parties[2];
    long annulees[2];
    long manches;
} bilan;

static strategie une_manche(bool avec_sabotage, int *duree_s, bilan *b)
{
    room_couperet c;
    room_cp_ouvrir(&c, 8, false);
    strategie strat[8];
    int       ligne_i[8];
    float     reste[8];
    int       tirage[8];

    for (uint8_t i = 0; i < 8; ++i) {
        strat[i] = (i % 2u == 0u) ? PRESSE : ENGAGE;
        (void)room_cp_asseoir(&c, i, (strat[i] == PRESSE) ? "presse" : "engage", i);
        ligne_i[i] = ligne_de(strat[i]);
        reste[i] = 0.0f;
        tirage[i] = 0;
    }
    room_cp_lancer(&c, 1);

    const float pas = 0.1f;
    int gardefou = (int)(ROOM_CP_MANCHE_MAX_S / pas) + 10;
    while (c.phase == ROOM_CP_COURSE && gardefou-- > 0) {
        for (uint8_t i = 0; i < 8; ++i) {
            room_cp_place *p = &c.place[i];
            if (!p->vivante) continue;

            if (p->jeu[0] == '\0') {
                const ligne *l = &g_lignes[ligne_i[i]];
                tirage[i] = (int)alea(VIVIER);
                reste[i] = l->duree[tirage[i]];
                room_cp_partie_debut(&c, i, l->jeu, l->hard);
                continue;
            }

            const ligne *l = &g_lignes[ligne_i[i]];
            const float total = l->duree[tirage[i]];
            reste[i] -= pas;
            /* L'avance progresse comme le score : linéairement, faute de mieux.
             * Elle ne sert qu'au départage, jamais aux points. */
            if (total > 0.0f) {
                const float part = 1.0f - (reste[i] / total);
                room_cp_avance(&c, i, (int64_t)((float)l->score[tirage[i]] * part));
            }
            if (reste[i] <= 0.0f) (void)room_cp_partie_fin(&c, i, l->score[tirage[i]]);
        }

        /*
         * LA MÊME POLITIQUE DE SABOTAGE POUR LES DEUX, et c'est délibéré.
         *
         * Donner au pressé le rôle d'attaquant et à l'engagé celui de garde du
         * corps aurait été écrire la conclusion dans l'énoncé : le résultat du
         * tournoi n'aurait plus mesuré les règles, mais mon idée de qui doit
         * faire quoi. Les deux jouent donc la même chose, qui est aussi la plus
         * évidente : se couvrir quand on a quelque chose à perdre, frapper le
         * premier quand on peut payer.
         *
         * La cible est le MEILLEUR adversaire vivant qui joue — c'est-à-dire
         * celui qu'on a intérêt à faire tomber. Viser la plus longue partie en
         * cours reviendrait à désigner l'engagé par construction.
         */
        if (avec_sabotage) {
            for (uint8_t i = 0; i < 8; ++i) {
                room_cp_place *p = &c.place[i];
                if (!p->occupee || !p->vivante) continue;

                if (p->jeu[0] != '\0' && !p->blindage &&
                    p->jetons >= room_cp_action_cout(ROOM_CP_BLINDAGE)) {
                    (void)room_cp_agir(&c, i, i, ROOM_CP_BLINDAGE);
                    continue;
                }
                if (p->jetons < room_cp_action_cout(ROOM_CP_COUPURE)) continue;
                uint8_t cible = ROOM_CP_MAX_PLACES;
                int32_t meilleur = -1;
                for (uint8_t k = 0; k < 8; ++k) {
                    if (k == i || !c.place[k].vivante || c.place[k].jeu[0] == '\0') continue;
                    const int32_t v = room_cp_valeur(&c, k);
                    if (v > meilleur) { meilleur = v; cible = k; }
                }
                if (cible < ROOM_CP_MAX_PLACES) {
                    (void)room_cp_agir(&c, i, cible, ROOM_CP_COUPURE);
                }
            }
        }

        room_cp_avancer(&c, pas);
    }

    if (duree_s) *duree_s = (int)c.horloge;
    if (b) {
        for (uint8_t i = 0; i < 8; ++i) {
            const int k = (int)strat[i];
            b->points[k]   += c.place[i].points;
            b->parties[k]  += c.place[i].parties;
            b->annulees[k] += c.place[i].annulees;
            /* Le vivant à la fin n'a pas de numéro de sortie : on lui donne le
             * suivant, sinon les deux moyennes ne se comparent pas. */
            b->sortie[k] += (c.place[i].sortie_a > 0) ? c.place[i].sortie_a
                                                      : (c.couperets + 1);
        }
        b->manches++;
    }
    if (c.vainqueur >= ROOM_CP_MAX_PLACES) return PRESSE;
    return strat[c.vainqueur];
}

static void test_tournoi(void)
{
    printf("\n-- le tournoi : l'équilibre des deux stratégies --\n");

    remplir_vivier();
    tarifer();
    CHECK(g_nlignes == 2 * ns_game_count(),
          "%d lignes de vivier pour %d jeux", g_nlignes, ns_game_count());

    printf("   %-9s %-7s  durée méd.  points méd.   pt/s médian   pt/s réel\n",
           "jeu", "régime");
    for (int i = 0; i < g_nlignes; ++i) {
        const ligne *l = &g_lignes[i];
        printf("   %-9s %-7s %9.1f s %10d %13.3f %11.3f\n",
               l->jeu, l->hard ? "hard" : "normal", (double)l->med_duree,
               l->med_points,
               (l->med_duree > 0.0f) ? (double)l->med_points / (double)l->med_duree : 0.0,
               l->rendement);
    }
    const int ip = ligne_de(PRESSE), ie = ligne_de(ENGAGE);
    printf("   le pressé joue %s %s (%.1f s, %d pts) ; "
           "l'engagé joue %s %s (%.1f s, %d pts)\n",
           g_lignes[ip].jeu, g_lignes[ip].hard ? "hard" : "normal",
           (double)g_lignes[ip].med_duree, g_lignes[ip].med_points,
           g_lignes[ie].jeu, g_lignes[ie].hard ? "hard" : "normal",
           (double)g_lignes[ie].med_duree, g_lignes[ie].med_points);

    CHECK(g_lignes[ip].med_duree < g_lignes[ie].med_duree,
          "les deux stratégies jouent la même longueur de partie");
    CHECK(g_lignes[ip].med_duree < room_cp_periode(),
          "le pressé joue plus long qu'un couperet : il n'est pas pressé");
    printf("   rendement réel : pressé %.3f pt/s, engagé %.3f pt/s (x%.2f)\n",
           g_lignes[ip].rendement, g_lignes[ie].rendement,
           g_lignes[ie].rendement / g_lignes[ip].rendement);

    /*
     * LE BALAYAGE — la carte dont la valeur livrée est un point.
     *
     * Seize couples (exposant, période) joués pour de vrai. C'est ce qui
     * transforme « 1,35 est un bon exposant » d'une affirmation en une lecture :
     * la ligne du dessus et celle du dessous sont dans la même sortie, et on
     * voit ce qu'elles donnent.
     */
    {
        const int kk[]  = { 100, 110, 120, 135 };
        const int pp[]  = { 400, 450, 500, 600 };
        const int essai = 60;
        printf("\n   balayage : part des manches gagnées par l'engagé, "
               "%d manches par case, AVEC sabotage\n", essai);
        printf("            période  ");
        for (unsigned j = 0; j < sizeof pp / sizeof pp[0]; ++j)
            printf("%6.0f s", (double)pp[j] * 0.1);
        printf("\n");
        for (unsigned i = 0; i < sizeof kk / sizeof kk[0]; ++i) {
            printf("     K = %.2f       ", (double)kk[i] * 0.01);
            for (unsigned j = 0; j < sizeof pp / sizeof pp[0]; ++j) {
                room_cp_set_exposant(kk[i]);
                room_cp_set_periode(pp[j]);
                tarifer();
                g_rng = 0x243F6A8885A308D3ull;
                int g[2] = { 0, 0 };
                for (int m = 0; m < essai; ++m) g[une_manche(true, NULL, NULL)]++;
                printf("%6.0f %%", 100.0 * (double)g[ENGAGE] / (double)essai);
            }
            printf("\n");
        }
        room_cp_set_exposant(0);
        room_cp_set_periode(0);
        tarifer();
    }

    /*
     * DEUX TOURNOIS SUR LA VALEUR LIVRÉE, et il faut les deux.
     *
     * Sans sabotage, on mesure l'ÉCONOMIE seule : est-ce que l'exposant paie
     * l'engagement à sa juste valeur ? Avec sabotage, on mesure si la coupure
     * — la seule arme qui puisse reprendre une durée — remet le pressé dans la
     * course. Un exposant peut être juste dans un cas et faux dans l'autre.
     */
    const int manches = 200;
    double part_mode[2] = { 0.0, 0.0 };
    for (int mode = 0; mode < 2; ++mode) {
        g_rng = 0x243F6A8885A308D3ull;   /* même tirage pour les deux : seule
                                          * la présence du sabotage change */
        int gagnes[2] = { 0, 0 };
        long total_s = 0;
        bilan b;
        memset(&b, 0, sizeof b);
        for (int m = 0; m < manches; ++m) {
            int d = 0;
            gagnes[une_manche(mode != 0, &d, &b)]++;
            total_s += d;
        }
        const double part = (double)gagnes[ENGAGE] / (double)manches;
        const double n = (double)(b.manches * 4);   /* quatre de chaque par manche */
        printf("   %-16s : engagé %3d / pressé %3d sur %d manches "
               "(%.0f %% pour l'engagé), durée moyenne %ld s\n",
               (mode == 0) ? "sans sabotage" : "avec sabotage",
               gagnes[ENGAGE], gagnes[PRESSE], manches, part * 100.0,
               total_s / manches);
        printf("        pressé : %.1f pts, %.1f parties, %.2f annulées, "
               "sortie au couperet %.2f\n"
               "        engagé : %.1f pts, %.1f parties, %.2f annulées, "
               "sortie au couperet %.2f\n",
               (double)b.points[PRESSE] / n, (double)b.parties[PRESSE] / n,
               (double)b.annulees[PRESSE] / n, (double)b.sortie[PRESSE] / n,
               (double)b.points[ENGAGE] / n, (double)b.parties[ENGAGE] / n,
               (double)b.annulees[ENGAGE] / n, (double)b.sortie[ENGAGE] / n);

        part_mode[mode] = part;
    }

    /*
     * LE SEUIL, et il ne porte QUE sur la configuration livrée.
     *
     * Ni l'une ni l'autre stratégie au-delà de deux fois sur trois. Ce n'est pas
     * un test d'égalité — deux stratégies ne peuvent pas être exactement
     * équivalentes, et l'exiger reviendrait à supprimer le choix. C'est un test
     * de VIVACITÉ : au-delà de 2/3, la stratégie perdante n'a plus de raison
     * d'être jouée, et le mode se réduit à une seule ligne de conduite.
     */
    CHECK(part_mode[1] >= 1.0 / 3.0 && part_mode[1] <= 2.0 / 3.0,
          "l'engagé gagne %.0f %% des manches — une stratégie domine, revoir "
          "ROOM_CP_K_CENT (%d), ROOM_CP_PERIODE_DS (%d) ou le prix des actions",
          part_mode[1] * 100.0, ROOM_CP_K_CENT, ROOM_CP_PERIODE_DS);

    /*
     * ET CE QUE LA MANCHE SANS SABOTAGE ÉTABLIT, qui est le résultat le plus
     * important de ce test.
     *
     * Elle n'est PAS tenue au même seuil, parce qu'elle n'est pas le jeu : le
     * mode se joue avec ses six actions. Elle sert à mesurer ce que ces actions
     * font, et la réponse est : tout. Sans elles, l'engagé gagne neuf manches
     * sur dix — la prime de durée n'est compensée par rien, et le pressé n'a
     * aucune raison d'exister.
     *
     * L'écart entre les deux colonnes est donc l'UTILITÉ des six actions,
     * chiffrée. Le test exige qu'il reste franc : si quelqu'un rend les actions
     * trop chères, trop faibles ou trop lentes, elles cesseront de rééquilibrer
     * quoi que ce soit, la manche redeviendra celle du haut, et cette ligne le
     * dira — alors que le seuil du dessus, lui, resterait vert par accident.
     *
     * C'est arrivé pendant la mise au point, exactement comme ça : avec un
     * blindage à durée, les deux colonnes rendaient 187 contre 13 AU JOUEUR
     * PRÈS. Six actions, pas une jouée, et rien pour le dire.
     */
    CHECK(part_mode[0] - part_mode[1] > 0.20,
          "le sabotage ne change presque rien à l'issue (%.0f %% sans, %.0f %% "
          "avec) : les six actions ne servent plus à rééquilibrer la manche",
          part_mode[0] * 100.0, part_mode[1] * 100.0);
    printf("   les six actions déplacent l'issue de %.0f points de pourcentage\n",
           (part_mode[0] - part_mode[1]) * 100.0);

    /*
     * LA STABILITÉ. Un équilibre obtenu sur une seule suite de tirages est un
     * équilibre qu'on a peut-être trouvé en cherchant. Trois graines
     * indépendantes, et la fourchette est imprimée : c'est elle qu'il faut lire
     * pour juger si 55 % veut dire « équilibré » ou « bruit ».
     */
    {
        const uint64_t graines[] = { 0x9E3779B97F4A7C15ull, 0xBF58476D1CE4E5B9ull,
                                     0x94D049BB133111EBull };
        double lo = 1.0, hi = 0.0;
        printf("   stabilité :");
        for (unsigned k = 0; k < sizeof graines / sizeof graines[0]; ++k) {
            g_rng = graines[k];
            int g[2] = { 0, 0 };
            for (int m = 0; m < manches; ++m) g[une_manche(true, NULL, NULL)]++;
            const double q = (double)g[ENGAGE] / (double)manches;
            if (q < lo) lo = q;
            if (q > hi) hi = q;
            printf(" %.0f %%", q * 100.0);
        }
        printf("  (étendue %.0f points)\n", (hi - lo) * 100.0);
        CHECK(lo >= 1.0 / 3.0 && hi <= 2.0 / 3.0,
              "l'équilibre ne tient pas d'une graine à l'autre : de %.0f %% à "
              "%.0f %%", lo * 100.0, hi * 100.0);
    }
}

/* ========================================================================== */

int main(void)
{
    printf("== le couperet ==\n");
    test_table();
    test_salon();
    test_couperet();
    test_equipes();
    test_actions();
    test_journal();
    test_tournoi();

    printf("\n%d contrôle(s), %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
