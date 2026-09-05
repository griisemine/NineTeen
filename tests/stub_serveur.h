/*
 * stub_serveur.h — un serveur HTTP de quinze routes, pour que la chaîne du
 * classement soit vérifiable SANS Docker.
 *
 * Pourquoi il existe
 * ------------------
 * `tests/test_online.c` savait déjà mettre bout à bout billet, sceau, file et
 * envoi — mais uniquement quand on lui passait l'adresse d'un vrai `nineteend`
 * et un jeton de session. Sans arguments, il ne vérifiait que du calcul pur.
 * L'intégration continue n'a ni base ni serveur : la moitié la plus fragile du
 * client n'était donc couverte nulle part, et c'est précisément là qu'une panne
 * s'est installée — un billet demandé sur un créneau, réclamé sur un autre, et
 * plus aucun score n'atteignait le classement mondial. Aucune erreur, aucun
 * test rouge, pendant des semaines.
 *
 * Ce bouchon répond ce que le serveur Go répond, aux quatre routes dont le
 * client se sert. Il ne prouve pas que le serveur est correct — c'est le travail
 * de `server/internal/...` et de ses tests Go. Il prouve que le CLIENT parle la
 * langue qu'il croit parler.
 *
 * Ce qu'il n'est pas
 * ------------------
 * Ni concurrent, ni durable, ni sûr : une connexion à la fois, en clair, sur
 * 127.0.0.1 et un port que le système choisit. Il n'a rien à faire ailleurs que
 * dans un test.
 */
#ifndef NS_STUB_SERVEUR_H
#define NS_STUB_SERVEUR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Démarre le bouchon sur 127.0.0.1 et un port libre choisi par le système.
 * Écrit `http://127.0.0.1:<port>` dans `url`. Renvoie false si la pile réseau
 * n'est pas disponible — un test doit alors se sauter, pas échouer.
 */
bool stub_demarrer(char *url, size_t cap);
void stub_arreter(void);

/* Le JETON que le bouchon accepte. Toute autre valeur reçoit 401, comme le vrai
 * serveur, ce qui est le seul moyen de vérifier le chemin « sans compte ». */
#define STUB_JETON "jeton-de-test"

/* Combien de parties ont été OUVERTES et SOUMISES depuis le démarrage. C'est ce
 * qu'on mesure : le client dit qu'il envoie, le serveur dit qu'il reçoit. */
uint32_t stub_parties_ouvertes(void);
uint32_t stub_parties_soumises(void);

/* Le dernier créneau demandé à `POST /api/v1/runs`, par exemple
 * « demineur-hard ». Vide si aucune demande n'est arrivée. */
const char *stub_dernier_creneau(void);

/* Le score que le bouchon a recalculé pour la dernière soumission, ou -1. */
int stub_dernier_score(void);

/*
 * CE QUE LE BOUCHON OFFRE AU TÉLÉCHARGEMENT, pour la mise à jour.
 *
 * `version` est ce qu'annonce `/api/v1/telechargements` ; `nom` est le fichier,
 * dont l'extension décide de la plateforme exactement comme chez le vrai
 * serveur ; `contenu` est ce que le fichier contient ; `somme_hex` est
 * l'empreinte ANNONCÉE, qu'on veut pouvoir mentir volontairement — un paquet
 * dont l'empreinte ne tombe pas juste doit être écarté, et c'est la seule
 * vérification qui protège d'un installeur tronqué.
 *
 * Passer NULL à `version` rend la route vide, ce qui est un serveur sans
 * paquet.
 */
void stub_poser_maj(const char *version, const char *nom,
                    const char *contenu, const char *somme_hex);

/*
 * UN SECOND PAQUET dans la liste, de la même plateforme et d'une AUTRE version.
 *
 * C'est la situation ordinaire d'un répertoire de téléchargement : on y garde
 * les anciennes versions à côté des nouvelles. Deux paquets d'une même
 * plateforme ont la même extension, et sans ce cas dans les tests, le client
 * pouvait prendre le premier venu — ce qu'il a fait, mesuré sur la pile réelle.
 *
 * NULL le retire.
 */
void stub_poser_maj_voisin(const char *nom);

/*
 * COUPER LE FICHIER : la route rend alors 503, comme un serveur qui vient de
 * tomber. C'est la panne ordinaire d'un transfert de 175 Mio, et le seul moyen
 * de vérifier qu'elle se rattrape sans relancer le jeu.
 */
void stub_couper_fichier(bool coupe);

/* Combien de fois le fichier a été demandé. Une reprise en compte deux. */
uint32_t stub_fichiers_servis(void);

#endif /* NS_STUB_SERVEUR_H */
