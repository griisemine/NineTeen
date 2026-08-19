cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Nineteen V15 — salle d'arcade rétro, moteur reconstruit
#
# Un seul arbre de build pour Windows / macOS / Linux, là où la V1 avait un
# makefile avec des `ifeq ($(UNAME), Darwin)` et des .o commités.
# ---------------------------------------------------------------------------
project(Nineteen
    VERSION     15.0.0
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
# IL FAIT : une archive autonome par plateforme, avec le binaire et ses assets
# côte à côte — la disposition exacte que `ns_paths` attend. Ça se vérifie sans
# rien signer et sans rien téléverser : `cpack -G TGZ` puis on lance le jeu
# depuis l'archive déballée. C'est le test qui manquait, et c'est lui qui a
# montré que `install()` n'emportait pas les assets.
#
# IL NE FAIT PAS la signature. Un .dmg notarié demande un certificat Apple
# Developer ID, un .msi signé un certificat Authenticode : ce sont des secrets
# qui appartiennent à qui publie, pas au dépôt. Le workflow de release les
# consomme s'ils existent et produit des paquets NON SIGNÉS sinon, en le
# disant — plutôt que d'échouer, et plutôt que de prétendre.
set(CPACK_PACKAGE_NAME "nineteen")
set(CPACK_PACKAGE_VENDOR "Nineteen")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "La salle d'arcade : huit jeux, dix-neuf bornes")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "nineteen-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "nineteen")
set(CPACK_VERBATIM_VARIABLES ON)
# L'archive contient un dossier racine : déballer chez soi ne doit pas déverser
# `bin/` dans le répertoire courant.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

message(STATUS "")
message(STATUS "Nineteen ${PROJECT_VERSION}")
message(STATUS "  build type ......... ${CMAKE_BUILD_TYPE}")
message(STATUS "  sanitizers ......... ${NINETEEN_SANITIZE}")
message(STATUS "  assets (source) .... ${NINETEEN_ASSET_DIR}")
message(STATUS "  assets (générés) ... ${NINETEEN_GEN_DIR}")
message(STATUS "")
