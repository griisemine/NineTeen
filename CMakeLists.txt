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

add_subdirectory(engine)
add_subdirectory(tools)
add_subdirectory(games)
add_subdirectory(room)
add_subdirectory(tests)

message(STATUS "")
message(STATUS "Nineteen ${PROJECT_VERSION}")
message(STATUS "  build type ......... ${CMAKE_BUILD_TYPE}")
message(STATUS "  sanitizers ......... ${NINETEEN_SANITIZE}")
message(STATUS "  assets (source) .... ${NINETEEN_ASSET_DIR}")
message(STATUS "  assets (générés) ... ${NINETEEN_GEN_DIR}")
message(STATUS "")
