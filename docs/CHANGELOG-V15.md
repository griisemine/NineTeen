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

### Atmosphère
- **Ombres par lumière.** Le lancer de rayons retient les **quatre sources** qui contribuent le
  plus à chaque pixel et écrit leur visibilité exacte ; les autres gardent la moyenne pondérée.
  Un point à l'ombre d'un pilier ne perd plus la lumière des écrans de bornes.
- **Brouillard volumétrique** en compute, à demi-résolution : marche le long du rayon de vue,
  phase de Henyey-Greenstein, densité bruitée, occultation par le BVH un pas sur quatre.
  Recomposé par une remontée **guidée par la profondeur**, entre l'éclairage et le halo.
- **Adaptation d'exposition** : luminance logarithmique moyenne mesurée en compute dans un
  tampon de stockage persistant, lissage asymétrique (l'œil s'habitue plus lentement au sombre).
  C'est ce qui a supprimé le plafond brûlé.
- **Réverbération par zone.** La salle déclare ses zones dans `salle.room.json` — les toilettes
  carrelées, le sas d'entrée, le coin billard — avec leur niveau d'écho et leur décroissance.
  `roomgen` les valide et les exporte, le mixeur les applique en **départ/retour** sur les bus
  SFX et AMBIENCE, et l'on passe de l'une à l'autre par amortissement plutôt que par saut : le
  temps de franchir une porte. La musique reste sèche.
  `docs/audio-pas-sec.wav` et `docs/audio-pas-toilettes.wav` sont le même pas rendu hors ligne
  dans les deux espaces — le sec retombe au silence en 0,5 s, les toilettes tiennent jusqu'à
  1,1 s.
- L'émissif de complaisance du plafond est **retiré**. Ce qui le rend lisible n'est pas le
  rebond du sol — l'illumination indirecte ne tourne qu'au palier « ultra » — mais la diffusion
  volumétrique devant lui et l'exposition qui cesse de le brûler. Le plan annonçait la première
  explication ; la capture a donné la seconde.

### Les jeux
- **Une interface commune** (`games/games.h`) : un jeu déclare la taille de son état, ses
  planches, ses trois sons, son vocabulaire d'événements et sept fonctions. Ce n'est pas un
  moteur de jeu — un mini-jeu d'arcade est un état qu'on avance d'un pas fixe et qu'on dessine,
  et c'est la seule chose qu'on abstrait.
- **Flappy Bird**, aux cotes de 2020 au pixel près, avec sa physique réécrite en flottant.
- **Snake** — et ce n'est pas le Snake à cases : un serpent à **angle libre** qui tourne tant
  qu'on tient la direction, avec de la **digestion** (la bosse d'un fruit avalé descend
  visiblement le corps) et les **trente-deux objets** de 2020, table `FRUIT_PROPRIETES`
  recopiée intégralement. Le **hardcore est l'inverse du normal** : manger coûte cinq fois la
  valeur du fruit, et le score vient de ceux qu'on laisse pourrir. C'est la règle la plus
  surprenante de l'original, et elle est conservée.
- Tout le temps de 2020 est compté **en images à 30 Hz**. Chaque constante est convertie en
  secondes, sa valeur d'origine écrite à côté, et un test vérifie que le jeu se comporte
  pareil à 120 Hz et à 40 Hz.

### Réglages
- **Un menu dessiné**, ouvert par `Échap` : palier de qualité, échelle de rendu, densité de
  poussière, luminosité, les quatre volumes, sensibilité de la souris. Tout est écrit dans
  `settings.cfg` à la fermeture.
- Chaque ligne porte **le chiffre qui aide à choisir** : le coût relatif du palier (`x0.17` à
  `x3.92`, mesuré) et le pourcentage de pixels que l'échelle économise. Ils existaient déjà dans
  le journal et la documentation ; les laisser hors de l'écran revenait à demander d'essayer les
  cinq paliers à l'aveugle.
- **La salle continue de vivre derrière le voile** — c'est ce qui permet de juger un réglage
  pendant qu'on le change. Seul le joueur est figé, et une partie en cours est en pause.
- `Échap` ne quitte plus le jeu. Il fallait deux appuis, le premier relâchait la souris et le
  second fermait la fenêtre : perdre sa partie ainsi est un défaut, pas un raccourci.
- Changer de palier reprend les défauts du palier **sauf** les trois réglages qui ont leur propre
  ligne. `tests/test_menu.c` (27 vérifications) le tient : sans ça, régler la poussière puis
  changer de palier l'effacerait sans le dire.

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

### Jouer

- **Couche 2D.** `engine/sprite/` : 4 096 quads en 128 lots, mélange alpha, police 5x7 intégrée
  au binaire, cible redirigeable. C'est elle qui permet à un jeu de dessiner aussi bien dans la
  dalle d'une borne que sur tout l'écran.
- **Flappy Bird jouable**, porté depuis les 1 402 lignes de 2020. Les planches et les cotes de
  découpe sont celles d'origine au pixel près ; les cotes du terrain viennent de ses constantes
  (`DISTANCE_BETWEEN_OBSTACLE`, `DISTANCE_UNDER_OBSTACLE`, l'échelle 4). La physique, elle, est
  réécrite en flottant au pas fixe : l'original intégrait en nombre d'images, ce qui rendait la
  chute dépendante de la fréquence d'affichage.
- **Le jeu tourne DANS la borne**, derrière son verre bombé — courbure barillet, lignes de
  balayage, masque de phosphore, reflet de vitre —, et la tête reste libre. On peut se pencher,
  reculer, regarder la borne d'à côté pendant qu'on joue.
- **Les mains sur les commandes** : la gauche empoigne le manche, la droite couvre les boutons,
  l'index s'enfonce à chaque battement d'aile. Les trois ancres d'une borne — fente, boutons,
  sommet de la boule — sont déclarées par `roomgen` et visées par une IK à deux os avec
  correction du bout du doigt. Vérifié à 3 cm près par `tests/test_ik.c`.
- **La poussière** dans les faisceaux : éclairée par les lumières réelles de la salle, réponse au
  carré pour que le grain ne se voie que DANS la lumière, et champ proche seulement — au-delà de
  quelques mètres c'est le brouillard volumétrique qui a raison.
- **Classement local**, écrit atomiquement, sans jamais demander de compte. Le journal de partie
  est scellé au format exact du serveur (HMAC-SHA256, charge canonique), et
  `tests/test_scores.c` le confronte à des vecteurs produits par le code Go lui-même.
- **Cinq paliers de qualité chiffrés** (`potato` à `ultra`), réglables en jeu par `F7`/`F8` et
  gardés d'une session à l'autre.
- **Du vrai mobilier** : `roomgen` sait instancier un glTF (`tools/geo_import.c`, sur le `cgltf`
  déjà vendoré). Tabourets de bar, canapé et extincteur viennent de modèles CC0 au lieu d'être
  des empilements de boîtes.

---

## Ce qui reste

- **Six des huit mini-jeux.** Flappy Bird et Snake sont portés et jouables, sur leur borne
  comme en plein écran. Tetris, Asteroid, Shooter, Démineur, Pac-Man et Piano tournent encore
  sur le code de 2020 dans `legacy/` — environ 8 000 lignes. Ajouter un jeu est désormais une
  ligne dans `games/games.c` : c'est ce que Snake a vérifié, et `room/main.c` n'a pas bougé
  d'une ligne pour l'accueillir.
- **Le transport réseau.** Le classement local marche, le journal de partie est scellé au format
  du serveur, la file d'attente sur disque existe et `--offline` est un verrou. Il manque la
  socket, délibérément : le temps réel et les duels se conçoivent avant de s'écrire. Le binaire
  n'importe **aucun** symbole réseau, et c'est vérifiable en une commande.
- **Un décimateur de maillage.** Les modèles CC0 sont taillés pour le cinéma — 14 000 triangles
  pour un tabouret. C'est ce qui limite aujourd'hui le mobilier importé à trois modèles :
  au-delà d'environ 160 000 sommets, le rasteriseur logiciel du conteneur de développement cesse
  de composer l'image finale, et je ne livre pas ce que je ne peux pas regarder.
- **Paquets de release.** Le workflow de compilation existe ; celui qui produit AppImage, `.dmg`
  et `.msi` signés reste à écrire.
- **Compression des textures.** Les cartes générées sont des PNG. Un passage en KTX2/BC7
  diviserait ça par cinq et accélérerait le chargement.

---

## Ce que la reconstruction a appris

Vingt défauts trouvés en chemin, tous instructifs.

**Sept bornes sur dix-neuf affichaient la partie d'une autre.** `roomgen` nomme les matériaux
par jeu — `ecran_snake` — et deux bornes du même jeu partageaient donc le même. Or le moteur
allume un écran vivant en surchargeant un MATÉRIAU : jouer sur la borne Snake normale faisait
apparaître la partie sur la borne Snake hard, à l'autre bout de la salle. Le défaut datait d'A4
et ne pouvait pas se voir tant qu'un seul jeu était porté — il fallait deux bornes du même jeu
dans le même cadre pour le rencontrer. La dalle d'une borne a maintenant son propre matériau,
cloné de celui qu'elle déclare : même image d'attente, surface distincte.

**Trois bogues dans un chemin qu'on croyait fini, tous trouvés en le généralisant.** En
extrayant l'interface commune des mini-jeux (`games/games.h`), il a fallu nommer les événements
qu'un jeu émet — et c'est là que ça s'est vu. (1) Le serveur Go **refuse sèchement** un événement
dont le nom n'est pas dans sa table ; le client émettait « flap », « score » et « death » quand la
table de Flappy n'accepte que « pipe ». Toute partie soumise était rejetée en bloc, et personne ne
le voyait : l'envoi est « au mieux, jamais bloquant », donc un refus ne remonte nulle part. (2) La
table du serveur se contredisait : elle limitait la fréquence de « flap » — donc l'attendait — tout
en refusant tout événement absent de son barème. (3) `flappy_tick` remettait ses drapeaux
d'événement à zéro EN TÊTE, alors que `flappy_flap` est appelée depuis le gestionnaire
d'événements, donc avant la boucle de pas fixe : le battement était effacé avant d'être lu. Le son
du battement n'a jamais été joué, l'index droit n'a jamais tapé sur le bouton, et « flap » n'est
jamais entré dans le journal. Trois tests neufs les tiennent maintenant, deux en C et un en Go,
parce que le vocabulaire est écrit dans les deux langages et que rien d'autre ne les tient
d'accord.

**Un preset qui ne construit pas ne dit rien, et personne ne s'en aperçoit.** Le preset
`linux-x64-asan` ne LIAIT pas : `ns_test_render` était la seule cible de test à ne pas appeler
`nineteen_apply_sanitizers`, alors qu'elle lie un moteur instrumenté. Une ligne. Elle a suffi à
ce que le tableau ASan reste incomplet sans que rien ne l'annonce — on construisait des cibles à
l'unité, jamais l'ensemble. Une fois la ligne ajoutée, UBSan a signalé du premier coup un
**dépassement d'entier signé dans le hachage de soudure des sommets** (`tools/geo_mesh.c`), écrit
deux fois : `(gx + dx) * 73856093` en `int` déborde dès 5 000 cellules de grille, soit un demi-
mètre. En pratique il bouclait et la soudure marchait — la salle générée est d'ailleurs restée
**identique octet pour octet** après correction. Mais le dépassement signé est un comportement
indéfini, que le compilateur a le droit d'exploiter : ça tient jusqu'au jour où une optimisation
le casse, et ce jour-là le symptôme serait une salle trouée sans message. Deux fuites de notre
côté sont tombées dans la même passe : la cible de rendu de la borne de classement, jamais
détruite depuis B10, et la texture blanche 1x1 du lot de sprites — `ns_texture_white` fabrique une
texture neuve à chaque appel malgré son nom. 3 919 octets à la sortie sont devenus 271, tous dans
l'énumération ALSA de SDL sur un conteneur sans carte son.

**Un test qui ne mesure que ce qu'on ajoute ne voit pas ce qu'on a cassé.** Le nœud d'écho de
miniaudio a d'abord été monté **en série** entre les bus et la sortie, sur une lecture erronée de
son API : son `dry` ressemble à un passage direct, et c'est en réalité le gain d'entrée dans la
ligne à retard — sa sortie vaut exactement `ligne × wet`. Un bus branché dessus perdait donc son
signal direct, et devenait entièrement muet à `wet = 0`, c'est-à-dire partout dans la salle. Le
test écrit en même temps ne mesurait que la **queue** après la fin du son : il passait, sur un
mixage devenu silencieux. Ce sont les deux tests d'atténuation par distance et d'occlusion, plus
anciens, qui sont tombés — et qui ont désigné le coupable. Le montage est maintenant un
départ/retour par séparateur, et le test vérifie **aussi que le son direct survit**.

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

**Un point de vue est une donnée qui se périme.** Deux captures de référence se sont
retrouvées *à l'intérieur* d'un meuble ajouté au palier suivant — l'allée dans une borne, puis
la vue du plafond dans la borne de classement. Le symptôme est un cadre noir, et rien ne le
dit : ni le build, ni le journal. La seconde fois, j'ai d'abord cru à une régression du
palier en cours. `roomgen` connaît à la fois l'emprise des meubles et la position des caméras :
il refuse maintenant, en nommant l'objet et en donnant les deux boîtes. La marge vaut le rayon
du corps du joueur — une caméra qui frôle une borne a déjà sa face avant en plein cadre.

**Le brouillard révèle que l'éclairage est chaud.** La première image volumétrique est sortie
entièrement blanche. L'éclairage d'une surface passe par un albédo divisé par pi — 0,25 pour un
mur clair, 0,01 pour la moquette noire ; la diffusion dans l'air n'a pas ce facteur. À intensité
égale, une source éclaire donc l'air plusieurs fois plus que le mur qu'elle éclaire, et 46
sources dans un hall de 22 m noient la salle dans son propre brouillard. Le coefficient qui
corrige cela est l'albédo de diffusion du milieu ; sa valeur vient de trois captures comparées,
et c'est dit comme tel dans le code plutôt que présenté comme une constante physique.

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
