# Nineteen 17.0.0 — le jeu se présente, et dit d'où il vient

16.0.0 avait rendu la salle juste. Cette version-ci s'occupe de ce qui sépare un
projet d'un produit : ce que le joueur comprend en arrivant, et ce que le
distributeur a le droit de distribuer. Comme la précédente, chaque affirmation
porte sa mesure, et ce qui n'est pas tenu est dit à la fin plutôt que passé sous
silence.

## Ce qui change en une phrase

Le jeu **dit enfin ce qu'il est** — écran de crédits, page de commandes, aide
d'arrivée — et l'inventaire de licences nomme pour la première fois **les huit
affiches qui interdisent de le vendre**.

---

## La liste de 16.0.0, reprise point par point

| Ce que 16.0.0 disait non tenu | Mesuré aujourd'hui, et comment | État |
|---|---|---|
| **Le paquet n'est pas signé** | inchangé : `CMakeLists.txt` produit des archives non signées et le dit. Les certificats appartiennent au propriétaire | **toujours non tenu** — décision et achat |
| **Trois critères d'éclairage** non atteints, « `sud` à 14,4 et `travee` à 16,3 pour ≥ 30 » | `--view=` sur les trois cadrages, `--quality=high`, 4 images, médiane lue dans le journal : **`centre` 39 ✓, `travee` 27 ✗, `sud` 14 ✗** | **partiellement tenu** : `travee` passe de 16,3 à 27, `centre` passe ; `sud` n'a pas bougé |
| **Douze défauts de placement mineurs** ouverts | les **bloquants** sont fermés et gardés : roomgen confronte **108 objets** au parement intérieur, **0 toléré**, et **36 paires de boîtes** dont **0 défaut connu non corrigé**. Les mineurs (P-16 à P-23) n'ont **pas** été re-mesurés un par un ici | **tenu pour les bloquants, non vérifié pour les mineurs** |
| **Cinq des neuf contrôles** de l'audit ne sont pas écrits | compté dans `tools/roomgen.c` : C-01 `check_inside_shell` ✓, C-02 `check_solid_overlaps` ✓, C-03 `check_grounded` ✓, C-09 `check_cabinet_clearance` ✓ — **C-04 à C-08 absents**. `check_grounded` le dit lui-même en commentaire pour C-06 | **toujours non tenu, cinq sur neuf** |
| **Déterminisme** sur deux architectures d'un seul système | `lipo -info` : le binaire porte `x86_64 arm64`. `tests/check_determinism.cmake` rejoue chaque journal dans **deux processus** — même machine, même système | **toujours non tenu** : un seul système |
| **Découverte d'adversaire** inexistante, relais sans TLS | rien n'a changé : l'identifiant de duel se convient hors du jeu | **toujours non tenu** |

Deux chiffres de 16.0.0 ont bougé sans qu'on les ait promis : `nineteen.env`
passe de **19 à 30 clés**, toutes consommées — le jeu le vérifie au démarrage et
nomme dans le journal celle que personne ne lirait — et la suite de tests de
**36 à 39**.

---

## Ce que 17.0.0 ajoute

### L'écran de crédits — une obligation, pas une finition

`assets/cc0/LICENSES.md` dit depuis le début que l'attribution de **CesiumMan**
(© 2017 Cesium, **CC BY 4.0**) est obligatoire et « doit l'être aussi partout où
le jeu est distribué — écran de crédits, page de téléchargement, archive ».

Vérifié : `grep -rin credit room/ engine/ games/` ne rendait **rien**. L'archive
était tenue — `room/CMakeLists.txt` y installe le fichier — l'écran ne l'était
pas. Un Markdown posé à côté d'un binaire n'est pas un écran de crédits.

Il existe maintenant : `Échap` → `CREDITS`. Il porte les quatre choses que
CC BY 4.0 demande nommément — l'œuvre, l'auteur, la licence, le lien vers son
texte — plus les cinq bibliothèques tierces liées dans le binaire, dont **cgltf
et jsmn, sous MIT, dont la licence exige la mention pour un binaire distribué**.

Et il est **défendu**. Les crédits sont une table publique
(`room/room_credits.c`), pas une suite d'appels de dessin : `tests/test_menu.c`
vérifie que « CESIUMMAN », « CESIUM », « CC BY 4.0 » et
« CREATIVECOMMONS.ORG » y sont, et qu'une ligne du menu ouvre la page. Effacer
l'un des quatre fait échouer le test `menu`, donc le build. Une mention légale
que rien ne défend finit par disparaître dans un nettoyage.

### La première minute

Lancé et regardé en joueur qui ne connaît rien : on démarre dans le sas, on voit
deux mains, et **l'écran ne porte pas un mot**. Ni titre, ni commande, ni
indication. Les touches n'étaient dites que dans `--help` et `docs/JOUER.md` —
c'est-à-dire nulle part pour qui a téléchargé un paquet et double-cliqué dessus.

Trois choses, dans l'ordre où on en a besoin :

* un **bandeau d'arrivée** en bas de l'écran pendant 14 secondes — quatre
  commandes, pas quinze : se déplacer, regarder, jouer, `Échap`. Quatorze
  secondes parce que le sas fait neuf mètres et qu'une aide disparue avant
  qu'on arrive dans la salle n'a aidé personne. Il se tait dès qu'on est devant
  une borne, où l'invite « E — JOUER À … » prend le relais ;
* une page **COMMANDES** dans `Échap`, permanente ;
* la page **CREDITS**, juste au-dessous.

### Deux défauts trouvés en regardant les captures, et corrigés

* **Le menu débordait de son propre cadre.** À seize lignes, la formule
  `78 + 16 × 38 + 74 = 760` dépassait les 700 disponibles : le cadre était
  raboté, les lignes ne l'étaient pas, et « QUITTER LE JEU » s'écrivait par
  dessus « FLECHES CHOISIR ET REGLER ». Le commentaire d'origine affirmait que
  la formule « désamorce le piège une fois pour toutes » — elle ne le
  désamorçait que tant que le rabot ne servait pas. C'est le pas des lignes qui
  cède maintenant, et le calcul se fait sur `MI_COUNT` au dessin.
* **Le bandeau d'arrivée dépassait sous le menu**, l'affichage étant dessiné
  avant lui.

Les deux se voyaient sur capture et ne se voyaient pas dans le code.

### `--menu=N` ouvre vraiment la page

`--menu=13` cadrait la ligne « CREDITS » sans jamais montrer les crédits :
l'écran qui porte une obligation de licence était le seul du jeu qu'on ne
pouvait pas capturer en ligne de commande, donc le seul qu'on ne pouvait pas
vérifier sans le jouer à la main. Les deux pages d'information s'ouvrent
maintenant, visées par leur **libellé** et non par un indice de ligne.

### `docs/JOUER.md` disait trois choses fausses

C'est le document **livré dans le paquet**. Il annonçait :

| Ce qu'il disait | Ce qui est mesuré |
|---|---|
| « le binaire ne contient pas le code nécessaire pour ouvrir une connexion » | `nm -u` : **huit symboles de l'API socket** importés — `_socket`, `_connect`, `_getaddrinfo`, `_recv`, `_send`, `_select`, `_setsockopt`, `_close` |
| « `engine/net/` est un répertoire vide » | **3 331 lignes** de C dans six modules |
| « `NS_CFG_SERVER_URL` … rien ne la lit » | `ns_config.h` explique lui-même qu'elle est revenue **parce qu'elle a un lecteur** |
| « Deux jeux sont portés : Flappy Bird et Snake » | **huit**, et les six autres sont décrits vingt lignes plus bas dans le même document |

Le document se contredisait donc lui-même, et le paragraphe rassurant a survécu
deux versions parce qu'il rassurait. Corrigé, avec la mesure à la place de
l'affirmation, et la mention explicite de ce qui a été faux — un document qui
efface ses erreurs demande qu'on le croie sur parole une seconde fois.

---

## Ce qui a été trouvé, et qui n'est pas réparable par du code

C'est le vrai résultat de cette version, et ce n'est pas un ajout : c'est un
inventaire, fait en **ouvrant les fichiers** plutôt qu'en lisant ce qui était
écrit à leur sujet.

`assets/cc0/LICENSES.md` documentait soigneusement les **2,6 Mio rapportés**
(Poly Haven, CC0) et laissait croire par omission que le reste était réglé. Le
paquet installé porte **93 images** dans `bin/assets/scene/textures/`, comptées
une par une : **19** viennent de Poly Haven, **14** sont générées au build, **2**
sont orphelines — aucune source dans l'arbre, aucun matériau qui les référence —
et **58 sont les images de 2020, sans une ligne d'origine ni de licence nulle
part**. `legacy/` ne contient aucun document de licence. Le total bouge pendant
que la direction artistique remplace des enseignes ; **le 58 ne bouge pas**.

Huit d'entre elles ont été ouvertes et regardées :

| Fichier | Ce que c'est | Ayant droit apparent |
|---|---|---|
| `poster_7.jpg` | le flyer d'arcade **PAC-MAN**, logo Midway compris | Bandai Namco / Midway |
| `poster_8.jpg` | le flyer d'arcade **DONKEY KONG**, logo Nintendo compris | Nintendo |
| `poster_3.jpg` | le flyer **ATARI « Video Pinball »** | Atari |
| `poster_6.jpg` | l'affiche **« Palace Arcade — Hawkins »** de *Stranger Things* | Netflix |
| `poster_5.jpg` | une illustration de la gamme **« Arcade »** de *League of Legends* | Riot Games |
| `poster_2.jpg` | **« Space Paranoids — ENCOM »**, l'arcade fictive de *Tron* | Disney |
| `poster_1.jpg` | affiche de festival « Arcade Armageddon », graphisme d'auteur | inconnu |
| `poster_4.jpg` | illustration de « gaming room » retitrée NINE 19 TEEN | inconnu |

Elles étaient **accrochées aux murs de la salle et visibles en jeu** ; elles ne
sont plus copiées. Le même examen, poussé jusqu'aux planches des jeux et au sol,
a donné treize images de plus — les personnages de Namco dans `games/pacman/`,
la planche d'oiseaux de *Flappy Bird*, et le tapis du hall. Toutes retirées,
toutes remplacées par du dessin.

**Une attribution ne rattrape pas ces fichiers.** Ce ne sont pas des œuvres sous
licence libre mal créditées ; ce sont des œuvres sous droit exclusif employées
sans droit. Le détail complet, avec les cas plus petits — `sega.ttf` dans le
serveur web, deux images orphelines dans le paquet — est dans
`assets/cc0/LICENSES.md`, section « Les images de 2020 : ce qui n'est PAS
établi ».

**Rien n'a été retiré.** `salle.room.json` appartient à la direction artistique,
et remplacer huit affiches est une décision de contenu.

---

## La manette : ce que ça coûterait, mesuré plutôt qu'estimé au doigt

Il n'y en a pas. `SDL_Init` demande `VIDEO | EVENTS | AUDIO` et **pas**
`SDL_INIT_GAMEPAD` ; aucun `SDL_EVENT_GAMEPAD_*` n'apparaît dans le dépôt. Une
manette branchée est vue par le système — `_GCControllerDidConnectNotification`
est bien dans les symboles importés, via SDL — et ignorée par le jeu.

Les points à toucher ont été **comptés**, pas devinés : dix sites, tous dans
`room/main.c`.

| Site | Lignes | Ce qu'il faut |
|---|---|---|
| `SDL_Init` | 1309 | ajouter `SDL_INIT_GAMEPAD` |
| ouverture / fermeture | — | `SDL_EVENT_GAMEPAD_ADDED` / `REMOVED`, `SDL_OpenGamepad` |
| déplacement | 2670-2674 | axes gauche → `input_forward` / `input_strafe`, avec zone morte |
| regard | 2608 | axe droit → l'accumulateur de la souris, à sensibilité propre |
| accroupi / saut | 2706-2709 | deux boutons |
| interaction `E` | 2523 | un bouton |
| menu | 2364-2383 | croix + A/B → les six `ROOM_MENU_*` |
| jeu, appuis | 2405-2445 | croix + A → `game_api->press` |
| jeu, maintiens | 2882-2886 | croix + A → `held[]` |
| **journal d'entrées** | **2904-2908** | **le même masque `hmask`** |

La dernière ligne est celle qui compte, et c'est elle qu'un devis rapide
oublierait. `hmask` est ce que `--journal-entrees` écrit, ce que
`tests/test_replay.c` rejoue sur les huit jeux, et ce qu'un duel en pas
verrouillé publie à l'adversaire. Une manette qui alimenterait `held[]` sans
alimenter `hmask` rendrait **toute partie jouée à la manette non reproductible**
et **ferait diverger les duels** — sans erreur, sans message, avec pour seul
symptôme deux scores qui se contredisent.

Estimation : **une demi-journée pour que ça marche, une journée pour que ce soit
juste**, plus une table de correspondance rendue réglable (`nineteen.env`) et un
test qui vérifie que les deux chemins d'entrée produisent le même `hmask`.

**Ce n'est pas fait, et délibérément :** il n'y a pas de manette branchée à cette
machine. Livrer du code d'entrée qu'on n'a pas pu essayer, dans le fichier le
plus sollicité du dépôt, contredirait la règle que ce projet tient depuis le
début — « je refuse de livrer une salle que je ne peux pas regarder ».

---

## La recette

| | |
|---|---|
| la suite de tests | **39 / 39** |
| le compilateur | **0 avertissement** |
| `nineteen.env` | **30 clés, toutes consommées**, vérifié au démarrage |
| roomgen, objets confrontés au parement | **108, dont 0 toléré** |
| roomgen, paires de boîtes | **36, dont 0 défaut connu non corrigé** |
| roomgen, couloir devant les bornes | **0,80 m sur 19 bornes**, 0 défaut |
| accessibilité à pied | **34 cibles** atteignables depuis le départ |
| la salle | 151 objets, 158 872 triangles, 45 lumières, 19 bornes |
| le binaire | universel, `x86_64 arm64` |
| le paquet | **249 Mio déballé**, 170 Mio en `.tar.gz` |
| l'éclairage | `centre` 39, `travee` 27, `sud` 14 (pour ≥ 30 demandés) |

---

## Ce qui n'est pas tenu, et que je ne masque pas

Dans l'ordre de ce qui empêche la vente.

### 1. ~~Huit affiches et deux planches de lutins appartiennent à des tiers~~ — TENU

Les huit affiches sont dessinées par `tools/posterart`. **Treize autres images**
ont été trouvées ensuite, dans le même examen mené jusqu'au bout : les deux
planches de Pac-Man, les sept planches de *Flappy Bird* — dont une image de
banque d'images **avec le filigrane « ©123RF » encore dessus** —, les deux
captures d'attract qui les montrent, et **le tapis du hall**, qui portait au
néon un Pac-Man, un de ses fantômes, ses cerises et une manette de console. Les
treize sortent du paquet ; `tools/spriteart` et `tools/moquetteart` dessinent
les remplaçantes. Le détail, image par image, est dans `assets/cc0/LICENSES.md`.

### 2. ~~Les noms « PACMAN » et « TETRIS » sont des marques déposées~~ — TENU, et il y en avait TROIS

**PAC-MAN → DÉDALE**, **TETRIS → APLOMB**, et **FLAPPY BIRD → ENVOL**, qui
manquait à la liste : c'est le nom d'une œuvre précise de 2013, exactement le
même motif. Les cinq autres titres ont été examinés et gardés — SNAKE et SHOOTER
sont des noms de genre, DEMINEUR et PIANO des noms communs. ASTEROID est signalé
comme risque **résiduel** : c'est un nom commun, mais il est à une lettre de la
marque *Asteroids* d'Atari, et cette décision-là n'est pas technique.

Une mécanique ne s'approprie pas ; un nom et un personnage, si. Les règles de
`games/` n'ont pas bougé d'une ligne — ni le barème, ni les vitesses, ni le
vocabulaire d'événements du serveur, qui aurait invalidé tout journal déjà
scellé.

### 3. Le paquet n'est pas signé

Ni Developer ID ni Authenticode. macOS et Windows avertiront au premier
lancement. Les certificats appartiennent au propriétaire : **achat et décision**,
pas travail.

### 4. Il n'y a pas de manette

Chiffré plus haut. Pour un jeu d'arcade, c'est l'attente évidente.

### 5. Le menu ne règle ni la résolution ni le plein écran

`window.width`, `window.height` et `window.fullscreen` existent dans
`ns_config.h`, sont lus au démarrage, et **aucune ligne du menu ne les écrit** :
il faut éditer `settings.cfg` à la main. Les seize lignes du menu sont par ailleurs
complètes et compréhensibles — vérifié sur capture.

### 6. `--width` et `--height` ne font rien en `--headless` sur macOS

Mesuré : trois tailles demandées, `640x360`, `1600x900`, `2560x1440`, **trois
fois le même résultat**, `3024x1964` — la définition native de l'écran. Toutes
les captures du dépôt ont donc été prises à une définition que personne n'a
choisie, et à un coût de rendu vingt-cinq fois supérieur à ce qui était demandé
dans le pire cas. Piste : `SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY`
dans `engine/rhi/ns_rhi.c:187-195`. **Le cas fenêtre visible n'a pas pu être
mesuré** depuis cette session.

### 7. Le personnage n'a qu'un seul cycle d'animation

2,00 s de marche. La cadence suit l'allure, mais **accroupi il reste debout à
l'écran** — le jeu le dit lui-même au démarrage.

### 8. Cinq des neuf contrôles de l'audit ne sont pas écrits

C-04 `check_facing`, C-05 `check_panel_visible`, C-06 `check_hanging`,
C-07 `check_light_has_body`, C-08 `check_floor_material`.

### 9. Douze défauts de placement mineurs

Ouverts depuis 16.0.0, listés dans `docs/AUDIT-PLACEMENT.md`. **Ils n'ont pas
été re-mesurés un par un ici**, et aucun contrôle de build ne les attrape :
`check_grounded` ne couvre pas les objets suspendus, et le dit lui-même.

### 10. Deux critères d'éclairage sur trois ne sont pas atteints

`sud` à 14 et `travee` à 27, pour ≥ 30. `sud` n'a pas bougé depuis 16.0.0.

### 11. Le déterminisme n'est mesuré que sur un système

Deux architectures — `x86_64` et `arm64` — mais un seul macOS. Ni Windows ni
Linux.

### 12. La découverte d'adversaire n'existe pas, et le relais ne parle pas TLS

L'identifiant de duel se convient hors du jeu.

### 13. Il n'y a pas d'icône de fenêtre

`SDL_SetWindowIcon` n'est appelé nulle part. `packaging/nineteen.png` n'est
installé que sur les bureaux Linux, via le `.desktop`. Le titre de fenêtre, lui,
est correct : « Nineteen ».

### 14. Il n'y a pas de page de téléchargement

C'est le troisième endroit que CC BY 4.0 demande. Les deux autres — l'écran,
l'archive — sont tenus depuis cette version.
