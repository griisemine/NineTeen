/*
 * ns_realtime.h — la présence dans la salle, et les duels en différé.
 *
 * Ce que c'est, en une phrase
 * ---------------------------
 * Voir les autres joueurs marcher dans l'allée, et pouvoir affronter leur
 * partie sans qu'ils soient là à la même seconde.
 *
 * DEUX VERROUS EN SÉRIE, et c'est le point le plus important du fichier
 * ---------------------------------------------------------------------
 * Le temps réel ne peut pas ouvrir une socket que le classement n'aurait pas
 * déjà le droit d'ouvrir. `ns_realtime_init` commence par interroger
 * `ns_online_enabled()` et rend `false` sans rien tenter si la réponse est non.
 *
 * La garantie « sans URL configurée, aucune socket n'est ouverte » reste donc
 * écrite à UN SEUL endroit — `ns_online_init` — et le temps réel en hérite au
 * lieu de la réimplémenter. Réimplémenter un verrou, c'est se donner deux
 * chances de le poser de travers ; celui-ci compte parmi les promesses que le
 * projet vérifie par un test, et il n'y a aucune raison de le dupliquer.
 *
 * Par-dessus vient un SECOND verrou, propre à ce fichier : `cfg->enabled`. Le
 * temps réel est INERTE PAR DÉFAUT même avec un serveur configuré et joignable.
 * Il faut l'activer explicitement (`--temps-reel`, le réglage `network.realtime`,
 * ou l'entrée du menu Échap). Un classement en ligne ne diffuse rien de soi ;
 * la présence, si — elle publie un pseudo et une position. Ce n'est pas à un
 * réglage de serveur de décider ça pour le joueur.
 *
 * Ce qui est ici, et ce qui n'y est PAS
 * -------------------------------------
 * Ici : la présence (position publiée, pairs relus, interpolés par l'appelant)
 * et le duel EN DIFFÉRÉ — le « fantôme » — c'est-à-dire le transport d'un
 * journal d'entrées et son rejeu à côté de sa propre partie.
 *
 * Pas ici, et c'est délibéré : le duel EN DIRECT (pas verrouillé). Il demande
 * une propriété que ce dépôt n'a pas encore mesurée — le déterminisme entre DEUX
 * MACHINES DIFFÉRENTES — et sans elle deux parties divergent en silence jusqu'à
 * ce que les scores se contredisent. `docs/RESEAU-TEMPS-REEL.md` dit ce qu'il
 * faudrait mesurer d'abord. Livrer un duel qui diverge serait pire que ne pas en
 * livrer.
 *
 * Pourquoi le fantôme et pas autre chose
 * --------------------------------------
 * Parce que la propriété dont il dépend est MESURÉE, pas supposée :
 * `tests/test_replay.c` établit que pour les huit jeux, une suite de masques de
 * boutons — un par pas fixe — reproduit une partie, état comparé au bit près. Un
 * duel en différé ne demande rien de plus que ça. Et il marche quand l'autre est
 * déconnecté, ce qui est la situation normale entre amis.
 *
 * Le transport, et son rythme
 * ---------------------------
 * HTTP/1.1 sans connexion persistante, sur le fil de `ns_http` — le même que le
 * classement. La présence est donc publiée à 4 Hz et INTERPOLÉE au rendu, et non
 * aux 20 Hz qu'évoquait le document de conception : 20 requêtes par seconde et
 * par joueur sur un transport qui rouvre sa socket à chaque fois coûterait plus
 * que ça ne rapporte, pour un bonhomme qui marche à 1,4 m/s. Le chiffre est
 * mesuré dans `tests/test_duel.c` plutôt qu'espéré.
 *
 * Rien ne bloque, jamais
 * ----------------------
 * Comme `ns_online`, tout passe par un fil de travail : la boucle de jeu DÉPOSE
 * une position et RELIT des pairs, elle n'attend aucune requête. Un serveur lent,
 * mort, ou qui tombe au milieu d'une partie ne se voit que d'une façon : les
 * autres joueurs se figent puis disparaissent, et la partie continue.
 */
#ifndef NS_REALTIME_H
#define NS_REALTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NS_RT_MAX_PEERS   16
#define NS_RT_MAX_GHOSTS   8
#define NS_RT_NAME        24
#define NS_RT_SLUG        32
#define NS_RT_ID          40   /* un UUID textuel tient dans 37 */

typedef struct ns_realtime_config {
    /*
     * Le second verrou. Faux — le défaut — signifie : aucune présence n'est
     * publiée, aucun fantôme n'est demandé, et le fil ne démarre pas.
     */
    bool        enabled;
    /*
     * Le pseudo montré aux autres. Sans jeton de session, le serveur le prend
     * tel quel et le marque NON VÉRIFIÉ ; avec un jeton, il l'écrase par le nom
     * du compte. On ne demande de compte à personne, donc on ne peut pas
     * garantir un nom — mais on peut dire lequel des deux on affiche.
     */
    const char *nickname;
} ns_realtime_config;

/*
 * Démarre le temps réel, ou pas. Renvoie true seulement si les DEUX verrous
 * sont levés : `ns_online_enabled()` et `cfg->enabled`.
 *
 * Aucun échec n'est fatal, ici comme partout ailleurs dans ce dossier : sans
 * temps réel, la salle est vide d'autres joueurs et le jeu est exactement celui
 * qu'il était.
 */
bool ns_realtime_init(const ns_realtime_config *cfg);
void ns_realtime_shutdown(void);

bool        ns_realtime_enabled(void);
const char *ns_realtime_status(void);

/* ==========================================================================
 * La présence
 * ========================================================================== */

typedef struct ns_realtime_peer {
    char    name[NS_RT_NAME];
    bool    verified;              /* le serveur répond du pseudo */
    float   x, y, z;
    float   yaw;
    char    cabinet[NS_RT_SLUG];   /* la borne devant laquelle il se tient */
    char    game[NS_RT_SLUG];      /* ce qu'il y joue, ou vide */
    int32_t score;
} ns_realtime_peer;

/*
 * Dépose MA position. Retour immédiat, sans réseau : le fil la publiera au
 * prochain battement. Appelable à chaque image sans y penser — seule la
 * dernière valeur compte, et c'est justement ce qu'on veut d'une position.
 */
void ns_realtime_publish(float x, float y, float z, float yaw,
                         const char *cabinet, const char *game, int32_t score);

/*
 * Recopie les pairs connus et renvoie leur nombre.
 *
 * Une COPIE et pas un pointeur : le fil réécrit cette table quand il veut, et
 * rendre l'adresse interne obligerait l'appelant à tenir un verrou pendant tout
 * son rendu. Seize joueurs de quelques dizaines d'octets se recopient pour
 * rien du tout.
 */
uint32_t ns_realtime_peers(ns_realtime_peer *out, uint32_t max);

/*
 * L'âge de la dernière réponse de présence, en millisecondes, ou UINT32_MAX si
 * aucune n'est encore arrivée.
 *
 * C'est ce qui permet à l'appelant de FAIRE DISPARAÎTRE les autres joueurs
 * quand le serveur tombe, au lieu de les laisser figés dans l'allée pour
 * l'éternité — un joueur immobile est un bogue visible, un joueur absent est
 * une déconnexion lisible.
 */
uint32_t ns_realtime_peers_age_ms(void);

/* ==========================================================================
 * Le duel en différé — le fantôme
 * ========================================================================== */

typedef struct ns_realtime_ghost_info {
    char    run_id[NS_RT_ID];
    char    name[NS_RT_NAME];
    int64_t score;
    int64_t seed;
    int32_t ticks;
} ns_realtime_ghost_info;

/*
 * Demande la liste des fantômes d'un créneau. Retour immédiat ; la réponse
 * arrive plus tard et se relit par `ns_realtime_ghosts`.
 */
void     ns_realtime_request_ghosts(const char *game, const char *difficulty);
uint32_t ns_realtime_ghosts(ns_realtime_ghost_info *out, uint32_t max);

/* Demande le JOURNAL d'un fantôme précis. */
void ns_realtime_fetch_ghost(const char *run_id);

/*
 * Prend le journal téléchargé, s'il est prêt. Le texte revient à l'appelant, qui
 * le libère par `SDL_free`. Un seul fantôme est gardé à la fois : on n'affronte
 * qu'un adversaire par partie.
 */
bool ns_realtime_take_ghost(char **out_text, size_t *out_len,
                            ns_realtime_ghost_info *out_info);

/*
 * Dépose SON journal d'entrées comme fantôme, pour que d'autres l'affrontent.
 *
 * À n'appeler qu'après une partie qui a été ACCEPTÉE par le serveur : le
 * fantôme se rattache à une partie dont le serveur a lui-même tiré la graine et
 * RECALCULÉ le score. Déposer un journal ne crée donc aucun score, et n'ouvre
 * aucune porte que la soumission n'ouvrait déjà.
 */
void ns_realtime_publish_ghost(const char *run_id, const char *inputs, size_t len);

/* Pour le journal et pour les tests : un envoi « au mieux » doit se compter. */
void ns_realtime_stats(uint32_t *presence_ok, uint32_t *presence_failed,
                       uint32_t *ghosts_sent);

#endif /* NS_REALTIME_H */
