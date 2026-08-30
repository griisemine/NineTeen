# room/ — la salle d'arcade : l'exécutable principal.

add_executable(nineteen
    main.c
    room_camera.c
    room_viewmodel.c
    room_hud.c
    room_attract.c
    room_menu.c
    room_economie.c
    room_pad.c
    room_poste.c
    room_couperet.c
    room_presence.c
    room_credits.c
    room_sound.c
    room_door.c
)

# `nineteen::games` porte deja `nineteen::engine` en PUBLIC (games/CMakeLists.txt:25) :
# le renommer ici le poserait DEUX FOIS sur la ligne d'edition de liens, et `ld` le dit
# a chaque cible (« ignoring duplicate libraries »). Un avertissement qui sort a chaque
# build est un avertissement que plus personne ne lit.
target_link_libraries(nineteen PRIVATE nineteen::games)
target_include_directories(nineteen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
nineteen_set_warnings(nineteen)
nineteen_apply_sanitizers(nineteen)

# L'ADRESSE DU SERVEUR, CUITE DANS LE BINAIRE — et VIDE par défaut.
#
# Ce qu'elle permet : bâtir un paquet qui parle au bon serveur sans que le joueur
# ait quoi que ce soit à régler.
#
#     cmake -B build -DNINETEEN_SERVER_URL=http://arcade.example:8080
#
# CE VIDE N'EST PAS UN OUBLI, c'est la règle du haut de `engine/net/ns_online.h` :
# sans URL configurée, aucune socket n'est ouverte et le fil réseau ne démarre
# même pas. Un dépôt cloné et bâti tel quel ne doit parler à personne. Y mettre
# une valeur « pratique » ferait de chaque compilation par défaut une connexion
# sortante que personne n'a demandée — et la promesse cesserait d'être une
# promesse.
#
# C'est le maillon le plus FAIBLE de la chaîne : config, environnement et
# `--server=` le battent, dans cet ordre. Le raisonnement est écrit une fois pour
# toutes au-dessus de `ns_online_resolve_url` dans `engine/net/ns_online.h`.
set(NINETEEN_SERVER_URL "" CACHE STRING
    "URL par defaut du serveur de classement, cuite dans le binaire (vide = hors ligne)")

# CE QUI NE SE COMPILE PAS, ET POURQUOI ON REFUSE.
#
#  * LE JETON (`NS_CFG_SERVER_TOKEN`). Un secret cuit dans un binaire distribué
#    n'est pas un secret : `strings nineteen | grep -F <jeton>` le rend, sans
#    outil, sans désassemblage, sur la machine de n'importe qui l'a téléchargé.
#    Et il serait le MÊME pour tous les joueurs, donc irrévocable sans refaire un
#    paquet. Le jeton reste ce qu'il est — une session, obtenue au lancement,
#    propre à une personne — et il n'aura jamais de `-D`. La même réponse vaut
#    pour tout mot de passe, clé d'API ou empreinte que quelqu'un voudrait
#    « juste passer au build ».
#
#  * UN NOM DE SERVEUR (`NINETEEN_SERVER_NAME`). Refusé pour l'autre raison,
#    celle du bloc lignes 37-59 de `engine/core/ns_config.h` : PERSONNE NE LE
#    LIRAIT. Il n'y a pas un pixel de l'interface qui affiche un nom de serveur,
#    donc la définition serait une clé sans lecteur — « une clé qu'on ne peut pas
#    lire ne documente rien, elle laisse croire qu'un réglage existe ». Ce dépôt
#    a déjà retiré deux clés pour ce motif exact, dont `network.serverUrl` lui-
#    même, revenu seulement le jour où il a eu un lecteur. Le jour où un écran
#    affichera le nom du serveur, la définition viendra avec, pas avant.
#
# Le jeu doit trouver ses assets qu'il tourne depuis l'arbre de build ou depuis
# un paquet installé ; le chemin de build est transmis au compilateur et sert
# de point de montage supplémentaire en développement.
target_compile_definitions(nineteen PRIVATE
    NINETEEN_BUILD_ASSET_DIR="${NINETEEN_GEN_DIR}"
    NINETEEN_VERSION="${PROJECT_VERSION}"
    NINETEEN_SERVER_URL="${NINETEEN_SERVER_URL}"
)

add_dependencies(nineteen nineteen_assets)

# --------------------------------------------------------------------------
# Installation
# --------------------------------------------------------------------------
#
# LE BINAIRE SEUL NE SUFFIT PAS, et c'est ce que cette règle faisait jusqu'ici.
#
# `ns_paths` monte « <dossier du binaire>/assets » — c'est la disposition d'un
# paquet installé, et le commentaire de `ns_paths.c:82` le dit. Or seul
# l'exécutable était installé : un `cmake --install` produisait un arbre où le
# jeu ne trouvait ni scène, ni texture, ni son, et se serait arrêté sur
# « scène introuvable ». Personne ne s'en apercevait parce que personne
# n'installait — on lançait toujours depuis l'arbre de build, où le chemin est
# passé en dur par `NINETEEN_BUILD_ASSET_DIR`.
#
# Les assets vont donc À CÔTÉ du binaire, exactement là où le moteur les cherche
# — et « à côté du binaire » ne veut pas dire le même répertoire sur les trois
# plateformes. Trois dispositions, une seule règle.
#
# ------------------------------------------------------------------------
# macOS : Contents/Resources, et surtout PAS Contents/MacOS
# ------------------------------------------------------------------------
# Un `.dmg` qu'on glisse dans Applications demande un vrai bundle. La question
# est alors : où poser les assets pour que `ns_paths` les trouve sans qu'on
# touche au moteur ? La réponse est dans SDL, pas dans une convention :
#
#   SDL3/src/filesystem/cocoa/SDL_sysfilesystem.m:51
#   « this returns the exedir for non-bundled and the resourceDir for bundled »
#
# `SDL_GetBasePath()` rend donc `Nineteen.app/Contents/Resources/` dès qu'il y a
# un Info.plist, et NON `Contents/MacOS/`. Poser les assets à côté de
# l'exécutable — le réflexe — les mettrait dans `Contents/MacOS/assets`, que
# `ns_paths` ne regarderait jamais : le jeu s'arrêterait sur « scène
# introuvable » depuis le .dmg, et seulement depuis le .dmg, parce que sur la
# machine de construction `NINETEEN_BUILD_ASSET_DIR` l'aurait sauvé.
#
# `Contents/Resources` demande donc ZÉRO ligne de moteur : ni point de montage
# de plus, ni cas particulier dans `ns_paths.c`. Le comportement est en outre
# ÉCRIT dans notre Info.plist (`SDL_FILESYSTEM_BASE_DIR_TYPE = resource`) plutôt
# que subi par défaut, pour qu'un futur changement d'avis se lise.
#
# ------------------------------------------------------------------------
# Linux : lib/nineteen, et bin/nineteen n'est qu'un lien
# ------------------------------------------------------------------------
# Un `.deb` s'installe sous `/usr`. Y poser `bin/assets` donnerait
# `/usr/bin/assets/` : un répertoire de données dans le répertoire du PATH, que
# lintian refuse et qui pollue la complétion de tous les shells de la machine.
# Le binaire ET ses assets vont donc dans `lib/nineteen/`, et `bin/nineteen` est
# un lien symbolique RELATIF vers lui.
#
# Ça marche parce que SDL résout `/proc/self/exe`, qui suit le lien : le
# processus lancé par `/usr/bin/nineteen` voit `/usr/lib/nineteen/` comme base,
# donc ses assets. Vérifié dans un conteneur, `.deb` installé par `apt`.
#
# ------------------------------------------------------------------------
# Windows : bin/, inchangé
# ------------------------------------------------------------------------
# L'installateur pose son propre répertoire dans %LOCALAPPDATA%\Programs ; il
# n'y a ni PATH partagé ni police de distribution à respecter, et `bin\assets`
# à côté de `bin\nineteen.exe` est exactement ce que `ns_paths` attend.
if(APPLE)
    set(_app "Nineteen.app")
    set(_bindir   "${_app}/Contents/MacOS")
    set(_datadir  "${_app}/Contents/Resources")
    set(_docdir   ".")                       # la racine du .dmg, à côté de l'app
elseif(UNIX)
    set(_bindir   "lib/nineteen")
    set(_datadir  "lib/nineteen")
    set(_docdir   "share/doc/nineteen")
else()
    set(_bindir   "bin")
    set(_datadir  "bin")
    set(_docdir   ".")
endif()

install(TARGETS nineteen RUNTIME DESTINATION "${_bindir}")

# On installe les quatre répertoires QUE LE JEU LIT, et pas le contenu du
# dossier de build. La première version copiait `${NINETEEN_GEN_DIR}/` en bloc :
# 455 Mio, dont les `CMakeFiles`, les `CTestTestfile.cmake`, les WAV de contrôle
# du mixeur et le répertoire intermédiaire de l'art généré. Un paquet qui
# emporte ses gravats est un paquet qu'on n'a pas regardé.
# `models` porte le PERSONNAGE de la vue a la troisieme personne : c'est le seul
# modele lu A L'EXECUTION — les autres sont cuits dans `salle.gltf` au build.
# L'oublier ne casse rien et ne dit rien : le jeu demarre, annonce « personnage
# indisponible » dans un journal que personne ne lit, et la touche F10 ne fait
# plus rien. C'est exactement le genre de panne muette qu'un paquet doit rendre
# impossible.
#
# CE QUI NE PART PAS, et la raison est juridique avant d'etre technique.
#
# `salle-legacy.*` est la salle de 2020 telle que son OBJ la decrit, gardee pour
# COMPARER la reconstruction a l'original. C'est un outil de developpement : le
# jeu livre a une salle, pas deux. Et elle est batie sur l'integralite du jeu de
# textures de 2020 — dont les huit affiches qui sont des oeuvres tierces sous
# droit exclusif, retirees du build pour cette raison (voir le bloc « LES
# AFFICHES DES MURS » dans assets/CMakeLists.txt). L'emporter, ce serait
# reintroduire par la porte de derriere ce qu'on vient de sortir par la grande,
# et livrer en prime une seconde copie de 5,8 Mio d'une salle que personne
# n'ouvre.
#
# `--room=legacy` reste donc un outil de l'arbre de build. Dans un paquet il
# retombe sur la salle reconstruite en le disant, plutot que de s'arreter.
#
# Le motif d'exclusion porte sur le NOM et non sur une liste : `salle-legacy`
# produit quatre fichiers (gltf, bin, lights.json, scene.json) et une liste
# nommee en aurait oublie un le jour ou un cinquieme apparait.
foreach(_dir scene materials sounds games models)
    install(DIRECTORY "${NINETEEN_GEN_DIR}/${_dir}" DESTINATION "${_datadir}/assets"
            PATTERN "salle-legacy*" EXCLUDE)
endforeach()

# Les reglages du personnage, LA OU `ns_env_load` les cherche en deuxieme :
# `SDL_GetBasePath()`, c'est-a-dire le meme repertoire que `assets/`. Sans ce
# fichier le jeu tourne sur ses valeurs par defaut — ce qui est correct — mais le
# joueur n'a rien a editer, et la promesse « configurable sans recompiler » ne
# tient pas dans le paquet.
install(FILES "${CMAKE_SOURCE_DIR}/nineteen.env" DESTINATION "${_datadir}")

# Ce qu'il faut lire pour jouer, et ce qu'il faut savoir sur les licences.
install(FILES "${CMAKE_SOURCE_DIR}/docs/JOUER.md"
              "${CMAKE_SOURCE_DIR}/assets/cc0/LICENSES.md"
        DESTINATION "${_docdir}")

if(APPLE)
    # --------------------------------------------------------------------
    # Ce qui fait qu'un répertoire EST une application, pour le Finder
    # --------------------------------------------------------------------
    # Sans `Info.plist`, `Nineteen.app` reste un dossier : double-cliquer
    # l'ouvre au lieu de lancer le jeu. Et sans lui `SDL_GetBasePath()` ne
    # bascule pas sur `Contents/Resources` — le bundle et l'emplacement des
    # assets sont la MÊME décision, pas deux.
    # Un build macOS lancé sans le préréglage n'a pas de cible de déploiement :
    # la clé `LSMinimumSystemVersion` sortirait vide, et le Finder d'un vieux
    # système lancerait quand même une application qui ne peut pas tourner.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
    endif()
    configure_file("${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
            DESTINATION "${_app}/Contents")

    # Huit octets que macOS n'exige plus depuis Snow Leopard, et que tout
    # bundle produit par Xcode porte encore. On les met : ils coûtent moins
    # que la question « pourquoi le nôtre ne l'a pas ».
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo" "APPL????")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo"
            DESTINATION "${_app}/Contents")

    install(FILES "${CMAKE_SOURCE_DIR}/packaging/macos/nineteen.icns"
            DESTINATION "${_app}/Contents/Resources")

elseif(UNIX)
    # --------------------------------------------------------------------
    # `bin/nineteen` : un lien, pas une copie
    # --------------------------------------------------------------------
    # Une copie doublerait 9,4 Mio et divergerait au premier correctif. Un
    # lien RELATIF, parce qu'un lien absolu vers `/usr/lib/...` casserait le
    # `.tar.gz` déballé ailleurs que sur `/` — et le `.tar.gz` est justement
    # celui qui n'est pas installé.
    #
    # `file(CREATE_LINK)` et non `install(FILES)` : CMake ne sait pas installer
    # un lien qui n'existe pas encore. `$ENV{DESTDIR}` est ajouté à la main —
    # c'est `install(FILES)` qui le fait d'ordinaire, pas `file()`, et l'oublier
    # écrirait dans le `/usr` de la machine de construction au lieu du paquet.
    install(CODE [[
        set(_racine "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
        file(MAKE_DIRECTORY "${_racine}/bin")
        file(CREATE_LINK "../lib/nineteen/nineteen" "${_racine}/bin/nineteen" SYMBOLIC)
        message(STATUS "Lien : ${_racine}/bin/nineteen -> ../lib/nineteen/nineteen")
    ]])

    # De quoi lancer le jeu depuis un menu, sur les bureaux qui en ont un.
    # C'est aussi ce que `appimagetool` lit pour nommer et illustrer l'AppImage.
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nineteen.desktop"
            DESTINATION share/applications)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nineteen.png"
            DESTINATION share/icons/hicolor/256x256/apps)
endif()
