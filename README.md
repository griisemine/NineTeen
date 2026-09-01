# Nineteen

Une salle d'arcade en 3D et ses mini-jeux. Écrite en 2020 pendant des études, reconstruite de
fond en comble : le contenu est le même, tout ce qui le fait tourner a été refait.

![La salle d'arcade rendue par le nouveau moteur](docs/render-salle.png)

---

## Ce que c'est

Quinze bornes dans un hall modélisé sous Blender, huit jeux jouables, un classement en ligne.
Le modèle 3D d'origine, les textures et les sons sont conservés tels quels — c'est le moteur
qui les fait vivre qui a changé.

| | V1 (2020) | V15 |
|---|---|---|
| Rendu | OpenGL en mode immédiat, une lumière fixe | Rendu différé PBR sur l'API GPU de SDL3 — Vulkan (Linux, Windows), Metal (macOS) |
| Éclairage | peint dans les textures | 64 sources déduites du modèle, ombres et illumination globale lancées en compute sur un BVH |
| Simulation | cadencée par l'affichage | pas de temps fixe à 120 Hz avec interpolation |
| Collision | boîtes écrites à la main | capsule balayée contre la géométrie réelle |
| Build | un makefile Unix, des `.o` commités | un arbre CMake, trois plateformes, CI |
| Serveur | PHP, requêtes concaténées | binaire Go unique, PostgreSQL, front embarqué |
| Scores | le client annonce son score | le serveur ouvre la partie et recalcule le score |

---

## Compiler

> **Le jeu n'a besoin ni de serveur, ni de base de données, ni de compte.** Par défaut il ne se
> connecte à rien : sans URL configurée, aucune socket n'est ouverte. Le classement en ligne
> s'active avec `--server=` et n'exige toujours aucun compte — sans jeton il est en lecture seule.
> Tout est dans **[docs/JOUER.md](docs/JOUER.md)** : prérequis, build hors ligne, touches, et
> comment vérifier le verrou soi-même.

Il faut CMake 3.21+, un compilateur C11, et `glslangValidator` pour les shaders. SDL3 est
récupéré automatiquement.

```sh
cmake --preset linux-x64          # ou macos-universal, windows-x64
cmake --build --preset linux-x64
./build/linux-x64/bin/nineteen
```

Dépendances système sur Debian/Ubuntu :

```sh
sudo apt install ninja-build glslang-tools libvulkan-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxkbcommon-dev libxtst-dev libxinerama-dev \
  libwayland-dev wayland-protocols libdecor-0-dev \
  libasound2-dev libpulse-dev libudev-dev libgl1-mesa-dev
```

macOS : `brew install ninja glslang`. Windows : le SDK Vulkan de LunarG fournit
`glslangValidator`.

Sur macOS, le premier build récupère aussi **SPIRV-Cross** : Metal ne consomme pas de SPIR-V,
et `tools/spv2msl` traduit les shaders en MSL aux emplacements de ressources que SDL3 attend.
C'est automatique — l'option `NINETEEN_SHADERCROSS` est activée d'office sur Apple, et
activable ailleurs (`-DNINETEEN_SHADERCROSS=ON`) pour vérifier la traduction sans Mac.

Direct3D 12 n'est pas alimenté : produire du DXIL demanderait DirectXShaderCompiler, que ce
dépôt ne tire pas. Le binaire n'annonce donc pas ce format à SDL, qui choisit Vulkan sur
Windows. Une machine Windows sans pilote Vulkan ne lancera pas le jeu.

Le premier build convertit aussi les assets — la salle passe en glTF, les textures reçoivent
leurs cartes PBR, le BVH est construit. Compter une douzaine de secondes.

### Tester

```sh
ctest --preset linux-x64
```

Le test de rendu **vérifie réellement l'image produite** : il crée un périphérique GPU, dessine,
relit le résultat et contrôle son contenu. Il tourne sans écran ni carte graphique, via le
pilote vidéo `offscreen` de SDL et le rasteriseur Vulkan logiciel — donc en intégration
continue.

### Voir la salle sans écran

```sh
SDL_VIDEO_DRIVER=offscreen ./build/linux-x64/bin/nineteen \
    --headless --screenshot=salle.png --frames=8 \
    --pos=-12,2.2,11.06 --yaw=-8 --pitch=-6 --quality=high
```

Options utiles : `--quality=low|medium|high|ultra`, X
pour inspecter une cible intermédiaire, `--scale=0.75` pour l'échelle de rendu interne.

---

## Le serveur, le site et les paquets, en une commande

```sh
cd server
docker compose up --build          # site sur http://localhost:8080
```

Quatre services : PostgreSQL migré, le serveur, le relais des salons, **et la fabrication du
jeu**. Le premier lancement compile le jeu, ce qui est long. Les suivants ne refont rien tant
que la version n'a pas changé. Ce qui sort part dans `telechargements/`, que le serveur sert
sur sa page « Télécharger ».

Pour monter le serveur seul, sans fabriquer :

```sh
docker compose up --build db server duelrelay
```

**Le jeu est construit par la pile et non avant elle**, parce que son binaire porte l'adresse
du serveur, cuite au build par `-DNINETEEN_SERVER_URL`. Elle vient de `NINETEEN_PUBLIC_URL`,
qui sert aussi au serveur pour l'annoncer sur la page. Une adresse ne se pose pas après coup
sur un paquet déjà fait, donc l'ordre compte : on dit d'abord à la pile quel est son domaine,
elle fabrique ensuite. Sans rien régler, elle prend `http://localhost:8080`, l'adresse
qu'elle publie elle-même.

Aucune machine ne fabrique les trois plateformes. Le `.dmg` se construit sur un Mac,
l'installateur `.exe` sous Windows. On les dépose dans `telechargements/` et la page les
affiche sans qu'on touche à quoi que ce soit, voir `telechargements/LISEZ-MOI.txt`.

Sans Docker :

```sh
NINETEEN_DB_URL='postgres://user:motdepasse@localhost:5432/nineteen?sslmode=require' \
NINETEEN_TELECHARGEMENTS=/chemin/vers/telechargements \
  go run ./cmd/nineteend
```

Le binaire contient le site, les polices, les images et les migrations SQL. Il n'y a rien à
déposer à côté. Voir [docs/DEPLOY.md](docs/DEPLOY.md).

---

## Organisation du dépôt

```
engine/      le moteur : noyau, RHI, rendu, scène, physique, audio
  shaders/   GLSL compilé en SPIR-V et embarqué dans le binaire
tools/       conversion des assets : obj2gltf, texgen, bvhbake
room/        la salle d'arcade — l'exécutable principal
games/       les mini-jeux
assets/      données écrites à la main (affectation des bornes, hitbox)
server/      le serveur Go et le site
tests/       tests du noyau et de rendu
legacy/      le code de 2020, inchangé, gardé comme référence
docs/        audit de sécurité, architecture, journal des versions
```

Les assets dérivés ne sont pas commités : ils sont produits par le build dans
`build/<preset>/assets/`.

---

## L'audit de sécurité

Le code d'origine a été audité ligne à ligne : [docs/SECURITY_AUDIT.md](docs/SECURITY_AUDIT.md).
Vingt-deux constats, sept critiques, chacun rattaché à un emplacement précis, avec son scénario
d'exploitation et le correctif appliqué.

Deux d'entre eux méritent le détour même si l'on ne s'intéresse pas au reste :

- **NIN-09** — un dépassement de pile d'un octet dans le calcul du jeton d'authentification,
  présent à chaque requête depuis cinq ans. Prouvé plutôt qu'affirmé : la reproduction est dans
  [docs/audit/nin09-repro.c](docs/audit/nin09-repro.c) et se vérifie en une commande.
- **NIN-22** — un débordement de tas déclenchable depuis le réseau, dont le garde de sécurité
  était écrit à l'envers, et qu'une *autre* erreur du programme masquait partiellement.

L'audit publie aussi les trois hypothèses qu'il a **écartées** après vérification. Un rapport
qui ne montre que ses succès n'est pas vérifiable.

---

## État des travaux

Le portage des mini-jeux depuis `SDL_Renderer` vers la couche sprite du nouveau moteur est en
cours ; le journal des versions ([docs/CHANGELOG-V15.md](docs/CHANGELOG-V15.md)) dit
précisément ce qui tourne et ce qui reste à faire.

---

## Licence et attribution

Le jeu, son modèle 3D, ses textures, ses sons et ses polices sont l'œuvre de l'auteur d'origine.
Les dépendances tierces et leurs licences sont listées dans
[third_party/README.md](third_party/README.md).
