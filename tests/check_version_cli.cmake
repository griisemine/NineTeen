# `--version` : le binaire sait-il DIRE lequel il est ?
#
# Ce qu'on empêche de revenir, mesuré. `cmake --preset` configure sans
# construire ; le binaire lancé après deux lignes de `cmake` datait du
# 1er septembre 21:51 — cinq jours et vingt-quatre commits en arrière, avec les
# assets du même jour (`salle.gltf` de 224199 octets et 499 noms, contre 245344
# et 563 le jour même). Interrogé, il répondait « option inconnue : --version »,
# et `--help` sortait la MÊME bannière que le binaire du jour, mot pour mot.
# Deux exécutables distants de cinq jours étaient indiscernables.
#
# Quatre choses vérifiées :
#   1. `--version` sort avec le code 0 — un script d'intégration en dépend ;
#   2. il imprime la version que CMake a compilée, et pas une autre ;
#   3. il imprime les quatre champs : commit, date de construction, assets,
#      sources. Un champ oublié rendrait la sortie inutile à la question posée ;
#   4. sa sortie DIFFÈRE de celle de `--help` : c'est l'indistinguabilité
#      elle-même que l'on refuse, et une bannière recopiée la ramènerait.

if(NOT EXE OR NOT VERSION_ATTENDUE)
    message(FATAL_ERROR "EXE et VERSION_ATTENDUE sont requis")
endif()

execute_process(COMMAND "${EXE}" --version
                OUTPUT_VARIABLE sortie ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--version a rendu ${rc} au lieu de 0 :\n${sortie}${err}")
endif()

if(NOT sortie MATCHES "Nineteen ${VERSION_ATTENDUE}")
    message(FATAL_ERROR
            "--version n'annonce pas « Nineteen ${VERSION_ATTENDUE} » :\n${sortie}")
endif()

foreach(_champ commit construit assets sources)
    if(NOT sortie MATCHES "${_champ}[ ]*:")
        message(FATAL_ERROR "--version n'imprime pas « ${_champ} » :\n${sortie}")
    endif()
endforeach()

# Le commit ne doit pas rester vide : bâti depuis un dépôt, il vaut une empreinte
# git ; bâti ailleurs, il vaut « inconnu » — mais jamais rien.
if(sortie MATCHES "commit[ ]*:[ ]*\n")
    message(FATAL_ERROR "--version imprime un commit vide :\n${sortie}")
endif()

execute_process(COMMAND "${EXE}" --help
                OUTPUT_VARIABLE aide ERROR_QUIET RESULT_VARIABLE rc_aide)
if(NOT rc_aide EQUAL 0)
    message(FATAL_ERROR "--help a rendu ${rc_aide} au lieu de 0")
endif()
if(sortie STREQUAL aide)
    message(FATAL_ERROR "--version rend exactement la même chose que --help")
endif()
if(NOT aide MATCHES "--version")
    message(FATAL_ERROR "--help ne mentionne pas --version :\n${aide}")
endif()

message(STATUS "version : code 0, ${VERSION_ATTENDUE}, commit, date, assets et sources")
