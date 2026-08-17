# EmbedShaders.cmake — script exécuté par `cmake -P` pendant le build.
#
# Transforme une liste de .spv en un couple .c/.h contenant les octets, plus un
# registre nom -> blob pour que le moteur puisse résoudre un shader par son nom.
#
# Volontairement sans dépendance externe (ni xxd, ni python) : c'est ce qui permet
# au build de se comporter pareil sur les trois plateformes.

if(NOT OUT_C OR NOT OUT_H)
    message(FATAL_ERROR "EmbedShaders: OUT_C et OUT_H sont requis")
endif()

set(body "")
set(header "")
set(registry "")

string(APPEND header "/* Généré par cmake/EmbedShaders.cmake — ne pas modifier à la main. */\n")
string(APPEND header "#ifndef NINETEEN_SHADER_BLOBS_H\n#define NINETEEN_SHADER_BLOBS_H\n\n")
string(APPEND header "#include <stddef.h>\n\n")
# SPIR-V est un flux de mots de 32 bits : l'aligner évite une copie au chargement.
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
    list(GET parts 0 name)
    list(GET parts 1 sym)

    # Retrouver le .spv correspondant dans SPV_FILES.
    set(spv "")
    foreach(candidate IN LISTS SPV_FILES)
        get_filename_component(cname "${candidate}" NAME)
        if(cname STREQUAL "${name}.spv")
            set(spv "${candidate}")
        endif()
    endforeach()
    if(NOT spv)
        message(FATAL_ERROR "EmbedShaders: SPIR-V introuvable pour ${name}")
    endif()

    file(READ "${spv}" hex HEX)
    string(LENGTH "${hex}" hexlen)
    math(EXPR bytelen "${hexlen} / 2")

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
    string(APPEND registry "    { \"${name}\", ns_${sym}, &ns_${sym}_len },\n")
endforeach()

# Registre : permet `ns_shader_lookup("room_gbuffer.vert")` côté moteur.
string(APPEND header "\ntypedef struct ns_shader_blob {\n")
string(APPEND header "    const char          *name;\n")
string(APPEND header "    const unsigned char *bytes;\n")
string(APPEND header "    const unsigned int  *len;\n")
string(APPEND header "} ns_shader_blob;\n\n")
string(APPEND header "extern const ns_shader_blob ns_shader_registry[];\n")
string(APPEND header "extern const size_t         ns_shader_registry_count;\n\n")
string(APPEND header "#endif /* NINETEEN_SHADER_BLOBS_H */\n")

string(APPEND body "const ns_shader_blob ns_shader_registry[] = {\n${registry}};\n")
string(APPEND body "const size_t ns_shader_registry_count = sizeof(ns_shader_registry) / sizeof(ns_shader_registry[0]);\n")

# Les fichiers d'embarquage compilent avec -Wall : couper le bruit de -pedantic
# sur les tableaux volumineux n'est pas nécessaire, mais on garde l'en-tête propre.
file(WRITE "${OUT_H}" "${header}")
file(WRITE "${OUT_C}" "${body}")
