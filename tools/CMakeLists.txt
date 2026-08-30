# tools/ — outils hors-ligne, exécutés au build.
#
# Ils ne dépendent ni de SDL ni du moteur : ce sont des programmes C autonomes
# qui transforment les assets d'origine en données prêtes à charger. Les garder
# séparés permet de les exécuter en compilation croisée depuis n'importe quelle
# machine, et de rejouer une conversion sans reconstruire le jeu.

function(nineteen_add_tool name)
    add_executable(${name} ${ARGN})
    # `engine/core` en plus du répertoire des outils : quelques en-têtes sont
    # des CONTRATS partagés entre l'outil qui écrit et le moteur qui lit —
    # `ns_math.h` pour les matrices, `nstex.h` pour le conteneur de textures
    # compressées. Les recopier côté outils ferait deux descriptions du même
    # format, qui divergeraient d'un champ un jour ou l'autre. Ça ne fait
    # dépendre les outils ni de SDL ni du moteur : ces en-têtes n'incluent que
    # la bibliothèque standard.
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/engine/core")
    target_link_libraries(${name} PRIVATE nineteen::stb nineteen::cgltf)
    if(UNIX)
        target_link_libraries(${name} PRIVATE m)
    endif()
    nineteen_set_warnings(${name})
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
endfunction()

nineteen_add_tool(obj2gltf obj2gltf.c gltf_write.c)
nineteen_add_tool(texgen   texgen.c)
nineteen_add_tool(sideart  sideart.c)
nineteen_add_tool(panelart panelart.c)
nineteen_add_tool(marqueeart marqueeart.c)
# `marqueeart` dessine avec la fonte DU JEU : une enseigne tracee dans une
# autre fonte que les scores de la meme borne se remarque tout de suite.
target_include_directories(marqueeart PRIVATE "${CMAKE_SOURCE_DIR}/engine/sprite")
# Les AFFICHES des murs. Elles remplacent huit images de 2020 qui etaient des
# oeuvres tierces sous droit exclusif — flyers PAC-MAN et DONKEY KONG, affiche
# Stranger Things, illustration League of Legends, arcade de Tron — employees
# sans droit et livrees dans le paquet. Aucune redaction de LICENSES.md ne rend
# `poster_8.jpg` distribuable ; le jeu ne pouvait pas etre vendu tant qu'elles y
# etaient. Elles sont donc DESSINEES, comme le sont deja les enseignes :
# l'iconographie des annees 80 appartient a tout le monde, ses personnages non.
nineteen_add_tool(posterart posterart.c)
target_include_directories(posterart PRIVATE
    "${CMAKE_SOURCE_DIR}/engine/sprite"
    # Le motif « plan » LIT la description de la salle plutot que de la
    # recopier : une affiche qui montrerait un plan perime serait pire
    # qu'une affiche absente.
    "${CMAKE_SOURCE_DIR}/third_party/jsmn")
# Les PLANCHES DE SPRITES des mini-jeux. Même raison que `posterart`, un cran
# plus haut dans la gravité : ce n'étaient pas des affiches accrochées au mur,
# c'étaient les PERSONNAGES eux-mêmes. `1_pacman/pacman.png` et
# `1_pacman/enemy.png` sont le héros et les fantômes de la borne de 1980
# (Bandai Namco) ; les huit `.png` de `3_flappy_bird/` sont l'oiseau, les
# tuyaux, le ciel et les médailles de Flappy Bird, dont l'un porte encore le
# filigrane « ©123RF » d'une banque d'images. Une mécanique ne s'approprie pas ;
# un nom et un personnage, si. Les règles de `games/` n'ont pas bougé, les
# figures sont redessinées ici — aux MÊMES cotes, pour que le code de découpe
# des jeux reste valable au pixel près.
nineteen_add_tool(spriteart spriteart.c)
# La TEXTURE DU PERSONNAGE. Meme geste que les trois precedents, sur le sujet le
# plus visible du jeu : le joueur qu'on voit a la troisieme personne portait le
# LOGOTYPE de Cesium — des rubans bleus et verts sur fond blanc — parce que le
# modele importe est le mannequin d'exemple de Khronos. Le modele est bon ; sa
# livree disait « fichier de test ». `skinart` rasterise le maillage DANS
# l'espace UV, ce qui lui donne le point du corps que porte chaque texel, et
# peint ensuite en coordonnees de corps. Il n'est PAS joue au build : il modifie
# une source versionnee, et une source ne se modifie pas pendant un build.
nineteen_add_tool(skinart skinart.c glb_image.c)
target_include_directories(skinart PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/jsmn"      # deux entiers a recoudre dans le JSON du GLB
    "${CMAKE_SOURCE_DIR}/third_party/cgltf")

# Le TAPIS DU HALL. Meme geste, au dernier endroit ou personne n'avait regarde :
# le SOL. `legacy/room/textures/floor.jpg` porte au neon un PAC-MAN, un de ses
# FANTOMES, ses CERISES de bonus (Bandai Namco) et la silhouette d'une MANETTE
# de console (Sony) — sur la plus grande surface de la salle, dans chaque image.
# `moquetteart` redessine le meme tapis avec des icones qui n'appartiennent a
# personne, et sort DEJA DE-CUIT a la reflectance visee.
nineteen_add_tool(moquetteart moquetteart.c)

nineteen_add_tool(bvhbake  bvhbake.c)
# Même raison d'être que `sideart` et `panelart`, sur l'autre sens : la banque de
# pas et les nappes d'ambiance sont SYNTHÉTISÉES au lieu d'être enregistrées. Un
# pas se décrit par un choc et deux résonateurs ; le décrire coûte moins qu'une
# banque téléchargée, et ne pose ni question de licence ni question de poids.
nineteen_add_tool(stepgen  stepgen.c)

# La salle reconstruite. `obj2gltf` reste, mais ne sert plus qu'à produire la
# salle de 2020 sous `salle-legacy.*` : les deux sortent du même binaire de jeu
# avec les mêmes réglages de rendu, ce qui rend une différence d'image
# attribuable — avec deux binaires elle ne l'est pas.
# `reach_grid.c` est à part, et pas par goût du découpage : c'est le seul morceau
# de `roomgen` qui soit éprouvable SANS salle. Le contrôle d'accessibilité se
# vérifie sur une pièce scellée fabriquée exprès (`ns_test_reach`), ce qu'un
# `roomgen` monolithique n'aurait permis qu'en écrivant une fausse description
# complète — donc jamais.
nineteen_add_tool(roomgen roomgen.c reach_grid.c geo_shapes.c geo_mesh.c geo_import.c gltf_write.c)
target_include_directories(roomgen PRIVATE
    "${CMAKE_SOURCE_DIR}/engine/core"      # ns_math.h : ni SDL ni moteur, juste <math.h>
    "${CMAKE_SOURCE_DIR}/third_party/jsmn"
    # cgltf : déjà vendoré et déjà employé par `bvhbake`. `roomgen` s'en sert
    # pour instancier du mobilier CC0 — pas de format neuf, pas de chargeur neuf.
    "${CMAKE_SOURCE_DIR}/third_party/cgltf")

# ---------------------------------------------------------------------------
# spv2msl — SPIR-V vers MSL, aux emplacements de ressources que SDL3 attend
# ---------------------------------------------------------------------------
# Seul outil de ce répertoire à dépendre d'un tiers, et le seul qui tire du C++
# dans l'arbre : il n'est donc construit que lorsque du MSL est demandé, c'est-à-
# dire sur macOS ou quand on veut vérifier la traduction depuis Linux.
if(NINETEEN_SHADERCROSS)
    nineteen_add_tool(spv2msl spv2msl.c)
    target_link_libraries(spv2msl PRIVATE spirv-cross-c)
    # SPIRV-Cross est du C++ : l'édition de liens doit passer par le pilote C++,
    # sans quoi il manque la bibliothèque standard C++ à l'assemblage final.
    set_target_properties(spv2msl PROPERTIES LINKER_LANGUAGE CXX)
endif()
