# Nineteen — origine et licence de ce qui n'est pas de nous

Ce fichier est livré **à la racine du paquet**. Il couvre quatre choses, et
c'est dans cet ordre qu'il faut le lire si l'on cherche à savoir ce qu'on a le
droit de faire :

1. les **textures et modèles rapportés** (Poly Haven, CC0) — ci-dessous ;
2. le **personnage** (CesiumMan, CC BY 4.0), la seule attribution qui soit une
   obligation et non une politesse — section « Le personnage » ;
3. le **logiciel tiers** lié dans le binaire — section « Le logiciel tiers » ;
4. les **images de 2020**, dont la provenance n'est pas établie, et dont huit
   sont identifiées comme appartenant à des tiers — section « Les images de
   2020 : ce qui n'est PAS établi ». **C'est la section à lire avant de
   vendre.**

## Les textures rapportées

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

## Le personnage

`assets/models/personnage/personnage.glb` — **CesiumMan**, © 2017 Cesium,
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/legalcode).
Récupéré depuis le dépôt officiel `KhronosGroup/glTF-Sample-Assets`.

**CC-BY, et non CC0 : l'attribution est OBLIGATOIRE.** Elle est portée ici, et
elle doit l'être aussi partout où le jeu est distribué — écran de crédits,
page de téléchargement, archive. Ce n'est pas une formalité : c'est la
condition à laquelle on a le droit de s'en servir.

Ces trois endroits ont été vérifiés un par un, et l'état de chacun est dit à la
fin de ce document, section **« Où l'attribution est portée, aujourd'hui »**.
Deux sur trois sont tenus depuis 17.0.0 ; le troisième attend qu'il existe une
page de téléchargement.

### Pourquoi celui-là, et pourquoi importé

Tout le reste de ce jeu est généré : la salle est décrite, les bras du joueur
sont écrits en C, les enseignes sont dessinées par `marqueeart`. Ce choix a une
raison — pas de format à inventer, pas de licence à démêler, reconstructible
partout — et il tient tant qu'un objet se décrit en quelques dizaines de lignes.

Un personnage humain ne s'y prête pas. Il faut un maillage cousu, des POIDS par
sommet vers un squelette, et un cycle de marche : c'est du travail d'artiste et
d'animateur, pas d'arithmétique. On importe donc, et le propriétaire l'a
demandé explicitement — « prend des personnages déjà existant que tu vas animé ».

CesiumMan a été retenu sur quatre critères, dans cet ordre :

1. **une licence claire et compatible** — CC-BY, attribuée ici. `BrainStem`,
   le seul autre humanoïde animé du lot, est sous EULA Poser : écarté ;
2. **une peau et une animation dans un seul fichier** — 19 os, 57 canaux, un
   cycle de deux secondes, 3 273 sommets et 4 672 triangles. À l'échelle de ce
   moteur, c'est un budget de décor, pas de personnage principal ;
3. **il vient du dépôt d'exemples de Khronos**, c'est-à-dire du jeu de fichiers
   sur lequel les lecteurs glTF sont éprouvés. Un défaut de chargement est donc
   NOTRE défaut, ce qui rend le déboguage possible ;
4. **il expose ce qui casse un lecteur naïf** : deux de ses nœuds portent une
   matrice au lieu d'un triplet translation/rotation/échelle — `Z_UP` pour le
   changement de repère, `Armature` pour la pose du squelette. Les ignorer
   couche le personnage sur le flanc. `ns_skin` les traite, et
   `tests/test_skin.c` le vérifie.

L'animation qu'il porte est une marche. Les autres allures — l'arrêt, la course
— sont dérivées d'elle au runtime plutôt que téléchargées : c'est la part
« que tu vas animer » de la demande.

---

# Le logiciel tiers

Ce document ne parlait que d'images et de modèles. Le paquet emporte aussi du
**code** qui n'est pas le nôtre, et deux de ces licences exigent que leur
mention accompagne le binaire distribué — pas seulement les sources.

| Bibliothèque | Auteur | Licence | Ce qu'elle exige d'un binaire distribué |
|---|---|---|---|
| **SDL3** | Sam Lantinga et contributeurs | zlib | rien ; la mention est « appréciée mais pas exigée » |
| **cgltf** | Johannes Kuhlmann | MIT | **la mention et le texte de licence** |
| **jsmn** | Serge Zaitsev | MIT | **la mention et le texte de licence** |
| **stb** (`image`, `image_write`, `image_resize2`, `dxt`) | Sean Barrett | MIT **ou** domaine public, au choix | rien si l'on retient le domaine public |
| **miniaudio** | David Reid | MIT-0 **ou** domaine public, au choix | rien |

SDL3 est **lié statiquement** : il n'y a pas de `.dll` ni de `.so` à côté de
l'exécutable, et le code de SDL est donc *dans* le binaire qu'on distribue.
C'est ce qui fait de sa mention une question de distribution et non de dépôt.

**SPIRV-Cross** (Apache-2.0) n'est pas dans cette table, et c'est délibéré : il
ne sert qu'à `tools/spv2msl`, au build, pour traduire les shaders en MSL. Il
n'entre dans aucun binaire livré, donc aucune obligation de distribution ne le
concerne. Il est nommé ici pour que la prochaine personne n'ait pas à refaire
l'enquête.

La **fonte** du jeu n'est pas tierce : `engine/sprite/ns_font5x7.h` est une
table de 95 glyphes de 5 x 7 bits écrite dans le dépôt. Les `.ttf` du dépôt —
`legacy/room/fonts/`, `server/internal/web/assets/fonts/` — ne sont lus ni par
le jeu ni par le paquet ; voir plus bas ce qu'il faut en penser quand même.

---

# Les images de 2020 : ce qui n'est PAS établi

C'est la section que ce document n'avait pas, et son absence était un trou et
non un oubli de forme : tout ce qui précède documente les **2,6 Mio rapportés**,
et laissait croire par omission que le reste était réglé. Il ne l'est pas.

## Ce qui a été mesuré

Le paquet installé porte, à la mesure de cette version, **93 images** dans
`bin/assets/scene/textures/`. Le total bouge — la direction artistique
remplace en ce moment des enseignes de 2020 par des planches dessinées au
build — mais **le nombre d'images de 2020 ne bouge pas**, et c'est lui qui
compte ici :

| | |
|---|---|
| viennent de `legacy/room/textures/`, c'est-à-dire de **2020** | **58** |
| viennent d'ici — Poly Haven, CC0, listées plus haut | **19** |
| **générées au build** (enseignes de `marqueeart`, écrans de jeu, flanc de borne, néon) | **14** |
| **orphelines** : aucune source dans l'arbre, aucun matériau qui les référence | **2** |

Le reste — enseignes, écrans de jeu, flanc de borne, néon — est **généré au
build** et ne pose aucune question de licence : c'est de l'arithmétique.

**Les cinquante-huit images de 2020 ne portent aucune trace d'origine ni de
licence** — ni dans ce fichier, ni dans `legacy/`, qui ne contient aucun
document de licence.

Une texture de béton ou de moquette sans provenance est un risque théorique.
Huit de ces images n'en sont pas un : elles ont été **ouvertes et regardées**,
et voici ce qu'elles sont.

| Fichier | Ce que c'est, en le regardant | Ayant droit apparent |
|---|---|---|
| `poster_7.jpg` | le flyer publicitaire d'arcade **PAC-MAN**, logo Midway compris | Bandai Namco / Midway |
| `poster_8.jpg` | le flyer publicitaire d'arcade **DONKEY KONG**, logo Nintendo compris | Nintendo |
| `poster_3.jpg` | le flyer publicitaire **ATARI « Video Pinball »**, logo Atari compris | Atari |
| `poster_6.jpg` | l'affiche **« Palace Arcade — Hawkins »** de *Stranger Things* | Netflix |
| `poster_5.jpg` | une illustration de la gamme **« Arcade »** de *League of Legends* | Riot Games |
| `poster_2.jpg` | une affiche **« Space Paranoids — ENCOM »**, l'arcade fictive de *Tron* | Disney (et l'illustrateur) |
| `poster_1.jpg` | une affiche de festival **« Arcade Armageddon »**, graphisme d'auteur | inconnu |
| `poster_4.jpg` | une illustration de « gaming room », retitrée **NINE 19 TEEN** | inconnu |

Ces huit images étaient **accrochées aux murs de la salle**. Elles ne sont plus
copiées par le build : `tools/posterart` en dessine huit autres.

## Ce qui a été retiré ensuite, et pourquoi — les PERSONNAGES

Le même examen, mené sur **toutes** les images de `legacy/games/` et de
`legacy/room/textures/` que le build copiait, donne le même genre de résultat.
Chacune a été ouverte, agrandie au plus proche et regardée sur un damier pour
voir la couche alpha.

| Fichier | Ce que c'est, en le regardant | Ayant droit apparent |
|---|---|---|
| `1_pacman/pacman.png` | quatre **PAC-MAN** : le disque jaune, la part de camembert retirée, quatre ouvertures de bouche | Bandai Namco |
| `1_pacman/enemy.png` | huit **FANTÔMES** de Pac-Man : le dôme arrondi, la jupe à trois vagues, les yeux à pupille décalée | Bandai Namco |
| `1_pacman/items.png` | les **CERISES** et la **FRAISE** de bonus du même jeu (elle n'était pas copiée, elle était là) | Bandai Namco |
| `3_flappy_bird/birds.png` | l'**OISEAU** de *Flappy Bird*, ses trois teintes et ses trois battements d'aile | Dong Nguyen / .Gears |
| `3_flappy_bird/pipes.png` | ses **TUYAUX** à embouchure, eux-mêmes dérivés d'ailleurs | idem |
| `3_flappy_bird/backgrounds.png` | son **CIEL** jour et nuit, sa ligne d'immeubles, sa haie | idem |
| `3_flappy_bird/sol.png` | son **SOL** rayé | idem |
| `3_flappy_bird/medals.png` | ses **MÉDAILLES**, l'oiseau gravé dessus | idem |
| `3_flappy_bird/scoreBoard.png` | son **TABLEAU** de fin de partie | idem |
| `3_flappy_bird/high_score.png` | une image de banque d'images, **avec le filigrane « ©123RF » encore dessus** | 123RF et son illustrateur |
| `flappy_easy_font.jpg`, `flappy_hard_font.jpg` | deux captures du jeu de 2020, qui montrent en grand l'oiseau et les tuyaux ci-dessus, employées comme écrans d'attract sur deux bornes | idem |
| `floor.jpg` | **le tapis du hall** — la plus grande surface de la salle : un **PAC-MAN** au néon jaune, un de ses **FANTÔMES** au néon rose, les **CERISES**, et la silhouette d'une **MANETTE** de console | Bandai Namco ; Sony |

`high_score.png` mérite une ligne à part : ce n'est pas une œuvre libre mal
créditée, c'est **l'aperçu non payé d'une œuvre payante**, et le filigrane le
dit lui-même. Elle n'était même pas affichée par le jeu — le `file(GLOB *.png)`
de `assets/CMakeLists.txt` l'emportait, avec `medals.png` et `scoreBoard.png`.
Ce n'est pas ce que le jeu montre qui compte, c'est ce que le build **copie**.

**Les treize sortent du paquet.** Elles restent dans `legacy/`, qui est
l'archive de 2020 et n'est pas distribuée ; ce qui change, c'est la liste que le
build copie — et le `GLOB` qui les emportait est devenu une liste nommée.

Deux outils dessinent les remplaçantes, pour la même raison que `posterart` :
une planche produite par arithmétique n'a **aucune licence à démêler**, se
régénère à l'identique sur les trois plateformes et appartient au dépôt.

* `tools/spriteart.c` — le héros et les poursuivants du labyrinthe (en niveaux
  de gris, le jeu les teinte), le mobile, les obstacles, le ciel et le sol du
  jeu d'envol. **Aux mêmes cotes au pixel près** que les planches remplacées :
  `games/envol/envol.c` découpe ses atlas en dur, et n'a pas eu une ligne à
  changer.
* `tools/moquetteart.c` — le tapis du hall, avec des icônes qui n'appartiennent
  à personne : un manche, un jeton, une étoile, un éclair, un dé, une note, une
  cible, une planète.

## Les NOMS : trois marques déposées

Un examen des huit `.title` a été mené, et il n'a pas donné le même verdict
partout.

| Nom de 2020 | Verdict | Devenu |
|---|---|---|
| **PAC-MAN** | marque déposée de Bandai Namco | **DÉDALE** |
| **TETRIS** | marque déposée de Tetris Holding, qui a obtenu en justice que la protection porte aussi sur l'apparence du jeu | **APLOMB** |
| **FLAPPY BIRD** | nom distinctif attaché à une œuvre précise (Dong Nguyen, 2013) | **ENVOL** |
| ASTEROID | nom commun (corps céleste), descriptif de ce qu'on tire — **proche** de la marque *Asteroids* (Atari) à une lettre près : risque résiduel, signalé | inchangé |
| SNAKE | nom commun et nom de **genre** — personne ne le détient | inchangé |
| SHOOTER | nom de **genre**, purement descriptif | inchangé |
| DEMINEUR | nom commun français de l'activité | inchangé |
| PIANO | nom d'un instrument de musique | inchangé |

Une **mécanique** ne s'approprie pas : manger des pastilles dans un labyrinthe
en fuyant quatre poursuivants, empiler des pièces de quatre cases, franchir des
ouvertures en battant des ailes — ce sont des systèmes de règles, et un système
de règles n'est pas protégeable. Un **nom** et un **personnage**, si. C'est
exactement la ligne qui a été tracée : les règles de `games/` n'ont pas bougé,
les noms et les figures ont changé.

## Ce que ça veut dire, et ce que ça ne veut pas dire

Ça ne dit rien de la qualité du travail de 2020 : un projet d'étude qui
décore sa salle avec les affiches du genre qu'il célèbre est une chose banale et
sans conséquence, parce qu'il n'est pas distribué.

Ça dit que **le passage à la vente change la nature de ces fichiers**. Une
attribution ne les rattrape pas — ce ne sont pas des œuvres sous licence libre
mal créditées, ce sont des œuvres sous droit exclusif employées sans droit. Il
n'existe pas de rédaction de ce document qui rende `poster_8.jpg` distribuable.

**Tout a été retiré, et remplacé.** Les huit affiches d'abord, puis les treize
planches et textures ci-dessus. Ce qui a été fait dans les deux cas est le même
geste : sortir le fichier de la liste que le build **copie** — un
`retiredTextures` ne suffirait pas, il déclare qu'une image n'est plus employée,
il n'empêche pas de la livrer — et dessiner la remplaçante.

Un arbre de build incrémental garde pourtant ce qu'on lui retire :
`copy_if_different` copie, il n'efface pas, et `room/CMakeLists.txt` installe
`games/`, `materials/` et `scene/` **en bloc**. Vérifié sur un arbre réel : il
contenait encore les seize cartes dérivées des huit affiches retirées deux
versions plus tôt. Une purge nommée tourne donc au build
(`assets/CMakeLists.txt`, bloc « LES GRAVATS ») et efface les retirés de l'arbre
généré. Sans elle, le contrôle qu'on croit avoir passé, on ne l'a pas passé.

## Trois autres points ouverts, plus petits

* **`server/internal/web/assets/fonts/`** porte `sega.ttf`, `neon.ttf` et
  `police.ttf`, sans licence. Le nom du premier annonce une reproduction du
  logotype d'un tiers. Ces fontes ne sont **pas** dans le paquet du jeu — elles
  ne concernent que le site web du classement — mais publier ce site les
  distribue.
* **Deux images orphelines** voyagent dans le paquet de 17.0.0 :
  `gamepad_diff_1k.jpg` (reliquat d'un modèle Poly Haven retiré) et
  `coffeetable_01_diff_1k.jpg` (la texture turquoise écartée, voir plus haut).
  Mesuré : `grep` sur `salle.gltf` **0**, sur `salle.room.json` **0**, et
  `find assets legacy` ne trouve **aucun fichier source** pour l'une ni pour
  l'autre. Ce sont des résidus d'un répertoire de build incrémental. Elles sont
  CC0, donc sans risque juridique — mais un paquet qui emporte deux fichiers
  dont plus aucune source ne rend compte est exactement ce que ce document
  existe pour empêcher. Une reconstruction depuis un répertoire de build neuf
  les fait disparaître.
* **`legacy/`** n'est pas installé par CPack, mais il est dans le dépôt. Si le
  dépôt devient public, il publie les 60 images de 2020 et les planches des
  jeux — les huit affiches, les personnages de Namco et l'oiseau de *Flappy
  Bird* compris.
* **`docs/render-*.png`** non plus n'est pas installé, et pose la même question
  en plus petit : une douzaine de captures de recette montrent l'état d'avant,
  donc les affiches, l'oiseau, le tapis et les personnages. Elles servent de
  preuve « avant / après » et c'est leur seule raison d'être ; elles sortiraient
  avec `legacy/` le jour où l'on ouvrirait le dépôt.

---

# Où l'attribution est portée, aujourd'hui

CC BY 4.0 demande que l'attribution accompagne l'œuvre « d'une manière
raisonnable au regard du support ». Pour un jeu, cela veut dire trois endroits,
et c'est ce que la section « Le personnage » exigeait déjà.

| Endroit | État | Où |
|---|---|---|
| **l'écran de crédits** | tenu depuis 17.0.0 | `Échap` → `CREDITS` ; le contenu est dans `room/room_credits.c` |
| **l'archive** | tenu | ce fichier est installé à la racine du paquet (`room/CMakeLists.txt`) |
| **la page de téléchargement** | **pas tenu** | il n'y a pas encore de page |

L'écran de crédits n'est pas seulement écrit, il est **défendu** :
`tests/test_menu.c` vérifie que la table porte l'œuvre (*CesiumMan*), l'auteur
(*Cesium*), la licence (*CC BY 4.0*) et le lien vers son texte, et qu'une ligne
du menu l'ouvre. Effacer l'un des quatre fait échouer le test `menu`, donc le
build. Une mention légale que rien ne défend finit par disparaître dans un
nettoyage.
