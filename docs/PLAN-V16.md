# PLAN V16 — « le jeu et la 3D ne sont pas au rendez-vous »

Ce document est le seul endroit où l'état d'avancement est vrai. Il est mis à
jour à chaque lot fermé, et un lot ne se ferme que sur une PREUVE — une mesure,
une capture, un test qui passe — jamais sur une affirmation.

## Pourquoi ce document existe

Le propriétaire a relevé neuf défauts en jouant. Ils sont recopiés ici mot pour
mot, parce qu'une reformulation est déjà une négociation :

1. « des objets 3D sont situé n'importe comment dans l'espace 3D »
2. « la hauteur du personnage ne permet pas d'être bien positionner à hauteur
   des bornes d'arcade »
3. « L'écran 2D des bornes d'arcade n'est pas du tout pareil quand on joue et ne
   joue pas »
4. « Quand on ne joue pas il est moche et ressort de l'écran »
5. « Le layout des boutons de la borne d'arcade n'a pas de sens »
6. « Les objets sont mal positionnée »
7. « le jeu ne respire aucune ame n'y atmoshpher »
8. « Le personnage a des bras très mal réalisé […] on dirait que c'est fait par
   un enfant »
9. « Tu as besoin de suivre un plan pour ne pas oublié ce que tu fais »

Le point 9 est la raison d'être du fichier. Les huit autres sont les lots.

### Ajoutés en cours de route, avec captures à l'appui

10. « On voit les écrans comme plannée au dessus de la borne d'arcade et non
    inclus dans la borne d'arcade — je pense que la 3D de la borne devrait être
    creusée pour que l'écran semble incrusté dans la borne, et non se rajouter
    en plus »
11. « Nineteen j'aurais aimé qu'il soit écrit en néon »
12. « l'écran plat devrait afficher les scores lives des joueurs et le
    classement »
13. « Les objets sont mal incrustés dans la salle, cf la capture de la lampe qui
    n'est pas accrochée au plafond »

Et une méthode, qui vaut instruction : « ne manque pas d'utiliser le connecteur
Blender pour améliorer la partie 3D en regardant directement ce que tu produis
via le MCP Blender, sinon tu vas commettre des erreurs. »

C'est ce qui a servi pour L10 : le creux a été rendu dans Blender sous l'angle
rasant EXACT de la capture du propriétaire avant d'être livré. Les chiffres
disaient que la dalle reculait de 35 mm ; seul le rendu disait qu'on voyait bien
les quatre parois autour de l'image.

## Ce qui a été MESURÉ avant d'écrire ce plan

Rien ici n'est une impression. Chaque ligne est sortie du code ou d'une capture.

### Le défaut 3/4 — l'écran a deux rendus, et c'est écrit dans le code

`engine/render/ns_render.c:1190` porte le commentaire qui l'avoue :

> « Le traitement de tube n'est appliqué QUE sur l'écran vivant. On pourrait le
> mettre sur tous les matériaux d'écran […] On ne le fait pas : la courbure
> déplace les UV, et une image fixe déjà cadrée pour la dalle se retrouverait
> rognée. »

Conséquence exacte : **une** borne sur dix-neuf a un tube (courbure, lignes de
balayage, triade RVB, verre sombre, reflet spéculaire). Les dix-huit autres
affichent leur JPEG en émissif pur — pas de verre, pas de grain, albédo non
rabattu. C'est la définition du défaut : « pas du tout pareil quand on joue et
ne joue pas », et « il ressort de l'écran » parce que rien ne l'assombrit.

Le motif invoqué est réel mais la correction choisie était la mauvaise : le
barillet dilate d'un facteur `1 + 0,5 a` dans les coins, il suffit de
pré-rétrécir les UV de l'inverse pour que RIEN ne soit rogné. Trois lignes.

### Le défaut 5 — le pavé de boutons, mesuré

`assets/blender/borne.py:559` pose deux rangées de trois par poste :

    base = cote * MANCHE_DX + 0.075        # MANCHE_DX = 0.155
    x    = base + col * 0.052 + rang * 0.012

Ce que ça donne, en centimètres depuis l'axe de la borne :

| poste | manche | boutons |
|---|---|---|
| gauche | −15,5 | de **−8,0 à +2,4** |
| droite | +15,5 | de +23,0 à +33,4 |

Deux choses fausses, et la seconde est celle qu'on voit :

- **Les boutons du joueur 1 franchissent l'axe** et viennent se loger sous la
  boule du joueur 2. Aucune borne n'a jamais été faite comme ça.
- **Ce n'est pas un arc.** Une grille 2×3 cisaillée de 12 mm n'est le geste
  d'aucune main. Les panneaux réels — Sega, Neo-Geo, Vewlix — décalent CHAQUE
  colonne en X *et* en Z pour suivre l'arc naturel des quatre doigts.

Le caisson fait 0,72 m de large. Deux postes à six boutons demandent 0,90 m au
minimum. La contradiction est géométrique, pas esthétique : il faut trancher
entre un poste ou un caisson plus large. C'est une décision de design, elle est
portée au lot L2.

### Le défaut 2 — l'ergonomie, chiffrée

| cote | valeur | source |
|---|---|---|
| œil debout | 1,70 m | `room_camera.c:39` |
| centre d'écran | 1,29 m | `borne.ancres.json` |
| surface du panneau | 0,97 à 1,05 m | `borne.py`, `Y_NEZ`/`Y_PANNEAU_FOND` |
| sommet du marquee | 1,88 m | `borne.py`, `H` |

L'œil est donc **41 cm au-dessus du centre de l'écran**. Mesuré depuis l'ancre
de joueur que la borne déclare elle-même (`playerAnchor`, à 0,662 m de la dalle
à l'horizontale), cela donne :

| ce qu'on regarde | angle sous l'horizon |
|---|---|
| haut de la dalle | 19,6° |
| **centre de la dalle** | **31,8°** |
| bas de la dalle | 41,5° |

Le panneau de commande, lui, est JUSTE : sa surface est à 0,97–1,05 m, ce qui
est la cote d'une vraie borne. C'est l'écran qui est bas, et le joueur qui est
trop près. Regarder une image dont le centre est à 32° sous l'horizon demande
de baisser la tête, pas seulement les yeux — c'est exactement la sensation que
le propriétaire décrit.

### Le défaut 8 — les bras

`engine/render/ns_viewmodel.c`, 277 lignes, sept segments : deux manches, deux
avant-bras, deux mains, et rien d'autre. Pas de bras, pas de coude, pas de
poignet. Chaque segment est un prisme à douze pans, la main est une paume
aplatie plus quatre doigts droits et un pouce.

Ce que ça donne à l'écran, capture à l'appui : deux tubes clairs qui montent du
bas du cadre, terminés par un éventail de bâtonnets. La main gauche ne referme
rien sur la boule du manche — elle la traverse.

### Le défaut 7 — l'atmosphère

Capture `--view=allee`, ce qu'on y voit :

- les FLANCS des bornes portent une trame de losanges matelassée. C'est une
  texture de tissu d'ameublement posée sur de la tôle peinte ;
- un liseré magenta vif court sur tous les chants (le T-molding), à une
  saturation qui prend l'œil avant le reste ;
- le plafond est une trame beige chaude qui se lit comme une pergola ;
- le centre de la salle est un grand vide noir : aucun mobilier, aucun repère,
  rien à parcourir.

## Les lots

Un lot = un défaut, un responsable, un critère de recette MESURABLE, un état.
L'état ne passe à « fermé » que quand la preuve est produite et vérifiée par
moi, pas par celui qui a fait le travail.

| # | lot | critère de recette | état |
|---|---|---|---|
| L1 | L'écran, un seul rendu | les 19 dalles passent par le tube ; capture attract et capture en jeu indiscernables en traitement | **fermé** (88fd75c) |
| L2 | Le panneau de commande | aucun bouton ne franchit l'axe ; layout de matériel réel ; le second bouton du démineur existe | **fermé** (428511f) |
| L3 | Les flancs et le liseré | plus de trame matelassée sur de la tôle ; le T-molding cesse de prendre l'œil avant le caisson | **fermé** (L3) |
| L4 | Les bras | bras + avant-bras + main, coude et poignet réels ; la main se REFERME sur la boule | **fermé** (a97cbfa) |
| L5 | L'ergonomie | l'écran est DANS le cadre quand on se plante devant une borne | **fermé** (2bd97c4) |
| L6 | Le placement | tout prop a une raison d'être là ; rien ne flotte, rien ne s'encastre ; contrôle au build | ouvert |
| L7 | L'atmosphère | le centre de la salle se parcourt ; le plafond n'est plus une pergola | ouvert |
| L8 | La recette finale | les 8 jeux, le réseau, le duel, les 35 tests, et une passe de jeu réelle | ouvert |
| L9 | Les écrans vivants | les dix-huit bornes non jouées jouent leur propre partie | **fermé** (a26f95b) |
| L10 | L'écran incrusté | le meuble est CREUSÉ ; on voit les parois du creux autour de l'image | **fermé** (428511f) |
| L11 | L'enseigne au néon | « NINETEEN » est un vrai néon, pas une image peinte | **fermé** (741856e) |
| L12 | L'écran plat du bar | il affiche le classement et les scores en direct, au lieu d'être noir | **fermé** (6ef43f2) |
| L13 | Les objets accrochés | rien ne flotte : la lampe pend du plafond, pas dans le vide | **fermé** (50fbfe6) |

## Qui contrôle quoi

Le propriétaire a demandé que rien ne soit pris pour argent comptant. Le
dispositif est donc :

- **Chef de projet** — tient ce fichier, refuse un lot dont la preuve manque.
- **Game designer** — porte la refonte de la salle en gardant l'ADN de 2020.
- **Historien** — arbitre toute question de fidélité au matériel réel.
- **Développeurs** — implémentent, un lot chacun.
- **Relecteurs** — cherchent la faute, pas l'accord.

Et par-dessus : **je vérifie moi-même chaque preuve**. Un agent qui dit qu'un
lot est fermé ne le ferme pas. C'est la capture qui le ferme.


## Journal des lots fermés

### L1 — `88fd75c`

Preuve : capture `--view=allee` avant / après. L'écran de la borne Snake passe
d'un aplat vert vif à bords francs à un tube bombé, coins assombris, lignes de
balayage gravées dans la surface. Les dix-huit dalles non jouées reçoivent
exactement le traitement de celle qui joue.

### L4 — `a97cbfa`

Preuve : `ns_test_ik`, 3 493 vérifications, 0 échec, et les trois distances qui
comptent, avant → après :

| ce qu'on mesure | main plate | main fléchie |
|---|---|---|
| bout du doigt gauche au manche | 3,0 cm | **0,2 cm** |
| bout du doigt droit aux boutons | 2,7 cm | **1,3 cm** |
| bout du doigt à la fente | — | **1,3 cm** |

Et la capture `--play-at=borne_arcade_1` : la main gauche enveloppe la boule du
manche au lieu de la traverser, la droite est en suspension au-dessus des
pastilles, le pouce est opposé. `ctest` : 35/35.

Trois défauts trouvés en le faisant, chacun réel et aucun soupçonné au départ :
les normales d'une main fléchie que l'étirement en Z faussait ; le roulis de la
main, laissé au hasard par la rotation minimale de `segment_matrix` ; et
`test_ik` qui mesurait le bout du doigt à l'ancienne place et accusait l'IK
d'une faute qui n'était pas la sienne.


### L9 — `a26f95b`

Il ne figurait pas dans les neuf défauts du propriétaire, et c'est pourtant la
cause la plus directe de « le jeu ne respire aucune âme » : dix-huit écrans
FIGÉS dans une salle dont le principe d'éclairage déclaré est que « l'essentiel
de la lumière vient des dix-neuf écrans ».

Chaque borne joue maintenant sa propre partie, menée par le pilote automatique
du jeu. Preuve de coût, palier medium, vue allée, 179 images :

| | temps GPU moyen |
|---|---|
| sans les démos | 23,7 ms |
| avec les dix-huit | **24,2 ms** |

Un demi-pas de temps, parce qu'on n'en redessine que quatre par image. Les
parties, elles, avancent toutes les dix-huit à 120 Hz — un état de jeu est une
valeur pure, et ça ne se mesure pas.


### L2 + L10 — `428511f`

Preuve chiffrée, dans le glTF exporté, avant → après :

| | avant | après |
|---|---|---|
| bouton le plus à droite | x = **+0,3621** (11,6 mm hors du meuble) | +0,0996, dans la tôle |
| centre de l'emprise des commandes | +92 mm de l'axe | **0 mm** |
| dalle par rapport à sa face | **76,2 mm devant** | 35 mm **derrière** |
| cadre autour de l'image | 7 mm | 26,5 mm en haut/bas, 84 sur les côtés |
| inclinaison de la dalle | 21,6° écrit / 19,87° construit | 19,87°, **calculée** |
| triangles | 4 992 | 3 354 |

Et `ns_test_ik` après le déplacement de toutes les ancres : doigt gauche à
**0,2 cm** du manche, droit à **1,3 cm** des boutons, 3 493 vérifications,
0 échec.

### L5 — `2bd97c4`

Le chiffre qui explique tout : demi-champ vertical de 31,0°, dalle à 31,8° sous
l'horizon. **Elle était huit dixièmes de degré sous le bord bas du cadre.** Le
défaut n'était dans aucune cote du meuble — toutes sont dans les plages du
matériel réel — mais dans le fait que le moteur ne posait le regard qu'à
l'entrée en partie, un instant sur cinq.


## L'atmosphère, poste par poste

`docs/SPEC-ATMOSPHERE.md` classe vingt-cinq postes par effet décroissant. État :

| # | poste | état | mesure |
|---|---|---|---|
| 1 | six suspensions basses | en cours | — |
| 2 | bancs, baby-foot, mange-debout, vitrine | en cours | — |
| 3 | le plafond cesse d'être une pergola | **fait** | teinte 0,84 → 0,30, trame 0,6 → 1,2 m |
| 4 | le liseré cesse d'être magenta | **fait** | saturation 0,93 → 0,22 |
| 5 | le sol arrête de faire la lumière | **fait** | émissif 0,25 → 0,10, motif 0,60 → 0,40 m |
| 6 | les flancs portent la couleur de leur caisson | **fait** | saturation 0,42 → 0,70 ; losange 18,75 → 6,6 cm |
| 10 | les écrans portent à 3,4 m | **fait** | couverture 70,2 → 88,7 m² |
| 12 | le tableau du bar | **fait**, autrement | rendu VIVANT plutôt que repeint — voir L12 |

Médiane de luminance de `--view=allee`, mesurée à chaque étape avec
`scratchpad/lum.py` : 53,5 au départ, 54,7 après le plafond, 49,6 après les
flancs, **50,6** après la portée des écrans. Le critère du designer était
« ≥ 42 ».
