/*
 * ns_paths.c — résolution des chemins de données.
 *
 * Remplace legacy/include/fullpath.c et le tableau `verifierFichier[54][128]` de
 * legacy/main.h, qui listait en dur des chemins du genre "../room/sounds/borne1.wav".
 * Deux défauts : le jeu ne démarrait que si le répertoire courant était `bin/`, et
 * un chemin de plus de 127 caractères débordait silencieusement du tableau.
 *
 * Ici : des points de montage empilés, un chemin *logique* dans le code
 * ("sounds/borne1.wav"), et un bornage systématique.
 */
#include "ns_core.h"

#include <SDL3/SDL.h>

#include <string.h>

#define NS_MAX_MOUNTS 8
#define NS_PATH_MAX   1024

typedef struct mount {
    char dir[NS_PATH_MAX];
} mount;

static mount  g_mounts[NS_MAX_MOUNTS];
static int    g_mount_count;
/*
 * Nombre de montages de *dernier recours*, maintenus en fin de tableau.
 *
 * Le répertoire du binaire est monté dès l'initialisation, avant que le jeu ait
 * eu l'occasion de monter l'arbre de build ou `$NINETEEN_ASSETS`. Comme la
 * résolution prend le premier montage qui contient le fichier, un simple ajout en
 * fin de tableau donnait donc la priorité à l'inverse de ce qui était voulu :
 * `$NINETEEN_ASSETS` devenait le montage le moins prioritaire et ne pouvait rien
 * surcharger. Ce compteur permet d'insérer les montages ultérieurs *avant* eux.
 */
static int    g_fallback_count;
static char   g_user_dir[NS_PATH_MAX];
static bool   g_initialised;

/* Refuse tout ce qui pourrait sortir de l'arborescence montée. Les chemins
 * logiques viennent du code, mais aussi de fichiers de configuration : un
 * "../../etc/passwd" ne doit jamais être résolu. */
static bool logical_is_safe(const char *logical)
{
    if (!logical || !*logical) return false;
    if (logical[0] == '/' || logical[0] == '\\') return false;
    if (strstr(logical, "..") != NULL) return false;
#ifdef _WIN32
    if (logical[1] == ':') return false;   /* "C:\..." */
#endif
    for (const char *p = logical; *p; ++p) {
        if ((unsigned char)*p < 0x20) return false;
    }
    return true;
}

/* Définie plus bas ; l'initialisation en a besoin pour marquer le répertoire du
 * binaire comme montage de dernier recours. */
static bool mount_at(const char *directory, bool fallback);

bool ns_paths_init(const char *argv0)
{
    (void)argv0;   /* SDL sait retrouver le répertoire du binaire tout seul */

    g_mount_count = 0;
    g_initialised = true;

    /* Répertoire inscriptible : ~/.local/share/Nineteen, %APPDATA%\..., etc.
     * L'original écrivait son journal à côté du binaire, ce qui échoue dès que
     * l'application est installée dans un répertoire en lecture seule. */
    char *pref = SDL_GetPrefPath("recognizer", "Nineteen");
    if (pref) {
        SDL_strlcpy(g_user_dir, pref, sizeof g_user_dir);
        SDL_free(pref);
    } else {
        g_user_dir[0] = '\0';
        NS_WARN("répertoire utilisateur indisponible : %s", SDL_GetError());
    }

    /* Le répertoire du binaire est toujours monté en dernier recours : c'est la
     * disposition d'un paquet installé (assets à côté de l'exécutable). Marqué
     * comme tel, pour que les montages ajoutés ensuite — l'arbre de build en
     * développement, `$NINETEEN_ASSETS` — passent devant et puissent réellement
     * surcharger. Sans ce marquage, installer le jeu à côté d'un arbre d'assets
     * périmé masquerait l'arbre de build sans un mot. */
    const char *base = SDL_GetBasePath();
    if (base) {
        char assets[NS_PATH_MAX];
        SDL_snprintf(assets, sizeof assets, "%sassets", base);
        mount_at(assets, true);
        mount_at(base, true);
    }
    return true;
}

void ns_paths_shutdown(void)
{
    g_mount_count = 0;
    g_fallback_count = 0;
    g_initialised = false;
}

/*
 * Monte un répertoire. `fallback` le place en dernier recours, derrière tous les
 * montages ordinaires présents et futurs.
 *
 * La résolution prend le premier montage qui contient le fichier, donc l'ordre du
 * tableau *est* la priorité. Le répertoire du binaire étant monté à
 * l'initialisation — avant que le jeu ait pu monter l'arbre de build ou
 * `$NINETEEN_ASSETS` — un ajout en fin de tableau plaçait les montages voulus
 * prioritaires *après* lui. On insère donc les montages ordinaires devant le bloc
 * de dernier recours, en le décalant.
 */
static bool mount_at(const char *directory, bool fallback)
{
    NS_ASSERT(g_initialised);
    if (!directory || !*directory) return false;

    if (g_mount_count >= NS_MAX_MOUNTS) {
        NS_WARN("trop de points de montage, « %s » ignoré", directory);
        return false;
    }

    /* Position d'insertion : à la fin pour un dernier recours, sinon juste avant
     * le bloc de dernier recours. */
    const int insert = fallback ? g_mount_count : (g_mount_count - g_fallback_count);

    mount candidate;
    const size_t n = SDL_strlcpy(candidate.dir, directory, NS_PATH_MAX);
    if (n >= NS_PATH_MAX) {
        NS_WARN("point de montage tronqué, ignoré : %s", directory);
        return false;
    }
    /* Retirer un séparateur final pour normaliser la concaténation. */
    size_t len = SDL_strlen(candidate.dir);
    while (len > 1 && (candidate.dir[len - 1] == '/' || candidate.dir[len - 1] == '\\')) {
        candidate.dir[--len] = '\0';
    }

    for (int i = g_mount_count; i > insert; --i) g_mounts[i] = g_mounts[i - 1];
    g_mounts[insert] = candidate;
    g_mount_count++;
    if (fallback) g_fallback_count++;

    NS_DEBUG("montage %d%s : %s", insert, fallback ? " (dernier recours)" : "", candidate.dir);
    return true;
}

bool ns_paths_mount(const char *directory)
{
    return mount_at(directory, false);
}

bool ns_path_resolve(const char *logical, char *out, size_t out_size)
{
    NS_ASSERT(out != NULL && out_size > 0);
    out[0] = '\0';

    if (!logical_is_safe(logical)) {
        NS_WARN("chemin logique refusé : %s", logical ? logical : "(nul)");
        return false;
    }

    for (int i = 0; i < g_mount_count; ++i) {
        char candidate[NS_PATH_MAX];
        const int n = SDL_snprintf(candidate, sizeof candidate, "%s/%s", g_mounts[i].dir, logical);
        if (n <= 0 || (size_t)n >= sizeof candidate) continue;   /* trop long : on saute */

        SDL_PathInfo info;
        if (SDL_GetPathInfo(candidate, &info) && info.type == SDL_PATHTYPE_FILE) {
            if (SDL_strlcpy(out, candidate, out_size) >= out_size) {
                NS_WARN("chemin résolu trop long pour le tampon fourni : %s", candidate);
                out[0] = '\0';
                return false;
            }
            return true;
        }
    }
    return false;
}

const char *ns_path_user_dir(void)
{
    return g_user_dir;
}

void *ns_file_read_all(ns_arena *arena, const char *logical, size_t *out_size)
{
    if (out_size) *out_size = 0;

    char full[NS_PATH_MAX];
    if (!ns_path_resolve(logical, full, sizeof full)) {
        NS_ERROR("fichier introuvable dans les montages : %s", logical ? logical : "(nul)");
        return NULL;
    }

    SDL_IOStream *io = SDL_IOFromFile(full, "rb");
    if (!io) {
        NS_ERROR("ouverture impossible (%s) : %s", full, SDL_GetError());
        return NULL;
    }

    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        NS_ERROR("taille illisible (%s) : %s", full, SDL_GetError());
        SDL_CloseIO(io);
        return NULL;
    }

    /* +1 : les consommateurs de texte (glTF, JSON, GLSL) veulent un zéro final. */
    uint8_t *buffer = (uint8_t *)ns_arena_alloc(arena, (size_t)size + 1, 16);
    if (!buffer) {
        SDL_CloseIO(io);
        return NULL;
    }

    const size_t read = SDL_ReadIO(io, buffer, (size_t)size);
    SDL_CloseIO(io);

    if (read != (size_t)size) {
        NS_ERROR("lecture partielle de %s : %zu / %lld octets", full, read, (long long)size);
        return NULL;
    }
    buffer[size] = 0;
    if (out_size) *out_size = (size_t)size;
    return buffer;
}
