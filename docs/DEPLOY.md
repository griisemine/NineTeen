# Déploiement

Deux artefacts indépendants : le jeu, que les joueurs téléchargent, et le serveur, qui héberge
comptes, classement et site.

---

## Le serveur

### Avec docker-compose

```sh
cd server
cp .env.example .env      # renseigner NINETEEN_DB_PASSWORD
docker compose up --build
```

Le site répond sur `http://localhost:8080`, la base est migrée et les jeux insérés au premier
démarrage.

Deux choix de la composition méritent d'être notés, parce qu'ils sont volontaires :

- **Le port de PostgreSQL n'est pas publié.** Seule l'application y accède, par le réseau
  interne. Exposer une base sur l'hôte est un réflexe fréquent qui n'apporte rien ici.
- **Le conteneur applicatif est en lecture seule, sans capacités, sur une image distroless** —
  ni shell ni gestionnaire de paquets à l'intérieur.

### Le média du site

Toutes les images et vidéos du site sortent du moteur. Rien n'est importé : le dépôt a déjà dû
retirer treize images tierces de son paquet, et la politique de contenu du serveur interdit
d'ailleurs toute origine extérieure.

Une commande régénère l'ensemble :

```sh
cmake --build --preset macos-universal -j8     # le média vient du binaire
python3 tools/site-media.py
```

Elle écrit dans `server/internal/web/assets/` — `media/` pour les vidéos et leurs affiches,
`img/` pour les photos de bornes et les vues de salle — plus `media/manifeste.json`, que le site
lit pour bâtir ses galeries.

Ce qu'elle produit, et d'où ça vient :

| Média | Commande du moteur | Définition |
|---|---|---|
| plan d'accueil | `--sequence --view=orbite --camera=orbit --angle=90` | 1280×720, 9,9 s en boucle |
| huit boucles de jeu | `--sequence --play-at=BORNE` | 640×360, 7,9 s en boucle |
| dix-neuf photos de bornes | `--screenshot --play-at=BORNE` | 640×360 |
| huit vues de salle | `--screenshot --view=NOM` | 1280×720 |

`--sequence=PREFIXE` est le drapeau ajouté au jeu pour ça : il écrit une image PNG numérotée par
image rendue, et fait avancer le temps d'un pas **fixe** (`--sequence-fps=`, 30 par défaut). Sans
ce pas fixe la vitesse du film dépendrait de la machine : mesuré, le rendu hors écran tourne à
760 images par seconde sur Metal, donc l'horloge murale ferait avancer la simulation de 1,3 ms
par image et 200 images filmeraient un quart de seconde de jeu.

**Le script refuse de produire une image ratée.** Chaque PNG est relu — définition exacte, et
luminance mesurée par ffmpeg — et le script s'arrête net si l'image est noire, unie, ou pas à la
bonne taille. Une capture noire est déjà entrée dans ce dépôt sans que personne ne la voie. Les
seuils sont calés sur des mesures : la plus sombre vue légitime de cette salle donne une médiane
de 25 et une moyenne de 48, le seuil d'échec est à 3 et 10.

Les fichiers produits **sont commités**. Ils doivent l'être : le site est servi depuis un
`embed.FS`, et le `Dockerfile` ne copie que `server/` — un média construit à la volée n'existerait
donc pas dans l'image, et le site rendrait des 404. Le contrôle de non-régression est dans
`server/internal/web/site_test.go`.

Dépendances : `ffmpeg` et `ffprobe` (mesurés en 9.0.1). Ce ffmpeg-là n'a **pas** d'encodeur WebP ;
les affiches sortent donc en AVIF avec un JPEG de repli, ce qui couvre les mêmes navigateurs.

### Sans Docker

Le binaire est autonome : le site, les polices et les migrations sont dedans.

```sh
cd server
go build -trimpath -ldflags="-s -w" -o nineteend ./cmd/nineteend

NINETEEN_DB_URL='postgres://nineteen:motdepasse@localhost:5432/nineteen?sslmode=require' \
NINETEEN_SECURE=1 \
NINETEEN_LOG=json \
  ./nineteend -addr 127.0.0.1:8080
```

### Variables

| Variable | Rôle |
|---|---|
| `NINETEEN_DB_URL` | URL PostgreSQL. **Requis.** Le mot de passe est masqué dans les journaux. |
| `NINETEEN_ADDR` | adresse d'écoute, défaut `:8080` |
| `NINETEEN_INSECURE_OK` | autorise une écoute **publique sans cookies `Secure`**. Le serveur refuse de démarrer dans ce cas, parce qu'un oubli de `-secure` sur une adresse joignable envoie le cookie de session en clair et que le seul symptôme serait l'absence d'un attribut que personne ne lit. L'écoute en boucle locale est exemptée — un navigateur ignore un cookie `Secure` reçu sur `http://`, donc l'exiger en développement rendrait la session impossible à établir. Le `docker-compose` fourni pose ce drapeau et dit pourquoi : dans un conteneur il FAUT écouter sur toutes les interfaces, et c'est la publication `127.0.0.1:8080:8080` qui borne l'exposition |
| `NINETEEN_SECURE` | à définir derrière HTTPS : active les cookies `Secure` et HSTS |
| `NINETEEN_LOG` | `text` ou `json` |
| `NINETEEN_PUBLIC_URL` | l'adresse que **le jeu** doit viser, annoncée au joueur sur la page de téléchargement. Vide = rien n'est annoncé. Voir ci-dessous |
| `NINETEEN_TELECHARGEMENTS` | le répertoire des paquets **que ce serveur sert lui-même**. Ce qui s'y trouve est offert, ce qui ne s'y trouve pas n'est pas annoncé : la page lit un `os.ReadDir`, pas une variable. Vide = ce serveur n'héberge aucun paquet. Voir ci-dessous |
| `NINETEEN_RELEASE_PUBLIEE` | à `1`, **et seulement si le répertoire ci-dessus est vide**, la page donne les liens de la release GitHub |
| `NINETEEN_DUEL_ADDR` | adresse d'écoute du **relais temps réel** (duel à deux et arène à N places), sur son propre port. **Vide par défaut : rien ne s'ouvre.** Voir ci-dessous |
| `NINETEEN_DUEL_PUBLIC` | l'adresse `hote:port` du relais **telle qu'on l'annonce aux joueurs**, distincte de celle d'écoute comme `NINETEEN_PUBLIC_URL` l'est de `NINETEEN_ADDR`. **C'est elle qui ouvre les salons du Couperet** ; vide, les routes `/api/v1/salons` répondent 503. Voir ci-dessous |
| `NINETEEN_BIND`, `NINETEEN_PORT` | *(docker-compose seulement)* interface et port **publiés** sur l'hôte. Défaut `127.0.0.1` et `8080` : le service ne sort pas de la machine tant que personne ne l'a demandé |

#### `NINETEEN_PUBLIC_URL` — l'adresse que le serveur annonce au joueur

Elle ne se déduit **pas** de `NINETEEN_ADDR`. Le serveur écoute sur `:8080` *dans* le conteneur ;
le monde le joint par la publication Docker, ou par un proxy, sur un tout autre nom. Seul
l'exploitant sait laquelle est la bonne — et sans elle, la page livrait un binaire et aucun moyen
de savoir quoi lui donner.

Posée, elle sort par `GET /api/v1/version` et la page de téléchargement écrit la ligne complète
sous le bouton :

```
nineteen --server=http://arcade.example:8080
```

**Elle doit être en `http`.** Le client refuse `https://` — `ns_http_parse_url` échoue
explicitement, plutôt que de parler en clair sur un port TLS — donc annoncer une adresse `https`
donnerait au joueur une URL que son jeu ne sait pas ouvrir. Le serveur contrôle la valeur au
démarrage et **refuse de l'annoncer** si elle ne convient pas, en disant pourquoi :

```
level=ERROR msg="NINETEEN_PUBLIC_URL en https : le jeu ne sait pas l'ouvrir, rien ne sera annoncé"
```

Le reste du site, lui, n'a besoin de rien : ses scripts appellent l'API en relatif avec
`credentials: "same-origin"`, donc il fonctionne sur n'importe quel hôte sans savoir son propre
nom. `TestLeSiteNeSupposePasSonPropreHote` interdit qu'un `localhost` y revienne.

**Elle sert une seconde fois, et c'est ce qui change l'ordre des opérations.** Le service
`paquets` du `docker-compose` la passe à `-DNINETEEN_SERVER_URL` : le binaire livré vise donc
déjà ce serveur, sans que le joueur ait rien à taper. Une adresse ne se pose pas après coup sur
un paquet déjà fabriqué, donc le jeu est construit **par** la pile et non avant elle. Le joueur
garde le dernier mot : la configuration, la variable d'environnement et `--server=` battent ce
défaut, dans cet ordre.

#### `NINETEEN_TELECHARGEMENTS` — les paquets servis par ce serveur

La page bâtissait trois liens vers `github.com/.../releases/download/v<version>/…` à partir du
seul numéro de version. Conséquence mesurée : qui montait la pile avec `docker compose up
--build` obtenait un site complet et **zéro bouton de téléchargement**, parce qu'aucune release
n'existe pour la version qu'il vient de construire. Le projet se lançait, et ne se distribuait
pas.

La vérité est maintenant un répertoire.

```
GET /api/v1/telechargements
{"ok":true,"version":"17.0.0","source":"locale","serveur":"http://localhost:8080",
 "fichiers":[{"nom":"nineteen_17.0.0_arm64.deb","url":"/telechargements/nineteen_17.0.0_arm64.deb",
              "plateforme":"linux","arch":"arm64","format":"paquet .deb",
              "octets":175580032,"sha256":"dea235e8…"}],
 "manifestes":[{"nom":"SIGNATURE-linux.txt","url":"/telechargements/SIGNATURE-linux.txt","octets":874}]}
```

`source` vaut `locale` (un répertoire garni), `github` (rien en local, mais
`NINETEEN_RELEASE_PUBLIEE=1`) ou `aucune`. **Le serveur tranche, la page affiche.** C'est le
déplacement qui compte : la décision se prend une fois, en Go, là où elle se teste.

La plateforme et l'architecture sont lues **dans le nom du fichier**, et nulle part ailleurs
(`telechargements.Classer`). La somme SHA-256 est calculée par le serveur en fond, une fois par
fichier, et republiée sur la page. Un paquet qui vient d'apparaître s'affiche donc avant sa
somme, pendant quelques secondes : un fichier téléchargeable tout de suite vaut mieux qu'un
fichier retenu le temps d'un calcul.

Le délai d'écriture de `http.Server` est **levé sur cette route seule**. Les 60 s par défaut sont
le bon réglage pour une API JSON et une coupure nette pour un fichier de 175,6 Mio : le tenir
demanderait 2,9 Mio/s soutenus, soit 23 Mbit/s, et toute connexion plus lente recevrait un
fichier tronqué sans le moindre message. La route accepte aussi les requêtes par plage, donc la
reprise d'un téléchargement interrompu.

Les identifiants ne sont jamais dans le code. Le dépôt d'origine les gardait en clair dans un
fichier commité — ils sont donc encore dans l'historique git et **doivent être changés**.

#### `NINETEEN_DUEL_ADDR` — le relais temps réel

**Vide par défaut, et ça ne changera pas** : un serveur de classement n'a aucune raison d'ouvrir
un port de plus tant que personne ne joue en direct. La promesse « rien ne s'ouvre sans qu'on le
demande » vaut aussi pour le serveur.

```sh
NINETEEN_DUEL_ADDR=0.0.0.0:8081 ./nineteend …     # dans nineteend
./duelrelay -addr 0.0.0.0:8081                    # ou tout seul, sans base
```

**Un seul port pour les deux modes.** Le duel à deux places et l'arène à 2–8 places sont le même
relais : même cadrage, même graine tirée par `crypto/rand`, un type de trame de plus. Il n'y a
donc ni seconde variable, ni second processus à déployer pour l'arène — voir
`docs/RESEAU-TEMPS-REEL.md`.

**Ce relais n'a aucune autorité.** Il ne connaît ni score, ni classement, ni règle de jeu : il
apparie et recopie. L'autorité sur les scores enregistrés reste le journal scellé par HMAC, envoyé
par HTTP et recalculé par le serveur. C'est pourquoi il peut vivre ailleurs que le classement, et
pourquoi `duelrelay` existe comme binaire séparé : il ne touche à aucune base.

Trois conséquences pour l'exploitant :

- **Ce n'est pas du HTTP.** Un reverse proxy qui ne relaie que le port 8080 ne le transporte pas ;
  c'est un port TCP à publier, ou à ne pas publier.
- **Il ne parle pas TLS**, comme le reste du réseau de ce projet et pour la même raison (voir
  `engine/net/ns_http.h`).
- **La mémoire est bornée par construction** : 512 places ouvertes au maximum, toutes sessions
  confondues, soit 67,4 Mio dans le pire cas — 256 duels, ou 64 salons de huit, ou n'importe quel
  mélange. Le calcul est écrit dans `server/internal/duel/relay.go`.

Le `docker-compose` fourni **ne le publie pas** et ne transmet pas la variable : la composition
sert le site et le classement. Un relais se déploie à côté, avec `duelrelay`, ou en ajoutant
soi-même l'entrée `ports` qui va bien.

#### `NINETEEN_DUEL_PUBLIC` — l'adresse du relais annoncée aux joueurs

Elle ne se déduit **pas** de `NINETEEN_DUEL_ADDR`, exactement comme `NINETEEN_PUBLIC_URL` ne se
déduit pas de `NINETEEN_ADDR` : l'une dit où le processus se pose — `0.0.0.0:8081` dans un
conteneur — l'autre où le monde le joint. Seul l'exploitant connaît la seconde.

```sh
NINETEEN_DUEL_PUBLIC=arcade.example:8081 ./nineteend …
```

**C'est elle qui ouvre le service de salons.** Le mode compétitif se rejoignait jusqu'ici en
convenant hors bande d'un numéro de salon et d'un numéro de place, tapés en ligne de commande.
Le serveur tient maintenant un rendez-vous : `POST /api/v1/salons` rend un code de six caractères,
`POST /api/v1/salons/{code}/join` attribue une place, et la réponse porte l'adresse du relais avec
l'identifiant de session à y présenter. Sans cette variable, le serveur n'a pas d'adresse à mettre
dans cette réponse : les sept routes de salon répondent **503**, plutôt que d'annoncer une adresse
devinée. C'est le même sens sûr que partout ailleurs ici — un serveur lancé sans rien dire
n'affirme rien.

Le serveur la contrôle au démarrage et refuse de l'annoncer si elle ne convient pas, en disant
pourquoi. Deux refus valent d'être connus :

- **un schéma** (`http://arcade.example:8081`) — le relais est un protocole binaire sur TCP, pas
  du HTTP ;
- **une adresse d'écoute** (`:8081`, `0.0.0.0:8081`, `[::]:8081`) — « toutes les interfaces » ne
  désigne aucune machine vue du joueur, et l'annoncer enverrait chaque client se connecter à
  lui-même. C'est la faute la plus facile à faire : c'est la valeur d'à côté.

Un relais qui écoute sans être annoncé produit un avertissement au démarrage, plutôt que de
laisser chercher pourquoi les salons répondent 503.

### Derrière un reverse proxy

Le serveur lit l'adresse cliente dans `RemoteAddr`, jamais dans `X-Forwarded-For` — cet en-tête
est écrit par le client et ne prouve rien. C'est donc au proxy de réécrire l'adresse réelle
(`proxy_protocol` avec nginx, ou un intergiciel de confiance explicite).

Terminer le TLS au proxy, définir `NINETEEN_SECURE=1`, et laisser le serveur écouter sur la
boucle locale.

### Sauvegarde

Tout l'état est dans PostgreSQL.

```sh
docker compose exec db pg_dump -U nineteen nineteen | gzip > nineteen-$(date +%F).sql.gz
```

Les migrations s'appliquent au démarrage, une fois chacune, dans une transaction : une migration
qui échoue laisse la base dans son état précédent.

---

## Le jeu

### Compiler pour distribution

```sh
cmake --preset linux-x64 -DCMAKE_BUILD_TYPE=Release
cmake --build --preset linux-x64
```

Le binaire produit dans `build/<preset>/bin/` a besoin des assets convertis, dans
`build/<preset>/assets/`. Un paquet contient donc les deux, l'exécutable cherchant ses données
dans `assets/` à côté de lui puis dans le répertoire du binaire.

### Fabriquer les paquets

Une balise `v*` déclenche `.github/workflows/release.yml`, qui produit les cinq artefacts. À la
main, depuis la machine de développement :

```sh
# macOS — le .dmg. `cpack` signe, écrit le lisez-moi et le manifeste lui-même.
cmake --preset macos-universal -DCMAKE_BUILD_TYPE=Release
cmake --build --preset macos-universal
cpack --config build/macos-universal/CPackConfig.cmake -B build/macos-universal/paquets

# Linux — .tar.gz, .deb et AppImage, dans le conteneur qui fixe la glibc à 2.35
docker build -f packaging/linux/Dockerfile.build -t nineteen-build:22.04 packaging/linux
docker run --rm -v "$PWD:/src" -w /src nineteen-build:22.04 sh packaging/linux/paquets.sh
```

**Les paquets Linux portent l'architecture de la machine qui les a produits**, lue par `uname -m`
et jamais supposée. Sur un Mac Apple Silicon, Docker est natif `arm64` : la commande ci-dessus
donne `nineteen_17.0.0_arm64.deb` et `Nineteen-17.0.0-aarch64.AppImage`, qui **ne s'installent
pas** sur un PC. Les paquets publiés sortent d'un runner `ubuntu-24.04`, donc `x86_64`.

Windows ne se fabrique **pas** ici : cette machine n'a ni `makensis`, ni `mingw-w64`, ni `wine`,
et `cpack -G NSIS` s'arrête avant même de lire la configuration. Ce qui s'en vérifie depuis un
Mac, c'est la configuration, par le test `paquets` (`packaging/verifier.cmake`).

### Signature des paquets

**Aucun de ces paquets n'est signé aujourd'hui**, et c'est une décision : le certificat qui évite
l'avertissement s'achète, et il n'y en a pas. La chaîne fabrique donc des paquets de production
non signés **sans échouer**, et signe automatiquement le jour où les variables existent.

| | variables absentes | variables présentes |
|---|---|---|
| macOS | signature **ad hoc**, sans certificat | Developer ID + notarisation + agrafage |
| Windows | rien | Authenticode sur le binaire **et** sur l'installateur |
| Linux | rien | signature GPG détachée `.asc` par fichier |

Les noms et le rôle de chaque variable sont dans le `.env.example` de la racine, section
« Signature des paquets ». Ils ne sont pas répétés ici.

**Un compte développeur Apple gratuit ne suffit pas.** Il ne délivre qu'une identité *Apple
Development*, faite pour lancer une application sur ses propres machines. Mesuré : bundle signé
avec elle, chaîne complète jusqu'à *Apple Root CA*, et `spctl -a -vv` répond quand même
`rejected`. Il faut le programme payant, pour le certificat *Developer ID Application* **et** la
notarisation — les deux, pas l'un des deux.

**Ce que la signature ad hoc fait, et ce qu'elle ne fait pas.** Elle sert à une chose : sur Apple
Silicon, un Mach-O `arm64` sans aucune signature n'est pas chargé — le noyau le tue. Mesuré sur
un Mac M-série, même arbre, sans quarantaine : signature retirée → tué (code 137) ; signature ad
hoc → démarre. Elle ne fait passer **ni** Gatekeeper **ni** la notarisation : sous quarantaine,
l'application ad hoc est tuée de la même façon. Le raisonnement complet et les mesures sont en
commentaire dans `packaging/macos/signature.cmake`.

**Ce que le joueur doit faire**, par plateforme, tant que rien n'est signé :

| Système | Ce qu'il voit | Le geste |
|---|---|---|
| macOS | « Apple could not verify "Nineteen" is free of malware… » | `xattr -dr com.apple.quarantine /Applications/Nineteen.app` |
| Windows | l'écran bleu SmartScreen | **Informations complémentaires** → **Exécuter quand même** |
| Linux | rien | rien |

Le geste macOS est **vérifié** : lancement tué sous quarantaine, lancement et rendu d'une image
après la commande. Le bouton **Ouvrir quand même** de *Réglages Système → Confidentialité et
sécurité* fait la même chose et c'est la voie qu'Apple documente, mais il n'a pas pu être essayé
depuis un script — il n'est donc écrit ni dans le `.dmg` ni sur la page de téléchargement, où une
marche à suivre fausse coûterait plus cher qu'une marche à suivre austère.

**Où l'information voyage.** Un artefact signé et un artefact non signé ne doivent pas être
confondables. Le nom de fichier reste **stable** — le site bâtit ses liens à partir du seul
numéro de version, et un suffixe qui disparaîtrait le jour de la bascule casserait ces liens
exactement comme ils l'ont déjà été. L'état est donc porté par trois choses :

- `SIGNATURE-<plateforme>.txt`, publié à côté des paquets, écrit dans **les deux** cas — jamais
  absent — avec les sommes SHA-256 que la page de téléchargement promettait sans les produire ;
- `A-LIRE-AVANT-D-OUVRIR.txt`, **dans** le `.dmg`, premier dans la fenêtre du volume ;
- le paquet lui-même : `codesign -dv` rend `Signature=adhoc` ou nomme l'autorité. Celui-là ne
  peut pas être séparé du fichier.

### Vérifier une installation

```sh
SDL_VIDEO_DRIVER=offscreen ./nineteen --headless --screenshot=/tmp/verif.png --frames=4
```

---

## Brancher la salle sur le serveur

Le jeu joue toujours **sans** serveur : sans URL, aucune socket n'est ouverte. Le classement en
ligne est un ajout, et il se règle en deux clés.

### 1. Un compte, et son jeton

Le classement se **lit** sans compte. Pour qu'une partie s'y **inscrive**, il faut un jeton de
session. On l'obtient en créant un compte — depuis le site, ou en une requête :

```sh
curl -s -X POST http://localhost:8080/api/v1/auth/register \
     -H 'Content-Type: application/json' \
     -d '{"username":"VOTRE_NOM","password":"au moins douze caracteres"}'
# → {"ok":true,"sessionKey":"…","username":"…"}
```

`sessionKey` est le jeton. Il vaut trente jours.

### 2. Le jeu

#### Les quatre sources de l'adresse, et leur ordre

L'URL peut venir de quatre endroits. Du plus **faible** au plus **fort** :

| # | Source | Où | Change sans… |
|---|---|---|---|
| 1 | défaut compilé | `cmake --preset macos-universal -DNINETEEN_SERVER_URL=http://arcade.example:8080` puis `cmake --build --preset macos-universal` | — (il faut recompiler) |
| 2 | configuration | `network.serverUrl` dans `settings.cfg` | recompiler |
| 3 | environnement | `NINETEEN_SERVER_URL=http://…` au lancement | recompiler ni éditer un fichier utilisateur |
| 4 | ligne de commande | `--server=http://…` | rien du tout |

```
défaut compilé  <  config  <  variable d'environnement  <  --server=
```

`--offline` (ou `NINETEEN_OFFLINE=1`) **n'est pas un cinquième niveau** : c'est un verrou. Il
s'applique après, quelle que soit la source retenue, et le fil réseau ne démarre pas.

**Pourquoi cet ordre-là.** Le défaut compilé est en bas parce que c'est le seul qu'on ne peut pas
changer sans refaire une compilation : un paquet livré doit rester surchargeable. Et
l'environnement bat la configuration parce que `settings.cfg` vit dans le répertoire utilisateur
— il n'existe pas dans un conteneur, et sur une machine de développement il garde ce qu'une
session précédente y a laissé. Si la config gagnait, un `docker run -e NINETEEN_SERVER_URL=…`
serait ignoré **sans un mot** le jour où un fichier traîne. Dans l'autre sens, le pire qui arrive
est qu'une variable explicitement posée l'emporte, ce qu'on a demandé en la posant.

C'est le même ordre que pour tous les autres réglages du jeu (`nineteen.env`, `.env.example`) :
la ligne de commande garde toujours le dernier mot.

#### Le défaut compilé est **vide**, et ça ne changera pas

```sh
# → aucune socket, jamais
cmake --preset macos-universal
cmake --build --preset macos-universal

# → parle à ce serveur
cmake --preset macos-universal -DNINETEEN_SERVER_URL=http://arcade:8080
cmake --build --preset macos-universal
```

Les deux lignes vont **toujours** par paire : `cmake --preset` configure et s'arrête, c'est
`cmake --build` qui recuit l'adresse dans le binaire. Et l'option se passe au preset, sans
`-B` — `-B build` écraserait le `binaryDir` du preset et poserait l'URL dans un second arbre
`build/` que `./build/macos-universal/bin/nineteen` ne lit pas. Voir
[JOUER.md](JOUER.md#les-trois-lignes-comptent-et-surtout-la-deuxième), où la sortie de `cmake`
le montre.

Un dépôt cloné et bâti tel quel ne parle à personne. C'est la règle du haut de
`engine/net/ns_online.h` : sans URL configurée, aucune socket n'est ouverte et le fil de travail
ne démarre même pas.

**Ce qui ne se compile pas, et pourquoi c'est refusé :**

- **Le jeton** (`network.token`). Un secret cuit dans un binaire distribué n'en est pas un :
  `strings nineteen | grep -F <jeton>` le rend, sans outil ni désassemblage, sur la machine de
  quiconque l'a téléchargé. Il serait de surcroît le **même pour tous les joueurs**, donc
  irrévocable sans refaire un paquet. Le jeton reste une session, obtenue au lancement, propre à
  une personne. La même réponse vaut pour tout mot de passe ou clé d'API.
- **Un nom de serveur** (`NINETEEN_SERVER_NAME`). Refusé pour l'autre raison : **personne ne le
  lirait**. Aucun élément d'interface n'affiche un nom de serveur, donc ce serait une clé sans
  lecteur — et ce dépôt en a déjà retiré deux pour ce motif exact, dont `network.serverUrl`
  lui-même, revenu le jour où il a eu un lecteur. La définition viendra avec l'écran qui
  l'affiche, pas avant.

#### Le jeton

Une seule clé, dans le fichier de configuration — sous macOS
`~/Library/Application Support/recognizer/Nineteen/settings.cfg` :

```
network.token = LE_SESSIONKEY
```

Il n'a **pas** d'équivalent en ligne de commande ni de défaut compilé, pour la raison ci-dessus.

#### Le démarrage dit d'où vient l'adresse

Avec quatre sources possibles, savoir laquelle a gagné est la seule façon de diagnostiquer
« pourquoi ça parle au mauvais serveur ». La ligne le nomme :

```
réseau : actif sur « http://localhost:8090 » (source : --server=) (lecture seule)
réseau : actif sur « http://localhost:8090 » (source : NINETEEN_SERVER_URL) (lecture seule)
réseau : actif sur « http://localhost:8090 » (source : config network.serverUrl) (avec jeton)
réseau : actif sur « http://localhost:8090 » (source : défaut compilé) (lecture seule)
```

Et quand il n'y en a aucune, il dit **où chercher** plutôt que de se taire :

```
réseau : aucun serveur configuré, le classement restera local (ni --server=,
ni NINETEEN_SERVER_URL, ni « network.serverUrl » en config, ni défaut compilé
-DNINETEEN_SERVER_URL)
```

Sous `--offline`, l'adresse verrouillée est nommée elle aussi — sinon « verrouillé » et « pas
d'URL » produiraient le même silence, et l'on ne saurait pas si le verrou a servi :

```
réseau : verrouillé par --offline, aucune connexion ne sera tentée
         (« http://localhost:8090 », de --server=, est ignorée)
```

### 3. Ce qui se passe, et dans quel ordre

Le score n'est pas une valeur que le jeu annonce. Le serveur **ouvre** la partie, tire la graine
et un secret ; le jeu joue sur cette graine et scelle son journal d'événements avec ce secret ;
le serveur **recalcule** le score. Concrètement :

1. Le joueur s'approche d'une borne → le jeu demande un **billet** d'avance
   (`POST /api/v1/runs`). C'est le seul moment où l'on peut attendre le réseau sans que ça se
   voie : entre l'arrivée devant la borne et l'appui sur le bouton il y a une seconde et demie.
2. La partie commence sur la graine du serveur. **Une partie n'attend jamais le réseau** : sans
   billet, elle se joue quand même, en local.
3. À la mort, le journal scellé part dans une file sur disque
   (`…/Nineteen/runs/*.json`), que le fil réseau vide (`POST /api/v1/runs/{id}/submit`).
   Un 5xx la garde, un 2xx comme un 4xx la retirent — le serveur a tranché.

### 4. Ce qui a été mesuré ici

Pile lancée par `docker compose up --build`, jeu lancé contre elle. Latences constatées en
boucle locale :

| Appel | Code | Temps |
|---|---|---|
| `POST /api/v1/auth/register` | 201 | 147 ms (Argon2id) |
| `POST /api/v1/auth/login` | 200 | 126 ms |
| `GET /api/v1/health` | 200 | 5,0 ms |
| `GET /api/v1/games` | 200 | 4,1 ms |
| `GET /api/v1/leaderboard` | 200 | 4,8 ms |

Une session de `--game=snake --warmup=200` a produit **quatre parties soumises**, toutes avec le
verdict `ok` en base, et une ligne dans `scores` rattachée à sa partie :

```
   run    | game_id | soumis | score | verdict
 cead69d9 |       8 | t      |     0 | ok
 d7807500 |       8 | t      |     0 | ok
```

### 5. Deux limites à connaître

**La première partie d'un processus n'est jamais soumise.** Le billet est demandé au moment où
la partie démarre, donc il arrive trop tard pour elle. C'est voulu — une partie n'attend pas le
réseau — et sans conséquence pour un joueur, qui en enchaîne plusieurs. Ça compte en revanche
pour un script : il faut que le processus joue **au moins deux** parties.

**`--autoplay` ne produit aucun score en ligne.** Une partie de démonstration n'est « ni classée
ni envoyée » (`finish_run`, `room/main.c`) — c'est délibéré : un robot n'a rien à faire sur le
classement mondial. Mais elle **prend quand même un billet**, donc elle ouvre côté serveur une
partie qui ne sera jamais soumise et qui reste indéfiniment dans `runs` avec `submitted_at NULL`.
Deux lignes de ce genre ont été mesurées ici après quelques essais.

Conséquence pratique : **la chaîne en ligne ne peut pas être vérifiée jusqu'au site sans
quelqu'un au clavier.** Sans `--autoplay` il n'y a aucune entrée, donc toute partie sans écran
finit à zéro ; et le classement écarte les zéros (`WHERE sc.score > 0`). Ce qui se vérifie sans
joueur s'arrête donc à « la partie est soumise et le verdict est `ok` », ce qui est déjà
l'essentiel du contrat.

Si l'image est produite et n'est pas noire, le pilote graphique, les assets et la chaîne de
rendu fonctionnent.
