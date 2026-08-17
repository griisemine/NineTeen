/* ns_log.c — journalisation. */
#include "ns_core.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ns_log_level g_min_level = NS_LOG_INFO;
static SDL_IOStream *g_file = NULL;
static SDL_Mutex    *g_mutex = NULL;

static const char *level_name(ns_log_level l)
{
    switch (l) {
    case NS_LOG_TRACE: return "TRACE";
    case NS_LOG_DEBUG: return "DEBUG";
    case NS_LOG_INFO:  return "INFO ";
    case NS_LOG_WARN:  return "WARN ";
    case NS_LOG_ERROR: return "ERROR";
    case NS_LOG_FATAL: return "FATAL";
    }
    return "?????";
}

/* Couleurs ANSI seulement si la sortie est un terminal : un fichier de log ne
 * doit pas se remplir de séquences d'échappement. */
static const char *level_color(ns_log_level l)
{
    switch (l) {
    case NS_LOG_TRACE: return "\x1b[90m";
    case NS_LOG_DEBUG: return "\x1b[36m";
    case NS_LOG_INFO:  return "\x1b[32m";
    case NS_LOG_WARN:  return "\x1b[33m";
    case NS_LOG_ERROR: return "\x1b[31m";
    case NS_LOG_FATAL: return "\x1b[1;41m";
    }
    return "";
}

void ns_log_set_level(ns_log_level min_level)
{
    g_min_level = min_level;
}

bool ns_log_open_file(const char *path)
{
    if (!path) return false;
    if (!g_mutex) g_mutex = SDL_CreateMutex();
    ns_log_close_file();
    g_file = SDL_IOFromFile(path, "w");
    if (!g_file) {
        fprintf(stderr, "[nineteen] journal impossible à ouvrir (%s) : %s\n", path, SDL_GetError());
        return false;
    }
    return true;
}

void ns_log_close_file(void)
{
    if (g_file) {
        SDL_CloseIO(g_file);
        g_file = NULL;
    }
}

void ns_log_write(ns_log_level level, const char *file, int line, const char *fmt, ...)
{
    if (level < g_min_level) return;

    /* Ne garder que le nom de fichier : les chemins absolus du build n'apportent
     * rien au lecteur et révèlent l'arborescence du développeur. */
    const char *base = file;
    for (const char *p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    char message[2048];
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(message, sizeof message, fmt, args);
    va_end(args);

    const double t = ns_time_seconds();

    if (g_mutex) SDL_LockMutex(g_mutex);

    static int is_tty = -1;
    if (is_tty < 0) {
        const char *no_color = SDL_getenv("NO_COLOR");
        is_tty = (no_color == NULL);
    }

    FILE *out = (level >= NS_LOG_WARN) ? stderr : stdout;
    if (is_tty) {
        fprintf(out, "%s[%8.3f] %s\x1b[0m %s:%d — %s\n",
                level_color(level), t, level_name(level), base, line, message);
    } else {
        fprintf(out, "[%8.3f] %s %s:%d — %s\n", t, level_name(level), base, line, message);
    }
    fflush(out);

    if (g_file) {
        char line_buf[2304];
        const int n = SDL_snprintf(line_buf, sizeof line_buf, "[%8.3f] %s %s:%d — %s\n",
                                   t, level_name(level), base, line, message);
        if (n > 0) SDL_WriteIO(g_file, line_buf, (size_t)n);
        SDL_FlushIO(g_file);
    }

    if (g_mutex) SDL_UnlockMutex(g_mutex);
}

void ns_assert_failed(const char *expr, const char *file, int line, const char *msg)
{
    if (msg) {
        ns_log_write(NS_LOG_FATAL, file, line, "assertion « %s » violée : %s", expr, msg);
    } else {
        ns_log_write(NS_LOG_FATAL, file, line, "assertion « %s » violée", expr);
    }
    ns_log_close_file();

    /* Un message visible même si le jeu tourne sans console — remplace le
     * `sprintf` + `system("osascript ...")` de legacy/include/communFunctions.c,
     * qui exposait une injection de commande shell. */
    char box[1024];
    SDL_snprintf(box, sizeof box,
                 "Nineteen a détecté une incohérence interne et doit s'arrêter.\n\n"
                 "%s:%d\nassertion : %s%s%s",
                 file, line, expr, msg ? "\n" : "", msg ? msg : "");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Nineteen — erreur interne", box, NULL);

    abort();
}
