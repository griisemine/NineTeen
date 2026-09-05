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
/*
 * Un entier 64 bits, sans passer par un flottant : `float` n'a que 24 bits de
 * mantisse, et une graine de partie en a 63. Repli si la valeur est absente,
 * n'est pas un nombre, ou n'est pas un ENTIER décimal — « 1.5 » et « 1e9 »
 * rendent le repli plutôt qu'une troncature silencieuse.
 */
int64_t ns_json_get_i64(const ns_json *doc, const ns_json_value *obj, const char *key,
                        int64_t fallback);
bool  ns_json_get_bool(const ns_json *doc, const ns_json_value *obj, const char *key, bool fallback);
void  ns_json_get_string(const ns_json *doc, const ns_json_value *obj, const char *key,
                         char *out, size_t out_size);

/* La même chose pour une valeur qu'on tient déjà — l'élément d'un tableau de
 * chaînes, typiquement. `ns_json_get_string` n'est plus que ce raccourci précédé
 * d'une recherche de clé ; il n'existait aucun moyen de lire une chaîne hors
 * d'un objet, ce qui obligeait à envelopper toute liste de mots dans des objets
 * à une clé. */
void  ns_json_string(const ns_json *doc, const ns_json_value *v, char *out, size_t out_size);

/*
 * L'intervalle d'octets qu'occupe une valeur dans `doc->text` — objets et
 * tableaux compris, accolades incluses. Sert à extraire un sous-document
 * inchangé, octet pour octet.
 */
bool  ns_json_span(const ns_json *doc, const ns_json_value *v, size_t *start, size_t *end);

/* Lit un tableau de trois nombres ; les composantes manquantes prennent `fallback`. */
void ns_json_get_vec3(const ns_json *doc, const ns_json_value *obj, const char *key,
                      float out[3], float fallback);

#endif /* NS_JSON_H */
