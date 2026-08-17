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
#define NS_CFG_QUALITY         "render.quality"        /* low | medium | high | ultra */
#define NS_CFG_RAYTRACING      "render.raytracing"     /* off | reflections | full */
#define NS_CFG_SHADOW_RES      "render.shadowResolution"
#define NS_CFG_VOL_MASTER      "audio.master"
#define NS_CFG_VOL_MUSIC       "audio.music"
#define NS_CFG_VOL_SFX         "audio.sfx"
#define NS_CFG_VOL_AMBIENCE    "audio.ambience"
#define NS_CFG_MOUSE_SENS      "input.mouseSensitivity"
#define NS_CFG_SERVER_URL      "network.serverUrl"

#endif /* NS_CONFIG_H */
