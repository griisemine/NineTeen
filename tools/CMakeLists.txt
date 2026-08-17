# tools/ — outils hors-ligne, exécutés au build.
#
# Ils ne dépendent ni de SDL ni du moteur : ce sont des programmes C autonomes
# qui transforment les assets d'origine en données prêtes à charger. Les garder
# séparés permet de les exécuter en compilation croisée depuis n'importe quelle
# machine, et de rejouer une conversion sans reconstruire le jeu.

function(nineteen_add_tool name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE nineteen::stb nineteen::cgltf)
    if(UNIX)
        target_link_libraries(${name} PRIVATE m)
    endif()
    nineteen_set_warnings(${name})
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
endfunction()

nineteen_add_tool(obj2gltf obj2gltf.c)
nineteen_add_tool(texgen   texgen.c)
nineteen_add_tool(bvhbake  bvhbake.c)
