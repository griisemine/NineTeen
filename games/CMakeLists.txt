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
    games.c
    flappy/flappy.c
    snake/snake.c
    demineur/demineur.c
    aplomb/aplomb.c
    asteroid/asteroid.c
    dedale/dedale.c
    piano/piano.c
    shooter/shooter.c
)
add_library(nineteen::games ALIAS nineteen_games)

target_include_directories(nineteen_games PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(nineteen_games PUBLIC nineteen::engine)
# La CONTRACTION flottante, coupée — et ce qu'elle a coûté de croire.
#
# `a * b + c` peut être fusionné en une seule instruction (FMA), qui n'arrondit
# qu'UNE fois au lieu de deux : le résultat est plus exact, et il est DIFFÉRENT.
# Clang le fait d'office sur arm64, où la FMA est dans le jeu d'instructions de
# base, et pas sur x86-64, où elle ne l'est pas. C'était donc l'explication
# évidente quand la mesure a montré que Piano divergeait entre les deux
# architectures.
#
# **Elle était fausse, et c'est mesuré.** Couper la contraction n'a rien changé :
# Piano divergeait toujours. La cause était un `const char *` dans son état, dont
# l'adresse change à chaque exécution. Et une fois ce pointeur retiré, les huit
# jeux sont identiques au bit près sur les deux architectures AVEC ou SANS
# contraction — vérifié en repassant à `fast`.
#
# Le drapeau reste quand même, et le dire ainsi vaut mieux que de le retirer ou
# que de lui attribuer un mérite qu'il n'a pas : il supprime une classe de
# divergence documentée, que cette mesure-ci n'a pas rencontrée mais qu'un autre
# compilateur ou une autre cible peut produire. Le coût est nul — un mini-jeu
# n'est pas limité par son arithmétique — et il ne porte que sur `games/` : le
# RENDU a tout intérêt à contracter, personne ne compare deux images au bit près.
target_compile_options(nineteen_games PRIVATE -ffp-contract=off)

nineteen_set_warnings(nineteen_games)
nineteen_apply_sanitizers(nineteen_games)
