/*
 * ns_alloc.c — arènes et allocation tracée.
 *
 * Rappel du défaut corrigé ici : la V1 avait dans include/communFunctions.c un
 * helper d'allocation qui appliquait `sizeof` à une *valeur* entière au lieu du
 * type demandé, réservant donc systématiquement 4 ou 8 octets. Les écritures
 * suivantes débordaient du bloc. Ce fichier rend ce genre d'erreur impossible :
 * les macros NS_ARENA_NEW / NS_ARENA_ARRAY dérivent taille et alignement du type.
 */
#include "ns_core.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/* Arènes                                                                     */
/* ========================================================================== */

bool ns_arena_init(ns_arena *a, size_t capacity, const char *name)
{
    NS_ASSERT(a != NULL);
    NS_ASSERT(capacity > 0);

    a->base = (uint8_t *)ns_alloc(capacity);
    if (!a->base) {
        NS_ERROR("arène « %s » : %zu octets refusés", name ? name : "?", capacity);
        return false;
    }
    a->capacity = capacity;
    a->used     = 0;
    a->peak     = 0;
    a->name     = name ? name : "anonyme";
    NS_DEBUG("arène « %s » : %zu Kio", a->name, capacity / 1024);
    return true;
}

void ns_arena_free(ns_arena *a)
{
    if (!a || !a->base) return;
    NS_DEBUG("arène « %s » libérée (pic %zu / %zu Kio, %.0f %%)",
             a->name, a->peak / 1024, a->capacity / 1024,
             a->capacity ? (100.0 * (double)a->peak / (double)a->capacity) : 0.0);
    ns_free(a->base);
    a->base = NULL;
    a->capacity = a->used = a->peak = 0;
}

void *ns_arena_alloc(ns_arena *a, size_t size, size_t align)
{
    NS_ASSERT(a != NULL && a->base != NULL);
    NS_ASSERT(align > 0 && (align & (align - 1)) == 0); /* puissance de deux */

    if (size == 0) return NULL;

    const size_t offset  = ((uintptr_t)a->base + a->used + (align - 1)) & ~(uintptr_t)(align - 1);
    const size_t aligned = offset - (uintptr_t)a->base;

    /* Le dépassement de capacité est un bug de dimensionnement, pas une condition
     * d'exécution normale : on le signale fort plutôt que de renvoyer NULL et de
     * laisser l'appelant déréférencer. */
    if (aligned + size > a->capacity) {
        NS_ERROR("arène « %s » saturée : %zu octets demandés, %zu disponibles (capacité %zu)",
                 a->name, size, a->capacity - aligned, a->capacity);
        NS_ASSERT_MSG(false, "arène saturée — augmenter la capacité au point d'initialisation");
        return NULL;
    }

    void *p = a->base + aligned;
    a->used = aligned + size;
    if (a->used > a->peak) a->peak = a->used;
    return p;
}

void ns_arena_reset(ns_arena *a)
{
    NS_ASSERT(a != NULL);
    a->used = 0;
}

/* ========================================================================== */
/* Allocation générale tracée                                                 */
/* ========================================================================== */
/*
 * En-tête devant chaque bloc pour connaître sa taille au free, et deux canaris
 * pour détecter les débordements. Le coût est négligeable face au bénéfice :
 * l'audit du code d'origine a relevé plusieurs écritures hors bornes.
 */
#define NS_ALLOC_CANARY 0xA5C3F00Du

typedef struct alloc_header {
    size_t   size;
    uint32_t canary;
    uint32_t _pad;
} alloc_header;

static SDL_AtomicInt g_live_blocks;
static size_t        g_live_bytes;   /* protégé par g_alloc_mutex */
static SDL_Mutex    *g_alloc_mutex;

static void alloc_mutex_ensure(void)
{
    if (!g_alloc_mutex) g_alloc_mutex = SDL_CreateMutex();
}

static void account(ptrdiff_t bytes, int blocks)
{
    alloc_mutex_ensure();
    SDL_LockMutex(g_alloc_mutex);
    if (bytes < 0) {
        const size_t d = (size_t)(-bytes);
        g_live_bytes = (g_live_bytes >= d) ? g_live_bytes - d : 0;
    } else {
        g_live_bytes += (size_t)bytes;
    }
    SDL_UnlockMutex(g_alloc_mutex);
    SDL_AddAtomicInt(&g_live_blocks, blocks);
}

void *ns_alloc(size_t size)
{
    if (size == 0) return NULL;
    if (size > SIZE_MAX - sizeof(alloc_header)) return NULL;   /* débordement du calcul */

    alloc_header *h = (alloc_header *)malloc(sizeof(alloc_header) + size);
    if (!h) {
        NS_ERROR("allocation de %zu octets refusée", size);
        return NULL;
    }
    h->size   = size;
    h->canary = NS_ALLOC_CANARY;
    h->_pad   = 0;
    account((ptrdiff_t)size, 1);
    return (void *)(h + 1);
}

void *ns_calloc(size_t count, size_t size)
{
    if (count == 0 || size == 0) return NULL;
    if (count > SIZE_MAX / size) {                             /* débordement multiplicatif */
        NS_ERROR("ns_calloc : %zu * %zu déborde", count, size);
        return NULL;
    }
    const size_t total = count * size;
    void *p = ns_alloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *ns_realloc(void *ptr, size_t size)
{
    if (!ptr) return ns_alloc(size);
    if (size == 0) { ns_free(ptr); return NULL; }

    alloc_header *h = ((alloc_header *)ptr) - 1;
    NS_ASSERT_MSG(h->canary == NS_ALLOC_CANARY, "bloc corrompu ou pointeur étranger passé à ns_realloc");
    const size_t old = h->size;

    alloc_header *nh = (alloc_header *)realloc(h, sizeof(alloc_header) + size);
    if (!nh) {
        NS_ERROR("réallocation de %zu octets refusée", size);
        return NULL;
    }
    nh->size = size;
    account((ptrdiff_t)size - (ptrdiff_t)old, 0);
    return (void *)(nh + 1);
}

void ns_free(void *ptr)
{
    if (!ptr) return;
    alloc_header *h = ((alloc_header *)ptr) - 1;
    NS_ASSERT_MSG(h->canary == NS_ALLOC_CANARY, "double libération, bloc corrompu, ou pointeur étranger");
    account(-(ptrdiff_t)h->size, -1);
    h->canary = 0;   /* rend la double libération détectable */
    free(h);
}

size_t ns_alloc_live_bytes(void)
{
    alloc_mutex_ensure();
    SDL_LockMutex(g_alloc_mutex);
    const size_t v = g_live_bytes;
    SDL_UnlockMutex(g_alloc_mutex);
    return v;
}

size_t ns_alloc_live_blocks(void)
{
    return (size_t)SDL_GetAtomicInt(&g_live_blocks);
}
