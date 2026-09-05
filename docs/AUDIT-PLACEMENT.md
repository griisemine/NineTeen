# Audit de placement — la salle de Nineteen

Audit mené le 29 août 2026 sur la branche `claude/game-engine-modernization-j3t6hw`,
arbre de travail propre au commit `ef8c840`. Il répond à deux phrases du propriétaire,
qui disent la même chose : « des objets 3D sont situés n'importe comment dans l'espace
3D » et « les objets sont mal positionnés ». Elles sont exactes. Ce document les
remplace par vingt-trois défauts nommés et chiffrés.

**Ce document ne corrige rien.** Il mesure, il montre, et il dit quel contrôle de
`tools/roomgen.c` aurait dû l'attraper.

---

## Comment c'est mesuré

Deux sources, aucune supposition.

**1. La géométrie réellement générée.** `tools/roomgen.c` cuit *tout* — y compris les
props importés depuis un glTF — dans un seul fichier en coordonnées du monde. Il a été
relancé dans un répertoire de travail pour vérifier qu'il reproduit bien le fichier
livré :

```sh
cd build/macos-universal/assets && ../tools/roomgen \
    ../../../assets/scene/salle.room.json /tmp/audit/salle.gltf \
    --textures=../../../legacy/room/textures --textures=../../../assets/cc0 \
    --textures=./sideart --expect-textures=83 --assets=../../../assets
```

Les 94 objets de `salle.gltf` partagent un unique tampon de positions ; chaque objet est
un maillage nommé. **La conséquence importante : les props à clé `model` sont mesurables
comme les autres.** Le garde-fou annoncé — « je ne peux pas mesurer un glTF » — ne
s'applique pas ici, parce que roomgen a déjà fait le travail. Rien dans ce rapport n'est
deviné pour cause de modèle importé.

**2. Le plan déclaré.** L'enveloppe de la salle est le polygone fermé `walls[0]`
(« coquille »), lu en ligne médiane, rentré de `wallThickness / 2` = 0,10 m pour obtenir
le **parement intérieur** :

| ligne médiane | parement intérieur |
|---|---|
| (−9,65 ; −7,20) → (−9,65 ; 7,20) → (6,173 ; 7,20) → (9,65 ; 3,644) → (9,65 ; −7,20) | (−9,55 ; −7,10) → (−9,55 ; 7,10) → (6,131 ; 7,10) → (9,55 ; 3,603) → (9,55 ; −7,10) |

Toutes les distances « au-delà du parement » de ce rapport sont mesurées sur ce polygone,
sommet par sommet, sur les 379 944 sommets de la salle. Le parement **extérieur** est
20 cm plus loin : un objet à plus de 20 cm du parement intérieur est **dehors, dans la
rue.**

**3. La pénétration réelle.** Le recouvrement de boîtes englobantes ne prouve rien sur
deux objets tournés. Les paires suspectes ont donc été retestées au triangle, par la même
parité de rayons que `light_is_enclosed` (`tools/roomgen.c:381`) : un sommet de A est
compté dedans si au moins 4 rayons sur 7 traversent B un nombre impair de fois. Le volume
d'intersection du canapé et du pilier est calculé par colonnes verticales de 2 cm sur
l'emprise exacte du pilier.

**4. L'œil.** Vingt-quatre cadrages regardés, dont **seize à hauteur d'œil de joueur**
(1,70 m), rendus
en `--quality=high` (lancer de rayons) en 3200 × 1800. Le préambule commun :

```sh
./build/macos-universal/bin/nineteen --headless --quality=high --no-hud \
    --width=3200 --height=1800 --frames=8 --screenshot=CHEMIN.png
```

**Convention de lacet, à ne pas confondre.** Un prop regarde `(sin yaw, cos yaw)` —
`parse_props`, `tools/roomgen.c:1861`. La caméra, elle, regarde `(cos yaw, sin yaw)` :
vérifié sur les quatre vues nommées `billard`, `allee`, `bar` et `entree`, dont le sujet
n'est au centre du cadre qu'avec cette lecture. Les `--yaw=` cités plus bas suivent la
convention **caméra**.

---

## Récapitulatif

| № | Objet | Nature du défaut | Mesure | Gravité |
|---|---|---|---|---|
| P-01 | `bureau` | entièrement **hors du bâtiment**, dans la rue devant la porte | 1,24 m au-delà du parement intérieur, soit **1,04 m dehors** | bloquant |
| P-02 | `poubelle_sas` | hors du bâtiment, **dans** le bureau, et flottante | 0,80 m dehors ; 1520/2820 sommets dans le solide du bureau ; **+10,9 cm** au-dessus du sol | bloquant |
| P-03 | `applique_sas`, `ampoule_sas`, lumières `applique_sas` et `neon_mur_est` #3 | luminaire et deux sources **sur la face extérieure** du mur de brique | applique 1,38 m dehors ; ampoule 1,25 m dehors ; sources à 1,11 m et 1,21 m au-delà du parement intérieur | bloquant |
| P-04 | `lavabo_toilettes` | axes X et Z **intervertis** : le plan de 1,40 m part perpendiculairement au mur et le traverse | **35,0 cm** au-delà du parement intérieur, soit **15 cm dehors** ; recouvrement avec la coquille 1,25 × 1,91 × 0,52 m | bloquant |
| P-05 | `canape` | **empalé** sur `pilier_est_1` | intersection réelle **0,0412 m³** (265 colonnes sur 576) ; 352 des 357 sommets du canapé présents dans la boîte du pilier sont dans son solide | bloquant |
| P-06 | `porte_cabine_1`, `porte_cabine_2` | portes de cabine suspendues **à un mètre du sol**, dépassant les joues | bas à **1,02 m**, haut à **2,78 m** contre 2,013 m pour les joues, soit **+76,7 cm** | visible |
| P-07 | `radio_murale` | axes X et Z intervertis **et** face tournée de 90° : elle regarde le sud, pas la salle | **13,0 cm** enterrés dans le mur, **27 cm** en saillie ; normale du panneau = −Z au lieu de +X | visible |
| P-08 | `panneau_restrooms` | panneau d'épaisseur nulle posé **dans** la cloison : invisible | **2,0 cm** derrière le parement | visible |
| P-09 | `sol_toilettes` | le carrelage ne va ni jusqu'à la cloison est ni jusqu'à la cloison nord : de la **moquette d'arcade** dans le bloc sanitaire | **3,15 m² sur 11,05**, soit **28,5 %** de la pièce | visible |
| P-10 | `suspension_comptoir`, `suspension_salon` | suspensions **accrochées à rien** : les chaînes s'arrêtent en l'air | **1,198 m** et **1,078 m** de vide jusqu'au sous-face du plafond (3,058 m) | visible |
| P-11 | `ampoule_comptoir`, `ampoule_salon` | l'ampoule est **au-dessus** de son abat-jour | base de l'ampoule à 1,680 m pour un abat-jour finissant vers 1,60 m : **+8 cm** (idem au salon) | visible |
| P-12 | `porte_entree_battant` | le battant s'enfonce sous le sol et ne monte pas au linteau | **−4,7 cm** sous le sol ; sommet à 2,213 m pour un linteau à 2,30 m, soit **8,7 cm de jour** | visible |
| P-13 | `panneau_sol_mouille` | le panneau « sol mouillé » est **planté dans** le meuble du lavabo | 34/38 sommets dans le solide ; enveloppe enterrée **13,7 × 9,3 × 21,1 cm** | visible |
| P-14 | `poutre_5` | la cinquième poutre **traverse le pan coupé** et ressort dehors | **1,05 m** au-delà du parement intérieur, soit **0,85 m dehors** | visible |
| P-15 | `panneau_sortie` | panneau SORTIE **au-dessus du linteau**, donc dans le mur plein, et posé sur le plan médian | 14 cm de ses 20 cm au-dessus du linteau (**70 %**), le reste à 9,6 cm de profondeur dans le tableau | visible |
| P-16 | `boombox` | **enfoncé** dans le plateau du comptoir | plateau à 1,048 m, base du boombox à 1,025 m : **−2,3 cm** | mineur |
| P-17 | `cabine_toilettes_2` | la joue droite de la deuxième cabine entre dans la cloison | **9,8 cm** | mineur |
| P-18 | `table_basse` | pénètre `pilier_est_2` | 58/64 sommets dedans ; enveloppe **8,2 × 24,8 × 2,0 cm** | mineur |
| P-19 | `distributeur` | dos enfoncé dans le mur ouest | **4,0 cm** | mineur |
| P-20 | `cible_flechettes` | cible **décollée** du mur | **3,0 cm** de jour derrière | mineur |
| P-21 | `nez_estrade` | le nez de marche **dépasse** du plateau qu'il borde | plateau à 0,120 m, nez à 0,150 m : **+3,0 cm** | mineur |
| P-22 | `billard_billes` | les trois billes flottent au-dessus du drap | drap à 0,785 m, base des billes à 0,791 m : **+0,6 cm** | mineur |
| P-23 | `tapis_technique` | le tapis technique traverse le mur ouest | **35,0 cm** au-delà du parement intérieur | mineur |

**Échelle employée.** *Bloquant* : l'objet est à un endroit où il ne peut pas être ce
qu'il est — hors du bâtiment, ou dans un autre solide. *Visible* : le défaut se voit
depuis un endroit où le joueur va, à hauteur d'œil. *Mineur* : mesuré, à corriger, mais
pas attrapé par l'œil dans le cours normal du jeu.

---

## Bloquant

### P-01 — Le bureau d'accueil est dans la rue

**Ce qu'on voit.** Depuis l'extérieur, le comptoir d'accueil est posé sur le trottoir,
à cheval devant la porte d'entrée, avec la poubelle dedans.

```sh
--camera=free --pos=10.6,1.90,8.6 --yaw=222 --pitch=-6
--camera=free --pos=9.70,1.10,7.60 --yaw=228 --pitch=-9
```

Et depuis l'intérieur, `--view=entree` : **le sas est vide**. Un sas de 2,8 m de profond,
mur de brique nu, un pilier de béton dedans, et rien d'autre.

**La preuve.** `bureau` est déclaré `"at": [8.2, 0.0, 5.7]`. Le pan coupé passe par
(6,131 ; 7,10) → (9,55 ; 3,603) : à x = 8,20 le parement intérieur est à z = 5,08. Le
bureau commence à z = 5,02 et va jusqu'à z = 6,38. Son sommet le plus lointain, mesuré,
est à **1,240 m au-delà du parement intérieur**, donc 1,04 m au-delà du parement
extérieur. Il n'est pas « mal centré » dans le sas : il n'y est pas.

**Correction.** Contre le mur sud du sas (z = 4,20, parement à 4,30), face à la porte :

```json
"at": [7.10, 0.0, 4.75],  "yaw": 0.0
```

Le plateau fait 1,72 × 0,82 : il tiendra alors de x = 6,24 à 7,96 et de z = 4,34 à 5,16,
entièrement dans le quadrilatère du sas et sans bloquer le passage vers le hall, qui est
percé dans le mur x = 4,60 entre z = 4,60 et 6,00.

---

### P-02 — La poubelle est dehors, dans le bureau, et flotte

Trois défauts sur un seul objet, et ils ne se voient pas au même endroit.

**Dehors** : 1,001 m au-delà du parement intérieur, donc 0,80 m dans la rue.

**Dans le bureau** : test au triangle, **1520 des 2820 sommets** de la poubelle sont à
l'intérieur du solide du bureau ; l'enveloppe enterrée mesure 43,8 × 56,2 × 26,1 cm. Sur
la capture `--pos=8.10,0.45,7.20 --yaw=250 --pitch=-3` on voit la façade du comptoir
passer au travers du fût.

**Flotte** : le point le plus bas du modèle `metal_trash_can` est à **y = 0,1087** pour
un `"at"` à 0,0 et un sol à 0,0. Dix centimètres et neuf millimètres d'air sous une
poubelle métallique.

**Correction.**

```json
"at": [5.30, 0.0, 4.70],  "modelOffset": [0.0, -0.109, 0.0]
```

`modelOffset` est la clé prévue pour ça (`tools/roomgen.c:1700`) : le calage vertical d'un
modèle importé n'appartient pas à `at`.

---

### P-03 — Le luminaire du sas est vissé sur la façade extérieure

**Ce qu'on voit.** Depuis la rue, deux appliques en cage sont accrochées au mur de brique,
au-dessus de la porte, côté extérieur (`--pos=10.6,1.90,8.6 --yaw=222 --pitch=-6`).
Depuis le sas, il n'y a aucun luminaire et la pièce est au noir.

**La preuve.** Trois objets et deux sources partagent la même faute :

| déclaration | position | écart au parement intérieur |
|---|---|---|
| prop `applique_sas` | (8,90 ; 2,15 ; 6,42) | **1,580 m** → 1,38 m dehors |
| prop `ampoule_sas` | (8,90 ; 2,12 ; 6,30) | **1,454 m** → 1,25 m dehors |
| light `applique_sas` | (8,90 ; 2,06 ; 5,85) | **1,11 m** → 0,91 m dehors |
| light `neon_mur_est` #3 | (8,90 ; 2,20 ; 6,00) | **1,21 m** → 1,01 m dehors |

Le commentaire de `applique_sas` dans le JSON raconte l'origine exacte du défaut : « le
contrôle de roomgen a refusé le premier emplacement parce que le point de vue nommé
« entree » tombait dedans — déplacée de 6,72 à 8,90 ». Le luminaire a donc été poussé le
long de **x** pour sortir de la capsule du joueur, et il est sorti du bâtiment par le pan
coupé. Un contrôle a été satisfait en violant celui qui n'existait pas.

`neon_mur_est` est déclaré `"at": [8.9, 2.2, -2.0]` avec `repeat: {count: 3, step: [0,0,4]}`.
Le mur est ne va que jusqu'à z = 3,603 : le troisième néon d'un mur de trois n'a pas de
mur.

**Correction.** Mur ouest du sas (parement x = 4,70), au-dessus du niveau du passage :

```json
"applique_sas": { "at": [4.70, 2.15, 6.40], "yaw": 90.0 }
"ampoule_sas":  { "at": [4.82, 2.12, 6.40] }
lights "applique_sas": { "at": [5.27, 2.06, 6.40] }
lights "neon_mur_est": { "at": [8.90, 2.20, -4.00], "repeat": { "count": 3, "step": [0.0, 0.0, 3.6] } }
```

L'emplacement proposé est à 1,71 m du point de vue `entree` (6,30 ; 1,70 ; 6,00), donc
hors de la capsule qui avait fait refuser le premier essai.

---

### P-04 — Le lavabo traverse le mur ouest et son miroir est planté dans le plan

**Ce qu'on voit.** `--pos=-7.40,1.70,-5.40 --yaw=180 --pitch=-38` et
`--pos=-7.60,1.70,-4.90 --yaw=163 --pitch=-6` : le meuble part perpendiculairement au mur
carrelé, comme un îlot de cuisine, et le miroir est une **plaque verticale plantée dans le
plan**, vue par la tranche, qui coupe le meuble en deux dans sa longueur.

**La preuve, et la cause exacte.** La convention de la salle est : un prop regarde son
**+Z local**, donc X est la largeur et Z la profondeur. `distributeur` la respecte
(`size: [0.86, 1.92, 0.78]`, panneaux de façade en `z = +0,356`) et se pose correctement
contre le mur ouest. `lavabo_toilettes` déclare l'inverse :

```json
"size": [0.52, 0.86, 1.4]
```

soit 0,52 de large et 1,40 de profond. Avec `yaw: 90`, le Z local devient le X du monde :
les 1,40 m de plan partent du mur vers la salle. Mesuré, le meuble occupe
x = −9,900 à −8,500 : **35,0 cm au-delà du parement intérieur (−9,55), donc 15 cm dehors**.
Recouvrement avec la coquille : 1,25 × 1,91 × 0,52 m.

Le miroir suit : il est déclaré `at: [0.226, 1.46, 0]` avec `yaw: 90`, ce qui compose avec
le lacet du prop pour donner une normale de **−Z dans le monde**. Le miroir regarde le sud.

C'est la mesure du R-26 de `docs/REVUE-STRICTE.md` (« miroir flottant, lavabo sans cuve »).
La piste qui y était donnée — « un plan coplanaire au mur » — est la bonne ; la cause est
plus simple qu'un problème de coplanarité : les deux cotes horizontales sont interverties.

**Correction.**

```json
"at": [-9.29, 0.0, -4.4],  "yaw": 90.0,
parts[0] "size": [1.4, 0.86, 0.52],
parts[1] "size": [1.2, 0.44],                       // la vasque, dans le même sens
parts[2] "at": [0.0, 1.46, -0.259], sans "yaw"      // le miroir sur le mur, face à la salle
```

`at.x = −9,29` place le dos du meuble exactement sur le parement (−9,29 − 0,26 = −9,55).
**Cette correction en règle une deuxième** : P-13 (le panneau « sol mouillé » planté dans
le meuble) disparaît, la façade du meuble reculant de −8,50 à −9,03 alors que le panneau
occupe x = −8,691 à −8,234.

Le robinet suit le meuble : `robinet_toilettes` → `"at": [-9.42, 0.0, -4.40]`.

---

### P-05 — Le canapé est empalé sur un pilier

**Ce qu'on voit.** `--pos=6.60,1.35,-2.60 --yaw=-113 --pitch=-9` : le pilier de béton
rouge sort du dossier du canapé. Le dossier n'est pas contre le pilier, il est dedans.

**La preuve.** `pilier_est_1` occupe exactement x ∈ [5,160 ; 5,640], z ∈ [−5,040 ; −4,560],
y ∈ [0 ; 2,920] — une boîte alignée sur les axes, donc mesurable sans ambiguïté.

- Test de parité au triangle : **352 des 357 sommets** du canapé qui tombent dans la boîte
  du pilier sont à l'intérieur de son solide. Enveloppe enterrée : 46,8 × 50,2 × 31,8 cm,
  c'est-à-dire **davantage que la section du pilier**.
- Volume d'intersection réel, par colonnes verticales de 2 cm sur l'emprise du pilier :
  **0,0412 m³**, 265 colonnes touchées sur 576.

`check_solid_overlaps` **le signale déjà** au build :

```
attention : « pilier_est_1 » et « canape » se recouvrent de 0.48 x 1.12 x 0.48 m
            (structure ou mobilier : toléré)
```

Il ne l'arrête pas, parce que la règle actuelle n'est fatale que si une **borne** est en
cause (`tools/roomgen.c:506`). Le raisonnement écrit à côté est juste — « une poutre repose
sur ses piliers, un tabouret glisse sous un comptoir » — mais il tolère du même geste un
canapé dans un poteau. Voir le contrôle C-04 plus bas.

**Correction.** Décaler le canapé de 1,00 m vers l'est et 0,55 m vers le sud :

```json
"at": [7.10, 0.0, -5.15]
```

Le canapé occupe alors x ∈ [5,718 ; 8,442] : il dégage le pilier de **7,8 cm** et la
plante en pot (x ≥ 8,562) de 12 cm. Vérifié aussi : le recouvrement de boîtes avec
`fauteuil_salon` tombe de 87 à 32 cm en z, et il n'y avait déjà **aucune** pénétration
réelle entre eux (voir « Ce qui a été vérifié et qui est juste »).

---

## Visible

### P-06 — Les portes de cabine sont suspendues à un mètre du sol

**Ce qu'on voit.** `--pos=-8.90,1.05,-4.55 --yaw=-56 --pitch=-16` : la porte occupe tout
le haut du cadre et son **arête basse s'arrête en l'air**. On voit le sol carrelé, la
cuvette et les joues de la cabine sous elle.

**La preuve.** Une boîte de prop est centrée en X et Z mais **posée en Y** : `at.y` est sa
base (vérifié sur `estrade`, `at.y = 0`, `size.y = 0,12`, mesuré 0,000 → 0,120). Les deux
portes déclarent :

```json
{ "at": [0.44, 1.06, 0.0], "size": [0.88, 1.72, 0.035] }
```

soit un vantail de 1,06 m à 2,78 m. Mesuré sur le maillage : 1,020 → 2,780, la poignée
descendant à 1,020. Les joues de cabine (`cabine_toilettes`) vont de 0,113 à 2,013 : la
porte les **dépasse de 76,7 cm par le haut** et **s'arrête 1,02 m au-dessus du sol par le
bas**. Elle entre au passage dans `poutre_1` (24 sommets, 2,3 × 0,6 × 2,8 cm).

**Correction**, sur les deux portes :

```json
parts[0] "at": [0.44, 0.15, 0.0]     // vantail 0,15 → 1,87 m
parts[1] "at": [0.80, 0.95, 0.026]   // la poignée suit
```

Le jeu de 15 cm sous la porte est la cote de cabine sanitaire courante, et le vantail
reste sous les joues, donc sous la poutre.

---

### P-07 — La radio murale est un bloc gris tourné vers le sud

**Ce qu'on voit.** `--pos=-8.10,1.60,3.60 --yaw=180 --pitch=0` : sur le mur ouest, un
**parallélépipède gris uni**, vu par la tranche, sans cadran, sans grille, sans façade.
La texture `Radio_c.png` est bien chargée — elle n'est simplement pas tournée vers la
salle.

**La preuve.** Même interversion qu'au lavabo, plus une erreur de face :

```json
"size": [0.1, 0.24, 0.4]          // 10 cm de large, 40 cm de profond
parts[1] { "at": [0.048, 0.113, 0.0], "size": [0.36, 0.22], "yaw": 90 }
```

Avec `yaw: 90` sur le prop, les 0,40 m de Z local deviennent le X du monde : mesuré,
x = −9,680 à −9,280, donc **13,0 cm dans le mur et 27 cm en saillie**. Et le panneau de
façade, décalé en X local et tourné de 90°, finit avec une normale de **−Z** : il regarde
le long du mur au lieu de regarder la salle.

**Correction.**

```json
"at": [-9.50, 1.53, 3.6],  "yaw": 90.0,
parts[0] "size": [0.4, 0.24, 0.1],
parts[1] { "at": [0.0, 0.113, 0.052], "size": [0.36, 0.22] }   // sans "yaw"
```

Sans `yaw` sur le panneau, sa normale est le +Z local, que le lacet du prop envoie sur +X,
c'est-à-dire dans la salle. `at.x = −9,50` colle le dos au parement (−9,50 − 0,05 = −9,55).

---

### P-08 — Le panneau « restrooms » est enterré dans la cloison

**Ce qu'on voit.** `--pos=-7.85,1.70,-2.30 --yaw=-90 --pitch=0` : la caméra est placée
pile en face du panneau, à 1,20 m, à sa hauteur. **Le cadre est un mur nu.**

**La preuve.** Le panneau est un `panel` d'épaisseur nulle à `"at": [-7.85, 1.72, -3.52]`.
La cloison des toilettes court en z = −3,60 sur 0,20 m d'épaisseur, soit de −3,70 à −3,50.
Le panneau est donc à **2,0 cm derrière le parement** côté hall. Un plan d'épaisseur nulle
derrière un mur opaque ne rend rien.

**Correction.**

```json
"at": [-7.85, 1.72, -3.48]
```

---

### P-09 — Trois mètres carrés de moquette d'arcade dans le bloc sanitaire

**Ce qu'on voit.** `--pos=-9.20,1.70,-5.30 --yaw=-8 --pitch=-8` et
`--pos=-8.90,1.05,-4.55 --yaw=-56 --pitch=-16` : au pied de la cloison carrelée, à
l'intérieur des toilettes, une bande de **moquette noire à motifs néon** — le tapis du
hall, cerise comprise.

**La preuve, et une correction du diagnostic de `REVUE-STRICTE.md` R-15.** Ce constat y
figure déjà, avec l'explication « ils se recouvrent au lieu de se joindre ». **Mesuré,
c'est faux : les trois sols sont disjoints, exactement comme le fichier l'annonce.**

| sol | matériau | emprise en x | emprise en z |
|---|---|---|---|
| `sol_toilettes` | `sol_toilettes` (carrelage) | −11,10 → **−7,00** | −8,10 → **−4,00** |
| `sol_couloir` | `sol` (moquette) | −11,10 → −7,00 | −4,00 → 8,10 |
| `sol_hall` | `sol` (moquette) | **−7,00** → 11,10 | −8,10 → 8,10 |

Il n'y a ni recouvrement ni coplanarité, et une grille de 10 cm sur les 26 415 points
intérieurs de la salle le confirme : **zéro trou, zéro double couverture**.

Le vrai défaut est ailleurs : **le bloc sanitaire va jusqu'à x = −6,30 et jusqu'à
z = −3,70** (parements intérieurs de `cloison_toilettes`, lignes médianes x = −6,20 et
z = −3,60). Le carrelage s'arrête avant les deux, et par **deux** bandes, pas une :

| bande | provenance | emprise | aire |
|---|---|---|---:|
| est | `sol_hall` | x −7,00 → −6,30 sur z −7,10 → −3,70 | **2,38 m²** |
| nord | `sol_couloir` | z −4,00 → −3,70 sur x −9,55 → −7,00 | **0,77 m²** |

Soit **3,15 m² de moquette d'arcade sur les 11,05 m² de la pièce — 28,5 %**. Mesuré par
échantillonnage à 10 cm : 71,8 % de carrelage, 28,2 % de moquette.

Ce n'est pas cosmétique : `footstep` est déclaré **par matériau**, donc plus d'un quart
du bloc sanitaire sonne aussi comme de la moquette.

La piste proposée par R-15 — ajouter une plinthe — ne masquerait rien : la bande est est
au milieu de la pièce, et la bande nord traverse la baie.

**Correction**, en gardant la disjonction des trois rectangles :

```json
"sol_hall":      { "centre": [2.40, 0.0],    "size": [17.40, 16.20] }  // x −6,30 → 11,10
"sol_couloir":   { "centre": [-8.70, 2.20],  "size": [4.80, 11.80] }   // x −11,10 → −6,30, z −3,70 → 8,10
"sol_toilettes": { "centre": [-8.70, -5.90], "size": [4.80, 4.40] }    // x −11,10 → −6,30, z −8,10 → −3,70
```

Vérifié : les trois restent disjoints (`sol_hall` à x ≥ −6,30, les deux autres à
x ≤ −6,30 ; `sol_toilettes` à z ≤ −3,70, `sol_couloir` à z ≥ −3,70) et leur union couvre
exactement la même emprise qu'aujourd'hui. Les deux nouvelles jonctions tombent entre
rectangles du **même** matériau `sol` : elles sont invisibles.

---

### P-10 — Les deux suspensions sont accrochées à rien

**Ce qu'on voit.** `--pos=-4.35,1.70,4.60 --yaw=90 --pitch=6` (comptoir) et
`--pos=6.60,1.35,-2.60 --yaw=-113 --pitch=-9` (salon) : le luminaire pend, ses deux
chaînes montent en V — et **s'arrêtent en l'air**, à plus d'un mètre du plafond.

**La preuve.**

| prop | sommet du luminaire | sous-face du plafond | vide |
|---|---:|---:|---:|
| `suspension_comptoir` | 1,860 m | 3,058 m | **1,198 m** |
| `suspension_salon` | 1,980 m | 3,058 m | **1,078 m** |

La sous-face du plafond est mesurée sur le maillage `plafond` : 792 sommets à 3,058 (les
rails en T) et 4528 à 3,100 (les dalles).

Le fichier sait pourtant faire : `suspension_billard` déclare explicitement son câble —
un `cylinder` de rayon 0,008 et de hauteur 1,0 posé à y = 1,90, qui monte donc à 2,90 et
rejoint la poutre. Les deux autres suspensions sont des `model`, et roomgen interdit à un
prop d'être à la fois un modèle et un empilement de morceaux (`tools/roomgen.c:1715`) :
le câble a disparu avec la conversion en modèle.

**Correction.** Deux props supplémentaires, sur le modèle de la manœuvre déjà employée
pour `ampoule_comptoir` :

```json
{ "name": "cable_comptoir", "at": [-4.35, 1.86, 6.20], "yaw": 0.0,
  "parts": [ { "type": "cylinder", "material": "bois_noir",
               "at": [0.0, 0.0, 0.0], "radius": 0.008, "height": 1.20, "sides": 8 } ] }
{ "name": "cable_salon", "at": [6.55, 1.98, -3.90], "yaw": 0.0,
  "parts": [ { "type": "cylinder", "material": "bois_noir",
               "at": [0.0, 0.0, 0.0], "radius": 0.008, "height": 1.08, "sides": 8 } ] }
```

---

### P-11 — L'ampoule est au-dessus de son abat-jour

**Ce qu'on voit.** Même capture que P-10, au comptoir : la boule lumineuse est **posée
au-dessus** de la barre horizontale du luminaire, entre les deux chaînes, au lieu d'être
dedans.

**La preuve.** Distribution des hauteurs des sommets, mesurée :

| objet | min | q10 | médiane | q90 | max |
|---|---:|---:|---:|---:|---:|
| `suspension_comptoir` (7542 sommets) | 1,545 | 1,563 | **1,585** | 1,783 | 1,860 |
| `ampoule_comptoir` (180 sommets) | **1,680** | 1,688 | 1,708 | 1,752 | 1,760 |

La masse du luminaire est entre 1,545 et 1,60 — c'est l'abat-jour ; ce qui monte au-dessus,
ce sont les deux chaînes. L'ampoule commence à 1,680, soit **8 cm au-dessus** de
l'abat-jour. `ampoule_salon` reproduit exactement le même écart (abat-jour 1,665 →
médiane 1,705, ampoule à 1,800).

Une sphère de prop est **posée sur son `at.y`** : le profil de `geo_revolve` est écrit
`sinf(a) * r0 + r0`, avec le commentaire « posée sur Y = 0 » (`tools/roomgen.c:1818`).

**Correction**, −14 cm sur les deux :

```json
"ampoule_comptoir": { "at": [-4.35, 1.54, 6.20] }
"ampoule_salon":    { "at": [6.55, 1.66, -3.90] }
```

Les lumières correspondantes (1,36 m et 1,48 m) restent **sous** l'abat-jour : elles ne
déclenchent donc pas `check_lights_not_enclosed`, conformément à ce que ce contrôle
exige — « poser la source SOUS ou DEVANT le luminaire ».

---

### P-12 — Le battant d'entrée s'enfonce sous le sol et laisse un jour au linteau

**Ce qu'on voit.** `--pos=6.30,1.70,4.80 --yaw=22 --pitch=10` : une bande sombre court en
haut du tableau, au-dessus du vantail et du panneau de ciel.

**La preuve.** Les deux `panel` du battant sont à `at.y = 1.083` pour une hauteur de 2,26 :
ils occupent donc **−0,047 → 2,213**. L'emprise mesurée du prop le confirme (min y
= −0,047, la valeur qui tire vers le bas l'emprise annoncée de toute la salle,
`−11,12 −0,05 −8,12`). L'ouverture `porte_entree`, elle, est déclarée `sill: 0.0`,
`head: 2.30`.

Bilan : **4,7 cm de battant sous le sol** et **8,7 cm de jour sous le linteau**.

**Correction**, sur les deux panneaux :

```json
"at": [±0.522, 1.15, 0.0],  "size": [1.15, 2.30]
```

---

### P-13 — Le panneau « sol mouillé » est planté dans le meuble du lavabo

**Ce qu'on voit.** `--pos=-7.40,1.70,-5.40 --yaw=180 --pitch=-38` : le triangle jaune
« CAUTION WET FLOOR » a son montant droit et son coin haut **dans le marbre**.

**La preuve.** Test au triangle : **34 des 38 sommets** du panneau qui tombent dans la
boîte du lavabo sont dans son solide. Enveloppe enterrée 13,7 × 9,3 × 21,1 cm.
Recouvrement de boîtes : 0,0281 m³.

**Correction.** Aucune action propre : **c'est P-04 qui règle celui-ci.** Une fois le
lavabo remis dans le bon sens (0,52 m de profondeur au lieu de 1,40), sa façade est à
x = −9,03 et le panneau, qui occupe x = −8,691 à −8,234, dégage de 34 cm.

---

### P-14 — La cinquième poutre sort du bâtiment par le pan coupé

**Ce qu'on voit.** Sur les deux vues extérieures (`--pos=13.5,3.2,11.0 --yaw=215 --pitch=-14`
et `--pos=10.6,1.90,8.6 --yaw=222 --pitch=-6`), un about de poutre claire sort du mur de
brique, à droite de la porte, au-dessus du linteau.

**La preuve.** `poutre` est déclarée `"at": [0.0, 2.7, -4.8]`, `"size": [19.3, 0.2, 0.42]`,
`repeat: {count: 5, step: [0,0,2.4]}` : les cinq exemplaires vont donc tous de x = −9,65 à
x = +9,65. C'est juste pour les quatre premiers, qui viennent porter dans les murs est et
ouest (10 cm d'about dans une paroi de 20, ce qui est le bon geste). La cinquième est à
z ∈ [4,59 ; 5,01], où le mur est **n'existe plus** : le pan coupé a déjà pris le relais.
Mesuré : **1,048 m au-delà du parement intérieur**, soit 0,85 m dehors.

**Correction.** Ramener `repeat.count` à 4 et déclarer la cinquième à part, arrêtée au
parement du pan coupé (x = 8,174 pour z = 5,01) :

```json
{ "name": "poutre", ... "repeat": { "count": 4, "step": [0.0, 0.0, 2.4] } }
{ "name": "poutre_5", "at": [-0.74, 2.7, 4.8], "size": [17.82, 0.2, 0.42],
  "material": "poutre", "chamfer": 0.01 }        // x −9,65 → 8,17
```

---

### P-15 — Le panneau SORTIE est aux sept dixièmes dans le mur plein

**Ce qu'on voit.** `--pos=6.30,1.70,4.80 --yaw=22 --pitch=10` : seul un liseré du panneau
dépasse dans le haut du tableau, et il est vu en biais depuis l'épaisseur du mur.

**La preuve.** `panneau_sortie` est un `panel` de 0,52 × 0,20 à
`"at": [7.91, 2.34, 5.42]`, donc de 2,240 à 2,440 en hauteur. L'ouverture s'arrête au
linteau, `head: 2.30` : **14 cm sur 20, soit 70 %**, sont au-dessus du percement, dans du
mur plein. Et en plan, (7,91 ; 5,42) est à 4 mm de la ligne médiane du pan coupé, donc à
**9,6 cm de profondeur** derrière le parement intérieur — le contraire du battant, qui est
visible parce qu'il occupe le percement.

**Correction.** Ramener le panneau sur le parement intérieur, 12 cm en avant de la ligne
médiane le long de la normale rentrante (−0,715 ; −0,699) :

```json
"at": [7.82, 2.42, 5.34],  "yaw": 225.0
```

Vérifié : (7,824 ; 5,340) est à +2 cm à l'intérieur du parement. La hauteur passe à 2,42
pour que le panneau s'inscrive entièrement au-dessus du linteau, contre le mur, comme un
vrai bloc de secours.

---

## Mineur

Groupés : ils se mesurent, ils se corrigent en une ligne, ils ne s'attrapent pas à l'œil
depuis un poste de jeu.

| № | Mesure | Correction |
|---|---|---|
| P-16 `boombox` | plateau du comptoir à **1,048** (partie `bois_noir`, `at.y 0.998` + `size.y 0.05`), base du boombox à **1,025** : enfoncé de 2,3 cm. Confirmé au triangle : 109 sommets dans le comptoir, enveloppe 62,2 × 1,2 × 21,4 cm | `"at": [-4.6, 1.048, 6.25]` |
| P-17 `cabine_toilettes_2` | joue droite à x = −6,202, parement de la cloison à −6,30 : **9,8 cm** dedans | `cabine_toilettes` → `"at": [-7.80, 0.0, -6.1]`, et les deux portes à x = −8,24 et −7,24 |
| P-18 `table_basse` | 58/64 sommets dans `pilier_est_2`, enveloppe **8,2 × 24,8 × 2,0 cm** | `"at": [6.30, 0.0, -3.30]` — dégage le pilier de 8 cm |
| P-19 `distributeur` | dos à x = −9,590, parement à −9,55 : **4,0 cm** dedans | `"at": [-9.16, 0.0, 5.6]` |
| P-20 `cible_flechettes` | dos à x = −9,520, parement à −9,55 : **3,0 cm** de jour | `"at": [-9.55, 1.68, -1.6]` |
| P-21 `nez_estrade` | plateau à 0,120, nez de 0,090 à 0,150 : **3,0 cm** de lèvre au-dessus du plateau | `"y": 0.06` (le nez couvre alors 0,06 → 0,12, arasé) |
| P-22 `billard_billes` | drap mesuré par colonne verticale sous chacune des trois billes : **0,785** ; base des billes **0,791** | les trois `at.y` → `0.785` |
| P-23 `tapis_technique` | 3,40 m de tapis centré en x = −8,20 : de −9,90 à −6,50, donc **35 cm** au-delà du parement | `"at": [-7.85, 0.0, 1.2]` — le tapis va alors de −9,55 à −6,15 |

---

## Ce qui a été vérifié et qui est juste

Un audit qui ne dit que ce qui cloche laisse croire que le reste n'a pas été regardé.

- **Aucune borne ne regarde un mur.** Les dix-neuf ont été testées en marchant le long de
  leur direction de regard `(sin yaw, cos yaw)` par pas de 1 cm sur 4 m, contre les murs,
  les cloisons et tout objet de plus de 30 cm de haut. Le plus petit dégagement est
  **1,64 m** (`borne_arcade_18` vers `table_basse`) ; les dix-huit autres n'ont rien à
  moins de 4 m. Le seuil de 80 cm demandé est tenu par toutes, avec deux fois la marge.
- **Les six bornes murales sont posées à l'identique** : dos à **1,5 cm** du parement,
  toutes les six.
- **Les jeux latéraux entre bornes voisines sont uniformes** : **7,8 cm** sur les treize
  paires adjacentes, sans exception.
- **Les dix-neuf bornes reposent à la même hauteur** : base mesurée à +0,006 m, qui est le
  décalage propre du modèle `borne.gltf`, et `borne_classement` à +0,126 sur une estrade
  dont le plateau est à 0,120 — même écart, donc pose correcte.
- **`canape` × `fauteuil_salon`** : les boîtes englobantes se recouvrent de 0,3202 m³,
  mais **aucun sommet** de l'un n'est dans le solide de l'autre (2759 candidats testés).
  Ce n'est pas un défaut.
- **`canape` × `table_basse`** : recouvrement de boîtes 0,0892 m³, **aucune pénétration
  réelle** (399 + 15 candidats testés). Ce n'est pas un défaut.
- **Les neuf points de vue nommés et le départ du joueur sont tous dans la salle** : le
  plus juste est `entree` à +0,65 m du parement, `playerStart` à +0,35 m.

---

## Ce qui manque

Des endroits où l'**absence** d'objet est elle-même la faute.

**1. Le sas d'entrée est vide, et c'est la première image du jeu.** `--view=entree`
montre un volume de brique nue de 2,8 m de profond, avec un pilier de béton dedans et
rien d'autre. Ce n'est pas un choix : les trois objets qui devaient le meubler — le
bureau, la poubelle, l'applique — sont dehors (P-01, P-02, P-03). Les remettre suffit.
Le `playerStart` est à (7,30 ; 0 ; 5,40) : c'est la pièce que le joueur regarde en
premier et la seule qu'il traverse deux fois par session.

**2. Sept sources de lumière n'ont aucun objet visible.** Mesuré : distance de chaque
source non-`ceilingPanel` au plus proche prop, en 3D.

| source | position | objet le plus proche | distance |
|---|---|---|---:|
| `plafonnier_classement` | (0,00 ; 2,74 ; −3,10) | `poutre_2` | 0,49 m |
| `applique_technique` | (−8,60 ; 2,25 ; 0,40) | `poutre_3` | 0,49 m |
| `neon_mur_ouest` #2 | (−8,90 ; 2,20 ; 0,00) | `poutre_3` | 0,50 m |
| `neon_mur_est` #2 | (8,90 ; 2,20 ; 2,00) | `poutre_4` | 0,53 m |
| `neon_mur_ouest` #3 | (−8,90 ; 2,20 ; 4,00) | `radio_murale` | 0,67 m |
| `applique_entree` #2 | (6,40 ; 2,05 ; 5,60) | `porte_entree_battant` | 0,73 m |
| `applique_entree` #1 | (6,40 ; 2,05 ; 3,20) | `poutre_4` | 0,88 m |

Deux d'entre elles s'appellent **`applique`**, une **`plafonnier`** : trois noms qui
promettent un luminaire qui n'existe pas. Le fichier s'interdit lui-même ce défaut, en
toutes lettres, dans le commentaire de `suspension_billard` : « Une source sans objet
visible est exactement ce que le plan s'interdit : c'est ce qui fait qu'une pièce paraît
éclairée par magie. » Il l'a corrigé une fois, pour le billard. Il reste sept cas.

Les quatre néons muraux sont peut-être voulus nus — un tube caché derrière une corniche
est un parti pris légitime. Mais il faut alors l'écrire, ce qui est exactement ce que
demande le contrôle C-07 ci-dessous.

**3. Le carrelage manque sur 3,15 m² du bloc sanitaire, soit 28,5 % de la pièce.** Voir P-09 : ce n'est pas un
objet de trop, c'est un objet trop petit.

---

## Le contrôle qui aurait dû l'attraper

C'est la partie utile. Chaque famille de défaut ci-dessus est une propriété qu'on peut
vérifier au build, et la culture du dépôt dit qu'un défaut trouvé deux fois devient un
contrôle. Trois de ces familles ont déjà été trouvées deux fois : le sol des toilettes
(R-15 puis P-09), le lavabo (R-26 puis P-04), l'affiche répétée (R-36 puis les trois
« ARCADE Armageddon » du mur ouest).

Les contrôles proposés suivent les deux modèles existants —
`check_lights_not_enclosed` et `check_solid_overlaps` — dans leur forme comme dans leur
philosophie : **fatals**, avec une échappatoire **explicite à écrire** dans le JSON.

---

### C-01 — `check_inside_shell` : rien ne sort du bâtiment

**Attrape** P-01, P-02, P-03, P-04, P-14, P-23 — soit **six défauts dont quatre
bloquants**, la moitié de tout ce rapport.

**Règle.** Chaque sommet de chaque objet doit être à l'intérieur du polygone du parement
intérieur, ou à moins de `wallThickness` derrière lui (un objet peut légitimement mordre
dans son mur : une affiche, un tableau électrique, un about de poutre). Au-delà, il est
dehors, et c'est fatal.

**Implémentation.** Tout est déjà là. `parse_walls` connaît le polygone et l'épaisseur ;
il suffit de le rentrer de `thickness / 2` et de garder le résultat dans `rg_builder`.
Le test est un point-dans-polygone plus une distance point-segment, appelé sur
`b->last_bounds` en pré-filtre puis sur les sommets du maillage.

**Échappatoire.** `"outsideOk": true`, à écrire, pour le seul cas légitime connu : rien
aujourd'hui — le battant d'entrée et son panneau occupent le percement, pas l'extérieur.

**Message.** Sur le modèle de `check_fixture_naming`, dire la distance et le sommet :

```
« bureau » est à 1,24 m AU-DELÀ du parement intérieur (1,04 m dehors).
  pire sommet   (9.14, 0.70, 5.79)
  parement le plus proche : pan coupé, de (6.13, 7.10) à (9.55, 3.60)
  Un objet dehors n'est pas invisible : il se voit depuis la rue, et il
  MANQUE là où il devait être. Si c'est voulu, l'écrire : "outsideOk": true.
```

---

### C-02 — `check_solid_overlaps`, au triangle et fatal

**Attrape** P-05, P-13, P-16, P-18, et le recouvrement porte/poutre de P-06.

**Ce qui ne va pas dans le contrôle actuel.** Il fait deux choses discutables. D'abord il
ne compare que des **boîtes englobantes** : sur mes 33 paires de mobilier signalées par
la boîte, j'en ai testé 8 au triangle, et **2 ne se pénétraient pas du tout**
(canapé/fauteuil, canapé/table basse). Un contrôle qui crie faux une fois sur quatre
devient un contrôle qu'on lit en diagonale. Ensuite il n'est fatal **que si une borne est
en cause**, ce qui laisse passer un canapé empalé sur un poteau de 0,0412 m³.

**Règle proposée, en deux temps.**

1. La boîte englobante reste le **pré-filtre** — c'est ce qu'elle sait faire, et c'est
   gratuit.
2. Toute paire qui passe le pré-filtre est confirmée au **triangle**, en réemployant
   `light_is_enclosed` : un sommet de A dans le solide de B. La routine existe déjà
   (`tools/roomgen.c:381`), elle est écrite pour des maillages non étanches, et elle est
   justement documentée pour ça.
3. Fatal dès qu'un sommet est confirmé dedans, **sauf** si la paire est déclarée.

**Échappatoire.** Nommée, pas générique. Une poutre qui repose sur son pilier est un
assemblage voulu, et il se déclare :

```json
{ "name": "poutre", ..., "traverse": ["pilier_ouest", "pilier_est"] }
```

C'est la différence exacte entre le contrôle actuel et celui-ci : aujourd'hui la
tolérance est **une catégorie** (« tout sauf les bornes »), demain elle est **une liste
de noms**. Une nouvelle intersection ne peut plus se glisser dans une catégorie
préexistante.

---

### C-03 — `check_grounded` : ce qui est posé touche son support

**Attrape** P-02 (le flottement), P-12, P-16, P-22, et P-11.

**Le fond du problème.** Le fichier déclare tout — le matériau, le son de pas, le pavage,
le point d'intérêt — sauf **sur quoi l'objet repose**. Il faut donc le déduire, et toute
déduction se trompe quelque part : c'est le raisonnement qui a fondé la réécriture
entière de la salle, appliqué à la pose.

**Règle.** Ajouter une clé `"pose"` obligatoire à tout prop :

| valeur | contrôle |
|---|---|
| `"sol"` | le sommet le plus bas est à ±10 mm du sol sous l'empreinte (0,0 ou le dessus de l'estrade) |
| `"meuble": "<nom>"` | le sommet le plus bas est à ±5 mm du dessus du meuble nommé, sous l'empreinte |
| `"mur"` | le point le plus proche du parement est à moins de 30 mm de celui-ci |
| `"suspendu"` | voir C-06 |
| `"libre"` | rien, mais il faut l'écrire |

Ce n'est pas une clé de plus pour le plaisir : c'est **la même clé** qui rend possibles
C-03, C-05 et C-06 d'un seul geste, et elle rend l'audit de placement automatique au lieu
d'être un document.

**Ce que ça aurait donné aujourd'hui :**

```
« poubelle_sas » est déclarée posée au sol et FLOTTE de 10,9 cm.
  base mesurée   y = 0.109
  sol dessous    y = 0.000
  Le modèle importé porte son propre décalage : le corriger avec
  "modelOffset", pas avec "at".
```

---

### C-04 — `check_facing` : une façade regarde la salle

**Attrape** P-07 (la radio) et le miroir de P-04.

**Règle.** Pour tout prop déclaré `"pose": "mur"`, la direction de regard du prop —
`(sin yaw, cos yaw)`, la même convention que partout — doit avoir un produit scalaire
supérieur à 0,7 avec la normale rentrante du parement le plus proche. La radio murale
donne aujourd'hui un produit scalaire de **0,0** : elle regarde exactement le long du mur.

Ce contrôle est bon marché — le parement le plus proche est déjà calculé par C-01 — et
il attrape la classe entière des lacets à 90° près, qui est **la faute la plus fréquente
de ce fichier** : elle explique à elle seule P-04 et P-07, soit un bloquant et un
visible.

**Variante à envisager, plus forte.** Contrôler aussi que la **profondeur** d'un prop
mural (son extension le long de sa direction de regard) est inférieure à sa **largeur**.
Un meuble adossé plus profond que large est presque toujours une interversion de X et Z :
le lavabo (0,52 large, 1,40 profond) et la radio (0,10 large, 0,40 profond) auraient tous
deux été refusés, et le distributeur (0,86 large, 0,78 profond) serait passé.

---

### C-05 — `check_panel_visible` : un panneau d'épaisseur nulle n'est pas dans un mur

**Attrape** P-08 et P-15.

**Règle.** Un `type: "panel"` a une normale connue. Tirer un rayon **le long de cette
normale** depuis le centre du panneau : si la première intersection est à moins de 5 mm,
le panneau est plaqué contre ou dans un solide, et il ne rendra rien. Fatal.

C'est le même outil que `light_is_enclosed`, avec un rayon au lieu de sept, et il attrape
une classe de défauts totalement muette : un panneau enterré ne produit ni erreur, ni
avertissement, ni pixel. Rien ne le signale — c'est exactement le critère que le
commentaire de `check_fixture_naming` retient pour justifier son existence : « le pire cas
de figure — un défaut qui produit un résultat, et qu'on ne trouve qu'en mesurant. » Ici il
ne produit même pas de résultat.

---

### C-06 — `check_hanging` : ce qui pend est accroché

**Attrape** P-10.

**Règle.** Un prop `"pose": "suspendu"` doit avoir de la géométrie à moins de 5 cm de la
sous-face du plafond, **ou** déclarer le prop qui lui sert de câble
(`"suspenduPar": "cable_comptoir"`), auquel cas c'est le câble qui subit le contrôle.

La sous-face du plafond est déjà connue de `parse_ceilings`. Le message doit donner le
vide mesuré, parce que c'est un nombre qui parle :

```
« suspension_comptoir » est déclarée suspendue et son sommet est à 1,198 m
sous le plafond, sans câble. Elle pend dans le vide.
```

---

### C-07 — `check_light_has_body` : symétrique de `check_lights_not_enclosed`

**Attrape** les sept sources de la section « Ce qui manque », et P-03 en second recours.

**Règle.** Le contrôle existant vérifie qu'une source n'est **pas dedans**. Il lui manque
son symétrique : une source qui n'est **pas dedans du tout** est une lumière sans
luminaire. Pour toute lumière sans `ceilingPanel`, exiger de la géométrie de prop à moins
de 40 cm. Fatal, avec `"nu": true` pour un tube volontairement caché.

C'est le contrôle le moins cher du lot : la boucle et la structure de données sont celles
de `check_lights_not_enclosed`, à la comparaison près. Et il a la même valeur, parce qu'il
attrape la même chose vue de l'autre côté — une source dont le résultat à l'écran ne
ressemble pas à sa cause.

---

### C-08 — `check_floor_material` : une pièce a un sol, pas deux

**Attrape** P-09.

**Attention à la règle qu'on est tenté d'écrire.** La première version de ce contrôle,
dans mon propre brouillon, comptait le nombre de rectangles de `floors` couvrant chaque
point : zéro = trou, deux = coplanarité. Je l'ai écrite, puis exécutée — **grille de
10 cm, 26 415 points intérieurs, zéro trou et zéro double couverture.** Elle n'attrape pas
P-09. La couverture est parfaite ; c'est le **matériau** qui est faux.

C'est le même piège que le R-15 de `REVUE-STRICTE.md`, qui a vu la moquette et en a conclu
que les sols se recouvraient. Le symptôme ressemble à un défaut de couverture et n'en est
pas un.

**La règle qui attrape.** Le fichier déclare déjà ses pièces : `soundZones` les nomme
(`toilettes`, `sas_entree`, `coin_billard`) et les borne par une boîte. Pour chaque zone,
échantillonner son emprise au sol et exiger que **tous** les rectangles de `floors` qui la
couvrent partagent un seul matériau — ou que la zone déclare la liste qu'elle accepte :

```json
{ "name": "toilettes", "min": [...], "max": [...], "sol": "sol_toilettes" }
```

Aujourd'hui, la zone `toilettes` mélange `sol_toilettes` (62,4 %), `sol_hall` (23,5 %) et
`sol_couloir` (14,1 %) : trois rectangles, deux matériaux, dans une pièce déclarée
carrelée.

**Le contrôle de couverture reste à écrire quand même**, mais comme garde-fou et non comme
correctif : le commentaire de `floors` **affirme la propriété en prose** — « Trois
rectangles disjoints plutôt qu'un grand recouvert : deux surfaces coplanaires se disputent
le tampon de profondeur, et le scintillement qui en résulte ne se voit qu'en mouvement » —
et personne ne la vérifie. Elle est vraie aujourd'hui. C'est le bon moment.

---

### C-09 — `check_cabinet_clearance` : on peut se tenir devant une borne

**N'attrape rien aujourd'hui**, et c'est la raison de l'ajouter.

**Règle.** Au moins 80 cm de vide devant chaque borne, le long de sa direction de regard,
en ignorant les objets de moins de 30 cm de haut (un tapis technique n'empêche pas de
jouer).

Les dix-neuf bornes tiennent la règle avec une marge de 2 fois. C'est précisément le
moment de l'écrire : un garde-fou qu'on ajoute pendant que tout passe est un garde-fou
gratuit, alors que celui qu'on ajoute après coup demande d'abord de corriger. La propriété
qu'il protège est la seule qui rende une borne jouable, et l'îlot central est manipulé à
chaque fois qu'on ajoute un jeu.

---

## À vérifier

Mesuré, mais **pas confirmé à l'œil** : je ne l'affirme pas.

**A-1 — Un bandeau de 8 cm entre le haut des murs et la corniche.** Les murs s'arrêtent à
`room.height` = **2,920**. La corniche occupe **3,000 → 3,100**. La surface la plus basse
du plafond est à **3,058** (792 sommets, les rails en T). Il y a donc, sur tout le
périmètre, une bande de **8 cm de hauteur sans aucune géométrie**, entre le sommet du mur
et le dessous de la corniche. Sur les cadrages tentés — dont
`--pos=-2.00,1.70,3.00 --yaw=-90 --pitch=22` — la corniche, qui déborde de 9 cm, la
masque : il faudrait une ligne de vue plus raide que 42° pour l'ouvrir, ce qu'un œil à
1,70 m n'obtient qu'en se collant au mur. **Je n'ai pas trouvé d'angle où elle se voit.**
Si un contrôle est écrit, la règle est simple : `ceilings[].y − railDrop` doit être
inférieur ou égal à `room.height`, ou une corniche doit couvrir l'écart.

**A-2 — Le sol et le plafond débordent du bâtiment.** `sol_hall` va jusqu'à x = 11,10 et
`plafond` jusqu'à ±11,119 / ±8,119, contre 9,55 et 7,10 pour les parements : **1,55 m de
moquette et de dalles au-delà du mur est**. C'est visible sur les deux captures
extérieures — la moquette néon continue sur le trottoir. Ce n'est pas atteignable par un
joueur et ce n'est probablement pas un défaut ; je le note parce que C-01, écrit
naïvement, refuserait ces trois objets. Le contrôle doit donc porter sur les **props, les
bornes et les boîtes**, pas sur les sols et le plafond.

**A-3 — `neon_mur_ouest` #1 est dans les toilettes.** Mesuré : la source est à
(−8,90 ; 2,20 ; −4,00), soit **30 cm au-delà de la cloison**, donc dans le bloc sanitaire
et dans la zone sonore `toilettes`. Elle porte le nom d'un mur du hall. C'est peut-être
voulu — une lumière n'a pas d'obligation de rester du côté de son nom — mais c'est aussi
la seule source, avec `plafonnier_toilettes`, dans une pièce que R-27 mesure **cinq fois
plus claire que le hall**. `check_fixture_naming` ne peut pas la voir : il ne confronte
que les noms se terminant par un `kind` de point d'intérêt. L'étendre aux `soundZones`,
qui sont des pièces nommées, le rendrait capable de poser la question.

**A-4 — Trois affiches identiques sur le mur ouest.** `affiche_ouest` est déclarée avec
`repeat: {count: 3}` et sort donc en trois exemplaires rigoureusement semblables, espacés
de 2,044 m, visibles ensemble sur `--pos=-6.20,1.70,1.60 --yaw=185 --pitch=-3`. C'est le
R-36 de `REVUE-STRICTE.md`, toujours là. Ce n'est pas un défaut de **placement** — les
trois sont à plat sur le mur, à la bonne hauteur, à 2 cm du parement — donc il ne compte
pas dans le décompte ci-dessous. Il est cité parce que le mécanisme, `repeat` sur un objet
qui porte une image, produit toujours cette faute-là.

---

## Décompte

**23 défauts de placement**, tous mesurés sur la géométrie générée et tous vus sur au
moins une capture :

- **5 bloquants** — P-01 à P-05. Quatre d'entre eux sont un objet posé **hors du
  bâtiment** ; le cinquième est un canapé traversé par un poteau de béton sur 0,0412 m³.
- **10 visibles** — P-06 à P-15.
- **8 mineurs** — P-16 à P-23.

Quatre entrées supplémentaires en « À vérifier », dont **aucune n'est comptée** : A-1 et
A-3 sont mesurées mais pas confirmées à l'œil, A-2 est vue mais depuis un point que le
joueur n'atteint pas, A-4 est vue et confirmée mais n'est pas un défaut de placement.

Neuf contrôles proposés pour `tools/roomgen.c`. Les trois premiers — C-01 (hors du
bâtiment), C-02 (recouvrement au triangle), C-03 (ce qui est posé touche son support) —
attrapent **17 des 23**, dont les cinq bloquants. Les six autres se réutilisent presque
entièrement : le polygone rentré de C-01 sert à C-04, la routine de parité de
`check_lights_not_enclosed` sert à C-02 et C-05, la clé `"pose"` de C-03 sert à C-04 et
C-06.
