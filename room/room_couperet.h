/*
 * room_couperet.h — LE COUPERET : la salle en compétition.
 *
 * LA RÈGLE, EN UNE PHRASE
 * -----------------------
 * Tes points ne comptent QUE quand la partie est finie, et toutes les
 * quarante-cinq secondes le couperet sort le dernier.
 *
 * Tout le reste de ce fichier découle de cette phrase, et il vaut mieux la lire
 * deux fois avant de lire le reste : c'est elle qui crée le choix, et le choix
 * est le mode.
 *
 * POURQUOI CETTE PHRASE SUFFIT À FAIRE UN JEU
 * -------------------------------------------
 * Parce que les dix-neuf bornes ne durent pas le même temps, et l'écart est
 * ÉNORME. Mesure du 2026-08-30 sur ce dépôt (protocole plus bas, seize lignes
 * dans `ROOM_CP_DUREES`), durée médiane d'une partie d'autopilote :
 *
 *     demineur difficile     7,1 s          aplomb normal        180,0 s
 *     aplomb difficile       9,1 s          dedale normal        180,0 s
 *     envol difficile       21,7 s          snake difficile      180,0 s
 *     demineur normal       25,4 s          dedale difficile     157,3 s
 *
 * Vingt-cinq fois. Un joueur qui enchaîne du démineur encaisse deux fois par
 * couperet : il n'est JAMAIS pris avec les mains vides. Un joueur qui s'engage
 * sur aplomb traverse QUATRE couperets sans avoir rien mis en banque — et s'il
 * est dernier quand l'un d'eux tombe, les trois minutes sont perdues avec lui.
 *
 * C'est le choix que le propriétaire a demandé : « devoir choisir entre jouer à
 * un jeu difficile pour espérer gagner ou rush des jeux faciles ». Il n'est pas
 * imposé par une règle qui dirait « les jeux longs rapportent plus » ; il sort
 * de la confrontation entre une horloge et une table de durées mesurées.
 *
 * CE QUE LE BARÈME LIBRE FAIT, ET POURQUOI IL NE PEUT PAS SERVIR TEL QUEL
 * ----------------------------------------------------------------------
 * `room_bareme.h` est calibré pour qu'une partie MÉDIANE de n'importe quel jeu
 * rende dix tickets. C'est juste en jeu libre — le même effort paie le même
 * prix — et c'est exactement ce qu'il ne faut pas ici : rapporté à la SECONDE,
 * ce barème paie le démineur difficile (11 tickets en 7,1 s) vingt-cinq fois ce
 * qu'il paie aplomb normal (11 tickets en 180 s). Branché tel quel, le mode
 * n'aurait qu'une stratégie, et ce serait la moins intéressante des deux.
 *
 * D'où le MULTIPLICATEUR DE BORNE, et il n'y a que lui :
 *
 *     points = tickets_du_bareme  x  (duree_mediane_de_la_borne / T_REF) ^ K
 *
 * `K = 1` rendrait le paiement plat À LA SECONDE (vérifié : l'étendue des
 * seize lignes tombe à x1,9, ce qui n'est que l'arrondi des tickets) — donc
 * aucune raison de s'engager. `K > 1` paie la durée plus que proportionnelle-
 * ment, ce qui est la définition d'une prime de risque. La valeur retenue est
 * plus bas, avec ce qu'elle produit.
 *
 * LA DURÉE MÉDIANE DE LA BORNE, PAS CELLE DE LA PARTIE — et c'est une
 * correction, pas un raccourci. La première version prenait la durée
 * RÉELLEMENT jouée, ce qui semblait plus juste : on paie ce qu'on a risqué.
 * Le tournoi a mesuré ce que ça produit, et c'est l'inverse de l'intention.
 *
 * Le paiement est convexe en durée — c'est la définition même d'un exposant
 * supérieur à 1 — donc par l'inégalité de Jensen la moyenne des paiements d'une
 * borne dépasse le paiement de sa durée moyenne, d'autant plus que la borne
 * DISPERSE. Envol difficile va de 6,1 à 63,9 s dans le vivier mesuré : un
 * facteur dix, qui se transformait en prime. Résultat chiffré, avec la durée
 * réelle : envol difficile rendait 0,354 point par seconde et dedale difficile
 * 0,247 — la borne de vingt secondes payait MIEUX que celle de cent soixante,
 * et les deux stratégies du tournoi convergeaient sur la même ligne. Le
 * coefficient récompensait la VARIANCE, pas la durée. Il n'y avait plus de
 * choix, donc plus de mode.
 *
 * Attaché à la borne, il ne récompense plus que ce qu'il annonce. Il a deux
 * autres mérites, et le second est le plus important :
 *
 *   - à l'intérieur d'une borne, les points redeviennent proportionnels au
 *     score. Mieux jouer paie mieux, linéairement, sans qu'un hasard de durée
 *     s'en mêle ;
 *   - IL EST AFFICHABLE. « x3,5 » se peint sur le fronton de dedale avant
 *     qu'on insère le jeton. Un multiplicateur qui dépendrait de la durée
 *     qu'on aura tenue ne peut pas s'annoncer, donc ne peut pas se choisir —
 *     et tout ce mode repose sur un choix fait AVANT de jouer.
 *
 * Le barème n'est PAS recopié ici : `room_eco_tickets_pour` reste la seule
 * description de ce que vaut une partie, et ce fichier la multiplie. Deux
 * descriptions d'une même chose finissent toujours par se contredire — c'est
 * écrit en tête de `room_bareme.h`, et ça vaut aussi contre moi.
 *
 * LA SÉRIE EST NEUTRALISÉE, et il faut le dire : `room_eco_tickets_pour` prend
 * une série en paramètre et ajoute jusqu'à +70 %. Elle récompense la fidélité,
 * ce qui n'a pas sa place dans une manche où l'on se compare — un joueur qui
 * revient tous les jours entrerait avec sept points d'avance sur un invité.
 * Ce module passe donc toujours `serie = 0`.
 *
 * CE QUE CE MODE N'EST PAS
 * ------------------------
 * `room_bareme.h` promet une économie FICTIVE ET FERMÉE : aucune voie d'achat,
 * aucun coffre payant, et « pas de minuterie qui punit l'absence ». Ce mode
 * ajoute une minuterie, et la promesse tient quand même — parce qu'elle ne
 * punit pas L'ABSENCE, elle arbitre une MANCHE qu'on a choisi de commencer.
 * Sortir du Couperet ne coûte rien, n'entame aucun compteur, et le portefeuille
 * de la salle n'est pas touché : les fusibles de la manche sont à elle, ils
 * naissent au coup d'envoi et meurent au verdict. Un joueur qui ne joue jamais
 * ce mode n'a rien perdu.
 *
 * Il n'y a pas non plus d'échelle payante, de saison, de laissez-passer, ni
 * quoi que ce soit qui s'achète. Le seul avantage qu'on puisse avoir sur un
 * autre joueur est d'avoir mieux joué.
 *
 * LE FICHIER EST PUR
 * ------------------
 * Ni SDL, ni GPU, ni socket, ni horloge — le temps ENTRE par `room_cp_avancer`.
 * Même raison que `room_economie.c` : c'est ce qui rend `tests/test_couperet.c`
 * possible sans écran, et c'est surtout ce qui permet de faire JOUER le mode
 * dix mille fois dans un test pour mesurer si les deux stratégies s'équilibrent
 * — ce qu'aucune relecture ne peut établir.
 *
 * `room/main.c` n'a donc rien à décider : il pousse des événements et lit un
 * classement.
 */
#ifndef NS_ROOM_COUPERET_H
#define NS_ROOM_COUPERET_H

#include "room_economie.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * LES DURÉES MESURÉES — la table qui fait le mode
 * ==========================================================================
 *
 * PROTOCOLE. Mesure du 2026-08-30 sur ce dépôt. Vingt-quatre parties par jeu et
 * par régime, jouées par l'autopilote de chaque jeu au pas fixe de 120 Hz,
 * graines `n x 0x9E3779B97F4A7C15 + 1`, chaque partie menée jusqu'à la mort ou
 * jusqu'à un plafond de 180 s. La colonne retenue est la MÉDIANE, robuste à la
 * dispersion énorme de ces jeux (`room_bareme.h` la chiffre : jusqu'à 1 077 %
 * d'étendue rapportée à la médiane).
 *
 * CE QUE CETTE MESURE NE DIT PAS, et il faut le lire avant de s'y fier :
 *
 *   - TROIS LIGNES SONT PLAFONNÉES PAR LE TEMPS, pas par la mort. L'autopilote
 *     survit aux 180 s dans 19 parties sur 24 pour aplomb normal, 13 sur 24
 *     pour dedale normal, 13 sur 24 pour snake difficile, 8 sur 24 pour dedale
 *     difficile. Leur « médiane » est donc le PLAFOND, pas une durée de vie.
 *     C'est exactement pourquoi le coefficient d'engagement est borné au même
 *     endroit : au-delà de 180 s je n'ai pas de mesure, et un coefficient qui
 *     continuerait de monter récompenserait ce que personne n'a chiffré.
 *   - L'AUTOPILOTE N'EST PAS UN JOUEUR. Il établit qu'une partie est jouable et
 *     qu'elle dure ; il ne dit rien de ce qu'un humain tient. Un humain qui
 *     tient plus longtemps sort par le plafond, ce qui est voulu.
 *   - PIANO NE DISPERSE PAS : min = médiane = max sur les 24 parties, aux deux
 *     régimes (le morceau a une longueur fixe). Sa ligne est exacte et ne dit
 *     rien de la variance humaine, la seule qui existe pour ce jeu-là.
 *   - CE N'EST PAS LA MÊME SUITE DE GRAINES que la mesure de `room_bareme.h`,
 *     et les deux ne coïncident donc pas exactement (démineur 25,4 s ici contre
 *     24,7 s là ; snake 45,4 contre 49,3). L'accord à quelques pour cent sur
 *     six lignes sur huit est un contrôle croisé, pas une reproduction.
 *
 * X(id, duree_mediane_normale_ds, duree_mediane_difficile_ds)
 *
 * En DIXIÈMES DE SECONDE, pour que la table reste entière : un flottant dans
 * une X-macro se compare mal en test et se lit mal en revue.
 */
#define ROOM_CP_DUREES(X)      \
    X(envol,     742,   217)   \
    X(snake,     454,  1800)   \
    X(demineur,  254,    71)   \
    X(aplomb,   1800,    91)   \
    X(asteroid,  619,   625)   \
    X(dedale,   1800,  1573)   \
    X(piano,    1068,   757)   \
    X(shooter,   549,   480)

/*
 * LA DURÉE DE RÉFÉRENCE, en dixièmes de seconde.
 *
 * La MÉDIANE DES SEIZE lignes ci-dessus : 7,1 9,1 21,7 25,4 45,4 48,0 54,9
 * 61,9 | 62,5 74,2 75,7 106,8 157,3 180,0 180,0 180,0 — soit (61,9 + 62,5) / 2.
 *
 * Les seize et non les huit du régime normal, parce que les deux régimes se
 * jouent dans une manche et que le coefficient doit donc valoir 1 au milieu de
 * ce qui est réellement joué. Prendre les huit lignes normales donnerait 68,0 s
 * et décalerait tout le régime difficile vers le bas sans raison.
 *
 * `room_bareme.h` publie 63,6 s sur une autre suite de graines. Les deux
 * mesures tombent à 2,2 % l'une de l'autre ; c'est un contrôle croisé.
 */
#define ROOM_CP_T_REF_DS 622

/*
 * LE PLAFOND DE DURÉE, en dixièmes de seconde — 180 s.
 *
 * Il est posé EXACTEMENT là où la mesure s'arrête, et pas ailleurs. Quatre des
 * seize lignes touchent ce plafond : au-delà, la table ne dit plus rien, et un
 * coefficient qui continuerait de croître paierait une durée que rien n'a
 * chiffrée. C'est le même raisonnement que `ROOM_ECO_PLAFOND_PARTIE`, qui coupe
 * la queue de distribution venue du hasard des graines plutôt que du joueur.
 *
 * Conséquence chiffrée : le coefficient est borné à (1800/622)^K.
 */
#define ROOM_CP_DUREE_MAX_DS 1800

/*
 * La borne la plus lente du tableau vaut donc (1800/622)^K, et la plus rapide
 * (71/622)^K. À K = 1,35 : x4,20 contre x0,04, soit un rapport de 105. Ce n'est
 * PAS le rapport des rendements — le barème paie déjà à peu près le même nombre
 * de tickets pour une partie médiane de n'importe quelle borne — mais celui des
 * PRIX AFFICHÉS, et c'est lui que le joueur lit sur les frontons.
 */

/*
 * L'EXPOSANT D'ENGAGEMENT, en centièmes.
 *
 * IL EST CHOISI, PAS DÉDUIT — et il faut le dire, parce que rien dans la table
 * des durées ne le dicte. Ce qui est MESURÉ, c'est ce qu'il produit, et c'est
 * `tests/test_couperet.c` qui le mesure : il fait jouer des manches entières
 * entre un « pressé » (toujours la borne la plus courte) et un « engagé »
 * (toujours la plus longue), et refuse toute valeur pour laquelle l'une des
 * deux stratégies gagne plus de deux fois sur trois. Un exposant au jugé casse
 * donc la construction, exactement comme un diviseur au jugé la casse déjà
 * dans `tests/test_economie.c`.
 *
 * Les valeurs essayées et l'étendue du paiement À LA SECONDE sur les seize
 * lignes, qui est la grandeur que l'exposant gouverne :
 *
 *     K = 1,00     x1,9      plat : aucune raison de s'engager
 *     K = 1,20     x2,2
 *     K = 1,35     x3,4
 *     K = 1,50     x5,5      une seule bonne partie décide de la manche
 *
 * 1,35 laisse aplomb normal payer 0,257 point par seconde contre 0,083 au
 * démineur difficile — trois fois — pour une exposition quatre couperets plus
 * longue. C'est le rapport que le tournoi de test valide.
 */
#define ROOM_CP_K_CENT 135

/*
 * DEUX RÉGLAGES INJECTABLES, et ce n'est pas un confort de test.
 *
 * L'exposant et la période sont les deux seuls nombres dont dépend l'équilibre
 * du mode, et les deux sont CHOISIS. Une valeur choisie qu'on ne peut pas
 * comparer à ses voisines est une valeur qu'on ne peut pas défendre : personne,
 * relecture comprise, ne peut dire depuis son fauteuil si 1,35 vaut mieux que
 * 1,20 ou que 1,60.
 *
 * Ces deux fonctions existent donc pour que `tests/test_couperet.c` puisse
 * BALAYER les valeurs voisines, faire jouer des manches entières pour chacune,
 * et imprimer la matrice. Ce qui est livré est alors le meilleur point d'une
 * carte, et la carte est dans la sortie du test.
 *
 * Même motif exactement que `room_eco_set_horloge`, et même réserve : ce sont
 * les tests et la recette qui s'en servent, pas le jeu. `0` rétablit la valeur
 * du barème.
 */
void room_cp_set_exposant(int centiemes);
void room_cp_set_periode(int dixiemes);
float room_cp_periode(void);

/*
 * LA PÉRIODE DU COUPERET, en dixièmes de seconde.
 *
 * QUARANTE-CINQ SECONDES, et le chiffre se lit dans la table des durées : c'est
 * la seule valeur qui laisse le pressé encaisser au moins une fois entre deux
 * couperets sur les cinq bornes courtes (7,1 / 9,1 / 21,7 / 25,4 / 45,4 s) tout
 * en obligeant l'engagé à en traverser au moins deux sur les six longues.
 *
 * Ce qu'on obtient si on s'en écarte, en gardant la même table :
 *   - 30 s : snake normal (45,4 s) ne rentre plus dans une fenêtre. Le mode n'a
 *     plus que trois bornes jouables sans risque, et le choix se referme.
 *   - 60 s : l'engagé traverse deux couperets au lieu de quatre sur aplomb.
 *     La prime de risque est payée sans le risque.
 *
 * PÉRIODE CONSTANTE, et c'est un refus délibéré de l'accélération de fin de
 * manche qui est l'usage du genre. Une période qui tomberait à 20 s en fin de
 * partie rendrait les six bornes longues INJOUABLES au moment précis où il ne
 * reste que des joueurs qui ont su les jouer : le mode se terminerait toujours
 * en démineur, quel que soit le chemin pris pour y arriver.
 */
#define ROOM_CP_PERIODE_DS 450

/* ==========================================================================
 * Les places, les camps, les durées de manche
 * ========================================================================== */

/* HUIT places. Ce n'est pas un chiffre rond : la salle a dix-neuf bornes et
 * quatre allées, et au-delà de huit joueurs on ne voit plus qui fait quoi —
 * or tout le mode repose sur le fait de VOIR l'autre jouer. C'est aussi la
 * taille que le relais annonce (`places_attendues`, 2 à 8). */
#define ROOM_CP_MAX_PLACES 8
#define ROOM_CP_PSEUDO     24
#define ROOM_CP_JEU        24

/* Le nombre de fusibles au coup d'envoi. TROIS : de quoi lancer un brouillage et
 * un blindage, ou un seul leurre, avant d'avoir fini la moindre partie. À zéro
 * la première minute serait muette et le mode ne commencerait qu'à la deuxième ;
 * à cinq, on ouvrirait sur une coupure, c'est-à-dire sur une partie annulée
 * avant que qui que ce soit ait joué. */
#define ROOM_CP_FUSIBLES_DEPART 3

/*
 * LE REVENU DU TEMPS : une tranche de trente secondes de jeu vaut un fusible.
 *
 * Trente parce que c'est la durée sous laquelle le mode n'a plus de bornes :
 * cinq des seize lignes du tableau durent moins (7,1 / 9,1 / 20,3 / 25,4 /
 * 28,3 s), et payer plus vite qu'elles ferait du va-et-vient entre deux parties
 * une source de fusibles plutôt qu'un choix de jeu.
 *
 * Ce que ça donne sur une manche pleine de huit places, qui dure 315 s : une
 * dizaine de fusibles de temps plus sept de couperet, soit dix-sept — quatre
 * coupures, ou huit blindages, ou dix-sept brouillages. Assez pour que la
 * dépense soit une décision à chaque fois, pas assez pour saboter en continu.
 *
 * Le temps est compté PAR TRANCHE ENTIÈRE et le reste est gardé : sans ça,
 * quatre parties de vingt-neuf secondes ne rapporteraient rien du tout.
 */
#define ROOM_CP_SECONDES_PAR_FUSIBLE 30

/*
 * LE PLAFOND DE MANCHE, en secondes — quinze minutes.
 *
 * Il ne sert JAMAIS dans une manche normale : huit places à un couperet toutes
 * les 45 s font 315 s, soit 5 min 15. Il existe pour le cas où plus personne ne
 * joue — huit clients figés, ou sept spectres et un vivant qui ne touche à
 * rien — où le couperet n'a plus rien à couper. Une manche doit se terminer.
 */
#define ROOM_CP_MANCHE_MAX_S 900

/* ==========================================================================
 * LES SIX ACTIONS — ce qu'on achète avec ses fusibles
 * ==========================================================================
 *
 * POURQUOI « FUSIBLE » ET PAS « JETON ». Parce que le JETON existe déjà et
 * veut dire autre chose : c'est la pièce qu'on enfonce dans la fente pour
 * jouer, elle a son geste, son bruit et son monnayeur, et l'affichage du haut
 * à gauche en compte le solde en permanence. Deux monnaies portant le même nom
 * sur le même écran, c'est un joueur qui lit « 3 JETONS » à un endroit et
 * « 0 JETONS » à un autre et qui conclut que le jeu est cassé — ce qui a été vu
 * sur la première capture du mode.
 *
 * Le fusible, lui, dit ce que les six actions FONT. Elles sont toutes
 * électriques : on brouille une image, on inverse un câblage, on coupe le
 * courant, on blinde un tableau, on renvoie une surtension. Cette salle a des
 * néons, des tubes et un compteur ; ce qu'on s'échange sous le comptoir pour
 * s'en prendre à la borne du voisin, ce sont des fusibles.
 *
 * D'OÙ ILS VIENNENT :
 *
 *     + 1 par TRANCHE DE `ROOM_CP_SECONDES_PAR_FUSIBLE` SECONDES DE JEU.
 *     + 1 par COUPERET, aux vivants comme aux spectres.
 *
 * LE PREMIER TERME A ÉTÉ « UN PAR PARTIE TERMINÉE », et il a fallu le retirer.
 * L'idée était jolie : le pressé enchaîne les parties, il est donc pauvre en
 * points et riche en sabotage, et une équipe qui l'associe à un engagé a un
 * marqueur et un garde du corps. Le tournoi a mesuré deux choses qui la
 * démentent.
 *
 *   - ELLE PAIE LE SUICIDE. La borne la plus courte du vivier descend à 0,1 s :
 *     l'autopilote meurt à la première image. Insérer, mourir exprès,
 *     recommencer — environ deux secondes par cycle, plus de cent cinquante
 *     fusibles dans une manche de cinq minutes, de quoi couper toutes les bornes
 *     de la salle en boucle. Une première rustine refusait le fusible aux parties
 *     à zéro point ; elle fermait le cas extrême et laissait tout le reste.
 *   - LE DÉSÉQUILIBRE N'EST PAS UNE TENSION, C'EST UN VAINQUEUR. Mesuré : à
 *     revenu par partie, le pressé finit la manche avec quatre fois les fusibles
 *     de l'engagé, et gagne 98 % des manches en le coupant avant chaque
 *     encaissement. L'engagé ne terminait plus 0,1 partie par manche.
 *
 * Le fusible se gagne donc AU TEMPS PASSÉ À JOUER, ce qui ferme les deux d'un
 * coup : mourir en une image ne rapporte rien parce qu'aucun temps n'a passé,
 * et les deux stratégies ont le même budget parce qu'elles jouent le même
 * temps.
 *
 * L'ALLIANCE QUE LE PROPRIÉTAIRE A DEMANDÉE EXISTE TOUJOURS, et elle est
 * meilleure ainsi : elle ne vient plus d'une asymétrie de revenu subie, mais
 * d'un CHOIX de dépense. Tout le monde a les mêmes fusibles ; celui qui les
 * dépense en blindages sur son porteur ne s'achète rien pour lui-même, et c'est
 * ça, jouer le soutien. Le couperet classant les CAMPS, ce rôle est survivable
 * — ce qui est exactement ce qui manquait pour qu'il soit jouable.
 *
 * LES SPECTRES REÇOIVENT LE FUSIBLE DU COUPERET mais plus celui du temps : ils ne
 * jouent plus. Leur budget décroît donc en pouvoir d'achat relatif au fil de la
 * manche, sans jamais tomber à zéro.
 *
 * LES SPECTRES. Un joueur sorti par le couperet NE PART PAS. Il ne peut plus
 * jouer — sa borne est éteinte — mais il garde ses fusibles, il en reçoit un à
 * chaque couperet, et il peut toujours agir. Sortir vous change donc de métier
 * plutôt que de vous mettre à la porte, et une équipe décimée devient une
 * escouade de saboteurs. C'est aussi la réponse au défaut classique du genre :
 * être éliminé à la première minute d'une manche de cinq, c'est quatre minutes
 * à regarder. Ici, ce sont quatre minutes à peser.
 *
 * X(cle, cout_en_fusibles, duree_en_dixiemes, offensive, titre, effet)
 */
#define ROOM_CP_ACTIONS(X)                                                     \
    X(BROUILLAGE, 1,  40, 1, "BROUILLAGE", "la dalle de la cible se brouille")  \
    X(INVERSION,  2,  50, 1, "INVERSION",  "son manche part a l'envers")        \
    X(COUPURE,    4,   0, 1, "COUPURE",    "sa borne s'eteint : partie annulee")\
    X(BLINDAGE,   2,   0, 0, "BLINDAGE",   "encaisse la prochaine attaque")     \
    X(RELAIS,     1,   0, 0, "RELAIS",     "donne un fusible")                    \
    X(LEURRE,     3,   0, 0, "LEURRE",     "renvoie la prochaine attaque")

/*
 * L'INVERSION N'EST JAMAIS ACHETÉE PAR UN RIVAL, et c'est mesuré : sur cent
 * manches, les rivaux prennent le brouillage, la plaque et le leurre, jamais
 * elle. La raison est arithmétique — elle coûte le double du brouillage, et un
 * rival subit les deux de la même façon, parce que `room_rivaux` modélise l'un
 * comme l'autre en faisant jouer sa victime au taux du débutant.
 *
 * SON PRIX N'EST DONC PAS RÉGLÉ PAR CETTE MESURE, et il ne peut pas l'être :
 * un manche inversé et une image brouillée ne coûtent pas la même chose à une
 * MAIN. Personne n'a chiffré ce que coûte le second, et un autopilote ne peut
 * pas le dire — il n'a pas de main. Deux fusibles est un jugement sur des
 * humains ; il attend la première partie jouée à plusieurs pour être confirmé
 * ou démenti, et il vaut mieux l'écrire que de le faire passer pour une mesure.
 *
 * LE BLINDAGE ET LE LEURRE SE TIENNENT, ILS NE S'ÉCOULENT PAS — et c'est le
 * réglage qui a le plus changé de nature au cours de la mise au point.
 *
 * Les deux ont d'abord duré trente secondes. Le tournoi a mesuré ce que ça
 * produit, et c'est une paralysie complète : à deux fusibles pour trente
 * secondes, entretenir un blindage coûte EXACTEMENT le revenu du temps, un
 * fusible par trente secondes. Les huit joueurs se couvraient donc en permanence,
 * aucun n'atteignait jamais les quatre fusibles d'une coupure, et la manche avec
 * sabotage rendait EXACTEMENT le même résultat que la manche sans — 187 contre
 * 13, au joueur près. Six actions dont pas une seule n'était jouée.
 *
 * Sans durée, la plaque est achetée une fois et ATTEND. Renouveler ne coûte
 * qu'après avoir encaissé, donc la défense est une dépense ponctuelle et non un
 * abonnement, et le budget se libère pour attaquer. Le rapport de prix devient
 * lisible : il faut quatre fusibles pour détruire ce que deux protègent, donc
 * frapper coûte le double de se garder — ce qui est la seule façon d'obtenir
 * que les deux existent.
 *
 * On n'en tient qu'un de chaque : acheter un second blindage quand on en a
 * déjà un est REFUSÉ plutôt qu'empilé, sans quoi le geste évident (marteler la
 * touche) jetterait des fusibles sans rien dire.
 */

#define ROOM_CP_ACTION_ENUM(cle, cout, duree, off, titre, effet) ROOM_CP_##cle,
typedef enum room_cp_action {
    ROOM_CP_ACTIONS(ROOM_CP_ACTION_ENUM)
    ROOM_CP_ACTION_COUNT
} room_cp_action;
#undef ROOM_CP_ACTION_ENUM

int32_t     room_cp_action_cout(room_cp_action a);
float       room_cp_action_duree(room_cp_action a);
bool        room_cp_action_offensive(room_cp_action a);
const char *room_cp_action_titre(room_cp_action a);
const char *room_cp_action_effet(room_cp_action a);

/* ==========================================================================
 * L'état
 * ========================================================================== */

typedef enum room_cp_phase {
    ROOM_CP_SALON = 0,   /* le salon se remplit ; rien ne court */
    ROOM_CP_COURSE,      /* la manche est lancée */
    ROOM_CP_FINI         /* un seul camp debout, ou le plafond atteint */
} room_cp_phase;

typedef struct room_cp_place {
    bool     occupee;
    bool     vivante;              /* faux = spectre : ne joue plus, agit encore */
    char     pseudo[ROOM_CP_PSEUDO];
    uint8_t  camp;                 /* 0..7 ; en individuel, camp = place */

    int32_t  points;
    int32_t  fusibles;
    int32_t  parties;              /* parties TERMINÉES, pas commencées */
    float    joue;                 /* secondes de jeu cumulées, reste de tranche
                                    * comprise : c'est le revenu en fusibles */
    int32_t  annulees;             /* parties perdues sur coupure */

    /* La partie en cours. `jeu[0] == 0` veut dire qu'on ne joue pas. */
    char     jeu[ROOM_CP_JEU];
    bool     hard;
    float    depuis;               /* secondes depuis l'insertion du jeton */
    int64_t  score_vu;             /* le dernier score annoncé par la borne */
    int32_t  provisoire;           /* ce que la partie en cours paierait si elle
                                    * finissait maintenant. JAMAIS crédité. */

    /*
     * LES EFFETS SUBIS, en secondes restantes. Zéro = rien.
     *
     * Ils sont portés par la VICTIME et non par l'attaquant, parce que c'est la
     * victime qui les subit et que deux attaquants sur la même cible ne doivent
     * pas produire deux effets qui s'ignorent. Une seconde attaque du même
     * genre PROLONGE au lieu de s'ajouter : deux brouillages simultanés ne
     * brouillent pas deux fois, ils brouillent plus longtemps.
     */
    float    brouillage;
    float    inversion;
    bool     blindage;             /* une plaque tenue : encaisse la prochaine */
    bool     leurre;               /* un miroir tenu : renvoie la prochaine */

    int32_t  sortie_a;             /* seconde du couperet qui l'a sortie, -1 sinon */
} room_cp_place;

/*
 * LE JOURNAL — ce qui vient de se passer, à lire une fois.
 *
 * Il existe pour une raison précise : la salle doit pouvoir faire du BRUIT et
 * de la LUMIÈRE sur un événement, et un état ne dit pas ce qui vient de
 * changer. Comparer l'état d'avant à celui d'après pour retrouver « la place 3
 * a été coupée » serait redécouvrir ce que ce module savait déjà.
 *
 * C'est aussi ce que le réseau envoie : une action et un verdict sont des
 * événements, pas un état, et les diffuser tels quels évite de resynchroniser
 * huit portefeuilles à quatre hertz.
 */
typedef enum room_cp_evt {
    ROOM_CP_EVT_ARRIVEE = 0,   /* a = place */
    ROOM_CP_EVT_DEPART,        /* a = place */
    ROOM_CP_EVT_DEBUT,         /* la manche est lancée */
    ROOM_CP_EVT_PARTIE,        /* a = place, valeur = points gagnés */
    ROOM_CP_EVT_ACTION,        /* a = auteur, b = cible, valeur = action */
    ROOM_CP_EVT_ABSORBE,       /* a = auteur, b = cible : le blindage a tenu */
    ROOM_CP_EVT_RENVOYE,       /* a = auteur, b = cible : le leurre a renvoyé */
    ROOM_CP_EVT_COUPERET,      /* a = place sortie, valeur = numéro du couperet */
    ROOM_CP_EVT_FIN            /* a = camp vainqueur */
} room_cp_evt;

typedef struct room_cp_evenement {
    room_cp_evt type;
    uint8_t     a, b;
    int32_t     valeur;
} room_cp_evenement;

#define ROOM_CP_JOURNAL 32

typedef struct room_couperet {
    room_cp_phase phase;
    uint8_t       places;          /* places attendues, 2 à 8 */
    bool          equipes;         /* faux = chacun pour soi (camp = place) */
    uint64_t      graine;

    room_cp_place place[ROOM_CP_MAX_PLACES];

    float         horloge;         /* secondes depuis le coup d'envoi */
    float         prochain;        /* secondes avant le prochain couperet */
    int32_t       couperets;       /* combien sont tombés */
    uint8_t       vainqueur;       /* camp vainqueur, valide en phase FINI */

    /*
     * SUIS-JE L'ARBITRE ? Vrai par défaut, et vrai pour toujours hors ligne.
     *
     * La règle du couperet vit ici, en C, UNE SEULE FOIS. En ligne, une seule
     * place la fait tourner et diffuse son verdict ; les autres l'appliquent
     * par `room_cp_verdict`. Porter la règle dans le relais Go l'aurait écrite
     * deux fois, en deux langages, et deux descriptions d'une même chose
     * finissent toujours par se contredire — c'est le raisonnement de
     * `room_bareme.h`, et il vaut ici.
     *
     * Ce que ça coûte, et il faut l'écrire : L'ARBITRE PEUT MENTIR. C'est
     * exactement la même franchise que celle déjà inscrite dans le relais —
     * « deux clients complices peuvent se mentir pendant un duel » — et la même
     * limite : l'autorité sur les scores ENREGISTRÉS ne bouge pas d'un pouce,
     * elle reste le journal scellé par HMAC et recalculé par le serveur.
     *
     * Un suiveur avance tout le reste normalement — l'horloge, les durées de
     * partie, les effets, le revenu du temps. Il ne fait que ne pas TOMBER.
     */
    bool          arbitre;

    room_cp_evenement journal[ROOM_CP_JOURNAL];
    uint8_t       jrn_tete, jrn_queue;
} room_couperet;

/* ==========================================================================
 * Le barème du mode
 * ========================================================================== */

/* La durée médiane mesurée d'une partie, en secondes. 0 si le jeu est inconnu —
 * auquel cas il ne rapporte pas de multiplicateur plutôt que n'importe lequel. */
float room_cp_duree_mediane(const char *jeu, bool hard);

/* La courbe nue : (secondes / T_REF)^K, bornée à [0, ROOM_CP_DUREE_MAX_DS]. */
float room_cp_engagement(float secondes);

/*
 * LE MULTIPLICATEUR D'UNE BORNE — le nombre qu'on peint sur son fronton.
 *
 * C'est `room_cp_engagement` de sa durée médiane, et c'est tout. Il vaut 0 pour
 * une borne inconnue, qui ne rapporte alors rien plutôt que n'importe quoi.
 */
float room_cp_multiplicateur(const char *jeu, bool hard);

/*
 * Ce qu'une partie rapporte en compétition, sans rien créditer.
 *
 * `score` est pris en 64 bits SIGNÉ pour la même raison que
 * `room_eco_tickets_pour` : la valeur peut venir d'un pair, et aucun pair n'est
 * digne de confiance. Un score négatif rend zéro point.
 */
int32_t room_cp_points_pour(const char *jeu, bool hard, int64_t score);

/*
 * CE QUE VAUT UNE PARTIE EN COURS DEVANT LE COUPERET.
 *
 * C'est `room_cp_points_pour` du score courant, et rien de plus. Elle n'est
 * jamais créditée : le contrat est
 *
 *     ta partie en cours te DÉFEND du couperet ; elle ne compte pas au
 *     classement tant que tu ne l'as pas finie.
 *
 * IL A FALLU TROIS ESSAIS, et les deux premiers restent écrits parce qu'ils
 * expliquent pourquoi celui-ci est si court.
 *
 *   ESSAI 1 — ne compter que les points ENCAISSÉS, et ne se départager sur la
 *   partie en cours qu'à égalité stricte. Le tournoi : l'engagé gagne 0 manche
 *   sur 200. Sur 157 s de borne longue il traverse trois couperets à zéro
 *   pendant que le pressé encaisse toutes les vingt secondes, et dès qu'un
 *   adversaire a UN point l'égalité disparaît, donc le départage aussi.
 *   L'engagement n'était pas risqué, il était mortel.
 *
 *   ESSAI 2 — compter ce que la partie paierait si elle s'arrêtait maintenant,
 *   avec le coefficient calculé sur la durée écoulée. Encore 0 sur 200 : le
 *   coefficient étant super-linéaire, à 29 % du chemin une partie ne valait pas
 *   29 % de son prix mais 2,4 %. La défense arrivait trop tard pour servir. Il
 *   a fallu extrapoler et proratiser, ce qui marchait mais demandait vingt
 *   lignes et deux hypothèses.
 *
 *   ESSAI 3 — celui-ci. Le jour où le multiplicateur est passé de la partie à
 *   la BORNE, les vingt lignes se sont effondrées en une : le prix ne dépendant
 *   plus de la durée, les points sont proportionnels au score, donc « ce que ça
 *   paierait maintenant » est DÉJÀ la fraction du chemin parcourue. Extrapoler
 *   puis proratiser revenait exactement à ne rien faire — la démonstration
 *   tient en une ligne d'algèbre.
 *
 * ELLE DÉPEND DU SCORE, et c'est ce qui interdit le bouclier gratuit : un
 * joueur qui insère un jeton sur une borne longue et ne touche plus à rien vaut
 * zéro devant la lame. S'engager protège ; s'asseoir devant une borne, non.
 */

/* ==========================================================================
 * Le salon
 * ========================================================================== */

/* Ouvre un salon vide. `places` est borné à [2, 8]. */
void room_cp_ouvrir(room_couperet *c, uint8_t places, bool equipes);

/* Assoit un joueur. Faux si la place est hors bornes, déjà prise, ou si la
 * manche a commencé. `camp` est ignoré hors mode équipes. */
bool room_cp_asseoir(room_couperet *c, uint8_t place, const char *pseudo, uint8_t camp);

/* Quelqu'un raccroche. Pendant le salon la place se libère ; pendant la manche
 * le joueur devient spectre et ses points restent acquis à son camp — partir ne
 * doit pas être un moyen de retirer ses points à son équipe. */
void room_cp_lever(room_couperet *c, uint8_t place);

/* Vrai quand toutes les places attendues sont prises. */
bool room_cp_salon_plein(const room_couperet *c);

/* Le coup d'envoi. Sans effet si le salon n'est pas plein. */
void room_cp_lancer(room_couperet *c, uint64_t graine);

/* ==========================================================================
 * La manche
 * ========================================================================== */

/*
 * Avance le temps. C'EST ICI QUE LE COUPERET TOMBE, et nulle part ailleurs.
 *
 * `dt` est le pas de simulation, pas le temps d'image : deux machines qui
 * avancent du même total dans un nombre d'appels différent doivent obtenir le
 * même nombre de couperets, sans quoi l'arbitre et les autres se contrediraient
 * sur QUI est sorti.
 */
void room_cp_avancer(room_couperet *c, float dt);

/*
 * Dit si cette machine arbitre. Vrai à l'ouverture, et le mode solo n'y touche
 * jamais.
 *
 * REDEVENIR ARBITRE EN COURS DE MANCHE EST NORMAL et doit marcher : le relais
 * laisse sa socket au dernier joueur, donc si l'arbitre raccroche, quelqu'un
 * d'autre doit reprendre la lame — sans quoi la manche se fige, vivante et
 * sans fin. La reprise ne demande aucun état : le nombre de lames tombées est
 * déjà dans `couperets`, et `room_cp_avancer` en déduit celles qui manquent.
 */
void room_cp_set_arbitre(room_couperet *c, bool oui);

/*
 * APPLIQUE UN VERDICT REÇU. Rend vrai s'il a été appliqué.
 *
 * `numero` est celui de la lame, et il n'est pas décoratif : quand l'arbitre
 * change — parce que le précédent a raccroché — deux machines peuvent diffuser
 * le MÊME numéro pendant le temps que met le tableau des places à circuler. Un
 * verdict dont le numéro est déjà tombé est donc jeté, en silence et sans
 * erreur : ce n'est pas une anomalie, c'est le fonctionnement normal d'un
 * arbitrage qui se transmet.
 *
 * Un verdict qui saute des numéros, lui, est appliqué : une trame perdue ne
 * doit pas figer la manche. Les lames sautées sont comptées sans sortir
 * personne, ce qui est la seule chose honnête à faire — on ne sait pas qui
 * elles auraient pris.
 */
bool room_cp_verdict(room_couperet *c, uint8_t place, int32_t numero);

/* Une partie commence sur une borne. Sans effet si la place est un spectre. */
void room_cp_partie_debut(room_couperet *c, uint8_t place, const char *jeu, bool hard);

/*
 * Le score COURANT d'une partie en cours. C'est lui qui alimente la défense
 * décrite au-dessus de la déclaration voisine — la partie en cours protège du
 * couperet sans jamais compter au classement.
 *
 * À appeler quand le score bouge, pas à chaque image : la valeur ne dépend plus
 * que du score depuis que le multiplicateur est attaché à la borne.
 */
void room_cp_avance(room_couperet *c, uint8_t place, int64_t score_courant);

/*
 * La partie se termine. Crédite les points et les rend.
 *
 * Elle ne crédite AUCUN jeton : ceux-là se gagnent au temps passé, dans
 * `room_cp_avancer`, et le raisonnement est écrit au-dessus de
 * `ROOM_CP_SECONDES_PAR_FUSIBLE`.
 *
 * Rend 0 sans rien créditer si la place ne jouait pas — ce qui arrive quand une
 * coupure a annulé la partie entre-temps, et c'est le comportement voulu : la
 * borne s'est éteinte, il n'y a rien à encaisser.
 */
int32_t room_cp_partie_fin(room_couperet *c, uint8_t place, int64_t score);

/*
 * Une action. Rend vrai si elle a eu lieu, et ne débite RIEN sinon.
 *
 * Refusée si : l'auteur n'a pas de quoi payer, la cible n'existe pas, une
 * action offensive vise son propre camp, ou une action offensive vise
 * QUELQU'UN QUI NE JOUE PAS — spectre, ou simplement entre deux parties.
 *
 * Cette dernière condition a l'air d'un détail de confort et c'est en réalité
 * ce qui rend le blindage utile. Sans elle, quatre attaquants qui visent la
 * même cible au même instant passent tous : le premier éteint la borne, les
 * trois autres l'éteignent une seconde fois — en payant, et en comptant une
 * annulation chacun. Le blindage n'absorbe qu'un coup, donc la concentration du
 * feu le battait toujours, et le tournoi le mesurait : l'engagé subissait une
 * coupure par manche malgré un budget de huit blindages.
 *
 * Avec elle, une borne déjà éteinte n'est plus une cible : le second attaquant
 * garde ses fusibles, et la défense redevient une affaire d'économie plutôt
 * qu'une affaire de simultanéité.
 *
 * L'ORDRE DE RÉSOLUTION d'une attaque, et il compte :
 *   1. LEURRE — la cible renvoie, l'auteur devient sa propre cible. Le leurre
 *      est consommé. Un leurre sur l'auteur ne renvoie pas une deuxième fois :
 *      sinon deux leurres face à face boucleraient.
 *   2. BLINDAGE — absorbé, consommé, rien ne passe.
 *   3. l'effet s'applique.
 * Le leurre d'abord parce qu'il coûte plus cher (3 contre 2) : la règle la plus
 * chère doit être celle qui gagne, sans quoi personne ne l'achète.
 */
bool room_cp_agir(room_couperet *c, uint8_t de, uint8_t vers, room_cp_action quoi);

/*
 * L'ISSUE D'UNE ACTION — ce que l'arbitre a le devoir de dire aux autres.
 *
 * Une action ne se rejoue pas chez chacun : `room_cp_agir` CONSOMME le blindage
 * de la victime et le leurre du visé, et deux machines qui la rejoueraient
 * chacune de leur côté finiraient par ne plus avoir le même nombre de plaques.
 * L'arbitre résout, et il diffuse le RÉSULTAT ; les autres l'appliquent tel
 * quel. C'est le même partage que pour la lame, et pour la même raison.
 *
 * L'ordre suit celui de la résolution : le leurre d'abord, le blindage ensuite.
 * Il est aussi celui de `ns_arene_issue`, et `room/main.c` le vérifie à la
 * COMPILATION — deux énumérations écrites dans deux fichiers qui doivent
 * coïncider ne peuvent pas se surveiller toutes seules.
 */
typedef enum room_cp_issue {
    ROOM_CP_PASSEE = 0,   /* l'effet s'est appliqué à la cible */
    ROOM_CP_ABSORBEE,     /* le blindage a tenu, et il est consommé */
    ROOM_CP_RENVOYEE,     /* le leurre a renvoyé : l'auteur est sa propre cible */
    ROOM_CP_REFUSEE       /* rien n'a eu lieu, rien n'a été débité */
} room_cp_issue;

/* La même chose que `room_cp_agir`, en disant ce qui s'est passé. C'est ce que
 * l'arbitre appelle ; `room_cp_agir` n'en est que l'enveloppe qui jette
 * l'issue, pour tout ce qui n'arbitre rien — le mode solo et les rivaux. */
bool room_cp_agir_issue(room_couperet *c, uint8_t de, uint8_t vers,
                        room_cp_action quoi, room_cp_issue *issue);

/*
 * APPLIQUE UNE ISSUE REÇUE, sans rien re-résoudre.
 *
 * Le coût est débité à l'auteur — c'est bien lui qui a payé — mais ni le
 * leurre ni le blindage ne sont consultés : l'arbitre l'a déjà fait, et les
 * consulter une seconde fois consommerait une plaque qui n'existe plus.
 *
 * Une issue REFUSEE n'applique rien et ne débite rien : c'est le cas où
 * l'arbitre a vu quelque chose que le suiveur ne voyait pas — une cible qui
 * venait de mourir, une borne qui venait de s'éteindre.
 */
bool room_cp_appliquer(room_couperet *c, uint8_t de, uint8_t vers,
                       room_cp_action quoi, room_cp_issue issue);

/* ==========================================================================
 * Lecture
 * ========================================================================== */

/* Les points d'un camp : la somme de ses places, spectres compris. C'est le
 * CLASSEMENT, celui qui désigne le vainqueur. */
int32_t room_cp_points_camp(const room_couperet *c, uint8_t camp);

/*
 * Ce que vaut une place DEVANT LE COUPERET : ses points encaissés plus ce que
 * sa partie en cours paierait si elle finissait maintenant.
 *
 * Deux grandeurs et non une, parce qu'elles répondent à deux questions
 * différentes : « qui gagne » se juge sur ce qui est en banque, « qui tombe »
 * sur ce qu'on est en train de faire. Les confondre a produit le défaut décrit
 * au-dessus de `room_cp_avance`.
 */
int32_t room_cp_valeur(const room_couperet *c, uint8_t place);
int32_t room_cp_valeur_camp(const room_couperet *c, uint8_t camp);

/* Combien de places vivantes ce camp a encore. */
int32_t room_cp_vivants_camp(const room_couperet *c, uint8_t camp);

/*
 * Le classement, du meilleur au pire, écrit dans `sortie` (au plus 8 places).
 * Rend le nombre de places écrites. Les spectres sont classés APRÈS les
 * vivants : ils ne courent plus.
 */
int room_cp_classement(const room_couperet *c, uint8_t sortie[ROOM_CP_MAX_PLACES]);

/*
 * LE CLASSEMENT DE FIN DE MANCHE, qui n'est PAS le précédent.
 *
 * Dans un mode à élimination, l'ordre d'arrivée est l'ORDRE INVERSE DES
 * SORTIES : le dernier debout est premier, celui sorti à la dernière lame est
 * deuxième, et ainsi de suite. Les points ne départagent qu'à égalité de lame —
 * ce qui n'arrive qu'entre des joueurs partis d'eux-mêmes, puisque le couperet
 * n'en sort qu'un à la fois.
 *
 * Les deux fonctions existent parce que les deux questions existent. Pendant la
 * manche, « qui mène » se lit sur les points : c'est ce que le tableau du bar
 * montre, et c'est ce sur quoi on décide de s'engager ou pas. À la fin, « qui a
 * gagné » se lit sur qui est resté. Les confondre donnerait un verdict où un
 * joueur sorti à la première lame avec une grosse partie en banque passerait
 * devant le survivant — c'est-à-dire un mode à élimination où l'élimination ne
 * décide de rien.
 */
int room_cp_classement_final(const room_couperet *c, uint8_t sortie[ROOM_CP_MAX_PLACES]);

/*
 * QUI TOMBERAIT SI LE COUPERET TOMBAIT MAINTENANT. `ROOM_CP_MAX_PLACES` si
 * personne — un seul camp debout, ou aucune place vivante.
 *
 * C'est la fonction la plus regardée du mode : c'est elle que le bandeau
 * affiche, et c'est ce qui transforme une minuterie en tension. Un compte à
 * rebours qui ne dit pas QUI est visé n'oblige personne à changer d'avis.
 */
uint8_t room_cp_menace(const room_couperet *c);

/*
 * ADOPTE L'ÉTAT PUBLIÉ D'UNE AUTRE PLACE.
 *
 * QUI FAIT AUTORITÉ SUR QUOI, et il faut que ce soit net parce que c'est la
 * seule chose qui empêche huit machines de raconter huit manches différentes :
 *
 *   - CHAQUE CLIENT fait autorité sur SES points, SES fusibles et la borne
 *     qu'il joue. Personne d'autre ne peut les calculer : ils dépendent de
 *     scores de partie que lui seul voit.
 *   - L'ARBITRE fait autorité sur les ÉLIMINATIONS et sur le SORT DES ACTIONS.
 *     Ce sont les seules décisions qui doivent être prises une fois pour tout
 *     le monde.
 *
 * Cette fonction sert la première moitié, `room_cp_verdict` et
 * `room_cp_appliquer` servent la seconde. Elle refuse d'écrire sur SA PROPRE
 * place — un client qui adopterait l'idée qu'un autre se fait de son solde
 * ouvrirait la porte à ce qu'on le lui vide.
 *
 * Elle n'écrit PAS `vivante` : la vie et la mort viennent de l'arbitre par le
 * verdict, et pas d'un état qu'un joueur publie sur lui-même. Sans quoi il
 * suffirait de se déclarer vivant pour le rester.
 */
bool room_cp_adopter(room_couperet *c, uint8_t place, uint8_t soi,
                     int32_t points, int32_t fusibles, int32_t provisoire,
                     const char *jeu, bool hard);

/* Retire le plus ancien événement du journal. Faux si le journal est vide. */
bool room_cp_prendre(room_couperet *c, room_cp_evenement *out);

/* Vrai si l'état est cohérent : aucun compteur négatif, aucune place vivante
 * hors bornes, aucun camp sans place. Pour le test et pour la recette. */
bool room_cp_valide(const room_couperet *c);

/* ==========================================================================
 * LE CARNET — ce qui survit à la manche, et c'est TOUT ce qui lui survit
 * ==========================================================================
 *
 * POURQUOI IL EXISTE. Les fusibles naissent au coup d'envoi et meurent au
 * verdict ; le portefeuille de la salle n'est pas touché ; il n'y a ni saison
 * ni laissez-passer. Une manche ne laisse donc RIEN — et une manche qui ne
 * laisse rien ne donne aucune raison d'en jouer une seconde, alors que c'est
 * précisément la seconde qui fait le mode.
 *
 * Ce carnet est la plus petite chose qui répare ça sans rien abîmer : un
 * COMPTE, pas une récompense. Il ne donne aucun avantage, ne débloque rien, ne
 * s'échange contre rien. Il dit ce qu'on a fait. C'est exactement le crochet
 * que `room_bareme.h` revendique — « la RÉPÉTITION D'UNE BONNE PARTIE » — et
 * c'est le seul qui ne demande rien au joueur en échange.
 *
 * IL EST SÉPARÉ DU PORTEFEUILLE, et son fichier aussi. Les mêler ferait du
 * Couperet une voie d'enrichissement, donc une raison de le jouer pour autre
 * chose que lui-même — c'est ce que l'en-tête de ce fichier refuse depuis sa
 * première ligne.
 *
 * LE CHEMIN VIENT DE L'APPELANT. C'est ce qui garde ce module sans dépendance :
 * il ne sait pas où vit le répertoire utilisateur, et il n'a pas à l'apprendre
 * pour compter jusqu'à trois. Le test peut donc écrire où il veut sans toucher
 * au carnet de qui l'exécute.
 */
typedef struct room_cp_carnet {
    int32_t manches;        /* manches menées jusqu'au verdict */
    int32_t victoires;
    int32_t meilleur_rang;  /* le plus petit atteint ; 0 = jamais joué */
    int32_t serie;          /* victoires consécutives, en cours */
    int32_t serie_record;
} room_cp_carnet;

/* Absent ou illisible : on repart d'un carnet neuf SANS erreur — quelqu'un qui
 * ouvre le mode pour la première fois n'a rien fait de mal. Même règle et même
 * format ligne à ligne que `room_eco_charger` : une ligne abîmée se saute et le
 * reste survit, ce qui est le cas de la coupure de courant. */
void room_cp_carnet_charger(room_cp_carnet *k, const char *chemin);
bool room_cp_carnet_sauver(const room_cp_carnet *k, const char *chemin);

/*
 * Note le résultat d'une manche. `rang` est 1-basé, `places` le nombre de
 * places qui y étaient.
 *
 * Une manche à UNE place n'est pas notée : on ne gagne pas contre personne, et
 * une victoire qui ne coûte rien dévalue toutes les autres.
 */
void room_cp_carnet_noter(room_cp_carnet *k, int rang, int places);

#endif /* NS_ROOM_COUPERET_H */
