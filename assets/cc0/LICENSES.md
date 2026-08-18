# Textures rapportées — origine et licence

Ces images ne viennent pas du modèle de 2020. Elles ont été rapportées **là, et
seulement là, où le modèle d'origine n'avait pas de texture** : sept de ses
matériaux étaient des aplats de couleur de 24 × 24 pixels, et `bordeau_uni.jpg`
faisait 1 × 1. Un aplat n'est pas une texture — il ne donne ni relief, ni
variation, ni prise à la lumière.

Les 58 images de 2020 restent intactes dans `legacy/room/textures/` et
continuent d'habiller la salle `--room=legacy`. Les images retirées de la salle
reconstruite sont déclarées une par une, avec leur motif, dans le bloc
`retiredTextures` de `assets/scene/salle.room.json` : `roomgen` refuse de bâtir
si une image cesse d'être employée sans être déclarée.

Seul l'**albédo** est versionné. La normale et la carte ORM (occlusion,
rugosité, métallicité) sont dérivées au build par `tools/texgen`, comme pour les
textures d'origine — ce qui divise par trois le poids ajouté au dépôt, et évite
d'y stocker des données que la chaîne sait recalculer.

## Fichiers

Tous sont sous **CC0 1.0** (domaine public, aucune attribution requise —
elle est donnée ici parce que c'est correct, pas parce que c'est exigé).

| Fichier | Source | Auteur | Licence | Emploi |
|---|---|---|---|---|
| `painted_metal_shutter.jpg` | [Poly Haven](https://polyhaven.com/a/painted_metal_shutter) | Rob Tuytel, Sergej Majboroda | CC0 1.0 | Caissons de bornes (teintés par jeu), rails du plafond, distributeur, jukebox, mobilier laqué |
| `brushed_concrete.jpg` | [Poly Haven](https://polyhaven.com/a/brushed_concrete) | Rob Tuytel | CC0 1.0 | Béton de structure |
| `metal_plate_02.jpg` | [Poly Haven](https://polyhaven.com/a/metal_plate_02) | Rob Tuytel | CC0 1.0 | Plénum, au-dessus des dalles du faux plafond |
| `black_oak_veneer.jpg` | [Poly Haven](https://polyhaven.com/a/black_oak_veneer) | Poly Haven | CC0 1.0 | Placage bois de la radio murale |

Résolution rapportée : **1024 × 1024**, l'albédo seul, en JPEG. Le 2K et le 4K
existent en amont ; ils ne servent à rien sur un caisson de 72 cm vu à un mètre,
et ils auraient multiplié par quatre le poids pour un texel qu'aucun écran ne
distingue.

## Pourquoi versionnés plutôt que téléchargés au build

Parce que `git clone && cmake --build` doit marcher **hors ligne** — c'est la
promesse tenue depuis A2b, et un `FetchContent` d'assets la casserait au premier
build. 2,6 Mio ajoutés au dépôt sont le prix de cette promesse, et c'est un prix
raisonnable.
