/* room_economie.c — voir room_economie.h pour le raisonnement, et
 * room_bareme.h pour les chiffres et les mesures qui les fondent. */
#include "room_economie.h"

#include "ns_core.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <time.h>

/* ==========================================================================
 * La table du barème, dépliée depuis la liste unique
 * ========================================================================== */

typedef struct eco_ligne {
    const char *jeu;
    int32_t     mediane[2];    /* [0] normal, [1] difficile */
    int32_t     diviseur[2];
} eco_ligne;

#define ECO_LIGNE(id, mn, dn, mh, dh) { #id, { (mn), (mh) }, { (dn), (dh) } },
static const eco_ligne g_bareme[] = { ROOM_ECO_BAREME(ECO_LIGNE) };
#undef ECO_LIGNE

static const eco_ligne *ligne_de(const char *jeu)
{
    if (!jeu || !*jeu) return NULL;
    for (size_t i = 0; i < SDL_arraysize(g_bareme); ++i) {
        if (SDL_strcasecmp(g_bareme[i].jeu, jeu) == 0) return &g_bareme[i];
    }
    return NULL;
}

int32_t room_eco_diviseur(const char *jeu, bool hard)
{
    const eco_ligne *l = ligne_de(jeu);
    return l ? l->diviseur[hard ? 1 : 0] : 0;
}

int32_t room_eco_mediane(const char *jeu, bool hard)
{
    const eco_ligne *l = ligne_de(jeu);
    return l ? l->mediane[hard ? 1 : 0] : 0;
}

/* ==========================================================================
 * La table des lots
 * ========================================================================== */

typedef struct eco_lot_def {
    int32_t     prix;
    const char *titre;
    const char *quoi;
} eco_lot_def;

#define ECO_LOT(cle, prix, titre, quoi) { (prix), (titre), (quoi) },
static const eco_lot_def g_lots[ROOM_ECO_LOT_COUNT] = { ROOM_ECO_LOTS(ECO_LOT) };
#undef ECO_LOT

static bool lot_valide(room_eco_lot lot)
{
    return lot >= 0 && lot < ROOM_ECO_LOT_COUNT;
}

const char *room_eco_lot_titre(room_eco_lot lot)
{
    return lot_valide(lot) ? g_lots[lot].titre : "";
}

const char *room_eco_lot_quoi(room_eco_lot lot)
{
    return lot_valide(lot) ? g_lots[lot].quoi : "";
}

int32_t room_eco_lot_prix(room_eco_lot lot)
{
    return lot_valide(lot) ? g_lots[lot].prix : 0;
}

/* ==========================================================================
 * Le temps
 * ========================================================================== */

static int64_t g_horloge;      /* 0 = horloge système */

void room_eco_set_horloge(int64_t epoch_secondes)
{
    g_horloge = epoch_secondes;
}

int64_t room_eco_jour_de(int64_t epoch_secondes)
{
    /*
     * Division EUCLIDIENNE, pas la division du C.
     *
     * `-1 / 86400` vaut 0 en C — la division entière tronque vers zéro — donc
     * la dernière seconde de 1969 tomberait le même jour que la première de
     * 1970. Le défaut ne se voit qu'avant l'epoch, ce qui n'arrive jamais en
     * jeu, mais il se voit tout de suite dans un test qui recule l'horloge, et
     * une règle de date qui n'est juste que d'un côté de zéro n'est pas une
     * règle de date.
     */
    const int64_t jour = 86400;
    int64_t q = epoch_secondes / jour;
    if (epoch_secondes % jour < 0) q -= 1;
    return q;
}

int64_t room_eco_jour(void)
{
    const int64_t maintenant = g_horloge ? g_horloge : (int64_t)time(NULL);
    return room_eco_jour_de(maintenant);
}

/* ==========================================================================
 * Le tournoi du jour
 * ========================================================================== */

/*
 * SplitMix64. Un mélangeur, pas un générateur : on lui donne un compteur
 * (ici le jour et le jeu) et il rend une valeur qui n'a plus de structure.
 *
 * Il est employé plutôt qu'un `rand()` ensemencé parce que la propriété
 * demandée n'est pas « du hasard » mais « la MÊME valeur partout » : deux
 * machines, deux systèmes, deux versions de la bibliothèque C doivent tirer la
 * même partie le même jour. `rand()` ne le garantit sur aucune des trois.
 */
static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t room_eco_graine_tournoi(int64_t jour, const char *jeu)
{
    /* Le nom du jeu entre par une empreinte FNV-1a plutôt que par son index :
     * un index dépendrait de l'ordre de `ns_game_at()`, et réordonner la table
     * des jeux changerait alors la partie du jour sans que personne ne l'ait
     * demandé. Le nom, lui, est ce que la borne déclare. */
    uint64_t h = 1469598103934665603ull;
    for (const char *p = jeu ? jeu : ""; *p; ++p) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    return splitmix64((uint64_t)jour * 0x100000001B3ull ^ h);
}

uint64_t room_eco_graine_du_jour(const char *jeu)
{
    return room_eco_graine_tournoi(room_eco_jour(), jeu);
}

/* ==========================================================================
 * Le barème appliqué
 * ========================================================================== */

int32_t room_eco_tickets_pour(const char *jeu, bool hard, int64_t score, int32_t serie)
{
    const int32_t div = room_eco_diviseur(jeu, hard);
    if (div <= 0) return 0;          /* jeu inconnu : rien, plutôt que n'importe quoi */
    if (score < 0) score = 0;        /* un score négatif ne peut pas venir d'une partie */

    int64_t t = score / div;

    /* La prime du régime difficile, en entier : `* 125 / 100` et non `* 1.25f`.
     * Un barème en virgule flottante donnerait un ticket de plus ou de moins
     * selon la machine, et deux joueurs du même tournoi n'auraient pas le même
     * solde pour la même partie. */
    if (hard) t = t * (100 + ROOM_ECO_PRIME_DIFFICILE_PCT) / 100;

    /* Le plafond porte sur ce que le SCORE rapporte, pas sur la série : sans
     * quoi un joueur au plafond cesserait de voir sa série payer, ce qui est
     * exactement le moment où elle doit se voir. */
    const int64_t plafond = (int64_t)ROOM_ECO_CIBLE_TICKETS * ROOM_ECO_PLAFOND_PARTIE;
    if (t > plafond) t = plafond;

    if (serie > ROOM_ECO_SERIE_MAX) serie = ROOM_ECO_SERIE_MAX;
    if (serie > 0) t += serie;

    return (int32_t)t;
}

/* ==========================================================================
 * Le portefeuille
 * ========================================================================== */

void room_eco_reset(room_eco *e)
{
    if (!e) return;
    SDL_zerop(e);
}

bool room_eco_valide(const room_eco *e)
{
    if (!e) return false;
    if (e->jetons < 0 || e->tickets < 0) return false;
    if (e->serie < 0 || e->serie_record < 0) return false;
    if (e->parties < 0 || e->tickets_gagnes < 0) return false;
    if (e->mise < 0) return false;
    /* Une mise sans jeu ne peut pas se rejouer : le quitte ou double n'aurait
     * rien à relancer, et les tickets resteraient sur la table pour toujours. */
    if (e->mise > 0 && !e->mise_jeu[0]) return false;
    if (e->serie > e->serie_record) return false;
    return true;
}

int32_t room_eco_monnayeur(room_eco *e)
{
    if (!e) return 0;
    if (e->jetons >= ROOM_ECO_PLANCHER_ACCUEIL) return 0;
    const int32_t rendu = ROOM_ECO_PLANCHER_ACCUEIL - e->jetons;
    e->jetons = ROOM_ECO_PLANCHER_ACCUEIL;
    return rendu;
}

int32_t room_eco_changer(room_eco *e, int32_t jetons_voulus)
{
    if (!e || jetons_voulus <= 0) return 0;
    const int32_t possible = e->tickets / ROOM_ECO_TICKETS_PAR_JETON;
    const int32_t n = (jetons_voulus < possible) ? jetons_voulus : possible;
    if (n <= 0) return 0;
    e->tickets -= n * ROOM_ECO_TICKETS_PAR_JETON;
    e->jetons  += n;
    return n;
}

bool room_eco_inserer(room_eco *e)
{
    if (!e || e->jetons < ROOM_ECO_COUT_PARTIE) return false;
    e->jetons -= ROOM_ECO_COUT_PARTIE;
    return true;
}

int32_t room_eco_fin_partie(room_eco *e, const char *jeu, bool hard, int64_t score)
{
    if (!e) return 0;

    /*
     * LA SÉRIE se met à jour AVANT le calcul, et c'est ce qui fait que la
     * première partie d'un nouveau jour paie déjà le bonus du jour. La régler
     * après aurait rendu la série visible avec un jour de retard — un défaut
     * muet, qu'on aurait pris pour une avarice du barème.
     */
    const int64_t aujourdhui = room_eco_jour();
    if (e->dernier_jour == 0 || aujourdhui != e->dernier_jour) {
        if (e->dernier_jour != 0 && aujourdhui == e->dernier_jour + 1) {
            e->serie += 1;
        } else {
            /* Un jour sauté rompt la série ; une horloge reculée aussi, et
             * c'est voulu : on ne récompense pas un retour en arrière. */
            e->serie = 1;
        }
        e->dernier_jour = aujourdhui;
    }
    if (e->serie < 1) e->serie = 1;
    if (e->serie > e->serie_record) e->serie_record = e->serie;

    const int32_t t = room_eco_tickets_pour(jeu, hard, score, e->serie);

    /* La mise remplace ce qui traînait : une partie non encaissée dont on
     * lance une autre est un refus implicite, et refuser verse. */
    if (e->mise > 0) (void)room_eco_encaisser(e);

    e->mise = t;
    e->mise_hard = hard;
    e->mise_score = (score < 0) ? 0 : (score > INT32_MAX ? INT32_MAX : (int32_t)score);
    SDL_strlcpy(e->mise_jeu, (jeu && *jeu) ? jeu : "", sizeof e->mise_jeu);
    e->parties += 1;

    /* Une partie sur un jeu inconnu ne rapporte rien et ne se rejoue pas : sans
     * ce garde-fou, la mise resterait posée sans que rien puisse la lever. */
    if (t == 0 || !e->mise_jeu[0]) {
        e->mise = 0;
        e->mise_jeu[0] = '\0';
        return 0;
    }
    return t;
}

int32_t room_eco_encaisser(room_eco *e)
{
    if (!e || e->mise <= 0) return 0;
    const int32_t t = e->mise;
    e->tickets += t;
    e->tickets_gagnes += t;
    e->mise = 0;
    e->mise_score = 0;
    e->mise_jeu[0] = '\0';
    return t;
}

int32_t room_eco_mise_a_battre(const room_eco *e)
{
    return (e && e->mise > 0) ? e->mise_score : 0;
}

int32_t room_eco_doubler(room_eco *e, int64_t score)
{
    if (!e || e->mise <= 0) return 0;

    const bool gagne = (score > (int64_t)e->mise_score);
    int32_t verse = 0;
    if (gagne) {
        /* Le double, borné : `mise` vient d'un fichier qu'on ne contrôle pas,
         * et deux fois INT32_MAX déborde. */
        verse = (e->mise > INT32_MAX / 2) ? INT32_MAX : e->mise * 2;
        e->tickets += verse;
        e->tickets_gagnes += verse;
    }
    /* Perdue ou gagnée, la mise est levée : on ne rejoue pas la même deux fois. */
    e->mise = 0;
    e->mise_score = 0;
    e->mise_jeu[0] = '\0';
    return verse;
}

/* ==========================================================================
 * La vitrine
 * ========================================================================== */

bool room_eco_lot_acquis(const room_eco *e, room_eco_lot lot)
{
    if (!e || !lot_valide(lot)) return false;
    return (e->lots & (1u << (unsigned)lot)) != 0u;
}

bool room_eco_acheter(room_eco *e, room_eco_lot lot)
{
    if (!e || !lot_valide(lot)) return false;
    if (room_eco_lot_acquis(e, lot)) return false;
    const int32_t prix = g_lots[lot].prix;
    if (e->tickets < prix) return false;      /* rien n'est débité */
    e->tickets -= prix;
    e->lots |= (1u << (unsigned)lot);
    return true;
}

room_eco_lot room_eco_lot_en_vue(const room_eco *e)
{
    if (!e) return ROOM_ECO_LOT_COUNT;
    /* Le plus cher qu'on puisse s'offrir tout de suite ; à défaut, le moins
     * cher qui manque. Dans cet ordre, parce qu'une vitrine qui montrerait
     * d'abord ce qu'on ne peut pas avoir ne donne envie de rien. */
    room_eco_lot offrable = ROOM_ECO_LOT_COUNT, manquant = ROOM_ECO_LOT_COUNT;
    for (int i = 0; i < ROOM_ECO_LOT_COUNT; ++i) {
        const room_eco_lot l = (room_eco_lot)i;
        if (room_eco_lot_acquis(e, l)) continue;
        if (e->tickets >= g_lots[i].prix) {
            if (offrable == ROOM_ECO_LOT_COUNT || g_lots[i].prix > g_lots[offrable].prix) {
                offrable = l;
            }
        } else if (manquant == ROOM_ECO_LOT_COUNT || g_lots[i].prix < g_lots[manquant].prix) {
            manquant = l;
        }
    }
    return (offrable != ROOM_ECO_LOT_COUNT) ? offrable : manquant;
}

/* ==========================================================================
 * Persistance
 * ==========================================================================
 *
 * Format : une ligne par champ, `clé=valeur`. Même discipline que `ns_scores` —
 * une ligne abîmée est sautée, les autres survivent — et même écriture
 * atomique par temporaire puis renommage.
 */

#define ECO_VERSION "v1"

static char g_chemin[1024];
static char g_surcharge[1024];

const char *room_eco_chemin(void)
{
    if (g_surcharge[0]) return g_surcharge;
    if (!g_chemin[0]) {
        const char *dir = ns_path_user_dir();
        SDL_snprintf(g_chemin, sizeof g_chemin, "%sportefeuille.txt", dir ? dir : "");
    }
    return g_chemin;
}

void room_eco_set_chemin(const char *chemin)
{
    if (chemin && *chemin) SDL_strlcpy(g_surcharge, chemin, sizeof g_surcharge);
    else                   g_surcharge[0] = '\0';
}

/* Un entier borné : ce qui vient du fichier n'est pas digne de confiance, et un
 * solde à quatre milliards lu tel quel casserait toutes les additions ensuite. */
static int32_t borne32(long long v, int32_t min, int32_t max)
{
    if (v < (long long)min) return min;
    if (v > (long long)max) return max;
    return (int32_t)v;
}

void room_eco_charger(room_eco *e)
{
    if (!e) return;
    room_eco_reset(e);

    const char *chemin = room_eco_chemin();
    size_t taille = 0;
    void *data = SDL_LoadFile(chemin, &taille);
    if (!data) return;      /* premier lancement : ce n'est pas une erreur */

    char *texte = (char *)data;
    uint32_t ligne_no = 0, mauvaises = 0;
    char *save = NULL;
    for (char *ligne = SDL_strtok_r(texte, "\n", &save); ligne;
         ligne = SDL_strtok_r(NULL, "\n", &save)) {
        ligne_no++;
        while (*ligne == '\r' || *ligne == ' ') ligne++;
        if (!*ligne) continue;

        if (ligne_no == 1) {
            if (SDL_strncmp(ligne, ECO_VERSION, SDL_strlen(ECO_VERSION)) != 0) {
                NS_WARN("portefeuille : « %s » d'une version inconnue, on repart de zéro",
                        chemin);
                break;
            }
            continue;
        }

        char cle[32] = {0}, val[64] = {0};
        if (SDL_sscanf(ligne, "%31[^=]=%63[^\n]", cle, val) != 2) { mauvaises++; continue; }

        /* `strtoll` plutôt que `%lld` : il dit où il s'est arrêté, donc une
         * valeur non numérique se distingue d'un zéro écrit exprès. */
        char *fin = NULL;
        const long long n = SDL_strtoll(val, &fin, 10);
        const bool numerique = (fin && fin != val);

        if      (SDL_strcmp(cle, "jetons") == 0 && numerique)  e->jetons = borne32(n, 0, 1000000);
        else if (SDL_strcmp(cle, "tickets") == 0 && numerique) e->tickets = borne32(n, 0, 100000000);
        else if (SDL_strcmp(cle, "serie") == 0 && numerique)   e->serie = borne32(n, 0, 100000);
        else if (SDL_strcmp(cle, "record") == 0 && numerique)  e->serie_record = borne32(n, 0, 100000);
        else if (SDL_strcmp(cle, "jour") == 0 && numerique)    e->dernier_jour = n;
        else if (SDL_strcmp(cle, "lots") == 0 && numerique)    e->lots = (uint32_t)(n & 0xFFFFFFFF);
        else if (SDL_strcmp(cle, "parties") == 0 && numerique) e->parties = (n < 0) ? 0 : n;
        else if (SDL_strcmp(cle, "gagnes") == 0 && numerique)  e->tickets_gagnes = (n < 0) ? 0 : n;
        else mauvaises++;
    }
    SDL_free(data);

    if (mauvaises) {
        NS_WARN("portefeuille : %u ligne(s) illisible(s) ignorée(s) dans « %s »",
                mauvaises, chemin);
    }

    /*
     * LA RÉPARATION, et pourquoi elle ne se contente pas de refuser.
     *
     * Un fichier tronqué peut donner un état incohérent sans qu'aucune ligne
     * ne soit illisible — une série de 40 avec un record de 3, par exemple, si
     * la coupure est tombée entre les deux. Refuser le fichier entier ferait
     * perdre au joueur des tickets qu'il a gagnés ; les laisser passer ferait
     * mentir `room_eco_valide` partout ensuite. On répare le minimum, et on le
     * dit.
     *
     * Les lots hors table sont effacés pour la même raison : un bit haut lu
     * dans un fichier abîmé donnerait un lot qui n'existe pas, que la vitrine
     * afficherait sans titre.
     */
    if (e->serie > e->serie_record) e->serie_record = e->serie;
    const uint32_t masque = (ROOM_ECO_LOT_COUNT >= 32)
                          ? 0xFFFFFFFFu : ((1u << ROOM_ECO_LOT_COUNT) - 1u);
    if (e->lots & ~masque) {
        NS_WARN("portefeuille : lot(s) inconnu(s) dans « %s », ignoré(s)", chemin);
        e->lots &= masque;
    }

    /* La mise n'est PAS persistée, et c'est une décision : un quitte ou double
     * en attente au moment où l'on quitte le jeu est un refus. Le sauver
     * rouvrirait la table au lancement suivant, devant un joueur qui ne se
     * souvient plus de la partie qu'il devrait battre. */
}

bool room_eco_sauver(const room_eco *e)
{
    if (!e) return false;

    const char *chemin = room_eco_chemin();
    char tmp[1088];
    SDL_snprintf(tmp, sizeof tmp, "%s.tmp", chemin);

    SDL_IOStream *io = SDL_IOFromFile(tmp, "w");
    if (!io) {
        NS_ERROR("portefeuille : écriture impossible (%s) : %s", tmp, SDL_GetError());
        return false;
    }

    char buf[512];
    int len = SDL_snprintf(buf, sizeof buf,
        ECO_VERSION "\n"
        "jetons=%d\ntickets=%d\nserie=%d\nrecord=%d\n"
        "jour=%lld\nlots=%u\nparties=%lld\ngagnes=%lld\n",
        e->jetons, e->tickets, e->serie, e->serie_record,
        (long long)e->dernier_jour, e->lots,
        (long long)e->parties, (long long)e->tickets_gagnes);
    SDL_WriteIO(io, buf, (size_t)len);
    SDL_CloseIO(io);

    /* Renommage atomique : une coupure pendant l'écriture laisse l'ancien
     * fichier intact plutôt qu'un fichier à moitié écrit. Même discipline que
     * `ns_config_save` et `ns_scores_save`. */
    if (!SDL_RenamePath(tmp, chemin)) {
        NS_ERROR("portefeuille : renommage impossible (%s -> %s) : %s",
                 tmp, chemin, SDL_GetError());
        return false;
    }
    return true;
}

/* ==========================================================================
 * LA FAÇADE DE LA SALLE — voir room_economie.h pour le raisonnement
 * ========================================================================== */

static room_eco g_eco;
static bool     g_ouverte;

/* Le bandeau : ce qui vient de se passer, et depuis combien de temps. Il vit
 * ici plutôt que dans `room_hud` parce que l'affichage ne doit posséder aucune
 * des données qu'il montre — c'est la règle que `room_hud.h` s'est donnée, et
 * un compteur de tickets rangé là-bas finirait par contredire celui-ci. */
static char  g_msg[80];
static float g_msg_reste;

/* Le pari en cours. Il est gardé À PART de `g_eco.mise` parce que la reprise
 * est une VRAIE partie : elle appelle `room_eco_fin_partie`, qui écraserait la
 * mise d'origine. On la met donc de côté avant de relancer. */
static bool    g_pari_arme;
static int32_t g_pari_mise;
static int32_t g_pari_battre;

/* `SDL_PRINTF_VARARG_FUNC` fait vérifier le format par le compilateur : sans
 * lui, un « %d » de trop passerait la construction et sortirait un bandeau
 * illisible chez le joueur. */
static void dire(SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);

static void dire(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(g_msg, sizeof g_msg, fmt, ap);
    va_end(ap);
    /* 3,5 s : le temps de lire deux mots en marchant. Le bandeau de réglages
     * tient 2,6 s pour une ligne plus courte, et c'est la seule mesure dont on
     * dispose ici. */
    g_msg_reste = 3.5f;
}

void room_eco_salle_ouvrir(void)
{
    if (g_ouverte) return;
    room_eco_charger(&g_eco);
    g_ouverte = true;
    NS_INFO("portefeuille : %d jeton(s), %d ticket(s), série %d jour(s) — « %s »",
            g_eco.jetons, g_eco.tickets, g_eco.serie, room_eco_chemin());
}

void room_eco_salle_fermer(void)
{
    if (!g_ouverte) return;
    /* Une offre laissée ouverte au moment de fermer est un REFUS : on verse.
     * Perdre les tickets d'une partie parce qu'on a quitté le jeu serait une
     * punition, et il n'y en a pas dans cette économie. */
    (void)room_eco_encaisser(&g_eco);
    g_pari_arme = false;
    (void)room_eco_sauver(&g_eco);
    g_ouverte = false;
}

const room_eco *room_eco_salle(void) { return &g_eco; }

bool room_eco_salle_jeton(void)
{
    if (room_eco_inserer(&g_eco)) return true;
    dire("PLUS DE JETON : VOIR LE MONNAYEUR");
    return false;
}

uint64_t room_eco_salle_graine(const char *jeu)
{
    if (room_eco_lot_acquis(&g_eco, ROOM_ECO_LOT_LIBRE)) {
        /* `SDL_GetPerformanceCounter` et non `rand()` : c'est la source que
         * `main.c` employait pour TOUTES les parties avant le tournoi, et la
         * garder ici veut dire que le lot rend exactement l'ancien
         * comportement plutôt qu'un autre. */
        return (uint64_t)SDL_GetPerformanceCounter();
    }
    return room_eco_graine_du_jour(jeu);
}

void room_eco_salle_fin(const char *jeu, bool hard, uint32_t score)
{
    if (g_pari_arme) {
        /* La reprise remet la mise d'origine sur la table, puis la joue. */
        g_eco.mise = g_pari_mise;
        g_eco.mise_score = g_pari_battre;
        SDL_strlcpy(g_eco.mise_jeu, (jeu && *jeu) ? jeu : "?", sizeof g_eco.mise_jeu);
        g_pari_arme = false;
        g_eco.parties += 1;

        const int32_t verse = room_eco_doubler(&g_eco, (int64_t)score);
        if (verse > 0) dire("DOUBLE : +%d TICKETS", verse);
        else           dire("PERDU : %d TICKETS ENVOLES", g_pari_mise);
        (void)room_eco_sauver(&g_eco);
        return;
    }

    const int32_t t = room_eco_fin_partie(&g_eco, jeu, hard, (int64_t)score);

    /* L'offre n'existe que si le lot a été acheté : le quitte ou double est
     * lui-même un lot de la vitrine, et c'est ce qui donne au premier achat
     * quelque chose à changer tout de suite. */
    if (t > 0 && room_eco_lot_acquis(&g_eco, ROOM_ECO_LOT_QUITTE)) {
        dire("%d TICKETS EN JEU", t);
        return;      /* la mise reste sur la table : la salle demandera */
    }

    const int32_t verse = room_eco_encaisser(&g_eco);
    if (verse > 0) dire("+%d TICKETS", verse);
    (void)room_eco_sauver(&g_eco);
}

void room_eco_salle_monnayeur(void)
{
    const int32_t rendu = room_eco_monnayeur(&g_eco);
    if (rendu > 0) {
        dire("+%d JETONS", rendu);
    } else {
        /* Le change, quand le plancher n'a rien à donner : c'est là que les
         * tickets font des jetons, et seulement là. Cinq à la fois, comme le
         * plancher — un joueur qui actionne le monnayeur veut de quoi jouer,
         * pas une pièce. */
        const int32_t n = room_eco_changer(&g_eco, ROOM_ECO_PLANCHER_ACCUEIL);
        if (n > 0) dire("CHANGE : +%d JETONS", n);
        else       dire("%d JETONS EN POCHE", g_eco.jetons);
    }
    (void)room_eco_sauver(&g_eco);
}

void room_eco_salle_vitrine(void)
{
    const room_eco_lot l = room_eco_lot_en_vue(&g_eco);
    if (l == ROOM_ECO_LOT_COUNT) { dire("VITRINE VIDE : TOUT EST A VOUS"); return; }

    if (room_eco_acheter(&g_eco, l)) {
        dire("%s !", room_eco_lot_titre(l));
        NS_INFO("vitrine : « %s » acquis (%d tickets) — %s",
                room_eco_lot_titre(l), room_eco_lot_prix(l), room_eco_lot_quoi(l));
        (void)room_eco_sauver(&g_eco);
    } else {
        /* On ne peut pas l'avoir : on dit son PRIX plutôt que « pas assez ».
         * Un joueur devant une vitrine veut savoir combien il lui manque. */
        dire("%s : %d TICKETS", room_eco_lot_titre(l), room_eco_lot_prix(l));
    }
}

bool    room_eco_salle_offre(void)        { return g_eco.mise > 0; }
int32_t room_eco_salle_offre_mise(void)   { return g_eco.mise; }
int32_t room_eco_salle_offre_battre(void) { return room_eco_mise_a_battre(&g_eco); }

bool room_eco_salle_accepter(void)
{
    if (g_eco.mise <= 0) return false;
    g_pari_arme   = true;
    g_pari_mise   = g_eco.mise;
    g_pari_battre = g_eco.mise_score;
    /* La table est levée le temps de la reprise : sans ça, une partie
     * abandonnée en cours de pari verserait la mise qu'on avait risquée. */
    g_eco.mise = 0;
    g_eco.mise_score = 0;
    g_eco.mise_jeu[0] = '\0';
    dire("QUITTE OU DOUBLE : BATTRE %d", g_pari_battre);
    return true;
}

void room_eco_salle_refuser(void)
{
    const int32_t verse = room_eco_encaisser(&g_eco);
    if (verse > 0) dire("+%d TICKETS", verse);
    (void)room_eco_sauver(&g_eco);
}

const char *room_eco_salle_message(void)       { return (g_msg_reste > 0.0f) ? g_msg : ""; }
float       room_eco_salle_message_reste(void) { return g_msg_reste; }

void room_eco_salle_avancer(float dt)
{
    if (g_msg_reste > 0.0f) {
        g_msg_reste -= dt;
        if (g_msg_reste < 0.0f) g_msg_reste = 0.0f;
    }
}
