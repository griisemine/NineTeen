/*
 * room_rivaux.h — LES RIVAUX : les places que personne n'occupe.
 *
 * POURQUOI CE FICHIER EXISTE
 * --------------------------
 * Le Couperet demande huit places. Ce dépôt n'a AUCUNE version publiée et
 * aucun serveur de rendez-vous en service : pendant longtemps, le seul joueur
 * en ligne sera celui qui a compilé le jeu. Un mode compétitif qui exige sept
 * inconnus est un mode que personne n'ouvrira jamais — il serait livré mort.
 *
 * Les places libres sont donc tenues par des rivaux, et la seule chose qui
 * compte dans tout ce fichier est la façon dont ils les tiennent.
 *
 * UN RIVAL JOUE POUR DE BON
 * -------------------------
 * Il n'y a ici AUCUN générateur de score, aucune courbe de progression, aucun
 * compteur qui monte. Un rival alloue un état de jeu, appelle
 * `api->reset(etat, graine, hard)`, puis à chaque pas fixe :
 *
 *     api->autopilot(etat)   (sauf aux pas qu'il saute, voir plus bas)
 *     api->tick(etat, 1/120)
 *     api->events(etat, &ev) — le contrat de `games.h` dit que les drapeaux se
 *                              CONSOMMENT ; ne pas les lire les laisserait levés
 *     api->score(etat)  ->  room_cp_avance
 *     api->dead(etat)   ->  room_cp_partie_fin
 *
 * C'est exactement la boucle de `tests/test_couperet.c:remplir_vivier`, et
 * exactement celle que `room/main.c` fait tourner pour l'humain. Un rival joue
 * la même partie, avec les mêmes règles, sur la même borne, et il perd pour les
 * mêmes raisons.
 *
 * La raison n'est pas la pureté : c'est qu'un rival qui tricherait serait
 * INVISIBLE. Personne, relecture comprise, ne peut distinguer un score simulé
 * d'un score joué en regardant un tableau — mais un joueur qui gagne toujours
 * contre le nombre finit par le sentir, et il n'aura aucun moyen de le
 * démontrer. Le seul remède est de ne pas se donner la possibilité de mentir.
 *
 * Conséquence à assumer : un rival est aussi bon que l'autopilote du jeu, ni
 * plus ni moins. Sur les bornes où l'autopilote est faible, les rivaux sont
 * faibles. C'est le prix de l'honnêteté, et il est payé au bon endroit — régler
 * un autopilote règle les rivaux le jour même.
 *
 * ==========================================================================
 * 1. LE NIVEAU, ET CE QUE LA MESURE EN DIT VRAIMENT
 * ==========================================================================
 *
 * Un rival faible et un rival fort ne peuvent pas être le même autopilote. Le
 * seul levier que l'interface de `games.h` offre sans mentir est de NE PAS
 * APPELER `autopilot` À TOUS LES PAS : le monde avance quand même — `tick` est
 * appelé, lui, à chaque pas — mais le joueur n'a pas réagi. C'est un joueur
 * plus lent, pas un joueur handicapé par une règle qui n'existe pas.
 *
 * PROTOCOLE. Mesure du 2026-08-30 sur ce dépôt, machine Apple Silicon, build
 * `macos-universal` en -O2. Quarante-huit parties par case, pas fixe de 120 Hz,
 * graines `n x 0x9E3779B97F4A7C15 + 1` (la même suite que `ROOM_CP_DUREES`),
 * plafond de 180 s. Le tirage des pas sautés est un splitmix64 semé par la
 * graine de la partie. Colonnes : la part des pas où `autopilot` n'est pas
 * appelé.
 *
 * DURÉE MÉDIANE, en secondes
 *
 *     jeu       régime      0 %    20 %    40 %    60 %    85 %    95 %
 *     envol     normal     74,2    67,4    49,0    16,0     5,3     2,2
 *     envol     hard       22,4    21,7    14,3     8,8     5,3     2,2
 *     snake     normal     43,3    47,0    42,4    48,7    44,0    32,4
 *     snake     hard      179,0   162,5   180,0   166,3   166,5    99,9
 *     demineur  normal     25,0    25,4    26,0    27,3    33,1    50,9
 *     demineur  hard        8,3     8,4     8,7     8,8    10,8    17,9
 *     aplomb    normal    180,0   180,0   180,0   180,0   180,0   180,0
 *     aplomb    hard        9,4     9,7    10,0    10,7    13,5    23,0
 *     asteroid  normal     56,0    44,8    65,5    50,9    30,7     6,2
 *     asteroid  hard       61,2    60,5    48,6    47,2    13,5     4,5
 *     dedale    normal    180,0   180,0   180,0   180,0   172,0   110,6
 *     dedale    hard      157,8   168,0   145,3   152,9   134,7    95,6
 *     piano     normal    106,8   106,8   106,8   106,8   106,8    85,3
 *     piano     hard       75,7    75,7    75,7    75,7    75,7    20,0
 *     shooter   normal     53,4    54,6    54,6    53,6    53,7    53,2
 *     shooter   hard       27,5    28,5    40,7    29,6    39,3    26,5
 *
 * SCORE MÉDIAN
 *
 *     jeu       régime      0 %    20 %    40 %    60 %    85 %    95 %
 *     envol     normal       46      41      28       6       0       0
 *     envol     hard         13      12       7       3       0       0
 *     snake     normal     1494    1474    1456    1595    1552     783
 *     snake     hard      86443   81445   92200   85568   56536   18218
 *     demineur  normal     2318    2318    2318    2318    2318    2318
 *     demineur  hard        470     470     470     470     470     470
 *     aplomb    normal   143100  139850  131850  123400   91450   45850
 *     aplomb    hard       1550    1550    1550    1550    1550    1550
 *     asteroid  normal      240     235     255     265     185      50
 *     asteroid  hard        240     260     245     260     130      10
 *     dedale    normal     4740    4690    4495    4775    4645    2185
 *     dedale    hard       4485    4465    4280    4275    3505    1985
 *     piano     normal     1050    1050    1050    1050    1050     425
 *     piano     hard       1050    1050    1050    1050    1050      75
 *     shooter   normal     3292    3915    3307    3322    3247    2932
 *     shooter   hard       2152    2175    2295    2227    2242    1980
 *
 * CE QUE CES DEUX TABLES NE DISENT PAS, et c'est pour ça qu'il en faut une
 * troisième : ni la durée ni le score ne mesurent la FORCE dans ce mode. Un
 * joueur du Couperet est fort s'il met beaucoup de points en banque par seconde
 * de manche, et rien d'autre. Le démineur en est la preuve : son score ne bouge
 * pas d'un point entre 0 % et 95 % (2 318 aux six colonnes), et pourtant le
 * rival y devient deux fois plus faible — parce qu'il met deux fois plus
 * longtemps (25,0 s -> 50,9 s) à obtenir le même chose.
 *
 * RENDEMENT, en points du Couperet par seconde de jeu — somme des points des
 * quarante-huit parties divisée par la somme de leurs durées (et non points
 * médians sur durée médiane : le paiement est convexe en durée, et l'en-tête du
 * Couperet chiffre l'écart que fait cette confusion).
 *
 *     jeu       régime      0 %    20 %    40 %    60 %    85 %    95 %
 *     envol     normal    0,191   0,189   0,172   0,132   0,013   0,000
 *     envol     hard      0,174   0,176   0,159   0,116   0,021   0,000
 *     snake     normal    0,185   0,154   0,166   0,172   0,159   0,078
 *     snake     hard      0,259   0,234   0,237   0,226   0,176   0,181
 *     demineur  normal    0,105   0,104   0,101   0,097   0,080   0,051
 *     demineur  hard      0,055   0,054   0,053   0,050   0,041   0,026
 *     aplomb    normal    0,348   0,343   0,332   0,315   0,237   0,115
 *     aplomb    hard      0,163   0,154   0,156   0,146   0,115   0,065
 *     asteroid  normal    0,166   0,152   0,144   0,180   0,174   0,071
 *     asteroid  hard      0,201   0,198   0,211   0,267   0,201   0,090
 *     dedale    normal    0,219   0,217   0,210   0,222   0,225   0,174
 *     dedale    hard      0,237   0,236   0,238   0,233   0,227   0,165
 *     piano     normal    0,197   0,197   0,197   0,197   0,194   0,089
 *     piano     hard      0,211   0,211   0,211   0,211   0,205   0,029
 *     shooter   normal    0,217   0,242   0,243   0,223   0,221   0,185
 *     shooter   hard      0,236   0,243   0,241   0,236   0,243   0,232
 *     ------------------------------------------------------------------
 *     MOYENNE DES 16     0,198   0,194   0,192   0,189   0,158   0,097
 *
 * LE BRUIT DE CETTE MOYENNE, mesuré et non supposé. Trois jeux de graines
 * indépendants (décalages 0, 48 et 96 dans la même suite), quatre taux :
 *
 *      0 %  :  0,1976   0,1942   0,2090      étendue 7,4 % de la moyenne
 *     60 %  :  0,1890   0,1815   0,1936      étendue 6,4 %
 *     85 %  :  0,1582   0,1585   0,1637      étendue 3,4 %
 *     95 %  :  0,0970   0,1007   0,1052      étendue 8,1 %
 *
 * D'OÙ TROIS NIVEAUX, ET PAS QUATRE. La grille 0 / 20 / 40 / 60 % perd 4,5 %
 * de rendement du premier au dernier point, quand deux mesures du MÊME point
 * s'écartent déjà de 7 %. Nommer un niveau là-dedans, ce serait vendre au
 * joueur une différence que la mesure ne voit pas. Il faut monter à 85 % pour
 * sortir du bruit (-20 %) et à 95 % pour que ce soit franc (-51 %).
 *
 * SUR QUOI LE LEVIER MORD, ET SUR QUOI IL NE MORD PAS — parce que la moyenne
 * ci-dessus cache deux comportements opposés :
 *
 *   - IL MORD SUR LE SCORE là où le jeu demande une réaction CONTINUE à un
 *     danger qui bouge : envol (0,191 -> 0,000 : le rival ne passe plus un seul
 *     tuyau), aplomb normal (0,348 -> 0,115), asteroid, snake normal.
 *   - IL NE MORD PAS DU TOUT SUR LE SCORE là où la décision est DISCRÈTE et sa
 *     fenêtre large. Démineur : 2 318 aux six colonnes, aux deux régimes — le
 *     plateau se résout de la même façon, seulement moins vite. Aplomb hard :
 *     1 550 partout. Piano : 1 050 jusqu'à 85 % inclus, aux deux régimes, parce
 *     que la fenêtre de frappe d'une note fait plusieurs dizaines de pas et
 *     qu'en sauter 85 % en laisse encore assez pour la toucher. C'est le
 *     soupçon que le propriétaire avait demandé de vérifier : il est fondé, et
 *     il ne devient faux qu'à 95 %, où le piano s'effondre d'un coup
 *     (0,211 -> 0,029) parce que le tirage de Bernoulli produit alors des
 *     silences plus longs que la fenêtre.
 *   - IL NE MORD PAS DU TOUT, POINT, SUR SHOOTER HARD : 0,236 / 0,243 / 0,241 /
 *     0,236 / 0,243 / 0,232. Six colonnes, aucune tendance, et l'étendue est
 *     celle du bruit. Un rival ne peut pas être réglé sur cette borne, et ce
 *     fichier ne prétend pas le contraire.
 *
 * Sur les bornes du deuxième groupe, le niveau agit quand même — mais par
 * L'HORLOGE et non par le score : le démineur met 25,0 s puis 50,9 s pour ses
 * 2 318 points, ce qui divise son rendement par deux. C'est la seule raison
 * pour laquelle la moyenne descend encore de moitié à 95 %.
 *
 * LE TIRAGE EST DÉTERMINISTE. Chaque rival porte un splitmix64 semé par sa
 * graine ; le pas est sauté quand le tirage tombe sous le seuil du niveau. Deux
 * machines qui rejouent la même manche voient donc le même rival rater les
 * mêmes tuyaux. Rien dans ce fichier ne lit l'horloge système.
 *
 * X(cle, saut_pour_mille, titre)
 */
#ifndef NS_ROOM_RIVAUX_H
#define NS_ROOM_RIVAUX_H

#include "room_couperet.h"

#include <stdbool.h>
#include <stdint.h>

/* `games.h` traîne SDL derrière lui ; ce fichier n'a besoin que du nom du type.
 * Qui veut déréférencer l'API du jeu inclut `games.h` de son côté — c'est déjà
 * le cas de `room/main.c` et du test. */
struct ns_game_api;

#define ROOM_RV_NIVEAUX(X)                 \
    X(CHEVRONNE,   0, "chevronne")         \
    X(DISTRAIT,  850, "distrait")          \
    X(DEBUTANT,  950, "debutant")

#define ROOM_RV_NIVEAU_ENUM(cle, saut, titre) ROOM_RV_##cle,
typedef enum room_rv_niveau {
    ROOM_RV_NIVEAUX(ROOM_RV_NIVEAU_ENUM)
    ROOM_RV_NIVEAU_COUNT
} room_rv_niveau;
#undef ROOM_RV_NIVEAU_ENUM

/*
 * ==========================================================================
 * 2. LES CONDUITES — quelle borne un rival choisit
 * ==========================================================================
 *
 * UN RIVAL CHOISIT SUR CE QUI EST PEINT SUR LE FRONTON, et c'est la contrainte
 * qui rend ces trois conduites honnêtes. Il ne consulte aucun vivier, aucune
 * table de scores, rien qu'un humain debout dans l'allée ne pourrait lire : la
 * durée médiane annoncée de la borne (`room_cp_duree_mediane`) et son
 * multiplicateur (`room_cp_multiplicateur`). Ces deux fonctions sont publiques
 * et ce fichier ne recopie donc AUCUNE table.
 *
 * De ces deux nombres sort le seul classement qu'un joueur puisse faire avant
 * d'insérer son jeton : le RENDEMENT AFFICHÉ, multiplicateur divisé par durée.
 * Le barème garantit qu'une partie médiane de n'importe quelle borne vaut à peu
 * près le même nombre de tickets (`room_bareme.h`), donc ce rapport est
 * proportionnel aux points par seconde qu'on peut espérer, à une constante près
 * qui est la même pour toutes les bornes — et qui n'a donc pas besoin d'être
 * connue. Aucun nombre du barème n'est répété ici.
 *
 *   PRESSÉ    — seulement les bornes dont la durée médiane est INFÉRIEURE À LA
 *               PÉRIODE DU COUPERET, et parmi elles le meilleur rendement
 *               affiché. Il encaisse au moins une fois entre deux lames et
 *               n'est jamais pris les mains vides. C'est la moitié « rush des
 *               jeux faciles » du choix que le mode met en jeu.
 *
 *   ENGAGÉ    — le meilleur rendement affiché, toutes bornes confondues. Comme
 *               le multiplicateur croît plus vite que la durée (exposant 1,35),
 *               ce maximum est TOUJOURS la borne la plus longue du tableau :
 *               l'engagé traverse quatre couperets sans rien avoir en banque et
 *               paie sa prime en exposition. C'est l'autre moitié.
 *
 *   HORLOGER  — la plus longue borne qui TIENNE ENCORE avant la prochaine lame,
 *               et la plus courte du tableau si aucune ne tient. Il ne commence
 *               donc jamais ce qu'il ne peut pas finir, et il n'use pas une
 *               fenêtre de quarante-cinq secondes à jouer sept secondes de
 *               démineur. C'est la conduite qu'un humain trouve au bout de
 *               quelques manches, et elle mérite d'être dans la salle pour ça :
 *               un salon où personne ne regarde le compte à rebours
 *               apprendrait au joueur que le compte à rebours ne sert à rien.
 *
 * Une quatrième conduite a été écartée : « jouer ce que joue le meneur ». Elle
 * se défend en théorie et elle est invisible en pratique — deux rivaux sur la
 * même borne se ressemblent, et le mode ne montre plus qu'une borne à la fois.
 *
 * X(cle, titre)
 */
#define ROOM_RV_CONDUITES(X)   \
    X(PRESSE,   "presse")      \
    X(ENGAGE,   "engage")      \
    X(HORLOGER, "horloger")

#define ROOM_RV_CONDUITE_ENUM(cle, titre) ROOM_RV_##cle,
typedef enum room_rv_conduite {
    ROOM_RV_CONDUITES(ROOM_RV_CONDUITE_ENUM)
    ROOM_RV_CONDUITE_COUNT
} room_rv_conduite;
#undef ROOM_RV_CONDUITE_ENUM

/*
 * ==========================================================================
 * 3. LES FUSIBLES — et le plaisir de jeu, qui est une contrainte MESURÉE
 * ==========================================================================
 *
 * La politique de départ était celle de `tests/test_couperet.c:une_manche` : se
 * blinder quand on joue, couper le meilleur adversaire dès qu'on a les quatre
 * fusibles. Elle est volontairement simpliste — elle sert à MESURER l'équilibre,
 * pas à jouer — et branchée telle quelle sur sept rivaux elle donne, mesuré sur
 * 200 manches de huit places dont une humaine qui ne dépense jamais rien :
 *
 *     4,60 coupures subies par l'humain et par manche
 *
 * C'est-à-dire une partie annulée toutes les 68 secondes. Personne ne joue à
 * ça. Trois changements, et le troisième est celui qui compte :
 *
 *   1. ON NE FRAPPE QUE DANS LA FENÊTRE D'AVANT-LAME (les 3 dernières secondes
 *      avant le couperet). Ce n'est pas un adoucissement : c'est le seul moment
 *      où une coupure change quelque chose. Éteindre une borne trente secondes
 *      avant la lame laisse à la victime le temps d'en rallumer une ; l'éteindre
 *      trois secondes avant lui retire sa défense au moment du verdict. La
 *      politique devient donc à la fois plus douce ET plus dangereuse.
 *   2. ON NE FRAPPE QUE PLUS HAUT QUE SOI — un adversaire dont la valeur devant
 *      le couperet dépasse la sienne. Frapper celui qu'on devance déjà ne
 *      rapproche personne de la victoire, et c'est ce qui faisait pleuvoir les
 *      coupures sur un humain en difficulté.
 *   3. LA CIBLE EST LE MENEUR, pas un joueur au hasard. Sept rivaux qui visent
 *      tous le même dos concentrent leurs coupures sur une place et une seule —
 *      et `room_cp_agir` refuse d'éteindre une borne déjà éteinte, donc les six
 *      autres gardent leurs fusibles au lieu de les jeter sur une place morte.
 *
 * Mesure après les trois changements, même protocole, 200 manches :
 *
 *     1,49 coupure subie par l'humain et par manche (contre 4,60)
 *     et 2,79 quand cet humain est le MEILLEUR joueur de la salle
 *
 * Un joueur ordinaire perd donc une partie et demie par manche de cinq
 * minutes ; celui qui domine en perd trois, ce qui est exactement le prix de la
 * tête et ce que le mode doit faire payer. Le seuil que le propriétaire a fixé
 * — deux ou trois — est tenu dans les deux cas.
 *
 * LE RESTE DE LA POLITIQUE, en une phrase chacun :
 *
 *   - LE MENEUR ACHÈTE UN LEURRE (3), les autres un BLINDAGE (2). Le meneur est
 *     celui que tout le monde vise ; lui rendre l'attaque coûte quatre fusibles à
 *     l'attaquant pour rien, là où le blindage n'en coûte que deux. C'est le
 *     seul endroit du mode où le leurre est le meilleur achat, et sans lui
 *     l'action ne serait jamais jouée par personne.
 *   - LE SURPLUS PART EN BROUILLAGE (1). Un rival qui a de quoi couper ET
 *     davantage brouille le meneur hors fenêtre plutôt que de dormir sur son
 *     magot : des fusibles jamais dépensés sont des fusibles qui n'ont rien fait.
 *   - EN ÉQUIPES, ON BLINDE LE PORTEUR. Si un coéquipier vivant joue, n'est pas
 *     couvert et vaut plus que soi, on paie sa plaque avant la sienne. C'est le
 *     rôle de soutien que l'en-tête du Couperet décrit comme « un CHOIX de
 *     dépense » ; il fallait que quelqu'un dans la salle le joue.
 *
 * LE BROUILLAGE ET L'INVERSION FONCTIONNENT CONTRE UN RIVAL, et ce n'était pas
 * acquis : `games.h` n'a pas de manche à inverser ni de dalle à brouiller pour
 * un joueur automatique. Un rival subissant l'un des deux monte donc son taux
 * de saut au niveau DÉBUTANT tant que l'effet dure (-51 % de rendement, mesuré
 * plus haut). C'est une convention, et elle est déclarée comme telle — mais
 * l'absence de convention en serait une autre, bien pire : les deux actions les
 * moins chères du mode n'auraient AUCUN effet sur sept joueurs sur huit, le
 * joueur l'apprendrait en trois manches, et il conclurait à juste titre que les
 * rivaux sont truqués.
 *
 * ==========================================================================
 * 4. LES NOMS — et la seule chose qu'on ne cache pas
 * ==========================================================================
 *
 * Les pseudos sont tirés de la graine dans une table de radicaux et de
 * suffixes, sans répétition dans un salon, et ils ne portent aucune marque :
 * pas de « BOT », pas de numéro d'ordre, rien qui trahisse la place au premier
 * coup d'oeil. C'est ce qui fait qu'un salon se sent habité plutôt que rempli.
 *
 * MAIS L'INTERFACE, ELLE, SAIT. `room_rv_tenue` dit de chaque place si elle est
 * tenue par un rival, et rien n'empêche `room/main.c` de l'afficher — au
 * contraire. Mettre en scène est une chose ; répondre « un humain » à un joueur
 * qui demande qui il affronte en serait une autre, et ce n'est pas le même
 * métier. La règle est donc : on ne le crie pas, on ne le nie jamais.
 *
 * ==========================================================================
 * 5. LE COÛT — mesuré, sur cette machine
 * ==========================================================================
 *
 * Sept rivaux à 120 Hz font 840 pas de jeu par seconde en plus de celui du
 * joueur. Mesuré par `tests/test_rivaux.c`, machine Apple Silicon, build
 * `macos-universal` en -O2, sur une manche entière :
 *
 *     3,3 us par image pour les sept rivaux réunis
 *
 * soit 0,04 % du budget d'une image à 120 Hz (8 333 us). Le pas de jeu coûte
 * 0,45 us en moyenne sur les huit jeux, et sept pas font 3,2 us : la boucle des
 * rivaux n'ajoute rien de mesurable à ce que coûtent les jeux eux-mêmes.
 *
 * IL N'Y A DONC AUCUNE RAISON DE LES FAIRE TOURNER PLUS LENTEMENT, et c'est la
 * conclusion qui compte : la solution honnête à un coût trop élevé aurait été
 * un pas plus grand pour les rivaux, annoncé ici. Elle n'a pas eu à être prise.
 *
 * LA MÉMOIRE, elle, se voit : chaque rival garde un bloc à la taille du PLUS
 * GROS état de jeu du dépôt (snake, 31 000 octets), pour ne pas réallouer entre
 * deux parties. Huit places font 248 Kio, alloués à l'ouverture du salon et
 * rendus par `room_rv_fermer`.
 *
 * ==========================================================================
 * 6. CE QUE CE FICHIER NE FAIT PAS
 * ==========================================================================
 *
 * Il n'avance PAS le couperet. `room_cp_avancer` reste l'affaire de l'appelant,
 * qui possède l'horloge ; les rivaux ne font que jouer et dépenser. Deux
 * modules qui feraient tous les deux couler le temps finiraient par ne pas être
 * d'accord sur l'heure.
 *
 * Il n'ouvre ni fenêtre, ni socket, ni fichier, et ne lit aucune horloge
 * système : le temps entre par `room_rv_avancer`, comme il entre dans le
 * Couperet par `room_cp_avancer`. C'est ce qui permet à `tests/test_rivaux.c`
 * de faire jouer deux cents manches complètes en quelques secondes.
 */

/* Le seuil de saut d'un niveau, en pour mille. */
int         room_rv_saut_pour_mille(room_rv_niveau n);
const char *room_rv_niveau_titre(room_rv_niveau n);
const char *room_rv_conduite_titre(room_rv_conduite d);

/* La fenêtre d'avant-lame, en secondes : le seul moment où un rival frappe. */
#define ROOM_RV_FENETRE_S 3.0f

/* ==========================================================================
 * L'état
 * ========================================================================== */

typedef struct room_rival {
    bool     tenue;             /* faux = la place n'est pas à nous */
    uint8_t  niveau;            /* room_rv_niveau */
    uint8_t  conduite;          /* room_rv_conduite */
    uint64_t graine;            /* ce qui rend ce rival reproductible */
    uint64_t alea;              /* l'état courant du tirage */

    const struct ns_game_api *api;  /* la borne en cours, NULL entre deux */
    void    *etat;                  /* le bloc d'état du jeu */
    bool     hard;
    int64_t  score_vu;          /* le dernier score poussé au Couperet */
    float    pause;             /* secondes avant d'insérer le jeton suivant */

    int32_t  parties;           /* parties terminées par CE rival */
    int32_t  coupures;          /* coupures qu'il a payées */
} room_rival;

typedef struct room_rivaux {
    room_rival place[ROOM_CP_MAX_PLACES];
    uint64_t   graine;
    float      horloge;         /* secondes reçues depuis l'ouverture */
    int64_t    pas;             /* pas fixes déjà joués */
} room_rivaux;

/* ==========================================================================
 * Le salon
 * ========================================================================== */

/*
 * Ouvre un banc de rivaux vide. La graine gouverne noms, niveaux et conduites.
 *
 * ELLE NE LIBÈRE RIEN : sur un banc déjà ouvert, appeler `room_rv_fermer`
 * d'abord. La raison est écrite dans le corps — une structure de pile neuve est
 * pleine de pointeurs qui ressemblent à des allocations.
 */
void room_rv_ouvrir(room_rivaux *r, uint64_t graine);

/* Rend la mémoire des états de jeu. Sans effet sur un banc déjà fermé. */
void room_rv_fermer(room_rivaux *r);

/*
 * Assoit un rival sur une place libre du salon. Faux si la place est prise, si
 * la manche a commencé, ou si l'allocation échoue — et dans ce dernier cas la
 * place reste libre plutôt que d'être tenue par un rival qui ne jouerait pas.
 */
bool room_rv_asseoir(room_rivaux *r, room_couperet *c, uint8_t place, uint8_t camp);

/*
 * Assoit un rival sur CHAQUE place libre du salon et rend combien il en a assis.
 *
 * En mode équipes, le camp est celui des deux qui a le moins de places : la
 * salle a besoin de deux camps pour qu'une manche ait un sens, et équilibrer
 * est le seul choix qui ne décide pas de l'issue à la place du joueur.
 */
int room_rv_remplir(room_rivaux *r, room_couperet *c);

/*
 * Impose le niveau et la conduite d'une place.
 *
 * Même motif que `room_cp_set_exposant` : le tirage par la graine est ce qu'on
 * livre, mais on ne peut pas MESURER qu'un rival fort bat un rival faible sans
 * pouvoir en poser un de chaque. Sans effet sur une place qui n'est pas tenue.
 */
void room_rv_regler(room_rivaux *r, uint8_t place,
                    room_rv_niveau n, room_rv_conduite d);

/* ==========================================================================
 * La manche
 * ========================================================================== */

/*
 * Fait jouer les rivaux pendant `dt` secondes.
 *
 * Le pas de jeu est FIXE (120 Hz, `NS_DEFAULT_TICK_HZ`) parce que les huit jeux
 * sont réglés pour lui et rejouables à ce pas-là seulement. Le nombre de pas se
 * DÉDUIT de l'horloge accumulée au lieu de se décompter, exactement comme le
 * couperet déduit ses lames : deux machines qui avancent du même total en un
 * nombre différent d'appels doivent voir le même rival.
 *
 * N'avance PAS le couperet : l'appelant garde `room_cp_avancer`.
 */
void room_rv_avancer(room_rivaux *r, room_couperet *c, float dt);

/* ==========================================================================
 * Lecture
 * ========================================================================== */

/* Cette place est-elle tenue par un rival ? La seule question à laquelle ce
 * module ne ment pas par omission — voir la section 4. */
bool room_rv_tenue(const room_rivaux *r, uint8_t place);

room_rv_niveau   room_rv_niveau_de(const room_rivaux *r, uint8_t place);
room_rv_conduite room_rv_conduite_de(const room_rivaux *r, uint8_t place);

/*
 * La borne qu'un rival joue en ce moment, et son état de jeu. NULL entre deux
 * parties.
 *
 * Ils sont publiés pour que l'écran de la borne montre CE QUE LE RIVAL JOUE :
 * `api->draw(sprite, etat, art, w, h)` suffit. Une salle où sept écrans sur
 * huit affichent une mire pendant qu'on annonce sept joueurs serait la première
 * chose que le joueur remarquerait.
 */
const struct ns_game_api *room_rv_api(const room_rivaux *r, uint8_t place);
const void               *room_rv_etat(const room_rivaux *r, uint8_t place);

#endif /* NS_ROOM_RIVAUX_H */
