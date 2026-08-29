# L'atmosphère : ce qui la tue, et quoi mettre à la place

`docs/DESIGN-SALLE.md` a établi l'ADN de 2020 et corrigé les réflectances. Ses postes 1 à 9 sont
appliqués — je l'ai vérifié dans le fichier — et la salle est plus juste qu'avant. Elle n'est
toujours pas **habitée**. Le propriétaire écrit : « le jeu ne respire aucune ame n'y atmoshpher »
et « Les objets sont mal positionnée ».

Ce document dit où, quoi et combien. Il ne contient aucun jugement qui ne soit rattaché à une
capture, une ligne de `assets/scene/salle.room.json` ou un nombre mesuré. Il ne remet en cause
aucune décision de `DESIGN-SALLE.md` ; il traite ce qui reste, et ce que ces corrections ont
révélé en s'appliquant.

**Une phrase pour tout le document :** une salle tamisée n'est pas une salle sombre. C'est une
salle où l'on a beaucoup de noir **et** quelques flaques fortes. Nineteen a le noir. Il lui
manque les flaques, et il lui manque ce qu'on met dedans.

---

## 0. Comment tout ici a été obtenu

```sh
./build/macos-universal/bin/nineteen --headless --screenshot=/tmp/x.png --frames=8 \
    --quality=high --width=1280 --height=720 --view=NOM --no-hud
./build/macos-universal/bin/nineteen --headless --screenshot=/tmp/y.png --frames=8 \
    --quality=high --no-hud --camera=free --pos=X,Y,Z --yaw=D --pitch=D
```

Le binaire journalise à chaque capture `luminance : moyenne / médiane / % sous 16 / % au-dessus
de 200`. Les chiffres ci-dessous sont vérifiés identiques en 1280 × 720 et en 3200 × 1800 : la
mesure ne dépend pas de la définition.

Les réflectances sont **linéaires** — sRGB→linéaire par texel, moyenne, luminance BT.709 — et
mesurées sur les fichiers eux-mêmes, texture d'origine ou planche dé-cuite selon ce que le
matériau emploie. Même méthode que `DESIGN-SALLE.md`, mêmes résultats sur les repères communs
(`plafond_c.png` 0,395 ; `floor_c.png` 0,093).

### 0.1 Les neuf vues nommées, aujourd'hui

| Vue | Moy. | **Méd.** | % < 16 | % > 200 | Méd. en §2.1 de DESIGN-SALLE | Δ |
|---|---|---|---|---|---|---|
| `allee` | 68,5 | **49** | 24,3 | 3,6 | 53 | −4 |
| `bar` | 52,0 | **38** | 30,7 | 1,4 | 63 | **−25** |
| `billard` | 33,1 | **20** | 42,3 | 0,2 | 20 | 0 |
| `plafond` | 56,7 | **43** | 14,8 | 3,1 | 61 | −18 |
| `entree` | 28,4 | **24** | 28,7 | 0,2 | 34 | −10 |
| `classement` | 33,4 | **18** | 44,0 | 0,8 | 31 | −13 |
| `toilettes` | 46,3 | **25** | 27,1 | 0,0 | 99 | **−74** |
| `borne` | 82,7 | **56** | 33,6 | 8,5 | 20 | **+36** |
| `orbite` | 31,1 | **11** | 63,7 | 0,3 | — | — |

Deux constats que personne n'a écrits :

1. **Les corrections de `DESIGN-SALLE.md` ont soustrait de la lumière et n'en ont rendu qu'au
   sol.** Le plafond est passé de 0,403 à 0,294 de réflectance, le mur de 0,333 à 0,142, le
   plafonnier des WC de 52 à 30. Sept médianes sur huit ont baissé. La seule qui monte est
   `borne`, et elle monte parce que le sol s'est mis à luire.
2. **Le poste 7 a dépassé sa cible d'un facteur 2,2.** Son critère écrit était « médiane de
   `toilettes` entre 55 et 70 ». Elle vaut **25**. La baisse d'intensité (52 → 30) *et* la baisse
   de portée (4,4 → 3,6) ont été appliquées ensemble alors que le calcul n'en justifiait qu'une.

### 0.2 Huit cadrages libres, faits pour voir ce que les vues nommées ne cadrent pas

| Nom | `--pos` | `--yaw` | `--pitch` | Moy. | **Méd.** | % < 16 |
|---|---|---|---|---|---|---|
| `vide_nord` | 0 ; 1,7 ; 1,6 | 90 | −6 | 37,0 | **21** | 43,4 |
| `vide_sud` | 0 ; 1,7 ; −5,2 | 270 | −4 | 29,9 | **21** | 29,5 |
| `vide_sud_large` | −4,5 ; 1,7 ; −3,2 | 300 | −3 | 26,7 | **15** | 52,2 |
| `depuis_bar` | −2,6 ; 1,7 ; 5,0 | 270 | −4 | 31,9 | **14** | 53,9 |
| `haut_salle` | 0 ; 2,85 ; −6,8 | 90 | −20 | 28,4 | **12** | 57,7 |
| `travee_ouest` | −6,2 ; 1,7 ; 3,6 | 200 | −5 | 35,6 | **21** | 41,1 |
| `salon_est` | 7,6 ; 1,7 ; −1,2 | 250 | −8 | 24,9 | **15** | 50,7 |
| `ilot_biais` | 5,0 ; 1,7 ; 4,6 | 215 | −7 | 10,0 | **10** | 78,6 |

`ilot_biais` est une caméra posée **dans la cloison d'entrée** (`walls[cloison_entree]`, x = 4,6
pour z de 4,2 à 7,2). Je la laisse au tableau parce qu'elle dit une chose utile : le contrôle
d'enfermement de `roomgen` (`check_lights_not_enclosed`) ne vaut que pour les *lumières*, pas pour
les caméras libres, et les vues nommées ne couvrent aucun des trois grands vides.

**Convention d'orientation, vérifiée sur six objets existants** (`enseigne_nineteen`,
`affiche_est`, `distributeur`, `bar_accueil`, `borne_arcade_13`, `panneau_restrooms`) :

| | props et bornes (`yaw`) | caméras (`--yaw`) |
|---|---|---|
| face vers **+z** | 0 | 90 |
| face vers **+x** | 90 | 0 |
| face vers **−z** | 180 | 270 |
| face vers **−x** | 270 | 180 |

Les deux conventions sont **décalées de 90°**. Ce n'est pas un défaut d'ambiance, mais c'est un
piège pour qui pose un objet d'après une capture ; toutes les positions de ce document sont
données en convention **prop**.

---

## 1. Le diagnostic, vue par vue

### 1.1 `allee` — méd. 49 · % > 200 : 3,6

**Ce qui marche, à ne pas casser.** La fuyante. On voit l'allée, la trouée de 2,4 m au milieu de
l'îlot, le bar au fond, l'enseigne. C'est le meilleur axe de la salle et la seule vue qui donne
de la profondeur. Le tapis néon porte l'image. Les deux marquees latéraux (`SHOOTER`) se lisent.

**Ce qui tue l'ambiance.**

- **Le liseré magenta.** `materials[borne_tmolding]` : `baseColor [0.78, 0.06, 0.55]`,
  `roughness 0.22`. Mesuré : linéaire (0,313 ; 0,022 ; 0,190), **saturation 0,93** — le matériau
  le plus saturé du décor après `bouton_jaune`. Il court sur **tout** le contour des dix-neuf
  bornes, des deux côtés, soit environ 8 m de ligne continue par borne. Une ligne continue est ce
  que l'œil intègre en premier : c'est elle qui décrit la forme. À `roughness 0.22` elle prend en
  plus le spéculaire des dalles du plafond. Elle est, littéralement, ce qu'on voit avant l'image.
- **Le flanc capitonné.** `assets/CMakeLists.txt:113` appelle `sideart` sans `--step`, donc au
  défaut **96 px** sur une planche de 512 px de large, et `flanc_*` déclare `uvMetres 1.0` : le
  losange mesure donc **18,75 cm**. Un losange de 19 cm est une cote de **capitonnage de
  fauteuil**. Une tôle gaufrée d'arcade fait 3 à 5 cm.
- **Le flanc est plus clair et moins saturé que le caisson qu'il habille.** Mesuré, moyenne des
  neuf paires : `flanc_*` réflectance **0,177**, saturation **0,42** ; `caisson_*` réflectance
  **0,106**, saturation **0,75**. La plus grande face de la borne — 1,4 m², celle qu'on voit de
  l'allée — est donc **1,7 fois plus claire** et **1,8 fois moins colorée** que son propre corps.
  Sur une vraie borne c'est l'inverse : le flanc **est** l'illustration.
- **Le plafond est la surface la plus claire du cadre.** Voir §5.

### 1.2 `orbite` — méd. **11** · % < 16 : **63,7**

C'est la vue qui donne raison au propriétaire mot pour mot.

- **Un pilier occupe le tiers central de l'image.** `captures[orbite]` déclare `radius 6.5`,
  `height 2.35`, centre (0 ; 0). Les dix piliers (`boxes[pilier_ouest]`, `boxes[pilier_est]`) sont
  à x = ±5,4. L'orbite passe à **1,1 m** en dehors de l'anneau de piliers : à chaque quart de
  tour, un pilier est plein cadre. La vue « d'ensemble » de la salle ne montre pas la salle.
- **63,7 % des pixels sont sous 16.** Le reste est le tapis. Il n'y a rien entre le sol et le
  plafond sur les trois quarts du tour.
- **Les piliers sont des dalles rouges lisses.** `materials[pilier]` : `bordeaux.jpg`,
  `uvMetres 3.0`. La face d'un pilier fait **0,48 m** : à 3 m par répétition, elle n'en montre que
  **16 %** de la largeur, c'est-à-dire un aplat sans grain. Réflectance 0,029, **saturation 0,88**.
  Dix masses rouges uniformes qui traversent tous les cadrages.

### 1.3 `vide_nord` (0 ; 1,7 ; 1,6 · yaw 90) — méd. 21 · % < 16 : 43,4

Le cadrage le plus utile du lot : on est adossé à l'îlot, on regarde le bar.

**Ce qui marche.** L'enseigne `NINETEEN`. La ligne de comptoir de 5,4 m. Le tabouret.

**Ce qui tue l'ambiance.**

- **4,7 m de moquette sans un seul objet**, sur toute la largeur du cadre. Voir §2 pour la mesure.
- **Le tableau des scores du bar est un rectangle noir de 1,8 × 0,9 m.**
  `props[bar_accueil].parts[3]` emploie `materials[classement_fond]`, qui porte pourtant
  `emissive [1.0, 0.9, 0.72]` / `emissiveStrength 1.6`. Ça ne marchera **jamais** :
  `engine/shaders/gbuffer.frag:207` écrit
  `vec3 emissive = u_emissive.rgb * u_emissive.a * albedo.rgb;` — l'émissif est multiplié par
  l'albédo, texel par texel. `background_classement.png` mesure **0,0158** de réflectance
  (linéaire 0,009 ; 0,017 ; 0,030 — et c'est un **bleu**, B/R = 3,4). 0,0158 × 1,6 = 0,025.
  Aucune valeur d'`emissiveStrength` ne rattrape un albédo de 1,6 %.
- **Le jukebox est un caisson noir à couvercle jaune.** `props[jukebox]` emploie
  `materials[bordeau_uni]` = `lacquered_wood.jpg` × [0,40 ; 0,07 ; 0,12]. `lacquered_wood.jpg`
  mesure **0,0148** ; après le multiplicateur, **0,0030**. Le panneau de façade emploie
  `materials[rouge]` = même texture × [0,86 ; 0,14 ; 0,16] → **0,0064**. Les deux matériaux de
  l'objet le plus coloré de la salle sont **plus sombres que la moquette noire de 2020**. Un
  jukebox est une lanterne ; celui-ci est un trou.
- **Le mur nord est un aplat kaki de 19,3 × 2,4 m**, cassé par une seule ligne de 2,8 cm
  (`mouldings[cimaise_hall]`). Le bandeau haut de mur bordeaux du poste 10 de `DESIGN-SALLE.md`
  n'a pas été posé.

### 1.4 `depuis_bar` (−2,6 ; 1,7 ; 5,0 · yaw 270) — méd. **14** · % < 16 : **53,9**

Le trajet inverse : du bar vers l'îlot. 4,2 m de moquette, puis les bornes.

- **Deux bornes sur six de la rangée nord disent qu'elles ne marchent pas.**
  `cabinets[borne_arcade_7]` (pacman) porte `screen: "ecran_bientot"` → `coming_soon.jpg`, et
  `cabinets[borne_arcade_8]` (piano) porte `screen: "ecran_chargement"` → `chargement.png`. Sur la
  capture, l'une affiche des bandes de danger jaune et noir, l'autre est noire. Leurs marquees
  disent pourtant `PAC-MAN` et `PIANO`, et les deux jeux sont jouables. Idem pour
  `borne_arcade_18` (pacman, est) et `borne_arcade_15` (piano, ouest). C'est le poste 12 de
  `DESIGN-SALLE.md` fait à moitié : les marquees ont été dessinés, les écrans non.
- Cinq piliers rouges occupent la moitié gauche.

### 1.5 `haut_salle` (0 ; 2,85 ; −6,8 · yaw 90 · pitch −20) — méd. **12** · % < 16 : **57,7**

Vue de dessus de la salle entière, prise sous les poutres.

- **L'îlot est un ruban à l'horizon ; le sol occupe 60 % du cadre et il est vide** à gauche, à
  droite, devant, derrière.
- **Les motifs du tapis sont trop grands.** `materials[sol]` déclare `uvMetres 2.4`.
  `floor.jpg` porte huit motifs par planche : l'entraxe au sol vaut donc ≈ **0,85 m** et un motif
  ≈ **0,60 m**. Sur `classement`, la cerise néon posée au pied de la borne de classement est
  **aussi large que la borne** (0,72 m). Un tapis d'arcade réel a des motifs de 20 à 35 cm. À
  0,60 m ils cessent d'être une texture et deviennent des objets — des objets qu'on ne peut pas
  ramasser, dans une salle où il n'y a rien à ramasser.
- **Le dos de la borne de classement est une dalle rouge plein centre.** `caisson_leaderboard`
  réflectance 0,067, saturation 0,79, sans aucun élément.

### 1.6 `travee_ouest` (−6,2 ; 1,7 ; 3,6 · yaw 200) — méd. 21

**Ce qui marche, et il faut le dire.** Les trois marquees `PIANO`, `DEMINEUR`, `SHOOTER` se lisent
à 4 m, dans la fonte du jeu, rétroéclairés. `tools/marqueeart` a fait exactement son travail.
**Ne rien y toucher.**

**Ce qui tue l'ambiance.**

- **Deux bornes voisines de la même couleur.** `borne_arcade_13` (shooter,
  `caisson_shooter` [0,56 ; 0,18 ; 0,07]) et `borne_arcade_14` (demineur,
  `caisson_demineur` [0,52 ; 0,38 ; 0,08]) sont côte à côte à 0,80 m d'entraxe. Sur la capture,
  elles rendent le même tan. C'est **la seule adjacence ratée** des dix-neuf, et elle est sur le
  mur le plus visible depuis le bar.
- **Les caissons rendent crayeux.** Voir §3 : à cet endroit, l'ambiante constante fournit **39 %
  de la lumière diffuse** et les rampes du plafond sont à l'aplomb des bornes, donc à cosinus
  d'incidence quasi nul sur leurs faces. Un caisson à 0,90 de saturation éclairé pour moitié par
  une constante grise remonte son canal le plus faible : c'est la définition du pastel, et c'est
  l'« essai raté n° 2 » que `DESIGN-SALLE.md` §3.5 s'interdisait — arrivé par la lumière au lieu
  d'arriver par l'albédo.
- **Deux affiches identiques.** `props[affiche_ouest]` (`affiche_1`) et
  `props[affiche_ouest_b]` (`affiche_2`) sont dans le même cadre et se lisent pareil.
- **6 m de mur nu sans rien**, sauf un carré blanc de 12 cm.

### 1.7 `bar` — méd. 38

**Ce qui marche.** Le cadrage frontal, l'enseigne, les deux tabourets, le boombox.

**Ce qui tue l'ambiance.**

- **Le comptoir est passé de la nappe de pique-nique au confetti.** Le poste 9 de
  `DESIGN-SALLE.md` demandait `comptoir.uvMetres` 2,6 → **0,9** pour un triangle Memphis de 11 cm.
  Le fichier déclare **0,3**. `desk.jpg` fait 490 px pour ≈ 8 cellules : à 0,3 m par répétition le
  triangle mesure **3,8 cm**. Le motif ne se lit plus comme un stratifié, il se lit comme du grain.
  La correction a dépassé sa cible d'un facteur **3**.
- **Le plateau du comptoir est vide sur 5,4 m.** Pas une bouteille, pas un verre, pas une caisse,
  pas un bocal à jetons. Poste 14 de `DESIGN-SALLE.md`, non fait.
- **La suspension du comptoir n'éclaire pas le comptoir.** `lights[suspension_comptoir]` est à
  (−4,35 ; 1,36 ; 6,2), soit à **2,25 m** de l'axe de l'enseigne et au bout ouest du meuble. Sur
  la capture, la flaque tombe hors cadre à droite et le centre du bar est à la lumière d'ambiance.

### 1.8 `classement` — méd. 18 · % < 16 : 44,0

**Ce qui marche, et c'est le meilleur cadrage de la salle.** Une borne isolée, centrée, marquee
lisible, écran lisible, deux affiches en contrepoint. C'est le seul endroit du jeu qui ressemble à
une photographie composée. **Ne pas y toucher, sauf ce qui suit.**

- **L'estrade est un carré noir de 12 cm.** `boxes[estrade]` : `size [2.4, 0.12, 2.4]`, matériau
  `sol_sombre` = `moquette_noire.jpg`, réflectance mesurée **0,0031**. En 2020 l'estrade était le
  **deuxième émetteur de la salle** : `salle.mtl:4`, matériau `Material` sur
  `CENTRE_MACHINE_SOL_Cube`, `map_Kd moquette.jpg`, **`Ke 0.229790`**. Elle est aujourd'hui la
  surface la plus sombre du décor. C'est une inversion exacte de l'ADN, et elle tient en deux
  lignes (§2.4).
- **Les tiers gauche et droit sont de la moquette nue.** Rien entre la borne et les murs.
- Le poste 11 de `DESIGN-SALLE.md` (l'estrade à ses 29 cm de 2020) n'est pas fait ; le poste 3
  (le nez de marche fantôme) l'est — `mouldings[nez_estrade]` épouse bien
  [−1,2 ; −4,3] → [1,2 ; −1,9].

### 1.9 `salon_est` (7,6 ; 1,7 ; −1,2 · yaw 250) — méd. 15 · % < 16 : 50,7

C'est ici que « les objets sont mal positionnés » est le plus littéral.

- **Le mobilier n'est pas d'une salle d'arcade.** `props[canape]` est `sofa_03`, un canapé
  **de style Régence** à dossier chantourné en bois verni ; `props[fauteuil_salon]` est
  `ArmChair_01`, un fauteuil **Louis XV** blanc cabriolet ; `props[table_basse]` est
  `CoffeeTable_01` avec `materials[table_salon]` = `black_oak_veneer.jpg` × [1,32 ; 1,02 ; 0,72],
  soit un chêne verni orangé. Trois meubles de salon bourgeois du XIXᵉ dans une salle d'arcade de
  quartier. Aucun n'est faux **en soi** — ils sont faux **là**.
- **Le coin salon ne regarde rien.** `canape` est à (6,1 ; −4,6) `yaw 205` ; les bornes est sont à
  x = 9,1, z de −1,2 à −2,8 ; l'îlot est à z ≈ 0. Assis sur ce canapé on regarde la table basse et
  un mur. Le seul endroit de la salle où l'on peut s'asseoir **tourne le dos aux deux endroits où
  l'on joue**. Le mandat dit « affronter leurs amis » : il faut pouvoir regarder jouer.
- **Deux piliers rouges occupent la moitié gauche du cadre.**

### 1.10 `vide_sud` / `vide_sud_large` — méd. 21 et **15**

- **Le mur sud fait 19,3 m et porte trois affiches**, à (−3,2 ; 1,41), (1,2 ; 1,53) et
  (4,6 ; 1,36). Trois hauteurs différentes, 4 m d'écart, format A2. Entre elles : rien.
- **La bande sud est le plus grand vide de la salle** : 5,12 m au point (−4,4 ; −7,2) sans le
  moindre meuble (§2.1).

### 1.11 `billard` — méd. 20 · % < 16 : 42,3

**C'est le seul coin de la salle qui est un lieu, et il faut comprendre pourquoi avant de
toucher au reste.** Il a trois choses que le reste n'a pas :

1. **une source à hauteur d'homme** — `lights[suspension_billard]`, y = **1,56 m**, intensité 150,
   portée 4,2 ;
2. **un luminaire visible** — `props[suspension_billard]`, câble + abat-jour + ampoule ;
3. **des objets entre 0,8 et 1,7 m** — la table, les trois billes, la cible de fléchettes, deux
   affiches.

Mesuré : l'éclairement au sol sous cette suspension vaut **3,95**, contre **0,59** de médiane sur
le reste du hall — un rapport de **6,7**. C'est la seule flaque de la salle. Tout le §3 découle de
cette observation.

**Ce qui reste à corriger ici** : trois billes blanches sans numéro ni triangle, pas de queue, pas
de craie — poste 15 de `DESIGN-SALLE.md`, non fait.

### 1.12 `borne` — méd. 56 · % > **200 : 8,5**

**Ce qui marche.** L'échelle. Le manche, les boutons, le panneau, l'écran : on est devant une
borne, à la bonne hauteur. Les écrans de borne éclairent le joueur (§3.2). C'est réussi.

**Ce qui tue l'ambiance.**

- **8,5 % de pixels brûlés.** Le poste 6 de `DESIGN-SALLE.md` visait **≤ 5** après `ecran_*`
  1,70 → 1,35. La baisse est appliquée ; le critère n'est pas atteint, parce que le sol s'est mis
  à luire dans le même temps (`sol.emissiveStrength 0.25`) et que l'adaptation d'exposition en tient
  compte.
- **La sérigraphie du panneau de commande est rendue à 7 fois son échelle de dessin.**
  `assets/CMakeLists.txt:145` appelle `panelart --field=0.30`, donc `--step` au défaut de **34 px**
  sur une planche de 1024 px. `materials[borne_commande]` porte `fit: true`, donc la planche couvre
  le panneau (≈ 0,62 m) : le losange devrait mesurer **2,1 cm**. Sur la capture j'en compte trois à
  quatre sur toute la largeur, soit ≈ **15 cm**. Les UV du panneau dans `assets/models/borne`
  n'exposent qu'une fenêtre de la planche. À corriger dans `assets/blender/borne.py` ou par
  `--step=6` ; en l'état, le panneau de commande de dix-neuf bornes se lit comme une nappe.

---

## 2. Le centre de la salle

### 2.1 La mesure du vide

Le hall utile — l'emprise jouable moins le bloc sanitaire et le sas — fait **250,4 m²**. J'ai
échantillonné au décimètre la distance de chaque point au **meuble** le plus proche. « Meuble »
veut dire : une borne, le billard, le comptoir, le canapé, le fauteuil, le jukebox, le
distributeur, un tabouret, la cible. Les **piliers et les poutres n'en sont pas** : on ne va pas
vers un pilier.

| Seuil | Aire | Part du hall |
|---|---|---|
| à plus de **2,0 m** de tout meuble | **60,9 m²** | **24 %** |
| à plus de **2,5 m** | 33,5 m² | 13 % |
| à plus de **3,0 m** | 19,7 m² | 8 % |
| à plus de **3,5 m** | 11,0 m² | 4 % |

Le point le plus désert est à **5,12 m** de tout, en (−4,4 ; −7,2).

Les cinq poches, centres séparés d'au moins 3 m :

| # | Centre (x ; z) | Distance au meuble le plus proche | Ce que c'est |
|---|---|---|---|
| 1 | (−4,4 ; −7,2) | **5,12 m** | bande sud, derrière le classement |
| 2 | (9,6 ; 4,2) | **4,93 m** | angle nord-est, contre la cloison du sas |
| 3 | (6,8 ; 2,8) | **4,01 m** | travée est, ce qu'on traverse en sortant du sas |
| 4 | (−5,7 ; 2,5) | **2,92 m** | travée ouest, entre le billard et le bar |
| 5 | (2,0 ; 3,5) | **2,69 m** | bande nord, entre l'îlot et le bar |

Les poches 3 et 5 sont sur le trajet d'arrivée. La poche 1 est là où l'on va après une partie.

### 2.2 Le principe : trois choses, et pas plus

Ce que la salle ne propose pas, et qu'une salle de quartier propose toujours :

1. **où prendre ses jetons** — il n'y a aucun monnayeur central ; `props[distributeur]` est à
   (−9,2 ; 5,6), c'est-à-dire dans l'angle le plus caché du hall ;
2. **où s'asseoir pour regarder jouer** — il y a **un** tabouret et **un** canapé qui tourne le dos
   aux bornes ; à dix-neuf bornes, ça fait deux places pour deux spectateurs ;
3. **quoi faire à deux sans écran** — le billard, seul, dans un coin.

Rien de ce qui suit n'ajoute une vingtième borne. Le nom du jeu est un compte.

### 2.3 Les objets à poser, avec leur position et leur raison

Toutes les cotes sont en mètres, en repère monde, `y` au sol sauf mention. Toutes les pièces sont
des primitives que `roomgen` sait déjà bâtir : `box`, `panel`, `cylinder`, `sphere`, chacune
acceptant `yaw`, `pitch` et `roll` (`tools/roomgen.c:1716-1718`). **Aucun modèle importé n'est
nécessaire.** Coût mesuré : la scène compte aujourd'hui 379 944 sommets pour 170 210 triangles ;
les dix familles d'objets ci-dessous en ajoutent environ 2 900, soit **+0,8 %**.

**La convention d'ancrage, lue dans le code, parce qu'elle n'est écrite nulle part.**
`tools/geo_shapes.c:220-224` termine `geo_box` par
`position[1] += half.y` — « la boîte est construite centrée puis relevée pour qu'elle repose sur
Y = 0 ». `geo_cylinder` (`geo_shapes.c:1078-1081`) émet ses anneaux à `y = 0` et `y = height`. Le
profil de la sphère est « posée sur Y = 0 » (`roomgen.c:1798`).

> Pour les quatre primitives, **`at` est le centre en x et z, et le DESSOUS en y.**

Trois clés courantes n'existent pas et feraient échouer le build : il n'y a **pas** de `repeat` sur
un morceau de prop (`repeat` n'est lu que sur `boxes` et `lights`, `roomgen.c:735` et `:2085`), et
`faces` n'accepte que `"all"`, `"sides"` ou `"noBottom"` (`roomgen.c:1733-1735`). La clé `_` est en
revanche acceptée partout et sert de commentaire.

#### A. Les deux bancs dos à dos, au nord de l'îlot — *l'objet qui manque le plus*

| | |
|---|---|
| `props[banc_ilot_ouest]` | `at [-2.4, 0, 2.45]`, `yaw 0` |
| `props[banc_ilot_est]` | `at [2.4, 0, 2.45]`, `yaw 0` |

Chacun : deux assises dos à dos autour d'un dossier commun.

```jsonc
"parts": [
  { "_":"les deux joues", "type":"box", "material":"bois_noir",
    "at":[-0.80, 0.00, 0.00], "size":[0.10, 0.40, 0.94], "chamfer":0.008 },
  { "type":"box", "material":"bois_noir",
    "at":[ 0.80, 0.00, 0.00], "size":[0.10, 0.40, 0.94], "chamfer":0.008 },
  { "_":"le dossier commun aux deux assises", "type":"box", "material":"bois_noir",
    "at":[ 0.00, 0.44, 0.00], "size":[1.80, 0.46, 0.10], "chamfer":0.010 },
  { "_":"assise nord, regarde le bar", "type":"box", "material":"cuir",
    "at":[ 0.00, 0.40, 0.25], "size":[1.80, 0.08, 0.40], "chamfer":0.012 },
  { "_":"assise sud, regarde l'ilot", "type":"box", "material":"cuir",
    "at":[ 0.00, 0.40,-0.25], "size":[1.80, 0.08, 0.40], "chamfer":0.012 }
]
```

Emprise 1,80 × 0,94, assise à **0,48 m**, haut de dossier à **0,90 m**.

**Pourquoi là, exactement là.** La rangée nord de l'îlot (bornes 7 à 12) est à z = +0,44 et regarde
+z. Un joueur debout devant elle a les pieds à z ≈ **1,34**. L'assise à z = 2,45 le place à
**1,1 m derrière lui**, ce qui est la distance à laquelle on voit l'écran par-dessus son épaule.
Les deux bancs sont à x = ±2,4, c'est-à-dire **en face des deux blocs de trois bornes**, et ils
laissent libre la trouée centrale x ∈ [−1,2 ; 1,2] : **la fuyante de la vue `allee` n'est pas
bouchée.** Le dos de l'autre assise regarde le bar : on s'y assoit avec un verre.

**Pourquoi `cuir`.** `materials[cuir]` est déclaré dans le fichier et **employé par personne**. Il
porte `cuir_rouge.jpg`, une des textures de 2020 qui n'a jamais été assombrie (0,698 de
réflectance brute). Les bancs sont donc en skaï rouge de 2020, sans un octet ajouté au dépôt.

#### B. Le monnayeur, en bout de comptoir

`props[monnayeur]` — `at [0.95, 0, 6.55]`, `yaw 180` (face vers −z, vers la salle).

```jsonc
"parts": [
  { "_":"le corps", "type":"box", "material":"laque_sombre",
    "at":[0.00, 0.00, 0.00], "size":[0.70, 1.42, 0.60], "chamfer":0.014 },
  { "_":"la facade, face locale +z", "type":"panel", "material":"jaune",
    "at":[0.00, 1.02, 0.302], "size":[0.52, 0.34] },
  { "_":"le bandeau qui le rend trouvable", "type":"panel", "material":"panneau_lumineux",
    "at":[0.00, 1.34, 0.302], "size":[0.56, 0.14] },
  { "_":"la sebile a jetons", "type":"box", "material":"borne_noir",
    "at":[-0.11, 0.62, 0.315], "size":[0.22, 0.10, 0.03], "chamfer":0.004 },
  { "type":"cylinder", "material":"bouton_rouge",
    "at":[0.18, 0.72, 0.31], "radius":0.028, "height":0.02, "sides":12, "pitch":90 }
]
```

Le corps occupe z de −0,30 à +0,30 ; la face avant est donc en z local **+0,30**, et un `panel`
ayant sa normale en +Z local (`roomgen.c:1759`), les quatre pièces de façade se posent bien à
z ≈ +0,30. Avec `yaw 180` cette face regarde le monde en −z, c'est-à-dire la salle.

**Pourquoi là.** Le comptoir occupe x de −5,5 à +0,3, z de 5,85 à 6,75. Le monnayeur se pose dans
son prolongement, à 0,65 m de son extrémité est, dos au mur nord (z = 7,2). En sortant du sas
(l'ouverture est en x = 4,6, z de 4,6 à 6,0, on marche vers −x), c'est le premier objet éclairé du
champ, à 3,7 m. **On entre, on prend ses jetons, on joue.** La bande `panneau_lumineux` est ce qui
le rend trouvable de l'autre bout de la salle : c'est le seul matériau émissif du décor qui ne soit
ni un écran ni une dalle de plafond.

#### C. Le baby-foot, dans la travée ouest

`props[babyfoot]` — `at [-4.0, 0, 2.80]`, `yaw 0`. Emprise 1,40 × 0,76, plateau à **0,87 m**.

```jsonc
"parts": [
  { "_":"quatre pieds", "type":"box", "material":"bois_noir",
    "at":[-0.62, 0.00,-0.31], "size":[0.09, 0.62, 0.09], "chamfer":0.006 },
  { "type":"box", "material":"bois_noir", "at":[ 0.62, 0.00,-0.31], "size":[0.09,0.62,0.09],
    "chamfer":0.006 },
  { "type":"box", "material":"bois_noir", "at":[-0.62, 0.00, 0.31], "size":[0.09,0.62,0.09],
    "chamfer":0.006 },
  { "type":"box", "material":"bois_noir", "at":[ 0.62, 0.00, 0.31], "size":[0.09,0.62,0.09],
    "chamfer":0.006 },
  { "_":"la caisse, dessus a 0,86", "type":"box", "material":"bois",
    "at":[0.00, 0.62, 0.00], "size":[1.40, 0.24, 0.76], "chamfer":0.012 },
  { "_":"le terrain : panneau couche, normale +Z basculee en +Y", "type":"panel",
    "material":"billard_repere", "at":[0.00, 0.865, 0.00], "size":[1.22, 0.62], "pitch":-90 },
  { "_":"huit barres, ecrites une par une : il n'y a pas de repeat sur un morceau de prop",
    "type":"cylinder", "material":"borne_chrome",
    "at":[-0.55, 0.93,-0.28], "radius":0.011, "height":1.10, "sides":8, "roll":90 }
  // …répéter à z = -0,20 / -0,12 / -0,04 / 0,04 / 0,12 / 0,20 / 0,28
]
```

Un cylindre part de son `at` et s'étend sur `height` le long de son axe local +Y ; couché par
`roll 90`, il part donc de `at[0]` et non de son milieu, d'où le −0,55. Le sens de la rotation est
à vérifier au premier build : si la barre part du mauvais côté, c'est `roll -90`.

**Pourquoi là.** Le pilier ouest le plus proche est à (−5,4 ; 2,4), emprise x −5,64…−5,16 : le
baby-foot s'arrête à x = −4,70, il reste **0,46 m** de passage. Il est à 3,1 m du bout ouest du
comptoir et à 4,4 m du billard : les trois « jeux sans écran » forment une diagonale ouest, ce qui
donne à cette moitié de la salle une raison d'être traversée.

**Pourquoi un baby-foot et pas un flipper.** Un flipper est une machine : le joueur se demandera
pourquoi elle ne se lance pas, et on retombe sur le défaut des quatre écrans « pas encore porté »
(§1.4). Un baby-foot est un meuble, comme le billard — personne n'attend qu'il démarre. Et c'est
l'objet qui dit le plus exactement « affronter ses amis comme en enfance » : on s'y tient à quatre,
debout, à hauteur de conversation.

#### D. Les deux bancs de la bande sud

| | |
|---|---|
| `props[banc_sud_ouest]` | `at [-4.0, 0, -6.80]`, `yaw 0` — dossier au mur, face vers +z |
| `props[banc_sud_est]` | `at [3.0, 0, -6.80]`, `yaw 0` |

Mêmes pièces qu'en A, avec trois différences : **une seule assise**, celle en z local **+0,25** ;
un dossier porté à `size [L, 0.62, 0.10]` en `at [0, 0.44, -0.20]`, donc adossé au mur ; et une
emprise en z de 0,60 au lieu de 0,94. Longueur **2,40 m** à l'ouest, **2,00 m** à l'est. Les deux
sont à z = −6,80, soit 0,40 m du mur (z = −7,2) : le dossier le touche, les pieds non.

**Pourquoi là.** La bande sud est la poche la plus profonde (5,12 m). Elle est aussi **derrière la
borne de classement**, qui regarde +z : c'est l'endroit d'où l'on voit à la fois le tableau des
scores et toute la rangée sud de l'îlot. On y va après une partie. Aujourd'hui on n'y va jamais,
parce qu'il n'y a rien, pas même un mur qui dit quelque chose.

#### E. Le tableau de la semaine, au mur sud

`props[tableau_semaine]` — `at [0, 1.58, -7.08]`, `yaw 0`.

Un `panel` de 2,20 × 0,90 encadré de deux `box` en `borne_cadre` de 0,06, et une bande
`panneau_lumineux` de 2,30 × 0,10 au-dessus, à y = 2,08.

**La planche est à dessiner** : `tools/marqueeart` la sait déjà faire, il produit du texte en fonte
de jeu sur fond sombre (`--titre=`, `--teinte=`, `--width=`, `--height=`). Un appel
`marqueeart --titre="MEILLEURS DE LA SEMAINE" --teinte=1,0.72,0.20 --width=1536 --height=628`
suffit à obtenir la planche, et le matériau la porte en émissif comme les marquees
(`emissive [1.0, 0.97, 0.9]`, `emissiveStrength 1.52`).

**Pourquoi là.** `DESIGN-SALLE.md` §4.2 demande une deuxième lecture des scores. Celle-ci se lit
depuis les bancs sud, depuis la rangée sud de l'îlot, et depuis l'entrée par la trouée centrale :
trois lignes de vue pour un panneau plat.

#### F. Deux mange-debout

| | |
|---|---|
| `props[mange_debout_nord]` | `at [1.60, 0, 3.90]` |
| `props[mange_debout_ouest]` | `at [-6.90, 0, 3.30]` |

Trois `cylinder`, empilés (rappel : `at[1]` est le dessous) :

```jsonc
"parts": [
  { "type":"cylinder", "material":"borne_noir",  "at":[0,0.00,0], "radius":0.28,
    "height":0.03, "sides":20 },
  { "type":"cylinder", "material":"borne_chrome","at":[0,0.03,0], "radius":0.035,
    "height":0.99, "sides":12 },
  { "type":"cylinder", "material":"bois",        "at":[0,1.02,0], "radius":0.31,
    "height":0.04, "sides":24 }
]
```

Plateau à **1,06 m** : la hauteur d'un mange-debout, et celle à laquelle on pose un verre sans
se pencher.

**Pourquoi.** C'est l'objet le moins cher qui existe — six primitives pour deux meubles — et c'est
le signe le plus fiable qu'une salle est **utilisée**. On y pose un verre, on y attend son tour, on
y regarde. Le nord occupe la poche 5 ; l'ouest occupe la poche 4, sur le passage bar → billard.

#### G. Deux poubelles

`props[poubelle_ilot]` — `at [-1.05, 0, 2.20]` — et `props[poubelle_sud]` — `at [-1.90, 0, -6.75]`.
Modèle `models/metal_trash_can` (déjà importé, déjà employé au sas), matériau `poubelle`.

**Pourquoi.** Une poubelle en bord d'allée est ce qui distingue un plan d'architecte d'un lieu où
l'on boit. Celle de l'îlot est au débouché de la trouée centrale, à 1,3 m du banc ouest : elle
marque le coin sans boucher l'axe.

#### H. Le socle des piliers

`boxes[socle_pilier_ouest]` et `boxes[socle_pilier_est]` : `size [0.56, 0.14, 0.56]`, matériau
`bois_noir`, `at [-5.4, 0, -4.8]` et `[5.4, 0, -4.8]`, avec le même
`repeat { "count": 5, "step": [0, 0, 2.4] }` que les piliers.

**Pourquoi.** Dix dalles rouges qui sortent du sol sans embase se lisent comme des volumes posés.
Une plinthe de 14 cm — la même cote que `mouldings[plinthe_est]` — les raccorde au sol et fait
qu'on les lit comme de la construction. Coût : dix boîtes, 240 sommets.

#### I. La vitrine à lots, contre la cloison du sas

`props[vitrine_lots]` — `at [7.60, 0, 3.95]`, `yaw 180` (face vers −z, vers la travée est).
Emprise 1,80 × 0,42, hauteur 1,90.

```jsonc
"parts": [
  { "_":"le socle plein", "type":"box", "material":"laque_sombre",
    "at":[0.00, 0.00, 0.00], "size":[1.80, 0.86, 0.42], "chamfer":0.012 },
  { "_":"le caisson vitre, en cadre peint : le moteur n'a pas de transparence ici",
    "type":"box", "material":"borne_cadre",
    "at":[0.00, 0.86, 0.00], "size":[1.80, 0.94, 0.42], "chamfer":0.008 },
  { "_":"les lots, en facade", "type":"panel", "material":"affiche_6",
    "at":[0.00, 1.28, 0.212], "size":[1.62, 0.76] },
  { "type":"panel", "material":"panneau_lumineux",
    "at":[0.00, 1.84, 0.212], "size":[1.76, 0.10] }
]
```

**Pourquoi là.** Avant ajout, le point le plus désert du hall après la bande sud était **(9,6 ; 4,2)
à 4,93 m de tout meuble** : la bande étroite coincée entre la cloison du sas (z = 4,2, de x = 4,6 à
9,65) et les bornes est. C'est aussi le premier mur qu'on longe en sortant du sas. Une vitrine à
lots — la vitrine à peluches, à briquets et à porte-clés de toutes les salles de quartier — est un
panneau plat sur un socle : elle habille 1,8 m de cloison, elle porte une bande lumineuse à 1,86 m
qui balise la sortie du sas, et elle donne une raison de regarder à gauche.

*Elle est aussi la seule proposition de ce document qui n'a pas d'antécédent direct en 2020.* Je
l'assume : la vitrine à lots est à une salle d'arcade de quartier ce que la cible de fléchettes est
au coin billard, et la salle a déjà accepté la seconde.

#### J. Ce qu'il faut déplacer, pas ajouter

| Objet | Aujourd'hui | Proposé | Pourquoi |
|---|---|---|---|
| `props[canape]` | `at [6.1, 0, -4.6]`, `yaw 205` | `at [7.30, 0, -1.60]`, **`yaw 270`** | Regarde alors les trois bornes est (x = 9,1 ; z de −1,2 à −2,8) à 1,8 m. On s'assoit pour voir jouer. |
| `props[fauteuil_salon]` | `at [7.55, 0, -4.05]`, `yaw 252` | `at [7.20, 0, -3.60]`, `yaw 300` | Reste en vis-à-vis oblique du canapé sans lui masquer les bornes. |
| `props[table_basse]` | `at [6.2, 0, -3.2]`, `yaw 8` | `at [6.30, 0, -2.70]`, `yaw 12` | Entre les deux sièges, pas devant. |
| `props[distributeur]` | `at [-9.2, 0, 5.6]`, `yaw 90` | `at [-9.25, 0, -0.30]`, `yaw 90` | Un distributeur de boissons se met **sur le trajet**, pas dans l'angle mort. À x = −9,25 il occupe x −9,64…−8,86 et z −0,93…0,53 : sur le mur ouest, entre le tapis technique et le billard, vu de toute la travée ouest. |
| `props[tapis_technique]` | `at [-8.2, 0, 1.2]` | `at [-8.2, 0, 1.70]` | Dégage les 8 cm de recouvrement avec le distributeur déplacé. |
| `props[cible_flechettes]` | `at [-9.52, 1.68, -1.6]` | `at [-9.52, 1.68, -2.45]` | Le distributeur passerait devant. À −2,45 la cible est en vis-à-vis du billard (z = −1,6), ce qui est sa place. |
| `props[affiche_ouest_b]` | `affiche_2` | `affiche_5` | Deux affiches qui se lisent pareil à 8 m l'une de l'autre sur le même mur (§1.6). |
| `props[suspension_comptoir]` et `lights[suspension_comptoir]` | x = −4,35 | **x = −2,60** | Sur l'axe de l'enseigne et au milieu du comptoir (§1.7). |

#### K. Le résultat, mesuré à l'avance

J'ai refait le calcul du §2.1 avec les objets A à J posés aux coordonnées ci-dessus.

| Seuil | Avant | **Après** |
|---|---|---|
| aire à plus de **2,0 m** de tout meuble | 60,9 m² (24 %) | **18,0 m² (7 %)** |
| aire à plus de **2,5 m** | 33,5 m² (13 %) | **5,8 m² (2 %)** |
| aire à plus de **3,0 m** | 19,7 m² (8 %) | **1,8 m² (1 %)** |
| point le plus désert | **5,12 m** en (−4,4 ; −7,2) | **3,95 m** en (−9,6 ; 7,2) |

Le point le plus désert devient l'angle nord-ouest de la salle — un angle de pièce, pas un milieu
de salle. C'est la différence qu'on cherche.

Et une vérification qui compte autant : les poches restantes de plus de 2 m sont en (−3,2 ; −3,7)
et (3,1 ; −3,7), c'est-à-dire **la bande de circulation entre la rangée sud de l'îlot et
l'estrade**. Elle doit rester vide : on y marche.

#### L. Les points de vue à ajouter à `captures`

Une vue non capturée est une vue non contrôlée. Les trois grands vides n'en ont aucune.

```jsonc
{ "name":"centre",  "position":[0.0, 1.7, 2.6],  "yaw":270, "pitch":-4 },
{ "name":"sud",     "position":[0.0, 1.7,-5.4],  "yaw":90,  "pitch":-3 },
{ "name":"arrivee", "position":[4.2, 1.7, 5.2],  "yaw":200, "pitch":-4 },
{ "name":"travee",  "position":[-6.2,1.7, 3.6],  "yaw":200, "pitch":-5 }
```

Et `captures[orbite]` : `height` **2,35 → 1,72**. À 2,35 m la caméra est au-dessus des marquees et
sous les poutres ; à 1,72 elle est à hauteur d'œil, ce qui est la seule hauteur dont on sache
juger. Le rayon reste 6,5 — au-delà, le cercle sort du mur nord (z = 7,2) — donc l'anneau de
piliers restera traversé ; c'est le prix d'une orbite circulaire dans une salle rectangulaire, et
ça devient acceptable dès que les piliers ont une embase (H) et un grain (§4.5).

---

## 3. La lumière

### 3.1 Ce que l'intention déclare

`salle.room.json`, commentaire `_lumieres` :

> « quatre plafonniers faibles balisent la circulation, des néons colorés rasent les murs, et
> l'essentiel de la lumière vient des **DIX-NEUF ÉCRANS DE BORNES**, que le moteur ajoute lui-même.
> […] dans une vraie salle d'arcade, ce sont les machines qui éclairent. »

### 3.2 Ce qui est vrai : là où l'on joue, l'intention est tenue

`engine/scene/ns_scene.c:1243-1358` ajoute bien dix-neuf sources, une dans le plan de chaque dalle,
teintées par le jeu et désaturées à 45 %, intensité 30, portée 2,8, demi-rayon 0,40, scintillement
désynchronisé.

J'ai calculé l'éclairement reçu par un plan vertical placé à **0,90 m devant l'écran de la borne 1,
à 1,45 m de haut** — c'est-à-dire le visage du joueur :

| | |
|---|---|
| éclairement direct total | **6,011** |
| dont les dix-neuf écrans | **5,793 — soit 96 %** |
| ambiante constante | 0,233, soit **11 %** du diffus |

**C'est exactement ce que le commentaire promet, et il ne faut y toucher sous aucun prétexte.**
La vue `borne` le confirme : médiane 56, contre 20 avant les corrections de `DESIGN-SALLE.md`.

### 3.3 Ce qui est faux : là où l'on marche, les écrans n'éclairent rien

Même formule d'atténuation (`engine/shaders/lighting.frag:142-149`), appliquée au sol du hall sur
25 044 points au décimètre.

| | |
|---|---|
| sources après `repeat` | **50** — 31 déclarées + 19 écrans |
| sources posées à **y = 2,74** (le plan du faux plafond) | **18** |
| budget d'intensité : plafonniers | **1 376, soit 51 %** |
| budget d'intensité : autres sources déclarées | 737, soit 28 % |
| budget d'intensité : les dix-neuf écrans | 570, soit **21 %** |
| part des écrans dans l'éclairement du sol, au point **médian** | **0 %** |
| aire du sol où les écrans donnent plus de 20 % du direct | **70,2 m² sur 250,4, soit 28 %** |

Et l'ambiante :

| | |
|---|---|
| `ns_render.c:361` + `:463` : ambiante × 2,6 en `high` | luminance **0,233** |
| direct médian au sol, converti en diffus (`/π`) | **0,226** |
| **part de la constante dans le diffus, point médian du sol** | **51 %** |
| idem, mur sud à 1,3 m | **74 %** |

**Plus de la moitié de la lumière du hall est une constante sans direction, sans source et sans
couleur.** Une constante ne fait ni flaque, ni ombre, ni contraste chaud/froid. Elle ne peut
produire qu'une chose : un aplat. C'est la formulation arithmétique de « n'y atmoshpher ».

### 3.4 La leçon du billard : baisser la lampe, pas monter l'intensité

Comparaison de deux sources existantes, calcul au sol, à l'aplomb :

| Source | Hauteur | Intensité | Portée | Éclairement au sol dessous |
|---|---|---|---|---|
| `rampe_ilot_nord` (dalle de faux plafond) | 2,74 | 74 | 4,6 | **0,49** |
| `suspension_billard` (suspension) | **1,56** | 150 | 4,2 | **3,74** |

**7,6 fois plus, pour deux fois l'intensité.** Le facteur qui travaille est le carré de la
distance, pas le réglage. Une salle tamisée ne se règle pas en baissant les plafonniers — l'essai
raté n° 1 l'a déjà montré — elle se règle en **descendant les lampes à la hauteur des gens**.

### 3.5 Ce qu'il faut faire, avec les valeurs

**(a) Six suspensions basses, au-dessus des six lieux du §2.** Nouvelles entrées de `lights`,
`color [1.0, 0.78, 0.50]`, `radius 0.10`.

| `name` | `at` | `intensity` | `range` |
|---|---|---|---|
| `suspension_banc_ouest` | −2,40 ; **1,75** ; 2,45 | 110 | 3,6 |
| `suspension_banc_est` | 2,40 ; **1,75** ; 2,45 | 110 | 3,6 |
| `suspension_babyfoot` | −4,00 ; **1,75** ; 2,80 | 110 | 3,6 |
| `suspension_banc_sud_o` | −4,00 ; **1,75** ; −6,40 | 110 | 3,6 |
| `suspension_banc_sud_e` | 3,00 ; **1,75** ; −6,40 | 110 | 3,6 |
| `suspension_monnayeur` | 0,95 ; **1,90** ; 6,55 | 70 | 2,8 |

Chacune **doit** avoir son luminaire visible : le plan de la salle s'interdit une source sans objet,
et cette règle est bonne. Copier les trois pièces de `props[suspension_billard]`, décalées de la
nouvelle hauteur — le luminaire du billard a son ampoule à 1,66 m pour une source à 1,56 :

```jsonc
"parts": [
  { "_":"le cable", "type":"cylinder", "material":"bois_noir",
    "at":[0,2.06,0], "radius":0.008, "height":1.04, "sides":8 },
  { "_":"l'abat-jour, ouvert vers le bas", "type":"cylinder", "material":"billard_abatjour",
    "at":[0,1.84,0], "radius":0.28, "radiusTop":0.10, "height":0.22, "sides":20,
    "capBottom":false, "capTop":true },
  { "type":"sphere", "material":"ampoule_cage",
    "at":[0,1.83,0], "radius":0.045, "sides":12, "rings":7 }
]
```

Environ 120 sommets par luminaire, soit 720 pour les six.

Effet calculé, sol :

| | Aujourd'hui | Avec les six |
|---|---|---|
| médiane | 0,707 | **0,962** (+36 %) |
| p10 | 0,231 | **0,294** (+27 %) |
| éclairement à l'aplomb d'une nouvelle suspension | — | **2,06** |
| le même, à 2 m de côté | — | **0,32** |

Une flaque qui perd **un facteur 6,4 en 2 m** : c'est un halo, pas un éclairage général.

**(b) Les rampes de l'îlot cessent de laver les bornes.** `lights[rampe_ilot_nord]` et
`lights[rampe_ilot_sud]`, trois exemplaires chacune, sont à l'aplomb exact de l'îlot. Sur une
face verticale de caisson, leur cosinus d'incidence est presque nul : elles n'éclairent pas la
borne, elles remplissent le fond du champ derrière elle. Elles pèsent **444 d'intensité, 21 % du
budget déclaré**.

`intensity` **74 → 34**, `range` **4,6 → 3,4**.

**(c) Les deux néons d'accent remontent en corniche.** `lights[neon_mur_ouest]` et
`lights[neon_mur_est]` sont à y = 2,20 avec `radius 0.55` : à cette hauteur ils lavent **le mur**,
qui est la deuxième plus grande surface du décor, en bleu à l'ouest. Sur `travee_ouest` le mur
kaki vire au vert-de-gris — la seule lumière froide d'un décor dont le mandat dit « couleurs
chaudes ».

| Clé | Actuel | Proposé |
|---|---|---|
| `neon_mur_ouest.at[1]` / `neon_mur_est.at[1]` | 2,20 | **2,58** |
| `radius` (les deux) | 0,55 | **0,90** — un tube, pas une ampoule |
| `neon_mur_ouest.intensity` | 40 | **24** |
| `neon_mur_est.intensity` | 40 | 40 *(inchangé : le magenta est chaud)* |

**(d) Les écrans portent plus loin sans briller plus.** `engine/scene/ns_scene.c:1339` :
`l->range` **2,8 → 3,4**, `l->intensity` **inchangé à 30**.

C'est la fenêtre de portée qui coupe, pas l'intensité : `t = 1 − (d/range)⁴`. À 2 m, `t²` passe de
**0,55 à 0,77** (+40 %) ; à 0,90 m — la position du joueur, celle que le commentaire du fichier a
calibrée au chiffre près — il passe de 0,966 à 0,986, soit **+2 %**. On ne touche donc pas à ce qui
est réussi, et on double presque la portée utile.

Effet calculé : l'aire où les écrans donnent plus de 20 % du direct passe de **70,2 à 88,7 m²**
(+26 %).

**(e) L'ambiante, en dernier et sous condition.** `engine/render/ns_render.c:463` :
`ambient_intensity` **2,6 → 1,9** pour `NS_RT_SHADOWS`. Luminance de la constante 0,233 → **0,170**,
et sa part au point médian du sol tombe de 51 % à 43 % — puis à 36 % une fois (a) appliqué.

**Condition d'application, à respecter à la lettre :** ne changer cette valeur qu'**après** avoir
mesuré (a), (b), (c) et (d), et l'annuler si une seule des neuf vues perd plus de **4 points** de
médiane globale. `DESIGN-SALLE.md` documente que 2,6 est une valeur mesurée, pas choisie ; elle ne
se touche pas à l'aveugle.

**Coût.** 50 sources → **56**. `NS_MAX_LIGHTS` vaut 128 (`engine/scene/ns_scene.h:21`) et la boucle
d'éclairage est bornée à 128 (`lighting.frag:221`). +12 % sur une passe qui n'est pas le poste
dominant. Acceptable ; à mesurer quand même en images par seconde sur `orbite`.

**Critères de réussite, mesurés et pas supposés.**

1. Médiane de `centre`, `sud` et `travee` (les vues du §2.3-L) **≥ 30**.
2. `% < 16` de `orbite` sous **45** (63,7 aujourd'hui).
3. Médiane de `borne` entre **50 et 60** (56 aujourd'hui) — ce qui marche ne bouge pas.
4. `% > 200` de `borne` **≤ 6** (8,5 aujourd'hui), obtenu par le §5 et par `sol.emissiveStrength`
   (§5.3), pas par les écrans.
5. Aucune des neuf vues nommées sous **20** de médiane.

### 3.6 Le cas des WC : la moitié du poste 7 était fausse

`DESIGN-SALLE.md` poste 7 baisse `plafonnier_toilettes` de **52 à 30** *et* sa portée de **4,4 à
3,6**, pour une cible écrite de « médiane de `toilettes` entre 55 et 70 ». Résultat mesuré :
**25**. L'argumentaire du §3.4 de ce document-là ne justifiait pourtant que la baisse d'intensité —
la portée y était présentée comme un simple accord avec la taille de la pièce (3,35 × 3,80 m).

Ce n'en est pas un, et c'est arithmétique. La lampe est à (−7,9 ; 2,74 ; −5,4) ; le coin
(−9,65 ; −7,2) du sol est à **3,72 m**. La fenêtre de portée vaut `t = 1 − (d/range)⁴`, appliquée au
carré :

| `range` | `t²` au coin | Éclairement **moyen** du sol des WC | Part du sol hors portée |
|---|---|---|---|
| 3,6 *(actuel)* | **0** (hors portée) | **0,051** | 1 % |
| 4,2 | 0,151 | 0,107 | 0 % |
| **4,4** *(valeur d'origine)* | **0,240** | **0,122** | 0 % |

La portée de 3,6 divise l'éclairement moyen par **2,4** — bien plus que la baisse d'intensité
elle-même (52 → 30, soit ÷1,7). Deux corrections ont été appliquées ensemble et une seule était
justifiée.

**Correction : `plafonnier_toilettes.range` 3,6 → 4,4, `intensity` maintenu à 30.** Une ligne, et
la cible du poste 7 est enfin tenable.

---

## 4. Les couleurs des bornes

### 4.1 Ce qui est juste, et qu'il ne faut pas « corriger »

**Une salle d'arcade est bariolée, et celle-ci a raison de l'être.** Les neuf familles de caissons
sont à 0,067–0,157 de réflectance pour 0,38–0,90 de saturation : ce sont des **couleurs profondes**,
pas des couleurs vives. Les baisser encore donnerait exactement l'« essai raté n° 2 » que
`DESIGN-SALLE.md` §3.5 documente. **Ne pas désaturer les caissons.**

De même : la chaleur de la salle ne doit pas venir des bornes. Toutes les sources sauf une sont à
(1,0 ; 0,76…0,83 ; 0,48…0,63). C'est la **lumière** qui fait la salle chaude ; les machines, elles,
doivent se distinguer les unes des autres. Dix-neuf bornes chaudes sous une lumière chaude, c'est
un aplat.

### 4.2 Ce qui est faux, mesuré

| Constat | Mesure | Où |
|---|---|---|
| Le liseré est le matériau le plus saturé du décor et il est **le même sur les dix-neuf** | `borne_tmolding` sat. **0,93**, un seul matériau, `roomgen.c:1372` le cherche par nom fixe | toutes les vues |
| Il est **laqué** | `roughness 0.22` ; un jonc de chant est du vinyle, 0,45–0,55 | `allee`, `borne` |
| Le flanc est **plus clair et moins coloré** que le caisson | flanc 0,177 / sat 0,41 ; caisson 0,106 / sat 0,75 | `allee`, `travee_ouest` |
| Le losange du flanc est à l'échelle d'un **capitonnage** | **18,75 cm** (`sideart` `--step` 96 px, planche 512 px, `uvMetres 1.0`) | `allee` |
| Deux bornes **voisines** ont la même couleur | `caisson_shooter` (0,225 ; 0,066 ; 0,024) et `caisson_demineur` (0,209 ; 0,139 ; 0,028), bornes 13 et 14, entraxe 0,80 m | `travee_ouest` |
| Deux caissons sont à **3 %** l'un de l'autre | `caisson_demineur` et `caisson_pacman` | partout |

### 4.3 Le jonc de chant

**Version minimale, une ligne, à faire tout de suite :**

```jsonc
{ "name": "borne_tmolding", "texture": "painted_panel.jpg",
  "baseColor": [0.86, 0.83, 0.78, 1.0], "roughness": 0.52, "metallic": 0.0, "uvMetres": 0.1 }
```

Réflectance 0,096 → **0,309**, saturation 0,93 → **0,22**. Un jonc **blanc cassé** : c'est la
couleur de jonc la plus courante des bornes de la période, et surtout c'est une ligne qui *décrit*
la silhouette au lieu de la crier.

**Version juste, six lignes de C en plus.** `tools/roomgen.c:1372` cherche `borne_tmolding` par nom
fixe ; ajouter une clé `materialTmolding` aux bornes, sur le modèle exact de `materialTrim`, et
quatre matériaux :

| Matériau | `baseColor` | `roughness` | Réflectance | Sur quelles bornes |
|---|---|---|---|---|
| `tmolding_clair` | [0.86, 0.83, 0.78] | 0.52 | 0,309 | tetris, asteroid, piano (les froides) |
| `tmolding_sombre` | [0.10, 0.10, 0.11] | 0.55 | 0,039 | shooter, demineur, pacman (les chaudes) |
| `tmolding_rouge` | [0.72, 0.16, 0.14] | 0.52 | 0,108 | flappy, snake (les vertes) |
| `tmolding_jaune` | [0.86, 0.62, 0.12] | 0.52 | 0,239 | leaderboard |

Le principe : **un jonc contraste avec le caisson en valeur, jamais en teinte.** Clair sur sombre,
sombre sur clair. Aujourd'hui il contraste en teinte, sur les dix-neuf, avec la teinte la plus
saturée disponible.

### 4.4 Les flancs

**Règle, en une phrase : le flanc porte la couleur du caisson, un cran plus clair — pas une autre
couleur, plus pâle.**

`flanc_X.baseColor = caisson_X.baseColor × 1.8`, avec la palette du §4.5 pour les quatre caissons
qui changent. Le facteur n'est pas 1,5 : `flanc_borne.png` mesure 0,303 et `painted_panel.jpg`
0,401, soit un rapport de **0,756** ; il faut donc 1,8 pour que le flanc ressorte réellement plus
clair que le corps. Les multiplicateurs au-dessus de 1,0 sont permis et déjà employés
(`materials[marron]` 1,15 ; `materials[table_salon]` 1,32).

| Matériau | `baseColor` actuel | **Proposé** | Réflectance av. → ap. | Saturation av. → ap. |
|---|---|---|---|---|
| `flanc_flappy` | [0.426, 0.736, 0.512] | **[0.18, 0.83, 0.36]** | 0,198 → 0,199 | 0,42 → **0,78** |
| `flanc_snake` | [0.478, 0.718, 0.460] | **[0.13, 0.47, 0.23]** | 0,196 → 0,115 | 0,36 → **0,72** |
| `flanc_tetris` | [0.426, 0.512, 0.787] | **[0.18, 0.36, 0.94]** | 0,156 → 0,110 | 0,46 → **0,81** |
| `flanc_asteroid` | [0.478, 0.503, 0.598] | **[0.16, 0.16, 0.20]** | 0,153 → 0,049 | 0,20 → 0,20 |
| `flanc_shooter` | [0.822, 0.495, 0.400] | **[1.00, 0.32, 0.13]** | 0,169 → 0,137 | 0,51 → **0,87** |
| `flanc_demineur` | [0.787, 0.667, 0.409] | **[0.29, 0.47, 0.61]** | 0,204 → 0,134 | 0,48 → **0,52** |
| `flanc_pacman` | [0.804, 0.701, 0.392] | **[1.04, 0.79, 0.09]** | 0,212 → 0,240 | 0,51 → **0,91** |
| `flanc_piano` | [0.632, 0.443, 0.736] | **[0.61, 0.22, 0.83]** | 0,153 → 0,105 | 0,40 → **0,73** |
| `flanc_leaderboard` | [0.718, 0.426, 0.452] | **[0.79, 0.18, 0.23]** | 0,148 → 0,095 | 0,41 → **0,77** |

Moyennes : réflectance **0,177 → 0,132** (−25 %), saturation **0,42 → 0,70** (+67 %). Avec la
palette de caissons du §4.5, dont la réflectance moyenne tombe à 0,090, le flanc reste **47 % plus
clair que le corps qu'il habille** — ce qui est le rapport qu'on veut, et l'inverse exact de ce que
`DESIGN-SALLE.md` n'avait pas mesuré.

`flanc_asteroid` sort à 0,049 et reste peu saturé : c'est **voulu**. Un caisson d'*Asteroids* est
noir ; sa planche à losanges continue de porter le dégradé, donc la forme se lit, mais la borne ne
crie pas. Une rangée a besoin d'un silence.

**Et la planche.** `assets/CMakeLists.txt:113`, `COMMAND sideart "${SIDEART}"` →

```cmake
COMMAND sideart --step=34 --line=0.06 --low=0.12 --high=0.62 "${SIDEART}"
```

- `--step` 96 → **34** : le losange passe de 18,75 cm à **6,6 cm**. Ce n'est plus du capitonnage,
  c'est une tôle gaufrée.
- `--line` 0,10 → **0,06** : les lignes deviennent un relief, pas un quadrillage.
- `--high` 0,86 → **0,62** : le haut du flanc cesse de saturer à blanc, donc le multiplicateur de
  couleur y garde sa teinte. C'est ce qui fait qu'un flanc à 0,79 de saturation le reste **en haut**,
  là où on le voit de l'allée.

C'est le préalable au poste 13 de `DESIGN-SALLE.md` (`sideart --logo=`) : une planche à losanges de
19 cm n'accueillerait aucun logo lisible ; à 6,6 cm elle devient un fond.

### 4.5 La palette des dix-neuf, et son ordre

Les matériaux sont **par jeu**, pas par emplacement : neuf teintes pour dix-neuf bornes, et c'est
juste — une salle d'arcade de quartier a six à huit familles de caisson qui reviennent. Le défaut
n'est pas le nombre, ce sont **deux collisions**.

| Matériau | `baseColor` actuel | **Proposé** | Pourquoi |
|---|---|---|---|
| `caisson_demineur` | [0.52, 0.38, 0.08] | **[0.16, 0.26, 0.34]** | Sépare la borne 14 de sa voisine 13 (shooter, orange) *et* de `caisson_pacman`, dont elle est à 3 %. Un bleu acier est aussi la couleur d'un champ de mines. |
| `caisson_pacman` | [0.54, 0.42, 0.06] | **[0.58, 0.44, 0.05]** | Devient le seul ambre de la salle, donc identifiable. |
| `caisson_asteroid` | [0.16, 0.19, 0.30] | **[0.09, 0.09, 0.11]** | *Asteroids* était un caisson **noir** ; l'ardoise actuelle ne se distingue de rien. Un caisson noir dans une rangée est un repos pour l'œil, et il fait ressortir ses deux voisins. |
| `caisson_snake` | [0.16, 0.44, 0.14] | **[0.07, 0.26, 0.13]** | Vert bouteille au lieu d'un second vert pomme : `caisson_flappy` [0.10, 0.46, 0.20] garde le vert clair. |

Les cinq autres — flappy, tetris, shooter, piano, leaderboard — **ne changent pas**.

**L'ordre obtenu, tel qu'on le parcourt :**

| Rangée | Slots, dans l'ordre de marche | Teintes |
|---|---|---|
| îlot sud (z = −0,44) | 1 · 2 · 3 ‖ 4 · 5 · 6 | vert clair · bleu roi · orange ‖ vert bouteille · **noir** · bleu acier |
| îlot nord (z = +0,44) | 7 · 8 · 9 ‖ 10 · 11 · 12 | **ambre** · violet · **noir** ‖ vert clair · bleu roi · vert bouteille |
| mur ouest (x = −9,1) | 13 · 14 · 15 | orange · **bleu acier** · violet |
| mur est (x = +9,1) | 16 · 17 · 18 | bleu roi · vert clair · **ambre** |
| estrade | 0 | rouge cerise |

Aucune paire adjacente ne partage une famille de teinte. Le noir tombe une fois par rangée d'îlot,
au milieu, ce qui casse la répétition sans faire de trou. Le ‖ marque la trouée de 2,4 m.

### 4.6 Deux autres corrections sur la borne

| Cible | Actuel | Proposé | Pourquoi |
|---|---|---|---|
| `assets/CMakeLists.txt:145` | `panelart --field=0.30` | `panelart --field=0.22 --step=6` | §1.12 : la planche est dessinée à 2,1 cm et rendue à ≈ 15 cm. `--step=6` compense le facteur 7 des UV du modèle. La vraie correction est dans `assets/blender/borne.py` ; celle-ci tient en un mot et se mesure tout de suite. |
| `materials[borne_commande].baseColor` | [0.44, 0.13, 0.11] | **[0.26, 0.08, 0.07]** | Le panneau est à 1,00 m, en pleine lumière d'écran (§3.2) : à 0,44 de multiplicateur il ressort saumon clair et vole la vedette aux boutons, qui sont précisément ce qu'on doit trouver sans le chercher. |

---

## 5. Le plafond

### 5.1 Ce qu'on voit, et pourquoi c'est une pergola

Trois trames régulières se superposent sur 360 m² :

| Trame | Pas | Déclarée par |
|---|---|---|
| les rails du faux plafond | **0,60 m**, largeur 3,8 cm, **saillie 4,2 cm** | `ceilings[0].tile`, `railWidth`, `railDrop` |
| la répétition de `plafond.jpg` | **0,60 m** | `materials[plafond].uvMetres` |
| les cinq poutres | **2,40 m** | `boxes[poutre].repeat.step` |

Et ce treillis est **la surface la plus claire de la salle** :

| Surface | Réflectance effective | Rapport au sol |
|---|---|---|
| **plafond** | **0,294** | **× 3,2** |
| mur | 0,142 | × 1,5 |
| sol | 0,093 | × 1,0 |
| pilier | 0,029 | × 0,3 |

Un quadrillage clair et régulier, en relief, éclairé par en dessous, au-dessus d'un sol sombre :
c'est la description d'une **pergola**. La capture `plafond` la montre en pleine page et
`vide_nord`, `depuis_bar`, `travee_ouest` en portent le tiers haut.

### 5.2 Ce que doit être le plafond d'une salle d'arcade

**Le plus sombre. Plus sombre que le sol.** C'est ce qui fait que les seules choses lumineuses en
hauteur sont les marquees et les dalles — donc que les machines sont ce qu'on voit.

Rien n'interdit d'y aller : `DESIGN-SALLE.md` §1.6 classe **« Plafond blanc uni : accidentel »** et
**« Lambris art déco du plafond : accidentel (jamais vu) »**, et le mesure à **0,0043** en 2020 —
je le retrouve à 0,0043 sur le fichier d'origine. L'aspect du plafond n'est pas de l'ADN. Sa
**présence** — le fichier `plafond.jpg` employé sur le plafond — en est. On le garde, on le baisse.

### 5.3 Les valeurs

| Cible | Actuel | **Proposé** | Effet mesuré / calculé |
|---|---|---|---|
| `materials[plafond].baseColor` | [0.84, 0.78, 0.31] | **[0.30, 0.26, 0.13]** | réflectance **0,294 → 0,101** ; linéaire (0,134 ; 0,094 ; 0,075) ; B/R 0,48 → **0,56**. Le plafond passe sous le mur et à égalité du sol. |
| *idem, deuxième cran si nécessaire* | | **[0.20, 0.17, 0.09]** | réflectance **0,067**. Le plafond passe sous le sol. À n'appliquer que si le premier cran laisse la trame lisible. |
| `ceilings[0].tile` | 0.6 | **1.2** | Divise par deux la densité de lignes. 1200 × 600 est un module de faux plafond aussi courant que 600 × 600. |
| `materials[plafond].uvMetres` | 0.6 | **1.2** | Suit la dalle, comme aujourd'hui : une répétition par dalle. |
| `ceilings[0].railDrop` | 0.042 | **0.016** | Le rail cesse d'être un relief de 4 cm qui accroche la lumière rasante ; il devient un joint. |
| `materials[rail].baseColor` | [0.52, 0.52, 0.54] | **[0.26, 0.24, 0.22]** | Aujourd'hui : grille sombre sur fond clair, le contraste maximal. Après : grille sombre sur fond sombre — elle disparaît, ce qui est le but. |
| `materials[poutre]` | *(inchangé)* | *(inchangé)* | Réflectance 0,115. Aujourd'hui c'est une barre sombre sur 0,294, donc un trait ; sur 0,101 c'est un volume qu'on sent sans le compter. **Le poste 8 de `DESIGN-SALLE.md` était juste ; il devient efficace quand le fond descend.** |
| `materials[panneau_lumineux]` | *(inchangé)* | *(inchangé)* | Émissif (1,0 ; 0,8 ; 0,55) × 1,35 × albédo (0,28 ; 0,22 ; 0,15) = luminance **0,259**. À plafond 0,101, les dix-sept dalles deviennent **les seuls objets clairs en hauteur**. C'est exactement le rôle d'une dalle. |

**Le sol descend en même temps.** `materials[sol].emissiveStrength` **0,25 → 0,10**.
La valeur de 0,25 est justifiée dans le fichier par `salle.mtl:4`, `Ke 0.229790` — mais ce `Ke`
était sur `CENTRE_MACHINE_SOL_Cube`, **le sol de l'estrade**, environ 8,5 m². L'appliquer aux
293 m² du hall est une extrapolation d'un facteur **34**. À 0,10 les motifs néon se lisent encore
— l'émissif est multiplié par l'albédo texel par texel, donc seuls eux portent — et le sol cesse
d'être la source principale de la salle.

**Et `uvMetres` du sol.** 2,4 → **1,6**. L'entraxe des motifs passe de 0,85 m à **0,57 m** et le
motif de 0,60 m à **0,40 m** (§1.5). C'est le bas de la fourchette d'un vrai tapis d'arcade, et
c'est un compromis assumé : à 1,1 (motif 0,28 m, la cote juste) le pavage compterait 242
répétitions sur le hall, ce qui redevient visible. À 1,6 il en compte 115, et la baisse d'émissif
supprime la raison principale pour laquelle on suivait les motifs à l'œil.

**Critères de réussite.**

1. Médiane de `plafond` **entre 14 et 22** (43 aujourd'hui).
2. Sur `allee`, `bar` et `classement`, médiane du **tiers haut** sous **30**.
3. Médiane globale de `allee` **≥ 42** (49 aujourd'hui) : le plafond descend, la salle non.
4. Sur `plafond`, les pixels des dalles `panneau_lumineux` au-dessus de **120**, et rien d'autre
   au-dessus de 90.

### 5.4 Le mur, tant qu'on y est

Le mur nord est un aplat de 19,3 × 2,4 m (§1.3). Le **bandeau haut de mur bordeaux** — poste 10 de
`DESIGN-SALLE.md`, `haut_mur` de `salle.mtl:574` sur neuf objets de 2020 — n'existe toujours pas,
et il est la seule chose qui casse ce mur horizontalement.

Deux blocages à lever, dans cet ordre :

1. **`materials[bordeau_uni]` est noir.** `lacquered_wood.jpg` mesure **0,0148** ; × [0,40 ; 0,07 ;
   0,12] il donne **0,0030**. Corriger d'abord :
   ```jsonc
   { "name":"bordeau_uni", "texture":"bordeaux.jpg", "roughness":0.42, "metallic":0.12,
     "uvMetres":0.6, "baseColor":[1.6, 1.6, 1.6, 1.0] }
   ```
   → linéaire (0,150 ; 0,018 ; 0,021), réflectance **0,046**, R/V = 8,5. Un vrai bordeaux profond,
   sur la texture de 2020. Cela répare **aussi le jukebox** (§1.3), qui est le seul autre emploi.
   Même traitement pour `materials[rouge]` : `lacquered_wood.jpg` → `bordeaux.jpg`,
   `baseColor [3.2, 1.1, 1.0]` → linéaire (0,301 ; 0,012 ; 0,013), réflectance 0,073.
2. **`roomgen` n'a pas de profil de bandeau.** Il connaît `plinthe`, `corniche`, `cimaise`, `nez`
   (`tools/roomgen.c:996-1018`). Ajouter `bandeau`, section **[0.02, 0.68]**, sur le modèle exact
   de `cimaise` : environ quinze lignes. Puis :
   ```jsonc
   { "name":"bandeau_haut", "material":"bordeau_uni", "profile":"bandeau",
     "section":[0.02, 0.68], "y":2.42, "offset":0.1, "closed":true,
     "points": [ … les mêmes que cimaise_hall … ] }
   ```

---

## 6. Ce qu'il ne faut surtout pas toucher

Liste explicite, pour que personne ne casse l'ADN en corrigeant le reste. Chaque ligne dit **sur
quoi** elle s'appuie.

| Ce qui ne bouge pas | Preuve |
|---|---|
| **Le tapis néon `floor.jpg` au sol du hall.** Sa texture, ses motifs, son noir. On touche à `uvMetres` et à `emissiveStrength`, **jamais au fichier ni au choix**. | `salle.mtl:1254`, `sol_moquette` sur `MUR_SAL_Cube.002` ; la plus grande surface de la salle ; `DESIGN-SALLE.md` §1.5 |
| **L'îlot de douze bornes en deux blocs dos à dos, avec sa trouée centrale de 2,4 m.** Aucun objet dans x ∈ [−1,2 ; 1,2], z ∈ [−1,5 ; 3,0]. | `room.c:2124` ; c'est la fuyante de `allee` |
| **Le bar au fond, face à l'entrée, avec son comptoir Memphis `desk.jpg`.** On corrige son `uvMetres`, pas son motif. | `room.c:2099` ; `desk.jpg` sur `bar_accueil_Cube.030` |
| **Le billard dans son coin, à l'écart, sous sa suspension basse.** C'est le seul lieu réussi de la salle et il sert de modèle au §3. | `room.c:2171` ; mesuré : 3,95 contre 0,59 |
| **L'entrée par un pan coupé et son sas.** | `room.c:2108-2120` |
| **Les piliers bordeaux, dix, sur leur trame de 2,4 m.** On leur ajoute une embase et un grain, on ne change ni leur teinte ni leur position. | `salle.mtl:649`, `marbre_poutre.014` sur treize `PILONNE` |
| **Les dix-neuf bornes, et le compte de dix-neuf.** Aucun ajout, aucun retrait, aucun déplacement des `cabinets[].at`. | Le nom du jeu |
| **Les marquees dessinés par `tools/marqueeart`.** Ils marchent, ils se lisent à 4 m, ils sont dans la fonte du jeu. | Capture `travee_ouest` |
| **Les dix-neuf lumières d'écran : leur position dans le plan de la dalle, leur intensité de 30, leur demi-rayon de 0,40, leur teinte à 45 %, leur scintillement.** On ne touche qu'à `range`. | `ns_scene.c:1264-1345` ; mesuré : 96 % de ce que reçoit le joueur à 0,90 m |
| **La température de couleur chaude de toutes les sources**, (1,0 ; 0,76…0,83 ; 0,48…0,63), sauf le tube vert des WC qui est un repoussoir assumé. | Mandat : « couleurs chaudes » |
| **Le caractère bariolé des caissons.** Ne pas désaturer. On sépare deux teintes qui se ressemblent, on n'aplatit pas les neuf. | `DESIGN-SALLE.md` §3.5, essai raté n° 2 |
| **La salle plus large que profonde, 19,3 × 14,4 sous 2,92.** Le rapport de 2020 est conservé ; le débat sur l'axe est tranché et clos. | `DESIGN-SALLE.md` §2.10 point 3 |
| **Les cinq zones de poussière et les trois zones sonores.** Elles sont déclarées là où les faisceaux tombent ; elles suivront les nouvelles suspensions, elles ne se refont pas. | `_poussiere`, `_zones` |
| **`props[bar_accueil]`, `props[billard]`, `props[enseigne_nineteen]`, `props[cible_flechettes]`, `props[boombox]`.** Positions inchangées. | Ils sont à leur place |

Et une règle générale, qui vaut pour tout ce document :

> **Une texture dont la réflectance en 2020 était sous 0,03 n'a jamais été vue.** Sa présence est
> caractéristique, son aspect est accidentel. On peut la garder et changer sa valeur ; on ne peut
> pas invoquer 2020 pour défendre son apparence actuelle.

C'est la règle de `DESIGN-SALLE.md` §1.4, et c'est elle qui autorise le §5.

---

## 7. Ordre de bataille

Classé par effet décroissant sur les deux reproches — « aucune âme ni atmosphère » et « les objets
sont mal positionnés ». **À faire un par un, en mesurant après chacun** : l'adaptation d'exposition
couple tous ces changements, et deux corrections appliquées ensemble ne se départagent plus.

| # | Poste | § | Effort | Critère mesuré |
|---|---|---|---|---|
| 1 | **Six suspensions basses à 1,75 m, avec leur luminaire visible** | 3.5 a | ≈ 60 lignes JSON | Médiane du sol 0,707 → **0,96** ; flaque de 2,06 sous la lampe contre 0,32 à 2 m. Médiane de `centre`, `sud`, `travee` ≥ **30** |
| 2 | **Les quatre bancs, le baby-foot, les deux mange-debout, les deux poubelles, la vitrine à lots** | 2.3 A · C · D · F · G · I | ≈ 210 lignes JSON, aucun asset | L'aire à plus de 2 m de tout meuble passe de **60,9 à 18,0 m²**, celle à plus de 3 m de **19,7 à 1,8 m²**, et le point le plus désert de **5,12 à 3,95 m** (calcul du §2.3-K). `% < 16` de `orbite` sous **45** |
| 3 | **Le plafond cesse d'être une pergola** — `baseColor` [0.30, 0.26, 0.13], `tile` 1.2, `railDrop` 0.016, `rail.baseColor` [0.26, 0.24, 0.22] | 5.3 | **4 lignes** | Médiane de `plafond` entre **14 et 22** (43). Tiers haut de `allee`, `bar`, `classement` sous **30**. Médiane globale de `allee` ≥ **42** |
| 4 | **Le liseré cesse d'être magenta** — `borne_tmolding.baseColor` [0.86, 0.83, 0.78], `roughness` 0.52 | 4.3 | **1 ligne** | Saturation du matériau 0,93 → **0,22**. Sur `allee` à 100 %, le contour de borne n'est plus l'élément le plus saturé du cadre |
| 5 | **Le sol arrête de faire la lumière** — `sol.emissiveStrength` 0.10, `sol.uvMetres` 1.6 | 5.3 | **2 lignes** | Motif au sol ≈ **0,40 m** (0,60 aujourd'hui). `% > 200` de `borne` **≤ 6** (8,5) |
| 6 | **Les flancs portent la couleur de leur caisson** — neuf `baseColor` + les quatre arguments de `sideart` | 4.4 | 9 lignes JSON + 1 ligne CMake | Saturation moyenne des flancs 0,41 → **0,71** ; réflectance 0,177 → **0,128**. Losange 18,75 cm → **6,6 cm** |
| 7 | **Le monnayeur en bout de comptoir** | 2.3 B | ≈ 30 lignes JSON | Un objet éclairé à moins de 4 m de la sortie du sas, sur `arrivee` |
| 8 | **L'estrade retrouve son émissif de 2020** — `sol_sombre.texture` → `moquette.jpg`, `emissive [1.0, 0.86, 0.62]`, `emissiveStrength 0.23` ; retirer `moquette.jpg` de `retiredTextures` ou en restreindre le motif au sol du hall | 1.8 | **4 lignes** | Réflectance de l'estrade 0,0031 → 0,101 ; sur `classement`, l'estrade se distingue du sol |
| 9 | **Les rampes de l'îlot cessent de laver les bornes** — 74 → 34, portée 4,6 → 3,4 | 3.5 b | **4 lignes** | Sur `travee_ouest`, le flanc de la borne la plus proche montre un dégradé vertical net. Médiane de `allee` ≥ **42** |
| 10 | **Les écrans portent à 3,4 m** — `ns_scene.c:1339` | 3.5 d | **1 ligne de C** | Aire couverte à plus de 20 % par les écrans : **70,2 → 88,7 m²**. Médiane de `borne` inchangée à ±3 |
| 11 | **Le mobilier du salon se retourne vers les bornes ; le distributeur remonte sur le trajet** | 2.3 J | **8 lignes** | Sur `salon_est`, au moins une borne dans l'axe du canapé |
| 12 | **Le tableau des scores du bar cesse d'être un trou noir** — remplacer `classement_fond` par une planche `marqueeart` sur le modèle de `enseigne` | 1.3 | ≈ 10 lignes + 1 planche | Aucun rectangle sous 20 de luminance sur `bar` |
| 13 | **`bordeau_uni` et `rouge` cessent d'être noirs** — texture → `bordeaux.jpg` | 5.4 | **4 lignes** | Le corps du jukebox passe de 0,0030 à **0,046** de réflectance ; il se distingue du mur sur `vide_nord` |
| 14 | **Quatre écrans d'attente sur des jeux jouables** — bornes 7, 8, 15, 18 | 1.4 | **4 lignes** + 2 planches | Zéro `ecran_bientot` et zéro `ecran_chargement` sur les dix-neuf |
| 15 | **Deux collisions de palette** — `caisson_demineur`, `caisson_pacman`, `caisson_asteroid`, `caisson_snake` | 4.5 | **4 lignes** | Aucune paire de bornes adjacentes de la même famille de teinte, vérifié sur `travee` et `allee` |
| 16 | **Les bancs sud, le tableau de la semaine, les socles de piliers** | 2.3 D · E · H | ≈ 90 lignes + 1 planche | La poche de 5,12 m tombe sous **2,5 m**. Médiane de `sud` ≥ **30** |
| 17 | **Le comptoir à la bonne échelle** — `comptoir.uvMetres` 0.3 → **0.9** | 1.7 | **1 ligne** | Le triangle Memphis mesure 10 à 13 cm sur `bar` |
| 18 | **Les WC remontent à leur cible** — `plafonnier_toilettes.range` 3.6 → **4.4**, intensité 30 inchangée | 0.1 · 3.6 | **1 ligne** | Éclairement moyen du sol des WC × **2,4** (calcul du §3.6). Médiane de `toilettes` entre **55 et 70** (25 aujourd'hui) — la cible écrite du poste 7 de `DESIGN-SALLE.md`, jamais atteinte |
| 19 | **Les néons d'accent remontent en corniche** — y 2,20 → 2,58, `radius` 0,90, ouest 40 → 24 | 3.5 c | **6 lignes** | Sur `travee`, le rapport B/R du mur ouest au-dessus de 1,8 m passe sous **1,0** |
| 20 | **Le jonc par borne** — `materialTmolding` dans `roomgen`, quatre matériaux | 4.3 | ≈ 6 lignes de C + 4 matériaux | Deux bornes voisines ont un jonc de valeur opposée |
| 21 | **Le bandeau haut de mur bordeaux** — profil `bandeau` dans `roomgen` | 5.4 | ≈ 15 lignes de C + 10 JSON | Une rupture horizontale nette au-dessus de la cimaise sur `bar` et `sud` |
| 22 | **L'ambiante baisse, sous condition** — `ns_render.c:463`, 2,6 → 1,9 | 3.5 e | **1 ligne de C** | Part de la constante au sol : 51 % → **36 %**. **Annuler si une seule vue perd plus de 4 points** |
| 23 | **La sérigraphie du panneau à son échelle** — `panelart --step=6`, puis les UV de `borne.py` | 4.6 | 1 ligne CMake, puis Blender | Le losange du panneau mesure 2 à 3 cm sur `borne` |
| 24 | **Le billard vécu** — queue, craie, billes numérotées | 1.11 | ≈ 25 lignes | Les trois matériaux de `salle.mtl` (`canne`, `bleu.001`, `de_qui_billard`) ont un équivalent |
| 25 | **Les quatre points de vue neufs, et `orbite` à hauteur d'œil** | 2.3 L | **6 lignes** | Les trois grands vides ont chacun une capture de contrôle |

**Les postes 1 à 5 tiennent en un peu plus de deux cents lignes de JSON et aucun code, et ils
portent l'essentiel des deux reproches.** Les postes 1 et 2 sont indissociables : une flaque de
lumière sur rien reste un vide, et un banc dans le noir ne se voit pas. Les faire ensemble, mais
les mesurer séparément.
