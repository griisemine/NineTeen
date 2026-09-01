/*
 * ns_maj.h — la mise à jour, et la leçon de la V1 tenue à la lettre.
 *
 * CE QU'ON NE REFERA PAS
 * ----------------------
 * En 2020, tout le corps de `main()` était enfermé dans `if (checkVersion(…))`.
 * Sans réponse du serveur, le jeu affichait « une nouvelle version est
 * disponible » puis se fermait : hors ligne, il ne pouvait pas atteindre sa
 * propre fenêtre. C'est le défaut fondateur de ce dépôt, cité dans
 * `ns_scores.h`, `ns_online.h` et `room/CMakeLists.txt`.
 *
 * Écrire un mécanisme de mise à jour, c'est donc écrire très exactement la
 * chose qui a tué la V1. Trois règles, et elles ne se négocient pas :
 *
 *   1. RIEN N'ATTEND. `ns_maj_init` rend la main tout de suite ; un fil fait la
 *      requête. Le jeu démarre, se joue et se ferme sans savoir s'il y a une
 *      réponse.
 *   2. RIEN N'ÉCHOUE. Pas de serveur, pas de réseau, pas de paquet pour cette
 *      plateforme, réponse absurde : le résultat est « on ne sait pas », et le
 *      jeu ne s'en porte pas plus mal.
 *   3. RIEN NE S'INSTALLE TOUT SEUL. Le paquet est téléchargé et VÉRIFIÉ, puis
 *      on le propose. Remplacer un exécutable sous les pieds de celui qui joue
 *      demande des droits qu'on n'a pas toujours, casse la signature du paquet
 *      là où il y en a une, et se fait sans que personne l'ait demandé. On
 *      ouvre l'installeur avec le système, et c'est le joueur qui décide.
 *
 * CE QUE « SE METTRE À JOUR » VEUT DIRE ICI
 * -----------------------------------------
 * Au lancement, sur un fil : demander `/api/v1/telechargements`, comparer la
 * version annoncée à celle qu'on est, choisir le paquet de CETTE plateforme et
 * de CETTE architecture, le télécharger dans le répertoire utilisateur en
 * reprenant s'il était commencé, vérifier son empreinte SHA-256 contre celle
 * que le serveur publie, et alors seulement le dire.
 *
 * L'empreinte n'est pas une formalité : sans elle, une coupure au milieu d'un
 * paquet de 175 Mio livrerait un installeur tronqué, ce qui est la seule façon
 * de casser une machine avec une mise à jour.
 *
 * OÙ VA LE PAQUET
 * ---------------
 * `<répertoire utilisateur>/maj/`, à côté de `runs/`. Jamais dans le
 * répertoire d'installation, qui est en lecture seule sur les trois systèmes
 * dès qu'on n'est pas développeur.
 */
#ifndef NS_MAJ_H
#define NS_MAJ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * COMPARER DEUX VERSIONS, et c'est du calcul pur — donc testable sans socket.
 *
 * Rend < 0 si `a` précède `b`, 0 si elles sont égales, > 0 sinon. Les numéros
 * sont lus champ par champ en ENTIERS et non en texte : « 17.10.0 » vient après
 * « 17.9.0 », ce qu'une comparaison de chaînes rend faux, et ce jour-là la mise
 * à jour cesserait de se proposer sans que rien ne le dise. Un champ absent
 * vaut zéro, ce qui fait de « 17 » et « 17.0.0 » la même version.
 *
 * Tout suffixe non numérique arrête la lecture du champ : « 17.1.0-rc2 » est
 * traité comme « 17.1.0 ». C'est volontaire — ce dépôt ne publie pas de
 * pré-versions, et inventer un ordre entre « rc2 » et « beta » serait décider
 * de quelque chose qui n'existe pas.
 */
int ns_maj_comparer(const char *a, const char *b);

/*
 * LE PAQUET QUI CONVIENT À CETTE MACHINE, choisi sans rien deviner.
 *
 * `plateforme` et `arch` sont ceux que le serveur annonce pour un fichier, dans
 * SON vocabulaire — celui de `Classer` dans
 * `server/internal/telechargements/telechargements.go` : « windows », « macos »,
 * « linux », et pour l'architecture « universel », « arm64 », « x86_64 » ou
 * rien. Ce vocabulaire est recopié ici parce qu'il est le contrat entre les
 * deux moitiés ; s'en écarter d'une lettre — « universal » au lieu de
 * « universel », « aarch64 » au lieu de « arm64 » — ne produit aucune erreur,
 * seulement un joueur à qui l'on ne propose jamais rien.
 *
 * Une architecture VIDE ou « universel » convient partout : c'est le cas du
 * `.dmg` macOS, qui porte les deux jeux d'instructions. Une architecture
 * nommée doit correspondre — un paquet `arm64` ne s'installe pas sur un PC
 * `x86_64`, et le proposer serait pire que ne rien proposer.
 *
 * Séparée du reste pour la même raison que `ns_maj_comparer` : c'est la
 * décision qui se trompe en silence, et elle se teste sans réseau.
 */
bool ns_maj_convient(const char *plateforme, const char *arch);

/*
 * LEQUEL PRENDRE quand il y en a plusieurs, et c'est le cas ordinaire sur
 * Linux : le serveur y publie une AppImage, un `.deb`, un `.rpm` et un
 * `.tar.gz` de la même version.
 *
 * Rend un rang, le plus petit gagnant, ou -1 si l'extension n'est pas un
 * paquet. Le classement suit CE QUE LE JOUEUR AURA À FAIRE :
 *
 *   Linux    AppImage  <  .deb  <  .rpm  <  .tar.gz
 *            l'AppImage se lance sans droits d'administrateur et sans
 *            gestionnaire de paquets ; le `.deb` exige les deux.
 *   macOS    .dmg  <  .pkg
 *            le `.dmg` se glisse dans Applications ; le `.pkg` installe.
 *   Windows  .exe  <  .msi  <  .zip
 *
 * Décidé ici et non par l'ordre de la liste : celle du serveur est triée par
 * nom, si bien que le bon paquet n'arrivait en tête que par la chance d'une
 * majuscule.
 */
int ns_maj_rang_paquet(const char *nom);

/*
 * LE PAQUET PORTE-T-IL LA VERSION ANNONCÉE.
 *
 * Un répertoire de téléchargement garde les anciennes versions à côté des
 * nouvelles : le serveur annonce la plus récente, la liste contient les deux, et
 * deux paquets de la même plateforme ont la même extension. Sans ce contrôle,
 * on téléchargeait le premier venu — mesuré sur la pile réelle, où le jeu
 * annonçait « 17.1.0 disponible » en s'apprêtant à installer le paquet 17.0.0
 * dont il partait.
 *
 * La comparaison est BORNÉE à droite : « 17.1.0 » ne doit pas se reconnaître
 * dans « 17.1.01 », ni « 17.1 » dans « 17.10 ».
 */
bool ns_maj_porte_version(const char *nom, const char *version);

/* Le nom que le serveur donne à cette plateforme et à cette architecture, tels
 * que compilés ici. */
const char *ns_maj_plateforme(void);
const char *ns_maj_arch(void);

typedef enum ns_maj_etat {
    NS_MAJ_INACTIVE = 0,   /* pas de serveur, ou --no-maj : rien ne se passe */
    NS_MAJ_QUESTION,       /* la question est posée, la réponse n'est pas là */
    NS_MAJ_A_JOUR,         /* le serveur ne propose rien de plus récent */
    NS_MAJ_DISPONIBLE,     /* une version plus récente existe, avec un paquet */
    NS_MAJ_TRANSFERT,      /* le paquet arrive */
    NS_MAJ_PRETE,          /* le paquet est là ET son empreinte est bonne */
    NS_MAJ_ECHEC,          /* on a essayé, on a échoué, on le dit */
} ns_maj_etat;

typedef struct ns_maj_config {
    const char *server_url;   /* NULL ou vide = INACTIVE, aucune socket */
    const char *version;      /* celle qu'on est, en général NINETEEN_VERSION */
    bool        locked;       /* --offline ou --no-maj : interdit tout */
    /*
     * Télécharger sans qu'on l'ait demandé. Faux par défaut, et ce défaut est
     * un choix : un paquet fait 175 Mio, et les prendre sur la ligne de
     * quelqu'un qui voulait jouer dix minutes n'est pas à nous de le décider.
     * Le réglage `update.auto` de `settings.cfg` le passe à vrai.
     */
    bool        auto_transfert;
} ns_maj_config;

/*
 * Démarre la question, ou pas. Rend true si un fil a été lancé. Aucun échec
 * n'est fatal, et l'appelant n'a rien à rattraper.
 */
bool ns_maj_init(const ns_maj_config *cfg);
void ns_maj_shutdown(void);

ns_maj_etat ns_maj_etat_courant(void);

/* La version proposée, ou une chaîne vide. Jamais NULL. */
const char *ns_maj_version_offerte(void);

/* Une phrase pour l'écran, toujours prête, jamais NULL. */
const char *ns_maj_message(void);

/* De 0 à 1 pendant `NS_MAJ_TRANSFERT`, 0 ailleurs. */
float ns_maj_avancement(void);

/* Le chemin du paquet vérifié, ou une chaîne vide tant qu'il n'est pas PRÊT. */
const char *ns_maj_paquet(void);

/*
 * Demande le téléchargement. Sans effet si l'état n'est pas DISPONIBLE, ou si
 * un transfert est déjà en cours. Rend la main tout de suite.
 */
void ns_maj_telecharger(void);

/*
 * Confie le paquet au système : `.dmg`, `.exe` ou `.AppImage` s'ouvrent avec
 * l'outil qui sait les installer. Rend false si rien n'est prêt.
 *
 * On n'exécute RIEN nous-mêmes : le paquet est passé au système, qui applique
 * ses propres contrôles — Gatekeeper, SmartScreen, le gestionnaire de paquets.
 * C'est ce qui fait qu'une mise à jour reste une décision du joueur.
 */
bool ns_maj_installer(void);

/*
 * Oublie cette version : elle ne sera plus proposée. Écrit dans la
 * configuration, donc survit au redémarrage — sans quoi « non merci » voudrait
 * dire « redemande-moi dans dix secondes ».
 */
void ns_maj_refuser(void);

/* Le répertoire où les paquets atterrissent. Pour le journal et les tests. */
const char *ns_maj_repertoire(void);

/* Redirige ce répertoire — pour les tests, qui n'ont rien à faire dans le
 * répertoire utilisateur de celui qui les exécute. NULL rétablit le défaut. */
void ns_maj_set_repertoire(const char *chemin);

#endif /* NS_MAJ_H */
