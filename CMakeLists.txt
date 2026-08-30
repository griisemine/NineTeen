cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Nineteen V15 — salle d'arcade rétro, moteur reconstruit
#
# Un seul arbre de build pour Windows / macOS / Linux, là où la V1 avait un
# makefile avec des `ifeq ($(UNAME), Darwin)` et des .o commités.
# ---------------------------------------------------------------------------
project(Nineteen
    VERSION     17.0.0
    DESCRIPTION "Salle d'arcade 3D et ses mini-jeux"
    LANGUAGES   C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Un seul endroit pour les binaires : ce que la CI ramasse pour les paquets.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)
include(Shaders)

enable_testing()

# ---------------------------------------------------------------------------
# Chemins de données : le jeu doit se retrouver qu'il tourne depuis l'arbre de
# build ou depuis un paquet installé. L'original bricolait des "../room/..."
# relatifs au répertoire courant, ce qui cassait dès qu'on lançait ailleurs.
# ---------------------------------------------------------------------------
set(NINETEEN_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets"     CACHE PATH "Racine des assets sources")
set(NINETEEN_GEN_DIR   "${CMAKE_BINARY_DIR}/assets"             CACHE PATH "Racine des assets générés")

# `tools` avant `engine` : la compilation des shaders du moteur invoque
# `spv2msl`, qui est un outil. CMake sait résoudre une cible déclarée plus tard,
# mais l'ordre de lecture rend la dépendance visible plutôt que subtile.
add_subdirectory(tools)
add_subdirectory(engine)
add_subdirectory(assets)
add_subdirectory(games)
add_subdirectory(room)
add_subdirectory(tests)

# ==========================================================================
# Empaquetage
# ==========================================================================
#
# Ce que CPack fait ici, et ce qu'il ne fait pas.
#
# IL FAIT un paquet que l'on INSTALLE, et non plus une archive que l'on déballe.
# Jusqu'en 17.0.0 les trois plateformes recevaient un ZIP ou un TGZ : le joueur
# devait deviner quel fichier lancer dans quel sous-répertoire, macOS refusait
# un binaire nu sorti d'une archive, et rien n'apparaissait dans aucun menu.
# Désormais :
#
#   macOS    DragNDrop  -> Nineteen-17.0.0-macOS-universal.dmg, un Nineteen.app
#                          qu'on glisse sur le raccourci /Applications du .dmg
#   Linux    TGZ + DEB  -> plus une AppImage, produite à part (packaging/linux)
#   Windows  NSIS       -> un .exe d'installation, raccourci menu Démarrer et
#                          désinstalleur, SANS droits administrateur
#
# IL NE FAIT PAS la signature. Un .dmg notarié demande un certificat Apple
# Developer ID, un .exe signé un certificat Authenticode : ce sont des secrets
# qui appartiennent à qui publie, pas au dépôt. Le workflow de release les
# consomme s'ils existent et produit des paquets NON SIGNÉS sinon, en le
# disant — plutôt que d'échouer, et plutôt que de prétendre.
#
# CE QUI A ÉTÉ VÉRIFIÉ EN ÉCRIVANT CE BLOC, ET CE QUI NE L'A PAS ÉTÉ.
#
#   macOS   VÉRIFIÉ sur la machine de développement : .dmg produit, monté,
#           Nineteen.app copiée dans /Applications, arbre de build RENOMMÉ pour
#           qu'aucun montage de développement ne puisse la sauver, puis lancée.
#   Linux   VÉRIFIÉ dans un conteneur ubuntu:22.04 (Docker) : .deb installé par
#           apt et lancé, AppImage lancée. Pas sur une machine Linux réelle.
#   Windows CONFIGURÉ, PAS VÉRIFIÉ. Cette machine n'a ni makensis, ni mingw-w64,
#           ni wine : le .exe ne peut pas être produit ici, et personne ne doit
#           lire ce bloc comme s'il l'avait été. Ce qui EST vérifié ici : que
#           `cpack -G NSIS` accepte la configuration et écrit un CPackConfig
#           cohérent. Le .exe lui-même sort des runners `windows-2022` de GitHub,
#           dont l'image annonce « NSIS 3.10 » (actions/runner-images,
#           images/windows/Windows2022-Readme.md).
set(CPACK_PACKAGE_NAME "nineteen")
set(CPACK_PACKAGE_VENDOR "Nineteen")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "La salle d'arcade : huit jeux, dix-neuf bornes")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "nineteen")
set(CPACK_PACKAGE_CONTACT "Nineteen <nineteen@example.invalid>")
set(CPACK_VERBATIM_VARIABLES ON)

# Les réglages qui ne valent QUE pour un générateur donné — nom de fichier,
# préfixe d'installation, pages de l'installateur — vivent là. CPack relit ce
# fichier une fois PAR générateur, avec `CPACK_GENERATOR` positionné : c'est le
# seul endroit où « le .deb sous /usr, le .tar.gz à plat » peut s'écrire sans
# que l'un contamine l'autre.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/CPackNineteen.cmake")
# Ce fichier-là est lu par `cpack`, dans un processus qui ne connaît NI les
# variables du projet NI son répertoire source : seules les variables préfixées
# `CPACK_` traversent, recopiées dans le CPackConfig généré. D'où ces deux-ci.
set(CPACK_NINETEEN_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_NINETEEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
elseif(APPLE)
    # DragNDrop SEUL, pas « DragNDrop;TGZ ». Deux fichiers pour la même
    # plateforme, c'est une question de plus à la page de téléchargement
    # (« lequel ? ») pour un gain nul : le Nineteen.app du .dmg se copie où l'on
    # veut, y compris hors d'/Applications, et se lance depuis un terminal comme
    # n'importe quel binaire — `Nineteen.app/Contents/MacOS/nineteen --help`.
    set(CPACK_GENERATOR "DragNDrop")
else()
    # L'AppImage n'est PAS un générateur CPack : elle est produite par
    # `packaging/linux/appimage.sh`, qui part du même `cmake --install`. Voir le
    # commentaire en tête de ce script pour la raison.
    set(CPACK_GENERATOR "TGZ;DEB")
endif()

include(CPack)

# Le test `paquets`. Chaque plateforme ne construit qu'un générateur sur trois :
# les réglages Windows ne sont exécutés par personne avant le jour de la
# release, et `cpack -G NSIS` s'arrête AVANT de lire notre configuration quand
# makensis manque (« Cannot find NSIS compiler »). Ce test-là relit la
# configuration des QUATRE générateurs depuis n'importe quelle machine et vérifie
# ce qui se vérifie partout : variables posées, fichiers désignés présents,
# en-tête réel de l'icône .ico, licence NSIS sans un octet hors ASCII.
# `packaging/verifier.cmake` dit en tête ce qu'il ne prouve pas.
add_test(NAME paquets COMMAND "${CMAKE_COMMAND}"
         "-DNINETEEN_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
         -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/verifier.cmake")

message(STATUS "")
message(STATUS "Nineteen ${PROJECT_VERSION}")
message(STATUS "  build type ......... ${CMAKE_BUILD_TYPE}")
message(STATUS "  sanitizers ......... ${NINETEEN_SANITIZE}")
message(STATUS "  assets (source) .... ${NINETEEN_ASSET_DIR}")
message(STATUS "  assets (générés) ... ${NINETEEN_GEN_DIR}")
message(STATUS "")
