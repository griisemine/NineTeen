# Le temps réel — ce qui est prêt, ce qui manque, et ce qu'il reste à trancher

Tu as tranché en début de projet : **le classement en ligne maintenant, la présence
temps réel et les duels plus tard, conçus ensemble.** Ce document est la moitié
que je peux faire seul — établir les faits pour que la conception se fasse sur
des chiffres et non sur des impressions. Il ne contient pas de code, et c'est
voulu.

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

## Ce qui MANQUE, précisément

**Une seule chose, mais elle est structurante : le journal ne porte pas les
entrées.**

Pour rejouer une partie ailleurs il faut, par tic où quelque chose change, le
masque des boutons tenus. Le moteur a déjà cette information au bon endroit —
`api->press` et `api->hold` la reçoivent dans la boucle à pas fixe de
`room/main.c` — elle n'est simplement écrite nulle part.

Ordre de grandeur, pour que la discussion ait des chiffres : une partie de trois
minutes à 120 Hz fait 21 600 tics. En n'écrivant que les CHANGEMENTS d'état des
cinq boutons, une partie de Flappy tient dans quelques centaines d'octets ; le
Snake, qui tourne en tenant une direction, dans quelques milliers. C'est du même
ordre que le journal d'événements actuel, dont la borne est déjà de
200 000 entrées (`NS_RUNLOG_MAX_EVENTS`, alignée sur celle du serveur).

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

## Ce que je propose comme prochaine étape, quand tu voudras

Dans cet ordre, parce que chaque étape rend la suivante moins risquée :

1. **Écrire les entrées dans le journal** — le format et le transport, la
   partie qui reste. Le test qui prouve que ça marcherait est fait
   (`tests/test_replay.c`) ; ce qui manque est de garder la trace d'une VRAIE
   partie plutôt que d'une suite fabriquée, et de choisir comment la compresser.
2. **Le duel en différé** (forme 1). C'est jouable, c'est sans latence, et ça
   répond à « affronter ses amis ».
3. Mesurer le déterminisme **entre plateformes** avant d'envisager la forme 2.

Rien de tout cela n'est commencé, et rien ne le sera sans que tu l'aies dit.
