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
# Les assets vont donc À CÔTÉ du binaire, dans `bin/assets`, exactement là où le
# moteur les cherche. Pas dans `share/` : ce serait plus conforme aux usages
# Unix, et ça demanderait au moteur un point de montage de plus dont un paquet
# .dmg ou .msi n'aurait que faire.
install(TARGETS nineteen RUNTIME DESTINATION bin)

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
    install(DIRECTORY "${NINETEEN_GEN_DIR}/${_dir}" DESTINATION bin/assets
            PATTERN "salle-legacy*" EXCLUDE)
endforeach()

# Les reglages du personnage, A COTE DU BINAIRE : c'est le premier des trois
# endroits ou `ns_env_load` les cherche. Sans ce fichier le jeu tourne sur ses
# valeurs par defaut — ce qui est correct — mais le joueur n'a rien a editer, et
# la promesse « configurable sans recompiler » ne tient pas dans le paquet.
install(FILES "${CMAKE_SOURCE_DIR}/nineteen.env" DESTINATION bin)

# Ce qu'il faut lire pour jouer, et ce qu'il faut savoir sur les licences.
install(FILES "${CMAKE_SOURCE_DIR}/docs/JOUER.md"
              "${CMAKE_SOURCE_DIR}/assets/cc0/LICENSES.md"
        DESTINATION .)

# De quoi lancer le jeu depuis un menu, sur les bureaux qui en ont un.
if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nineteen.desktop"
            DESTINATION share/applications)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nineteen.png"
            DESTINATION share/icons/hicolor/256x256/apps)
endif()
