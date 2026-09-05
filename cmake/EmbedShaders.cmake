# EmbedShaders.cmake — script exécuté par `cmake -P` pendant le build.
#
# Transforme une liste de blobs de shaders en un couple .c/.h contenant les
# octets, plus un registre (nom, format) -> blob pour que le moteur puisse
# résoudre un shader par son nom **dans le format que le périphérique accepte**.
#
# Le champ de format n'est pas décoratif : c'est lui qui permet au même binaire
# de porter du SPIR-V et du MSL, et surtout à `ns_rhi.c` de n'annoncer à SDL que
# les formats réellement embarqués. Sans lui, le moteur annonçait savoir produire
# du MSL, SDL lui rendait un périphérique Metal, et les seize shaders étaient
# refusés l'un après l'autre au démarrage.
#
# Volontairement sans dépendance externe (ni xxd, ni python) : c'est ce qui permet
# au build de se comporter pareil sur les trois plateformes.
#
# Entrée : ROWS = liste de "nom|symbole|format|chemin du blob".

if(NOT OUT_C OR NOT OUT_H)
    message(FATAL_ERROR "EmbedShaders: OUT_C et OUT_H sont requis")
endif()
if(NOT ROWS)
    message(FATAL_ERROR "EmbedShaders: aucun shader à embarquer")
endif()

set(body "")
set(header "")
set(registry "")
set(all_formats 0)

string(APPEND header "/* Généré par cmake/EmbedShaders.cmake — ne pas modifier à la main. */\n")
string(APPEND header "#ifndef NINETEEN_SHADER_BLOBS_H\n#define NINETEEN_SHADER_BLOBS_H\n\n")
string(APPEND header "#include <stddef.h>\n\n")
# SPIR-V est un flux de mots de 32 bits : l'aligner évite une copie au chargement.
# Le MSL est du texte, que l'aligner ne gêne pas.
# MSVC et GCC/Clang ne s'accordent pas sur la syntaxe, d'où la macro.
string(APPEND header "#if defined(_MSC_VER)\n")
string(APPEND header "#  define NS_ALIGN4 __declspec(align(4))\n")
string(APPEND header "#else\n")
string(APPEND header "#  define NS_ALIGN4 __attribute__((aligned(4)))\n")
string(APPEND header "#endif\n\n")

string(APPEND body "/* Généré par cmake/EmbedShaders.cmake — ne pas modifier à la main. */\n")
string(APPEND body "#include \"shader_blobs.h\"\n\n")

foreach(row IN LISTS ROWS)
    string(REPLACE "|" ";" parts "${row}")
    list(LENGTH parts nparts)
    if(NOT nparts EQUAL 4)
        message(FATAL_ERROR "EmbedShaders: ligne mal formée « ${row} » (attendu nom|symbole|format|chemin)")
    endif()
    list(GET parts 0 name)
    list(GET parts 1 sym)
    list(GET parts 2 format)
    list(GET parts 3 blob)

    if(NOT EXISTS "${blob}")
        message(FATAL_ERROR "EmbedShaders: blob introuvable pour ${name} : ${blob}")
    endif()

    file(READ "${blob}" hex HEX)
    string(LENGTH "${hex}" hexlen)
    math(EXPR bytelen "${hexlen} / 2")
    if(bytelen EQUAL 0)
        message(FATAL_ERROR "EmbedShaders: ${name} (format ${format}) est vide — ${blob}")
    endif()

    string(APPEND body "NS_ALIGN4 const unsigned char ns_${sym}[] = {")
    set(col 0)
    string(REGEX MATCHALL "[0-9a-f][0-9a-f]" bytes "${hex}")
    foreach(b IN LISTS bytes)
        if(col EQUAL 0)
            string(APPEND body "\n    ")
        endif()
        string(APPEND body "0x${b},")
        math(EXPR col "(${col} + 1) % 16")
    endforeach()
    string(APPEND body "\n};\n")
    string(APPEND body "const unsigned int ns_${sym}_len = ${bytelen}u;\n\n")

    string(APPEND header "extern const unsigned char ns_${sym}[];\n")
    string(APPEND header "extern const unsigned int  ns_${sym}_len;\n")
    string(APPEND registry "    { \"${name}\", ${format}u, ns_${sym}, &ns_${sym}_len },\n")

    math(EXPR all_formats "${all_formats} | ${format}")
endforeach()

# Registre : permet `ns_shader_lookup("gbuffer.vert", formats acceptés)` côté moteur.
string(APPEND header "\ntypedef struct ns_shader_blob {\n")
string(APPEND header "    const char          *name;\n")
string(APPEND header "    /* Valeur d'un SDL_GPU_SHADERFORMAT_*. Portée telle quelle plutôt que\n")
string(APPEND header "     * par une enum : ce fichier est produit par un script CMake, qui n'a\n")
string(APPEND header "     * aucune raison d'inclure SDL. */\n")
string(APPEND header "    unsigned int         format;\n")
string(APPEND header "    const unsigned char *bytes;\n")
string(APPEND header "    const unsigned int  *len;\n")
string(APPEND header "} ns_shader_blob;\n\n")
string(APPEND header "extern const ns_shader_blob ns_shader_registry[];\n")
string(APPEND header "extern const size_t         ns_shader_registry_count;\n\n")
string(APPEND header "/* OU de tous les formats embarqués. `ns_rhi.c` en fait le masque qu'il\n")
string(APPEND header " * présente à SDL_CreateGPUDevice : on ne peut donc plus demander un backend\n")
string(APPEND header " * que l'on ne sait pas alimenter. */\n")
string(APPEND header "extern const unsigned int   ns_shader_registry_formats;\n\n")
string(APPEND header "#endif /* NINETEEN_SHADER_BLOBS_H */\n")

string(APPEND body "const ns_shader_blob ns_shader_registry[] = {\n${registry}};\n")
string(APPEND body "const size_t ns_shader_registry_count = sizeof(ns_shader_registry) / sizeof(ns_shader_registry[0]);\n")
string(APPEND body "const unsigned int ns_shader_registry_formats = ${all_formats}u;\n")

file(WRITE "${OUT_H}" "${header}")
file(WRITE "${OUT_C}" "${body}")
