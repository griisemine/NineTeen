/*
 * ns_online.h — le classement en ligne, activable, et jamais bloquant.
 *
 * La règle qui prime sur tout le reste
 * ------------------------------------
 * **Le jeu ne demande jamais de compte pour jouer, et rien n'échoue quand le
 * réseau n'est pas là.** C'était l'erreur de fond de la V1 : tout le corps de
 * `main()` était enfermé dans un contrôle de version en ligne, et sans réseau le
 * jeu affichait une boîte mensongère puis se fermait. Il ne pouvait pas
 * atteindre sa propre fenêtre.
 *
 * Ici c'est l'inverse par construction : on joue, le score est acquis
 * localement, ET ENSUITE on se demande s'il y a un serveur. Une partie qui ne
 * part pas reste une partie jouée.
 *
 * Activable, au sens strict
 * -------------------------
 * Sans URL de serveur configurée, **aucune socket n'est ouverte** — le fil de
 * travail ne démarre même pas. L'URL vient de `NS_CFG_SERVER_URL`, une clé
 * réservée depuis M1 que personne n'avait jamais lue, ou de `--server=`.
 * `--offline` verrouille par-dessus, et un test le vérifie.
 *
 * Tout se passe sur un fil
 * ------------------------
 * Une requête HTTP prend le temps qu'elle prend. La boucle de jeu ne l'attend
 * jamais : elle dépose une demande, le fil la traite, et le résultat est relu
 * quand il est là. C'est ce qui fait qu'un serveur lent ou mort ne se voit pas
 * autrement que par un classement qui reste local.
 */
#ifndef NS_ONLINE_H
#define NS_ONLINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NS_ONLINE_MAX_ROWS 8
#define NS_ONLINE_NAME 24

typedef struct ns_online_row {
    char     name[NS_ONLINE_NAME];
    uint32_t score;
} ns_online_row;

typedef struct ns_online_board {
    char           game[24];
    char           difficulty[16];
    ns_online_row  row[NS_ONLINE_MAX_ROWS];
    uint32_t       count;
    bool           fresh;      /* au moins une réponse reçue */
} ns_online_board;

typedef struct ns_online_config {
    const char *server_url;   /* NULL ou vide = hors ligne, aucune socket */
    const char *token;        /* jeton de session, ou NULL : lecture seule */
    bool        locked;       /* --offline : interdit tout, même avec une URL */
} ns_online_config;

/*
 * Démarre le fil, ou pas. Renvoie true si le réseau est ACTIF. Aucun échec
 * n'est fatal : sans serveur, le jeu tourne exactement comme avant.
 */
bool ns_online_init(const ns_online_config *cfg);
void ns_online_shutdown(void);

bool ns_online_enabled(void);
/* Ce que l'interface affiche pour dire au joueur où il en est. */
const char *ns_online_status(void);

/*
 * Demande le classement mondial d'un jeu. Retour immédiat : la réponse arrive
 * plus tard, et `ns_online_board_get` la rend quand elle est là.
 */
void ns_online_request_board(const char *game, const char *difficulty);
bool ns_online_board_get(const char *game, const char *difficulty, ns_online_board *out);

/*
 * Un BILLET de partie : ce que le serveur délivre AVANT qu'on joue.
 *
 * C'est le renversement de M6. Le serveur tire la graine du générateur et un
 * secret propre à cette partie ; le client joue avec cette graine et scelle son
 * journal avec ce secret. Le score cesse d'être une valeur que le client
 * annonce pour devenir une conséquence que le serveur recalcule.
 */
typedef struct ns_online_ticket {
    char     run_id[64];
    int64_t  seed;
    uint8_t  secret[64];
    size_t   secret_len;
} ns_online_ticket;

/*
 * Prend un billet pour ce jeu, s'il y en a un de PRÊT. Retour immédiat, sans
 * réseau : le fil en tient un d'avance et en redemande un dès qu'on prend
 * celui-là.
 *
 * Pourquoi d'avance : une partie commence quand le joueur appuie sur le bouton,
 * pas quand le serveur répond. Faire l'aller-retour à ce moment-là figerait la
 * salle pendant le temps d'une requête — c'est-à-dire exactement le défaut de
 * la V1, où tout le jeu attendait le réseau. Sans billet prêt, la partie se joue
 * hors ligne et le score reste local : `false`, et rien n'échoue.
 */
bool ns_online_take_ticket(const char *game, const char *difficulty,
                           ns_online_ticket *out);

/*
 * Prévient qu'on va probablement jouer à `game`, pour que le billet soit là
 * quand le jeton tombera. Appelé quand le joueur ARRIVE devant une borne : il
 * reste alors le temps d'approcher la main, d'insérer le jeton et d'appuyer —
 * largement de quoi faire un aller-retour HTTP sans que personne n'attende.
 *
 * Sans ça, la première partie de chaque borne se jouerait toujours hors ligne :
 * `ns_online_take_ticket` ne peut rendre que ce qui a déjà été demandé.
 */
void ns_online_prefetch_ticket(const char *game, const char *difficulty);

/*
 * Vide la file des parties en attente : chaque fichier scellé part sur
 * `POST /api/v1/runs/{id}/submit`. À appeler de temps en temps ; sans serveur
 * ou sans jeton, ne fait rien et ne se plaint pas.
 */
void ns_online_flush_queue(void);

/* Nombre de parties envoyées et refusées depuis le démarrage — pour le journal
 * et pour les tests, parce qu'un envoi « au mieux » doit quand même se compter. */
void ns_online_stats(uint32_t *sent, uint32_t *failed);

/* ==========================================================================
 * Ce que le TEMPS RÉEL emprunte ici
 *
 * `ns_realtime` (présence et duels) parle au même serveur que le classement, et
 * ne doit surtout pas redécider tout seul s'il a le droit de parler. Ces trois
 * accesseurs sont le moyen d'hériter du verrou au lieu de le réimplémenter :
 * ils rendent NULL / -1 tant que `ns_online_init` n'a pas dit oui.
 * ========================================================================== */

/*
 * L'URL et le jeton effectivement retenus, ou NULL si le réseau est INACTIF —
 * pas d'URL, ou `--offline`. Un appelant qui n'obtient pas d'URL n'a rien à
 * ouvrir : c'est ainsi que la garantie « sans URL, aucune socket » reste écrite
 * à un seul endroit.
 */
const char *ns_online_server_url(void);
const char *ns_online_session_token(void);

/*
 * Le NUMÉRO que le serveur donne au créneau « flappy » + « hard ».
 *
 * Le classement et les fantômes s'interrogent par numéro ; le moteur ne connaît
 * que des noms, et les deux ne parlent pas la même langue — c'est le premier des
 * trois défauts que le test de bout en bout avait attrapés. La traduction est
 * faite ici, contre la liste que le serveur a RÉELLEMENT renvoyée, et elle sert
 * maintenant aux deux.
 *
 * Renvoie -1 si le réseau est inactif, si la liste n'est pas encore arrivée, ou
 * si le serveur ne connaît pas ce jeu. Un jeu inconnu reste inconnu : on
 * n'invente pas d'identifiant.
 */
int ns_online_game_id(const char *game, const char *difficulty);

/*
 * Le FANTÔME que le prochain billet doit affronter, ou NULL pour aucun.
 *
 * C'est ce qui fait qu'un duel en est un. Le serveur, quand il ouvre une partie
 * avec ce champ, ne tire PAS une graine neuve : il reprend celle de la partie du
 * fantôme. Sans ça, les deux joueurs ne jouent pas la même partie — ils jouent
 * deux parties et comparent deux nombres, ce qui est un classement, et on en a
 * déjà un.
 *
 * Rien n'est affaibli : une graine n'est pas un secret, c'est ce qui doit être
 * partagé. Le secret HMAC reste tiré pour la partie seule, et le score reste
 * recalculé par le serveur depuis le journal d'événements scellé.
 *
 * Le réglage vaut pour les billets SUIVANTS. Un billet déjà en main garde la
 * graine avec laquelle il a été délivré, ce qui est la seule chose correcte à
 * faire — sa partie est peut-être déjà commencée.
 */
void ns_online_set_duel(const char *ghost_run_id);

#endif /* NS_ONLINE_H */
