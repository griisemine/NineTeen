/*
 * games.h — l'interface commune des mini-jeux.
 *
 * Pourquoi elle arrive maintenant
 * -------------------------------
 * Flappy Bird a été porté en premier comme tranche verticale : il fallait
 * qu'UN jeu marche de bout en bout — la borne, le jeton, l'écran, le score, le
 * journal scellé — avant de généraliser quoi que ce soit. Le résultat est que
 * `room/main.c` nomme « flappy » à quinze endroits.
 *
 * Sept jeux restent à porter. Les ajouter sur ce modèle demanderait quinze
 * modifications de `main.c` par jeu, dans un fichier qui gère déjà la salle, la
 * caméra, le son, le rendu et le classement. Cette interface remplace les
 * quinze par une ligne de table.
 *
 * Ce qu'elle N'est PAS
 * -------------------
 * Un moteur de jeu. Il n'y a ni entités, ni composants, ni scène : un mini-jeu
 * d'arcade est un état qu'on avance d'un pas fixe et qu'on dessine. Les huit de
 * 2020 ont exactement cette forme, et c'est la seule chose qu'on abstrait.
 *
 * Les règles que tout jeu doit tenir
 * ----------------------------------
 * 1. **Aucun appel SDL, aucun accès disque hors `art_load`.** C'est ce qui rend
 *    un jeu vérifiable sans écran.
 * 2. **Aucun état global.** Tout vit dans le bloc rendu par `state_size`.
 * 3. **`tick` reçoit un pas CONSTANT.** La partie est donc rejouable à la graine
 *    près — ce qui est la condition pour qu'un journal de partie signé veuille
 *    dire quelque chose, et pour qu'un duel soit un jour possible.
 * 4. **Les événements sont lus, pas joués.** Le jeu dit « un point », « touché »,
 *    « fini » ; c'est l'appelant qui connaît le mixeur.
 */
#ifndef NS_GAMES_H
#define NS_GAMES_H

#include "ns_rhi.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Les entrées d'une borne d'arcade, et rien de plus : un manche à quatre
 * directions et un bouton. C'est ce qu'il y a sur le panneau de commande, donc
 * c'est ce qu'un jeu peut lire — et ça garantit que tout jeu porté reste
 * jouable à la borne, pas seulement au clavier.
 */
typedef enum ns_game_button {
    NS_GAME_UP = 0,
    NS_GAME_DOWN,
    NS_GAME_LEFT,
    NS_GAME_RIGHT,
    NS_GAME_ACTION,
    NS_GAME_BUTTON_COUNT
} ns_game_button;

/*
 * Ce que l'image vient de produire.
 *
 * `events` CONSOMME : chaque drapeau est rendu une fois et une seule, quel que
 * soit le moment où il a été levé — pendant `tick` ou pendant `press`. C'est le
 * seul contrat qui marche, parce que `press` est appelée depuis le gestionnaire
 * d'événements, donc avant la boucle de pas fixe. Un jeu qui remettait ses
 * drapeaux à zéro en tête de `tick` perdait tout ce que `press` avait levé.
 *
 * Les NOMS d'événements viennent du jeu, et ce n'est pas un détail de style :
 * ils forment le vocabulaire que le serveur Go accepte, jeu par jeu
 * (`server/internal/runs/runs.go`, table `rulesTable`). Un nom générique côté
 * client — « score » au lieu de « pipe » — fait rejeter la partie ENTIÈRE, avec
 * pour seul retour « événement inconnu ». C'est exactement ce qui se passait.
 *
 * `tests/test_scores.c` confronte le vocabulaire de chaque jeu porté à celui du
 * serveur. Les deux tables sont écrites dans deux langages ; la seule chose qui
 * les tienne d'accord est ce test.
 */
typedef struct ns_game_events {
    bool        blip;        /* un geste : battement d'aile, virage, rotation */
    const char *blip_kind;   /* « flap », « turn »… */

    bool        score;       /* un gain */
    const char *score_kind;  /* « pipe », « fruit », « bonus »… */
    int64_t     score_value; /* la quantité, pour les barèmes proportionnels */

    bool        die;
} ns_game_events;

typedef struct ns_game_api {
    const char *id;       /* la clé du classement ET celle des bornes */
    const char *title;    /* affiché en toutes lettres */
    const char *label;    /* 6 caractères au plus : la colonne du classement */

    size_t state_size;
    size_t art_size;

    /* Les trois sons, cherchés par `ns_audio_load`. NULL = pas de son. */
    const char *sound_blip;
    const char *sound_score;
    const char *sound_die;

    bool (*art_load)(ns_rhi *r, void *art);
    void (*art_free)(ns_rhi *r, void *art);

    void (*reset)(void *g, uint64_t seed, bool hard);
    /* Un appui. Les maintiens se lisent par `hold`, appelé avant chaque pas. */
    void (*press)(void *g, ns_game_button b);
    void (*hold)(void *g, const bool held[NS_GAME_BUTTON_COUNT]);
    void (*tick)(void *g, float dt);
    void (*draw)(ns_sprite *s, const void *g, const void *art,
                 float logical_w, float logical_h);

    /* Un joueur automatique, pour les captures et l'intégration continue. Il ne
     * prouve pas que le jeu est amusant — il prouve qu'on peut y jouer plusieurs
     * milliers de pas sans NaN, sans fuite et avec un score qui monte. */
    bool (*autopilot)(void *g);

    /* Le vocabulaire d'événements que ce jeu peut émettre, terminé par NULL.
     * Sert au test qui le confronte à la table du serveur. */
    const char *const *event_kinds;

    uint32_t (*score)(const void *g);
    uint32_t (*best)(const void *g);
    void     (*set_best)(void *g, uint32_t best);
    /* `dead_time` : secondes écoulées depuis la mort, pour temporiser la
     * relance. Un appui maintenu au moment du choc ne doit pas redémarrer avant
     * qu'on ait vu ce qui s'est passé. */
    bool (*dead)(const void *g, float *dead_time);
    void (*events)(void *g, ns_game_events *out);
} ns_game_api;

/* Les jeux portés, dans l'ordre où ils apparaissent au classement. */
/*
 * L'empreinte de l'état d'une partie.
 *
 * FNV-1a sur les `state_size` octets du bloc. C'est la PREMIÈRE brique d'un duel
 * en pas verrouillé, et `docs/RESEAU-TEMPS-REEL.md` le dit dans cet ordre :
 * sans elle, deux parties qui divergent continuent chacune de leur côté jusqu'à
 * ce que les scores se contredisent, et le défaut devient indébogable.
 *
 * Elle n'est légitime que parce qu'une propriété est MESURÉE ailleurs :
 * `tests/test_replay.c` compare les états entiers au bit près sur les huit jeux,
 * ce qui établit qu'un état ne contient ni pointeur, ni bourrage indéterminé, ni
 * rémanence entre deux parties. Sans cette mesure, une empreinte d'octets
 * comparerait du bruit.
 *
 * FNV-1a et non un condensé cryptographique : on cherche à détecter une
 * divergence entre deux machines de bonne foi, pas à résister à un adversaire.
 * Le score, lui, reste scellé par HMAC et recalculé par le serveur.
 */
uint64_t ns_game_state_hash(const ns_game_api *api, const void *state);

const ns_game_api *ns_game_find(const char *id);   /* NULL si non porté */
const ns_game_api *ns_game_at(int index);          /* NULL au-delà */
int                ns_game_count(void);

#endif /* NS_GAMES_H */
