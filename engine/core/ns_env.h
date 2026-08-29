/*
 * ns_env.h — les réglages qu'on veut pouvoir CHANGER SANS RECOMPILER.
 *
 * Pourquoi un fichier de plus alors que `ns_config` existe
 * --------------------------------------------------------
 * `ns_config` est la mémoire du JEU : il vit dans le répertoire utilisateur, il
 * est ÉCRIT par le menu, et il garde ce que le joueur a choisi entre deux
 * parties. On ne va pas l'éditer à la main — il n'est même pas dans le dépôt,
 * et sur une application installée dans /Applications on ne saurait pas où le
 * trouver.
 *
 * Celui-ci est l'inverse : un fichier POSÉ À CÔTÉ DU JEU, versionné, lu au
 * démarrage et jamais écrit. Il porte les cotes du personnage et de la caméra —
 * taille, vitesse, portée du bras, distance de la vue à la troisième personne —
 * c'est-à-dire précisément ce qu'on veut régler par essais successifs sans
 * relancer une compilation de trois minutes à chaque essai.
 *
 * Le format, et pourquoi celui-là
 * -------------------------------
 * `CLE=VALEUR`, une par ligne, `#` en commentaire. C'est le format `.env`, et
 * il est choisi pour une raison simple : n'importe qui sait l'éditer, sans
 * outil, sans virgule à oublier. Une clé inconnue est IGNORÉE en silence —
 * c'est ce qui permet d'y laisser des notes et des essais commentés.
 *
 * La règle qui compte
 * -------------------
 * Une clé qui n'a pas de LECTEUR n'existe pas. Ce dépôt a déjà retiré deux
 * clés de configuration réservées depuis des mois que personne ne lisait :
 * « une clé qu'on ne peut pas lire ne documente rien, elle laisse croire qu'un
 * réglage existe ». Chaque clé listée dans `nineteen.env` est lue quelque part,
 * et `ns_env_report_unread()` le VÉRIFIE au démarrage : un fichier qui contient
 * une clé que personne ne consomme le dit dans le journal.
 *
 * L'ordre de préséance
 * --------------------
 *   valeur par défaut dans le code  <  nineteen.env  <  ligne de commande
 *
 * Le fichier peut donc tout régler, et la ligne de commande garde le dernier
 * mot — ce qui est indispensable aux captures et à l'intégration continue, qui
 * ne doivent pas dépendre du fichier posé sur la machine.
 */
#ifndef NS_ENV_H
#define NS_ENV_H

#include <stdbool.h>

/*
 * Cherche et charge le fichier. `explicit_path` vient de `--env=` et l'emporte
 * sur tout ; sans lui on cherche `nineteen.env` dans le répertoire courant,
 * puis à côté de l'exécutable, puis dans le dossier des assets.
 *
 * L'absence de fichier N'EST PAS une erreur : le jeu tourne sur ses valeurs par
 * défaut, et c'est le cas normal pour quelqu'un qui vient d'installer.
 */
void ns_env_load(const char *explicit_path);
void ns_env_shutdown(void);

/* Le chemin réellement chargé, ou NULL. Pour le journal et pour le menu. */
const char *ns_env_path(void);

float ns_env_float(const char *key, float fallback);
int   ns_env_int(const char *key, int fallback);
bool  ns_env_bool(const char *key, bool fallback);
const char *ns_env_str(const char *key, const char *fallback);

/*
 * Prévient pour chaque clé du fichier que PERSONNE n'a lue.
 *
 * À appeler une fois, après que tous les consommateurs ont lu les leurs. C'est
 * le garde-fou qui empêche ce fichier de devenir un cimetière de réglages
 * imaginaires : une faute de frappe dans une clé est silencieuse par
 * construction — la valeur par défaut s'applique et rien ne change — et c'est
 * exactement le genre de silence que ce dépôt s'emploie à supprimer.
 */
void ns_env_report_unread(void);

#endif /* NS_ENV_H */
