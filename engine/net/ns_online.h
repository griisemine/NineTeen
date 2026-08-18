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
 * Vide la file des parties en attente. À appeler de temps en temps ; sans
 * serveur ou sans jeton, ne fait rien et ne se plaint pas.
 */
void ns_online_flush_queue(void);

/* Nombre de parties envoyées et refusées depuis le démarrage — pour le journal
 * et pour les tests, parce qu'un envoi « au mieux » doit quand même se compter. */
void ns_online_stats(uint32_t *sent, uint32_t *failed);

#endif /* NS_ONLINE_H */
