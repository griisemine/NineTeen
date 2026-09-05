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

## La salle était une photo de la mauvaise pièce

Le propriétaire a fourni une image de ce que la salle doit donner : une salle
d'arcade **néon**, traversée de tubes roses et cyan au plafond et en haut des
murs, avec des cœurs de source qui brûlent. Le rendu montrait un entrepôt au
tungstène.

**Cette image est un agrandissement d'une capture du jeu lui-même**, et ça se
démontre : son HUD affiche « JETONS 100072 / TICKETS 19856 / SERIE 1 J »,
c'est-à-dire au chiffre près le portefeuille du propriétaire ; le cadre de
fenêtre est celui de macOS et porte le titre « Nineteen » ; les frontons nomment
DEMINEUR et SNAKE ; la moquette est celle de `moquetteart`. Il faut donc en viser
la **lumière** et non les **légendes** : l'agrandissement a inventé du texte —
l'enseigne du fond y lit « RETRO CAFE » là où le jeu écrit NINETEEN, et le
tableau du bar y porte des mots qui ne sont d'aucune langue. Rien de tout cela
n'a été reproduit ; le reproduire reviendrait à copier une hallucination dans le
produit. Ce qui a été visé, ce sont les grandeurs photométriques, qui sont
justement ce que l'agrandissement n'a pas inventé : il a rehaussé une lumière,
il ne l'a pas fabriquée.

### Ce que le défaut coûtait, et pourquoi personne ne pouvait le dire

Aucun outil du dépôt ne savait répondre à la question posée. `vues-diff.py`
répond à « est-ce que ça a bougé » : une salle deux fois plus claire mais
toujours ambrée y montre un écart énorme sans se rapprocher d'un pas de la
photo. On jugeait donc à l'œil, une vue à la fois, et l'on ne se mettait jamais
d'accord — deux personnes devant la même capture, l'une dit « trop sombre »,
l'autre « trop jaune ».

`tools/ambiance.py` chiffre les cinq mots qu'on prononce devant l'image : la
**médiane** de luminance (pas la moyenne — un seul néon à 255 sur 5 % de l'image
la déplace de 12 points sans que la salle s'éclaire), la part de pixels qui
brûlent, la saturation pondérée, la masse de chaque famille de teinte, et
l'écart interquartile. Le premier passage a suffi à trancher :

| | départ | après | consigne |
|---|---|---|---|
| médiane de luminance | 35,0 | **72,7** | 55 à 85 |
| écart interquartile | 55,0 | **77,8** | ≥ 45 |
| part au-dessus de 200 | 0,38 % | **2,38 %** | 2 à 8 % |
| masse ambrée | 87,3 % | **36,6 %** | — |
| masse rose + cyan | 2,1 % | **30,1 %** | ≥ 22 |

La **saturation était déjà bonne au départ** — 0,563 pour 0,30 demandés. C'est
exactement pourquoi l'outil sépare la teinte de la saturation : mesurées
ensemble, elles auraient conclu que la couleur allait bien.

`tools/planche.py` assemble les huit vues en une mosaïque, et
`tools/ambiance-releve.sh` fige l'enchaînement capturer / mesurer / regarder avec
les réglages du propriétaire — trois erreurs de méthode ont été commises avant
qu'il existe, dont comparer une capture en `medium` à une capture en `high`.

### Les six causes, toutes mesurées

1. **Les six tubes colorés de la salle étaient invisibles**, pour deux raisons
   indépendantes. Leur portée de 4,2 m les éteignait à deux mètres : le shader
   fenêtre la contribution par `(1 − (d/portée)⁴)²`, donc à 4 m il ne restait
   3,2 % de ce que la distance laissait déjà passer. Et le tube « bleu » l'était
   vraiment — son émissif multiplié par son albédo sort à 217° de teinte,
   c'est-à-dire dans la famille BLEU et jamais dans CYAN. **La couleur qu'on
   croyait poser n'était pas celle qu'on posait.**

2. **Aucune enseigne de la salle n'atteignait le seuil du halo.** Le pixel le
   plus lumineux de tout le décor — le cœur d'une lettre du fronton PIANO —
   plafonnait à 0,842 de luminance pour un seuil à 0,85. Zéro fronton, zéro
   tube, zéro écran ne le franchissait ; tout le halo visible venait des dalles
   de plafond *éclairées*, la seule chose de l'image qui ne soit pas une
   enseigne.

3. **Le halo n'en portait qu'un millième.** Cinq niveaux de pyramide étaient
   calculés, le tone mapping n'en lisait qu'un — 32 × 18 texels pour une image
   de 1024 × 576. Crête de la cible de halo rapportée à celle de la source :
   **0,0013**. On ajoutait un millième de néon autour d'un néon.

4. **Le tiers bas de chaque image est de la moquette, et rien ne l'éclairait**
   de moins de 2,74 m. Six courses de néon en plinthe la portent maintenant.

5. **L'ambiance constante du moteur était ambrée.** Le raisonnement écrit
   au-dessus d'elle est juste — l'indirect prend la couleur de ce qui éclaire —
   et c'est sa prémisse qui a cessé d'être vraie. Recalculée depuis les sources
   réelles : les 85 lumières déclarées pèsent (3861 ; 3358 ; 5065) en somme de
   couleur × intensité, et le crépi des murs a une réflectance mesurée de
   (0,3427 ; 0,3338 ; 0,2970) ; le produit donne (0,903 ; 0,778 ; 1,000). **La
   luminance ne bouge pas** — 0,0898 avant comme après —, seule la teinte était
   périmée. À `high`, où l'ambiance est multipliée par 2,6, c'était
   (0,291 ; 0,224 ; 0,161) posés sur tout ce qu'aucune source n'atteint : le
   crépi du bar ressortait laiteux et jaune dans une salle rose et cyan.

6. **On jouait sans mains.** Pendant une partie, aucune des deux n'était à
   l'image. Le poste de jeu avance l'œil de 27,7 cm vers la dalle sans déplacer
   le corps : le sommet du manche descend de 23,3° à **32,0°** sous l'axe du
   regard, pour 29° de demi-ouverture. Les mains tombaient trois degrés sous le
   bord du cadre. Le champ des bras s'ouvre à 70° **pendant une partie
   seulement**, amorti pour arriver avec le poste.

### Ce qui a été posé

Dix-huit courses de néon — cinq au plafond sous les poutres, six en haut des
murs, six en plinthe, une dans le couloir —, seize déclarations de sources
(85 lumières au total sur les 128 du moteur), une remontée de pyramide de halo
sur quatre niveaux, un gain émissif en courbe (`1 + 6 y²`, choisi contre 4 et 8),
et la garniture de café qui manquait : **six bancs, deux mange-debout, un canapé
et sa table basse, et rien dessus depuis toujours.** Une table d'arcade vide se
lit comme un meuble de catalogue.

### Ce que ça coûte, mesuré

`--bench`, qui mesure le **temps GPU réel** de chaque image, sur 199 images en
1280 × 720, vue `centre`, machine au repos :

| palier | avant | après | |
|---|---|---|---|
| `low` | 6,2 ms — 162 i/s | **8,3 ms — 121 i/s** | + 34 % |
| `medium` | 17,8 ms — 56 i/s | **18,4 ms — 54 i/s** | **+ 3 %** |
| `high` | 30,5 ms — 33 i/s | **36,1 ms — 28 i/s** | + 18 % |

**Le palier le plus employé est celui qui paie le moins.** Ce n'est pas un
hasard : `medium` n'a ni ombre lancée ni indirect tracé, donc les quarante et une
sources ajoutées n'y coûtent qu'une boucle d'éclairage direct, tandis que `high`
les rejoue dans le lancer de rayons.

LA MÉTHODE A CHANGÉ LA CONCLUSION, et il faut le dire parce que la première
version de ce paragraphe était fausse. Mesuré au chronomètre — durée totale de
deux exécutions à nombres d'images différents, différence divisée par l'écart —
`medium` ressortait à 11,7 → 15,4 ms, soit + 32 %. Onze fois moins de coût réel
annoncé comme dix fois plus. Deux raisons, et aucune n'est le hasard : le
chronomètre compte le chargement de la scène, la construction du BVH et
l'enregistrement des commandes, dont aucun n'est du temps GPU ; et il compte le
reste de la machine, ce qui donnait des écarts de ± 40 % tant que d'autres
compilations tournaient. `--bench` existait depuis le début, et le jeu le dit
lui-même à chaque arrêt : « ce chiffre ne mesure QUE l'enregistrement des
commandes ; employer --bench ».

`ultra` reste à 132 ms, soit 7,6 images par seconde. C'est ce que `--help`
annonce déjà — « superbe en capture, coûteux en temps réel ».

### Ce qui résiste

- **Le couloir d'entrée** reste à 85 % d'ambre : c'est un sas, deux appliques à
  filament, et le passer au néon effacerait le contraste d'entrée. Hors consigne
  assumé.
- **`medium` est plus clair que `high`** — médiane 88,8 contre 67,1 sur `centre`
  — et c'est déjà documenté dans `ns_render.c` : sans ombre lancée, chaque
  lumière traverse les caissons. Il est plus clair parce qu'il est plus faux.
- **Les frontons bavent** de près : la lettre brûle vers le blanc et son halo
  comble l'espace entre les glyphes. C'est le comportement voulu du gain émissif,
  balayé contre 4 et 8 ; à 4 le halo reste court, à 8 le fond des frontons blanchit
  avec les lettres.
- **La main gauche n'empoigne pas la boule** du manche : elle la touche du bout
  du majeur. La corriger demande de viser la paume, or `tests/test_ik.c:492`
  verrouille « bout du majeur à moins de 3 cm du sommet de la boule », ce
  qu'aucune vraie prise ne satisfait — paume sur une boule de 42 mm, le bout du
  majeur est à 6,4 cm du sommet. Le test mesure au mauvais endroit.
- **Une borne n'a qu'un seul manche** (`assets/blender/borne.py` ; `manche_rouge`
  est devenu le bouton START). Deux mains sur deux boules demanderaient une
  seconde ancre côté salle.

---

## Le couloir d'entrée était resté au tungstène

Vingt-quatre commits ont amené la salle au néon. **Une vue sur huit n'a pas
suivi**, et c'est la première chose qu'on voit du jeu : le sas d'entrée, neuf
mètres de couloir entre la porte de la rue et la trouée du pan coupé.

| `entree`, mesuré au palier `high`, échelle 1,0 | valeur | consigne |
|---|---|---|
| médiane de luminance | 87,4 | 55 à 85 |
| masse rose + cyan | **2,66** | ≥ 22 |
| masse ambrée | **85,3 %** | — |
| cyan | 0,1 % | — |
| rose | 2,5 % | — |

C'est-à-dire, au chiffre près, le rendu **d'avant** le travail sur le néon : un
couloir de plâtre beige éclairé par quatre appliques ambre. La moquette portait
bien les symboles néon et un tube magenta courait au plafond ; ils pesaient 2,66
pour une cible de 22.

### La cause, et elle n'est pas « il manquait du néon »

Le couloir **avait** son néon — `neon_couloir`, une course de 6,40 m posée dans
l'axe du plafond. Elle ne servait à rien, et la géométrie dit pourquoi.

Le couloir fait 1,669 m de libre. Ce qui remplit son cadre, ce sont ses **deux
joues**. Or le tube est au plafond, à 2,67 m, donc à **1,30 m** d'une joue à
hauteur d'œil, pendant que les appliques sont à **0,64 m** de la leur. Mesuré sur
la joue ouest à 1,70 m :

| source | éclairement sur le plâtre |
|---|---|
| `applique_entree`, à 0,64 m | 6,9 |
| `plafonnier_sas` ×3 | 3,8 |
| `neon_couloir`, à 1,30 m | **1,2** |

Neuf contre un. C'est exactement le rapport que la vue rendait en 85,3 % d'ambre
pour 2,5 % de rose. Le commentaire de `neon_couloir` justifiait d'ailleurs son
réglage bas par « une source y touche ses deux parements à moins d'un mètre » :
c'est vrai des appliques, pas de lui. La course avait été réglée comme si elle
éclairait de près ce qu'elle éclaire de loin.

**Monter le seul tube axial n'aurait donc pas suffi** : il aurait éclairci la
moquette et le plafond sans jamais reprendre les murs à l'ambre.

### Ce qui change, dans `assets/scene/salle.room.json`

| Où | Avant | Après | Pourquoi |
|---|---|---|---|
| `plafonnier_sas` (×3) | 62, portée 4,2 | **10, portée 3,0** | Un pavé de faux plafond éclaire **à plat** : la même nappe sur les deux joues, sur toute la longueur. C'est elle qui empêchait une autre couleur d'exister. |
| `applique_entree` (×2) | 36 | **22** | Une applique fait une **tache**, et une tache se garde : c'est ce qui dit qu'il y a une lampe. |
| `applique_sas` | 50 | **25** | Même correction ; le rapport 50/36 entre les deux joues est gardé à 25/22. |
| `neon_couloir` (×3) | 46, portée 6,0 | **55, portée 4,0** | Il teint désormais le plafond et l'axe de la moquette, plus les murs. À 6,0 les trois sources se recouvraient de bout en bout. |
| `neon_cimaise_couloir_ouest` | — | **neuf** | Une course de 6,40 m au ras de la cimaise, 3 sources à 38. |
| `neon_cimaise_couloir_est` | — | **neuf** | Sa jumelle, en miroir du parement 6,953. |

Les deux courses neuves sont à **0,15 m** du plâtre qu'elles teignent, contre
1,30 m pour le tube du plafond : 0,1137 d'éclairement par unité d'intensité,
contre 0,0621. C'est le seul endroit d'où l'on pouvait reprendre les joues.

Verre de 5 cm à 2 cm du parement — la garde de `neon_pan_est` —, occupant 1,10 à
1,19 m, soit 2 cm d'air au-dessus de la cimaise dont le dessus est à 1,08. Les
sources sont à 0,15 m de leur tube, là où **C-07** en tolère 0,40.

### Magenta les deux, et pas la paire rose/cyan du hall

Le réflexe était de reprendre les deux couleurs de la salle, cyan à l'ouest et
magenta à l'est, comme le font `neon_mur_ouest` et `neon_mur_est`. **Mesuré, ça
ne marche pas dans un couloir de 1,669 m.** Un point de la joue ouest reçoit
0,1137 par unité d'intensité de sa propre course et 0,0446 de celle d'en face,
soit 2,5 pour 1 seulement — pas assez pour que l'une l'emporte. Sur le plâtre du
couloir, dont l'albédo est [0,52 ; 0,44 ; 0,34], la somme des deux retombe à
**0,27 de saturation** : un gris violacé. Deux courses de la même teinte donnent
**0,79** au même point.

Et c'est aussi ce qu'on veut voir. **Le couloir annonce la salle, il ne la
copie pas.** Il tient le rose, qui est déjà la teinte de son mur est ; le cyan
n'apparaît qu'au bout, par la trouée du pan coupé, quand le hall s'ouvre. L'une
est ce qu'on traverse, l'autre ce vers quoi l'on va.

### Le remplissage uniforme est l'autre façon de rater la photo

Le premier jet posait 60 d'intensité sur une portée de 4,5. Il passait les deux
consignes manquées — 79,1 de masse rose — et **cassait une troisième** :

| | départ | remplissage uniforme | feston retenu |
|---|---|---|---|
| médiane | 87,4 | 86,1 | **82,3** |
| écart interquartile | 71,4 | **42,9** ✗ | **53,7** |
| masse rose + cyan | 2,66 | 79,1 | **45,1** |

À 4,5, chaque source couvrait les 6,40 m de la course entière : les trois se
recouvraient et la joue recevait une nappe plate. On avait remplacé un couloir
ambré uniforme par un couloir magenta uniforme, ce qui n'est pas plus la photo
de référence — elle est faite de masses **et** de sources. À 38 sur une portée
de 2,0, le plâtre reçoit 14,2 à l'aplomb d'une source et 4,3 à mi-chemin entre
deux : ce feston de 3 pour 1 rend 10,8 points d'écart interquartile.

### Les huit vues, avant et après

Captures `--headless --offline --frames=40 --width=1024 --height=576 --scale=1.0
--quality=high`, par `tools/ambiance-releve.sh`.

| vue | médiane | écart interquartile | masse rose + cyan |
|---|---|---|---|
| allee | 75,1 → 75,1 | 105,1 → 104,7 | 37,2 → 37,4 |
| travee | 74,9 → 74,9 | 88,8 → 88,8 | 31,7 → 31,8 |
| sud | 59,1 → 59,1 | 88,0 → 88,1 | 36,6 → 36,7 |
| bar | 86,3 → 86,1 | 69,8 → 69,5 | 35,6 → 35,8 |
| billard | 82,8 → 82,8 | 82,3 → 82,3 | 22,1 → 22,2 |
| classement | 57,6 → 57,6 | 79,8 → 79,8 | 26,9 → 26,9 |
| plafond | 58,4 → 58,4 | 37,5 → 37,4 | 48,1 → 48,1 |
| **entree** | **87,4 → 82,3** | **71,4 → 53,7** | **2,66 → 45,1** |

Les sept autres vues bougent de moins de 0,3 sur chaque grandeur, c'est-à-dire
du bruit d'un rendu stochastique. Aucune source touchée n'éclaire hors du sas, et
c'est ce que la table vérifie plutôt que d'en donner l'assurance.

`roomgen` finit à **zéro défaut** sur ses neuf contrôles, C-02 (interpénétration)
et C-07 (une source a un luminaire) compris. L'emprise atteignable passe de
118,4 à 117,8 m² : les deux tubes prennent 7 cm sur chaque joue à 1,10 m de
haut, ce qui laisse 1,529 m de passage sur 1,669.

### Le plafond, contraste 37,5 pour 45 — non corrigé, et pourquoi

La vue `plafond` rate la même consigne d'écart interquartile, et **la cause n'est
pas de la même famille**. Elle ne manque pas de néon : c'est la mieux servie des
huit avec 48,1 de masse rose + cyan, pour 0,360 de saturation et 58,4 de médiane,
toutes deux dans les clous. Son histogramme :

| | p05 | q1 | médiane | q3 | p95 | max |
|---|---|---|---|---|---|---|
| `plafond` | 13,8 | 39,7 | 58,3 | 77,2 | 175,0 | 218,0 |
| `allee` | 3,5 | 23,7 | 75,1 | 128,4 | 202,5 | 232,7 |

Elle **a** ses masses noires — 25,3 % des pixels sous 40 — et elle **a** ses
sources — 7,7 % au-dessus de 150. Ce n'est pas l'aplat gris que la consigne
existe pour attraper. Ce qui lui manque, c'est de l'étalement dans la moitié
centrale, et cette moitié-là est **un seul plan** : le regard est levé à 34° et
le faux plafond occupe les trois quarts du cadre, à distance et albédo presque
constants.

L'essai a été fait plutôt qu'argumenté : **+35 % sur les cinq courses de poutre**
du hall.

| | avant | +35 % sur les poutres |
|---|---|---|
| `plafond`, écart interquartile | 37,5 | **41,2** — toujours sous 45 |
| `plafond`, médiane | 58,4 | 64,4 |
| `allee`, saturation | 0,311 | **0,307** — pour 0,30 demandés |

Doubler le néon du plafond achète 3,8 points d'écart et consomme la marge de
saturation de `allee`, qui n'en a que 0,011. Le reste du chemin coûterait
davantage aux six vues qui vont bien qu'il ne rapporterait à celle-ci. **Non
corrigé, délibérément** : un écart interquartile forcé par du noir ajouté serait
un chiffre acheté, pas une image améliorée.

---

## Le palier « high » choisi dans le menu redevenait « medium » tout seul

Deux défauts distincts, trouvés en cherchant pourquoi le jeu démarrait en
`medium` alors que `settings.cfg` portait `render.quality = high`.

**Le premier n'en est pas un, c'est un silence.** Un fichier `.env` à la racine
du dépôt — non suivi par git, ignoré par `.gitignore` — porte
`NINETEEN_QUALITY=medium`, `NINETEEN_WIDTH=1600` et `NINETEEN_HEIGHT=900`.
`apply_env_file` les pose **avant** la ligne de commande, donc `opt.quality_set`
passe à vrai et la configuration n'est jamais consultée. Cette précédence est
voulue et documentée. Ce qui manquait, c'est qu'elle le dise :

```
fenêtre : 1600x900 (source : fichier /…/NineTeen/.env) — la configuration gardait 1280x720
palier  : medium   (source : fichier /…/NineTeen/.env) — la configuration gardait « high »
```

Les quatre sources se nomment maintenant, comme le démarrage nomme déjà celle de
l'adresse du serveur. Le défaut n'était pas trouvable depuis un autre
répertoire : lancé depuis `/tmp`, tout marchait.

**Le second est un vrai dégât.** `room_menu_persist` écrivait le palier et
l'échelle **sans garde**. Mesuré : une exécution `--headless --frames=2` depuis
la racine transformait `render.quality = high` en `medium` et `render.scale = 1`
en `0.6`, sans qu'aucun menu soit ouvert. C'est le défaut déjà corrigé pour
`window.width` ; ces deux lignes y avaient échappé. Elles passent sous le même
garde, levé aussi par F7 et F8 qui sont des gestes du joueur.

Deux tests l'épinglent (`tests/test_menu.c`) : l'un vérifie qu'une exécution
sans menu ne réécrit rien, l'autre écrit un `settings.cfg` à la main — forme
exacte du jeu, espaces autour du `=`, dernière valeur vide — et vérifie que
palier, définition, échelle et plein écran en ressortent.

---

## La documentation apprenait à lancer un binaire périmé

Le propriétaire a suivi la documentation, lancé le jeu, et n'a vu **aucune** des
améliorations de vingt-quatre commits. Les trois lignes qu'il avait sous les yeux
étaient fausses de deux façons distinctes, et ni l'une ni l'autre ne dit un mot en
passant.

```sh
cmake --preset macos-universal
cmake -B build -DNINETEEN_SERVER_URL=http://127.0.0.1:8080 --preset macos-universal
./build/macos-universal/bin/nineteen --server=http://127.0.0.1:8080 --temps-reel
```

**Aucune des deux premières lignes ne compile.** `cmake --preset` configure et
s'arrête ; il manquait `cmake --build --preset macos-universal -j8`. La troisième
ligne a donc lancé ce que l'arbre contenait déjà : un binaire du **1er septembre à
21 h 51**, cinq jours et vingt-quatre commits en arrière. Le `build/CMakeCache.txt`
de cette séance-là est encore daté du **1er septembre 21 h 46**.

**`-B` écrase le `binaryDir` du preset, en silence.** Vérifié en relançant les deux
commandes l'une après l'autre :

```console
$ cmake --preset macos-universal
-- Build files have been written to: /…/NineTeen/build/macos-universal

$ cmake -B build --preset macos-universal
-- Build files have been written to: /…/NineTeen/build
```

Le `-DNINETEEN_SERVER_URL=` de la deuxième ligne atterrissait donc dans `build/`, un
arbre que la troisième ne lance jamais. Les deux arbres sont toujours sur la machine
et se contredisent l'un l'autre : `build/CMakeCache.txt` porte
`CMAKE_CACHEFILE_DIR=…/NineTeen/build`, `build/macos-universal/CMakeCache.txt` porte
`…/NineTeen/build/macos-universal`, et les deux exécutables ne font pas la même
taille — **10 163 528** octets contre **10 170 936**.

### Ce qui était faux, page par page

| Page | Le défaut |
| --- | --- |
| `docs/DEPLOY.md`, les quatre sources de l'adresse | `cmake -B build -DNINETEEN_SERVER_URL=…` : ni compilation, ni le bon arbre. C'est la ligne que le propriétaire a copiée. |
| `docs/DEPLOY.md`, « le défaut compilé est vide » | deux `cmake -B build` de plus, mêmes deux défauts |
| `docs/JOUER.md`, les deux échappatoires hors ligne | configuraient sans jamais compiler |
| `docs/DESIGN-SALLE.md`, `docs/SPEC-ATMOSPHERE.md` | lançaient le binaire sans ligne de configuration |
| `README.md`, `docs/JOUER.md` | ne disaient nulle part que `--preset` ne compile pas |

`docs/JOUER.md` porte maintenant la règle à l'endroit où l'on apprend les presets,
avec la sortie de `cmake` ci-dessus en preuve, et le répertoire de construction
s'écrit `build/<preset>/` partout — jamais `build/` tout court. Les invocations
corrigées ont été relancées : configuration en 0,8 s, `cmake --build --preset
macos-universal -j8` en 12,4 s pour 71 cibles.

### Ce qui reste écrit `build/` exprès

Deux lignes de ce document même : la preuve de la chaîne de score plus haut
(`./build/bin/nineteen --headless --game=snake …`) et la vérification des textures
(`build/assets/scene/textures/`). Ce sont des relevés de séances réelles, faites dans
l'arbre parasite. Les réécrire reviendrait à maquiller une mesure : elles restent, et
elles datent l'époque où les deux arbres coexistaient sans que personne le sache.

---

## Le binaire ne savait pas dire lequel il était

La section précédente corrige la documentation qui a fait lancer un exécutable
périmé. Celle-ci corrige l'autre moitié du même incident : **une fois lancé, rien
dans ce binaire ne permettait de s'apercevoir qu'il était vieux.**

Les deux exécutables de cette séance — celui du 1er septembre 21 h 51 et celui du
jour même, cinq jours et vingt-quatre commits plus loin — ont été interrogés côte à
côte :

```console
$ ./nineteen --version
option inconnue : --version
Nineteen 17.1.0 — salle d'arcade
```

La même réponse des deux côtés, puis la **même bannière mot pour mot**. La seule
différence observable était leur taille en octets, 10 163 528 contre 10 170 936 —
un écart que personne ne regarde et qui ne dit rien de ce qu'il contient. La salle
affichée, elle, n'était pas la même : `salle.gltf` de **224 199** octets et
**499** noms d'un côté, **245 344** et **563** de l'autre. Le jeu montrait donc une
autre pièce et affirmait être la même version.

### `--version`

```console
$ ./build/bin/nineteen --version
Nineteen 17.1.0
commit    : 67205cf-sale
construit : Sep  6 2026 00:43:40
assets    : /…/NineTeen/build/assets
sources   : /…/NineTeen
```

Code de sortie **0**. `-sale` marque un arbre porteur de modifications non
commitées au moment de la construction. La date vient de `__DATE__`/`__TIME__`,
c'est-à-dire de la compilation de `main.c` : quand rien n'a changé, `main.c` n'est
pas recompilé et la date reste celle du binaire réellement en place, ce qui est
exactement ce qu'elle doit dire.

### L'avertissement au démarrage

`--version` ne répond qu'à qui la tape, et ce jour-là personne ne l'a tapée : on ne
soupçonne pas un binaire d'être vieux, on soupçonne le code d'être faux. Le
démarrage pose donc la question tout seul. Il lit `.git/HEAD` puis la ref pointée
— **sans lancer `git`** : quarante octets à lire, aucun processus, et rien à
installer chez un joueur.

```console
$ ./build/bin/nineteen --headless …
WARN main.c:528 — binaire en retard sur ses sources : construit sur le commit
67205cf-sale, le dépôt /…/NineTeen est sur d0b9ada — reconstruisez-le par
« cmake --build /…/NineTeen/build » ; « cmake --preset » ne fait que configurer
```

Mesuré dans les deux sens, sur une branche jetable défaite ensuite : HEAD déplacé
d'un commit, le binaire non reconstruit avertit ; `cmake --build` seul, **sans
reconfigurer**, le fait taire.

| Cas | Ce qui se passe |
| --- | --- |
| HEAD a bougé, binaire pas rebâti | l'avertissement, une seule ligne |
| Même commit, arbre modifié localement | **silence** — c'est l'état de travail normal |
| Binaire installé, plus de sources | silence — rien à comparer |
| `.git` est un fichier (worktree), HEAD détaché | silence — voir plus bas |

### Pourquoi l'empreinte est prise au build et non à la configuration

C'est le point qui décide tout le reste. `cmake --build` **ne reconfigure pas** :
un commit figé au `cmake -B` serait périmé dès la construction suivante, et
l'avertissement crierait « tu es en retard » à un binaire qui vient d'être bâti.
Un avertissement qui se trompe est un avertissement qu'on apprend à ignorer, donc
pire que pas d'avertissement du tout. `room/CMakeLists.txt` réengendre donc
`ns_buildinfo.h` à chaque build, et le recopie par `copy_if_different` pour ne pas
recompiler les 7 000 lignes de `main.c` quand le commit n'a pas bougé — vérifié :
un build à vide ne réexécute que l'étape d'empreinte.

### Ce qui n'est pas couvert

Le repli sur `.git/packed-refs` — le fichier que `git gc` écrit à la place des refs
individuelles — est en place et exercé sur le vrai fichier du dépôt par un banc
d'essai, mais **pas** par une exécution du jeu : le chemin des sources est cuit
dans le binaire, donc le brancher sur un dépôt fabriqué demanderait un second arbre
de build entier. Les cas « worktree » et « HEAD détaché » sont mesurés de la même
façon. Tous trois se taisent par construction : dans le doute, ce code ne dit rien.

---

## Compiler le jeu suffisait à écrire dans la sauvegarde du propriétaire

### Ce qui se voyait

Un `settings.cfg` « qui change tout seul ». Le propriétaire s'en est plaint sans
pouvoir dire quand : le fichier reprenait des valeurs qu'il n'avait pas posées,
entre deux séances où il n'avait pas joué.

### Ce qui se passait

Toute exécution du jeu écrit dans `~/Library/Application Support/recognizer/Nineteen/`.
Pas seulement une partie — **n'importe quelle** exécution :

```console
$ nineteen --headless --frames=1
```

Cette commande ne joue rien, ne dessine rien, et réécrit pourtant trois fichiers :
`nineteen.log`, `settings.cfg` **et** `portefeuille.txt`. Or ce répertoire est
celui du propriétaire : il contient ses jetons, ses tickets, et un solde dont la
vérification est en cours. `ctest` y passait aussi — les fichiers
`menu-test.cfg`, `menu-fenetre-test.cfg` et `menu-palier-test.cfg` qu'y déposait
`ns_test_menu` s'y trouvaient encore, datés d'exécutions antérieures. Chacun de
ces tests s'applique pourtant à effacer ce qu'il écrit, et c'est bien la preuve
que le problème n'était pas la propreté : c'était l'**adresse**.

### Pourquoi personne n'y échappait

`engine/core/ns_paths.c` appelait `SDL_GetPrefPath("recognizer", "Nineteen")`,
seul point d'appel du dépôt, sans aucune porte de sortie. Le réflexe — rediriger
`HOME` — **ne protège rien** : sur macOS SDL passe par `NSHomeDirectory`, qui
ignore la variable. Trois intervenants s'y sont fait prendre le même jour, chacun
croyant avoir isolé son exécution. `CFFIXED_USER_HOME` fonctionne, mais c'est un
détail d'implémentation d'Apple, non documenté ici et sans équivalent Linux ni
Windows : s'en servir protégerait une plateforme sur trois, par un moyen que
personne ne penserait à chercher.

### La correction

Une variable à nous, lue au seul endroit qui appelait `SDL_GetPrefPath`.

```
NINETEEN_USER_DIR=/un/repertoire
```

| Cas | Ce qui se passe |
| --- | --- |
| Absente | **Rien ne change.** `SDL_GetPrefPath` décide comme avant |
| Posée | Ce chemin est utilisé, et **créé** s'il manque, parents compris |
| Posée sur un chemin impossible | `NS_WARN`, puis le répertoire habituel reprend la main |

Le troisième cas est le seul qui demandait un arbitrage : perdre la sauvegarde
d'un joueur en silence parce qu'une variable pointe un chemin impossible serait
un défaut plus grave que celui qu'on corrige.

Le démarrage **dit lequel a gagné**, dans la forme déjà employée pour la
définition, le palier et l'adresse du serveur :

```console
$ NINETEEN_USER_DIR=/tmp/bac nineteen --headless --frames=1
INFO ns_paths.c — données : « /tmp/bac/ » (source : NINETEEN_USER_DIR)

$ nineteen --headless --frames=1
INFO ns_paths.c — données : « /Users/…/recognizer/Nineteen/ » (source : SDL_GetPrefPath)

$ NINETEEN_USER_DIR=/dev/null/impossible nineteen --headless --frames=1
WARN ns_paths.c — NINETEEN_USER_DIR = « /dev/null/impossible » : répertoire
impossible à créer (Can't create directory: File exists) — le répertoire
habituel reprend la main
INFO ns_paths.c — données : « /Users/…/recognizer/Nineteen/ » (source : SDL_GetPrefPath)
```

Cette ligne sort sur la **console** et non dans `nineteen.log`, et ce n'est pas un
oubli : c'est ce répertoire-là qui décide où `nineteen.log` s'ouvre, donc la
décision précède forcément le fichier qui la consignerait.

Le détail qui aurait mordu : le chemin ressort avec sa **barre finale**. Les onze
appelants de `ns_path_user_dir()` concatènent sans séparateur — `ns_config.c` fait
`"%s%s"`, `room/main.c` fait `"%snineteen.log"` — parce que `SDL_GetPrefPath` la
garantit. Une variable écrite à la main, non : `…/bac` aurait donné
`…/bacnineteen.log`, un fichier posé **à côté** du répertoire visé.

### Qui s'en sert

Tout ce qui lance le jeu sans être le joueur : `tools/vues-capture.sh`,
`tools/ambiance-releve.sh`, et les 53 tests de `tests/CMakeLists.txt`, détournés
d'un seul bloc vers `build/donnees-test`. Un seul répertoire pour toute la suite,
délibérément : les tests partageaient déjà celui du propriétaire, donc leur en
donner un chacun changerait leurs conditions d'exécution en même temps que leur
adresse.

`vues-capture.sh` a gagné un `--quality=high` au passage, et il le fallait : ce
script ne passait pas de palier et héritait `render.quality = high` du
`settings.cfg` du propriétaire. Dans un répertoire neuf, c'est le défaut compilé
qui se serait appliqué — `medium` — et les captures auraient changé sans que
personne l'ait demandé. `ambiance-releve.sh` figeait déjà le sien, pour cette
raison exactement.

### La preuve

Empreintes du répertoire du propriétaire, relevées juste avant et juste après une
suite complète, doublées d'une sonde qui recalcule l'empreinte du dossier **toutes
les 0,2 seconde** pendant l'exécution :

```console
$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 54
Total Test time (real) =  42.86 sec

$ diff AVANT.txt APRES.txt
< ### AVANT — 01:16:29
> ### APRES — 01:17:13

$ cat sonde.txt        # la sonde, pendant toute la durée de la suite
(vide)
```

Seul l'horodatage du relevé diffère. `portefeuille.txt` reste à
`78ad58fd89201847dfcb8e8e8e5e174ec9a89650`, et la date de modification du
**répertoire** ne bouge pas non plus, ce qui prouve qu'aucune entrée n'y a été
créée ni retirée. La sonde compte parce qu'un fichier réécrit à l'identique puis
remis en place passerait entre deux relevés d'extrémités ; elle n'a rien vu.

Les deux sens sont mesurés séparément. Avec la variable, les trois fichiers
atterrissent dans le bac à sable et nulle part ailleurs. Sans elle — vérifié sous
un faux `CFFIXED_USER_HOME`, pour ne pas écrire chez le propriétaire — le jeu
reconstruit exactement `…/Library/Application Support/recognizer/Nineteen/`.
Huit lancements de `vues-capture.sh` laissent le répertoire du propriétaire
identique au bit et à la date près.

L'équivalence des captures est mesurée avec l'instrument du dépôt, parce qu'un
rendu **n'est pas** reproductible au bit près — deux lancements identiques
donnent deux PNG différents. `tools/vues-diff.py` compare l'ancien comportement
(le `settings.cfg` du propriétaire) au nouveau (bac vide, palier figé) et rend
**0,00 %** de sous-pixels changés, écart moyen 0,03, max 1 : exactement le bruit
de fond de deux lancements identiques.

### Ce qui n'est pas couvert

Le repli n'est pas exercé par `ctest` : il appelle `SDL_GetPrefPath`, donc il vise
le répertoire du joueur, ce que la suite ne fait plus. Il est mesuré à la main,
ci-dessus. `tools/site-media.py` lance aussi le jeu et n'a pas été détourné.

---

## Ce qui n'a pas changé

Les six décisions de 17.0.0 restent ouvertes, et la première reste la seule qui
bloque une vente au grand public : **les paquets ne sont signés d'aucune façon**,
donc macOS met en quarantaine et Windows affiche SmartScreen. Voir
`docs/CHANGELOG-V17.md`.
