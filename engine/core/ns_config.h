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
/*
 * Le TEMPS RÉEL : se voir dans la salle, et affronter les fantômes des autres.
 *
 * Faux par défaut, et c'est un réglage distinct de l'URL du serveur pour une
 * raison de fond : les deux n'engagent pas la même chose. Consulter un
 * classement ne diffuse rien de soi ; la présence publie un pseudo et une
 * position dans une salle où d'autres gens sont. Ce n'est pas à une URL de
 * serveur de décider ça à la place du joueur.
 *
 * Sans serveur configuré, ce réglage ne peut rien : `ns_realtime` hérite du
 * verrou de `ns_online` et ne démarre pas si celui-ci n'a pas démarré.
 */
#define NS_CFG_REALTIME     "network.realtime"
/*
 * LA MISE À JOUR — deux clés, et pas une de plus.
 *
 * `update.auto` : télécharger le paquet sans qu'on l'ait demandé. FAUX par
 * défaut, et ce défaut est un choix : un paquet fait 175 Mio, et les prendre
 * sur la ligne de quelqu'un qui voulait jouer dix minutes n'est pas au jeu d'en
 * décider. Vrai, la mise à jour arrive toute seule et attend qu'on l'installe.
 *
 * `update.skipped` : la version qu'on a écartée. Sans elle, « non merci »
 * voudrait dire « redemande-moi au prochain lancement », ce qui est la
 * définition d'un logiciel insistant.
 *
 * Aucune clé pour « vérifier ou non » : c'est l'adresse du serveur qui décide,
 * et `--no-maj` la coupe pour une exécution. Un troisième interrupteur pour la
 * même chose ferait trois endroits où chercher pourquoi rien ne se passe.
 */
#define NS_CFG_MAJ_AUTO     "update.auto"
#define NS_CFG_MAJ_REFUSEE  "update.skipped"

#define NS_CFG_MOUSE_SENS      "input.mouseSensitivity"
#define NS_CFG_ROOM_SOURCE     "room.source"            /* generated | legacy */

#endif /* NS_CONFIG_H */
