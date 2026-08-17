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
