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
  bonhomme à dessiner — qui n'existait pas quand ces lignes ont été écrites (on
  n'avait que des bras en vue subjective). Les deux points sont réglés depuis :
  4 Hz interpolés plutôt que 20 Hz, et le personnage de `ns_skin`. Voir plus bas.
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

**L'avatar est un personnage qui marche**, surmonté d'une plaque à son nom, avec
sa borne et son score, plus une liste « DANS LA SALLE » en haut à droite.

Ce paragraphe disait le contraire, et il avait raison de le dire à l'époque : il
n'existait alors aucun modèle de personnage dans le dépôt. Il en existe un depuis
— `engine/anim/ns_skin.c`, celui que le joueur porte en troisième personne — et
les pairs portent le même. Ce qu'il a fallu lever pour ça tient en trois points,
tous dans `room/room_presence.h` :

- **Le rendu ne savait dessiner QU'UN personnage.**
  `ns_renderer_set_character` est devenu `ns_renderer_set_characters`. Le
  maillage reste monté une seule fois ; seules changent la matrice de modèle et
  les 32 matrices d'os, soit 2 176 octets d'uniformes poussés par corps et par
  image. Mesuré à 1400x875 en qualité haute : **seize corps ajoutent 0,65 ms à
  une image qui en prend 24,3**, soit 2,7 %. Il n'y a donc aucun plafond sous la
  borne du réseau (`NS_RT_MAX_PEERS` = 16).
- **Les positions arrivent à 4 Hz, le rendu tourne à 60.** Elles sont
  **interpolées et jamais extrapolées** : le corps montré est celui d'il y a une
  période, soit **250 ms de retard** plus le trajet réseau. Extrapoler ferait
  reculer un pair qui s'arrête, c'est-à-dire devant une borne.
- **La phase de marche n'est pas publiée.** Elle est déduite de la distance que
  le corps *rendu* parcourt, convertie par `ns_skin_stride_length` : une foulée
  parcourue fait un tour de cycle, ce qui est la seule façon que les pieds ne
  patinent pas. À l'arrêt, le cycle revient à la pose de passage.

Le **cap** vient du champ `yaw`, déjà au protocole ; un pair qui ne le publie pas
— une version antérieure — est orienté par sa direction de déplacement. La
migration `0004_hauteur_oeil.sql` ajoute `eye`, la hauteur d'œil au-dessus des
pieds, sans laquelle un pair accroupi s'enfonce de 39 cm dans la moquette.

**Un pair n'est pas un obstacle**, et c'est écrit dans le code : avec 250 ms de
retard, faire d'un corps un mur bloquerait le joueur contre quelqu'un qui n'est
plus là. On se traverse.

**L'étiquette, elle, n'est toujours pas occultée par les murs** ; la salle est
une pièce ouverte, et un lancer de rayon par joueur et par image pour cacher une
étiquette coûterait plus que ça ne gêne. Le **corps**, lui, l'est : il passe par
la passe des personnages, qui charge la profondeur de la scène et teste contre
elle. Un pair derrière une borne est donc caché par la borne, et seul son nom
flotte au-dessus — voir `docs/render-presence-allee.png`, où c'est le cas du
marcheur de gauche.

Pour regarder tout ça sans serveur : `--pairs-demo=N` peuple l'allée de N
marcheurs fabriqués, qui entrent par le même chemin que les vrais pairs. Inerte
par défaut, comme le reste du temps réel.

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

## L'ARÈNE — le même relais, à N places

Le mode compétitif met **deux à huit joueurs** dans la même salle en même temps : chacun sur sa
borne, des points, des sabotages qu'on s'achète et qu'on lance sur les autres, et un « couperet »
qui élimine périodiquement le dernier du classement.

Le transport qu'il demande est celui du duel, **au nombre de places près**. C'est donc le même
relais, sur le même port, avec un type de trame de plus — et la même bêtise volontaire.

### Ce qu'elle transporte

Le cadrage ne bouge pas : `uint16 longueur (petit-boutiste) | uint8 type | charge`, 512 octets de
charge au maximum.

| Type | Sens | Charge |
|---|---|---|
| `0x10` `fJoin` | client → relais | `uint64 salon` \| `uint8 place` \| `uint8 places_attendues` \| `char pseudo[24]` |
| `0x11` `fRoster` | relais → tous | `uint8 n` \| `{ uint8 place ; char pseudo[24] } × n` |
| `0x02` `fStart` | relais → tous | `uint64 graine` — **la même trame que le duel** |
| `0x05` `fBye` | relais → tous | `uint8 place` : qui est parti |
| `≥ 0x20` | client → relais | **diffusé tel quel** à toutes les *autres* places, précédé de l'octet de place de l'émetteur |

`fJoin` est un type **nouveau**, et pas un `fHello` rallongé. Un `if len(charge) >= 34` sur le
HELLO ferait dépendre le *sens* d'une trame de sa *longueur* : le jour où un client installé
allonge son HELLO d'un champ, il se réveille dans l'arène. Le chemin du duel n'est donc pas
touché — même HELLO, mêmes places 0 et 1, même recopie vers l'**autre** et non une diffusion.

Tout ce qui est au-dessus de `0x20` traverse sans être interprété : c'est par là que passent
l'état des joueurs, les sabotages et le verdict du couperet, et le relais n'a pas à savoir ce que
c'est. En dessous de `0x20`, les types appartiennent au relais et ne sont **jamais** rediffusés —
sans quoi n'importe qui fabriquerait un START, un tableau des places, ou le départ d'un autre.

### Ce qu'elle n'arbitre pas, et la conséquence assumée

Le relais **apparie et diffuse**. Il ne connaît ni score, ni classement, ni couperet, ni minuterie
de partie. Aucune règle de jeu n'est portée en Go : la règle vit en C, une seule fois, chez le
joueur de **la place 0**, qui est l'arbitre et publie son verdict comme n'importe quelle autre
trame.

**La place 0 peut donc mentir.** C'est écrit ici parce que c'est une concession, et c'est
exactement la même que celle du duel — deux clients complices peuvent se mentir l'un à l'autre —
assumée pour la même raison : l'autorité sur les scores **enregistrés** reste le journal scellé
par HMAC, envoyé par HTTP et recalculé par le serveur. Rien de l'arène ne le touche.

L'alternative aurait été un arbitre en Go. Il faudrait alors y porter la règle du couperet, celle
des sabotages et le barème des points, en double de leur version C — soit la duplication qui
finit toujours par diverger, pour défendre un classement qui n'est de toute façon pas décidé là.

### L'identité de l'émetteur est **insérée**

Le relais écrit un octet devant chaque trame libre qu'il diffuse : la place de l'émetteur. Le
raisonnement, parce que l'autre option — diffuser brut — était défendable :

- **Le prix réel est d'un octet sur le fil, pas d'une recopie.** Le relais recopie déjà chaque
  charge une fois (`readFrame` alloue, `frame` recopie ailleurs) ; la version préfixée alloue une
  fois et recopie une fois, exactement comme l'autre. Il n'y a pas de tampon supplémentaire.
- **Sans elle, un sabotage « de la part de la place 3 » se fabrique en changeant un octet chez
  soi.** Le mode entier repose sur qui a envoyé quoi.
- **Le client n'y gagnerait rien** : il devrait de toute façon écrire sa place dans la charge pour
  que la trame veuille dire quelque chose. On ne supprime pas un octet, on décide **qui** l'écrit
  — et celui qui l'écrit ici ne peut pas se tromper de place.

Ce que ça ne ferme pas, et il faut le dire aussi net : l'arbitre ment de sa propre place. On ferme
l'usurpation *entre joueurs*, pas la malhonnêteté de la place 0, qui est assumée plus haut.

### Les règles d'appariement

- `places_attendues` va de **2 à 8**. Toutes les places d'un même salon doivent annoncer la même
  valeur ; la première annonce fait foi, une autre valeur fait refuser la connexion. Sans cette
  règle, un salon de huit et un salon de trois partageraient une table et personne ne recevrait
  jamais de START.
- `place` doit être inférieure à `places_attendues`, et **libre**.
- `fStart` part quand **toutes** les places sont prises, avec une graine tirée par `crypto/rand`,
  bit de poids fort effacé — la même règle que le duel, et pour la raison déjà écrite : la graine
  traverse des entiers signés.
- `fRoster` part à **chaque** arrivée et à **chaque** départ, à tout le monde. C'est ce qui permet
  d'afficher un salon en train de se remplir plutôt qu'un écran d'attente muet, et c'est aussi le
  seul endroit où un client apprend le pseudo des autres. Les places y sortent dans l'ordre : le
  client qui dessine le salon n'a pas à trier.
- Un **départ en cours de partie ne ferme pas le salon** : le mode continue avec un joueur de
  moins, et les restants reçoivent le `fBye` portant la place du partant, puis le tableau à jour.
- Un salon **dont il ne reste qu'une place est fermé** : il quitte la table, son identifiant
  redevient libre. On ne coupe pas pour autant la socket du dernier — une socket fermée par le
  relais est indiscernable d'une panne de réseau, et c'est justement le joueur à qui il faut
  montrer une fin de mode.
- Un salon **déjà lancé n'accepte plus personne**, même sur une place libérée. Un arrivant
  recevrait la graine d'une partie commencée depuis longtemps : il serait au pas 0 pendant que les
  autres sont au pas 40 000, et le relais n'a rien pour le rattraper. Le rattraper serait le
  travail de l'arbitre, donc une règle de jeu, donc pas là.
- Un salon **incomplet attend `pairTimeout`** (60 s), et non les 10 s d'inactivité du pas
  verrouillé : un joueur qui patiente devant un salon à moitié plein n'a rien à envoyer.
- La charge utile d'une trame libre s'arrête à **511 octets** et non 512 : l'octet de place doit
  tenir devant. Une charge de 512 fait **fermer la connexion** plutôt que d'être rognée — un octet
  perdu au bout d'une charge n'est pas une trame trop longue, c'est une charge qui veut dire autre
  chose.

Le pseudo est recopié tel quel, à trois coupes près qui ne sont pas des règles de jeu mais de
l'hygiène de champ : au premier octet nul, sans les octets de commande (avec lesquels un joueur
écrirait ce qu'il veut sur l'écran d'un autre), et à 23 octets sans couper une séquence UTF-8 en
deux — 23 et non 24 pour que le champ soit **toujours** terminé par un zéro, puisqu'il atterrit
dans un `char[24]` en C.

### Le plafond se compte en places, pas en salons

Une place coûte une file de 256 trames, et un salon en a jusqu'à **quatre fois plus** qu'un duel.
Continuer à ne compter que les sessions aurait quadruplé le pire cas en silence.

Le calcul, par place et **au pire** :

```
file             256 emplacements × 24 octets (en-tête de tranche)  =    6 144
trames retenues  256 × (3 + 512) octets                            =  131 840
                                                                     ---------
                                                          137 984 octets = 134,8 Kio
```

C'est bien le pire et pas la moyenne : il suppose une file pleine de trames maximales dont chacune
n'est plus retenue que par cette file-là — une trame diffusée est **une** allocation partagée par
ses destinataires, pas une par destinataire. Il ne compte ni l'en-tête du canal ni les piles des
deux routines, et il ignore l'arrondi de l'allocateur, qui joue contre nous.

- **512 places = 70 647 808 octets = 67,4 Mio.** C'est le plafond retenu, et c'est *exactement* ce
  que 256 duels ont toujours pu coûter : le budget ne bouge pas, seule la façon de le dépenser
  change. Il paie 256 duels, ou 64 salons de huit, ou n'importe quel mélange.
- Pour comparaison, ce que l'ancien plafond de 256 **sessions** aurait donné seul le jour de
  l'arène : 256 salons pleins = 2 048 places = **269,5 Mio**, quatre fois le budget, sans que rien
  ne le dise.

Les deux verrous coexistent donc : 256 sessions bornent la *table* (ouvrir des connexions avec des
identifiants différents ne doit pas la faire grossir sans fin), 512 places bornent la *mémoire*.

### Comment c'est prouvé

Le relais n'avait aucun test Go. Il en a maintenant **12, plus 16 sous-tests**, dans
`server/internal/duel/relay_test.go`, tous sur un vrai écouteur TCP sur `127.0.0.1:0` plutôt que
sur `net.Pipe` : les délais de lecture, la socket fermée vue d'en face et le cadrage sur un flux
qui peut se couper n'importe où sont précisément ce qu'on veut éprouver.

```sh
cd server && go test -race ./internal/duel/     # ok, 0 échec
```

| Ce qui est vérifié | Pourquoi c'est là |
|---|---|
| le duel à deux : même graine, trame recopiée **telle quelle** vers l'autre et pas vers l'émetteur, `fBye` de motif `01` | s'il tombe, c'est un client installé qui tombe |
| le START ne part qu'après la **dernière** arrivée, et il est le même pour tous | c'est la définition d'un salon complet |
| le tableau grandit à chaque arrivée, rétrécit à chaque départ | c'est le seul écran d'attente qui dit quelque chose |
| une trame `0x20` de la place 2 arrive aux places 0, 1 et 3, **précédée de `02`**, et pas à la place 2 | la diffusion et l'identité, en une assertion |
| les types réservés au relais ne sont **pas** rediffusés | sinon n'importe qui fabrique un START |
| sept refus : place prise, place hors du salon, `places_attendues` divergentes, bornes 0/1/9/255, JOIN trop court, identifiant déjà pris par un duel, salon déjà lancé | c'est la moitié qu'aucun test C ne pouvait atteindre |
| un départ en cours de partie laisse le salon vivant, et les restants reçoivent le `fBye` avec la **bonne** place | la règle du mode en dépend |
| **120 connexions qui raccrochent au même signal** (12 duels et 12 salons de huit) | le cas qui a déjà emporté le processus : sans le champ `closed`, deux départs simultanés écrivent dans un canal fermé |
| une trame annonçant plus que `frameMax` fait tomber la connexion **sans allouer** | vérifié par le *temps* : on n'envoie que l'en-tête, et un relais qui allouerait d'abord resterait bloqué jusqu'aux 10 s d'inactivité |

Et le duel a été vérifié **par son vrai client**, pas seulement par un test Go : le
`ns_test_lockstep` du dépôt, inchangé, lancé contre le relais modifié.

```
duel sain :
  240 pas joués, état commun f6f3bdc6e12d2f52
divergence provoquée :
  divergence détectée au pas 48 (falsifiée au pas 48)
29 vérifications, 0 échec(s)
```

Ce qui n'est **pas** prouvé ici, et qu'il faut dire : le client C de l'arène ne fait pas partie de
ce qui est décrit ci-dessus. Ce document décrit le transport, et rien de ce que le mode en fera.

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


---

## LE CLIENT C DE L'ARÈNE — `ns_arene`

Le paragraphe qui ferme la section précédente disait : « le client C de l'arène ne fait pas partie
de ce qui est décrit ci-dessus ». Il en fait partie maintenant. `engine/net/ns_arene.{c,h}` est
l'autre bout du relais à huit places, et voici ce qu'il pose, ce qu'il coûte et ce qui est
réellement vérifié.

### Le vocabulaire au-dessus de `0x20`

Le relais ne connaît rien de ce qu'il diffuse : tout ce qui est au-dessus de `0x20` traverse sans
être regardé, précédé du seul octet qu'il écrive lui-même — **la place de l'émetteur**. Le
vocabulaire du mode est donc entièrement défini côté C, et il tient en quatre trames.

| type | ce que ça dit | écrit | reçu |
|---|---|---|---|
| `0x20` **ÉTAT** | l'état d'une place, à 4 Hz | 46 o | 47 o |
| `0x21` **ACTION** | « je vise la place C avec l'action A » | 2 o | 3 o |
| `0x22` **VERDICT** | ce que le couperet a décidé | 7 o | 8 o |
| `0x23` **EFFET** | ce que la règle a fait d'une action | 4 o | 5 o |

L'**ÉTAT**, octet par octet : `uint8` drapeaux (bit 0 vivante, bit 1 régime difficile), `uint8`
camp, `int32` points encaissés, `int32` fusibles, `int32` valeur devant le couperet
(`room_cp_valeur`), `int64` score courant, `char jeu[24]`. Deux choix méritent d'être dits.
`jeu[0] == 0` veut dire « ne joue pas », exactement comme dans `room_cp_place` — un drapeau de plus
aurait pu contredire le nom. Et la *valeur devant le couperet* voyage à côté des *points* parce que
ce sont deux grandeurs et non une : « qui gagne » se juge sur ce qui est en banque, « qui tombe »
sur ce qu'on est en train de faire.

L'**ACTION** ne porte **pas** son auteur. C'est le point entier : le relais l'insère, donc un
joueur ne peut pas saboter « de la part » d'un autre. L'**EFFET**, lui, porte deux auteurs, et ce
n'est pas une redondance — le premier octet, posé par le relais, dit « c'est l'arbitre qui parle » ;
le second dit « de qui venait l'action dont voici le sort ». Sans lui, l'auteur d'une action refusée
ne saurait jamais que ses fusibles lui restent.

Aucune de ces trames ne transporte un type de `room_couperet.h`. `engine/` ne dépend pas de
`room/`, et l'inverse seulement : ce sont des entiers nus dont la salle donne le sens, et le module
ne se recompile pas parce qu'une action a changé de numéro.

### Le rythme : 4 Hz, pour une autre raison que la présence

C'est le rythme de `ns_realtime` (`NS_RT_PERIOD_MS`), mais **la justification de là-bas ne
s'applique pas ici**, et le recopier sans le dire aurait été une double description de plus. La
présence est à 4 Hz parce que `ns_http` rouvre une socket à chaque battement ; ici la socket est
ouverte une fois pour toute la manche, et ce prix-là n'existe pas.

Ce qui décide est la nature de ce qu'on publie. L'ÉTAT n'est pas une position, c'est un **tableau
de scores** : points, fusibles, camp et borne jouée ne bougent qu'à des événements, et les
événements voyagent par leurs propres trames, immédiatement. Le seul champ qui varie sans arrêt est
le score de la partie en cours, et son seul lecteur pressé est le bandeau de menace
(`room_cp_menace`) — un chiffre que regarde un humain, pour qui 250 ms de retard ne se voient pas.

Le prix se calcule exactement :

```
charge d'ÉTAT                          46 octets
sur le fil, en-tête + place ajoutés    50 octets
salon plein, 8 places à 4 Hz           8 × 7 × 4 × 50 = 11 200 o/s au relais
ce qui arrive chez un joueur           7 × 4 × 50     =  1 400 o/s
```

À 20 Hz ce serait cinq fois plus, pour rafraîchir cinq fois plus vite un chiffre que personne ne lit
cinq fois plus vite. À 1 Hz on économiserait 9 kio/s et le bandeau prendrait jusqu'à une seconde de
retard, ce qui se voit. Pour situer : `ns_lockstep.c` chiffre le duel à 1,2 kio/s **par joueur** à
120 Hz.

### Qui arbitre, et ce que ça concède

**La place 0 arbitre** : elle seule fait tourner `room_cp_avancer`, résout les actions par
`room_cp_agir`, et diffuse le verdict. Les autres appliquent. La règle du couperet n'est pas une
fonction du seul état local — `room_cp_agir` consomme le blindage de la *victime* et débite les
fusibles de l'*auteur* — donc huit clients qui résoudraient chacun leur copie divergeraient au
premier ordre d'arrivée différent, et deux d'entre eux se contrediraient sur qui est tombé.

**Hors ligne, le chemin de code est le même.** Sans arène ouverte le handle est nul :
`ns_arene_ma_place(NULL)` vaut 0, `ns_arene_arbitre(NULL)` vaut vrai, et chaque fonction d'envoi ne
fait rien. Il n'y a pas de « mode hors ligne » à maintenir à côté du vrai.

**La place 0 peut donc mentir**, et il faut l'écrire comme le relais écrit la sienne. C'est la même
franchise que « deux clients complices peuvent se mentir pendant un duel ». Ce que ça ne touche
pas : l'autorité sur les scores **enregistrés** n'a pas bougé depuis M6 — journal scellé par HMAC,
envoyé par HTTP, recalculé par le serveur. Une manche de Couperet ne fabrique aucun score mondial ;
elle distribue des points qui naissent au coup d'envoi et meurent au verdict. L'usurpation qui *est*
fermée, elle, l'est par l'octet d'identité du relais : un verdict qui ne vient pas de l'arbitre est
jeté par le client qui le reçoit.

**Pourquoi le relais n'arbitre pas**, alors que ce serait tentant : il faudrait porter la règle en
Go, en double de sa version C — la table des seize durées mesurées, l'exposant 1,35, la période de
45 s, les six actions et leur ordre de résolution. Deux descriptions d'une même chose finissent
toujours par se contredire (c'est le raisonnement de `room_bareme.h`), et le fait que la seconde
soit dans un autre langage n'arrange rien : ça l'aggrave. Un relais qui arbitrerait serait aussi une
seconde autorité, donc une seconde surface à défendre.

### La lame ne doit jamais rester en l'air

L'arbitre est en réalité **la plus petite place présente**, et pas la place 0 en dur. La différence
n'apparaît qu'une fois, mais elle est fatale : le relais laisse sa socket au dernier joueur d'un
salon, donc quand la place 0 raccroche, la liaison des six autres est parfaitement vivante et plus
personne n'avance le couperet. Tout le monde décide sur le **même** tableau — le relais le diffuse
identique à toutes les places — donc personne n'a rien à négocier. Il en va de même quand la liaison
meurt : `ns_arene_arbitre` redevient vrai quelle que soit la place occupée, et la manche se termine
avec les rivaux locaux.

Ce que ça laisse ouvert, et il vaut mieux l'écrire : le temps qu'un tableau vole, deux places
peuvent se croire arbitres et deux verdicts du **même numéro** peuvent arriver. C'est borné par le
temps de vol d'une trame, et la salle s'en protège en une ligne — un verdict dont le numéro est déjà
appliqué se jette. C'est aussi pour ça que le numéro est dans la trame.

### Rien ne bloque, y compris l'ouverture

Un fil de travail par arène, comme `ns_online` et `ns_realtime` : la boucle de jeu dépose et relit.
Trois différences avec `ns_lockstep.c`, et chacune a une raison.

- **Un fil, pas un `poll` appelé par la boucle.** Le duel est en pas verrouillé : sa boucle *doit*
  attendre le pair. L'arène ne doit jamais faire attendre personne.
- **Une connexion non bloquante, avec `select`.** `ns_arene_fermer` doit pouvoir arrêter le fil à
  tout instant, et un `connect` bloquant ne se laisse pas interrompre — `SO_SNDTIMEO` ne le borne
  pas sous Linux, où un hôte injoignable coûte plus d'une minute. Fermer l'arène aurait attendu
  cette minute-là.
- **Une file d'événements bornée**, au lieu d'un anneau indexé par le pas. Quand elle déborde on
  jette **le plus ancien** et on le compte : jeter le plus récent perdrait le verdict qui vient de
  tomber pour garder un état périmé de trois secondes.

### Le verrou est hérité, et il est mesuré

`ns_arene_ouvrir` interroge `ns_online_server_url()` et rend `NULL` sans rien tenter si la réponse
est `NULL`. La garantie « sans URL configurée, aucune socket n'est ouverte » reste écrite à **un
seul endroit**, `ns_online_init`, exactement comme `ns_realtime_init` en hérite. Le relais n'est
pourtant pas le serveur HTTP — autre port, autre protocole, adresse fournie par l'appelant — et
hériter quand même est délibéré : il n'y a aucune raison d'ouvrir l'une des deux sockets quand
l'autre est interdite, et `--offline` doit vouloir dire hors ligne.

Ce module n'ajoute **pas** de second verrou propre, contrairement au temps réel : la présence
publie une position sans qu'on ait rien demandé, alors qu'entrer dans une arène est un geste
explicite.

Et la promesse se **mesure** au lieu de se relire. `ns_arene_sockets()` est incrémenté juste avant
le seul `socket()` du fichier ; le test le lit avant et après une tentative vers une adresse
*valable*, sans URL puis sous `--offline`. Viser un hôte inexistant aurait donné le même refus sans
rien prouver.

### Comment c'est prouvé

`tests/test_arene.c`, deux régimes comme `ns_test_lockstep` et pour la même raison : le protocole
est écrit deux fois, une fois en C et une fois en Go, et un faux relais écrit en C ne prouverait
que la cohérence du C avec lui-même.

**Sans relais — 142 vérifications, et c'est ce que fait la CI.** Le verrou ci-dessus ; le chemin
hors ligne (les onze fonctions publiques appelées sur un pointeur nul) ; « qui arbitre » sur les
**256** tableaux de places possibles ; chaque trame aller-retour, y compris le pseudo de 23 octets,
le tableau de huit (201 octets), un score négatif sur 64 bits ; les charges malveillantes — trame
tronquée d'un octet, 513 octets annoncés, 65 535 annoncés, tableau qui annonce huit places et n'en
porte que trois, entrée de place 8, issue hors énumération — dont aucune n'alloue ni ne déborde ; et
le découpage, la même trame livrée **octet par octet** devant rendre zéro à chacun de ses préfixes
puis exactement sa taille, deux trames dans un seul paquet, une charge de 511.

**Avec une adresse — 194 vérifications contre le vrai relais Go.**

```sh
cd server && go run ./cmd/duelrelay -addr 127.0.0.1:8099 &
ns_test_arene 127.0.0.1 8099
```

```
salon de deux contre un vrai relais :
  salon de 2 : 8 trames reçues, 2 émises, graine 1268649438bd5e3f
entrée refusée :
  place prise : « entree refusee : place 0 du salon 326023 »
194 vérifications, 0 échec(s)
```

Ce que ces 52 vérifications de plus ajoutent : deux clients dans un salon de deux reçoivent la
**même** graine, bit de poids fort effacé ; une action de la place 1 arrive à la place 0 **attribuée
à la place 1**, sans que la place 1 ait écrit son numéro nulle part ; le verdict et l'effet
descendent de l'arbitre ; une place qui n'arbitre pas n'émet **rien**, mesuré au compteur de trames
émises faute de quoi on ne prouverait qu'une absence ; la place 0 raccroche et la place 1 **reprend
la lame** ; et une place déjà prise se voit comme une fin de liaison avec un motif.

**Ce qui n'est pas prouvé, et il faut le dire.** Le découpage est vérifié sur `ns_arene_trame`, la
seule fonction du module qui sache découper — le fil s'en sert, il ne le refait pas — mais le
tampon de réception du fil lui-même n'est exercé que par le régime avec relais, c'est-à-dire pas en
CI. Et rien ici ne mesure un salon de **huit** contre un vrai relais : le test en ouvre deux. Le
relais, lui, a ses salons de huit dans `relay_test.go`, mais des deux côtés à la fois, personne ne
l'a encore fait.
