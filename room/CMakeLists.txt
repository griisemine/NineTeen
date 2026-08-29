# room/ — la salle d'arcade : l'exécutable principal.

add_executable(nineteen
    main.c
    room_camera.c
    room_viewmodel.c
    room_hud.c
    room_attract.c
    room_menu.c
    room_sound.c
    room_door.c
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

# --------------------------------------------------------------------------
# Installation
# --------------------------------------------------------------------------
#
# LE BINAIRE SEUL NE SUFFIT PAS, et c'est ce que cette règle faisait jusqu'ici.
#
# `ns_paths` monte « <dossier du binaire>/assets » — c'est la disposition d'un
# paquet installé, et le commentaire de `ns_paths.c:82` le dit. Or seul
# l'exécutable était installé : un `cmake --install` produisait un arbre où le
# jeu ne trouvait ni scène, ni texture, ni son, et se serait arrêté sur
# « scène introuvable ». Personne ne s'en apercevait parce que personne
# n'installait — on lançait toujours depuis l'arbre de build, où le chemin est
# passé en dur par `NINETEEN_BUILD_ASSET_DIR`.
#
# Les assets vont donc À CÔTÉ du binaire, dans `bin/assets`, exactement là où le
# moteur les cherche. Pas dans `share/` : ce serait plus conforme aux usages
# Unix, et ça demanderait au moteur un point de montage de plus dont un paquet
# .dmg ou .msi n'aurait que faire.
install(TARGETS nineteen RUNTIME DESTINATION bin)

# On installe les quatre répertoires QUE LE JEU LIT, et pas le contenu du
# dossier de build. La première version copiait `${NINETEEN_GEN_DIR}/` en bloc :
# 455 Mio, dont les `CMakeFiles`, les `CTestTestfile.cmake`, les WAV de contrôle
# du mixeur et le répertoire intermédiaire de l'art généré. Un paquet qui
# emporte ses gravats est un paquet qu'on n'a pas regardé.
# `models` porte le PERSONNAGE de la vue a la troisieme personne : c'est le seul
# modele lu A L'EXECUTION — les autres sont cuits dans `salle.gltf` au build.
# L'oublier ne casse rien et ne dit rien : le jeu demarre, annonce « personnage
# indisponible » dans un journal que personne ne lit, et la touche F10 ne fait
# plus rien. C'est exactement le genre de panne muette qu'un paquet doit rendre
# impossible.
foreach(_dir scene materials sounds games models)
    install(DIRECTORY "${NINETEEN_GEN_DIR}/${_dir}" DESTINATION bin/assets)
endforeach()

# Les reglages du personnage, A COTE DU BINAIRE : c'est le premier des trois
# endroits ou `ns_env_load` les cherche. Sans ce fichier le jeu tourne sur ses
# valeurs par defaut — ce qui est correct — mais le joueur n'a rien a editer, et
# la promesse « configurable sans recompiler » ne tient pas dans le paquet.
install(FILES "${CMAKE_SOURCE_DIR}/nineteen.env" DESTINATION bin)

# Ce qu'il faut lire pour jouer, et ce qu'il faut savoir sur les licences.
install(FILES "${CMAKE_SOURCE_DIR}/docs/JOUER.md"
              "${CMAKE_SOURCE_DIR}/assets/cc0/LICENSES.md"
        DESTINATION .)

# De quoi lancer le jeu depuis un menu, sur les bureaux qui en ont un.
if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nineteen.desktop"
            DESTINATION share/applications)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nineteen.png"
            DESTINATION share/icons/hicolor/256x256/apps)
endif()
