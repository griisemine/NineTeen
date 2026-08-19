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
nineteen_add_tool(bvhbake  bvhbake.c)

# La salle reconstruite. `obj2gltf` reste, mais ne sert plus qu'à produire la
# salle de 2020 sous `salle-legacy.*` : les deux sortent du même binaire de jeu
# avec les mêmes réglages de rendu, ce qui rend une différence d'image
# attribuable — avec deux binaires elle ne l'est pas.
nineteen_add_tool(roomgen roomgen.c geo_shapes.c geo_mesh.c geo_import.c gltf_write.c)
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
