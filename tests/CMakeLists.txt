# tests/ — vérifications automatisées.
#
# Deux familles :
#   * tests purement logiques (mathématiques, déterminisme, BVH) : rapides, sans GPU ;
#   * tests de rendu : exécutés en headless sur Vulkan logiciel (lavapipe), ils
#     produisent un PNG que l'on compare. C'est ce qui permet de vérifier le rendu
#     en intégration continue, sans écran ni carte graphique.

# Les emplacements de ressources du MSL, confrontés à ce que le moteur annonce à
# SDL. Le test ne demande ni GPU ni Mac : il lit le texte MSL embarqué dans le
# binaire. Il n'existe donc que là où du MSL est produit — sur macOS par défaut,
# et partout ailleurs avec -DNINETEEN_SHADERCROSS=ON, ce que fait la CI Linux
# pour que la traduction soit vérifiée à chaque commit.
if(NINETEEN_SHADERCROSS)
    add_executable(ns_test_msl test_msl.c)
    target_link_libraries(ns_test_msl PRIVATE nineteen::engine)
    nineteen_set_warnings(ns_test_msl)
    nineteen_apply_sanitizers(ns_test_msl)
    add_test(NAME msl COMMAND ns_test_msl)
endif()

# La collision et l'audio traversent le même BVH que le rendu. Aucune des quatre
# fonctions n'était testée — et le changelog affirmait pourtant que l'occlusion
# audio l'était. Le BVH est construit à la main ici : ni fichier, ni GPU.
add_executable(ns_test_bvh test_bvh.c "${CMAKE_SOURCE_DIR}/room/room_camera.c")
target_include_directories(ns_test_bvh PRIVATE "${CMAKE_SOURCE_DIR}/room")
target_link_libraries(ns_test_bvh PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_bvh)
nineteen_apply_sanitizers(ns_test_bvh)
add_test(NAME bvh COMMAND ns_test_bvh)

# La porte coulissante des toilettes : sa machine à états, et le fait qu'une
# porte fermée ARRÊTE le joueur.
#
# Ni GPU, ni fenêtre, ni fichier : `room_door.c` est du calcul pur, et il l'est
# précisément pour pouvoir être vérifié ici. `room_camera.c` vient avec, comme
# pour `ns_test_bvh`, parce que le dégagement de la capsule contre un obstacle
# hors BVH vit là-bas et qu'on le mesure par son vrai chemin plutôt qu'en le
# réécrivant dans le test.
add_executable(ns_test_door test_door.c
    "${CMAKE_SOURCE_DIR}/room/room_door.c"
    "${CMAKE_SOURCE_DIR}/room/room_camera.c")
target_include_directories(ns_test_door PRIVATE "${CMAKE_SOURCE_DIR}/room")
target_link_libraries(ns_test_door PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_door)
nineteen_apply_sanitizers(ns_test_door)
add_test(NAME porte COMMAND ns_test_door)

# La caméra de TROISIÈME PERSONNE : le bras qui balaie un volume, et
# l'effacement du personnage quand la caméra lui rentre dedans.
#
# Le défaut d'origine était une IMAGE — la tête du personnage au tiers gauche du
# cadre au démarrage — et aucun test ne remplacera le fait de l'ouvrir. Mais tout
# ce qui l'a produite est du calcul pur : une distance voulue, un obstacle, une
# distance rendue, une opacité. Ce test cloue ce calcul-là, y compris le cas qui
# justifie le faisceau de rayons — l'arête qu'un rayon unique manque — et le
# patinage des pieds, qui se mesure sur le vrai cycle du modèle.
#
# Il reçoit le répertoire d'assets, comme `ns_test_skin` : la partie « foulée »
# lit le personnage. Le reste tourne sans aucun fichier.
add_executable(ns_test_camera test_camera.c "${CMAKE_SOURCE_DIR}/room/room_camera.c")
target_include_directories(ns_test_camera PRIVATE "${CMAKE_SOURCE_DIR}/room")
target_link_libraries(ns_test_camera PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_camera)
nineteen_apply_sanitizers(ns_test_camera)
add_test(NAME camera COMMAND ns_test_camera "${CMAKE_BINARY_DIR}/assets")

# Le classement local et le journal de partie scellé.
#
# Le contrôle qui justifie ce test à lui seul : le format canonique et le sceau
# HMAC sont écrits DEUX FOIS — en Go dans `server/internal/runs/runs.go` et en C
# dans `engine/net/ns_runlog.c`. Une divergence d'un octet invaliderait toutes
# les parties, avec pour seul symptôme un « sceau invalide » côté serveur. Les
# vecteurs employés ici sont produits par le code Go lui-même.
# Il lie AUSSI `nineteen::games` : depuis que les jeux déclarent le vocabulaire
# d'événements qu'ils émettent, ce test confronte celui de chaque jeu porté à la
# table du serveur Go. Sans lui, un nom générique côté client faisait rejeter la
# partie entière — silencieusement, l'envoi étant « au mieux, jamais bloquant ».
add_executable(ns_test_scores test_scores.c)
target_link_libraries(ns_test_scores PRIVATE nineteen::games)
nineteen_set_warnings(ns_test_scores)
nineteen_apply_sanitizers(ns_test_scores)
add_test(NAME scores COMMAND ns_test_scores)

# La poussière. Le système se construit ici SANS GPU — `ns_particles_create` accepte
# un `ns_rhi *` nul et ne monte alors que sa simulation — donc ce test tourne aussi
# sur les machines d'intégration continue de macOS et de Windows, qui n'en ont pas.
#
# Ce qu'il attrape : un recyclage aux bornes de zone qui laisse le nuage s'échapper.
# La population ne bouge pas d'un grain quand ça arrive, aucune erreur n'est émise,
# et la poussière quitte simplement les faisceaux au bout de quelques minutes.
add_executable(ns_test_particles test_particles.c)
target_link_libraries(ns_test_particles PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_particles)
nineteen_apply_sanitizers(ns_test_particles)
add_test(NAME particles COMMAND ns_test_particles)

# Le solveur à deux os et la machine à états qui pose les bras. Les deux se
# compilent depuis `room/`, comme `ns_test_bvh` le fait déjà pour la caméra : ce
# sont des fichiers de jeu, pas de moteur, et les descendre dans `engine/` pour
# les tester reviendrait à déplacer du code pour arranger un build.
#
# Ce que ces tests attrapent vraiment, c'est un NaN. Une matrice de pose qui en
# contient un ne produit ni erreur ni message : le bras disparaît, et on cherche
# une heure du côté du rendu.
add_executable(ns_test_ik test_ik.c
    "${CMAKE_SOURCE_DIR}/room/room_camera.c"
    "${CMAKE_SOURCE_DIR}/room/room_viewmodel.c")
target_include_directories(ns_test_ik PRIVATE "${CMAKE_SOURCE_DIR}/room")
target_link_libraries(ns_test_ik PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_ik)
nineteen_apply_sanitizers(ns_test_ik)
add_test(NAME ik COMMAND ns_test_ik)

# Le personnage articulé. Il lit un VRAI fichier — c'est justement la lecture
# qu'on vérifie — donc il reçoit le dossier des assets en argument.
#
# Les trois défauts qu'il cherche sont muets : un indice d'os hors bornes envoie
# un sommet à l'autre bout de la salle, des poids qui ne somment pas à un font
# gonfler le personnage selon la pose, un NaN fait disparaître un membre. Aucun
# ne produit d'erreur, tous produisent une image fausse qu'on impute au rendu.
add_executable(ns_test_skin test_skin.c)
target_link_libraries(ns_test_skin PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_skin)
nineteen_apply_sanitizers(ns_test_skin)
add_test(NAME peau COMMAND ns_test_skin "${CMAKE_BINARY_DIR}/assets")

# Le mixeur, rendu dans un WAV plutôt que vers un périphérique.
#
# C'est ce test qui rend vraie une phrase que `docs/CHANGELOG-V15.md` a affirmée
# pendant deux mois : que l'occlusion audio sur le BVH était testée. Elle ne
# l'était pas — il n'y avait ni mixeur, ni test, ni même de lien vers miniaudio.
# Il tourne sans carte son, donc en intégration continue.
#
# `room_sound.c` et `room_camera.c` sont compilés ici depuis `room/` — comme
# `ns_test_bvh` le fait déjà — parce que B23 ne mesure plus seulement le mixeur
# mais la SALLE : la banque de pas, la cadence, et le niveau de l'ambiance en
# trois points. Réécrire la logique de `room_sound` dans le test mesurerait la
# réécriture, ce qui est le contraire de ce qu'on cherche.
add_executable(ns_test_audio test_audio.c
    "${CMAKE_SOURCE_DIR}/room/room_sound.c"
    "${CMAKE_SOURCE_DIR}/room/room_door.c"
    "${CMAKE_SOURCE_DIR}/room/room_camera.c")
target_include_directories(ns_test_audio PRIVATE "${CMAKE_SOURCE_DIR}/room")
target_link_libraries(ns_test_audio PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_audio)
nineteen_apply_sanitizers(ns_test_audio)
add_test(NAME audio COMMAND ns_test_audio "${CMAKE_BINARY_DIR}")

# Flappy Bird : ses RÈGLES, sans écran.
#
# Ce qu'il attrape est une partie devenue injouable — un écart trop étroit pour
# la hauteur de saut, un recyclage qui empile les tuyaux, un barème qui compte
# deux fois. Aucun de ces défauts ne se voit sur une capture fixe, et tous
# rendent le jeu sans intérêt.
add_executable(ns_test_flappy test_flappy.c)
target_link_libraries(ns_test_flappy PRIVATE nineteen::games)
nineteen_set_warnings(ns_test_flappy)
nineteen_apply_sanitizers(ns_test_flappy)
add_test(NAME flappy COMMAND ns_test_flappy)

# Snake. Le portage convertit tout un jeu écrit en IMAGES à 30 Hz vers un pas
# fixe en secondes ; une conversion ratée donne un jeu qui a l'air juste et se
# joue autrement. Le cas qui justifie ce fichier à lui seul : le hardcore est
# l'INVERSE du normal — manger coûte, laisser pourrir rapporte — et c'est
# exactement la règle qu'on « corrigerait » par mégarde.
add_executable(ns_test_snake test_snake.c)
target_link_libraries(ns_test_snake PRIVATE nineteen::games)
target_include_directories(ns_test_snake PRIVATE "${CMAKE_SOURCE_DIR}/games/snake")
nineteen_set_warnings(ns_test_snake)
nineteen_apply_sanitizers(ns_test_snake)
add_test(NAME snake COMMAND ns_test_snake)

add_executable(ns_test_shooter test_shooter.c)
target_link_libraries(ns_test_shooter PRIVATE nineteen::games)
target_include_directories(ns_test_shooter PRIVATE "${CMAKE_SOURCE_DIR}/games/shooter")
nineteen_set_warnings(ns_test_shooter)
nineteen_apply_sanitizers(ns_test_shooter)
add_test(NAME shooter COMMAND ns_test_shooter)

add_executable(ns_test_piano test_piano.c)
target_link_libraries(ns_test_piano PRIVATE nineteen::games)
target_include_directories(ns_test_piano PRIVATE "${CMAKE_SOURCE_DIR}/games/piano")
nineteen_set_warnings(ns_test_piano)
nineteen_apply_sanitizers(ns_test_piano)
add_test(NAME piano COMMAND ns_test_piano)

add_executable(ns_test_pacman test_pacman.c)
target_link_libraries(ns_test_pacman PRIVATE nineteen::games)
target_include_directories(ns_test_pacman PRIVATE "${CMAKE_SOURCE_DIR}/games/pacman")
nineteen_set_warnings(ns_test_pacman)
nineteen_apply_sanitizers(ns_test_pacman)
add_test(NAME pacman COMMAND ns_test_pacman)

add_executable(ns_test_asteroid test_asteroid.c)
target_link_libraries(ns_test_asteroid PRIVATE nineteen::games)
target_include_directories(ns_test_asteroid PRIVATE "${CMAKE_SOURCE_DIR}/games/asteroid")
nineteen_set_warnings(ns_test_asteroid)
nineteen_apply_sanitizers(ns_test_asteroid)
add_test(NAME asteroid COMMAND ns_test_asteroid)

add_executable(ns_test_tetris test_tetris.c)
target_link_libraries(ns_test_tetris PRIVATE nineteen::games)
target_include_directories(ns_test_tetris PRIVATE "${CMAKE_SOURCE_DIR}/games/tetris")
# Le test relit le fichier de 2020 pour vérifier que la table en est bien la
# copie : il lui faut donc son chemin, et pas une copie de plus.
target_compile_definitions(ns_test_tetris PRIVATE
    TETRIS_LEGACY_PIECES="${CMAKE_SOURCE_DIR}/legacy/games/5_tetris/pieces.h")
nineteen_set_warnings(ns_test_tetris)
nineteen_apply_sanitizers(ns_test_tetris)
add_test(NAME tetris COMMAND ns_test_tetris)

add_executable(ns_test_demineur test_demineur.c)
target_link_libraries(ns_test_demineur PRIVATE nineteen::games)
target_include_directories(ns_test_demineur PRIVATE "${CMAKE_SOURCE_DIR}/games/demineur")
nineteen_set_warnings(ns_test_demineur)
nineteen_apply_sanitizers(ns_test_demineur)
add_test(NAME demineur COMMAND ns_test_demineur)

add_executable(ns_test_core test_core.c)
target_link_libraries(ns_test_core PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_core)
nineteen_apply_sanitizers(ns_test_core)
add_test(NAME core COMMAND ns_test_core)

# L'écrivain glTF est testé contre le validateur que la chaîne de build utilise
# réellement (cgltf, appelé par bvhbake) : un document mal formé doit être refusé
# ici, et non trois étapes plus loin dans un message parlant d'octets.
add_executable(ns_test_gltf_write test_gltf_write.c "${CMAKE_SOURCE_DIR}/tools/gltf_write.c")
target_include_directories(ns_test_gltf_write PRIVATE "${CMAKE_SOURCE_DIR}/tools")
target_link_libraries(ns_test_gltf_write PRIVATE nineteen::cgltf)
if(UNIX)
    target_link_libraries(ns_test_gltf_write PRIVATE m)
endif()
nineteen_set_warnings(ns_test_gltf_write)
nineteen_apply_sanitizers(ns_test_gltf_write)
add_test(NAME gltf_write
         COMMAND ns_test_gltf_write "${CMAKE_BINARY_DIR}/test-gltf-write.gltf")

# Le substrat de génération de géométrie : soudure, normales, tangentes. Sans GPU
# et sans asset — ces traitements « marchent à moitié » sans rien signaler, et le
# défaut ne se voit qu'en mouvement.
add_executable(ns_test_geo test_geo.c
    "${CMAKE_SOURCE_DIR}/tools/geo_mesh.c"
    "${CMAKE_SOURCE_DIR}/tools/gltf_write.c")
target_include_directories(ns_test_geo PRIVATE
    "${CMAKE_SOURCE_DIR}/tools" "${CMAKE_SOURCE_DIR}/engine/core")
if(UNIX)
    target_link_libraries(ns_test_geo PRIVATE m)
endif()
nineteen_set_warnings(ns_test_geo)
nineteen_apply_sanitizers(ns_test_geo)
add_test(NAME geo COMMAND ns_test_geo)

# L'ACCESSIBILITÉ de la salle : le remplissage par diffusion sur lequel repose
# C-10 (`check_reachable`, tools/roomgen.c).
#
# Il existe parce que la salle est partie en production avec son joueur ENFERMÉ —
# un sas déclaré comme contour fermé, sans ouverture, et `playerStart` dedans. Le
# build a réussi et les trente-six tests d'alors sont passés : aucun ne regardait
# le VIDE entre les objets, qui est pourtant tout ce que le joueur habite.
#
# Ce test éprouve le contrôle sur les quatre cas qu'il doit départager — pièce
# ouverte, pièce scellée, porte à la largeur exacte du corps, porte trop étroite
# — et sur cinq alignements de grille, parce qu'un verdict qui dépendrait de
# l'endroit où la grille tombe serait irreproductible et ne serait plus cru. Ni
# GPU, ni asset, ni salle : de la géométrie posée à la main.
add_executable(ns_test_reach test_reach.c "${CMAKE_SOURCE_DIR}/tools/reach_grid.c")
target_include_directories(ns_test_reach PRIVATE
    "${CMAKE_SOURCE_DIR}/tools" "${CMAKE_SOURCE_DIR}/engine/core")
if(UNIX)
    target_link_libraries(ns_test_reach PRIVATE m)
endif()
nineteen_set_warnings(ns_test_reach)
nineteen_apply_sanitizers(ns_test_reach)
add_test(NAME acces COMMAND ns_test_reach)

# Les générateurs paramétriques, au-dessus du substrat.
#
# Les cas de refus valent autant que les invariants : `geo_wall_run` promet de
# s'arrêter sur une description incohérente plutôt que de produire un accesseur
# glTF vide que `bvhbake` refuserait deux étapes plus loin. Comme `tool_fatalf`
# sort par `exit(1)`, chaque refus est un processus séparé, déclaré `WILL_FAIL` —
# portable sur les trois plateformes, contrairement à un fork. Un refus qui
# cesserait de se déclencher fait donc échouer son test, au lieu de le vider.
add_executable(ns_test_shapes test_shapes.c
    "${CMAKE_SOURCE_DIR}/tools/geo_shapes.c"
    "${CMAKE_SOURCE_DIR}/tools/geo_mesh.c"
    "${CMAKE_SOURCE_DIR}/tools/gltf_write.c")
target_include_directories(ns_test_shapes PRIVATE
    "${CMAKE_SOURCE_DIR}/tools" "${CMAKE_SOURCE_DIR}/engine/core")
if(UNIX)
    target_link_libraries(ns_test_shapes PRIVATE m)
endif()
nineteen_set_warnings(ns_test_shapes)
nineteen_apply_sanitizers(ns_test_shapes)
add_test(NAME shapes COMMAND ns_test_shapes)

foreach(refusal IN ITEMS baie-deborde baie-a-cheval baies-superposees
                         allege-au-dessus-du-linteau linteau-trop-haut
                         points-confondus boite-plate instance-miroir)
    add_test(NAME refus-${refusal} COMMAND ns_test_shapes ${refusal})
    set_tests_properties(refus-${refusal} PROPERTIES WILL_FAIL TRUE)
endforeach()

add_executable(ns_test_render test_render.c)
target_link_libraries(ns_test_render PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_render)
# Le SEUL test qui ne l'appelait pas — donc le preset `linux-x64-asan` ne
# LIAIT pas : le moteur y est instrumenté et cette cible ne l'était pas. Le
# tableau ASan n'a donc jamais été complet, et ça ne se voyait pas parce que
# personne ne construisait ce preset en entier.
nineteen_apply_sanitizers(ns_test_render)
add_test(NAME render COMMAND ns_test_render "${CMAKE_BINARY_DIR}/test-render.png")

# En intégration continue il n'y a ni écran ni GPU : SDL passe par son pilote
# vidéo « offscreen » et Vulkan par le rasteriseur logiciel lavapipe.
#
# `SKIP_RETURN_CODE` : le test renvoie 77 quand aucun périphérique GPU n'existe
# (tests/test_render.c:56-62). Sans cette propriété CTest comptait ce 77 comme un
# échec — donc, pour garder le tableau vert, la CI n'exécutait tout simplement pas
# ce test hors Linux. C'est exactement ainsi que le rendu Metal a pu rester cassé
# deux mois sous un build vert. Un test « ignoré » doit se voir, pas disparaître.
#
# Le pilote « offscreen » n'est imposé que là où il aide. Sur macOS il n'y a pas
# de couche logicielle à forcer — Metal est le seul backend — et une fenêtre
# offscreen n'a pas de CAMetalLayer à rattacher, donc l'imposer transformerait un
# test exécutable en test ignoré.
if(APPLE)
    set(_render_env "")
else()
    set(_render_env "SDL_VIDEO_DRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1")
endif()

# Sous le preset instrumenté, les fils de lavapipe laissent 112 octets derrière
# eux. La suppression est nommée et commentée dans le fichier ; ce qui compte est
# qu'on n'éteint PAS la détection de fuites, on écarte celles d'un pilote.
if(NINETEEN_SANITIZE)
    list(APPEND _render_env
         "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan-suppressions.txt")
endif()

set_tests_properties(render PROPERTIES
    ENVIRONMENT "${_render_env}"
    SKIP_RETURN_CODE 77
    TIMEOUT 120
)

# Le menu de réglages. Aucune fenêtre, aucun GPU : ce qu'on vérifie est le
# comportement — les bornes, le bouclage, et surtout qu'un changement de palier
# n'écrase pas les trois réglages qui ont leur propre ligne.
#
# `room_sound.c` et `room_camera.c` sont liés ici parce que les deux lignes de
# niveau ajoutées en B23 — « PAS » et « FOND DE SALLE » — vivent au niveau du
# module `room_sound`, comme les volumes de bus vivent au niveau de `ns_audio`.
# Tester la ligne sans lier ce qu'elle pilote reviendrait à tester le dessin.
# `ns_test_bvh` compile déjà `room_camera.c` de la même façon : ce sont des
# fichiers de jeu, et les descendre dans `engine/` pour arranger un build serait
# déplacer du code pour la mauvaise raison.
#
# `room_credits.c` y entre pour une raison qui n'est pas de commodité : il porte
# l'attribution de CesiumMan, qui est une OBLIGATION de licence (CC BY 4.0) et
# non une politesse. Ce test-ci est ce qui rend cette obligation vérifiable :
# effacer la ligne fait échouer `menu`, donc le build. Une mention légale que
# rien ne défend finit par disparaître dans un nettoyage.
add_executable(ns_test_menu test_menu.c
    "${CMAKE_SOURCE_DIR}/room/room_menu.c"
    "${CMAKE_SOURCE_DIR}/room/room_credits.c"
    "${CMAKE_SOURCE_DIR}/room/room_sound.c"
    "${CMAKE_SOURCE_DIR}/room/room_door.c"
    "${CMAKE_SOURCE_DIR}/room/room_camera.c")
target_include_directories(ns_test_menu PRIVATE "${CMAKE_SOURCE_DIR}/room")
target_link_libraries(ns_test_menu PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_menu)
nineteen_apply_sanitizers(ns_test_menu)
add_test(NAME menu COMMAND ns_test_menu "$<TARGET_FILE_DIR:ns_test_menu>")

# Le client HTTP et le classement en ligne.
#
# Il ouvre de VRAIES sockets vers un serveur local — délibérément. Le reste du
# moteur se vérifie sans périphérique, mais un client réseau qu'on ne fait jamais
# parler à personne est un client dont on ne sait rien. Sans URL passée en
# argument, le test se limite au découpage d'URL, au verrou hors ligne et au
# serveur mort ; c'est ce que fait la CI par défaut.
#
# Avec une URL ET un jeton de session, le même binaire déroule la chaîne
# complète contre le vrai serveur Go et sa base — billet, sceau, envoi,
# classement. Ça demande PostgreSQL, donc ça ne s'ajoute pas ici : la marche à
# suivre est en tête de `test_online.c`.
add_executable(ns_test_online test_online.c)
target_link_libraries(ns_test_online PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_online)
nineteen_apply_sanitizers(ns_test_online)
add_test(NAME online COMMAND ns_test_online)
set_tests_properties(online PROPERTIES TIMEOUT 60)

# La présence et le duel en différé, sur le même principe que `ns_test_online`
# et pour la même raison : c'est ENTRE le C et le Go que vivent les défauts que
# personne ne voit d'un seul côté.
#
# Sans argument — ce que fait la CI — il vérifie les DEUX VERROUS (pas d'URL,
# `--offline`, et le temps réel inerte par défaut), l'analyseur de journal
# d'entrées face à ce qu'un serveur peut envoyer, et le serveur mort. Rien de
# tout ça n'ouvre de socket vers quoi que ce soit d'écoutant.
#
# Avec une URL, il ajoute la présence ANONYME ; avec une URL et un jeton, la
# chaîne entière du duel — billet, partie jouée, envoi, dépôt, téléchargement,
# rejeu comparé au bit près. Ça demande PostgreSQL, donc ça ne s'ajoute pas
# ici : la marche à suivre est en tête de `test_duel.c`.
#
# Il est lié à `nineteen::games` — contrairement à `ns_test_online` — parce
# qu'il JOUE réellement une partie : un duel qu'on vérifie sur des événements
# fabriqués à la main ne vérifie pas un duel.
add_executable(ns_test_duel test_duel.c)
target_link_libraries(ns_test_duel PRIVATE nineteen::games)
nineteen_set_warnings(ns_test_duel)
nineteen_apply_sanitizers(ns_test_duel)
add_test(NAME duel COMMAND ns_test_duel)
set_tests_properties(duel PROPERTIES TIMEOUT 60)

# Le duel EN DIRECT : deux clients, un vrai relais Go, de vraies sockets.
#
# Sans argument, il ne vérifie que les refus — c'est ce que fait la CI, et ça ne
# demande rien. Le duel entier se lance à la main, contre le relais autonome :
#
#   cd server && go run ./cmd/duelrelay -addr 127.0.0.1:8081 &
#   ns_test_lockstep 127.0.0.1 8081
#
# C'est le seul endroit qui confronte les deux moitiés du protocole binaire,
# écrites une fois en C et une fois en Go. Le classement en ligne a payé trois
# défauts pour avoir sauté cette étape-là.
add_executable(ns_test_lockstep test_lockstep.c)
target_link_libraries(ns_test_lockstep PRIVATE nineteen::games)
nineteen_set_warnings(ns_test_lockstep)
nineteen_apply_sanitizers(ns_test_lockstep)
add_test(NAME lockstep COMMAND ns_test_lockstep)
set_tests_properties(lockstep PROPERTIES TIMEOUT 60)

# Le plafond des cartes se vérifie sur les FICHIERS PRODUITS, pas sur l'intention.
#
# `texgen --max-map=` est un drapeau qu'on peut oublier de passer à l'un des
# trois appels — c'est arrivé pendant l'écriture, et la seule trace en était
# 197 Mio de plus dans l'archive, ce qui ne se voit nulle part. Le test relit
# l'entête PNG de chaque carte générée et refuse celle qui dépasse.
add_test(NAME cartes-plafonnees
         COMMAND ${CMAKE_COMMAND}
                 -DMAPS_DIR=${NINETEEN_GEN_DIR}/materials
                 -DMAX_MAP=${NINETEEN_MAX_MAP}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_map_size.cmake)

# Un journal d'ENTRÉES rejoue-t-il une partie ? C'est la propriété dont dépend
# n'importe quelle forme de duel, et elle n'était pas mesurée — j'affirmais
# même qu'elle l'était. Voir `docs/RESEAU-TEMPS-REEL.md`.
add_executable(ns_test_replay test_replay.c)
target_link_libraries(ns_test_replay PRIVATE nineteen::games)
nineteen_set_warnings(ns_test_replay)
nineteen_apply_sanitizers(ns_test_replay)
add_test(NAME replay COMMAND ns_test_replay)

# L'état d'une partie est-il une VALEUR ? Mesuré entre deux processus, sur les
# huit jeux. `ns_test_replay` compare dans un seul processus, où une adresse est
# stable : c'est ce qui a laissé un `const char *` dans l'état de Piano.
add_test(NAME determinisme
         COMMAND ${CMAKE_COMMAND}
                 -DEXE=$<TARGET_FILE:nineteen>
                 -DDATA=${CMAKE_CURRENT_SOURCE_DIR}/data
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_determinism.cmake)

# `--rejouer=` de bout en bout : le format sur disque, son analyse, la reprise
# de la graine. `ns_test_replay` ne couvre que le rejeu EN MÉMOIRE.
add_test(NAME rejeu-cli
         COMMAND ${CMAKE_COMMAND}
                 -DEXE=$<TARGET_FILE:nineteen>
                 -DJOURNAL=${CMAKE_CURRENT_SOURCE_DIR}/data/rejeu-demineur.txt
                 -DCMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_replay_cli.cmake)
