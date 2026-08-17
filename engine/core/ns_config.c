/*
 * ns_config.c — configuration persistante.
 *
 * La V1 codait ses réglages en dur (`#define BASE_WINDOW_W 1920`, plein écran
 * décidé à la compilation) et n'avait aucun moyen de retenir un choix entre deux
 * lancements. Ici : un fichier clé=valeur dans le répertoire utilisateur, écrit
 * de manière atomique, avec des valeurs par défaut si le fichier manque.
 */
#include "ns_config.h"
#include "ns_core.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_CONFIG_MAX_ENTRIES 128
#define NS_CONFIG_KEY_MAX     64
#define NS_CONFIG_VAL_MAX     192

typedef struct entry {
    char key[NS_CONFIG_KEY_MAX];
    char value[NS_CONFIG_VAL_MAX];
} entry;

static entry g_entries[NS_CONFIG_MAX_ENTRIES];
static int   g_count;
static char  g_file[1024];
static bool  g_dirty;

static entry *find(const char *key)
{
    for (int i = 0; i < g_count; ++i) {
        if (SDL_strcasecmp(g_entries[i].key, key) == 0) return &g_entries[i];
    }
    return NULL;
}

static entry *find_or_add(const char *key)
{
    entry *e = find(key);
    if (e) return e;
    if (g_count >= NS_CONFIG_MAX_ENTRIES) {
        NS_WARN("configuration pleine, clé « %s » ignorée", key);
        return NULL;
    }
    e = &g_entries[g_count++];
    SDL_strlcpy(e->key, key, sizeof e->key);
    e->value[0] = '\0';
    return e;
}

/* Retire espaces et tabulations aux deux bouts, en place. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    size_t n = SDL_strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
    return s;
}

void ns_config_init(const char *filename)
{
    g_count = 0;
    g_dirty = false;

    const char *dir = ns_path_user_dir();
    if (!dir || !*dir) {
        g_file[0] = '\0';
        NS_WARN("pas de répertoire utilisateur : la configuration ne sera pas conservée");
        return;
    }
    SDL_snprintf(g_file, sizeof g_file, "%s%s", dir, filename);

    size_t size = 0;
    void *raw = SDL_LoadFile(g_file, &size);
    if (!raw) {
        NS_INFO("configuration absente, valeurs par défaut (%s)", g_file);
        return;
    }

    char *text = (char *)raw;
    char *save = NULL;
    for (char *line = SDL_strtok_r(text, "\n", &save); line; line = SDL_strtok_r(NULL, "\n", &save)) {
        char *t = trim(line);
        if (*t == '\0' || *t == '#' || *t == ';') continue;      /* commentaire */

        char *eq = SDL_strchr(t, '=');
        if (!eq) continue;                                        /* ligne malformée : ignorée */
        *eq = '\0';

        char *k = trim(t);
        char *v = trim(eq + 1);
        if (*k == '\0') continue;

        entry *e = find_or_add(k);
        if (e) SDL_strlcpy(e->value, v, sizeof e->value);
    }
    SDL_free(raw);
    NS_INFO("configuration chargée : %d clés (%s)", g_count, g_file);
}

bool ns_config_save(void)
{
    if (!g_dirty) return true;
    if (g_file[0] == '\0') return false;

    /* Écriture atomique : un plantage pendant la sauvegarde ne doit pas laisser
     * un fichier de configuration tronqué qui empêcherait le jeu de redémarrer. */
    char tmp[1088];
    SDL_snprintf(tmp, sizeof tmp, "%s.tmp", g_file);

    SDL_IOStream *io = SDL_IOFromFile(tmp, "w");
    if (!io) {
        NS_ERROR("écriture de la configuration impossible (%s) : %s", tmp, SDL_GetError());
        return false;
    }

    const char *head = "# Nineteen — configuration\n"
                       "# Généré automatiquement ; les lignes commençant par # sont ignorées.\n\n";
    SDL_WriteIO(io, head, SDL_strlen(head));

    for (int i = 0; i < g_count; ++i) {
        char line[NS_CONFIG_KEY_MAX + NS_CONFIG_VAL_MAX + 4];
        const int n = SDL_snprintf(line, sizeof line, "%s = %s\n", g_entries[i].key, g_entries[i].value);
        if (n > 0) SDL_WriteIO(io, line, (size_t)n);
    }
    SDL_CloseIO(io);

    if (!SDL_RenamePath(tmp, g_file)) {
        NS_ERROR("remplacement de la configuration impossible : %s", SDL_GetError());
        return false;
    }
    g_dirty = false;
    return true;
}

void ns_config_shutdown(void)
{
    ns_config_save();
    g_count = 0;
}

/* ------------------------------------------------------------------ lecture */

const char *ns_config_get_str(const char *key, const char *fallback)
{
    const entry *e = find(key);
    return (e && e->value[0]) ? e->value : fallback;
}

int ns_config_get_int(const char *key, int fallback)
{
    const entry *e = find(key);
    if (!e || !e->value[0]) return fallback;
    char *end = NULL;
    const long v = SDL_strtol(e->value, &end, 10);
    if (end == e->value) return fallback;                 /* pas un nombre */
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int)v;
}

float ns_config_get_float(const char *key, float fallback)
{
    const entry *e = find(key);
    if (!e || !e->value[0]) return fallback;
    char *end = NULL;
    const double v = SDL_strtod(e->value, &end);
    if (end == e->value) return fallback;
    return (float)v;
}

bool ns_config_get_bool(const char *key, bool fallback)
{
    const entry *e = find(key);
    if (!e || !e->value[0]) return fallback;
    return SDL_strcasecmp(e->value, "true") == 0
        || SDL_strcasecmp(e->value, "yes") == 0
        || SDL_strcasecmp(e->value, "on") == 0
        || SDL_strcmp(e->value, "1") == 0;
}

/* ------------------------------------------------------------------ écriture */

void ns_config_set_str(const char *key, const char *value)
{
    entry *e = find_or_add(key);
    if (!e) return;
    if (SDL_strcmp(e->value, value) == 0) return;
    SDL_strlcpy(e->value, value, sizeof e->value);
    g_dirty = true;
}

void ns_config_set_int(const char *key, int value)
{
    char buf[32];
    SDL_snprintf(buf, sizeof buf, "%d", value);
    ns_config_set_str(key, buf);
}

void ns_config_set_float(const char *key, float value)
{
    char buf[32];
    SDL_snprintf(buf, sizeof buf, "%.6g", (double)value);
    ns_config_set_str(key, buf);
}

void ns_config_set_bool(const char *key, bool value)
{
    ns_config_set_str(key, value ? "true" : "false");
}
