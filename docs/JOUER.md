# Jouer — sans serveur, sans base de données, sans compte

Le jeu n'a besoin de rien d'autre que lui-même. Le serveur Go et PostgreSQL ne servent qu'au site
web et au classement en ligne ; ils sont facultatifs et le jeu ne les cherche pas.

## Ce n'est pas une promesse, c'est vérifiable

Le binaire **ne contient pas le code nécessaire pour ouvrir une connexion** :

```sh
$ ldd build/linux-x64/bin/nineteen
        linux-vdso.so.1
        libm.so.6
        libc.so.6
        /lib64/ld-linux-x86-64.so.2

$ nm -D --undefined-only build/linux-x64/bin/nineteen \
    | grep -iE 'socket|connect|getaddrinfo|recv|send|ssl|curl|tls'
$ echo $?
1        # aucun résultat
```

260 symboles importés en tout, tous de la libc et de libm. **Zéro symbole `SDL_`** : SDL3 est lié
statiquement. Les shaders sont compilés au build et **embarqués dans le binaire** — il n'y
a même pas de fichier à retrouver, encore moins à télécharger. En SPIR-V pour Vulkan ; en MSL
pour Metal, traduit au build par `tools/spv2msl`.

`engine/net/` est un répertoire vide. `NS_CFG_SERVER_URL` existe dans `engine/core/ns_config.h`
comme clé réservée et **rien ne la lit** : même en écrivant `network.serverUrl = …` à la main dans
`settings.cfg`, personne ne viendrait la chercher.

C'est l'inverse de la version de 2020, qui hors ligne ne pouvait pas atteindre sa propre fenêtre —
voir la fin de ce document.

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
| `Échap` | libérer la souris, puis quitter |
| `F2` | capture d'écran dans le répertoire utilisateur |
| `F5` | basculer caméra joueur / caméra libre |
| `F6` | caméra orbite |

Les touches de déplacement sont lues par **position physique** et non par lettre : le bloc en haut
à gauche du clavier avance, quelle que soit la disposition. C'est le même bloc de touches qui
s'appelle ZQSD en AZERTY et WASD en QWERTY — il n'y a pas deux liaisons, il y en a une.

En caméra libre (`F5`), `Espace` et `Ctrl` montent et descendent au lieu de sauter et de
s'accroupir : ce mode n'a ni gravité ni collision, c'est un outil de cadrage.

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

On y trouve `nineteen.log`, les captures de `F2`, et `settings.cfg` — ce dernier n'étant écrit que si
un réglage a changé.

## Ce qui manque encore, dit franchement

Le jeu se parcourt, se joue, et se règle. Ce qui manque :

- **sept des huit mini-jeux.** Flappy Bird est porté et jouable, sur sa borne comme en plein
  écran. Snake, Tetris, Asteroid, Shooter, Démineur, Pac-Man et Piano tournent encore sur le
  code de 2020 dans `legacy/` — environ 9 800 lignes, mécaniques à porter maintenant que la
  couche 2D existe ;
- **le transport réseau**, délibérément : voir la section précédente ;
- **un menu dessiné.** `F7` et `F8` règlent la qualité et l'échelle de rendu, et le choix est
  gardé — mais rien ne s'affiche à l'écran pour le dire ;
- **la réverbération par zone.** Les quatre bus, les sources positionnelles, l'occlusion amortie
  et les pas variés selon le sol fonctionnent ; la réverbe déclarée par pièce, non ;
- **du mobilier importé au-delà de trois modèles.** La mécanique existe et marche ; ce qui la
  limite est expliqué dans `assets/cc0/LICENSES.md`, et c'est une limite de ce que je peux
  vérifier, pas du moteur.

Cette liste a été fausse : elle annonçait encore « aucun son », « aucune interaction » et « la
collision n'est pas branchée » longtemps après que les trois aient été livrés.
`docs/CHANGELOG-V15.md` dit précisément ce qui tourne.

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
score est écrit dans `scores.txt`, dans le répertoire utilisateur du jeu
(`~/.local/share/nineteen/` sous Linux, `~/Library/Application Support/nineteen/`
sous macOS, `%APPDATA%\nineteen\` sous Windows). L'écriture est **atomique** — un
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

**Ce qui n'existe pas encore, délibérément : le transport.** Le binaire
n'importe aucun symbole réseau, et c'est vérifiable :

```sh
nm -D --undefined-only build/linux-x64/bin/nineteen \
  | grep -icE 'socket|connect|getaddrinfo|ssl|curl|tls'   # -> 0
```

Le temps réel — présence, duels — se conçoit avant de s'écrire, et cette
conception reste à faire. En attendant, une partie jouée sans serveur est
classée localement et **rien n'échoue**.

### `--offline`

```sh
./build/linux-x64/bin/nineteen --offline
```

C'est un **verrou**, pas un repli : il interdit toute mise en file d'envoi, même
quand le transport existera. Aujourd'hui il ne change rien au comportement
observable — une partie sans secret de serveur n'est de toute façon pas mise en
file — et il est là pour que la garantie soit exprimable dès maintenant plutôt
que rajoutée après coup.

## Régler la fluidité

### Les cinq paliers, chiffrés

Mesurés avec `--bench`, qui **attend réellement le GPU** — sans cette attente on
chronomètre l'enregistrement des commandes, pas leur exécution, et c'est ainsi
qu'un rendu à une image par seconde avait pu être annoncé à 1 793. 1280 × 720,
point de vue `allee`, sur lavapipe (le rasteriseur logiciel de ce conteneur) :

| Palier | ms/image | rapport | ce qu'il coupe |
|---|---:|---:|---|
| `potato` | 114 | ×1,0 | volumétrique, occlusion ambiante, rendu à 60 % |
| `low` | 281 | ×2,5 | volumétrique, rendu à 75 % |
| `medium` | 675 | ×5,9 | lancer de rayons — **le défaut** |
| `high` | 1056 | ×9,3 | rien ; ombres lancées |
| `ultra` | 2647 | ×23,2 | rien ; + réflexions et illumination globale |

**Les valeurs absolues ne disent rien d'un vrai GPU** — lavapipe calcule sur le
processeur. Ce sont les rapports qui se transposent.

L'échelle de rendu est le second levier, et le moins visible, parce que le tone
mapping, le halo et la couche 2D travaillent après. Au palier `medium` :

| Échelle | ms/image | gain |
|---|---:|---:|
| 1,0 | 649 | — |
| 0,8 | 427 | −34 % |
| 0,6 | 248 | −62 % |

En dessous de 0,5, le texte des écrans de bornes cesse d'être lisible.

### En jeu

| Touche | Effet |
|---|---|
| `F5` | caméra libre / joueur |
| `F6` | caméra orbite |
| `F7` | palier de qualité suivant |
| `F8` | échelle de rendu, par pas de 0,1 entre 0,5 et 1,0 |
| `F2` | capture d'écran |

**Les réglages sont gardés** d'une session à l'autre, dans `settings.cfg` du
répertoire utilisateur. Ce n'est pas un détail : `render.quality` et
`render.scale` étaient des clés réservées depuis M1 que personne ne lisait — un
réglage qu'on ne peut pas garder n'est pas un réglage, c'est une option de ligne
de commande.

Ce qui n'existe pas : un **menu dessiné**. Ces deux touches donnent ce qui
manquait — pouvoir essayer un palier sur sa propre machine — et le menu reste à
faire.
