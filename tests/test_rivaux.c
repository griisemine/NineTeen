/*
 * test_rivaux.c — LES RIVAUX : est-ce qu'ils JOUENT, et à quel prix.
 *
 * Ce test répond à quatre questions, et une seule d'entre elles pourrait se
 * relire dans le code plutôt que se mesurer.
 *
 * 1. UN RIVAL JOUE-T-IL VRAIMENT ? C'est la question qui a fait naître le
 *    module : un générateur de nombres qui fait monter un compteur serait
 *    indistinguable d'un joueur pour qui regarde un tableau. On vérifie donc
 *    que ses scores montent, que ses parties se TERMINENT, qu'elles passent par
 *    `room_cp_partie_fin` et que les points arrivent au classement.
 *
 * 2. EST-IL LE MÊME SUR DEUX MACHINES ? Deux manches semées pareil doivent
 *    rendre le même état, au point et au fusible près. Sans ça, rejouer une
 *    manche depuis un journal ne rendrait pas le même verdict et le mode ne
 *    pourrait jamais passer en réseau.
 *
 * 3. LE NIVEAU CHANGE-T-IL QUELQUE CHOSE ? L'en-tête publie une table de
 *    rendement mesurée borne par borne ; ce qu'elle ne dit pas, c'est si l'écart
 *    survit à une manche entière, avec ses couperets et ses coupures. On fait
 *    donc jouer des chevronnés contre des débutants et on compte.
 *
 * 4. COMBIEN ÇA COÛTE, en microsecondes par image et en coupures subies par le
 *    joueur humain. Le premier chiffre décide si sept rivaux tiennent dans une
 *    image ; le second décide si le mode est jouable, et c'est le plus
 *    important des deux.
 *
 * L'HUMAIN DE CE TEST est piloté ICI, pas par le module : il alloue son état,
 * appelle l'autopilote à chaque pas et ne dépense JAMAIS un fusible. C'est le pire
 * cas — un joueur qui ne se défend pas — et c'est celui dont le nombre de
 * coupures doit rester supportable.
 */
#include "games.h"
#include "room_couperet.h"
#include "room_rivaux.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* Le pas de la manche. Ce n'est PAS le pas de jeu — les rivaux redécoupent en
 * pas fixes de 120 Hz — mais celui d'une boucle d'image, et le prendre plus
 * grand que 1/120 est justement ce qui exerce ce redécoupage. */
#define PAS_IMAGE (1.0f / 60.0f)

/* ==========================================================================
 * L'humain du test
 * ========================================================================== */

/*
 * La borne qu'un humain choisit, avec les MÊMES fonctions publiques que le
 * module : multiplicateur affiché divisé par durée médiane. `longue` demande la
 * meilleure du tableau (c'est l'engagé, et c'est aussi celui qui mène) ; sinon
 * la meilleure qui rentre dans une fenêtre de couperet (c'est le pressé).
 */
static bool borne_humaine(bool longue, const ns_game_api **api_out, bool *hard_out)
{
    const float periode = room_cp_periode();
    const ns_game_api *choix = NULL;
    bool  choix_hard = false;
    float meilleur = -1.0f;

    for (int i = 0; i < ns_game_count(); ++i) {
        const ns_game_api *api = ns_game_at(i);
        if (!api || !api->autopilot) continue;
        for (int h = 0; h < 2; ++h) {
            const bool hard = (h != 0);
            const float duree = room_cp_duree_mediane(api->id, hard);
            if (!(duree > 0.0f)) continue;
            if (!longue && duree >= periode) continue;
            const float rendement = room_cp_multiplicateur(api->id, hard) / duree;
            if (rendement > meilleur) { meilleur = rendement; choix = api; choix_hard = hard; }
        }
    }
    if (!choix) return false;
    *api_out = choix;
    *hard_out = choix_hard;
    return true;
}

typedef struct humain {
    uint8_t            place;
    bool               longue;
    const ns_game_api *api;
    void              *etat;
    bool               hard;
    int64_t            score_vu;
    float              pause;
    uint64_t           alea;
    int32_t            parties;
} humain;

static uint64_t hum_tirer(uint64_t *etat)
{
    uint64_t z = (*etat += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void hum_ouvrir(humain *h, uint8_t place, bool longue, uint64_t graine)
{
    memset(h, 0, sizeof *h);
    h->place  = place;
    h->longue = longue;
    h->alea   = graine ^ 0x5DEECE66Dull;
    size_t max = 0;
    for (int i = 0; i < ns_game_count(); ++i) {
        if (ns_game_at(i)->state_size > max) max = ns_game_at(i)->state_size;
    }
    h->etat = calloc(1, max);
}

static void hum_fermer(humain *h) { free(h->etat); h->etat = NULL; h->api = NULL; }

/* Un pas de jeu de l'humain : exactement la boucle du module, sans la dépense.
 * Il joue toujours à plein régime — c'est un joueur, pas un niveau. */
static void hum_pas(humain *h, room_couperet *c, float pas)
{
    if (!h->etat) return;
    const room_cp_place *p = &c->place[h->place];
    if (!p->occupee) return;

    if (h->api && (!p->vivante || p->jeu[0] == '\0')) {
        h->api = NULL; h->score_vu = 0; h->pause = 1.5f;
    }
    if (!p->vivante) return;

    if (!h->api) {
        h->pause -= pas;
        if (h->pause > 0.0f) return;
        const ns_game_api *api = NULL; bool hard = false;
        if (!borne_humaine(h->longue, &api, &hard)) return;
        memset(h->etat, 0, api->state_size);
        api->reset(h->etat, hum_tirer(&h->alea), hard);
        api->set_best(h->etat, 0);
        h->api = api; h->hard = hard; h->score_vu = 0;
        room_cp_partie_debut(c, h->place, api->id, hard);
        return;
    }

    (void)h->api->autopilot(h->etat);
    h->api->tick(h->etat, pas);
    ns_game_events ev;
    memset(&ev, 0, sizeof ev);
    h->api->events(h->etat, &ev);

    const int64_t score = (int64_t)h->api->score(h->etat);
    if (score != h->score_vu) { h->score_vu = score; room_cp_avance(c, h->place, score); }

    float mort = 0.0f;
    if (h->api->dead(h->etat, &mort)) {
        (void)room_cp_partie_fin(c, h->place, score);
        h->parties++;
        h->api = NULL; h->score_vu = 0; h->pause = 1.5f;
    }
}

/* ==========================================================================
 * 1. LE SALON
 * ========================================================================== */

static void test_salon(void)
{
    printf("\n-- le salon : qui tient quelle place --\n");

    room_couperet c;
    room_cp_ouvrir(&c, 8, false);
    CHECK(room_cp_asseoir(&c, 3, "Momo", 3), "la place humaine refuse de s'asseoir");

    room_rivaux r;
    room_rv_ouvrir(&r, 0xC0FFEEu);
    const int assis = room_rv_remplir(&r, &c);

    CHECK(assis == 7, "%d rivaux assis pour sept places libres", assis);
    CHECK(room_cp_salon_plein(&c), "le salon n'est pas plein après remplissage");
    CHECK(!room_rv_tenue(&r, 3), "la place humaine est comptée comme un rival");
    for (uint8_t i = 0; i < 8; ++i) {
        if (i == 3) continue;
        CHECK(room_rv_tenue(&r, i), "la place %u n'est pas tenue", i);
        CHECK(c.place[i].pseudo[0] != '\0', "la place %u n'a pas de nom", i);
    }

    /* Les noms ne se répètent pas — celui de l'humain compris. C'est la seule
     * chose qui empêche un bandeau de montrer deux « Momo » dont l'un respire. */
    for (uint8_t i = 0; i < 8; ++i) {
        for (uint8_t k = (uint8_t)(i + 1u); k < 8; ++k) {
            CHECK(strcmp(c.place[i].pseudo, c.place[k].pseudo) != 0,
                  "deux places portent le nom « %s »", c.place[i].pseudo);
        }
    }
    printf("   salon :");
    for (uint8_t i = 0; i < 8; ++i) {
        printf(" %s%s", c.place[i].pseudo, room_rv_tenue(&r, i) ? "*" : "(humain)");
    }
    printf("\n   (* = rival, et l'interface peut le dire : room_rv_tenue)\n");

    /* Le niveau et la conduite sont tirés de la graine, donc reproductibles. */
    room_rivaux r2;
    room_couperet c2;
    room_cp_ouvrir(&c2, 8, false);
    (void)room_cp_asseoir(&c2, 3, "Momo", 3);
    room_rv_ouvrir(&r2, 0xC0FFEEu);
    (void)room_rv_remplir(&r2, &c2);
    for (uint8_t i = 0; i < 8; ++i) {
        CHECK(strcmp(c.place[i].pseudo, c2.place[i].pseudo) == 0,
              "la place %u change de nom à graine égale", i);
        CHECK(room_rv_niveau_de(&r, i) == room_rv_niveau_de(&r2, i),
              "la place %u change de niveau à graine égale", i);
        CHECK(room_rv_conduite_de(&r, i) == room_rv_conduite_de(&r2, i),
              "la place %u change de conduite à graine égale", i);
    }
    room_rv_fermer(&r2);

    /* Une autre graine donne un autre salon : sans ça, toutes les manches se
     * ressembleraient et la graine ne servirait à rien. */
    room_rivaux r3;
    room_couperet c3;
    room_cp_ouvrir(&c3, 8, false);
    room_rv_ouvrir(&r3, 0xBADCAFEull);
    (void)room_rv_remplir(&r3, &c3);
    int differents = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (strcmp(c3.place[i].pseudo, c.place[i].pseudo) != 0) differents++;
    }
    CHECK(differents >= 4, "deux graines donnent %d noms différents sur huit",
          differents);
    room_rv_fermer(&r3);

    /* En équipes, les camps s'équilibrent : un camp seul debout termine la
     * manche au premier couperet, ce qui n'est pas une manche. */
    room_couperet ce;
    room_rivaux re;
    room_cp_ouvrir(&ce, 8, true);
    room_rv_ouvrir(&re, 7u);
    CHECK(room_rv_remplir(&re, &ce) == 8, "huit places d'équipe non remplies");
    int n0 = 0, n1 = 0;
    for (int i = 0; i < 8; ++i) { if (ce.place[i].camp == 0) n0++; else n1++; }
    CHECK(n0 == 4 && n1 == 4, "camps déséquilibrés : %d contre %d", n0, n1);
    room_rv_fermer(&re);

    room_rv_fermer(&r);
}

/* ==========================================================================
 * 2. UNE MANCHE — le moteur commun aux mesures qui suivent
 * ========================================================================== */

typedef struct bilan {
    int32_t points[ROOM_CP_MAX_PLACES];
    int32_t parties[ROOM_CP_MAX_PLACES];
    int32_t annulees[ROOM_CP_MAX_PLACES];
    int32_t fusibles[ROOM_CP_MAX_PLACES];
    int32_t sortie[ROOM_CP_MAX_PLACES];
    uint8_t vainqueur;
    int32_t couperets;
    float   duree;
    int     bornes_vues;        /* combien de bornes distinctes ont été jouées */
    double  us_par_pas;         /* coût mesuré de room_rv_avancer, par pas 120 Hz */
    bool    valide;

    /* Ce que les rivaux ont ACHETÉ, lu dans le journal du Couperet. Une action
     * que personne ne joue est une action qui n'existe pas — c'est le défaut
     * que l'en-tête du Couperet raconte avoir corrigé sur le blindage, et il
     * n'y a aucune raison qu'une politique de dépense y échappe. */
    int32_t achats[ROOM_CP_ACTION_COUNT];
    int32_t absorbe, renvoye;

    /* Combien de lames sont tombées sur une place COUPÉE PENDANT LE MÊME CYCLE.
     * C'est la seule façon de chiffrer si une coupure a servi à quelque chose :
     * une borne éteinte trente secondes avant le verdict se rallume, une borne
     * éteinte juste avant retire sa défense au moment où elle compte. */
    int32_t coupures_decisives;
} bilan;

/*
 * Joue une manche entière.
 *
 * `hum` vaut `ROOM_CP_MAX_PLACES` s'il n'y a pas d'humain ; sinon cette place
 * est pilotée par le test et ne dépense jamais rien. `regler` reçoit le banc
 * juste après le remplissage, pour imposer niveaux et conduites.
 */
static void jouer_manche_pas(uint64_t graine, uint8_t hum, bool hum_longue,
                             void (*regler)(room_rivaux *, int), int arg,
                             bilan *out, bool chronometrer, float pas_image)
{
    room_couperet c;
    room_rivaux   r;
    humain        h;
    bool          avec_humain = (hum < ROOM_CP_MAX_PLACES);

    room_cp_ouvrir(&c, 8, false);
    if (avec_humain) (void)room_cp_asseoir(&c, hum, "Toi", hum);
    room_rv_ouvrir(&r, graine);
    (void)room_rv_remplir(&r, &c);
    if (regler) regler(&r, arg);
    if (avec_humain) hum_ouvrir(&h, hum, hum_longue, graine);
    room_cp_lancer(&c, graine);

    char vues[16][ROOM_CP_JEU + 2];
    int  nvues = 0;
    bool valide = true;
    double ns_total = 0.0;
    int32_t achats[ROOM_CP_ACTION_COUNT];
    int32_t absorbe = 0, renvoye = 0, decisives = 0;
    bool    coupe_ce_cycle[ROOM_CP_MAX_PLACES];
    memset(achats, 0, sizeof achats);
    memset(coupe_ce_cycle, 0, sizeof coupe_ce_cycle);

    int gardefou = (int)(ROOM_CP_MANCHE_MAX_S / pas_image) + 16;
    while (c.phase == ROOM_CP_COURSE && gardefou-- > 0) {
        if (chronometrer) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            room_rv_avancer(&r, &c, pas_image);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ns_total += (double)(t1.tv_sec - t0.tv_sec) * 1e9 +
                        (double)(t1.tv_nsec - t0.tv_nsec);
        } else {
            room_rv_avancer(&r, &c, pas_image);
        }
        if (avec_humain) hum_pas(&h, &c, pas_image);
        room_cp_avancer(&c, pas_image);
        if (!room_cp_valide(&c)) valide = false;

        /* Le journal se vide à chaque image, comme la salle le fera : il ne
         * garde que trente-deux événements, et une manche en produit des
         * centaines. */
        room_cp_evenement e;
        while (room_cp_prendre(&c, &e)) {
            if (e.type == ROOM_CP_EVT_ACTION && e.valeur >= 0 &&
                e.valeur < ROOM_CP_ACTION_COUNT) {
                achats[e.valeur]++;
                if (e.valeur == ROOM_CP_COUPURE && e.b < ROOM_CP_MAX_PLACES)
                    coupe_ce_cycle[e.b] = true;
            } else if (e.type == ROOM_CP_EVT_ABSORBE) absorbe++;
            else if (e.type == ROOM_CP_EVT_RENVOYE) renvoye++;
            else if (e.type == ROOM_CP_EVT_COUPERET) {
                if (e.a < ROOM_CP_MAX_PLACES && coupe_ce_cycle[e.a]) decisives++;
                memset(coupe_ce_cycle, 0, sizeof coupe_ce_cycle);
            }
        }

        for (int i = 0; i < 8; ++i) {
            if (c.place[i].jeu[0] == '\0') continue;
            char cle[ROOM_CP_JEU + 2];
            snprintf(cle, sizeof cle, "%s%c", c.place[i].jeu, c.place[i].hard ? 'H' : 'N');
            bool deja = false;
            for (int k = 0; k < nvues; ++k) if (strcmp(vues[k], cle) == 0) { deja = true; break; }
            if (!deja && nvues < 16) snprintf(vues[nvues++], sizeof vues[0], "%s", cle);
        }
    }

    memset(out, 0, sizeof *out);
    for (int i = 0; i < 8; ++i) {
        out->points[i]   = c.place[i].points;
        out->parties[i]  = c.place[i].parties;
        out->annulees[i] = c.place[i].annulees;
        out->fusibles[i] = c.place[i].fusibles;
        out->sortie[i]   = c.place[i].sortie_a;
    }
    out->vainqueur    = c.vainqueur;
    out->couperets    = c.couperets;
    out->duree        = c.horloge;
    out->bornes_vues  = nvues;
    out->valide       = valide;
    out->absorbe      = absorbe;
    out->renvoye      = renvoye;
    out->coupures_decisives = decisives;
    memcpy(out->achats, achats, sizeof achats);
    /*
     * LE COÛT SE RAPPORTE AU PAS DE SIMULATION, pas à l'image du test : le
     * module redécoupe ce qu'on lui donne en pas fixes de 120 Hz, donc une
     * image de 1/60 s en fait deux. Diviser par les images rendrait un chiffre
     * qui dépend du pas choisi par l'appelant et ne dirait rien.
     */
    const double pas_sim = (double)c.horloge * 120.0;
    out->us_par_pas = (pas_sim > 0.0) ? ns_total / pas_sim / 1000.0 : 0.0;

    if (avec_humain) hum_fermer(&h);
    room_rv_fermer(&r);
}


/* Le pas d'image ordinaire. La variante ci-dessus n'existe que pour le contrôle
 * de découpage du test de déterminisme. */
static void jouer_manche(uint64_t graine, uint8_t hum, bool hum_longue,
                         void (*regler)(room_rivaux *, int), int arg,
                         bilan *out, bool chronometrer)
{
    jouer_manche_pas(graine, hum, hum_longue, regler, arg, out, chronometrer,
                     PAS_IMAGE);
}

/* ==========================================================================
 * 3. UN RIVAL JOUE VRAIMENT
 * ========================================================================== */

static void test_joue(void)
{
    printf("\n-- un rival joue : scores, parties finies, points encaissés --\n");

    bilan b;
    jouer_manche(0x1234u, ROOM_CP_MAX_PLACES, false, NULL, 0, &b, false);

    CHECK(b.valide, "l'état du couperet est devenu invalide pendant la manche");
    CHECK(b.duree > 60.0f, "la manche n'a duré que %.1f s", (double)b.duree);
    CHECK(b.couperets >= 6, "seulement %d couperets sont tombés", b.couperets);

    int32_t parties = 0, points = 0, joueurs_avec_points = 0;
    for (int i = 0; i < 8; ++i) {
        parties += b.parties[i];
        points  += b.points[i];
        if (b.points[i] > 0) joueurs_avec_points++;
    }
    /* Des parties TERMINÉES, pas commencées : c'est `room_cp_partie_fin` qui
     * les compte, donc chacune est passée par l'encaissement. */
    CHECK(parties >= 8, "seulement %d parties terminées dans la manche", parties);
    CHECK(points > 0, "personne n'a encaissé un seul point");
    CHECK(joueurs_avec_points >= 4,
          "seulement %d rivaux sur huit ont encaissé quelque chose",
          joueurs_avec_points);
    /* Plusieurs bornes jouées : une salle où les huit rivaux se pressent sur la
     * même machine n'est pas une salle. */
    CHECK(b.bornes_vues >= 4, "les rivaux n'ont visité que %d bornes", b.bornes_vues);

    printf("   manche de %.0f s : %d couperets, %d parties terminées, "
           "%d points, %d bornes visitées\n",
           (double)b.duree, b.couperets, parties, points, b.bornes_vues);
    printf("   par place :");
    for (int i = 0; i < 8; ++i) printf(" %d/%dp", b.points[i], b.parties[i]);
    printf("   (points/parties)\n");

    /*
     * LES ACTIONS SONT-ELLES JOUÉES ? L'en-tête du Couperet raconte une version
     * où les six existaient et où aucune n'était achetée. Une politique de
     * dépense qui n'achèterait qu'un blindage vaudrait la même remarque, et
     * c'est le genre de chose qu'on ne voit pas en relisant.
     */
    printf("   achats des rivaux :");
    int32_t total_achats = 0;
    for (int a = 0; a < ROOM_CP_ACTION_COUNT; ++a) {
        printf(" %s=%d", room_cp_action_titre((room_cp_action)a), b.achats[a]);
        total_achats += b.achats[a];
    }
    printf(" (absorbés %d, renvoyés %d)\n", b.absorbe, b.renvoye);
    CHECK(total_achats > 0, "les rivaux n'ont acheté aucune action de la manche");
    CHECK(b.achats[ROOM_CP_BLINDAGE] > 0, "aucun rival ne s'est jamais couvert");

    /*
     * LE SCORE MONTE PENDANT LA PARTIE, et pas seulement à la fin : c'est
     * `room_cp_avance` qui porte la défense du joueur devant le couperet, et un
     * rival qui ne la pousserait pas serait une cible gratuite.
     */
    room_couperet c;
    room_rivaux   r;
    room_cp_ouvrir(&c, 8, false);
    room_rv_ouvrir(&r, 99u);
    (void)room_rv_remplir(&r, &c);
    room_cp_lancer(&c, 99u);
    int32_t provisoire_max = 0;
    int     avec_borne = 0;
    for (int t = 0; t < 40 * 60; ++t) {
        room_rv_avancer(&r, &c, PAS_IMAGE);
        room_cp_avancer(&c, PAS_IMAGE);
        for (int i = 0; i < 8; ++i) {
            if (c.place[i].jeu[0] != '\0') avec_borne = 1;
            if (c.place[i].provisoire > provisoire_max) provisoire_max = c.place[i].provisoire;
        }
    }
    CHECK(avec_borne == 1, "aucun rival n'a inséré de jeton en quarante secondes");
    CHECK(provisoire_max > 0,
          "aucun rival n'a poussé de score en cours de partie (provisoire max %d)",
          provisoire_max);
    printf("   défense devant la lame : provisoire max vu en 40 s = %d points\n",
           provisoire_max);

    /* L'interface peut lire ce que joue un rival, sinon sept écrans sur huit
     * afficheraient une mire pendant qu'on annonce sept joueurs. */
    int montrables = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (room_rv_api(&r, i) && room_rv_etat(&r, i)) montrables++;
    }
    CHECK(montrables > 0, "aucun rival ne publie sa partie pour l'écran de sa borne");
    room_rv_fermer(&r);
}

/* ==========================================================================
 * 3 bis. LES ÉQUIPES — le rôle de soutien, qui n'existe que s'il est joué
 * ========================================================================== */

static void test_equipes(void)
{
    printf("\n-- les équipes : couvrir le porteur, financer depuis la tombe --\n");

    long blindages_offerts = 0, relais = 0;
    int  manches = 12;
    bool valide = true;

    for (int m = 0; m < manches; ++m) {
        room_couperet c;
        room_rivaux   r;
        room_cp_ouvrir(&c, 8, true);
        room_rv_ouvrir(&r, 0xE9u + (uint64_t)m * 3571u);
        (void)room_rv_remplir(&r, &c);
        room_cp_lancer(&c, 0xE9u + (uint64_t)m * 3571u);

        int gardefou = (int)(ROOM_CP_MANCHE_MAX_S / PAS_IMAGE) + 16;
        while (c.phase == ROOM_CP_COURSE && gardefou-- > 0) {
            room_rv_avancer(&r, &c, PAS_IMAGE);
            room_cp_avancer(&c, PAS_IMAGE);
            if (!room_cp_valide(&c)) valide = false;
            room_cp_evenement e;
            while (room_cp_prendre(&c, &e)) {
                if (e.type != ROOM_CP_EVT_ACTION) continue;
                if (e.valeur == ROOM_CP_RELAIS) relais++;
                /* Un blindage posé sur QUELQU'UN D'AUTRE : c'est le soutien, et
                 * c'est la seule trace qu'il en reste dans le journal. */
                if (e.valeur == ROOM_CP_BLINDAGE && e.a != e.b) blindages_offerts++;
            }
        }
        room_rv_fermer(&r);
    }

    printf("   %d manches en équipes : %ld blindages posés sur un coéquipier, "
           "%ld relais depuis la tombe\n", manches, blindages_offerts, relais);
    CHECK(valide, "l'état d'une manche en équipes est devenu invalide");
    CHECK(blindages_offerts > 0,
          "personne ne couvre son porteur : le rôle de soutien n'est pas joué");
    CHECK(relais > 0, "aucun spectre ne finance son équipe");
}

/* ==========================================================================
 * 3 ter. LES BORNES — un rival se tient devant une vraie machine
 * ========================================================================== */

/*
 * UNE SALLE DE TEST, ET CE N'EST PAS LA SALLE LIVRÉE.
 *
 * Elle en a la FORME, relevée dans `assets/scene/salle.room.json` et écrite
 * dans l'en-tête : douze lignes à une seule borne, deux lignes à trois bornes
 * (dedale et piano en régime normal), et deux lignes du tableau des durées qui
 * n'existent nulle part — dedale difficile et piano difficile. C'est cette
 * forme, et pas les indices, qui met les règles à l'épreuve : une machine
 * unique qu'on ne peut pas partager, une machine en trois exemplaires qu'il
 * faut savoir répartir, et un jeu qu'aucun meuble ne porte.
 *
 * La vraie table, elle, vient de `room_rv_bornes_scene` et n'est recopiée nulle
 * part — c'est tout l'intérêt de l'injection.
 */
static int salle_de_test(room_rv_borne *t, int max)
{
    static const char *const solos[] = { "envol", "aplomb", "asteroid",
                                         "demineur", "snake", "shooter" };
    int n = 0;
    for (unsigned i = 0; i < sizeof solos / sizeof solos[0]; ++i) {
        for (int h = 0; h < 2 && n < max; ++h) {
            t[n].index = n;
            snprintf(t[n].jeu, sizeof t[n].jeu, "%s", solos[i]);
            t[n].hard = (h != 0);
            n++;
        }
    }
    for (int k = 0; k < 3 && n < max; ++k) {
        t[n].index = n; snprintf(t[n].jeu, sizeof t[n].jeu, "dedale");
        t[n].hard = false; n++;
    }
    for (int k = 0; k < 3 && n < max; ++k) {
        t[n].index = n; snprintf(t[n].jeu, sizeof t[n].jeu, "piano");
        t[n].hard = false; n++;
    }
    return n;
}

static void test_bornes(void)
{
    printf("\n-- les bornes : qui se tient devant quelle machine --\n");

    room_rv_borne table[ROOM_RV_BORNES];
    const int nb = salle_de_test(table, ROOM_RV_BORNES);
    CHECK(nb == 18, "la salle de test déclare %d bornes au lieu de dix-huit", nb);

    const int32_t reservee = 3;   /* la machine du joueur : personne n'y touche */

    room_couperet c;
    room_rivaux   r;
    room_cp_ouvrir(&c, 8, false);
    room_rv_ouvrir(&r, 0x50FAu);
    CHECK(room_rv_bornes(&r, table, nb) == nb, "la table de bornes est refusée");
    room_rv_reserver(&r, reservee);
    (void)room_rv_remplir(&r, &c);
    room_cp_lancer(&c, 0x50FAu);

    bool vues[ROOM_RV_BORNES];
    memset(vues, 0, sizeof vues);
    int  sans_machine = 0, collisions = 0, reservee_prise = 0, inverse_faux = 0;
    int  jeu_absent = 0;

    int gardefou = (int)(ROOM_CP_MANCHE_MAX_S / PAS_IMAGE) + 16;
    while (c.phase == ROOM_CP_COURSE && gardefou-- > 0) {
        room_rv_avancer(&r, &c, PAS_IMAGE);
        room_cp_avancer(&c, PAS_IMAGE);

        for (uint8_t i = 0; i < 8; ++i) {
            const int32_t b = room_rv_borne_de(&r, i);
            const bool joue = (c.place[i].jeu[0] != '\0');
            if (joue && b < 0) { sans_machine++; continue; }
            if (b < 0) continue;

            vues[b] = true;
            if (b == reservee) reservee_prise++;
            /* La borne dit la place, et la place dit la borne : c'est ce qui
             * permettra de viser en se plantant devant la machine. */
            if (room_rv_place_a_la_borne(&r, b) != i) inverse_faux++;
            for (uint8_t k = (uint8_t)(i + 1u); k < 8; ++k) {
                if (room_rv_borne_de(&r, k) == b) collisions++;
            }
            /*
             * La machine porte bien ce qu'on joue dessus — vérifié SEULEMENT
             * quand la place joue encore. Un couperet ou une coupure éteint la
             * borne dans `room_cp_avancer`, et le rival ne l'apprend qu'au pas
             * suivant : pendant une image, sa machine est celle d'une partie
             * qui vient d'être annulée. C'est le prix de deux modules qui ne
             * partagent pas la même horloge, et c'est 1/120 de seconde.
             */
            if (joue && (strcmp(table[b].jeu, c.place[i].jeu) != 0 ||
                         table[b].hard != c.place[i].hard)) jeu_absent++;
        }
    }

    int occupees = 0;
    for (int i = 0; i < nb; ++i) if (vues[i]) occupees++;
    printf("   %d bornes distinctes occupées sur %d au fil de la manche :",
           occupees, nb);
    for (int i = 0; i < nb; ++i) {
        if (vues[i]) printf(" %s%s", table[i].jeu, table[i].hard ? "-hard" : "");
    }
    printf("\n");

    CHECK(sans_machine == 0,
          "%d fois un rival a joué sans être devant une machine", sans_machine);
    CHECK(collisions == 0, "%d fois deux rivaux ont pris la même borne", collisions);
    CHECK(reservee_prise == 0,
          "%d fois un rival s'est mis à la machine du joueur", reservee_prise);
    CHECK(inverse_faux == 0,
          "%d fois la borne et la place ne se désignaient pas l'une l'autre",
          inverse_faux);
    CHECK(jeu_absent == 0,
          "%d fois un rival a joué un jeu que sa machine ne porte pas", jeu_absent);
    CHECK(occupees >= 6, "seulement %d bornes ont servi de toute la manche", occupees);

    /* Les deux lignes que la salle ne porte pas — dedale et piano difficiles —
     * ne doivent jamais être jouées : on ne joue pas sur une machine absente. */
    room_rv_fermer(&r);

    /* Sans table déclarée, le module retombe sur son comportement d'avant :
     * les rivaux jouent, et ne se tiennent nulle part. */
    room_couperet c2;
    room_rivaux   r2;
    room_cp_ouvrir(&c2, 8, false);
    room_rv_ouvrir(&r2, 0x50FAu);
    (void)room_rv_remplir(&r2, &c2);
    room_cp_lancer(&c2, 0x50FAu);
    int joue_sans_salle = 0, borne_de_nulle_part = 0;
    for (int t = 0; t < 60 * 60 && c2.phase == ROOM_CP_COURSE; ++t) {
        room_rv_avancer(&r2, &c2, PAS_IMAGE);
        room_cp_avancer(&c2, PAS_IMAGE);
        for (uint8_t i = 0; i < 8; ++i) {
            if (c2.place[i].jeu[0] != '\0') joue_sans_salle++;
            if (room_rv_borne_de(&r2, i) != -1) borne_de_nulle_part++;
        }
    }
    CHECK(joue_sans_salle > 0, "sans salle déclarée, plus personne ne joue");
    CHECK(borne_de_nulle_part == 0,
          "%d fois une borne a été rendue alors qu'aucune salle n'est déclarée",
          borne_de_nulle_part);
    room_rv_fermer(&r2);
}

/* ==========================================================================
 * 4. LE DÉTERMINISME
 * ========================================================================== */

static bool memes_bilans(const bilan *a, const bilan *b)
{
    if (a->vainqueur != b->vainqueur || a->couperets != b->couperets) return false;
    for (int i = 0; i < 8; ++i) {
        if (a->points[i] != b->points[i]) return false;
        if (a->parties[i] != b->parties[i]) return false;
        if (a->annulees[i] != b->annulees[i]) return false;
        if (a->fusibles[i] != b->fusibles[i]) return false;
        if (a->sortie[i] != b->sortie[i]) return false;
    }
    return true;
}

static void test_determinisme(void)
{
    printf("\n-- le déterminisme : même graine, même manche --\n");

    for (int essai = 0; essai < 3; ++essai) {
        const uint64_t g = 0xA5A5u + (uint64_t)essai * 977u;
        bilan a, b;
        jouer_manche(g, ROOM_CP_MAX_PLACES, false, NULL, 0, &a, false);
        jouer_manche(g, ROOM_CP_MAX_PLACES, false, NULL, 0, &b, false);
        CHECK(memes_bilans(&a, &b),
              "la graine %llu rend deux manches différentes",
              (unsigned long long)g);
    }

    /* Et avec un humain dans la salle : c'est le cas réel, et c'est celui où
     * une divergence coûterait le plus cher. */
    bilan a, b;
    jouer_manche(0x2468u, 2, true, NULL, 0, &a, false);
    jouer_manche(0x2468u, 2, true, NULL, 0, &b, false);
    CHECK(memes_bilans(&a, &b), "la même manche avec humain diverge");

    /* Deux graines différentes ne doivent PAS rendre la même manche, sans quoi
     * le contrôle ci-dessus ne prouverait rien. */
    bilan d;
    jouer_manche(0x13579u, ROOM_CP_MAX_PLACES, false, NULL, 0, &d, false);
    CHECK(!memes_bilans(&a, &d), "deux graines différentes rendent la même manche");
    printf("   trois graines rejouées à l'identique, points et fusibles compris\n");

    /*
     * LE MÊME TOTAL DANS UN DÉCOUPAGE DIFFÉRENT — la propriété que le module
     * revendique en déduisant ses pas de l'horloge au lieu de les décompter.
     *
     * Elle N'EST PAS établie ici, elle est MESURÉE, et le résultat est imprimé
     * quel qu'il soit. Le module et le couperet accumulent tous deux leur
     * horloge en flottant simple : additionner 1/60 deux mille fois et 1/120
     * quatre mille fois ne donne pas le même nombre au bit près, donc rien ne
     * garantit a priori que le compte de pas coïncide sur toute une manche. Ce
     * qui est garanti, c'est qu'il ne DÉRIVE pas — un décompte accumulerait
     * l'écart, une déduction le borne à un pas.
     */
    for (int essai = 0; essai < 3; ++essai) {
        const uint64_t g = 0x3C3Cu + (uint64_t)essai * 131u;
        bilan lent, rapide;
        jouer_manche_pas(g, ROOM_CP_MAX_PLACES, false, NULL, 0, &lent, false,
                         1.0f / 60.0f);
        jouer_manche_pas(g, ROOM_CP_MAX_PLACES, false, NULL, 0, &rapide, false,
                         1.0f / 120.0f);
        int32_t ecart = 0;
        for (int i = 0; i < 8; ++i) {
            const int32_t e = lent.points[i] - rapide.points[i];
            ecart += (e < 0) ? -e : e;
        }
        printf("   graine %llu : 60 Hz contre 120 Hz -> %s, écart de points %d\n",
               (unsigned long long)g,
               memes_bilans(&lent, &rapide) ? "manche identique" : "manche différente",
               ecart);
    }
    /* Aucun CHECK sur ce dernier point : la propriété qui compte pour le réseau
     * est celle du dessus — même graine, même découpage, même manche — et c'est
     * elle qui est exigée. Ce contrôle-ci RENSEIGNE, il ne juge pas. */
}

/* ==========================================================================
 * 5. LE NIVEAU
 * ========================================================================== */

/*
 * Un chevronné sur les places de parité `arg`, un débutant sur les autres, et
 * la MÊME conduite pour tout le monde : ce qu'on mesure doit être le niveau et
 * rien d'autre.
 *
 * LA PARITÉ ALTERNE d'une manche à l'autre, et ce n'est pas une coquetterie.
 * Un rival choisit la meilleure borne LIBRE, et les places basses servent en
 * premier : sans alternance, le groupe assis sur les places paires aurait
 * toujours les meilleures machines, et on mesurerait l'ordre des sièges.
 */
static void regler_niveaux(room_rivaux *r, int arg)
{
    for (uint8_t i = 0; i < ROOM_CP_MAX_PLACES; ++i) {
        if (!room_rv_tenue(r, i)) continue;
        const bool fort = (((int)i + arg) % 2) == 0;
        room_rv_regler(r, i, fort ? ROOM_RV_CHEVRONNE : ROOM_RV_DEBUTANT,
                       ROOM_RV_ENGAGE);
    }
}

static void test_niveau(void)
{
    printf("\n-- le niveau : le chevronné bat-il le débutant ? --\n");

    const int manches = 120;
    long gagne[2] = { 0, 0 };
    long points[2] = { 0, 0 };
    long parties[2] = { 0, 0 };
    long sortie[2] = { 0, 0 };

    for (int m = 0; m < manches; ++m) {
        bilan b;
        const int parite = m % 2;
        jouer_manche(0x51D0u + (uint64_t)m * 7919u, ROOM_CP_MAX_PLACES, false,
                     regler_niveaux, parite, &b, false);
        for (int i = 0; i < 8; ++i) {
            const int fort = (((i + parite) % 2) == 0) ? 0 : 1;
            points[fort]  += b.points[i];
            parties[fort] += b.parties[i];
            sortie[fort]  += (b.sortie[i] > 0) ? b.sortie[i] : (b.couperets + 1);
        }
        if (b.vainqueur < ROOM_CP_MAX_PLACES) {
            gagne[((((int)b.vainqueur + parite) % 2) == 0) ? 0 : 1]++;
        }
    }

    const double part = (double)gagne[0] / (double)(gagne[0] + gagne[1]);
    printf("   %d manches : le chevronné gagne %ld fois, le débutant %ld "
           "(%.0f %% contre %.0f %%)\n",
           manches, gagne[0], gagne[1], part * 100.0, (1.0 - part) * 100.0);
    printf("   points cumulés : chevronné %ld, débutant %ld (x%.2f)\n",
           points[0], points[1],
           (points[1] > 0) ? (double)points[0] / (double)points[1] : 0.0);
    printf("   rang de sortie moyen : chevronné %.2f, débutant %.2f "
           "(plus grand = sorti plus tard)\n",
           (double)sortie[0] / (double)(manches * 4),
           (double)sortie[1] / (double)(manches * 4));
    printf("   parties terminées : chevronné %ld, débutant %ld\n",
           parties[0], parties[1]);

    /*
     * DEUX SEUILS, ET ILS DISENT DEUX CHOSES DIFFÉRENTES.
     *
     * Les points cumulés mesurent la force ; c'est la grandeur que la table de
     * l'en-tête prédit, et l'écart doit s'y retrouver. Les manches gagnées
     * mesurent ce que le JOUEUR voit ; un niveau qui ne changerait pas qui
     * gagne serait un niveau invisible, donc inexistant.
     */
    CHECK(points[0] > points[1] * 5 / 4,
          "le chevronné ne marque que %ld points contre %ld au débutant",
          points[0], points[1]);
    CHECK(part > 0.60,
          "le chevronné ne gagne que %.0f %% des manches contre un débutant",
          part * 100.0);

    /*
     * LE BROUILLAGE MORD SUR UN RIVAL. C'est la convention déclarée dans
     * l'en-tête — un rival gêné retombe au taux du débutant — et elle doit se
     * voir sur le terrain, sinon les deux actions les moins chères du Couperet
     * ne serviraient à rien contre sept places sur huit.
     */
    long sans = 0, avec = 0;
    for (int m = 0; m < 8; ++m) {
        for (int gene = 0; gene < 2; ++gene) {
            room_couperet c;
            room_rivaux   r;
            room_cp_ouvrir(&c, 8, false);
            room_rv_ouvrir(&r, 0x9E37u + (uint64_t)m);
            (void)room_rv_remplir(&r, &c);
            /* Des pressés : leurs bornes tiennent dans une fenêtre de couperet,
             * donc ils ENCAISSENT plusieurs fois en deux minutes. Des engagés
             * n'auraient rien fini du tout et les deux colonnes vaudraient zéro
             * — c'est ce que la première version de ce contrôle a mesuré. */
            for (uint8_t i = 0; i < 8; ++i)
                room_rv_regler(&r, i, ROOM_RV_CHEVRONNE, ROOM_RV_PRESSE);
            room_cp_lancer(&c, 0x9E37u + (uint64_t)m);
            for (int t = 0; t < 120 * 60 && c.phase == ROOM_CP_COURSE; ++t) {
                /* La place 0 est brouillée en permanence : on entretient
                 * l'effet à la main plutôt que de dépenser des fusibles, pour
                 * que la seule différence entre les deux colonnes soit lui. */
                if (gene) c.place[0].brouillage = 1.0f;
                room_rv_avancer(&r, &c, PAS_IMAGE);
                room_cp_avancer(&c, PAS_IMAGE);
            }
            if (gene) avec += c.place[0].points; else sans += c.place[0].points;
            room_rv_fermer(&r);
        }
    }
    printf("   brouillage permanent sur une place : %ld points contre %ld sans "
           "(x%.2f)\n", avec, sans, (avec > 0) ? (double)sans / (double)avec : 0.0);
    CHECK(avec < sans, "le brouillage ne coûte rien à un rival (%ld contre %ld)",
          avec, sans);
}

/* ==========================================================================
 * 6. LE PLAISIR DE JEU — combien de coupures un humain subit
 * ========================================================================== */

static void test_coupures(void)
{
    printf("\n-- les coupures subies par un humain qui ne se défend jamais --\n");

    const int manches = 100;
    for (int profil = 0; profil < 2; ++profil) {
        long coupures = 0, parties = 0, points = 0, gagnees = 0;
        long duree = 0, coupures_salle = 0, meneur = 0, decisives = 0;
        for (int m = 0; m < manches; ++m) {
            bilan b;
            jouer_manche(0x7E57u + (uint64_t)m * 4099u, 4,
                         profil == 1, NULL, 0, &b, false);
            coupures += b.annulees[4];
            parties  += b.parties[4];
            points   += b.points[4];
            duree    += (long)b.duree;
            coupures_salle += b.achats[ROOM_CP_COUPURE];
            decisives += b.coupures_decisives;
            for (int i = 0; i < 8; ++i) if (b.points[i] > b.points[4]) { meneur++; break; }
            if (b.vainqueur == 4) gagnees++;
        }
        const double par_manche = (double)coupures / (double)manches;
        printf("   humain %-8s : %.2f coupure par manche, %.1f parties finies, "
               "%ld points, %ld manches gagnées sur %d\n",
               profil ? "engagé" : "pressé", par_manche,
               (double)parties / (double)manches, points, gagnees, manches);
        printf("                     (manche moyenne %.0f s, soit une coupure "
               "toutes les %.0f s ; %.2f coupures par manche dans toute la "
               "salle, et l'humain finit devant tout le monde %ld fois sur "
               "%d)\n",
               (double)duree / (double)manches,
               (coupures > 0) ? (double)duree / (double)coupures : 0.0,
               (double)coupures_salle / (double)manches,
               (long)manches - meneur, manches);
        /* Une coupure DÉCISIVE est celle dont la victime est prise par la lame
         * du même cycle. C'est ce que la fenêtre d'avant-lame cherche à
         * produire : moins de coupures, mais qui décident. */
        printf("                     (%.2f coupure décisive par manche, soit "
               "%.0f %% des coupures)\n",
               (double)decisives / (double)manches,
               (coupures_salle > 0)
                   ? 100.0 * (double)decisives / (double)coupures_salle : 0.0);

        /*
         * LE SEUIL EST UNE RÈGLE DE JEU, pas un contrôle de régression : au-delà
         * de trois parties annulées par manche, le joueur passe son temps à
         * regarder sa borne s'éteindre et le mode cesse d'être jouable. C'est le
         * chiffre que le propriétaire a fixé, et la politique de dépense des
         * rivaux a été corrigée jusqu'à le tenir.
         */
        CHECK(par_manche <= 3.0,
              "un humain %s subit %.2f coupures par manche",
              profil ? "engagé" : "pressé", par_manche);
    }
}

/* ==========================================================================
 * 7. LE COÛT
 * ========================================================================== */

static void test_cout(void)
{
    printf("\n-- le coût : sept rivaux dans une image --\n");

    double somme = 0.0;
    const int manches = 5;
    for (int m = 0; m < manches; ++m) {
        bilan b;
        jouer_manche(0xC057u + (uint64_t)m * 131u, 0, false, NULL, 0, &b, true);
        somme += b.us_par_pas;
    }
    const double us = somme / (double)manches;

    /* Une image à 120 Hz dure 8 333 us, et le module joue exactement un pas par
     * image à cette cadence : le coût par pas EST le coût par image. Le seuil
     * est posé à 1 % de ce budget — au-delà, la réponse honnête serait de faire
     * tourner les rivaux à un pas plus grand, en le disant, plutôt que de les
     * faire semblant-jouer. */
    const double budget = 1e6 / 120.0;
    printf("   %.2f us par pas de 120 Hz pour sept rivaux, soit autant par "
           "image à cette cadence (%.3f %% du budget d'image)\n",
           us, us / budget * 100.0);
    printf("   une seconde de manche coûte donc %.0f us de rivaux\n", us * 120.0);
    CHECK(us < budget * 0.01,
          "les rivaux coûtent %.1f us par image, soit plus de 1 %% du budget",
          us);
}

/* ==========================================================================
 * Le point d'entrée
 * ========================================================================== */

int main(void)
{
    printf("== room_rivaux : les places que personne n'occupe ==\n");

    test_salon();
    test_joue();
    test_bornes();
    test_equipes();
    test_determinisme();
    test_niveau();
    test_coupures();
    test_cout();

    printf("\n%d contrôles, %d échecs\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
