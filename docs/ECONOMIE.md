# L'économie de la salle

**JETON → PARTIE → TICKETS → LOT.**

On prend ses jetons au monnayeur, on en insère un pour jouer, la partie rend des tickets selon un
barème calibré jeu par jeu, et les tickets s'échangent à la vitrine contre des choses qui se
voient ou qui se jouent.

## Ce que cette économie n'est pas

**Elle est fictive et fermée.** Il n'existe aucune voie d'achat : pas de monnaie réelle, pas de
boutique, pas de coffre aléatoire payant, pas d'abonnement, pas de publicité. Un jeton ne
s'obtient qu'en jouant ou en le demandant au monnayeur, et le monnayeur ne demande rien en
retour.

Il n'y a pas non plus de minuterie qui punit l'absence, ni de compte à rebours qui presse. Les
trois crochets — le tournoi du jour, la série, le quitte ou double — se refusent tous les trois
sans rien perdre d'autre que ce qu'on n'a pas joué.

Les affiches de 2020 annonçaient « 5 POUR 1 EURO » et « 20 POUR 3 EUROS ». C'était le décor
plausible d'une vraie salle, et c'est devenu un mensonge le jour où le monnayeur a été branché.
Elles ne le disent plus : voir *Les affiches ne peuvent plus mentir*, plus bas.

## Où ça vit

| | |
|---|---|
| Les chiffres, source unique | `room/room_bareme.h` — aucune dépendance, ni SDL ni moteur |
| L'état et les règles | `room/room_economie.{c,h}` — pur, testable sans GPU ni fenêtre |
| Le test | `tests/test_economie.c` — 362 contrôles |
| La sauvegarde | `portefeuille.txt`, à côté de `settings.cfg` et `scores.txt` |
| Ce que la salle appelle | sept points d'appel dans `room/main.c`, tous groupés |

Le portefeuille est écrit atomiquement — un temporaire puis un renommage — et son format est du
texte ligne à ligne, comme `ns_scores` et pour la même raison : une ligne abîmée se saute et le
reste survit, alors qu'un fichier structuré tronqué est perdu en entier. C'est le cas de la
coupure de courant, et c'est celui qu'on veut survivre.

## Le jeton : ce qu'il est, et ce qu'il n'est pas

Une partie coûte **un jeton**, inséré par le geste qui existait déjà — `NS_VM_TOKEN`, la pièce
que le joueur tient dans la main droite depuis A7, et `--pose=insert`. Relancer après la mort en
coûte un aussi : sans quoi la borne offrirait une partie gratuite à qui reste devant elle, et le
jeton n'aurait coûté qu'à celui qui marche.

**Le monnayeur complète jusqu'à cinq jetons, gratuitement, autant de fois qu'on le demande.**
Pas de minuterie : un crédit qui se recharge à l'heure punit l'absence, et une salle d'arcade n'a
pas à décider quand on a le droit de jouer. Un joueur sans jeton et sans moyen d'en avoir a fini
de jouer — ce n'est pas une difficulté, c'est un défaut de conception, et ce plancher le rend
impossible.

**Cinq**, et le chiffre est mesuré. La durée médiane d'une partie d'autopilote sur les huit jeux
en régime normal vaut **63,6 s** (24 parties par jeu : 64,8 / 49,3 / 24,7 / 156,7 / 62,4 / 150,9
/ 106,8 / 52,4 s). Cinq jetons valent donc environ cinq minutes de jeu entre deux passages au
monnayeur, qui est à 3,4 m de la sortie du sas : l'aller-retour reste un geste de salle. À deux
jetons on y retournerait toutes les deux minutes et le monnayeur deviendrait le jeu ; à vingt, le
jeton cesserait d'être un geste.

Le **change** — dix tickets pour un jeton — existe en plus, et il est **demandé, jamais
automatique**. C'est la correction d'un défaut que le test a chiffré : voir *Ce qui a été écarté*.

**Le jeton n'est pas la monnaie de ce jeu, c'est le geste.** La monnaie, c'est le ticket. Un
geste ne doit rien coûter, sinon il devient un péage ; c'est la monnaie qui doit être rare.

## Le barème : calibré, pas proportionnel

### Pourquoi il n'y a pas de taux unique

Les huit jeux ne marquent pas dans la même unité, et l'écart n'est pas de l'ordre du réglage :
mesuré sur ce dépôt, le score médian d'une partie va de **38 points** (envol) à **124 600**
(aplomb). Un taux unique ferait payer une partie d'aplomb trois mille fois une partie d'envol,
pour un joueur qui a fourni le même effort. Ce n'est pas une injustice de degré, c'est un barème
qui ne veut rien dire.

### La mesure

Faite sur ce dépôt le 2026-08-30, à `c5c8b1d`. Vingt-quatre parties par jeu et par régime, jouées
par l'autopilote de chaque jeu au pas fixe de `NS_DEFAULT_TICK_HZ` (120 Hz), graines espacées par
le nombre d'or 64 bits, chaque partie menée jusqu'à la mort ou jusqu'à un plafond de 180 s.

| jeu | min | **médiane** | moyenne | max | durée | étendue/médiane | morts |
|---|---:|---:|---:|---:|---:|---:|---:|
| envol normal | 15 | **38** | 39,3 | 61 | 64,8 s | 121 % | 24/24 |
| envol dur | 1 | **10** | 11,6 | 31 | 20,4 s | 300 % | 24/24 |
| snake normal | 550 | **1 740** | 2 760,5 | 19 289 | 49,3 s | 1 077 % | 24/24 |
| snake dur | 6 409 | **104 405** | 91 457,6 | 158 330 | 159,8 s | 146 % | 12/24 |
| demineur normal | 159 | **2 320** | 2 081,4 | 2 320 | 24,7 s | 93 % | 24/24 |
| demineur dur | 50 | **476** | 550,8 | 1 467 | 10,6 s | 298 % | 24/24 |
| aplomb normal | 4 000 | **124 600** | 139 558,3 | 416 300 | 156,7 s | 331 % | 6/24 |
| aplomb dur | 0 | **1 300** | 10 941,7 | 224 800 | 9,8 s | 17 292 % | 24/24 |
| asteroid normal | 0 | **290** | 289,2 | 580 | 62,4 s | 200 % | 24/24 |
| asteroid dur | 0 | **260** | 307,5 | 650 | 58,7 s | 250 % | 24/24 |
| dedale normal | 2 210 | **4 830** | 4 234,6 | 5 660 | 150,9 s | 71 % | 13/24 |
| dedale dur | 1 340 | **4 550** | 4 349,2 | 5 800 | 157,2 s | 98 % | 14/24 |
| piano normal | 1 050 | **1 050** | 1 050,0 | 1 050 | 106,8 s | 0 % | 24/24 |
| piano dur | 1 050 | **1 050** | 1 050,0 | 1 050 | 75,7 s | 0 % | 24/24 |
| shooter normal | 2 160 | **3 105** | 4 537,3 | 21 315 | 52,4 s | 617 % | 24/24 |
| shooter dur | 1 395 | **2 070** | 2 194,6 | 6 260 | 32,1 s | 235 % | 24/24 |

La **médiane** est employée et non la moyenne, parce que la dispersion l'exige : shooter en
régime normal a une moyenne de 4 537 pour une médiane de 3 105, tirée par un seul 21 315.

### Ce que cette mesure ne dit pas

**L'autopilote n'est pas un joueur.** Il ne prouve pas qu'une partie est amusante, seulement
qu'elle est jouable et qu'elle marque. Trois réserves qu'aucun chiffre de la table ne lève :

- **Quatre lignes sont plafonnées par le TEMPS, pas par la mort.** Snake dur survit à 180 s dans
  12 parties sur 24, aplomb normal dans 18 sur 24, dedale dans 11 et 10 sur 24. Leur médiane
  mesure « ce qu'on marque en trois minutes », pas « ce qu'on marque avant de perdre ». Un joueur
  humain qui tient plus longtemps sortira du plafond par partie ; c'est voulu.
- **Piano ne disperse pas.** Sa médiane vaut 1 050 aux deux régimes, min = max, sur 24 parties :
  l'autopilote joue le morceau juste, et le morceau a une longueur fixe. Son diviseur est exact et
  ne dit rien de la variance humaine, qui est la seule qui existe pour ce jeu-là.
- **Aplomb dur a une étendue de 17 292 %** : de 0 à 224 800 pour une médiane de 1 300. Le
  diviseur y est le moins fiable des seize.

### La table

Le diviseur est la médiane divisée par la cible de dix tickets, arrondie. Il vit dans
`room/room_bareme.h`, à côté de la médiane qui le justifie.

| jeu | points/ticket normal | points/ticket dur |
|---|---:|---:|
| envol | 4 | 1 |
| snake | 174 | 10 441 |
| demineur | 232 | 48 |
| aplomb | 12 460 | 130 |
| asteroid | 29 | 26 |
| dedale | 483 | 455 |
| piano | 105 | 105 |
| shooter | 311 | 207 |

Le calcul complet d'une partie :

```
tickets = min(score / diviseur, 60)      # le plafond vaut 6 x la cible
si régime dur :  x 1,25                  # en entier : x 125 / 100
+ min(série, 7)                          # la série s'ajoute APRÈS le plafond
```

Le plafond porte sur ce que le **score** rapporte, pas sur la série : sans quoi un joueur au
plafond cesserait de voir sa série payer, ce qui est exactement le moment où elle doit se voir.

**Le plafond à soixante tickets** : la dispersion mesurée est telle qu'un rapport non borné
ferait d'une seule partie l'équivalent d'une soirée — shooter normal va de 2 160 à 21 315 (6,9
fois la médiane) et aplomb normal jusqu'à 416 300. Six fois la cible laisse passer la
quasi-totalité des bonnes parties et coupe la queue qui vient du hasard des graines plutôt que du
joueur.

### La prime du régime difficile : choisie, pas mesurée

**Il faut le dire, parce que l'énoncé demandait de la mesurer.** La mesure a été faite et elle
*contredit* l'idée d'un coefficient unique. Le rapport des médianes dur/normal :

| jeu | dur / normal |
|---|---:|
| aplomb | 0,011 |
| demineur | 0,21 |
| envol | 0,26 |
| shooter | 0,64 |
| asteroid | 0,96 |
| dedale | 0,96 |
| piano | 1,00 |
| **snake** | **45,7** |

Snake est le cas qui tranche : son régime difficile **inverse la règle de score** — manger coûte,
laisser pourrir rapporte (`tests/test_snake.c`). Un coefficient unique appliqué à ces seize
lignes paierait le snake difficile quarante-cinq fois le snake normal.

La difficulté est donc absorbée **là où elle se mesure** : la table a seize lignes et non huit,
chaque régime calé sur sa propre médiane. Une fois les unités égalisées, il ne reste qu'à
récompenser le **choix** du régime difficile, et ça, aucune mesure ne le dicte. **25 %**, soit
deux à trois tickets sur une partie médiane : assez pour qu'on le choisisse, pas assez pour que le
régime normal devienne un mauvais calcul.

## Les trois crochets

### Le tournoi du jour

Les huit jeux tirés d'une graine dérivée de la **date**. Tout le monde joue la même partie
aujourd'hui, et demain c'en est une autre.

C'est le crochet le plus fort et le plus honnête de cette salle, et il coûte presque rien à écrire
parce que le dépôt l'avait préparé sans le savoir : les huit jeux sont des états purs rejouables
au bit près (`tests/test_replay.c`), donc deux machines qui reçoivent la même graine jouent
réellement la même partie, et le journal d'entrées scellé rend le score vérifiable.

**Le jour est le jour UTC**, et c'est un arbitrage assumé. « Tout le monde joue la même partie
aujourd'hui » et « le classement se remet à zéro à minuit » ne peuvent pas être vraies ensemble :
un minuit local fait basculer Tokyo neuf heures avant Paris, et les deux joueraient alors des
grilles différentes en s'imaginant le contraire. Entre les deux, c'est la première qui fait le
tournoi — un classement où l'on ne joue pas la même chose n'est pas un classement. L'affiche le
dit en toutes lettres : « REMISE A ZERO A MINUIT UTC ».

La graine est `splitmix64(jour × 0x100000001B3 ^ FNV1a(nom du jeu))`. Un mélangeur et non un
`rand()` ensemencé, parce que la propriété demandée n'est pas « du hasard » mais « la même valeur
partout » : deux machines, deux systèmes, deux versions de la bibliothèque C doivent tirer la même
partie. Le **nom** du jeu et non son index : un index dépendrait de l'ordre de `ns_game_at()`, et
réordonner la table des jeux changerait la partie du jour sans que personne ne l'ait demandé.

Le test vérifie que la graine **arrive** aux huit jeux, et pas seulement qu'elle existe : il
compare les empreintes d'état après `reset` sur les huit, le même jour et le lendemain. Un jeu
dont l'état de départ ne dépendrait pas de la graine ferait un tournoi immuable — tout le monde
jouant « la partie du jour » qui ne change jamais, sans qu'aucun message le dise.

**Et il vérifie que la SALLE s'en sert**, ce qui est une propriété distincte. C'est la preuve par
mutation qui l'a exigé : remplacer `room_eco_salle_graine` par une constante ne faisait tomber
aucune assertion, parce que le tournoi était vérifié de bout en bout dans sa partie pure et pas du
tout à l'endroit où la borne décide de l'employer. Une propriété vraie et un chemin réel qui ne
l'emprunte pas, c'est exactement le défaut que ce dépôt passe son temps à rendre impossible.

Les chemins de **capture** gardent leur graine fixe : `--play-at=` et l'avance rapide de
`--warmup=` doivent rendre la même image d'un jour à l'autre, ce qu'une graine dérivée de la date
leur retirerait.

### La série

Jours consécutifs où au moins une partie a été terminée. Elle ajoute **un ticket par jour de
série** à chaque partie, plafonnée à **sept**.

Sept parce qu'une semaine est la plus longue série qu'on garde en tête, et parce qu'un plafond
borne ce qu'une rupture coûte : au pire une semaine, jamais une année. Rapporté aux dix tickets de
la médiane, une série pleine vaut jusqu'à **+70 %** — visible, et pas au point qu'un joueur revenu
d'un mois d'absence se sente disqualifié.

Deux parties le même jour ne comptent qu'une fois : sinon la série mesurerait l'acharnement d'une
soirée et non l'assiduité. Un jour sauté la remet à 1 — pas à 0 : le joueur vient de jouer, il est
au premier jour d'une nouvelle série.

**Rien ne punit l'absence au-delà de la perte du bonus** : pas de compte à rebours, pas de rappel,
pas de « série en danger ». Elle se lit dans la salle, en haut à gauche, et seulement si elle vaut
quelque chose — une ligne « SERIE 0 J » affichée en permanence à un joueur qui vient d'arriver ne
l'informe pas, elle lui reproche quelque chose.

### Le quitte ou double

Après une partie, risquer ses tickets sur une reprise **en régime difficile**, avec son propre
score à battre. Gagner double la mise ; échouer la perd — et **rien d'autre** : le solde acquis
avant la partie ne bouge pas.

**Le refus est aussi facile que l'acceptation**, et c'est une contrainte de conception, pas une
politesse :

- les deux réponses sont une touche, écrites sur la même ligne, dans la même taille et la même
  couleur ;
- rejouer, repartir, ouvrir le menu ou quitter le jeu valent tous **refus**, et versent ;
- ce qui est annoncé en premier est le **risque** — « RISQUER 14 » avant « GARDER 14 ». Un pari
  qui annonce d'abord ce qu'on peut gagner ment par cadrage, même quand tous ses chiffres sont
  justes ;
- ni compte à rebours, ni « êtes-vous sûr », ni animation qui pousse vers le oui.

La mise **n'est pas persistée** : une table laissée ouverte au moment où l'on quitte le jeu est un
refus. La sauver rouvrirait la table au lancement suivant, devant un joueur qui ne se souvient plus
de la partie qu'il devrait battre.

C'est lui-même un **lot** de la vitrine, à 60 tickets : c'est ce qui donne au premier achat
quelque chose à changer tout de suite.

## Les lots

Quatre, et chacun **change** quelque chose qu'on voit ou qu'on joue. Aucun n'est un compteur qui
monte : un lot qui n'ajouterait qu'un chiffre à un écran ne serait pas un lot, ce serait un score
de plus.

| lot | prix | ce que ça change |
|---|---:|---|
| **QUITTE OU DOUBLE** | 60 | le pari après chaque partie |
| **REGIME DIFFICILE** | 150 | le régime dur sur les dix-neuf bornes, pas seulement les six qui le déclarent — et 25 % de plus par partie |
| **PLAQUE DOREE** | 320 | la salle passe en or |
| **PARTIE LIBRE** | 600 | jouer autre chose que la partie du jour |

**`PARTIE LIBRE` est le plus cher, et c'est cohérent.** Par défaut, toutes les parties du jour sur
une borne donnée sont la *même* partie — c'est ce que le tournoi veut dire. Ce lot rend la graine
tirée de l'horloge, donc la variété : un joueur qui a fait une soixantaine de parties a gagné le
droit de s'entraîner sur autre chose que la grille du jour.

Une partie médiane rend dix tickets, une bonne série jusqu'à dix-sept. Le premier lot tombe donc
en **six parties** — le temps d'une première visite, ce qui est exactement ce qu'il faut pour que
la vitrine cesse d'être un meuble et devienne une destination. Le dernier demande une soixantaine
de parties, soit quelques semaines de jeu quotidien : c'est long, et c'est le seul des quatre qui
le soit. Rien n'accélère ces prix contre de l'argent, parce qu'il n'y a pas d'argent.

**La vitrine montre ce qu'on peut avoir**, à deux niveaux : sa face porte la liste complète avec
les prix — une planche dessinée depuis la même table — et l'invite affiche le lot en vue, son prix
et son effet. Elle montre le plus cher qu'on puisse s'offrir tout de suite ; à défaut, le moins
cher qui manque. Dans cet ordre, parce qu'une vitrine qui montrerait d'abord l'inatteignable ne
donne envie de rien.

## Les affiches ne peuvent plus mentir

C'est la demande explicite du propriétaire, et c'était le défaut le plus voyant : quatre des huit
planches parlaient d'argent, et les quatre décrivaient un autre jeu.

| affiche | ce qu'elle disait | ce qu'elle dit |
|---|---|---|
| `jetons` | « 5 POUR 1 EURO », « 20 POUR 3 EUROS » | la grille des huit taux, le plancher, le change, « AUCUN EURO ICI, JAMAIS » |
| `reglement` | six articles inventés — la file d'attente, les verres sur les panneaux, la queue de billard | les six vraies règles, écrites depuis les constantes |
| `records` | huit noms et huit scores inventés (« LEA 812340 ») | le score de référence des huit jeux |
| `tournoi` | « SAMEDI 21 HEURES, INSCRIPTION AU COMPTOIR » | tous les jours, remise à zéro à minuit UTC, sans inscription |

Une cinquième planche s'est ajoutée : `lots`, la face du meuble `vitrine_lots`, qui portait
jusqu'ici `affiche_attention.png` — l'avertissement « lumières clignotantes ». Une vitrine à lots
qui affiche un avertissement d'épilepsie apprend au joueur que ce meuble ne le concerne pas.

**Le mécanisme, et c'est lui qui compte.** Les cinq motifs incluent `room/room_bareme.h`, qui est
la source unique et que le jeu lit aussi. C'est le raisonnement du motif `plan`, qui *trace* la
salle au lieu de la dessiner de mémoire, appliqué à l'économie : une affiche qui recopie un
chiffre est une deuxième description, et deux descriptions d'une même chose finissent par se
contredire — ici **en silence**, le seul endroit où on lit l'affiche étant un mur à cinq mètres.
Changer un taux redessine les affiches au build suivant.

`room_bareme.h` n'a **aucune dépendance** — ni SDL, ni `ns_core.h`, ni allocation. C'est ce qui
permet à un outil d'hôte de partager ses chiffres avec le jeu sans partager son moteur. Y ajouter
un include de moteur casserait `posterart` et rouvrirait la porte aux deux descriptions.

**Trois contrôles arrêtent la construction** plutôt que de livrer une affiche fausse :

1. `tests/test_economie.c` confronte chaque diviseur à la **médiane mesurée** gardée à côté de
   lui, et refuse tout taux qui ne rendrait pas la cible. Un taux modifié sans remesure casse le
   build.
2. Le même test confronte la table du barème à la liste des jeux **portés** (`ns_game_at`). Un
   neuvième jeu sans tarif paierait zéro ticket en silence ; ici, ça arrête la construction.
3. `posterart` mesure les douze lignes composées du règlement : un taux qui gagnerait un chiffre
   au point de dépasser dix-neuf caractères fait échouer l'outil avec le nom de l'article fautif,
   au lieu de sortir une affiche coupée.

### Ce que `records` ne peut pas faire, et pourquoi c'est dit plutôt que contourné

Elle ne peut **pas** montrer les vrais records du joueur. Cette planche est dessinée à la
construction, des mois avant qu'un joueur existe, et les records vivent dans son `scores.txt`. Une
affiche murale cuite dans le paquet ne peut pas les lire.

Les vrais records **sont** affichés dans la salle, par deux surfaces qui les lisent à l'exécution :
la dalle de `borne_classement` (`room_hud_draw_leaderboard`) et le tableau du bar
(`room_hud_draw_scoreboard`). L'affiche porte donc le **score de référence** — la médiane mesurée
qui a servi à poser chaque taux — et renvoie à la borne pour le reste.

## La preuve par mutation

**Un test qui ne tombe pas quand on casse ce qu'il vérifie ne vérifie rien.** Chaque règle de
`room_economie.c` et de `room_bareme.h` a donc été cassée volontairement, une à la fois, le test
relancé, le nombre d'assertions tombées relevé, et la source remise en état. Le harnais est un
script jetable ; la mesure, elle, est ci-dessous.

**Référence : 406 contrôles, 0 échec. 28 mutations, 0 muette.**

| ce qu'on casse | assertions qui tombent |
|---|---:|
| le monnayeur ne complète plus au plancher | 33 |
| la graine du tournoi ignore le jeu | 29 |
| la série ne met plus son record à jour | 24 |
| le garde d'insertion saute (solde négatif possible) | 21 |
| un taux unique remplace le barème par jeu | 17 |
| la graine du tournoi ignore le jour | 11 |
| le change ne vérifie plus les tickets | 5 |
| le prix d'un lot n'est plus vérifié | 5 |
| la mise n'est pas effacée à l'encaissement | 5 |
| un jeu du barème n'est pas porté (piano → pacman) | 5 |
| la prime du régime difficile disparaît | 4 |
| la mise gagnée n'est pas levée | 4 |
| le lot `PARTIE LIBRE` n'a plus d'effet sur la graine | 4 |
| un jour sauté ne rompt plus la série | 3 |
| l'égalité gagne le quitte ou double | 3 |
| un lot déjà acquis se rachète | 3 |
| la vitrine montre le plus cher qui manque | 3 |
| le plafond par partie saute | 2 |
| la série n'est plus plafonnée | 2 |
| le score négatif n'est plus borné | 2 |
| les valeurs lues du fichier ne sont plus bornées | 2 |
| la réparation série/record au chargement saute | 2 |
| `room_eco_valide` accepte un solde négatif | 2 |
| un diviseur écrit au jugé (envol 4 → 7) | 2 |
| la division de date n'est plus euclidienne | 1 |
| les lots inconnus d'un fichier abîmé sont gardés | 1 |
| la version du fichier n'est plus vérifiée | 1 |
| `room_eco_valide` accepte une mise sans jeu | 1 |

### Ce que la première passe a trouvé, et qui comptait plus que le tableau

Trois mutations sont d'abord restées **muettes**. Les trois étaient de vrais trous, et les trois
ont été bouchées :

1. **Retirer le bornage du score négatif ne cassait rien.** La raison est arithmétique et
   vicieuse : le test essayait `-1` et `INT64_MIN`, or `-1 / 4` vaut 0 en C — la division entière
   tronque vers zéro — et `INT64_MIN / 4` vaut `0xE000000000000000`, dont les 32 bits de poids
   faible sont nuls. Le retour en `int32_t` valait donc 0 dans les deux cas. Il fallait un score
   négatif dont le quotient ne tombe ni sur zéro ni sur un multiple de 2³² : `-1000` donne -250.
2. **Casser `room_eco_valide` ne cassait rien.** Rien dans les autres tests ne *produit* d'état
   invalide, puisque c'est justement ce qu'on a rendu impossible : la fonction n'était appelée que
   sur des états sains, où elle rend vrai quoi qu'on lui ait fait dire. Un contrôle qui ne voit
   jamais le cas qu'il refuse ne refuse rien. Les états invalides lui sont maintenant donnés à la
   main, un champ à la fois.
3. **Remplacer `room_eco_salle_graine` par une constante ne cassait rien.** Le tournoi était
   vérifié de bout en bout dans sa partie pure — même graine le même jour, autre le lendemain,
   propagée aux huit jeux — et pas du tout à l'endroit où la borne décide de s'en servir. Une
   propriété vraie et un chemin réel qui ne l'emprunte pas.

Le troisième est le plus instructif : c'est exactement le défaut que ce dépôt documente ailleurs
sous une autre forme — « une vérification qui emprunte un chemin que le joueur n'emprunte pas ne
vérifie rien » (`room/main.c`, à propos de `--play-at`).

## Ce qui a été écarté, et pourquoi

### Le change automatique au monnayeur

**La première version était fausse, et c'est le test qui l'a chiffrée.** Le monnayeur changeait
d'abord les tickets en jetons, puis complétait au plancher — l'idée étant que bien jouer devait
monter au-dessus des cinq jetons. `test_session` a démenti : sur vingt parties médianes, le joueur
finissait avec **57 tickets au lieu de 200**, et le premier lot en coûte 60.

La cause est arithmétique et elle était sous mes yeux : une partie médiane rapporte dix tickets et
un jeton en coûte dix. Le change automatique faisait donc du **sur-place exact** — la vitrine
restait inatteignable pour toujours, et rien dans le jeu ne l'aurait dit.

Le crédit d'accueil est désormais gratuit et le change se demande. C'est de là que vient la
formule *le jeton est le geste, le ticket est la monnaie*.

### Un coefficient unique pour le régime difficile

Écarté par la mesure, pas par le goût : voir plus haut. Snake dur paierait quarante-cinq fois
snake normal.

### Le minuit local

Écarté au profit d'UTC : voir *Le tournoi du jour*. Deux joueurs du même tournoi doivent jouer la
même grille.

### La moyenne comme référence de calibrage

Écartée pour la médiane. Shooter normal a une moyenne de 4 537 pour une médiane de 3 105, tirée
par une seule partie à 21 315 : calibrer sur la moyenne aurait sous-payé les trois quarts des
parties.

### Tout ce que l'énoncé interdit

Monnaie réelle, achat, coffre aléatoire payant, minuterie qui punit l'absence, compte à rebours
qui presse. Aucun n'est implémenté, et `room_bareme.h` le déclare en tête pour que le prochain qui
ouvre le fichier n'ait pas à le déduire.

### Ce qui n'a pas été fait

- **La série n'a pas de récompense de palier.** Elle paie linéairement jusqu'à sept et s'arrête.
  Un palier à sept jours aurait été un crochet de plus, mais il aurait aussi été le premier
  endroit où rater un jour coûte quelque chose de nommé — et c'est précisément ce que l'énoncé
  écarte.
- **`PLAQUE DOREE` n'a pas encore d'effet visible.** Il est acheté, persisté et affiché, mais
  repeindre la salle demande `engine/render/`, sur lequel un autre chantier travaille. Les trois
  autres lots sont entièrement branchés : le pari, le régime dur sur les dix-neuf bornes, et la
  graine libre.
- **Le tournoi n'a pas de classement séparé.** La graine du jour est calculée, vérifiée, et **la
  salle s'en sert** — c'est la graine de toute partie lancée sur une borne. Ce qui manque est le
  classement *du jour*, distinct du classement de toujours : il demande une clé par date dans
  `ns_scores`, donc une modification d'`engine/net/`, hors du périmètre de ce lot. Aujourd'hui, une
  partie de tournoi entre au classement local comme les autres.
- **La médiane de calibrage est celle de l'AUTOPILOTE.** Elle est reproductible et vérifiée à
  chaque build, mais elle ne mesure pas un joueur. Quatre des seize lignes sont même plafonnées
  par le temps plutôt que par la mort. Le barème devra être remesuré sur de vraies parties le jour
  où il y en aura assez — et la table est faite pour ça : les médianes sont gardées à côté des
  taux, et le test refuse un taux qui ne découlerait pas de la sienne.
