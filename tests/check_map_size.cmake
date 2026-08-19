# Relit l'entête IHDR de chaque carte _n / _orm et vérifie qu'aucune ne dépasse
# MAX_MAP. Un PNG commence par 8 octets de signature, puis un chunk IHDR dont
# la largeur et la hauteur sont deux entiers 32 bits gros-boutistes aux
# offsets 16 et 20 : quatre octets à lire, pas de décodeur à embarquer.
if(NOT MAX_MAP OR MAX_MAP EQUAL 0)
    message(STATUS "aucun plafond demandé — rien à vérifier")
    return()
endif()

file(GLOB _maps "${MAPS_DIR}/*_n.png" "${MAPS_DIR}/*_orm.png")
list(LENGTH _maps _count)
if(_count EQUAL 0)
    message(FATAL_ERROR "aucune carte trouvée dans « ${MAPS_DIR} » : le test ne vérifie rien")
endif()

set(_bad 0)
foreach(_m IN LISTS _maps)
    file(READ "${_m}" _hex OFFSET 16 LIMIT 8 HEX)
    string(SUBSTRING "${_hex}" 0 8 _wh)
    string(SUBSTRING "${_hex}" 8 8 _hh)
    math(EXPR _w "0x${_wh}")
    math(EXPR _h "0x${_hh}")
    if(_w GREATER MAX_MAP OR _h GREATER MAX_MAP)
        get_filename_component(_n "${_m}" NAME)
        message(SEND_ERROR "${_n} : ${_w}x${_h}, au-delà du plafond de ${MAX_MAP}")
        math(EXPR _bad "${_bad} + 1")
    endif()
endforeach()

if(_bad GREATER 0)
    message(FATAL_ERROR "${_bad} carte(s) au-dessus du plafond")
endif()
message(STATUS "${_count} cartes, toutes au plus ${MAX_MAP} de côté")
