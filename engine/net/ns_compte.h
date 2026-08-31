/*
 * ns_compte.h — le compte du joueur et les salons, vus du jeu.
 *
 * POURQUOI CE MODULE EXISTE
 * -------------------------
 * Jusqu'ici, se connecter au serveur consistait à coller à la main une clé de
 * session dans `settings.cfg`, sous la clé `network.token`. Aucun joueur ne
 * fait ça. Et il ne le POUVAIT pas depuis le jeu : `SDL_StartTextInput`
 * n'apparaissait nulle part dans le dépôt, donc aucun champ de saisie
 * n'existait — ni pseudo, ni mot de passe, ni code de salon.
 *
 * Ce module est la moitié réseau de la réparation : il sait s'inscrire, se
 * connecter, lister les salons, en créer un, en rejoindre un par son code, et
 * y battre. La moitié interface est ailleurs (`room_comptoir`), et la saisie
 * elle-même est une brique du moteur (`ns_saisie`) — trois modules parce que
 * trois responsabilités, et parce que seuls les deux premiers se testent sans
 * fenêtre.
 *
 * POURQUOI PAS DANS `ns_online`, QUI PARLE DÉJÀ À CE SERVEUR
 * ----------------------------------------------------------
 * Ce n'en serait pas une duplication de principe, mais de MODÈLE, et le modèle
 * de `ns_online` est faux pour ce qu'on fait ici. Il garde « une demande en
 * attente, au plus » par genre, et son commentaire dit pourquoi : « un
 * classement qu'on redemande avant d'avoir la réponse ne sert à rien ». C'est
 * exact — un classement demandé deux fois est le même classement.
 *
 * Créer un salon deux fois crée DEUX SALONS. Rejoindre puis quitter n'est pas
 * la même chose que quitter puis rejoindre. Ces requêtes ne se fusionnent pas
 * et ne se rejouent pas : il leur faut une file où chacune part une fois, dans
 * l'ordre, et où son résultat est rendu une fois. C'est un mécanisme différent
 * pour une raison différente, pas le même mécanisme recopié.
 *
 * CE QU'IL NE FAIT JAMAIS
 * -----------------------
 *   - Ouvrir une socket sans URL configurée. C'est l'invariant du dépôt, et il
 *     tient ici comme ailleurs : `ns_compte_init(NULL)` rend `false` et le
 *     module reste muet.
 *   - Écrire un mot de passe ailleurs qu'en mémoire vive, le journaliser, ou le
 *     garder après l'envoi. Le tampon qui l'a porté est écrasé.
 *   - Journaliser la clé de session. Le jeton n'apparaît dans aucune trace.
 *
 * BLOQUANT / NON BLOQUANT
 * -----------------------
 * Rien de ce qui est ici ne bloque l'appelant. Un fil de travail exécute la
 * file ; l'écran interroge l'état à chaque image. Une requête HTTP dure des
 * dizaines de millisecondes au mieux — la faire sur le fil de rendu ferait
 * sauter des images à chaque frappe de touche.
 */
#ifndef NS_COMPTE_H
#define NS_COMPTE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Les bornes des champs. Elles ne sont pas choisies ici : elles suivent ce que
 * le serveur accepte, et s'en écarter fabriquerait un refus que le joueur ne
 * pourrait pas comprendre.
 *
 *   - pseudo : 24 caractères au plus (auth.ValidateUsername) ;
 *   - mot de passe : 256 au plus, 12 au moins (auth.ValidatePassword). Tronquer
 *     à moins de 256 rendrait impossible la connexion d'un compte créé sur le
 *     site avec un mot de passe long — un échec sans message, le pire des cas.
 */
#define NS_COMPTE_PSEUDO_MAX  32
#define NS_COMPTE_MDP_MAX     264
#define NS_COMPTE_JETON_MAX   256
#define NS_COMPTE_MESSAGE_MAX 160

typedef enum ns_compte_etat {
    NS_COMPTE_ETEINT = 0,   /* pas d'URL : le module ne fait rien */
    NS_COMPTE_ANONYME,      /* joignable, personne de connecté */
    NS_COMPTE_ATTENTE,      /* une requête est partie */
    NS_COMPTE_CONNECTE
} ns_compte_etat;

/* --- salons -------------------------------------------------------------- */

#define NS_SALON_CODE_MAX    12   /* six caractères, et de la marge */
#define NS_SALON_NOM_MAX     40
#define NS_SALON_MAX_PLACES   8   /* la borne du mode, voir room_couperet.h */
#define NS_SALON_MAX_LISTE   16

typedef struct ns_salon_occupant {
    int  place;
    char pseudo[NS_COMPTE_PSEUDO_MAX];
    int  camp;
    int  points;
    int  fusibles;
    bool vivante;
    char borne[32];
} ns_salon_occupant;

/*
 * Une ligne de la liste publique. Elle ne porte PAS de quoi rejoindre : le
 * relais n'arrive qu'après un `join` accepté. Voir plus bas.
 */
typedef struct ns_salon_resume {
    char     code[NS_SALON_CODE_MAX];
    char     nom[NS_SALON_NOM_MAX];
    char     proprietaire[NS_COMPTE_PSEUDO_MAX];
    int      places;
    int      occupes;
    int      camps;
    char     etat[12];          /* attente | manche | fini */
    uint32_t depuis_ms;
} ns_salon_resume;

/*
 * Le salon où l'on est.
 *
 * `relais_salon` est un nombre de 64 bits tiré par le serveur : c'est LA
 * CAPACITÉ, ce qui donne effectivement une place sur le relais. Le code à six
 * caractères ne l'est pas — il est court parce qu'un joueur le tape, donc
 * devinable, et il ne sert qu'à demander au serveur la permission d'entrer.
 * Confondre les deux reviendrait à laisser n'importe qui s'asseoir dans
 * n'importe quelle manche en essayant des codes.
 */
typedef struct ns_salon {
    char     code[NS_SALON_CODE_MAX];
    char     nom[NS_SALON_NOM_MAX];
    char     proprietaire[NS_COMPTE_PSEUDO_MAX];
    int      places;
    int      camps;
    bool     prive;
    char     etat[12];

    char     relais_hote[128];
    uint16_t relais_port;
    uint64_t relais_salon;
    int      place;             /* la mienne, 0..places-1 */

    ns_salon_occupant occupant[NS_SALON_MAX_PLACES];
    int               occupants;
} ns_salon;

/* --- vie du module ------------------------------------------------------- */

/*
 * `url` peut être NULL ou vide : le module reste alors ÉTEINT et n'ouvre
 * aucune socket. `jeton` peut être NULL ; s'il est fourni (celui gardé d'une
 * session précédente), le module le vérifie contre `/api/v1/me` et passe
 * CONNECTÉ si le serveur le reconnaît encore.
 */
bool ns_compte_init(const char *url, const char *jeton);
void ns_compte_shutdown(void);

ns_compte_etat ns_compte_etat_courant(void);
bool           ns_compte_occupe(void);   /* une requête est en vol */

/* Le pseudo connecté, ou une chaîne vide. Jamais NULL. */
const char *ns_compte_pseudo(void);

/*
 * Le dernier message, en français, prêt à afficher tel quel. C'est ce que le
 * serveur a répondu quand il a répondu — « le nom doit faire entre 3 et 24
 * caractères » vient de lui, pas d'ici. Recopier ses règles de ce côté-ci les
 * ferait diverger le jour où elles changent.
 */
const char *ns_compte_message(void);

/* --- compte -------------------------------------------------------------- */

void ns_compte_inscrire(const char *pseudo, const char *mdp);
void ns_compte_connecter(const char *pseudo, const char *mdp);
void ns_compte_deconnecter(void);

/*
 * Le jeton de session, à donner à `ns_online` pour que les scores partent sous
 * le bon compte, et à garder dans la configuration pour ne pas retaper.
 * Rend une chaîne vide tant que personne n'est connecté.
 */
const char *ns_compte_jeton(void);

/*
 * Vrai UNE FOIS après chaque changement de jeton (connexion, déconnexion).
 * L'appel consomme le drapeau. C'est ce qui évite de réécrire la configuration
 * et de rebrancher `ns_online` à chaque image.
 */
bool ns_compte_jeton_change(void);

/* --- salons -------------------------------------------------------------- */

void ns_salons_demander_liste(void);
int  ns_salons_liste(ns_salon_resume *out, int max);
uint32_t ns_salons_liste_age_ms(void);   /* UINT32_MAX si jamais reçue */

void ns_salon_creer(const char *nom, int places, int camps, bool prive);
void ns_salon_rejoindre(const char *code);
void ns_salon_quitter(void);
void ns_salon_supprimer(void);

/* Faux si l'on n'est dans aucun salon. */
bool ns_salon_courant(ns_salon *out);

/*
 * Le battement : il dit au serveur qu'on est vivant ET publie la ligne de
 * classement de ce joueur. Un seul aller-retour pour les deux, comme la
 * présence, et pour la même raison — c'est le seul échange périodique du mode.
 *
 * `commence` n'a d'effet que dans la main du propriétaire : c'est ce qui fait
 * passer le salon de l'attente à la manche.
 */
void ns_salon_battre(int points, int fusibles, bool vivante,
                     const char *borne, int camp, bool commence);

/* Compteurs, pour le diagnostic. */
void ns_compte_stats(uint32_t *envoyees, uint32_t *echouees);

#endif /* NS_COMPTE_H */
