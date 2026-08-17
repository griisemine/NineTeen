/*
 * tool_json.h — lecture JSON pour les outils hors-ligne, au-dessus de jsmn.
 *
 * L'API est le **miroir** de `engine/core/ns_json.h` : mêmes noms, mêmes
 * signatures à `tool_` près, mêmes sémantiques de repli. C'est délibéré — l'outil
 * et le moteur lisent les mêmes fichiers, et une divergence entre les deux
 * lecteurs se manifesterait comme un asset qui « marche à la génération et pas au
 * chargement », le genre de bug qui coûte une journée.
 *
 * Pourquoi une seconde implémentation plutôt que partager la première : les deux
 * ont des contrats **opposés**. Le moteur ne doit jamais s'arrêter sur un fichier
 * de données malformé — il avertit et retombe sur une valeur par défaut, parce
 * qu'un joueur préfère une salle imparfaite à un refus de démarrer. L'outil, lui,
 * doit s'arrêter net : un asset produit à partir d'une description à moitié lue
 * est un piège qu'on découvrirait à l'exécution. Et `ns_json` dépend de l'arène et
 * de SDL, dont les outils n'ont ni l'un ni l'autre.
 *
 * La différence tient donc en une ligne : ici, un document malformé est fatal.
 */
#ifndef NS_TOOL_JSON_H
#define NS_TOOL_JSON_H

#include "tools_common.h"

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include "jsmn.h"

#include <stdlib.h>
#include <string.h>

#define TOOL_JSON_MAX_TOKENS 65536

typedef struct tool_json_value { jsmntok_t tok; } tool_json_value;

typedef struct tool_json {
    const char      *text;
    tool_json_value *tokens;
    int              token_count;
} tool_json;

/* ------------------------------------------------------------------ analyse */

/* Analyse `text` (non modifié). S'arrête net si le document est malformé :
 * l'appelant est un outil de build, et un build doit casser plutôt que produire
 * un asset incomplet. La mémoire est libérée par `tool_json_free`. */
static inline void tool_json_parse(tool_json *out, const char *text, size_t length,
                                   const char *what)
{
    memset(out, 0, sizeof *out);

    jsmn_parser parser;
    jsmn_init(&parser);

    /* Premier passage sans tampon : jsmn renvoie le nombre de jetons requis. */
    const int needed = jsmn_parse(&parser, text, length, NULL, 0);
    if (needed < 0) tool_fatalf("%s : JSON malformé (code jsmn %d)", what, needed);
    if (needed > TOOL_JSON_MAX_TOKENS) {
        tool_fatalf("%s : JSON trop volumineux, %d jetons (maximum %d)",
                    what, needed, TOOL_JSON_MAX_TOKENS);
    }

    tool_json_value *tokens = (tool_json_value *)calloc((size_t)needed ? (size_t)needed : 1,
                                                        sizeof(tool_json_value));
    if (!tokens) tool_fatalf("mémoire épuisée (jetons JSON de %s)", what);

    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, text, length, &tokens[0].tok, (unsigned int)needed);
    if (count < 0) tool_fatalf("%s : JSON malformé au second passage (code %d)", what, count);

    out->text = text;
    out->tokens = tokens;
    out->token_count = count;
}

static inline void tool_json_free(tool_json *doc)
{
    free(doc->tokens);
    memset(doc, 0, sizeof *doc);
}

static inline const tool_json_value *tool_json_root(const tool_json *doc)
{
    if (!doc || doc->token_count <= 0) return NULL;
    return &doc->tokens[0];
}

/* ------------------------------------------------------------- navigation */

static inline int tool_json_index(const tool_json *doc, const tool_json_value *v)
{
    if (!doc || !v) return -1;
    const ptrdiff_t idx = v - doc->tokens;
    if (idx < 0 || idx >= doc->token_count) return -1;
    return (int)idx;
}

/* Nombre de jetons occupés par une valeur et tout son contenu : jsmn produit une
 * liste plate, et c'est ce qui permet de sauter d'un membre au suivant. */
static inline int tool_json_span(const tool_json *doc, int index)
{
    if (index < 0 || index >= doc->token_count) return 0;
    const jsmntok_t *t = &doc->tokens[index].tok;

    int span = 1;
    if (t->type == JSMN_OBJECT) {
        for (int i = 0; i < t->size; ++i) {
            span += 1;                                    /* la clé */
            span += tool_json_span(doc, index + span);    /* la valeur */
        }
    } else if (t->type == JSMN_ARRAY) {
        for (int i = 0; i < t->size; ++i) {
            span += tool_json_span(doc, index + span);
        }
    }
    return span;
}

static inline bool tool_json_key_is(const tool_json *doc, const jsmntok_t *t, const char *key)
{
    const int len = t->end - t->start;
    if (len < 0) return false;
    return (int)strlen(key) == len && strncmp(doc->text + t->start, key, (size_t)len) == 0;
}

static inline const tool_json_value *tool_json_get(const tool_json *doc,
                                                   const tool_json_value *obj,
                                                   const char *key)
{
    const int base = tool_json_index(doc, obj);
    if (base < 0 || !key) return NULL;
    if (doc->tokens[base].tok.type != JSMN_OBJECT) return NULL;

    const int members = doc->tokens[base].tok.size;
    int cursor = base + 1;
    for (int i = 0; i < members; ++i) {
        if (cursor >= doc->token_count) break;
        const jsmntok_t *k = &doc->tokens[cursor].tok;
        const int value_index = cursor + 1;
        if (value_index >= doc->token_count) break;

        if (tool_json_key_is(doc, k, key)) return &doc->tokens[value_index];

        cursor = value_index + tool_json_span(doc, value_index);
    }
    return NULL;
}

static inline int tool_json_array_count(const tool_json *doc, const tool_json_value *arr)
{
    const int base = tool_json_index(doc, arr);
    if (base < 0) return 0;
    if (doc->tokens[base].tok.type != JSMN_ARRAY) return 0;
    return doc->tokens[base].tok.size;
}

static inline const tool_json_value *tool_json_at(const tool_json *doc,
                                                  const tool_json_value *arr, int index)
{
    const int base = tool_json_index(doc, arr);
    if (base < 0 || index < 0) return NULL;
    if (doc->tokens[base].tok.type != JSMN_ARRAY) return NULL;
    if (index >= doc->tokens[base].tok.size) return NULL;

    int cursor = base + 1;
    for (int i = 0; i < index; ++i) {
        cursor += tool_json_span(doc, cursor);
        if (cursor >= doc->token_count) return NULL;
    }
    return &doc->tokens[cursor];
}

/* ------------------------------------------------------------ conversions */

static inline float tool_json_value_float(const tool_json *doc, const tool_json_value *v,
                                          float fallback)
{
    if (!v) return fallback;
    const jsmntok_t *t = &v->tok;
    if (t->type != JSMN_PRIMITIVE) return fallback;

    char buf[64];
    const int len = t->end - t->start;
    if (len <= 0 || len >= (int)sizeof buf) return fallback;
    memcpy(buf, doc->text + t->start, (size_t)len);
    buf[len] = '\0';

    char *end = NULL;
    const double d = strtod(buf, &end);
    if (end == buf) return fallback;                      /* "true", "null"… */
    return (float)d;
}

static inline float tool_json_get_float(const tool_json *doc, const tool_json_value *obj,
                                        const char *key, float fallback)
{
    return tool_json_value_float(doc, tool_json_get(doc, obj, key), fallback);
}

static inline bool tool_json_get_bool(const tool_json *doc, const tool_json_value *obj,
                                      const char *key, bool fallback)
{
    const tool_json_value *v = tool_json_get(doc, obj, key);
    if (!v || v->tok.type != JSMN_PRIMITIVE) return fallback;
    const char c = doc->text[v->tok.start];
    if (c == 't') return true;
    if (c == 'f') return false;
    if (c == 'n') return fallback;                        /* null : on garde le repli */
    return tool_json_value_float(doc, v, fallback ? 1.0f : 0.0f) != 0.0f;
}

static inline void tool_json_get_string(const tool_json *doc, const tool_json_value *obj,
                                        const char *key, char *out, size_t out_size)
{
    out[0] = '\0';
    const tool_json_value *v = tool_json_get(doc, obj, key);
    if (!v || v->tok.type != JSMN_STRING) return;

    const int len = v->tok.end - v->tok.start;
    if (len <= 0) return;
    const size_t copy = ((size_t)len < out_size - 1) ? (size_t)len : out_size - 1;
    memcpy(out, doc->text + v->tok.start, copy);
    out[copy] = '\0';
}

/* Lit un tableau de n nombres ; les composantes manquantes prennent `fallback`.
 * Renvoie le nombre effectivement lu, ce qui permet à l'appelant de distinguer
 * « absent » de « présent mais court » — utile pour refuser une description de
 * salle incomplète plutôt que de la compléter en silence. */
static inline int tool_json_get_floats(const tool_json *doc, const tool_json_value *obj,
                                       const char *key, float *out, int n, float fallback)
{
    for (int i = 0; i < n; ++i) out[i] = fallback;

    const tool_json_value *arr = tool_json_get(doc, obj, key);
    if (!arr || arr->tok.type != JSMN_ARRAY) return 0;

    const int count = tool_json_array_count(doc, arr);
    const int read = (count < n) ? count : n;
    for (int i = 0; i < read; ++i) {
        out[i] = tool_json_value_float(doc, tool_json_at(doc, arr, i), fallback);
    }
    return read;
}

/* Raccourcis, pour que les appels se lisent comme dans le moteur. */
static inline void tool_json_get_vec2(const tool_json *doc, const tool_json_value *obj,
                                      const char *key, float out[2], float fallback)
{
    (void)tool_json_get_floats(doc, obj, key, out, 2, fallback);
}

static inline void tool_json_get_vec3(const tool_json *doc, const tool_json_value *obj,
                                      const char *key, float out[3], float fallback)
{
    (void)tool_json_get_floats(doc, obj, key, out, 3, fallback);
}

static inline void tool_json_get_vec4(const tool_json *doc, const tool_json_value *obj,
                                      const char *key, float out[4], float fallback)
{
    (void)tool_json_get_floats(doc, obj, key, out, 4, fallback);
}

#endif /* NS_TOOL_JSON_H */
