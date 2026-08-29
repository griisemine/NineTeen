# Le plan de 2020, mesuré

Ce document donne les cotes du **plan d'origine** de la salle, relevées dans
`legacy/room/textures/salle.obj` (8,3 Mo, 127 objets, 60 125 sommets), converties en mètres dans
le repère de `assets/scene/salle.room.json`, et confrontées à ce que ce fichier décrit
aujourd'hui.

Il ne contient que des valeurs **mesurées**. Ce qui n'a pas pu l'être est rassemblé en §11, sous
son propre titre. Il ne modifie aucun fichier : la liste des changements (§9) est une consigne,
pas une application.

---

## 0. Comment ces chiffres ont été obtenus

Un lecteur OBJ écrit pour l'occasion, hors du dépôt
(`…/scratchpad/plan2020/{parse,groups,faces,poly,conv}.py`). Trois précautions, qui sont
exactement les trois pièges du format :

1. **Les indices `v` sont globaux et continus.** Le lecteur ne remet jamais un compteur à zéro
   sur un `o`. Contrôle : aucun des 127 objets ne référence un sommet hors de son propre bloc
   `v` (0 cas sur 127) — donc la boîte englobante par bloc et la boîte englobante par faces
   coïncident, et les deux ont été calculées.
2. **La boîte englobante globale sert de témoin.** Elle mesure
   **55,4509 × 10,0380 × 53,9715 unités**. `docs/DESIGN-SALLE.md` §1.1 publie
   « 55,45 × 10,04 × 53,97 sur 60 125 sommets ». Les deux lectures, indépendantes, tombent sur
   les mêmes chiffres au centième. Le lecteur est bon.
3. **Les cotes fines sont prises sur les faces, pas sur les boîtes.** Une boîte englobante
   d'objet mélange le meuble et sa lampe : le billard mesure 9,25 unités de haut parce que la
   tige de sa suspension traverse le plafond. Toutes les hauteurs de §5 et §6 sont donc relevées
   **par groupe de matériau** (183 groupes) et, quand il le fallait, **face par face** avec la
   normale (Newell) — c'est ainsi que l'orientation des bornes a été établie et non devinée.

---

## 1. L'échelle : deux chemins, et l'endroit exact où ils divergent

### 1.1 Chemin A — la caméra de l'auteur (celui du dépôt)

`legacy/room/room.c:90` `HAUTEUR_CAMERA_DEBOUT 3.5F`, ligne 91
`HAUTEUR_CAMERA_ACCROUPI 2.7F`. Pour un œil à 1,70 m et 1,31 m : **2,059** et **2,061 u/m**.
C'est le chemin déjà retenu par `salle.room.json` (`_echelle`). Il repose sur une seule source —
un fichier `.c` — et sur deux constantes qui ne sont pas indépendantes l'une de l'autre.

### 1.2 Chemin B — onze cotes normées relevées dans la géométrie

| Ce qui est mesuré | Unités | Norme retenue | Échelle impliquée |
|---|---|---|---|
| Section d'un `PILONNE` | 1,000 | 0,50 m | **2,000** |
| Épaisseur d'un panneau `fond_mur` | 0,200 | 0,10 m | **2,000** |
| Hauteur de cloison de cabine WC (`Cube.012_Cube.005`) | 4,000 | 2,00 m | **2,000** |
| Profondeur de cabine WC | 3,000 | 1,50 m | **2,000** |
| Hauteur d'une affiche encadrée | 2,000 | 1,00 m | **2,000** |
| Largeur d'une affiche encadrée | 1,600 | 0,80 m | **2,000** |
| Hauteur du lambris `planche_bois_mur` | 4,000 | 2,00 m | **2,000** |
| Hauteur d'une applique `sconce_02` | 0,807 | 0,40 m | 2,018 |
| Hauteur de la porte extérieure (`Cube.049_Cube.014`) | 4,406 | 2,15 m | 2,049 |
| Barre de tirage de cette porte, au-dessus du sol du sas | 1,967 | 1,00 m | 1,967 |
| Plaque WC (`Cube.020_Cube.008`), centre au-dessus du sol | 3,096 | 1,50 m | 2,064 |
| Œil debout (`room.c:90`) | 3,500 | 1,70 m | 2,059 |
| Œil accroupi (`room.c:91`) | 2,700 | 1,31 m | 2,061 |

Les onze premières lignes ne doivent rien à `room.c` ; les deux dernières y sont rappelées
pour comparaison. Sur les **onze cotes géométriques** : **médiane 2,000 ; extrêmes 1,967 et
2,064 ; étendue ±2,5 %.**

### 1.3 Verdict

Les deux chemins **s'accordent à 3 % près**, et cet écart est plus petit que l'incertitude sur
la norme choisie (une porte de magasin fait 2,04 ou 2,15 m ; une poignée est à 1,00 ou 1,05 m).
**L'échelle du bâtiment est donc établie : 2,00 à 2,06 unités par mètre.**

Mais il faut dire ce qui gêne. Sept des onze ancrages géométriques tombent sur **2,000 exactement**, et ce
n'est pas un hasard de mesure : ce sont les objets que l'auteur a modélisés lui-même, et leurs
cotes sont rondes **en unités** (1,000 ; 0,200 ; 6,000 ; 4,000 ; 3,000 ; 1,600). À 2,00 u/m le
hall ferait exactement **15,00 × 14,84 m** sous **2,965 m**, et les piliers **0,50 m** de section.
À 2,06 il fait 14,56 × 14,40 sous 2,879, et les piliers 0,485.

> **Décision retenue : 2,06 u/m**, parce que c'est déjà la valeur du dépôt (`_echelle`,
> `SPEC-BORNE.md`, `DESIGN-SALLE.md`) et qu'en changer déplacerait tout le reste pour un gain de
> 3 %. Les cotes porteuses sont néanmoins données **aux deux échelles** en §3.4, pour que le
> choix reste réversible.

### 1.4 Le mobilier, lui, est à une autre échelle — et c'est mesurable

| Ce qui est mesuré | Unités | Norme | Échelle impliquée | Rendu à 2,06 |
|---|---|---|---|---|
| Dessus du comptoir du bar, au-dessus du sol | 2,584 | 1,10 m | 2,349 | **1,254 m** |
| Dossier de canapé | 2,120 | 0,85 m | 2,494 | 1,029 m |
| Dessus de bande du billard | 2,030 | 0,80 m | 2,537 | **0,985 m** |
| Marche de l'estrade | 0,470 | 0,18 m | 2,611 | 0,228 m |
| Plan de vasque en marbre (WC) | 2,228 | 0,85 m | 2,621 | 1,082 m |
| Tapis de billard | 1,982 | 0,75 m | 2,643 | 0,962 m |
| Hauteur d'une borne d'arcade | 4,925 | 1,80 m | 2,736 | **2,391 m** |
| Table du salon | 1,385 | 0,45 m | 3,078 | 0,672 m |

Médiane **2,62 u/m** — soit **1,31 fois** l'échelle du bâtiment.

Ce second groupe n'est pas une échelle concurrente : il est **exclu** par la géométrie. À
2,62 u/m la porte extérieure ferait 1,68 m de haut, la cloison de cabine WC 1,53 m et le plafond
2,26 m. Ce sont des valeurs impossibles. Donc **c'est le mobilier qui est trop grand, pas le
bâtiment qui est trop petit** — et il l'est de façon dispersée (2,35 à 3,08), c'est-à-dire objet
par objet, sans facteur commun. Le commentaire `_bornes` de `salle.room.json` avait raison en
écrivant « le modèle de 2020 mélangeait deux échelles » ; la mesure ajoute qu'il en mélangeait
plutôt huit.

**Conséquence pour la suite : le plan se reprend, les hauteurs de meubles ne se reprennent pas.**
C'est la frontière de §10.

---

## 2. Le repère : la correspondance d'axes, et pourquoi elle est l'identité

Ce paragraphe est celui qu'il faut lire avant de convertir quoi que ce soit. Une erreur ici rend
tout le document symétrique de la réalité.

**Dans l'OBJ, la verticale est Y.** Les treize `PILONNE` vont de Y = 0,007 à Y = 6,007 ; la dalle
`toit_Cube.033` est à Y = 6,030 ; le sol du hall est le plan Y = 0,100. L'export Blender 2.82
(en-tête ligne 1) applique sa conversion Z-up → Y-up, comme prévu.

**Le moteur de 2020 mesure ses angles depuis +Z, vers +X.** `room.c` appelle
`gluLookAt(px + sin(angle), …, pz + cos(angle), 0,1,0)`. La direction visée est donc
`(sin θ, ·, cos θ)` : `θ = 0` regarde **+Z**, `θ = π/2` regarde **+X**. Par ailleurs
`detectionEnvironnement(float x, float y)` est appelée avec `(camera.px, camera.pz)` : son
paramètre `y` est le **Z du monde**. Les cotes de `DESIGN-SALLE.md` §1.2 lues « en z » sont donc
bien des Z.

**Le moteur d'aujourd'hui utilise exactement la même convention.** `tools/roomgen.c:2167`
commente `X' = X cos + Z sin, Z' = -X sin + Z cos` : l'axe local +Z d'une borne part sur
`(sin yaw, cos yaw)`. `yaw = 0` regarde +Z, `yaw = 90` regarde +X. Vérification croisée dans le
JSON : `borne_arcade_7..12` sont à `z = +0,44` avec `yaw 0`, `borne_arcade_1..6` à `z = −0,44`
avec `yaw 180` — deux rangées dos à dos, ce qui n'est cohérent que si `yaw 0` regarde +Z.

**Le sens de parcours des murs confirme que le repère est direct.** `_convention` dit que
l'intérieur est à gauche. Sur `walls[coquille]`, le segment `(−9,65 ; −7,2) → (−9,65 ; +7,2)` a
pour direction +Z ; avec la verticale +Y et la règle de la main droite, la gauche est
`Y × Z = +X` — et l'intérieur de la salle est bien à +X de ce mur. La règle tient sur les cinq
segments.

**Donc les deux repères sont directs, tous deux Y en haut, et l'application de l'un vers l'autre
est une translation pure — pas une rotation, et surtout pas une symétrie.**

```
x_json = X_obj / 2,06
y_json = (Y_obj − 0,100) / 2,06        (sol du hall à y = 0)
z_json = (Z_obj − 9,6341) / 2,06       (9,6341 = milieu du hall en Z)
```

Le centre en X vaut déjà 0 dans l'OBJ : le hall est modélisé symétrique autour de X = 0.
Le décalage en Z est le milieu de `[−5,2004 ; 24,4686]`, l'emprise extérieure du hall.

**Contrôle de non-symétrie.** Sous cette application, le pan coupé d'entrée reste dans le
quadrant (+x, +z), c'est-à-dire au **nord-est** — là où `walls[coquille]` le place déjà
aujourd'hui, et là où `props[porte_entree_battant]` l'attend. Une symétrie l'aurait envoyé au
nord-ouest. Deuxième contrôle : le billard reste à l'**ouest** (x < 0) et les sanitaires à
l'**est**, ce que dit aussi `room.c:2171` (`x <= -5.0`) et `room.c:2176` (`x > 15.0`).

---

## 3. Le gabarit du bâtiment

### 3.1 Ce que la géométrie donne

Le sol du hall est **une seule face de treize sommets** (`salle.obj:601`, plan Y = 0,100, aire
782,70 u²), doublée par sa sous-face (`:596`, Y = −0,100, aire 813,10 u²). Cinq des treize
sommets sont colinéaires — ce sont les tableaux de baie du mur est. Le polygone n'a donc que
**huit coins réels**.

Emprises brutes, en unités :

| | X | Z | |
|---|---|---|---|
| Nu extérieur | −15,0000 … +15,0000 | −5,2004 … +24,4686 | **30,0000 × 29,6690** |
| Nu intérieur | −14,7272 … +14,7272 | −4,9276 … +24,1958 | **29,4544 × 29,1234** |
| Épaisseur de paroi | 0,2728 | 0,2728 | |

Deux **pans coupés symétriques** ferment le nord :

* nord-est, de `(13,1250 ; 15,0000)` à `(8,7640 ; 24,4686)`, pente `dx/dz = −0,46057` ;
* nord-ouest, de `(−13,1250 ; 15,0000)` à `(−8,7275 ; 24,4686)`, pente `dx/dz = +0,46443`.

Les deux pentes sont symétriques à 0,8 % près. `room.c:2108` utilise pour la collision
`x = −0,4736842 z + 19,36842` : c'est la même droite, décalée vers l'intérieur du rayon du
joueur. Le commentaire `_plan` de `salle.room.json` cite « `x = -0,4737 z + 9,401` » : la pente
est la bonne, l'ordonnée à l'origine ne correspond à aucune des deux et n'a pas été retrouvée.

Le mur nord — celui du bar — court de `(−8,7275 ; 24,4686)` à `(8,7640 ; 24,4686)` :
**17,4915 unités**, soit 8,49 m.

Hauteur sous plafond : sol à Y = 0,100, sous-face de `toit_Cube.033` à Y = 6,030 →
**5,930 unités = 2,879 m**.

### 3.2 Le polygone du hall, en mètres, prêt à écrire dans `walls`

Ligne **médiane** de la paroi (moyenne du nu extérieur et du nu intérieur), listée dans l'ordre
qui met l'intérieur à gauche, donc directement utilisable comme `walls[].points` :

```
[  7.215, -7.135 ]      mur sud, angle sud-est
[ -7.215, -7.135 ]      mur sud, angle sud-ouest
[ -7.215,  2.539 ]      mur ouest, départ du décrochement nord-ouest
[ -6.329,  2.539 ]      retour de 0,886 m
[ -4.203,  7.135 ]      pan coupé nord-ouest
[  4.203,  7.135 ]      mur nord (adossé au bar)
[  6.329,  2.539 ]      pan coupé nord-est
[  7.215,  2.539 ]      retour de 0,886 m
                        (fermé — le segment suivant est le mur est jusqu'au sommet 1)
```

Valeurs brutes mesurées, avant symétrisation : le sommet 5 vaut `−4,194` et le sommet 6 `4,212`
(écart de 1,8 cm, qui est du jeu de modélisation) ; les sommets 4 et 7 valent `−6,3291` et
`+6,3290`. La symétrisation ci-dessus est le seul arrondi appliqué au polygone.

Contrôles :

* aire signée **−188,00 m²** — le signe est négatif, comme celui de l'actuel `coquille`
  (−271,74) : même sens de parcours, la convention est respectée ;
* périmètre **54,084 m** ;
* hors-tout **14,430 m en X**, **14,270 m en Z** ;
* longueur de chaque pan coupé **5,064 m**, mur nord **8,406 m**, murs est et ouest **9,674 m**
  chacun avant le décrochement.

Les deux ouvertures du pourtour, avec leur `offset` mesuré **le long du périmètre depuis le
sommet 1 dans l'ordre ci-dessus** :

| Ouverture | `offset` | `width` | `sill` | `head` |
|---|---|---|---|---|
| `porte_entree` (dans le pan coupé nord-est) | **40,88** | **1,453** | 0,0 | **2,330** |
| `baie_sanitaires` (mur est, de z = −0,650 à z = −4,068) | **47,60** | **3,418** | 0,0 | 2,879 |

La porte d'entrée n'a ni battant ni tableau dans le modèle : c'est un vide franc de 1,45 m dans
le pan coupé, de Y = 0,101 à Y = 4,901, par lequel le sas s'aboute.

### 3.3 L'écart avec la salle décrite aujourd'hui

| | 2020, mesuré | `salle.room.json` | Écart |
|---|---|---|---|
| Largeur (X) hors-tout | **14,43 m** | 19,30 m | **+4,87 m, soit +34 %** |
| Profondeur (Z) hors-tout | **14,27 m** | 14,40 m | +0,13 m |
| Aire du pourtour | **188,0 m²** | 271,7 m² | **+83,7 m², soit +45 %** |
| Périmètre | 54,08 m | 65,34 m | +11,26 m |
| Hauteur sous plafond | **2,879 m** | 2,92 (`room.height`) / 3,10 (`ceilings[].y`) | +0,04 / +0,22 m |
| Pans coupés | **deux**, pente 0,462 (24,7°) | **un**, pente 0,978 (44,4°) | un manquant, l'autre deux fois plus incliné |
| Épaisseur de paroi | 0,133 m | 0,200 m | +0,067 m |

**D'où vient l'erreur.** `salle.room.json` (`_plan`) écrit « le hall d'origine faisait
19,3 x 14,4 m ». Ces deux nombres viennent de `DESIGN-SALLE.md` §1.2, où ils désignent
l'**emprise de collision totale** de `room.c` — c'est-à-dire le hall **plus le sas de 9 m**
(z de −4,5 à 35,2 → 19,27 m) — et la **largeur de collision** du hall seul (x de −14 à 15 →
14,08 m). Les deux cotes n'appartiennent donc pas au même volume, et elles ont ensuite été
attribuées l'une à X et l'autre à Z.

La coïncidence qui a rendu l'erreur invisible : la **profondeur** réelle du hall de 2020 vaut
29,669 u ÷ 2,06 = **14,4024 m**, et `salle.room.json` a mis 14,4 sur son axe Z. Cette moitié-là
est juste au centimètre. C'est la largeur qui est fausse : le hall de 2020 est **presque carré**
(rapport 1,011), pas allongé.

### 3.4 Les cotes porteuses aux deux échelles

| | unités | à 2,06 | à 2,00 |
|---|---|---|---|
| Largeur hall, nu extérieur | 30,0000 | 14,563 m | 15,000 m |
| Profondeur hall, nu extérieur | 29,6690 | 14,402 m | 14,835 m |
| Largeur hall, nu intérieur | 29,4544 | 14,298 m | 14,727 m |
| Profondeur hall, nu intérieur | 29,1234 | 14,138 m | 14,562 m |
| Mur nord (bar) | 17,4915 | 8,491 m | 8,746 m |
| Hauteur sous plafond | 5,9300 | 2,879 m | 2,965 m |
| Longueur d'un pan coupé | 10,4269 | 5,062 m | 5,213 m |
| Baie d'entrée | 2,9945 | 1,454 m | 1,497 m |
| Entraxe des bornes | 1,9415 | 0,942 m | 0,971 m |
| Allée centrale de l'îlot | 3,5535 | 1,725 m | 1,777 m |

### 3.5 Les deux volumes annexes

**Le bloc sanitaire — à l'EST, pas au sud-ouest.** Sol carrelé (`sol_toilette`,
`carllage_toilette.jpg`) de `x 7,279 … 11,952` et `z −6,131 … 1,536`, soit **4,672 × 7,667 m =
35,8 m²**, accolé au mur est du hall et relié par la baie de 3,42 m ci-dessus. Un mur de refend
(`mur_toilette`, `carllage_mur_toilette.jpg`) à `x = 8,94 … 9,06`, de `z = −4,750` à `z = 0,153`,
hauteur 2,185 m, sépare un sas de lavabos (x 7,28 … 8,94) de deux salles carrelées. Cloison en
marbre à `x = 10,48 … 10,53` de `z = −4,135` à `z = −0,506`, et deux refends transversaux à
`z = −4,12` et `z = −0,53`, `x 10,52 … 11,98`. Deux plans de vasque
(`Cube.022_Cube.012`, `Cube.027_Cube.052`), 0,78 × 1,94 m, dessus à 1,08 m.

**Le sas d'entrée — un couloir de 9 m.** Sol de `x 5,284 … 6,953` (**1,669 m de large**) et
`z 3,646 … 12,738` (**9,092 m de long**), plafond à **2,333 m** — donc plus bas que le hall de
55 cm. Il part du pan coupé nord-est et monte plein nord jusqu'à une porte extérieure
(`porte_exterieur.jpg`) de **1,214 × 2,139 m** au seuil `z = 12,755`. La position de départ
d'origine du jeu (`room.c:97-99`, `START_PX 12.5 / START_PZ 35.0`, angle π) est **dans ce sas, à
la porte, tourné vers la salle** : `(6,068 ; 12,313)` dans le repère du JSON.

---

## 4. Les treize piliers

Tous portent `marbre_poutre.014` → `bordeaux.jpg`. Section carrée **1,000 × 1,000 unité =
0,486 × 0,486 m**, de Y = 0,007 à Y = 6,007 (soit **2,867 m** au-dessus du sol, donc jusqu'au
plafond).

| Objet | Centre (x ; z), m | Situation |
|---|---|---|
| `PILONNE.017` | −7,257 ; −7,118 | angle sud-ouest |
| `PILONNE.016` | −4,804 ; −7,256 | mur sud |
| `PILONNE.011` | −1,648 ; −7,256 | mur sud |
| `PILONNE.009` | +1,648 ; −7,256 | mur sud |
| `PILONNE.008` | +4,804 ; −7,256 | mur sud |
| `PILONNE.010` | +7,269 ; −7,118 | angle sud-est |
| `PILONNE.001` | −7,300 ; −3,963 | mur ouest |
| `PILONNE.002` | −7,300 ; −0,666 | mur ouest |
| `PILONNE.004` | −7,300 ; +2,489 | mur ouest, départ du pan coupé |
| `PILONNE.006` | +7,269 ; −3,963 | mur est, jambage sud de la baie sanitaire |
| `PILONNE.003` | +7,269 ; −0,666 | mur est |
| `PILONNE.007` | +7,269 ; +2,489 | mur est, départ du pan coupé |
| `PILONNE.005` | **+1,648 ; −2,394** | **le seul isolé** : il coiffe le bout nord de la cloison du salon |

**Ce que la disposition raconte, et que la salle actuelle a perdu.** Douze des treize piliers
sont **engagés dans les murs** : ce sont des pilastres, qui débordent de 6 à 18 cm dans la pièce
et rythment le mur sud (cinq travées de 2,45 / 3,16 / 3,30 / 3,16 / 2,47 m) et les murs est et
ouest (trois travées de 3,16 / 3,30 / 3,16 m). Un seul est libre. La salle décrite aujourd'hui n'a que **deux** boîtes `pilier`, toutes
deux libres, à `(±5,4 ; −4,8)` — c'est-à-dire ni au bon endroit, ni en bon nombre, ni du bon
type.

Deux entraxes seulement dans toute la grille : **3,155 m** (6,4995 u) et **3,297 m** (6,791 u),
répétés à l'identique sur les quatre colonnes.

---

## 5. Les bornes

Quinze caissons `FLAPPY_BIRD_HARD.*` existent dans le modèle. `room.c:172` déclare
`NB_BORNES 16` : les quinze caissons plus le **poste de classement au bar** (`cible[15]`,
`room.c:1863`, en `(0,000 ; 4,800)` face au +Z). Douze sont jouables, ce sont ceux de l'îlot.

Gabarit identique pour les quinze : **0,889 (largeur) × 2,391 (hauteur) × 1,179 m (profondeur)**.

**Comment l'orientation a été établie** — par la normale de la face d'écran, pas par déduction.
Sur `FLAPPY_BIRD_HARD.009`, la face `Matériau.079` (`flappy_hard_font.jpg`) a pour normale
`(0 ; 0,31 ; 0,95)` : elle regarde +Z, inclinée de 18° vers le haut. Le panneau de commande
(`0 ; 0,97 ; 0,26`) est à `z 12,75 … 13,45`, le dos plein (`0 ; 0,01 ; −1,00`) à `z = 11,18`.
Confirmé par les caméras fixes de `room.c` : `cible[FLAPPY_HARD]` est posée au panneau de
commande avec `angle = π`, donc le joueur regarde −Z, donc la borne regarde +Z.

| Objet | Jeu (`room.c`) | `at` [x ; 0 ; z] | `yaw` | Rangée |
|---|---|---|---|---|
| `.009` | FLAPPY_HARD | [−3,186 ; 0 ; +1,293] | 0 | îlot nord |
| `.001` | TETRIS_HARD | [−2,241 ; 0 ; +1,293] | 0 | îlot nord |
| `.006` | ASTEROID_HARD | [−1,301 ; 0 ; +1,293] | 0 | îlot nord |
| `.013` | DEMINEUR_EASY | [+1,312 ; 0 ; +1,293] | 0 | îlot nord |
| `.005` | SNAKE_EASY | [+2,258 ; 0 ; +1,293] | 0 | îlot nord |
| `.014` | SHOOTER_EASY | [+3,198 ; 0 ; +1,293] | 0 | îlot nord |
| `.007` | SHOOTER_HARD | [−3,184 ; 0 ; +0,092] | 180 | îlot sud |
| `.008` | SNAKE_HARD | [−2,245 ; 0 ; +0,092] | 180 | îlot sud |
| `.010` | DEMINEUR_HARD | [−1,299 ; 0 ; +0,092] | 180 | îlot sud |
| `.004` | ASTEROID_EASY | [+1,315 ; 0 ; +0,092] | 180 | îlot sud |
| `.003` | TETRIS_EASY | [+2,254 ; 0 ; +0,092] | 180 | îlot sud |
| `.002` | FLAPPY_EASY | [+3,200 ; 0 ; +0,092] | 180 | îlot sud |
| `.011` | `cible[12]` | [−6,621 ; 0 ; +0,422] | 90 | mur ouest |
| `.012` | `cible[13]` | [−6,621 ; 0 ; +1,362] | 90 | mur ouest |
| `.015` | `cible[14]` | [ 0,000 ; 0 ; −4,541] | 0 | sur l'estrade centrale |

Cotes de l'implantation :

* **entraxe dans une rangée : 0,942 m** (1,9415 u) — l'actuel est 0,80 m ;
* **entraxe dos à dos : 1,201 m** (2,474 u), soit un jeu de 2,2 cm entre les deux dos —
  l'actuel est 0,88 m ;
* **allée centrale de l'îlot : 1,725 m**, entre x = −0,857 et x = +0,868 ;
* **emprise totale de l'îlot : 7,273 × 2,381 m**, de `x −3,631 … +3,642` et `z −0,498 … +1,883`.

**La règle de difficulté est spatiale, et elle est lisible.** Les six bornes HARD sont toutes à
x < 0, les six EASY toutes à x > 0. Au-dessus de l'allée centrale, `PANNEAU_EASY/HARD_Cube.035`
est suspendu au plafond par `Cylindre.005` en `(0,02 ; 0,67)`, à `y 2,088 … 2,476` : sa moitié
**rouge** est du côté −x (HARD), sa moitié **bleue** du côté +x (EASY), et les quatre `Texte.*`
(§6) répètent la même partition sur ses deux faces. Rien de tout cela n'existe aujourd'hui : les
dix-neuf bornes portent un champ `difficulty`, mais aucune géométrie ne le dit au joueur.

**Écart avec les dix-neuf bornes actuelles.**

| | 2020 | Aujourd'hui |
|---|---|---|
| Îlot central dos à dos | 12 | 12 |
| Contre un mur | **2**, mur ouest, `x = −6,62` | **6**, trois à `x = −9,1` et trois à `x = +9,1` |
| Isolée | 1, sur l'estrade, `(0 ; −4,54)` | 1 (`borne_classement`), `(0 ; −3,45)` |
| Total caissons | **15** | 19 |
| Emprise de l'îlot | 7,27 × 2,38 m | 6,48 × 1,76 m |
| Centre de l'îlot (z) | **+0,69 m** | 0,00 m |

Les quatre bornes en trop sont les trois du mur est — qui n'existe pas comme façade de bornes en
2020 — et une de plus au mur ouest.

---

## 6. Le mobilier nommé

Positions en mètres, repère de §2. Sauf mention, `centre` est le centre de la boîte englobante
au sol et les hauteurs sont comptées depuis le sol du hall.

| Objet d'origine | Centre (x ; z) | Emprise (m) | Hauteurs (m) | Matière |
|---|---|---|---|---|
| `Billiard_Table` (table seule, `contour_billard`) | −4,490 ; −4,686 | 3,606 × 1,943 | tapis 0,962 / bande 0,985 | `billard_table.jpg`, `bois.jpg` |
| `Billiard_Table` (suspension, `lampe_billard`) | −4,531 ; −4,788 | 1,474 × 0,639 | 2,320 … 2,336 | `jaune.jpg` |
| `sofa_Cube.001` | +3,708 ; −4,766 | 3,328 × 3,994 | −0,009 … 1,029 | `cuir_rouge.jpg` |
| `Cube.028/030/031` (table du salon) | +4,611 ; −5,275 | 1,456 × 1,456 | plateau 0,672 | `pub.jpg` |
| `Cube.013_Cube.029` (cloison du salon, bas) | +1,650 ; −4,817 | 0,437 × 4,369 | −0,385 … 1,071 | `poutre.jpg` |
| `PILONNE.005` (partie lambris, haut de cloison) | +1,659 ; −4,907 | 0,291 × 4,377 | 0,513 … 2,092 | `bois_noir.jpg` |
| `bar_accueil_Cube.030` | **0,000 ; +6,001** | 5,534 × 2,136 | dessus **1,254** | `desk.jpg` |
| `Cube.046_Cube.003` (tableau de classement) | 0,000 ; +5,140 | 0,823 × 0,092 | 1,448 … 1,910 | `leaderboard_font.jpg` |
| `Cube.059_Cube.007` (enseigne NINETEEN) | 0,000 ; +7,202 | 2,545 × 0,461 | 2,046 … 2,507 | `nineteen_name.jpg` |
| `Cube.047/039/032` (panneaux pub, face du bar) | −1,761 / 0,000 / +1,761 ; +5,103 | 1,602 × 0,243 | 0,208 … 1,179 | `pub.jpg` |
| `Cube.035/042` (panneaux pub, flancs du bar) | ∓2,602 ; +5,993 | 0,243 × 1,602 | 0,208 … 1,179 | `pub.jpg` |
| `CENTRE_MACHINE_SOL_Cube` (estrade) | 0,000 ; −4,677 | 2,427 × 3,398 | dessus **+0,228** | `moquette.jpg`, `Ke 0,230` |
| `PANNEAU_EASY/HARD_Cube.035` | +0,016 ; +0,673 | 0,971 × 0,078 | 2,088 … 2,476 | `rouge.jpg` / `bleu.jpg` |
| `Cylindre.005` (tige du panneau) | +0,023 ; +0,671 | 0,049 × 0,049 | 2,467 … 2,953 | — |

**Les six `sconce_02`** — applique murale, 0,232 × 0,392 × 0,276 m, toutes à
**y 2,048 … 2,439**, pied `jaune.jpg` :

| Objet | Centre (x ; z) | Mur |
|---|---|---|
| `sconce_02.002` | −4,783 ; −6,871 | sud |
| `sconce_02.001` | −1,653 ; −6,871 | sud |
| `sconce_02` | +1,652 ; −6,871 | sud |
| `sconce_02.003` | +4,810 ; −6,871 | sud |
| `sconce_02.005` | −6,924 ; −3,970 | ouest |
| `sconce_02.004` | −6,924 ; −0,667 | ouest |

Elles sont **exactement en face des piliers du mur sud** (±1,648 et ±4,804) : une applique par
travée.

**Les huit `planche_bois_mur`** — lambris `bois_noir.jpg`, tous à **y 0,718 … 2,660**,
épaisseur 0,291 m :

| Objet | Centre (x ; z) | Longueur | Mur |
|---|---|---|---|
| `.005` | −6,038 ; −7,296 | 1,766 | sud |
| `.004` | −3,213 ; −7,296 | 2,495 | sud |
| `.003` | 0,000 ; −7,296 | 2,495 | sud |
| `.002` | +3,221 ; −7,296 | 2,495 | sud |
| `.001` | +6,028 ; −7,296 | 2,126 | sud |
| `.006` | −7,326 ; −5,557 | 2,495 | ouest |
| `.007` | −7,326 ; −2,306 | 2,495 | ouest |
| `.008` | −7,326 ; +0,905 | 2,495 | ouest |

**Les cinq `Texte.*`** :

| Objet | Centre (x ; z) | Hauteur | Ce que c'est |
|---|---|---|---|
| `Texte.005` (rouge) | −0,218 ; +0,717 | 2,236 … 2,338 | « HARD », face nord du panneau |
| `Texte.004` (bleu) | +0,250 ; +0,717 | 2,236 … 2,338 | « EASY », face nord du panneau |
| `Texte.006` (rouge) | −0,223 ; +0,625 | 2,236 … 2,338 | « HARD », face sud |
| `Texte.007` (bleu) | +0,258 ; +0,625 | 2,236 … 2,338 | « EASY », face sud |
| `Texte.002` | +8,935 ; −2,347 | 1,518 … 1,749 | lettrage sur le refend des sanitaires |

**Le `CENTRE_MACHINE_SOL_Cube`** mérite une ligne à lui : c'est l'**estrade centrale**, un
podium de 2,427 × 3,398 m au milieu exact du hall en X, à `z = −4,677`, dépassant de **22,8 cm**
le sol, texturé `moquette.jpg` et — seul objet du décor avec le plafond dans ce cas —
**émissif** (`Ke 0,230`, `salle.mtl:4`). La borne `.015` est posée dessus.

**Les affiches encadrées** — cadre `poutre.jpg`, format 0,777 × 0,971 m, toutes à
**y 1,172 … 2,143** dans le hall :

| Objet | Centre (x ; z) | Mur | Image |
|---|---|---|---|
| `Cube.005_Cube.079` | −6,057 ; −7,149 | sud | `poster_6.jpg` |
| `Cube.003_Cube.077` | −3,389 ; −7,149 | sud | `poster_1.png` |
| `Cube.002_Cube.076` | −0,054 ; −7,149 | sud | `poster_3.jpg` |
| `Cube.004_Cube.078` | +3,297 ; −7,149 | sud | `poster_2.png` |
| `Cube.001_Cube.075` | +6,010 ; −7,149 | sud | `poster_8.jpg` |
| `Cube.044_Cube.004` | −7,142 ; −5,549 | ouest | `poster_4.jpg` |
| `Cube.045_Cube.010` | −7,142 ; −2,260 | ouest | `poster_5.jpg` |

Neuf autres cadres identiques (`Cube.050 … Cube.058`) sont posés à `x = 8,533` et `x = 10,172`,
de `z = 3,633` à `z = 11,668` : ils sont **hors de la salle**, dans une galerie parallèle au sas
que `detectionEnvironnement` interdit au joueur (`room.c:2176`). Ils ne font pas partie du plan.

**Trois postes de radio** (`Radio.jpg`, matériaux `Material.008/.009/.254`) : sur le comptoir du
bar `(−0,852 ; +5,157)`, au-dessus du billard `(−5,881 ; −3,879)`, et dans le salon
`(+5,178 ; −4,892)`. `room.c:2218` (`detecterRadio`) confirme qu'ils sont interactifs.

---

## 7. Le tableau de correspondance

| Objet d'origine | Position mesurée (m) | Ce que la salle actuelle en fait | Écart |
|---|---|---|---|
| Pourtour du hall | 14,43 × 14,27, aire 188,0 m² | 19,30 × 14,40, aire 271,7 m² | **+4,87 m en X, +45 % d'aire** |
| Pan coupé nord-est | `(6,329 ; 2,539)` → `(4,203 ; 7,135)`, 5,06 m | `(6,173 ; 7,2)` → `(9,65 ; 3,644)`, 4,97 m | même coin, pente 0,462 → 0,978 |
| Pan coupé nord-ouest | `(−6,329 ; 2,539)` → `(−4,203 ; 7,135)` | **absent** | manquant |
| Hauteur sous plafond | 2,879 | 2,92 / 3,10 | +0,04 / +0,22 |
| Sas d'entrée | couloir 1,67 × 9,09, plafond 2,33 | `cloison_entree`, L de 5,05 × 3,00 | longueur ÷ 3 |
| Porte extérieure | 1,214 × 2,139, seuil `z = 12,755` | `porte_entree`, 2,4 large, dans le pan coupé | 2,4 m au lieu de 1,45 ; sas raccourci |
| Bloc sanitaire | **à l'est**, 4,67 × 7,67 = 35,8 m² | **au sud-ouest**, 3,45 × 3,60 = 12,4 m² | autre côté, aire ÷ 2,9 |
| 13 `PILONNE` | 12 pilastres engagés + 1 libre, 0,486² | 2 boîtes libres à `(±5,4 ; −4,8)`, 0,48² | 11 manquants, 2 mal placés |
| Îlot de 12 bornes | 7,27 × 2,38, centre `z = +0,69`, entraxe 0,942 | 6,48 × 1,76, centre `z = 0`, entraxe 0,80 | îlot 12 % plus court en X, 26 % moins profond |
| Bornes murales | **2**, mur ouest, `x = −6,62` | **6**, `x = ±9,1` | +4 |
| Borne isolée | `(0 ; −4,54)` sur l'estrade | `borne_classement` `(0 ; −3,45)` | 1,09 m plus au sud |
| Estrade | 2,43 × 3,40, `(0 ; −4,68)`, +0,228 | `boxes[estrade]` 2,4 × 2,4, `(0 ; −3,10)`, +0,12 | −1,00 m de profondeur, 1,58 m plus au sud |
| Panneau EASY/HARD | `(0,02 ; 0,67)`, `y 2,09 … 2,48` | **absent** | manquant |
| `bar_accueil` | **`(0,000 ; +6,001)`**, 5,53 × 2,14, dessus 1,254 | `props[bar_accueil]` `(−2,60 ; +6,30)` | **décentré de 2,60 m** |
| Enseigne NINETEEN | `(0,000 ; +7,202)`, `y 2,05 … 2,51` | `(−2,60 ; +7,05)`, `y 2,47` | décentrée de 2,60 m |
| Tableau de classement | `(0,000 ; +5,140)`, sur le comptoir | `scoreboard: classement_fond`, derrière le comptoir | changé de côté |
| `Billiard_Table` | `(−4,49 ; −4,69)`, 3,61 × 1,94, aligné sur X | `props[billard]` `(−7,20 ; −1,60)`, `yaw 12` | **+2,71 m en X, −3,09 m en Z**, et tourné |
| `sofa_*` | `(+3,71 ; −4,77)`, 3,33 × 3,99 | `props[canape]` `(+7,30 ; −1,60)`, `yaw 270` | **−3,59 m en X, −3,17 m en Z** |
| Table du salon | `(+4,61 ; −5,28)`, 1,46 × 1,46 | `props[table_basse]` `(+6,30 ; −2,70)` | −1,69 / −2,58 m |
| Cloison du salon | `x = 1,650`, de `z = −7,00` à `z = −2,63` | **absente** | manquante |
| 6 `sconce_02` | 4 mur sud, 2 mur ouest, `y 2,05 … 2,44` | `applique_sas` seule, `(4,70 ; 6,40)` | 5 manquantes |
| 8 `planche_bois_mur` | lambris `y 0,72 … 2,66` sur les murs sud et ouest | plinthe 0,14 m + cimaise à 2,42 m | lambris absent |
| 7 affiches du hall | 5 mur sud, 2 mur ouest, 0,78 × 0,97 | 8 `affiche_*` réparties sur quatre murs | placement différent |
| 5 panneaux `publicite` | sur les faces et les flancs du bar | **absents** | manquants |
| 3 radios | comptoir, billard, salon | `radio_murale` `(−9,50 ; 3,60)`, `boombox`, `jukebox` | 1 sur 3 au bon endroit |

---

## 8. Ce qui a disparu, et ce qui est apparu

### 8.1 Dans l'original, absent de la salle décrite

1. **Le pan coupé nord-ouest** — la salle de 2020 est chanfreinée des deux côtés, en entonnoir
   vers le bar. La reconstruction n'a gardé que celui de l'entrée.
2. **Les onze pilastres de mur** et leur rythme de travées (3,155 / 3,297 m).
3. **Le lambris `bois_noir` de 1,94 m** sur les murs sud et ouest (8 panneaux).
4. **Le panneau EASY/HARD suspendu** et ses quatre lettrages, avec la règle spatiale
   HARD à l'ouest / EASY à l'est qu'il annonce.
5. **La cloison du salon** à `x = 1,650`, qui isole le coin canapé du hall, et le pilier libre
   qui la termine.
6. **Les cinq panneaux publicitaires du bar** (`pub.jpg`), sur sa façade et ses deux flancs.
7. **Le tableau de classement posé SUR le comptoir**, à `(0 ; +5,140)`.
8. **Le sas de 9 m** : le couloir d'entrée de 2020 n'est pas un sas, c'est une entrée de galerie
   marchande, avec un plafond 55 cm plus bas et sa propre porte vitrée au bout.
9. **Le bloc sanitaire de 35,8 m² à l'est**, avec son sas de lavabos et son mur de refend.
10. **Quatre appliques murales sur six.**
11. **La galerie extérieure** (9 cadres, `x 8,5 … 10,2`) — décor de fond visible depuis le sas,
    inaccessible. Sa perte est sans conséquence sur le plan mais elle explique pourquoi le
    modèle est deux fois plus large que la salle.

### 8.2 Dans la salle décrite, absent de l'original

* **Quatre bornes** : les trois du mur est et une du mur ouest.
* **Le mobilier de loisir ajouté** : `babyfoot`, `jukebox`, `distributeur`, `vitrine_lots`,
  `monnayeur`, `cible_flechettes`, `boombox`, deux `mange_debout`, quatre `banc_*`,
  `poubelle_*` (3), `plante_salon`, `fauteuil_salon`, `bureau`, `tabouret_bar`,
  `extincteur`, `poubelle_sas` — aucun n'a d'équivalent dans l'OBJ.
* **Le plafond suspendu à dalles** (`ceilings[0]`, `tile 1,2`, rails, plénum, dalle manquante) :
  en 2020 le plafond est **une seule face plane** de 26,9 × 21,5 m.
* **Les cinq moulures** (`plinthe_est`, `plinthe_ouest`, `corniche_hall`, `cimaise_hall`,
  `nez_estrade`) : aucune n'existe dans le modèle d'origine.
* **Les 25 lumières nommées** : le modèle de 2020 n'a aucune source déclarée, seulement deux
  matériaux émissifs (le plafond `Ke 1,0`, l'estrade `Ke 0,230`) et six appliques.
* **Les cinq poutres de plafond.** À vérifier avant de les retirer : `poutre.jpg` est employé
  dans l'OBJ par 16 matériaux, et **tous** sont des cadres d'affiche ou la cloison du salon.
  Il n'y a **aucune poutre au plafond** en 2020. Le « cinq poutres de 19,3 m » de
  `DESIGN-SALLE.md` §2.5 ne correspond à rien de mesurable dans le modèle.

---

## 9. La liste ordonnée des changements

Du plus structurant au plus fin. Chaque ligne porte le risque qu'elle fait courir.

### C1 — Écrire l'échelle et le repère dans le fichier *(aucun risque)*

Remplacer dans `_echelle` et `_plan` la phrase « le hall d'origine faisait 19,3 x 14,4 m » par
les cotes mesurées, et consigner l'application de §2. Aucune géométrie ne bouge. **À faire en
premier** : c'est ce qui empêche l'erreur de se reformer.

### C2 — Le pourtour *(risque le plus élevé du lot)*

Écrire les huit points de §3.2 dans `walls[coquille].points`. La salle rétrécit de 4,87 m en X
et gagne un pan coupé.

**Ce qui casse, mesuré :** **39 entités** du fichier tombent hors du nouveau polygone —
6 bornes (`borne_arcade_13` à `18`), 26 props et 7 lumières. Le détail :

* bornes : `borne_arcade_13/14/15` (`x = −9,1`), `16/17/18` (`x = +9,1`) ;
* props murs est/ouest : `affiche_ouest`, `affiche_ouest_b`, `affiche_est`, `affiche_est_b`,
  `radio_murale`, `cible_flechettes`, `distributeur`, `extincteur`, `tapis_technique`,
  `plante_salon`, `mange_debout_ouest`, `vitrine_lots`, `bureau`, `canape` ;
* tout le bloc toilettes actuel : `lavabo_toilettes`, `robinet_toilettes`, `flaque_toilettes`,
  `panneau_sol_mouille`, `panneau_restrooms`, `cabine_toilettes`, `porte_cabine_1` ;
* tout le sas actuel : `porte_entree_battant`, `panneau_sortie`, `applique_sas`, `ampoule_sas` ;
* `affiche_nord` (`z = 7,08`, désormais au-delà du mur nord raccourci) ;
* lumières : `plafonnier_sas`, `plafonnier_toilettes`, `neon_mur_ouest`, `neon_mur_est`,
  `applique_entree`, `applique_technique`, `applique_sas`.

**Ce que `roomgen` en fait :** `check_solid_overlaps` (`tools/roomgen.c:1069`) appelle
`tool_fatalf` — build interrompu, pas un avertissement — dès qu'une **borne** partage un volume
avec un autre solide, même sans triangle commun (ligne 1102 : « une borne doit se tenir dans du
VIDE »). Une borne poussée dans un mur arrête donc la compilation. Pour les autres solides,
l'échec est prononcé à la pénétration confirmée au triangle (`first_vertex_inside`), sauf paire
inscrite dans `RG_OVERLAP_ASSEMBLY` ou `RG_OVERLAP_DEBT`.

**Ordre imposé :** vider ou déplacer les 39 entités **avant** d'écrire le nouveau polygone, pas
après. Sinon le fichier ne compile plus et on perd la capacité de vérifier les étapes suivantes
une par une.

**Effet de bord obligatoire :** le périmètre passe de **65,34 m** à **54,084 m**. Tout `offset`
d'ouverture et toute liste `points` de moulure sont exprimés le long de ce périmètre ou sur ces
murs : `openings[porte_entree]`, `plinthe_est`, `plinthe_ouest`, `corniche_hall`,
`cimaise_hall` sont **tous** à recalculer. Le commentaire de `openings[porte_entree]` note que
ce piège a déjà mordu une fois.

### C3 — Le plafond, la hauteur, l'emprise jouable *(risque faible)*

`room.height` 2,92 → **2,879**. `ceilings[0].y` 3,10 → **2,879**. `ceilings[0].size`
[22,2 ; 16,2] → **[14,8 ; 14,7]**, `centre` [0 ; 0] conservé. `room.playable` → la boîte de
§3.2, soit min [−7,215 ; 0 ; −7,135], max [+7,215 ; 2,879 ; +7,135].

Risque : la dalle manquante `missing: [[14, 10]]` est indexée sur une grille de 22,2 × 16,2 m ;
à 14,8 × 14,7 l'index 14 sort de la trame. À réindexer sinon le plafond se troue au mauvais
endroit ou pas du tout.

### C4 — Les sols *(risque faible)*

`sol_hall` → `centre [0 ; 0]`, `size [14,8 ; 14,7]`. `sol_couloir` (le sas) →
`centre [6,119 ; 8,192]`, `size [1,77 ; 9,19]`. `sol_toilettes` → `centre [9,616 ; −2,298]`,
`size [4,77 ; 7,77]`, c'est-à-dire **à l'est**.

Risque : les sols débordent volontairement sous les murs ; un débord de 0,3 m suffit, au-delà
on crée des faces coplanaires visibles depuis le sas.

### C5 — L'îlot de bornes *(risque élevé)*

Réécrire les douze `at` de l'îlot avec les valeurs de §5 : entraxe **0,942**, rangées à
**z = +0,092** et **z = +1,293**, allée centrale de **1,725 m**.

**Ce qui casse :** l'îlot de 2020 occupe `x −3,631 … +3,642` et `z −0,498 … +1,883`.
`props[poubelle_ilot]`, à `(−1,55 ; +1,55)`, est **dans** cette emprise ;
`props[banc_ilot_ouest]` `(−2,40 ; 2,45)` et `banc_ilot_est` `(2,40 ; 2,45)` sont à 57 cm du
dos de la rangée nord, `props[babyfoot]` `(−4,00 ; 2,80)` à 92 cm. Un seul de ces quatre qui
déborde et `check_solid_overlaps` arrête le build avec le message « occupent le même volume ».

Note : l'entraxe de 0,942 m est **la cote de 2020**, mais `DESIGN-SALLE.md` §1.6 la classe
« accidentelle » et 0,80 m « jouable ». Deux valeurs défendables ; le propriétaire a demandé
le plan d'origine, donc 0,942. Voir §10 pour la frontière.

### C6 — Les quatre bornes en trop *(risque moyen)*

Supprimer `borne_arcade_16/17/18` (mur est, inexistant en 2020) et une des trois du mur ouest.
Placer les deux restantes à `x = −6,621`, `z = +0,422` et `z = +1,362`, `yaw 90`. Déplacer
`borne_classement` sur l'estrade, `(0 ; −4,541)`, `yaw 0`, `y` = hauteur d'estrade.

Risque : le mur ouest intérieur est à `x = −7,149` et une borne de 1,179 m de profondeur centrée
à −6,621 s'y enfonce de **6 cm** — c'est le cas dans le modèle d'origine, où la borne pénètre le
mur. Ramener le centre à `x = −6,56` pour l'éviter, ou inscrire la paire dans
`RG_OVERLAP_DEBT`. **Ne pas reprendre l'interpénétration telle quelle** : c'est un défaut, pas
un trait de plan.

### C7 — L'estrade et le pilier libre *(risque faible)*

`boxes[estrade]` → `at [0 ; 0 ; −4,677]`, `size [2,427 ; h ; 3,398]`. `mouldings[nez_estrade]`
recalé sur cette emprise (il l'était déjà sur l'ancienne, ne pas laisser le décalage revenir).
Ajouter le pilier libre `PILONNE.005` à `(1,650 ; −2,394)`, section 0,486.

### C8 — Le bar, l'enseigne, le classement *(risque faible)*

`props[bar_accueil]` de `(−2,60 ; +6,30)` à **`(0,000 ; +6,001)`** ; emprise 5,534 × 2,136 ;
face client au sud. `props[enseigne_nineteen]` à **`(0,000 ; +7,202)`**, `y 2,046 … 2,507`.
Le tableau de classement passe **sur** le comptoir, `(0,000 ; +5,140)`, `y 1,448 … 1,910`.

Risque : `scoreboard: "classement_fond"` désigne aujourd'hui un matériau **derrière** le
comptoir ; le déplacer devant change quel matériau le moteur traite comme écran vivant. Vérifier
que le matériau nommé suit le déplacement, sinon le classement se dessine sur un mur vide.

### C9 — Le billard, le canapé, la cloison du salon *(risque moyen)*

`props[billard]` de `(−7,20 ; −1,60) yaw 12` à **`(−4,49 ; −4,69) yaw 0`** (grand axe sur X).
`props[canape]` à **`(+3,71 ; −4,77)`**. `props[table_basse]` à **`(+4,61 ; −5,28)`**.
Ajouter la cloison du salon : `x = 1,650`, de `z = −7,00` à `z = −2,63`, hauteur 2,09 m.

Risque : la cloison coupe la circulation entre le hall et le mur sud-est. Le sas des toilettes
étant lui aussi passé à l'est (C10), le joueur ne doit pas se retrouver enfermé. Vérifier le
passage entre le bout nord de la cloison (`z = −2,63`) et la baie sanitaire (`z = −4,07 … −0,65`)
avant de valider.

### C10 — Le bloc sanitaire *(risque élevé)*

Déplacer les toilettes du sud-ouest vers l'est : `walls[cloison_toilettes]` remplacé par un
volume `x 7,28 … 11,95`, `z −6,13 … +1,54`, relié par `openings[baie_sanitaires]` du mur est
(offset 47,60, largeur 3,418). Mur de refend à `x = 9,00`, de `z = −4,75` à `z = +0,15`.

Risque : c'est un volume **hors du pourtour**, comme le sas. `walls[coquille]` étant fermé, il
faut soit l'ouvrir, soit décrire les sanitaires comme un second pourtour accolé. Le comportement
de `roomgen` sur deux pourtours qui partagent une paroi n'a pas été vérifié (§11).

### C11 — Le sas *(risque moyen)*

`walls[cloison_entree]` remplacé par le couloir de §3.5 : 1,669 m de large, 9,092 m de long, de
la baie du pan coupé (`offset 40,88`, largeur 1,453) jusqu'à `z = 12,738`, plafond à 2,333 m.
`playerStart` à `(6,068 ; 12,313)`, `yaw 180` — la position de départ d'origine, `room.c:97`.

Risque : le sas de 2020 est 55 cm plus bas que le hall. Si `ceilings` ne sait décrire qu'un
plafond, le sas prendra celui du hall et l'effet de compression à l'entrée — qui est ce qui fait
« découvrir » la salle — sera perdu. À vérifier avant de raccourcir le sas pour compenser.

### C12 — Les treize piliers *(risque faible)*

Remplacer les deux `boxes[pilier_*]` par les treize positions de §4, section 0,486, hauteur
2,867 m, matériau bordeaux.

Risque : douze sur treize sont **engagés dans le mur**, donc en recouvrement permanent avec la
coquille. `check_solid_overlaps` échouera sur chaque paire pilier/mur si elle n'est pas déclarée.
Prévoir soit `RG_OVERLAP_ASSEMBLY`, soit un `profile` de moulure verticale plutôt que des boîtes.
C'est la raison technique pour laquelle la reconstruction n'en avait gardé que deux.

### C13 — Les appliques, le lambris, les affiches *(risque faible)*

Six `sconce` aux positions de §6 (`y 2,048 … 2,439`). Huit panneaux de lambris `bois_noir`,
`y 0,718 … 2,660`, aux longueurs mesurées. Sept affiches, format 0,777 × 0,971,
`y 1,172 … 2,143`.

Risque : le lambris de 1,94 m recouvre la cimaise actuelle (à 2,42 m) et une bonne partie du mur.
Comme il porte `bois_noir.jpg` (réflectance 2020 : 0,0199), sa dé-cuisson est concernée par la
règle de `DESIGN-SALLE.md` §1.4 — poser le lambris, ce n'est pas décider de sa couleur.

### C14 — Le panneau EASY/HARD *(risque faible)*

Panneau suspendu à `(0,016 ; +0,673)`, `y 2,088 … 2,476`, 0,971 m de large, moitié rouge à
l'ouest, moitié bleue à l'est ; tige jusqu'au plafond. Réordonner les `game` et `difficulty`
des douze bornes de l'îlot pour que les six HARD soient à x < 0 et les six EASY à x > 0, comme
au tableau de §5.

### C15 — Les panneaux du bar, les radios *(risque nul)*

Cinq `publicite` sur la façade et les flancs du bar. Trois radios : comptoir `(−0,852 ; +5,157)`,
billard `(−5,881 ; −3,879)`, salon `(+5,178 ; −4,892)`.

---

## 10. Ce qu'il ne faut PAS reprendre de 2020

**La frontière, en une phrase :** on reprend les positions au sol, les orientations, les
entraxes, les emprises en plan et les hauteurs de PAROI. On ne reprend pas les hauteurs de
MEUBLE, ni un matériau, ni une UV, ni un émissif, ni un défaut de modélisation.

Ce qui est mesuré ici et qui doit rester dehors :

| Défaut de 2020, mesuré | Valeur | Ce qu'il faut à la place |
|---|---|---|
| **Bornes de 2,391 m** sous 2,879 de plafond | 4,925 u | 1,86 m — la cote de `_bornes`, déjà décidée |
| **Comptoir de bar à 1,254 m** | 2,584 u | 1,10 m |
| **Bande de billard à 0,985 m**, tapis à 0,962 | 2,030 / 1,982 u | 0,80 / 0,75 m |
| **Plan de vasque à 1,082 m** | 2,228 u | 0,85 m |
| **Dossier de canapé à 1,029 m** | 2,120 u | 0,85 m |
| **Marche d'estrade de 22,8 cm** | 0,470 u | 17 cm, ou deux marches |
| **Table de salon à 0,672 m** | 1,385 u | 0,45 m |
| **UV du plafond : un motif tous les 8,69 m en Z et 7,87 m en X** | `u` varie de 2,4792 sur 44,361 u ; `v` de 3,4185 sur 55,450 u | `uvMetres` déclaré, 0,60 pour une dalle |
| **`Ke 1,0` sur le plafond** (`salle.mtl:1188`) | — | un plafond n'est pas une source ; les luminaires le sont |
| **`fond_mur` à `Kd (0,0038 ; 0,0031 ; 0,0028)`** | `salle.mtl:552` | intenable en PBR ; la leçon est déjà tirée |
| **Textures qui sont des aplats** : `bleu`, `rouge`, `noir`, `jaune`, `gris_foncer`, `marron_foncer` en **24 × 24 px** ; `bordeau_uni` en **1 × 1 px** | mesuré sur les JPEG | des couleurs déclarées, pas des images |
| **La tige de la suspension du billard monte à 4,487 m** — elle traverse un plafond à 2,879 | `support_lampe_billard`, Y jusqu'à 9,344 | arrêter la tige au plafond |
| **Les bornes du mur ouest pénètrent le mur de 6 cm** | centre à −6,621, nu à −7,149 | reculer de 6 cm |
| **`booleen.001_Cube.036`** — un cube sans matériau, oublié : c'est l'outil booléen qui a percé la baie d'entrée, resté dans la scène | `x 8,09 … 9,06 ; z 3,65 … 4,97` | ne pas l'importer |
| **Paroi de 0,133 m** | 0,2728 u | 0,20 m, la valeur actuelle, est meilleure |
| **La galerie extérieure de 9 cadres** (`x 8,5 … 10,2`, `z 3,6 … 11,7`), interdite au joueur | `room.c:2176` | décor de fond, à traiter comme tel ou à retirer |
| **Deux faces de sol à 13 sommets** dont 5 colinéaires | `salle.obj:596` et `:601` | 8 points suffisent |

Une nuance sur l'entraxe. `DESIGN-SALLE.md` §1.6 classe les 0,942 m « accidentels ». La mesure ne
tranche pas cette question : elle constate que les six bornes d'une rangée sont à 0,942 ± 0,008 m
et que le jeu latéral entre deux caissons est de **5,3 cm**. C'est un entraxe **délibéré et
régulier**, pas une dérive. Le propriétaire a demandé son plan : 0,942 m en fait partie. Si
`roomgen` refuse l'îlot à cette cote, la bonne réaction est de vérifier pourquoi, pas de
retomber sur 0,80 sans le dire.

---

## 11. Ce qui n'a PAS pu être mesuré

Rien de ce qui suit n'est affirmé ailleurs dans ce document.

1. **L'échelle absolue à mieux que ±3 %.** Aucun ancrage du modèle ne porte de cote inscrite.
   Les onze valeurs géométriques de §1.2 dépendent chacune d'une norme choisie par moi. 2,00 et
   2,06 sont tous deux défendables ; je n'ai pas de mesure qui les départage.
2. **L'identité de `Cube.165_Cube.011` et `Cube.129_Cube.039`** : deux volumes gris identiques
   de 0,756 × 1,410 × 0,819 m, matériaux `Material.001` et `Material.004` (`Kd 0,8` uni, aucune
   `map_Kd`), debout dans chacune des deux salles carrelées, à `(9,764 ; −1,785)` et
   `(9,769 ; −2,823)`. Cuvettes, urinoirs, distributeurs : je ne sais pas.
3. **Le sens de la règle HARD/EASY.** Il est mesuré que les six HARD sont à x < 0 ; il n'est pas
   mesuré que c'était voulu plutôt que subi par l'ordre de l'`enum` de `room.c:171`.
4. **Le détail des appareils sanitaires.** J'ai relevé les deux plans de marbre, le mur de
   refend et les cloisons ; les cuvettes et les vasques elles-mêmes n'ont pas été démêlées objet
   par objet.
5. **Ce que `roomgen` fait de deux pourtours accolés** (C10) et **d'un plafond à deux hauteurs**
   (C11). Je n'ai pas compilé. Ce sont des inconnues de conception, pas des inconnues de mesure.
6. **L'ordonnée à l'origine `9,401`** citée dans `_plan` pour la diagonale. La pente `−0,4737`
   est bien celle de `room.c:2108`, mais `9,401` ne correspond ni à la droite de collision
   (19,368), ni au nu extérieur (20,034), ni au nu intérieur (19,734). Je n'ai pas trouvé d'où
   elle vient.
7. **L'effet visuel du plan rétréci.** Je n'ai produit aucune capture. Passer de 271,7 à
   188,0 m² à densité de mobilier constante peut rendre la salle étroite ; c'est une question
   d'usage, pas de mesure, et elle se tranche à l'image.

### 11 bis. Trois valeurs déjà publiées que la mesure corrige

Elles ne sont pas des lacunes mais des rectifications, et elles sont séparées du reste pour
qu'on ne les confonde pas avec des relevés neufs.

* **« Le hall d'origine faisait 19,3 × 14,4 m »** (`salle.room.json`, `_plan`). Mesuré :
  **14,43 × 14,27 m**. Le 19,3 est l'emprise de collision **hall + sas** de `room.c:2203-2213`,
  et le 14,4 la profondeur du hall seul — deux volumes différents, portés sur deux axes.
  Détail en §3.3.
* **« Les UV du plafond bouclaient une fois tous les 17,9 m »** (`salle.room.json:30` et
  `:4323`). Mesuré sur la face basse de `toit_Cube.033` : `u` parcourt 2,4792 sur 44,361 unités
  de Z, `v` 3,4185 sur 55,450 unités de X. Périodes : **17,894 unités en Z** et **16,221 unités
  en X**. Le nombre 17,9 est juste, mais c'est **17,9 unités**, soit **8,69 m**. La période en
  X, **7,87 m**, n'avait jamais été relevée. Le reproche tient ; le chiffre publié est dans la
  mauvaise unité.
* **« Cinq poutres de 19,3 m »** (`DESIGN-SALLE.md` §2.5). Les seize matériaux `poutre*` de
  `salle.mtl` sont employés dans l'OBJ sur **seize cadres d'affiche** et **une cloison de
  salon**, et sur rien d'autre. Il n'y a **aucune poutre de plafond** dans le modèle de 2020.
  La `boxes[poutre]` de la salle actuelle, de 19,3 m de long, n'a pas d'antécédent.

---

## 12. Les décisions

* L'échelle retenue est **2,06 unité par mètre** ; **2,00** est aussi défendable et les cotes
  porteuses sont données aux deux.
* Les deux chemins de vérification de l'échelle **s'accordent à 3 % près** sur tout ce que
  l'auteur a construit, et **divergent de 14 à 50 %** sur tout ce qu'il a importé.
* Le mobilier de 2020 est surdimensionné d'un facteur médian **1,31**, de façon dispersée : ses
  hauteurs ne sont donc pas une source.
* L'application OBJ → `salle.room.json` est une **translation pure**,
  `x = X/2,06`, `y = (Y − 0,100)/2,06`, `z = (Z − 9,6341)/2,06` : pas de rotation, pas de
  symétrie, contrôlée sur le pan coupé, le billard et les sanitaires.
* Le hall de 2020 mesure **14,43 × 14,27 m** hors-tout, aire **188,0 m²** : il est **presque
  carré**, et non 19,3 × 14,4.
* Le « 19,3 » du fichier actuel est la profondeur de collision **hall + sas** de `room.c`, portée
  par erreur sur l'axe X.
* Le pourtour à écrire est le **polygone de huit points de §3.2**, périmètre 54,084 m, avec
  **deux** pans coupés symétriques de pente 0,462.
* La hauteur sous plafond mesurée est **2,879 m**, contre 2,92 déclarée et 3,10 dessinée.
* Il y a **treize** piliers de 0,486 m de côté, dont **douze engagés dans les murs** et **un
  seul libre**, et non deux libres.
* Il y a **quinze** caissons de borne, et non dix-neuf : douze en îlot dos à dos, deux au mur
  ouest, un sur l'estrade centrale. La seizième « borne » de `room.c` est le poste de classement
  du bar.
* L'entraxe d'origine est **0,942 m**, l'entraxe dos à dos **1,201 m**, l'allée centrale
  **1,725 m**.
* Les six bornes HARD sont à **x < 0**, les six EASY à **x > 0**, sous un panneau suspendu qui
  le dit.
* Le bar est **centré sur x = 0**, pas à −2,60.
* Les sanitaires sont **à l'est** et font **35,8 m²**, pas au sud-ouest sur 12,4.
* Le sas fait **9,09 m de long sous 2,33 m**, pas 3 m sous le plafond du hall.
* La position de départ d'origine est **dans le sas, à la porte extérieure**, tournée vers la
  salle.
* Il n'y a **aucune poutre de plafond** dans le modèle de 2020.
* Les UV du plafond bouclent tous les **17,894 unités en Z (8,69 m)** et **16,221 en X
  (7,87 m)** : le « 17,9 m » publié est un « 17,9 unités » lu comme des mètres.
* La liste ordonnée des quinze changements est **C1 à C15** ; **C2 casse 39 entités** et doit
  être précédée de leur déplacement, sans quoi `check_solid_overlaps` arrête le build.
* Ce qui se reprend de 2020 est le **plan** ; ce qui ne se reprend pas est listé au §10, cote par
  cote.
