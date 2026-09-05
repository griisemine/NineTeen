# engine/ — le cœur réutilisable.
#
# Une seule bibliothèque statique. Les mini-jeux et la salle en dépendent ; aucun
# d'eux ne parle directement à SDL, ce qui permettra de changer de backend sans
# retoucher le gameplay (exactement ce qui manquait à la V1, où chaque jeu
# ouvrait sa propre fenêtre et gérait sa propre boucle).

add_library(nineteen_engine STATIC
    core/ns_log.c
    core/ns_alloc.c
    core/ns_clock.c
    core/ns_paths.c
    core/ns_config.c
    core/ns_env.c
    core/ns_json.c
    core/ns_saisie.c
    core/ns_saisie_sdl.c

    rhi/ns_rhi.c
    rhi/ns_rhi_shader.c

    scene/ns_scene.c
    scene/ns_bvh.c

    render/ns_render.c
    render/ns_shaders.c
    render/ns_viewmodel.c

    anim/ns_ik.c

    anim/ns_skin.c

    audio/ns_audio.c

    sprite/ns_sprite.c
    sprite/ns_saisie_draw.c

    fx/ns_particles.c

    net/ns_scores.c
    net/ns_runlog.c
    net/ns_http.c
    net/ns_maj.c
    net/ns_online.c
    net/ns_realtime.c
    net/ns_lockstep.c
    net/ns_arene.c
    net/ns_compte.c
)

add_library(nineteen::engine ALIAS nineteen_engine)

target_include_directories(nineteen_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"          # #include "core/ns_core.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/core"     # #include "ns_core.h" en interne
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi"
        "${CMAKE_CURRENT_SOURCE_DIR}/scene"
        "${CMAKE_CURRENT_SOURCE_DIR}/render"
        "${CMAKE_CURRENT_SOURCE_DIR}/anim"
        "${CMAKE_CURRENT_SOURCE_DIR}/audio"
        "${CMAKE_CURRENT_SOURCE_DIR}/net"
        "${CMAKE_CURRENT_SOURCE_DIR}/sprite"
        "${CMAKE_CURRENT_SOURCE_DIR}/fx"
)

target_link_libraries(nineteen_engine
    PUBLIC
        nineteen::sdl3
    PUBLIC
        nineteen::stb
    PRIVATE
        nineteen::jsmn
        nineteen::cgltf
        # Vendoré depuis M0, et jamais lié jusqu'ici : la cible existait,
        # frameworks CoreAudio compris, et rien ne s'en servait.
        nineteen::miniaudio
)

if(UNIX)
    target_link_libraries(nineteen_engine PUBLIC m)
endif()

nineteen_set_warnings(nineteen_engine)
nineteen_apply_sanitizers(nineteen_engine)

# Les shaders du moteur sont embarqués dans la bibliothèque : le jeu n'a aucun
# fichier .spv à retrouver à l'exécution.
file(GLOB NINETEEN_ENGINE_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
)
if(NINETEEN_ENGINE_SHADERS)
    nineteen_add_shaders(nineteen_engine SOURCES ${NINETEEN_ENGINE_SHADERS})
endif()
