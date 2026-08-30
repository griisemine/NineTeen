/*
 * demineur.h — le Démineur de 2020, porté.
 *
 * Ce qui est repris tel quel
 * --------------------------
 * La grille de **16 x 25**, les **100 bombes** (25 % des cases, comme
 * `NOMBRE_BOMBES_GRILLE`), les cases de 50 px, les huit états de case de
 * `config.h` et le découpage de `demineur.png` en tuiles de 54 x 54 — chaque
 * chiffre à la place exacte où l'original allait le chercher.
 *
 * La première case est toujours sûre
 * ----------------------------------
 * Les bombes ne sont posées qu'APRÈS le premier dévoilement, en évitant la case
 * jouée et ses voisines. C'est ce que fait l'original avec son `premier_click`,
 * et c'est la règle qui distingue un démineur d'une loterie : perdre au premier
 * coup n'apprend rien et ne se joue pas.
 *
 * L'adaptation qui compte : pas de souris
 * ---------------------------------------
 * L'original se joue à la souris. Une borne d'arcade n'en a pas — elle a un
 * manche et des boutons. Le curseur se déplace donc case par case au manche, et
 * le bouton dévoile ; **maintenir une direction fait défiler**, sinon traverser
 * vingt-cinq colonnes demanderait vingt-cinq appuis.
 *
 * C'est la seule liberté prise avec le jeu d'origine, et elle est prise parce
 * que le contraire — exiger une souris sur une borne — rendrait le jeu
 * injouable là où il est censé se jouer.
 */
#ifndef NS_DEMINEUR_H
#define NS_DEMINEUR_H

#include "games.h"
#include "ns_math.h"
#include "ns_sprite.h"

#include <stdbool.h>
#include <stdint.h>

#define DEM_ROWS 16          /* TAILLE_GRILLE_LIGNE */
#define DEM_COLS 25          /* TAILLE_GRILLE_COLONNE */
#define DEM_CELL 50.0f       /* TAILLE_CASE */
/*
 * LES BOMBES, ET POURQUOI ELLES NE SONT PLUS CENT PARTOUT.
 *
 * `NOMBRE_BOMBES_GRILLE` de 2020 vaut le quart des cases : cent bombes sur
 * quatre cents. C'est plus dense que la grille « expert » du démineur de
 * Windows (20,6 %), sur laquelle un humain gagne déjà rarement.
 *
 * Ce que ça donne, mesuré : deux cents parties jouées par le solveur du
 * fichier — celui qui déduit d'abord et ne devine qu'en dernier recours, en
 * choisissant la case la moins risquée — se soldent par **deux cents
 * défaites**. Zéro victoire. Durée moyenne : onze secondes. Autrement dit,
 * `PTS_WIN`, l'écran « GAGNE » et la branche `DEM_WON` étaient du code que
 * personne n'atteindrait jamais : le jeu n'avait pas de fin heureuse, il avait
 * une fin.
 *
 * La densité passe donc à 15 % sur la borne ordinaire — entre le « débutant »
 * (12,3 %) et l'« intermédiaire » (15,6 %) de la version de référence — et
 * garde les cent bombes de 2020 sur la borne « hard », qui les mérite. La
 * règle de 2020 n'est pas perdue : elle est devenue la difficulté qu'elle
 * décrivait.
 */
#define DEM_BOMBS_HARD ((DEM_ROWS * DEM_COLS) / 4)          /* 25 %, les 100 de 2020 */
#define DEM_BOMBS_EASY (((DEM_ROWS * DEM_COLS) * 15) / 100) /* 15 %, soit 60 */


/* Le repère logique : la grille de 1250 x 800 centrée dans 1920 x 1080. */
#define DEM_W 1920.0f
#define DEM_H 1080.0f

typedef enum dem_phase { DEM_READY = 0, DEM_PLAYING, DEM_DEAD, DEM_WON } dem_phase;

typedef struct demineur {
    dem_phase phase;

    /* `bomb` : une bombe est cachée là. `shown` : la case est dévoilée.
     * `flag` : le joueur y a posé un drapeau. Trois booléens valent mieux que
     * les huit états de 2020 — qui mélangeaient le contenu, l'affichage et le
     * voisinage dans une seule énumération. */
    bool bomb[DEM_ROWS][DEM_COLS];
    bool shown[DEM_ROWS][DEM_COLS];
    bool flag[DEM_ROWS][DEM_COLS];

    int  cursor_r, cursor_c;
    float repeat;           /* temporisation du défilement au maintien */
    bool  held[NS_GAME_BUTTON_COUNT];

    bool  placed;           /* les bombes sont posées (après le 1er dévoilement) */
    uint32_t revealed;      /* cases sûres découvertes */
    uint32_t flags;

    float time;
    float dead_time;
    float robot_wait;       /* la cadence du joueur automatique, voir demineur.c */

    int64_t  score;
    uint32_t best;
    bool     hard;          /* la borne « hard » retire les drapeaux */

    ns_rng rng;

    /*
     * Les gains EN ATTENTE, et pourquoi il faut une file là où les deux jeux
     * précédents se contentaient d'un booléen.
     *
     * `ns_game_events` rend UN gain par appel, ce qui suffit à Flappy et à
     * Snake : on passe un tuyau, on mange un fruit, un gain à la fois. Le
     * Démineur, lui, ouvre une PLAGE — un seul appui dévoile jusqu'à trois
     * cents cases, et la dernière de la cascade peut gagner la partie.
     *
     * Un booléen aurait annoncé « une case ouverte » là où le score en comptait
     * cent. Or le serveur ne croit pas le score : il le RECALCULE à partir du
     * journal (`server/internal/runs/runs.go`). Une partie affichée à 1 500
     * points aurait été classée à 5. D'où deux choses : « cell » porte sa
     * quantité, et la fin de partie n'est annoncée qu'une fois la file vide,
     * parce que `finish_run` scelle le journal et qu'un gain annoncé après lui
     * serait perdu.
     */
    uint32_t pend_cells;    /* cases ouvertes, pas encore journalisées */
    uint32_t pend_flags;
    bool     pend_win;
    bool     over;          /* la partie est finie et ne l'a pas encore dit */

    bool     moved;         /* le curseur a bougé — consommé par `events` */
} demineur;

typedef struct demineur_art {
    ns_texture tiles;
    bool ready;
} demineur_art;

bool demineur_art_load(ns_rhi *r, demineur_art *a);
void demineur_art_free(ns_rhi *r, demineur_art *a);

/* Le compte de bombes de la partie en cours : il dépend de la difficulté.
 * Exposé parce que c'est une RÈGLE — la condition de victoire et le calcul de
 * risque du solveur en dépendent tous les deux, et le test la vérifie. */
int demineur_bombs(const demineur *g);

void demineur_reset(demineur *g, uint64_t seed, bool hard);
void demineur_press(demineur *g, ns_game_button b);
void demineur_hold(demineur *g, const bool held[NS_GAME_BUTTON_COUNT]);
void demineur_tick(demineur *g, float dt);
void demineur_draw(ns_sprite *s, const demineur *g, const demineur_art *a,
                   float logical_w, float logical_h);
bool demineur_autopilot(demineur *g);

extern const ns_game_api g_demineur_api;

#endif /* NS_DEMINEUR_H */
