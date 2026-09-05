# Le déterminisme d'un état, mesuré ENTRE DEUX PROCESSUS.
#
# `tests/test_replay.c` rejoue deux fois dans le MÊME processus et compare les
# états au bit près. C'est nécessaire et ce n'est pas suffisant : dans un seul
# processus, une adresse est stable. L'état de Piano a ainsi porté un
# `const char *` vers une chaîne littérale pendant tout le projet — un état que
# `test_replay` déclarait identique, et dont l'empreinte changeait à chaque
# lancement du jeu.
#
# Ce contrôle-ci rejoue chaque journal dans DEUX processus séparés et compare
# l'empreinte imprimée par `--rejouer=`. Sous l'ASLR du système, deux processus
# ne placent pas leurs littéraux au même endroit : un pointeur dans un état s'y
# voit immédiatement.
#
# C'est la première des trois briques que `docs/RESEAU-TEMPS-REEL.md` réclame
# pour un duel en pas verrouillé. Sans elle, deux parties qui divergent
# continuent chacune de leur côté jusqu'à ce que les scores se contredisent.

if(NOT EXE OR NOT DATA)
    message(FATAL_ERROR "EXE et DATA sont requis")
endif()

file(GLOB _journaux "${DATA}/rejeu-*.txt")
list(LENGTH _journaux _n)
if(_n LESS 8)
    message(FATAL_ERROR "attendu au moins huit journaux dans ${DATA}, trouvé ${_n} — "
                        "un jeu ajouté sans son journal passerait sans être mesuré")
endif()

foreach(journal IN LISTS _journaux)
    get_filename_component(nom "${journal}" NAME_WE)

    execute_process(COMMAND "${EXE}" "--rejouer=${journal}"
                    OUTPUT_VARIABLE out1 ERROR_QUIET RESULT_VARIABLE rc1)
    if(NOT rc1 EQUAL 0)
        message(FATAL_ERROR "${nom} : le rejeu a échoué (code ${rc1})")
    endif()
    if(NOT out1 MATCHES "empreinte [.]+ ([0-9a-f]+)")
        message(FATAL_ERROR "${nom} : aucune empreinte imprimée :\n${out1}")
    endif()
    set(h1 "${CMAKE_MATCH_1}")

    # Un SECOND processus. C'est tout l'intérêt : même binaire, même journal,
    # mais un espace d'adressage neuf.
    execute_process(COMMAND "${EXE}" "--rejouer=${journal}"
                    OUTPUT_VARIABLE out2 ERROR_QUIET RESULT_VARIABLE rc2)
    string(REGEX MATCH "empreinte [.]+ ([0-9a-f]+)" _m "${out2}")
    set(h2 "${CMAKE_MATCH_1}")

    if(NOT h1 STREQUAL h2)
        message(FATAL_ERROR
            "${nom} : deux processus rejouant le MÊME journal donnent deux états "
            "différents.\n  ${h1}\n  ${h2}\n"
            "Un état doit être une VALEUR : ni pointeur, ni adresse, ni lecture "
            "d'horloge. Voir games.h, règle 2.")
    endif()
    message(STATUS "  ${nom} : ${h1}")
endforeach()

message(STATUS "déterminisme : ${_n} journaux, empreinte identique entre deux processus")
