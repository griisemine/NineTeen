/* Voir ns_env.h pour ce que fait ce fichier et pourquoi il double ns_config. */
#include "ns_env.h"

#include "ns_core.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#define ENV_MAX_KEYS 128
#define ENV_KEY_MAX   64
#define ENV_VAL_MAX  192

typedef struct env_pair {
    char key[ENV_KEY_MAX];
    char value[ENV_VAL_MAX];
    bool read;              /* quelqu'un l'a-t-il consommée ? voir report_unread */
} env_pair;

static env_pair g_pair[ENV_MAX_KEYS];
static int      g_count;
static char     g_path[1024];
static bool     g_loaded;

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e;
    *e = '\0';
    return s;
}

static bool parse_file(const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "r");
    if (!io) return false;

    Sint64 size = SDL_GetIOSize(io);
    if (size <= 0 || size > (1 << 20)) { SDL_CloseIO(io); return false; }

    char *text = (char *)SDL_calloc(1, (size_t)size + 1);
    if (!text) { SDL_CloseIO(io); return false; }
    SDL_ReadIO(io, text, (size_t)size);
    SDL_CloseIO(io);

    int line_no = 0;
    char *save = NULL;
    for (char *line = SDL_strtok_r(text, "\n", &save); line;
         line = SDL_strtok_r(NULL, "\n", &save)) {
        ++line_no;
        char *s = trim(line);
        if (!*s || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) {
            NS_WARN("%s:%d — ligne sans « = », ignorée : « %s »", path, line_no, s);
            continue;
        }
        *eq = '\0';
        char *k = trim(s);
        char *v = trim(eq + 1);
        /* Les guillemets sont tolérés autour de la valeur : on les écrit par
         * réflexe, et refuser pour ça serait une chicane. */
        const size_t vl = strlen(v);
        if (vl >= 2 && ((v[0] == '"' && v[vl - 1] == '"') ||
                        (v[0] == '\'' && v[vl - 1] == '\''))) {
            v[vl - 1] = '\0';
            ++v;
        }
        if (!*k) continue;

        if (g_count >= ENV_MAX_KEYS) {
            NS_WARN("%s : plus de %d clés, le reste est ignoré", path, ENV_MAX_KEYS);
            break;
        }
        /* Une clé répétée : la DERNIÈRE gagne, comme dans un shell. On le dit,
         * parce qu'un fichier qui contient deux fois la même clé est presque
         * toujours une édition qu'on croyait avoir faite ailleurs. */
        int at = -1;
        for (int i = 0; i < g_count; ++i) {
            if (SDL_strcasecmp(g_pair[i].key, k) == 0) { at = i; break; }
        }
        if (at < 0) { at = g_count++; }
        else NS_WARN("%s:%d — « %s » était déjà défini, la dernière valeur gagne", path, line_no, k);

        SDL_strlcpy(g_pair[at].key, k, sizeof g_pair[at].key);
        SDL_strlcpy(g_pair[at].value, v, sizeof g_pair[at].value);
        g_pair[at].read = false;
    }
    SDL_free(text);
    SDL_strlcpy(g_path, path, sizeof g_path);
    return true;
}

void ns_env_load(const char *explicit_path)
{
    g_count = 0;
    g_path[0] = '\0';
    g_loaded = false;

    if (explicit_path && *explicit_path) {
        /* Donné à la main : s'il manque, c'est une ERREUR et on le dit fort.
         * Retomber en silence sur les valeurs par défaut ferait croire que le
         * fichier a été pris en compte. */
        if (!parse_file(explicit_path)) {
            NS_WARN("--env=%s : illisible, les valeurs par défaut s'appliquent", explicit_path);
            return;
        }
        g_loaded = true;
        NS_INFO("réglages : « %s », %d clé(s)", g_path, g_count);
        return;
    }

    char candidate[1024];
    /* 1. le répertoire courant — celui d'où l'on lance pendant le développement */
    if (parse_file("nineteen.env")) { g_loaded = true; }
    /* 2. à côté de l'exécutable — le cas d'un jeu installé */
    if (!g_loaded) {
        const char *base = SDL_GetBasePath();
        if (base) {
            SDL_snprintf(candidate, sizeof candidate, "%snineteen.env", base);
            if (parse_file(candidate)) g_loaded = true;
        }
    }
    /* 3. dans les dossiers de données montés — le cas d'une arborescence de
     *    build, où les assets ne sont ni dans le répertoire courant ni à côté
     *    de l'exécutable. `ns_path_resolve` connaît déjà ces dossiers ; les
     *    redécouvrir ici en ferait une seconde vérité. */
    if (!g_loaded) {
        if (ns_path_resolve("nineteen.env", candidate, sizeof candidate)) {
            if (parse_file(candidate)) g_loaded = true;
        }
    }

    if (g_loaded) NS_INFO("réglages : « %s », %d clé(s)", g_path, g_count);
    else          NS_INFO("réglages : aucun nineteen.env trouvé, valeurs par défaut");
}

void ns_env_shutdown(void) { g_count = 0; g_path[0] = '\0'; g_loaded = false; }

const char *ns_env_path(void) { return g_path[0] ? g_path : NULL; }

static env_pair *find(const char *key)
{
    for (int i = 0; i < g_count; ++i) {
        if (SDL_strcasecmp(g_pair[i].key, key) == 0) {
            g_pair[i].read = true;
            return &g_pair[i];
        }
    }
    return NULL;
}

float ns_env_float(const char *key, float fallback)
{
    const env_pair *p = find(key);
    if (!p) return fallback;
    char *end = NULL;
    const double v = SDL_strtod(p->value, &end);
    if (end == p->value) {
        NS_WARN("réglage « %s » : « %s » n'est pas un nombre, %.4f conservé",
                key, p->value, (double)fallback);
        return fallback;
    }
    return (float)v;
}

int ns_env_int(const char *key, int fallback)
{
    const env_pair *p = find(key);
    if (!p) return fallback;
    return SDL_atoi(p->value);
}

bool ns_env_bool(const char *key, bool fallback)
{
    const env_pair *p = find(key);
    if (!p) return fallback;
    return SDL_strcasecmp(p->value, "1") == 0 ||
           SDL_strcasecmp(p->value, "true") == 0 ||
           SDL_strcasecmp(p->value, "oui") == 0 ||
           SDL_strcasecmp(p->value, "yes") == 0 ||
           SDL_strcasecmp(p->value, "on") == 0;
}

const char *ns_env_str(const char *key, const char *fallback)
{
    const env_pair *p = find(key);
    return p ? p->value : fallback;
}

void ns_env_report_unread(void)
{
    if (!g_loaded) return;
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_pair[i].read) continue;
        NS_WARN("réglage « %s » : personne ne le lit — faute de frappe, ou clé "
                "d'une version antérieure", g_pair[i].key);
        ++n;
    }
    if (n == 0) NS_INFO("réglages : les %d clés sont toutes consommées", g_count);
}
