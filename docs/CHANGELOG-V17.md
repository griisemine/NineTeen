# Nineteen 17.0.0 — le paquet devient vendable, sauf sur trois points

16.0.0 avait rendu la salle juste. 17.0.0 s'occupe de ce qui séparait un projet
d'un produit : ce que le joueur comprend en arrivant, ce qu'il tient dans les
mains, et ce que le distributeur a le **droit** de distribuer.

Ce document a été **réécrit après recette**, et non tenu à jour au fil des
commits. Tout ce qui suit a été vérifié en **déballant l'archive et en lançant
le jeu depuis le répertoire déballé** — jamais depuis l'arbre de build. C'est la
distinction qui compte : ce projet a déjà cru livrer des assets que seul l'arbre
de build fournissait.

## Ce qui change en une phrase

Les **vingt et une images de tiers** identifiées ne sont plus dans le paquet,
**trois jeux ont changé de nom**, la **manette** est branchée jusque dans le
journal d'entrées, et quatre jeux qui ne se jouaient pas se jouent.

---

## Comment cette recette a été faite

```sh
cd build/macos-universal && cpack                 # nineteen-17.0.0-Darwin.tar.gz
tar xzf …/nineteen-17.0.0-Darwin.tar.gz -C /ailleurs
cd /ailleurs/nineteen-17.0.0-Darwin/bin && ./nineteen …
```

Le jeu le confirme lui-même à la première ligne de son journal, et c'est cette
ligne qui rend la recette valable :

```
assets trouvés à côté du binaire : l'arbre de build n'est pas monté
(c'est une installation, elle se suffit)
```

---

## La liste de 16.0.0, reprise point par point

| Ce qui était annoncé non tenu | Mesuré aujourd'hui, et comment | État |
|---|---|---|
| **Huit affiches de tiers** dans la salle | archive déballée, chaque image classée par son origine dans l'arbre : **0 `poster_*`**, 0 planche *Flappy Bird*, 0 personnage Namco, 0 `high_score.png` (celle au filigrane « ©123RF ») | **tenu** |
| **Deux noms de marque** (`PACMAN`, `TETRIS`) | il y en avait **trois** : + `FLAPPY BIRD`. Les enseignes de borne ont été ouvertes : elles portent **APLOMB**, **DÉDALE**, **ENVOL** | **tenu, et sous-estimé** |
| **Il n'y a pas de manette** | `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` (`room/main.c:1328`), branchement à chaud suivi, et le pad est fusionné dans `hmask` (`room/main.c:3188`). Test **n° 4 « manette »** dans la suite | **tenu** |
| **Le menu ne règle ni la résolution ni le plein écran** | capture du menu ouverte : les lignes **DEFINITION** et **PLEIN ECRAN** y sont, sur **18 lignes**, toutes dans le cadre | **tenu** |
| **Il n'y a pas d'icône de fenêtre** | `SDL_SetWindowIcon` appelé (`engine/rhi/ns_rhi.c:189`) | **tenu** |
| **`--width`/`--height` ne font rien** | corrigé, **mais pas complètement** : 640x360, 1280x720, 1920x1080, 2560x1440 sortent juste — **1600x900 sort en 1280x720**. Voir « ce qui n'est pas tenu », point 1 | **partiellement tenu** |
| **Le paquet n'est pas signé** | `codesign -dv` sur le binaire déballé : `Signature=adhoc`, `linker-signed`. Ni Developer ID ni Authenticode | **toujours non tenu** — achat et décision |
| **Le personnage n'a qu'un cycle d'animation** | le jeu le dit encore lui-même : « un seul cycle d'animation (2.00 s) ». Mais l'accroupi n'est plus un défaut : il est **dérivé et calé** à 67,6° de cuisse, pieds recalés à 1e-4 près | **toujours non tenu, mais l'accroupi est réparé** |
| **Cinq des neuf contrôles de l'audit** absents | recompté dans `tools/roomgen.c` : `check_inside_shell`, `check_solid_overlaps`, `check_grounded`, `check_cabinet_clearance`. **C-04 à C-08 toujours absents** | **toujours non tenu, cinq sur neuf** |
| **Déterminisme sur un seul système** | `lipo -info` : `x86_64 arm64`. `tests/check_determinism.cmake` rejoue dans **deux processus** — même machine, même macOS | **toujours non tenu** |
| **Découverte d'adversaire, relais sans TLS** | inchangé | **toujours non tenu** |

---

## Ce que 17.0.0 tient, avec sa mesure

### Le paquet ne porte plus d'œuvre de tiers identifiée

Chaque image du paquet déballé a été classée par son origine réelle dans
l'arbre — pas par ce qu'une liste en disait.

| | |
|---|---|
| images dans le paquet | **103** |
| générées au build (`posterart`, `spriteart`, `moquetteart`, `marqueeart`, `texgen`) | **43** |
| Poly Haven, CC0, documentées | **19** |
| venant de `legacy/`, **sans origine ni licence établie** | **41** |
| dont dans le décor (`scene/textures/`) | **29**, contre 58 en 16.0.0 |
| **œuvres de tiers identifiées** | **0** |
| images orphelines (aucune source, aucun matériau) | **0**, contre 2 |

Les **dix-huit `retiredTextures`** déclarées par la salle ont été cherchées une
par une dans l'archive, **cartes normales et ORM dérivées comprises** :
**aucune n'est livrée**. La salle de 2020 (`salle-legacy.*`) n'est pas installée
non plus.

Trois vérifications ont été faites **en ouvrant les images**, parce que le nom
d'un fichier ne prouve rien :

* le **tapis du hall** (`moquette_neon.png`) portait un Pac-Man, un de ses
  fantômes, ses cerises et une manette de console. Celui qui est livré porte une
  épingle, un dé, une étoile, une note, une planète, un éclair et une cible —
  dessiné par `tools/moquetteart` ;
* les **poursuivants de DÉDALE** ne sont plus des fantômes : ce sont des croix
  et des rotors à quatre pales, en niveaux de gris ;
* `tetris_font.jpg`, `snake_font.jpg`, `asteroid_font.jpg` portent des noms qui
  inquiètent et ne sont **pas** des œuvres de tiers : ce sont les écrans d'aide
  du projet de 2020, sans logo ni personnage. `tetris_font.jpg` ne porte nulle
  part le mot TETRIS.

### Trois jeux renommés, jusque dans la base

**TETRIS → APLOMB**, **PAC-MAN → DÉDALE**, **FLAPPY BIRD → ENVOL**. Les
enseignes de borne ont été ouvertes et lues. La migration `0003_debaptise.sql`
**renomme le créneau** au lieu d'en créer un neuf : un classement mondial change
d'étiquette, pas de contenu. Les règles de `games/` n'ont pas bougé.

`ASTEROID` **n'est pas renommé** : nom commun, mais à une lettre de la marque
*Asteroids* d'Atari. Signalé comme risque résiduel — c'est une décision, pas un
travail.

### La manette, et le piège qu'elle cachait

Le branchement n'était pas la difficulté. La difficulté était que
`--journal-entrees`, `--rejouer` et les duels en pas verrouillé reposent tous
sur un masque d'appuis. Une manette qui aurait alimenté les touches sans
alimenter ce masque aurait rendu **toute partie jouée au pad irreproductible**,
et fait diverger les duels — sans erreur et sans message.

Elle l'alimente : `hmask = room_keys_mask(keys) | pad_mask`
(`room/main.c:3188`), et le test **n° 4 « manette »** vérifie que les deux
périphériques produisent le même masque. Le test garde le build.

Ce qu'elle ne fait pas : **ni saut, ni accroupi, ni course** — la table est
`dpad` + `A` + `Start` + `B` + deux sticks, et elle est **en dur**.

### Quatre jeux qui ne se jouaient pas

Personne n'avait joué les jeux ; ce sont eux qu'on vient chercher. Les tests qui
les gardent sont les n° 12 à 19 de la suite.

| Jeu | Ce qui n'allait pas | Vérifié par |
|---|---|---|
| **DÉMINEUR** | **ingagnable** : 100 bombes sur 400 cases, 200 parties, 200 défaites. `PTS_WIN` et tout l'écran GAGNE étaient du code inatteignable | `tests/test_demineur.c` compte les `DEM_WON` |
| **PIANO** | **imperdable** : la limite d'oublis était gardée par `hard &&`, donc une borne se jouait en ne touchant à rien, indéfiniment | `test_ne_rien_jouer_finit_par_tuer` |
| **SHOOTER** | le boss naissait à `y = -170`, hors écran, à vitesse nulle : **invisible et invulnérable**, les vagues gelées derrière lui, les cinq armes en code mort | `tests/test_shooter.c` |
| **ENVOL** | un **chronomètre déguisé en score** : une droite à 0,588 tuyau/s, identique sur cinq graines | `tests/test_envol.c` : « deux graines différentes donnent des parties différentes » |
| **DÉDALE** | parties **toutes identiques** | `tests/test_dedale.c` ; la graine décide l'ordre de sortie de l'enclos |

Les huit jeux ont été relancés **depuis le paquet déballé**, en `--autoplay` :
tous démarrent et tous marquent — APLOMB 12 700, SHOOTER 2 130, ASTEROID 1 980,
DÉDALE 980, DÉMINEUR 287, PIANO 100, SNAKE 70, ENVOL 12.

### La première minute, regardée sur capture

Les captures ont été sorties du paquet, réduites et **ouvertes** — c'est comme
ça que les deux défauts de cadre de 16.0.0 avaient été trouvés, et pas
autrement.

* **Le bandeau d'arrivée** est lisible et tient sur une ligne :
  « ZQSD/WASD SE DEPLACER · SOURIS REGARDER · E JOUER SUR UNE BORNE · ECHAP
  REGLAGES ET AIDE ».
* **Le menu tient dans son cadre** à **18 lignes** — il en avait 16 quand il a
  débordé, et deux lui ont été ajoutées depuis. Le cadre se calcule au dessin
  sur `MI_COUNT` ; c'est le pas des lignes qui cède.
* **La page COMMANDES** et **la page CREDITS** s'ouvrent et tiennent dans leur
  cadre.
* **L'écran de crédits** porte les quatre mentions que CC BY 4.0 exige —
  CESIUMMAN, © 2017 CESIUM, CC BY 4.0 INTERNATIONAL,
  CREATIVECOMMONS.ORG/LICENSES/BY/4.0/ — **et la mention « MODIFIE »** qu'exige
  le § 3.a.1.B, formulée « TEXTURE REPEINTE — MAILLAGE ET ANIMATION INTACTS ».
  `tests/test_menu.c` échoue si l'une disparaît.

---

## La recette

Mesurée sur un arbre de build **neuf** (625 cibles, reconfiguré depuis zéro),
pour que « zéro avertissement » veuille dire quelque chose.

| | |
|---|---|
| la suite de tests | **41 / 41** |
| le compilateur | **0 avertissement** sur 625 cibles reconstruites |
| `nineteen.env` | **34 clés, toutes consommées**, vérifié au démarrage |
| le binaire | universel, `x86_64 arm64` |
| la signature | **`adhoc` / `linker-signed`** — c'est-à-dire aucune |
| le paquet | **231 Mio déballé**, **166 Mio** en `.tar.gz` |
| images de tiers identifiées dans le paquet | **0** |
| `retiredTextures` livrées | **0 sur 18** |
| la salle | 152 objets, 140 matériaux, 64 lumières, **19 bornes** |
| les jeux | **8 sur 8** démarrent et marquent, depuis le paquet |
| le personnage | 3 273 sommets, 4 672 triangles, **19 os**, monté |
| roomgen, contrôles d'audit écrits | **4 sur 9** |

---

## Ce qui n'est pas tenu, et que je ne masque pas

Dans l'ordre de ce qui empêche la vente.

### 1. `--width`/`--height` mentent encore, sur une valeur précise

Le correctif est réel — 640x360, 1280x720, 1920x1080 et 2560x1440 sortent
exactement à la taille demandée. Mais **`--width=1600 --height=900` sort en
1280x720**, c'est-à-dire à la définition gardée dans `settings.cfg`.

La cause est à `room/main.c:1359` :

```c
const int win_w = (opt.width  != 1600) ? opt.width  : ns_config_get_int(NS_CFG_WINDOW_W, 1600);
const int win_h = (opt.height != 900)  ? opt.height : ns_config_get_int(NS_CFG_WINDOW_H, 900);
```

« L'option a-t-elle été donnée ? » est testée en **comparant à la valeur par
défaut** au lieu de retenir si le drapeau a été passé. Demander explicitement le
défaut documenté revient donc à ne rien demander. Et comme les deux axes
décident **séparément**, on obtient des cadrages que personne n'a demandés :
mesuré, `--width=1600 --height=1080` rend **2560x1080**, et
`--width=1920 --height=900` rend **1920x1080** — rapport d'image faux, en
silence.

C'est un défaut de recette : toute capture prise à 1600x900, la définition par
défaut annoncée par `--help`, sort à une autre taille dès qu'un `settings.cfg`
existe.

### 2. Le paquet n'est pas signé

`Signature=adhoc`. macOS et Windows avertiront au premier lancement. Les
certificats appartiennent au propriétaire : **achat et décision**, pas travail.

### 3. La page COMMANDES ignore la manette

La manette marche, et **le jeu ne le dit nulle part**. La page `COMMANDES` ne
liste que le clavier et la souris. Le commentaire qui garde cette page
(`room/room_credits.c:80-84`) affirme encore, noir sur blanc, qu'il n'y a pas de
manette parce que « `SDL_Init` ne demande pas `SDL_INIT_GAMEPAD` » — ce qui
était vrai à l'écriture et ne l'est plus depuis `room/main.c:1328`.

Le commentaire disait : « une page d'aide qui annonce une commande qui n'existe
pas est pire que pas de page du tout ». Le défaut est exactement l'inverse, et
il coûte autant : un acheteur qui branche une manette n'a **aucun moyen depuis
le jeu** d'apprendre qu'elle marche.

Il manque aussi au pad le **saut**, l'**accroupi** et la **course**, et sa table
n'est pas réglable.

### 4. Un lieu de la salle est refusé au chargement, à chaque démarrage

Le paquet livré écrit un avertissement au démarrage :

```
WARN ns_scene.c:526 — lieu « monnayeur » de nature inconnue (« monnayeur ») : ignoré
```

`salle.scene.json` déclare **7 lieux**, le moteur en charge **6**.
`ns_poi_kind_from_name` (`engine/scene/ns_scene.c:88`) connaît quinze mots,
`monnayeur` n'en fait pas partie. Le monnayeur est un poste déclaré non fait
dans `salle.room.json` ; ce qui n'est pas voulu, c'est qu'un paquet vendu crie
un avertissement à chaque lancement.

### 5. Le personnage n'a qu'un seul cycle d'animation

2,00 s de marche, et le jeu le dit lui-même au démarrage. La cadence suit
l'allure, l'accroupi et le balancement d'arrêt en sont **dérivés** — l'accroupi
est calé à 67,6° de cuisse, genou 135,2°, pieds recalés à 1e-4 près, ce qui
règle le défaut de 16.0.0 où il restait debout à l'écran. Mais il n'y a
toujours **ni course, ni saut, ni animation de repos** distinctes.

### 6. Le déterminisme n'est mesuré que sur un système

Deux architectures — `x86_64` et `arm64`, dans le même binaire universel — mais
**un seul macOS**. `tests/check_determinism.cmake` rejoue chaque journal dans
deux processus séparés, ce qui attrape les états qui dépendent d'une adresse ;
ça n'attrape pas une différence de bibliothèque mathématique entre systèmes. Ni
Windows ni Linux n'ont été mesurés.

### 7. Cinq des neuf contrôles de l'audit ne sont pas écrits

Comptés dans `tools/roomgen.c` : C-01 `check_inside_shell`,
C-02 `check_solid_overlaps`, C-03 `check_grounded`,
C-09 `check_cabinet_clearance`. Absents : **C-04 `check_facing`,
C-05 `check_panel_visible`, C-06 `check_hanging`, C-07 `check_light_has_body`,
C-08 `check_floor_material`**. `check_grounded` le dit lui-même en commentaire
pour C-06.

### 8. `ASTEROID` reste un risque de marque

Nom commun, à une lettre d'*Asteroids* (Atari). Les quatre autres titres ont été
examinés et gardés : SNAKE et SHOOTER sont des noms de genre, DEMINEUR et PIANO
des noms communs. **Décision du propriétaire**, pas travail.

### 9. Vingt-neuf images du décor n'ont toujours pas d'origine établie

Aucune n'a été identifiée comme appartenant à un tiers — ce sont des bétons, des
carrelages, des bois, des marbres. Mais « pas identifié » n'est pas « établi »,
et `legacy/` ne contient aucun document de licence. Le risque a été divisé par
deux ; il n'est pas éteint.

### 10. `legacy/` reste dans le dépôt

Les images sont sorties du **paquet**, pas du **dépôt** : `legacy/` porte
**685 images**, dont les huit affiches, les personnages de Namco, l'oiseau de
*Flappy Bird* et le tapis au Pac-Man. Ouvrir le dépôt les publierait. Même
question, en plus petit, pour la douzaine de `docs/render-*.png` qui montrent
l'état d'avant.

### 11. Douze défauts de placement mineurs

Ouverts depuis 16.0.0, listés dans `docs/AUDIT-PLACEMENT.md`. **Ils n'ont pas
été re-mesurés ici**, et aucun contrôle de build ne les attrape.

### 12. La découverte d'adversaire n'existe pas, et le relais ne parle pas TLS

L'identifiant de duel se convient hors du jeu.

### 13. Il n'y a pas de page de téléchargement

C'est le troisième endroit où CC BY 4.0 demande l'attribution. Les deux autres —
l'écran de crédits, l'archive — sont tenus.

---

## Ce qui reste entre ce paquet et une mise en vente

### Ce qui demande du TRAVAIL

1. **Réparer `--width`/`--height`** (point 1). Une heure : un drapeau
   « donné / pas donné » à la place de la comparaison au défaut. C'est le seul
   défaut de cette liste qui fausse *les mesures des autres recettes*.
2. **Une ligne MANETTE dans la page COMMANDES** (point 3), et corriger le
   commentaire de `room_credits.c` qui affirme le contraire. Une demi-journée
   avec le saut, l'accroupi et la course au pad.
3. **Faire taire l'avertissement `monnayeur`** (point 4) : soit le moteur
   connaît le mot, soit la salle ne le déclare pas.
4. **Écrire C-04 à C-08** (point 7), et re-mesurer les douze défauts mineurs.
5. **Mesurer le déterminisme sur Windows et Linux** (point 6) — la chaîne CI
   existe, le contrôle est déjà écrit.
6. **Un deuxième cycle d'animation** (point 5) : course et repos.
7. **Une page de téléchargement** portant l'attribution CesiumMan (point 13).

### Ce qui demande une DÉCISION ou un ACHAT du propriétaire

1. **Acheter les certificats** Developer ID (Apple) et Authenticode (Windows),
   et brancher la signature dans CPack — sans quoi les deux systèmes avertissent
   au premier lancement. **C'est le seul point qui bloque une vente au grand
   public.**
2. **Trancher `ASTEROID`** : renommer, ou assumer le risque par écrit.
3. **Décider du sort de `legacy/`** avant d'ouvrir le dépôt : le purger, ou
   garder le dépôt privé.
4. **Décider quoi faire des 29 images sans origine** : les remplacer par du
   dessin ou du CC0 comme les 21 autres, ou assumer le risque par écrit.
5. **Décider si le relais chiffre** (TLS) avant d'ouvrir les duels au public.

**En l'état, le jeu est jouable, complet et déballable sans erreur bloquante.
Ce qui empêche la vente n'est plus le contenu — c'est la signature (achat) et
deux décisions de marque et de dépôt.**
