# Shaders.cmake — compilation, traduction et embarquage des shaders.
#
# Choix : les shaders sont écrits **une fois** en GLSL (dialecte Vulkan), compilés
# hors-ligne en SPIR-V, puis **embarqués dans le binaire** sous forme de tableaux
# d'octets. Conséquences voulues :
#
#   * un seul exécutable à distribuer, aucun fichier .spv à retrouver à l'exécution
#     (la V1 cherchait ses assets en "../room/..." et cassait dès qu'on la lançait
#     depuis un autre répertoire) ;
#   * l'API GPU de SDL3 consomme le SPIR-V directement sur Vulkan.
#
# Metal, lui, ne consomme pas de SPIR-V — il veut du MSL. Ce fichier déclarait
# jadis une option `NINETEEN_SHADERCROSS` que **rien ne lisait**, forcée à ON sur
# Apple avec un commentaire expliquant le problème. Résultat : le build macOS
# passait au vert, et le jeu refusait ses seize shaders au démarrage. L'option
# fait maintenant ce qu'elle annonce, via `tools/spv2msl`.
#
# Il faut glslangValidator (paquet `glslang-tools`, ou le SDK Vulkan) au moment du build.

find_program(GLSLANG_EXECUTABLE
    NAMES glslangValidator glslang
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES bin
    DOC "Compilateur GLSL -> SPIR-V"
)

if(NOT GLSLANG_EXECUTABLE)
    message(FATAL_ERROR
        "glslangValidator est introuvable — impossible de compiler les shaders.\n"
        "  Debian/Ubuntu : apt install glslang-tools\n"
        "  macOS         : brew install glslang\n"
        "  Windows       : installer le SDK Vulkan (LunarG)")
endif()
message(STATUS "Nineteen: glslang ....... ${GLSLANG_EXECUTABLE}")

# ---------------------------------------------------------------------------
# Quels formats embarquer
# ---------------------------------------------------------------------------
# Un format embarqué en trop, c'est du poids mort dans le binaire ; un format
# manquant, c'est un backend qui refuse tout. On n'embarque donc que ce que la
# plateforme peut consommer — et `ns_rhi.c` construit son masque de périphérique
# à partir de ce qui est réellement présent, de sorte qu'on ne puisse plus
# annoncer à SDL un format qu'on ne sait pas fournir.
#
# `NINETEEN_SHADERCROSS` reste activable sur Linux : c'est ce qui permet de
# vérifier la traduction MSL et ses emplacements de ressources sans posséder de
# Mac (cible de test `msl`).
if(APPLE)
    set(_spirv_default OFF)   # Metal n'accepte que MSL / metallib
    set(_msl_default   ON)
else()
    set(_spirv_default ON)
    set(_msl_default   OFF)
endif()

option(NINETEEN_SHADER_SPIRV "Embarquer les shaders en SPIR-V (Vulkan)" ${_spirv_default})
option(NINETEEN_SHADERCROSS  "Traduire et embarquer les shaders en MSL (Metal)" ${_msl_default})

if(NOT NINETEEN_SHADER_SPIRV AND NOT NINETEEN_SHADERCROSS)
    message(FATAL_ERROR
        "Aucun format de shader activé : le binaire produit ne pourrait dessiner nulle part.\n"
        "  Activer NINETEEN_SHADER_SPIRV (Vulkan) et/ou NINETEEN_SHADERCROSS (Metal).")
endif()

message(STATUS "Nineteen: formats shaders  SPIR-V=${NINETEEN_SHADER_SPIRV} MSL=${NINETEEN_SHADERCROSS}")

# ---------------------------------------------------------------------------
# SPIRV-Cross — uniquement quand du MSL est demandé
# ---------------------------------------------------------------------------
# Mêmes trois chemins que SDL3 (cf. cmake/Dependencies.cmake), pour que la
# promesse de build hors ligne d'A2b tienne aussi ici.
#
# Pourquoi SPIRV-Cross seul et pas SDL_shadercross, qui fait déjà ce travail :
# son CMake en mode « vendored » exige les sous-modules SPIRV-Cross,
# SPIRV-Headers, SPIRV-Tools **et** DirectXShaderCompiler — ce dernier vérifié
# même avec la compilation HLSL désactivée, soit ~1 Gio de LLVM inutile ici ; et
# en mode non vendoré il réclame une installation système. SPIRV-Cross est un
# dépôt autonome, sans sous-module, qui compile en quelques dizaines de secondes.
set(NINETEEN_SPIRV_CROSS_TAG "vulkan-sdk-1.4.357.0" CACHE STRING "Tag SPIRV-Cross utilisé par FetchContent")
option(NINETEEN_USE_SYSTEM_SPIRV_CROSS "Utiliser un SPIRV-Cross déjà installé" OFF)
set(NINETEEN_SPIRV_CROSS_LOCAL_DIR "" CACHE PATH "Répertoire source SPIRV-Cross local (build hors-ligne)")

if(NINETEEN_SHADERCROSS)
    # SPIRV-Cross est du C++ ; le reste du projet est du C pur, et le reste
    # de manière visible : le langage n'est activé que sur ce chemin.
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    set(SPIRV_CROSS_CLI          OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED       OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_STATIC       ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL  ON  CACHE BOOL "" FORCE)  # requis par l'émetteur MSL
    set(SPIRV_CROSS_ENABLE_MSL   ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL  OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP   OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API ON  CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON  CACHE BOOL "" FORCE)

    if(NINETEEN_USE_SYSTEM_SPIRV_CROSS)
        find_package(spirv_cross_c REQUIRED CONFIG)
        message(STATUS "Nineteen: SPIRV-Cross système")
    elseif(NINETEEN_SPIRV_CROSS_LOCAL_DIR)
        if(NOT EXISTS "${NINETEEN_SPIRV_CROSS_LOCAL_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "NINETEEN_SPIRV_CROSS_LOCAL_DIR ne contient pas de CMakeLists.txt : ${NINETEEN_SPIRV_CROSS_LOCAL_DIR}")
        endif()
        message(STATUS "Nineteen: SPIRV-Cross local ${NINETEEN_SPIRV_CROSS_LOCAL_DIR}")
        add_subdirectory("${NINETEEN_SPIRV_CROSS_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/_spirv_cross" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        message(STATUS "Nineteen: SPIRV-Cross via FetchContent (${NINETEEN_SPIRV_CROSS_TAG})")
        FetchContent_Declare(spirv_cross
            GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
            GIT_TAG        ${NINETEEN_SPIRV_CROSS_TAG}
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
        )
        FetchContent_MakeAvailable(spirv_cross)
    endif()

    if(NOT TARGET spirv-cross-c)
        message(FATAL_ERROR "Cible « spirv-cross-c » introuvable : SPIRV-Cross n'expose pas son API C.")
    endif()
endif()

# ---------------------------------------------------------------------------
# nineteen_add_shaders(<target> SOURCES <fichiers glsl...>)
#
# Compile chaque shader en SPIR-V, le traduit en MSL si demandé, génère un .c/.h
# d'embarquage, et attache le tout à la cible. Les symboles générés sont nommés
# d'après le fichier et le format :
#   gbuffer.vert  ->  ns_gbuffer_vert / ns_gbuffer_vert_msl
# ---------------------------------------------------------------------------
function(nineteen_add_shaders target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    set(gen_dir "${CMAKE_BINARY_DIR}/generated/shaders/${target}")
    file(MAKE_DIRECTORY "${gen_dir}")

    # Valeurs des bits SDL_GPU_SHADERFORMAT_*, recopiées de SDL_gpu.h. Le
    # registre généré les porte telles quelles, ce qui évite d'inclure SDL dans
    # un en-tête produit par un script CMake.
    set(FORMAT_SPIRV 2)   # 1 << 1
    set(FORMAT_MSL  16)   # 1 << 4

    set(blob_files "")
    set(registry_rows "")

    foreach(src IN LISTS ARG_SOURCES)
        get_filename_component(src_abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)              # gbuffer.vert
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" sym "${name}")   # gbuffer_vert

        # Le SPIR-V est produit dans tous les cas : c'est l'entrée du traducteur
        # MSL, même quand on ne l'embarque pas.
        set(spv "${gen_dir}/${name}.spv")
        add_custom_command(
            OUTPUT  "${spv}"
            COMMAND "${GLSLANG_EXECUTABLE}" --target-env vulkan1.0 -Os -o "${spv}" "${src_abs}"
            DEPENDS "${src_abs}"
            COMMENT "Shader ${name} -> SPIR-V"
            VERBATIM
        )

        if(NINETEEN_SHADER_SPIRV)
            list(APPEND blob_files "${spv}")
            list(APPEND registry_rows "${name}|${sym}|${FORMAT_SPIRV}|${spv}")
        endif()

        if(NINETEEN_SHADERCROSS)
            set(msl "${gen_dir}/${name}.msl")
            add_custom_command(
                OUTPUT  "${msl}"
                COMMAND spv2msl "${spv}" "${msl}"
                DEPENDS spv2msl "${spv}"
                COMMENT "Shader ${name} -> MSL (emplacements SDL3 vérifiés)"
                VERBATIM
            )
            list(APPEND blob_files "${msl}")
            list(APPEND registry_rows "${name}|${sym}_msl|${FORMAT_MSL}|${msl}")
        endif()
    endforeach()

    # Le fichier d'embarquage est produit par un script CMake : pas de dépendance
    # à python ou xxd, qui manquent selon les plateformes.
    set(embed_c "${gen_dir}/shader_blobs.c")
    set(embed_h "${gen_dir}/shader_blobs.h")
    add_custom_command(
        OUTPUT  "${embed_c}" "${embed_h}"
        COMMAND ${CMAKE_COMMAND}
                -DOUT_C=${embed_c} -DOUT_H=${embed_h}
                "-DROWS=${registry_rows}"
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        DEPENDS ${blob_files} "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        COMMENT "Embarquage de ${target}"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${embed_c}" "${embed_h}")
    target_include_directories(${target} PUBLIC "${gen_dir}")
endfunction()
