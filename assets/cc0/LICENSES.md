# Textures rapportées — origine et licence

Ces images ne viennent pas du modèle de 2020. Elles ont été rapportées **là, et
seulement là, où le modèle d'origine n'avait pas de texture** : sept de ses
matériaux étaient des aplats de couleur de 24 × 24 pixels, et `bordeau_uni.jpg`
faisait 1 × 1. Un aplat n'est pas une texture — il ne donne ni relief, ni
variation, ni prise à la lumière.

Les 58 images de 2020 restent intactes dans `legacy/room/textures/` et
continuent d'habiller la salle `--room=legacy`. Les images retirées de la salle
reconstruite sont déclarées une par une, avec leur motif, dans le bloc
`retiredTextures` de `assets/scene/salle.room.json` : `roomgen` refuse de bâtir
si une image cesse d'être employée sans être déclarée.

Seul l'**albédo** est versionné. La normale et la carte ORM (occlusion,
rugosité, métallicité) sont dérivées au build par `tools/texgen`, comme pour les
textures d'origine — ce qui divise par trois le poids ajouté au dépôt, et évite
d'y stocker des données que la chaîne sait recalculer.

## Fichiers

Tous sont sous **CC0 1.0** (domaine public, aucune attribution requise —
elle est donnée ici parce que c'est correct, pas parce que c'est exigé).

| Fichier | Source | Auteur | Licence | Emploi |
|---|---|---|---|---|
| `painted_panel.jpg` | [Poly Haven](https://polyhaven.com/a/painted_plaster_wall) | Rob Tuytel | CC0 1.0 | Caissons de bornes (teintés par jeu), cadres, distributeur, mobilier laqué |
| `lacquered_wood.jpg` | [Poly Haven](https://polyhaven.com/a/lacquered_cherry_wood) | Poly Haven | CC0 1.0 | Jukebox |
| `painted_metal_shutter.jpg` | [Poly Haven](https://polyhaven.com/a/painted_metal_shutter) | Rob Tuytel, Sergej Majboroda | CC0 1.0 | Rails du faux plafond |
| `brushed_concrete.jpg` | [Poly Haven](https://polyhaven.com/a/brushed_concrete) | Rob Tuytel | CC0 1.0 | Béton de structure |
| `metal_plate_02.jpg` | [Poly Haven](https://polyhaven.com/a/metal_plate_02) | Rob Tuytel | CC0 1.0 | Plénum, au-dessus des dalles du faux plafond |
| `black_oak_veneer.jpg` | [Poly Haven](https://polyhaven.com/a/black_oak_veneer) | Poly Haven | CC0 1.0 | Placage bois de la radio murale |

### Pourquoi `painted_panel` a remplacé le volet roulant sur les caissons

`painted_metal_shutter` est, comme son nom le dit, un **volet roulant** : des
nervures horizontales régulières, tous les deux centimètres. Employé sur les
dix-neuf caissons, les deux appareils et le mobilier laqué — quinze matériaux au
total — il rayait toute la salle des mêmes cannelures, et une borne d'arcade s'y
lisait comme une devanture fermée. C'était refaire à moindre échelle le défaut
qu'on venait de corriger : une seule image pour tout.

Un flanc de borne est un panneau de MDF **peint**, lisse, avec la légère
irrégularité d'un rouleau. `painted_plaster_wall` — renommé `painted_panel.jpg`
pour ce qu'il sert ici — donne exactement ça : neutre, à grain fin, et il prend
la teinte de chaque jeu sans imposer de motif. Le volet roulant garde le seul
emploi qui lui convienne vraiment, les rails du faux plafond.

## Modèles

`assets/cc0/models/` porte des **modèles 3D**, instanciés dans la salle par le
type `"model"` de `roomgen`. Même règle que pour les textures : seul l'albédo est
versionné, la normale et l'ORM sont dérivées au build par `texgen`.

| Modèle | Source | Licence | Emploi |
|---|---|---|---|
| `bar_chair_round_01` | [Poly Haven](https://polyhaven.com/a/bar_chair_round_01) | CC0 1.0 | Deux tabourets au comptoir |
| `sofa_03` | [Poly Haven](https://polyhaven.com/a/sofa_03) | CC0 1.0 | Le canapé du coin salon |
| `korean_fire_extinguisher_01` | [Poly Haven](https://polyhaven.com/a/korean_fire_extinguisher_01) | CC0 1.0 | L'extincteur du mur est |
| `ArmChair_01` | [Poly Haven](https://polyhaven.com/a/ArmChair_01) | CC0 1.0 | Le fauteuil, face au canapé |
| `CoffeeTable_01` | [Poly Haven](https://polyhaven.com/a/CoffeeTable_01) | CC0 1.0 | La table basse du salon |
| `potted_plant_01` | [Poly Haven](https://polyhaven.com/a/potted_plant_01) | CC0 1.0 | La plante de l'angle sud-est |
| `boombox` | [Poly Haven](https://polyhaven.com/a/boombox) | CC0 1.0 | Le poste sur le comptoir |
| `metal_trash_can` | [Poly Haven](https://polyhaven.com/a/metal_trash_can) | CC0 1.0 | La poubelle du sas |
| `dartboard` | [Poly Haven](https://polyhaven.com/a/dartboard) | CC0 1.0 | La cible du coin billard |
| `WetFloorSign_01` | [Poly Haven](https://polyhaven.com/a/WetFloorSign_01) | CC0 1.0 | Devant les toilettes |
| `industrial_caged_sconce` | [Poly Haven](https://polyhaven.com/a/industrial_caged_sconce) | CC0 1.0 | L'applique du sas |
| `caged_hanging_light` | [Poly Haven](https://polyhaven.com/a/caged_hanging_light) | CC0 1.0 | Les suspensions du comptoir et du salon |

### Comment ils entrent, et ce qui a été trouvé en le faisant

`assets/blender/import_cc0.py` fait le passage, et ce n'est pas un import direct.
Trois choses s'y règlent, chacune apprise sur pièce :

1. **La décimation.** Ces modèles sont taillés pour le cinéma : la plante en fait
   176 226 triangles, la poubelle 13 960. Le décimateur de Blender les ramène au
   budget déclaré — 1 100 à 4 200 selon l'objet — en mode COLLAPSE, jamais en
   « non planaire » qui garderait les faces plates et détruirait les courbes,
   c'est-à-dire l'inverse de ce qu'on veut sur un fauteuil ou une plante.

   C'est aussi ce qui règle le poste « un décimateur de maillage » que le journal
   réclamait depuis A2b, sans avoir à en écrire un en C.

2. **Les VARIANTES posées côte à côte.** Poly Haven livre souvent plusieurs
   versions d'un objet dans le même fichier — une poubelle propre et une
   rouillée, plusieurs appliques — simplement décalées les unes des autres. Les
   fusionner donne un objet deux fois trop large, et c'est mesuré : la poubelle
   sortait à **1,78 m** de large et l'applique à **1,22 m**, pour des objets qui
   en font 0,4 et 0,25. Le script ne garde donc que l'objet le plus proche de
   l'origine et ce qui TOUCHE sa boîte — un poste de radio a un corps et des
   haut-parleurs, et ceux-là se chevauchent.

3. **L'échelle**, quand elle est fausse, se corrige à l'import et pas dans la
   description de la salle : c'est le modèle qui est à la mauvaise cote, pas son
   emploi.

Et une leçon qui n'est pas du ressort du script : **un budget trop serré casse
un objet fin**. La suspension en cage décimée à 1 100 triangles ne rendait plus
qu'une plaque plate — les barreaux d'une cage sont des tubes minces, et il n'en
restait rien. À 3 200 elle redevient une cage.

### Ce qui n'est pas gardé

Les matériaux du modèle sont **jetés** : la salle décide de la matière, comme
pour tous les autres props. C'est ce qui garde une seule table de matériaux,
donc un seul endroit où régler l'aspect du décor.

Deux textures d'origine ont d'ailleurs été écartées après coup. La table basse
est un meuble peint **turquoise**, et dans une salle tamisée au tungstène c'était
le seul objet froid du champ : il tirait l'œil hors du coin salon au lieu de l'y
garder. La teinter ne suffisait pas — un multiplicateur ne peut pas rendre chaud
ce qui est cyan, il ne fait que l'assombrir — elle porte donc le placage de chêne
déjà dans l'arbre, passé par les UV du modèle.

### Pourquoi si peu, alors que le mécanisme en accepte autant qu'on veut

Ce n'est pas un choix esthétique, c'est une limite de ce que je peux VÉRIFIER
depuis ce conteneur. Le rendu y tourne sur lavapipe, un rasteriseur logiciel.
Au-delà d'environ **160 000 sommets** de scène, il cesse de composer l'image
finale : le G-buffer et la cible HDR restent corrects — mesurés — mais la passe
de tone mapping ne produit plus rien et la capture sort noire, sans une seule
erreur émise. Le seuil n'est même pas monotone : la même scène passe en
720 x 405 et échoue en 640 x 360.

Ces modèles sont taillés pour le cinéma : **14 000 triangles pour un tabouret**,
8 000 pour un canapé. Quatre meubles suffisent à doubler le poids d'une salle
dont toute l'architecture en fait 19 000. Un vrai GPU n'y verrait rien, mais je
ne peux pas l'affirmer sans l'avoir vu, et je refuse de livrer une salle que je
ne peux pas regarder.

Ce qui débloquerait la suite, dans l'ordre : un décimateur de maillage dans
`tools/` (les modèles n'ont pas besoin du dixième de leur définition à deux
mètres), ou une vérification sur une vraie carte graphique.

Résolution rapportée : **1024 × 1024**, l'albédo seul, en JPEG. Le 2K et le 4K
existent en amont ; ils ne servent à rien sur un caisson de 72 cm vu à un mètre,
et ils auraient multiplié par quatre le poids pour un texel qu'aucun écran ne
distingue.

## Pourquoi versionnés plutôt que téléchargés au build

Parce que `git clone && cmake --build` doit marcher **hors ligne** — c'est la
promesse tenue depuis A2b, et un `FetchContent` d'assets la casserait au premier
build. 2,6 Mio ajoutés au dépôt sont le prix de cette promesse, et c'est un prix
raisonnable.
