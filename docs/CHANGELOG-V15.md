# Journal de la reconstruction

Ce document dit ce qui tourne, ce qui reste, et ce qui a été appris en route. Il ne prétend pas
que le chantier est terminé.

---

## Ce qui tourne

### Build et portabilité
- Un seul arbre CMake, presets `linux-x64`, `linux-x64-asan`, `macos-universal`, `windows-x64`.
- SDL3 récupéré sur un tag épinglé ; dépendances header-only vendorées, donc build hermétique.
- Shaders GLSL compilés au build et **embarqués dans le binaire** : rien à retrouver à
  l'exécution. En SPIR-V pour Vulkan ; traduits en MSL par `tools/spv2msl` (sur SPIRV-Cross)
  pour Metal, aux emplacements de ressources exacts qu'impose SDL3.
- Le masque de formats présenté à `SDL_CreateGPUDevice` est **dérivé des blobs embarqués** :
  demander un backend qu'on ne sait pas alimenter est devenu impossible.
- CI GitHub Actions : matrice trois OS, tâche ASan+UBSan dédiée, tâche serveur Go avec
  PostgreSQL, `go vet` et `gosec`.
- Avertissements en erreur là où ils traduisent un vrai bug.

### Noyau moteur
- Journalisation à niveaux, sortie fichier, boîte de dialogue native sur assertion.
- Arènes mémoire et allocation tracée avec canaris.
- Horloge à pas fixe 120 Hz avec accumulateur et interpolation au rendu.
- Résolution de chemins par points de montage, refus des chemins remontants.
- Configuration persistante écrite atomiquement.
- Algèbre : matrices colonne-majeure en profondeur [0,1] avec reverse-Z, quaternions,
  intersections rayon/AABB et rayon/triangle partagées par le rendu, la collision et l'audio.
- PCG32 à graine explicite, sans biais modulo.

### Rendu
- G-buffer sur trois cibles, normales encodées en octaédrique.
- Éclairage différé Cook-Torrance, 64 sources.
- Occlusion ambiante en espace écran, échantillonnage hémisphérique cosinus.
- Ombres, illumination globale à un rebond et réflexions **lancées en compute** sur le BVH de
  la salle, avec accumulation temporelle et débruitage à-trous guidé par la géométrie.
- Halo séparable sur cinq niveaux, tone mapping ACES, brouillard, vignettage, grain.
- Élimination par frustum, regroupement des liaisons de texture par matériau.
- Visualisation des cibles intermédiaires en ligne de commande.

### Assets
- `obj2gltf` : la salle Blender vers glTF 2.0, avec conversion Blinn-Phong vers
  metallic-roughness adaptée à l'exporteur détecté, génération des tangentes, déduction des
  sources lumineuses par aire émissive, bibliothèque de matériaux par famille de texture.
- `texgen` : normal map, occlusion de cavité et rugosité dérivées des textures diffuses
  d'origine.
- `bvhbake` : BVH par découpage SAH, partagé entre rendu, collision et audio.

### Joueur
- Position des pieds et position de l'œil distinguées ; `eye_height` est enfin lue.
- Collision en capsule balayée contre la géométrie réelle, avec gravité, glissement le long des
  murs, **hauteur de marche** (une plinthe de trois centimètres n'arrête plus personne) et
  vérification du plafond au saut.
- Accroupissement avec contrôle de dégagement : on ne se relève pas sous une table.
- Oscillation de marche pilotée par la **distance réellement parcourue** — pas par le temps, pas
  par la vitesse demandée. Impulsion à l'atterrissage, roulis en virage, respiration à l'arrêt.
- Tout l'état d'animation est interpolé au rendu, et l'oscillation est appliquée *après*
  l'interpolation : échantillonnée à 120 Hz puis interpolée linéairement, une sinusoïde perd ses
  sommets.
- `roomgen` : la salle **générée depuis sa description** (`assets/scene/salle.room.json`), en
  mètres, objet par objet. Coquille avec baies, sols, plafond en dalles de 0,60 m sur rails en T,
  piliers, poutres, plinthes, corniche, cimaise, nez de marche. Il écrit aussi les lumières et le
  fichier de scène : rien n'y est déduit. Un matériau inconnu, une texture absente, un budget
  dépassé ou une baie incohérente cassent le build en nommant le coupable.
- `geo_mesh` / `geo_shapes` : le substrat — soudure, normales lissées pondérées par l'aire,
  tangentes de Lengyel, boîte chanfreinée, plan, panneau, extrusion de profil mitrée, pan de mur
  percé **sans aucune opération booléenne**. 114 vérifications, plus huit refus prouvés chacun dans
  son propre processus.

### Serveur et site
- Binaire Go unique, front et migrations embarqués, PostgreSQL.
- Argon2id, sessions opaques stockées hachées, limitation de débit, CSRF, en-têtes stricts.
- Autorité serveur sur les scores : journal de partie scellé par HMAC, score recalculé,
  invariants par jeu. Seize scénarios d'attaque couverts par des tests, et vérifié de bout en
  bout contre une vraie base.
- Site refait, direction artistique empruntée au décor du jeu, polices du jeu.
- `docker-compose` avec base non exposée, image distroless, conteneur en lecture seule.

### Documentation
- Audit du code d'origine : 22 constats, dont un prouvé sous AddressSanitizer et trois
  hypothèses explicitement écartées.

---

## Ce qui reste

- **Portage des mini-jeux.** Les huit jeux tournent encore sur le code de 2020 dans `legacy/`.
  Le travail est mécanique — aucun n'utilise OpenGL, tous passent par `SDL_Renderer` et
  partagent une signature d'entrée presque commune — mais il représente 12 000 lignes de
  gameplay. La couche `engine/sprite/` qui les recevra reste à écrire.
- **Audio spatialisé.** miniaudio est vendoré et l'occlusion par lancer de rayon sur le BVH est
  écrite côté CPU **et maintenant testée** (`tests/test_bvh.c`) ; le mixage positionnel, les bus
  et la réverbe par zone ne sont pas encore branchés.

  *Historique de cette ligne :* elle a d'abord annoncé la fonction comme « testée » alors qu'aucun
  test de BVH n'existait, puis a été corrigée pour dire le contraire. Les tests existent depuis le
  palier joueur : `ns_bvh_raycast`, `ns_bvh_occluded`, `ns_bvh_occlusion_factor` et
  `ns_bvh_move_capsule` sont vérifiés sur un BVH construit à la main, sans fichier ni GPU.
- **Écrans de bornes en direct.** L'infrastructure est là (les jeux sauront dessiner dans une
  texture cible, les bornes ont déjà leur écran repéré et leur lumière colorée) mais les écrans
  affichent encore une texture fixe.
- **Interaction.** Les bornes et les trois lieux du décor — billard, canapé, bar — sont
  détectés et exposés par le moteur ; l'invite d'interaction et l'entrée dans un jeu ne sont pas
  câblées.
- **Paquets de release.** Le workflow de compilation existe ; celui qui produit AppImage, `.dmg`
  et `.msi` signés reste à écrire.
- **Compression des textures.** Les cartes générées sont des PNG (215 Mio au total). Un passage
  en KTX2/BC7 diviserait ça par cinq et accélérerait le chargement.

---

## Ce que la reconstruction a appris

Douze défauts trouvés en chemin, tous instructifs.

**Le garde-fou d'une arène a rapporté plus qu'un débogueur.** Le chargeur de scène dupliquait le
tampon de sommets une fois par primitive, parce que le glTF partage un seul jeu d'accesseurs
entre ses 183 primitives : 96 067 sommets devenaient 17,5 millions. L'arène a refusé net une
allocation de 843 Mio au lieu de la servir, et a nommé le coupable dans le message d'erreur.

**Un écran noir peut venir de six endroits, et les regarder un par un est le seul moyen
fiable.** La passe d'éclairage échantillonnait une cible de réflexions que rien n'avait jamais
écrite. Elle contenait de la mémoire GPU arbitraire, donc des NaN, qui traversaient toute la
couleur et ressortaient en noir après le tone mapping. C'est la visualisation des cibles
intermédiaires — un G-buffer parfait, un HDR rempli de bruit — qui a isolé le problème en deux
minutes.

**Une formule de conversion « standard » peut être exactement fausse.** La rugosité des 120
matériaux était dérivée de l'exposant spéculaire par l'équivalence Blinn-Phong/GGX classique.
La distribution des valeurs du fichier — 101 matériaux à exactement 225, maximum à 900 —
révélait un export Blender, dont l'exporteur écrit `Ns = (1 - rugosité)² × 900`. L'inversion est
donc exacte, et la formule générique donnait 0,09 là où l'auteur avait mis 0,5 : tous les murs
en miroir.

**L'unité d'une intensité lumineuse n'est pas un détail.** L'atténuation en 1/d² sans
normalisation sphérique ni rayon de source donnait plusieurs centaines de fois l'exposition
correcte près d'une applique. Les hautes lumières étaient brûlées, et aucun réglage
d'exposition ne pouvait le rattraper.

**Le nom d'un fichier est une donnée.** Le MTL ne distingue pas une moquette d'un carrelage : tout
sortait à la rugosité 0,5 par défaut. Mais l'auteur avait nommé ses textures `moquette`, `bois`,
`marbre`, `carllage_toilette`, `cuir_rouge`, `pilonne_rouge`. C'est la meilleure information
disponible, et elle suffit à donner à chaque famille une rugosité et une métallicité plausibles.

**Un rayon de source est une propriété du luminaire, pas du moteur.** Suite du point sur l'unité
d'intensité, et celui qui a coûté le plus d'allers-retours. Le rayon qui borne la décroissance en 1/d² était
une constante globale de 0,22 m — l'ampoule d'une applique. Un pavé lumineux de faux plafond fait
1,20 m : traité comme une ampoule, il portait 3 600 fois plus d'énergie sur la dalle à 5 cm que sur
le sol à 3 m. Toutes les valeurs d'intensité essayées donnaient soit un plafond carbonisé, soit un
sol noir, et souvent les deux. Aucun réglage ne rattrape un rapport de 1 à 3 600 : c'était le
modèle de source qu'il fallait corriger. Le champ tenait dans le `_pad[2]` que la structure
traînait déjà, donc sans changer un seul octet de disposition.

**Un albédo se mesure avant de s'expliquer.** La salle reconstruite est restée noire une bonne
demi-heure, et l'explication cherchée était l'éclairage. Une vingtaine de lignes de C qui moyennent
les pixels ont donné la vraie : `moquette_noire.jpg` a un albédo de 10/255, soit 4 %. Le plus
grand plan de la salle était un piège à lumière. `moquette.jpg`, dans le même dossier depuis 2020,
en a 47 — et c'est en plus le motif bordeaux d'une vraie salle d'arcade.

**Une police peut mentir sur ce qu'elle sait dessiner.** `sega.ttf` déclare les codes des
caractères accentués, mais leurs glyphes sont vides : « rallumée » s'affichait « rallum e ». Il
a fallu inspecter la table `loca` de chaque police pour savoir laquelle dessine réellement quoi.
Elle rend aussi son `E` d'une façon qui se lit comme un `C` — « CLASSEMENT » devenait
« CLASSCMCNT ».

**Un build vert ne prouve que ce qu'il exécute.** Le jeu n'avait jamais démarré sur macOS. Le
build y passait depuis deux mois, parce que la CI n'y lançait que `ctest -R core`, un test sans
GPU — et l'étape s'appelait honnêtement « hors rendu ». Le binaire n'était exécuté sur aucune
plateforme sauf Linux. La cause tenait en une ligne : le moteur annonçait à SDL savoir produire
du SPIR-V, du DXIL **et** du MSL, alors que le build ne produisait que du premier. SDL choisit
son backend d'après cette annonce : il rendait donc un périphérique Metal parfaitement valide,
puis refusait les seize shaders l'un après l'autre. Une option `NINETEEN_SHADERCROSS` existait,
était forcée à ON sur Apple, et n'était lue par rien. La correction qui compte n'est pas la
traduction MSL : c'est que le masque de formats soit maintenant **calculé à partir des blobs
réellement embarqués**, ce qui rend la faute impossible plutôt que corrigée.

**Une fonction sans appelant est une fonction sans preuve.** `ns_bvh_move_capsule` existait
depuis M5, avec des commentaires soignés, et n'a jamais été appelée : le joueur traversait les
murs et volait. En la branchant, trois défauts sont apparus d'un coup. Elle renvoyait la position
des *pieds* alors que la caméra tient celle de l'*œil* — un mètre soixante-dix d'écart. Elle
n'avait aucune notion de hauteur de marche, donc une plinthe de trois centimètres arrêtait net.
Et son recollement au sol, appliqué sans regarder le signe du déplacement vertical, annulait la
première fraction de seconde de tout saut : sauter n'aurait « rien fait », sans message. Aucun de
ces trois-là n'était visible à la lecture.

**Ce qui pilote une animation compte autant que sa forme.** L'oscillation de marche était d'abord
pilotée par la vitesse *souhaitée*. Un test l'a prise en défaut sans le chercher : plaqué contre un
mur, le joueur continuait de dodeliner comme s'il avançait. La corriger pour suivre le déplacement
*réellement effectué* règle le cas du mur, celui de la marche gravie, et d'avance celui des bruits
de pas — qui, branchés sur la même phase, auraient sinon résonné dans le vide.

**Traduire un shader révèle ce qu'il n'utilise pas.** En passant `raytrace.comp` en MSL,
l'outil a refusé de continuer : le set 0 avait un trou au binding 2. L'albédo du G-buffer y était
déclaré, lié à chaque image par le moteur, et jamais échantillonné — le rebond indirect se colore
avec l'albédo du matériau du BVH. L'optimiseur SPIR-V supprimait donc la déclaration. En SPIR-V
cela ne coûtait qu'une texture liée pour rien ; en MSL, un emplacement mort décale tous les
suivants. La correction remonte jusqu'aux numéros de binding des quatre tampons de stockage, qui
suivaient les textures et devaient reculer d'un cran — sur Vulkan aussi.
