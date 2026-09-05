/*
 * ns_scores.h — les meilleurs scores, chez le joueur.
 *
 * La règle, et elle vient de la V1
 * -------------------------------
 * **Le jeu ne demande jamais de compte pour jouer.** C'était l'erreur de fond de
 * 2020 : tout le corps de `main()` était enfermé dans `if (checkVersion(...))`,
 * et sans réponse du serveur le jeu affichait « une nouvelle version est
 * disponible » puis se fermait. Hors ligne, la V1 ne pouvait pas atteindre sa
 * propre fenêtre.
 *
 * Ici le classement local est la source de vérité de ce que le joueur voit. Il
 * marche sans réseau, sans compte, sans serveur, et il marchera toujours ainsi :
 * le classement en ligne viendra **s'ajouter**, jamais se substituer.
 *
 * Ce que ce fichier fait, et ce qu'il ne fait pas
 * ----------------------------------------------
 * Il lit et écrit un fichier texte dans `ns_path_user_dir()`, atomiquement — un
 * temporaire puis un renommage, comme `ns_config`. Une coupure de courant pendant
 * l'écriture laisse l'ancien fichier intact plutôt qu'un fichier tronqué.
 *
 * Il ne parle à personne. La soumission en ligne vit dans `ns_runlog` (le journal
 * scellé) et dans la file d'attente qu'un transport futur videra.
 */
#ifndef NS_SCORES_H
#define NS_SCORES_H

#include <stdbool.h>
#include <stdint.h>

#define NS_SCORE_SLOTS      10    /* entrées gardées par tableau */
#define NS_SCORE_BOARDS     32    /* tableaux : un par (jeu, difficulté) */
#define NS_SCORE_NAME_MAX   24
#define NS_SCORE_KEY_MAX    40

typedef struct ns_score_entry {
    uint32_t score;
    uint32_t duration_ms;
    int64_t  when;                       /* epoch en secondes, 0 si inconnu */
    char     name[NS_SCORE_NAME_MAX];    /* vide = anonyme, ce qui est permis */
} ns_score_entry;

typedef struct ns_score_board {
    char           key[NS_SCORE_KEY_MAX];   /* « flappy/hard » */
    ns_score_entry entry[NS_SCORE_SLOTS];
    uint32_t       count;
} ns_score_board;

/*
 * Charge le fichier. Absent ou illisible, on repart d'un classement vide **sans
 * erreur** : un joueur qui lance le jeu pour la première fois n'a rien fait de
 * mal. Un fichier corrompu est signalé en WARN et ignoré ligne à ligne — perdre
 * une entrée vaut mieux que perdre le fichier.
 */
void ns_scores_load(void);

/* Écrit si quelque chose a changé. Sans effet sinon. */
bool ns_scores_save(void);

/*
 * Enregistre une partie. Renvoie le RANG obtenu (1 = meilleur) ou 0 si le score
 * n'entre pas dans le tableau — c'est ce que l'écran de fin veut afficher.
 *
 * `game` et `difficulty` forment la clé ; `name` peut être NULL ou vide.
 */
uint32_t ns_scores_record(const char *game, const char *difficulty,
                          uint32_t score, uint32_t duration_ms, const char *name);

/* Le tableau d'un couple (jeu, difficulté), ou NULL s'il n'existe pas encore. */
const ns_score_board *ns_scores_board(const char *game, const char *difficulty);

/* Le meilleur score connu, 0 si aucun. Raccourci pour l'attract mode. */
uint32_t ns_scores_best(const char *game, const char *difficulty);

/*
 * LA DIFFICULTÉ QUE DIT LA SALLE, ET CELLE QUI CLASSE.
 *
 * Une borne DÉCLARE sa difficulté dans `salle.room.json`, et elle la déclare
 * telle que l'enseigne la dit au joueur : le plan de 2020 sépare la salle en
 * EASY et HARD, et six caissons portent donc « easy ». Le jeu, lui, n'a que
 * deux régimes — `bool hard` — et enregistre sous « hard » ou « normal ».
 *
 * Les deux vocabulaires se sont croisés : le tableau des scores se range sur la
 * chaîne VERBATIM, si bien qu'une borne « easy » interrogeait un tableau
 * `<jeu>|easy` que rien n'écrit jamais, et n'affichait plus son meilleur score.
 * La faute ne se voit pas : il n'y a ni erreur ni tableau vide, seulement une
 * ligne qui disparaît de l'écran.
 *
 * Cette fonction est le SEUL endroit où la traduction a lieu. Tout ce qui va
 * chercher un score à partir d'une difficulté déclarée doit passer par elle —
 * l'affichage, le billet en ligne, la demande de fantômes.
 */
const char *ns_scores_bucket(const char *declared);

/* Tout effacer — pour les tests, et pour un futur réglage « remettre à zéro ». */
void ns_scores_clear(void);

/*
 * Chemin du fichier employé. Exposé pour les tests et pour le journal : c'est le
 * seul moyen honnête de vérifier qu'on écrit là où on dit.
 */
const char *ns_scores_path(void);

/*
 * Redirige le fichier — uniquement pour les tests, qui ne doivent pas toucher au
 * classement du joueur qui les exécute. NULL rétablit le chemin normal.
 */
void ns_scores_set_path(const char *path);

#endif /* NS_SCORES_H */
