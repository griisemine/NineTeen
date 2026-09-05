/*
 * ns_http.h — le plus petit client HTTP qui fasse le travail.
 *
 * Pourquoi il existe
 * ------------------
 * `NS_CFG_SERVER_URL` est une clé réservée depuis M1 que **personne n'a jamais
 * lue**, et l'audit la listait parmi les réglages qui n'en étaient pas. Le
 * serveur Go, sa base et son barème anti-triche existent depuis M6 ; le journal
 * de partie scellé et sa file d'attente depuis B8. Il ne manquait que la
 * socket — donc rien de tout ça n'avait jamais parlé à rien.
 *
 * Ce qu'il est, et ce qu'il n'est pas
 * -----------------------------------
 * HTTP/1.1, `GET` et `POST`, corps en mémoire, délais bornés. C'est tout ce dont
 * le classement a besoin. Il n'y a ni redirection suivie, ni `chunked` en
 * émission, ni cookies, ni connexions persistantes : une soumission de score est
 * un aller-retour de quelques kilo-octets toutes les quelques minutes, et une
 * pile complète coûterait plus qu'elle ne rapporte.
 *
 * **Pas de TLS**, et c'est dit franchement plutôt que sous-entendu : le chiffrer
 * demanderait OpenSSL ou mbedTLS, c'est-à-dire une dépendance lourde dans un
 * projet qui se veut reconstructible en une commande. Le classement vise un
 * serveur qu'on héberge soi-même, sur un réseau qu'on choisit ; `docs/JOUER.md`
 * l'écrit noir sur blanc, et le jeton de session ne doit pas voyager sur un
 * réseau qu'on ne contrôle pas. Mettre un reverse-proxy TLS devant le serveur
 * Go est la bonne réponse, et elle ne coûte rien au client.
 *
 * Ce que ça change pour le mode hors ligne
 * ----------------------------------------
 * A2b démontrait que le binaire n'importait **aucun** symbole réseau. Ce n'est
 * plus vrai, et il serait malhonnête de laisser la phrase telle quelle : les
 * symboles existent désormais. Ce qui reste vrai, et qui est ce qui compte :
 * **rien ne part sans qu'on l'ait demandé**. Sans URL de serveur configurée,
 * aucune socket n'est ouverte ; `--offline` verrouille en plus, et le verrou est
 * vérifié par un test.
 */
#ifndef NS_HTTP_H
#define NS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ns_http_response {
    int    status;        /* code HTTP, ou 0 si la requête n'a pas abouti */
    char  *body;          /* alloué par SDL_malloc, à libérer par l'appelant */
    size_t length;
    char   error[128];    /* vide si tout s'est bien passé */
} ns_http_response;

/*
 * `url` est de la forme `http://hôte[:port]/chemin`. Le schéma `https://` est
 * REFUSÉ explicitement plutôt que tenté en clair : échouer bruyamment vaut mieux
 * qu'envoyer un jeton de session en clair sur un port 443 qui ne comprendra pas.
 *
 * `bearer` peut être NULL. `body` peut être NULL pour un GET.
 * Bloquant, borné par `timeout_ms`. À appeler depuis un fil de travail.
 */
bool ns_http_request(const char *method, const char *url,
                     const char *content_type, const char *bearer,
                     const void *body, size_t body_len,
                     uint32_t timeout_ms, ns_http_response *out);

void ns_http_response_free(ns_http_response *r);

/* ==========================================================================
 * LE TÉLÉCHARGEMENT, qui ne passe PAS par `ns_http_request`
 * ==========================================================================
 * `ns_http_request` garde toute la réponse en mémoire et la borne à 256 Kio.
 * C'est le bon choix pour un classement de quelques kilo-octets, et le mauvais
 * pour un paquet du jeu : le `.deb` mesuré fait 175 580 032 octets, soit sept
 * cents fois la borne, et le charger d'un bloc coûterait autant de mémoire que
 * toute la salle.
 *
 * Celui-ci écrit au fil de l'eau dans un fichier, en 64 Kio à la fois, et il
 * REPREND : si le fichier existe déjà il demande la suite avec un en-tête
 * `Range`, ce que le serveur sait servir. Une coupure de réseau au milieu d'un
 * paquet de 175 Mio ne recommence donc pas depuis zéro — ce qui, sur une ligne
 * ordinaire, est la différence entre une mise à jour qui aboutit et une qui
 * n'aboutit jamais.
 * ========================================================================== */

/*
 * Appelée à chaque bloc reçu. Rendre `false` ARRÊTE le téléchargement — c'est
 * ainsi qu'on referme le jeu sans attendre la fin d'un paquet, et le fichier
 * partiel reste sur le disque pour la fois suivante.
 *
 * `total` vaut la taille annoncée par le serveur, ou 0 s'il ne l'annonce pas.
 */
typedef bool (*ns_http_progres)(void *contexte, int64_t recu, int64_t total);

/*
 * Télécharge `url` vers `chemin`, en reprenant ce qui s'y trouve déjà.
 *
 * Rend true quand le corps est arrivé en entier. `statut` reçoit le code HTTP
 * — 200 pour un début, 206 pour une reprise — même en cas d'échec, parce que
 * distinguer un 404 d'une coupure est ce qui permet de décider si l'on
 * réessaie. Bloquant, à appeler depuis un fil de travail.
 */
bool ns_http_telecharger(const char *url, const char *chemin,
                         uint32_t timeout_ms,
                         ns_http_progres progres, void *contexte,
                         int *statut, char *erreur, size_t erreur_cap);

/* Découpe une URL. Rendue publique parce que c'est la partie qu'on veut tester
 * sans ouvrir de socket — et parce qu'une URL mal découpée est le genre de
 * défaut qui ne se voit qu'en production. */
bool ns_http_parse_url(const char *url, char *host, size_t host_size,
                       uint16_t *port, char *path, size_t path_size,
                       char *error, size_t error_size);

#endif /* NS_HTTP_H */
