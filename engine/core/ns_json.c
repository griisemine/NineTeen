/* ns_json.c — lecture JSON au-dessus de jsmn. */
#include "ns_json.h"

#include <SDL3/SDL.h>

#define JSMN_STATIC
#define JSMN_PARENT_LINKS      /* nécessaire pour retrouver le parent d'un jeton */
#include "jsmn.h"

#include <stdlib.h>
#include <string.h>

/* ns_json_value est un alias opaque du jeton jsmn : le reste du moteur n'a pas
 * à connaître jsmn, et on peut changer d'analyseur sans toucher aux appelants. */
struct ns_json_value { jsmntok_t tok; };

/* Un jeton par valeur, plus un par clé. Les fichiers de scène en comptent
 * quelques centaines ; on prévoit large et on signale le dépassement. */
#define NS_JSON_MAX_TOKENS 8192

bool ns_json_parse(ns_json *out, const char *text, size_t length, ns_arena *arena)
{
    NS_ASSERT(out && text && arena);
    SDL_zerop(out);

    jsmn_parser parser;
    jsmn_init(&parser);

    /* Premier appel sans tampon : jsmn renvoie le nombre de jetons nécessaires,
     * ce qui évite de deviner. */
    const int needed = jsmn_parse(&parser, text, length, NULL, 0);
    if (needed < 0) {
        NS_ERROR("JSON malformé (code jsmn %d)", needed);
        return false;
    }
    if (needed > NS_JSON_MAX_TOKENS) {
        NS_ERROR("JSON trop volumineux : %d jetons (maximum %d)", needed, NS_JSON_MAX_TOKENS);
        return false;
    }

    ns_json_value *tokens = NS_ARENA_ARRAY(arena, ns_json_value, needed);
    if (!tokens) return false;

    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, text, length, &tokens[0].tok, (unsigned int)needed);
    if (count < 0) {
        NS_ERROR("JSON malformé au second passage (code %d)", count);
        return false;
    }

    out->text = text;
    out->tokens = tokens;
    out->token_count = count;
    return true;
}

const ns_json_value *ns_json_root(const ns_json *doc)
{
    if (!doc || doc->token_count <= 0) return NULL;
    return &doc->tokens[0];
}

static int token_index(const ns_json *doc, const ns_json_value *v)
{
    if (!doc || !v) return -1;
    const ptrdiff_t idx = v - doc->tokens;
    if (idx < 0 || idx >= doc->token_count) return -1;
    return (int)idx;
}

/* Nombre de jetons occupés par une valeur et tout son contenu : indispensable
 * pour sauter d'un membre au suivant dans la liste plate de jsmn. */
static int token_span(const ns_json *doc, int index)
{
    if (index < 0 || index >= doc->token_count) return 0;
    const jsmntok_t *t = &doc->tokens[index].tok;

    int span = 1;
    if (t->type == JSMN_OBJECT) {
        for (int i = 0; i < t->size; ++i) {
            span += 1;                                   /* la clé */
            span += token_span(doc, index + span);       /* la valeur */
        }
    } else if (t->type == JSMN_ARRAY) {
        for (int i = 0; i < t->size; ++i) {
            span += token_span(doc, index + span);
        }
    }
    return span;
}

static bool token_equals(const ns_json *doc, const jsmntok_t *t, const char *key)
{
    const int len = t->end - t->start;
    if (len < 0) return false;
    return (int)SDL_strlen(key) == len && SDL_strncmp(doc->text + t->start, key, (size_t)len) == 0;
}

const ns_json_value *ns_json_get(const ns_json *doc, const ns_json_value *obj, const char *key)
{
    const int base = token_index(doc, obj);
    if (base < 0 || !key) return NULL;
    if (doc->tokens[base].tok.type != JSMN_OBJECT) return NULL;

    const int members = doc->tokens[base].tok.size;
    int cursor = base + 1;
    for (int i = 0; i < members; ++i) {
        if (cursor >= doc->token_count) break;
        const jsmntok_t *k = &doc->tokens[cursor].tok;
        const int value_index = cursor + 1;
        if (value_index >= doc->token_count) break;

        if (token_equals(doc, k, key)) return &doc->tokens[value_index];

        cursor = value_index + token_span(doc, value_index);
    }
    return NULL;
}

int ns_json_array_count(const ns_json *doc, const ns_json_value *arr)
{
    const int base = token_index(doc, arr);
    if (base < 0) return 0;
    if (doc->tokens[base].tok.type != JSMN_ARRAY) return 0;
    return doc->tokens[base].tok.size;
}

const ns_json_value *ns_json_at(const ns_json *doc, const ns_json_value *arr, int index)
{
    const int base = token_index(doc, arr);
    if (base < 0 || index < 0) return NULL;
    if (doc->tokens[base].tok.type != JSMN_ARRAY) return NULL;
    if (index >= doc->tokens[base].tok.size) return NULL;

    int cursor = base + 1;
    for (int i = 0; i < index; ++i) {
        cursor += token_span(doc, cursor);
        if (cursor >= doc->token_count) return NULL;
    }
    return &doc->tokens[cursor];
}

/* ------------------------------------------------------------- conversions */

static float token_to_float(const ns_json *doc, const ns_json_value *v, float fallback)
{
    if (!v) return fallback;
    const jsmntok_t *t = &v->tok;
    if (t->type != JSMN_PRIMITIVE) return fallback;

    char buf[64];
    const int len = t->end - t->start;
    if (len <= 0 || len >= (int)sizeof buf) return fallback;
    SDL_memcpy(buf, doc->text + t->start, (size_t)len);
    buf[len] = '\0';

    char *end = NULL;
    const double d = SDL_strtod(buf, &end);
    if (end == buf) return fallback;                     /* "true", "null"… */
    return (float)d;
}

float ns_json_get_float(const ns_json *doc, const ns_json_value *obj, const char *key, float fallback)
{
    return token_to_float(doc, ns_json_get(doc, obj, key), fallback);
}

/*
 * Un entier 64 bits LU COMME TEL, et pourquoi ce n'est pas un doublon de
 * `ns_json_get_float`.
 *
 * Un `float` porte 24 bits de mantisse. La graine de partie que le serveur tire
 * en fait 63 (`randomSeed`, bit de signe effacé) : la relire en flottant en
 * arrondit tout ce qui est sous ~2^39. Le client jouerait alors une autre partie
 * que celle que le serveur a ouverte, scellerait son journal sur SA graine, et
 * le serveur recalculerait sur la sienne — chaque soumission refusée pour sceau
 * invalide, sans qu'aucune ligne ne dise pourquoi. Le symptôme aurait été « le
 * classement en ligne ne marche pas », et la cause, six bits de mantisse.
 */
int64_t ns_json_get_i64(const ns_json *doc, const ns_json_value *obj, const char *key,
                        int64_t fallback)
{
    const ns_json_value *v = ns_json_get(doc, obj, key);
    if (!v || v->tok.type != JSMN_PRIMITIVE) return fallback;

    char buf[32];
    const int len = v->tok.end - v->tok.start;
    if (len <= 0 || len >= (int)sizeof buf) return fallback;
    SDL_memcpy(buf, doc->text + v->tok.start, (size_t)len);
    buf[len] = '\0';

    char *end = NULL;
    const long long n = SDL_strtoll(buf, &end, 10);
    /* `end == buf` : « true », « null ». `*end` non nul : « 1.5 », « 1e9 » — un
     * entier tronqué en silence serait pire qu'un repli annoncé. */
    if (end == buf || *end != '\0') return fallback;
    return (int64_t)n;
}

bool ns_json_get_bool(const ns_json *doc, const ns_json_value *obj, const char *key, bool fallback)
{
    const ns_json_value *v = ns_json_get(doc, obj, key);
    if (!v || v->tok.type != JSMN_PRIMITIVE) return fallback;
    const char c = doc->text[v->tok.start];
    if (c == 't') return true;
    if (c == 'f') return false;
    if (c == 'n') return fallback;                       /* null : on garde le repli */
    return token_to_float(doc, v, fallback ? 1.0f : 0.0f) != 0.0f;
}

/*
 * Les octets BRUTS d'une valeur, dans le document d'origine.
 *
 * Pour un objet ou un tableau, jsmn borne le jeton sur l'accolade fermante :
 * l'intervalle rendu est donc le sous-document complet, réutilisable tel quel.
 * C'est ce qui permet de sortir d'une enveloppe le morceau qu'un tiers attend
 * — ici la soumission qu'un serveur décode avec `DisallowUnknownFields` — sans
 * le re-sérialiser, donc sans risquer d'en changer un octet.
 */
bool ns_json_span(const ns_json *doc, const ns_json_value *v, size_t *start, size_t *end)
{
    if (!doc || !v || !start || !end) return false;
    if (v->tok.start < 0 || v->tok.end <= v->tok.start) return false;
    *start = (size_t)v->tok.start;
    *end   = (size_t)v->tok.end;
    return true;
}

void ns_json_string(const ns_json *doc, const ns_json_value *v, char *out, size_t out_size)
{
    NS_ASSERT(out && out_size > 0);
    out[0] = '\0';
    if (!doc || !v || v->tok.type != JSMN_STRING) return;

    const int len = v->tok.end - v->tok.start;
    if (len <= 0) return;
    const size_t copy = ((size_t)len < out_size - 1) ? (size_t)len : out_size - 1;
    SDL_memcpy(out, doc->text + v->tok.start, copy);
    out[copy] = '\0';
}

void ns_json_get_string(const ns_json *doc, const ns_json_value *obj, const char *key,
                        char *out, size_t out_size)
{
    ns_json_string(doc, ns_json_get(doc, obj, key), out, out_size);
}

void ns_json_get_vec3(const ns_json *doc, const ns_json_value *obj, const char *key,
                      float out[3], float fallback)
{
    out[0] = out[1] = out[2] = fallback;

    const ns_json_value *arr = ns_json_get(doc, obj, key);
    if (!arr || arr->tok.type != JSMN_ARRAY) return;

    const int n = ns_json_array_count(doc, arr);
    for (int i = 0; i < 3 && i < n; ++i) {
        out[i] = token_to_float(doc, ns_json_at(doc, arr, i), fallback);
    }
}
