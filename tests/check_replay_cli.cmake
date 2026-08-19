# Le rejeu en ligne de commande, de bout en bout.
#
# `tests/test_replay.c` prouve que les huit jeux se rejouent à l'identique EN
# MÉMOIRE. Ce contrôle-ci vérifie l'outil que le joueur utilise vraiment :
# `--rejouer=<fichier>` lit un journal sur disque, remonte la partie et imprime
# ce qu'elle donne. Ce sont deux choses différentes — le format du fichier, son
# analyse, la reprise de la graine et de la difficulté ne sont exercés que par
# celle-ci.
#
# Trois choses vérifiées, dont deux échecs :
#   1. un journal valide rejoue, deux fois de suite, à la sortie près ;
#   2. un fichier qui n'est pas un journal est REFUSÉ avec un code non nul ;
#   3. un fichier absent aussi.
# Sans les deux derniers, un outil qui rendrait 0 sur n'importe quoi passerait.

if(NOT EXE OR NOT JOURNAL)
    message(FATAL_ERROR "EXE et JOURNAL sont requis")
endif()

execute_process(COMMAND "${EXE}" "--rejouer=${JOURNAL}"
                OUTPUT_VARIABLE out1 ERROR_QUIET RESULT_VARIABLE rc1)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "le rejeu d'un journal valide a échoué (code ${rc1})")
endif()
if(NOT out1 MATCHES "score")
    message(FATAL_ERROR "le rejeu n'a pas imprimé de score :\n${out1}")
endif()

execute_process(COMMAND "${EXE}" "--rejouer=${JOURNAL}"
                OUTPUT_VARIABLE out2 ERROR_QUIET RESULT_VARIABLE rc2)
if(NOT out1 STREQUAL out2)
    message(FATAL_ERROR "deux rejeux du MÊME journal diffèrent :\n---\n${out1}---\n${out2}")
endif()

# Un fichier qui n'est pas un journal.
set(_bad "${CMAKE_CURRENT_BINARY_DIR}/pas-un-journal.txt")
file(WRITE "${_bad}" "ceci n'est pas un journal\n")
execute_process(COMMAND "${EXE}" "--rejouer=${_bad}"
                OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE rc3)
if(rc3 EQUAL 0)
    message(FATAL_ERROR "un fichier quelconque a été accepté comme journal")
endif()

# Un fichier absent.
execute_process(COMMAND "${EXE}" "--rejouer=${CMAKE_CURRENT_BINARY_DIR}/absent.txt"
                OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE rc4)
if(rc4 EQUAL 0)
    message(FATAL_ERROR "un journal absent a été accepté")
endif()

message(STATUS "rejeu : journal valide reproductible, fichier invalide et absent refusés")
