# Le temps réel — ce qui est prêt, ce qui manque, et ce qu'il reste à trancher

Tu as tranché en début de projet : **le classement en ligne maintenant, la présence
temps réel et les duels plus tard, conçus ensemble.** Ce document a d'abord été la
moitié que je pouvais faire seul — établir les faits pour que la conception se
fasse sur des chiffres et non sur des impressions.

**Il a maintenant deux parties, et il faut savoir laquelle on lit.** Tout ce qui
suit jusqu'à « Les trois formes possibles » reste le constat d'origine, y compris
l'affirmation fausse que j'y corrige. À partir de « Ce qui a été fait », c'est le
compte rendu de ce qui est écrit, branché et mesuré : **la présence et le duel en
différé sont livrés ; le duel en direct ne l'est pas**, et la dernière section dit
précisément ce qu'il faudrait pour l'envisager.

Là où le constat d'origine et le résultat divergent, c'est signalé sur place —
notamment le rythme de la présence, annoncé ici à ~20 Hz et livré à 4 Hz pour une
raison de transport.

---

## Une affirmation de ma part qui était fausse

J'ai écrit, dans le plan et plus d'une fois depuis :

> « Le journal de partie est déjà une suite d'entrées horodatées au pas fixe —
> c'est exactement ce qu'il faut pour rejouer une partie chez quelqu'un d'autre,
> donc pour un duel. »

**C'est faux, et le mot « entrées » est l'endroit exact où ça se joue.** Le
journal enregistre des `ns_run_event { at_ms, kind, value }` où `kind` vaut
`pipe`, `score`, `lines`, `cell`, `death` : ce sont des **conséquences**, pas des
appuis sur des boutons. On y lit qu'un tuyau a été passé à 2 340 ms ; on n'y lit
nulle part que la barre d'espace a été pressée au tic 281.

Conséquence : le journal **authentifie un score** — c'est son travail et il le
fait bien — mais il **ne rejoue pas une partie**. La fondation que j'annonçais
posée ne l'est pas.

---

## Ce qui EST prêt, et vérifié

| Brique | État | Où c'est prouvé |
|---|---|---|
| Les huit jeux sont **déterministes** : même graine + mêmes entrées → même partie | vérifié pour les **8/8** | un `test_determinisme` dans chacun des `tests/test_<jeu>.c` |
| Le jeu avance à **pas fixe** (120 Hz), jamais à l'image | vérifié | `ns_clock`, `tests/test_core.c` |
| La **graine vient du serveur**, avant que la partie soit jouée | vérifié de bout en bout | B23, contre `nineteend` + PostgreSQL |
| Le journal est **scellé** (HMAC-SHA256, charge canonique identique au Go) | vérifié | `tests/test_scores.c`, vecteurs produits par le code Go |
| Le transport HTTP, la file d'attente, le renvoi différé | vérifié | `ns_http`, `ns_online`, `ns_test_online <url> <jeton>` |

C'est beaucoup, et c'est la partie difficile : **un jeu non déterministe rend un
duel impossible**, quel que soit le réseau qu'on mette dessus. Cette propriété-là
est acquise pour les huit.

---

## La propriété dont tout dépend est maintenant mesurée

`tests/test_replay.c` répond à la seule question qui décide si un duel est
possible : **une suite de masques de boutons, un par pas fixe, suffit-elle à
reproduire une partie ?**

Pour chacun des huit jeux, il fabrique une suite d'entrées plausible et
reproductible — une direction tenue quelques dixièmes de seconde, le bouton
d'action tapé par à-coups, pas du bruit blanc qui ne ferait rien bouger dans un
jeu où l'on tourne en tenant une touche — la joue deux fois à travers
`press`/`hold`, et compare :

- le score,
- les gains cumulés vus par `events`,
- **et l'état complet, au bit près** — deux parties peuvent finir sur le même
  score en ayant divergé, et pour un duel les deux machines doivent voir LA MÊME
  partie, pas seulement le même nombre.

Plus deux garde-fous contre un test qui passerait à vide : une AUTRE suite
d'entrées doit donner une autre partie (sans quoi le test passerait sur un jeu
qui ignore ses commandes), et l'état final doit différer d'un état fraîchement
remis à zéro (sans quoi il passerait sur une partie qui n'a pas bougé).

**Résultat : 8/8 rejoués à l'identique, 49 vérifications.** Sept des huit
meurent avant la fin du journal, ce qui est exactement ce que fait une suite
d'entrées prise au hasard ; Piano n'a pas de condition de mort.

Ce que ça vaut sans aucun duel : un jeu qui lirait une horloge, un `rand()` non
semé ou un état résiduel entre deux parties serait signalé ici. Trois défauts
qui ne se voient pas en jouant et qui rendent tout rapport de bug
irreproductible.

Ce que ça ne dit pas : rien sur le déterminisme ENTRE machines différentes. Tout
tourne ici sur la même.

## Le journal d'entrées existe maintenant

`ns_runlog` porte un second journal, à côté de celui des événements, et pour une
raison qui n'attend pas les duels : **une partie devient reproductible.**

```sh
./build/linux-x64/bin/nineteen --journal-entrees=partie.txt
```

Un rapport de bug cesse d'être « ça a planté quelque part après deux minutes »
pour devenir un fichier qu'on rejoue. C'est pour ça que cette moitié est écrite
et que le réseau temps réel ne l'est pas.

Deux journaux et pas un, parce qu'ils ne répondent pas à la même question : celui
des événements enregistre des conséquences et sert à AUTHENTIFIER un score — le
serveur le recalcule, le sceau le protège ; celui des entrées enregistre des
appuis et sert à REJOUER. Le second n'entre ni dans la charge canonique ni dans
le sceau : il n'a rien à prouver au serveur.

Le format est du texte, une ligne par changement :

```
v1 demineur normal 20240418 3599
0 0 0
281 4 0
```

Le quatrième champ de l'en-tête — le dernier pas de la partie — a été ajouté
depuis, et il est **facultatif** : les journaux à trois champs se relisent
toujours. Il manquait, et son absence cassait le rejeu d'une manière qu'on ne
voyait pas sur un score ; voir « Deux défauts que seul le bout en bout pouvait
voir ».

Du texte parce qu'il se lit à l'œil quand on débogue, et qu'une partie tient dans
quelques kilo-octets — la compresser serait optimiser ce qu'on n'a pas mesuré.

Il s'écrit à la fin de la partie **et à la sortie du programme**. Le premier jet
ne faisait que le premier, ce qui est l'inverse du besoin : on veut ce fichier
quand quelque chose a mal tourné, et dans ce cas la partie n'est jamais
« terminée ».

Et il se relit : `--rejouer=partie.txt` remonte la partie et imprime ce qu'elle
donne, **sans fenêtre ni GPU**. Un enregistrement qu'on ne sait pas relire est
une moitié d'outil.

Ce qui reste pour un duel, du coup, n'est plus l'enregistrement NI le rejeu mais
le TRANSPORT : une route pour déposer et récupérer un journal, et le second jeu
dessiné à côté du sien. C'est la partie qui demande ta décision, parce que la
forme du transport dépend de la forme du duel.

> **Depuis :** ce transport est écrit, et le format a dû changer sur un point que
> cette section ne pouvait pas voir — l'en-tête ne portait pas la DURÉE de la
> partie, donc un fantôme s'arrêtait avant la fin dès que le joueur lâchait les
> commandes. Voir « Deux défauts que seul le bout en bout pouvait voir ».

---

## Les trois formes possibles, et ce qu'elles coûtent

Ce sont trois jeux différents, pas trois implémentations du même. C'est le choix
qui t'appartient.

### 1. Le duel en différé — « bats mon fantôme »

Ton ami joue **la même graine** que toi, plus tard, et voit ta partie rejouée à
côté de la sienne. Aucun serveur temps réel, aucune latence, rien à
synchroniser : on télécharge un journal d'entrées et on le rejoue.

- **Ce qu'il faut** : écrire les entrées dans le journal, une route pour les
  récupérer, un second jeu dessiné en surimpression.
- **Ce que ça ne demande pas** : socket persistante, autorité serveur,
  interpolation.
- **Le risque** : nul côté réseau, et le risque de déterminisme est désormais
  MESURÉ plutôt que supposé — voir « la propriété dont tout dépend » ci-dessus.
- **C'est de loin le meilleur rapport plaisir / risque**, et c'est ce que je
  recommanderais : « affronter ses amis comme en enfance » ne demande pas qu'ils
  soient là à la même seconde.

### 2. Le duel en direct, en pas verrouillé (lockstep)

Deux joueurs, la même partie, chacun envoyant ses entrées par tic et attendant
celles de l'autre avant d'avancer.

- **Ce qu'il faut** : une socket persistante, un tampon d'entrées, et une
  politique quand le paquet est en retard (attendre, ou prédire puis corriger).
- **Le risque** : c'est là qu'il est. Le pas verrouillé transforme la latence en
  saccade partagée, et la moindre divergence de déterminisme fait diverger les
  deux parties **sans que personne ne s'en aperçoive** avant que les scores ne
  se contredisent. Il faut une somme de contrôle d'état échangée
  périodiquement, sans quoi le bug est indébogable.
- Nos jeux sont en virgule flottante ; le déterminisme entre deux machines
  différentes (x86 et Apple Silicon) n'est **pas** garanti par les tests
  actuels, qui tournent tous sur la même. C'est le premier point à mesurer si tu
  choisis cette voie.

### 3. La présence — se voir dans la salle

Pas un duel : juste les autres joueurs visibles, marchant dans l'allée, devant
les bornes.

- **Ce qu'il faut** : un état positionnel diffusé à ~20 Hz, interpolé, et un
  bonhomme à dessiner — qui n'existe pas (on n'a que des bras en vue subjective).
- **Le risque** : modéré et surtout visuel. Techniquement c'est le plus simple
  des trois ; c'est le travail d'art qui domine.

---

## Ce qui a été fait : la présence et le duel en différé

Les formes **3** (la présence) et **1** (le duel en différé) sont écrites,
branchées et vérifiées de bout en bout. La forme **2** (le pas verrouillé) ne
l'est pas, et la dernière section dit pourquoi.

### L'interrupteur, parce que rien de tout ça ne s'allume tout seul

Le temps réel est **inerte par défaut**, et il faut trois « oui » pour qu'il
s'anime :

1. un serveur configuré (`--server=` ou `NS_CFG_SERVER_URL`) ;
2. l'absence de `--offline` ;
3. une activation **explicite** : `--temps-reel`, le réglage persistant
   `network.realtime`, ou la ligne **TEMPS RÉEL** du menu `Échap`.

Les deux premiers sont ceux du classement, et le temps réel en **hérite** au
lieu de les réimplémenter : `ns_realtime_init` demande son URL à
`ns_online_server_url()`, qui rend `NULL` tant que `ns_online_init` n'a pas dit
oui. La garantie « sans URL configurée, aucune socket n'est ouverte » reste donc
écrite **à un seul endroit**. Réimplémenter un verrou, c'est se donner deux
occasions de le poser de travers.

Le troisième est nouveau, et il existe parce que les deux choses n'engagent pas
la même chose : **consulter un classement ne diffuse rien de soi ; la présence
publie un pseudo et une position.** Ce n'est pas à une URL de serveur d'en
décider à la place du joueur.

Ça se constate en une commande, sans lire le code :

```
$ nineteen --temps-reel                          # sans serveur
réseau : aucun serveur configuré, le classement restera local
temps réel : demandé, mais le réseau est inactif — rien ne sera ouvert

$ nineteen --temps-reel --offline --server=…     # verrouillé
réseau : verrouillé par --offline, aucune connexion ne sera tentée
temps réel : demandé, mais le réseau est inactif — rien ne sera ouvert

$ nineteen --server=…                            # le DÉFAUT
réseau : actif sur « … » (lecture seule)
temps réel : désactivé (défaut) — ni présence ni duel
```

### La présence

`POST /api/v1/presence` en un aller-retour : je dis où je suis, le serveur me
dit qui d'autre est là. Une seule requête pour les deux, parce que c'est le seul
échange **périodique** du jeu et que le couper en deux doublerait son trafic.

**Aucun compte n'est exigé** — se montrer dans une salle d'arcade n'est pas une
action privilégiée. Le serveur ne croit donc pas le pseudo : sans jeton il
accepte le nom déclaré et le marque `verified: false` ; avec un jeton il
**écrase** le nom déclaré par celui du compte. Le jeu affiche la différence (les
pseudos non vérifiés sont en ambre) plutôt que de garantir ce qu'il ne sait pas.
C'est la règle de l'autorité serveur, appliquée à ce qu'elle peut ici réellement
établir.

**Le rythme est 4 Hz, pas les ~20 Hz que ce document envisageait plus haut**, et
c'est le transport qui décide : `ns_http` rouvre une socket à chaque requête, et
vingt allers-retours par seconde et par joueur coûteraient vingt poignées de main
TCP pour décrire un bonhomme qui marche à 1,4 m/s. Quatre battements, c'est 35 cm
entre deux positions connues, interpolées au rendu. **Mesuré : 3,7 Hz** sur une
fenêtre de 3 002 ms contre le serveur en conteneur (`tests/test_duel.c` compte
les battements dans une fenêtre franche plutôt que de diviser un total par une
durée supposée — le premier jet affichait 0,2 Hz, ce qui était faux).

Une présence expire en 12 s côté serveur ; le client cesse de rendre des pairs
après 3 s sans réponse. Un joueur immobile est un bogue qu'on regarde, un joueur
absent est une déconnexion qu'on comprend.

**L'avatar est une plaque au nom du joueur**, avec sa borne et son score, plus
une liste « DANS LA SALLE » en haut à droite. Pas un bonhomme, et c'est assumé :
il n'existe aucun modèle de personnage dans ce dépôt — on n'a que des bras en vue
subjective — et la scène est un tampon de géométrie **cuit au build**, sans
chemin pour y ajouter un maillage animé à l'exécution. Une plaque répond
exactement à la question qu'on se pose en entrant dans une salle : **qui est là,
et à quelle borne**. Elle n'est pas occultée par les murs ; la salle est une
pièce ouverte, et un lancer de rayon par joueur et par image pour cacher une
étiquette coûterait plus que ça ne gêne.

### Le duel en différé

Le transport qui manquait, et rien de plus :

| Route | Ce qu'elle fait |
|---|---|
| `POST /api/v1/runs/{id}/inputs` | dépose le journal d'entrées d'une partie **déjà validée** |
| `GET /api/v1/ghosts?game=N` | liste les fantômes d'un créneau, **sans** leur journal |
| `GET /api/v1/ghosts/{id}` | le journal lui-même, en texte brut |
| `POST /api/v1/runs` + `{"ghost":…}` | ouvre une partie **sur la graine du fantôme** |

La dernière ligne est celle qui fait qu'un duel en est un. Deux joueurs sur deux
graines différentes ne jouent pas la même partie : ils jouent deux parties et
comparent deux nombres, ce qui est un classement, et on en a déjà un.

**Rien n'est affaibli par là.** Une graine n'est pas un secret — c'est justement
ce qui doit être partagé. Le secret HMAC reste tiré pour la partie seule, et le
score reste **recalculé par le serveur** depuis le journal d'événements scellé.
Déposer un journal d'entrées ne peut donc pas créer de score : le serveur refuse
un journal qui ne se rattache pas à une partie qu'il a lui-même ouverte, close et
validée, et il ne le rejoue pas. Au pire on dépose des appuis qui ne
reproduisent rien, et le seul perdant est celui qui croyait avoir enregistré son
fantôme.

En jeu : en arrivant devant une borne, le client demande la liste et télécharge
**le meilleur** — pas de menu de sélection, à une borne d'arcade on essaie de
battre le meilleur. Pendant la partie, le fantôme avance **d'un pas exactement
quand le joueur avance d'un pas**, et un tableau affiche les deux scores.

### Deux défauts que seul le bout en bout pouvait voir

Ce sont les deux du même genre que les trois du classement : chaque moitié était
juste, et elles étaient fausses ensemble.

1. **Le journal encodé en JSON serait arrivé illisible.** `ns_json_string`
   (engine/core/ns_json.c) ne **déséchappe pas** : il rend les octets bruts entre
   les guillemets. Un journal de trois cents lignes serait donc arrivé comme une
   seule ligne parsemée de « \n » littéraux, et l'analyseur, qui découpe sur les
   retours à la ligne, en aurait tiré **zéro** entrée — un fantôme immobile, sans
   un message d'erreur. Le serveur produisait du JSON valide ; le client lisait
   ce qu'on lui avait dit de lire. Corrigé en supprimant l'endroit où les deux
   peuvent diverger : le journal voyage en **texte brut** dans les deux sens, et
   du texte brut n'a pas d'échappement.

2. **Le journal ne portait pas la durée de la partie.** Il n'enregistre que les
   **changements** de commandes — c'est ce qui le garde à quelques kilo-octets —
   mais un joueur qui lâche les commandes avant de mourir laisse un journal dont
   la dernière ligne précède la fin. Le rejeu, qui déduisait la durée de cette
   dernière ligne, **s'arrêtait trop tôt**. Le score pouvait coïncider quand
   même ; l'état non. L'en-tête porte maintenant un quatrième champ facultatif —
   le dernier pas — et l'analyseur **synthétise** l'entrée finale manquante, ce
   qui corrige d'un coup `--rejouer=`, le fantôme et les tests sans changer une
   seule signature. Les journaux au format à trois champs se relisent toujours.

Ce second défaut a été attrapé par une seule assertion : comparer l'**état
complet au bit près** après un aller-retour réseau, et pas seulement le score.

### Comment c'est prouvé

`tests/test_duel.c`, sur le modèle de `test_online.c` et pour la même raison.

```sh
ns_test_duel                       # les verrous, l'analyseur, le serveur mort
ns_test_duel <url>                 # + la présence ANONYME
ns_test_duel <url> <jeton>         # + la chaîne entière du duel
```

Sans argument — ce que fait la CI — il vérifie les **deux verrous**, l'analyseur
de journal face à ce qu'un serveur peut envoyer (corps sans octet nul terminal,
sans retour à la ligne final, lignes illisibles, masques hors bornes), et le
serveur mort. Rien n'ouvre de socket vers quoi que ce soit d'écoutant.

Avec un serveur et un jeton, il déroule : billet → **partie de Pac-Man réellement
jouée par le chemin des boutons** → envoi → dépôt du fantôme → liste →
téléchargement → **rejeu** → billet de duel. Résultats mesurés contre
`docker compose up` :

| Régime | Vérifications |
|---|---|
| hors ligne (CI) | **33**, 0 échec |
| + présence anonyme | **44**, 0 échec |
| + chaîne complète du duel | **54**, 0 échec |
| `ns_test_online` (non régressé) | **31**, 0 échec |

Les assertions qui portent le reste :

- le journal redescendu est **octet pour octet** celui qu'on a déposé ;
- rejoué sur la graine du serveur, il **refait le même score** ;
- **et le même état au bit près, après un aller-retour réseau** ;
- le billet de duel porte **la même graine** que le fantôme, tout en étant une
  partie distincte avec son propre secret ;
- la graine survit sur ses **63 bits** (une relecture en flottant n'en garderait
  que 24 — c'est le défaut n° 2 du classement, qui se serait reproduit mot pour
  mot).

Le cas « le serveur tombe au milieu » est vérifié aussi : publier une position
**ne bloque jamais** (mesuré : < 100 ms pour 200 appels vers un serveur mort),
aucun joueur fantôme n'apparaît, et la partie continue.

---

## Le déterminisme entre machines : une première mesure

Ce document demandait de mesurer ça avant d'envisager le pas verrouillé. C'est
fait, partiellement, et voici exactement ce que ça vaut.

Le binaire `macos-universal` contient les deux jeux d'instructions. On peut donc
faire tourner **le même exécutable** sur deux architectures :

```sh
arch -arm64  ./build/macos-universal/bin/ns_test_replay
arch -x86_64 ./build/macos-universal/bin/ns_test_replay
```

**Résultat : sorties identiques pour les huit jeux** — mêmes scores, mêmes gains
cumulés, 49/49 vérifications de part et d'autre.

Ce que ça établit : sur ce compilateur et ce système, l'arithmétique flottante
des huit jeux donne le même résultat sur ARM et sur x86. C'est encourageant, et
c'est plus que ce qu'on avait, qui était rien.

**Ce que ça n'établit pas**, et il faut le dire aussi net : la comparaison porte
sur les scores et les gains **imprimés**, pas sur l'état complet octet par octet
entre les deux architectures — le test compare les états au bit près à
l'intérieur d'une exécution, pas d'une architecture à l'autre. Et tout tourne ici
avec **le même compilateur** et la même bibliothèque mathématique. Un autre
compilateur, une autre libm, ou `-ffast-math` quelque part peuvent tout changer.
Ce n'est donc pas un feu vert pour le pas verrouillé : c'est un premier point
mesuré sur une droite qui en demande plusieurs.

---

## Le duel EN DIRECT — livré, et ce qu'il a fallu pour ça

Cette section disait « pas livrée, et c'est un choix ». Elle posait trois
conditions ; les voici, avec ce que chacune a trouvé en chemin.

### 1. Une empreinte d'état

`ns_game_state_hash` — FNV-1a sur les `state_size` octets, dans `games.c`.
`--rejouer=` l'imprime, ce qui fait de l'outil un instrument : deux machines
peuvent tomber sur le même score par des chemins différents, un oiseau mort deux
pas plus tôt après le même nombre de tuyaux donne le même chiffre.

FNV et non un condensé cryptographique : on cherche une divergence entre deux
machines de bonne foi. Le score, lui, reste scellé par HMAC et **recalculé par
le serveur** — le relais n'y touche pas.

### 2. Le déterminisme entre architectures, mesuré

Le binaire de ce dépôt est **universel** : il contient arm64 et x86_64. `arch
-arm64` et `arch -x86_64` rejouent donc le même journal avec le même code, sur
la même machine. Sur les huit jeux :

> sept identiques au bit près, **Piano non** — et, en creusant, trois rejeux du
> même journal sur la même architecture donnaient trois empreintes différentes.

La cause : un `const char *fail_reason` dans l'état de Piano, dont l'adresse
change à chaque exécution sous ASLR. `games.h` promet depuis le début que « tout
vit dans le bloc rendu par `state_size` » ; un pointeur dans ce bloc rompt la
promesse. Remplacé par une énumération. **8 / 8 identiques** ensuite.

`tests/check_determinism.cmake` rejoue désormais chaque journal dans DEUX
processus et compare — c'est ce que `test_replay.c` ne pouvait pas faire, lui
qui rejoue dans le même processus où un littéral a la même adresse.

### 3. Un transport qui tienne le tic

`engine/net/ns_lockstep.{h,c}` : socket persistante, protocole binaire à trames
préfixées de leur longueur, six types, dix octets pour l'entrée d'un pas — soit
1,2 kio/s par joueur à 120 Hz. `TCP_NODELAY` n'y est pas une optimisation mais
une condition : l'algorithme de Nagle ajouterait à chaque pas les dizaines de
millisecondes que le retard d'entrée est censé absorber.

Le relais est `server/internal/duel` et `server/cmd/duelrelay`, sur son propre
port, vide par défaut. Il RECOPIE les trames et ne fait rien d'autre : ni
simulation, ni validation, ni score. Un relais qui arbitrerait serait une
deuxième autorité, et il faudrait y porter les règles des huit jeux en Go, en
double de leur version C.

### 4. La forme du duel, et pourquoi ce n'est pas un pas verrouillé pur

Deux parties **séparées** sur la même graine, chacune rejouant celle de l'autre
avec huit pas de retard. Pas une simulation unique nourrie par deux joueurs.

La différence compte pour le joueur : dans un pas verrouillé pur, la partie de
chacun s'arrête dès que l'autre a un hoquet réseau. Ici un hoquet fige
l'adversaire à l'écran, pas la borne sous les doigts.

L'empreinte publiée est donc celle de SA partie, et celle qu'on vérifie est
celle du FANTÔME — notre copie de la partie de l'autre. Le module ne compare
rien tout seul : il imposerait la première forme.

### Ce qui a été trouvé en branchant

- **Le record personnel entre dans l'état.** `set_best` écrit dans le bloc dont
  on compare l'empreinte, et deux joueurs n'ont pas le même record : les deux
  clients annonçaient « divergence au pas 0 » sur une partie parfaitement saine.
  Le record est mis à zéro le temps d'un duel — il n'a de toute façon rien à
  faire dans une course à deux.
- **L'INSTANT de l'empreinte compte autant que sa valeur.** Elle était publiée
  après les appuis mais avant le pas, tandis que le fantôme était mesuré avant
  ses appuis : deux photographies du même pas prises à deux moments différents
  ne coïncident jamais. Les deux côtés la prennent maintenant APRÈS le pas, le
  seul instant qu'on puisse nommer sans ambiguïté.
- **`--autoplay` ne peut pas dueller**, et le duel l'a révélé : `autopilot()`
  appelle les fonctions du jeu directement — `flappy_flap` — au lieu de passer
  par `press`. Ses appuis n'entrent jamais dans le journal, donc jamais dans la
  socket. La combinaison est refusée, comme celle avec `--warmup=`.

### Comment on le vérifie

```sh
cd server && go run ./cmd/duelrelay -addr 127.0.0.1:8081 &
ns_test_lockstep 127.0.0.1 8081
```

Deux clients, un vrai relais, de vraies sockets. Ce qui est mesuré :

| Contrôle | Résultat |
|---|---|
| appariement et graine commune | les deux reçoivent la même, tirée par le relais |
| une troisième connexion sur une place prise | refusée |
| 240 pas de duel sain | **état commun au bit près**, aucune divergence |
| une empreinte FALSIFIÉE au pas 48 | détectée au pas 48, **par les deux joueurs** |

Le dernier contrôle est celui qui compte. Un duel qui reste synchronisé quand
tout va bien ne prouve rien — deux simulations identiques nourries des mêmes
entrées le seraient de toute façon. Ce qu'il faut prouver, c'est qu'on S'EN
APERÇOIT quand elles divergent.

Il a d'ailleurs fallu deux essais pour le rendre honnête. La première version
faisait jouer à l'un un appui de plus en espérant que le jeu diverge : un test au
HASARD, puisque la graine vient du relais et change à chaque exécution — selon
elle, l'oiseau était parfois déjà mort au pas visé, et le test échouait sans
qu'aucun code ne soit fautif.

En jeu : `--duel-direct=hôte:port,identifiant,place`, avec `--game=`.

---

## Ce qui reste

- **Le déterminisme sur TROIS systèmes**, et pas seulement sur deux
  architectures d'un même macOS avec le même compilateur. C'est la mesure qui
  manque pour affirmer qu'un duel Windows-Linux tient.
- **La découverte d'adversaire.** L'identifiant de duel se convient hors bande.
  Le classement sait déjà qui joue à quoi (`ns_realtime_peers`) ; il y aurait un
  « défier » à écrire.
- **Le relais ne parle pas TLS**, comme le reste du réseau de ce projet, et pour
  la même raison — voir `ns_http.h`.

