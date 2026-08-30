/*
 * ns_arene.h — LE COUPERET sur le fil : deux à huit salles, une seule règle.
 *
 * Ce que c'est, en une phrase
 * ---------------------------
 * Le client C du relais d'arène : il entre dans un salon, apprend qui est là,
 * reçoit le coup d'envoi, publie l'état de sa place et transporte les actions
 * et le verdict du couperet. Il ne joue à rien et n'arbitre rien lui-même.
 *
 * Son frère est `ns_lockstep.h`, et la parenté va loin : même cadrage binaire,
 * même relais Go, même refus de laisser le serveur décider quoi que ce soit.
 * La différence tient en une phrase — le duel RECOPIE vers l'autre, l'arène
 * DIFFUSE aux sept autres, et le relais écrit devant chaque trame la place de
 * celui qui l'a envoyée.
 *
 * ==========================================================================
 * QUI ARBITRE — le point le plus important de ce fichier
 * ==========================================================================
 * LA PLACE 0 ARBITRE. C'est elle qui fait tourner `room_cp_avancer`, elle qui
 * résout les actions par `room_cp_agir`, et elle qui diffuse ce que la règle a
 * décidé. Les autres places APPLIQUENT ce qu'elles reçoivent.
 *
 * Pourquoi pas chacun chez soi : la règle du couperet n'est pas une fonction du
 * seul état local. `room_cp_agir` consomme le blindage de la VICTIME et débite
 * les fusibles de l'AUTEUR ; `room_cp_avancer` sort la place la moins-disante de
 * TOUTES. Huit clients qui résoudraient chacun leur copie divergeraient au
 * premier ordre d'arrivée différent, et deux d'entre eux se contrediraient
 * alors sur qui est tombé — c'est-à-dire exactement le défaut que
 * `ns_lockstep.h` refuse de livrer en silence.
 *
 * HORS LIGNE, LE CHEMIN DE CODE EST LE MÊME. Sans arène ouverte, `ns_arene` est
 * un pointeur nul : `ns_arene_ma_place(NULL)` vaut 0, `ns_arene_arbitre(NULL)`
 * vaut vrai, et chaque fonction d'envoi ne fait rien. La salle appelle donc la
 * même suite d'instructions qu'elle joue seule ou à huit, et il n'y a pas un
 * « mode hors ligne » à maintenir à côté du vrai.
 *
 * LA PLACE 0 PEUT MENTIR, et il faut l'écrire ici comme le relais écrit la
 * sienne. C'est la même franchise que « deux clients complices peuvent se
 * mentir pendant un duel », déjà en tête de `server/internal/duel/relay.go` :
 * un arbitre malhonnête peut déclarer sorti quelqu'un qui ne l'était pas. Ce
 * que ça ne touche PAS : l'autorité sur les scores ENREGISTRÉS n'a pas bougé
 * d'un pouce depuis M6 — le journal de partie scellé par HMAC, envoyé par
 * HTTP, RECALCULÉ par le serveur. Une manche de Couperet ne fabrique aucun
 * score mondial ; elle distribue des points qui naissent au coup d'envoi et
 * meurent au verdict (`room_bareme.h` : économie fictive et fermée).
 *
 * L'usurpation qui EST fermée, elle, l'est par le relais : il écrit lui-même la
 * place de l'émetteur devant chaque trame diffusée, donc personne ne peut
 * envoyer une action « de la part de la place 3 ». C'est le seul octet
 * d'identité du protocole, et ce module s'en sert pour un contrôle qu'il est le
 * seul à pouvoir faire : UN VERDICT QUI NE VIENT PAS DE LA PLACE 0 EST JETÉ.
 *
 * ==========================================================================
 * POURQUOI LE RELAIS N'ARBITRE PAS, alors que ce serait tentant
 * ==========================================================================
 * Parce qu'il faudrait porter la règle du couperet en Go, en double de sa
 * version C. La règle vit dans `room_couperet.c` : la table des seize durées
 * mesurées, l'exposant 1,35, la période de 45 s, les six actions et leur ordre
 * de résolution (leurre, puis blindage, puis l'effet). Deux descriptions d'une
 * même chose finissent toujours par se contredire — c'est le raisonnement écrit
 * en tête de `room_bareme.h`, et il ne s'affaiblit pas parce que la seconde
 * copie serait dans un autre langage : il empire.
 *
 * Un relais qui arbitrerait serait aussi une seconde AUTORITÉ, donc une seconde
 * surface à défendre. Le relais actuel n'a rien à défendre : il apparie et il
 * diffuse, et son pire échec est de perdre une trame.
 *
 * ==========================================================================
 * LE VERROU, HÉRITÉ ET NON RÉIMPLÉMENTÉ
 * ==========================================================================
 * `ns_arene_ouvrir` commence par interroger `ns_online_server_url()` et rend
 * NULL sans rien tenter si la réponse est NULL. La garantie « sans URL
 * configurée, aucune socket n'est ouverte » reste donc écrite à UN SEUL endroit
 * — `ns_online_init` — exactement comme `ns_realtime_init` en hérite.
 *
 * Le relais n'est pourtant PAS le serveur HTTP : il écoute un autre port, et
 * son adresse arrive par `ns_arene_config`. Hériter du verrou quand même est
 * délibéré — il n'y a aucune raison d'ouvrir l'une des deux sockets quand
 * l'autre est interdite, et `--offline` doit vouloir dire hors ligne.
 *
 * Ce module n'ajoute PAS de second verrou propre, contrairement à
 * `ns_realtime` : la présence publie une position sans qu'on ait rien demandé,
 * alors qu'entrer dans une arène est un geste explicite. Le second verrou du
 * temps réel protège d'une diffusion subie ; ici il n'y en a pas.
 *
 * ==========================================================================
 * RIEN NE BLOQUE, JAMAIS
 * ==========================================================================
 * Comme `ns_online` et `ns_realtime` : UN FIL DE TRAVAIL par arène. La boucle
 * de jeu DÉPOSE (`ns_arene_publier`, `ns_arene_agir`, …) et RELIT
 * (`ns_arene_prendre`, `ns_arene_places`). Elle n'attend jamais la socket, pas
 * même à l'ouverture : `ns_arene_ouvrir` rend la main tout de suite et c'est le
 * fil qui se connecte.
 *
 * Un relais lent, mort, ou qui tombe au milieu d'une manche se voit d'UNE
 * SEULE façon : les autres places cessent d'envoyer leur état, leur `age_ms`
 * grimpe, elles se figent puis passent la péremption — et la manche continue.
 * `ns_arene_arbitre` redevient vrai dès que la liaison est morte, quelle que
 * soit la place occupée : sans ça, un joueur de la place 3 dont le relais
 * disparaît attendrait pour l'éternité un couperet que plus personne n'envoie.
 * UNE MANCHE NE DOIT JAMAIS S'ARRÊTER.
 */
#ifndef NS_ARENE_H
#define NS_ARENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * HUIT places. Ce n'est pas un chiffre choisi ici : c'est celui du relais
 * (`placesMax`, 2 à 8) et celui du mode (`ROOM_CP_MAX_PLACES`). Le tableau des
 * places doit tenir dans une trame, et 1 + 8 x 25 = 201 octets y tiennent.
 */
#define NS_ARENE_MAX_PLACES 8
#define NS_ARENE_PLACES_MIN 2

/* 24 octets sur le fil, dont 23 utiles : le relais coupe à 23 pour que le champ
 * soit TOUJOURS terminé par un zéro une fois posé dans un `char[24]` en C.
 * Voir `pseudoPropre` dans `relay.go`. On le termine quand même nous-mêmes :
 * on ne fait pas confiance à ce qui sort d'une socket. */
#define NS_ARENE_PSEUDO 24

/* La largeur du nom de borne sur le fil, alignée sur `ROOM_CP_JEU`. */
#define NS_ARENE_JEU 24

/*
 * LE RYTHME DE PUBLICATION DE L'ÉTAT : 4 Hz, soit 250 ms.
 *
 * C'est le rythme de la présence (`NS_RT_PERIOD_MS`), mais PAS pour la même
 * raison, et il faut le dire parce que la justification de `ns_realtime.c` ne
 * s'applique pas ici. Là-bas, 4 Hz est imposé par le TRANSPORT : `ns_http`
 * rouvre une socket à chaque battement, et vingt allers-retours par seconde y
 * coûteraient vingt poignées de main TCP. Ici la socket est ouverte une fois
 * pour toute la manche, et cette contrainte-là n'existe pas.
 *
 * Ce qui décide ici est la NATURE de ce qu'on publie. L'ÉTAT n'est pas une
 * position, c'est un tableau de scores : points, fusibles, camp et borne jouée ne
 * changent qu'à des ÉVÉNEMENTS — et les événements voyagent par leurs propres
 * trames, immédiatement, sans attendre le battement. Le seul champ qui varie
 * sans arrêt est le score de la partie en cours, et son seul lecteur pressé est
 * le bandeau de menace (`room_cp_menace`), c'est-à-dire un chiffre que regarde
 * un humain. 250 ms de retard sur un chiffre affiché ne se voient pas.
 *
 * Le PRIX, lui, se calcule exactement — les tailles sont plus bas :
 *
 *     charge d'ÉTAT                        46 octets
 *     sur le fil, en-tête + place ajoutés  50 octets
 *     salon plein, 8 places à 4 Hz         8 x 7 x 4 x 50 = 11 200 o/s au relais
 *     ce qui arrive chez un joueur         7 x 4 x 50     =  1 400 o/s
 *
 * À 20 Hz ce serait cinq fois plus pour rafraîchir cinq fois plus vite un
 * chiffre que personne ne lit cinq fois plus vite. À 1 Hz on économiserait
 * 9 kio/s et le bandeau prendrait jusqu'à une seconde de retard, ce qui se
 * voit. Pour comparaison, `ns_lockstep.c` chiffre le duel à 1,2 kio/s par
 * joueur à 120 Hz : l'arène coûte donc au joueur à peu près le même ordre de
 * grandeur, pour sept adversaires au lieu d'un.
 */
#define NS_ARENE_PERIODE_MS 250u

/*
 * LA PÉREMPTION D'UN ÉTAT : 3 secondes, soit DOUZE battements manqués.
 *
 * Sur une socket persistante, perdre douze battements d'affilée n'est plus de
 * la latence : c'est un client qui ne parle plus. C'est aussi la valeur que le
 * temps réel retient pour la présence (`NS_RT_STALE_MS`), et il n'y aurait
 * aucun sens à ce qu'un joueur disparaisse de l'allée tout en restant frais au
 * tableau des scores.
 *
 * Ce n'est PAS une déconnexion : le relais annonce les départs par son propre
 * `fBye`, qui devient `NS_ARENE_EVT_DEPART`. La péremption ne couvre que le cas
 * où la trame n'arrive plus alors que la socket tient encore.
 */
#define NS_ARENE_PEREMPTION_MS 3000u

/* La charge maximale d'une trame, la même que le relais (`frameMax`). Un pair
 * qui annonce plus est un pair qu'on ferme, SANS allouer. */
#define NS_ARENE_CHARGE_MAX 512

/*
 * La charge maximale d'une trame LIBRE, celle qu'on envoie et que le relais
 * rediffuse en écrivant la place devant : 511, parce que l'octet de place doit
 * tenir dans la même trame. Une charge de 512 fait FERMER la connexion côté
 * relais — il refuse de rogner, parce qu'une charge amputée d'un octet ne veut
 * pas dire « trop long », elle veut dire autre chose. On applique la même
 * borne ici pour ne pas la découvrir en se faisant raccrocher au nez.
 */
#define NS_ARENE_LIBRE_MAX (NS_ARENE_CHARGE_MAX - 1)

/* ==========================================================================
 * LE VOCABULAIRE DES TRAMES, octet par octet
 * ==========================================================================
 *
 * LE CADRAGE est celui du relais, et il n'appartient pas à ce fichier :
 *
 *     uint16 longueur de la charge (petit-boutiste) | uint8 type | charge
 *
 * La longueur ne couvre pas les trois octets d'en-tête. Tous les entiers sont
 * PETIT-BOUTISTES et écrits octet par octet — une arène peut mêler des machines
 * d'ordre différent, et c'est le genre de détail qui ne se voit qu'une fois
 * qu'un joueur a perdu une manche.
 *
 * LES TYPES QUI APPARTIENNENT AU RELAIS (< 0x20) — on les subit, on ne les
 * fabrique pas. Le relais ne rediffuse JAMAIS un type inférieur à 0x20 venu
 * d'un client, ce qui interdit à un joueur de forger un coup d'envoi ou un
 * tableau des places :
 *
 *   0x10 JOIN    nous -> relais, 34 octets EXACTEMENT (moins = on est fermé)
 *                  0   8 : uint64 salon
 *                  8   1 : uint8  place, 0-basée, < places_attendues
 *                  9   1 : uint8  places_attendues, 2 à 8
 *                 10  24 : char   pseudo[24], complété de zéros
 *   0x11 ROSTER  relais -> tous, 1 + 25n octets
 *                  0   1 : uint8  n
 *                 puis n entrées de 25 octets, TRIÉES par place croissante :
 *                  +0  1 : uint8  place
 *                  +1 24 : char   pseudo[24], terminé par un zéro
 *                Il part à chaque arrivée ET à chaque départ, à tout le monde.
 *   0x02 START   relais -> tous, 8 octets de graine, bit de poids fort effacé.
 *                Il part quand toutes les places sont prises, APRÈS le tableau
 *                de la dernière arrivée.
 *   0x05 BYE     relais -> tous, 1 octet = LA PLACE DU PARTANT. À un départ
 *                l'ordre est BYE puis ROSTER : on apprend qui s'en va, puis on
 *                reçoit la salle telle qu'elle est.
 *
 * LES TYPES QUI SONT À NOUS (>= 0x20) — c'est ici que ce fichier décide.
 *
 * Le relais les rediffuse sans les regarder, aux AUTRES places seulement, en
 * écrivant devant la charge UN OCTET : la place de l'émetteur. Les dispositions
 * ci-dessous sont donc données DEUX FOIS quand elles diffèrent — ce qu'on
 * écrit, et ce qui arrive.
 *
 *   0x20 ETAT     l'état d'une place, publié à 4 Hz par CHACUNE.
 *
 *                 écrit (46 octets) :
 *                   0   1 : uint8  drapeaux
 *                            bit 0 : la place est VIVANTE (0 = spectre)
 *                            bit 1 : la partie en cours est en régime difficile
 *                            bits 2-7 : zéro, réservés
 *                   1   1 : uint8  camp, 0 à 7
 *                   2   4 : int32  points ENCAISSÉS
 *                   6   4 : int32  fusibles
 *                  10   4 : int32  ce que la place vaut DEVANT LE COUPERET,
 *                                  c'est-à-dire `room_cp_valeur` : les points
 *                                  encaissés plus ce que la partie en cours
 *                                  paierait si elle finissait maintenant. Deux
 *                                  grandeurs et non une, pour la raison écrite
 *                                  au-dessus de `room_cp_valeur` : « qui gagne »
 *                                  et « qui tombe » ne se jugent pas pareil.
 *                  14   8 : int64  le score COURANT de la partie. Signé et sur
 *                                  64 bits pour la même raison que
 *                                  `room_cp_points_pour` : la valeur vient d'un
 *                                  pair, et aucun pair n'est digne de confiance.
 *                  22  24 : char   jeu[24], complété de zéros. `jeu[0] == 0`
 *                                  veut dire « ne joue pas », exactement comme
 *                                  dans `room_cp_place` — un drapeau de plus
 *                                  aurait pu contredire le nom.
 *                 reçu (47 octets) : [place émettrice] + les 46 ci-dessus.
 *
 *   0x21 ACTION   « je vise la place C avec l'action A ».
 *
 *                 écrit (2 octets) :
 *                   0   1 : uint8  cible
 *                   1   1 : uint8  action (`room_cp_action`)
 *                 reçu (3 octets) :
 *                   0   1 : uint8  AUTEUR — écrit par le relais, pas par nous
 *                   1   1 : uint8  cible
 *                   2   1 : uint8  action
 *
 *                 L'auteur n'est PAS dans ce qu'on écrit, et c'est tout
 *                 l'intérêt : un joueur ne peut pas saboter « de la part » d'un
 *                 autre. C'est la seule chose que le relais soit en position de
 *                 garantir, et elle ne lui coûte pas de connaître les règles.
 *
 *   0x22 VERDICT  ce que le couperet a décidé. LA PLACE 0 SEULE l'envoie, et
 *                 une trame de ce type venue d'une autre place est JETÉE.
 *
 *                 écrit (7 octets) :
 *                   0   1 : uint8  numéro du couperet, 1 pour le premier.
 *                                  Un octet suffit et c'est démontrable : une
 *                                  manche est bornée à `ROOM_CP_MANCHE_MAX_S`
 *                                  (900 s) et la période vaut 45 s, donc vingt
 *                                  couperets au maximum, contre 255 tenables.
 *                   1   1 : uint8  la place SORTIE, ou NS_ARENE_MAX_PLACES (8)
 *                                  si le couperet n'a sorti personne
 *                   2   1 : uint8  le camp VAINQUEUR, ou NS_ARENE_AUCUN_CAMP
 *                                  (0xFF) tant que la manche court. C'est ce
 *                                  champ qui dit que la manche est finie : un
 *                                  drapeau séparé aurait pu le contredire.
 *                   3   4 : uint32 l'horloge de la manche en millisecondes,
 *                                  telle que l'arbitre la compte. Elle voyage
 *                                  parce que les autres places ne font PAS
 *                                  tourner leur propre couperet : sans elle
 *                                  elles n'auraient aucun moyen d'afficher un
 *                                  compte à rebours d'accord avec la lame.
 *                 reçu (8 octets) : [place émettrice, qui doit être 0] + 7.
 *
 *   0x23 EFFET    ce que la règle a FAIT d'une action. LA PLACE 0 SEULE.
 *
 *                 écrit (4 octets) :
 *                   0   1 : uint8  auteur
 *                   1   1 : uint8  cible
 *                   2   1 : uint8  action
 *                   3   1 : uint8  issue (`ns_arene_issue`)
 *                 reçu (5 octets) : [0, la place de l'arbitre] + les 4.
 *
 *                 L'AUTEUR EST ÉCRIT DANS LA CHARGE ICI, alors qu'il est
 *                 interdit de l'écrire dans une ACTION, et ce n'est pas une
 *                 incohérence : les deux octets ne disent pas la même chose. Le
 *                 premier, posé par le relais, dit « c'est l'arbitre qui
 *                 parle » ; le second dit « de qui venait l'action dont voici
 *                 le sort ». Sans lui, l'auteur d'une action refusée ne saurait
 *                 jamais que ses fusibles lui restent.
 *
 *                 POURQUOI CETTE TRAME EXISTE, alors que l'ACTION est déjà
 *                 diffusée à tout le monde : parce qu'aucune place sauf
 *                 l'arbitre ne peut CONCLURE. `room_cp_agir` consomme le
 *                 blindage de la victime, renvoie l'attaque si elle tient un
 *                 leurre, et refuse tout net si la cible ne joue pas — trois
 *                 décisions qui dépendent de l'état de quelqu'un d'autre. Une
 *                 victime qui appliquerait l'ACTION reçue se brouillerait
 *                 l'écran malgré son blindage.
 *
 * CE QUI N'EST PAS DANS CE VOCABULAIRE, et pourquoi. Aucune trame ne transporte
 * le `room_cp_evenement` du journal de l'arbitre, ni aucun autre type de
 * `room_couperet.h`. Le moteur ne connaît pas la salle : `engine/` ne dépend
 * pas de `room/`, et l'inverse seulement. Les octets ci-dessus sont donc des
 * entiers nus dont la salle donne le sens, et ce fichier ne se recompile pas
 * parce qu'une action a changé de numéro.
 * ========================================================================== */

#define NS_ARENE_T_ETAT    0x20u
#define NS_ARENE_T_ACTION  0x21u
#define NS_ARENE_T_VERDICT 0x22u
#define NS_ARENE_T_EFFET   0x23u

/* Les tailles de charge, telles qu'on les ÉCRIT. Publiées parce que le test
 * s'en sert, et parce qu'un chiffre nommé se contredit moins qu'un chiffre
 * recopié. */
#define NS_ARENE_JOIN_OCTETS    34
#define NS_ARENE_ETAT_OCTETS    46
#define NS_ARENE_ACTION_OCTETS   2
#define NS_ARENE_VERDICT_OCTETS  7
#define NS_ARENE_EFFET_OCTETS    4

/* Une entrée de tableau des places fait 25 octets : la place, puis 24 de nom. */
#define NS_ARENE_ENTREE_OCTETS  (1 + NS_ARENE_PSEUDO)

/* « Personne » pour une place, « aucun » pour un camp. Deux sentinelles et non
 * une : une place vaut 0 à 7 et un camp aussi, mais 8 est une place possible du
 * point de vue d'un décodeur alors que 0xFF ne l'est jamais. */
#define NS_ARENE_AUCUNE_PLACE NS_ARENE_MAX_PLACES
#define NS_ARENE_AUCUN_CAMP   0xFFu

/* ==========================================================================
 * L'état de la liaison
 * ========================================================================== */

typedef enum ns_arene_liaison {
    NS_ARENE_OFF = 0,     /* rien n'est ouvert */
    NS_ARENE_CONNEXION,   /* le fil ouvre la socket et annonce le JOIN */
    NS_ARENE_SALON,       /* entré : le salon se remplit */
    NS_ARENE_COURSE,      /* le coup d'envoi est donné, la graine est là */
    NS_ARENE_TERMINEE,    /* le relais a raccroché proprement */
    NS_ARENE_ERREUR       /* réseau : voir `ns_arene_erreur()` */
} ns_arene_liaison;

/* Le sort d'une action, tel que l'arbitre le rapporte. L'ordre suit celui de
 * `room_cp_agir` : le leurre d'abord, le blindage ensuite. */
typedef enum ns_arene_issue {
    NS_ARENE_PASSEE = 0,  /* l'effet s'est appliqué */
    NS_ARENE_ABSORBEE,    /* le blindage de la cible a tenu, et est consommé */
    NS_ARENE_RENVOYEE,    /* le leurre a renvoyé : l'auteur est sa propre cible */
    NS_ARENE_REFUSEE      /* rien n'a eu lieu, rien n'a été débité */
} ns_arene_issue;

/* ==========================================================================
 * Ce qu'on relit
 * ========================================================================== */

typedef struct ns_arene_place {
    bool     presente;     /* le tableau des places la porte */
    char     pseudo[NS_ARENE_PSEUDO];

    /*
     * Faux tant qu'aucun ÉTAT n'est arrivé de cette place. Sans ce champ, une
     * place qui vient d'entrer serait indiscernable d'une place à zéro point :
     * les deux donnent `points == 0`, et l'une ne doit pas s'afficher.
     */
    bool     etat_recu;
    bool     vivante;
    bool     hard;
    uint8_t  camp;
    char     jeu[NS_ARENE_JEU];   /* vide = ne joue pas */
    int32_t  points;
    int32_t  fusibles;
    int32_t  valeur;              /* devant le couperet */
    int64_t  score;               /* de la partie en cours */

    /*
     * L'âge du dernier ÉTAT reçu, en millisecondes, ou UINT32_MAX si aucun.
     *
     * C'est ce qui permet à la salle de FIGER puis d'EFFACER un rival dont le
     * client ne parle plus, au lieu de le laisser au tableau pour l'éternité.
     * Un joueur immobile est un bogue visible ; un joueur périmé est une
     * déconnexion lisible. Comparer à `NS_ARENE_PEREMPTION_MS`.
     */
    uint32_t age_ms;
} ns_arene_place;

typedef enum ns_arene_evt {
    /*
     * Le tableau des places a changé. `a` = le nombre de places présentes.
     * Il arrive à chaque arrivée ET à chaque départ, y compris le nôtre : c'est
     * ce qui permet d'afficher un salon en train de se remplir plutôt qu'un
     * écran d'attente muet, et c'est le seul endroit d'où viennent les pseudos.
     */
    NS_ARENE_EVT_TABLEAU = 0,
    /* Le coup d'envoi. La graine se lit par `ns_arene_graine`. */
    NS_ARENE_EVT_DEBUT,
    /* `a` = la place qui vient de partir. Le relais l'annonce lui-même, avec la
     * place qu'il SAIT être la bonne : un joueur ne peut pas déclarer le départ
     * d'un autre. */
    NS_ARENE_EVT_DEPART,
    /* `a` = la place dont l'état vient d'arriver ; le détail par
     * `ns_arene_places`. */
    NS_ARENE_EVT_ETAT,
    /* `a` = auteur (écrit par le relais), `b` = cible, `valeur` = l'action. */
    NS_ARENE_EVT_ACTION,
    /* `a` = auteur, `b` = cible, `valeur` = l'action, `issue` = son sort. */
    NS_ARENE_EVT_EFFET,
    /*
     * `a` = la place sortie ou NS_ARENE_AUCUNE_PLACE, `b` = le camp vainqueur
     * ou NS_ARENE_AUCUN_CAMP, `valeur` = le numéro du couperet,
     * `horloge_ms` = l'horloge de la manche chez l'arbitre.
     */
    NS_ARENE_EVT_VERDICT
} ns_arene_evt;

typedef struct ns_arene_evenement {
    ns_arene_evt   type;
    uint8_t        a;      /* la place concernée, ou l'AUTEUR */
    uint8_t        b;      /* la CIBLE, ou le camp vainqueur */
    ns_arene_issue issue;  /* EFFET seulement */
    int32_t        valeur; /* selon le type — voir `ns_arene_evt` */
    /*
     * VERDICT seulement. Il sort par le MÊME appel que le reste de
     * l'événement, et c'est ce qui le rend utilisable : le relire par un
     * accesseur séparé laisserait un second verdict se glisser entre les deux
     * appels, et daterait le premier avec l'horloge du second. Même
     * raisonnement que `at_ms` dans `ns_realtime_peers`.
     */
    uint32_t       horloge_ms;
} ns_arene_evenement;

/* ==========================================================================
 * Le cycle de vie
 * ========================================================================== */

typedef struct ns_arene_config {
    /* Le RELAIS, qui n'est pas le serveur HTTP : autre port, autre protocole.
     * Voir `server/cmd/duelrelay`. */
    const char *hote;
    uint16_t    port;

    uint64_t    salon;    /* convenu hors bande, comme l'identifiant de duel */
    uint8_t     place;    /* 0-basée, < places */
    uint8_t     places;   /* attendues, 2 à 8 ; la PREMIÈRE annonce fait foi */
    const char *pseudo;   /* 23 octets utiles ; le relais coupe le reste */

    /* La borne de la connexion, sur le fil de travail. 0 = 2000 ms. La boucle
     * de jeu ne l'attend pas : ce délai ne borne que le fil. */
    uint32_t    delai_ms;
} ns_arene_config;

typedef struct ns_arene ns_arene;

/*
 * Entre dans un salon. NE BLOQUE PAS : la socket s'ouvre sur le fil, et l'état
 * part de `NS_ARENE_CONNEXION`.
 *
 * Rend NULL — et n'ouvre RIEN — si la configuration est invalide, ou si le
 * réseau est inactif (pas d'URL de serveur, ou `--offline`). Le motif est écrit
 * dans `erreur`. Un appelant qui obtient NULL joue hors ligne, ce qui est le
 * défaut de ce dépôt et pas une panne.
 */
ns_arene *ns_arene_ouvrir(const ns_arene_config *cfg, char *erreur, size_t taille);

/* Ferme la liaison et attend le fil. Tolère NULL. */
void ns_arene_fermer(ns_arene *a);

/*
 * CES ACCESSEURS NE SONT PAS `const`, ET C'EST VOULU.
 *
 * Ils lisent un état qu'un FIL réécrit, donc ils prennent un verrou — et un
 * verrou ne se prend pas sur un objet constant. Le dépôt compile avec
 * `-Wcast-qual` précisément pour qu'on n'aille pas jeter le `const` en douce :
 * il vaut mieux que l'interface dise la vérité. `ns_arene_ma_place` reste
 * `const`, elle : la place est figée à l'ouverture et personne ne la réécrit.
 */
ns_arene_liaison ns_arene_etat(ns_arene *a);

/* Le motif de la panne, ou "". Il est écrit UNE SEULE FOIS, au premier échec,
 * et jamais ensuite : le pointeur rendu reste donc valable, et le premier motif
 * est de toute façon le bon — les suivants n'en sont que les conséquences. */
const char      *ns_arene_erreur(ns_arene *a);

/* La graine du salon, tirée par le relais et identique pour tous. Elle ne vaut
 * quelque chose qu'une fois l'état passé à `NS_ARENE_COURSE`. */
uint64_t ns_arene_graine(ns_arene *a);

/*
 * NOTRE place. ZÉRO quand rien n'est ouvert, et c'est délibéré : hors ligne on
 * est seul dans un salon d'une place, et le seul joueur est le premier.
 */
uint8_t ns_arene_ma_place(const ns_arene *a);

/*
 * VRAI si c'est à nous d'arbitrer — c'est-à-dire de faire tourner
 * `room_cp_avancer` et de diffuser le verdict.
 *
 * Trois cas, et les trois rendent vrai pour la même raison de fond : il n'y a
 * personne d'autre pour le faire.
 *   - `a == NULL` : on joue hors ligne, le même chemin de code que place 0 ;
 *   - notre place est 0 : c'est la règle du mode ;
 *   - la liaison est MORTE (`NS_ARENE_TERMINEE`, `NS_ARENE_ERREUR`) : l'arbitre
 *     ne parle plus, et une manche qui attendrait son verdict s'arrêterait pour
 *     toujours. On reprend la lame et on finit la manche avec les rivaux
 *     locaux.
 */
bool ns_arene_arbitre(ns_arene *a);

/* ==========================================================================
 * Déposer
 * ==========================================================================
 * Toutes ces fonctions rendent la main IMMÉDIATEMENT et tolèrent `a == NULL` :
 * hors ligne elles ne font rien, ce qui est exactement ce qu'il faut.
 */

/*
 * Dépose MON état. Le fil l'enverra au prochain battement (4 Hz). Appelable à
 * chaque image sans y penser : seule la dernière valeur compte, et c'est
 * justement ce qu'on veut d'un tableau de scores.
 *
 * `jeu` à NULL ou vide veut dire « je ne joue pas », comme `room_cp_place.jeu`.
 * `valeur` est `room_cp_valeur` : les points plus ce que la partie en cours
 * paierait, c'est-à-dire ce qui défend du couperet.
 */
void ns_arene_publier(ns_arene *a, bool vivante, uint8_t camp,
                      const char *jeu, bool hard, int64_t score,
                      int32_t points, int32_t fusibles, int32_t valeur);

/*
 * Achète une action contre une place. L'AUTEUR n'est pas un paramètre : c'est
 * le relais qui l'écrit, et c'est la seule identité que quiconque garantisse.
 */
void ns_arene_agir(ns_arene *a, uint8_t cible, uint8_t action);

/*
 * Diffuse le verdict du couperet. SANS EFFET si l'on n'est pas la place 0 —
 * refusé ici plutôt qu'ignoré à l'autre bout, pour que le refus se voie du côté
 * qui a tort.
 */
void ns_arene_verdict(ns_arene *a, uint8_t sortie, uint8_t vainqueur,
                      uint8_t numero, uint32_t horloge_ms);

/* Diffuse le sort d'une action. SANS EFFET si l'on n'est pas la place 0. */
void ns_arene_effet(ns_arene *a, uint8_t auteur, uint8_t cible,
                    uint8_t action, ns_arene_issue issue);

/* ==========================================================================
 * Relire
 * ========================================================================== */

/*
 * Recopie la table des places. Elle est INDEXÉE PAR PLACE : `out[3]` est la
 * place 3, présente ou non, et c'est `presente` qui le dit. Un tableau tassé
 * aurait obligé chaque lecteur à retrouver l'indice d'une place, alors que
 * toutes les trames du protocole en désignent une par son numéro. Le retour est
 * le nombre de cases écrites, c'est-à-dire `min(max, NS_ARENE_MAX_PLACES)`.
 *
 * Une COPIE et pas un pointeur : le fil réécrit cette table quand il veut, et
 * rendre l'adresse interne obligerait l'appelant à tenir un verrou pendant tout
 * son rendu. Huit places de quelques dizaines d'octets se recopient pour rien
 * du tout — même raisonnement que `ns_realtime_peers`.
 *
 * NOTRE PROPRE PLACE N'A JAMAIS D'ÉTAT REÇU : le relais ne renvoie pas nos
 * trames à nous-mêmes. `etat_recu` y reste faux et `age_ms` vaut UINT32_MAX,
 * ce qui est correct — on connaît son propre état sans passer par le réseau.
 */
uint32_t ns_arene_places(ns_arene *a, ns_arene_place *out, uint32_t max);

/*
 * Retire le plus ancien événement. Faux si la file est vide. À vider une fois
 * par image : c'est par là que passent le tableau, le coup d'envoi, les
 * départs, les actions et les verdicts.
 *
 * La file est BORNÉE. Quand elle déborde, les plus anciens événements sont
 * PERDUS et comptés par `ns_arene_stats` — pas d'allocation sans borne sur ce
 * qui vient d'une socket, et une salle qui ne dépile pas ne doit pas faire
 * grossir un tampon jusqu'à la panne.
 */
bool ns_arene_prendre(ns_arene *a, ns_arene_evenement *out);

/* Compteurs, pour le journal et pour les tests : un transport « au mieux » doit
 * quand même se compter. `perdus` compte ce que le module a laissé tomber faute
 * de place, DANS LES DEUX SENS — un événement jeté d'une file d'entrée pleine,
 * une trame jetée d'une file de sortie pleine. Les deux disent la même chose :
 * quelqu'un ne lit plus assez vite. */
void ns_arene_stats(ns_arene *a, uint32_t *trames_recues,
                    uint32_t *trames_envoyees, uint32_t *perdus);

/*
 * LE COMPTEUR DE SOCKETS de ce module, depuis le démarrage du programme.
 *
 * Il existe pour une seule raison : « sans URL configurée, aucune socket n'est
 * ouverte » est une PROMESSE du dossier, et une promesse qui ne se mesure pas
 * finit par ne plus être vraie. Il est incrémenté juste avant l'appel à
 * `socket()`, au seul endroit du fichier qui en fasse un, et il n'est jamais
 * remis à zéro — un test qui le lit avant et après sait donc exactement ce qui
 * s'est passé, plutôt que de relire le code.
 *
 * Il compte pour TOUT le module, pas par arène : c'est une propriété du
 * fichier, pas d'une liaison.
 */
uint32_t ns_arene_sockets(void);

/* ==========================================================================
 * LE CODEC, PUR ET EXPOSÉ
 * ==========================================================================
 * Ces fonctions n'ouvrent rien, n'allouent rien et ne touchent à aucun état.
 * Elles sont publiques pour la même raison que `ns_online_resolve_url` : c'est
 * la moitié du protocole qui peut se tromper toute seule, et le seul moyen d'en
 * exercer les cas limites — une trame tronquée, un tableau qui annonce plus de
 * places qu'il n'en porte, une charge de 511 octets — est de les appeler
 * directement, sans relais et sans réseau.
 */

typedef struct ns_arene_entree {
    uint8_t place;
    char    pseudo[NS_ARENE_PSEUDO];
} ns_arene_entree;

/*
 * DÉCOUPE une trame dans un flux. TCP est un FLUX, pas une file de messages :
 * une trame peut arriver en deux morceaux, et deux trames en un seul paquet.
 * L'oublier est le défaut classique d'un lecteur de socket.
 *
 * Rend le nombre d'octets CONSOMMÉS (> 0) et pose la trame dans `type`,
 * `charge` (qui pointe DANS `flux`, sans copie) et `charge_len`.
 * Rend 0 si la trame n'est pas encore entière : il faut lire davantage.
 * Rend -1 si la longueur annoncée dépasse `NS_ARENE_CHARGE_MAX` — la liaison
 * est alors à fermer, SANS avoir rien alloué.
 */
int ns_arene_trame(const uint8_t *flux, size_t len, uint8_t *type,
                   const uint8_t **charge, uint16_t *charge_len);

/* Écrit la charge d'un JOIN. Rend `NS_ARENE_JOIN_OCTETS`, ou 0 si `cap` est
 * trop petit ou si la place et le nombre de places ne tiennent pas ensemble. */
size_t ns_arene_ecrire_join(uint8_t *out, size_t cap, uint64_t salon,
                            uint8_t place, uint8_t places, const char *pseudo);

/*
 * Lit un tableau des places. Rend le nombre d'entrées écrites, ou -1 si la
 * charge est incohérente — `n` plus grand que ce que la charge porte, ou plus
 * grand que huit, ou une place hors bornes. On ne lit alors RIEN : un décodeur
 * qui croit un compte annoncé est un décodeur qui déborde.
 *
 * Les pseudos rendus sont TOUJOURS terminés par un zéro, quoi qu'ait envoyé
 * l'autre bout.
 */
int ns_arene_lire_tableau(const uint8_t *charge, size_t len,
                          ns_arene_entree *out, int max);

/* Écrit la charge d'un ÉTAT. Rend `NS_ARENE_ETAT_OCTETS` ou 0. */
size_t ns_arene_ecrire_etat(uint8_t *out, size_t cap, bool vivante, uint8_t camp,
                            const char *jeu, bool hard, int64_t score,
                            int32_t points, int32_t fusibles, int32_t valeur);

/*
 * Lit un ÉTAT REÇU — donc avec l'octet de place devant, 47 octets. Rend faux si
 * la charge est trop courte ou si la place annoncée est hors bornes. `out` ne
 * reçoit que les champs de l'état ; `out->presente` et `out->age_ms` ne sont
 * pas touchés, ils n'appartiennent pas à la trame.
 */
bool ns_arene_lire_etat(const uint8_t *charge, size_t len,
                        uint8_t *place, ns_arene_place *out);

size_t ns_arene_ecrire_action(uint8_t *out, size_t cap, uint8_t cible,
                              uint8_t action);
bool   ns_arene_lire_action(const uint8_t *charge, size_t len, uint8_t *auteur,
                            uint8_t *cible, uint8_t *action);

size_t ns_arene_ecrire_verdict(uint8_t *out, size_t cap, uint8_t sortie,
                               uint8_t vainqueur, uint8_t numero,
                               uint32_t horloge_ms);
/*
 * Lit un VERDICT REÇU. Rend FAUX si la place émettrice n'est pas 0 : un verdict
 * qui ne vient pas de l'arbitre n'est pas un verdict. C'est le contrôle que
 * l'octet d'identité du relais rend possible, et le seul endroit du dépôt qui
 * puisse le faire.
 */
bool ns_arene_lire_verdict(const uint8_t *charge, size_t len, uint8_t *sortie,
                           uint8_t *vainqueur, uint8_t *numero,
                           uint32_t *horloge_ms);

size_t ns_arene_ecrire_effet(uint8_t *out, size_t cap, uint8_t auteur,
                             uint8_t cible, uint8_t action, ns_arene_issue issue);
/* Rend FAUX si la place émettrice n'est pas 0, pour la même raison. */
bool   ns_arene_lire_effet(const uint8_t *charge, size_t len, uint8_t *auteur,
                           uint8_t *cible, uint8_t *action,
                           ns_arene_issue *issue);

#endif /* NS_ARENE_H */
