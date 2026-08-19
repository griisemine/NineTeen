# Vérifie qu'aucune carte de normales ou d'ORM ne dépasse le plafond demandé.
#
# Deux formats, deux en-têtes, quatre octets à lire dans chacun — pas de
# décodeur à embarquer :
#
#   PNG    signature de 8 octets, puis un chunk IHDR dont la largeur et la
#          hauteur sont deux entiers 32 bits GROS-boutistes, aux offsets 16 et 20.
#   nstex  « NSTX », version, format, niveaux, puis largeur et hauteur en
#          entiers 32 bits PETIT-boutistes, aux offsets 8 et 12
#          (voir `engine/core/nstex.h`).
#
# Le boutisme diffère parce que PNG est un format d'échange né gros-boutiste et
# que `nstex` est le nôtre, aligné sur les machines qui l'écrivent. Se tromper
# ici donnerait des dimensions absurdes — 16 777 216 au lieu de 1 — donc un test
# qui hurle plutôt qu'un test qui ment.
#
# Ce fichier a déjà servi une fois : au passage des cartes en `.nstex`, il ne
# trouvait plus un seul PNG et A ÉCHOUÉ au lieu de passer à vide. C'est
# exactement ce qu'on lui demande.
if(NOT MAX_MAP OR MAX_MAP EQUAL 0)
    message(STATUS "aucun plafond demandé — rien à vérifier")
    return()
endif()

file(GLOB _maps
     "${MAPS_DIR}/*_n.png"   "${MAPS_DIR}/*_orm.png"
     "${MAPS_DIR}/*_n.nstex" "${MAPS_DIR}/*_orm.nstex")
list(LENGTH _maps _count)
if(_count EQUAL 0)
    message(FATAL_ERROR "aucune carte trouvée dans « ${MAPS_DIR} » : le test ne vérifie rien")
endif()

# Un entier 32 bits petit-boutiste depuis quatre octets en hexadécimal.
function(_le32 hex out)
    string(SUBSTRING "${hex}" 0 2 _b0)
    string(SUBSTRING "${hex}" 2 2 _b1)
    string(SUBSTRING "${hex}" 4 2 _b2)
    string(SUBSTRING "${hex}" 6 2 _b3)
    math(EXPR _v "0x${_b3}${_b2}${_b1}${_b0}")
    set(${out} "${_v}" PARENT_SCOPE)
endfunction()

set(_bad 0)
set(_png 0)
set(_bc 0)
foreach(_m IN LISTS _maps)
    if(_m MATCHES "\\.nstex$")
        # En HEXADÉCIMAL, pas en texte : `file(READ)` sur du binaire s'arrête au
        # premier octet nul et rend une chaîne tronquée. La signature « NSTX »
        # vaut 4e 53 54 58.
        file(READ "${_m}" _sig OFFSET 0 LIMIT 4 HEX)
        if(NOT _sig STREQUAL "4e535458")
            message(SEND_ERROR "${_m} : ce n'est pas un fichier nstex")
            math(EXPR _bad "${_bad} + 1")
            continue()
        endif()
        file(READ "${_m}" _hex OFFSET 8 LIMIT 8 HEX)
        string(SUBSTRING "${_hex}" 0 8 _wh)
        string(SUBSTRING "${_hex}" 8 8 _hh)
        _le32("${_wh}" _w)
        _le32("${_hh}" _h)
        math(EXPR _bc "${_bc} + 1")
    else()
        file(READ "${_m}" _hex OFFSET 16 LIMIT 8 HEX)
        string(SUBSTRING "${_hex}" 0 8 _wh)
        string(SUBSTRING "${_hex}" 8 8 _hh)
        math(EXPR _w "0x${_wh}")
        math(EXPR _h "0x${_hh}")
        math(EXPR _png "${_png} + 1")
    endif()

    if(_w GREATER MAX_MAP OR _h GREATER MAX_MAP)
        get_filename_component(_n "${_m}" NAME)
        message(SEND_ERROR "${_n} : ${_w}x${_h}, au-delà du plafond de ${MAX_MAP}")
        math(EXPR _bad "${_bad} + 1")
    endif()
endforeach()

if(_bad GREATER 0)
    message(FATAL_ERROR "${_bad} carte(s) en défaut")
endif()
message(STATUS "${_count} cartes (${_bc} en blocs, ${_png} en PNG), toutes au plus ${MAX_MAP} de côté")
