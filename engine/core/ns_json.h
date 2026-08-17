/*
 * ns_json.h — lecture JSON, au-dessus de jsmn.
 *
 * jsmn produit une liste plate de jetons ; cette couche fournit l'accès par
 * clé et par index dont le moteur a besoin, avec une valeur par défaut à chaque
 * lecture. Le principe : **un fichier de données malformé ne doit jamais faire
 * planter le jeu**, seulement produire un avertissement et une valeur de repli.
 *
 * Volontairement en lecture seule : le moteur consomme des fichiers de données
 * produits par les outils ou écrits à la main, il n'en génère pas.
 */
#ifndef NS_JSON_H
#define NS_JSON_H

#include "ns_core.h"

typedef struct ns_json_value ns_json_value;

typedef struct ns_json {
    const char    *text;
    ns_json_value *tokens;
    int            token_count;
} ns_json;

/*
 * Analyse `text` (non modifié) en allouant les jetons dans `arena`.
 * Renvoie false si le document est malformé.
 */
bool ns_json_parse(ns_json *out, const char *text, size_t length, ns_arena *arena);

const ns_json_value *ns_json_root(const ns_json *doc);

/* Accès à un membre d'objet. Renvoie NULL si absent ou si `obj` n'est pas un objet. */
const ns_json_value *ns_json_get(const ns_json *doc, const ns_json_value *obj, const char *key);

/* Accès à un élément de tableau. */
int                  ns_json_array_count(const ns_json *doc, const ns_json_value *arr);
const ns_json_value *ns_json_at(const ns_json *doc, const ns_json_value *arr, int index);

/* Lectures typées, avec repli. Chacune accepte un `obj` nul (renvoie le repli). */
float ns_json_get_float(const ns_json *doc, const ns_json_value *obj, const char *key, float fallback);
bool  ns_json_get_bool(const ns_json *doc, const ns_json_value *obj, const char *key, bool fallback);
void  ns_json_get_string(const ns_json *doc, const ns_json_value *obj, const char *key,
                         char *out, size_t out_size);

/* Lit un tableau de trois nombres ; les composantes manquantes prennent `fallback`. */
void ns_json_get_vec3(const ns_json *doc, const ns_json_value *obj, const char *key,
                      float out[3], float fallback);

#endif /* NS_JSON_H */
