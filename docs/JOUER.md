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
| `Échap` | le menu de réglages — qualité, échelle de rendu, poussière, luminosité, les quatre volumes, la souris |
| `F2` | capture d'écran dans le répertoire utilisateur |
| `F5` | basculer caméra joueur / caméra libre |
| `F6` | caméra orbite |

Les touches de déplacement sont lues par **position physique** et non par lettre : le bloc en haut
à gauche du clavier avance, quelle que soit la disposition. C'est le même bloc de touches qui
s'appelle ZQSD en AZERTY et WASD en QWERTY — il n'y a pas deux liaisons, il y en a une.

En caméra libre (`F5`), `Espace` et `Ctrl` montent et descendent au lieu de sauter et de
s'accroupir : ce mode n'a ni gravité ni collision, c'est un outil de cadrage.

**Jouer sur une borne** : approchez-vous, `E` insère un jeton et la partie démarre **dans la
dalle** — on reste en 3D, la tête reste libre, on voit l'écran à travers son verre bombé. Deux
jeux sont portés : **Flappy Bird** (`Espace`) et **Snake** (les flèches, maintenues : le serpent
tourne tant qu'on tient). `Échap` sort de la partie, pas du jeu.

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

Le **Tetris** de 2020 ne compte pas comme les autres, et c'est voulu : chaque ligne
simultanée vaut le **double** de la précédente — un quadruple fait donc 1 500 et non 400 — et une
ligne d'**une seule couleur** vaut **dix fois** son total. Viser la couleur rapporte plus que viser
le quadruple.

Pour voir un jeu sans traverser la salle : `--game=flappy`, `--game=snake`, `--game=demineur`,
`--game=tetris`, `--game=asteroid`, `--game=pacman`, `--game=piano` ou `--game=shooter` démarre directement en plein écran. Sans écran (`--headless`), la graine est fixe :
la même commande rend exactement la même image.

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

On y trouve `nineteen.log`, les captures de `F2`, et `settings.cfg` — ce dernier n'étant écrit que si
un réglage a changé.

## Ce qui manque encore, dit franchement

Le jeu se parcourt, se joue, et se règle. Ce qui manque :

- ~~les mini-jeux~~ : **les huit sont portés**, et les dix-neuf bornes de la salle jouent
  toutes ;
- **le transport réseau**, délibérément : voir la section précédente ;
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

Deux détails que cette vérification a mis au jour, et qu'aucun test unitaire
n'aurait attrapés parce qu'ils vivent *entre* les deux moitiés du projet : le
serveur nomme ses tableaux `flappy-easy` / `flappy-hard` là où le moteur porte un
jeu et une difficulté séparés (la traduction se fait maintenant contre la liste
que le serveur renvoie), et la graine de partie fait 63 bits, donc la relire dans
un `float` la détruisait silencieusement.

### `--offline`

```sh
./build/linux-x64/bin/nineteen --offline
```

C'est un **verrou**, pas un repli : il interdit toute mise en file d'envoi, même
quand le transport existera. Aujourd'hui il ne change rien au comportement
observable — une partie sans secret de serveur n'est de toute façon pas mise en
file — et il est là pour que la garantie soit exprimable dès maintenant plutôt
que rajoutée après coup.

## Installer, ou fabriquer un paquet

Le jeu se lance très bien depuis l'arbre de build — c'est ce que fait tout le
reste de cette page. Pour le donner à quelqu'un, il faut un paquet :

```sh
cmake --preset linux-x64 -DCMAKE_BUILD_TYPE=Release
cmake --build --preset linux-x64
cpack --config build/linux-x64/CPackConfig.cmake -B build/linux-x64/paquets
```

Une archive par plateforme, autonome : `bin/nineteen` et `bin/assets/` côte à
côte, ce qui est exactement la disposition que `ns_paths` cherche. Déballer et
lancer, rien à installer, rien à configurer.

**Ce que ça pèse : 153 Mio compressés.** C'était 421 : les cartes de normales et
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

**Vérifié plutôt qu'affirmé** : l'archive a été déballée et le jeu lancé depuis
l'arbre déballé, **avec les assets du build masqués** pour qu'aucun montage de
développement ne puisse le sauver. C'est ce test qui a montré que la règle
d'installation n'emportait que le binaire : `cmake --install` produisait un
arbre sans une seule texture, et personne ne s'en apercevait parce que personne
n'installait.

La signature est affaire de certificats, donc de qui publie : le workflow
`.github/workflows/release.yml` signe si les secrets existent et produit des
paquets **non signés** sinon, en le disant. Un paquet non signé se télécharge et
se lance, mais macOS le met en quarantaine et Windows affiche SmartScreen.

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

`Échap` ouvre le **menu de réglages** : palier de qualité (avec son coût relatif
mesuré, de `x0.17` à `x3.92`), échelle de rendu (avec le pourcentage de pixels
économisé), densité de poussière, luminosité, les quatre volumes et la
sensibilité de la souris. Les flèches choisissent et règlent, `Entrée` valide,
`Échap` referme.

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
