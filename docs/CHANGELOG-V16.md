# Nineteen 16.0.0 — la salle retrouve son plan, et le joueur un corps

Cette version répond à neuf reproches nommés par le propriétaire, plus quatre
ajoutés en cours de route. Chaque correction porte sa mesure ; ce qui n'est pas
tenu est dit à la fin plutôt que passé sous silence.

## Ce qui change en une phrase

La salle reprend le **plan de 2020** — 188 m² presque carrés au lieu de 271
allongés — le joueur peut se voir **à la troisième personne**, et ses cotes se
règlent dans un **fichier texte** sans recompiler.

## Les neuf reproches, et ce qui a été mesuré

| Reproche | Ce qui n'allait pas, mesuré | État |
|---|---|---|
| « des objets 3D situés n'importe comment » | 4 objets étaient **dans la rue** — le comptoir d'accueil à 1,04 m dehors, la poubelle à 0,80, l'applique à 1,38 | corrigé, et **4 contrôles au build** l'empêchent de revenir |
| « la hauteur ne permet pas d'être bien positionné » | le centre de la dalle était à **31,8° sous l'horizon** pour un demi-champ de 31,0° : l'écran était **hors du cadre** | le regard se pose en arrivant devant une borne, et à la fin d'une partie |
| « l'écran n'est pas pareil quand on joue et ne joue pas » | le traitement de tube était réservé à **1 dalle sur 19** | les dix-neuf ont le même tube |
| « quand on ne joue pas il est moche et ressort » | albédo non rabattu : la dalle était éclairée **deux fois** | corrigé par le même changement |
| « le layout des boutons n'a pas de sens » | le bouton le plus à droite sortait à x = +0,3621 pour une tôle qui s'arrête à ±0,3505 : **vissé dans le vide** | un poste, layout Midway, deux boutons |
| « les objets sont mal positionnés » | 23 défauts, dont 5 bloquants | 11 corrigés, les autres listés |
| « le jeu ne respire aucune âme » | **63,4 m²** du hall à plus de 2 m de tout meuble | **15,8 m²** |
| « des bras horribles » | quatre tubes droits en éventail ; la main traversait la boule | main fléchie, doigt à **2 mm** du manche |
| « il faut suivre un plan » | — | `docs/PLAN-V16.md` |

## Les quatre ajoutés

* **l'écran incrusté** — la dalle flottait **76,2 mm** devant sa face, et il n'y
  avait rien à incruster *dans* : le meuble est maintenant creusé de 35 mm ;
* **NINETEEN en néon** — un tube de verre sur panneau noir, plus une image peinte ;
* **le tableau du bar** — il affiche le classement et les joueurs en direct ; il
  était noir parce que sa texture réfléchit 0,0158 et que l'émissif est modulé
  par l'albédo ;
* **la lampe accrochée** — elle pendait dans le vide sur **1,12 m**.

## Le plan de 2020

* hall **14,43 × 14,27 m** au lieu de 19,30 × 14,40 ; **deux** pans coupés ;
* **treize piliers**, dont douze **engagés dans les murs** — il y en avait deux ;
* l'îlot aux entraxes mesurés : 0,942 m en rangée, 1,201 m dos à dos ;
* la **règle de difficulté redevient spatiale** : les six HARD à x < 0 ;
* les **toilettes passent à l'est** (35,8 m² au lieu de 12,4) ;
* le **sas devient un couloir de neuf mètres**, et le joueur y démarre à la
  porte, tourné vers la salle.

**Dix-neuf bornes et non quinze**, seul écart assumé : le jeu s'appelle Nineteen.

## Le personnage et les réglages

`nineteen.env`, **19 clés, toutes lues** — vérifié au démarrage, et une clé que
personne ne consomme est nommée dans le journal. Taille, allures, portée du
bras, champ de vision, recul de la caméra.

Le personnage est **CesiumMan, CC-BY 4.0 Cesium** : l'attribution est
obligatoire et figure dans `assets/cc0/LICENSES.md`.

## La recette

| | |
|---|---|
| les huit jeux, depuis le paquet | 14 / 170 / 14 900 / 2 100 / 1 050 / 105 / 1 170 / 791 |
| les bras | 3 493 vérifications, 0 échec |
| le duel contre le vrai relais | 6 duels, **0 divergence**, jusqu'à 1 098 attentes |
| la suite de tests | **36/36** |
| le compilateur | **0 avertissement** |
| le paquet | 242 Mio, tourne déballé |

## Ce qui n'est pas tenu, et que je ne masque pas

* **Le paquet n'est pas signé.** Ni Developer ID ni Authenticode : les
  certificats appartiennent au propriétaire. macOS et Windows afficheront un
  avertissement au premier lancement.
* **Trois critères d'éclairage** du document d'ambiance ne sont pas atteints :
  `sud` à 14,4 et `travee` à 16,3 pour ≥ 30 demandés. Ce sont deux cadrages qui
  regardent à l'opposé des lampes ajoutées.
* **Douze défauts de placement mineurs** restent ouverts, listés dans
  `docs/AUDIT-PLACEMENT.md`.
* **Cinq des neuf contrôles** recommandés par l'audit ne sont pas écrits.
* **Le déterminisme** est mesuré sur deux architectures d'un seul système, pas
  sur trois systèmes.
* **La découverte d'adversaire** n'existe pas : l'identifiant de duel se convient
  hors du jeu, et le relais ne parle pas TLS.
