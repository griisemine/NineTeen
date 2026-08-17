# assets/ — pipeline de conversion, exécuté au build.
#
# Les sources d'art restent où elles sont (legacy/), inchangées : ce sont les
# fichiers d'origine, et on ne les réécrit jamais. Tout ce qui est dérivé est
# produit ici, dans l'arbre de build, et n'est donc jamais commité.
#
# Conséquence pratique : régénérer les assets ne demande qu'un `cmake --build`,
# et modifier une heuristique de texgen se répercute partout sans intervention.

set(SRC_ROOM "${CMAKE_SOURCE_DIR}/legacy/room")
set(GEN      "${CMAKE_BINARY_DIR}/assets")

file(MAKE_DIRECTORY "${GEN}/scene" "${GEN}/textures" "${GEN}/materials")

# ---------------------------------------------------------------------------
# 1. La salle : OBJ Blender -> glTF 2.0 + lumières déduites
# ---------------------------------------------------------------------------
add_custom_command(
    OUTPUT  "${GEN}/scene/salle.gltf" "${GEN}/scene/salle.bin" "${GEN}/scene/salle.lights.json"
    COMMAND obj2gltf "${SRC_ROOM}/textures/salle.obj" "${GEN}/scene/salle.gltf"
    DEPENDS obj2gltf "${SRC_ROOM}/textures/salle.obj" "${SRC_ROOM}/textures/salle.mtl"
    COMMENT "Salle : OBJ -> glTF 2.0 (matériaux PBR, tangentes, lumières)"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 2. Le BVH : sert au ray tracing, à la collision et à l'occlusion audio
# ---------------------------------------------------------------------------
add_custom_command(
    OUTPUT  "${GEN}/scene/salle.nsbvh"
    COMMAND bvhbake "${GEN}/scene/salle.gltf" "${GEN}/scene/salle.nsbvh"
    DEPENDS bvhbake "${GEN}/scene/salle.gltf"
    COMMENT "Salle : construction du BVH (SAH)"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 3. Les textures : diffus d'origine -> normal map + ORM
# ---------------------------------------------------------------------------
file(GLOB ROOM_TEXTURES "${SRC_ROOM}/textures/*.jpg" "${SRC_ROOM}/textures/*.png")

set(GENERATED_MAPS "")
foreach(tex IN LISTS ROOM_TEXTURES)
    get_filename_component(tex_name "${tex}" NAME_WE)

    # Les grandes images sont déjà assez définies ; on n'agrandit que les
    # textures minuscules (il y en a en 1x1 et 24x24 dans la salle), pour
    # lesquelles l'analyse de contraste n'aurait sinon aucun support.
    file(SIZE "${tex}" tex_bytes)
    if(tex_bytes LESS 20000)
        set(upscale "--upscale=4")
    elseif(tex_bytes LESS 200000)
        set(upscale "--upscale=2")
    else()
        set(upscale "--upscale=1")
    endif()

    add_custom_command(
        OUTPUT  "${GEN}/materials/${tex_name}_n.png" "${GEN}/materials/${tex_name}_orm.png"
        COMMAND texgen ${upscale} "${tex}" "${GEN}/materials"
        DEPENDS texgen "${tex}"
        COMMENT "Matériau PBR : ${tex_name}"
        VERBATIM
    )
    list(APPEND GENERATED_MAPS "${GEN}/materials/${tex_name}_n.png" "${GEN}/materials/${tex_name}_orm.png")

    # Le diffus d'origine est copié tel quel à côté de ses cartes dérivées :
    # le glTF y fait référence sous textures/<nom>.
    add_custom_command(
        OUTPUT  "${GEN}/scene/textures/${tex_name}${CMAKE_MATCH_0}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${tex}" "${GEN}/scene/textures/"
        DEPENDS "${tex}"
        VERBATIM
    )
endforeach()

# Copie groupée des diffus (plus simple et plus rapide qu'une règle par fichier).
add_custom_command(
    OUTPUT "${GEN}/scene/textures/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOM_TEXTURES} "${GEN}/scene/textures/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/scene/textures/.stamp"
    DEPENDS ${ROOM_TEXTURES}
    COMMENT "Copie des textures diffuses d'origine"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 4. Données écrites à la main (affectation des bornes, hitbox, ambiances)
# ---------------------------------------------------------------------------
add_custom_command(
    OUTPUT "${GEN}/scene/cabinets.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scene/cabinets.json" "${GEN}/scene/cabinets.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scene/cabinets.json"
    VERBATIM
)

add_custom_target(nineteen_assets ALL
    DEPENDS
        "${GEN}/scene/salle.gltf"
        "${GEN}/scene/salle.nsbvh"
        "${GEN}/scene/salle.lights.json"
        "${GEN}/scene/cabinets.json"
        "${GEN}/scene/textures/.stamp"
        ${GENERATED_MAPS}
)
