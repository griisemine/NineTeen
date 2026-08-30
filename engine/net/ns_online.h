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
 * Quatre sources pour une adresse, et il faut savoir laquelle a gagné
 * -------------------------------------------------------------------
 * Voir `ns_online_resolve_url` plus bas. La règle ci-dessus ne bouge pas d'un
 * pouce : les quatre sources peuvent toutes être vides, et c'est le défaut d'un
 * dépôt fraîchement cloné.
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

/* ==========================================================================
 * D'OU VIENT L'ADRESSE
 *
 * Quatre sources peuvent la fournir, et jusqu'ici le journal n'en nommait
 * aucune : il disait « réseau : actif sur "http://…" » sans dire QUI l'avait
 * décidé. C'est précisément l'information qui manque quand le jeu parle au
 * mauvais serveur — on relit alors les quatre endroits à la main, sans savoir
 * lequel a effectivement gagné, et un `settings.cfg` oublié dans le répertoire
 * utilisateur peut battre en silence un `-DNINETEEN_SERVER_URL` posé au build.
 *
 * L'ORDRE, du plus faible au plus fort :
 *
 *   défaut compilé  <  config (settings.cfg)  <  environnement  <  --server=
 *
 * Pourquoi celui-là, source par source :
 *
 *  - Le DÉFAUT COMPILÉ est en bas parce que c'est le seul qu'on ne peut pas
 *    changer sans refaire une compilation. Il est VIDE par défaut, et cette
 *    valeur-là n'est pas négociable : un dépôt cloné et bâti sans rien demander
 *    n'ouvre aucune socket. C'est la règle du haut de ce fichier, et elle prime
 *    sur la commodité d'un défaut « utile ».
 *
 *  - L'ENVIRONNEMENT bat la CONFIG, et c'est le seul choix qui demande un
 *    argument. `settings.cfg` vit dans le répertoire utilisateur : dans un
 *    conteneur il n'existe pas, et sur une machine de développement il garde ce
 *    qu'une session précédente y a laissé. Si la config gagnait, un
 *    `docker run -e NINETEEN_SERVER_URL=…` serait ignoré SANS UN MOT le jour où
 *    un fichier traîne — c'est-à-dire la panne muette que ce dépôt s'emploie à
 *    supprimer. Dans l'autre sens le pire qui arrive est qu'une variable
 *    d'environnement explicitement posée l'emporte, ce qui est ce qu'on a
 *    demandé en la posant.
 *
 *  - La LIGNE DE COMMANDE garde le dernier mot, comme partout ailleurs ici
 *    (`ns_env.h`, `.env.example`, `room/main.c`). C'est ce qui permet d'essayer
 *    un serveur sans toucher ni fichier ni environnement.
 *
 * `--offline` n'est PAS dans cette échelle : c'est un verrou, pas une source.
 * Il s'applique APRÈS, quelle que soit la source retenue, et `ns_online_init`
 * refuse alors de démarrer le fil.
 *
 * Le jeton, lui, n'a que deux sources et n'en aura jamais de compilée : voir le
 * bloc « CE QUI NE SE COMPILE PAS » de `room/CMakeLists.txt`.
 * ========================================================================== */

typedef enum ns_online_source {
    NS_ONLINE_SRC_AUCUNE = 0,        /* les quatre sont vides : hors ligne */
    NS_ONLINE_SRC_COMPILEE,          /* -DNINETEEN_SERVER_URL au build */
    NS_ONLINE_SRC_CONFIG,            /* NS_CFG_SERVER_URL, settings.cfg */
    NS_ONLINE_SRC_ENVIRONNEMENT,     /* NINETEEN_SERVER_URL au lancement */
    NS_ONLINE_SRC_LIGNE_COMMANDE,    /* --server= */
} ns_online_source;

typedef struct ns_online_url {
    /*
     * Pointe DANS l'une des quatre chaînes reçues, sans copie : la fonction est
     * pure et n'alloue rien. L'appelant doit donc garder ses chaînes en vie
     * aussi longtemps qu'il se sert du résultat — ce qui est le cas naturel,
     * elles viennent de `argv`, de l'environnement ou de la config.
     *
     * NULL quand aucune source ne dit rien, et jamais la chaîne vide : un
     * appelant qui teste `url != NULL` et un autre qui teste `url[0]` doivent
     * tomber d'accord.
     */
    const char      *url;
    ns_online_source source;
} ns_online_url;

/*
 * Choisit l'adresse, et dit d'où elle vient. N'ouvre rien, ne journalise rien,
 * ne touche à aucun état : c'est du calcul pur, et c'est pour ça que
 * `tests/test_online.c` peut en couvrir les seize combinaisons.
 *
 * Une source VIDE ou faite uniquement d'espaces compte pour absente. Le blanc
 * est traité parce qu'il arrive vraiment : `NINETEEN_SERVER_URL=` dans un
 * `.env`, ou un `docker run -e NINETEEN_SERVER_URL=" "`, produiraient sinon une
 * URL d'un caractère qui échouerait plus loin, à un endroit qui ne saurait plus
 * dire d'où elle vient.
 */
ns_online_url ns_online_resolve_url(const char *compilee,
                                    const char *config,
                                    const char *environnement,
                                    const char *ligne_commande);

/* Le nom de la source, tel que le journal de démarrage l'imprime. Jamais NULL,
 * y compris pour une valeur d'énumération inattendue. */
const char *ns_online_source_nom(ns_online_source source);

typedef struct ns_online_config {
    const char *server_url;   /* NULL ou vide = hors ligne, aucune socket */
    const char *token;        /* jeton de session, ou NULL : lecture seule */
    bool        locked;       /* --offline : interdit tout, même avec une URL */
    /*
     * D'où vient `server_url`, pour que le journal de démarrage le DISE. Le
     * laisser à zéro reste correct — `SDL_zero(cfg)` suffit toujours à
     * construire une configuration valable — et le journal écrit alors
     * « source non déclarée » plutôt que d'inventer une origine.
     */
    ns_online_source source;
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
