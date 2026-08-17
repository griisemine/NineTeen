# CompilerWarnings.cmake — hygiène de compilation.
#
# L'original compilait avec `-g -c -Wall` et laissait passer les avertissements.
# Ici, les avertissements qui traduisent un vrai bug (débordement de tampon, format,
# retour ignoré) sont des erreurs : c'est la première ligne de défense, gratuite.

function(nineteen_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /we4013   # appel de fonction non déclarée
            /we4020   # trop d'arguments
            /we4715   # chemin sans valeur de retour
            /wd4996   # « fonction non sécurisée » : on gère nous-mêmes le bornage
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra
            -Wshadow                    # variable masquée : source de bugs classique
            -Wpointer-arith
            -Wcast-qual
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wwrite-strings
            -Wno-unused-parameter       # fréquent et légitime dans les callbacks
            # Ceux-ci sont des bugs, pas des remarques :
            -Werror=implicit-function-declaration
            -Werror=incompatible-pointer-types
            -Werror=return-type
            -Werror=format-security
            -Werror=array-bounds
        )
        # Durcissement du binaire distribué.
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${target} PRIVATE -D_FORTIFY_SOURCE=2 -fstack-protector-strong)
        endif()
    endif()
endfunction()

# Sanitizers : activés par preset sur Linux (voir CMakePresets.json).
option(NINETEEN_SANITIZE "Compiler avec ASan + UBSan" OFF)

function(nineteen_apply_sanitizers target)
    if(NINETEEN_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
