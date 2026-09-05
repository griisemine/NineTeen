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

    # ----------------------------------------------------------------------
    # `_DEFAULT_SOURCE` sur les glibc : ce que `-std=c11` cache, et qui manque
    # ----------------------------------------------------------------------
    #
    # `CMAKE_C_EXTENSIONS OFF` fait compiler en `-std=c11` et non `-std=gnu11`.
    # Le compilateur pose alors `__STRICT_ANSI__`, et les en-têtes de la glibc
    # RETIRENT tout ce que la norme C ne mentionne pas : `popen`, `pclose`,
    # `getaddrinfo`, `struct addrinfo`… Le projet les emploie, et
    # `-Werror=implicit-function-declaration` (deux lignes plus haut) transforme
    # l'omission en erreur de compilation.
    #
    # Pourquoi personne ne l'avait vu : la CI construit sur `ubuntu-24.04`, dont
    # la glibc 2.39 les expose quand même. MESURÉ sur ubuntu:22.04 (glibc 2.35),
    # la base choisie pour l'AppImage — voir packaging/linux/Dockerfile.build,
    # qui explique pourquoi c'est cette version-là :
    #
    #   engine/net/ns_lockstep.c:215  error: storage size of 'hints' isn't known
    #   engine/net/ns_lockstep.c:221  error: implicit declaration of 'getaddrinfo'
    #   tests/test_place.c:53         error: implicit declaration of 'popen'
    #
    # Le jeu ne compilait donc PAS sur Debian 12 ni sur Ubuntu 22.04 LTS, et le
    # message n'aidait pas à comprendre pourquoi. `_DEFAULT_SOURCE` remet
    # exactement ce que la glibc expose lorsqu'aucun mode strict n'est demandé —
    # c'est la macro que gcc pose lui-même en `-std=gnu11`. Elle n'ajoute aucune
    # extension au langage : elle rend visibles des déclarations POSIX qui
    # existaient déjà dans les bibliothèques liées.
    #
    # Posée ici plutôt que dans chaque `CMakeLists.txt` : cette fonction est
    # appelée par TOUTES nos cibles (36 au dernier compte) et par aucune cible
    # tierce, qui ont leurs propres macros de test de fonctionnalités.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PRIVATE _DEFAULT_SOURCE)
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
