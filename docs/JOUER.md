# Jouer — sans serveur, sans base de données, sans compte

Le jeu n'a besoin de rien d'autre que lui-même. Le serveur Go et PostgreSQL ne servent qu'au site
web et au classement en ligne ; ils sont facultatifs et le jeu ne les cherche pas.

## Ce n'est pas une promesse, c'est vérifiable

**Cette section disait le contraire jusqu'en 17.0.0, et elle avait tort.** Elle annonçait un
binaire « qui ne contient pas le code nécessaire pour ouvrir une connexion », un `engine/net/`
« répertoire vide » et un `NS_CFG_SERVER_URL` que « rien ne lit ». Les trois étaient vrais quand
ils ont été écrits. Aucun ne l'est plus, et le document se contredisait lui-même quarante lignes
plus loin, où il explique comment activer le classement en ligne. Voici la mesure d'aujourd'hui,
refaite sur le binaire livré :

```sh
$ nm -u build/macos-universal/bin/nineteen | grep -icE 'socket|connect|getaddrinfo|recv|send|ssl|tls'
12
$ wc -l engine/net/*.c | tail -1
    3331 total
```

Ce que le jeu fait vraiment du réseau, en une phrase : **il n'ouvre rien tant qu'on ne lui a pas
donné d'URL.** Sans `--server=` ni `network.serverUrl`, `ns_online` ne démarre pas son fil, et
`ns_realtime` hérite du même verrou — le jeu le dit au démarrage, en clair :

```
réseau : aucun serveur configuré, le classement restera local
temps réel : désactivé (défaut) — ni présence ni duel
```

C'est un défaut, pas une impossibilité, et c'est la formulation honnête. `--offline` en fait un
verrou : voir la section du même nom. La différence avec 2020 tient : cette version-là ne pouvait
pas atteindre sa propre fenêtre hors ligne — voir la fin de ce document.

Ce qui reste exact du paragraphe d'origine : **zéro symbole `SDL_`** importé, SDL3 est lié
statiquement, et les shaders sont compilés au build puis **embarqués dans le binaire** — il n'y
a même pas de fichier à retrouver, encore moins à télécharger. En SPIR-V pour Vulkan ; en MSL
pour Metal, traduit au build par `tools/spv2msl`.

## Prérequis

| | |
|---|---|
| CMake | 3.21 ou plus |
| Compilateur | C11 (GCC, Clang, MSVC) |
| `glslangValidator` | **obligatoire**, pour compiler les shaders |
| GPU | Vulkan (Linux, Windows) ou Metal (macOS). Sur Windows, **un pilote Vulkan est requis** : le binaire ne contient pas de DXIL, donc SDL n'y choisira pas Direct3D 12. |

Debian / Ubuntu :

```sh
sudo apt install cmake ninja-build glslang-tools libvulkan-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxkbcommon-dev libxtst-dev libxinerama-dev \
  libwayland-dev wayland-protocols libdecor-0-dev \
  libasound2-dev libpulse-dev libudev-dev libgl1-mesa-dev
```

macOS : `brew install cmake ninja glslang`.
Windows : le SDK Vulkan de LunarG fournit `glslangValidator`.

### Le seul accès réseau de toute la chaîne

Le **premier** build récupère SDL3 depuis GitHub, sur un tag épinglé (`release-3.4.14`). C'est la
seule opération réseau du projet, et elle a deux échappatoires si tu veux un build entièrement hors
ligne :

```sh
# SDL3 déjà installé sur le système (paquet, Homebrew, vcpkg)
cmake --preset linux-x64 -DNINETEEN_USE_SYSTEM_SDL3=ON

# ou : sources SDL3 déjà sur le disque
cmake --preset linux-x64 -DNINETEEN_SDL3_LOCAL_DIR=/chemin/vers/SDL
```

## Compiler et jouer

```sh
cmake --preset linux-x64          # ou macos-universal, windows-x64
cmake --build --preset linux-x64
./build/linux-x64/bin/nineteen
```

C'est tout. Fenêtre 1600×900, souris capturée, caméra à hauteur d'yeux.

| Touche | |
|---|---|
| `ZQSD` (AZERTY) / `WASD` (QWERTY) / flèches | se déplacer |
| Souris | regarder |
| `Maj` gauche | courir |
| `Espace` | sauter |
| `Ctrl` gauche ou `C` | s'accroupir |
| `E` | devant une borne : insérer un jeton et jouer. Devant le **monnayeur** : prendre des jetons. Devant la **vitrine** : échanger ses tickets |
| `R` | après une partie : **quitte ou double** (si le lot est acquis) |
| `F` | **cogner la borne** devant soi. Marche en partie comme hors partie ; pendant les 500 ms du geste, la main quitte les boutons |
| `Échap` | le menu — réglages, **commandes**, **crédits**, quitter |
| `F2` | capture d'écran dans le répertoire utilisateur |
| `F5` | basculer caméra joueur / caméra libre |
| `F6` | caméra orbite |
| `F7` / `F8` | palier de qualité / échelle de rendu |
| `F9` | ouvrir ou abandonner une manche du **Couperet**, le mode compétitif |
| `Maj`+`F9` | l'ouvrir en **équipes** : deux camps |
| `F10` | première ou troisième personne |
| `Tab` | pendant une manche : changer de **cible** |
| `1` à `6` | pendant une manche : acheter une action — brouillage, inversion, coupure, blindage, relais, leurre |

**La manette est branchée**, depuis 17.0.0. `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` est appelé
(`room/main.c:1328`), les branchements et débranchements à chaud sont suivis
(`SDL_EVENT_GAMEPAD_ADDED` / `REMOVED`), et on traverse la salle, on ouvre le menu et on joue aux
huit jeux au pad :

| Manette | |
|---|---|
| Stick gauche | se déplacer |
| Stick droit | regarder |
| A (bouton du bas) | devant une borne : insérer un jeton et jouer ; en partie : le bouton |
| Croix directionnelle | en partie : le manche de la borne ; dans le menu : choisir et régler |
| `Start` | ouvrir et fermer le menu |
| B (bouton de droite) | fermer le menu |

Le point qui comptait n'est pas le branchement, c'est le **journal**. Les appuis de la manette
sont fusionnés dans le même masque que le clavier — `hmask = room_keys_mask(keys) | pad_mask`
(`room/main.c:3188`) — et c'est ce masque que `--journal-entrees` écrit, que `--rejouer` relit et
qu'un duel en pas verrouillé publie. Une partie jouée à la manette est donc **rejouable et
reproductible** exactement comme une partie au clavier. Une implémentation qui aurait nourri les
touches sans nourrir le masque aurait rendu toute partie à la manette irreproductible, sans
message et sans erreur. Le test `manette` (`tests/test_pad.c`, n° 4 de la suite) vérifie que les
deux périphériques produisent le **même masque**, et il fait partie des 41 qui gardent le build.

**Ce que la manette n'a pas encore**, et il faut le savoir avant de brancher :

- **ni saut, ni accroupi, ni course** — `Espace`, `Ctrl` gauche et `Maj` gauche n'ont pas
  d'équivalent au pad ;
- **la page `COMMANDES` du jeu ne l'affiche pas**. Elle ne liste que le clavier et la souris. Le
  tableau ci-dessus est, pour l'instant, le seul endroit où la correspondance est écrite — dans
  un fichier à ouvrir à côté du jeu, donc pas là où le joueur la cherchera ;
- **la correspondance n'est pas réglable** : elle est en dur.

Ces touches sont aussi **dans le jeu**, depuis 17.0.0 : `Échap` puis `COMMANDES`. Un bandeau
les rappelle pendant les quatorze premières secondes — le temps de traverser le sas.

Les touches de déplacement sont lues par **position physique** et non par lettre : le bloc en haut
à gauche du clavier avance, quelle que soit la disposition. C'est le même bloc de touches qui
s'appelle ZQSD en AZERTY et WASD en QWERTY — il n'y a pas deux liaisons, il y en a une.

En caméra libre (`F5`), `Espace` et `Ctrl` montent et descendent au lieu de sauter et de
s'accroupir : ce mode n'a ni gravité ni collision, c'est un outil de cadrage.

**Jouer sur une borne** : approchez-vous, `E` insère un jeton et la partie démarre **dans la
dalle** — on reste en 3D, la tête reste libre, on voit l'écran à travers son verre bombé. **Les
huit jeux sont portés** : Envol (`Espace`), Snake, Aplomb, Démineur, Asteroid, Dédale, Piano et
le shooter, tous au manche — les flèches — et au bouton — `Espace`. Snake se joue aux flèches
**maintenues** : le serpent tourne tant qu'on tient. `Échap` sort de la partie, pas du jeu.

*(Cette ligne annonçait « deux jeux sont portés » jusqu'en 17.0.0, alors que les six autres sont
décrits juste en dessous et que `tests/test_replay.c` les rejoue tous les huit.)*

Les bornes Snake « hard » ne se jouent pas comme les autres, et c'est la règle de 2020 : **manger
coûte** cinq fois la valeur du fruit, et le score vient de ceux qu'on laisse **pourrir** sur le
terrain. Ce n'est pas un bogue, c'est le mode.

Le **Démineur** se joue au manche : il n'y a pas de souris sur une borne, donc le curseur se
déplace case par case et **maintenir une direction le fait défiler**. La grille est celle de
2020 — 16 x 25, cent bombes, soit le quart des cases — et le premier coup est **toujours sûr** :
les bombes ne sont posées qu'ensuite, en épargnant la case jouée et ses voisines.

Le **Piano** se joue au manche : les quatre directions sont les quatre voies, et **frapper une
voie vide termine la partie** — c'est la règle de 2020, et c'est elle qui empêche de marteler.
Laisser passer une note ne fait que casser le combo.

**Aplomb** — l'empilement — ne compte pas comme les autres, et c'est voulu : chaque ligne
simultanée vaut le **double** de la précédente — un quadruple fait donc 1 500 et non 400 — et une
ligne d'**une seule couleur** vaut **dix fois** son total. Viser la couleur rapporte plus que viser
le quadruple.

Pour voir un jeu sans traverser la salle : `--game=envol`, `--game=snake`, `--game=demineur`,
`--game=aplomb`, `--game=asteroid`, `--game=dedale`, `--game=piano` ou `--game=shooter` démarre directement en plein écran. Sans écran (`--headless`), la graine est fixe :
la même commande rend exactement la même image.

**Dédale** — le labyrinthe — donne **trois vies**, et la partie ne s'arrête qu'à la troisième
prise : les deux premières coûtent une manche, pas la partie, et le score comme le labyrinthe
entamé sont conservés. Les quatre poursuivants sont **les hélices**, un rotor à quatre pales, et
le héros est **le rubis**, une pierre taillée : deux silhouettes qu'on ne confond pas, l'une
pleine, l'autre ajourée. La graine change la partie — c'est elle qui décide quel comportement
sort de quel côté de l'enclos, et dans quel ordre.

**Trois jeux ont changé de nom**, et pas par goût : « PAC-MAN », « TETRIS » et « FLAPPY BIRD »
sont des marques déposées, et le paquet ne pouvait pas être vendu tant qu'elles y étaient. Une
mécanique ne s'approprie pas, un nom et un personnage si. Les règles n'ont pas bougé d'une ligne.

| Avant | Maintenant | `--game=` |
|---|---|---|
| PAC-MAN | **DÉDALE** | `dedale` |
| TETRIS | **APLOMB** | `aplomb` |
| FLAPPY BIRD | **ENVOL** | `envol` |

Un score enregistré sous l'ancien nom **n'est pas perdu, il devient invisible** : le classement
local (`ns_scores`) est indexé par `jeu/difficulté`, et une ligne `pacman/normal` reste dans le
fichier sans qu'aucune borne ne la demande. Elle se récupère à la main en renommant la clé dans
le fichier de scores. Côté serveur, rien n'est perdu : la migration
`0003_debaptise.sql` **renomme le créneau** au lieu d'en créer un neuf, et les parties pointent
vers l'identifiant du jeu, pas vers son nom. Un classement mondial change d'étiquette, pas de
contenu.

Le son suit la pièce : les pas changent selon ce qu'on a sous les pieds, et les **toilettes, le
sas d'entrée et le coin billard** ont chacun leur écho — déclaré dans `assets/scene/salle.room.json`,
pas deviné. On l'entend en passant la porte des toilettes ; la transition prend le temps de la
franchir. `docs/audio-pas-sec.wav` et `docs/audio-pas-toilettes.wav` sont le même pas rendu dans
les deux espaces, si vous voulez comparer sans lancer le jeu.

Le premier build convertit aussi les assets — la salle passe en glTF, les textures reçoivent leurs
cartes PBR, le BVH est construit. Compter une douzaine de secondes.

## Regarder sans jouer

```sh
./build/linux-x64/bin/nineteen --camera=orbit
```

Un tour lent de la salle, sans toucher au clavier.

## Vérifier que la machine suit — sans écran ni carte graphique

```sh
ctest --preset linux-x64
```

Trois tests. Celui de rendu **crée réellement un périphérique GPU, dessine, relit l'image et
contrôle son contenu** ; il tourne via le pilote vidéo `offscreen` de SDL et le rasteriseur Vulkan
logiciel (lavapipe), donc sans écran. C'est aussi ce qui le rend utilisable en intégration continue.

Et une capture, toujours sans écran :

```sh
SDL_VIDEO_DRIVER=offscreen ./build/linux-x64/bin/nineteen \
    --headless --screenshot=salle.png --frames=8 --quality=high
```

Utile aussi quand quelque chose cloche : `--debug=albedo|normal|emissive|depth|visibility|hdr|bloom|volumetric`
affiche une cible intermédiaire au lieu de l'image finale. C'est ce qui a permis d'isoler un écran
noir en deux minutes, en montrant un G-buffer parfait et un HDR rempli de NaN.

## Comparer les deux salles

Deux salles cohabitent : celle reconstruite par `roomgen` et celle convertie du modèle de 2020.

```sh
./build/linux-x64/bin/nineteen --room=legacy    --view=allee
./build/linux-x64/bin/nineteen --room=generated --view=allee
```

Points de vue nommés : `allee`, `bar`, `billard`, `orbite`. Ils sont **nommés** et non donnés en
coordonnées parce que les deux salles n'ont pas la même échelle — le modèle de 2020 est en unités
Blender, 2,06 par mètre — si bien que `--pos=0,1.68,8` y désignerait deux endroits différents. Chaque
salle déclare les mêmes noms dans son propre référentiel.

Pour produire les huit captures d'un coup :

```sh
cmake --build build/linux-x64 --target render-compare
```

## Où le jeu écrit

Uniquement dans le répertoire utilisateur — jamais à côté du binaire, ce qui échouerait sur une
installation en lecture seule :

- Linux : `~/.local/share/recognizer/Nineteen/`
- macOS : `~/Library/Application Support/recognizer/Nineteen/`
- Windows : `%APPDATA%\recognizer\Nineteen\`

On y trouve `nineteen.log`, les captures de `F2`, `settings.cfg` — ce dernier n'étant écrit que si
un réglage a changé —, `scores.txt` et `portefeuille.txt`.

`portefeuille.txt` porte les jetons, les tickets, la série et les lots acquis. Il est écrit
atomiquement, comme les deux autres : un temporaire puis un renommage, de sorte qu'une coupure de
courant laisse l'ancien fichier intact plutôt qu'un fichier à moitié écrit. Le supprimer remet
l'économie à zéro et ne casse rien — le jeu repart d'un portefeuille neuf sans un message
d'erreur, ce qui est le cas du premier lancement.

## L'économie : jeton, partie, tickets, lot

**Une partie coûte un jeton, et on n'est jamais bloqué.** Le monnayeur — le meuble jaune à
l'est du comptoir, le premier objet éclairé quand on sort du sas — complète jusqu'à cinq jetons,
gratuitement, autant de fois qu'on le demande. Il n'y a pas de minuterie, pas de compte à
rebours, et **aucune voie d'achat** : ni monnaie réelle, ni boutique, ni coffre aléatoire payant.

Une partie terminée rend des **tickets**, selon un barème calibré jeu par jeu pour qu'une bonne
partie rapporte à peu près autant partout — les huit jeux ne marquent pas dans la même unité, et
le score médian mesuré va de 38 points à 124 600. Les tickets s'échangent à la **vitrine à lots**,
sur le mur est du hall, contre quatre déverrouillages. Le solde se lit en haut à gauche.

Trois choses donnent envie de revenir, et aucune ne punit l'absence :

- **le tournoi du jour** — les huit jeux tirés d'une graine dérivée de la date, la même pour tout
  le monde, remise à zéro à minuit UTC ;
- **la série** — jours consécutifs joués, qui ajoute jusqu'à sept tickets par partie ;
- **le quitte ou double** — risquer les tickets d'une partie sur une reprise en régime difficile.
  Refuser est aussi facile qu'accepter : rejouer, repartir ou ne rien faire valent refus, et
  versent.

Le barème complet, ses mesures, les lots et ce qui a été écarté : **`docs/ECONOMIE.md`**. Les
taux sont aussi affichés dans la salle — l'affiche `jetons` porte la grille des huit, le
`reglement` les six règles, et les deux sont dessinées à la construction depuis la même table que
le jeu, de sorte qu'elles ne *peuvent* pas le contredire.

## Le Couperet — le mode compétitif

**Une phrase suffit à le décrire, et tout le reste en découle :** tes points ne comptent
qu'*une fois la partie finie*, et toutes les quarante-cinq secondes le couperet sort le dernier.

`F9` ouvre une manche. On y joue à deux à huit places, chacun sur les bornes de la salle.

**Le choix qu'il crée.** Les dix-neuf bornes ne durent pas le même temps, et l'écart est énorme :
mesuré sur ce dépôt, la durée médiane d'une partie va de **5,9 s** (démineur en régime difficile)
à **180 s** (aplomb, dedale, snake difficile). Enchaîner du court met en banque deux fois entre
deux lames — on n'est jamais pris les mains vides. S'engager sur du long traverse **quatre lames
sans avoir rien encaissé**, et si l'on est dernier quand l'une tombe, les trois minutes partent
avec. En échange, les bornes longues paient beaucoup plus : le multiplicateur est peint sur leur
fronton, il va de `×0,05` à `×4,20` — un rapport de **79** — et il se lit **avant** d'insérer le
jeton.

**La partie en cours défend.** Elle ne compte pas au classement tant qu'elle n'est pas finie,
mais le couperet, lui, la voit : au train où l'on marque, elle vaut ce qu'elle vaut, et cela suffit
à tenir la lame à distance. C'est ce qui rend l'engagement jouable — sans cette règle, mesuré, il
perdait **200 manches sur 200**.

**Les fusibles.** Un par tranche de trente secondes de jeu, un de plus à chaque lame. Ils
s'achètent six actions, toutes électriques :

| | Prix | Effet |
|---|---|---|
| `1` **Brouillage** | 1 | la dalle de la cible se brouille, 4 s |
| `2` **Inversion** | 2 | son manche part à l'envers, 5 s |
| `3` **Coupure** | 4 | sa borne s'éteint : partie annulée, durée perdue |
| `4` **Blindage** | 2 | encaisse la prochaine attaque, et attend |
| `5` **Relais** | 1 | donne un fusible |
| `6` **Leurre** | 3 | renvoie la prochaine attaque à son auteur |

`Tab` choisit la cible ; une action défensive sans cible se pose sur soi. On ne frappe ni son
propre camp, ni un spectre, ni une borne déjà éteinte.

**Être sorti ne met pas à la porte.** Un éliminé devient **spectre** : il ne joue plus, mais il
garde ses fusibles, en reçoit un à chaque lame, et continue d'agir. Sortir vous change de métier.

**Les équipes** — `Maj`+`F9`, ou `--couperet=8x2` sur la ligne de commande. Le couperet classe
les **camps** et ne descend au joueur qu'à l'intérieur du camp condamné. C'est ce qui rend le rôle
de soutien survivable : celui qui dépense tout en blindages sur son porteur ne marque rien et
n'est pourtant pas condamné pour ça. Deux camps de quatre plutôt que quatre camps de deux, parce
qu'à deux chacun est son propre porteur et le rôle disparaît.

**L'équilibre est mesuré, pas affirmé.** `tests/test_couperet.c` ne simule pas les parties, il les
**joue** : les huit autopilotes du dépôt constituent un vivier de durées et de scores réels, et le
test fait tourner deux cents manches de huit places. Résultat livré : **55 %** de victoires pour
l'engagé contre le pressé, et 51 / 49 / 56 % sur trois graines indépendantes. Les six actions
déplacent l'issue de **38 points de pourcentage** — sans elles, l'engagé gagne 94 % des manches —
et le test *exige* cet écart, pour qu'une action affaiblie ne passe pas inaperçue.

**Les rivaux jouent pour de vrai.** Les places que personne n'occupe sont tenues par des rivaux
qui **allouent un état de jeu, appellent l'autopilote et le pas du jeu à chaque pas fixe**, et
encaissent comme vous. Ils ne sont pas des compteurs qui montent : un rival qui tricherait serait
invisible et impardonnable, et il rendrait sans objet la mesure d'équilibre ci-dessus, qui est
faite sur ces mêmes autopilotes.

Trois niveaux, séparés par la **fraction de pas où le rival ne réagit pas** — mesurée, pas
supposée : le chevronné bat le débutant **102 manches sur 120** pour 1,91 fois ses points. Trois
et pas quatre : sur la grille 0/20/40/60 % que j'avais d'abord demandée, deux mesures du même
point s'écartent de 7 % pour un écart réel de 4,5 %. Elle ne mesurait rien.

Et ils **s'installent sur de vraies bornes**. Pendant une manche, la borne d'en face ne joue plus
une démonstration anonyme : elle joue *le dedale de quelqu'un*, avec son score et sa mort.
S'approcher d'une borne tenue par un rival en fait votre cible — viser en se plantant devant la
machine, plutôt qu'en faisant défiler une liste. Mesuré : **7 bornes sur 18 s'allument** pendant
une manche, parce que toutes les conduites classent sur le même rendement affiché et veulent donc
les mêmes machines.

**En ligne** — `--couperet-en-ligne=hôte:port,salon,place,places`. Deux à huit personnes, chacune
donnant le même salon et le même nombre de places. Le salon est convenu hors bande, comme
l'identifiant d'un duel : ce dépôt n'a pas d'appariement, et en inventer un demanderait un
service, des comptes et une file d'attente. Deux amis conviennent d'un nombre et le tapent.

Le partage d'autorité est écrit et vérifié : **chaque client fait autorité sur ses points, ses
fusibles et la borne qu'il joue** — personne d'autre ne peut les calculer ; **une seule place
arbitre les éliminations et le sort des actions**, parce que ce sont les seules décisions qui
doivent être prises une fois pour tout le monde. Qui arbitre peut changer en cours de manche, et
la lame est reprise sans perdre un tour. Conséquence assumée, la même qu'en duel : **l'arbitre
peut mentir**, et l'autorité sur les scores *enregistrés* ne bouge pas — elle reste le journal
scellé par HMAC, recalculé par le serveur.

**Ce que ce mode n'est pas.** Il n'a ni saison, ni laissez-passer, ni rien qui s'achète. Ses
fusibles naissent au coup d'envoi et meurent au verdict ; le portefeuille de la salle n'est pas
touché. La minuterie n'y punit pas l'absence — elle arbitre une manche qu'on a choisi de
commencer, et sortir du mode ne coûte rien.

## Ce qui manque encore, dit franchement

Le jeu se parcourt, se joue, et se règle. Ce qui manque :

- ~~les mini-jeux~~ : **les huit sont portés**, et les dix-neuf bornes de la salle jouent
  toutes ;
- ~~le transport réseau~~ : **écrit** — classement en ligne, présence, duels. Il reste éteint
  par défaut et ne parle pas TLS ;
- ~~du mobilier importé au-delà de trois modèles~~ : **douze modèles**, listés dans
  `assets/cc0/LICENSES.md` ;
- ~~la manette~~ : **branchée**, et le journal d'entrées la suit. Il lui manque le saut,
  l'accroupi, la course, une ligne dans la page `COMMANDES` du jeu et une table réglable ;
- ~~les huit affiches de tiers~~ : **dessinées** par `tools/posterart`, avec treize autres
  planches empruntées et le tapis du hall. Vérifié en déballant l'archive : plus une seule
  des vingt et une images identifiées n'y est ;
- **les licences des images de 2020.** Il reste **29 images** dans le décor livré qui viennent
  de `legacy/room/textures/` et **ne portent aucune trace d'origine ni de licence**. Aucune n'a
  été identifiée comme appartenant à un tiers — ce sont des bétons, des carrelages, des bois —
  mais « pas identifié » n'est pas « établi ». Détail dans `assets/cc0/LICENSES.md`, section
  « Les images de 2020 » ;
- **le nom `ASTEROID`.** À une lettre de la marque *Asteroids* d'Atari. Risque résiduel signalé,
  pas tranché : c'est une décision, pas un travail ;
- **la signature du paquet.** Ni Developer ID ni Authenticode, et ça n'est pas près de changer :
  le compte développeur Apple **gratuit** ne suffit pas, mesuré — un certificat « Apple
  Development » avec quarantaine posée donne `spctl : rejected` et le processus est tué. Il faut
  le programme payant. En attendant, le paquet est signé **ad hoc**, ce qui lui permet de se
  charger mais ne convainc pas Gatekeeper, et il porte un `A-LIRE-AVANT-D-OUVRIR.txt` avec le
  geste exact — `xattr -dr com.apple.quarantine`, vérifié ici : tué avant, démarre après. La
  bascule vers un paquet signé est déjà écrite et n'attend que les variables d'environnement ;
  côté Windows, rien n'a pu être exécuté sur cette machine ;
- **le pas verrouillé en duel direct**, toujours pas mesuré entre deux machines.

Cette liste a été fausse deux fois plutôt qu'une : elle annonçait encore « aucun son », « aucune
interaction » et « la collision n'est pas branchée » longtemps après que les trois aient été
livrés, puis elle a annoncé un transport réseau absent pendant deux versions où il tournait.
`docs/CHANGELOG-V17.md` dit ce qui tient aujourd'hui, et ce qui ne tient pas.

## Pour mémoire : la même chose en 2020

Hors ligne, la V1 **ne pouvait pas afficher sa fenêtre**. Tout le corps de `main()` était enfermé
dans `if (checkVersion(...) == EXIT_SUCCESS)` (`legacy/main.c:1173`), et `checkVersionOnline`
(`legacy/include/libWeb.c:643`) ne renvoyait un succès que si un POST vers `checkVersion.php`
répondait littéralement `"1"`. Sans réseau : une boîte de dialogue annonçant faussement « une
nouvelle version est disponible », puis fermeture. Aucun mode hors ligne, aucun contournement.

Et si l'on passait ce verrou, il en restait trois : une connexion par compte obligatoire, une
vérification de 54 fichiers en chemins relatifs qui n'aboutissait que depuis `bin/`, et un
chargement du classement dont la boucle de réessai — `while (updateMeilleureScoreStruct(...) ==
EXIT_FAILURE);` (`legacy/room/room.c:1302`) — tournait **indéfiniment** en cas de panne serveur, sans
message.

## Les scores, et le réseau

### Ce qui marche aujourd'hui, sans rien installer

Le classement est **local**, et il l'est par conception. À la fin d'une partie le
score est écrit dans `scores.txt`, dans le répertoire utilisateur du jeu — celui
de la section « Où le jeu écrit » plus haut, et **pas** un répertoire `nineteen/`
séparé : `SDL_GetPrefPath("recognizer", "Nineteen")` (`engine/core/ns_paths.c:72`)
donne `~/Library/Application Support/recognizer/Nineteen/` sous macOS,
`~/.local/share/recognizer/Nineteen/` sous Linux et `%APPDATA%\recognizer\Nineteen\`
sous Windows. *(Ce paragraphe annonçait un chemin sans `recognizer/` jusqu'en
17.0.0 : il envoyait chercher le fichier de scores dans un répertoire qui n'a
jamais existé.)* L'écriture est **atomique** — un
temporaire puis un renommage —, donc une coupure de courant laisse l'ancien
fichier intact plutôt qu'un fichier tronqué.

```sh
./build/linux-x64/bin/nineteen --nom=Nine     # le nom porté au classement
```

Le nom est facultatif. **Le jeu ne demande jamais de compte pour jouer**, et ne
le demandera jamais : c'était l'erreur de fond de la version de 2020, dont tout
le `main()` était enfermé dans un contrôle de version en ligne. Sans réponse du
serveur, la V1 affichait « une nouvelle version est disponible » et se fermait —
elle ne pouvait pas atteindre sa propre fenêtre.

### Ce qui est préparé, et pas branché

Chaque partie produit un **journal d'événements horodatés au pas fixe**, scellé
par HMAC-SHA256 au format exact que le serveur Go attend
(`server/internal/runs/runs.go`). Le serveur ne croit pas le score annoncé : il
ouvre la partie, tire la graine et un secret, puis **recalcule** le score depuis
le journal.

La moitié client de ce contrat est écrite et vérifiée : `tests/test_scores.c`
confronte la charge canonique et le sceau aux vecteurs produits par le code Go
lui-même. Une divergence d'un octet invaliderait toutes les parties, avec pour
seul symptôme un « sceau invalide » côté serveur ; c'est exactement ce que ce
test empêche.

### Le classement en ligne, activable

**Par défaut, le jeu ne se connecte à rien.** Sans URL de serveur configurée,
aucune socket n'est ouverte et le fil réseau ne démarre même pas.

```sh
./build/linux-x64/bin/nineteen --server=http://mon-serveur:8080
```

ou, une fois pour toutes, `network.serverUrl` dans `settings.cfg`. Le jeu
affiche alors le **meneur mondial** sur la borne de classement, à côté des
scores locaux. **Aucun compte n'est demandé** : sans jeton, le classement est en
lecture seule, ce qui suffit à voir où l'on en est. C'était l'erreur de fond de
la V1, qui exigeait trois allers-retours HTTP et un compte avant de montrer quoi
que ce soit.

**Ce que le binaire importe désormais.** Jusqu'à B14 cette page affirmait qu'il
n'importait *aucun* symbole réseau, et c'était vrai. Ça ne l'est plus :

```sh
nm -D --undefined-only build/linux-x64/bin/nineteen \
  | grep -icE 'socket|connect|getaddrinfo|recv|send'   # -> 5
```

Ce qui reste vrai, et qui est ce qui compte : **rien ne part sans qu'on l'ait
demandé**. Sans URL, aucune de ces cinq fonctions n'est appelée ; `--offline`
verrouille par-dessus, et `tests/test_online.c` le vérifie en demandant un
classement puis en constatant qu'il n'arrive jamais.

**Pas de TLS**, dit franchement : le chiffrement demanderait OpenSSL ou mbedTLS,
c'est-à-dire une dépendance lourde dans un projet reconstructible en une
commande. Le classement vise un serveur qu'on héberge soi-même ; mettre un
proxy TLS devant est la bonne réponse, et une URL `https://` est **refusée**
plutôt que tentée en clair.

**Une partie jouée hors ligne n'est pas soumettable**, et c'est une conséquence
de l'anti-triche, pas une lacune. Le serveur tire la graine ET le secret d'une
partie AVANT qu'elle soit jouée ; le score n'est pas une valeur que le client
annonce mais une conséquence que le serveur recalcule. Une partie sans graine ni
secret du serveur ne peut pas être vérifiée, et l'accepter reviendrait
exactement à la V1, où le client annonçait son score et le serveur le croyait.

Le temps réel — présence, duels — se conçoit avant de s'écrire, et cette
conception reste à faire : `docs/RESEAU-TEMPS-REEL.md` en pose les faits, les
trois formes possibles et leur coût. En attendant, une partie jouée sans serveur est
classée localement et **rien n'échoue**.

### Ce que « en ligne » veut dire concrètement, et comment le vérifier

La chaîne complète tourne, et elle a été **déroulée en entier** contre le vrai
`nineteend` et sa base : billet → partie → sceau → file → envoi → classement.
Voici ce qui se passe, dans l'ordre :

1. **Le billet est pris d'avance.** Quand le joueur arrive devant une borne — ou
   qu'un jeu se charge — le client demande `POST /api/v1/runs`, et le serveur
   répond avec un identifiant de partie, **la graine** et **le secret**. Ça se
   passe pendant que la main s'avance vers le jeton.
2. **La partie se joue sur la graine du serveur.** C'est ce qui rend le score
   recalculable : le serveur rejoue le journal et retrouve la valeur.
3. **La partie scellée entre dans une file locale**, un fichier par partie.
4. **La file part** à la fin de chaque partie et au démarrage suivant, sur
   `POST /api/v1/runs/{id}/submit`. Un 2xx ou un 4xx efface le fichier — le
   serveur a tranché ; un 5xx ou un silence le **garde** : c'est une panne, pas
   un verdict, et la partie repartira plus tard.

**Une partie n'attend jamais le réseau.** Elle commence quand le joueur appuie
sur le bouton. S'il n'y a pas encore de billet, elle se joue hors ligne, le score
est acquis localement, et c'est tout — ce qui veut dire, concrètement, que la
toute première partie d'une session est souvent locale et les suivantes en
ligne.

Pour le vérifier soi-même, avec un serveur de développement :

```sh
# 1. le serveur
NINETEEN_DB_URL=postgres://…/nineteen go run ./server/cmd/nineteend &

# 2. un compte, puis son jeton de session
curl -s -X POST localhost:8080/api/v1/auth/register -H 'Content-Type: application/json' \
     -d '{"username":"moi","email":"moi@exemple.invalid","password":"un-mot-de-passe-long"}'
jeton=$(curl -s -X POST localhost:8080/api/v1/auth/login -H 'Content-Type: application/json' \
        -d '{"username":"moi","password":"un-mot-de-passe-long"}' \
        | sed 's/.*"sessionKey":"\([^"]*\)".*/\1/')

# 3. la chaîne entière, de bout en bout
./build/linux-x64/bin/ns_test_online http://127.0.0.1:8080 "$jeton"
```

Le jeton se range dans `network.token` de `settings.cfg` pour que le jeu lui-même
s'en serve. **Sans jeton, tout marche encore** : le classement mondial s'affiche
en lecture seule, et les parties restent locales.

**Mais plus personne n'a à faire ça.** Cette manipulation était la SEULE façon de
se connecter, et elle était impossible depuis le jeu : `SDL_StartTextInput`
n'existait nulle part dans le dépôt, donc aucun champ n'était saisissable. Le
comptoir (`F1`) fait maintenant l'inscription, la connexion et le rangement du
jeton. Les lignes ci-dessus restent utiles pour vérifier une chaîne de bout en
bout depuis un terminal, pas pour jouer.

## Le comptoir — s'inscrire, se connecter, trouver une manche

`F1`, depuis n'importe où dans la salle. Il s'ouvre aussi **tout seul** au premier
lancement branché sur un serveur, tant qu'aucune session n'est gardée.

| Page | Ce qu'on y fait |
|---|---|
| Accueil | se connecter, créer un compte, ou jouer sans compte |
| Les salons | la liste des salons **publics** ouverts, rafraîchie à la demande |
| Créer un salon | un nom, de 2 à 8 places, 1 ou 2 camps, public ou privé |
| Rejoindre par code | six caractères, sans I, O, 0 ni 1 pour qu'ils se dictent |
| Le salon | le code en grand, qui est assis où, et le bouton qui lance la manche |

Sans serveur configuré, le comptoir s'ouvre quand même et **dit ce qui manque** —
`--server=`, `NINETEEN_SERVER_URL`, ou `network.serverUrl`. Une touche qui ne fait
rien selon un réglage qu'on ne voit pas est pire qu'une page qui explique.

Ce que le comptoir refuse avant même d'appeler le serveur : un champ vide, et une
deuxième demande quand la première est encore en vol — sans quoi un double appui
créerait deux salons.

**Le code n'est pas ce qui donne la place.** Six caractères tapés à la main, c'est
court par nécessité, donc devinable. Ce qui ouvre réellement une place sur le
relais est un nombre de 63 bits tiré par le serveur, rendu au seul joueur
authentifié et admis ; il n'apparaît ni dans la liste publique ni dans le
classement en direct, et un test échoue s'il venait à y fuir.

**Les camps sont attribués par le serveur**, en alternance sur les places. Ce n'est
pas une étiquette d'affichage : le Couperet classe les camps et la lame descend
dans le camp dernier. Un camp déclaré par le client serait un camp qu'on change en
cours de manche pour rejoindre celui qui mène.

### Regarder une manche sans y jouer

`/salon.html` sur le serveur web suit une manche pendant qu'elle se joue — le camp,
la borne tenue, les fusibles, les points, et qui est déjà sorti. L'adresse porte le
code (`/salon.html?c=K7M3QP`) : c'est ce lien qu'on envoie à quelqu'un. La page
liste aussi les salons publics ouverts.

Deux tableaux, deux sources, et la page le dit : le classement général n'accepte
que des parties **recalculées** par le serveur, alors qu'ici ce sont les joueurs
qui publient leur ligne pendant la manche. Suffisant pour regarder, insuffisant
pour un palmarès.

Deux détails que cette vérification a mis au jour, et qu'aucun test unitaire
n'aurait attrapés parce qu'ils vivent *entre* les deux moitiés du projet : le
serveur nomme ses tableaux `envol-easy` / `envol-hard` là où le moteur porte un
jeu et une difficulté séparés (la traduction se fait maintenant contre la liste
que le serveur renvoie), et la graine de partie fait 63 bits, donc la relire dans
un `float` la détruisait silencieusement.

### Rendre une partie reproductible

```sh
./build/linux-x64/bin/nineteen --journal-entrees=partie.txt
```

Écrit les commandes de la partie — un masque de boutons par pas fixe, seulement
quand quelque chose change — dans un fichier texte. Un rapport de bug cesse
d'être « ça a planté après deux minutes » pour devenir quelque chose qu'on
rejoue. Le fichier s'écrit à la fin de la partie **et** à la fermeture du jeu,
parce que le cas où on en a besoin est justement celui où la partie ne s'est pas
terminée normalement.

Et pour le relire :

```sh
./build/linux-x64/bin/nineteen --rejouer=partie.txt
```

**Ni fenêtre, ni GPU, ni assets, ni réseau** : un mini-jeu est une simulation à
pas fixe, et le dessin n'y change rien. On peut donc rejouer le journal de
quelqu'un sur une machine sans écran, dans l'intégration continue, ou sous un
débogueur — c'est-à-dire là où l'on cherche un bug. La commande imprime le jeu,
la graine, le nombre de pas, le score, les gains cumulés et le pas où la partie
s'est terminée.

`tests/test_replay.c` vérifie que les huit jeux se rejouent à l'identique en
mémoire — score, gains, et état comparé au bit près — et `rejeu-cli` vérifie
l'outil lui-même : deux rejeux du même fichier donnent la même sortie, un
fichier qui n'est pas un journal est refusé, un fichier absent aussi.

### `--offline`

```sh
./build/linux-x64/bin/nineteen --offline
```

C'est un **verrou**, pas un repli : il interdit toute mise en file d'envoi, même
quand le transport existera. Aujourd'hui il ne change rien au comportement
observable — une partie sans secret de serveur n'est de toute façon pas mise en
file — et il est là pour que la garantie soit exprimable dès maintenant plutôt
que rajoutée après coup.

## Installer

Le jeu se lance très bien depuis l'arbre de build — c'est ce que fait tout le
reste de cette page. Pour le donner à quelqu'un, on prend le paquet de la page
des versions, et **il n'y a rien à déballer** :

| Système | Fichier | Ce qu'on en fait |
|---|---|---|
| macOS 11+ | `Nineteen-*-macOS-universal.dmg` | Ouvrir, glisser **Nineteen** sur **Applications**. Universel : Apple Silicon et Intel. |
| Linux | `Nineteen-*-x86_64.AppImage` | `chmod +x`, puis lancer. Rien à installer. |
| Debian / Ubuntu | `nineteen_*_amd64.deb` | `sudo apt install ./nineteen_*_amd64.deb` — le jeu apparaît dans le menu. |
| Linux, sans installer | `nineteen-*-linux-x86_64.tar.gz` | Déballer, lancer `bin/nineteen`. |
| Windows 10/11 | `Nineteen-*-windows-x64.exe` | Double-clic. **Aucun droit administrateur** : le jeu s'installe dans votre profil, avec un raccourci au menu Démarrer et un désinstalleur. |

Jusqu'en 17.0.0 les trois plateformes recevaient un ZIP ou un TGZ. Il fallait
deviner quel fichier lancer dans quel sous-répertoire, macOS refusait un binaire
nu sorti d'une archive, et rien n'apparaissait dans aucun menu. Une archive
autonome n'est pas un installateur.

### Où va quoi, et pourquoi ce n'est pas au même endroit partout

`ns_paths` cherche les assets dans **le répertoire que `SDL_GetBasePath()`
rend** — et ce répertoire n'est pas le même selon la forme du paquet :

| | binaire | assets et `nineteen.env` |
|---|---|---|
| macOS | `Nineteen.app/Contents/MacOS/nineteen` | `Nineteen.app/Contents/Resources/` |
| Linux | `/usr/lib/nineteen/nineteen`, avec `/usr/bin/nineteen` en lien | `/usr/lib/nineteen/` |
| Windows | `bin\nineteen.exe` | `bin\` |

Sur macOS ce n'est **pas** `Contents/MacOS/`, contrairement au réflexe :
`SDL_sysfilesystem.m:51` dit que `SDL_GetBasePath()` rend le répertoire
`Resources` dès qu'il y a un `Info.plist`. Poser les assets à côté de
l'exécutable les mettrait là où `ns_paths` ne regarde jamais — et le défaut ne
se verrait **que** depuis le `.dmg`, parce que sur la machine de construction
`NINETEEN_BUILD_ASSET_DIR` sauverait le lancement.

Sur Linux ce n'est pas `bin/` non plus : un `.deb` s'installe sous `/usr`, et
`bin/assets` y donnerait `/usr/bin/assets/`, un répertoire de données dans le
répertoire du `PATH`. Le binaire vit donc dans `/usr/lib/nineteen/` avec ses
assets, et `/usr/bin/nineteen` est un lien symbolique relatif vers lui — SDL
résout `/proc/self/exe`, qui suit le lien.

### Fabriquer les paquets soi-même

```sh
# macOS : le .dmg
cmake --preset macos-universal -DCMAKE_BUILD_TYPE=Release
cmake --build --preset macos-universal
cpack --config build/macos-universal/CPackConfig.cmake -B build/macos-universal/paquets

# Windows : l'installateur .exe (demande NSIS dans le PATH)
cmake --preset windows-x64 -DCMAKE_BUILD_TYPE=Release
cmake --build --preset windows-x64
cpack --config build/windows-x64/CPackConfig.cmake -B build/windows-x64/paquets

# Linux : .tar.gz, .deb et AppImage, DANS un conteneur Ubuntu 22.04
docker build -f packaging/linux/Dockerfile.build -t nineteen-build:22.04 packaging/linux
docker run --rm -v "$PWD:/src" -w /src nineteen-build:22.04 sh packaging/linux/paquets.sh
```

Le conteneur n'est pas un caprice. Un binaire lié à la glibc ne tourne que sur
une glibc **au moins aussi récente** : les symboles versionnés sont résolus au
chargement, sans repli. Construite sur une base récente, l'AppImage exigerait
`GLIBC_2.39` sans l'annoncer et refuserait de démarrer sur Debian 12 ou
Ubuntu 22.04 LTS. Mesuré dans le conteneur : glibc 2.35, et un binaire qui
n'exige que `GLIBC_2.34` — tout ce qui est sorti depuis octobre 2021.

`ldd` sur ce binaire ne trouve que `libm` et `libc` : SDL3 est lié
statiquement, et ce qu'il utilise du système (X11, Wayland, ALSA, PulseAudio,
Vulkan) il l'ouvre par `dlopen` au démarrage. C'est pour cette raison que le
`.deb` déclare `Depends: libc6 (>= 2.34)` et **rien d'autre** : `dpkg-shlibdeps`
lit les `NEEDED` de l'ELF, et un `dlopen` n'y figure pas. Ces
bibliothèques-là sont en `Recommends`, et le jeu dit laquelle manque à
l'exécution plutôt que de refuser de s'installer.

### Ce que ça pèse, mesuré

| Paquet | Taille |
|---|---|
| `.dmg` (macOS, universel) | 177 001 095 octets — pour 237,1 Mio de `Nineteen.app`, dont 227,6 Mio d'assets |
| `.AppImage` | 171 432 456 octets |
| `.deb` | 174 788 720 octets |
| `.tar.gz` | 174 788 622 octets |

Les chiffres Linux sont ceux du paquet **aarch64** construit sur la machine de
développement ; le x86_64 sort de la CI, avec la même image et le même script.

**Le contenu, lui, pèse 153 Mio compressés.** C'était 421 : les cartes de normales et
d'ORM étaient écrites à la résolution de leur texture source, quelle qu'elle soit
— jusqu'à 3840 x 2160 pour l'image d'un écran de jeu. Elles sont désormais
plafonnées à 1024 de côté (`NINETEEN_MAX_MAP`), ce qui divise ces deux familles
par 2,8 sans différence visible : une carte de normales porte une variation de
pente que l'éclairage intègre sur plusieurs pixels, là où un albédo porte du
lettrage et se lit au texel. Un test relit l'entête PNG de chaque carte produite
et refuse celle qui dépasse.

Puis les cartes sont passées en **blocs compressés** : BC5 pour les normales
(deux canaux, Z reconstruit dans le shader), BC1 pour l'ORM, mips comprises,
dans un conteneur maison de vingt octets d'en-tête (`engine/core/nstex.h`). 148
Mio de cartes deviennent 69, et l'archive tombe à 153.

Ça demande un GPU qui lit le BC — c'est-à-dire tout GPU de bureau, y compris les
Mac Apple Silicon. Le moteur le **vérifie** avant de créer la texture et le dit
s'il manque, plutôt que d'afficher du noir sans message. `-DNINETEEN_BC_MAPS=OFF`
redonne des PNG si un jour une plateforme visée ne suit pas.

### Vérifié plutôt qu'affirmé

Chaque paquet a été **ouvert et lancé**, pas seulement produit.

- Le `.dmg` a été monté, `Nineteen.app` copiée dans `/Applications`, **l'arbre
  de build renommé** pour qu'aucun montage de développement ne puisse la sauver,
  puis l'application lancée : elle a chargé
  `/Applications/Nineteen.app/Contents/Resources/assets/scene/salle.gltf`,
  438 426 sommets, et écrit sa capture. Relancée aussi par `open`, donc par
  LaunchServices, donc par le chemin du double-clic.
- L'AppImage a été relancée depuis le fichier fini : elle trouve ses assets et
  son `nineteen.env` dans l'image montée.
- Le `.deb` a été installé par `apt` dans un `ubuntu:22.04` nu, lancé depuis le
  `PATH` hors de tout arbre de build, puis désinstallé sans rien laisser.
- Le `.tar.gz` a été déballé dans le même conteneur et lancé par `bin/nineteen`,
  c'est-à-dire par le lien relatif.

C'est ce genre d'essai qui avait montré, à la version précédente, que la règle
d'installation n'emportait que le binaire : `cmake --install` produisait un
arbre sans une seule texture, et personne ne s'en apercevait parce que personne
n'installait.

**Ce qui n'est PAS vérifié, et il faut le savoir :** l'installateur Windows. La
machine de développement est un Mac sans `makensis`, sans mingw-w64 et sans
wine ; `cpack -G NSIS` s'y arrête avant même de lire la configuration. Ce qui
est vérifié depuis le Mac, c'est la **configuration** — le test `paquets`
(`packaging/verifier.cmake`) relit les réglages des quatre générateurs, vérifie
que les fichiers désignés existent, que l'icône a bien l'en-tête d'un `.ico` et
que la licence affichée par NSIS ne contient pas un octet hors ASCII. Le `.exe`
lui-même sort du job Windows de la CI, qui l'installe et le lance.

### La signature

Affaire de certificats, donc de qui publie : le workflow
`.github/workflows/release.yml` signe si les secrets existent et produit des
paquets **non signés** sinon, en le disant. Un paquet non signé se télécharge et
se lance, mais macOS le met en quarantaine et Windows affiche SmartScreen.

- macOS : clic droit sur **Nineteen** → **Ouvrir**, ou
  `xattr -dr com.apple.quarantine /Applications/Nineteen.app`.
- Windows : **Informations complémentaires** → **Exécuter quand même**.

Aucune de ces étapes de signature n'a jamais été exécutée : il n'existe pas de
certificat pour ce projet.

## Régler la fluidité

### Les cinq paliers, chiffrés

Mesurés avec `--bench`, qui **attend réellement le GPU** — sans cette attente on
chronomètre l'enregistrement des commandes, pas leur exécution, et c'est ainsi
qu'un rendu à une image par seconde avait pu être annoncé à 1 793.

Deux tableaux, et l'écart entre les deux est la leçon.

**Sur un vrai GPU** — Apple M1, Metal, cible de rendu 1600 × 900, minimum de
trois exécutions de 40 images, points de vue `allee` et `bar` :

| Palier | ms/image | rapport | images/s | ce qu'il coupe |
|---|---:|---:|---:|---|
| `potato` | 6,0 | ×0,30 | 164 | volumétrique, occlusion ambiante, rendu à 60 % |
| `low` | 6,1 | ×0,31 | 164 | volumétrique, rendu à 75 % |
| `medium` | 20,5 | ×1,00 | 48 | lancer de rayons — **le défaut** |
| `high` | 36,0 | ×1,71 | 28 | rien ; ombres lancées |
| `ultra` | 119,8 | ×6,20 | 8 | rien ; + réflexions et illumination globale |

**Sur lavapipe**, le rastériseur logiciel du conteneur de développement, à
1280 × 720 :

| Palier | ms/image | rapport |
|---|---:|---:|
| `potato` | 114 | ×1,0 |
| `low` | 281 | ×2,5 |
| `medium` | 675 | ×5,9 |
| `high` | 1056 | ×9,3 |
| `ultra` | 2647 | ×23,2 |

Cette page affirmait que « les valeurs absolues ne disent rien d'un vrai GPU,
mais les rapports se transposent ». **C'est faux, et c'est mesuré.** Rapportés
à `medium`, les deux colonnes ne se ressemblent pas :

| Palier | rapport lavapipe | rapport M1 |
|---|---:|---:|
| `potato` | ×0,17 | ×0,30 |
| `low` | ×0,42 | ×0,31 |
| `high` | ×1,56 | ×1,71 |
| `ultra` | ×3,92 | ×6,20 |

L'écart le plus utile n'est pas celui de l'`ultra` : c'est que **`potato` et
`low` coûtent la même chose sur du vrai matériel**, là où le logiciel les
séparait d'un facteur 2,5. Ce qui les distingue — brouillard volumétrique,
occlusion ambiante, poussière — ne pèse presque rien à côté de ce qui leur reste
commun : l'écriture du G-buffer et une passe d'éclairage à quarante-sept sources
sur chaque pixel. Personne ne devrait donc choisir `potato` : il coûte autant
que `low` et rend moins.

La raison de fond est simple : un rastériseur logiciel et un GPU n'ont pas le
même goulot. L'un compte les instructions, l'autre la bande passante. Un palier
qui économise du calcul par pixel se voit beaucoup sur le premier et peu sur le
second.

L'échelle de rendu est le second levier, et le moins visible, parce que le tone
mapping, le halo et la couche 2D travaillent après. Au palier `medium`, sur M1,
fenêtre par défaut (cible 3200 × 1800 sur un écran Retina) :

| Échelle | ms/image | gain |
|---|---:|---:|
| 1,0 | 81,3 | — |
| 0,75 | 48,4 | −40 % |
| 0,5 | 22,5 | −72 % |

**`--scale` n'a pas de valeur par défaut à elle** : sans l'option, c'est le
PALIER qui la fixe — 0,50 à `medium`. C'est ce qui explique qu'une même machine
rende 48 images/s au défaut et 12 en passant `--scale=1.0`.

En dessous de 0,5, le texte des écrans de bornes cesse d'être lisible.

### En jeu

`Échap` ouvre le **menu de réglages**. Il porte **dix-huit lignes**, dans cet ordre : palier de
qualité (avec son coût relatif mesuré sur M1, de `x0.30` à `x6.20`), échelle de rendu (avec le
pourcentage de pixels économisé), densité de poussière, luminosité, **définition**, **plein
écran**, les six volumes — général, musique, effets, ambiance, pas, fond de salle —, sensibilité
de la souris, temps réel, puis les deux pages d'information **COMMANDES** et **CREDITS**,
reprendre et quitter. Les flèches choisissent et règlent, `Entrée` valide, `Échap` referme.

**La définition et le plein écran se règlent depuis le menu** depuis 17.0.0 : il n'est plus
nécessaire d'éditer `settings.cfg` à la main pour changer de résolution. Les deux sont gardés
comme les autres réglages.

Les deux pages d'information s'ouvrent aussi en ligne de commande, pour la recette :
`--menu=14` donne `COMMANDES`, `--menu=15` donne `CREDITS`. **`--menu=` attend un numéro de
ligne, pas un libellé** — `--menu=CREDITS` ne produit pas d'erreur, il vaut `--menu=0` et ouvre
le menu sur la première ligne. Et un numéro se décale dès qu'on ajoute un réglage : vérifiez la
capture plutôt que le numéro.

La salle **continue de vivre derrière le voile** — c'est ce qui permet de juger
un réglage pendant qu'on le change plutôt qu'après l'avoir fermé. Seul le joueur
est figé, et une partie en cours est mise en pause.

| Touche | Effet |
|---|---|
| `Échap` | le menu de réglages |
| `F5` | caméra libre / joueur |
| `F6` | caméra orbite |
| `F7` | palier de qualité suivant (raccourci) |
| `F8` | échelle de rendu, par pas de 0,1 entre 0,5 et 1,0 (raccourci) |
| `F2` | capture d'écran |

**Les réglages sont gardés** d'une session à l'autre, dans `settings.cfg` du
répertoire utilisateur. Ce n'est pas un détail : `render.quality` et
`render.scale` étaient des clés réservées depuis M1 que personne ne lisait — un
réglage qu'on ne peut pas garder n'est pas un réglage, c'est une option de ligne
de commande.

`F7` et `F8` restent comme raccourcis : quand on compare deux paliers d'affilée,
on gagne l'ouverture du menu — et c'est exactement ce qu'on fait en cherchant le
bon réglage.
