# Revue stricte — Nineteen v15

Revue menée le 29 août 2026 sur la branche `claude/game-engine-modernization-j3t6hw`,
commit `7fe338f`, sur **MacBook Pro M1 Pro (GPU 14 cœurs), macOS 27.0, backend Metal**,
écran Retina 3024 x 1964.

Tout ce qui suit a été **reproduit en ligne de commande et regardé image par image**.
Les paliers de performance ont été mesurés cinq fois trois avec `--bench`, qui attend
réellement le GPU. Aucun constat n'est déduit de la lecture du code seul : le code n'est
cité que pour désigner l'endroit à corriger.

`ctest --preset macos-universal` : **32/32 en 10,6 s**. Rien de ce qui suit n'est
attrapé par la suite de tests.

> **Sur quel binaire.** Toutes les mesures et toutes les captures de ce document ont été
> produites entre 00h19 et 00h50 avec le binaire de `7fe338f`, `cmake --build` répondant
> « ninja: no work to do ». Pendant la rédaction, l'arbre de travail a été modifié par
> quelqu'un d'autre — `assets/CMakeLists.txt`, `assets/blender/borne.py`,
> `assets/models/borne/borne.bin` — et reconstruit à 00h58. Ces changements touchent le
> maillage de la borne et la chaîne de dépendances des assets. **Les constats portant sur
> la géométrie de la borne (R-21, R-22) sont donc à revérifier sur le nouveau maillage** ;
> je ne les ai ni retirés ni réécrits, faute de les avoir regardés.

---

## Récapitulatif

| № | Gravité | Constat |
|---|---|---|
| R-01 | bloquant | Le réglage par défaut rend **12,6 images/s** sur un M1 Pro |
| R-02 | bloquant | Aux trois paliers jouables, les caissons de borne virent au **blanc** |
| R-03 | bloquant | `--game=… --autoplay --frames=N` **ne se termine jamais** |
| R-04 | grave | Le classement est **illisible** : les colonnes se chevauchent |
| R-05 | grave | `--journal-entrees` écrit un journal **vide** et `--rejouer` l'accepte |
| R-06 | grave | Flappy Bird : décor **portrait** étiré dans un cadre 16/9, oiseau à 3,5 % |
| R-07 | grave | Sur la dalle d'une borne, le texte des jeux **n'est pas lisible** |
| R-08 | grave | Piano : **trois voies sur quatre** invisibles |
| R-09 | grave | Shooter : une **couture verticale** coupe le couloir de jeu |
| R-10 | grave | `--autoplay` **écrit ses scores** dans le classement du joueur |
| R-11 | grave | Les dix-neuf bornes annoncent « APPUYER SUR **ESPACE** » |
| R-12 | grave | Le menu **annonce de faux coûts** de qualité |
| R-13 | grave | Le palier `medium` **saccade** : pointes à 3,1 fois la moyenne |
| R-14 | grave | Toilettes : le panneau « PUSH » est **en miroir** |
| R-15 | grave | Toilettes : la **moquette rouge traverse** le sol des toilettes |
| R-16 | gênant | Cinq jeux sur huit **gaspillent 45 à 66 %** de la dalle en noir |
| R-17 | gênant | Le billard : rails **feuilletés**, texture étirée |
| R-18 | gênant | La suspension du billard est **noire par en dessous** |
| R-19 | gênant | Le tapis néon : motifs de **1,5 à 2 m**, texture étirée |
| R-20 | gênant | De la **lumière fuit sous les caissons** |
| R-21 | gênant | Les boutons sont des **pastilles plates** ; la boule du manche est facettée |
| R-22 | gênant | Les mains : **palettes sans doigts**, sur aucune commande, et qui traversent |
| R-23 | gênant | Colonnes et bar : **aplats saturés** sans atténuation sur six mètres |
| R-24 | gênant | Pac-Man est un **carré jaune**, les fantômes des carrés |
| R-25 | gênant | Tetris : quatre blocs posés rendus **30 % plus sombres** |
| R-26 | gênant | Toilettes : miroir **flottant**, lavabo sans cuve, robinet en double |
| R-27 | gênant | Les toilettes sont **cinq fois plus claires** que le hall |
| R-28 | gênant | Le pavage du crépi mural est **visible partout** |
| R-29 | gênant | Le « 1/4 » du classement est **hors de la zone sûre** et déchiré |
| R-30 | gênant | La dalle a un **bord en escalier** et les filets se cassent |
| R-31 | gênant | La poussière passe **devant la dalle** et se lit comme des traces de doigts |
| R-32 | gênant | Le menu **cache ce qu'il fait régler** ; « QUITTER » colle à « REPRENDRE » |
| R-33 | gênant | `--play-at` : noms opaques, et un nom faux **échoue en silence** |
| R-34 | gênant | Snake : **ni tête ni queue** identifiable, score coupé par le bord |
| R-35 | gênant | Une dalle de borne n'affiche que **`[BETA]`** |
| R-36 | cosmétique | Deux affiches **identiques** à deux mètres l'une de l'autre |
| R-37 | cosmétique | Le téléviseur du bar est **noir absolu**, un badge **flotte** devant |
| R-38 | cosmétique | `assets/scene/cabinets.json` est **périmé** et contredit la salle |
| R-39 | cosmétique | Le README annonce **quinze** bornes et **64** sources : c'est 19 et 47 |
| R-40 | cosmétique | Une ligne de journal **par point marqué** |

---

## Bloquant

### R-01 — Le réglage par défaut rend 12,6 images par seconde

**Gravité** : bloquant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --bench --frames=40 \
    --quality=medium --scale=1.0 --view=allee --no-hud
```
(`medium` et `scale=1.0` sont les défauts annoncés par `--help`.)

**Ce qu'on voit** : la salle vide, sans HUD, sans jeu, tourne à douze images par seconde
sur une machine sortie pour faire tourner des jeux.

**La preuve** — matrice complète, `--view=allee`, cinq paliers x trois échelles :

*À 1280 x 720 (ms par image, images/s) :*

| Palier | 0,6 | 0,8 | 1,0 |
|---|---:|---:|---:|
| potato | 5,8 (172) | 9,1 (110) | 13,3 (75) |
| low | 6,1 (163) | 9,7 (103) | 14,1 (71) |
| **medium** (défaut) | 18,5 (54) | 37,5 (27) | **57,1 (17,5)** |
| high | 35,0 (28,5) | 58,3 (17) | 95,2 (10,5) |
| ultra | 113,9 (8,8) | 218,7 (4,6) | 319,0 (3,1) |

*À la fenêtre par défaut — `--width=1600 --height=900` devient **3200 x 1800** de cible
de rendu sur un écran Retina, et c'est ce que reçoit un vrai joueur :*

| Palier / échelle | ms | images/s | tient 60 ? |
|---|---:|---:|---|
| potato 0,8 | 14,5 | **68,9** | oui |
| low 0,8 | 15,6 | **64,0** | oui |
| potato 1,0 | 21,6 | 46,2 | non |
| low 1,0 | 23,4 | 42,8 | non |
| medium 0,6 | 29,3 | 34,1 | non |
| **medium 1,0 (défaut)** | **79,6** | **12,6** | non |
| high 0,6 | 71,2 | 14,0 | non |
| high 1,0 | 177,2 | 5,6 | non |
| ultra 1,0 | 507,6 | 2,0 | non |

**Verdict de palier** : sur un M1 Pro à la résolution par défaut, **seuls `potato` et
`low`, et seulement à `--scale` ≤ 0,8, tiennent 60 images/s.** Le défaut livré en tient 12,6.

**Pourquoi c'est un défaut** : un joueur qui lance le binaire pour la première fois ne
passe pas par le menu. Il voit une salle qui rame, il conclut que le jeu est cassé, et il
ferme. Le palier par défaut ne doit pas être un palier qu'aucune configuration de sortie
d'usine ne peut tenir.

**Piste** : deux choses distinctes. (1) Le défaut de `--quality` dans `room/main.c` ;
`low` ne coûte que 6 % de plus que `potato` (cf. R-12) et serait un bien meilleur défaut.
(2) Demander une fenêtre de 1600 x 900 produit une cible de **3200 x 1800** — quatre fois
les pixels — sans que `--scale` ne compense. Il n'existe aucun réglage de résolution ni de
plein écran dans le menu (`room/room_menu.c`) : le joueur ne peut pas corriger ce qui le
gêne le plus.

---

### R-02 — Aux trois paliers jouables, les caissons de borne virent au blanc

**Gravité** : bloquant
**Où** :
```sh
for q in potato medium high; do
  ./build/macos-universal/bin/nineteen --headless --screenshot=q_$q.png --frames=6 \
      --quality=$q --scale=1.0 --width=1280 --height=720 --view=allee --no-hud
done
```
puis comparer la région (0, 620) 300 x 420 des trois captures.

**Ce qu'on voit** : le nez du caisson de la borne Snake — la plus grande surface au centre
du cadre — est **vert sombre** en `high` et `ultra`, et **blanc gris (#d8e0dc)** en
`potato`, `low` et `medium`. Le losangé du flanc suit : traits sombres en `high`, traits
**blancs** en `medium`.

**La preuve** : les captures `high` et `medium` sont pixel pour pixel le même cadrage, la
même échelle, la même vue. La bande verticale qui descend du haut au centre du cadre passe
de vert sombre à blanc laiteux entre les deux. Ce n'est pas un effet retiré, c'est une
**couleur qui change** : la caisse de la borne ne fait plus la même couleur selon le
palier.

**Pourquoi c'est un défaut** : `high` et `ultra` ajoutent le lancer de rayons — donc les
ombres. En dessous, il n'y a **aucun repli d'ombre** : les surfaces en retrait reçoivent
la pleine lumière des plafonniers et brûlent. Et le seul palier qui corrige cela tourne à
5,6 images/s (R-01). Autrement dit : **on peut avoir la bonne couleur ou 60 images/s, pas
les deux.** La borne d'arcade, l'objet dont le jeu porte le nom, n'a sa vraie peinture dans
aucune configuration jouable.

**Piste** : `engine/shaders/lighting.frag`. Il faut un repli d'ombre — carte d'ombre, ou
au minimum une occlusion de contact — actif dès `potato`, ou une atténuation de la
contribution directe sur les faces non testées.

---

### R-03 — `--game=… --autoplay --frames=N` ne se termine jamais

**Gravité** : bloquant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --frames=2 --width=320 --height=180 \
    --game=envol --autoplay --warmup=20
```

**Ce qu'on voit** : le processus imprime « 20.0 s avancées (2399 pas), 1 partie(s),
score 9 » à t = 1,76 s, puis **continue à jouer indéfiniment**, une ligne de journal toutes
les 1,67 s. Tué à 300 s, le score en était à 188. `--frames=2` n'a aucun effet.

**La preuve** : la même commande avec `--view=allee` à la place de `--game=envol` sort
normalement (code 0) en moins de deux secondes. Avec `--game=`, seul l'ajout de
`--screenshot=` provoque la sortie. Vérifié trois fois avec un chien de garde à 30 s :
`--view=allee` sort, les deux variantes `--game=` sont tuées.

**Pourquoi c'est un défaut** : `--frames=N` est documenté dans `--help` comme « nombre
d'images à rendre avant la capture ». Une tâche d'intégration continue qui lance les huit
jeux pour vérifier qu'ils démarrent se bloque pour toujours au premier. C'est exactement
l'emploi que `--autoplay` annonce (« captures, CI »).

**Piste** : `room/main.c`, la condition de sortie de la boucle principale — le compteur
d'images n'est consulté que sur le chemin salle, pas sur le chemin mini-jeu.

---

## Grave

### R-04 — Le classement est illisible : les colonnes se chevauchent

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=classement.png --frames=8 \
    --quality=high --scale=1.0 --width=1280 --height=720 --view=classement --no-hud
```
puis agrandir la dalle (région 1010, 620, 560 x 290).

**Ce qu'on voit** : sous les en-têtes `FLAPPY   F.HARD   SNAKE   S.HARD`, la première
ligne se lit `1    2   1    1   AUCUN   1    37`. Impossible de dire quel chiffre
appartient à quelle colonne. L'œil apparie « 2 1 » et « 1 1 », qui sont respectivement le
score de FLAPPY collé au rang de F.HARD.

**La preuve** : `room/room_hud.c:283` —
```c
SDL_snprintf(line, sizeof line, "%u %-3.3s %5u", i + 1u, e->name[0] ? e->name : "", e->score);
```
Le nom est **facultatif** (`--nom=` n'est jamais passé par défaut, et le commentaire du
code dit lui-même « le jeu n'a jamais demandé de nom »). Un nom vide donne donc
`"1" + espace + 3 espaces + espace + "    2"` : **sept cellules de blanc entre le rang et
le score**, plus large que l'espace qui sépare deux colonnes. L'état par défaut du
classement est l'état cassé.

**Pourquoi c'est un défaut** : c'est la borne que le joueur croise en premier en
descendant l'allée (le commentaire de `salle.room.json:1408` le dit), et la seule qui donne
envie de rejouer. Un tableau qu'on ne peut pas lire ne donne envie de rien.

**Piste** : `room/room_hud.c:283`. Rendre le champ nom élastique, ou supprimer les
séparateurs quand il est vide, ou centrer rang et score séparément sur des ancres fixes.

---

### R-05 — « AUCUN SCORE » se lit comme deux lignes de résultat

**Gravité** : grave
**Où** : même capture que R-04.

**Ce qu'on voit** : dans la colonne SNAKE, la ligne de rang 1 porte `AUCUN` et la ligne de
rang 2 porte `SCORE`. Le message d'absence est aligné **sur les lignes de données**, à
côté de `1 37` et `2 0` de la colonne voisine.

**La preuve** : `room/room_hud.c` —
```c
centred(s, cx, y, 1.5f * u, dim, "AUCUN");
centred(s, cx, y + 26.0f * u, 1.5f * u, dim, "SCORE");
```
`y` vaut `92.0f * u`, exactement le `y` de la première ligne de résultat, et le pas de
26 u est proche du pas de 32 u des lignes.

**Pourquoi c'est un défaut** : un joueur lit « rang 1 : AUCUN, rang 2 : SCORE ». Le
commentaire du code se justifie longuement sur le choix de deux mots courts plutôt qu'une
phrase ; le problème n'est pas la longueur, c'est la **position**.

**Piste** : `room/room_hud.c`, décaler le message hors de la grille et le teinter
différemment.

---

### R-06 — Flappy Bird : un décor portrait étiré dans un cadre 16/9

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=flappy.png --frames=4 \
    --game=envol --autoplay --warmup=25 --width=1280 --height=720
```

**Ce qu'on voit** : cinq paires de tuyaux à l'écran en même temps, et un oiseau grand comme
un grain de poussière.

**La preuve**, chiffrée depuis les constantes du jeu :

| | valeur | source |
|---|---|---|
| Planche d'origine | `BACKGROUND` 144 x 256 px, **portrait** | `games/flappy/flappy.c:25-26` |
| Facteur d'échelle | `SCALE 4` | `flappy.c:20` |
| Terrain que la planche décrit | **576 x 1024** | 144x4, 256x4 |
| Terrain réellement déclaré | **1920 x 1080** | `games/flappy/flappy.h:46-47` |

L'axe horizontal est donc **3,33 fois plus large** que le dessin ne le prévoyait, l'axe
vertical est juste. Conséquences mesurables :

- oiseau : `BIRD_W` 68 px sur 1920 = **3,5 % de la largeur**. Dans l'original :
  17 px sur 144 = **11,8 %**. Rapport : 3,4.
- écart entre tuyaux : `SPACING_EASY` 400 px sur 1920 = **4,8 paires visibles**.
  Dans l'original : 400 px sur 576 = 1,4 paire.
- la hauteur du passage, elle, est correcte : `GAP_HEIGHT` 196 px sur 1080 = 18 %,
  contre 196 sur 1024 = 19 % à l'origine.

Sur la dalle d'une borne (`--play-at=borne_arcade_1`), la dalle occupe 730 px d'un cadre de
2000 : l'oiseau y fait **25 pixels**, moins que les grains de poussière qui passent devant.

**Pourquoi c'est un défaut** : c'est le jeu auquel le propriétaire tient nommément. Le
personnage est illisible, et voir cinq obstacles d'un coup supprime la seule tension du
jeu — la décision au dernier moment.

**Piste** : `games/flappy/flappy.h:46-47`. Soit `FLAPPY_W` doit descendre à 576–800 avec
des bandes latérales assumées, soit `SCALE` doit monter sur l'axe horizontal. La deuxième
solution est la bonne : elle rétablit le gros pixel d'arcade que le commentaire de
`flappy.c:15-19` revendique.

---

### R-07 — Sur la dalle d'une borne, le texte des jeux n'est pas lisible

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=borne2.png --frames=6 \
    --quality=high --scale=1.0 --width=1280 --height=720 \
    --play-at=borne_arcade_2 --autoplay --warmup=12
```

**Ce qu'on voit** : sur la dalle de la borne Tetris, « 8 LIGNES » se lit `E _IGNES` et
« SUIVANTE » se lit `SU|VANTE`. Le puits de Tetris fait 210 px de large sur un cadre de
2000, soit **10,5 % de l'image** ; les blocs font 20 px.

**La preuve** : `room/room_hud.c:161-180` documente longuement le problème — « la dalle
fait 62 cm de large, on la lit à deux mètres et demi, elle n'occupe alors que trois cents
pixels » — et le corrige **pour le seul tableau de scores**. Les huit mini-jeux n'ont
jamais été repassés à cette aune : ils dessinent aux cotes du plein écran.

**Pourquoi c'est un défaut** : la promesse du jeu est de jouer **dans la dalle**, en
restant en 3D. Si tout ce qui est écrit sur la dalle est illisible, la promesse ne tient
que pour les jeux sans texte.

**Piste** : la même leçon que `room_hud.c` a apprise, appliquée à `games/*/`.
Un facteur d'échelle typographique commun, plus grand quand le jeu tourne dans une dalle
que quand il tourne en plein écran.

---

### R-08 — Piano : trois voies sur quatre sont invisibles

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=piano.png --frames=4 \
    --game=piano --autoplay --warmup=25 --width=1280 --height=720
```

**Ce qu'on voit** : la ligne de frappe porte quatre pavés. Le deuxième est orange vif. Les
trois autres sont **bordeaux très sombre, vert très sombre et bleu nuit** — quasiment de la
même valeur que le fond de la voie.

**La preuve** : luminance globale moyenne 19,1, médiane 19, **45,1 % des pixels sous 16**.
Le seul élément lisible de l'image est un pavé sur quatre. La ligne de frappe elle-même est
un filet d'un pixel à peine plus clair que le fond, et les séparateurs de voie aussi.

**Pourquoi c'est un défaut** : la règle du jeu, dite dans `docs/JOUER.md`, est que
**frapper une voie vide termine la partie**. On demande donc au joueur de viser
précisément quatre cibles dont il n'en voit qu'une.

**Piste** : `games/piano/`, la palette des voies. La ligne de frappe et les quatre cibles
sont l'information la plus importante de l'écran ; elles sont aujourd'hui les moins
contrastées.

---

### R-09 — Shooter : une couture verticale coupe le couloir de jeu

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=shooter.png --frames=4 \
    --game=shooter --autoplay --warmup=25 --width=1280 --height=720
```

**Ce qu'on voit** : le couloir de jeu est délimité par deux filets verticaux. À l'intérieur,
une **arête verticale franche** sépare un quart gauche en noir absolu (#000000) des trois
quarts droits en bleu très sombre (#0a0a14). Le fond du couloir ne couvre pas tout le
couloir.

**La preuve** : la discontinuité est nette, verticale, parfaitement rectiligne sur toute la
hauteur du cadre, et elle ne coïncide avec aucun des deux filets de bordure. Le vaisseau,
les ennemis et les tirs la traversent sans réagir.

**Pourquoi c'est un défaut** : le joueur voit une frontière qui n'en est pas une et croit
que la zone noire est hors-jeu. La dernière ligne du journal des versions (`7fe338f`,
« un quart d'écran perdu ») dit qu'un défaut de cette famille a déjà été corrigé ailleurs ;
celui-ci est resté.

**Piste** : `games/shooter/`, le quad de fond de l'aire de jeu et ses coordonnées, ou le
recouvrement de deux quads dont l'un est plus étroit.

---

### R-10 — `--autoplay` écrit ses scores dans le classement du joueur

**Gravité** : grave
**Où** :
```sh
wc -l ~/Library/Application\ Support/recognizer/Nineteen/scores.txt
./build/macos-universal/bin/nineteen --headless --screenshot=/tmp/x.png --frames=4 \
    --game=demineur --autoplay --warmup=25
wc -l ~/Library/Application\ Support/recognizer/Nineteen/scores.txt
```

**Ce qu'on voit** : le fichier passe de 23 à 29 lignes. Cette session de revue y a ajouté
six entrées.

**La preuve** : les lignes ajoutées, horodatées à la minute de mes lancements :
```
shooter/normal|735|10592|1787956344|
demineur/normal|980|16712|1787956346|
demineur/normal|245|4472|1787956346|
demineur/normal|200|2024|1787956346|
demineur/normal|105|152|1787956346|
demineur/normal|77|296|1787956346|
```
Noter les durées : **152 ms** et **296 ms**. Ce sont des parties de robot qui meurent
aussitôt, et elles sont désormais dans le tableau des meilleurs scores du joueur.

**Pourquoi c'est un défaut** : `--help` présente `--autoplay` comme un outil de capture et
de CI. Un outil de capture ne doit pas modifier les données du joueur. Et le classement de
`--view=classement` — la borne qu'on regarde pour savoir où on en est — devient un mélange
de parties humaines et de parties de robot indiscernables.

**Piste** : `room/main.c`, la soumission de score en fin de partie : la conditionner à
`!opt.autoplay`.

---

### R-11 — Les dix-neuf bornes annoncent « APPUYER SUR ESPACE »

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=borne.png --frames=8 \
    --quality=high --scale=1.0 --width=1280 --height=720 --view=borne --no-hud
```

**Ce qu'on voit** : la borne Flappy **et** la borne Tetris affichent le même écran
d'attente : « APPUYER SUR ESPACE POUR JOUER ! ». La vue `allee` montre la borne Snake avec
le même message.

**La preuve** : trois bornes hébergeant trois jeux différents, trois fois le même texte —
sur des jeux dont `docs/JOUER.md` dit qu'ils se jouent aux flèches (Snake, Tetris) ou au
manche (Démineur, Piano). Et dans la salle, la touche pour démarrer est `E` (insérer un
jeton), pas Espace.

**Pourquoi c'est un défaut** : le seul texte que le joueur lit avant de jouer lui donne une
touche qui n'est ni celle du démarrage en 3D, ni celle du jeu qu'il regarde. Une borne
d'arcade n'a d'ailleurs pas de barre d'espace : ce message casse aussi la fiction.

**Piste** : l'écran d'attente est commun aux huit jeux. Il doit dire « INSÉRER UN JETON —
E » dans une dalle et le nom des commandes réelles du jeu ; le message par jeu existe déjà
comme donnée (`ns_game_api`), il suffit de l'utiliser.

---

### R-12 — Le menu annonce de faux coûts de qualité

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=menu.png --frames=4 \
    --quality=high --scale=1.0 --width=1280 --height=720 --menu=2 --view=allee
```

**Ce qu'on voit** : la ligne QUALITE affiche « x1.56  HAUTE ».

**La preuve** : `room/room_menu.c:73-79` renvoie des constantes en dur —
`x0.17 / x0.42 / x1.00 / x1.56 / x3.92` — mesurées, dit le commentaire, « sur le rasteriseur
logiciel du conteneur de développement », avec la promesse que « c'est le rapport entre
paliers qui transporte ». Mesuré sur Metal, `--view=allee`, 1280 x 720, `--scale=1.0` :

| Palier | annoncé | **mesuré (M1 Pro)** | écart |
|---|---:|---:|---|
| potato | x0,17 | **x0,23** | +35 % |
| low | x0,42 | **x0,25** | **−40 %** |
| medium | x1,00 | x1,00 | — |
| high | x1,56 | **x1,67** | +7 % |
| ultra | x3,92 | **x5,59** | **+43 %** |

À 3200 x 1800, `high` coûte **x2,23**, pas x1,56.

Le cas le plus parlant est `low` : annoncé à 2,5 fois le prix de `potato`, il coûte en
réalité **1,06 fois** (14,1 ms contre 13,3). Sur un vrai GPU, **`low` est gratuit par
rapport à `potato`** — et c'est exactement l'information dont le joueur a besoin, puisque
c'est le seul couple de paliers qui tient 60 images/s (R-01).

**Pourquoi c'est un défaut** : le commentaire du code dit « c'est le seul chiffre qui aide
vraiment à choisir ». Un chiffre faux n'aide pas à choisir, il fait choisir de travers : un
joueur qui lit x0.17 contre x0.42 descend à `potato` et perd la couleur des caissons (R-02)
pour 6 % de gain.

**Détail aggravant** : ce chiffre est rendu à environ la moitié de la taille du reste de la
ligne et en gris sombre. C'est l'élément le moins lisible de tout le menu.

**Piste** : `room/room_menu.c:66-79`. Mesurer au premier lancement, ou au moins étiqueter
la valeur comme un ordre de grandeur. `docs/JOUER.md` porte la même affirmation
(« Ce sont les rapports qui se transposent ») et est démentie par la mesure.

---

### R-13 — Le palier `medium` saccade

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --bench --frames=60 \
    --quality=medium --scale=1.0 --width=1280 --height=720 --view=allee --no-hud
```

**Ce qu'on voit** : `57.1 ms en moyenne, min 46.3 ms, max 177.3 ms`.

**La preuve** : le maximum vaut **3,1 fois la moyenne**. À `--scale=0.8`, même palier :
37,5 ms de moyenne, **139,9 ms de pointe** (3,7 fois). Aux paliers voisins, la dispersion
est saine : `potato 1.0` fait 12,8–13,7 ms, `low 1.0` 13,4–15,3 ms, `high 1.0` 82,6–104,1 ms.
La pointe n'apparaît qu'à `medium`, et à toutes ses échelles.

**Pourquoi c'est un défaut** : c'est le palier par défaut. Une pointe à 177 ms est un
à-coup de six images perdues qui se sent dans les mains, pas un chiffre.

**Piste** : `medium` est le premier palier à activer le lancer de rayons (`docs/JOUER.md`).
L'accumulation temporelle ou le débruitage à-trous fait probablement un travail
intermittent — première image d'un cache, réallocation, ou un tampon rechargé par vagues.

---

### R-14 — Le panneau « PUSH » des toilettes est en miroir

**Gravité** : grave
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=toilettes.png --frames=8 \
    --quality=high --scale=1.0 --width=1280 --height=720 --view=toilettes --no-hud
```

**Ce qu'on voit** : sur la porte de cabine, l'étiquette se lit `H2Nq` — « PUSH » retourné.

**La preuve** : les quatre lettres sont dans l'ordre inverse et chaque glyphe est
lui-même retourné horizontalement. Ce n'est donc pas une porte vue de l'autre côté, c'est
une UV mise en miroir sur la face avant. On voit la même chose en arrière-plan de
`--view=billard`, sur le même panneau.

**Pourquoi c'est un défaut** : c'est le seul texte lisible de la pièce, et il est faux.
C'est aussi le genre de défaut qui trahit immédiatement le rendu de synthèse : une
normale ou une UV retournée quelque part dans la chaîne.

**Piste** : le matériau `panneau_toilettes` de `assets/scene/salle.room.json:874`, ou
l'orientation de l'instance qui le porte (une mise à l'échelle négative sur X retourne à la
fois la géométrie et la texture).

---

### R-15 — La moquette rouge traverse le sol des toilettes

**Gravité** : grave
**Où** : même capture que R-14, coin inférieur gauche.

**Ce qu'on voit** : au pied du mur carrelé, une bande de **moquette rouge de l'arcade**
apparaît sous le carrelage sombre des toilettes.

**La preuve** : `assets/scene/salle.room.json` déclare deux sols distincts, `sol_toilettes`
(ligne 50, 1611) et celui du hall. Ils se recouvrent au lieu de se joindre, et le rouge
ressort à la jonction avec le mur. Le sol des toilettes ne va pas jusqu'au mur, ou il est
posé quelques millimètres au-dessus sans plinthe.

**Pourquoi c'est un défaut** : c'est un trou dans le décor à hauteur de regard quand on
entre dans la pièce. Le joueur voit la pièce d'à côté à travers le sol.

**Piste** : l'emprise de `sol_toilettes` dans `salle.room.json:1611`, ou l'ajout d'une
plinthe qui masque la jonction.

---

## Gênant

### R-16 — Cinq jeux sur huit gaspillent 45 à 66 % de la dalle en noir

**Gravité** : gênant
**Où** : `--game=NOM --autoplay --warmup=25 --headless --screenshot=…` pour chacun.

**Ce qu'on voit** : l'aire de jeu occupe une bande centrale, le reste est noir.

**La preuve**, mesurée sur les captures 1280 x 720 :

| Jeu | largeur utile | noir perdu | luminance médiane | % sous 16 |
|---|---:|---:|---:|---:|
| Shooter | 34 % | **66 %** | 5 | 98,1 % |
| Tetris | 38 % | 62 % | 13 | 76,0 % |
| Pac-Man | 52 % | 48 % | 5 | 73,6 % |
| Piano | 55 % | 45 % | 19 | 45,1 % |
| Asteroid | 90 % | 10 % | 1 | **98,2 %** |
| Démineur | 65 % | 35 % | 25 | 0,1 % |
| Flappy | 100 % | 0 % | 111 | 1,7 % |
| Snake | 100 % | 0 % | 162 | 0,3 % |

**Pourquoi c'est un défaut** : la dalle d'une borne ne fait déjà que 300 pixels à l'écran
(cf. le commentaire de `room_hud.c:161`). En perdre les deux tiers en noir laisse cent
pixels utiles. Et le score, systématiquement collé au coin supérieur gauche du cadre
complet, se retrouve à six cents pixels de l'action — hors de la dalle en pratique.

**Piste** : chaque jeu déclare sa surface virtuelle (`games/*/…​.h`). Les jeux en portrait
(Shooter, Tetris, Piano) devraient soit remplir la hauteur et centrer un décor latéral,
soit être posés dans une dalle verticale. Le score doit rejoindre l'aire de jeu.

---

### R-17 — Le billard : rails feuilletés, texture étirée

**Gravité** : gênant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=billard.png --frames=8 \
    --quality=high --scale=1.0 --width=1280 --height=720 --view=billard --no-hud
```

**Ce qu'on voit** : les bandes de la table et ses pieds sont rayés de strates
horizontales, comme du contreplaqué feuilleté vu par la tranche.

**La preuve** : le motif est une répétition régulière de fines bandes claires et sombres
parallèles à la longueur de chaque pièce, sur les quatre bandes et sur les deux pieds
visibles. C'est la signature d'une projection planaire sur un volume : la texture
`billard_table.jpg` (`salle.room.json:831`) est étirée dans un axe et écrasée dans
l'autre.

**Pourquoi c'est un défaut** : le billard est un des trois « lieux » que la salle déclare,
un endroit où le joueur est censé vouloir aller. Le meuble qui le désigne est le seul objet
de la pièce dont la matière ne se lit pas.

**Piste** : le matériau `billard_bois` (`salle.room.json:836`) et le mode de pavage
`uvMetres` déclaré par matériau, dont l'en-tête du fichier (ligne 28) dit qu'il est justement
là pour ça.

---

### R-18 — La suspension du billard est noire par en dessous

**Gravité** : gênant
**Où** : même capture que R-17.

**Ce qu'on voit** : la suspension pend au-dessus de la table. Son abat-jour tronconique est
**noir absolu**, sans ampoule visible dedans, et il ne projette aucun cône de lumière sur
le tapis.

**La preuve** : `salle.room.json:487-504` déclare un matériau `billard_ampoule` et un
commentaire qui dit précisément pourquoi il existe : « Une source sans objet visible est ce
que le plan s'interdit explicitement : c'est ce qui fait qu'une pièce paraît éclairée par
magie. Voici l'objet. » L'objet est là mais **ne se voit pas** : depuis un point de vue à
hauteur d'œil, sous le plan de l'abat-jour, on voit l'intérieur du cône et il est noir.
La table est éclairée ; la lampe, non.

**Pourquoi c'est un défaut** : le défaut que la correction visait est toujours là, exactement
sous la forme décrite. Le coin billard est par ailleurs le plus sombre de la salle : moyenne
30,9, **médiane 19, 40,1 % des pixels sous 16**.

**Piste** : `salle.room.json:487`. Vérifier que l'ampoule est bien sous le bord de
l'abat-jour et non dedans, et que son émissif n'est pas écrasé.

---

### R-19 — Le tapis néon : des motifs de un mètre et demi

**Gravité** : gênant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=pied.png --frames=6 \
    --quality=high --scale=1.0 --width=1280 --height=720 --no-hud \
    --camera=free --pos=2.0,0.9,2.0 --yaw=-25 --pitch=-14
```

**Ce qu'on voit** : un tapis noir semé d'icônes néon — manette, cerises, Pac-Man, borne.
Vus depuis 90 cm, une manette de jeu mesure **environ un mètre cinquante** de large et une
paire de cerises **deux mètres**.

**La preuve** : dans le même cadre, la borne Snake fait 72 cm de large (cote déclarée,
`salle.room.json:_bornes`). Les icônes du tapis atteignent le double de cette largeur.
L'échelle du motif est donc au moins cinq fois trop grande.

**Pourquoi c'est un défaut** : une échelle fausse se voit avant tout le reste, et c'est ici
la surface sur laquelle le joueur marche — la plus grande du champ quand il regarde ses
pieds. Le tapis se lit comme une projection vidéo au sol, pas comme de la moquette.

**Piste** : le `uvMetres` du matériau du tapis dans `assets/scene/salle.room.json`. À
titre de comparaison, la moquette du hall boucle correctement tous les 1,5 m (en-tête du
fichier, ligne 28).

---

### R-20 — De la lumière fuit sous les caissons

**Gravité** : gênant
**Où** : même capture que R-19.

**Ce qu'on voit** : une **bande jaune orangée vive** court le long du bas de la borne
Snake, entre le caisson et le sol, alors qu'aucune source ne se trouve à cette hauteur.

**La preuve** : la bande est plus claire que le sol qui l'entoure et que le caisson qui la
surplombe. Un objet posé au sol devrait produire l'inverse — une ombre de contact. Au
palier `high`, avec le lancer de rayons actif, la même image montre les autres ombres de
contact correctement.

**Pourquoi c'est un défaut** : cela donne l'impression que la borne lévite légèrement, sur
tous les caissons à la fois.

**Piste** : le décalage d'ombre (bias) du compute d'ombres, ou la base des bornes qui ne
descend pas tout à fait au niveau zéro dans `salle.room.json`.

---

### R-21 — Les boutons sont des pastilles plates, la boule du manche est facettée

**Gravité** : gênant
**Où** : même capture que R-19, et
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=borne1.png --frames=6 \
    --quality=high --scale=1.0 --width=1280 --height=720 \
    --play-at=borne_arcade_1 --autoplay --warmup=14 --no-hud
```

**Ce qu'on voit** : vus de trois quarts à quatre-vingt-dix centimètres, les quatre boutons
n'ont **aucun corps cylindrique, aucun jonc, aucune ombre propre** : ce sont des ellipses
peintes sur le panneau. La boule du manche montre ses facettes en silhouette.

**La preuve** : à cet angle, un bouton d'arcade de 24 mm de haut devrait présenter un flanc
visible sur environ un tiers de sa hauteur apparente. Il n'y en a aucun. Quant à la boule,
son contour n'est pas rond mais polygonal — on compte les segments à l'œil.

**Pourquoi c'est un défaut** : ce sont **les deux seuls objets que le joueur touche**, les
plus proches de ses yeux pendant toute une partie. Le journal des versions (`ddb29a4`,
B25d) fait exactement ce raisonnement pour justifier le passage du manche à `geo_revolve` ;
il s'est arrêté à la boule, et la boule n'a pas assez de segments.

**Piste** : les boutons méritent le même traitement que le manche (`geo_revolve`, un
cylindre bas plus une calotte). Pour la boule, monter le nombre de segments de révolution.

---

### R-22 — Les mains : des palettes sans doigts, sur aucune commande

**Gravité** : gênant
**Où** : `--play-at=borne_arcade_1 --autoplay --warmup=14` (avec ou sans `--no-hud`).

**Ce qu'on voit** : deux formes pâles au bas du cadre. Chacune est un pavé arrondi avec
**trois rainures droites**, sans pouce, sans articulation, sans poignet. La main gauche
flotte au-dessus du panneau sans rien tenir ; la main droite a un « doigt » qui
**traverse le bouton rouge**.

**La preuve** : sur `borne_arcade_2` comme sur `borne_arcade_1`, aucune des deux mains
n'est en contact avec le manche ni avec un bouton. Le doigt de droite et le disque rouge
occupent les mêmes pixels avec le doigt dessus.

**Pourquoi c'est un défaut** : le journal des versions consacre deux entrées entières à ces
mains (`7e7eb52` sur leur taille, la suivante sur leur peau) et conclut par une mesure au
pixel de la couleur. La couleur est en effet corrigée. Ce qui manque n'a jamais été
regardé : **la forme**, et le fait que l'IK ne pose la main sur aucune cible réelle.

**Piste** : `room/room_viewmodel.c` et la cible de l'IK. Une main à trois rainures est un
maillage de remplacement ; le problème du contact est ailleurs, dans le choix de la cible.

---

### R-23 — Colonnes et bar : des aplats saturés sans atténuation

**Gravité** : gênant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=entree.png --frames=8 \
    --quality=high --scale=1.0 --width=1280 --height=720 --view=entree --no-hud
./build/macos-universal/bin/nineteen --headless --screenshot=rasol.png --frames=6 \
    --quality=high --scale=1.0 --width=1280 --height=720 --no-hud \
    --camera=free --pos=0,0.06,0 --yaw=90 --pitch=0
```

**Ce qu'on voit** : les colonnes rouges sont d'un rouge quasi pur, uniforme du sol au
plafond sur trois mètres. Le comptoir du bar porte son motif de triangles à pleine
saturation sur **plus de six mètres de profondeur**, l'extrémité lointaine aussi claire
que l'extrémité proche.

**La preuve** : sur la capture au ras du sol, le comptoir traverse le cadre en diagonale ;
la différence de valeur entre son point le plus proche et son point le plus éloigné est
visuellement nulle, alors que le mur derrière lui, éclairé par les mêmes sources, présente
un dégradé net. Un éclairage en `intensity / (4 pi d^2)` — la formule que
`salle.room.json:1106` revendique — ne peut pas produire cela.

**Pourquoi c'est un défaut** : c'est la signature exacte du « peint dans les textures » que
la reconstruction dit avoir supprimé. Deux des trois grands volumes du hall (les colonnes,
le bar) ne participent pas à l'éclairage de la pièce.

**Piste** : les matériaux du comptoir et des colonnes. Un albédo trop élevé, ou une
composante émissive non nulle, suffit à écraser l'atténuation — c'est exactement le défaut
que `salle.room.json:105` documente pour le plafond de 2020 (`Ke 1 1 1`).

---

### R-24 — Pac-Man est un carré jaune

**Gravité** : gênant
**Où** : `--game=dedale --autoplay --warmup=25`

**Ce qu'on voit** : le héros est un carré jaune plein. Les fantômes sont des carrés :
**deux bleus identiques**, un blanc plus petit, deux orange. Il n'y a **aucune
super-pastille** — toutes les pastilles ont la même taille.

**La preuve** : les six mobiles de l'image sont des rectangles pleins de 40 px de côté
(35 pour le blanc). Aucune bouche, aucun œil, aucune direction lisible. Le labyrinthe est
en blocs bleus pleins et non en double filet, ce qui inverse la lecture habituelle
(on cherche le chemin, on voit le mur).

**Pourquoi c'est un défaut** : le journal des versions consacre un paragraphe entier à la
**connexité** du labyrinthe et un autre à la **vitesse** des fantômes — deux corrections
justes et bien mesurées. Personne n'a regardé si on distinguait un fantôme d'un autre. À
quatre fantômes de deux couleurs, la poursuite ne se lit pas.

**Piste** : `games/pacman/`. Quatre couleurs distinctes et une forme de fantôme sont un
sprite chacun ; les super-pastilles sont un rayon différent.

---

### R-25 — Tetris : quatre blocs posés rendus trente pour cent plus sombres

**Gravité** : gênant
**Où** : `--game=aplomb --autoplay --warmup=25`

**Ce qu'on voit** : dans la colonne de droite du puits, une pile de quatre blocs
**rouge bordeaux très sombre** repose sur un bloc violet. Tous les autres blocs posés de
l'image — vert, violet, orange, et un rouge sur la ligne du bas — sont à pleine luminosité.
La pièce rouge qui tombe en haut du puits est, elle aussi, à pleine luminosité.

**La preuve** : deux blocs de la même couleur (le rouge vif de la ligne du bas et le rouge
sombre de la colonne) coexistent dans le même cadre, sous le même éclairage — il n'y en a
pas, c'est de la 2D — avec un écart de valeur d'environ 3 pour 1.

**Pourquoi c'est un défaut** : le joueur ne peut pas savoir si ces quatre cases sont
occupées ou non. Dans un jeu où toute la décision porte sur la forme du tas, une case dont
on ne sait pas si elle est pleine est un défaut de règle, pas de décor.

**Piste** : `games/tetris/`, la table de couleurs des blocs posés — soit un index qui
déborde, soit un état « ligne en train de disparaître » qui reste collé.

---

### R-26 — Toilettes : miroir flottant, lavabo sans cuve, robinet en double

**Gravité** : gênant
**Où** : `--view=toilettes`

**Ce qu'on voit** : trois objets, trois problèmes.
1. Le miroir est une **plaque de marbre blanc** posée devant le mur, avec un écart visible
   entre elle et le mur, et un plan qui n'est pas parallèle à celui du mur.
2. Le lavabo est un **pavé** avec un dessus plat : ni cuve, ni bonde, ni égouttoir.
3. Le robinet apparaît **deux fois** — un petit objet sombre posé sur le plan, et une
   tige noire rayée qui flotte derrière et au-dessus du meuble, sans le toucher.

**La preuve** : les trois sont dans le même cadre. Le placard du lavabo porte par ailleurs
une veine de marbre agrandie au point de ne plus se lire comme du marbre.

**Pourquoi c'est un défaut** : c'est la pièce que `docs/JOUER.md` met en avant pour son
écho propre, donc une pièce où l'on invite le joueur à entrer. Il y trouve trois objets
qu'il ne reconnaît pas.

**Piste** : `assets/scene/salle.room.json`, les props des toilettes. Un miroir a besoin
d'un cadre et d'un plan coplanaire au mur ; un lavabo a besoin d'une cuve creusée, ce que
`geo_revolve` sait faire depuis `ddb29a4`.

---

### R-27 — Les toilettes sont cinq fois plus claires que le hall

**Gravité** : gênant
**Où** : les huit vues nommées, en lisant la ligne de luminance.

**La preuve** :

| Vue | moyenne | **médiane** | % sous 16 |
|---|---:|---:|---:|
| toilettes | 91,5 | **99** | 10,5 % |
| bar | 62,8 | 58 | 28,0 % |
| plafond | 68,8 | 58 | 15,9 % |
| allee | 70,4 | 48 | 22,1 % |
| entree | 44,4 | 35 | 13,0 % |
| classement | 45,5 | 28 | 32,4 % |
| **borne** | 56,7 | **19** | **42,8 %** |
| **billard** | 30,9 | **19** | **40,1 %** |

**Pourquoi c'est un défaut** : franchir une porte fait passer la médiane de 19 à 99. Ce
n'est pas un contraste voulu entre deux ambiances, c'est un saut d'exposition de plus de
deux diaphragmes qui éblouit. Et à l'inverse, les deux endroits où le joueur va vraiment —
devant une borne, au billard — sont ceux où **quatre pixels sur dix sont sous 16**.

**Piste** : les intensités déclarées dans `salle.room.json`, section `_luminaires`. Le
commentaire de la ligne 154 dit qu'un essai de baisse globale a été abandonné parce qu'il
noircissait l'allée — la bonne action n'est pas globale, elle est **par pièce** : baisser
`plafonnier_toilettes` et monter `suspension_billard`.

---

### R-28 — Le pavage du crépi mural est visible partout

**Gravité** : gênant
**Où** : `--view=classement --no-hud`, et la capture au ras du sol de R-23.

**Ce qu'on voit** : le crépi granuleux beige des murs présente un **motif répété
identifiable** : les mêmes amas de graviers reviennent à intervalle régulier, formant un
quadrillage perceptible sur toute la longueur du mur.

**La preuve** : sur un mur pris de face et sur toute la largeur du cadre, on peut suivre le
même groupe de taches sombres d'une répétition à l'autre. Le mur mesure vingt-deux mètres,
la texture y boucle beaucoup plus souvent que ne le suggère la taille apparente des
graviers.

**Pourquoi c'est un défaut** : c'est la surface la plus étendue de la salle. Un pavage
visible est l'un des trois ou quatre indices qui font dire « c'est de la synthèse » avant
même qu'on ait regardé quoi que ce soit d'autre.

**Piste** : le matériau du crépi et son `uvMetres`. Une variation à grande échelle
(deuxième octave, ou décalage aléatoire par tuile) coûte peu et casse le réseau.

---

### R-29 — Le « 1/4 » du classement est hors de la zone sûre et déchiré

**Gravité** : gênant
**Où** : `--view=classement`, coin supérieur droit de la dalle, agrandi 8 fois.

**Ce qu'on voit** : l'indicateur de page est mangé par des stries verticales et se lit
plutôt `L/4` que `1/4`.

**La preuve** : `room/room_hud.c` place ce texte à `w * 0.94f`, alors que le même fichier
place ses deux filets horizontaux à `w * 0.08f` sur `w * 0.84f` — donc dans une zone sûre
de 8 % à 92 %. Le commentaire de la fonction dit lui-même : « la dalle est déformée en
barillet et son cadre déborde, donc les bords ne se voient pas ». L'indicateur est placé en
dehors de la marge que le fichier vient de définir.

**Pourquoi c'est un défaut** : le tableau tourne tout seul toutes les six secondes ; ce
« 1/4 » est la seule chose qui dit au joueur qu'il y a une suite et qu'il n'a pas rêvé.
C'est aussi la seule chose illisible de l'écran.

**Piste** : `room/room_hud.c`, la ligne `centred(s, w * 0.94f, 16.0f * u, …)` → `0.90f`.

---

### R-30 — La dalle a un bord en escalier et les filets se cassent

**Gravité** : gênant
**Où** : `--view=classement`, agrandissement 8 fois de la région (1400, 620).

**Ce qu'on voit** : le bord droit de la dalle bleue n'est pas une ligne mais un
**escalier** à marches d'une trentaine de pixels. Le filet horizontal du haut est rendu en
**deux segments à deux hauteurs différentes**, et il s'arrête avant sa largeur déclarée. Le
`D` de `S.HARD` a un morceau manquant sur son flanc droit.

**La preuve** : le filet est déclaré comme un seul rectangle
(`ns_sprite_rect(s, w*0.08f, 46.0f*u, w*0.84f, 2.0f*u, rule)`). Il ne peut pas se casser en
deux tronçons décalés verticalement à moins d'un sous-échantillonnage de la texture de
dalle suivi d'une déformation en barillet, sans aucun lissage.

**Pourquoi c'est un défaut** : en mouvement, un bord en escalier de trente pixels grouille.
Un filet cassé se lit comme une erreur d'affichage plutôt que comme un décor.

**Piste** : la résolution de la texture de dalle et le filtrage à l'échantillonnage du
barillet dans le shader d'écran de borne. Un filtrage bilinéaire sur la dalle, ou un
suréchantillonnage x2, coûte peu à cet endroit.

---

### R-31 — La poussière passe devant la dalle et se lit comme des traces de doigts

**Gravité** : gênant
**Où** : `--play-at=borne_arcade_2 --autoplay --warmup=12`

**Ce qu'on voit** : cinq à huit **taches blanches floues** de douze à vingt pixels flottent
devant l'écran du jeu, dont une en plein milieu du puits de Tetris et deux sur le marquee.

**La preuve** : le journal indique `2549 grains dans 3 zone(s)`. À trente centimètres de la
dalle, un grain de poussière occupe assez de pixels pour lire comme une salissure sur le
verre plutôt que comme une particule en suspension dans la pièce.

**Pourquoi c'est un défaut** : la poussière est une bonne idée qui vend l'atmosphère quand
on traverse la salle. Elle devient une gêne quand elle se pose entre l'œil et l'information
du jeu — et le joueur ne peut pas l'attribuer à la pièce, il croit que sa borne est sale.

**Piste** : atténuer ou supprimer les particules dans le tronc de cône entre la caméra et
la dalle quand une partie est en cours (`engine/fx/ns_particles.c` + l'état de partie de
`room/main.c`).

---

### R-32 — Le menu cache ce qu'il fait régler, et « QUITTER » colle à « REPRENDRE »

**Gravité** : gênant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=menu.png --frames=4 \
    --quality=high --scale=1.0 --width=1280 --height=720 --menu=2 --view=allee
```

**Ce qu'on voit** : un pavé opaque couvre les **55 % centraux** de l'image. La salle reste
visible sur les bords, et **assombrie** : luminance médiane 16 contre 48 pour la même vue
sans menu.

**La preuve** : `docs/JOUER.md` promet « La salle continue de vivre derrière le voile —
c'est ce qui permet de juger un réglage pendant qu'on le change ». Les deux réglages qui
demandent précisément d'être jugés à l'œil, QUALITE et LUMINOSITE, sont les deux premières
lignes du pavé — placées au-dessus de la partie de la salle qu'elles cachent. Et juger une
luminosité à travers un voile qui divise la médiane par trois n'est pas possible.

**Deux autres points sur le même écran** :
- `QUITTER LE JEU` est la ligne immédiatement sous `REPRENDRE`, sans séparateur et sans
  confirmation. Une flèche bas et Entrée depuis « reprendre » ferment le jeu et perdent la
  partie en cours.
- `docs/JOUER.md` annonce « échelle de rendu (avec le pourcentage de pixels économisé) ».
  Le menu affiche `1.00`, sans pourcentage.

**Piste** : `room/room_menu.c`. Réduire le pavé et le décaler d'un côté, alléger le voile,
et séparer `QUITTER LE JEU` du reste.

---

### R-33 — `--play-at` : des noms opaques, et un nom faux qui échoue en silence

**Gravité** : gênant
**Où** :
```sh
./build/macos-universal/bin/nineteen --headless --screenshot=x.png --frames=6 \
    --play-at=flappy --autoplay --warmup=12 ; echo "code=$?"
```

**Ce qu'on voit** : le binaire imprime `borne inconnue : flappy` sur la sortie d'erreur,
liste dix-neuf noms de la forme `borne_arcade_1 … borne_arcade_18 borne_classement`,
**puis continue et écrit une capture** qui ressemble à une réussite.

**La preuve** : les captures produites par `--play-at=flappy`, `=tetris`, `=asteroid` et
`=piano` ont la **même ligne de luminance au dixième près** : `moyenne 79.4, médiane 75,
8.7 % sous 16`. Ce sont quatre fois la même image, celle du point de vue joueur par défaut.
Le code de sortie n'est pas non nul.

Par ailleurs, aucun des dix-neuf noms ne dit à quoi la borne joue. Pour lancer Flappy en 3D
il faut découvrir que c'est `borne_arcade_1` (facile) ou `borne_arcade_10` (difficile) —
ce que seul un parcours des dix-huit permet d'apprendre.

**Pourquoi c'est un défaut** : un outil qui produit un fichier plausible après avoir refusé
son argument est pire qu'un outil qui échoue. Un script de capture ne s'en aperçoit jamais.

**Piste** : `room/main.c:1167-1176`. Sortir en erreur quand `pick` est nul, et accepter
aussi un nom de jeu (avec un suffixe de difficulté optionnel) en plus du nom de borne.

---

### R-34 — Snake : ni tête ni queue identifiable, score coupé par le bord

**Gravité** : gênant
**Où** : `--game=snake --autoplay --warmup=25`

**Ce qu'on voit** : le serpent est un tube vert lisse. Une extrémité porte une boule orange,
l'autre est arrondie et verte. **Aucune des deux n'a d'yeux**, et rien ne dit laquelle est
la tête. Le score « 70 », en haut au centre, est **coupé par le bord haut du cadre** et
chevauche la bordure brune.

**La preuve** : dans le même cadre, quatorze petites fleurs à quatre pétales (jaunes,
blanches, roses) sont dispersées, plus une boule rouge et un coffre au trésor. Rien ne
distingue une fleur décorative d'un fruit à manger, alors que le mode « hard » du jeu
consiste précisément à choisir **lesquels laisser pourrir** (`docs/JOUER.md`).

**Pourquoi c'est un défaut** : on pilote la tête. Ne pas savoir où elle est, dans un jeu où
l'on tourne, est une gêne de premier ordre. Et le mode qui fait l'intérêt du Snake « hard »
repose sur une distinction visuelle qui n'existe pas.

**Piste** : `games/snake/`. Deux yeux sur la tête, et deux états visuels pour un fruit
(frais / en train de pourrir).

---

### R-35 — Une dalle de borne n'affiche que `[BETA]`

**Gravité** : gênant
**Où** : `--play-at=borne_arcade_2 --autoplay --warmup=12` — la borne visible à gauche du
cadre.

**Ce qu'on voit** : sa dalle est un rectangle bleu uni, sans jeu, sans écran d'attente. Le
seul contenu est une petite étiquette rouge `[BETA]` dans le coin inférieur droit.

**La preuve** : la même chose apparaît sur la borne violette de la capture
`--pos=-5.5,1.4,2.0 --yaw=200`, dont la dalle est noire avec un texte minuscule illisible,
alors que ses deux voisines affichent Snake et Asteroid.

**Pourquoi c'est un défaut** : une salle de dix-neuf bornes dont certaines n'affichent rien
se lit comme une salle en panne. Le journal des versions affirme que « les dix-neuf bornes
de la salle jouent toutes » (`docs/JOUER.md`), et c'est vrai à l'exécution — j'ai vérifié
les dix-huit, toutes rendent un jeu quand on s'y met. Mais **en attente**, certaines ne
montrent rien.

**Piste** : l'écran d'attente (`attract`) de ces bornes, et la texture de dalle qui porte
encore un marqueur `[BETA]`.

---

## Cosmétique

### R-36 — Deux affiches identiques à deux mètres l'une de l'autre

**Gravité** : cosmétique
**Où** : `--camera=free --pos=-5.5,1.4,2.0 --yaw=200 --pitch=-25`

**Ce qu'on voit** : deux affiches « ARCADE Armageddon » **rigoureusement identiques**,
côte à côte sur le même mur, séparées d'environ deux mètres. Une troisième apparaît dans
`--view=billard`.

**Pourquoi c'est un défaut** : rien ne trahit plus vite un décor dupliqué qu'une affiche
répétée dans le même champ. Il existe d'autres affiches dans la salle (Video Pinball,
Hawkins Palace, une affiche Nintendo au bar) : le stock existe, il est mal réparti.

**Piste** : l'affectation des textures d'affiche dans `assets/scene/salle.room.json`.

---

### R-37 — Le téléviseur du bar est noir absolu, un badge flotte devant

**Gravité** : cosmétique
**Où** : `--view=bar --no-hud`

**Ce qu'on voit** : sous l'enseigne NINETEEN, un grand rectangle **parfaitement noir**,
sans cadre, sans reflet, sans contenu — un écran éteint qui se lit comme un trou dans le
mur. Devant son bord droit, une **pastille bleue à étoile blanche** flotte sans support ni
ombre.

**Pourquoi c'est un défaut** : dans une salle où dix-neuf écrans sont allumés, le vingtième,
au-dessus du bar, est éteint. Et un objet qui flotte est le premier détail que l'œil
attrape.

**Piste** : `assets/scene/salle.room.json`. Le téléviseur mérite au minimum un cadre et un
reflet ; le badge mérite d'être fixé au mur ou supprimé.

---

### R-38 — `assets/scene/cabinets.json` est périmé et contredit la salle

**Gravité** : cosmétique
**Où** :
```sh
python3 -c "import json;print(json.load(open('assets/scene/cabinets.json'))['cabinets'][0])"
for i in 1 2 3; do ./build/macos-universal/bin/nineteen --headless --frames=1 \
    --width=320 --height=180 --play-at=borne_arcade_$i --autoplay --warmup=0 2>&1 \
    | grep -oE "borne « [^»]*» \([a-z]+, [a-z]+\)"; done
```

**Ce qu'on voit** : `cabinets.json` déclare `slot 1 → flappy hard`. La salle rend
`borne_arcade_1 → flappy normal`, `borne_arcade_2 → tetris normal`,
`borne_arcade_3 → shooter normal`. Aucun décalage d'indice ne réconcilie les deux listes.

**La preuve** : `engine/scene/ns_scene.c:1083-1086` ne charge `cabinets.json` que
`if (!out->has_declared_cabinets)`. La salle reconstruite déclare ses dix-neuf bornes dans
`salle.room.json:1848`, donc **`cabinets.json` n'est plus lu du tout** pour la salle par
défaut. Il reste malgré tout copié dans `build/macos-universal/assets/scene/`.

**Pourquoi c'est un défaut** : un fichier livré, commenté sur douze lignes, qui décrit
faussement l'affectation des jeux. Le prochain qui voudra changer une borne le modifiera
et ne comprendra pas pourquoi rien ne bouge.

**Piste** : soit le supprimer et déplacer son commentaire (qui est bon) vers
`salle.room.json`, soit le réserver explicitement au chemin `--room=legacy` en le nommant
ainsi.

---

### R-39 — Le README annonce quinze bornes et 64 sources ; c'est dix-neuf et 47

**Gravité** : cosmétique
**Où** : `README.md` lignes « Quinze bornes dans un hall » et « 64 sources déduites du
modèle », contre :
```
main.c — scène déclarée : 19 borne(s), 6 lieu(x)
main.c — capture : … 47 lumières         (salle generated)
main.c — capture : … 64 lumières         (--room=legacy)
```

**Ce qu'on voit** : le README décrit la salle **legacy** (15 bornes, 64 sources) alors que
le jeu démarre sur la salle **generated** (19 bornes, 47 sources). `docs/JOUER.md` dit bien
dix-neuf.

**Pourquoi c'est un défaut** : le README est la première page. Deux chiffres faux dès le
deuxième paragraphe fragilisent la confiance dans tout le reste du document, qui est par
ailleurs remarquablement précis.

**Piste** : `README.md`. Au passage, `docs/JOUER.md` donne deux chemins différents pour
`scores.txt` (« `~/.local/share/recognizer/Nineteen/` » en haut, « `~/.local/share/nineteen/` »
dans la section scores) ; le bon est le premier. Et sa liste de points de vue nommés
(« allee, bar, billard, orbite ») en oublie cinq : la scène en déclare neuf.

---

### R-40 — Une ligne de journal par point marqué

**Gravité** : cosmétique
**Où** : n'importe quelle partie longue ; visible en clair dans le blocage de R-03.

**Ce qu'on voit** : `main.c:1864` émet une ligne INFO à **chaque point marqué** :
```
[  20.540] INFO  main.c:1864 — FLAPPY BIRD : 21
[  22.207] INFO  main.c:1864 — FLAPPY BIRD : 22
```
Sur cinq minutes de Flappy, 178 lignes. Un joueur qui fait un gros score sur Snake ou
Tetris en produit des milliers dans `nineteen.log`.

**Pourquoi c'est un défaut** : le journal sert à diagnostiquer. Noyé sous le score, il ne
sert plus à rien — et c'est justement le fichier qu'on demande à un joueur de joindre à un
rapport de bug.

**Piste** : `room/main.c:1864`, passer en `NS_DEBUG` ou ne journaliser que le score final.

---

## Ce que j'ai vérifié et qui va bien

Ces points ont été cherchés, éprouvés, et tiennent.

- **La suite de tests.** `ctest --preset macos-universal` : **32 sur 32 en 10,6 s**, y
  compris le test de rendu qui crée un vrai périphérique GPU. Aucun test lent, aucun test
  instable sur trois exécutions.
- **Le refus des mauvaises entrées.** `--view=zzzz` répond `point de vue inconnu : zzzz`.
  Les cinq tests `refus-*` de la suite montrent que le générateur de géométrie refuse
  activement ses cas dégénérés. C'est ce qui rend R-33 (`--play-at` qui accepte en
  silence) d'autant plus visible : le reste du binaire fait mieux.
- **Aucun NaN, aucune image noire, aucun artefact de compression.** Les sept cibles
  intermédiaires (`--debug=albedo|normal|emissive|depth|visibility|hdr|bloom`) sortent
  toutes avec une luminance cohérente et sans valeur aberrante — `normal` à 121,9 de
  moyenne et 0,0 % sous 16, `depth` à 93,4 avec 0,0 % aux deux bouts. La chaîne BC5/BC1
  ne laisse aucune trace visible : ni banding vert sur les normales, ni blocs sur l'ORM.
- **La géométrie tient de partout.** Vues du dessus, du ras du sol, de sous le plancher
  (`--pos=0,-1.0,0`), des quatre coins, depuis l'extérieur du bâtiment : aucun trou dans le
  décor, aucun z-fighting, aucun triangle retourné, aucune interpénétration franche.
  Le seul défaut trouvé de cette famille est R-15, à une jonction de sols.
- **Le plafond.** C'est la plus belle surface de la salle et celle dont la correction a le
  mieux tenu : trame en dalles de 0,60 m, motif art déco lisible, luminaires intégrés à la
  trame, et une médiane de 58 avec seulement 15,9 % sous 16. La note de
  `salle.room.json:106` — qui corrige publiquement une explication fausse qu'elle avait
  elle-même donnée — est le meilleur commentaire du dépôt.
- **Le tri des lots.** L'élimination par frustum fonctionne : 17 lots dessinés sur 285
  depuis le bar, 10 sur 285 depuis l'extérieur, 232 sur 285 depuis l'entrée. Les chiffres
  suivent le contenu du champ.
- **Le mode `--rejouer` refuse ce qu'il doit refuser.** Les tests `replay` et `rejeu-cli`
  passent, et la commande refuse un fichier absent et un fichier qui n'est pas un journal.
  Le défaut R-05 est en amont, dans l'écriture, pas dans la lecture.
- **Le déterminisme.** `--game=X --autoplay --warmup=25` rend deux fois exactement le même
  score et la même ligne de luminance sur les huit jeux. C'est la propriété la plus
  difficile de la liste et elle est acquise.
- **La sérigraphie du panneau de commande existe et se voit.** Le losangé annoncé par
  `bae8b44` est bien là sur les dix-neuf bornes, à un pas court, avec ses deux filets. C'est
  une vraie amélioration par rapport à l'aplat bordeaux qu'il remplace ; il lui manque
  seulement du contraste sur les caissons clairs.
- **Le son est déclaré et chargé.** `19 borne(s) sonorisée(s), ambiance en place`, sept
  fichiers dont 350 secondes de boucles de borne, et trois zones de réverbération déclarées
  dans la scène. Non évaluable en capture, mais rien n'échoue au chargement.
- **Aucune connexion réseau sans qu'on la demande.** `réseau : aucun serveur configuré, le
  classement restera local` à chaque lancement, sur une trentaine de lancements. La
  promesse centrale de `docs/JOUER.md` tient.

---

## Ce que je ferais dans l'ordre

1. **R-01 + R-02 ensemble.** Ce sont les deux faces d'un même problème : le palier qui a la
   bonne image n'est pas jouable, et le palier jouable n'a pas la bonne image. Un repli
   d'ombre à `low` réglerait les deux, et `low` deviendrait le bon défaut.
2. **R-03.** Une ligne. Elle débloque toute vérification automatisée des mini-jeux.
3. **R-06 et R-07.** Les jeux sont l'objet du logiciel, et deux d'entre eux ne se lisent
   pas dans la dalle où le projet promet de les jouer.
4. **R-04.** Le classement est la borne qui donne envie de rejouer, et elle est illisible
   dans son état par défaut.
5. **R-10.** Un outil de capture qui écrit dans les données du joueur, c'est à corriger
   avant de faire une seule capture de plus.
