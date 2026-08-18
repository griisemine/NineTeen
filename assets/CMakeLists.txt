# assets/ — pipeline de conversion, exécuté au build.
#
# Les sources d'art restent où elles sont (legacy/), inchangées : ce sont les
# fichiers d'origine, et on ne les réécrit jamais. Tout ce qui est dérivé est
# produit ici, dans l'arbre de build, et n'est donc jamais commité.
#
# Conséquence pratique : régénérer les assets ne demande qu'un `cmake --build`,
# et modifier une heuristique de texgen se répercute partout sans intervention.

set(SRC_LEGACY "${CMAKE_SOURCE_DIR}/legacy")
set(SRC_ROOM "${SRC_LEGACY}/room")
set(GEN      "${CMAKE_BINARY_DIR}/assets")

file(MAKE_DIRECTORY "${GEN}/scene" "${GEN}/textures" "${GEN}/materials")

# ---------------------------------------------------------------------------
# 1. La salle : description -> glTF 2.0 + lumières et scène déclarées
# ---------------------------------------------------------------------------
# `scene/salle.room.json` est une SOURCE versionnée, pas un asset : elle décrit
# la salle en mètres, objet par objet, et `roomgen` en tire la géométrie, les
# lumières et le fichier de scène. Rien n'y est déduit — c'est ce qui supprime
# les six heuristiques qui produisaient la moquette vernie, les seize lampes de
# plafond dont sept hors des murs, et les écrans devinés à la fraction près.
#
# `--textures=` fait de la moindre faute de frappe dans un nom d'image une erreur
# de build, là où elle se serait vue à l'exécution sous forme de substitut
# procédural noyé dans le journal.
# Le nombre de textures d'origine, compté ici plutôt qu'écrit à la main : c'est ce
# qui fait qu'une image qui cesserait d'être employée casse le build au lieu de
# disparaître du décor en silence.
# Les modèles CC0 : listés en dépendance pour que remplacer un tabouret refasse
# la salle. `CONFIGURE_DEPENDS` parce qu'en ajouter un ne doit pas demander de
# savoir qu'il faut relancer la configuration.
file(GLOB CC0_MODELS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/cc0/models/*/*.gltf"
     "${CMAKE_CURRENT_SOURCE_DIR}/cc0/models/*/*.bin")

file(GLOB _room_textures "${SRC_ROOM}/textures/*.jpg" "${SRC_ROOM}/textures/*.png")
file(GLOB _cc0_textures  "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.jpg"
                         "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.png")
list(LENGTH _room_textures _n_room)
list(LENGTH _cc0_textures  _n_cc0)
math(EXPR ROOM_TEXTURE_COUNT "${_n_room} + ${_n_cc0}")

add_custom_command(
    OUTPUT  "${GEN}/scene/salle.gltf" "${GEN}/scene/salle.bin"
            "${GEN}/scene/salle.lights.json" "${GEN}/scene/salle.scene.json"
    COMMAND roomgen "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json"
                    "${GEN}/scene/salle.gltf"
                    "--textures=${SRC_ROOM}/textures"
                    "--textures=${CMAKE_CURRENT_SOURCE_DIR}/cc0"
                    "--expect-textures=${ROOM_TEXTURE_COUNT}"
                    # Racine des modèles CC0 instanciés dans la salle.
                    "--assets=${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS roomgen "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json"
            ${CC0_MODELS}
    COMMENT "Salle : description -> glTF 2.0 (objets nommés, lumières déclarées)"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 1b. La même salle, produite à part, pour pouvoir comparer
# ---------------------------------------------------------------------------
# La salle de 2020 reste disponible sous `salle-legacy.*`, sélectionnable à
# l'exécution par `--room=legacy`. Les deux sortant du même binaire avec les mêmes
# réglages de rendu, une différence d'image est attribuable ; avec deux binaires
# elle ne l'est pas.
#
# Volontairement dans le MÊME répertoire que la salle reconstruite, sous un autre
# nom de base : l'URI des textures dans un glTF est relative au fichier, donc un
# sous-dossier `legacy/` imposerait un préfixe `../textures/` — que le moteur
# refuse, à raison, puisque `ns_path_resolve` a pour rôle d'interdire les chemins
# remontants. Partager le répertoire évite à la fois la remontée et la
# duplication des 58 textures.
option(NINETEEN_BUILD_LEGACY_ROOM "Produire aussi la salle OBJ de 2020, pour comparaison" ON)

if(NINETEEN_BUILD_LEGACY_ROOM)
    add_custom_command(
        OUTPUT  "${GEN}/scene/salle-legacy.gltf" "${GEN}/scene/salle-legacy.bin"
                "${GEN}/scene/salle-legacy.lights.json"
        COMMAND obj2gltf "${SRC_ROOM}/textures/salle.obj"
                         "${GEN}/scene/salle-legacy.gltf"
        DEPENDS obj2gltf "${SRC_ROOM}/textures/salle.obj" "${SRC_ROOM}/textures/salle.mtl"
        COMMENT "Salle 2020 : OBJ -> glTF (chemin de référence)"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${GEN}/scene/salle-legacy.nsbvh"
        COMMAND bvhbake "${GEN}/scene/salle-legacy.gltf" "${GEN}/scene/salle-legacy.nsbvh"
        DEPENDS bvhbake "${GEN}/scene/salle-legacy.gltf"
        COMMENT "Salle 2020 : construction du BVH (SAH)"
        VERBATIM
    )
    # Les points de vue nommés de l'ancienne salle, écrits à la main dans SON
    # échelle (2,06 unité/m) : c'est ce qui rend la comparaison possible.
    add_custom_command(
        OUTPUT  "${GEN}/scene/salle-legacy.scene.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle-legacy.scene.json"
                "${GEN}/scene/salle-legacy.scene.json"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle-legacy.scene.json"
        VERBATIM
    )
    set(LEGACY_ROOM_OUTPUTS
        "${GEN}/scene/salle-legacy.gltf"
        "${GEN}/scene/salle-legacy.nsbvh"
        "${GEN}/scene/salle-legacy.lights.json"
        "${GEN}/scene/salle-legacy.scene.json")
else()
    set(LEGACY_ROOM_OUTPUTS "")
endif()

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
#
# Deux sources, un seul traitement.
#
# `legacy/room/textures/` porte les 58 images de 2020 : c'est ce qui garde la
# même salle, et on n'y touche pas. `assets/cc0/` porte les albédos CC0
# rapportés — uniquement là où le modèle d'origine n'avait qu'un aplat de
# couleur de 24 x 24 pixels, ce qui n'est pas une texture. Les deux passent par
# `texgen` sans distinction : une source d'art est une source d'art, et un
# chemin séparé serait un chemin qui dérive.
#
# Voir `assets/cc0/LICENSES.md` pour l'origine et la licence de chaque fichier.
file(GLOB LEGACY_TEXTURES "${SRC_ROOM}/textures/*.jpg" "${SRC_ROOM}/textures/*.png")
# Un seul répertoire pour TOUS les albédos rapportés, y compris ceux des modèles.
# Poly Haven les range dans un sous-dossier par modèle ; on les remonte ici parce
# que la chaîne (copie, puis normale et ORM dérivées par `texgen`) est la même
# pour tous, et qu'un chemin par modèle ne ferait qu'ajouter des cas.
file(GLOB CC0_TEXTURES    "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.jpg"
                          "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.png")
set(ROOM_TEXTURES ${LEGACY_TEXTURES} ${CC0_TEXTURES})

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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LEGACY_TEXTURES} "${GEN}/scene/textures/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CC0_TEXTURES} "${GEN}/scene/textures/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/scene/textures/.stamp"
    DEPENDS ${ROOM_TEXTURES}
    COMMENT "Copie des textures diffuses d'origine"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 3b. Les sons
# ---------------------------------------------------------------------------
#
# Ils étaient là depuis le début — `legacy/room/sounds/` et
# `legacy/assets/background.wav` — et il n'y avait **aucune étape pour les
# copier**. Le mixeur pouvait bien exister, il n'aurait rien trouvé à jouer.
file(GLOB ROOM_SOUNDS "${SRC_ROOM}/sounds/*.wav" "${SRC_LEGACY}/assets/*.wav")

add_custom_command(
    OUTPUT "${GEN}/sounds/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOM_SOUNDS} "${GEN}/sounds/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/sounds/.stamp"
    DEPENDS ${ROOM_SOUNDS}
    COMMENT "Copie des sons"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 3c. Les mini-jeux : planches de sprites et sons
# ---------------------------------------------------------------------------
#
# Elles sont reprises TELLES QUELLES de 2020 — `birds.png`, `pipes.png`,
# `sol.png`, `chiffre.png`… C'est ce qui fait qu'on rejoue au même jeu et pas à
# une imitation : les cotes de la planche (oiseau 17 x 12, tuyau 26 x 160,
# chiffre 12 x 18) sont exactement celles que le code de 2020 découpait, et le
# portage les reprend au pixel près.
file(GLOB FLAPPY_ART   "${SRC_LEGACY}/games/3_flappy_bird/Textures/*.png")
file(GLOB FLAPPY_SOUND "${SRC_LEGACY}/games/3_flappy_bird/Sounds/*.wav")

add_custom_command(
    OUTPUT "${GEN}/games/flappy/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/flappy"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FLAPPY_ART} "${GEN}/games/flappy/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FLAPPY_SOUND} "${GEN}/games/flappy/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/flappy/.stamp"
    DEPENDS ${FLAPPY_ART} ${FLAPPY_SOUND}
    COMMENT "Copie des planches et sons de Flappy Bird"
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
        "${GEN}/scene/salle.scene.json"
        "${GEN}/sounds/.stamp"
        "${GEN}/games/flappy/.stamp"
        "${GEN}/scene/cabinets.json"
        "${GEN}/scene/textures/.stamp"
        ${LEGACY_ROOM_OUTPUTS}
        ${GENERATED_MAPS}
)

# ---------------------------------------------------------------------------
# 5. Comparaison des deux salles, aux mêmes cadrages
# ---------------------------------------------------------------------------
# `cmake --build … --target render-compare` produit une paire de captures par
# point de vue nommé. Le critère n'est pas « ça compile » mais « regarde les
# deux images » : c'est le seul moyen de tenir la promesse que la salle
# reconstruite est meilleure, plutôt que de l'affirmer.
if(NINETEEN_BUILD_LEGACY_ROOM)
    set(COMPARE_COMMANDS "")
    foreach(view IN ITEMS allee bar billard orbite)
        foreach(room IN ITEMS legacy generated)
            list(APPEND COMPARE_COMMANDS
                COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEO_DRIVER=offscreen
                        "$<TARGET_FILE:nineteen>" --headless
                        "--room=${room}" "--view=${view}" --frames=8
                        --width=1280 --height=720 --quality=high
                        "--screenshot=${CMAKE_BINARY_DIR}/compare-${view}-${room}.png")
        endforeach()
    endforeach()
    add_custom_target(render-compare
        ${COMPARE_COMMANDS}
        DEPENDS nineteen nineteen_assets
        COMMENT "Captures comparées des deux salles, par point de vue nommé"
        VERBATIM
    )
endif()
