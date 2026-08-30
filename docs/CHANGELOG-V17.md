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

### Fait depuis la rédaction de cette liste

Les trois premiers points ci-dessous ont été traités APRÈS que cette recette les
ait relevés. Ils restent écrits parce qu'un changelog qui efface ses propres
constats ne dit plus comment on y est arrivé.

- **`--width`/`--height` réparés.** La correction précédente avait redressé le
  rendu hors écran et laissé la LECTURE des options, qui demandait « l'option
  a-t-elle été donnée ? » en comparant à la valeur par défaut : `--width=1600`
  — la valeur que `--help` annonce — était donc prise pour une absence. Zéro sert
  désormais de sentinelle, comme `render_scale` le faisait déjà deux lignes plus
  bas. Vérifié sur quatre définitions, RHI et PNG.
- **La page COMMANDES annonce la manette**, avec le saut, l'accroupi et la
  course qui lui manquaient (face gauche, face droite, gâchette d'épaule). Le
  test qui exigeait l'ABSENCE de cette ligne exige maintenant sa présence : la
  règle n'a pas changé, elle s'est retournée.
- **L'avertissement `monnayeur` est tu** : le moteur connaît le mot. Au passage,
  il est écrit dans l'en-tête que **rien ne lit la liste des lieux** — pour que
  le prochain ne croie pas qu'elle pilote déjà quelque chose.
- **Snake, piano et asteroid repris.** Le pilote automatique de Snake ne
  connaissait pas son propre corps : la courbe mesurée décrivait le pilote, pas
  le jeu. Quatre règles de 2020 étaient mal lues, dont une invincibilité de
  départ qui supprimait les murs de l'arène pendant huit secondes, et un `6` lu
  comme des secondes au lieu d'une chance par image — un facteur trente. Piano
  reposait sur une série géométrique **convergente** : son morceau infini se
  consommait en 3 min 40, après quoi la vitesse atteignait 207 949. Asteroid
  tirait aux dés deux règles que 2020 déduisait de l'horloge.

### Ce qui demande du TRAVAIL

1. **Écrire C-04 à C-08**, et re-mesurer les douze défauts mineurs.
2. **Mesurer le déterminisme sur Windows et Linux** — la chaîne CI existe, le
   contrôle est déjà écrit.
3. **Un deuxième cycle d'animation** : s'asseoir, ramasser, être poussé. Un
   cycle ne se fabrique pas par arithmétique — l'accroupi, lui, est fait.
4. **Une page de téléchargement** portant l'attribution CesiumMan.
5. **Un quatrième crochet de son** pour les mini-jeux : l'interface n'en offre
   que trois, et Snake a cinq sons de potion et de bombe que le portage n'a
   jamais pu brancher. Les sons existent, les événements aussi.

### Ce qui demande une DÉCISION ou un ACHAT du propriétaire

## Ce qui a été ajouté APRÈS cette recette

Cette section est datée, et elle le dit : la recette ci-dessus a été faite, puis
le travail a continué. Ce qui suit n'a donc pas traversé la même recette
paragraphe par paragraphe — mais l'**archive a été reconstruite et revérifiée**
en entier (déballée ailleurs, lancée depuis ailleurs, zéro message au démarrage,
les huit jeux marquent), et chaque chiffre cité ici est mesuré.

### La salle a une économie

**JETON → PARTIE → TICKETS → LOT.** Le monnayeur complète gratuitement jusqu'à
cinq jetons, une partie en coûte un, une partie finie rend des tickets, et la
vitrine les échange contre quatre lots. Le barème est mesuré : 24 parties par
jeu **et par régime**, les seize médianes rendant 9 ou 10 tickets.

**Il n'existe pas de coefficient unique pour le mode difficile** — le rapport
dur/normal va de 0,011 pour aplomb à 45,7 pour snake, dont le régime dur
*inverse* la règle de score. Seize lignes, donc, pas huit.

Le test a chiffré un défaut de conception avant qu'il ne sorte : le monnayeur
changeait d'abord les tickets en jetons, ce qui faisait du sur-place exact —
vingt parties médianes rendaient 57 tickets au lieu de 200, pour un premier lot
à 60. **La vitrine était inatteignable pour toujours, en silence.**

Aucune monnaie réelle, aucune voie d'achat, aucun coffre aléatoire. Le
quitte-ou-double annonce le **risque** avant le gain, et ne rien faire vaut
refus.

### Les autres joueurs ont un corps

Ils n'étaient qu'une étiquette flottante à 3,40 m du sol, sous un plafond à
2,92. Ils marchent : maillage articulé, cap publié ou déduit du déplacement,
**phase de marche tirée de la distance parcourue et non du temps** — c'est ce
qui empêche les pieds de patiner. Seize corps coûtent **0,65 ms sur une image de
24,3**, soit 41 µs par corps.

Le retard d'interpolation vaut **250 ms**, une période de publication, soit
35 cm à 1,4 m/s : c'est ce chiffre qui décide qu'un pair n'est pas un obstacle.

### Quatre défauts visuels, mesurés

| | |
|---|---|
| **Les murs** — 197 m², la plus grande surface verticale | la photo de 2020 avait une tache dominante de 12 à 14 mm là où un crépi en fait 1 à 3, et un écart-type de luminance de 32,3 sur 255, c'est-à-dire une **ombre peinte dans l'albédo**. Redessinée par `tools/murart` : spectre borné à quatre texels, écart-type 7,2, **réflectance identique sur les trois canaux à la quatrième décimale** |
| **Les ombres** | la cible d'ombres par lumière est entière, donc non interpolable, donc lue au plus proche : chaque bord sortait en escalier de trois pixels **au palier que `--help` annonce « superbe en capture »**. Filtrées par indice apparié, **20,5 ms avant, 20,5 ms après** |
| **Le dos des bornes** | les deux rangées de l'îlot sont dos à dos à 32 cm et on longe cet intervalle en entrant : 0,72 × 1,60 m de peinture sans un accident. Trappe de service, aération, serrures, embase de cordon |
| **L'écran du bar** | un panneau nu flottant à **32,7 cm du parement**, dont le bord haut passait devant la tablette de l'enseigne. C'est un meuble : patte, caisson, cadre, dalle |

### Le site montre le jeu

Vidéos d'accueil et de chacun des huit jeux, dix-neuf photos de bornes, huit
vues de salle — **64 fichiers, 7,66 Mio, 223 s de moteur**, tout produit par
`tools/site-media.py` et rien d'importé. Le serveur annonçait `15.0.0` contre un
projet en 17.0.0 : il **fabriquait des liens vers des paquets qui n'ont jamais
existé**.

Et il en fabriquait encore. Mesuré contre l'API GitHub : le dépôt répond 200,
`releases/tags/v17.0.0` répond 404, la liste des releases est vide. **Les trois
boutons « Télécharger » étaient trois 404.** Ils disent « Bientôt » tant que
`NINETEEN_RELEASE_PUBLIEE` n'est pas posé, avec la raison — la signature.

### Ce qui portait encore une marque

`tetris_font.jpg` : contenu propre — c'est l'écran d'aide du jeu de 2020, le mot
n'y figure pas — mais **la marque était dans le nom du fichier** et dans le nom
de matériau que `salle.gltf` en tirait. Recopiée sous `ecran_aplomb_repos.jpg`,
à l'octet près. Le piège était ailleurs : après le renommage,
`materials/tetris_font_n.nstex` restait dans le paquet — la purge ne connaissait
que les images *retirées*, pas les *renommées*.

Le mode démonstration nommait aussi les jeux à la main : « TETRIS », « PONG ».
Il lit le registre maintenant, qui ne peut contenir que ce que la salle porte.

**Mesuré sur l'archive : plus aucun nom de fichier ne contient TETRIS, PAC-MAN,
FLAPPY ni PONG, et `salle.gltf` n'en a aucune occurrence.**

### Trois défauts de réglage, tous de la même famille

Un **état de session confondu avec une préférence**. `--width=` était pris pour
une absence d'option parce qu'on le comparait à son défaut ; corrigé, il
**écrivait** ensuite dans `settings.cfg`, si bien qu'une seule capture lancée en
1920×900 remplaçait la définition du joueur. Et `--autoplay`, qui n'a pas le
droit de se classer, **ouvrait quand même un dossier sur le serveur**.

## Un second lot après cette recette

### L'écran du bar était rendu comme un tube d'arcade

Le moteur savait dire « ceci est un écran » et rien de plus, et il en tirait un
seul traitement : celui du **tube** — courbure en barillet, lignes de balayage,
masque de phosphore, coins assombris. Juste pour dix-neuf bornes de 1985, faux
pour le téléviseur du bar, qui le recevait quand même. Le défaut était **écrit**
à côté du champ `scoreboard_material` — « celles-ci reçoivent le traitement de
tube, celui-là non » — pendant que `ns_render.c` posait `is_screen = live_screen`
et le lui donnait.

Mesuré sur les deux filets horizontaux du tableau, qui sont **droits** dans
l'image dessinée, capture 1600 × 900 à 1,60 m :

| | avant | après |
|---|---|---|
| flèche du filet haut | −3,53 px | **0,00 px** |
| flèche du filet bas | +5,69 px | **+0,65 px** |

Le masque de phosphore comptait 640 colonnes sur 1,70 m, soit **1,34 px** de
période à ce cadrage ; les lignes de balayage, **0,90 px**. Tous deux sous la
limite d'échantillonnage : ce qu'ils produisaient n'était pas un grain de tube,
c'était du moiré.

Un matériau déclare désormais son **espèce** — `"ecran": "tube"` ou `"plat"`. Sans
déclaration le moteur retombe sur l'ancienne déduction, qui vaut tube : la salle
de 2020 ne bouge pas d'un texel. Ce que la dalle plate a en propre : rugosité
0,035 au lieu de 0,06, **métal à zéro** là où le tube force 0,35 (un artifice qui
teinte le reflet de la couleur de la dalle ; une glace est un diélectrique),
albédo résiduel 2 % au lieu de 6 %, et échantillonnage **linéaire** au lieu de
`nearest`.

Le meuble suit : bordure de cadre 30 → **10 mm**, dalle 1,70 × 0,85 →
**1,78 × 0,89** (1,99 m de diagonale), définition 753 → **1438 texels/m**, soit
1,50 fois une dalle de borne contre 0,78 avant.

### Le grand écran du bar était le seul texte illisible de la pièce

Le défaut était écrit dans `room/main.c` sans être corrigé : « c'est la **taille
du lettrage sur le panneau** qu'il faudrait revoir, pas le nombre de texels ».
Depuis le comptoir, l'enseigne au néon **au-dessus** du panneau se lit, la petite
borne de classement à trois mètres à droite se lit, et le grand écran entre les
deux était une bouillie grise.

Profil de luminance sur la vue « bar » en 1400 × 875 :

| | avant | après |
|---|---|---|
| titre / bandeau | 10 px | **24 px** |
| lignes de données | **inséparables** — une seule bande claire de 112 px, et elles le restent en montant le seuil | **15, 16, 15, 15 px**, séparées par 12 à 13 px de noir |

Du lettrage deux fois plus haut, c'est quatre fois moins de lignes : le panneau
**tourne** — deux pages de quatre records, entrecoupées de la page « en direct ».
Et le noir est vrai : le fond passe de (0,020 ; 0,026 ; 0,045) à
(0,003 ; 0,004 ; 0,008). Ce que le panneau rend à la salle, il le rend par son
bandeau et par ses chiffres.

### On jouait sur 8,4 % du cadre

Debout sur l'ancre que la salle déclare, l'œil est à 0,870 m du centre de la
dalle, qui fait 0,5333 × 0,300 m : elle occupait **8,4 % de l'aire du cadre**.
`room/room_poste.{c,h}` avance le **point de vue** vers elle quand une partie
tourne dedans — le corps ne bouge pas, c'est le partage que la troisième personne
a établi, pris dans l'autre sens.

Deux leviers, et il en faut deux : pour que la dalle tienne 58 % de la hauteur à
62° de champ, il faudrait mettre l'œil à 0,455 m du verre, c'est-à-dire la tête
dans le meuble. On avance de 0,277 m **et** on resserre le champ à 46°. Résultat
mesuré en projetant les quatre coins de la dalle : **31,2 % de l'aire**, soit 3,7
fois plus. Les deux nombres sont dans `nineteen.env` avec la table des cinq
réglages essayés — c'est un arbitrage de mise en scène, il se règle sans
recompiler.

### Le panneau qui dit HARD et EASY ne disait rien

Il pend au-dessus de l'allée centrale et c'est le **seul** signal spatial de la
règle de difficulté. On y lisait deux rectangles vides, dans des couleurs
qu'aucun spectateur ne peut relier à une consigne. La dette était écrite — « les
glyphes de 2020 sont de la géométrie 3D que cette description ne sait pas
produire : ils sont ici deux bandeaux de couleur inversée, **qui portent la même
lecture** » — et c'était la seule affirmation vérifiable du bloc. La capture l'a
réfutée.

La description ne sait pas produire de glyphes ; le dépôt, si. Même outil et même
recette que le bloc de secours du sas : `marqueeart` dessine une plaque
rétroéclairée avec le mot en réserve, dans la fonte du jeu. Lettrage mesuré sur
la vue « allee » en 1400 × 875 : **EASY 14 px, HARD 19 px**, pour un seuil de
lisibilité relevé à 11.

### Les appliques éclairaient l'intérieur d'un pilier

Le modèle est une applique **double** — son maillage se sépare en deux grappes de
1524 et 1526 sommets. La description ne posait qu'**une** bille, à l'origine du
meuble, c'est-à-dire pile dans le vide entre les deux cages et 16 cm sous leur
centre. Les cages restaient noires et une boule blanche flottait devant la
platine.

Trois corrections, et la première a révélé la troisième :

- **deux ampoules**, une par cage, à la cote de la plus grande **sphère
  inscrite** — on balaie l'intérieur au demi-millimètre et l'on retient le point
  qui maximise la distance au sommet le plus proche : (±0,2160 ; +0,1350 ;
  +0,2650), rayon libre 0,0603 m. Le centre de la boîte englobante est à 3,9 cm
  de là et le contrôle de placement l'a refusé ;
- **une ampoule est du verre.** Elle portait `painted_panel.jpg` pavé à 20 cm :
  sur une bille de 9 cm on n'en voyait qu'un fragment étiré, et `texgen` en avait
  tiré une carte de normales qui le mettait en relief — une croûte de chou-fleur
  de 12,6 cm mesurée sur capture, pour un objet qui en déclare 9 ;
- **les quatre appliques du mur sud étaient montées à l'envers**, cages pointant
  dans le pilier. Le contrôle ne pouvait pas le voir tant que l'ampoule tombait
  devant la platine ; mise là où elle doit être, la pénétration est devenue
  mesurable et le build s'est arrêté dessus.

### Trois lots livrés par agents, mesurés et fusionnés

**De vrais installateurs.** `.dmg` avec `Nineteen.app` glissable (177 Mo,
universel arm64 + x86_64) — monté, copié dans `/Applications`, **arbre de build
masqué**, lancé : il charge ses assets depuis `Contents/Resources`. AppImage
(164 Mio), `.deb` et `.tar.gz` construits dans un conteneur Ubuntu 22.04 (glibc
2.35) ; le `.deb` s'installe par `apt` dans un conteneur nu, ne déclare que
`libc6 (>= 2.34)`, et le binaire tourne depuis le `PATH` hors de tout arbre de
build. L'installateur Windows NSIS est configuré et vérifié par un test
(`paquets`) qui relit la configuration générateur par générateur — **rien n'a été
exécuté sous Windows**, faute de makensis, de mingw et de wine ; c'est la CI qui
le produira. Deux défauts trouvés en chemin : le jeu ne compilait pas sur
glibc < 2.39 (`-std=c11` pose `__STRICT_ANSI__`, qui cache `getaddrinfo`), et les
conditions de signature du workflow étaient toujours fausses.

**L'URL du serveur à la compilation.** Préséance écrite, documentée et
vérifiée : `défaut compilé < config < environnement < --server=`, avec `--offline`
qui verrouille par-dessus. Le journal **dit d'où vient l'adresse**. Le défaut
CMake reste vide et c'est vérifié : `strings` ne trouve aucune URL dans le
binaire livré et le jeu redit « aucun serveur configuré ». Le jeton, lui, est
refusé à la compilation par écrit — `strings` le rendrait chez quiconque a
téléchargé le paquet, et il serait le même pour tous.

**Frapper une borne.** Touche `F`, en partie comme hors partie. Geste en
première et troisième personne, contrecoup de caméra, son synthétisé par
`tools/stepgen`, et l'image de la dalle qui déraille. Durées mesurées : armé
110 ms, aller 90 ms à **5,5 m/s** (un vrai coup de poing va de 5 à 9 m/s à
l'impact), retour 300 ms. Le coup coûte ce qu'il doit coûter : pendant 500 ms la
main est sur la machine et non sur les boutons, les commandes sont perdues et la
partie continue.


---

1. **Acheter les certificats** Developer ID (Apple) et Authenticode (Windows).
   La signature est branchée et attend un secret ; sans elle macOS met en
   quarantaine et Windows affiche SmartScreen. **C'est le seul point qui bloque
   une vente au grand public.**
2. **Publier une release et poser `NINETEEN_RELEASE_PUBLIEE=1`.** Le dépôt n'en
   a aucune ; les trois boutons du site disent « Bientôt » et le resteront tant
   que rien n'est publié. Les paquets, eux, sont prêts : un `.dmg` glissable,
   une AppImage, un `.deb`, une archive, et l'installateur Windows que la CI
   produit.
3. **Trancher `ASTEROID`** : renommer, ou assumer le risque par écrit.
4. **Décider du sort de `legacy/`** avant d'ouvrir le dépôt : le purger, ou
   garder le dépôt privé.
5. **Décider quoi faire des 27 images sans origine** : les remplacer par du
   dessin ou du CC0 comme les 21 autres, ou assumer le risque par écrit.
6. **Décider si le relais chiffre** (TLS) avant d'ouvrir les duels au public.

**En l'état, le jeu est jouable, complet, et il s'INSTALLE : trois formats
vérifiés en les lançant depuis l'installation, arbre de build masqué. Ce qui
empêche la vente n'est plus le contenu ni l'empaquetage — c'est la signature
(un achat), une release à publier, et deux décisions de marque et de dépôt.**
