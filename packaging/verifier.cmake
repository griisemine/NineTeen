# verifier.cmake — le test `paquets` : ce que l'on peut prouver sur la
# configuration d'empaquetage SANS avoir la plateforme sous la main.
#
# POURQUOI CE FICHIER EXISTE
# --------------------------
# Les réglages Windows de `packaging/CPackNineteen.cmake` ne sont exécutés par
# personne avant le jour de la release : ce Mac n'a ni makensis, ni mingw-w64,
# ni wine, et `cpack -G NSIS` s'arrête AVANT même de lire le fichier
# (« Cannot find NSIS compiler makensis » — l'initialisation du générateur passe
# en premier). Une faute de frappe dans un nom de fichier d'icône, un chemin de
# licence qui n'existe plus après un renommage : rien ne le dirait, et on
# l'apprendrait sur une balise poussée.
#
# Ce script relit donc `CPackNineteen.cmake` une fois par générateur, exactement
# comme `cpack` le fait, et vérifie ce qui est vérifiable de partout : que les
# variables attendues sont posées, que les fichiers désignés existent, et que la
# licence affichée par NSIS ne contient QUE de l'ASCII — parce que le modèle NSIS
# de CPack compile un installateur ANSI et qu'un accent y sortirait en mojibake,
# ce dont on ne s'apercevrait qu'en regardant l'installateur tourner.
#
# CE QU'IL NE PROUVE PAS : que makensis accepte le script produit, que l'icône
# est valide à ses yeux, ou que l'installateur s'installe. Rien ici ne remplace
# le job Windows de la CI.
#
#   cmake -DNINETEEN_SOURCE_DIR=<racine> -P packaging/verifier.cmake

cmake_minimum_required(VERSION 3.21)

if(NOT NINETEEN_SOURCE_DIR)
    message(FATAL_ERROR "NINETEEN_SOURCE_DIR n'est pas défini")
endif()

set(_echecs 0)

function(echoue message)
    message(SEND_ERROR "ÉCHEC — ${message}")
    math(EXPR _echecs "${_echecs} + 1")
    set(_echecs "${_echecs}" PARENT_SCOPE)
endfunction()

# Rejoue ce que fait `cpack` : poser CPACK_GENERATOR, puis inclure le fichier de
# configuration du projet. Les variables qu'il pose restent dans la portée du
# répertoire, d'où la remise à zéro explicite entre deux générateurs.
macro(charger generateur)
    unset(CPACK_PACKAGE_FILE_NAME)
    unset(CPACK_PACKAGING_INSTALL_PREFIX)
    unset(CPACK_INCLUDE_TOPLEVEL_DIRECTORY)
    unset(CPACK_DMG_VOLUME_NAME)
    unset(CPACK_PRE_BUILD_SCRIPTS)
    unset(CPACK_POST_BUILD_SCRIPTS)
    unset(CPACK_DEBIAN_FILE_NAME)
    unset(CPACK_DEBIAN_PACKAGE_SHLIBDEPS)
    unset(CPACK_NSIS_DEFINES)
    unset(CPACK_NSIS_INSTALL_ROOT)
    unset(CPACK_NSIS_MUI_ICON)
    unset(CPACK_PACKAGE_EXECUTABLES)
    unset(CPACK_RESOURCE_FILE_LICENSE)
    set(CPACK_GENERATOR "${generateur}")
    include("${NINETEEN_SOURCE_DIR}/packaging/CPackNineteen.cmake")
endmacro()

# Les entrées que le CPackConfig généré fournit d'ordinaire.
set(CPACK_PACKAGE_VERSION "0.0.0")
set(CPACK_PACKAGE_CONTACT "essai <essai@example.invalid>")
set(CPACK_NINETEEN_ARCH "x86_64")
set(CPACK_NINETEEN_SOURCE_DIR "${NINETEEN_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# macOS
# ---------------------------------------------------------------------------
charger("DragNDrop")
if(NOT CPACK_PACKAGE_FILE_NAME MATCHES "^Nineteen-0\\.0\\.0-macOS-universal$")
    echoue("DragNDrop : nom de paquet inattendu « ${CPACK_PACKAGE_FILE_NAME} »")
endif()
# Un répertoire racine enterrerait Nineteen.app d'un cran dans la fenêtre du
# .dmg, à côté du raccourci /Applications sur lequel il faut le glisser.
if(NOT DEFINED CPACK_INCLUDE_TOPLEVEL_DIRECTORY OR CPACK_INCLUDE_TOPLEVEL_DIRECTORY)
    echoue("DragNDrop : le .dmg ne doit PAS avoir de répertoire racine")
endif()
if(NOT CPACK_DMG_VOLUME_NAME)
    echoue("DragNDrop : volume sans nom")
endif()
foreach(_f "packaging/macos/Info.plist.in" "packaging/macos/nineteen.icns")
    if(NOT EXISTS "${NINETEEN_SOURCE_DIR}/${_f}")
        echoue("macOS : ${_f} manque — le bundle serait un simple dossier")
    endif()
endforeach()

# LES DEUX CROCHETS DE SIGNATURE. Sans eux `cpack` produit toujours un .dmg —
# c'est là le danger : il sortirait sans signature ad hoc du bundle, sans
# `A-LIRE-AVANT-D-OUVRIR.txt` et sans manifeste, et RIEN ne le dirait. Un
# renommage de `signature.cmake` ou une faute de frappe dans ce chemin
# passeraient jusqu'à la balise. C'est exactement le genre de panne muette pour
# lequel ce fichier existe.
foreach(_c CPACK_PRE_BUILD_SCRIPTS CPACK_POST_BUILD_SCRIPTS)
    if(NOT ${_c})
        echoue("DragNDrop : ${_c} n'est pas posé — le .dmg partirait sans signature ni lisez-moi")
    elseif(NOT EXISTS "${${_c}}")
        echoue("DragNDrop : ${_c} désigne « ${${_c}} », qui n'existe pas")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Linux
# ---------------------------------------------------------------------------
charger("TGZ")
if(NOT CPACK_PACKAGE_FILE_NAME STREQUAL "nineteen-0.0.0-linux-x86_64")
    echoue("TGZ : nom de paquet inattendu « ${CPACK_PACKAGE_FILE_NAME} »")
endif()
# L'archive se déballe où l'on veut : un préfixe /usr y graverait des chemins
# absolus qui n'ont de sens qu'installés.
if(NOT DEFINED CPACK_PACKAGING_INSTALL_PREFIX OR NOT CPACK_PACKAGING_INSTALL_PREFIX STREQUAL "")
    echoue("TGZ : le préfixe d'installation doit être vide")
endif()
if(NOT CPACK_INCLUDE_TOPLEVEL_DIRECTORY)
    echoue("TGZ : sans répertoire racine, le déballage déverse bin/ lib/ share/ dans le dossier courant")
endif()

charger("DEB")
if(NOT CPACK_PACKAGING_INSTALL_PREFIX STREQUAL "/usr")
    echoue("DEB : préfixe « ${CPACK_PACKAGING_INSTALL_PREFIX} », attendu /usr")
endif()
# Sans SHLIBDEPS le .deb sort SANS aucune dépendance : il s'installe partout et
# ne démarre nulle part.
if(NOT CPACK_DEBIAN_PACKAGE_SHLIBDEPS)
    echoue("DEB : CPACK_DEBIAN_PACKAGE_SHLIBDEPS doit être ON")
endif()
if(NOT CPACK_DEBIAN_FILE_NAME STREQUAL "DEB-DEFAULT")
    echoue("DEB : le nom doit suivre la convention Debian (DEB-DEFAULT)")
endif()
foreach(_f "packaging/nineteen.desktop" "packaging/nineteen.png")
    if(NOT EXISTS "${NINETEEN_SOURCE_DIR}/${_f}")
        echoue("Linux : ${_f} manque — ni menu, ni AppImage")
    endif()
endforeach()
if(NOT EXISTS "${NINETEEN_SOURCE_DIR}/packaging/linux/paquets.sh")
    echoue("Linux : packaging/linux/paquets.sh manque — pas d'AppImage")
endif()

# ---------------------------------------------------------------------------
# Windows — la partie que personne n'exécute avant la release
# ---------------------------------------------------------------------------
charger("NSIS")
# Le modèle de CPack pose `RequestExecutionLevel admin` en dur ; c'est cette
# ligne, insérée deux lignes plus bas, qui le renverse. La perdre rendrait
# l'installateur inutilisable sur un poste sans droits d'administration, et rien
# d'autre ne le dirait.
if(NOT CPACK_NSIS_DEFINES MATCHES "RequestExecutionLevel[ \t]+user")
    echoue("NSIS : l'installateur redemanderait les droits administrateur")
endif()
if(NOT CPACK_NSIS_INSTALL_ROOT MATCHES "^\\$LOCALAPPDATA")
    echoue("NSIS : racine « ${CPACK_NSIS_INSTALL_ROOT} » — hors du domaine de l'utilisateur")
endif()
if(NOT CPACK_PACKAGE_EXECUTABLES)
    echoue("NSIS : aucun raccourci de menu Démarrer déclaré")
endif()
if(NOT CPACK_NSIS_MUI_ICON OR NOT EXISTS "${CPACK_NSIS_MUI_ICON}")
    echoue("NSIS : icône introuvable « ${CPACK_NSIS_MUI_ICON} »")
endif()

# L'icône doit être un vrai .ico, et non un PNG renommé : makensis refuse le
# second, et on ne s'en apercevrait qu'à la release. Signature ICONDIR : deux
# octets à zéro, puis le type 1 en petit-boutiste.
file(READ "${CPACK_NSIS_MUI_ICON}" _ico HEX LIMIT 6)
if(NOT _ico MATCHES "^00000100")
    echoue("NSIS : « ${CPACK_NSIS_MUI_ICON} » n'a pas l'en-tête d'un .ico (${_ico})")
endif()

if(NOT CPACK_RESOURCE_FILE_LICENSE OR NOT EXISTS "${CPACK_RESOURCE_FILE_LICENSE}")
    echoue("NSIS : licence introuvable « ${CPACK_RESOURCE_FILE_LICENSE} »")
else()
    # ASCII SEUL. Le modèle NSIS de CPack ne déclare pas `Unicode true` :
    # makensis compile un installateur ANSI, qui afficherait « é » en « Ã© ».
    # On ne peut pas voir l'installateur tourner d'ici ; on peut en revanche
    # garantir que la question ne se pose pas.
    #
    # On découpe D'ABORD en octets (`..`) avant de regarder le bit de poids
    # fort. Chercher directement « un chiffre de 8 à f suivi d'un autre » dans la
    # chaîne hexadécimale entière donnerait des faux positifs à cheval sur deux
    # octets : « 0a20 » (saut de ligne puis espace, deux octets ASCII parfaits)
    # contient « a2 » à l'indice 1.
    file(READ "${CPACK_RESOURCE_FILE_LICENSE}" _lic HEX)
    string(REGEX MATCHALL ".." _octets "${_lic}")
    set(_hors 0)
    foreach(_o IN LISTS _octets)
        if(_o MATCHES "^[89a-f]")
            math(EXPR _hors "${_hors} + 1")
        endif()
    endforeach()
    if(_hors GREATER 0)
        echoue("NSIS : la licence contient ${_hors} octet(s) hors ASCII — mojibake garanti dans un installateur ANSI")
    endif()
endif()

if(_echecs EQUAL 0)
    message(STATUS "paquets : les quatre générateurs sont configurés et leurs fichiers sont là")
endif()
