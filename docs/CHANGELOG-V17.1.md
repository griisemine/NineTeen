# Nineteen 17.1.0 — le classement remonte, et le jeu sait qu'il vieillit

17.0.0 avait rendu le jeu installable. 17.1.0 répare la chose qui rendait tout
le mode compétitif décoratif — **aucun score n'atteignait le classement
mondial** — et donne au jeu le moyen de se mettre à jour tout seul.

Ce document est écrit **après mesure**, contre une pile Docker réelle et sa base
Postgres. Chaque chiffre cité vient d'une commande, jamais d'une estimation.

---

## Aucun score ne remontait, et rien ne le disait

### Ce qui se voyait

Rien. C'est tout le sujet. Le jeu affichait le score, l'écrivait dans
`scores.txt`, ouvrait la partie sur le serveur, et le classement public restait
vide. Aucune erreur, aucun avertissement, aucune ligne de journal.

### Ce que la base disait

```
 runs | soumises
------+----------
    7 |        0
```

Sept parties ouvertes côté serveur, aucune soumise, aucun score. Cinq des sept
portaient un créneau en `-easy`.

### La cause

Une borne **déclare** sa difficulté dans `salle.room.json` : six caissons portent
`easy`, six `hard`, six `normal`. Ce n'est pas le régime auquel on joue, puisque
le lot `REGIME DIFFICILE` ouvre le régime dur sur les dix-neuf.

Le billet de partie était demandé sur la valeur **déclarée** et réclamé sur le
régime **joué**. Deux gardes se bloquaient alors l'une l'autre :

- `ns_online_take_ticket` ne consomme pas un billet d'un autre créneau, donc il
  restait prêt ;
- le fil de travail ne va chercher un billet que si aucun n'est prêt, donc il
  n'allait plus jamais en chercher.

Le blocage était **définitif**. La borne ne recevait plus jamais de billet, la
partie se jouait hors ligne, et `ns_runlog_enqueue` la refusait comme il doit le
faire — une partie sans secret n'a rien à prouver. Toute la chaîne fonctionnait,
sauf le premier maillon, et personne ne pouvait le voir.

### Les trois corrections

| Où | Ce qui change |
| --- | --- |
| `ns_online.c`, `arm_ticket` | Un billet prêt pour un autre créneau est **jeté**. C'est la panne elle-même, en trois lignes. |
| `room/main.c`, `borne_en_regime_dur` | Le régime d'une borne se décide à **un seul endroit**. Trois appelants le recalculaient chacun à sa façon, dont le multiplicateur affiché devant la fente, qui annonçait donc celui de l'autre régime. |
| `room/main.c`, démarrage | Le billet de `--game=` est demandé au lancement, pas à l'instant de jouer. Les cinq secondes de chargement de la salle suffisent largement. |

### La preuve

```
$ ./build/bin/nineteen --headless --game=snake --warmup=60 --server=http://localhost:8080
$ psql -c "select g.slug, r.submitted_at, r.verdict from runs r join games g …"
    slug    |         submitted_at          | verdict
------------+-------------------------------+---------
 snake-easy | 2026-09-01 19:04:50.487402+00 | ok
```

Et, planté devant `borne_arcade_4` — qui déclare `easy` — le jeu ouvre désormais
`demineur-hard`, le créneau qu'il jouera.

### Ce qui empêchera que ça recommence

`tests/stub_serveur.c` : un serveur HTTP de six routes, **dans le test**. La
chaîne complète — billet, sceau, file d'attente, envoi, classement — se vérifie
maintenant sans base ni Docker, donc dans l'intégration continue, qui est
l'endroit où cette panne aurait dû s'allumer. Le test échoue sans la correction,
c'est vérifié dans les deux sens.

---

## Le jeu sait qu'une version plus récente existe

### Ce que « se mettre à jour » veut dire ici

Au lancement, si une adresse de serveur est configurée, le jeu demande
`/api/v1/telechargements` **sur un fil**, compare la version annoncée à la
sienne, et propose le paquet de cette machine. `F11` télécharge, `F11` encore
installe, `Maj+F11` écarte la version pour de bon.

### Les trois règles, et pourquoi elles ne se négocient pas

Écrire un mécanisme de mise à jour, c'est réécrire très exactement la chose qui a
tué la V1 : en 2020, tout le corps de `main()` était enfermé dans
`if (checkVersion(...))`, et sans réponse du serveur le jeu affichait « une
nouvelle version est disponible » puis se fermait. Hors ligne, il ne pouvait pas
atteindre sa propre fenêtre.

1. **Rien n'attend.** `ns_maj_init` lance un fil et rend la main. Mesuré par le
   test face à un serveur mort : **moins de 500 ms**, et le jeu démarre.
2. **Rien n'échoue.** Pas de serveur, pas de réseau, pas de paquet pour cette
   plateforme : le résultat est « on ne sait pas », et on joue.
3. **Rien ne s'installe tout seul.** Le paquet est vérifié puis **confié** au
   système, qui applique Gatekeeper ou SmartScreen. Le jeu ne remplace jamais son
   propre binaire et n'exécute rien.

### Ce qu'il a fallu ajouter autour

- **`ns_http_telecharger`** écrit dans un fichier au lieu de la mémoire, par blocs
  de 64 Kio, et **reprend** avec un en-tête `Range`. `ns_http_request` borne les
  réponses à 256 Kio, ce qui est juste pour un classement et sept cents fois trop
  peu pour un paquet de 175 580 032 octets.
- **`ns_sha256_fichier`**, pour vérifier ce paquet sans le charger d'un bloc. Une
  coupure au milieu d'un transfert livrerait un installeur tronqué, et c'est la
  seule façon de casser une machine avec une mise à jour. Une empreinte qui ne
  tombe pas juste **efface** le fichier.
- **Le serveur annonce la version des paquets qu'il a**, lue dans leurs noms, et
  non celle de son binaire Go. Sans ça, un serveur avancé devant un répertoire
  pas encore refait ferait installer l'ancien paquet, qui ne changerait pas la
  version, qui se reproposerait au lancement suivant.

### Deux défauts attrapés par la mesure, pas par la relecture

**Le vocabulaire.** Le serveur écrit `universel` et `arm64` ; le client disait
`universal` et `aarch64`. Aucune erreur nulle part, seulement un joueur à qui
l'on ne propose jamais rien.

**Le choix du paquet.** Un répertoire garde les anciennes versions à côté des
nouvelles, et deux `.dmg` ont la même extension. Le jeu annonçait
« 17.1.0 disponible (175 Mio) » en s'apprêtant à installer le 17.0.0 dont il
partait. Un paquet doit maintenant porter la version annoncée, et la comparaison
est bornée à droite pour que `17.1` ne se reconnaisse pas dans `17.10`.

### La preuve

Un paquet 17.1.0 déposé dans le répertoire est vu, téléchargé, et l'empreinte
calculée par le jeu est égale à celle de `shasum` et à celle du serveur :

```
somme independante : e24067e22aac3f21fb6078747dc95da90981f49c138c5bb60439e1f23bd0bf4c
somme du serveur   : e24067e22aac3f21fb6078747dc95da90981f49c138c5bb60439e1f23bd0bf4c
```

Le même fichier tronqué à 1 000 000 octets se termine par une **reprise** et
retombe sur la même empreinte.

À l'échelle réelle, contre le vrai paquet de la version : **183 165 784 octets
en 16,8 secondes**, soit 10,9 Mio/s sur boucle locale, pendant que le jeu
continuait de tourner. Le fichier reçu est identique à la source, `cmp` à
l'appui.

---

## Deux rustines posées deux fois

`room/main.c` portait deux lignes écrites en double, la seconde mal indentée :
un `run_tick = 0; pending_press = 0;` et un `room_cp_partie_debut(...)`. Ni
l'une ni l'autre ne changeait le comportement — la seconde fonction est
idempotente — mais elles laissaient croire que l'appel comptait deux fois. Un
commentaire de neuf lignes sur le prix d'une borne avait par ailleurs été séparé
du code qu'il décrit par un bloc entier sur un autre sujet.

---

## Vérifications

| Contrôle | Résultat |
| --- | --- |
| `ctest` (macOS universel) | 53 / 53 |
| `ctest` sous ASan et UBSan | 53 / 53, 597 s |
| `go build`, `go vet`, `gofmt -l` | propres |
| `go test ./...` | tous les paquets |
| Chaîne de score contre la vraie pile | partie soumise, verdict `ok` |
| Mise à jour contre la vraie pile | 183 165 784 octets reçus, empreinte vérifiée, reprise vérifiée |
| Paquets 17.1.0 | `.dmg` universel monté et lancé depuis le volume, AppImage, `.deb`, `.tar.gz` |

---

---

## Les paquets de cette version

Fabriqués sur la machine de l'auteur, empreintes relues par `shasum` hors de la
chaîne qui les a produites :

| Paquet | Octets | SHA-256 (début) |
| --- | --- | --- |
| `Nineteen-17.1.0-macOS-universal.dmg` | 183 165 784 | `0586116cf28dd0fe` |
| `Nineteen-17.1.0-aarch64.AppImage` | 172 182 024 | `776900d0c47ae547` |
| `nineteen_17.1.0_arm64.deb` | 175 587 708 | `009d3bac9060cf8d` |
| `nineteen-17.1.0-linux-aarch64.tar.gz` | 175 583 107 | `f11b6ed99ab1df35` |

Le `.dmg` est universel — `lipo -archs` rend `x86_64 arm64` — et l'application a
été lancée **depuis le volume monté**, pas depuis l'arbre de build.

Les paquets Linux sont `aarch64` parce que Docker est natif ARM sur cette
machine. **Un PC `x86_64` ne peut pas les installer** : c'est l'intégration
continue qui produit cette architecture, et elle seule qui produit le paquet
Windows, qu'aucune machine du projet ne sait construire localement.

---

---

## Ce que la mise à jour ne garantit pas

L'empreinte SHA-256 protège d'un fichier **abîmé**. Elle ne protège **pas** d'un
fichier remplacé en chemin : le client HTTP ne fait pas de TLS, donc l'empreinte
arrive par le même canal en clair que le paquet. Qui peut réécrire l'un peut
réécrire l'autre.

C'est écrit dans `engine/net/ns_maj.h` et dans `docs/JOUER.md`, et ça pèse plus
lourd ici qu'ailleurs : un classement altéré fausse un tableau, un paquet altéré
exécute du code. Ce qui la contient — aucune socket sans adresse donnée, aucune
exécution par le jeu lui-même, un serveur qu'on héberge soi-même — ne remplace
pas la réponse, qui est **un proxy TLS devant le serveur**. Elle rejoint la
signature des paquets dans la liste des six décisions ouvertes.

---

## L'audit sous Blender

Les modèles et le personnage ont été relus dans Blender, articulation par
articulation et face par face, plutôt qu'à la lecture des scripts qui les
produisent. Trois choses en sont sorties.

### Le personnage glisse des pieds, et on sait enfin pourquoi

`ns_skin.h` disait depuis des mois que le cycle livré n'a « pas de pied cloué au
sol », sans jamais le chiffrer. Mesuré :

| | |
|---|---|
| dérapage du pied posé | **0,258 unité**, soit 31 cm à l'échelle du jeu |
| distance hanche-cheville | **0,5419**, c'est-à-dire exactement cuisse + tibia, sur **29 des 96** couples (image, jambe) |
| foulée du fichier | **1,306 m**, là où la salle en emploie **1,550** |

La deuxième ligne explique la première : la jambe **ne peut pas** atteindre le
pas demandé, alors le pied dérape pour compenser. Ce n'est pas une négligence
d'animateur, c'est une triche géométrique — et elle était invisible parce que
l'écart de foulée, 16 %, passe sous le seuil de 33 % qui déclenche
l'avertissement.

La correction est écrite, elle est dans le dépôt, et **elle n'est pas livrée** :
`assets/blender/personnage.py`. Elle ramène le glissement à un micron par image,
ferme la boucle au zéro exact, sort le genou de sa butée et porte la foulée à
celle que la salle emploie déjà — donc sans retoucher les bruits de pas ni
l'oscillation de la tête, qui lisent la même valeur.

Ce qui l'empêche de partir est `tests/test_allure.c`, qui tombe sur cinq
contrôles et qui a raison. L'accroupi du jeu n'est pas animé : il est **dérivé**,
en ajoutant un angle à la cuisse et le double au genou depuis la pose de
passage. Cette dérivation a besoin d'une jambe porteuse quasi verrouillée :

| pose de passage | genou porteur | angle d'accroupi trouvé |
|---|---|---|
| cycle d'origine | 173,5° | 67,5° |
| cycle corrigé | 151,6° | **105,5°**, soit un genou à 211° |

**2,9 % d'écart d'extension coûtent 22° de genou** : près de l'extension totale,
l'angle est extrêmement sensible à la longueur. Or corriger le glissement, c'est
précisément dessouder la jambe de sa butée. Sept variantes ont été essayées avec
le test pour oracle — trois réserves de flexion, deux foulées, deux façons de
conserver la longueur — et toutes échouent de la même manière. Une bissection
innocente l'outil : le cycle d'origine repassé par la même tuyauterie passe.

Ce qu'il faudrait d'abord est déjà écrit dans le test lui-même — donner à
l'accroupi une cinématique inverse **par jambe** — et le moteur a le module qu'il
faut, `engine/anim/ns_ik.c`.

### Le biseau laissait des faces d'aire nulle

`roomgen` le disait à chaque compilation, dix-neuf fois de suite, et personne
n'allait voir. Relevé par matériau :

| modèle | matériau | faces nulles |
|---|---|---|
| billard | `repere` | **256 sur 704** (36,4 %) |
| borne | `grille` | 100 sur 796 (12,6 %) |
| borne | `manche_bleu` | 24 sur 144 (16,7 %) |

Ce sont les pièces les plus **minces** : les visées font 2 mm d'épaisseur pour un
chanfrein de 4, les anneaux de grille 3,5 mm pour un chanfrein de 3. Le rabotage
anti-chevauchement ramène alors la largeur du biseau à ce qui tient, et quand ce
qui tient vaut zéro, la face naît avec deux sommets confondus.

Les deux scripts les dissolvent maintenant. La borne passe de 4610 à 4466
triangles, le billard de 3708 à 3452, et **l'aire de surface de la borne bouge
de six nanomètres carrés** — l'arrondi de la sommation. Les boîtes englobantes
sont identiques à la neuvième décimale.

### Ce que Blender n'a pas trouvé

Les chemins de texture des modèles CC0 pointent vers des fichiers absents
(`textures/…_diff_1k.jpg`), ce qui fait crier tout lecteur glTF. Ce n'est **pas**
un défaut du jeu : la chaîne d'assets aplatit ces noms et `ns_scene` les résout
par leur base. Vérifié dans `build/assets/scene/textures/`.

---

## Deux lignes du procès-verbal de 17.0.0 étaient périmées

Le tableau « ce qui était annoncé non tenu » de `docs/CHANGELOG-V17.md` porte
deux affirmations que la mesure dément aujourd'hui.

| Ce que 17.0.0 déclarait | Mesuré maintenant |
|---|---|
| « `--width`/`--height` : **1600x900 sort en 1280x720** » | les neuf définitions essayées sortent **exactes**, 1600x900 comprise. Vérifié en lisant l'en-tête PNG des captures : 640x360, 800x600, 1024x768, 1280x720, 1366x768, 1600x900, 1920x1080, 2560x1440, 3440x1440 |
| « **C-04 à C-08 toujours absents**, cinq des neuf contrôles » | les **neuf** existent et tournent : `check_inside_shell`, `check_solid_overlaps`, `check_grounded`, `check_facing`, `check_panel_visible`, `check_hanging`, `check_light_has_body`, `check_floor_material`, `check_cabinet_clearance` |

Les quatre autres lignes du tableau restent vraies : les paquets ne sont pas
signés, le déterminisme n'est vérifié que sur une machine, la découverte
d'adversaire et le relais restent sans TLS, et le personnage n'a toujours qu'un
seul cycle d'animation.

---

## Ce qui n'a pas changé

Les six décisions de 17.0.0 restent ouvertes, et la première reste la seule qui
bloque une vente au grand public : **les paquets ne sont signés d'aucune façon**,
donc macOS met en quarantaine et Windows affiche SmartScreen. Voir
`docs/CHANGELOG-V17.md`.
