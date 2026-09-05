/*
 * ns_core.h — briques de base du moteur Nineteen.
 *
 * Ce qui manquait à la V1 et qui a coûté cher :
 *   - aucune journalisation structurée (des printf éparpillés) ;
 *   - malloc/free au fil de l'eau, avec un `_malloc` qui calculait la taille avec
 *     un sizeof sur une valeur entière — donc des allocations trop petites ;
 *   - des chemins d'assets relatifs au répertoire courant ("../room/textures/...") ;
 *   - un temps de simulation lié au framerate.
 *
 * Tout est en C11, sans dépendance autre que SDL3 pour l'horloge et les chemins.
 */
#ifndef NS_CORE_H
#define NS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================== */
/* Journalisation                                                             */
/* ========================================================================== */

typedef enum ns_log_level {
    NS_LOG_TRACE = 0,
    NS_LOG_DEBUG,
    NS_LOG_INFO,
    NS_LOG_WARN,
    NS_LOG_ERROR,
    NS_LOG_FATAL
} ns_log_level;

void ns_log_set_level(ns_log_level min_level);

/* Écrit aussi dans un fichier si un chemin est fourni (NULL = console seule). */
bool ns_log_open_file(const char *path);
void ns_log_close_file(void);

void ns_log_write(ns_log_level level, const char *file, int line, const char *fmt, ...);

#define NS_TRACE(...) ns_log_write(NS_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define NS_DEBUG(...) ns_log_write(NS_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define NS_INFO(...)  ns_log_write(NS_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define NS_WARN(...)  ns_log_write(NS_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define NS_ERROR(...) ns_log_write(NS_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define NS_FATAL(...) ns_log_write(NS_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

/* Assertion active en Debug comme en Release : une invariante violée est un bug
 * qu'on veut voir en production, pas un comportement indéfini silencieux. */
void ns_assert_failed(const char *expr, const char *file, int line, const char *msg);
#define NS_ASSERT(expr) \
    do { if (!(expr)) ns_assert_failed(#expr, __FILE__, __LINE__, NULL); } while (0)
#define NS_ASSERT_MSG(expr, msg) \
    do { if (!(expr)) ns_assert_failed(#expr, __FILE__, __LINE__, (msg)); } while (0)

/* ========================================================================== */
/* Allocation par arène                                                       */
/* ========================================================================== */
/*
 * Le moteur alloue par régions dont la durée de vie est connue (durée de la
 * frame, durée d'un niveau, durée du programme) plutôt que par objets.
 * Un `ns_arena_reset` libère tout d'un coup : plus de fuite possible par oubli
 * de free, et l'ordre d'allocation devient contigu en mémoire (donc rapide).
 */
typedef struct ns_arena {
    uint8_t *base;
    size_t   capacity;
    size_t   used;
    size_t   peak;      /* pour dimensionner correctement, mesures à l'appui */
    const char *name;
} ns_arena;

bool  ns_arena_init(ns_arena *a, size_t capacity, const char *name);
void  ns_arena_free(ns_arena *a);
void *ns_arena_alloc(ns_arena *a, size_t size, size_t align);
void  ns_arena_reset(ns_arena *a);

#define NS_ARENA_NEW(arena, type)        ((type *)ns_arena_alloc((arena), sizeof(type), _Alignof(type)))
#define NS_ARENA_ARRAY(arena, type, n)   ((type *)ns_arena_alloc((arena), sizeof(type) * (size_t)(n), _Alignof(type)))

/* Marque/restauration : allocation temporaire dans une arène longue durée. */
typedef struct ns_arena_mark { size_t used; } ns_arena_mark;
static inline ns_arena_mark ns_arena_save(const ns_arena *a) { ns_arena_mark m = { a->used }; return m; }
static inline void ns_arena_restore(ns_arena *a, ns_arena_mark m) { a->used = m.used; }

/* ========================================================================== */
/* Allocation générale tracée                                                 */
/* ========================================================================== */
/*
 * Pour ce qui ne peut pas vivre dans une arène. Contrairement au `_malloc` de la
 * V1, la taille demandée est la taille réellement allouée, le retour est vérifié,
 * et le total est comptabilisé pour repérer les fuites en fin de programme.
 */
void  *ns_alloc(size_t size);
void  *ns_calloc(size_t count, size_t size);
void  *ns_realloc(void *ptr, size_t size);
void   ns_free(void *ptr);
size_t ns_alloc_live_bytes(void);
size_t ns_alloc_live_blocks(void);

/* ========================================================================== */
/* Temps et boucle à pas fixe                                                 */
/* ========================================================================== */
/*
 * La V1 faisait avancer la simulation d'un cran par image affichée, avec des
 * SDL_Delay pour freiner. Résultat : la physique dépendait de la machine, ce que
 * l'historique du dépôt documente en bugs de collision (Flappy, Snake, Asteroid).
 *
 * Ici la simulation avance par pas fixes (défaut 120 Hz) et le rendu interpole
 * entre les deux derniers états. Le comportement du jeu devient identique à
 * 30 comme à 240 images par seconde, et reproductible pour les tests.
 */
#define NS_DEFAULT_TICK_HZ 120.0

typedef struct ns_clock {
    double tick_seconds;     /* durée d'un pas de simulation */
    double accumulator;      /* temps en attente de simulation */
    double alpha;            /* [0,1] interpolation pour le rendu */
    uint64_t frame_index;
    uint64_t tick_index;
    uint64_t last_counter;
    uint64_t counter_freq;
    double   max_frame_seconds; /* garde-fou anti « spirale de la mort » */
    /* Mesures */
    double frame_seconds;
    double fps_smoothed;
} ns_clock;

void ns_clock_init(ns_clock *c, double tick_hz);

/* À appeler une fois par image : met à jour frame_seconds et remplit l'accumulateur. */
void ns_clock_begin_frame(ns_clock *c);

/*
 * Variante à durée IMPOSÉE, pour l'écriture d'une séquence d'images.
 *
 * Une séquence capturée hors écran rend aussi vite que la machine le permet :
 * mesuré ici, 760 images par seconde en headless sur Metal. L'horloge murale
 * ferait donc avancer la simulation de 1,3 ms par image, et 200 images
 * filmeraient un quart de seconde de jeu — un ralenti de facteur 25, dont le
 * facteur dépendrait de la machine qui a lancé la capture. En imposant dt on
 * obtient l'inverse : la même durée filmée partout, et un film dont la vitesse
 * est celle qu'on a demandée.
 */
void ns_clock_begin_frame_fixed(ns_clock *c, double dt);

/* Boucle : while (ns_clock_consume_tick(c)) { simuler(c->tick_seconds); } */
bool ns_clock_consume_tick(ns_clock *c);

/* Après la boucle de ticks : alpha pour l'interpolation du rendu. */
void ns_clock_end_frame(ns_clock *c);

double ns_time_seconds(void);

/* ========================================================================== */
/* Chemins de données                                                         */
/* ========================================================================== */
/*
 * Remplace legacy/include/fullpath.c. Résout un chemin logique
 * ("scene/salle.gltf") vers un fichier réel, en cherchant dans l'ordre :
 *   1. le répertoire d'assets généré (build) ;
 *   2. le répertoire d'assets source ;
 *   3. le répertoire du binaire (paquet installé) ;
 * ce qui permet de lancer le jeu depuis l'arbre de build comme depuis un paquet.
 */
bool ns_paths_init(const char *argv0);
void ns_paths_shutdown(void);

/* Ajoute un point de montage. Priorité décroissante selon l'ordre d'ajout. */
bool ns_paths_mount(const char *directory);

/* Écrit le chemin résolu dans out. Retourne false si le fichier n'existe nulle part. */
bool ns_path_resolve(const char *logical, char *out, size_t out_size);

/* Répertoire inscriptible propre à l'utilisateur (config, journaux, sauvegardes). */
const char *ns_path_user_dir(void);

/* Lecture complète d'un fichier logique. Le tampon est alloué dans l'arène. */
void *ns_file_read_all(ns_arena *arena, const char *logical, size_t *out_size);

#endif /* NS_CORE_H */
