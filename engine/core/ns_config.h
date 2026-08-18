/*
 * ns_config.h — réglages persistants (résolution, volumes, qualité de rendu…).
 *
 * Stocké dans le répertoire utilisateur, pas à côté du binaire : une application
 * installée dans /usr/local ou /Applications ne peut pas écrire chez elle.
 */
#ifndef NS_CONFIG_H
#define NS_CONFIG_H

#include <stdbool.h>

/* `filename` est relatif au répertoire utilisateur, p. ex. "settings.cfg". */
void ns_config_init(const char *filename);
bool ns_config_save(void);
void ns_config_shutdown(void);

const char *ns_config_get_str(const char *key, const char *fallback);
int         ns_config_get_int(const char *key, int fallback);
float       ns_config_get_float(const char *key, float fallback);
bool        ns_config_get_bool(const char *key, bool fallback);

void ns_config_set_str(const char *key, const char *value);
void ns_config_set_int(const char *key, int value);
void ns_config_set_float(const char *key, float value);
void ns_config_set_bool(const char *key, bool value);

/* Clés utilisées par le moteur — regroupées ici pour éviter les fautes de frappe
 * disséminées dans le code, qui feraient silencieusement retomber sur le défaut. */
#define NS_CFG_WINDOW_W        "window.width"
#define NS_CFG_WINDOW_H        "window.height"
#define NS_CFG_FULLSCREEN      "window.fullscreen"
#define NS_CFG_VSYNC           "window.vsync"
#define NS_CFG_RENDER_SCALE    "render.scale"
#define NS_CFG_QUALITY         "render.quality"   /* potato | low | medium | high | ultra */
#define NS_CFG_RAYTRACING      "render.raytracing"     /* off | reflections | full */
/*
 * `render.shadowResolution` et `network.serverUrl` ont été RETIRÉS d'ici.
 *
 * Ils étaient réservés depuis M1 et n'ont jamais eu de lecteur — l'audit les
 * listait avec `input.mouseSensitivity`, qui en a un depuis B9. La différence
 * est qu'ils ne peuvent pas en avoir : il n'y a pas de carte d'ombre dans ce
 * moteur (les ombres sont lancées), et pas une ligne de code réseau dans le
 * client. Une clé qu'on ne peut pas lire ne documente rien, elle laisse croire
 * qu'un réglage existe. Elles reviendront avec ce qui les consomme.
 */
#define NS_CFG_VOL_MASTER      "audio.master"
#define NS_CFG_VOL_MUSIC       "audio.music"
#define NS_CFG_VOL_SFX         "audio.sfx"
#define NS_CFG_VOL_AMBIENCE    "audio.ambience"

/*
 * L'URL du serveur de classement. Vide — le défaut — signifie HORS LIGNE au
 * sens strict : aucune socket n'est ouverte, et le fil réseau ne démarre même
 * pas.
 *
 * La clé avait été RETIRÉE en M9 parce qu'elle ne servait à rien : elle était
 * réservée depuis M1 et personne ne la lisait. Elle revient maintenant qu'elle
 * a un lecteur, ce qui est la seule raison valable d'avoir une clé.
 */
#define NS_CFG_SERVER_URL   "network.serverUrl"
/* Le jeton de session, si l'on en a un. Sans lui, le classement est en LECTURE
 * seule — ce qui suffit à voir les scores du monde, et n'exige aucun compte. */
#define NS_CFG_SERVER_TOKEN "network.token"
#define NS_CFG_MOUSE_SENS      "input.mouseSensitivity"
#define NS_CFG_ROOM_SOURCE     "room.source"            /* generated | legacy */

#endif /* NS_CONFIG_H */
