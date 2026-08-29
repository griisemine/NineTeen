/* ns_scores.c — voir ns_scores.h pour le raisonnement. */
#include "ns_scores.h"

#include "ns_core.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

/*
 * Format : une ligne par entrée, texte, champs séparés par des barres verticales.
 *
 *   v1
 *   flappy/hard|4210|182350|1755500000|Nine
 *
 * Pourquoi pas du JSON, alors que le moteur sait le lire : parce qu'un lecteur
 * JSON complet pour cinq champs est une dépendance de plus sur le chemin de
 * démarrage, et surtout parce qu'un fichier ligne à ligne se répare tout seul —
 * une ligne abîmée est sautée, les autres survivent. Un JSON tronqué est perdu
 * en entier, et c'est exactement ce qui arrive quand on coupe le courant.
 */
#define SCORES_VERSION "v1"

static ns_score_board g_board[NS_SCORE_BOARDS];
static uint32_t       g_board_count;
static bool           g_dirty;
static bool           g_loaded;
static char           g_path[1024];
static char           g_override[1024];

/* -------------------------------------------------------------------------- */

static void make_key(char *out, size_t cap, const char *game, const char *difficulty)
{
    /* Une difficulté absente ou vide donne « normal » : sans ça la même borne
     * écrirait dans deux tableaux selon que le champ est renseigné ou non. */
    const char *d = (difficulty && *difficulty) ? difficulty : "normal";
    SDL_snprintf(out, cap, "%s/%s", (game && *game) ? game : "inconnu", d);
    for (char *p = out; *p; ++p) {
        /* Le séparateur du fichier ne peut pas apparaître dans une clé, et un
         * saut de ligne encore moins. On les remplace plutôt que de refuser :
         * un nom de jeu est de la donnée, pas une entrée utilisateur hostile —
         * mais il vient quand même d'un fichier de salle. */
        if (*p == '|' || *p == '\n' || *p == '\r') *p = '_';
    }
}

static ns_score_board *find_board(const char *key, bool create)
{
    for (uint32_t i = 0; i < g_board_count; ++i) {
        if (SDL_strcmp(g_board[i].key, key) == 0) return &g_board[i];
    }
    if (!create) return NULL;
    if (g_board_count >= NS_SCORE_BOARDS) {
        NS_WARN("scores : %d tableaux au maximum, « %s » ignoré", NS_SCORE_BOARDS, key);
        return NULL;
    }
    ns_score_board *b = &g_board[g_board_count++];
    SDL_zerop(b);
    SDL_strlcpy(b->key, key, sizeof b->key);
    return b;
}

static void sanitize_name(char *out, size_t cap, const char *in)
{
    size_t n = 0;
    if (in) {
        for (const char *p = in; *p && n + 1 < cap; ++p) {
            /* Ni séparateur, ni saut de ligne, ni caractère de contrôle. Le nom
             * est saisi par le joueur : c'est la seule donnée de ce fichier qui
             * ne vienne pas du jeu lui-même. */
            const unsigned char c = (unsigned char)*p;
            if (c == '|' || c < 0x20 || c == 0x7F) continue;
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

/* -------------------------------------------------------------------------- */

const char *ns_scores_path(void)
{
    if (g_override[0]) return g_override;
    if (!g_path[0]) {
        const char *dir = ns_path_user_dir();
        SDL_snprintf(g_path, sizeof g_path, "%sscores.txt", dir ? dir : "");
    }
    return g_path;
}

void ns_scores_set_path(const char *path)
{
    if (path && *path) SDL_strlcpy(g_override, path, sizeof g_override);
    else               g_override[0] = '\0';
    /* Le contenu en mémoire appartient à l'ancien fichier : le garder ferait
     * écrire les scores du joueur dans le fichier de test, ou l'inverse. */
    ns_scores_clear();
    g_loaded = false;
}

void ns_scores_clear(void)
{
    SDL_memset(g_board, 0, sizeof g_board);
    g_board_count = 0;
    g_dirty = false;
}

void ns_scores_load(void)
{
    ns_scores_clear();
    g_loaded = true;

    const char *path = ns_scores_path();
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) return;      /* premier lancement : ce n'est pas une erreur */

    char *text = (char *)data;
    uint32_t line_no = 0, bad = 0;
    char *save = NULL;
    for (char *line = SDL_strtok_r(text, "\n", &save); line;
         line = SDL_strtok_r(NULL, "\n", &save)) {
        line_no++;
        while (*line == '\r' || *line == ' ') line++;
        if (!*line) continue;

        if (line_no == 1) {
            if (SDL_strncmp(line, SCORES_VERSION, SDL_strlen(SCORES_VERSION)) != 0) {
                NS_WARN("scores : « %s » d'une version inconnue, on repart de zéro", path);
                break;
            }
            continue;
        }

        char key[NS_SCORE_KEY_MAX] = {0}, name[NS_SCORE_NAME_MAX] = {0};
        unsigned long long score = 0, dur = 0;
        long long when = 0;
        /* `%[^|]` s'arrête au séparateur ; le nom est le dernier champ et peut
         * être vide, d'où la lecture en deux temps. */
        int n = SDL_sscanf(line, "%39[^|]|%llu|%llu|%lld|%23[^\n]",
                           key, &score, &dur, &when, name);
        if (n < 4) { bad++; continue; }

        ns_score_board *b = find_board(key, true);
        if (!b || b->count >= NS_SCORE_SLOTS) continue;
        ns_score_entry *e = &b->entry[b->count++];
        e->score = (uint32_t)score;
        e->duration_ms = (uint32_t)dur;
        e->when = (int64_t)when;
        sanitize_name(e->name, sizeof e->name, (n >= 5) ? name : NULL);
    }
    SDL_free(data);

    if (bad) NS_WARN("scores : %u ligne(s) illisible(s) ignorée(s) dans « %s »", bad, path);

    uint32_t total = 0;
    for (uint32_t i = 0; i < g_board_count; ++i) total += g_board[i].count;
    if (total) NS_INFO("scores : %u entrée(s) dans %u tableau(x)", total, g_board_count);
}

bool ns_scores_save(void)
{
    if (!g_dirty) return true;

    const char *path = ns_scores_path();
    char tmp[1088];
    SDL_snprintf(tmp, sizeof tmp, "%s.tmp", path);

    SDL_IOStream *io = SDL_IOFromFile(tmp, "w");
    if (!io) {
        NS_ERROR("scores : écriture impossible (%s) : %s", tmp, SDL_GetError());
        return false;
    }

    char buf[256];
    int len = SDL_snprintf(buf, sizeof buf, "%s\n", SCORES_VERSION);
    SDL_WriteIO(io, buf, (size_t)len);
    for (uint32_t i = 0; i < g_board_count; ++i) {
        const ns_score_board *b = &g_board[i];
        for (uint32_t k = 0; k < b->count; ++k) {
            const ns_score_entry *e = &b->entry[k];
            len = SDL_snprintf(buf, sizeof buf, "%s|%u|%u|%lld|%s\n",
                               b->key, e->score, e->duration_ms,
                               (long long)e->when, e->name);
            SDL_WriteIO(io, buf, (size_t)len);
        }
    }
    SDL_CloseIO(io);

    /* Renommage atomique : une coupure pendant l'écriture laisse l'ancien
     * fichier intact au lieu d'un fichier à moitié écrit. Même discipline que
     * `ns_config_save`. */
    if (!SDL_RenamePath(tmp, path)) {
        NS_ERROR("scores : renommage impossible (%s -> %s) : %s", tmp, path, SDL_GetError());
        return false;
    }
    g_dirty = false;
    return true;
}

uint32_t ns_scores_record(const char *game, const char *difficulty,
                          uint32_t score, uint32_t duration_ms, const char *name)
{
    if (!g_loaded) ns_scores_load();

    char key[NS_SCORE_KEY_MAX];
    make_key(key, sizeof key, game, difficulty);
    ns_score_board *b = find_board(key, true);
    if (!b) return 0;

    /*
     * Insertion par décalage plutôt que « ajouter puis trier ». À dix entrées la
     * différence de coût est nulle, mais l'insertion garde une propriété qui
     * compte : à score égal, l'entrée DÉJÀ présente reste devant. Un tri sans
     * précaution ferait remonter la nouvelle et déclasserait un ancien record
     * sans que le joueur ait fait mieux.
     */
    uint32_t rank = 0;
    while (rank < b->count && b->entry[rank].score >= score) rank++;
    if (rank >= NS_SCORE_SLOTS) return 0;

    const uint32_t last = (b->count < NS_SCORE_SLOTS) ? b->count : NS_SCORE_SLOTS - 1;
    for (uint32_t i = last; i > rank; --i) b->entry[i] = b->entry[i - 1];
    if (b->count < NS_SCORE_SLOTS) b->count++;

    ns_score_entry *e = &b->entry[rank];
    SDL_zerop(e);
    e->score = score;
    e->duration_ms = duration_ms;
    /* `SDL_GetCurrentTime` rend des nanosecondes depuis l'époque Unix, par un
     * paramètre de sortie. Une seconde suffit ici : l'horodatage sert à trier et
     * à afficher une date, pas à mesurer. */
    SDL_Time now = 0;
    e->when = SDL_GetCurrentTime(&now) ? (int64_t)(now / 1000000000LL) : 0;
    sanitize_name(e->name, sizeof e->name, name);

    g_dirty = true;
    return rank + 1;
}

const ns_score_board *ns_scores_board(const char *game, const char *difficulty)
{
    if (!g_loaded) ns_scores_load();
    char key[NS_SCORE_KEY_MAX];
    make_key(key, sizeof key, game, difficulty);
    return find_board(key, false);
}

const char *ns_scores_bucket(const char *declared)
{
    /* Seul « hard » est un régime à part ; « easy » et « normal » sont le même
     * jeu sous deux enseignes, et partagent donc leur tableau. */
    return (declared && SDL_strcasecmp(declared, "hard") == 0) ? "hard" : "normal";
}

uint32_t ns_scores_best(const char *game, const char *difficulty)
{
    const ns_score_board *b = ns_scores_board(game, difficulty);
    return (b && b->count) ? b->entry[0].score : 0u;
}
