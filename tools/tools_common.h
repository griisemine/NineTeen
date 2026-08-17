/*
 * tools_common.h — petites briques partagées par les outils hors-ligne.
 *
 * Les outils tournent au build, jamais dans le jeu : ils peuvent se permettre
 * d'allouer largement et de s'arrêter net sur une erreur. Ils ne dépendent ni
 * de SDL ni du moteur — seulement de la bibliothèque standard et de stb.
 */
#ifndef NS_TOOLS_COMMON_H
#define NS_TOOLS_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ sorties */

static inline void tool_infof(const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    fprintf(stdout, "  "); vfprintf(stdout, fmt, a); fprintf(stdout, "\n");
    va_end(a);
}

static inline void tool_warnf(const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    fprintf(stderr, "  attention : "); vfprintf(stderr, fmt, a); fprintf(stderr, "\n");
    va_end(a);
}

/* Un outil de build qui échoue doit casser le build, pas produire un asset
 * incomplet que l'on découvrira à l'exécution. */
static inline void tool_fatalf(const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    fprintf(stderr, "ERREUR : "); vfprintf(stderr, fmt, a); fprintf(stderr, "\n");
    va_end(a);
    exit(1);
}

/* ------------------------------------------------------- tableaux dynamiques */
/*
 * Un seul motif, générique par macro, pour éviter de réécrire dix fois la même
 * croissance géométrique — et surtout pour vérifier le débordement une fois.
 */
typedef struct tool_vec {
    void  *data;
    size_t count;
    size_t capacity;
    size_t elem_size;
} tool_vec;

static inline void tool_vec_init(tool_vec *v, size_t elem_size)
{
    v->data = NULL; v->count = 0; v->capacity = 0; v->elem_size = elem_size;
}

static inline void tool_vec_reserve(tool_vec *v, size_t need)
{
    if (need <= v->capacity) return;
    size_t cap = v->capacity ? v->capacity : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) tool_fatalf("tableau trop grand (%zu éléments)", need);
        cap *= 2;
    }
    void *p = realloc(v->data, cap * v->elem_size);
    if (!p) tool_fatalf("mémoire épuisée (%zu octets)", cap * v->elem_size);
    v->data = p;
    v->capacity = cap;
}

static inline void *tool_vec_push(tool_vec *v)
{
    tool_vec_reserve(v, v->count + 1);
    return (char *)v->data + (v->count++) * v->elem_size;
}

static inline void tool_vec_free(tool_vec *v)
{
    free(v->data);
    tool_vec_init(v, v->elem_size);
}

#define TOOL_VEC_AT(v, type, i) (((type *)(v)->data)[(i)])

/* -------------------------------------------------------------- lecture I/O */

static inline char *tool_read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    const long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); tool_fatalf("mémoire épuisée en lisant %s", path); }
    const size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    if (out_size) *out_size = read;
    return buf;
}

/* Retourne le répertoire contenant `path`, terminé par '/'. */
static inline void tool_dirname(const char *path, char *out, size_t out_size)
{
    const char *slash = NULL;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') slash = p;
    }
    if (!slash) { snprintf(out, out_size, "./"); return; }
    const size_t n = (size_t)(slash - path) + 1;
    if (n >= out_size) tool_fatalf("chemin trop long : %s", path);
    memcpy(out, path, n);
    out[n] = '\0';
}

/* Nom de fichier sans répertoire ni extension. */
static inline void tool_basename_noext(const char *path, char *out, size_t out_size)
{
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    const char *dot = strrchr(base, '.');
    const size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n >= out_size) tool_fatalf("nom trop long : %s", base);
    memcpy(out, base, n);
    out[n] = '\0';
}

/* --------------------------------------------------------------- découpage */

/* Avance jusqu'au prochain caractère non blanc (hors saut de ligne). */
static inline char *tool_skip_ws(char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    return p;
}

/* Copie le mot courant dans out et renvoie le pointeur après le mot. */
static inline char *tool_token(char *p, char *out, size_t out_size)
{
    p = tool_skip_ws(p);
    size_t n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (n + 1 < out_size) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return p;
}

/* Fin de ligne : renvoie le début de la ligne suivante, ou NULL à la fin. */
static inline char *tool_next_line(char *p)
{
    while (*p && *p != '\n') p++;
    return *p ? p + 1 : NULL;
}

#endif /* NS_TOOLS_COMMON_H */
