/*
 * room_economie.h — le portefeuille de la salle, et la boucle qui le remplit.
 *
 * LA BOUCLE :  JETON  ->  PARTIE  ->  TICKETS  ->  LOT.
 *
 * On prend ses jetons au monnayeur, on en insère un pour jouer, la partie rend
 * des tickets selon un barème calibré jeu par jeu, et les tickets s'échangent à
 * la vitrine contre des choses qui se voient ou qui se jouent.
 *
 * CETTE ÉCONOMIE EST FICTIVE ET FERMÉE
 * ------------------------------------
 * Il n'y a AUCUNE voie d'achat : pas de monnaie réelle, pas de boutique, pas
 * de coffre aléatoire payant, pas d'abonnement, pas de publicité. Un jeton ne
 * s'obtient qu'en jouant ou en le demandant au monnayeur, qui ne demande rien
 * en retour. Les taux vivent dans `room_bareme.h`, que les affiches des murs
 * lisent à la construction — voir ce fichier pour le raisonnement complet.
 *
 * Il n'y a pas non plus de minuterie qui punit l'absence, ni de compte à
 * rebours qui presse. Le crochet de ce jeu est la RÉPÉTITION D'UNE BONNE
 * PARTIE : le tournoi du jour, la série, et le quitte ou double. Tous les trois
 * se refusent sans rien perdre d'autre que ce qu'on n'a pas joué.
 *
 * POURQUOI CE FICHIER EST PUR
 * ---------------------------
 * Rien ici n'ouvre de fenêtre, ne touche au GPU ni ne dessine. C'est un état,
 * des règles, et deux fonctions de fichier — exactement la forme qui rend
 * `tests/test_economie.c` possible sans écran, et exactement la raison pour
 * laquelle `room/main.c` n'a que quelques appels à faire. Le corps du travail
 * est ici, pas là-bas : `main.c` est modifié par d'autres chantiers en ce
 * moment, et un module qui s'y répandrait entrerait en conflit à chaque ligne.
 *
 * L'HORLOGE EST INJECTABLE, et ce n'est pas un confort de test. Le tournoi du
 * jour et la série se règlent sur une date ; les vérifier au passage de minuit
 * et au changement d'année demande de pouvoir dire quel jour on est. Sans ça,
 * ces deux règles ne seraient vérifiables qu'en attendant demain.
 */
#ifndef NS_ROOM_ECONOMIE_H
#define NS_ROOM_ECONOMIE_H

#include "room_bareme.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * Le portefeuille
 * ========================================================================== */

/* Les lots, dérivés de la liste unique de `room_bareme.h` : ajouter un lot
 * là-bas l'ajoute ici, dans la vitrine et dans le test, sans autre édition. */
#define ROOM_ECO_LOT_ENUM(cle, prix, titre, quoi) ROOM_ECO_LOT_##cle,
typedef enum room_eco_lot {
    ROOM_ECO_LOTS(ROOM_ECO_LOT_ENUM)
    ROOM_ECO_LOT_COUNT
} room_eco_lot;
#undef ROOM_ECO_LOT_ENUM

/*
 * L'état complet, et il tient dans cette structure — c'est ce qui permet de le
 * copier, de le comparer et de le sérialiser sans que rien ne traîne ailleurs.
 *
 * Les compteurs sont SIGNÉS bien qu'ils ne puissent pas être négatifs. C'est
 * délibéré : un solde en `unsigned` qui passe sous zéro devient quatre
 * milliards, et le défaut se voit alors comme une fortune plutôt que comme une
 * erreur. En signé, une soustraction fautive donne un nombre négatif, que
 * `room_eco_valide()` attrape et que le test peut nommer.
 */
typedef struct room_eco {
    int32_t jetons;
    int32_t tickets;

    /* La série : jours consécutifs où au moins une partie a été terminée. */
    int32_t serie;
    int32_t serie_record;
    int64_t dernier_jour;      /* jour UTC de la dernière partie, 0 = jamais */

    /* Les lots acquis, un bit par lot. */
    uint32_t lots;

    /* Le compteur de vie, pour l'affiche des records et la page de fin. */
    int64_t parties;
    int64_t tickets_gagnes;

    /*
     * LE QUITTE OU DOUBLE EN ATTENTE.
     *
     * `mise` non nulle veut dire qu'une partie vient de finir et que ses
     * tickets ne sont PAS encore versés : ils sont sur la table. Le joueur
     * accepte ou refuse, et refuser les verse. Tant que rien n'est décidé,
     * l'état porte de quoi rejouer la même partie : le jeu, son régime, et le
     * score à battre.
     */
    int32_t mise;
    int32_t mise_score;        /* le score à battre pour doubler */
    char    mise_jeu[24];
    bool    mise_hard;
} room_eco;

/* ==========================================================================
 * Le temps — et pourquoi il est en UTC
 * ==========================================================================
 *
 * Le tournoi du jour veut que TOUT LE MONDE joue la même partie aujourd'hui.
 * Cette phrase et « le classement se remet à zéro à minuit » ne peuvent pas
 * être vraies ensemble : un minuit local fait basculer Tokyo neuf heures avant
 * Paris, et les deux joueraient alors des grilles différentes en s'imaginant le
 * contraire. Entre les deux, c'est la première qui fait le tournoi — un
 * classement où l'on ne joue pas la même chose n'est pas un classement.
 *
 * Le jour est donc le jour UTC, et l'affiche du tournoi le dit en toutes
 * lettres. La série suit le même jour, pour la même raison : deux règles de
 * date différentes dans le même portefeuille finiraient par se contredire au
 * passage de minuit.
 */

/* Le numéro de jour UTC d'un instant epoch. Négatif avant 1970, ce qui n'a pas
 * d'usage mais reste juste : une division entière tronque vers zéro, et un
 * jour de plus au mauvais endroit ferait sauter le test du changement d'année. */
int64_t room_eco_jour_de(int64_t epoch_secondes);

/* Le jour courant. Passe par l'horloge injectée si `room_eco_set_horloge` a
 * été appelée, sinon par l'horloge système. */
int64_t room_eco_jour(void);

/* Fixe l'instant courant, en secondes epoch. `0` rétablit l'horloge système.
 * Réservé aux tests et à la recette : c'est le seul moyen de vérifier le
 * passage de minuit sans attendre minuit. */
void room_eco_set_horloge(int64_t epoch_secondes);

/* ==========================================================================
 * Le tournoi du jour
 * ==========================================================================
 *
 * Les huit jeux tirés d'une graine dérivée de la DATE. Tout le monde joue la
 * même partie aujourd'hui, et demain c'en est une autre.
 *
 * C'est le crochet le plus fort et le plus honnête de cette salle, et il ne
 * coûte presque rien à écrire ici parce que le dépôt l'avait déjà préparé sans
 * le savoir : les huit jeux sont des états purs rejouables au bit près
 * (`tests/test_replay.c`), donc deux machines qui reçoivent la même graine
 * jouent réellement la même partie, et le journal d'entrées scellé rend le
 * score vérifiable.
 */

/* La graine du tournoi pour un jour et un jeu. Déterministe, identique sur
 * toutes les machines, et différente pour chaque couple. */
uint64_t room_eco_graine_tournoi(int64_t jour, const char *jeu);

/* La graine du tournoi d'aujourd'hui. */
uint64_t room_eco_graine_du_jour(const char *jeu);

/* ==========================================================================
 * Le barème
 * ========================================================================== */

/* Les points qui valent un ticket, pour un jeu et un régime. 0 si le jeu est
 * inconnu — auquel cas il ne rapporte rien plutôt que n'importe quoi. */
int32_t room_eco_diviseur(const char *jeu, bool hard);

/* La médiane mesurée qui a servi à poser ce diviseur. Exposée pour le test,
 * qui recalcule les seize quotients, et pour l'affiche des records. */
int32_t room_eco_mediane(const char *jeu, bool hard);

/*
 * Ce qu'une partie rapporte, sans rien verser.
 *
 * `score` est pris en 64 bits SIGNÉ alors qu'un jeu rend un `uint32_t` : c'est
 * la fonction qui reçoit ce qui vient d'un fichier de sauvegarde, d'un journal
 * rejoué ou d'un serveur, et aucune de ces trois sources n'est digne de
 * confiance. Un score négatif rend zéro ticket, pas un solde négatif.
 *
 * `serie` entre en paramètre plutôt que d'être lue dans l'état : c'est ce qui
 * rend la fonction pure, donc vérifiable ligne à ligne.
 */
int32_t room_eco_tickets_pour(const char *jeu, bool hard, int64_t score, int32_t serie);

/* ==========================================================================
 * Les gestes du joueur
 * ========================================================================== */

void room_eco_reset(room_eco *e);

/* Vrai si l'état est cohérent : aucun compteur négatif, aucune mise sans jeu. */
bool room_eco_valide(const room_eco *e);

/*
 * LE MONNAYEUR — le crédit d'accueil, et RIEN d'autre.
 *
 * Il complète jusqu'au plancher, gratuitement, autant de fois qu'on le
 * demande. Il ne consomme aucun ticket. Rend le nombre de jetons ajoutés ;
 * zéro veut dire qu'on avait déjà de quoi.
 *
 * IL A COMMENCÉ PAR FAIRE LES DEUX, ET C'ÉTAIT FAUX. La première version
 * changeait d'abord les tickets en jetons, puis complétait — l'idée étant que
 * bien jouer devait monter au-dessus du plancher. `test_session` l'a démentie
 * en un chiffre : sur vingt parties médianes, le joueur finissait avec 57
 * tickets au lieu de 200, parce que le monnayeur les avait mangés en route
 * pour lui rendre des jetons qu'il aurait eus gratuitement. Une partie médiane
 * rapporte dix tickets et un jeton en coûte dix : le change automatique faisait
 * exactement du sur-place, et la vitrine restait hors de portée pour toujours.
 *
 * Ce que ça a appris sur la conception, et qui est écrit ici pour qu'on ne le
 * réinvente pas : le JETON n'est pas la monnaie de ce jeu, c'est le GESTE — la
 * pièce qu'on enfonce, que `NS_VM_TOKEN` tient déjà dans la main droite. La
 * monnaie, c'est le TICKET. Un geste ne doit rien coûter, sans quoi il devient
 * un péage ; c'est la monnaie qui doit être rare.
 *
 * Le change existe toujours, mais il est DEMANDÉ (`room_eco_changer`) : il sert
 * à celui qui veut un stock et refuse de revenir marcher jusqu'ici, pas à celui
 * qui n'a plus rien.
 */
int32_t room_eco_monnayeur(room_eco *e);

/*
 * LE CHANGE, à la demande : des tickets vers des jetons, au taux du barème.
 *
 * Rend le nombre de jetons effectivement rendus, qui peut être inférieur à ce
 * qu'on demande si les tickets manquent — et vaut zéro si l'on n'a pas de quoi.
 * Rien n'est débité dans ce cas.
 */
int32_t room_eco_changer(room_eco *e, int32_t jetons_voulus);

/* Insère un jeton pour une partie. Faux s'il n'y en a pas — l'appelant renvoie
 * alors au monnayeur plutôt que de laisser jouer à crédit. */
bool room_eco_inserer(room_eco *e);

/*
 * Fin de partie : met à jour la série, calcule les tickets et les POSE SUR LA
 * TABLE sans les verser. Rend ce qui est en jeu.
 *
 * Rien n'est crédité ici, et c'est ce qui rend le quitte ou double possible
 * sans double comptabilité : les tickets d'une partie existent en un seul
 * endroit, la mise. `room_eco_encaisser` les verse, `room_eco_doubler` les
 * risque. Un appelant qui ne veut pas du quitte ou double appelle simplement
 * `room_eco_encaisser` juste après, ce que fait la salle quand le lot
 * correspondant n'est pas acquis.
 */
int32_t room_eco_fin_partie(room_eco *e, const char *jeu, bool hard, int64_t score);

/* Verse la mise et l'efface. Rend ce qui a été versé. C'est le REFUS du quitte
 * ou double, et c'est aussi le chemin normal : refuser doit être au moins aussi
 * facile qu'accepter, donc c'est la même fonction que ne rien faire. */
int32_t room_eco_encaisser(room_eco *e);

/* Le score à battre pour doubler, 0 s'il n'y a pas de mise en cours. */
int32_t room_eco_mise_a_battre(const room_eco *e);

/*
 * QUITTE OU DOUBLE : le résultat de la reprise.
 *
 * `score` est celui de la reprise, jouée en régime DIFFICILE quel que soit le
 * régime de la partie d'origine — c'est ce qui fait le risque. Battre le score
 * de départ double la mise et la verse ; échouer la perd.
 *
 * Rend ce qui a été versé : le double, ou zéro. La mise est effacée dans les
 * deux cas, donc on ne peut pas rejouer la même mise deux fois.
 */
int32_t room_eco_doubler(room_eco *e, int64_t score);

/* ==========================================================================
 * La vitrine à lots
 * ========================================================================== */

const char *room_eco_lot_titre(room_eco_lot lot);
const char *room_eco_lot_quoi(room_eco_lot lot);
int32_t     room_eco_lot_prix(room_eco_lot lot);

bool room_eco_lot_acquis(const room_eco *e, room_eco_lot lot);

/* Échange les tickets contre un lot. Faux si le lot est déjà pris, inconnu, ou
 * hors de prix — et dans ce dernier cas rien n'est débité. */
bool room_eco_acheter(room_eco *e, room_eco_lot lot);

/* Le prochain lot que le joueur peut s'offrir, ou le moins cher qui lui manque.
 * C'est ce que la vitrine MONTRE : une vitrine qui n'annonce pas ce qu'on peut
 * avoir est un meuble. `ROOM_ECO_LOT_COUNT` s'il n'en manque aucun. */
room_eco_lot room_eco_lot_en_vue(const room_eco *e);

/* ==========================================================================
 * Persistance
 * ==========================================================================
 *
 * À CÔTÉ DES RÉGLAGES, dans `ns_path_user_dir()` — le même répertoire que
 * `ns_config` et `ns_scores`, et pas un troisième emplacement. Format texte
 * ligne à ligne pour la même raison que `ns_scores` : une ligne abîmée se saute
 * et le reste survit, alors qu'un fichier structuré tronqué est perdu en
 * entier. C'est le cas de la coupure de courant, et c'est celui qu'on veut
 * survivre.
 */
const char *room_eco_chemin(void);

/* Redirige le fichier. NULL rétablit le chemin normal. Pour les tests, qui ne
 * doivent pas toucher au portefeuille de qui les exécute. */
void room_eco_set_chemin(const char *chemin);

/* Absent ou illisible, on repart d'un portefeuille neuf SANS erreur : un joueur
 * qui lance le jeu pour la première fois n'a rien fait de mal. */
void room_eco_charger(room_eco *e);
bool room_eco_sauver(const room_eco *e);

/* ==========================================================================
 * LA FAÇADE DE LA SALLE
 * ==========================================================================
 *
 * Pourquoi elle existe, alors que tout ce qui précède suffirait
 * -------------------------------------------------------------
 * Parce que `room/main.c` est modifié par d'autres chantiers en ce moment, et
 * qu'un module qui s'y répandrait entrerait en conflit à chaque ligne. Tout ce
 * que la salle a besoin de dire à l'économie tient donc dans une poignée
 * d'appels sans état : le portefeuille, le pari en cours et le bandeau
 * transitoire vivent ICI, pas là-bas.
 *
 * Ce n'est pas seulement une commodité de fusion. `main.c` fait déjà 4 059
 * lignes et gère la salle, la caméra, le son, le rendu, le duel et le
 * classement ; y ajouter six variables d'état d'économie les mettrait dans le
 * même sac que les autres, et la prochaine règle de barème s'écrirait là-bas.
 * L'interface commune des jeux (`games.h`) a été écrite pour exactement cette
 * raison, et pour exactement ce fichier.
 *
 * Le singleton est assumé : il n'y a qu'un joueur et qu'un portefeuille. Les
 * fonctions pures au-dessus restent le vrai sujet du test — la façade n'est
 * qu'un branchement.
 */

/* Charge le portefeuille. Idempotent. */
void            room_eco_salle_ouvrir(void);
/* Sauve et referme. Sans effet si l'on n'a jamais ouvert. */
void            room_eco_salle_fermer(void);
/* Le portefeuille courant, pour l'affichage. Jamais NULL. */
const room_eco *room_eco_salle(void);

/* Insère un jeton pour une partie. Faux s'il n'y en a plus — l'appelant renvoie
 * alors au monnayeur, et le bandeau le dit déjà. */
bool room_eco_salle_jeton(void);

/*
 * Fin de partie. Verse les tickets, OU résout le quitte ou double s'il était
 * armé. C'est le seul appel que la salle a à faire : la décision de verser, de
 * proposer ou de résoudre est prise ici.
 */
void room_eco_salle_fin(const char *jeu, bool hard, uint32_t score);

/* Le monnayeur et la vitrine, actionnés par le joueur qui s'en approche. */
void room_eco_salle_monnayeur(void);
void room_eco_salle_vitrine(void);

/*
 * LE QUITTE OU DOUBLE.
 *
 * `offre` est vraie tant qu'une partie attend une réponse. Accepter rend vrai
 * s'il faut relancer la même borne EN RÉGIME DIFFICILE — c'est tout ce que la
 * salle a à savoir. Refuser verse, et ne demande rien de plus qu'accepter :
 * c'est la contrainte de conception, et c'est pour ça que les deux sont deux
 * fonctions symétriques plutôt qu'une confirmation et un défaut.
 */
bool    room_eco_salle_offre(void);
int32_t room_eco_salle_offre_mise(void);
int32_t room_eco_salle_offre_battre(void);
bool    room_eco_salle_accepter(void);
void    room_eco_salle_refuser(void);

/* Le bandeau transitoire : ce qui vient de se passer, en un mot. Chaîne vide
 * quand il n'y a rien à dire. */
const char *room_eco_salle_message(void);
float       room_eco_salle_message_reste(void);
void        room_eco_salle_avancer(float dt);

#endif /* NS_ROOM_ECONOMIE_H */
