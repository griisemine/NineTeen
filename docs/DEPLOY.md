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
| `NINETEEN_SECURE` | à définir derrière HTTPS : active les cookies `Secure` et HSTS |
| `NINETEEN_LOG` | `text` ou `json` |

Les identifiants ne sont jamais dans le code. Le dépôt d'origine les gardait en clair dans un
fichier commité — ils sont donc encore dans l'historique git et **doivent être changés**.

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

### Ce qui reste à faire

Le workflow de compilation existe pour les trois plateformes ; celui qui produit les paquets
signés — AppImage, `.dmg` universal, `.msi` — reste à écrire. La page de téléchargement du site
pointe déjà vers les artefacts de release GitHub et n'aura pas à changer.

### Vérifier une installation

```sh
SDL_VIDEO_DRIVER=offscreen ./nineteen --headless --screenshot=/tmp/verif.png --frames=4
```

Si l'image est produite et n'est pas noire, le pilote graphique, les assets et la chaîne de
rendu fonctionnent.
