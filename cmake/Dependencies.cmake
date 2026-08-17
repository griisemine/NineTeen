# Dependencies.cmake — récupération de SDL3.
#
# Trois chemins, dans cet ordre de priorité :
#   1. NINETEEN_USE_SYSTEM_SDL3=ON  -> find_package(SDL3) (distribution, vcpkg, Homebrew)
#   2. NINETEEN_SDL3_LOCAL_DIR=/chemin -> add_subdirectory (build hors-ligne, source déjà là)
#   3. défaut                        -> FetchContent sur un tag épinglé
#
# Le tag est épinglé volontairement : un projet qu'on rouvre dans cinq ans doit se recompiler
# à l'identique, pas suivre la branche principale d'un tiers.

set(NINETEEN_SDL3_TAG "release-3.4.14" CACHE STRING "Tag SDL3 utilisé par FetchContent")

option(NINETEEN_USE_SYSTEM_SDL3 "Utiliser un SDL3 déjà installé sur le système" OFF)
set(NINETEEN_SDL3_LOCAL_DIR "" CACHE PATH "Répertoire source SDL3 local (build hors-ligne)")

if(NINETEEN_USE_SYSTEM_SDL3)
    find_package(SDL3 REQUIRED CONFIG)
    message(STATUS "Nineteen: SDL3 système ${SDL3_VERSION}")

elseif(NINETEEN_SDL3_LOCAL_DIR)
    if(NOT EXISTS "${NINETEEN_SDL3_LOCAL_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "NINETEEN_SDL3_LOCAL_DIR ne contient pas de CMakeLists.txt : ${NINETEEN_SDL3_LOCAL_DIR}")
    endif()
    message(STATUS "Nineteen: SDL3 local ${NINETEEN_SDL3_LOCAL_DIR}")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory("${NINETEEN_SDL3_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/_sdl3" EXCLUDE_FROM_ALL)

else()
    include(FetchContent)
    message(STATUS "Nineteen: SDL3 via FetchContent (${NINETEEN_SDL3_TAG})")

    # Statique : un seul binaire à distribuer, pas de .dll/.so à côté de l'exécutable.
    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        ${NINETEEN_SDL3_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# Cible unifiée : le reste du projet ne dépend que de nineteen::sdl3, quel que soit le chemin pris.
add_library(nineteen_sdl3 INTERFACE)
add_library(nineteen::sdl3 ALIAS nineteen_sdl3)
if(TARGET SDL3::SDL3-static)
    target_link_libraries(nineteen_sdl3 INTERFACE SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
    target_link_libraries(nineteen_sdl3 INTERFACE SDL3::SDL3)
else()
    message(FATAL_ERROR "Aucune cible SDL3 exploitable n'a été trouvée")
endif()

# ---------------------------------------------------------------------------
# Dépendances vendorées (header-only) — exposées comme cibles INTERFACE
# ---------------------------------------------------------------------------
set(NINETEEN_TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

foreach(lib miniaudio stb cgltf jsmn)
    add_library(nineteen_${lib} INTERFACE)
    add_library(nineteen::${lib} ALIAS nineteen_${lib})
    target_include_directories(nineteen_${lib} SYSTEM INTERFACE "${NINETEEN_TP}/${lib}")
endforeach()

# miniaudio a besoin de pthread/dl/m sur les Unix.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(nineteen_miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
elseif(APPLE)
    target_link_libraries(nineteen_miniaudio INTERFACE
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
