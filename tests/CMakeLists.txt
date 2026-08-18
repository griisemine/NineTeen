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

# Le mixeur, rendu dans un WAV plutôt que vers un périphérique.
#
# C'est ce test qui rend vraie une phrase que `docs/CHANGELOG-V15.md` a affirmée
# pendant deux mois : que l'occlusion audio sur le BVH était testée. Elle ne
# l'était pas — il n'y avait ni mixeur, ni test, ni même de lien vers miniaudio.
# Il tourne sans carte son, donc en intégration continue.
add_executable(ns_test_audio test_audio.c)
target_link_libraries(ns_test_audio PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_audio)
nineteen_apply_sanitizers(ns_test_audio)
add_test(NAME audio COMMAND ns_test_audio "${CMAKE_BINARY_DIR}")

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

set_tests_properties(render PROPERTIES
    ENVIRONMENT "${_render_env}"
    SKIP_RETURN_CODE 77
    TIMEOUT 120
)
