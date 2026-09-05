# La salle : cahier des charges

Ce document dit ce que la salle **était** en 2020, ce qu'elle **est** aujourd'hui — mesuré, pas
jugé — et ce qu'il faut changer, valeur par valeur, dans `assets/scene/salle.room.json`.

Il ne contient aucune opinion qui ne soit rattachée à un fichier, une ligne ou un nombre. Là où
je suppose, c'est écrit **hypothèse**.

## Comment tout ici a été obtenu

```sh
cmake --preset macos-universal          # configure ; ne compile rien à lui seul
cmake --build --preset macos-universal
./build/macos-universal/bin/nineteen --headless --screenshot=/tmp/x.png --frames=8 \
    --quality=high --width=1280 --height=720 --view=NOM --no-hud
```

Le binaire journalise à chaque capture `luminance : moyenne / médiane / % sous 16 / % au-dessus
de 200`. Les huit vues nommées sont déclarées dans le bloc `captures` de `salle.room.json`.

Les réflectances de texture sont mesurées en **linéaire** — passage sRGB→linéaire par texel, puis
moyenne, puis luminance BT.709. C'est la seule grandeur comparable entre deux images : la moyenne
des octets sRGB ne l'est pas, et ce document montre en §2.6 une décision qui a été prise sur elle
et qui est fausse à cause de ça. La méthode donne les mêmes chiffres que `texgen` : `moquette_c.png`
mesure 0,101 pour `ALBEDO_moquette 0.10` déclaré en `assets/CMakeLists.txt:307`, et `plafond_c.png`
mesure 0,403 pour `ALBEDO_plafond 0.42` en `assets/CMakeLists.txt:306`.

---

# 1. L'ADN de 2020, établi sur pièces

## 1.1 L'échelle : 2,06 unités par mètre, et la preuve

`legacy/room/room.c:90` déclare `HAUTEUR_CAMERA_DEBOUT 3.5F`, et `room.c:91`
`HAUTEUR_CAMERA_ACCROUPI 2.7F`. Divisées par 2,06 : **1,70 m** debout et **1,31 m** accroupi. Ce
sont les deux cotes anatomiques exactes d'un adulte. L'échelle n'est donc pas une convention
retenue faute de mieux : elle est vérifiée par deux mesures indépendantes du même fichier.

Boîte englobante de `legacy/room/textures/salle.obj` : 55,45 × 10,04 × 53,97 unités sur
60 125 sommets, soit 26,9 × 4,9 × 26,2 m. La géométrie déborde largement l'emprise jouable —
murs extérieurs, ciel, volumes hors salle.

## 1.2 Le plan, lu dans les collisions

`detectionEnvironnement()` (`legacy/room/room.c:2090-2215`) est la description la plus fiable du
plan de 2020 : c'est elle qui décide où le joueur peut aller, donc elle épouse les meubles.

| Élément | `room.c` | Unités Blender | En mètres |
|---|---|---|---|
| Emprise totale | 2203-2213 | x −14,0…22,8 ; z −4,5…35,2 | 17,86 × 19,27 |
| Hall seul (hors bloc sanitaire, x < 15) | 2176 | x −14,0…15,0 | **14,08 × 19,27** |
| Îlot de 12 bornes, deux blocs | 2124 | x 1,4…8,0 et −8,0…−1,4 ; z 8,5…13,5 | 2 × (3,20 × 2,43) |
| Double borne mur ouest | 2129 | x ≤ −12,5 ; z 9,0…13,72 | 0,73 × 2,29 |
| Borne centrale isolée | 2134 | x −1,5…1,5 ; z −1,5…1,5 | 1,46 × 1,46 |
| Estrade centrale surélevée | 2062 | x −3,0…2,1 ; z −3,5…3,5, **+0,6** | 2,48 × 3,40, **+29 cm** |
| Billard | 2171 | x −13,5…−5,0 ; z −2,5…2,5 | 4,13 × 2,43 |
| Canapé + table | 2151-2167 | x 2,5…11,0 ; z < 5,6 | ≈ 4,1 × 4,2 |
| Accueil / bar « NINETEEN » | 2099-2103 | x −6…6 ; z 19,5…25,0 | 5,83 × 2,67 |
| Mur en diagonale (l'entrée) | 2108-2120 | x = −0,4737 z + 19,368 | — |
| Bloc sanitaire, deux cabinets | 2176-2197 | x > 15,0 ; z −2,6…12,0 | 3,79 × 7,09 |

Le joueur part de `room.c:1726-1731` : `px −6,56 ; py 3,61 ; pz 9,196 ; angle 0`. Seize bornes
sont déclarées (`room.c:172`, `NB_BORNES 16`), douze jouables, et chacune a sa caméra fixe
(`room.c:1738-1780`) — trois par rangée, à 1,93 unité d'écart, soit **94 cm d'entraxe**.
L'entraxe de la salle reconstruite est de 80 cm (`cabinets[].at`, pas de 0,8 en x).

**Ce que ce plan raconte.** Une salle **profonde** — 19,3 m dans l'axe de marche pour 14,1 m de
large, soit un rapport 1,37 — dans laquelle on entre par un pan coupé, on descend une allée, et
l'îlot de bornes se découvre par la tranche. Le bar est au fond, en vis-à-vis de l'entrée. Le
billard est dans un coin, à l'écart. Les toilettes sont un appendice latéral.

La salle reconstruite a **le même rapport, sur l'autre axe** : `room.playable` vaut
[−9,65 ; 9,65] × [−7,2 ; 7,2], soit 19,3 **de large** pour 14,4 **de profond**. Les deux cotes de
2020 sont là au décimètre près, échangées. Voir §2.10.

## 1.3 D'où venait la lumière : du plafond et du sol, pas des murs

C'est le point le plus important de cette section, et il ne se lit que dans le `.mtl`.

| Matériau 2020 | `salle.mtl` | Objet | `Kd` (aplat) ou texture | `Ke` |
|---|---|---|---|---|
| `Matériau.078` | 1188 | `toit_Cube.033` — **le plafond** | `plafond.jpg` | **1,000** |
| `Material` | 4 | `CENTRE_MACHINE_SOL_Cube` — **le sol de l'estrade** | `moquette.jpg` | **0,230** |
| `fond_mur` | 552 | 8 objets — **les murs du hall** | *(aucune)* Kd (0,0038 ; 0,0031 ; 0,0028) | 0 |
| `haut_mur` | 574 | 9 objets — **le bandeau haut de mur** | `bordeau_uni.jpg`, Kd (0,072 ; 0,004 ; 0,004) | 0 |
| `sol_moquette` | 1254 | `MUR_SAL_Cube.002` — **le sol du hall** | `floor.jpg` | 0 |
| `marbre_poutre.014` | 649 | **13 PILONNE** | `bordeaux.jpg` | 0 |

En clair : **le plafond était une source, l'estrade en était une autre, et les murs étaient
noirs** — 0,0038 de réflectance, soit dix fois moins que du bitume frais. La lumière venait
d'en haut et d'en bas ; les parois disparaissaient.

C'est *exactement* l'inverse de la salle actuelle, où les murs sont la deuxième surface la plus
réfléchissante du décor (§2.4).

## 1.4 Les matières, et ce que « conserver les textures » veut dire

Les 58 images de 2020 sont conservées, et c'est bien ce qui garde la même salle. Mais une
conservation à l'identique du *fichier* n'est pas une conservation de l'*apparence*, parce que le
moteur de 2020 n'éclairait rien : `SDL_RenderCopy` affichait la texture telle quelle. Neuf des
textures les plus employées avaient une réflectance sous 0,035 — elles étaient, à l'écran de
2020, des **aplats noirs**.

| Texture | Réflectance 2020 | Ce qu'on en voyait en 2020 | Ce qu'on en voit après dé-cuisson |
|---|---|---|---|
| `plafond.jpg` | **0,0048** | rien : `Ke` 1,0 l'écrasait en blanc | un lambris art déco **lilas** à filets dorés |
| `moquette_noire.jpg` | 0,0036 | noir | gris moyen |
| `poutre.jpg` | **0,0110** | noir | un **marbre** gris veiné de blanc |
| `bois_noir.jpg` | 0,0199 | noir | gris moyen |
| `gris_foncer.jpg` | 0,0194 | noir | gris moyen |
| `bordeau_uni.jpg` | 0,0189 | noir | bordeaux |
| `carllage_toilette.jpg` | 0,0234 | noir | carrelage brun |
| `bordeaux.jpg` | 0,0295 | presque noir | **un vrai bordeaux chaud** |
| `moquette.jpg` | 0,0325 | presque noir | bordeaux vif |

**Règle qui en découle, et qui vaut pour toute la refonte.** Dé-cuire une texture qui était
invisible en 2020 ne restitue pas l'ADN : cela **invente** une apparence que personne n'a jamais
choisie. Toute texture dont la réflectance de 2020 était sous ≈ 0,03 doit donc être jugée sur ce
qu'elle a l'air d'être **maintenant**, pas sur le fait qu'elle était là. Sa *présence* est
caractéristique ; son *aspect* est accidentel.

Les textures qui n'ont jamais été assombries sont, elles, l'ADN visible de 2020, et elles se
tiennent : `mur_gris.jpg` 0,333, `mur_brique.jpg` 0,245, `desk.jpg` 0,262,
`carllage_mur_toilette.jpg` 0,868, `marbre_toilettes.jpg` 0,808, `cuir_rouge.jpg` 0,698.

## 1.5 Le sol : l'objet le plus caractéristique de la salle, et il a disparu

`legacy/room/textures/salle.mtl:1254-1263` déclare `sol_moquette` avec `map_Kd floor.jpg`.
Ce matériau est employé deux fois dans `salle.obj` (lignes 592 et 222602), dont sur
`MUR_SAL_Cube.002` : **c'est le sol du hall.**

`floor.jpg` n'est pas une moquette unie. C'est **le tapis de salle d'arcade** : un tissage noir
semé de motifs néon — Pac-Man, un fantôme, une planète annelée, des cerises, une manette, des
dés, un flipper « RACE ». 19,7 % de ses pixels sont au-dessus du fond ; 10,3 % sont franchement
néon. C'est la plus grande surface de la salle et l'objet qu'on reconnaît avant tout le reste.

La salle reconstruite le remplace par `moquette.jpg`, une boucle bordeaux **sans aucun motif** :
98,9 % de ses pixels sont au-dessus du fond, c'est-à-dire qu'elle ne porte aucune information.

La capture `--room=legacy --view=allee` le montre en une image, avec deux autres pertes :

- les flancs de borne de 2020 portaient **le logo du jeu en grand** — `flappy.jpg` sur le flanc
  vert, `snake.jpg`, `tetris.jpg`, `asteroid.jpg`, `coming_soon.jpg` ailleurs (matériaux
  `Matériau.080`, `.084`, `.082`, `.092`, `.012`… sur les objets `FLAPPY_BIRD_HARD.*`). Ce sont
  les mêmes fichiers que la salle actuelle emploie, mais réduits au bandeau de marquee : d'une
  surface d'environ 1,6 m² à environ 0,16 m² ;
- une borne **« WORK IN PROGRESS »** en tôle larmée et bandes de danger, sur une estrade rouge.

## 1.6 Caractéristique ou accidentel

| Trait | Verdict | Sur quoi je m'appuie |
|---|---|---|
| Tapis noir à motifs néon au sol | **caractéristique** | `salle.mtl:1254` ; la plus grande surface |
| Moquette bordeaux (`moquette.jpg`) | **caractéristique** mais **secondaire** | employée sur l'estrade seule (`Material`), pas sur le hall |
| Murs qui disparaissent (réflectance très basse) | **caractéristique** dans son *intention*, **accidentel** dans sa *valeur* | `fond_mur` à 0,0038 : intenable en PBR, la leçon est déjà tirée dans le journal |
| Bandeau haut de mur bordeaux | **caractéristique** | `haut_mur`, 9 objets |
| Piliers bordeaux | **caractéristique** | `marbre_poutre.014` sur 13 PILONNE |
| Îlot de 12 bornes en deux blocs dos à dos | **caractéristique** | `room.c:2124` |
| Estrade centrale surélevée, lumineuse | **caractéristique** | `room.c:2062` + `Ke` 0,230 |
| Bar au fond, face à l'entrée | **caractéristique** | `room.c:2099` |
| Comptoir à triangles Memphis | **caractéristique** | `desk.jpg` sur `bar_accueil_Cube.030` |
| Billard dans un coin, à l'écart | **caractéristique** | `room.c:2171` |
| Entrée par un pan coupé | **caractéristique** | `room.c:2108-2120` |
| Salle plus profonde que large (1,37) | **caractéristique** | 19,27 / 14,08 |
| Logo du jeu en grand sur le flanc | **caractéristique** | 15 objets `FLAPPY_BIRD_HARD.*` |
| Plafond blanc uni | **accidentel** | `Ke` 1,0 écrase l'albédo : limite du moteur de 2020 |
| Lambris art déco du plafond | **accidentel** (jamais vu) | 0,0048 de réflectance |
| Marbre des poutres | **accidentel** (jamais vu) | 0,0110 de réflectance |
| Entraxe des bornes à 94 cm | **accidentel** | contrainte de modélisation ; 80 cm est jouable |
| Seize bornes | **accidentel** | `NB_BORNES 16`, dont 4 non jouables ; 19 est mieux |
| Quel axe du monde porte la profondeur | **accidentel** | le rapport compte, pas l'orientation |

---

# 2. Le diagnostic, mesuré

## 2.1 Les huit vues

Capture à `--quality=high --width=1280 --height=720`. Les trois dernières colonnes sont la
médiane de luminance par tiers d'image, mesurée sur le PNG.

| Vue | Moyenne | Médiane | % < 16 | % > 200 | Tiers haut | Tiers milieu | **Tiers bas** |
|---|---|---|---|---|---|---|---|
| `allee` | 73,5 | 53 | 17,9 | 6,2 | 72 | 68 | **27** |
| `bar` | 64,9 | 63 | 24,5 | 1,1 | 84 | 80 | **11** |
| `billard` | 30,7 | **20** | **37,7** | 0,0 | 32 | 31 | **12** |
| `plafond` | 71,1 | 61 | 11,7 | 4,0 | 54 | 67 | 61 |
| `entree` | 44,5 | 34 | 11,1 | 0,3 | 43 | 37 | 27 |
| `classement` | 46,4 | 31 | 29,0 | 1,3 | 42 | 47 | **15** |
| `toilettes` | 91,2 | **99** | 9,3 | 0,4 | 123 | 109 | 71 |
| `borne` | 59,1 | **20** | **40,4** | **9,6** | 31 | 50 | **11** |

## 2.2 « Trop sombre » ne désigne pas la salle : il désigne son tiers bas

Sur sept vues sur huit, le tiers **haut** et le tiers **milieu** sont entre 31 et 123 de médiane —
lisibles. Le tiers **bas** tombe à 11, 12, 15, 27. Sur `bar` l'écart est de 80 à **11**, soit un
facteur sept dans une seule image ; 52,6 % des pixels du tiers bas y sont sous 16.

Le tiers bas d'une image prise à 1,70 m d'œil avec un tangage de −2°, c'est **le sol** — et le
sol devant soi, celui qu'on regarde en marchant.

**Pourquoi c'est un défaut pour un joueur.** Il ne voit pas où il pose les pieds, il ne voit pas
le pied des bornes, il ne voit pas la limite de l'estrade, et surtout : la salle ne lui donne
rien à regarder entre lui et la borne qu'il vise. Un tiers de chaque image est un aplat sombre.

**Conséquence pour la refonte.** Rien de ce qui touche aux luminaires de plafond ne corrigera
ça sans brûler autre chose — la mesure est déjà faite, elle est dans `salle.room.json`
(`panneau_lumineux._essai_rate`) et elle est reprise en §3.5. Ce qui doit changer, c'est **le
sol lui-même**.

## 2.3 La hiérarchie des réflectances est à l'envers de 2020

Réflectance linéaire effective (texture dé-cuite × `baseColor`), par ordre de surface :

| Surface | Matériau | Ce qui est échantillonné | Réflectance | Teinte linéaire R : G : B |
|---|---|---|---|---|
| Plafond, ≈ 360 m² | `plafond` | `plafond_c.png` | **0,403** | 0,454 : 0,369 : **0,587** — violet |
| Murs, ≈ 197 m² | `mur` | `mur_gris.jpg` *(non dé-cuite)* | **0,333** | 0,343 : 0,334 : 0,300 — neutre |
| Sol du hall, ≈ 293 m² | `sol` | `moquette_c.png` | 0,101 | 0,245 : 0,063 : 0,056 |
| Poutres, 5 × 19,3 m | `poutre` | `poutre_c.png` | 0,159 | neutre — marbre veiné |
| Piliers, 10 | `pilier` | `pilonne_rouge_c.png` × (0,70 ; 0,72 ; 0,72) | 0,112 | **0,500 : 0,006 : 0,020** |
| Faïence WC | `mur_toilettes` | `carllage_mur_toilette.jpg` *(non dé-cuite)* | **0,868** | neutre |
| Marbre WC | `marbre` | `marbre_toilettes.jpg` *(non dé-cuite)* | **0,808** | neutre |

En 2020 : plafond source de lumière, sol source de lumière, **murs à 0,0038**. Aujourd'hui :
plafond et murs sont les deux surfaces les plus claires du décor, et le sol est la plus sombre.
La salle est devenue une boîte gris-beige avec un sol rouge, alors qu'elle était une boîte noire
avec un plafond et un sol lumineux.

C'est la formulation la plus courte du reproche « les pièces sont trop sombres **et** les textures
mal choisies » : les deux moitiés du reproche sont la même inversion.

## 2.4 Le plafond est lilas — sur 360 m², sous une lumière tungstène

`plafond_c.png` mesure sRGB (178 162 200), linéaire (0,454 ; 0,369 ; **0,587**). Le bleu est
**1,59 fois** le vert et **1,29 fois** le rouge : c'est un violet franc.

La cause est arithmétique. La source `plafond.jpg` est à sRGB (15 13 17) — quasi noire, avec un
biais bleu léger (rapports 1,15 : 1,00 : 1,31). `texgen --albedo=0.42` recentre la réflectance
en **préservant les rapports de canaux**. Un biais imperceptible à 0,005 devient une teinte
franche à 0,42.

Sous les onze plafonniers, tous chauds (`color` autour de (1,0 ; 0,79 ; 0,54)), une surface
violette rend un gris-mauve mort et les filets dorés du motif virent au rose. Les captures
`plafond`, `allee`, `bar`, `classement` et `entree` le montrent toutes : le tiers haut de la
salle est **froid**, dans un décor dont le mandat dit « couleurs chaudes ».

Vérifié aussi sur `--room=legacy` : la salle de 2020 rendue par le moteur actuel a le même
plafond lilas. La cause est donc bien dans la dé-cuisson, pas dans la reconstruction.

## 2.5 Les poutres sont en marbre, et personne ne l'a décidé

`poutre_c.png` est une photo de **marbre gris veiné de blanc**, à 0,159 de réflectance, sur cinq
poutres de 19,3 m qui barrent le tiers haut de la moitié des cadrages. `assets/CMakeLists.txt:310`
la commente pourtant « acier peint ». L'intention déclarée et l'image ne disent pas la même chose.

En 2020 elle valait 0,0110 : elle était noire, on ne voyait aucune veine. Son aspect actuel n'a
donc jamais été choisi par personne — il est apparu quand on l'a éclaircie d'un facteur 14.

## 2.6 Les piliers ne sont pas ceux de 2020, et la raison invoquée pour changer le sol est fausse

**Les piliers.** `salle.mtl:649` déclare `marbre_poutre.014` avec `map_Kd bordeaux.jpg`, et ce
matériau est employé sur **13 objets `PILONNE`** dans `salle.obj`. Les piliers de 2020 étaient
donc **bordeaux**. Le commentaire du matériau `pilier` de `salle.room.json` affirme que « les
PILONNE du modèle portaient un matériau gris uni » : c'est faux, vérifiable en une ligne de
`grep` sur le `.mtl`.

`bordeaux.jpg` a été mis dans `retiredTextures` pour une raison qui ne concerne que **le panneau
de commande** (« l'aplat rouge du panneau de commande des dix-neuf bornes ; remplacé par la
sérigraphie dessinée `panneau_borne.png` »). Le retrait a emporté l'autre emploi sans que la
ligne le mentionne.

Ce qui l'a remplacé, `pilonne_rouge.jpg`, donne après dé-cuisson et `baseColor` une teinte
linéaire de (0,500 ; **0,006** ; 0,020) : le vert est à **six millièmes** quand le rouge est à
un demi. Rapport R/V = **78**. C'est un rouge de signalisation quasi monochromatique. Sur la vue
`entree`, deux piliers occupent 30 % du cadre et la saturation moyenne du tiers bas monte à
**0,88**, avec un rapport R/B de **8,45**. La vue entière est rouge.

**Le sol.** Le matériau `sol` de `salle.room.json` justifie l'abandon de `floor.jpg` ainsi :
« `floor.jpg` a 30/255 d'albédo moyen contre 47 pour `moquette.jpg` […] à 4 % d'albédo il avale
toute la lumière ». Mesuré en linéaire :

| | Moyenne sRGB | **Réflectance linéaire** |
|---|---|---|
| `floor.jpg` | (33 28 29) → 30 | **0,0410** |
| `moquette.jpg` | (81 32 29) → 47 | **0,0325** |

**Le tapis néon réfléchit 26 % de plus que la moquette bordeaux.** La comparaison des moyennes
d'octets sRGB s'est laissé dominer par le canal rouge de `moquette.jpg` (81 contre 33), qui pèse
un tiers de la moyenne mais seulement 21 % de la luminance. C'est exactement l'erreur d'inégalité
de Jensen que le journal documente déjà pour `texgen` (`--albedo=0.55` rendant 0,39) — commise
ici dans l'autre sens, sur la décision de décor la plus lourde du projet.

Et après dé-cuisson, l'argument disparaît complètement :

| | Réflectance après `texgen` |
|---|---|
| `floor_c.png` (`ALBEDO_floor 0.11`, `assets/CMakeLists.txt:309`) | **0,1025** |
| `moquette_c.png` (`ALBEDO_moquette 0.10`, ligne 307) | **0,1014** |

Un écart de **1 %**. Le tapis néon ne coûte rien en lumière. La preuve est déjà dans le jeu : le
matériau `sol_technique` emploie `floor.jpg` dé-cuit sur le prop `tapis_technique`, et une capture
à `--pos=-8.2,1.7,3.6 --yaw=270 --pitch=-34` le montre rendu par le moteur actuel, sous
l'éclairage actuel, avec ses motifs parfaitement lisibles.

## 2.7 Une moulure de 6,09 × 4,44 m posée sur la moquette, autour de rien

C'est le défaut de modèle le plus visible de la salle.

- `mouldings[nez_estrade]` trace un rectangle fermé de **(−3,043 ; −1,333) à (3,043 ; 3,111)** à
  `y = 0,09`, en matériau `bois`.
- `boxes[estrade]` est à `at [0 ; 0 ; −3,1]`, `size [2,4 ; 0,12 ; 2,4]`, soit **x −1,2…1,2 ;
  z −4,3…−1,9**.

Les deux emprises **ne se recoupent pas du tout**. Le nez de marche court donc à 9 cm du sol au
milieu du hall, en travers de l'îlot de bornes, autour d'aucune marche. Une capture à
`--pos=0,1.7,5.5 --yaw=270 --pitch=-22` le montre comme une latte de bois traversant tout le
cadre. Il se lit exactement comme ce qu'il est : une planche oubliée par terre.

Symétriquement, **l'estrade réelle** — celle qui porte la borne de classement, `borne_classement`
étant à `y = 0,12`, la hauteur exacte de l'estrade — n'a **aucun** nez de marche : son arête est
un chanfrein de 2 cm en `sol_sombre` à 0,057 de réflectance, invisible.

**Hypothèse** sur la cause : l'emprise du nez (x ±3,04) colle à celle de l'îlot de bornes
(x −3,16…3,16) décalée de +0,89 m en z. Une valeur qui n'a pas suivi un déplacement.

## 2.8 Les toilettes sont la pièce la mieux éclairée d'une salle d'arcade

`toilettes` mesure une médiane de **99**, contre 20 pour `billard`, 20 pour `borne`, 31 pour
`classement`. Le bloc sanitaire est **cinq fois** plus lumineux que l'endroit où l'on joue, et
sa saturation moyenne est de 0,03 — un gris parfait.

La cause est mécanique : `carllage_mur_toilette.jpg` (0,868) et `marbre_toilettes.jpg` (0,808)
ne passent pas par `texgen --albedo=` — elles n'y ont pas leur place, ce sont d'authentiques
couleurs de base. Un seul luminaire, `plafonnier_toilettes` à `intensity 52` et `range 4,4`,
éclaire une pièce de 3,35 × 3,80 m tapissée à 87 % de réflectance.

Le journal se sert de cette pièce comme **preuve par l'exception** de l'ancien défaut (« les
toilettes étaient la seule pièce lisible de la salle »). Elle l'est restée. C'est aujourd'hui le
dernier résidu du problème, et il est retourné : ce n'est plus le hall qui est trop sombre par
rapport aux WC, c'est le WC qui est trop clair par rapport au hall.

## 2.9 Ce qui, dans la salle, ne dit pas au joueur de jouer

- **Huit marquees sur dix-neuf affichent « votre publicité ici ? contactez-nous »** :
  `marquee_pub` sur les bornes 3, 6, 7, 8, 13, 14, 15, 18 (shooter ×2, démineur ×2, pacman ×2,
  piano ×2). Un marquee sert à vendre son jeu de l'autre bout de la salle ; huit d'entre eux
  vendent un espace publicitaire vide. Visible en pleine page sur la capture
  `--pos=0,1.7,5.5 --yaw=270 --pitch=-22`.
- **Quatre écrans d'attente disent que le jeu n'est pas prêt** : `ecran_bientot`
  (`coming_soon.jpg`) sur les bornes 7 et 18, `ecran_chargement` (`chargement.png`, 3 Ko) sur les
  bornes 8 et 15. Or Pac-Man et Piano sont **portés et jouables** — le journal l'écrit :
  « Les dix-neuf bornes de la salle jouent toutes. Plus une seule ne dit “pas encore porté” ».
  Quatre d'entre elles le disent encore, par leur image.
- **Les dix-neuf flancs portent la même planche.** `flanc_borne.png` est une trame de losanges en
  niveaux de gris, teintée par `baseColor`. La différence entre deux bornes tient donc à une
  teinte. En 2020 le flanc portait **le logo du jeu, en grand** (§1.5).
- **Le bar est vide.** `props[bar_accueil]` a quatre pièces : le comptoir, le plateau, une plinthe
  de pied, et un panneau `classement_fond` à `y = 1,601`. Aucune bouteille, aucun verre, aucune
  tireuse, aucune étagère, aucune caisse. Le panneau `classement_fond`
  (`background_classement.png`, 0,129 de réflectance) se lit sur la capture `bar` comme un
  **rectangle noir** dans le mur, sous l'enseigne.
- **Le billard a perdu ses accessoires.** `salle.mtl` déclare sur `Billiard_Table` un matériau
  `canne` (ligne 487, `bois.jpg`), un `bleu.001` (ligne 477, Kd 0,014 : 0,030 : 0,800 — la craie)
  et un `de_qui_billard` (ligne 509, `Ke` 1,0 : 0,485 : 0,082 — les boules de marque, émissives).
  `props[billard]` en a seize pièces et **aucune** des trois. Une queue posée en travers d'une
  table est ce qui distingue « une partie interrompue » de « un meuble ».
- **Les écrans brûlent.** `borne` mesure **9,6 % de pixels au-dessus de 200**, avec un tiers haut
  à 16,5 %. Les onze matériaux `ecran_*` sont à `emissiveStrength 1.7` avec `emissive [1,1,1]`.
  À 50 cm, la dalle sature et le halo la déborde. Le même cadrage a 68,8 % de pixels sous 16
  dans son tiers bas : **la vue de jeu est la plus contrastée de la salle, et dans le mauvais
  sens** — brûlée en haut, bouchée en bas.

## 2.10 Incohérences de données à trancher

Ce ne sont pas des défauts visibles, mais des chiffres qui se contredisent et qui rendront la
prochaine correction fausse si on ne tranche pas.

1. `room._plan` écrit que la salle « fait 22 × 16 sous 3,10 ». `room.playable` vaut
   **19,3 × 14,4**, et `room.height` vaut **2,92**. Aucune des trois cotes annoncées n'est celle
   des données. Les cotes réelles sont exactement celles de 2020 (§1.2), ce qui est mieux que ce
   que le texte annonce — c'est le texte qu'il faut corriger.
2. `room.height` (2,92) pilote la hauteur des murs (`tools/roomgen.c:2775` puis `:2822`), alors
   que `ceilings[0].y` et `mouldings[corniche_hall].y` valent **3,10** — le défaut de roomgen
   étant d'ailleurs 3,10. Il y a 18 cm entre le haut du mur généré et le plan du plafond. Sur les
   captures la corniche (section 9 × 10 cm) et le rail masquent la jonction et **je ne vois pas de
   trou** ; je ne signale donc pas un défaut visible, mais une incohérence à résoudre avant qu'un
   changement de corniche ne la révèle.
3. La profondeur et la largeur du hall sont **échangées** par rapport à 2020 (§1.2). Le rapport
   1,34 est conservé, la circulation ne l'est pas : en 2020 on entrait dans une salle *profonde*
   et l'allée fuyait devant soi. C'est ce que la vue `allee` cherche encore à donner. À trancher
   explicitement plutôt qu'à subir.

---

# 3. La refonte

Toutes les valeurs ci-dessous s'appliquent à `assets/scene/salle.room.json`, sauf mention
contraire. `baseColor` est un **multiplicateur linéaire** de la texture : `gbuffer.frag:132`
écrit `vec4 albedo = texture(u_albedo, uv) * u_baseColor;`.

## 3.1 Le sol — le poste qui rend le plus

### Décision : remettre `floor.jpg`, dé-cuit, avec un émissif porté par son propre albédo

**Je tranche pour le tapis néon d'origine, sans nouvel outil**, contre les deux autres options.
Argumentaire :

| Option | Coût | Ce qu'on gagne | Ce qu'on perd |
|---|---|---|---|
| **A. Remettre `floor.jpg` dé-cuit** | **3 lignes de JSON**, rien d'autre : la texture, sa cible de dé-cuisson (`assets/CMakeLists.txt:309`) et son rendu (`sol_technique`) existent déjà | l'ADN, et 10,3 % de surface néon là où l'image est la plus sombre | rien de mesurable : 0,1025 contre 0,1014 |
| **B. Le redessiner (`tools/floorart.c`)** | un outil neuf — `sideart.c` fait 132 lignes, `panelart.c` 172 — plus une règle CMake, une cible de dé-cuisson et une planche à dessiner | la densité de motifs et la période réglables, un pavage moins reconnaissable | 3 à 4 jours, et l'on redessine une image qui existe et qui est bonne |
| **C. Garder le bordeaux** | 0 | rien | l'objet le plus reconnaissable du lieu |

**C est à écarter** : la seule raison invoquée pour l'avoir choisi est arithmétiquement fausse
(§2.6), et il ne reste alors aucun argument en sa faveur.

**B est à écarter pour maintenant, pas pour toujours.** Son seul avantage réel sur A est la
période de pavage, et cette période se règle déjà par `uvMetres` sans écrire une ligne de C. Le
dépôt dessine ses planches quand **aucune source n'existe** — c'est le cas du panneau de commande
(`bordeaux.jpg` était un aplat) et du flanc (rien n'existait). Ici la source existe, elle est
bonne, et elle est l'ADN. Écrire un outil pour la remplacer serait l'inverse de la règle du dépôt.
B redevient le bon choix le jour où l'on voudra **quatre planches différentes** pour casser la
périodicité sur 293 m² ; c'est un poste de §5, pas de maintenant.

**A, avec deux réglages qui ne sont pas dans l'original :**

```jsonc
{ "name": "sol", "texture": "floor.jpg", "roughness": 0.94, "metallic": 0.0,
  "uvMetres": 2.4,
  "emissive": [1.0, 0.92, 0.84], "emissiveStrength": 0.25,
  "footstep": "moquette" }
```

| Clé | Actuel | Proposé | Pourquoi |
|---|---|---|---|
| `texture` | `moquette.jpg` | **`floor.jpg`** | §1.5 et §2.6. Réflectance dé-cuite 0,1025 contre 0,1014 : coût nul |
| `uvMetres` | 1,5 | **2,4** | à 1,5 le hall (18,1 × 16,2 m) montre **130** pavages et le flipper « RACE » se reconnaît en grille. À 2,4 il en montre **51**, et l'entraxe des motifs passe de 50 à ≈ **80 cm** — huit motifs par dalle de 2,4 m — qui est celui d'un vrai tapis d'arcade |
| `emissive` | absent | **[1,0 ; 0,92 ; 0,84]** | voir ci-dessous |
| `emissiveStrength` | absent | **0,25** (à mesurer, voir critère) | voir ci-dessous |

**L'émissif du sol n'est ni une licence ni un bricolage.** Trois raisons, dans cet ordre :

1. **C'est l'ADN.** `salle.mtl:4` déclare le matériau `Material`, employé sur
   `CENTRE_MACHINE_SOL_Cube`, avec `Ke 0.229790` sur `moquette.jpg`. L'auteur de 2020 a lui-même
   fait luire le sol de l'estrade. La valeur proposée, 0,25, est la sienne à 8 % près.
2. **Le shader le masque tout seul.** `engine/shaders/gbuffer.frag:207` écrit
   `vec3 emissive = u_emissive.rgb * u_emissive.a * albedo.rgb;` — l'émissif est multiplié par
   **l'albédo, par texel**. Le tissage noir de `floor_c.png` (64 % des texels sous le seuil de
   fond) ne luira donc quasiment pas, et les motifs néon (35,8 % au-dessus, dont 8,7 % francs)
   porteront tout. **Aucun masque, aucune texture supplémentaire, aucun outil ne sont
   nécessaires** — le comportement voulu tombe de la convention glTF que le shader suit déjà.
3. **C'est le seul levier qui attaque le tiers bas sans toucher aux plafonniers**, c'est-à-dire
   sans refaire l'essai raté n° 1 (§3.5). Un vrai tapis d'arcade est d'ailleurs lu sous UV : le
   motif *est* ce qui luit, et le tissage ne luit pas.

**Critère de réussite, à mesurer et pas à supposer :** médiane du tiers bas de `bar`, `billard`,
`classement` et `borne` **≥ 30** (elle vaut 11, 12, 15, 11), sans que `% > 200` ne monte de plus
d'un demi-point sur aucune des huit vues. Si 0,25 n'y suffit pas, monter par pas de 0,05 jusqu'à
0,40 ; au-delà, le sol devient une source et ce n'est plus un tapis.

## 3.2 Le plafond, les murs, les poutres, les piliers

| Matériau | Clé | Valeur actuelle | **Valeur proposée** | Réflectance avant → après | Pourquoi |
|---|---|---|---|---|---|
| `plafond` | `baseColor` | *(absent = 1,1,1)* | **`[0.84, 0.78, 0.31, 1.0]`** | 0,403 → **0,300** | §2.4. Linéaire (0,454 ; 0,369 ; 0,587) → (0,382 ; 0,288 ; 0,182). Le rapport B/R passe de 1,29 à **0,48** : le lilas devient un bronze chaud, les filets redeviennent dorés. On perd 25 % de niveau sur la plus grande surface du décor — c'est voulu, un plafond de salle tamisée ne doit pas être ce qu'on regarde |
| `mur` | `baseColor` | *(absent)* | **`[0.52, 0.44, 0.34, 1.0]`** | 0,333 → **0,150** | §2.3. Linéaire (0,343 ; 0,334 ; 0,300) → (0,178 ; 0,147 ; 0,102), R/B de 1,14 à **1,75**. Le mur cesse d'être la deuxième source de la salle et devient un taupe chaud. **Ce n'est pas le retour au `fond_mur` à 0,0038 de 2020** : 0,150 reste quarante fois plus clair, et au-dessus du bitume frais |
| `poutre` | `texture` | `poutre.jpg` | **`painted_metal_shutter.jpg`** | 0,159 → à mesurer | §2.5. Le marbre n'a jamais été vu en 2020 (0,011), son aspect est accidentel. La texture proposée est déjà dans l'arbre (employée par `rail`), donc sans nouvel asset |
| `poutre` | `baseColor` | *(absent)* | **`[0.60, 0.50, 0.36, 1.0]`** | → ≈ **0,09** | Acier peint brun sombre, chaud. Cinq poutres de 19,3 m qui barrent le tiers haut doivent tenir la structure, pas la regarder |
| `pilier` | `texture` | `pilonne_rouge.jpg` | **`bordeaux.jpg`** | — | §2.6 : c'est la texture de 2020, sur 13 `PILONNE`. Retirer la ligne `bordeaux.jpg` de `retiredTextures` : son motif de retrait ne visait que le panneau de commande |
| `pilier` | `baseColor` | `[0.70, 0.72, 0.72, 1.0]` | **à retirer** | 0,112 → **0,102** | Linéaire (0,500 ; 0,006 ; 0,020) → (0,277 ; 0,053 ; 0,061). Rapport R/V de **78 à 5,2** : un vrai bordeaux et non un rouge de signalisation. Niveau quasi inchangé (−9 %) |
| `pilier` | `uvMetres` | 1,2 | **1,2** *(inchangé)* | — | `bordeaux.jpg` est carrée (1200²), le pavage tient |

**Le bandeau haut de mur, à rétablir.** `haut_mur` (`salle.mtl:574`, 9 objets) posait un bandeau
bordeaux au-dessus de la cimaise. Il n'existe plus. Il est utile pour deux raisons : c'est l'ADN,
et il casse horizontalement un mur de 19,3 m qui se lit aujourd'hui comme un aplat. À ajouter en
`walls` ou en `mouldings` entre `y = 2,42` (la cimaise existante) et `y = 3,10`, matériau
`bordeau_uni` — déjà déclaré, `lacquered_wood.jpg` tinté (0,40 ; 0,07 ; 0,12).

## 3.3 Les autres matériaux

| Matériau | Clé | Actuel | **Proposé** | Pourquoi |
|---|---|---|---|---|
| `comptoir` | `uvMetres` | 2,6 | **0,9** | `desk.jpg` fait 490 px pour ≈ 8 cellules de motif, soit 61 px la cellule. À 2,6 m par répétition, un triangle fait **32 cm** — d'où l'effet nappe de pique-nique de la vue `bar`. À 0,9 il fait **11 cm**, la cote d'un vrai stratifié Memphis. Le motif est l'ADN (§1.6) ; c'est son échelle qui est fausse, pas sa présence |
| `mur_toilettes` | — | *(rien)* | *(rien)* | La faïence à 0,868 est **physiquement juste**. C'est la lumière qu'il faut corriger, pas la matière — voir §3.4 |
| `marbre` | — | *(rien)* | *(rien)* | Idem, 0,808 |
| `ecran_*` (11 matériaux) | `emissiveStrength` | 1,70 | **1,35** | §2.9. `borne` sature 9,6 % de ses pixels, 16,5 % dans son tiers haut. 1,35 est la valeur déjà retenue pour `panneau_lumineux`, qui est un diffuseur du même ordre. À mesurer : viser `% > 200` **≤ 5** sur `borne` sans que la médiane du tiers milieu descende sous 45 |
| `classement_fond` | — | panneau du bar | **à remplacer** | §2.9 : à 0,129 de réflectance sur 1,8 × 0,9 m, il fait un trou noir dans le mur du bar. Soit lui donner `emissive [1.0, 0.9, 0.7]` / `emissiveStrength 1.2` — c'est un tableau de scores, il doit être rétroéclairé — soit le retirer et mettre des étagères (§4.1) |

## 3.4 Les lumières

Une seule modification, et elle ne ressemble qu'en apparence à l'essai raté n° 1.

| Lumière | Clé | Actuel | **Proposé** | Pourquoi |
|---|---|---|---|---|
| `plafonnier_toilettes` | `intensity` | 52,0 | **30,0** | §2.8 |
| `plafonnier_toilettes` | `range` | 4,4 | **3,6** | la pièce fait 3,35 × 3,80 m |

**Pourquoi ce n'est pas l'essai raté n° 1.** Celui-ci baissait de 38 % les **onze plafonniers du
hall**, qui éclairent des surfaces à 0,10 de réflectance ; les médianes s'effondraient parce que
ces surfaces n'ont aucune réserve. Ici on baisse **un** luminaire qui éclaire des surfaces à
**0,87**. Même à moitié, le rapport reste : 0,87 × 0,5 = 0,435, soit **4,35 fois** ce que rend le
sol du hall à pleine lumière. Les WC resteront la pièce la plus claire de la salle — ce qui est
juste, c'est un local carrelé au néon — mais ils cesseront d'être cinq fois plus clairs que
l'endroit où l'on joue.

**Critère :** médiane de `toilettes` entre **55 et 70** (elle vaut 99), et aucune autre vue
modifiée de plus d'un point.

**Ce qui ne change pas, et pourquoi.** Les seize déclarations de `lights` sont bien posées : la
température est chaude partout (`color` autour de (1,0 ; 0,79 ; 0,54)) sauf le tube fluo vert des
WC, qui est un repoussoir assumé ; les deux néons d'accent (bleu à l'ouest, magenta à l'est) sont
à 40 d'intensité, c'est-à-dire des accents et non des sources ; les trois plafonniers ajoutés pour
les trous de couverture ont fait passer la zone non éclairée du sol de 8 % à 0,2 %. **Le plan
d'éclairage n'est pas le problème de cette salle.** Le problème est ce que la lumière frappe.

## 3.5 Ce qu'il ne faut pas refaire

Deux essais sont documentés comme ratés dans `salle.room.json`. Aucune proposition de ce
document ne les reprend, et voici pourquoi, explicitement :

1. **Baisser les luminaires de plafond du hall** (essai à −38 %). Résultat mesuré : saturation
   6,9 → 6,7 %, mais médiane de l'allée 80 → 58 et classement à 50 % de pixels sous 16. *Ici :*
   aucune intensité de plafonnier de hall n'est touchée. La seule baisse porte sur les WC, sur un
   substrat sept fois plus réfléchissant (§3.4).
2. **Désaturer et assombrir les caissons de borne** — un albédo neutre de valeur moyenne vire au
   pastel crayeux. *Ici :* les neuf `caisson_*` ne sont pas touchés. Les `baseColor` proposés en
   §3.2 sont des multiplicateurs **non neutres** appliqués à des textures : un multiplicateur
   uniforme conserve les rapports de canaux et ne peut pas produire de pastel, puisque le pastel
   consiste à **remonter** le canal le plus faible. Ceux du plafond, du mur et des poutres
   *baissent* le bleu davantage que le rouge : ils saturent en chaud plutôt qu'ils ne désaturent.

## 3.6 Géométrie et placement

| Objet | Défaut | Correction |
|---|---|---|
| `mouldings[nez_estrade]` | trace 6,09 × 4,44 m autour de rien (§2.7) | `points` → `[[-1.2,-4.3],[1.2,-4.3],[1.2,-1.9],[-1.2,-1.9]]`, ce qui est l'emprise exacte de `boxes[estrade]`. `y` → `0.09` reste juste (estrade à 0,12) |
| `boxes[estrade]` | 2,4 × 2,4 à +12 cm ; 2020 avait 2,48 × 3,40 à **+29 cm** (`room.c:2062`) | `size` → `[2.5, 0.29, 3.4]`, `at` → `[0, 0, -3.1]`. Vérifier ensuite `borne_classement.at[1]` → `0.29`, et que `room.playable` laisse passer un joueur : la hauteur de marche du moteur monte les 29 cm en deux fois |
| `props[billard]` | ni queue, ni craie, ni boules de marque, alors que 2020 les déclarait (§2.9) | ajouter trois pièces : un `cylinder` `radius 0.014 height 1.45` en `bois` posé en travers de la table, un `box` `[0.04,0.025,0.04]` en bleu craie sur la bande, et deux petites sphères `billard_bille` sur le bord |
| `props[bar_accueil]` | quatre pièces, aucun contenu | §4.1 |
| `cabinets[3,6,7,8,13,14,15,18]` | `marquee_pub` — « votre publicité ici ? » | §4.1 |
| `cabinets[7,18]` / `[8,15]` | `ecran_bientot` / `ecran_chargement` sur des jeux jouables | §4.1 |
| `room._plan`, `room.height` | trois cotes annoncées, aucune conforme aux données (§2.10) | corriger le texte sur les données : 19,3 × 14,4 sous 2,92. Et aligner `ceilings[0].y` et `corniche_hall.y` sur `room.height`, ou l'inverse — mais une seule fois |

---

# 4. Ce qui manque pour avoir envie d'y rester

Le mandat demande une salle où l'on reste et où l'on revient affronter ses amis. Ce que la salle
ne fait pas encore, classé par ce que ça change.

## 4.1 Ce qui change tout

**1. Dix-neuf bornes doivent vendre dix-neuf jeux.** Huit marquees vendent un espace publicitaire
et quatre écrans annoncent un jeu pas prêt (§2.9). C'est le défaut le plus coûteux du décor : un
joueur qui traverse la salle lit les marquees, et huit fois sur dix-neuf on lui répond
« contactez-nous ». Quatre jeux — Pac-Man, Piano, Shooter, Démineur — n'ont ni marquee ni écran
d'attente dans le jeu de 58 images de 2020.

*Correction :* un `tools/marqueeart` sur le modèle de `sideart.c` (132 lignes) et
`panelart.c` (172 lignes) : quatre planches en niveaux de gris, teintées par le matériau comme le
flanc et le panneau le sont déjà. Ce sont quatre logos typographiques, pas quatre illustrations.
Même outil, même passe, pour les quatre écrans d'attente.

**2. Le flanc doit dire quel jeu c'est.** En 2020, l'oiseau de Flappy Bird faisait toute la
hauteur du flanc vert (§1.5). Aujourd'hui les dix-neuf flancs sont la même trame de losanges à
une teinte près. On ne repère plus « sa » borne de loin ; on lit une couleur.

*Correction :* `sideart` reçoit un argument `--logo=` qui compose l'image du jeu dans le tiers
haut de la planche. **Pour quatre jeux sur huit — flappy, snake, tetris, asteroid — l'image
existe déjà** (`flappy.jpg`, `snake.jpg`, `tetris.jpg`, `asteroid.jpg`, employées comme marquees).
Les quatre autres viennent de la même passe que le point 1.

**3. Le bar doit être un bar.** Quatre boîtes et un rectangle noir (§2.9). C'est le fond du champ
sur toute la moitié nord de la salle et le seul endroit où l'on ne joue pas — donc le seul endroit
où l'on regarde.

*Correction, en props, sans nouvel asset :* une étagère à trois tablettes contre le mur, huit à
douze bouteilles (`cylinder` + `geo_revolve`, la primitive existe depuis B14), quatre verres
retournés, une caisse enregistreuse, une machine à café, un présentoir à jetons. Et remplacer ou
rétroéclairer le panneau `classement_fond` (§3.3) : un tableau des scores derrière un bar est
exactement ce qui fait revenir.

**4. Le sol.** Traité en §3.1. Il est ici aussi parce que c'est ce qu'on voit en marchant, et que
marcher est ce qu'on fait entre deux parties.

## 4.2 Ce qui aide

- **De la signalétique.** La salle n'a aucun panneau écrit sauf « EXIT » et « RESTROOMS ». Il
  manque : un tarif au mur, une flèche « TOILETTES », un « HIGH SCORE OF THE WEEK » au-dessus de
  la borne de classement, un règlement affiché, un « HORS SERVICE » scotché sur une borne. Ce
  sont des panneaux plats, donc quelques `props` de type `panel` et une planche dessinée.
- **Du désordre vécu.** La salle est parfaitement rangée, et c'est ce qui la fait lire comme un
  plan d'architecte. Manquent : un gobelet écrasé près d'une poubelle, des jetons par terre,
  une chaise de travers, un blouson sur un tabouret, un carton plié derrière le bar, du ruban
  adhésif sur un flanc de borne. Le journal a déjà fait ce raisonnement une fois — « trois billes
  posées, une partie interrompue, pas un meuble ». Il faut le faire dix fois de plus.
- **Une deuxième borne de classement**, ou un écran mural près du bar. La salle a dix-neuf bornes
  et **un seul** endroit qui dit qui gagne. Le mandat parle d'affronter ses amis : le score doit
  être lisible sans traverser la salle.
- **Deux ou trois points de vue de plus dans `captures`.** Il n'y a aujourd'hui aucune vue depuis
  l'estrade vers l'entrée, aucune vue du bar vers l'îlot, aucune vue à hauteur de joueur devant
  une borne du mur ouest. Une vue non capturée est une vue non contrôlée — le journal documente
  déjà deux caméras qui se sont retrouvées dans un meuble.

## 4.3 Confort

- Une horloge murale : elle donne l'heure et elle dit que le lieu est ouvert.
- Un ventilateur de plafond, ou une bouche d'aération : les 999 dalles du plafond n'ont qu'une
  seule dalle manquante et aucun équipement.
- Des traces d'usage au sol devant les bornes les plus jouées — un assombrissement local du tapis,
  qui se fait par un `props` de type panneau posé à 5 mm.
- Un tabouret devant une des bornes : rien ne dit qu'on peut rester.

---

# 5. Ordre de bataille

Classé par rendement décroissant. « Effort » suppose que l'on connaît déjà le fichier.

| # | Poste | Effort | Critère de réussite, mesuré |
|---|---|---|---|
| 1 | **Le tapis néon** — `sol.texture` → `floor.jpg`, `uvMetres` 2,4, `emissive` [1,0 ; 0,92 ; 0,84] à 0,25 (§3.1) | **3 lignes**, aucun nouvel asset | Médiane du tiers bas ≥ **30** sur `bar`, `billard`, `classement`, `borne` (11, 12, 15, 11 aujourd'hui). `% > 200` ne monte de plus de 0,5 point sur aucune des huit vues |
| 2 | **Le plafond cesse d'être lilas** — `plafond.baseColor` [0,84 ; 0,78 ; 0,31] (§3.2) | **1 ligne** | Rapport B/R du tiers haut de `plafond` sous **0,60** (1,29 aujourd'hui). Médiane de `plafond` ≥ **50** (61 aujourd'hui) |
| 3 | **Le nez de marche fantôme** — `nez_estrade.points` sur l'emprise de l'estrade (§2.7, §3.6) | **1 ligne** | La latte n'apparaît plus sur `--pos=0,1.7,5.5 --yaw=270 --pitch=-22` ; l'arête de l'estrade est visible sur `classement` |
| 4 | **Les piliers redeviennent bordeaux** — `pilier.texture` → `bordeaux.jpg`, retirer `baseColor` et la ligne de `retiredTextures` (§3.2) | **3 lignes** | Saturation moyenne du tiers bas de `entree` sous **0,60** (0,88 aujourd'hui) ; R/B sous **3,0** (8,45 aujourd'hui). Médiane de `entree` inchangée à ±3 |
| 5 | **Les murs passent en taupe chaud** — `mur.baseColor` [0,52 ; 0,44 ; 0,34] (§3.2) | **1 ligne** | R/B du tiers milieu ≥ **1,7** sur `bar` et `classement` (1,91 et 1,63). Aucune vue sous **25** de médiane globale. Si `entree` ou `billard` passe sous 25, remonter par pas de 0,04 |
| 6 | **Les écrans cessent de brûler** — `ecran_*.emissiveStrength` 1,70 → 1,35 (§3.3) | **11 lignes** | `% > 200` de `borne` ≤ **5** (9,6 aujourd'hui), médiane du tiers milieu ≥ **45** (50 aujourd'hui) |
| 7 | **Les toilettes rentrent dans le rang** — `plafonnier_toilettes` 52 → 30, portée 4,4 → 3,6 (§3.4) | **2 lignes** | Médiane de `toilettes` entre **55 et 70** (99). Aucune autre vue bougée de plus d'un point |
| 8 | **Les poutres cessent d'être en marbre** — texture `painted_metal_shutter.jpg`, `baseColor` [0,60 ; 0,50 ; 0,36] (§3.2) | **2 lignes** | Aucune veine blanche identifiable sur `allee` et `classement` en vue à 100 % ; médiane du tiers haut ≥ **40** (72 et 42) |
| 9 | **Le comptoir à la bonne échelle** — `comptoir.uvMetres` 2,6 → 0,9 (§3.3) | **1 ligne** | Le triangle mesure 10 à 13 cm au sol sur `bar` ; saturation du tiers bas de `bar` sous **0,60** (0,73) |
| 10 | **Le bandeau haut de mur bordeaux** (§3.2) | ≈ **10 lignes** | Sur `bar` et `classement`, une rupture horizontale nette au-dessus de la cimaise ; médiane globale à ±2 |
| 11 | **L'estrade retrouve ses 29 cm** (§3.6) | ≈ **5 lignes** + contrôle de franchissement | Le joueur monte l'estrade ; `roomgen` ne refuse aucune caméra |
| 12 | **Quatre marquees et quatre écrans d'attente** — `tools/marqueeart` (§4.1) | ≈ **200 lignes** de C + règle CMake + 8 planches | Zéro `marquee_pub` sur une borne jouable ; zéro `ecran_bientot` / `ecran_chargement` sur les 19 |
| 13 | **Le logo du jeu sur le flanc** — `sideart --logo=` (§4.1) | ≈ **50 lignes** + CMake ; 4 images sur 8 existent déjà | Deux bornes de jeux différents se distinguent sur une capture à 6 m, en niveaux de gris |
| 14 | **Le bar habité** — étagère, bouteilles, verres, caisse (§4.1) | ≈ **80 lignes** de JSON | Plus aucun rectangle noir sur `bar` ; `% < 16` du tiers bas sous **35** (52,6) |
| 15 | **Le billard vécu** — queue, craie, boules de marque (§3.6) | ≈ **25 lignes** | Les trois objets de `salle.mtl` (`canne`, `bleu.001`, `de_qui_billard`) ont un équivalent |
| 16 | **Signalétique et désordre** (§4.2) | ≈ **150 lignes** de JSON + 3 planches | Au moins six objets non alignés sur une trame et quatre panneaux écrits |
| 17 | **Trancher les incohérences de données** (§2.10) | **texte** | `_plan` dit ce que disent les données ; une seule hauteur dans le fichier |
| 18 | **`tools/floorart` — quatre planches de tapis** (§3.1, option B) | ≈ **250 lignes** | Aucune répétition identifiable à moins de 8 m sur `allee` |

Les postes **1 à 9 tiennent en 25 lignes de JSON et aucun code**, et ils portent l'essentiel des
deux reproches. Les mesurer un par un, dans cet ordre : ce sont des changements qui interagissent
par l'adaptation d'exposition, et deux corrections appliquées ensemble ne se départagent plus.
