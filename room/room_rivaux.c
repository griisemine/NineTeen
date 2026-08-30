/*
 * room_rivaux.c — les rivaux jouent.
 *
 * Le raisonnement complet est dans l'en-tête ; ce fichier ne fait que trois
 * choses, et la première est la seule qui compte : à chaque pas fixe, chaque
 * rival avance UNE VRAIE PARTIE d'un des huit jeux du dépôt. Les deux autres
 * sont le choix de la borne et la dépense des fusibles.
 *
 * Aucune horloge système, aucun fichier, aucune socket : le temps entre par
 * `room_rv_avancer`.
 */
#include "room_rivaux.h"

#include "games.h"
#include "ns_core.h"

#include <string.h>

/* ==========================================================================
 * Les deux tables de l'en-tête
 * ========================================================================== */

typedef struct rv_niveau_def {
    int         saut;           /* pour mille */
    const char *titre;
} rv_niveau_def;

#define RV_NIVEAU_ROW(cle, saut, titre) { (saut), titre },
static const rv_niveau_def g_niveaux[ROOM_RV_NIVEAU_COUNT] = {
    ROOM_RV_NIVEAUX(RV_NIVEAU_ROW)
};
#undef RV_NIVEAU_ROW

#define RV_CONDUITE_ROW(cle, titre) titre,
static const char *const g_conduites[ROOM_RV_CONDUITE_COUNT] = {
    ROOM_RV_CONDUITES(RV_CONDUITE_ROW)
};
#undef RV_CONDUITE_ROW

static bool niveau_valide(room_rv_niveau n)
{
    return (int)n >= 0 && (int)n < ROOM_RV_NIVEAU_COUNT;
}
static bool conduite_valide(room_rv_conduite d)
{
    return (int)d >= 0 && (int)d < ROOM_RV_CONDUITE_COUNT;
}

int room_rv_saut_pour_mille(room_rv_niveau n)
{
    return niveau_valide(n) ? g_niveaux[n].saut : 0;
}
const char *room_rv_niveau_titre(room_rv_niveau n)
{
    return niveau_valide(n) ? g_niveaux[n].titre : "";
}
const char *room_rv_conduite_titre(room_rv_conduite d)
{
    return conduite_valide(d) ? g_conduites[d] : "";
}

/* ==========================================================================
 * Le tirage
 * ==========================================================================
 *
 * splitmix64 : deux lignes, aucun état caché, et la même suite sur toutes les
 * machines parce qu'elle ne travaille que sur des entiers 64 bits. C'est déjà
 * la fonction que `tests/test_couperet.c` utilise pour espacer ses graines.
 */
static uint64_t tirer(uint64_t *etat)
{
    uint64_t z = (*etat += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* ==========================================================================
 * Les noms
 * ==========================================================================
 *
 * Des radicaux courts qu'on entend dans une salle, et des suffixes dont la
 * moitié sont vides — un salon où tout le monde porte un numéro se repère
 * autant qu'un salon où tout le monde s'appelle BOT.
 *
 * Aucun accent : ce sont les mêmes chaînes que les titres d'actions du
 * Couperet, qui passent par la même police de bandeau.
 */
static const char *const g_radicaux[] = {
    "Lou",   "Naim",  "Yanis", "Sofia", "Momo",  "Kenza", "Bibi",  "Tonio",
    "Zaza",  "Kevin", "Manu",  "Nadia", "Riko",  "Vince", "Jojo",  "Alix",
    "Sam",   "Theo",  "Lila",  "Bruno", "Elsa",  "Gus",   "Mina",  "Ferdi",
    "Ludo",  "Sabri", "Ines",  "Cricri"
};
#define RV_RADICAUX ((int)(sizeof g_radicaux / sizeof g_radicaux[0]))

static const char *const g_suffixes[] = {
    "", "", "", "", "", "77", "62", "2000", "13", "31"
};
#define RV_SUFFIXES ((int)(sizeof g_suffixes / sizeof g_suffixes[0]))

/* Vrai si ce pseudo est déjà assis dans le salon. Les places humaines comptent
 * : deux « Momo » dans le même bandeau seraient illisibles quel que soit celui
 * des deux qui respire. */
static bool pseudo_pris(const room_couperet *c, const char *nom)
{
    for (int i = 0; i < ROOM_CP_MAX_PLACES; ++i) {
        if (c->place[i].occupee && strcmp(c->place[i].pseudo, nom) == 0) return true;
    }
    return false;
}

static void nommer(uint64_t *alea, const room_couperet *c, uint8_t place,
                   char *sortie, size_t taille)
{
    /* 28 radicaux x 10 suffixes = 280 pseudos pour huit places : le rejet
     * s'arrête en pratique au premier tour. Le plafond d'essais est là pour que
     * la boucle se termine même si l'appelant a assis huit homonymes à la
     * main. */
    for (int essai = 0; essai < 64; ++essai) {
        const char *rad = g_radicaux[tirer(alea) % (uint64_t)RV_RADICAUX];
        const char *suf = g_suffixes[tirer(alea) % (uint64_t)RV_SUFFIXES];
        SDL_snprintf(sortie, taille, "%s%s", rad, suf);
        if (!pseudo_pris(c, sortie)) return;
    }
    SDL_snprintf(sortie, taille, "%s-%u",
                 g_radicaux[tirer(alea) % (uint64_t)RV_RADICAUX], (unsigned)place);
}

/* ==========================================================================
 * Le bloc d'état
 * ========================================================================== */

/* Un seul bloc par rival, à la taille du plus gros état du dépôt : réallouer à
 * chaque partie ferait une allocation toutes les dix secondes par place, pour
 * gagner les 30 Kio d'un seul jeu. */
static size_t taille_max_etat(void)
{
    size_t max = 0;
    for (int i = 0; i < ns_game_count(); ++i) {
        const ns_game_api *api = ns_game_at(i);
        if (api && api->state_size > max) max = api->state_size;
    }
    return max;
}

/* ==========================================================================
 * Le salon
 * ========================================================================== */

void room_rv_ouvrir(room_rivaux *r, uint64_t graine)
{
    if (!r) return;
    /*
     * OUVRIR NE LIBÈRE RIEN, et c'est délibéré : cette fonction reçoit le plus
     * souvent une structure de pile jamais initialisée, où les pointeurs qu'elle
     * verrait seraient du bruit. Elle ne peut pas distinguer ce bruit d'une
     * allocation réelle, donc elle ne le libère pas. Rouvrir un banc déjà ouvert
     * demande donc `room_rv_fermer` d'abord — c'est écrit au-dessus de la
     * déclaration.
     */
    const room_rivaux vide = { 0 };
    *r = vide;
    /* Une graine nulle donnerait le même salon à chaque ouverture, ce qui est
     * exactement ce qu'on veut en test et jamais ce qu'on veut en jeu. On la
     * garde telle quelle : c'est l'appelant qui sait laquelle des deux il
     * demande. */
    r->graine = graine;
}

void room_rv_fermer(room_rivaux *r)
{
    if (!r) return;
    for (int i = 0; i < ROOM_CP_MAX_PLACES; ++i) {
        ns_free(r->place[i].etat);
        r->place[i].etat = NULL;
        r->place[i].api  = NULL;
        r->place[i].tenue = false;
    }
}

bool room_rv_asseoir(room_rivaux *r, room_couperet *c, uint8_t place, uint8_t camp)
{
    if (!r || !c) return false;
    if (place >= ROOM_CP_MAX_PLACES) return false;
    if (r->place[place].tenue) return false;

    const size_t taille = taille_max_etat();
    if (taille == 0) return false;

    room_rival *v = &r->place[place];
    const room_rival neuf = { 0 };
    *v = neuf;

    /*
     * LA GRAINE D'UN RIVAL EST CELLE DU BANC MÊLÉE À SA PLACE, et pas un
     * compteur : deux rivaux assis dans un ordre différent sur les mêmes places
     * doivent être les mêmes joueurs, sans quoi rejouer une manche depuis un
     * journal ne rendrait pas la même salle.
     */
    v->graine = r->graine ^ ((uint64_t)(place + 1u) * 0x9E3779B97F4A7C15ull);
    v->alea   = v->graine;

    char nom[ROOM_CP_PSEUDO];
    nommer(&v->alea, c, place, nom, sizeof nom);

    if (!room_cp_asseoir(c, place, nom, camp)) return false;

    v->etat = ns_calloc(1, taille);
    if (!v->etat) {
        /* La place reste libre plutôt que d'être tenue par un rival qui ne
         * jouerait pas : un siège occupé par personne est pire qu'un siège
         * vide, parce que le couperet le compte. */
        room_cp_lever(c, place);
        return false;
    }

    v->niveau   = (uint8_t)(tirer(&v->alea) % (uint64_t)ROOM_RV_NIVEAU_COUNT);
    v->conduite = (uint8_t)(tirer(&v->alea) % (uint64_t)ROOM_RV_CONDUITE_COUNT);
    v->tenue    = true;
    v->pause    = 0.0f;   /* il insère au coup d'envoi, comme tout le monde */
    return true;
}

int room_rv_remplir(room_rivaux *r, room_couperet *c)
{
    if (!r || !c) return 0;
    int assis = 0;
    for (uint8_t i = 0; i < c->places; ++i) {
        if (c->place[i].occupee) continue;

        /* Le camp le moins peuplé des deux. Hors mode équipes le Couperet
         * ignore ce paramètre — le camp y EST la place. */
        uint8_t camp = 0;
        if (c->equipes) {
            int n0 = 0, n1 = 0;
            for (int k = 0; k < c->places; ++k) {
                if (!c->place[k].occupee) continue;
                if (c->place[k].camp == 0) n0++; else n1++;
            }
            camp = (n1 < n0) ? 1u : 0u;
        }
        if (room_rv_asseoir(r, c, i, camp)) assis++;
    }
    return assis;
}

void room_rv_regler(room_rivaux *r, uint8_t place, room_rv_niveau n, room_rv_conduite d)
{
    if (!r || place >= ROOM_CP_MAX_PLACES) return;
    if (!r->place[place].tenue) return;
    if (niveau_valide(n))   r->place[place].niveau   = (uint8_t)n;
    if (conduite_valide(d)) r->place[place].conduite = (uint8_t)d;
}

/* ==========================================================================
 * Le choix de la borne
 * ========================================================================== */

/* Vrai si quelqu'un joue DÉJÀ exactement cette borne. Un rival ne se met pas
 * derrière une machine occupée — c'est la règle d'une salle d'arcade, et c'est
 * accessoirement ce qui empêche quatre pressés de jouer le même démineur au
 * même instant devant un joueur qui regarde. */
static bool borne_occupee(const room_couperet *c, const char *jeu, bool hard)
{
    for (int i = 0; i < c->places; ++i) {
        const room_cp_place *p = &c->place[i];
        if (!p->occupee || !p->vivante || p->jeu[0] == '\0') continue;
        if (p->hard == hard && strcmp(p->jeu, jeu) == 0) return true;
    }
    return false;
}

/*
 * Le meilleur RENDEMENT AFFICHÉ sous la contrainte de la conduite.
 *
 * Rendement affiché = multiplicateur / durée médiane, les deux lus par les
 * fonctions publiques du Couperet. Le barème payant à peu près le même nombre
 * de tickets pour une partie médiane de n'importe quelle borne, ce rapport est
 * proportionnel aux points par seconde espérés — la constante de
 * proportionnalité étant commune à toutes les bornes, elle ne change aucun
 * classement et n'a pas à être connue ici.
 */
static bool choisir(const room_rivaux *r, const room_couperet *c, uint8_t place,
                    const ns_game_api **api_out, bool *hard_out, bool libre_seulement)
{
    const room_rival *v = &r->place[place];
    const float periode = room_cp_periode();

    const ns_game_api *choix = NULL;
    bool  choix_hard = false;
    float meilleur = -1.0f;

    /* Le repli de l'horloger : la borne la plus courte du tableau, celle qui
     * tient dans n'importe quelle fenêtre. */
    const ns_game_api *plus_courte = NULL;
    bool  courte_hard = false;
    float courte_duree = 0.0f;

    for (int i = 0; i < ns_game_count(); ++i) {
        const ns_game_api *api = ns_game_at(i);
        if (!api || !api->autopilot) continue;
        for (int h = 0; h < 2; ++h) {
            const bool hard = (h != 0);
            const float duree = room_cp_duree_mediane(api->id, hard);
            if (!(duree > 0.0f)) continue;   /* borne sans tarif : on n'y touche pas */

            if (!plus_courte || duree < courte_duree) {
                plus_courte = api; courte_hard = hard; courte_duree = duree;
            }
            if (libre_seulement && borne_occupee(c, api->id, hard)) continue;

            switch ((room_rv_conduite)v->conduite) {
            case ROOM_RV_PRESSE:
                if (duree >= periode) continue;
                break;
            case ROOM_RV_HORLOGER:
                if (duree > c->prochain) continue;
                break;
            case ROOM_RV_ENGAGE:
            default:
                break;
            }

            const float rendement = room_cp_multiplicateur(api->id, hard) / duree;
            if (rendement > meilleur) {
                meilleur = rendement; choix = api; choix_hard = hard;
            }
        }
    }

    if (!choix && (room_rv_conduite)v->conduite == ROOM_RV_HORLOGER) {
        choix = plus_courte; choix_hard = courte_hard;
    }
    if (!choix) return false;
    *api_out = choix;
    *hard_out = choix_hard;
    return true;
}

/* La pause entre deux parties : de une à trois secondes.
 *
 * ELLE EST CHOISIE ET NON MESURÉE, et il faut le dire : rien dans ce dépôt ne
 * chronomètre le temps qu'un humain met à lever les yeux et à réinsérer. Ce
 * qu'elle coûte, en revanche, se calcule — deux secondes sur une borne de
 * vingt-cinq en retirent 7 % de rendement, et c'est autant de moins pour les
 * rivaux que pour personne. À zéro, un rival relancerait dans la même image que
 * sa mort, ce qu'aucune main ne fait. */
static float pause_tiree(room_rival *v)
{
    return 1.0f + (float)(tirer(&v->alea) % 201u) * 0.01f;
}

static void commencer(room_rivaux *r, room_couperet *c, uint8_t place)
{
    room_rival *v = &r->place[place];
    const ns_game_api *api = NULL;
    bool hard = false;

    if (!choisir(r, c, place, &api, &hard, true) &&
        !choisir(r, c, place, &api, &hard, false)) {
        v->pause = pause_tiree(v);
        return;
    }

    /*
     * LE BLOC EST REMIS À ZÉRO AVANT CHAQUE `reset`, et ce n'est pas de la
     * prudence gratuite : il est dimensionné pour le plus gros jeu et sert donc
     * à tous. Passer de snake à envol laisserait 30 Kio de l'état précédent
     * au-delà de ce qu'envol initialise — invisible tant que personne ne lit
     * plus loin, et fatal le jour où `ns_game_state_hash` comparera deux
     * machines qui n'ont pas joué les mêmes jeux dans le même ordre.
     */
    memset(v->etat, 0, taille_max_etat());
    api->reset(v->etat, tirer(&v->alea), hard);
    /* Le record est remis à zéro comme le fait le vivier de test : un record
     * traînant d'une partie précédente changerait ce que le jeu affiche, et
     * pour certains ce qu'il joue. */
    if (api->set_best) api->set_best(v->etat, 0);

    v->api      = api;
    v->hard     = hard;
    v->score_vu = 0;
    room_cp_partie_debut(c, place, api->id, hard);
}

/* ==========================================================================
 * Un pas de jeu
 * ========================================================================== */

/*
 * Le seuil de saut EFFECTIF, brouillage et inversion compris.
 *
 * Un joueur automatique n'a ni dalle ni manche : les deux actions les moins
 * chères du Couperet n'auraient donc aucun effet sur sept places sur huit. La
 * convention retenue — et c'en est une — est de rabattre le rival gêné au taux
 * du DÉBUTANT tant que l'effet dure, soit -51 % de rendement d'après la table
 * de l'en-tête. Le raisonnement complet y est aussi.
 */
static int seuil_saut(const room_rival *v, const room_cp_place *p)
{
    const int base = room_rv_saut_pour_mille((room_rv_niveau)v->niveau);
    if (p->brouillage > 0.0f || p->inversion > 0.0f) {
        const int gene = room_rv_saut_pour_mille(ROOM_RV_DEBUTANT);
        return (gene > base) ? gene : base;
    }
    return base;
}

static void un_pas(room_rivaux *r, room_couperet *c, uint8_t place, float pas)
{
    room_rival *v = &r->place[place];
    if (!v->tenue || !v->etat) return;
    const room_cp_place *p = &c->place[place];
    if (!p->occupee) return;

    /*
     * DEUX FAÇONS DE PERDRE SA PARTIE SANS ÊTRE MORT : la coupure a éteint la
     * borne, ou le couperet a fait de nous un spectre. Dans les deux cas le
     * Couperet a déjà tout jeté de son côté (`p->jeu[0]` est vide) et il n'y a
     * rien à encaisser — s'obstiner à jouer l'état laisserait un rival pousser
     * des scores sur une borne éteinte.
     */
    if (v->api && (!p->vivante || p->jeu[0] == '\0')) {
        v->api = NULL;
        v->score_vu = 0;
        v->pause = pause_tiree(v);
    }
    if (!p->vivante) return;   /* un spectre ne joue plus ; il dépense encore */

    if (!v->api) {
        v->pause -= pas;
        if (v->pause <= 0.0f) commencer(r, c, place);
        return;
    }

    /*
     * Le tirage a lieu à CHAQUE pas joué, même pour un chevronné dont le seuil
     * est nul. C'est ce qui rend la suite indépendante du niveau : deux rivaux
     * de niveaux différents sur la même graine voient la même suite de nombres,
     * et la seule chose qui les sépare est le seuil auquel on la compare.
     */
    const int seuil = seuil_saut(v, p);
    const int tir = (int)(tirer(&v->alea) % 1000u);

    if (tir >= seuil && v->api->autopilot) (void)v->api->autopilot(v->etat);
    v->api->tick(v->etat, pas);

    /* `games.h` : les drapeaux d'événements se CONSOMMENT. Ne pas les lire les
     * laisserait levés jusqu'à la prochaine lecture, et le premier appelant qui
     * regarderait verrait la mort d'une partie d'il y a dix secondes. */
    ns_game_events ev;
    memset(&ev, 0, sizeof ev);
    v->api->events(v->etat, &ev);

    const int64_t score = (int64_t)v->api->score(v->etat);
    if (score != v->score_vu) {
        v->score_vu = score;
        room_cp_avance(c, place, score);
    }

    float mort = 0.0f;
    if (v->api->dead(v->etat, &mort)) {
        (void)room_cp_partie_fin(c, place, score);
        v->parties++;
        v->api = NULL;
        v->score_vu = 0;
        v->pause = pause_tiree(v);
    }
}

/* ==========================================================================
 * La dépense
 * ========================================================================== */

/* La place vivante qui vaut le plus devant le couperet, `ROOM_CP_MAX_PLACES`
 * s'il n'y en a aucune. C'est le meneur : celui que tout le monde vise, et donc
 * celui qui a besoin d'un leurre plutôt que d'une plaque. */
static uint8_t meneur_vivant(const room_couperet *c)
{
    uint8_t choisi = ROOM_CP_MAX_PLACES;
    int32_t mieux = -1;
    for (int i = 0; i < c->places; ++i) {
        if (!c->place[i].occupee || !c->place[i].vivante) continue;
        const int32_t val = room_cp_valeur(c, (uint8_t)i);
        if (val > mieux) { mieux = val; choisi = (uint8_t)i; }
    }
    return choisi;
}

static void depenser(room_rivaux *r, room_couperet *c, uint8_t place)
{
    room_rival *v = &r->place[place];
    if (!v->tenue) return;
    const room_cp_place *moi = &c->place[place];
    if (!moi->occupee) return;

    const uint8_t meneur = meneur_vivant(c);

    /* 1. SE COUVRIR — une seule fois, et seulement quand on a quelque chose à
     *    perdre : la plaque et le miroir se tiennent jusqu'à usage, donc les
     *    acheter est une dépense ponctuelle et non un abonnement. */
    if (moi->vivante && moi->jeu[0] != '\0' && moi->provisoire > 0) {
        if (meneur == place && !moi->leurre &&
            moi->fusibles >= room_cp_action_cout(ROOM_CP_LEURRE)) {
            (void)room_cp_agir(c, place, place, ROOM_CP_LEURRE);
            return;
        }
        if (!moi->blindage && moi->fusibles >= room_cp_action_cout(ROOM_CP_BLINDAGE)) {
            (void)room_cp_agir(c, place, place, ROOM_CP_BLINDAGE);
            return;
        }
    }

    /*
     * 2. COUVRIR LE PORTEUR, en équipes seulement.
     *
     * La réserve n'est gardée que par un VIVANT : lui a une plaque à s'offrir,
     * et se dépouiller pour son porteur ferait perdre deux places au lieu
     * d'une. Un spectre, lui, ne peut plus rien être protégé — aucune coupure
     * ne lui prend rien — donc il n'a rien à réserver.
     */
    const int32_t reserve = moi->vivante ? room_cp_action_cout(ROOM_CP_BLINDAGE) : 0;
    if (c->equipes &&
        moi->fusibles >= room_cp_action_cout(ROOM_CP_BLINDAGE) + reserve) {
        for (int k = 0; k < c->places; ++k) {
            const room_cp_place *ami = &c->place[k];
            if (k == (int)place || !ami->occupee || !ami->vivante) continue;
            if (ami->camp != moi->camp || ami->jeu[0] == '\0' || ami->blindage) continue;
            if (ami->provisoire <= moi->provisoire) continue;
            (void)room_cp_agir(c, place, (uint8_t)k, ROOM_CP_BLINDAGE);
            return;
        }
    }

    /* 3. LA CIBLE : le meilleur adversaire qui joue, et SEULEMENT s'il nous
     *    devance. Frapper celui qu'on précède déjà ne rapproche de rien, et
     *    c'est ce qui faisait pleuvoir les coupures sur un joueur en
     *    difficulté — le chiffre mesuré est dans l'en-tête. */
    uint8_t cible = ROOM_CP_MAX_PLACES;
    int32_t mieux = room_cp_valeur(c, place);
    for (int k = 0; k < c->places; ++k) {
        const room_cp_place *lui = &c->place[k];
        if (k == (int)place || !lui->occupee || !lui->vivante) continue;
        if (lui->camp == moi->camp || lui->jeu[0] == '\0') continue;
        const int32_t val = room_cp_valeur(c, (uint8_t)k);
        if (val > mieux) { mieux = val; cible = (uint8_t)k; }
    }

    /* 4. FRAPPER, dans la fenêtre d'avant-lame et là seulement. Une borne
     *    éteinte trente secondes avant le verdict se rallume ; éteinte trois
     *    secondes avant, elle retire sa défense au moment où elle compte. */
    if (c->prochain <= ROOM_RV_FENETRE_S) {
        if (cible < ROOM_CP_MAX_PLACES &&
            moi->fusibles >= room_cp_action_cout(ROOM_CP_COUPURE) &&
            room_cp_agir(c, place, cible, ROOM_CP_COUPURE)) {
            v->coupures++;
        }
        return;
    }

    /*
     * 5. LE SPECTRE FINANCE — en équipes, hors fenêtre, et jusqu'à la PARITÉ.
     *
     * Les deux camps gagnent le même fusible à chaque lame, mais seul celui qui
     * joue gagne aussi au temps de jeu : un fusible arrive donc au prix d'une
     * coupure bien plus vite dans la main d'un vivant que dans celle d'un
     * spectre. Le spectre donne tant qu'il en a plus que son porteur, et
     * s'arrête à égalité — il ne se dépouille pas, il avance au même rythme que
     * lui, et les deux atteignent les quatre fusibles ensemble au lieu que l'un
     * y arrive seul.
     *
     * Hors équipes le relais n'a aucun sens : donner un fusible à un adversaire,
     * c'est payer pour se faire couper. C'est le seul endroit du mode où
     * l'action existe pour un rival, et c'est pour ça qu'elle existe.
     */
    if (c->equipes && !moi->vivante &&
        moi->fusibles >= room_cp_action_cout(ROOM_CP_RELAIS)) {
        for (int k = 0; k < c->places; ++k) {
            const room_cp_place *ami = &c->place[k];
            if (k == (int)place || !ami->occupee || !ami->vivante) continue;
            if (ami->camp != moi->camp || ami->jeu[0] == '\0') continue;
            if (ami->fusibles >= moi->fusibles) continue;
            (void)room_cp_agir(c, place, (uint8_t)k, ROOM_CP_RELAIS);
            return;
        }
    }

    /* 6. LE SURPLUS. Hors fenêtre, un rival qui a de quoi couper ET davantage
     *    brouille le meneur plutôt que de dormir dessus. Contre une cible
     *    blindée le brouillage est absorbé — un fusible qui en efface deux, ce
     *    qui reste le meilleur échange du tableau.
     *
     *    L'INVERSION N'EST JAMAIS ACHETÉE, et c'est un choix qui se chiffre :
     *    elle coûte deux fusibles là où le brouillage en coûte un, et sur un
     *    rival les deux ont exactement le même effet — le taux de saut du
     *    débutant, faute de manche à inverser pour un joueur automatique.
     *    Payer double pour le même effet serait un défaut, pas une nuance.
     *    Contre un humain c'est probablement faux ; personne n'a mesuré ce que
     *    coûte un manche inversé à une main, et tant que ce n'est pas mesuré ce
     *    module ne fait pas semblant de le savoir. */
    if (cible < ROOM_CP_MAX_PLACES &&
        moi->fusibles >= room_cp_action_cout(ROOM_CP_COUPURE) +
                         room_cp_action_cout(ROOM_CP_BROUILLAGE)) {
        (void)room_cp_agir(c, place, cible, ROOM_CP_BROUILLAGE);
    }
}

/* ==========================================================================
 * L'avance
 * ========================================================================== */

/* Une décision de dépense tous les huit pas, soit quinze fois par seconde.
 * À chaque pas, ce serait cent vingt évaluations par seconde et par rival pour
 * un état qui ne peut pas changer huit fois entre deux images ; jamais, ce
 * serait un rival qui laisse passer la fenêtre d'avant-lame. */
#define RV_DECISION 8

void room_rv_avancer(room_rivaux *r, room_couperet *c, float dt)
{
    if (!r || !c) return;
    if (c->phase != ROOM_CP_COURSE) return;
    if (!(dt > 0.0f)) return;   /* attrape aussi les NaN */

    const float pas = 1.0f / (float)NS_DEFAULT_TICK_HZ;
    r->horloge += dt;

    /*
     * LE NOMBRE DE PAS SE DÉDUIT DE L'HORLOGE, il ne se décompte pas — même
     * raisonnement que `room_cp_avancer` pour ses lames, et pour la même
     * raison : deux machines qui avancent du même total en 60 et en 120 appels
     * doivent jouer le même nombre de pas, sinon les rivaux divergent et la
     * manche rejouée ne rend pas le même classement.
     */
    const int64_t dus = (int64_t)((double)r->horloge * NS_DEFAULT_TICK_HZ);
    while (r->pas < dus && c->phase == ROOM_CP_COURSE) {
        r->pas++;
        for (uint8_t i = 0; i < c->places; ++i) un_pas(r, c, i, pas);
        if ((r->pas % RV_DECISION) == 0) {
            for (uint8_t i = 0; i < c->places; ++i) depenser(r, c, i);
        }
    }
}

/* ==========================================================================
 * Lecture
 * ========================================================================== */

bool room_rv_tenue(const room_rivaux *r, uint8_t place)
{
    if (!r || place >= ROOM_CP_MAX_PLACES) return false;
    return r->place[place].tenue;
}

room_rv_niveau room_rv_niveau_de(const room_rivaux *r, uint8_t place)
{
    if (!room_rv_tenue(r, place)) return ROOM_RV_CHEVRONNE;
    return (room_rv_niveau)r->place[place].niveau;
}

room_rv_conduite room_rv_conduite_de(const room_rivaux *r, uint8_t place)
{
    if (!room_rv_tenue(r, place)) return ROOM_RV_PRESSE;
    return (room_rv_conduite)r->place[place].conduite;
}

const struct ns_game_api *room_rv_api(const room_rivaux *r, uint8_t place)
{
    if (!room_rv_tenue(r, place)) return NULL;
    return r->place[place].api;
}

const void *room_rv_etat(const room_rivaux *r, uint8_t place)
{
    if (!room_rv_tenue(r, place)) return NULL;
    return r->place[place].api ? r->place[place].etat : NULL;
}
