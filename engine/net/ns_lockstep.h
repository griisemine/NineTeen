/*
 * ns_lockstep.h — le duel EN DIRECT : deux parties, un seul pas.
 *
 * Ce que c'est
 * ------------
 * Un pas verrouillé (« lockstep ») : les deux joueurs ne simulent le pas T que
 * lorsque les DEUX entrées du pas T sont connues. Personne ne prédit, personne
 * ne rembobine, et les deux parties sont la MÊME partie — ce qui est la seule
 * définition honnête d'un duel où l'on se compare.
 *
 * Pourquoi ce n'est pas la même chose que le reste du réseau
 * ----------------------------------------------------------
 * `ns_http` ouvre une socket par requête. C'est parfait pour un score toutes les
 * trois minutes et une présence à 4 Hz ; c'est disqualifiant à 120 Hz. Le duel
 * en direct demande donc une socket PERSISTANTE, un protocole binaire, un
 * tampon d'entrées et une politique quand le paquet est en retard. Rien de tout
 * ça n'a sa place dans un client HTTP, et l'y mettre l'aurait abîmé.
 *
 * Les trois conditions, et où elles sont remplies
 * -----------------------------------------------
 * `docs/RESEAU-TEMPS-REEL.md` les pose dans l'ordre, et aucune n'est supposée :
 *
 *   1. **Une empreinte d'état** — `ns_game_state_hash`, FNV-1a sur les
 *      `state_size` octets. Sans elle, deux parties qui divergent continuent
 *      chacune de leur côté jusqu'à ce que les scores se contredisent.
 *   2. **Le déterminisme entre architectures, mesuré** — 8 jeux sur 8 rendent
 *      la même empreinte en arm64 et en x86_64 sur le même journal. C'est cette
 *      mesure qui a trouvé le `const char *` caché dans l'état de Piano.
 *   3. **Ce transport-ci.**
 *
 * Ce qu'il fait quand ça diverge quand même
 * ------------------------------------------
 * Il S'ARRÊTE. Les empreintes sont échangées tous les `NS_LOCKSTEP_HASH_EVERY`
 * pas ; au premier désaccord, le duel est déclaré rompu et les deux joueurs le
 * savent. Livrer un duel qui diverge en silence serait pire que de ne pas en
 * livrer : le joueur perdrait des parties sans savoir pourquoi.
 *
 * Le serveur n'arbitre pas
 * ------------------------
 * Le relais Go ne fait que RECOPIER les trames d'un pair vers l'autre. Il ne
 * simule rien, ne valide rien, ne connaît aucune règle de jeu. L'autorité sur
 * les scores reste là où elle est depuis M6 : le journal scellé par HMAC, envoyé
 * par HTTP, et recalculé par le serveur. Un relais qui arbitrerait serait une
 * deuxième autorité, donc une deuxième surface à défendre.
 *
 * Rien ne s'ouvre sans qu'on le demande
 * --------------------------------------
 * `ns_lockstep_connect` est le seul point qui ouvre une socket, et il n'est
 * appelé que depuis le duel. La garantie « sans URL configurée, aucune socket
 * n'est ouverte » tient donc telle quelle.
 */
#ifndef NS_LOCKSTEP_H
#define NS_LOCKSTEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Le RETARD d'entrée, en pas de simulation.
 *
 * L'entrée saisie au pas T n'est consommée qu'au pas T + NS_LOCKSTEP_DELAY.
 * C'est ce qui donne au réseau le temps de livrer sans que la simulation
 * s'arrête : à 120 Hz, huit pas font 66 ms, soit un aller-retour confortable
 * sur un réseau local ou une liaison nationale.
 *
 * Le prix est une latence de commande de 66 ms, et il est ASSUMÉ : c'est le
 * choix classique du pas verrouillé, et il vaut mieux qu'une prédiction suivie
 * d'un rembobinage, qui demanderait de pouvoir revenir en arrière dans l'état
 * d'un jeu — ce que l'interface de `games.h` ne promet pas.
 */
#define NS_LOCKSTEP_DELAY 8

/* Les empreintes sont comparées tous les seize pas — sept fois par seconde à
 * 120 Hz. Assez souvent pour attraper une divergence avant qu'elle ne change le
 * score, assez rare pour peser moins d'un pour mille de la bande passante. */
#define NS_LOCKSTEP_HASH_EVERY 16

typedef enum ns_lockstep_state {
    NS_LOCKSTEP_OFF = 0,      /* rien n'est ouvert */
    NS_LOCKSTEP_WAITING,      /* connecté, en attente du second joueur */
    NS_LOCKSTEP_RUNNING,      /* les deux sont là, la partie court */
    NS_LOCKSTEP_ENDED,        /* l'autre est parti proprement */
    NS_LOCKSTEP_DESYNC,       /* les états ont divergé : le duel est rompu */
    NS_LOCKSTEP_ERROR         /* réseau : voir `ns_lockstep_error()` */
} ns_lockstep_state;

typedef struct ns_lockstep ns_lockstep;

/*
 * Ouvre la liaison et annonce le duel. `host`/`port` désignent le relais.
 *
 * `duel_id` est convenu hors bande (par le classement, ou à la main pour un
 * essai) : le relais apparie les deux clients qui présentent le même. `slot`
 * vaut 0 ou 1 et dit qui est qui — le relais refuse deux fois le même.
 *
 * Ne bloque pas plus de `timeout_ms`. Rend NULL en cas d'échec, avec le motif
 * dans `error`.
 */
ns_lockstep *ns_lockstep_connect(const char *host, uint16_t port,
                                 uint64_t duel_id, int slot,
                                 const char *game, const char *difficulty,
                                 uint32_t timeout_ms,
                                 char *error, size_t error_size);

void ns_lockstep_close(ns_lockstep *ls);

/* Draine ce qui est arrivé. À appeler une fois par image, jamais bloquant. */
void ns_lockstep_poll(ns_lockstep *ls);

ns_lockstep_state ns_lockstep_status(const ns_lockstep *ls);
const char       *ns_lockstep_error(const ns_lockstep *ls);

/* La graine du duel, décidée par le relais et identique des deux côtés. Ne vaut
 * quelque chose qu'une fois l'état passé à RUNNING. */
uint64_t ns_lockstep_seed(const ns_lockstep *ls);

/* Publie l'entrée du pas `tick`. À appeler une fois par pas, dans l'ordre. */
void ns_lockstep_send_input(ns_lockstep *ls, int32_t tick,
                            uint8_t held, uint8_t pressed);

/*
 * L'entrée de l'ADVERSAIRE au pas `tick`, si elle est arrivée.
 *
 * Rend `false` tant qu'elle manque — et c'est le cœur du pas verrouillé :
 * l'appelant ne doit PAS avancer sa simulation tant que ce `false` dure. Une
 * simulation qui avance sans l'entrée de l'autre n'est plus la même partie.
 */
bool ns_lockstep_peer_input(const ns_lockstep *ls, int32_t tick,
                            uint8_t *held, uint8_t *pressed);

/*
 * Publie l'empreinte de SA partie au pas `tick`. N'ENVOIE que ça.
 *
 * La comparaison est séparée, et ce n'est pas une coquetterie : un duel peut
 * prendre deux formes, et le transport n'a pas à présumer laquelle.
 *
 *   - Deux parties qui n'en font qu'UNE (pas verrouillé pur) : les deux états
 *     doivent être égaux, donc on vérifie avec sa propre empreinte.
 *   - Deux parties SÉPARÉES sur la même graine, chacune rejouant celle de
 *     l'autre — la forme qu'a ce jeu, où l'on court l'un contre l'autre. Là,
 *     l'empreinte reçue doit correspondre à celle du FANTÔME qu'on tient de
 *     l'adversaire, pas à celle de sa propre partie.
 *
 * Un module qui comparerait tout seul imposerait la première forme.
 */
void ns_lockstep_publish_hash(ns_lockstep *ls, int32_t tick, uint64_t hash);

/*
 * Confronte `expected` à l'empreinte que le pair a publiée pour ce pas.
 *
 * Sans effet tant qu'elle n'est pas arrivée : on ne conclut jamais d'une
 * absence. Passe l'état à `NS_LOCKSTEP_DESYNC` au premier désaccord, et le dit
 * à l'autre.
 */
void ns_lockstep_verify_peer(ns_lockstep *ls, int32_t tick, uint64_t expected);

/* Le pas où la divergence a été constatée, ou -1. */
int32_t ns_lockstep_desync_tick(const ns_lockstep *ls);

/* Dit à l'autre qu'on s'arrête, proprement. */
void ns_lockstep_say_bye(ns_lockstep *ls);

/* Compteurs, pour les tests et le journal. */
void ns_lockstep_stats(const ns_lockstep *ls,
                       uint32_t *frames_in, uint32_t *frames_out,
                       uint32_t *stalls);

#endif /* NS_LOCKSTEP_H */
