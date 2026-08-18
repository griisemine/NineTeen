# games/ — les mini-jeux.
#
# Chacun est une bibliothèque de LOGIQUE PURE au-dessus de `ns_sprite` : aucun
# appel SDL, aucun accès disque hors du chargement de ses planches, aucun état
# global. C'est ce qui les rend rejouables à la graine près — donc scriptables
# pour les captures, et vérifiables sans écran.
#
# C'est aussi ce qui manquait le plus à la version de 2020, où chaque jeu ouvrait
# sa propre fenêtre et faisait tourner sa propre boucle.

add_library(nineteen_games STATIC
    flappy/flappy.c
)
add_library(nineteen::games ALIAS nineteen_games)

target_include_directories(nineteen_games PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nineteen_games PUBLIC nineteen::engine)
nineteen_set_warnings(nineteen_games)
nineteen_apply_sanitizers(nineteen_games)
