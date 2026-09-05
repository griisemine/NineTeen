# Relevé de la salle de 2020

*Établi par la mesure sur `legacy/room/textures/salle.obj`, `legacy/room/textures/salle.mtl` et `legacy/room/room.c`. Aucun chiffre de ce document n'est repris d'un autre document : chacun a été recalculé depuis un fichier source, et la référence est donnée à côté. Comparaison faite avec `assets/scene/salle.room.json` dans l'état de la copie de travail, empreinte SHA-256 commençant par `0066a9fba6492c43` — voir l'avertissement du § 6.*

---

## 0. Repère, échelle, méthode

**Échelle.** Le modèle de 2020 est en unités Blender. L'auteur donne lui-même le facteur : `HAUTEUR_CAMERA_DEBOUT 3.5F` (`legacy/room/room.c:90`) est la hauteur d'œil d'un homme debout, donc 2,06 unités valent un mètre — 3,5 / 2,06 = 1,699 m.

**Conversion vers le repère de `salle.room.json` :**

```
x_m = x_u / 2,06
y_m = (y_u − 0,100) / 2,06
z_m = z_u / 2,06 − 4,68
```

Les deux premières lignes m'ont été données et je les ai vérifiées sur le sas, le bloc sanitaire et les douze bornes de l'îlot (§ 6.1). La **troisième, celle des hauteurs, est de moi** : le terme −0,100 amène le dessus du sol de 2020 (`y_u = 0,100`, face `l601` de `MUR_SAL_Cube.002`) sur `y = 0`. Elle se vérifie seule : le fichier actuel déclare `head = 2,879` pour ses deux baies, et (6,030 − 0,100) / 2,06 = 2,879 exactement, 6,030 étant la sous-face du plafond de 2020.

**Axes.** `x` croît vers l'est, `z` vers le nord, `y` vers le haut. Dans le plan de 2020 le comptoir est au **nord**, le billard au **sud-ouest**, les toilettes à l'**est**, le sas d'entrée au **nord-est**.

**Un piège à ne pas rouvrir.** L'objet de murs `MUR_SAL_Cube.002` mesure **19,304 m en X et 14,402 m en Z**. Le X est bien l'axe qui porte les 19,30 m, **bloc sanitaire compris** ; le Z porte les 14,40 m. Ce sont les cotes de l'ENVELOPPE bâtie, pas celles du hall.

**Ce que dit `detectionEnvironnement`.** Son second paramètre s'appelle `y` mais il reçoit `camera->pz` (`legacy/room/room.c:1996`, `:2014`, `:2029`, `:2045`) : c'est le **z monde**. Dans tout ce document je l'écris `z`.

**Direction du regard.** `gluLookAt` vise `(px + sin(angle), …, pz + cos(angle))` (`legacy/room/room.c:2084`) : angle 0 regarde +z, π regarde −z, π/2 regarde +x, 3π/2 regarde −x. `ANGLE_DETECTION_MACHINE` vaut π/6, soit une tolérance de ±30° (le commentaire du code dit 60° : il parle de l'ouverture totale).

---

## 1. Les cotes d'ensemble, mesurées

| Grandeur | Unités du modèle | Mètres | Où c'est mesuré |
|---|---|---|---|
| Enveloppe bâtie (hall + bloc sanitaire) | x −15,000…24,767 ; z −5,200…24,469 | **19,304 × 14,402** | `salle.obj`, boîte de `MUR_SAL_Cube.002` |
| Hall, parements intérieurs | x ±14,727 ; z −4,928…24,196 | 14,298 × 14,138 | faces `l648`, `l649`, `l629`, `l641` |
| Hall, lignes médianes des murs | x ±14,8635 ; z −5,064…24,3325 | 14,431 × 14,273 | idem, épaisseur 0,273 u = 0,133 m |
| Surface de plancher du hall | — | **184,44 m²** | polygone des parements, pans coupés compris |
| Idem mesurée sur les lignes médianes | — | 188,00 m² | c'est le chiffre du fichier actuel |
| Bloc sanitaire, intérieur | x 14,995…24,620 ; z −2,995…12,799 | 4,672 × 7,667 = **35,82 m²** | faces `l699`, `l715`, `l716`, `l717` |
| Sas d'entrée, intérieur | x 10,886…14,324 ; z 17,146…35,875 | 1,669 × 9,092 = **15,17 m²** | faces de `Cube.048_Cube.006` |
| Hauteur libre du hall | y 0,100…6,030 | **2,879 m** | sol `l601`, sous-face du plafond `toit_Cube.033` |
| Hauteur de l'objet de murs | y −0,100…6,007 | 2,965 m | boîte de `MUR_SAL_Cube.002` |
| Hauteur libre du sas | y 0,097…4,898 | **2,331 m** | faces `l222626` et `l222627` |
| Épaisseur des murs du hall | 0,273 u | 0,133 m | écart parement/parement |
| Pans coupés, pente | dx/dz = −0,4606 (est), +0,4645 (ouest) | — | faces `l644` et `l697` |

Le hall n'est **pas** un rectangle : c'est un rectangle de 14,298 × 9,541 m (z −7,072 à +2,469) surmonté d'un trapèze fermé par **deux pans coupés symétriques** et par un mur nord de 8,32 m.

---

## 2. Les 127 objets de `salle.obj`

Boîtes englobantes, calculées sur les sommets réellement référencés par les faces de chaque objet. L'ordre est celui du fichier ; la colonne `#` est le rang de la déclaration `o`.

| # | Objet | Rôle | Boîte en mètres (x / y / z) | Boîte en unités (x / y / z) | Texture(s) |
|---|---|---|---|---|---|
| 1 | `CENTRE_MACHINE_SOL_Cube` | Estrade (podium) moquettée de la borne centrale | −1,214…1,214 / −0,257…0,228 / −6,379…−2,981 | −2,500…2,500 / −0,430…0,570 / −3,500…3,500 | moquette.jpg |
| 2 | `MUR_SAL_Cube.002` | COQUILLE COMPLÈTE : sols, murs du hall, bloc sanitaire et ses cloisons, jambages du sas. Objet unique | −7,282…12,023 / −0,097…2,867 / −7,204…7,198 | −15,000…24,767 / −0,100…6,007 / −5,200…24,469 | bordeau_uni.jpg, carllage_mur_toilette.jpg, carllage_toilette.jpg, floor.jpg, marbre_toilettes.jpg, moquette_noire.jpg |
| 3 | `PILONNE.007_Cube.027` | Pilier engagé, mur est, travée nord | 7,026…7,512 / −0,045…2,868 / 2,243…2,729 | 14,474…15,474 / 0,007…6,007 / 14,261…15,262 | bordeaux.jpg |
| 4 | `sconce_02` | Applique murale (pied + ampoule), mur sud | 1,536…1,768 / 2,048…2,439 / −7,012…−6,736 | 3,165…3,643 / 4,318…5,125 / −4,804…−4,235 | jaune.jpg |
| 5 | `sconce_02.001` | Applique murale, mur sud | −1,769…−1,537 / 2,048…2,439 / −7,012…−6,736 | −3,644…−3,166 / 4,318…5,125 / −4,804…−4,235 | jaune.jpg |
| 6 | `sconce_02.002` | Applique murale, mur sud | −4,899…−4,667 / 2,048…2,439 / −7,012…−6,736 | −10,091…−9,613 / 4,318…5,125 / −4,804…−4,235 | jaune.jpg |
| 7 | `sconce_02.003` | Applique murale, mur sud | 4,693…4,926 / 2,048…2,439 / −7,012…−6,736 | 9,669…10,147 / 4,318…5,125 / −4,804…−4,235 | jaune.jpg |
| 8 | `Cube.033_Cube.042` | Fond sombre d'habillage de haut de mur, mur sud | 1,761…4,674 / 0,922…2,864 / −7,291…−7,194 | 3,628…9,628 / 2,000…6,000 / −5,379…−5,179 | marron_foncer.jpg |
| 9 | `planche_bois_mur.002_Cube.040` | Lattes de bois noir, mur sud | 1,973…4,469 / 0,718…2,660 / −7,444…−7,153 | 4,065…9,205 / 1,579…5,579 / −5,695…−5,095 | bois_noir.jpg |
| 10 | `Texte.002` | Texte 3D de signalétique, cloison des toilettes (face ouest) | 8,901…8,969 / 1,518…1,749 / −2,703…−1,997 | 18,337…18,477 / 3,227…3,703 / 4,072…5,526 | ∅ (null) |
| 11 | `toit_Cube.033` | Dalle de plafond unique (déborde très largement le bâtiment) | −10,388…16,530 / 2,879…2,986 / −13,327…8,207 | −21,398…34,052 / 6,030…6,252 / −17,813…26,548 | plafond.jpg |
| 12 | `PANNEAU_EASY/HARD_Cube.035` | Panneau suspendu au-dessus de l'allée : fond BLEU côté x<0 (HARD) et fond ROUGE côté x>0 (EASY) — les lettres font l'inverse | −0,470…0,501 / 2,088…2,476 / 0,631…0,709 | −0,967…1,033 / 4,400…5,200 / 10,941…11,101 | bleu.jpg, rouge.jpg |
| 13 | `Texte.004` | Texte bleu (côté EASY, x>0), face nord du panneau | 0,081…0,420 / 2,236…2,338 / 0,695…0,733 | 0,166…0,865 / 4,706…4,917 / 11,072…11,152 | bleu.jpg |
| 14 | `Texte.005` | Texte rouge (côté HARD, x<0), face nord du panneau | −0,390…−0,047 / 2,236…2,338 / 0,695…0,733 | −0,803…−0,097 / 4,706…4,917 / 11,072…11,152 | rouge.jpg |
| 15 | `Texte.007` | Texte bleu (côté EASY, x>0), face sud du panneau | 0,088…0,428 / 2,236…2,338 / 0,602…0,641 | 0,182…0,881 / 4,706…4,917 / 10,881…10,961 | bleu.jpg |
| 16 | `Texte.006` | Texte rouge (côté HARD, x<0), face sud du panneau | −0,395…−0,052 / 2,236…2,338 / 0,602…0,641 | −0,813…−0,107 / 4,706…4,917 / 10,881…10,961 | rouge.jpg |
| 17 | `Cylindre.005` | Tige de suspension du panneau HARD/EASY | −0,002…0,047 / 2,467…2,953 / 0,643…0,692 | −0,003…0,097 / 5,183…6,183 / 10,966…11,066 | ∅ Matériau.070 |
| 18 | `Cube.165_Cube.011` | Appareil sanitaire (cuvette + réservoir), cabine côté FEMMES — identification probable, modèle blanc sans texture | 9,385…10,142 / 0,010…1,420 / −2,198…−1,379 | 19,334…20,892 / 0,121…3,026 / 5,113…6,800 | ∅ Material.001 |
| 19 | `Cube.129_Cube.039` | Appareil sanitaire (cuvette + réservoir), cabine côté HOMMES — identification probable | 9,391…10,147 / 0,010…1,420 / −3,235…−2,416 | 19,345…20,902 / 0,121…3,026 / 2,976…4,663 | ∅ Material.004 |
| 20 | `Billiard_Table` | Table de billard, ses queues, ses billes, sa craie et son luminaire | −6,295…−2,671 / −0,001…4,488 / −5,675…−3,390 | −12,967…−5,502 / 0,098…9,344 / −2,049…2,657 | billard_table.jpg, bois.jpg, gris_foncer.jpg, jaune.jpg |
| 21 | `Cube_Cube.074` | Pictogramme toilettes, cloison des toilettes | 8,902…8,950 / 0,922…1,408 / −2,652…−2,070 | 18,338…18,438 / 2,000…3,000 / 4,177…5,377 | toilet.jpg |
| 22 | `Cube.001_Cube.075` | Cadre d'affiche, mur sud (poster_8) | 5,621…6,398 / 1,172…2,143 / −7,201…−7,104 | 11,580…13,180 / 2,515…4,515 / −5,194…−4,994 | poster_8.jpg, poutre.jpg |
| 23 | `Cube.002_Cube.076` | Cadre d'affiche, mur sud (poster_3) | −0,443…0,334 / 1,172…2,143 / −7,201…−7,104 | −0,912…0,688 / 2,515…4,515 / −5,194…−4,994 | poster_3.jpg, poutre.jpg |
| 24 | `Cube.003_Cube.077` | Cadre d'affiche, mur sud (poster_1) | −3,778…−3,001 / 1,172…2,143 / −7,201…−7,104 | −7,782…−6,182 / 2,515…4,515 / −5,194…−4,994 | poster_1.png, poutre.jpg |
| 25 | `Cube.004_Cube.078` | Cadre d'affiche, mur sud (poster_2) | 2,909…3,685 / 1,172…2,143 / −7,201…−7,104 | 5,992…7,592 / 2,515…4,515 / −5,194…−4,994 | poster_2.png, poutre.jpg |
| 26 | `Cube.005_Cube.079` | Cadre d'affiche, mur sud (poster_6) | −6,445…−5,669 / 1,172…2,143 / −7,201…−7,104 | −13,277…−11,677 / 2,515…4,515 / −5,194…−4,994 | poster_6.jpg, poutre.jpg |
| 27 | `Cube.014_Cube.086` | Bandeau bordeaux de haut de mur, mur ouest (travée nord) | −7,520…−7,132 / 1,896…2,867 / −0,552…2,361 | −15,491…−14,691 / 4,006…6,006 / 8,504…14,504 | bordeau_uni.jpg |
| 28 | `Cube.012_Cube.005` | Joue de coffre de la porte coulissante FEMMES (côté sud) | 10,523…11,979 / −0,096…1,845 / −0,560…−0,512 | 21,677…24,677 / −0,099…3,901 / 8,487…8,587 | marbre_toilettes.jpg |
| 29 | `Cube.015_Cube.009` | Joue de coffre de la porte coulissante FEMMES (côté nord) | 10,523…11,979 / −0,096…1,845 / −0,454…−0,430 | 21,677…24,677 / −0,099…3,901 / 8,705…8,755 | marbre_toilettes.jpg |
| 30 | `Cube.016_Cube.022` | Linteau du coffre de la porte coulissante FEMMES | 10,523…11,979 / 1,840…1,845 / −0,544…−0,447 | 21,677…24,677 / 3,891…3,901 / 8,520…8,720 | marbre_toilettes.jpg |
| 31 | `Cube.019_Cube.025` | Joue de coffre de la porte coulissante HOMMES (côté nord) | 10,523…11,979 / −0,096…1,845 / −4,144…−4,096 | 21,677…24,677 / −0,099…3,901 / 1,103…1,203 | marbre_toilettes.jpg |
| 32 | `Cube.018_Cube.024` | Joue de coffre de la porte coulissante HOMMES (côté sud) | 10,523…11,979 / −0,096…1,845 / −4,226…−4,202 | 21,677…24,677 / −0,099…3,901 / 0,935…0,985 | marbre_toilettes.jpg |
| 33 | `Cube.017_Cube.023` | Linteau du coffre de la porte coulissante HOMMES | 10,523…11,979 / 1,840…1,845 / −4,209…−4,112 | 21,677…24,677 / 3,891…3,901 / 0,971…1,171 | marbre_toilettes.jpg |
| 34 | `Cube.020_Cube.008` | Bouton poussoir d'ouverture, porte FEMMES | 8,992…9,113 / 1,382…1,624 / −0,232…0,011 | 18,523…18,773 / 2,946…3,446 / 9,163…9,663 | toilette_push.jpg |
| 35 | `Cube.021_Cube.001` | Bouton poussoir d'ouverture, porte HOMMES | 8,992…9,113 / 1,382…1,624 / −4,646…−4,403 | 18,523…18,773 / 2,946…3,446 / 0,070…0,570 | toilette_push.jpg |
| 36 | `Cube.022_Cube.012` | Plan de lavabo (marbre), toilettes HOMMES | 11,194…11,971 / 0,815…1,082 / −6,150…−4,208 | 23,060…24,660 / 1,778…2,328 / −3,029…0,971 | lavabo.jpg |
| 37 | `Cube.023_Cube.026` | Robinetterie ou distributeur au-dessus du lavabo HOMMES (sud) | 11,666…11,977 / 1,287…1,428 / −5,754…−5,696 | 24,032…24,673 / 2,751…3,042 / −2,213…−2,093 | ∅ None |
| 38 | `Cube.024_Cube.028` | Robinetterie ou distributeur au-dessus du lavabo HOMMES (nord) | 11,666…11,977 / 1,287…1,428 / −4,696…−4,637 | 24,032…24,673 / 2,751…3,042 / −0,032…0,088 | ∅ None |
| 39 | `Cube.027_Cube.052` | Plan de lavabo (marbre), toilettes FEMMES | 11,194…11,971 / 0,815…1,082 / −0,413…1,529 | 23,060…24,660 / 1,778…2,328 / 8,790…12,790 | lavabo.jpg |
| 40 | `Cube.026_Cube.050` | Robinetterie ou distributeur au-dessus du lavabo FEMMES (sud) | 11,666…11,977 / 1,287…1,428 / −0,017…0,041 | 24,032…24,673 / 2,751…3,042 / 9,606…9,726 | ∅ None |
| 41 | `Cube.025_Cube.047` | Robinetterie ou distributeur au-dessus du lavabo FEMMES (nord) | 11,666…11,977 / 1,287…1,428 / 1,042…1,100 | 24,032…24,673 / 2,751…3,042 / 11,787…11,907 | ∅ None |
| 42 | `bar_accueil_Cube.030` | Comptoir d'accueil | −2,767…2,767 / 0,000…1,254 / 4,930…7,066 | −5,700…5,700 / 0,100…2,684 / 19,797…24,197 | desk.jpg |
| 43 | `Cube.047_Cube.089` | Panneau publicitaire, face avant du comptoir (ouest) | −2,561…−0,960 / 0,208…1,179 / 4,979…5,221 | −5,277…−1,977 / 0,529…2,529 / 19,897…20,397 | pub.jpg |
| 44 | `Cube.039_Cube.090` | Panneau publicitaire, face avant du comptoir (centre) | −0,801…0,801 / 0,208…1,179 / 4,979…5,221 | −1,650…1,650 / 0,529…2,529 / 19,897…20,397 | pub.jpg |
| 45 | `Cube.032_Cube.091` | Panneau publicitaire, face avant du comptoir (est) | 0,960…2,561 / 0,208…1,179 / 4,979…5,221 | 1,977…5,277 / 0,529…2,529 / 19,897…20,397 | pub.jpg |
| 46 | `Cube.035_Cube.092` | Panneau publicitaire, joue ouest du comptoir | −2,723…−2,480 / 0,208…1,179 / 5,189…6,791 | −5,610…−5,110 / 0,529…2,529 / 20,330…23,630 | pub.jpg |
| 47 | `Cube.042_Cube.093` | Panneau publicitaire, joue est du comptoir | 2,480…2,723 / 0,208…1,179 / 5,189…6,791 | 5,110…5,610 / 0,529…2,529 / 20,330…23,630 | pub.jpg |
| 48 | `sofa_Cube.001` | Canapé d'angle en cuir rouge (salon) | 2,044…5,372 / −0,009…1,317 / −6,766…−2,772 | 4,210…11,066 / 0,082…2,814 / −4,296…3,931 | cuir_rouge.jpg |
| 49 | `PILONNE.005_Cube.044` | Pilier LIBRE du salon + cloison de lattes qui y aboutit | 1,405…1,891 / −0,250…2,093 / −7,099…−2,155 | 2,895…3,896 / −0,414…4,411 / −4,982…5,202 | bois_noir.jpg, bordeaux.jpg |
| 50 | `Cube.013_Cube.029` | Muret bas de la cloison du salon | 1,432…1,869 / −0,385…1,071 / −7,004…−2,635 | 2,950…3,850 / −0,694…2,306 / −4,788…4,212 | poutre.jpg |
| 51 | `Cube.028_Cube.031` | Plateau de la table basse du salon | 3,883…5,339 / 0,581…0,672 / −6,006…−4,550 | 7,998…10,998 / 1,296…1,485 / −2,732…0,268 | pub.jpg |
| 52 | `Cube.030_Cube.032` | Piètement est de la table basse | 5,042…5,339 / −0,003…0,580 / −6,007…−4,550 | 10,387…10,999 / 0,093…1,294 / −2,733…0,267 | pub.jpg |
| 53 | `Cube.031_Cube.015` | Piètement ouest de la table basse | 3,882…4,179 / −0,003…0,580 / −6,006…−4,550 | 7,997…8,608 / 0,093…1,294 / −2,732…0,268 | pub.jpg |
| 54 | `PILONNE.003_Cube.041` | Pilier engagé, mur est, travée centre | 7,026…7,512 / −0,045…2,868 / −0,912…−0,427 | 14,474…15,474 / 0,007…6,007 / 7,762…8,762 | bordeaux.jpg |
| 55 | `PILONNE.006_Cube.055` | Pilier engagé, mur est, travée sud | 7,026…7,512 / −0,045…2,868 / −4,209…−3,723 | 14,474…15,474 / 0,007…6,007 / 0,971…1,971 | bordeaux.jpg |
| 56 | `PILONNE.010_Cube.058` | Pilier engagé, angle sud-est | 7,026…7,512 / −0,045…2,868 / −7,364…−6,878 | 14,474…15,474 / 0,007…6,007 / −5,529…−4,529 | bordeaux.jpg |
| 57 | `PILONNE.008_Cube.060` | Pilier engagé, mur sud (4e depuis l'ouest) | 4,561…5,046 / −0,045…2,868 / −7,502…−7,016 | 9,395…10,395 / 0,007…6,007 / −5,814…−4,813 | bordeaux.jpg |
| 58 | `PILONNE.009_Cube.094` | Pilier engagé, mur sud (3e depuis l'ouest) | 1,405…1,891 / −0,045…2,868 / −7,502…−7,016 | 2,895…3,896 / 0,007…6,007 / −5,814…−4,813 | bordeaux.jpg |
| 59 | `PILONNE.011_Cube.095` | Pilier engagé, mur sud (2e depuis l'ouest) | −1,891…−1,405 / −0,045…2,868 / −7,502…−7,016 | −3,896…−2,895 / 0,007…6,007 / −5,814…−4,813 | bordeaux.jpg |
| 60 | `PILONNE.016_Cube.096` | Pilier engagé, mur sud (1er depuis l'ouest) | −5,046…−4,561 / −0,045…2,868 / −7,502…−7,016 | −10,395…−9,395 / 0,007…6,007 / −5,814…−4,813 | bordeaux.jpg |
| 61 | `PILONNE.017_Cube.097` | Pilier engagé, angle sud-ouest | −7,500…−7,015 / −0,045…2,868 / −7,364…−6,878 | −15,450…−14,450 / 0,007…6,007 / −5,529…−4,529 | bordeaux.jpg |
| 62 | `PILONNE.001_Cube.098` | Pilier engagé, mur ouest, travée sud | −7,543…−7,057 / −0,045…2,868 / −4,209…−3,723 | −15,538…−14,538 / 0,007…6,007 / 0,971…1,971 | bordeaux.jpg |
| 63 | `PILONNE.002_Cube.100` | Pilier engagé, mur ouest, travée centre | −7,543…−7,057 / −0,045…2,868 / −0,912…−0,427 | −15,538…−14,538 / 0,007…6,007 / 7,762…8,762 | bordeaux.jpg |
| 64 | `PILONNE.004_Cube.101` | Pilier engagé, mur ouest, travée nord | −7,543…−7,057 / −0,045…2,868 / 2,243…2,729 | −15,538…−14,538 / 0,007…6,007 / 14,261…15,262 | bordeaux.jpg |
| 65 | `sconce_02.005` | Applique murale, mur ouest (sud) | −7,062…−6,786 / 2,048…2,439 / −4,089…−3,857 | −14,548…−13,979 / 4,318…5,125 / 1,217…1,695 | jaune.jpg |
| 66 | `sconce_02.004` | Applique murale, mur ouest (nord) | −7,062…−6,786 / 2,048…2,439 / −0,786…−0,554 | −14,548…−13,979 / 4,318…5,125 / 8,021…8,499 | jaune.jpg |
| 67 | `Cube.038_Cube.102` | Bandeau bordeaux de haut de mur, mur ouest (travée centre) | −7,520…−7,132 / 1,896…2,867 / −3,775…−0,862 | −15,491…−14,691 / 4,006…6,006 / 1,864…7,864 | bordeau_uni.jpg |
| 68 | `Cube.011_Cube.103` | Bandeau bordeaux de haut de mur, mur ouest (travée sud) | −7,520…−7,132 / 1,896…2,867 / −6,993…−4,080 | −15,491…−14,691 / 4,006…6,006 / −4,764…1,236 | bordeau_uni.jpg |
| 69 | `Cube.010_Cube.104` | Bandeau bordeaux de haut de mur, mur sud (travée 1) | −7,522…−4,609 / 1,896…2,867 / −7,496…−7,108 | −15,495…−9,495 / 4,006…6,006 / −5,801…−5,001 | bordeau_uni.jpg |
| 70 | `Cube.007_Cube.105` | Bandeau bordeaux de haut de mur, mur sud (travée 3) | −1,456…1,456 / 1,896…2,867 / −7,496…−7,108 | −3,000…3,000 / 4,006…6,006 / −5,801…−5,001 | bordeau_uni.jpg |
| 71 | `Cube.006_Cube.106` | Bandeau bordeaux de haut de mur, mur sud (travée 2) | −4,736…−1,823 / 1,896…2,867 / −7,496…−7,108 | −9,756…−3,756 / 4,006…6,006 / −5,801…−5,001 | bordeau_uni.jpg |
| 72 | `Cube.041_Cube.107` | Bandeau bordeaux de haut de mur, mur sud (travée 4) | 1,761…4,674 / 1,896…2,867 / −7,496…−7,108 | 3,628…9,628 / 4,006…6,006 / −5,801…−5,001 | bordeau_uni.jpg |
| 73 | `Cube.008_Cube.108` | Bandeau bordeaux de haut de mur, mur sud (travée 5) | 4,582…7,495 / 1,896…2,867 / −7,496…−7,108 | 9,440…15,440 / 4,006…6,006 / −5,801…−5,001 | bordeau_uni.jpg |
| 74 | `Cube.009_Cube.110` | Fond sombre d'habillage de haut de mur, mur sud (est) | 4,753…7,665 / 0,922…2,864 / −7,291…−7,194 | 9,791…15,791 / 2,000…6,000 / −5,379…−5,179 | marron_foncer.jpg |
| 75 | `planche_bois_mur.001_Cube.109` | Lattes de bois noir, mur sud (est) | 4,965…7,091 / 0,718…2,660 / −7,444…−7,153 | 10,228…14,607 / 1,579…5,579 / −5,695…−5,095 | bois_noir.jpg |
| 76 | `Cube.029_Cube.112` | Fond sombre d'habillage de haut de mur, mur sud (centre) | −1,459…1,453 / 0,922…2,864 / −7,291…−7,194 | −3,006…2,994 / 2,000…6,000 / −5,379…−5,179 | marron_foncer.jpg |
| 77 | `planche_bois_mur.003_Cube.111` | Lattes de bois noir, mur sud (centre) | −1,247…1,248 / 0,718…2,660 / −7,444…−7,153 | −2,569…2,571 / 1,579…5,579 / −5,695…−5,095 | bois_noir.jpg |
| 78 | `Cube.034_Cube.114` | Fond sombre d'habillage de haut de mur, mur sud (ouest-centre) | −4,673…−1,760 / 0,922…2,864 / −7,291…−7,194 | −9,625…−3,625 / 2,000…6,000 / −5,379…−5,179 | marron_foncer.jpg |
| 79 | `planche_bois_mur.004_Cube.113` | Lattes de bois noir, mur sud (ouest-centre) | −4,461…−1,965 / 0,718…2,660 / −7,444…−7,153 | −9,189…−4,048 / 1,579…5,579 / −5,695…−5,095 | bois_noir.jpg |
| 80 | `Cube.036_Cube.116` | Fond sombre d'habillage de haut de mur, mur sud (ouest) | −7,862…−4,949 / 0,922…2,864 / −7,291…−7,194 | −16,196…−10,196 / 2,000…6,000 / −5,379…−5,179 | marron_foncer.jpg |
| 81 | `planche_bois_mur.005_Cube.115` | Lattes de bois noir, mur sud (ouest) | −6,921…−5,155 / 0,718…2,660 / −7,444…−7,153 | −14,256…−10,619 / 1,579…5,579 / −5,695…−5,095 | bois_noir.jpg |
| 82 | `planche_bois_mur.006_Cube.117` | Lattes de bois noir, mur ouest (sud) | −7,471…−7,180 / 0,718…2,660 / −6,808…−4,312 | −15,391…−14,791 / 1,579…5,579 / −4,383…0,758 | bois_noir.jpg |
| 83 | `Cube.037_Cube.118` | Fond sombre d'habillage de haut de mur, mur ouest (sud) | −7,315…−7,218 / 0,922…2,864 / −7,132…−4,219 | −15,069…−14,869 / 2,000…6,000 / −5,050…0,950 | marron_foncer.jpg |
| 84 | `Cube.040_Cube.119` | Fond sombre d'habillage de haut de mur, mur ouest (centre) | −7,315…−7,218 / 0,922…2,864 / −3,821…−0,908 | −15,069…−14,869 / 2,000…6,000 / 1,769…7,769 | marron_foncer.jpg |
| 85 | `Cube.043_Cube.120` | Fond sombre d'habillage de haut de mur, mur ouest (nord) | −7,315…−7,218 / 0,922…2,864 / −0,571…2,342 | −15,069…−14,869 / 2,000…6,000 / 8,465…14,465 | marron_foncer.jpg |
| 86 | `planche_bois_mur.007_Cube.121` | Lattes de bois noir, mur ouest (centre) | −7,471…−7,180 / 0,718…2,660 / −3,557…−1,062 | −15,391…−14,791 / 1,579…5,579 / 2,313…7,453 | bois_noir.jpg |
| 87 | `planche_bois_mur.008_Cube.122` | Lattes de bois noir, mur ouest (nord) | −7,471…−7,180 / 0,718…2,660 / −0,346…2,149 | −15,391…−14,791 / 1,579…5,579 / 8,928…14,069 | bois_noir.jpg |
| 88 | `Cube.044_Cube.004` | Cadre d'affiche, mur ouest (sud, poster_4) | −7,190…−7,093 / 1,172…2,143 / −5,941…−5,164 | −14,812…−14,612 / 2,515…4,515 / −2,597…−0,997 | poster_4.jpg, poutre.jpg |
| 89 | `Cube.045_Cube.010` | Cadre d'affiche, mur ouest (nord, poster_5) | −7,190…−7,093 / 1,172…2,143 / −2,652…−1,875 | −14,812…−14,612 / 2,515…4,515 / 4,177…5,777 | poster_5.jpg, poutre.jpg |
| 90 | `FLAPPY_BIRD_HARD.006_Cube.043` | BORNE 3 — ASTEROID HARD, rangée nord (écran vers +z) | −1,746…−0,857 / −0,002…2,388 / 0,700…1,879 | −3,597…−1,765 / 0,095…5,020 / 11,083…13,512 | asteroid.jpg, asteroid_font_hard.jpg |
| 91 | `Plane.098_Plane.110` | Poste de radio mural, coin billard | −6,069…−5,692 / 0,961…1,626 / −4,032…−3,733 | −12,502…−11,725 / 2,080…3,449 / 1,336…1,951 | Radio.jpg |
| 92 | `Plane.098_Plane.001` | Poste de radio, comptoir d'accueil | −1,041…−0,664 / 1,255…1,920 / 5,026…5,280 | −2,144…−1,368 / 2,685…4,055 / 19,995…20,518 | Radio.jpg |
| 93 | `Plane.098_Plane.002` | Poste de radio, salon | 5,051…5,305 / 0,671…1,336 / −5,084…−4,707 | 10,405…10,928 / 1,483…2,853 / −0,832…−0,055 | Radio.jpg |
| 94 | `FLAPPY_BIRD_HARD.009_Cube.062` | BORNE 1 — FLAPPY BIRD HARD, rangée nord (écran vers +z) | −3,631…−2,742 / −0,002…2,388 / 0,700…1,879 | −7,480…−5,648 / 0,095…5,020 / 11,083…13,512 | flappy.jpg, flappy_hard_font.jpg |
| 95 | `FLAPPY_BIRD_HARD.001_Cube.066` | BORNE 2 — TETRIS HARD, rangée nord (écran vers +z) | −2,685…−1,796 / −0,002…2,388 / 0,700…1,879 | −5,531…−3,700 / 0,095…5,020 / 11,083…13,512 | tetris.jpg, tetris_font.jpg |
| 96 | `FLAPPY_BIRD_HARD.005_Cube.067` | BORNE 8 — SNAKE EASY, rangée nord (écran vers +z) | 1,814…2,703 / −0,002…2,388 / 0,700…1,879 | 3,736…5,568 / 0,095…5,020 / 11,083…13,512 | snake.jpg, snake_font.jpg |
| 97 | `FLAPPY_BIRD_HARD.008_Cube.068` | BORNE 5 — SNAKE HARD, rangée sud (écran vers -z) | −2,690…−1,800 / −0,002…2,388 / −0,501…0,678 | −5,541…−3,709 / 0,095…5,020 / 8,609…11,038 | snake.jpg, snake_font.jpg |
| 98 | `FLAPPY_BIRD_HARD.003_Cube.069` | BORNE 11 — TETRIS EASY, rangée sud (écran vers -z) | 1,809…2,698 / −0,002…2,388 / −0,501…0,678 | 3,727…5,559 / 0,095…5,020 / 8,609…11,038 | tetris.jpg, tetris_font.jpg |
| 99 | `FLAPPY_BIRD_HARD.002_Cube.070` | BORNE 12 — FLAPPY BIRD EASY, rangée sud (écran vers -z) | 2,755…3,644 / −0,002…2,388 / −0,501…0,678 | 5,675…7,507 / 0,095…5,020 / 8,609…11,038 | flappy.jpg, flappy_easy_font.jpg |
| 100 | `FLAPPY_BIRD_HARD.004_Cube.071` | BORNE 10 — ASTEROID EASY, rangée sud (écran vers -z) | 0,870…1,759 / −0,002…2,388 / −0,501…0,678 | 1,792…3,624 / 0,095…5,020 / 8,609…11,038 | asteroid.jpg, asteroid_font.jpg |
| 101 | `Cube.046_Cube.003` | Dalle d'écran du poste de CLASSEMENT (borne 16), sur le comptoir | −0,412…0,412 / 1,448…1,910 / 5,091…5,183 | −0,848…0,848 / 3,084…4,034 / 20,128…20,319 | leaderboard_font.jpg |
| 102 | `Cube.048_Cube.006` | SAS D'ENTRÉE : sol, deux murs, plafond bas, en un seul objet | 5,196…7,041 / −0,096…2,429 / 3,546…12,832 | 10,705…14,505 / −0,097…5,103 / 16,946…36,075 | floor.jpg |
| 103 | `Cube.049_Cube.014` | Porte extérieure, extrémité nord du sas | 5,504…6,718 / −0,005…2,134 / 12,630…12,873 | 11,338…13,838 / 0,090…4,496 / 35,659…36,159 | porte_exterieur.jpg |
| 104 | `booleen.001_Cube.036` | Volume sans matériau, hors du bâtiment — rôle NON DÉTERMINÉ (reliquat de modélisation probable) | 8,093…9,064 / 0,000…2,331 / 3,644…4,965 | 16,671…18,671 / 0,101…4,901 / 17,148…19,868 | ∅ None |
| 105 | `Cylindre` | Poignée de la porte extérieure (barre basse) | 5,579…5,638 / 0,924…0,983 / 12,652…12,846 | 11,494…11,614 / 2,004…2,124 / 35,703…36,103 | porte_exterieur.jpg |
| 106 | `Cylindre.001` | Poignée de la porte extérieure (barre haute) | 5,579…5,638 / 1,008…1,067 / 12,652…12,846 | 11,494…11,614 / 2,177…2,297 / 35,703…36,103 | porte_exterieur.jpg |
| 107 | `Cylindre.002` | Poignée de la porte extérieure (retour) | 5,599…5,793 / 1,023…1,052 / 12,644…12,673 | 11,533…11,933 / 2,207…2,267 / 35,688…35,748 | porte_exterieur.jpg |
| 108 | `Cube.051_Cube.038` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 8,484…8,581 / 0,957…1,928 / 9,334…10,111 | 17,477…17,677 / 2,071…4,071 / 28,869…30,469 | poster_4.jpg, poutre.jpg |
| 109 | `Cube.050_Cube.037` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 8,484…8,581 / 0,957…1,928 / 11,276…12,053 | 17,477…17,677 / 2,071…4,071 / 32,869…34,469 | poster_5.jpg, poutre.jpg |
| 110 | `Cube.052_Cube.045` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 8,484…8,581 / 0,957…1,928 / 5,451…6,228 | 17,477…17,677 / 2,071…4,071 / 20,869…22,469 | poster_4.jpg, poutre.jpg |
| 111 | `Cube.053_Cube.046` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 8,484…8,581 / 0,957…1,928 / 7,393…8,169 | 17,477…17,677 / 2,071…4,071 / 24,869…26,469 | poster_4.jpg, poutre.jpg |
| 112 | `Cube.057_Cube.053` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 10,124…10,221 / 0,957…1,928 / 11,276…12,053 | 20,855…21,055 / 2,071…4,071 / 32,869…34,469 | poster_5.jpg, poutre.jpg |
| 113 | `Cube.056_Cube.051` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 10,124…10,221 / 0,957…1,928 / 9,334…10,111 | 20,855…21,055 / 2,071…4,071 / 28,869…30,469 | poster_4.jpg, poutre.jpg |
| 114 | `Cube.055_Cube.049` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 10,124…10,221 / 0,957…1,928 / 5,451…6,228 | 20,855…21,055 / 2,071…4,071 / 20,869…22,469 | poster_4.jpg, poutre.jpg |
| 115 | `Cube.054_Cube.048` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 10,124…10,221 / 0,957…1,928 / 7,393…8,169 | 20,855…21,055 / 2,071…4,071 / 24,869…26,469 | poster_4.jpg, poutre.jpg |
| 116 | `Cube.058_Cube.054` | Cadre d'affiche HORS BÂTIMENT (couloir fantôme est) — rôle non déterminé | 9,241…10,018 / 0,957…1,928 / 3,581…3,678 | 19,037…20,637 / 2,071…4,071 / 17,018…17,218 | poster_4.jpg, poutre.jpg |
| 117 | `FLAPPY_BIRD_HARD.010_Cube.021` | BORNE 6 — DEMINEUR HARD, rangée sud (écran vers -z) | −1,744…−0,855 / −0,002…2,388 / −0,501…0,678 | −3,592…−1,760 / 0,095…5,020 / 8,609…11,038 | coming_soon.jpg, demineur_font.jpg |
| 118 | `FLAPPY_BIRD_HARD.007_Cube.034` | BORNE 4 — SHOOTER HARD, rangée sud (écran vers -z) | −3,629…−2,740 / −0,002…2,388 / −0,501…0,678 | −7,475…−5,644 / 0,095…5,020 / 8,609…11,038 | coming_soon.jpg, shooter_font.jpg |
| 119 | `FLAPPY_BIRD_HARD.011_Cube.056` | BORNE 13 — COMING SOON, mur ouest (écran vers +x) — détection DÉSACTIVÉE | −7,210…−6,031 / −0,002…2,388 / −0,025…0,864 | −14,853…−12,424 / 0,095…5,020 / 9,588…11,420 | coming_soon.jpg, noir.jpg |
| 120 | `FLAPPY_BIRD_HARD.012_Cube.057` | BORNE 14 — COMING SOON, mur ouest (écran vers +x) — détection DÉSACTIVÉE | −7,210…−6,031 / −0,002…2,388 / 0,914…1,803 | −14,853…−12,424 / 0,095…5,020 / 11,523…13,355 | coming_soon.jpg, noir.jpg |
| 121 | `FLAPPY_BIRD_HARD.014_Cube.061` | BORNE 9 — SHOOTER EASY, rangée nord (écran vers +z) | 2,753…3,642 / −0,002…2,388 / 0,700…1,879 | 5,671…7,503 / 0,095…5,020 / 11,083…13,512 | coming_soon.jpg, shooter_font.jpg |
| 122 | `FLAPPY_BIRD_HARD.013_Cube.059` | BORNE 7 — DEMINEUR EASY, rangée nord (écran vers +z) | 0,868…1,757 / −0,002…2,388 / 0,700…1,879 | 1,788…3,619 / 0,095…5,020 / 11,083…13,512 | coming_soon.jpg, demineur_font.jpg |
| 123 | `FLAPPY_BIRD_HARD.015_Cube.072` | BORNE 15 — COMING SOON, borne centrale sur l'estrade (écran vers +z) — détection DÉSACTIVÉE | −0,445…0,445 / 0,212…2,602 / −5,134…−3,955 | −0,916…0,916 / 0,536…5,460 / −0,935…1,494 | coming_soon.jpg, noir.jpg |
| 124 | `Cube.059_Cube.007` | Enseigne lumineuse NINETEEN, au-dessus du comptoir | −1,273…1,273 / 2,046…2,507 / 6,968…7,429 | −2,622…2,622 / 4,315…5,264 / 23,995…24,945 | nineteen_name.jpg |
| 125 | `Cube.060_Cube.013` | Capot / dos du moniteur de classement | −0,425…0,425 / 1,420…1,931 / 5,082…5,203 | −0,875…0,875 / 3,025…4,077 / 20,109…20,359 | gris_foncer.jpg |
| 126 | `Cube.061_Cube.016` | Socle du moniteur de classement | −0,146…0,146 / 1,199…1,296 / 5,023…5,217 | −0,300…0,300 / 2,569…2,769 / 19,987…20,387 | gris_foncer.jpg |
| 127 | `Cylindre.003` | Pied du moniteur de classement | −0,049…0,049 / 1,286…1,675 / 5,128…5,176 | −0,100…0,100 / 2,750…3,550 / 20,204…20,304 | noir.jpg |

Quelques lectures qui ne se voient pas dans le tableau :

- **`MUR_SAL_Cube.002` n'est pas « les murs » : c'est tout le gros œuvre.** Ses 139 faces portent six matériaux : `sol_moquette` (le sol et les murs du hall), `sol_toilette`, `mur_toilette`, `marbre_toilette`, `haut_mur` (les tableaux du sas) et `moquette_noir` (le soubassement). Toute la géométrie de circulation du bâtiment tient dans cet objet, à l'exception du sas, qui est `Cube.048_Cube.006`.
- **Il n'y a aucune poutre de plafond.** Les seize matériaux nommés `poutre*` de `salle.mtl` servent tous des **cadres d'affiche**, sauf `marbre_poutre.001` qui sert le muret du salon. Vérifié objet par objet.
- **Le mur est traité en trois couches** : un soubassement de moquette noire de 0 à 1,056 m ; un fond sombre (`marron_foncer`) de 0,922 à 2,864 m ; des lattes de bois noir de 0,718 à 2,660 m devant ce fond ; et un bandeau bordeaux (`bordeau_uni`) de 1,896 à 2,867 m. Les cadres d'affiche sont tous à la même hauteur, 1,172 à 2,143 m.
- **Trois objets sont hors du bâtiment** : les neuf cadres `Cube.050` à `Cube.058` (x 8,484 à 10,221, z 3,581 à 12,014), le volume sans matériau `booleen.001_Cube.036`, et la dalle `toit_Cube.033` qui déborde de −10,387 à 16,530 en x. Ce sont des décors laissés hors champ, pas une pièce perdue.
- **Un défaut d'origine** : la tige du luminaire du billard (`support_lampe_billard`) monte à 4,487 m alors que la sous-face du plafond est à 2,879 m. Elle traverse le plafond sur 1,61 m.

---

## 3. L'espace jouable de 2020, règle par règle

`detectionEnvironnement(x, z)` (`legacy/room/room.c:2090` à `:2214`) est une **union de zones interdites** : chaque règle qui matche renvoie 0 (bloqué), et la fonction ne renvoie 1 qu'après les avoir toutes traversées. L'ordre n'a donc aucune importance, sauf pour les deux portes coulissantes dont le seuil dépend d'une variable mobile.

| Ligne | Ce que la règle représente | Condition (unités) | En mètres |
|---|---|---|---|
| `2099` | Le bloc du comptoir et tout ce qui est derrière | `x <= 6.0 && x >= -6.0 && z >= 19.5 && z < 25.0` | x de −2,913 à 2,913 ; z de 4,786 à 7,456 |
| `2102` | Le mur nord, plus large que le comptoir | `x > -9.0 && x < 9.0 && z > 23.8 && z < 24.5` | x de −4,369 à 4,369 ; z de 6,868 à 7,208 |
| `2108` | Le mur en diagonale du coin nord-ouest | `z >= 14.0 && x <= 0.4736842105*z - 19.36842105` | z ⩾ 2,116 ; bloqué si x ⩽ 0,4737·z − 7,185 |
| `2116` | Le mur en diagonale du coin nord-est, avant la porte du sas | `z >= 14.0 && z < 17.5 && x >= -0.4736842105*z + 19.36842105` | z de 2,116 à 3,816 ; bloqué si x ⩾ 7,185 − 0,4737·z |
| `2119` | Le même mur, après la porte du sas ; la borne x < 11,3 laisse entrer dans le sas | `z >= 19.2 && x >= -0.4736842105*z + 19.36842105 && x < 11.3` | z ⩾ 4,641 ; bloqué si x de (7,185 − 0,4737·z) à 5,485 |
| `2124` | Les deux rangées de six bornes, dos à dos, et rien entre elles | `z >= 8.5 && z <= 13.5 && ((x <= 8.0 && x >= 1.4) || (x > -8.0 && x <= -1.4))` | z de −0,553 à 1,874 ; x de 0,680 à 3,883 et de −3,883 à −0,680 |
| `2129` | Les deux caissons COMING SOON contre le mur ouest | `x <= -12.5 && z <= 13.72 && z >= 9.0` | x ⩽ −6,068 ; z de −0,311 à 1,981 |
| `2134` | Le caisson COMING SOON posé sur l'estrade | `x >= -1.5 && x <= 1.5 && z >= -1.5 && z <= 1.5` | x de −0,728 à 0,728 ; z de −5,408 à −3,952 |
| `2141` | Le mur qui sépare le hall des sanitaires ; seule la bande z ]2,5 ; 7,0[ laisse passer | `(z >= 7.0 || z <= 2.5) && x >= 14.0 && x <= 16.0` | mur pour x de 6,796 à 7,767 ; passage libre de z −3,466 à −1,282, soit 2,184 m |
| `2146` | La cloison nord-sud du bloc sanitaire, pleine sur toute sa longueur | `x >= 18.0 && x <= 19.0 && z >= -0.5 && z <= 10.5` | x de 8,738 à 9,223 ; z de −4,923 à 0,417 |
| `2151` | La cloison de lattes qui ferme le salon à l'ouest | `x > 2.5 && x < 4.45 && z < 5.6` | x de 1,214 à 2,160 ; z < −1,961 |
| `2156` | Le retour long du canapé d'angle | `x > 4.4 && x < 11.0 && z < 4.2 && z > 0.9` | x de 2,136 à 5,340 ; z de −4,243 à −2,641 |
| `2161` | Le retour court, jusqu'au mur sud | `x > 4.4 && x < 7.2 && z < 0.9` | x de 2,136 à 3,495 ; z < −4,243 |
| `2166` | La table basse devant le canapé | `x > 7.7 && x < 11.0 && z < 0.2 && z > -3.05` | x de 3,738 à 5,340 ; z de −6,161 à −4,583 |
| `2171` | La table de billard, hitbox rectangulaire alignée sur les axes | `x <= -5.0 && x >= -13.5 && z <= 2.5 && z >= -2.5` | x de −6,553 à −2,427 ; z de −5,894 à −3,466 |
| `2176` | Les murs sud et nord des toilettes | `x > 15.0 && (z < -2.6 || z > 12.0)` | x > 7,282 ; bloque z < −5,942 et z > 1,145 |
| `2181` | La cloison nord-sud qui ferme les cabines : on ne peut jamais y entrer | `x > 21.0 && z < 9.0 && z > 0.5` | x > 10,194 ; z de −4,437 à −0,311 |
| `2186` | La cloison est-ouest entre les deux blocs | `x > 18.0 && z > 4.5 && z < 5.5` | x > 8,738 ; z de −2,495 à −2,010 |
| `2191` | Le vantail mobile : porte fermée son seuil est à x = 18,0 ; porte ouverte à x = 20,8 | `z < 9.1 && z > 8.2 && x > toiletteFemme.x - 2.2` | bande z de −0,699 à −0,262 ; seuil x = 8,738 fermée, 10,097 ouverte |
| `2196` | Idem, côté hommes | `z < 1.4 && z > 0.4 && x > toiletteHomme.x - 2.2` | bande z de −4,486 à −3,995 ; seuil x = 8,738 fermée, 10,097 ouverte |
| `2203` | Le mur sud du hall | `z < -4.5` | z < −6,865 |
| `2206` | Le fond du sas, derrière la porte extérieure | `z > 35.2` | z > 12,407 |
| `2209` | Le mur ouest du hall | `x < -14.0` | x < −6,796 |
| `2212` | Le mur est du bloc sanitaire | `x > 22.8` | x > 11,068 |

Deux remarques sur la fidélité de cette fonction à la géométrie :

- Les deux droites des pans coupés (`−0,4736842105·z + 19,36842105`) ne sont pas celles du modèle. La géométrie donne une pente de −0,4606 et passe par (12,950 ; 14,727) ; la collision passe 0,27 m plus à l'intérieur au sud et 0,33 m plus à l'intérieur au nord. Le joueur ne touche donc jamais le pan coupé.
- La zone qui **remonte la caméra de 0,291 m** au-dessus de l'estrade (`legacy/room/room.c:2062`) couvre x −1,456 à +1,019 alors que l'estrade occupe x −1,214 à +1,214. Elle déborde de 0,242 m à l'ouest et manque 0,195 m à l'est : on monte sur l'estrade côté est sans que la caméra se lève.

---

## 4. Les ouvertures

### 4.1 Hall ↔ sas d'entrée

- **Où** : Trouée rectangulaire percée dans le pan coupé nord-est, avec jambages de 0,146 m et linteau.
- **Unités** : z 17,148 à 19,868 ; x 11,835 à 10,582 sur le parement intérieur ; hauteur 0,101 à 4,901
- **Mètres** : début 5,745 , 0,000 , 3,645 m ; fin 5,137 , 2,330 , 4,965 m ; largeur sur la diagonale 1,453 m ; hauteur 2,330 m
- **Preuve** : `salle.obj`, faces l644 / l670 / l696 / l697 de `MUR_SAL_Cube.002` — elles ont six sommets chacune, et c'est la trouée qui les leur donne ; le linteau et les jambages sont les faces l731 à l734, matériau `haut_mur`.
- **Collision** : `legacy/room/room.c:2116` et `:2119` ne laissent passer que la bande z 3,816 à 4,641 m, soit 0,825 m mesurés en z, contre 1,320 m en z (1,454 m sur la diagonale) pour la trouée géométrique.

### 4.2 Hall ↔ bloc sanitaire

- **Où** : Baie sur toute la hauteur dans le mur est du hall.
- **Unités** : z 1,254 à 8,295 ; épaisseur x 14,727 à 15,000 ; pleine hauteur
- **Mètres** : début 7,149 , 0,000 , −4,071 m ; fin 7,282 , 2,879 , −0,653 m ; largeur 3,418 m ; hauteur 2,879 m
- **Preuve** : `salle.obj`, faces l643 et l639 (les retours de mur qui s'arrêtent à z = 1,254 et reprennent à z = 8,295) et l714 / l719 (le parement x = 15,000 côté sanitaires).
- **Collision** : `legacy/room/room.c:2141` ne laisse passer que z 2,5 à 7,0 unités, soit 2,184 m, centrés sur −2,374 m ; la baie géométrique, elle, est centrée sur −2,362 m. Le joueur dispose donc de 1,23 m de moins que ce qu'il voit.

### 4.3 Corridor sanitaire ↔ toilettes HOMMES

- **Où** : Contournement par le SUD de la cloison x = 9,0. Il n'y a pas de porte : la cloison s'arrête avant le mur.
- **Unités** : z de −2,995 (parement sud) à −0,150 (about de la cloison)
- **Mètres** : z début −6,134 m ; z fin −4,753 m ; largeur 1,381 m
- **Preuve** : `salle.obj`, faces l706 (about sud de la cloison, z = −0,150) et l715 (parement sud du bloc, z = −2,995).
- **Collision** : `legacy/room/room.c:2146` (mur x 18…19 pour z −0,5…10,5) et `:2176` (z < −2,6 bloqué) laissent z de −5,942 à −4,923 m, soit 1,019 m.

### 4.4 Corridor sanitaire ↔ toilettes FEMMES

- **Où** : Contournement par le NORD de la même cloison, symétrique du précédent.
- **Unités** : z de 9,950 (about de la cloison) à 12,799 (parement nord)
- **Mètres** : z début 0,150 m ; z fin 1,533 m ; largeur 1,383 m
- **Preuve** : `salle.obj`, faces l704 (about nord, z = 9,950) et l717 (parement nord, z = 12,799).
- **Collision** : `legacy/room/room.c:2146` et `:2176` laissent z de 0,417 à 1,145 m, soit 0,728 m.

### 4.5 Porte coulissante FEMMES

- **Où** : Vantail de 1,456 m qui coulisse vers l'est de 1,359 m et se loge dans un coffre.
- **Unités** : vantail x 18,7…21,7 fermé, 21,5…24,5 ouvert ; z 8,60…8,70 ; hauteur −0,05…3,85
- **Mètres** : z −0,481 m ; x porte fermée 9,078 , 10,534 m ; x porte ouverte 10,437 , 11,893 m ; hauteur 1,869 m ; course 1,359 m
- **Preuve** : `legacy/room/room.c:162` (`toiletteFemme`), `:1591` (`GlDessinerQuad`, qui centre le quad sur `x` et l'étend de ±largeur/2), `:2977` (la course s'arrête à x ⩾ 23,0). Le coffre est modélisé : `Cube.012_Cube.005`, `Cube.015_Cube.009`, `Cube.016_Cube.022`.
- **Collision** : `legacy/room/room.c:2191`. Porte ouverte, le passage utile ne fait que 0,874 m (x 9,223 à 10,097 m) : au-delà de 10,194 m la cloison de fond bloque de toute façon.

### 4.6 Porte coulissante HOMMES

- **Où** : La même, symétrique, au sud.
- **Unités** : z 1,00…1,10 ; reste identique
- **Mètres** : z −4,170 m ; x porte fermée 9,078 , 10,534 m ; x porte ouverte 10,437 , 11,893 m ; hauteur 1,869 m ; course 1,359 m
- **Preuve** : `legacy/room/room.c:163` ; coffre `Cube.019_Cube.025`, `Cube.018_Cube.024`, `Cube.017_Cube.023`.
- **Collision** : `legacy/room/room.c:2196`, mêmes valeurs.

### 4.7 Allée centrale de l'îlot de bornes

- **Où** : Entre les deux groupes de trois, sur toute la profondeur du lot.
- **Unités** : x −1,4 à 1,4 ; z 8,5 à 13,5
- **Mètres** : x début −0,680 m ; x fin 0,680 m ; z début −0,553 m ; z fin 1,874 m ; largeur 1,360 m
- **Preuve** : `legacy/room/room.c:2124` : le lot est bloqué SAUF cette bande. Le panneau HARD/EASY est suspendu exactement au-dessus, x −0,469 à 0,501.
- **Collision** : Aucune : c'est le seul passage nord-sud au centre du hall, et il est large de 1,36 m.

### 4.8 Tour du comptoir

- **Où** : De part et d'autre du bloc d'accueil, jusqu'au mur nord.
- **Unités** : comptoir bloqué x −6,0…6,0 pour z 19,5…25,0 ; mur arrière bloqué x −9,0…9,0 pour z 23,8…24,5
- **Mètres** : passage ouest x −6,796 , −2,913 m ; passage est x 2,913 , 5,485 m ; jusqu'à z 6,868 m
- **Preuve** : `legacy/room/room.c:2099` et `:2102`.
- **Collision** : On peut passer derrière le comptoir par l'ouest (3,88 m de large) ou par l'est (2,57 m) et remonter jusqu'à z = 6,868 m, où le mur arrière arrête.

Autrement dit, en 2020 on entre dans le hall par **un seul point** (la trouée du pan coupé nord-est, 1,45 m), on passe d'une moitié à l'autre de l'îlot de bornes par **une seule allée** (1,36 m), on entre dans les sanitaires par **une seule baie** (3,42 m géométriques, 2,18 m praticables), et chacun des deux blocs de toilettes se referme derrière **une porte coulissante** qu'il faut ouvrir.

---

## 5. Les seize bornes

`NB_BORNES` vaut 16 (`legacy/room/room.c:172`) et l'énumération de `legacy/room/room.c:171` en nomme treize ; les rangs 13, 14, 15 sont des `COMMING SOON` (`legacy/room/room.c:1285-1287`) et le rang 16 est le classement. `detecterMachine` (`legacy/room/room.c:2307`) renvoie un code de 1 à 16, et le code moins un indexe `adresseFontImg`, `lancerJeu` et `cible` (`legacy/room/room.c:2701` et `:2703`).

**La règle de difficulté est spatiale et sans exception : les six bornes HARD sont à x < 0, les six EASY à x > 0.** Un panneau suspendu au-dessus de l'allée le dit — et il faut le dire précisément, parce que la mesure surprend : le **fond** rouge est du côté x > 0 (EASY) et le fond bleu du côté x < 0 (HARD), tandis que les **lettres** font l'inverse, rouges à x < 0 et bleues à x > 0. Chaque face porte les deux mots.

| N° | Jeu | Difficulté | Objet dans `salle.obj` | Caisson, x / z (m) | Écran, centre (m) | Cible caméra (m) | Zone de détection (m) + angle |
|---|---|---|---|---|---|---|---|
| 1 | FLAPPY BIRD | HARD | `FLAPPY_BIRD_HARD.009_Cube.062` | −3,631…−2,742 / 0,700…1,879 | −3,186 ; 1,607 ; 1,297 | −3,186 ; 1,594 | x −3,883…−2,767 ; z 0,660…2,602 ; angle π ±30° |
| 2 | TETRIS | HARD | `FLAPPY_BIRD_HARD.001_Cube.066` | −2,685…−1,796 / 0,700…1,879 | −2,240 ; 1,607 ; 1,297 | −2,239 ; 1,595 | x −2,767…−1,796 ; z 0,660…2,602 ; angle π ±30° |
| 3 | ASTEROID | HARD | `FLAPPY_BIRD_HARD.006_Cube.043` | −1,746…−0,857 / 0,700…1,879 | −1,301 ; 1,607 ; 1,297 | −1,301 ; 1,595 | x −1,796…−0,728 ; z 0,660…2,602 ; angle π ±30° |
| 4 | SHOOTER | HARD | `FLAPPY_BIRD_HARD.007_Cube.034` | −3,629…−2,740 / −0,501…0,678 | −3,185 ; 1,607 ; 0,081 | −3,184 ; −0,216 | x −3,883…−2,767 ; z −1,282…0,660 ; angle 0 ±30° |
| 5 | SNAKE | HARD | `FLAPPY_BIRD_HARD.008_Cube.068` | −2,690…−1,800 / −0,501…0,678 | −2,246 ; 1,607 ; 0,081 | −2,246 ; −0,216 | x −2,767…−1,796 ; z −1,282…0,660 ; angle 0 ±30° |
| 6 | DEMINEUR | HARD | `FLAPPY_BIRD_HARD.010_Cube.021` | −1,744…−0,854 / −0,501…0,678 | −1,300 ; 1,607 ; 0,081 | −1,300 ; −0,216 | x −1,796…−0,728 ; z −1,282…0,660 ; angle 0 ±30° |
| 7 | DEMINEUR | EASY | `FLAPPY_BIRD_HARD.013_Cube.059` | 0,868…1,757 / 0,700…1,879 | 1,313 ; 1,607 ; 1,297 | 1,312 ; 1,595 | x 0,728…1,650 ; z 0,660…2,602 ; angle π ±30° |
| 8 | SNAKE | EASY | `FLAPPY_BIRD_HARD.005_Cube.067` | 1,814…2,703 / 0,700…1,879 | 2,259 ; 1,607 ; 1,297 | 2,258 ; 1,594 | x 1,650…2,816 ; z 0,660…2,602 ; angle π ±30° |
| 9 | SHOOTER | EASY | `FLAPPY_BIRD_HARD.014_Cube.061` | 2,753…3,642 / 0,700…1,879 | 3,198 ; 1,607 ; 1,297 | 3,198 ; 1,594 | x 2,816…3,883 ; z 0,660…2,602 ; angle π ±30° |
| 10 | ASTEROID | EASY | `FLAPPY_BIRD_HARD.004_Cube.071` | 0,870…1,759 / −0,501…0,678 | 1,314 ; 1,607 ; 0,081 | 1,314 ; −0,217 | x 0,728…1,650 ; z −1,282…0,660 ; angle 0 ±30° |
| 11 | TETRIS | EASY | `FLAPPY_BIRD_HARD.003_Cube.069` | 1,809…2,698 / −0,501…0,678 | 2,253 ; 1,607 ; 0,081 | 2,252 ; −0,217 | x 1,650…2,816 ; z −1,282…0,660 ; angle 0 ±30° |
| 12 | FLAPPY BIRD | EASY | `FLAPPY_BIRD_HARD.002_Cube.070` | 2,755…3,644 / −0,501…0,678 | 3,199 ; 1,607 ; 0,081 | 3,199 ; −0,216 | x 2,816…3,883 ; z −1,282…0,660 ; angle 0 ±30° |
| 13 | COMING SOON | — | `FLAPPY_BIRD_HARD.011_Cube.056` | −7,211…−6,031 / −0,026…0,864 | −6,613 ; 1,607 ; 0,419 | −6,359 ; 0,417 | x < −5,340 ; z −0,311…0,782 ; angle 3π/2 ±30° — **renvoie 0** |
| 14 | COMING SOON | — | `FLAPPY_BIRD_HARD.012_Cube.057` | −7,211…−6,031 / 0,914…1,803 | −6,613 ; 1,607 ; 1,358 | −6,359 ; 1,359 | x < −5,340 ; z 0,782…1,874 ; angle 3π/2 ±30° — **renvoie 0** |
| 15 | COMING SOON | — | `FLAPPY_BIRD_HARD.015_Cube.072` | −0,445…0,445 / −5,134…−3,955 | 0,000 ; 1,820 ; −4,537 | 0,000 ; −4,282 | x −0,485…0,485 ; z −4,194…−3,223 ; angle π ±30° — **renvoie 0** |
| 16 | CLASSEMENT | — | `Cube.046_Cube.003` (+ `Cube.060`, `Cube.061`, `Cylindre.003`) | −0,412…0,412 / 5,091…5,184 | 0,000 ; 1,679 ; 5,137 | 0,000 ; 4,797 | x −0,485…0,485 ; z 4,301…4,786 ; angle 0 ±30° |

**Dimensions d'un caisson de 2020** : 0,889 m de large, 1,179 m de profond, **2,391 m de haut** (y −0,002 à 2,388), sous 2,879 m de plafond. La dalle d'écran mesure 0,778 × 0,440 m et est inclinée de 18,3°, centre à 1,607 m.

**Implantation de l'îlot** : entraxe 0,942 m dans une rangée, 1,201 m dos à dos, allée centrale de 2,613 m entre les deux groupes de trois. Les dos des deux rangées se touchent : 21 mm d'écart.

**La cible de cadrage** de chaque borne (tableau `cible[]`, `legacy/room/room.c:1738` à `:1859`) est le point où la caméra vient se poser quand on lance la partie : 0,297 m devant la dalle pour les douze bornes de l'îlot (0,25 à 0,34 m pour les quatre autres), à 1,704 m du sol. Elle est donc **à l'intérieur du caisson**, ce qui est normal — c'est un cadrage, pas une position jouable.

**Où se tient le joueur** : au **nord** de l'îlot (z 1,874 à 2,602) il regarde vers le sud et actionne les bornes 1, 2, 3, 7, 8, 9, dont les écrans sont tournés vers +z ; au **sud** (z −1,282 à −0,553) il regarde vers le nord et actionne les 4, 5, 6, 10, 11, 12, écrans vers −z. Les bornes 13 et 14 sont contre le mur ouest, écran vers l'est. La 15 est sur l'estrade, écran vers le nord. La 16 est un moniteur posé sur le comptoir.

---

## 6. Comparaison avec `assets/scene/salle.room.json`

### 6.0 Avertissement : le fichier a bougé pendant que je mesurais

`assets/scene/salle.room.json` est en cours de modification par un autre ingénieur. Au démarrage de ma session l'arbre de travail était propre au commit `be19850` ; il a changé au moins deux fois pendant mon relevé, et la refonte en cours touche précisément la coquille : les murs `coquille_sas` et `cloison_entree` ont été fusionnés dans `coquille`, et le pan coupé nord-est a été supprimé.

**Tout le § 6 est daté de l'état dont l'empreinte SHA-256 commence par `0066a9fba6492c43`** (`walls` = `coquille`, `coquille_sanitaires`, `cloison_toilettes` ; `playerStart` = (6,118 ; 11,95) ; `floors[sol_sas]` = 1,869 × 5,638 centré sur (6,1185 ; 10,019)). Les § 1 à 5, eux, ne dépendent que de fichiers figés et restent valables quoi qu'il arrive.

### 6.1 Ce qui est déjà juste, et qu'il ne faut pas toucher

- **Coquille du hall, sauf son angle nord-est.** Les lignes médianes du hall de 2020 tombent sur `walls[coquille]` à 1–4 mm près : x ±7,216 (fichier : ±7,215), z −7,138 / +7,132 (fichier : −7,135 / +7,135), retour de mur nord à z 2,535 (fichier : 2,539), départ du pan coupé ouest à x −6,329 (fichier : −6,329), extrémité mesurée à −4,194 (fichier : −4,203, symétrisé). Le pan coupé EST, mesuré à +4,212 / +6,329, n'est plus dans le fichier — voir l'écart correspondant au § 6.2.
- **Bloc sanitaire.** Intérieur mesuré x 7,279 à 11,951, z −6,134 à 1,533, soit 4,672 × 7,667 m = 35,82 m². `walls[coquille_sanitaires]` déclare 7,279 à 11,952 et −6,131 à 1,536. Écart maximal 3 mm.
- **Baie des sanitaires.** Trouée géométrique de 3,418 m entre z −4,071 et −0,653. Le fichier déclare 3,418 m entre −4,068 et −0,650, avec `head = 2,879` — la hauteur libre exacte de 2020.
- **Couloir d'entrée.** Intérieur mesuré 1,669 × 9,092 m, x 5,284 à 6,953, z 3,643 à 12,735. Le fichier lui donne exactement cette largeur (`walls[coquille]` passe par x 5,284 et 6,953) et le mène à z 12,738. Son sol est partagé entre `floors[sol_hall]` jusqu'à z 7,20 et `floors[sol_sas]` au-delà ; la somme couvre la même chose. Écart maximal 3 mm.
- **Les treize piliers.** Les treize `PILONNE` de 2020, convertis, tombent sur les treize `boxes[pilier_*]` à 5 mm près au maximum, section comprise : 0,4854 m mesurés, 0,486 déclarés. Douze sont engagés dans les murs, un est libre — celui du salon.
- **Les douze bornes de l'îlot.** Les douze centres convertis tombent sur `cabinets[borne_arcade_1..12]` à 3 mm près, orientation comprise : rangée nord écran vers +z (lacet 0), rangée sud écran vers −z (lacet 180). Entraxe 0,942 m dans une rangée, 1,201 m dos à dos, allée centrale de 2,613 m entre les deux groupes de trois.
- **Affectation des jeux.** Les douze jeux et leurs difficultés sont ceux de 2020, dans le même ordre et du même côté. Vérifié deux fois et indépendamment : par la texture d'écran de chaque caisson dans `salle.obj`, et par `detecterMachine()` croisée avec l'énumération de `legacy/room/room.c:171`.
- **Hauteur libre.** 2,879 m mesurés (sol en y = 0,100, sous-face de plafond en y = 6,030, unités). Le fichier déclare `head = 2,879` pour ses deux baies. `room.height` vaut 2,92, soit 4,1 cm de plus.

C'est le point important de ce relevé : **le pourtour du hall, le couloir d'entrée, le bloc sanitaire, les treize piliers et les douze bornes de l'îlot sont déjà le plan de 2020, au centimètre.** La plupart des écarts qui suivent portent sur ce qui remplit cette coquille, et non sur elle — la seule exception, et elle est de taille, est la suppression du pan coupé nord-est.

### 6.2 Écarts bloquants — ils changent la circulation ou l'usage

#### Cloison du salon

- **2020** — Une cloison de lattes de bois noir, x 1,513 à 1,805 m, z −7,098 à −2,721 m, montant de 0,513 à 2,093 m, doublée d'un muret plein de −0,385 à 1,071 m (x 1,432 à 1,869 ; z −7,004 à −2,635). Elle part du mur sud et se termine au nord par le seul pilier libre de la salle. La collision la double sur x 1,214 à 2,160 pour tout z < −1,961.
- **Aujourd'hui** — Rien. `boxes[pilier_salon]` est bien là, à 5 mm près, mais il ne tient plus rien.
- **Écart** — 4,38 m de cloison et 2,09 m de hauteur absents. Le salon n'est plus une pièce : c'est un coin de hall avec un poteau isolé au milieu.
- **Preuve** — `salle.obj` objets `PILONNE.005_Cube.044` (matériau `planche_mur`) et `Cube.013_Cube.029` ; `legacy/room/room.c:2151` ; `salle.room.json` `boxes[pilier_salon]`

#### Les trois cloisons intérieures du bloc sanitaire

- **2020** — TROIS cloisons, et non une : (a) x = 8,999 m, z −4,753 à 0,150, pleine, haute de 2,185 m ; (b) x = 10,507 m, z −4,139 à −0,509, haute de 1,847 m ; (c) z = −2,311 m, x 9,017 à 11,960, haute de 1,850 m. Les toilettes de 2020 sont QUATRE pièces : deux sas de lavabos et deux blocs de cabines, reliés par deux portes coulissantes.
- **Aujourd'hui** — Une seule, `walls[cloison_toilettes]` : x = 9,0, z −4,75 à 0,153 — c'est la (a), au centimètre. Les deux autres n'existent pas. Le bloc sanitaire est une seule pièce.
- **Écart** — Deux cloisons manquantes, de 3,63 m et 2,94 m de long, et les deux portes coulissantes de 1,456 m.
- **Preuve** — `salle.obj` `MUR_SAL_Cube.002` faces l701–l707 pour (a), l708–l713 et l721–l725 pour (b), l682–l684, l718, l726–l727 pour (c) ; `legacy/room/room.c:2181` et `:2186` ; `salle.room.json` `walls[cloison_toilettes]`

#### Estrade centrale, et la borne qui s'y pose

- **2020** — Estrade moquettée de 2,427 × 3,398 m, centrée sur (0 ; −4,680), haute de 0,228 m. La borne 15 (COMING SOON) est dessus, centrée sur (0 ; −4,544). Le moteur remonte la caméra de 0,291 m sur x −1,456…1,019 et z −6,379…−2,981.
- **Aujourd'hui** — `boxes[estrade]` : 2,40 × 2,40 m centrée sur (0 ; −3,100), haute de 0,12 m, donc z −4,30 à −1,90. `cabinets[borne_classement]` est à z = −4,541 avec y = 0,12.
- **Écart** — Centre décalé de 1,580 m vers le nord, profondeur réduite de 0,998 m, hauteur de 0,108 m. Conséquence directe : la borne de classement commence 0,241 m au SUD du bord de l'estrade et repose sur du vide sur les deux tiers de sa profondeur. LA CAUSE EST LISIBLE dans le fichier : son commentaire dit « Il faisait 7 x 5 m, ce qui était une scène et non un marquage ». 7 × 5 sont les cotes en UNITÉS BLENDER (z −3,5…3,5 et x −2,5…2,5) ; en mètres l'estrade de 2020 fait 2,427 × 3,398. Elle a donc été rétrécie pour corriger une erreur qui n'existait pas — c'est très exactement la classe de faute que ce relevé sert à fermer.
- **Preuve** — `salle.obj` `CENTRE_MACHINE_SOL_Cube` et `FLAPPY_BIRD_HARD.015_Cube.072` ; `legacy/room/room.c:2062` ; `salle.room.json` `boxes[estrade]`, `cabinets[borne_classement]`

#### Le poste de CLASSEMENT n'est pas au même endroit

- **2020** — La borne 16 est un MONITEUR POSÉ SUR LE COMPTOIR : dalle x −0,412 à 0,412, y 1,449 à 1,910, z 5,091 à 5,184, regardée depuis z 4,301 à 4,786, face au nord. La place (0 ; −4,544) était occupée par la borne 15, un caisson COMING SOON injouable.
- **Aujourd'hui** — `cabinets[borne_classement]` est un caisson d'arcade complet à (0 ; −4,541) : la place de la borne 15.
- **Écart** — 9,68 m d'écart. Le classement a quitté le comptoir pour le centre de la salle, et le comptoir n'a plus d'écran.
- **Preuve** — `salle.obj` `Cube.046_Cube.003`, `Cube.060_Cube.013`, `Cube.061_Cube.016`, `Cylindre.003` ; `legacy/room/room.c:2417` et `:1854` ; `salle.room.json` `cabinets[borne_classement]`

#### Le commentaire de `borne_classement` se trompe de borne

- **2020** — En (0 ; −4,541) et regardant +z, il y a la borne 15, un caisson COMING SOON dont `detecterMachine` renvoie 0 (`legacy/room/room.c:2409-2413`) : elle est injouable. Le CLASSEMENT est la borne 16 : un moniteur posé sur le comptoir, dalle centrée sur (0 ; 1,679 ; 5,137), détecté depuis z 4,301…4,786 face au nord (`legacy/room/room.c:2417`), et sa cible de cadrage `cible[15]` vaut (0 ; 4,797) — juste devant le comptoir. Il y a bien SEIZE bornes et QUINZE caissons : le seizième poste n'est pas un caisson, c'est un écran sur un meuble.
- **Aujourd'hui** — Le commentaire de `cabinets[borne_classement]` dit : « En 2020 il est en (0 ; -4,541) et regarde +Z ; `room.c` le declare comme la seizieme cible alors qu'il n'y a que quinze caissons. »
- **Écart** — Les deux moitiés de la phrase sont vraies séparément et fausses ensemble : (0 ; −4,541) est bien une position de 2020, mais c'est celle de la borne 15, pas du classement ; et le seizième rang n'est pas une incohérence du code, c'est le moniteur du comptoir. Tant que ce commentaire reste, la borne de classement restera au centre de la salle avec une justification historique qu'elle n'a pas.
- **Preuve** — `legacy/room/room.c:171` (l'énumération), `:1854` (`cible[15]`), `:2409-2413` (la borne 15 renvoie 0), `:2417` (la zone du classement) ; `salle.obj` `FLAPPY_BIRD_HARD.015_Cube.072` contre `Cube.046_Cube.003` ; `salle.room.json` `cabinets[borne_classement]`

#### Pan coupé nord-est supprimé — et la raison invoquée est fausse

- **2020** — Le pan coupé nord-est EXISTE dans le décor de 2020, et la porte du sas est une VRAIE trouée percée dedans, pas un trou dans la fonction de collision. Preuve : les quatre faces du mur diagonal (l644, l670, l696, l697 de `MUR_SAL_Cube.002`) ont SIX sommets chacune, et le sixième est là parce que la face contourne l'ouverture ; le tableau de la trouée est modélisé à part, faces l731 à l734, matériau `haut_mur` — deux jambages de 0,146 m et un linteau à 2,330 m. Le couloir de 2020 résout le problème géométrique ainsi : sa joue ouest suit le pan coupé, OBLIQUE, sur 1,32 m en z — de (5,284 ; 4,968) à (5,889 ; 3,643) en parements — avant de devenir parallèle à l'axe. Le couloir de 2020 est donc un contour polygonal ordinaire : SE (6,953 ; 3,643), NE (6,953 ; 12,735), NO (5,284 ; 12,735), puis l'oblique jusqu'à (5,889 ; 3,643), puis le mur sud jusqu'au SE. Largeur libre 1,669 m ; ses murs font 0,088 m, donc 1,756 m d'axe en axe — avec les 0,20 m du projet il faut 1,869 m d'axe en axe pour garder 1,669 m de libre.
- **Aujourd'hui** — `walls[coquille]` va tout droit de (−4,203 ; 7,135) à (5,284 ; 7,135) : le pan coupé est remplacé par un mur nord et un angle droit. Le commentaire qui accompagne le changement affirme qu'« en 2020 le problème existait déjà et l'auteur l'avait tranché dans la fonction de collision, en supprimant purement le mur diagonal entre z 17,5 et 19,2 — la porte était un TROU dans le code, pas dans le décor ».
- **Écart** — La prémisse est fausse, et c'est mesurable : le décor de 2020 a bien son trou, avec jambages et linteau, à l'endroit exact où la collision en ouvre un. Le hall perd donc un de ses deux pans coupés symétriques — 5,06 m de mur en diagonale — pour un motif qui ne tient pas. La contrainte de construction invoquée (une bouche de couloir orientée au nord ne peut rencontrer la diagonale qu'en un point) se lève exactement comme 2020 l'avait levée : en donnant à la joue ouest du couloir les 1,32 premiers mètres en oblique.
- **Preuve** — `salle.obj` `MUR_SAL_Cube.002` faces l644 / l670 / l696 / l697 (six sommets) et l731 à l734 (jambages et linteau) ; `Cube.048_Cube.006` faces l222621 et l222638 (les strips obliques du sol et du plafond du couloir) ; `salle.room.json` `walls[coquille].points`

### 6.3 Écarts visibles — le joueur les voit, la salle marche quand même

#### Panneau HARD / EASY

- **2020** — Un panneau suspendu au-dessus de l'allée centrale, x −0,469 à 0,501, y 2,087 à 2,476, z 0,631 à 0,709, pendu par une tige qui monte à 2,953 m. Il est bicolore et porte deux mots sur chacune de ses deux faces. MESURE : le fond ROUGE est du côté x > 0 (côté EASY) et le fond BLEU du côté x < 0 (côté HARD) ; les LETTRES font l'inverse, rouges à x < 0 et bleues à x > 0.
- **Aujourd'hui** — Absent. Aucun panneau, aucune couleur, aucune géométrie ne dit au joueur où est le côté difficile.
- **Écart** — L'unique signal spatial de la règle HARD/EASY a disparu. Et le champ qui aurait pu la porter ne la porte pas : `cabinets[].difficulty` vaut `normal` pour cinq des six bornes du côté EASY (4, 5, 6, 10, 11) et `easy` pour la seule 12.
- **Preuve** — `salle.obj` `PANNEAU_EASY/HARD_Cube.035`, `Texte.004` à `Texte.007`, `Cylindre.005` ; `legacy/room/room.c:171` ; `salle.room.json` `cabinets[4..12].difficulty`

#### Rangée de bornes du mur ouest

- **2020** — DEUX caissons COMING SOON, écran vers l'est, centrés sur (−6,621 ; 0,419) et (−6,621 ; 1,359), emprise 1,180 m en x sur 0,889 m en z. Leur détection est désactivée : `legacy/room/room.c:2401` et `:2403` renvoient 0.
- **Aujourd'hui** — SIX caissons, x = −6,48, z de −2,955 à 1,755 au pas de 0,942, jouant `pacman` et `piano`.
- **Écart** — Quatre bornes ajoutées ; les deux qui correspondent sont décalées de +0,395 m en z et +0,141 m en x.
- **Preuve** — `salle.obj` `FLAPPY_BIRD_HARD.011_Cube.056` et `.012_Cube.057` ; `legacy/room/room.c:2394-2405` ; `salle.room.json` `cabinets[13..18]`

#### Enseigne NINETEEN

- **2020** — Centrée sur x = 0, y 2,046 à 2,507, z 6,968 à 7,429 ; 2,546 × 0,461 m, texture `nineteen_name.jpg` émissive.
- **Aujourd'hui** — `props[enseigne_nineteen]` à (−2,60 ; 2,47 ; 7,05), panneau de 3,20 × 0,80 m.
- **Écart** — Décalée de 2,600 m vers l'ouest, élargie de 0,654 m. Elle n'est plus dans l'axe du comptoir, qui lui est resté sur x = 0.
- **Preuve** — `salle.obj` `Cube.059_Cube.007` ; `salle.room.json` `props[enseigne_nineteen]`, `props[bar_accueil]`

#### Comptoir d'accueil

- **2020** — 5,534 m en x sur 2,136 m en z, haut de 1,254 m, centré sur (0 ; 5,998), ADOSSÉ au mur nord : sa face arrière est en z 7,066, exactement le parement du mur. Trois panneaux publicitaires en façade, deux en joue.
- **Aujourd'hui** — `props[bar_accueil]` : un caisson de 5,40 × 0,72 m centré sur (0 ; 6,05).
- **Écart** — Profondeur réduite de 1,416 m, et il n'est plus adossé : il reste 0,62 m de vide entre son dos et le mur.
- **Preuve** — `salle.obj` `bar_accueil_Cube.030` et `Cube.047/039/032/035/042` ; `salle.room.json` `props[bar_accueil]`

#### Billard

- **2020** — Table ALIGNÉE SUR LES AXES du modèle : tapis x −6,294 à −2,671, z −5,664 à −3,724, soit 3,623 × 1,940 m, centre (−4,483 ; −4,694). Sa hitbox est un rectangle droit, x −6,553 à −2,427, z −5,894 à −3,466.
- **Aujourd'hui** — `props[billard]` à (−3,85 ; −4,75), lacet 12°.
- **Écart** — Centre décalé de 0,633 m vers l'est ; table pivotée de 12° alors que l'originale ne l'était pas.
- **Preuve** — `salle.obj` `Billiard_Table`, matériaux `Table_de_billard` et `contour_billard` ; `legacy/room/room.c:2171` ; `salle.room.json` `props[billard]`

#### Canapé et table basse du salon

- **2020** — Canapé d'angle en cuir rouge, emprise x 2,044 à 5,372, z −6,765 à −2,772, centre (3,708 ; −4,769) : il occupe l'angle sud-est du hall. Table basse x 3,882 à 5,339, z −6,007 à −4,550, centre (4,611 ; −5,279), plateau à 0,672 m.
- **Aujourd'hui** — `props[canape]` à (5,60 ; −1,60) lacet 90° ; `props[table_basse]` à (5,60 ; −3,75) lacet 12°.
- **Écart** — Canapé décalé de 1,892 m à l'est et 3,169 m au nord ; table basse de 0,989 m à l'est et 1,529 m au nord. Le salon a quitté son angle.
- **Preuve** — `salle.obj` `sofa_Cube.001`, `Cube.028_Cube.031`, `Cube.030_Cube.032`, `Cube.031_Cube.015` ; `legacy/room/room.c:2156`, `:2161`, `:2166` ; `salle.room.json` `props[canape]`, `props[table_basse]`

#### Postes de radio

- **2020** — TROIS, tous interactifs, chacun avec sa zone de détection : (−5,880 ; 1,294 ; −3,882) au coin billard, (−0,852 ; 1,587 ; 5,154) au comptoir, (5,178 ; 1,004 ; −4,895) au salon.
- **Aujourd'hui** — UN seul, `props[radio_murale]`, à (−7,06 ; 1,53 ; 1,90).
- **Écart** — Deux postes absents ; celui qui reste est à 5,90 m à plat du plus proche des trois emplacements de 2020.
- **Preuve** — `salle.obj` `Plane.098_Plane.110`, `.001`, `.002`, matériau `Material.254` / `Radio.jpg` ; `legacy/room/room.c:2218-2251` ; `salle.room.json` `props[radio_murale]`

#### Appliques murales

- **2020** — SIX. Quatre sur le mur sud à z = −6,874, x −4,782 / −1,653 / 1,652 / 4,809 ; deux sur le mur ouest à x = −6,924, z −3,973 et −0,670. Ampoule de 2,048 à 2,439 m.
- **Aujourd'hui** — `lights[neon_mur_ouest]` (x −6,95 ; z −4,0 / 0,0 / 4,0) et `lights[neon_mur_est]` (x 6,95 ; z −2,0 / 2,0 / 6,0).
- **Écart** — Les deux appliques ouest sont reprises à 0,03 m et 0,67 m près, avec une troisième en plus. Les QUATRE du mur sud n'ont pas d'équivalent. Les trois du mur est n'existaient pas en 2020.
- **Preuve** — `salle.obj` `sconce_02`, `.001` à `.005` ; `salle.room.json` `lights[neon_mur_ouest]`, `lights[neon_mur_est]`

#### Affiches du mur sud

- **2020** — CINQ cadres identiques, y 1,172 à 2,143, z −7,201 à −7,104, centrés sur x −6,057 / −3,389 / −0,054 / 3,297 / 6,010.
- **Aujourd'hui** — TROIS : `affiche_sud` (−3,20), `affiche_sud_b` (2,40), `affiche_sud_c` (4,60).
- **Écart** — Deux cadres absents (x −6,057 et x −0,054) ; les trois présents sont à 0,19 m, 0,90 m et 1,41 m de leurs homologues.
- **Preuve** — `salle.obj` `Cube.001` à `Cube.005_Cube.075` à `.079` ; `salle.room.json` `props[affiche_sud*]`

#### Affiches du mur ouest

- **2020** — DEUX cadres, x −7,190 à −7,093, y 1,172 à 2,143, centrés sur z −5,553 et −2,264.
- **Aujourd'hui** — `affiche_ouest` (z −2,60) et `affiche_ouest_b` (z +0,90).
- **Écart** — Le cadre nord est repris à 0,34 m près ; celui de z −5,553 est absent ; celui de z +0,90 n'a pas d'original.
- **Preuve** — `salle.obj` `Cube.044_Cube.004` et `Cube.045_Cube.010` ; `salle.room.json` `props[affiche_ouest]`, `props[affiche_ouest_b]`

#### Équipement des toilettes

- **2020** — DEUX plans de lavabo en marbre de 1,940 m contre le mur est : hommes z −6,150 à −4,209, femmes z −0,413 à 1,529, chacun avec DEUX robinets. DEUX appareils sanitaires blancs, x 9,385 à 10,147, l'un en z −3,235…−2,417 (hommes), l'autre en z −2,198…−1,379 (femmes). Deux boutons poussoirs de 0,121 × 0,243 m sur la cloison, centrés sur z −0,111 (femmes) et −4,525 (hommes), à 1,477 m du sol.
- **Aujourd'hui** — `props[lavabo_toilettes]` (un seul, 1,40 m, x 11,36…11,88 / z −3,00…−1,60), `props[robinet_toilettes]`, `props[cabine_toilettes]` (répété deux fois, à (9,60 ; −4,60) et (10,60 ; −4,60)), `props[porte_cabine_1]` et `[porte_cabine_2]`.
- **Écart** — Un plan de lavabo sur deux manquant, et celui qui reste est 2,88 m au nord du plan hommes de 2020. Les cabines ont reculé de 2,2 à 3,2 m vers le sud. Les deux boutons poussoirs, qui étaient le seul mode d'emploi des portes, n'ont pas d'équivalent.
- **Preuve** — `salle.obj` `Cube.022_Cube.012`, `Cube.027_Cube.052`, `Cube.023` à `Cube.026`, `Cube.165_Cube.011`, `Cube.129_Cube.039`, `Cube.020_Cube.008`, `Cube.021_Cube.001` ; `salle.room.json` `props[lavabo_toilettes]`, `props[cabine_toilettes]`

#### Hauteur du sas

- **2020** — Le sas est PLUS BAS que le hall : 2,331 m sous plafond contre 2,879 m. Sa trouée dans le pan coupé est fermée par un LINTEAU à 2,330 m.
- **Aujourd'hui** — `ceilings[plafond_sas]` est au même y = 3,1 que `ceilings[plafond]`, et la jonction hall/sas n'a aucune entrée dans `openings` : le passage monte jusqu'au plafond.
- **Écart** — 0,55 m de retombée perdus, et un linteau de 2,33 m qui n'existe plus.
- **Preuve** — `salle.obj` `Cube.048_Cube.006` (faces l222627 et l222642, plafond en y 4,898…5,103) et `MUR_SAL_Cube.002` faces l731–l734 ; `salle.room.json` `ceilings[plafond_sas]`, `walls[coquille]`

#### Une porte de toilettes que 2020 n'avait pas

- **2020** — La cloison x = 9,0 est PLEINE de z −4,753 à 0,150. On entre dans chaque bloc en la CONTOURNANT : 1,381 m de passage au sud, 1,383 m au nord.
- **Aujourd'hui** — `walls[cloison_toilettes]` porte une ouverture `porte_toilettes` de 0,95 m en z −2,45 à −1,50, EN PLUS des deux contournements, qui sont eux conservés au centimètre.
- **Écart** — Un troisième passage là où il n'y en avait que deux. La circulation des sanitaires n'est plus celle de 2020, même si leurs murs le sont.
- **Preuve** — `salle.obj` `MUR_SAL_Cube.002` faces l701 à l707 ; `legacy/room/room.c:2146` ; `salle.room.json` `walls[cloison_toilettes].openings`

### 6.4 Écarts cosmétiques, incohérences internes et défauts d'origine

#### Tracé des moulures

- **2020** — Sans objet : 2020 n'a ni plinthe, ni corniche, ni cimaise. Son habillage de mur est un empilement — soubassement de moquette noire de 0 à 1,056 m, fond sombre de 0,922 à 2,864 m, lattes de bois noir de 0,718 à 2,660 m devant ce fond, bandeau bordeaux de 1,896 à 2,867 m.
- **Aujourd'hui** — `mouldings` (plinthes, corniche, cimaise) suivent un contour qui passe par x = ±9,65 et z = ±7,20.
- **Écart** — Le mur du hall est à x = ±7,215 et z = −7,135 / +7,135. Les moulures sont donc 2,435 m HORS du bâtiment à l'est et à l'ouest, et 0,065 m hors au sud. Ce tracé est celui de l'ancienne emprise 19,3 × 14,4, restée dans la prose du fichier.
- **Preuve** — `salle.obj` (aucune moulure) ; `salle.room.json` `mouldings[*].points` comparés à `walls[coquille].points`

#### Zones sonores

- **2020** — Sans objet — 2020 n'en a pas — mais les objets qu'elles nomment sont mesurables : le billard occupe x −6,55…−2,43 / z −5,89…−3,47, les toilettes x 7,28…11,95 / z −6,13…1,53.
- **Aujourd'hui** — `soundZones[toilettes]` couvre x −9,55…−6,20 / z −7,10…−3,30, et `soundZones[coin_billard]` x −9,55…−5,40 / z −3,20…0,60.
- **Écart** — Les deux zones sont restées sur l'ancien plan : la zone « toilettes » est au sud-OUEST alors que les toilettes sont à l'est, 13,5 m plus loin ; la zone « billard » est 4,1 m au nord du billard actuel.
- **Preuve** — `salle.room.json` `soundZones[*]` comparés à `walls[coquille_sanitaires]` et `props[billard]`

#### Poutres de plafond

- **2020** — AUCUNE. Les seize matériaux nommés `poutre*` de `salle.mtl` servent tous des cadres d'affiche, sauf `marbre_poutre.001` qui sert le muret du salon. Vérifié objet par objet.
- **Aujourd'hui** — `boxes[poutre]` et `boxes[poutre_5]`, 14,2 m et 10,5 m de long, à y = 2,70.
- **Écart** — Invention de la reconstruction. Le fichier le dit déjà lui-même (`salle.room.json` ligne 2930) ; je le confirme par la mesure. Bonne pour le bâti, mais qu'on ne peut pas justifier par la fidélité.
- **Preuve** — `salle.mtl` (matériaux `poutre`, `poutre.001` à `.014`, `marbre_poutre.001`, `marbre_poutre.014`) et leurs porteurs dans `salle.obj`

#### La prose du fichier contredite par ses propres données

- **2020** — Sans objet.
- **Aujourd'hui** — Le champ `_plan` (`salle.room.json:7`) affirme que les toilettes sont « ici au sud-ouest sur 12,4 », que le bar est « ici à −2,60 », qu'il y a « ici deux [piliers], libres », et que l'emprise jouable « va de −9,65 à +9,65 et de −7,2 à +7,2 ».
- **Écart** — Les données du même fichier disent le contraire : toilettes à l'est sur 35,82 m² (`walls[coquille_sanitaires]`), bar à x = 0 (`props[bar_accueil]`), treize piliers dont douze engagés (`boxes[pilier_*]`), emprise jouable −7,115…11,85 et −7,035…12,64 (`room.playable`). La prose décrit un état antérieur au commit `90f3d9e`. Aucun effet sur le rendu — mais c'est exactement le genre de texte qui a produit l'inversion d'axes qu'on est en train de réparer.
- **Preuve** — `assets/scene/salle.room.json:7` comparé à `walls`, `boxes`, `props` et `room.playable` du même fichier

#### Point de départ du joueur

- **2020** — Les `#define` disent (6,068 ; 12,311), face au sud, dans le sas. Mais le binaire de 2020 ne démarre PAS là : `InitCamera` écrase ces valeurs sept lignes plus bas par px = −6,56, pz = 9,195632, angle = 0 — soit (−3,184 ; −0,216) face au NORD, c'est-à-dire exactement la cible de cadrage de la borne SHOOTER HARD, le nez sur l'écran.
- **Aujourd'hui** — `playerStart` = (6,118 ; 11,95), lacet 270°, donc face au sud — c'est-à-dire la valeur des `#define` reculée de 0,36 m pour dégager la capsule du joueur du vantail.
- **Écart** — Le fichier actuel reprend l'INTENTION de 2020 à 0,05 m près, et non son comportement. C'est le bon choix, mais il faut savoir que ce n'est pas ce que voyait le joueur de 2020 au lancement.
- **Preuve** — `legacy/room/room.c:96-98` (les `#define`) et `:1726-1731` (l'écrasement) ; `salle.room.json` `playerStart`

#### Objets de 2020 sans équivalent, et sans raison d'en avoir

- **2020** — Neuf cadres d'affiche en x 8,484…10,221 et z 3,581…12,014, plus un volume sans matériau en x 8,092…9,063 / z 3,632…4,953 : tous HORS du bâtiment, dans une bande à l'est du sas qu'aucun mur ne ferme. La dalle de plafond, elle, déborde de −10,387 à 16,530 en x et de −13,328 à 8,203 en z.
- **Aujourd'hui** — Absents.
- **Écart** — Aucun. Je les signale pour que personne ne les « restaure » : ce sont des décors laissés hors champ, pas une pièce perdue.
- **Preuve** — `salle.obj` `Cube.050` à `Cube.058`, `booleen.001_Cube.036`, `toit_Cube.033`

#### Un défaut de 2020 à ne pas reproduire

- **2020** — La tige du luminaire du billard (matériau `support_lampe_billard`) monte jusqu'à y = 4,487 m, alors que la sous-face du plafond est à 2,879 m. Elle traverse le plafond sur 1,61 m.
- **Aujourd'hui** — Sans objet.
- **Écart** — À signaler comme défaut d'origine, pas comme cote à reprendre.
- **Preuve** — `salle.obj` `Billiard_Table`, groupe `support_lampe_billard` (y 4,881 à 9,344 unités)

#### Zone de remontée de caméra décalée de son estrade

- **2020** — L'estrade occupe x −1,214 à 1,214 ; la zone qui remonte la caméra de 0,291 m occupe x −1,456 à 1,019. Elle déborde de 0,242 m à l'ouest et manque 0,195 m à l'est : on monte sur l'estrade côté est sans que la caméra se lève.
- **Aujourd'hui** — Sans objet : le moteur actuel ne fait pas de remontée scriptée.
- **Écart** — Défaut d'origine, mesuré pour mémoire.
- **Preuve** — `legacy/room/room.c:2062` comparé à `salle.obj` `CENTRE_MACHINE_SOL_Cube`

---

## 7. Ce que je n'ai pas pu établir

- **Le rôle exact de `Cube.165_Cube.011` et `Cube.129_Cube.039`.** Deux maillages blancs de 306 faces, sans texture (matériaux `Material.001` et `Material.004`), de 0,756 × 0,819 × 1,410 m, un par bloc de toilettes, posés au sol. « Cuvette et réservoir » est l'hypothèse la plus économique et je l'ai écrite comme telle, mais rien dans le fichier ne le nomme.
- **Le rôle de `Cube.023`, `Cube.024`, `Cube.025`, `Cube.026`.** Quatre petites boîtes de 0,311 × 0,141 × 0,058 m, sans matériau, en saillie du mur est au-dessus des plans de lavabo, deux par lavabo. Robinet ou distributeur : la géométrie ne tranche pas.
- **Le rôle de `booleen.001_Cube.036`** (x 8,092…9,063 ; z 3,632…4,953 ; hauteur 0,000…2,331), sans matériau et hors du bâtiment. Son nom suggère un reliquat d'opération booléenne. Je le signale, je ne l'interprète pas.
- **Le rôle des neuf cadres d'affiche hors bâtiment** (`Cube.050` à `Cube.058`). Ils forment un couloir parallèle au sas, à 1,45 m à l'est de lui, qu'aucun mur ne borde. Décor de fond, façade abandonnée, essai : je ne sais pas.
- **Quel mot est écrit sur quelle moitié du panneau HARD/EASY.** Les glyphes sont de la géométrie 3D (`Texte.004` à `Texte.007`), pas du texte. J'ai déduit HARD à x < 0 et EASY à x > 0 de l'implantation des bornes et de l'énumération de `room.c:171`, pas de la lecture des lettres.
- **L'inversion apparente des couleurs de ce même panneau.** Le fond rouge est mesuré du côté EASY et le fond bleu du côté HARD, alors que les lettres font l'inverse. Je n'ai pas de moyen de dire si c'est un choix de contraste ou une erreur de 2020.
- **Le cap des points de vue `captures` du fichier actuel.** Sous la convention du moteur (`room/room_camera.c:94`, `forward = (cos lacet, sin tangage, sin lacet)`), `entree` (lacet 270°) et `arrivee` (200°) visent bien ce que leur nom annonce, mais `bar` (0°), `sud` (0°), `borne` (180°), `billard` (180°) et `classement` (180°) visent à 90° de leur sujet — ils tomberaient juste avec un lacet augmenté de 90°. Je n'ai pas remonté toute la chaîne de consommation de ces valeurs, donc je pose le constat sans conclure.
- **Les dimensions des accessoires importés du fichier actuel** (`props[canape]`, `props[billard]`, `props[table_basse]` : des glTF externes). J'ai comparé leurs **positions** aux emprises de 2020, pas leurs volumes.
- **L'état du fichier `salle.room.json` au moment où vous lisez.** Il a changé deux fois pendant ma session, dont une refonte de la coquille. Le § 6 est daté de l'empreinte `0066a9fba6492c43` (voir § 6.0) et doit être revérifié avant d'agir. Les § 1 à 5 ne dépendent que de `salle.obj`, `salle.mtl` et `room.c`, qui sont figés.
- **Le pourquoi de l'écrasement du point de départ** (`legacy/room/room.c:1726-1731`). Le code place le joueur au démarrage nez sur l'écran de la borne SHOOTER HARD, en écrasant les `#define` qui l'envoyaient dans le sas. Cela ressemble à un raccourci de mise au point resté en place, mais c'est bien ce que faisait le binaire de 2020 ; je ne peux pas prouver l'intention.

---

*Manifeste lisible par machine : `docs/releve-2020.json`. Il contient les mêmes chiffres, plus la boîte englobante de chacun des 127 objets en unités et en mètres.*
