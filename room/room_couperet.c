/*
 * room_couperet.c — la règle du Couperet.
 *
 * Tout est ici et rien n'est ailleurs : ni horloge système, ni socket, ni
 * fichier. Le temps entre par `room_cp_avancer`, les scores par
 * `room_cp_partie_fin`, et le module ne rend que des nombres. C'est ce qui
 * permet à `tests/test_couperet.c` de faire jouer dix mille manches en une
 * seconde pour mesurer si les deux stratégies s'équilibrent.
 */
#include "room_couperet.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * Petites choses
 * ========================================================================== */

static void copier(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    if (!dst || n == 0) return;
    if (src) {
        for (; i + 1 < n && src[i] != '\0'; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static float maxf(float a, float b) { return (a > b) ? a : b; }

/* ==========================================================================
 * La table des actions, dérivée de la liste unique de l'en-tête
 * ========================================================================== */

typedef struct cp_action_def {
    int32_t     cout;
    int32_t     duree_ds;
    bool        offensive;
    const char *titre;
    const char *effet;
} cp_action_def;

#define CP_ACTION_ROW(cle, cout, duree, off, titre, effet) \
    { (cout), (duree), (off) != 0, titre, effet },
static const cp_action_def g_actions[ROOM_CP_ACTION_COUNT] = {
    ROOM_CP_ACTIONS(CP_ACTION_ROW)
};
#undef CP_ACTION_ROW

static bool action_valide(room_cp_action a)
{
    return (int)a >= 0 && (int)a < ROOM_CP_ACTION_COUNT;
}

int32_t room_cp_action_cout(room_cp_action a)
{
    return action_valide(a) ? g_actions[a].cout : 0;
}

float room_cp_action_duree(room_cp_action a)
{
    return action_valide(a) ? (float)g_actions[a].duree_ds * 0.1f : 0.0f;
}

bool room_cp_action_offensive(room_cp_action a)
{
    return action_valide(a) && g_actions[a].offensive;
}

const char *room_cp_action_titre(room_cp_action a)
{
    return action_valide(a) ? g_actions[a].titre : "";
}

const char *room_cp_action_effet(room_cp_action a)
{
    return action_valide(a) ? g_actions[a].effet : "";
}

/* ==========================================================================
 * La table des durées
 * ========================================================================== */

typedef struct cp_duree_row {
    const char *jeu;
    int32_t     normale_ds;
    int32_t     difficile_ds;
} cp_duree_row;

#define CP_DUREE_ROW(id, dn, dh) { #id, (dn), (dh) },
static const cp_duree_row g_durees[] = {
    ROOM_CP_DUREES(CP_DUREE_ROW)
};
#undef CP_DUREE_ROW

#define CP_DUREE_COUNT ((int)(sizeof g_durees / sizeof g_durees[0]))

float room_cp_duree_mediane(const char *jeu, bool hard)
{
    if (!jeu) return 0.0f;
    for (int i = 0; i < CP_DUREE_COUNT; ++i) {
        if (strcmp(g_durees[i].jeu, jeu) == 0) {
            const int32_t ds = hard ? g_durees[i].difficile_ds
                                    : g_durees[i].normale_ds;
            return (float)ds * 0.1f;
        }
    }
    return 0.0f;
}

/* Les deux réglages injectables. Zéro = la valeur du barème. */
static int g_exposant_cent = 0;
static int g_periode_ds    = 0;

void room_cp_set_exposant(int centiemes) { g_exposant_cent = (centiemes > 0) ? centiemes : 0; }
void room_cp_set_periode(int dixiemes)   { g_periode_ds    = (dixiemes  > 0) ? dixiemes  : 0; }

static int exposant_cent(void) { return g_exposant_cent ? g_exposant_cent : ROOM_CP_K_CENT; }
static int periode_ds(void)    { return g_periode_ds    ? g_periode_ds    : ROOM_CP_PERIODE_DS; }

float room_cp_periode(void) { return (float)periode_ds() * 0.1f; }

float room_cp_engagement(float secondes)
{
    const float plafond = (float)ROOM_CP_DUREE_MAX_DS * 0.1f;
    const float tref    = (float)ROOM_CP_T_REF_DS * 0.1f;
    const float k       = (float)exposant_cent() * 0.01f;

    if (!(secondes > 0.0f)) return 0.0f;          /* attrape aussi les NaN */
    if (secondes > plafond) secondes = plafond;
    return powf(secondes / tref, k);
}

float room_cp_multiplicateur(const char *jeu, bool hard)
{
    return room_cp_engagement(room_cp_duree_mediane(jeu, hard));
}

int32_t room_cp_points_pour(const char *jeu, bool hard, int64_t score)
{
    /*
     * La série est passée à ZÉRO, et c'est écrit dans l'en-tête : elle
     * récompense la fidélité, ce qui n'a pas sa place dans une manche où l'on
     * se compare. Le plafond par partie de `room_bareme.h` s'applique en
     * revanche tel quel — c'est le même besoin, couper la queue de
     * distribution qui vient du hasard des graines.
     */
    const int32_t tickets = room_eco_tickets_pour(jeu, hard, score, 0);
    if (tickets <= 0) return 0;

    const float pts = (float)tickets * room_cp_multiplicateur(jeu, hard);
    if (!(pts > 0.0f)) return 0;
    return (int32_t)lrintf(pts);
}

/* ==========================================================================
 * Le journal
 * ========================================================================== */

static void pousser(room_couperet *c, room_cp_evt type,
                    uint8_t a, uint8_t b, int32_t valeur)
{
    const uint8_t suivant = (uint8_t)((c->jrn_queue + 1u) % ROOM_CP_JOURNAL);
    if (suivant == c->jrn_tete) {
        /* Plein. On jette le PLUS ANCIEN : un journal drainé à chaque image ne
         * se remplit que si plus personne ne le lit, et dans ce cas ce sont les
         * événements récents qui décrivent la manche. */
        c->jrn_tete = (uint8_t)((c->jrn_tete + 1u) % ROOM_CP_JOURNAL);
    }
    c->journal[c->jrn_queue].type   = type;
    c->journal[c->jrn_queue].a      = a;
    c->journal[c->jrn_queue].b      = b;
    c->journal[c->jrn_queue].valeur = valeur;
    c->jrn_queue = suivant;
}

bool room_cp_prendre(room_couperet *c, room_cp_evenement *out)
{
    if (!c || c->jrn_tete == c->jrn_queue) return false;
    if (out) *out = c->journal[c->jrn_tete];
    c->jrn_tete = (uint8_t)((c->jrn_tete + 1u) % ROOM_CP_JOURNAL);
    return true;
}

/* ==========================================================================
 * Le salon
 * ========================================================================== */

void room_cp_ouvrir(room_couperet *c, uint8_t places, bool equipes)
{
    if (!c) return;
    const room_couperet vide = { 0 };
    *c = vide;

    if (places < 2) places = 2;
    if (places > ROOM_CP_MAX_PLACES) places = ROOM_CP_MAX_PLACES;

    c->phase    = ROOM_CP_SALON;
    c->places   = places;
    c->equipes  = equipes;
    c->prochain = room_cp_periode();
    c->vainqueur = ROOM_CP_MAX_PLACES;

    for (int i = 0; i < ROOM_CP_MAX_PLACES; ++i) c->place[i].sortie_a = -1;
}

bool room_cp_asseoir(room_couperet *c, uint8_t place, const char *pseudo, uint8_t camp)
{
    if (!c || c->phase != ROOM_CP_SALON) return false;
    if (place >= c->places) return false;
    if (c->place[place].occupee) return false;

    room_cp_place *p = &c->place[place];
    const room_cp_place neuf = { 0 };
    *p = neuf;

    p->occupee  = true;
    p->vivante  = true;
    p->fusibles   = ROOM_CP_FUSIBLES_DEPART;
    p->sortie_a = -1;
    copier(p->pseudo, sizeof p->pseudo, pseudo);
    /*
     * Hors mode équipes, le camp EST la place. C'est ce qui permet aux deux
     * modes de partager toute la suite : le couperet classe des camps, et un
     * camp d'une place se comporte exactement comme un joueur seul. Écrire deux
     * chemins aurait donné deux règles à tenir d'accord.
     */
    p->camp = c->equipes ? (uint8_t)(camp % ROOM_CP_MAX_PLACES) : place;

    pousser(c, ROOM_CP_EVT_ARRIVEE, place, 0, 0);
    return true;
}

void room_cp_lever(room_couperet *c, uint8_t place)
{
    if (!c || place >= ROOM_CP_MAX_PLACES) return;
    room_cp_place *p = &c->place[place];
    if (!p->occupee) return;

    if (c->phase == ROOM_CP_SALON) {
        const room_cp_place neuf = { 0 };
        *p = neuf;
        p->sortie_a = -1;
    } else {
        /*
         * En course, partir ne LIBÈRE pas la place : le joueur devient spectre
         * et ses points restent acquis à son camp. Sans ça, un coéquipier fâché
         * retirerait à son équipe tout ce qu'il lui a rapporté en raccrochant,
         * et le mode équipe se jouerait au chantage plutôt qu'à l'arcade.
         */
        p->vivante = false;
        p->jeu[0]  = '\0';
        p->provisoire = 0;
        p->score_vu = 0;
    }
    pousser(c, ROOM_CP_EVT_DEPART, place, 0, 0);
}

bool room_cp_salon_plein(const room_couperet *c)
{
    if (!c) return false;
    int n = 0;
    for (int i = 0; i < c->places; ++i) if (c->place[i].occupee) n++;
    return n == (int)c->places;
}

void room_cp_lancer(room_couperet *c, uint64_t graine)
{
    if (!c || c->phase != ROOM_CP_SALON) return;
    if (!room_cp_salon_plein(c)) return;

    c->graine   = graine;
    c->phase    = ROOM_CP_COURSE;
    c->horloge  = 0.0f;
    c->prochain = room_cp_periode();
    c->couperets = 0;
    pousser(c, ROOM_CP_EVT_DEBUT, 0, 0, 0);
}

/* ==========================================================================
 * Camps
 * ========================================================================== */

int32_t room_cp_points_camp(const room_couperet *c, uint8_t camp)
{
    if (!c) return 0;
    int32_t total = 0;
    for (int i = 0; i < c->places; ++i) {
        if (c->place[i].occupee && c->place[i].camp == camp) {
            total += c->place[i].points;
        }
    }
    return total;
}

int32_t room_cp_vivants_camp(const room_couperet *c, uint8_t camp)
{
    if (!c) return 0;
    int32_t n = 0;
    for (int i = 0; i < c->places; ++i) {
        if (c->place[i].occupee && c->place[i].vivante && c->place[i].camp == camp) n++;
    }
    return n;
}

int32_t room_cp_valeur(const room_couperet *c, uint8_t place)
{
    if (!c || place >= c->places) return 0;
    const room_cp_place *p = &c->place[place];
    if (!p->occupee) return 0;
    return p->points + p->provisoire;
}

/*
 * Ce que vaut un camp DEVANT LE COUPERET.
 *
 * Les parties en cours ne comptent que pour les VIVANTS — un spectre n'en a
 * pas — mais les points encaissés comptent pour tout le monde, spectres
 * compris : ils ont été gagnés, et les retirer punirait une équipe de ses
 * morts une seconde fois.
 */
int32_t room_cp_valeur_camp(const room_couperet *c, uint8_t camp)
{
    if (!c) return 0;
    int32_t total = 0;
    for (int i = 0; i < c->places; ++i) {
        const room_cp_place *p = &c->place[i];
        if (!p->occupee || p->camp != camp) continue;
        total += p->points;
        if (p->vivante) total += p->provisoire;
    }
    return total;
}

/*
 * Le camp le plus faible parmi ceux qui ont encore quelqu'un debout.
 * `ROOM_CP_MAX_PLACES` s'il n'y en a pas, ou s'il n'en reste qu'un.
 */
static uint8_t camp_le_plus_faible(const room_couperet *c)
{
    uint8_t vus[ROOM_CP_MAX_PLACES];
    int n = 0;
    for (int i = 0; i < c->places; ++i) {
        const room_cp_place *p = &c->place[i];
        if (!p->occupee || !p->vivante) continue;
        bool deja = false;
        for (int k = 0; k < n; ++k) if (vus[k] == p->camp) { deja = true; break; }
        if (!deja) vus[n++] = p->camp;
    }
    if (n <= 1) return ROOM_CP_MAX_PLACES;   /* un seul camp debout : plus rien à couper */

    uint8_t pire = vus[0];
    int32_t pire_val = room_cp_valeur_camp(c, pire);
    for (int k = 1; k < n; ++k) {
        const int32_t val = room_cp_valeur_camp(c, vus[k]);
        if (val < pire_val) { pire = vus[k]; pire_val = val; }
    }
    return pire;
}

uint8_t room_cp_menace(const room_couperet *c)
{
    if (!c || c->phase != ROOM_CP_COURSE) return ROOM_CP_MAX_PLACES;

    const uint8_t camp = camp_le_plus_faible(c);
    if (camp >= ROOM_CP_MAX_PLACES) return ROOM_CP_MAX_PLACES;

    uint8_t choisi = ROOM_CP_MAX_PLACES;
    for (int i = 0; i < c->places; ++i) {
        const room_cp_place *p = &c->place[i];
        if (!p->occupee || !p->vivante || p->camp != camp) continue;
        if (choisi == ROOM_CP_MAX_PLACES) { choisi = (uint8_t)i; continue; }
        /* La VALEUR — points encaissés plus partie en cours. À égalité stricte,
         * l'indice de place le plus BAS : arbitraire, mais déterministe, et le
         * déterminisme est ce qui permet à l'arbitre et aux autres de voir le
         * même verdict. */
        if (room_cp_valeur(c, (uint8_t)i) < room_cp_valeur(c, choisi)) {
            choisi = (uint8_t)i;
        }
    }
    return choisi;
}

/* ==========================================================================
 * Le couperet
 * ========================================================================== */

static void conclure(room_couperet *c)
{
    /* Le camp vainqueur : le seul qui ait encore quelqu'un debout. S'il n'en
     * reste aucun — cas du plafond de manche avec huit spectres — c'est celui
     * qui a le plus de points. */
    uint8_t gagnant = ROOM_CP_MAX_PLACES;
    int camps_debout = 0;
    for (int i = 0; i < c->places; ++i) {
        const room_cp_place *p = &c->place[i];
        if (!p->occupee || !p->vivante) continue;
        bool deja = false;
        for (int k = 0; k < i; ++k) {
            if (c->place[k].occupee && c->place[k].vivante &&
                c->place[k].camp == p->camp) { deja = true; break; }
        }
        if (!deja) { camps_debout++; gagnant = p->camp; }
    }
    if (camps_debout != 1) {
        int32_t meilleur = 0;
        gagnant = ROOM_CP_MAX_PLACES;
        for (int i = 0; i < c->places; ++i) {
            if (!c->place[i].occupee) continue;
            const int32_t pts = room_cp_points_camp(c, c->place[i].camp);
            if (gagnant >= ROOM_CP_MAX_PLACES || pts > meilleur) {
                meilleur = pts; gagnant = c->place[i].camp;
            }
        }
    }
    c->vainqueur = gagnant;
    c->phase     = ROOM_CP_FINI;
    pousser(c, ROOM_CP_EVT_FIN, gagnant, 0, 0);
}

static void tomber(room_couperet *c)
{
    const uint8_t victime = room_cp_menace(c);
    if (victime >= ROOM_CP_MAX_PLACES) { conclure(c); return; }

    c->couperets++;
    room_cp_place *v = &c->place[victime];
    v->vivante  = false;
    v->jeu[0]   = '\0';
    v->depuis   = 0.0f;
    v->provisoire = 0;
    v->score_vu = 0;
    v->sortie_a = c->couperets;
    pousser(c, ROOM_CP_EVT_COUPERET, victime, 0, c->couperets);

    /*
     * LE FUSIBLE DU COUPERET. Un pour chaque place qui n'est pas celle qu'on
     * vient de sortir — vivants ET spectres.
     *
     * Aux vivants, c'est le revenu qui empêche un joueur enfermé dans une
     * partie de trois minutes d'arriver au bout sans un seul fusible.
     * Aux spectres, c'est ce qui les garde dangereux : un spectre sans revenu
     * dépense son magot en une minute et redevient un écran qui regarde, ce
     * qui est exactement le défaut que les spectres corrigent.
     *
     * La victime du couperet n'en reçoit pas : elle n'a ni survécu, ni été
     * spectre pendant celui-ci.
     */
    for (int i = 0; i < c->places; ++i) {
        if (!c->place[i].occupee || i == (int)victime) continue;
        c->place[i].fusibles++;
    }

    if (camp_le_plus_faible(c) >= ROOM_CP_MAX_PLACES) conclure(c);
}

void room_cp_avancer(room_couperet *c, float dt)
{
    if (!c || c->phase != ROOM_CP_COURSE) return;
    if (!(dt > 0.0f)) return;

    c->horloge += dt;

    for (int i = 0; i < c->places; ++i) {
        room_cp_place *p = &c->place[i];
        if (!p->occupee) continue;
        if (p->jeu[0] != '\0') {
            p->depuis += dt;
            /* Le revenu du temps. La tranche entière est versée et le RESTE est
             * gardé : sans ça, quatre parties de vingt-neuf secondes ne
             * rapporteraient rien du tout. */
            p->joue += dt;
            const float tranche = (float)ROOM_CP_SECONDES_PAR_FUSIBLE;
            while (p->joue >= tranche) { p->joue -= tranche; p->fusibles++; }
        }
        /* Le blindage et le leurre ne s'écoulent PAS : ce sont des charges
         * tenues jusqu'à usage. Le raisonnement est dans l'en-tête. */
        p->brouillage = maxf(0.0f, p->brouillage - dt);
        p->inversion  = maxf(0.0f, p->inversion  - dt);
    }

    /*
     * LE NOMBRE DE COUPERETS SE DÉDUIT DE L'HORLOGE, il ne se décompte pas.
     *
     * Un `prochain -= dt` accumulerait l'erreur d'arrondi différemment selon le
     * nombre d'appels, et deux machines qui avancent du même total en 60 et en
     * 120 appels finiraient par diverger d'un couperet — c'est-à-dire par ne
     * pas voir sortir le même joueur. Ici la seule grandeur accumulée est
     * l'horloge, et la même horloge donne le même compte.
     */
    const float periode = room_cp_periode();
    int32_t dus = (int32_t)(c->horloge / periode);
    while (c->couperets < dus && c->phase == ROOM_CP_COURSE) tomber(c);

    c->prochain = periode - fmodf(c->horloge, periode);

    if (c->phase == ROOM_CP_COURSE && c->horloge >= (float)ROOM_CP_MANCHE_MAX_S) {
        conclure(c);
    }
}

/* ==========================================================================
 * Les parties
 * ========================================================================== */

void room_cp_partie_debut(room_couperet *c, uint8_t place, const char *jeu, bool hard)
{
    if (!c || c->phase != ROOM_CP_COURSE) return;
    if (place >= c->places) return;
    room_cp_place *p = &c->place[place];
    if (!p->occupee || !p->vivante) return;

    copier(p->jeu, sizeof p->jeu, jeu);
    p->hard     = hard;
    p->depuis   = 0.0f;
    p->provisoire = 0;
    p->score_vu = 0;
}

void room_cp_avance(room_couperet *c, uint8_t place, int64_t score_courant)
{
    if (!c || place >= c->places) return;
    room_cp_place *p = &c->place[place];
    if (!p->occupee || !p->vivante || p->jeu[0] == '\0') return;
    p->score_vu   = (score_courant > 0) ? score_courant : 0;
    p->provisoire = room_cp_points_pour(p->jeu, p->hard, p->score_vu);
}

int32_t room_cp_partie_fin(room_couperet *c, uint8_t place, int64_t score)
{
    if (!c || c->phase != ROOM_CP_COURSE) return 0;
    if (place >= c->places) return 0;
    room_cp_place *p = &c->place[place];
    if (!p->occupee || !p->vivante || p->jeu[0] == '\0') return 0;

    const int32_t pts = room_cp_points_pour(p->jeu, p->hard, score);
    p->points += pts;
    p->parties++;
    p->jeu[0] = '\0';
    p->depuis = 0.0f;
    p->provisoire = 0;
    p->score_vu = 0;

    pousser(c, ROOM_CP_EVT_PARTIE, place, 0, pts);
    return pts;
}

/* ==========================================================================
 * Les actions
 * ========================================================================== */

/* Applique l'effet d'une action offensive, sans plus rien vérifier. */
static void appliquer(room_couperet *c, uint8_t cible, room_cp_action quoi)
{
    room_cp_place *v = &c->place[cible];
    const float duree = room_cp_action_duree(quoi);

    switch (quoi) {
    case ROOM_CP_BROUILLAGE:
        /* PROLONGE au lieu de s'ajouter : deux brouillages simultanés ne
         * brouillent pas deux fois, ils brouillent plus longtemps. */
        v->brouillage = maxf(v->brouillage, 0.0f) + duree;
        break;
    case ROOM_CP_INVERSION:
        v->inversion = maxf(v->inversion, 0.0f) + duree;
        break;
    case ROOM_CP_COUPURE:
        /*
         * LA BORNE S'ÉTEINT ET LA PARTIE EST PERDUE — zéro point, la durée est
         * jetée avec.
         *
         * C'est l'action la plus chère (4 fusibles) et la seule qui détruise du
         * travail plutôt que de le gêner. Elle existe parce que sans elle
         * l'engagé n'a aucun adversaire : les quatre autres attaques ne font
         * que réduire son score, or son score n'est pas ce qui le porte — c'est
         * la DURÉE, et rien d'autre ne peut la lui reprendre.
         *
         * Le prix se lit dans le revenu du temps : un fusible par trente
         * secondes de jeu, donc DEUX MINUTES passées à jouer pour annuler les
         * trois minutes d'un aplomb. C'est un bon échange pour l'attaquant, et
         * c'est voulu : c'est ce qui empêche l'engagement d'être gratuit.
         */
        v->jeu[0] = '\0';
        v->depuis = 0.0f;
        v->provisoire = 0;
        v->score_vu = 0;
        v->annulees++;
        break;
    default:
        break;
    }
}

bool room_cp_agir(room_couperet *c, uint8_t de, uint8_t vers, room_cp_action quoi)
{
    if (!c || c->phase != ROOM_CP_COURSE) return false;
    if (!action_valide(quoi)) return false;
    if (de >= c->places || vers >= c->places) return false;

    room_cp_place *a = &c->place[de];
    room_cp_place *b = &c->place[vers];
    if (!a->occupee || !b->occupee) return false;

    const bool offensive = room_cp_action_offensive(quoi);
    if (offensive) {
        /* On ne frappe ni son propre camp, ni un spectre : dans les deux cas ce
         * serait jeter ses fusibles, et un module qui laisse faire ça oblige
         * l'interface à réimplémenter la règle pour griser le bouton. */
        if (a->camp == b->camp) return false;
        if (!b->vivante) return false;
        /* On ne sabote pas une borne éteinte. Le raisonnement complet est
         * au-dessus de la déclaration : c'est cette ligne qui rend le blindage
         * utile face à plusieurs attaquants. */
        if (b->jeu[0] == '\0') return false;
    }

    /* On ne tient qu'une plaque et qu'un miroir : acheter le second est refusé
     * plutôt qu'empilé, sans quoi marteler la touche jetterait des fusibles. */
    if (quoi == ROOM_CP_BLINDAGE && b->blindage) return false;
    if (quoi == ROOM_CP_LEURRE   && b->leurre)   return false;

    const int32_t cout = room_cp_action_cout(quoi);
    if (a->fusibles < cout) return false;
    a->fusibles -= cout;
    pousser(c, ROOM_CP_EVT_ACTION, de, vers, (int32_t)quoi);

    if (!offensive) {
        switch (quoi) {
        case ROOM_CP_BLINDAGE: b->blindage = true; break;
        case ROOM_CP_LEURRE:   b->leurre   = true; break;
        case ROOM_CP_RELAIS:   b->fusibles  += 1; break;
        default: break;
        }
        return true;
    }

    /* L'ordre de résolution est écrit dans l'en-tête : leurre, puis blindage. */
    uint8_t cible = vers;
    if (b->leurre) {
        b->leurre = false;
        pousser(c, ROOM_CP_EVT_RENVOYE, de, vers, (int32_t)quoi);
        cible = de;
        /* Un leurre chez l'auteur ne renvoie PAS une seconde fois : deux
         * leurres face à face boucleraient, et une boucle dans une règle de jeu
         * est un gel. Le blindage de l'auteur, lui, joue encore. */
        if (c->place[cible].blindage) {
            c->place[cible].blindage = false;
            pousser(c, ROOM_CP_EVT_ABSORBE, de, cible, (int32_t)quoi);
            return true;
        }
        appliquer(c, cible, quoi);
        return true;
    }
    if (b->blindage) {
        b->blindage = false;
        pousser(c, ROOM_CP_EVT_ABSORBE, de, vers, (int32_t)quoi);
        return true;
    }
    appliquer(c, cible, quoi);
    return true;
}

/* ==========================================================================
 * Lecture
 * ========================================================================== */

int room_cp_classement(const room_couperet *c, uint8_t sortie[ROOM_CP_MAX_PLACES])
{
    if (!c || !sortie) return 0;
    int n = 0;
    for (int i = 0; i < c->places; ++i) if (c->place[i].occupee) sortie[n++] = (uint8_t)i;

    /* Tri par insertion : huit éléments, et il est STABLE — deux places
     * strictement à égalité gardent l'ordre des places, ce qui rend le
     * classement reproductible d'une image à l'autre. */
    for (int i = 1; i < n; ++i) {
        const uint8_t x = sortie[i];
        int j = i - 1;
        while (j >= 0) {
            const room_cp_place *pa = &c->place[sortie[j]];
            const room_cp_place *px = &c->place[x];
            /* Les vivants d'abord : un spectre ne court plus. */
            const bool avant = (px->vivante && !pa->vivante) ||
                               (px->vivante == pa->vivante &&
                                (px->points > pa->points ||
                                 (px->points == pa->points &&
                                  px->provisoire > pa->provisoire)));
            if (!avant) break;
            sortie[j + 1] = sortie[j];
            j--;
        }
        sortie[j + 1] = x;
    }
    return n;
}

int room_cp_classement_final(const room_couperet *c, uint8_t sortie[ROOM_CP_MAX_PLACES])
{
    if (!c || !sortie) return 0;
    int n = 0;
    for (int i = 0; i < c->places; ++i) if (c->place[i].occupee) sortie[n++] = (uint8_t)i;

    /* Même tri par insertion et même stabilité que `room_cp_classement` : huit
     * éléments, et deux places strictement à égalité gardent l'ordre des
     * places. La clé, elle, est la lame qui les a sorties. */
    for (int i = 1; i < n; ++i) {
        const uint8_t x = sortie[i];
        int j = i - 1;
        while (j >= 0) {
            const room_cp_place *pa = &c->place[sortie[j]];
            const room_cp_place *px = &c->place[x];
            /* `sortie_a` vaut -1 pour qui est encore debout : on le remplace
             * par une lame plus tardive que toutes celles qui sont tombées,
             * ce qui met le survivant en tête sans cas particulier. */
            const int32_t la = (pa->sortie_a >= 0) ? pa->sortie_a : c->couperets + 1;
            const int32_t lx = (px->sortie_a >= 0) ? px->sortie_a : c->couperets + 1;
            const bool avant = (lx > la) || (lx == la && px->points > pa->points);
            if (!avant) break;
            sortie[j + 1] = sortie[j];
            j--;
        }
        sortie[j + 1] = x;
    }
    return n;
}

bool room_cp_valide(const room_couperet *c)
{
    if (!c) return false;
    if (c->places < 2 || c->places > ROOM_CP_MAX_PLACES) return false;
    if (c->couperets < 0) return false;
    if (c->jrn_tete >= ROOM_CP_JOURNAL || c->jrn_queue >= ROOM_CP_JOURNAL) return false;

    for (int i = 0; i < ROOM_CP_MAX_PLACES; ++i) {
        const room_cp_place *p = &c->place[i];
        if (i >= c->places && p->occupee) return false;
        if (!p->occupee) continue;
        if (p->points < 0 || p->fusibles < 0 || p->parties < 0 || p->annulees < 0) return false;
        if (p->joue < 0.0f || p->joue >= (float)ROOM_CP_SECONDES_PAR_FUSIBLE) return false;
        if (p->camp >= ROOM_CP_MAX_PLACES) return false;
        if (!c->equipes && p->camp != (uint8_t)i) return false;
        if (p->vivante && p->sortie_a >= 0) return false;
        if (p->brouillage < 0.0f || p->inversion < 0.0f) return false;
        if (p->provisoire < 0) return false;
        /* Un provisoire sans partie en cours est le symptôme d'une coupure ou
         * d'un couperet mal nettoyé : la place serait défendue par une partie
         * qui n'existe plus. */
        if (p->jeu[0] == '\0' && p->provisoire != 0) return false;
        /* Un spectre ne joue pas : c'est l'invariant que `tomber` et
         * `room_cp_lever` doivent tenir, et le seul qui se casse en silence. */
        if (!p->vivante && p->jeu[0] != '\0') return false;
    }
    return true;
}
