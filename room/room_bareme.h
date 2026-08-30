/*
 * room_bareme.h — LA SOURCE UNIQUE de l'économie de la salle.
 *
 * Pourquoi ce fichier existe séparément de `room_economie.h`
 * ---------------------------------------------------------
 * Parce que DEUX programmes ont besoin des mêmes chiffres, et qu'ils ne peuvent
 * pas partager du code : le jeu (`room_economie.c`, qui parle SDL) et
 * `tools/posterart.c`, un outil d'hôte qui dessine les affiches des murs à la
 * construction et n'a ni SDL, ni moteur, ni salle.
 *
 * Le propriétaire a demandé que « les affiches aient du sens dans le jeu ».
 * Une affiche qui recopierait les taux serait une DEUXIÈME description de
 * l'économie, et deux descriptions d'une même chose finissent toujours par se
 * contredire — en silence, puisque le seul endroit où on lirait l'affiche est
 * un mur, à cinq mètres. C'est exactement le défaut que le motif « plan » a
 * déjà résolu en LISANT `salle.room.json` au lieu de le dessiner de mémoire.
 *
 * Ce fichier applique la même règle aux quatre affiches qui parlent d'argent :
 * `reglement`, `jetons`, `records` et `tournoi` n'écrivent plus un seul chiffre
 * en dur. Elles lisent la table ci-dessous, à la construction. Changer un taux
 * ici REDESSINE les affiches au build suivant ; il n'y a pas de chemin par
 * lequel elles pourraient dire autre chose que le jeu.
 *
 * D'où l'absence totale de dépendance : ni SDL, ni `ns_core.h`, ni allocation.
 * Des `#define` et une liste en X-macro, c'est-à-dire du texte que le
 * préprocesseur recopie dans les deux programmes. Ajouter un `#include` de
 * moteur ici casserait `posterart` et rouvrirait la porte aux deux
 * descriptions.
 *
 * CE QUE CETTE ÉCONOMIE N'EST PAS
 * -------------------------------
 * Elle est FICTIVE et FERMÉE. Il n'existe aucune voie d'achat : pas de monnaie
 * réelle, pas de boutique, pas de coffre aléatoire payant, pas d'abonnement.
 * Un jeton ne s'obtient qu'en jouant ou en le demandant au monnayeur, et le
 * monnayeur ne demande rien en retour. Les affiches de 2020 disaient « 5 POUR
 * 1 EURO » : c'était le décor d'une vraie salle, et c'est devenu un mensonge le
 * jour où le monnayeur a été branché. Il n'y a pas d'euro dans ce jeu.
 */
#ifndef NS_ROOM_BAREME_H
#define NS_ROOM_BAREME_H

/* ==========================================================================
 * LA BOUCLE :  JETON  ->  PARTIE  ->  TICKETS  ->  LOT
 * ========================================================================== */

/* Une partie coûte UN jeton, inséré par le geste qui existe déjà
 * (`NS_VM_TOKEN`, `--pose=insert`). Un seul, et c'est l'article 1 du
 * règlement affiché au mur depuis 2020 : « un jeton par partie, pas deux ». */
#define ROOM_ECO_COUT_PARTIE 1

/*
 * LE PLANCHER D'ACCUEIL — la garantie qu'on n'est JAMAIS bloqué.
 *
 * Le monnayeur ramène le joueur à ce nombre de jetons dès qu'il en a moins, à
 * chaque visite, sans condition et sans attente. Pas de minuterie : un crédit
 * qui se recharge à l'heure punit l'absence, et une salle d'arcade n'a pas à
 * décider quand on a le droit de jouer.
 *
 * Un joueur sans jeton et sans moyen d'en avoir a fini de jouer. Ce n'est pas
 * une difficulté, c'est un défaut de conception — et c'est le défaut que ce
 * plancher rend impossible.
 *
 * CINQ, et le chiffre est mesuré. La durée médiane d'une partie d'autopilote
 * sur les huit jeux en régime normal vaut 63,6 s (mesure du 2026-08-30,
 * 24 parties par jeu : 64,8 / 49,3 / 24,7 / 156,7 / 62,4 / 150,9 / 106,8 /
 * 52,4 s). Cinq jetons valent donc environ cinq minutes de jeu entre deux
 * passages au monnayeur, qui est à 3,4 m de la sortie du sas : l'aller-retour
 * reste un geste de salle, pas une corvée. À deux jetons on y retournerait
 * toutes les deux minutes et le monnayeur deviendrait le jeu ; à vingt, le
 * jeton cesserait d'être un geste et le monnayeur redeviendrait le décor
 * inerte qu'il était.
 */
#define ROOM_ECO_PLANCHER_ACCUEIL 5

/*
 * LE CHANGE — des tickets vers des jetons, À LA DEMANDE.
 *
 * DIX tickets pour un jeton, et ce n'est pas un chiffre rond choisi pour être
 * rond : c'est la CIBLE de calibrage ci-dessous. Le barème est réglé pour
 * qu'une partie médiane rende dix tickets ; à ce taux, une partie médiane paie
 * exactement le jeton de la suivante.
 *
 * IL EST DEMANDÉ, ET JAMAIS AUTOMATIQUE. Le monnayeur a d'abord changé les
 * tickets tout seul avant de compléter au plancher, et `test_session` a chiffré
 * ce que ça coûtait : sur vingt parties médianes, le joueur finissait avec 57
 * tickets au lieu de 200. À dix tickets la partie et dix tickets le jeton, un
 * change automatique fait du sur-place exact — la vitrine reste hors de portée
 * pour toujours, et rien dans le jeu ne le dit.
 *
 * Le change sert donc à celui qui veut un STOCK au-dessus du plancher et refuse
 * de revenir marcher jusqu'au monnayeur. Celui qui n'a plus rien, lui, prend le
 * crédit d'accueil, qui est gratuit. Les deux chemins existent, aucun des deux
 * ne bloque, et c'est le second qui est le chemin normal.
 */
#define ROOM_ECO_TICKETS_PAR_JETON 10

/* La cible de calibrage : ce qu'une partie MÉDIANE doit rendre, sur les huit
 * jeux et les deux régimes. C'est ce que `tests/test_economie.c` vérifie sur
 * les seize lignes de la table, et ce qui interdit d'y écrire un taux au
 * jugé. */
#define ROOM_ECO_CIBLE_TICKETS 10

/*
 * LA SÉRIE — jours consécutifs joués.
 *
 * Elle ajoute UN ticket par jour de série à chaque partie terminée, plafonnée
 * à sept. Sept parce qu'une semaine est la plus longue série qu'on garde en
 * tête, et parce qu'un plafond borne ce qu'une rupture coûte : au pire une
 * semaine, jamais une année. Rapporté aux dix tickets de la médiane, une série
 * pleine vaut donc jusqu'à +70 % — visible, et pas au point qu'un joueur
 * revenu d'un mois d'absence se sente disqualifié.
 *
 * Rien ne punit l'absence au-delà de la perte du bonus : pas de compte à
 * rebours, pas de rappel, pas de « série en danger ». La série se lit dans la
 * salle et c'est tout.
 */
#define ROOM_ECO_SERIE_MAX 7

/*
 * LA PRIME DU RÉGIME DIFFICILE, en pourcentage.
 *
 * ELLE EST CHOISIE, PAS MESURÉE, et il faut le dire parce que l'énoncé
 * demandait de la mesurer. La mesure a été faite et elle CONTREDIT l'idée d'un
 * coefficient unique : le rapport des scores médians hard/normal vaut 0,26
 * pour envol, 0,21 pour demineur, 0,011 pour aplomb, 0,64 pour shooter, 0,96
 * pour asteroid et dedale, 1,00 pour piano — et 45,7 pour snake, dont le
 * régime difficile INVERSE la règle de score (manger coûte, laisser pourrir
 * rapporte, cf. `tests/test_snake.c`). Un coefficient unique appliqué à ces
 * seize lignes paierait le snake difficile quarante-cinq fois le snake normal.
 *
 * La difficulté est donc absorbée là où elle se mesure — dans la table, qui a
 * SEIZE lignes et non huit : chaque régime a son propre diviseur, calé sur sa
 * propre médiane. Une fois les unités égalisées, il ne reste qu'à récompenser
 * le CHOIX du régime difficile, et ça, aucune mesure ne le dicte. 25 %, soit
 * deux à trois tickets sur une partie médiane : assez pour qu'on le choisisse,
 * pas assez pour que le régime normal devienne un mauvais calcul.
 */
#define ROOM_ECO_PRIME_DIFFICILE_PCT 25

/*
 * LE PLAFOND PAR PARTIE, en multiple de la cible.
 *
 * Une partie exceptionnelle rapporte plus qu'une partie médiane — c'est tout
 * l'intérêt — mais la dispersion mesurée est telle qu'un rapport non borné
 * ferait d'une seule partie l'équivalent d'une soirée : shooter en régime
 * normal va de 2 160 à 21 315 points (max/médiane = 6,9) et aplomb de 4 000 à
 * 416 300 (3,3 fois la médiane). Le plafond à six fois la cible, soit soixante
 * tickets, laisse passer la quasi-totalité des bonnes parties et coupe la
 * queue de distribution qui vient du hasard des graines plutôt que du joueur.
 */
#define ROOM_ECO_PLAFOND_PARTIE 6

/* ==========================================================================
 * LE BARÈME, JEU PAR JEU ET RÉGIME PAR RÉGIME
 * ==========================================================================
 *
 * POURQUOI IL N'Y A PAS DE TAUX UNIQUE
 * ------------------------------------
 * Les huit jeux ne marquent pas dans la même unité, et l'écart n'est pas de
 * l'ordre du réglage : mesuré sur ce dépôt, le score médian d'une partie va de
 * 38 points (envol) à 124 600 (aplomb). Un taux unique ferait payer une partie
 * d'aplomb trois mille fois une partie d'envol, pour un joueur qui a fourni le
 * même effort. Ce n'est pas une injustice de degré, c'est un barème qui ne
 * veut rien dire.
 *
 * COMMENT LES DIVISEURS ONT ÉTÉ OBTENUS
 * -------------------------------------
 * Mesure du 2026-08-30 sur ce dépôt, à `c5c8b1d`. Vingt-quatre parties par jeu
 * et par régime, jouées par l'autopilote de chaque jeu au pas fixe de
 * `NS_DEFAULT_TICK_HZ` (120 Hz), graines espacées par le nombre d'or 64 bits,
 * chaque partie menée jusqu'à la mort ou jusqu'à un plafond de 180 s. Le
 * diviseur est la MÉDIANE divisée par la cible de dix tickets, arrondie.
 *
 * Les colonnes de la table sont donc : identifiant du jeu, médiane normale,
 * diviseur normal, médiane difficile, diviseur difficile. Les médianes sont
 * gardées À CÔTÉ des diviseurs et non dans un commentaire, parce que c'est
 * elles qui les justifient : `tests/test_economie.c` recalcule les seize
 * quotients et refuse tout diviseur qui ne rendrait pas 9 ou 10 tickets sur sa
 * propre médiane. Écrire un taux au jugé casse la construction.
 *
 * CE QUE CETTE MESURE NE DIT PAS, et il faut le lire avant de s'y fier
 * -------------------------------------------------------------------
 * L'autopilote n'est pas un joueur. Il ne prouve pas qu'une partie est
 * amusante, seulement qu'elle est jouable et qu'elle marque. Trois réserves
 * précises, qu'aucun chiffre de cette table ne lève :
 *
 *   - QUATRE LIGNES SONT PLAFONNÉES PAR LE TEMPS, pas par la mort. L'autopilote
 *     de snake difficile survit à 180 s dans 12 parties sur 24, celui d'aplomb
 *     normal dans 18 sur 24, dedale dans 11 et 10 sur 24. Leur médiane mesure
 *     donc « ce qu'on marque en trois minutes », pas « ce qu'on marque avant de
 *     perdre ». Un joueur humain qui tient plus longtemps sortira du plafond
 *     par partie ; c'est voulu, et c'est à quoi sert le plafond.
 *   - PIANO NE DISPERSE PAS. Sa médiane vaut 1 050 aux deux régimes, min = max,
 *     sur les 24 parties : l'autopilote joue le morceau juste, et le morceau a
 *     une longueur fixe. Son diviseur est donc exact et ne dit rien de la
 *     variance humaine, qui est la seule qui existe pour ce jeu-là.
 *   - LA DISPERSION EST ÉNORME AILLEURS. Étendue rapportée à la médiane :
 *     envol 121 %, snake 1 077 %, demineur 93 %, aplomb 331 %, asteroid 200 %,
 *     dedale 71 %, piano 0 %, shooter 617 %. La médiane est robuste à ça — la
 *     moyenne ne l'aurait pas été, et c'est pourquoi elle n'est pas employée.
 */

/*
 * X(id, mediane_normale, diviseur_normal, mediane_difficile, diviseur_difficile)
 *
 * `id` est celui de `ns_game_api.id`, et l'ordre est celui de `ns_game_at()`.
 * Le test confronte les deux listes : un jeu porté sans ligne ici, ou une ligne
 * ici sans jeu porté, arrête la construction. C'est ce qui empêche l'affiche de
 * promettre un taux pour un jeu qui n'existe pas, et le jeu de payer un taux
 * que l'affiche ne montre pas.
 */
#define ROOM_ECO_BAREME(X)                    \
    X(envol,     38,        4,     10,     1) \
    X(snake,   1740,      174, 104405, 10441) \
    X(demineur, 2320,     232,    476,    48) \
    X(aplomb, 124600,   12460,   1300,   130) \
    X(asteroid,  290,      29,    260,    26) \
    X(dedale,   4830,     483,   4550,   455) \
    X(piano,    1050,     105,   1050,   105) \
    X(shooter,  3105,     311,   2070,   207)

/* ==========================================================================
 * LES LOTS DE LA VITRINE
 * ==========================================================================
 *
 * Quatre, et chacun CHANGE quelque chose qu'on voit ou qu'on joue. Aucun
 * n'est un compteur qui monte : un lot qui n'ajouterait qu'un chiffre à un
 * écran ne serait pas un lot, ce serait un score de plus.
 *
 * LES PRIX, et pourquoi ceux-là. Une partie médiane rend dix tickets, une
 * bonne série jusqu'à dix-sept. Le premier lot tombe donc en six parties — le
 * temps d'une première visite, ce qui est exactement ce qu'il faut pour que la
 * vitrine cesse d'être un meuble et devienne une destination. Le dernier
 * demande une soixantaine de parties, soit quelques semaines de jeu quotidien
 * pour un joueur régulier : c'est long, et c'est le seul des quatre qui le
 * soit. Rien n'accélère ces prix contre de l'argent, parce qu'il n'y a pas
 * d'argent.
 *
 * X(cle, prix_en_tickets, titre_affiche, ce_que_ca_change)
 */
#define ROOM_ECO_LOTS(X)                                                     \
    X(QUITTE,    60,  "QUITTE OU DOUBLE", "risquer ses tickets apres coup")   \
    X(DIFFICILE, 150, "REGIME DIFFICILE", "le regime dur sur les 19 bornes")  \
    X(DOREE,     320, "PLAQUE DOREE",     "la salle vous passe en or")        \
    X(LIBRE,     600, "TOURNOI LIBRE",    "rejouer la partie du jour")

#endif /* NS_ROOM_BAREME_H */
