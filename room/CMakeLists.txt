# room/ — la salle d'arcade : l'exécutable principal.

add_executable(nineteen
    main.c
    room_camera.c
    room_viewmodel.c
    room_hud.c
    room_sound.c
)

target_link_libraries(nineteen PRIVATE nineteen::engine nineteen::games)
target_include_directories(nineteen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
nineteen_set_warnings(nineteen)
nineteen_apply_sanitizers(nineteen)

# Le jeu doit trouver ses assets qu'il tourne depuis l'arbre de build ou depuis
# un paquet installé ; le chemin de build est transmis au compilateur et sert
# de point de montage supplémentaire en développement.
target_compile_definitions(nineteen PRIVATE
    NINETEEN_BUILD_ASSET_DIR="${NINETEEN_GEN_DIR}"
    NINETEEN_VERSION="${PROJECT_VERSION}"
)

add_dependencies(nineteen nineteen_assets)

install(TARGETS nineteen RUNTIME DESTINATION bin)
