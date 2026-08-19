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

/*
 * L'IDENTIFIANT que le serveur a donné à cette partie, et sans lequel elle
 * n'est envoyable nulle part.
 *
 * `POST /api/v1/runs/{id}/submit` : le sceau prouve QUE la partie est honnête,
 * l'identifiant dit DE QUELLE partie il s'agit. La file d'attente ne portait que
 * le premier — un fichier scellé qu'aucune adresse n'attendait, donc un envoi
 * qui ne pouvait pas aboutir même avec un serveur en face.
 *
 * Vide ou NULL : partie hors ligne, non soumettable, et `ns_runlog_enqueue` le
 * refuse comme il refuse déjà une partie sans secret.
 */
void ns_runlog_set_run_id(ns_runlog *r, const char *run_id);
const char *ns_runlog_run_id(const ns_runlog *r);

/*
 * Le JOURNAL D'ENTRÉES, à côté du journal d'événements.
 *
 * Deux journaux et pas un, parce qu'ils ne répondent pas à la même question. Le
 * journal d'événements enregistre des CONSÉQUENCES (`pipe`, `score`, `death`)
 * et sert à AUTHENTIFIER un score : c'est lui que le serveur recalcule et que
 * le sceau protège. Le journal d'entrées enregistre des APPUIS, et sert à
 * REJOUER une partie.
 *
 * J'ai longtemps écrit que le premier faisait le travail du second. C'est faux,
 * et ça se lit dans les données : on y trouve qu'un tuyau a été passé à
 * 2 340 ms, jamais que la barre d'espace a été pressée au tic 281.
 *
 * Ce que le rejeu vaut AUJOURD'HUI, sans aucun duel : une partie devient
 * reproductible. Un rapport de bug cesse d'être « ça a planté quelque part
 * après deux minutes » pour devenir un fichier qu'on rejoue. C'est pour ça que
 * cette moitié est écrite maintenant et que le réseau temps réel ne l'est pas.
 *
 * Le journal d'entrées n'entre PAS dans la charge canonique ni dans le sceau :
 * il n'a rien à prouver au serveur, et l'y mettre changerait un format que deux
 * langages tiennent d'accord.
 */
typedef struct ns_run_input {
    int32_t  tick;      /* numéro de pas fixe depuis le début de la partie */
    uint8_t  held;      /* masque des maintiens */
    uint8_t  pressed;   /* masque des appuis de ce pas */
} ns_run_input;

/*
 * Enregistre l'état des commandes à ce pas. N'écrit une ligne QUE si quelque
 * chose a changé depuis le pas précédent : une partie de trois minutes fait
 * 21 600 pas, et un joueur n'en change pas mille fois.
 */
void ns_runlog_input(ns_runlog *r, int32_t tick, uint8_t held, uint8_t pressed);

uint32_t             ns_runlog_input_count(const ns_runlog *r);
const ns_run_input  *ns_runlog_inputs(const ns_runlog *r);

/*
 * Écrit le journal d'entrées en texte, une ligne par changement :
 *   v1 <jeu> <difficulté> <graine>
 *   <tic> <maintiens> <appuis>
 *
 * Du texte parce qu'il se lit à l'œil quand on débogue, et qu'un journal
 * d'entrées de partie tient dans quelques kilo-octets — le compresser serait
 * optimiser ce qu'on n'a pas encore mesuré.
 */
bool ns_runlog_write_inputs(const ns_runlog *r, const char *path);

/*
 * Relit un journal d'entrées. Un enregistrement qu'on ne sait pas rejouer est
 * une moitié d'outil : c'est la lecture qui transforme le fichier en « je
 * reproduis ton bug » plutôt qu'en « j'ai ton fichier ».
 *
 * `out_input` est alloué par la fonction et revient à l'appelant, qui le libère
 * par `SDL_free`. Les trois champs d'en-tête sont rendus tels qu'ils étaient à
 * l'enregistrement — le jeu, la difficulté, la graine — parce que rejouer sur
 * une autre graine ne rejoue rien.
 */
bool ns_runlog_read_inputs(const char *path,
                           char *game, size_t game_cap,
                           char *difficulty, size_t difficulty_cap,
                           int64_t *seed,
                           ns_run_input **out_input, uint32_t *out_count);

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

/*
 * Lit un fichier de la file, rend son identifiant de partie et son corps JSON.
 * `body` reçoit ce qu'il faut poster tel quel. Renvoie false si le fichier est
 * illisible ou n'a pas d'identifiant — auquel cas il n'y a rien à en faire.
 */
bool ns_runlog_queue_read(const char *path, char *run_id, size_t run_id_cap,
                          char **body, size_t *body_len);

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
