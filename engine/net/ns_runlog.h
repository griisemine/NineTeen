/*
 * ns_runlog.h — le journal de partie, et son sceau.
 *
 * À quoi ça sert, et pourquoi c'est écrit maintenant
 * -------------------------------------------------
 * Le serveur Go de M6 ne croit plus le score que le client annonce : il ouvre la
 * partie, tire la graine et un secret, le client joue et renvoie son JOURNAL
 * d'événements scellé, et le serveur **recalcule** le score depuis les
 * événements. C'est le renversement de charge de la preuve décrit dans
 * `server/internal/runs/runs.go`.
 *
 * Ce fichier est la moitié client de ce contrat. Il est écrit maintenant, alors
 * qu'aucun transport n'existe encore, pour une raison précise : le format
 * canonique scellé est **dupliqué à l'identique des deux côtés**, et une
 * divergence d'un seul octet invaliderait toutes les parties. Le mettre en place
 * pendant qu'on peut le comparer ligne à ligne au fichier Go — et le tester
 * contre les vecteurs de son propre test — coûte une heure ; le retrouver plus
 * tard sur un « sceau invalide » en coûte dix.
 *
 * Ce qui est là, et ce qui ne l'est PAS
 * -------------------------------------
 * Là : l'enregistrement des événements au pas fixe, la sérialisation canonique,
 * HMAC-SHA256, et la file d'attente sur disque.
 *
 * Pas là, **délibérément** : la moindre socket. Le binaire n'importe aucun
 * symbole réseau, et c'est une propriété vérifiée depuis A2b (`nm -D` ne rend
 * rien sur socket/connect/getaddrinfo/ssl). Le transport viendra avec la
 * conception du temps réel, qui reste à faire ensemble. En attendant, une partie
 * jouée hors ligne est scellée, mise en file, et n'échoue jamais.
 */
#ifndef NS_RUNLOG_H
#define NS_RUNLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NS_RUNLOG_MAX_EVENTS 200000   /* la borne du serveur, à l'identique */
#define NS_RUNLOG_KIND_MAX   16

typedef struct ns_run_event {
    int64_t at_ms;                     /* depuis le début de la partie */
    int64_t value;
    char    kind[NS_RUNLOG_KIND_MAX];
} ns_run_event;

typedef struct ns_runlog ns_runlog;

ns_runlog *ns_runlog_create(uint32_t capacity);
void       ns_runlog_destroy(ns_runlog *r);

/*
 * Ouvre une partie. `seed` et `secret` viennent du serveur quand il y en a un ;
 * hors ligne on passe la graine locale et un secret nul, et le sceau vaut alors
 * ce qu'il vaut — la partie est gardée, marquée « non authentifiée », et ne sera
 * pas soumise. C'est plus honnête que de fabriquer un faux secret.
 */
void ns_runlog_begin(ns_runlog *r, const char *game, const char *difficulty,
                     int64_t seed, const uint8_t *secret, size_t secret_len);

/* Un fait de jeu. `kind` est un mot court : « score », « death », « flap ». */
void ns_runlog_event(ns_runlog *r, int64_t at_ms, const char *kind, int64_t value);

/* Clôt la partie. Le score annoncé sert de recoupement, pas de vérité. */
void ns_runlog_end(ns_runlog *r, int64_t duration_ms, int64_t claimed_score);

/*
 * La chaîne exacte que le serveur scelle, dans `out`. Renvoie la longueur qui
 * aurait été écrite (comme `snprintf`), donc plus grande que `cap` si le tampon
 * est trop petit.
 *
 * Format, copié de `runs.CanonicalPayload` :
 *   v1|<seed>|<durationMs>|<claimed>|<n>|<t>:<kind>:<v>|...
 */
size_t ns_runlog_canonical(const ns_runlog *r, char *out, size_t cap);

/*
 * Le sceau : HMAC-SHA256 de la charge canonique, en base64 **sans remplissage**
 * — `base64.RawStdEncoding` côté Go. Le « Raw » compte : avec les `=` finaux la
 * comparaison échoue.
 *
 * Renvoie false si la partie n'a pas de secret (jouée hors ligne).
 */
bool ns_runlog_seal(const ns_runlog *r, char *out, size_t cap);

/*
 * Écrit la partie dans la file d'attente locale, en JSON, au format exact de
 * `runs.Submission` — plus les champs dont un futur transport aura besoin pour
 * savoir où l'envoyer. Un fichier par partie, nommé par son horodatage : une
 * écriture ne peut donc pas en abîmer une autre, et une partie non envoyée
 * survit à une coupure.
 *
 * Renvoie false et n'écrit rien si la partie n'est pas authentifiée : mettre en
 * file ce qui sera refusé ne ferait que remplir le disque du joueur.
 */
bool ns_runlog_enqueue(const ns_runlog *r);

/* Le répertoire de la file. Créé à la demande. */
const char *ns_runlog_queue_dir(void);
void        ns_runlog_set_queue_dir(const char *dir);   /* tests ; NULL = défaut */

/* Combien de parties attendent d'être envoyées. */
uint32_t ns_runlog_pending(void);

uint32_t ns_runlog_event_count(const ns_runlog *r);
bool     ns_runlog_authenticated(const ns_runlog *r);

/* ---------------------------------------------------------------------------
 * Primitives exposées pour les tests.
 *
 * SHA-256 et HMAC sont écrits ici plutôt que tirés d'une bibliothèque : c'est
 * deux cents lignes sans dépendance, contre une dépendance de plus dans un
 * projet qui se veut reconstructible en une commande hors ligne. Elles sont
 * exposées pour être confrontées aux vecteurs de la RFC, sans quoi « ça compile »
 * serait la seule vérification d'un code dont l'erreur est silencieuse.
 * ------------------------------------------------------------------------- */
void ns_sha256(const void *data, size_t len, uint8_t out[32]);
void ns_hmac_sha256(const uint8_t *key, size_t key_len,
                    const void *data, size_t len, uint8_t out[32]);
size_t ns_base64_raw(const uint8_t *data, size_t len, char *out, size_t cap);

#endif /* NS_RUNLOG_H */
