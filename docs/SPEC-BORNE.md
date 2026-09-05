# La borne : spécification chiffrée

Ce document **tranche**. Il ne propose pas, il n'énumère pas des options : chaque section se
termine par des nombres que `assets/blender/borne.py` doit produire, au millimètre.

Le propriétaire a relevé deux défauts en jouant : « le layout des boutons de la borne d'arcade n'a
pas de sens » et « la hauteur du personnage ne permet pas d'être bien positionné à hauteur des
bornes d'arcade ». Les deux sont réels. Ce ne sont pas le même défaut, ils n'ont pas la même cause,
et un seul des deux se corrige dans la géométrie.

Comme `DESIGN-SALLE.md`, ce document ne contient aucune affirmation qui ne soit rattachée à un
fichier, une ligne, un nombre ou une pièce de matériel réelle. Là où je ne suis pas sûr d'une cote
de matériel, **c'est écrit** — voir §8. Aucun chiffre inventé.

## Comment tout ici a été obtenu

Les cotes du modèle sont lues dans `assets/blender/borne.py` et **vérifiées dans le glTF exporté**,
qui est la seule chose que le moteur consomme :

```sh
python3 -c "import json; d=json.load(open('assets/models/borne/borne.gltf')); \
  print([(m['name']) for m in d['materials']])"   # + les min/max de chaque primitive
```

Les images sont des captures du binaire existant, sur l'ancre que la salle déclare elle-même :

```sh
./build/macos-universal/bin/nineteen --headless --screenshot=/tmp/x.png --frames=8 \
    --quality=high --width=1280 --height=720 --no-hud \
    --pos=-2.778,1.70,-1.278 --yaw=90 --pitch=-34     # la capture « borne »
```

Les angles de regard sont calculés depuis `borne.ancres.json` et `tools/roomgen.c:1586`
(`player_anchor = panel_centre + 0,46 m`), avec l'œil à 1,70 m (`room/room_camera.c:39`).

---

# 1. Ce qui est JUSTE, et qu'il ne faut pas « corriger »

C'est la première section parce que la tentation, devant un rapport de défauts, est de tout
rouvrir. **Les hauteurs de la borne sont bonnes.** Toutes.

| Cote | Modèle | Matériel réel | Verdict |
|---|---|---|---|
| Hauteur hors tout | 1,88 m (74") | un upright classique fait 70 à 72" (1,78–1,83 m) | 2 à 4 pouces de trop — **acceptable**, c'est une contrainte de salle assumée |
| Largeur | 0,72 m (28,3") | un upright **un joueur** fait 25 à 27" | légèrement large : c'est exactement ce qu'il faut pour UN panneau généreux |
| Profondeur | 0,88 m (34,6") | 32 à 36" | **juste** |
| Surface du panneau sous les commandes | 0,995 m | un panneau de commande réel est à 36–40" (0,91–1,02 m) | **juste**, dans le haut de la plage |
| Centre de l'écran | 1,290 m | le tube d'un upright est centré entre 1,20 et 1,35 m | **juste** (voir §5 : c'est son CADRAGE qui est faux, pas sa hauteur) |
| Bas du marquee → haut | 1,590 → 1,780, soit 190 mm | une glace de marquee fait 6 à 8" (150–200 mm) | **juste** |
| Fente à jetons | 0,616 m | une porte à monnaie place sa fente vers 0,60–0,66 m | **juste** |
| Porte à monnaie | 250 × 550 mm | une porte standard américaine fait ≈ 267 × 495 mm | **juste** |
| Jonc de chant | rayon 9,5 mm | un T-molding 3/4" fait 19 mm de large, soit un demi-rond de 9,5 mm | **juste** |
| Rondelle anti-poussière | Ø 48 mm | entre la rondelle Sanwa (42–45) et le cache Happ (≈ 55) | **juste** |
| Boule de manche | Ø 42 mm | entre une LB-35 (35 mm) et une LB-45 (45 mm) | **juste** |

Ne touchez à aucune de ces valeurs. Ce document n'en modifie qu'**une** : le cadrage de la dalle
dans son ouverture (§5), et c'est un défaut de placement, pas de hauteur.

---

# 2. Un poste ou deux ?

## Décision : **UN SEUL POSTE.** Le second est supprimé.

Trois raisons, dans cet ordre de force.

### 2.1 Le moteur n'a qu'un joueur, et il ne l'aura jamais sur la même borne

`games/games.h:49-56` déclare la totalité des entrées d'un jeu :

```c
typedef enum ns_game_button {
    NS_GAME_UP = 0, NS_GAME_DOWN, NS_GAME_LEFT, NS_GAME_RIGHT,
    NS_GAME_ACTION, NS_GAME_BUTTON_COUNT
} ns_game_button;
```

Un manche à quatre directions et **un** bouton. `ns_game_api` porte un seul état (`void *g`), un
seul `press`, un seul `score`. Il n'y a nulle part un second joueur.

Et il n'y en aura pas : le duel de ce jeu est **distant**. `room/main.c:420` définit le fantôme
différé (une partie déjà jouée, rejouée à côté de la sienne) et `main.c:74` / `:320` la liaison
`--duel-direct=` par relais. Affronter quelqu'un, ici, ne consiste jamais à se tenir à deux devant
le même meuble.

Le moteur, enfin, ne plante **qu'un** joueur : `roomgen.c:1586` calcule un `player_anchor` unique,
et `room/room_viewmodel.c:229-230` pose la main gauche sur `stick` et la main droite sur `panel` —
les deux ancres du poste de GAUCHE. Le poste de droite n'a aucune ancre. Personne ne peut le
toucher, ni maintenant ni plus tard.

### 2.2 Le caisson ne peut pas s'élargir, et la salle l'interdit

`assets/scene/salle.room.json` pose les dix-neuf bornes au **pas de 0,80 m** : l'îlot central en
deux rangées de trois à x = −2,8 / −2,0 / −1,2 et +1,2 / +2,0 / +2,8, les rangées murales à
x = ±9,1 au pas de 0,80 en z. Avec W = 0,72 il reste 8 cm entre deux voisines.

`tools/roomgen.c:478-512` refuse deux bornes qui se recouvrent de plus de 5 mm sur **les trois**
axes, et c'est fatal dès qu'une borne est en cause. D'où deux plafonds durs :

- **largeur : W < 0,81 m** est le plafond dur ; **0,79 m** est le plafond utile (10 mm de jeu
  visible). Au-delà, `roomgen` s'arrête ;
- **profondeur : D ≤ 0,885 m**. Les deux rangées de l'îlot sont dos à dos à z = ±0,44, donc
  exactement à une profondeur de caisson. D = 0,89 donnerait 10 mm de recouvrement — fatal.

Un vrai poste double demande beaucoup plus. Deux adultes côte à côte veulent 450 mm entre les deux
manches ; avec l'entraxe retenu en §3 (manche → premier bouton 130 mm, entraxe 40 mm), le dernier
bouton du joueur 2 tombe à +395 mm de l'axe, et il faut 40 mm de tôle derrière lui :

> **un upright deux joueurs à deux boutons demande W ≈ 0,94 m.**

C'est 0,15 m de plus que ce que la salle tolère sans déplacer dix-neuf bornes, l'îlot, l'allée
centrale, l'estrade et les moulures — c'est-à-dire tout le §3.6 de `DESIGN-SALLE.md`, pour une
commande que le moteur ne lira jamais.

### 2.3 Ce que la référence montre, et pourquoi ça ne tranche pas

Le docstring de `borne.py:509-513` affirme que la référence montre deux postes. Je n'ai pas pu ouvrir
le modèle (19 $, licence *Editorial only* — voir `borne.py:15-22`), donc je ne le conteste pas.

Mais deux postes sur 72 cm **existent** dans le matériel réel : c'est le panneau deux joueurs des
*candy cabs* japonais (Sega Astro City et sa descendance), des meubles d'environ 60 cm de large où
les deux joueurs se serrent l'épaule contre l'épaule, souvent assis sur des tabourets. Ce n'est
pas la culture d'un upright occidental, et ce n'est pas ce jeu.

Et surtout : **c'est le second poste qui casse le premier.** Les six pastilles du joueur 1 courent
de −80 à +36 mm, c'est-à-dire qu'elles franchissent l'axe et se rangent sous la boule du joueur 2.
On ne peut pas garder les deux et bien placer l'un des deux. Le second poste n'est pas un
supplément inutile : il est la cause directe du défaut relevé.

**Retenu : un poste, la boule bleue (`manche_bleu`), à gauche de l'axe.** Le matériau
`manche_rouge` reste dans la table — l'ordre des matériaux est lu par `roomgen.c:1405-1421` et
`borne.py:798` casse le build s'il bouge — mais il ne porte plus une seconde boule : il devient le
matériau du bouton START (§3.4), qui est la pièce que le poste supprimé libère.

---

# 3. Le layout des boutons, au millimètre

## 3.1 Ce qui ne va pas aujourd'hui, mesuré dans le glTF

Les bornes de chaque primitive du modèle exporté, `assets/models/borne/borne.gltf` :

| primitive | x min | x max |
|---|---|---|
| `panneau` (la tôle du panneau) | −0,3505 | **+0,3505** |
| `caisson` | −0,3594 | +0,3594 |
| `tmolding` | −0,3600 | +0,3600 |
| `bouton_a` | −0,0961 | **+0,3621** |

> **Le bouton le plus à droite déborde de 11,6 mm la tôle sur laquelle il est censé être percé, de
> 2,7 mm le caisson, et de 2,1 mm le jonc de chant.** Il n'est vissé dans rien. C'est visible à
> l'œil sur une capture rapprochée du panneau : la pastille chevauche le liseré magenta.

Le reste, calculé depuis `borne.py:559-590` :

| | valeur | ce que ça donne |
|---|---|---|
| Emprise des commandes | −179 → +362 mm | **centrée sur +92 mm**, pas sur l'axe |
| Tôle nue à gauche du manche 1 | **172 mm** | un tiers du panneau, vide |
| Tôle nue à droite du dernier bouton | **−12 mm** | il n'y a pas de tôle : il sort |
| Rangée avant → nez du panneau | **31,5 mm** | il n'y a pas de repose-poignet |
| Rangée arrière → bord arrière | **175 mm** | deux tiers de la profondeur utile, vides |
| Boutons | 12 par borne | le moteur en lit **1** |

Autrement dit : les commandes sont tassées contre la lèvre avant, décalées de 9 cm vers la droite,
et le douzième bouton pend dans le vide.

## 3.2 Quel layout réel

Les grands layouts documentés sont **Sega Player 1**, **Neo-Geo** (4 boutons en arc),
**Capcom / Vewlix** (6 en deux rangées décalées) et **Astro City**. Aucun ne convient ici, et pour
une raison qui n'a rien d'esthétique : **ce sont tous des layouts de jeu de combat**, dessinés pour
que quatre doigts couvrent six boutons. Les huit mini-jeux de ce dépôt n'en lisent qu'un (§4).

Le bon matériel de référence est celui des uprights de la même génération que la silhouette :

- **Pac-Man** (Midway, 1980) : un manche à quatre directions, **aucun** bouton d'action ; seuls
  1P START et 2P START sont percés dans la tôle ;
- **Donkey Kong** (Nintendo, 1981), **Galaga** (Namco/Midway, 1981) : un manche à gauche, **un**
  bouton à sa droite, et les deux START ;
- **Asteroids** (Atari, 1979) : pas de manche du tout, cinq boutons en ligne.

**Layout retenu : « Midway un joueur » — un manche, deux boutons alignés à sa droite, un bouton
START au centre.** C'est le panneau d'un upright occidental de 1981, c'est ce que la silhouette du
meuble raconte déjà, et c'est ce que le moteur sait lire.

## 3.3 Les cotes de la quincaillerie

| Pièce | Valeur retenue | D'où elle sort |
|---|---|---|
| **Diamètre d'un bouton d'action** | **30 mm** (rayon 0,0150) | c'est la cote nommée du matériel réel : Sanwa **OBSF-30**, Seimitsu **PS-14-G**, perçage 28 mm. Les 33 mm du modèle actuel ne sont pas faux — c'est la **collerette** d'un bouton Happ/IL (perçage 1-1/8" = 28,6 mm, collerette ≈ 34–35 mm) — mais le modèle peint la collerette de la couleur du capuchon, donc la pastille se lit 10 % trop grosse |
| **Diamètre du START** | **24 mm** (rayon 0,0120) | Sanwa **OBSF-24**, la cote des boutons auxiliaires |
| **Entraxe entre deux boutons** | **40 mm** | la collerette d'un OBSF-30 fait 35 mm : **36 mm est le plancher physique**, deux collerettes se touchent. 40 mm laisse 10 mm de tôle visible entre deux capuchons de 30 — ce qui les fait lire comme deux boutons distincts à deux mètres, et il n'y a aucune raison de serrer quand il n'y en a que deux |
| **Manche → premier bouton** | **130 mm** | dérivé des deux mains, pas copié : la boule fait 42 mm, une main refermée dessus occupe ≈ 90 mm ; le talon de la main droite en occupe ≈ 80. 85 mm est le minimum de non-collision, 130 mm est confortable et reste dans la plage des panneaux réels |
| **Saillie d'un bouton** | **5 mm** (cylindre h = 0,008, enfoncé de 0,003) | un OBSF-30 dépasse de 4 à 5 mm. Le modèle actuel le fait dépasser de **9 mm** (h = 0,012 − 0,003), soit le double |
| **Repose-poignet, nez → ligne de commande** | **75 mm sur la pente** | un panneau réel garde 60 à 90 mm de tôle nue devant la première commande ; c'est là que se pose le talon de la main. Le modèle en a **31,5** |

## 3.4 Les coordonnées, par rapport au centre de la boule

Repère : **X** = largeur, positif vers la droite ; **Z** = mesuré **sur la pente du panneau**,
positif vers le joueur. Origine à l'axe du manche (le centre de la boule, à l'aplomb de sa tige).

| Pièce | ΔX | ΔZ | Ø |
|---|---|---|---|
| **Manche** (boule + rondelle + tige) | 0 | 0 | boule 42 |
| **Bouton 1 — ACTION** (`bouton_a`) | **+130 mm** | **0** | 30 |
| **Bouton 2 — secondaire** (`bouton_b`) | **+170 mm** | **0** | 30 |
| **START** (`manche_rouge`, le matériau que le poste supprimé libère) | **+85 mm** | **−65 mm** *(vers l'arrière)* | 24 |

## 3.5 Les mêmes, en cotes de borne, prêtes à écrire dans `borne.py`

La pente du panneau vaut `atan2(1,052 − 0,972 ; 0,434 − 0,185)` = **17,81°**, donc
`cos(pente) = 0,9521`, et la longueur de la tôle sur sa pente est **261,5 mm**.

```python
# --- la ligne de commande : 75 mm derrière le nez, SUR LA PENTE ---
Z_COMMANDES   = 0.3626      # Z_NEZ - 0.075 * 0.9521
Z_START       = 0.3007      # 140 mm derrière le nez, sur la pente

MANCHE_X      = -0.085      # le manche, à GAUCHE de l'axe
BOUTON_X      = (0.045, 0.085)      # ACTION, puis le secondaire
START_X       =  0.000      # sur l'axe de la borne

BOUTON_R      =  0.0150     # 30 mm — un OBSF-30, pas une collerette Happ
BOUTON_H      =  0.008      # 5 mm de saillie une fois enfoncé de 3 mm
START_R       =  0.0120     # 24 mm — un OBSF-24
```

Ce que `panneau_y()` en tire, et qu'il ne faut donc PAS écrire une seconde fois :

| | X | Y (surface) | Z |
|---|---|---|---|
| Manche | −0,085 | 0,9949 | 0,3626 |
| Bouton ACTION | +0,045 | 0,9949 | 0,3626 |
| Bouton secondaire | +0,085 | 0,9949 | 0,3626 |
| START | 0,000 | 1,0148 | 0,3007 |

**Les gardes, vérifiées :**

| | valeur | il faut |
|---|---|---|
| Bord de la rondelle → jonc de chant (gauche) | **242 mm** | > 40 |
| Bord du bouton 2 → jonc de chant (droite) | **250 mm** | > 40 |
| Centre de l'emprise des commandes | **0,0 mm** | l'axe |
| Nez → première commande, sur la pente | **75 mm** | 60 à 90 |
| START → bord arrière de la tôle, sur la pente | **122 mm** | > 40 |

**242 et 250 mm de tôle nue de chaque côté, ce n'est pas une erreur : c'est ce à quoi ressemble un
vrai panneau d'upright un joueur.** Sur une borne Ms. Pac-Man, le manche est seul au milieu d'un
panneau de 56 cm. Et cette tôle n'est pas vide : c'est là que vit `panneau_borne.png`, la
sérigraphie que `tools/panelart` dessine déjà et que `borne_commande` applique en `fit` sur le quad
du panneau. Un panneau réel y porte le titre du jeu, « 1 PLAYER — PUSH START » et la légende des
boutons. **La place que ce layout libère est la place qu'il faut au dessin.**

## 3.6 Les deux ancres, qui changent

```jsonc
"stick": [-0.085, 1.0719, 0.3626],   // le SOMMET de la boule — voir le calcul ci-dessous
"panel": [ 0.045, 1.0069, 0.3626],   // le DESSUS du bouton ACTION
```

`panel` cesse d'être le barycentre de six pastilles — un point qui n'était **aucun** bouton — et
devient le dessus de celui que le doigt presse. C'est la sémantique que `roomgen.c:1462-1472`
annonce déjà (« quatre points qu'on TOUCHE »).

**Deux conséquences à traiter dans le même lot, sinon la correction est incomplète :**

1. Le sommet de la boule **descend de 14,7 mm** (1,0866 → 1,0719), et c'est la somme de deux
   mouvements contraires : le manche recule de 49 mm, donc il remonte la pente de +9,4 mm, mais la
   tige raccourcie de §6.4 le rabaisse de 24 mm. La boule culmine alors à **77 mm au-dessus de la
   tôle**, ce qui est la cote d'un manche à boule réel (elle est à 101 mm aujourd'hui).
   `tests/test_ik.c:461-484` mesure la portée des deux mains sur des constantes figées : **il
   faudra les remesurer**. C'est exactement le réglage que le journal dit avoir coûté cher en B14b.
2. `roomgen.c:1586` dérive la position du joueur de `panel_centre.x`. Avec l'ancre sur le bouton
   ACTION, le joueur se planterait 45 mm à droite de l'axe. **Le joueur doit se planter sur l'axe
   de la borne** — c'est l'axe de l'écran qu'il regarde, pas celui du bouton qu'il presse. La
   distance (0,46 m) reste dérivée du panneau ; seule l'abscisse doit venir de la borne.

---

# 4. Le nombre de boutons doit-il dépendre du jeu ?

## Décision : **le même percage pour les dix-neuf. Ce qui change par jeu, c'est la peinture.**

### 4.1 Ce que chaque jeu lit vraiment

Relevé dans `games/*/`, pas supposé :

| Jeu | Directions lues | Bouton d'action | Source |
|---|---|---|---|
| **flappy** | aucune | **1** — toutes les touches battent des ailes | `flappy.c:542-547` |
| **snake** | gauche, droite | **1** (accélérer) | `snake.c:1014-1021` |
| **tetris** | les quatre | **1** (rotation) | `tetris.c:313` |
| **asteroid** | les quatre | **1** (tirer ; bas = bombe) | `asteroid.c:445, 526-531` |
| **pacman** | les quatre | **0** | aucun `NS_GAME_ACTION` dans `games/pacman/` |
| **piano** | les quatre = les quatre touches | **1** (démarrer) | `piano.c:180-196` |
| **shooter** | les quatre | **1** | `games/shooter/` |
| **demineur** | les quatre | **1 aujourd'hui, 2 voulus** | `demineur.c:218-222` |

Deux de ces lignes valent d'être lues deux fois.

**Pac-Man ne lit aucun bouton — et la borne Pac-Man de 1980 n'en avait aucun.** La convergence est
totale : le portage a redécouvert tout seul le panneau du matériel d'origine.

**Le démineur en veut deux, et le dit.** `games/demineur/demineur.c:218-222` :

> « Les drapeaux ne sont PAS posables à la main dans ce portage : une borne n'a qu'un bouton
> d'action, et le second bouton qu'il faudrait n'existe pas sur le panneau. […] elle se lèvera le
> jour où le panneau aura deux boutons déclarés. »

Le panneau porte **douze** boutons et le jeu en réclame **un deuxième**. C'est la formulation la
plus courte du reproche du propriétaire : le layout ne veut rien dire parce qu'il ne décrit aucun
des jeux qui tournent dessus. **Les deux boutons du §3 lèvent cette limite** ; c'est la raison
d'être du second, et il n'y en a pas de troisième.

### 4.2 Pourquoi le même panneau pour les dix-neuf

1. **Le coût.** `roomgen.c:1434` importe UN glTF et l'instancie dix-neuf fois. Un perçage par jeu,
   c'est huit exports, huit tables de matériaux, et huit occasions que l'ordre des matériaux dérive
   — un risque assez réel pour que `borne.py:796-801` casse déjà le build là-dessus.
2. **C'est ce que faisait le matériel réel.** Un exploitant achetait un caisson générique
   (« conversion cabinet ») et changeait la carte, le marquee et **la sérigraphie** du panneau. Le
   perçage restait. C'est très exactement ce qu'est un caisson Dynamo ou Happ.
3. **La différenciation ne coûte rien là où elle est déjà branchée.** Le modèle expose deux
   matériaux de bouton, `bouton_a` et `bouton_b`, et `salle.room.json` les peint par borne. Un
   bouton qu'un jeu ne lit pas se peint **sombre** — c'est ce que fait une vraie borne d'un bouton
   inutilisé : un capuchon noir, ou un bouchon.

### 4.3 La table

| Jeu | Bouton ACTION (`bouton_a`) | Bouton 2 (`bouton_b`) | START |
|---|---|---|---|
| flappy, snake, tetris, asteroid, piano, shooter | **vif** | **sombre** (bouchon) | vif |
| demineur | **vif** | **vif** — c'est le drapeau | vif |
| pacman | **sombre** | **sombre** | vif |
| leaderboard | sombre | sombre | vif |

Zéro géométrie, zéro export : deux clés nouvelles — `materialButtonA` et `materialButtonB` — dans
les entrées `cabinets` de `salle.room.json`, sur le modèle de `materialPanel` qui existe déjà
(`roomgen.c:1341`), plus les deux matériaux sombres à déclarer une fois.

### 4.4 Un défaut à corriger dans le même geste

`borne.py:472` peint les **inserts rouges de la porte à monnaie** avec `MI["bouton_a"]` — vérifié
dans le glTF, la primitive `bouton_a` court de y = 0,592 (les inserts) à y = 1,010 (les pastilles).

> **Repeindre les boutons par jeu repeint aussi les inserts du monnayeur.** Une borne Pac-Man aux
> boutons gris aurait un monnayeur gris.

Les inserts doivent prendre `monnayeur`, ou leur propre matériau ajouté **en fin de table**
(`borne.py:187`).

---

# 5. La hauteur de l'écran, et le regard

## 5.1 Sur du matériel réel

Sur un upright, le centre du tube est entre **1,20 et 1,35 m**, et le joueur adulte le regarde de
haut : il se tient debout, le ventre près du caisson, et **plonge de 25 à 35°**. C'est la posture
de l'upright, et c'est pour ça que le tube y est incliné vers l'arrière — pour rendre au regard une
face à peu près perpendiculaire.

Ici, avec l'œil à 1,70 m sur l'ancre que la salle déclare :

| | valeur |
|---|---|
| Œil → centre de dalle, à plat | 0,661 m |
| Dénivelé | 0,410 m |
| **Plongée** | **−31,8°** |
| Angle entre le regard et la normale de la dalle | 10,2° |

**−31,8°, c'est une borne d'arcade.** La cote n'est pas fausse. Ni l'écran ni le joueur ne doivent
bouger — et surtout pas le joueur : 1,70 m debout / 1,31 m accroupi sont les valeurs de l'auteur de
2020, mesurées et démontrées anatomiquement exactes en `DESIGN-SALLE.md` §1.1. Les changer
déferait l'échelle de toute la salle pour corriger un angle qui est juste.

## 5.2 Le vrai chiffre du problème

`room/room_camera.c:54` : `fov_y = 62°`, donc **un demi-champ vertical de 31,0°**.

> La dalle est à **31,8°** sous l'horizon. **Elle est 0,8° SOUS le bord bas du cadre.**
> Au repos, planté sur l'ancre que la borne déclare, le joueur ne voit pas du tout son écran.

Ce qu'il voit à la place, calculé au même endroit : le **marquee**, centré à **−1,8°** et à 48 cm
de l'œil. C'est-à-dire pile au milieu de l'image. Vérifié en capture (`--pitch=0` sur l'ancre) :
l'enseigne Flappy Bird occupe la bande centrale, et la dalle est coupée par le bord inférieur.

Le moteur sait déjà baisser le regard : `room/main.c:2248-2254` pose la vue sur `screen_center` en
un tiers de seconde **quand une partie démarre**, et le commentaire de `main.c:2224` appelle ça
« la vraie cause du "je suis obligé de m'accroupir" ». Ce correctif est bon. Il est simplement
**incomplet** : il ne couvre qu'un instant sur cinq.

| Moment | Le regard est-il posé ? |
|---|---|
| On marche vers la borne | non — normal, on regarde où on va |
| **On est planté devant, avant d'insérer** | **non** — et l'écran d'attente, le tableau des scores et le « APPUYER SUR ESPACE POUR JOUER » sont hors cadre |
| Le jeton part, la main s'avance | non |
| **La partie démarre** | **oui** (`main.c:2253`) |
| **La partie est finie** | **non** — le tangage reste où le joueur l'a laissé |

C'est la même leçon que `main.c:2236-2241` a déjà tirée une fois, et elle vaut encore :
*« une vérification qui emprunte un chemin que le joueur n'emprunte pas ne vérifie rien »*. La
capture nommée `borne` porte `"pitch": -34.0` écrit à la main, et `--play-at=` calcule le sien
(`main.c:1861`). Les deux chemins d'inspection sont cadrés ; le chemin du joueur au repos ne l'est
pas.

## 5.3 Décision

**L'écran ne monte pas. Le joueur ne descend pas. C'est le REGARD qu'il faut poser, et à deux
moments de plus** — quand `room_viewmodel_target()` désigne une borne alors qu'aucune partie ne
tourne, et à la fin d'une partie. Le calcul existe déjà, trois lignes, à `main.c:2248-2253`.

## 5.4 Ce que j'ai envisagé et que je REFUSE

**Remonter l'ouverture du cadre.** Y_ECRAN_HAUT (1,430) est le bas de la casquette
haut-parleurs, qui court jusqu'à 1,565. La monter de 35 mm laisserait encore 184 mm de casquette
sur la pente — assez pour la grille de 100 mm. Gain mesuré : la plongée passe de 31,8° à **30,2°**,
soit **1,6°**, et la dalle gagne un vrai bandeau noir de 23 mm au lieu de 7.

**Je le refuse quand même** : 1,6° ne change rien au ressenti, et cette cote est une **mesure**
(`borne.py:126-140`, douze vues de référence relevées au pixel). On ne défait pas une mesure pour
un degré et demi. Le noter ici suffit ; si l'ouverture doit un jour bouger pour une autre raison,
le chiffre est prêt.

**Rapprocher le joueur.** Contre-productif, et c'est arithmétique : à 0,30 m du panneau au lieu de
0,46, la plongée passe à **39,3°**. Plus près, c'est plus bas.

**L'éloigner.** Pour descendre à 25° il faudrait 0,93 m entre l'œil et la dalle, soit un joueur
planté à 0,68 m du panneau — hors de portée du bras (`tests/test_ik.c` travaille sur 0,59 m).

**Il n'existe aucune correction géométrique à ce défaut.** C'est la conclusion, et c'est elle qui
justifie de ne toucher à rien.

---

# 6. Les autres écarts

Par ordre de gravité. Chacun est vérifié dans le fichier ou dans une capture.

## 6.1 La dalle flotte 76 mm devant son cadre, et dépasse de son ouverture

Le défaut le plus visible du modèle, et il n'a rien à voir avec la hauteur.

`borne.py:819-826` pose le centre de la dalle en **translatant de `sin(incl) × h/2` en z**. Ce
n'est pas la bonne correction : pour poser une dalle sur une face inclinée, on décale d'un
millimètre ou deux **le long de la normale**, pas de la moitié de sa hauteur le long de z.

| | mesuré |
|---|---|
| z du centre de dalle | 0,1768 |
| z de la face du cadre à cette hauteur | 0,1006 |
| **Saillie** | **+76,2 mm** |
| Haut de la dalle | 1,4522 — l'ouverture s'arrête à **1,430** |
| **Dépassement en haut** | **+22,2 mm : l'image sort du meuble** |
| Centre géométrique de l'ouverture | 1,2640 — la dalle est à 1,290, soit **+26,0 mm** |

Vérifié en capture, en vue rasante depuis le flanc : on voit la cavité noire du cadre **derrière**
la dalle, et le coin bas de l'image en surplomb au-dessus du panneau de commande. À cet angle, la
borne n'a pas d'écran : elle a un panneau publicitaire accroché devant.

## 6.2 Deux inclinaisons écrites deux fois, et qui ne sont pas d'accord

| | déclaré | construit | écart |
|---|---|---|---|
| Pente du panneau de commande | **19°** (`borne.py:119-121`) | **17,81°** (`atan2(0,080 ; 0,249)`) | 1,19° |
| Inclinaison de l'écran | **21,6°** (`ECRAN_INCL`) | **19,87°** — la face du cadre va de (0,170 ; 1,098) à (0,050 ; 1,430) | **1,73°** |

Le second est le plus grave : `ECRAN_INCL` est **exportée dans les ancres** et `roomgen.c:1455-1456`
s'en sert pour construire la normale de la dalle. La dalle est donc collée à 1,73° de la face qui
la porte, et cette normale est ce que le moteur annonce à l'éclairage et au son.

Les deux doivent être **calculées** depuis les extrémités des segments, jamais écrites — c'est
exactement le principe que `panneau_y()` applique déjà et que `borne.py:492-499` défend en toutes
lettres.

## 6.3 Le chrome rend noir

Vérifié en capture zoomée : sous chaque boule, la rondelle et la tige forment une **tache noire**.
Le matériau `borne_chrome` est à `metallic 1.0`. Or `engine/shaders/lighting.frag:259` écrit
`kd = (1 − F) × (1 − metallic)` : à 1,0, un métal n'a **aucune** composante diffuse, et la
réflexion d'environnement n'existe que si la couche de lancer de rayons tourne
(`lighting.frag:290`, `u_counts.y >= 2`). Sans elle, il ne reste que le lobe spéculaire ponctuel.

C'est précisément le défaut que `roomgen.c:1375-1382` croyait avoir corrigé (« dans la pénombre de
l'allée elle disparaissait sous sa boule ») : la tige a changé de matériau, pas de résultat.

Correction : `metallic 0.85`, `roughness 0.30`, ou une `baseColor` plus claire — **dans
`salle.room.json`, pas dans la géométrie**. Ce n'est pas un défaut de borne, c'est un défaut de
matière, et il est hors du périmètre de `borne.py`.

## 6.4 La tige de manche est un levier de vitesse

`borne.py:540-542` : Ø 18 mm à la base, **62 mm de tige visible** entre la tôle et la boule.
Une tige de manche réelle fait 10 à 12 mm de diamètre et laisse voir 30 à 40 mm.

Retenu : **Ø 12 mm (r0 = 0,006, r1 = 0,0055), longueur 0,038.** La boule descend de 24 mm avec
elle ; l'ancre `stick` qui en résulte est déjà celle de §3.6 (1,0719), tige corrigée comprise. **Il
n'y a qu'un seul chiffre à écrire, et c'est celui-là.**

## 6.5 Aucun bouton START

Le jeu affiche « APPUYER SUR ESPACE POUR JOUER » sur la dalle et il n'y a rien à presser sur la
tôle. Toutes les bornes réelles citées en §3.2, sans exception, ont un bouton START, et sur
Pac-Man c'est **le seul** bouton du panneau. Il est réintroduit en §3.4 : Ø 24 mm, sur l'axe, 65 mm
derrière la ligne de commande.

## 6.6 Points mineurs, notés sans correction demandée

- **Le cadre noir autour de la dalle ne peut pas être régulier.** L'image de 620 mm dans une
  ouverture de 720 laisse 50 mm de noir à gauche et à droite ; correctement centrée, elle n'en
  laisserait que **2 mm** en haut et en bas (353 − 349, réparti). L'ouverture (720 × 353) a un
  rapport de 2,04:1 quand la dalle est en 16:9 : **aucune marge uniforme n'est possible**, quelle
  que soit la taille de la dalle. Le compromis retenu en §7 (600 × 337,5) donne 7 mm en haut et en
  bas contre 60 sur les côtés. C'est ce à quoi ressemble un écran 16:9 monté derrière un cadre
  percé pour un tube 4:3 — c'est-à-dire une vraie borne de conversion.
- **1,88 m de haut**, soit 74". Deux à quatre pouces au-dessus d'un upright classique. Contrainte de
  salle assumée, `room.height` vaut 2,92 : il reste 1,04 m de dégagement, rien ne pousse.
- Le nombre de triangles (**4 992**, `borne.ancres.json`) baisse mécaniquement de dix boutons
  supprimés et d'un manche : environ **−700 triangles par borne**, soit **−13 300 sur la salle**.

---

# 7. Ordre de bataille

Classé par rendement. « Effort » suppose qu'on connaît déjà `borne.py`.

| # | Poste | Effort | Critère de réussite, mesuré |
|---|---|---|---|
| 1 | **Un seul poste, et le layout Midway** — §3.5 : manche à −0,085, boutons à +0,045 / +0,085, START à 0,000, ligne à z = 0,3626 | ≈ **30 lignes** dans `commandes()` | Dans le glTF, `bouton_a`.x max ≤ **+0,100** (il vaut +0,3621). Emprise des commandes centrée à **±5 mm** de l'axe. Une capture `--pitch=-34` montre le manche et deux boutons, rien au-delà du tiers central |
| 2 | **La dalle rentre dans son cadre** — `ECRAN_INCL` dérivée, `ECRAN_Y = 1,2626`, `ECRAN_W/H = 0,600 / 0,3375`, ancre posée **le long de la normale** et non en z | ≈ **10 lignes** | `screen` = **(0,000 ; 1,2640 ; 0,1143)**. La dalle court de y = 1,105 à 1,423 : **7 mm de cadre en haut ET en bas**, zéro dépassement. En vue rasante depuis le flanc, plus aucun bord de dalle devant le meuble |
| 3 | **Les deux pentes calculées, plus écrites** — `PANNEAU_PITCH` et `ECRAN_INCL` depuis les extrémités des segments | **2 lignes** | `ancres["screenTilt"]` vaut **0,3469 rad (19,87°)** et non 0,3770. Le docstring dit 17,81° pour le panneau, ou la cote change pour valoir 19° |
| 4 | **Les ancres et la pose du joueur** — §3.6 : `panel` sur le bouton ACTION, abscisse du joueur sur l'axe de la borne | **1 ligne** dans `borne.py`, **1** dans `roomgen.c:1586` | `--pose=press` pose la main droite **sur** un bouton et la gauche **sur** la boule ; `tests/test_ik.c` repasse après remesure de ses constantes |
| 5 | **Le regard se pose aussi hors partie** — §5.3, le calcul de `main.c:2248-2253` appliqué quand une borne est désignée et à la fin d'une partie | ≈ **10 lignes** dans `room/main.c` | Une capture prise sur l'ancre **sans `--pitch=`** montre la dalle entière. Elle est aujourd'hui **0,8° hors cadre** |
| 6 | **Les inserts du monnayeur quittent `bouton_a`** — §4.4 | **1 ligne** | Dans le glTF, `bouton_a`.y min ≥ **0,97** (il vaut 0,592) |
| 7 | **Le bouton secondaire est déclaré au démineur** — le drapeau, `demineur.c:218-222` | ≈ **20 lignes** de jeu | Un joueur humain pose un drapeau ; le commentaire qui dit la limite est supprimé parce qu'il est faux |
| 8 | **La tige de manche** — §6.4, Ø 12 mm, 38 mm visibles. **À faire dans le poste 1**, c'est la même fonction | **2 lignes** | L'ancre `stick` vaut **1,0719** et la boule culmine à **77 mm** au-dessus de la tôle (101 aujourd'hui) |
| 9 | **Les boutons par jeu, en JSON** — §4.3, `materialButtonA` / `materialButtonB` par borne | ≈ **25 lignes** de JSON + **6** dans `roomgen.c` | La borne Pac-Man a deux capuchons sombres et un START vif ; la borne démineur a deux boutons vifs |
| 10 | **Le chrome cesse d'être noir** — §6.3, `borne_chrome.metallic` 1,0 → 0,85 | **1 ligne** de JSON | La rondelle et la tige sont distinctes du panneau sur la capture `borne` |

Les postes **1 à 4 sont dans `borne.py` seul** et répondent entièrement au premier reproche. Le
poste **5 est dans `room/main.c` seul** et répond entièrement au second. Aucun des deux reproches
ne demande de toucher à la salle.

---

# 8. Ce que je n'ai pas pu vérifier

Par honnêteté, et parce qu'un chiffre dont on ne dit pas la provenance finit par être recopié.

- **Le modèle de référence.** Je ne l'ai pas ouvert : 19 $, licence *Editorial only*
  (`borne.py:15-22`). Tout ce que j'en dis passe par la transcription mesurée du script. Je ne
  conteste donc pas qu'il montre deux postes — §2.3 explique pourquoi ça ne change pas la décision.
- **L'entraxe exact des layouts japonais.** Je retiens 36 mm comme plancher, et je le **dérive** de
  la collerette de 35 mm d'un OBSF-30 plutôt que de le citer : je crois me souvenir de 36 mm sur
  les planches de layout de référence, je ne le certifie pas au millimètre. La valeur que je
  spécifie (40 mm) ne dépend pas de ce souvenir.
- **La distance manche → premier bouton sur un panneau nommé.** Je n'ai pas de cote certifiée. Les
  130 mm de §3.3 sont dérivés de l'encombrement de deux mains adultes, et c'est dit.
- **La hauteur exacte du centre du tube sur une borne nommée.** Je donne une plage (1,20–1,35 m),
  pas une cote. Elle suffit largement : elle contient 1,264 et 1,290, donc elle ne départage rien
  et n'a pas besoin d'être plus fine.
- **La largeur exacte d'un upright deux joueurs américain.** De mémoire 30 à 32 pouces ; je ne le
  garantis pas au demi-pouce. Le chiffre de §2.2 (0,94 m) est calculé depuis le layout retenu, pas
  emprunté.

---

# 9. Les décisions, en une ligne chacune

1. **Un seul poste de jeu** : le moteur n'a qu'un joueur et le duel est distant, le second poste est
   supprimé.
2. **Le caisson ne change pas** : W = 0,72, D = 0,88 ; la salle plafonne W à 0,79 et D à 0,885, et
   un vrai poste double demanderait 0,94.
3. **Layout « Midway un joueur »** : un manche, deux boutons alignés à sa droite, un START au
   centre — pas un layout de jeu de combat, aucun des huit jeux n'en lit six.
4. **Le manche est à −0,085 de l'axe, les boutons à +0,045 et +0,085, le START à 0,000**, tous sur
   la ligne z = 0,3626 sauf le START à z = 0,3007.
5. **Relativement à la boule : bouton 1 à (+130 ; 0), bouton 2 à (+170 ; 0), START à (+85 ; −65)**,
   en millimètres sur la pente du panneau.
6. **Bouton d'action Ø 30 mm** (OBSF-30) et non 33 ; **START Ø 24 mm** ; **entraxe 40 mm** ;
   **saillie 5 mm** et non 9.
7. **75 mm de repose-poignet** entre le nez du panneau et la première commande, contre 31,5
   aujourd'hui.
8. **Le même perçage sur les dix-neuf bornes** : ce qui varie par jeu est la peinture des capuchons
   et la sérigraphie, pas la géométrie.
9. **Le second bouton existe pour le démineur** — il lève une limite que le jeu documente lui-même.
10. **Ni l'écran ni le joueur ne bougent** : 31,8° de plongée, c'est une borne d'arcade, et 1,70 m
    est la cote mesurée de 2020.
11. **Le vrai défaut de hauteur est que le regard n'est posé qu'au démarrage d'une partie** : au
    repos la dalle est 0,8° sous le bord bas d'un champ de 62°, et c'est le marquee qu'on regarde.
12. **Remonter l'ouverture du cadre est refusé** : 1,6° de gain contre une cote mesurée.
13. **La dalle est recentrée dans son ouverture** : ancre (0 ; 1,2640 ; 0,1143), taille 0,600 ×
    0,3375, décalée le long de la normale et non en z — elle flottait 76 mm devant le cadre et
    dépassait de 22 mm par le haut.
14. **Les deux inclinaisons sont calculées, plus jamais écrites** : le panneau vaut 17,81° et non
    19, l'écran 19,87° et non 21,6.
15. **`panel` désigne le dessus du bouton d'action**, plus le barycentre de six pastilles.
16. **Le joueur se plante sur l'axe de la borne**, pas sur l'abscisse du bouton.
17. **Les inserts du monnayeur quittent `bouton_a`**, sans quoi repeindre les boutons repeint la
    porte à monnaie.
18. **La tige de manche passe à Ø 12 mm sur 38 mm visibles**, contre 18 mm sur 62 — l'ancre `stick`
    vaut alors **(−0,085 ; 1,0719 ; 0,3626)** et la boule culmine à 77 mm au-dessus de la tôle.
19. **Le chrome noir est un défaut de matériau, pas de géométrie** — il se corrige dans
    `salle.room.json`, hors du périmètre de `borne.py`.
