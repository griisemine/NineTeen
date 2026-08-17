# Shaders.cmake — compilation et embarquage des shaders.
#
# Choix : les shaders sont écrits **une fois** en GLSL (dialecte Vulkan), compilés
# hors-ligne en SPIR-V, puis **embarqués dans le binaire** sous forme de tableaux
# d'octets. Conséquences voulues :
#
#   * un seul exécutable à distribuer, aucun fichier .spv à retrouver à l'exécution
#     (la V1 cherchait ses assets en "../room/..." et cassait dès qu'on la lançait
#     depuis un autre répertoire) ;
#   * l'API GPU de SDL3 consomme SPIR-V sur Vulkan directement ; sur Metal et D3D12,
#     SDL_shadercross transpile le même SPIR-V en MSL / DXIL (option ci-dessous).
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

# Transpilation vers MSL/DXIL : nécessaire pour macOS et D3D12, inutile sur Vulkan pur.
option(NINETEEN_SHADERCROSS "Transpiler le SPIR-V en MSL/DXIL via SDL_shadercross" OFF)
if(APPLE)
    set(NINETEEN_SHADERCROSS ON CACHE BOOL "" FORCE)  # Metal n'accepte pas le SPIR-V
endif()

# ---------------------------------------------------------------------------
# nineteen_add_shaders(<target> SOURCES <fichiers glsl...>)
#
# Compile chaque shader en SPIR-V, génère un .c/.h d'embarquage, et attache le
# tout à la cible. Les symboles générés sont nommés d'après le fichier :
#   room_gbuffer.vert  ->  ns_room_gbuffer_vert / ns_room_gbuffer_vert_len
# ---------------------------------------------------------------------------
function(nineteen_add_shaders target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    set(gen_dir "${CMAKE_BINARY_DIR}/generated/shaders/${target}")
    file(MAKE_DIRECTORY "${gen_dir}")

    set(spv_files "")
    set(symbol_decls "")
    set(symbol_defs "")
    set(registry_rows "")

    foreach(src IN LISTS ARG_SOURCES)
        get_filename_component(src_abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)          # room_gbuffer.vert
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" sym "${name}")  # room_gbuffer_vert

        set(spv "${gen_dir}/${name}.spv")
        add_custom_command(
            OUTPUT  "${spv}"
            COMMAND "${GLSLANG_EXECUTABLE}" --target-env vulkan1.0 -Os -o "${spv}" "${src_abs}"
            DEPENDS "${src_abs}"
            COMMENT "Shader ${name} -> SPIR-V"
            VERBATIM
        )
        list(APPEND spv_files "${spv}")
        string(APPEND symbol_decls "extern const unsigned char ns_${sym}[];\nextern const unsigned int  ns_${sym}_len;\n")
        list(APPEND registry_rows "${name}|${sym}")
    endforeach()

    # Le fichier d'embarquage est produit par un script CMake : pas de dépendance
    # à python ou xxd, qui manquent selon les plateformes.
    set(embed_c "${gen_dir}/shader_blobs.c")
    set(embed_h "${gen_dir}/shader_blobs.h")
    add_custom_command(
        OUTPUT  "${embed_c}" "${embed_h}"
        COMMAND ${CMAKE_COMMAND}
                -DOUT_C=${embed_c} -DOUT_H=${embed_h}
                "-DSPV_FILES=${spv_files}"
                "-DROWS=${registry_rows}"
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        DEPENDS ${spv_files} "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        COMMENT "Embarquage de ${target} : ${registry_rows}"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${embed_c}" "${embed_h}")
    target_include_directories(${target} PUBLIC "${gen_dir}")
endfunction()
