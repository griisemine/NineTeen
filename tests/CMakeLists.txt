# tests/ — vérifications automatisées.
#
# Deux familles :
#   * tests purement logiques (mathématiques, déterminisme, BVH) : rapides, sans GPU ;
#   * tests de rendu : exécutés en headless sur Vulkan logiciel (lavapipe), ils
#     produisent un PNG que l'on compare. C'est ce qui permet de vérifier le rendu
#     en intégration continue, sans écran ni carte graphique.

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

add_executable(ns_test_render test_render.c)
target_link_libraries(ns_test_render PRIVATE nineteen::engine)
nineteen_set_warnings(ns_test_render)
add_test(NAME render COMMAND ns_test_render "${CMAKE_BINARY_DIR}/test-render.png")

# En intégration continue il n'y a ni écran ni GPU : SDL passe par son pilote
# vidéo « offscreen » et Vulkan par le rasteriseur logiciel lavapipe.
set_tests_properties(render PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;LIBGL_ALWAYS_SOFTWARE=1"
    TIMEOUT 120
)
