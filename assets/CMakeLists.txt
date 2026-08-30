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

# Le côté maximal des CARTES (_n et _orm), pas de l'albédo.
#
# Elles étaient écrites à la résolution de la texture source, quelle qu'elle
# soit : des normales en 3840 x 2160 pour l'image d'un écran de jeu, en 2048²
# pour le flanc d'une radio. Sur les 421 Mio de l'archive de release, 330
# étaient ces deux familles — pour une information que l'éclairage intègre sur
# plusieurs pixels et qui ne se lit jamais au texel près, contrairement à un
# albédo qui porte du lettrage et de la trame.
set(NINETEEN_MAX_MAP 1024 CACHE STRING
    "Côté maximal des cartes de normales et ORM (0 = aucun plafond)")

# Les cartes en blocs compressés (BC5 pour les normales, BC1 pour l'ORM), dans
# le conteneur `engine/core/nstex.h`, mips comprises.
#
# ON par défaut : tout GPU de bureau lit du BC — c'est la base de Direct3D
# depuis vingt ans, et les Mac Apple Silicon le gèrent. Le moteur VÉRIFIE quand
# même le support avant de créer la texture, et le dit s'il manque, plutôt que
# d'afficher du noir sans message.
#
# OFF redonne des PNG : c'est le repli si un jour une plateforme visée ne suit
# pas, et c'est aussi ce qui rend la comparaison possible.
option(NINETEEN_BC_MAPS "Cartes de normales et ORM en blocs compressés (.nstex)" ON)
if(NINETEEN_BC_MAPS)
    set(_bc "--bc")
    set(_map_n "_n.nstex")
    set(_map_orm "_orm.nstex")
else()
    set(_bc "")
    set(_map_n "_n.png")
    set(_map_orm "_orm.png")
endif()
if(NOT NINETEEN_MAX_MAP MATCHES "^[0-9]+$")
    message(FATAL_ERROR "NINETEEN_MAX_MAP doit être un entier, pas « ${NINETEEN_MAX_MAP} »")
endif()

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
# Les modèles construits sous Blender par les scripts d'`assets/blender/` — la
# borne, aujourd'hui. Ils n'étaient PAS dans cette liste : régénérer la borne ne
# refaisait donc pas la salle, et le jeu continuait de montrer l'ancien maillage
# en affichant « rien à faire ». Un modèle qu'on modifie sans que le décor bouge
# est exactement le genre de silence que ce dépôt refuse. Les ancres sont là
# aussi : `roomgen` les LIT, elles font donc partie de la description.
file(GLOB BLENDER_MODELS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/models/*/*.gltf"
     "${CMAKE_CURRENT_SOURCE_DIR}/models/*/*.bin"
     "${CMAKE_CURRENT_SOURCE_DIR}/models/*/*.json"
     # Le PERSONNAGE est un glTF BINAIRE — maillage, squelette, animation et
     # texture dans un seul fichier. C'est le bon format pour lui : ses parties
     # n'ont aucun sens separement, et un .glb ne peut pas arriver a moitie.
     "${CMAKE_CURRENT_SOURCE_DIR}/models/*/*.glb")

#
# L'art latéral d'une borne, DESSINÉ plutôt que téléchargé.
#
# Les vues de référence montrent, sur le flanc, un dégradé qui s'éclaircit vers
# le marquee et une trame de losanges en diagonale. C'est ce qui distingue une
# borne d'arcade d'un caisson peint, et c'est ce qui manquait encore à la
# silhouette refaite en B14.
#
# Le modèle free3d demande un compte : le versionner casserait la promesse de
# reconstructibilité hors ligne et la clarté d'`assets/cc0/LICENSES.md`. Une
# planche produite par vingt lignes d'arithmétique n'a ni licence à démêler ni
# fichier à rapporter, et se régénère à l'identique partout.
#
# Elle est en niveaux de gris : c'est le MATÉRIAU de chaque borne qui la teinte.
# Les dix-neuf bornes gardent donc leur couleur de jeu — on trouve « sa » borne
# de loin, ce qui est un vrai service rendu au joueur — tout en portant chacune
# le flanc de la référence.
#
# Elle sort dans un répertoire À ELLE, et pas directement dans `scene/textures`.
# `roomgen` vérifie que CHAQUE image de ses répertoires de recherche est
# référencée par un matériau — c'est ce qui fait qu'une texture abandonnée casse
# le build au lieu de disparaître du décor en silence. Lui donner
# `scene/textures`, qui est sa propre SORTIE, ferait compter deux fois les
# soixante-neuf autres. Un répertoire d'une seule image, plus une copie vers
# l'emplacement d'exécution, garde la vérification exacte.
set(SIDEART "${GEN}/sideart/flanc_borne.png")
set(SIDEART_RUNTIME "${GEN}/scene/textures/flanc_borne.png")
add_custom_command(
    OUTPUT  "${SIDEART}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
    # SPEC-ATMOSPHERE poste 6. Le losange faisait 18,75 cm : une cote de
    # capitonnage de fauteuil, pas de tole gaufree — et c'est ce qui donnait aux
    # flancs leur air de tissu d'ameublement. A 6,6 cm c'est un gaufrage.
    # `--high` 0,86 -> 0,62 : le haut du flanc cessait de saturer a blanc, ce
    # qui y effacait la teinte du multiplicateur, justement la ou on le voit
    # depuis l'allee.
    COMMAND sideart --step=34 --line=0.06 --low=0.12 --high=0.62 "${SIDEART}"
    DEPENDS sideart
    COMMENT "Art latéral de borne : flanc_borne.png"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${SIDEART_RUNTIME}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SIDEART}" "${SIDEART_RUNTIME}"
    DEPENDS "${GEN}/scene/textures/.stamp" "${SIDEART}"
    VERBATIM
)
# Elle passe par `texgen` comme toutes les autres : une planche sans normale ni
# ORM serait la seule surface plate de la salle.
add_custom_command(
    OUTPUT  "${GEN}/materials/flanc_borne${_map_n}" "${GEN}/materials/flanc_borne${_map_orm}"
    COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.8 "${SIDEART}" "${GEN}/materials"
    DEPENDS texgen "${SIDEART}"
    COMMENT "Matériau PBR : flanc_borne"
    VERBATIM
)

# La sérigraphie du panneau de commande, même famille et même répertoire que
# l'art latéral : le flanc et le panneau partagent la trame en losanges, à deux
# pas différents. Les dix-neuf bornes portaient jusqu'ici le `bordeaux.jpg` de
# 2020 en pavage — un aplat, sur la plus grande surface que le joueur ait sous
# les yeux quand il joue.
set(PANELART "${GEN}/sideart/panneau_borne.png")
set(PANELART_RUNTIME "${GEN}/scene/textures/panneau_borne.png")
add_custom_command(
    OUTPUT  "${PANELART}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
    COMMAND panelart --field=0.30 "${PANELART}"
    DEPENDS panelart
    COMMENT "Sérigraphie de panneau : panneau_borne.png"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${PANELART_RUNTIME}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PANELART}" "${PANELART_RUNTIME}"
    DEPENDS "${GEN}/scene/textures/.stamp" "${PANELART}"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${GEN}/materials/panneau_borne${_map_n}" "${GEN}/materials/panneau_borne${_map_orm}"
    COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.7 "${PANELART}" "${GEN}/materials"
    DEPENDS texgen "${PANELART}"
    COMMENT "Matériau PBR : panneau_borne"
    VERBATIM
)


# Les enseignes manquantes, dessinées.
#
# Huit des dix-neuf bornes affichaient « votre publicité ici ? » au-dessus de
# leur écran. La raison était simple : quatre jeux — Shooter, Démineur,
# Pac-Man, Piano — n'ont jamais eu d'enseigne dessinée, et les bornes qui les
# portent tombaient donc sur la pancarte du régisseur. Une salle d'arcade se lit
# d'abord par ses enseignes : on traverse l'allée en lisant les noms, et c'est
# comme ça qu'on choisit sa borne.
#
# `marqueeart` les dessine avec la fonte DU JEU (`engine/sprite/ns_font5x7.h`).
# Chaque teinte est celle de la laque du caisson correspondant, pour qu'une
# borne se reconnaisse d'aussi loin par son enseigne que par sa couleur.
#
# RECETTE VISUELLE — LES HUIT AUTRES. Le poste ci-dessus a été fait à moitié :
# il a couvert les quatre jeux SANS planche, et laissé les quatre qui EN
# AVAIENT UNE, parce que leur matériau `marquee_*` était renseigné. Il l'était
# avec la mauvaise image. `flappy.jpg`, `tetris.jpg`, `asteroid.jpg` et
# `snake.jpg` ne sont pas des enseignes : ce sont les ATLAS DE CAISSON de 2020,
# la peau du meuble mise à plat — deux joues, un bandeau d'écran, un flanc, et
# le nom du jeu en bandeau vertical dans le coin droit. Posés en `fit` sur une
# enseigne de 0,62 x 0,155 m, ils y écrasaient l'atlas entier : le joueur lisait
# deux oiseaux coupés et un aplat vert, jamais « FLAPPY ». Vu à 40 cm de l'œil
# sous `--play-at=borne_arcade_1`, où l'enseigne occupe le haut du cadre pendant
# toute la partie.
#
# Les huit bornes concernées : 1 et 12 (flappy), 2 et 11 (tetris), 3 et 10
# (asteroid), 5 et 8 (snake). Le compte est le même que celui de `pub.jpg`, et
# la cause aussi : une image disponible employée pour boucher un trou.
#
# Les quatre atlas SORTENT DONC DU DÉCOR, et c'est dit par écrit : le
# caisson d'une borne est `painted_panel.jpg` teinté, son flanc est
# `flanc_borne.png` sérigraphié. La reconstruction avait déjà abandonné les
# peaux de 2020 partout ailleurs ; le marquee était le dernier endroit où l'une
# d'elles restait accrochée, et elle y était fausse. Elles passent dans
# `retiredTextures` avec leur motif, comme `pub.jpg` avant elles.
set(MARQUEE_ART "")

# L'ENSEIGNE DE LA SALLE, en vrai neon.
#
# Elle portait `nineteen_name.jpg` : une image peinte, avec ombre portee et
# contour dessine. Le proprietaire l'a demandee « ecrite en neon ». Un neon
# n'est pas un lettrage clair sur fond sombre — c'est un TUBE de verre nu de
# douze millimetres devant un panneau noir, dont le halo porte loin. C'est le
# mode `--neon` de marqueeart, ajoute pour ca.
#
# Cyan et non magenta : le liseré des dix-neuf bornes est deja le materiau le
# plus sature du decor, et l'enseigne doit se detacher des murs chauds, pas s'y
# ajouter.
set(_neon "${GEN}/sideart/nineteen_neon.png")
add_custom_command(
    OUTPUT  "${_neon}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
    COMMAND marqueeart --titre=NINETEEN --neon "--teinte=0.24,0.78,1.0"
            --width=2048 --height=512 "${_neon}"
    DEPENDS marqueeart
    COMMENT "Enseigne au neon : nineteen_neon.png"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${GEN}/scene/textures/nineteen_neon.png"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_neon}" "${GEN}/scene/textures/nineteen_neon.png"
    DEPENDS "${GEN}/scene/textures/.stamp" "${_neon}"
    VERBATIM
)
# `--strength=0.35` : le panneau est PLAT. Le relief deduit du contraste y
# graverait les tubes en creux, or un tube de neon est en saillie devant sa
# tole — et de toute facon on le regarde a trois metres.
add_custom_command(
    OUTPUT  "${GEN}/materials/nineteen_neon${_map_n}" "${GEN}/materials/nineteen_neon${_map_orm}"
    COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.35 "${_neon}" "${GEN}/materials"
    DEPENDS texgen "${_neon}"
    COMMENT "Materiau PBR : nineteen_neon"
    VERBATIM
)
list(APPEND MARQUEE_ART "${_neon}" "${GEN}/scene/textures/nineteen_neon.png"
                        "${GEN}/materials/nineteen_neon${_map_n}"
                        "${GEN}/materials/nineteen_neon${_map_orm}")

# LE BLOC DE SECOURS, au-dessus de la porte du sas.
#
# Il portait `exit.png`, et `exit.png` n'est pas une pancarte : c'est une
# CAPTURE D'ÉCRAN DU MENU du jeu de 2020 — un cartouche « ECHAP » bleu, un
# bouton gris « Q : Quitter », un bouton rouge « D : Deconnexion », arrondis et
# antialiasés comme une interface. Elle était collée à 2,42 m sur le linteau,
# émissive à 2,6, verte, au-dessus de la seule porte de la salle. C'est la même
# faute que `pub.jpg` et `coming_soon.jpg` : le seul fichier disponible employé
# pour boucher un trou, et qui reste en place parce que personne ne l'ouvre.
# Vu en capture libre à 2,4 m du linteau, on lit les deux boutons.
#
# 1024 x 394, soit le 2,6:1 du panneau déclaré (0,52 x 0,20 m) : les images de
# ce dépôt qui ne sont pas au format de leur support sont justement celles qui
# n'ont jamais été regardées.
#
# Mode MARQUEE et non `--neon`, et la capture a tranché. `--neon` a été essayé
# d'abord, par analogie avec l'enseigne de la salle : le halo est DESSINÉ DANS
# LA PLANCHE, il occupe la même fraction de l'image quelle que soit la taille du
# support, et sur un panneau de 0,52 m vu à 2,40 m il mange les lettres. Baisser
# l'émissif de 2,6 à 0,8 n'y change rien — on lisait toujours une bouillie
# verte. Un bloc de secours n'est de toute façon pas un tube nu : c'est une
# PLAQUE RÉTROÉCLAIRÉE, verte, avec le mot en réserve dedans, c'est-à-dire
# exactement ce que le mode marquee dessine — le même objet qu'un marquee de
# borne, en plus petit.
set(_sortie "${GEN}/sideart/bloc_sortie.png")
add_custom_command(
    OUTPUT  "${_sortie}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
    COMMAND marqueeart --titre=SORTIE "--teinte=0.20,0.86,0.36"
            --width=1024 --height=394 "${_sortie}"
    DEPENDS marqueeart
    COMMENT "Bloc de secours : bloc_sortie.png"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${GEN}/scene/textures/bloc_sortie.png"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_sortie}" "${GEN}/scene/textures/bloc_sortie.png"
    DEPENDS "${GEN}/scene/textures/.stamp" "${_sortie}"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${GEN}/materials/bloc_sortie${_map_n}" "${GEN}/materials/bloc_sortie${_map_orm}"
    COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.35 "${_sortie}" "${GEN}/materials"
    DEPENDS texgen "${_sortie}"
    COMMENT "Materiau PBR : bloc_sortie"
    VERBATIM
)
list(APPEND MARQUEE_ART "${_sortie}" "${GEN}/scene/textures/bloc_sortie.png"
                        "${GEN}/materials/bloc_sortie${_map_n}"
                        "${GEN}/materials/bloc_sortie${_map_orm}")

# DEDALE et APLOMB portaient « PAC-MAN » et « TETRIS », deux MARQUES DÉPOSÉES
# (Bandai Namco, Tetris Holding), écrites en toutes lettres sur cinq enseignes
# et deux écrans d'attente. Une enseigne est ce qu'on lit d'une borne AVANT d'y
# jouer : c'était l'endroit le plus visible du paquet. Les teintes, elles, ne
# bougent pas — une couleur n'appartient à personne, et c'est par sa couleur
# qu'on retrouve une borne dans l'allée.
set(MARQUEE_shooter  "SHOOTER"  "1.0,0.36,0.14")
set(MARQUEE_demineur "DEMINEUR" "1.0,0.74,0.20")
set(MARQUEE_dedale   "DEDALE"   "1.0,0.84,0.16")
set(MARQUEE_piano    "PIANO"    "0.72,0.45,1.0")
# Les quatre teintes ajoutées suivent la même règle que les quatre premières :
# c'est la laque du flanc, remontée juste assez pour qu'une plaque rétroéclairée
# ne sorte pas grise. ASTEROID fait exception et c'est le caisson qui le veut :
# `flanc_asteroid` vaut 0,16 / 0,16 / 0,20, un noir — une enseigne de cette
# couleur ne se lirait pas. Elle prend le blanc bleuté du vaisseau et des
# étoiles de son propre écran. SNAKE est décalé vers le jaune-vert pour ne pas
# se confondre, dans la même rangée, avec le vert de FLAPPY.
set(MARQUEE_envol    "ENVOL"    "0.22,0.88,0.40")
set(MARQUEE_aplomb   "APLOMB"   "0.34,0.52,1.0")
set(MARQUEE_asteroid "ASTEROID" "0.72,0.80,1.0")
set(MARQUEE_snake    "SNAKE"    "0.66,0.94,0.26")
foreach(jeu shooter demineur dedale piano envol aplomb asteroid snake)
    list(GET MARQUEE_${jeu} 0 _titre)
    list(GET MARQUEE_${jeu} 1 _teinte)
    set(_png "${GEN}/sideart/marquee_${jeu}.png")
    add_custom_command(
        OUTPUT  "${_png}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
        COMMAND marqueeart "--titre=${_titre}" "--teinte=${_teinte}" "${_png}"
        DEPENDS marqueeart
        COMMENT "Enseigne de borne : marquee_${jeu}.png"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${GEN}/scene/textures/marquee_${jeu}.png"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_png}" "${GEN}/scene/textures/marquee_${jeu}.png"
        DEPENDS "${GEN}/scene/textures/.stamp" "${_png}"
        VERBATIM
    )
    # `--strength=0.5` : une enseigne est une plaque de PLEXIGLAS. Le relief
    # deduit du contraste y graverait les lettres, or elles sont imprimees
    # dessous et la surface est lisse.
    add_custom_command(
        OUTPUT  "${GEN}/materials/marquee_${jeu}${_map_n}" "${GEN}/materials/marquee_${jeu}${_map_orm}"
        COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.5 "${_png}" "${GEN}/materials"
        DEPENDS texgen "${_png}"
        COMMENT "Materiau PBR : marquee_${jeu}"
        VERBATIM
    )
    list(APPEND MARQUEE_ART "${_png}" "${GEN}/scene/textures/marquee_${jeu}.png"
                            "${GEN}/materials/marquee_${jeu}${_map_n}"
                            "${GEN}/materials/marquee_${jeu}${_map_orm}")
endforeach()

# LES ÉCRANS D'ATTENTE : deux qui manquaient, deux qu'il a fallu remplacer.
#
# SPEC-ATMOSPHERE poste 14. Quatre bornes sur dix-neuf déclaraient un écran qui
# dit que le jeu n'est pas là : `coming_soon.jpg` sur les deux bornes du jeu de
# labyrinthe (7 et 18), `chargement.png` sur les deux bornes Piano (8 et 15).
# Les deux jeux sont jouables, et leurs enseignes le disaient déjà — c'est le
# poste 12 de `DESIGN-SALLE.md` fait à moitié : les marquees ont été dessinés,
# les écrans non.
#
# LES DEUX DE FLAPPY s'ajoutent ici, et pour la raison juridique cette fois.
# `flappy_easy_font.jpg` et `flappy_hard_font.jpg` sont des captures du jeu de
# 2020 : ouvertes, elles montrent L'OISEAU DE FLAPPY BIRD et SES TUYAUX, en
# grand, sur deux dalles devant lesquelles le joueur passe. Les planches du jeu
# viennent d'être redessinées pour cette raison exacte (voir « LES PLANCHES DES
# MINI-JEUX ») ; laisser les mêmes sprites accrochés à deux écrans de la salle
# aurait défait le geste, et aurait en plus montré un jeu qui n'existe plus.
# Les deux fichiers sortent de `_room_textures` comme les huit affiches, donc du
# compte ET de la copie.
#
# Même outil et même fonte que les enseignes, en mode `--neon` : un écran
# d'attente n'est PAS un marquee. Un marquee est une plaque de plexiglas
# rétro-éclairée, donc claire de bord à bord ; un tube cathodique est noir avec
# du phosphore dessus. Mesuré sur les deux planches d'essai : le mode marquee
# sort à 0,241 de réflectance, le mode neon à 0,191, et surtout son fond est
# noir — or `gbuffer.frag` multiplie l'émissif par l'albédo texel par texel, si
# bien qu'un fond clair ferait luire toute la dalle au lieu du seul lettrage.
#
# 1024 x 576, le 16:9 de la dalle et de la cible d'attract (512 x 288). Les deux
# images retirées faisaient 1562 x 727 et 1024 x 512 : elles n'étaient pas au
# format d'un écran, ce qui est un indice de plus qu'elles étaient des
# bouche-trous.
#
# La teinte est celle de l'enseigne du même jeu : une borne se lit de loin par
# sa couleur, et son écran ne doit pas la démentir.
#
# Les deux écrans de Flappy portent le MÊME titre et deux teintes différentes :
# les deux bornes lancent le même jeu, seule la difficulté change, et c'est déjà
# ainsi que la salle distingue une paire — par la couleur, jamais par le mot.
set(ECRAN_dedale        "DEDALE" "1.0,0.84,0.16")
set(ECRAN_piano         "PIANO"  "0.72,0.45,1.0")
set(ECRAN_envol_facile "ENVOL" "0.22,0.88,0.40")
set(ECRAN_envol_dur    "ENVOL" "1.0,0.52,0.20")
foreach(jeu dedale piano envol_facile envol_dur)
    list(GET ECRAN_${jeu} 0 _titre)
    list(GET ECRAN_${jeu} 1 _teinte)
    set(_png "${GEN}/sideart/ecran_${jeu}.png")
    add_custom_command(
        OUTPUT  "${_png}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
        COMMAND marqueeart "--titre=${_titre}" "--teinte=${_teinte}" --neon
                --width=1024 --height=576 "${_png}"
        DEPENDS marqueeart
        COMMENT "Écran d'attente : ecran_${jeu}.png"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${GEN}/scene/textures/ecran_${jeu}.png"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_png}" "${GEN}/scene/textures/ecran_${jeu}.png"
        DEPENDS "${GEN}/scene/textures/.stamp" "${_png}"
        VERBATIM
    )
    # `--strength=0.35`, comme l'enseigne au néon : la dalle est PLATE. Le relief
    # déduit du contraste y graverait le lettrage en creux, or on regarde à
    # travers une vitre bombée.
    add_custom_command(
        OUTPUT  "${GEN}/materials/ecran_${jeu}${_map_n}" "${GEN}/materials/ecran_${jeu}${_map_orm}"
        COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.35 "${_png}" "${GEN}/materials"
        DEPENDS texgen "${_png}"
        COMMENT "Materiau PBR : ecran_${jeu}"
        VERBATIM
    )
    list(APPEND MARQUEE_ART "${_png}" "${GEN}/scene/textures/ecran_${jeu}.png"
                            "${GEN}/materials/ecran_${jeu}${_map_n}"
                            "${GEN}/materials/ecran_${jeu}${_map_orm}")
endforeach()



# ---------------------------------------------------------------------------
# LES AFFICHES DES MURS — et la raison JURIDIQUE de les dessiner
# ---------------------------------------------------------------------------
#
# Les huit `poster_*` de 2020 ne sont pas des textures : ce sont des ŒUVRES
# TIERCES sous droit exclusif, employées sans droit et livrées dans le paquet.
# Ouvertes une par une : `poster_7` est le flyer d'arcade PAC-MAN au logo
# Midway, `poster_8` celui de DONKEY KONG au logo Nintendo, `poster_3` un flyer
# ATARI, `poster_6` l'affiche « HAWKINS / PALACE ARCADE » de Stranger Things
# avec ses personnages, `poster_5` une illustration Arcade de League of Legends,
# `poster_2` l'arcade « SPACE PARANOIDS / ENCOM » de Tron. `poster_1` et
# `poster_4` sont d'origine inconnue, ce qui ne vaut pas mieux.
#
# AUCUNE RÉDACTION DE `LICENSES.md` NE LES REND DISTRIBUABLES. Une attribution
# répare une licence non tenue — c'était le cas de CesiumMan — mais elle ne
# donne pas un droit qu'on n'a pas. Le jeu ne pouvait pas être vendu tant
# qu'elles y étaient, et il ne suffisait pas de cesser de les NOMMER : la copie
# groupée plus bas emporte tout le répertoire, employé ou non. Elles sortent
# donc de la liste elle-même, et le paquet ne les contient plus.
#
# Elles restent dans `legacy/`, qui est l'archive du projet de 2020 et n'est pas
# distribuée. Ce qui change ici, c'est ce que le build COPIE.
#
# Les huit remplaçantes sont DESSINÉES, comme le sont déjà les enseignes de
# borne et le bloc de secours : l'iconographie des années 80 — grilles en
# perspective, néons, dégradés, typographie à empattements carrés — appartient à
# tout le monde ; ses personnages et ses logos non. Huit intentions différentes
# plutôt que huit variations d'une seule, parce que huit fois la même grille
# rose serait pire que ce qu'on remplace.
#
# `plan` LIT la description de la salle au lieu de la recopier : une affiche qui
# montrerait un plan périmé serait pire qu'une affiche absente, et celle-ci suit
# les murs, les dix-neuf bornes et la position de départ à chaque build.
#
# `lots` N'EST PAS UNE AFFICHE MURALE : c'est la face du meuble `vitrine_lots`,
# et elle sort donc en PAYSAGE. Ses cotes viennent du meuble — la face fait
# 1,62 x 0,76 m, soit 2,13:1 — quand les huit autres remplissent des cadres de
# 0,777 x 0,971 m. Sortie au gabarit portrait, elle serait etiree de 113 %.
#
# Ce qu'elle remplace : la vitrine portait `affiche_6`, c'est-a-dire
# `affiche_attention.png`, l'avertissement « lumieres clignotantes ». Une
# vitrine a lots qui affiche un avertissement d'epilepsie apprend au joueur que
# ce meuble ne le concerne pas. L'avertissement reste accroche au mur, ou il
# doit etre.
set(POSTER_MOTIFS tournoi reglement jetons concert plan attention orbite records lots)
foreach(motif IN LISTS POSTER_MOTIFS)
    set(_png "${GEN}/sideart/affiche_${motif}.png")
    set(_dim --width=820 --height=1024)
    if(motif STREQUAL "lots")
        set(_dim --width=1024 --height=480)
    endif()
    add_custom_command(
        OUTPUT  "${_png}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
        COMMAND posterart "--motif=${motif}" ${_dim}
                          "--salle=${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json"
                          "${_png}"
        DEPENDS posterart "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json"
        COMMENT "Affiche murale : affiche_${motif}.png"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${GEN}/scene/textures/affiche_${motif}.png"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_png}"
                                   "${GEN}/scene/textures/affiche_${motif}.png"
        DEPENDS "${GEN}/scene/textures/.stamp" "${_png}"
        VERBATIM
    )
    # `--strength=0.5`, comme les enseignes : une affiche est IMPRIMÉE sur un
    # papier lisse. Le relief déduit du contraste y graverait les lettres.
    add_custom_command(
        OUTPUT  "${GEN}/materials/affiche_${motif}${_map_n}"
                "${GEN}/materials/affiche_${motif}${_map_orm}"
        COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=0.5 "${_png}" "${GEN}/materials"
        DEPENDS texgen "${_png}"
        COMMENT "Materiau PBR : affiche_${motif}"
        VERBATIM
    )
    list(APPEND MARQUEE_ART "${_png}" "${GEN}/scene/textures/affiche_${motif}.png"
                            "${GEN}/materials/affiche_${motif}${_map_n}"
                            "${GEN}/materials/affiche_${motif}${_map_orm}")
endforeach()

# ---------------------------------------------------------------------------
# LE TAPIS DU HALL — l'endroit où personne n'avait regardé
# ---------------------------------------------------------------------------
#
# `floor.jpg` est la plus grande surface de la salle. Ouverte et agrandie, elle
# porte AU NÉON : un PAC-MAN (le disque et sa part de camembert), un de ses
# FANTÔMES (le dôme à jupe ondulée), les CERISES de bonus du même jeu, et la
# silhouette d'une MANETTE de console. Trois personnages de Bandai Namco et un
# matériel de Sony, allumés sous les pieds du joueur dans chaque image de la
# salle — pendant qu'on retirait les mêmes personnages du jeu de labyrinthe.
#
# Ce que `salle.room.json` documente du tapis reste ENTIÈREMENT vrai et est
# tenu : un tissage sombre, des motifs fluo, l'émissif à 0,10 qui ne fait luire
# que les motifs parce que `gbuffer.frag` multiplie l'émissif par l'albédo texel
# par texel. C'est « l'objet le plus reconnaissable de la salle » et il le
# reste ; ce sont les FIGURES qui changent — un manche, un jeton, une étoile, un
# éclair, un dé, une note, une cible, une planète.
#
# `moquetteart` la dessine DÉJÀ DÉ-CUITE, et c'est pour cela qu'elle passe par
# le chemin des affiches et non par la table `ALBEDO_*` : `floor_c.png` sort à
# 0,1061 de réflectance linéaire moyenne, la planche dessinée à 0,1145 — 8 %
# d'écart, mesurés des deux côtés. Il n'y a donc rien à corriger, et une
# dé-cuisson de plus ne ferait que déplacer un chiffre déjà juste.
set(_moquette "${GEN}/sideart/moquette_neon.png")
add_custom_command(
    OUTPUT  "${_moquette}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
    COMMAND moquetteart "${_moquette}"
    DEPENDS moquetteart
    COMMENT "Tapis du hall : moquette_neon.png"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${GEN}/scene/textures/moquette_neon.png"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_moquette}"
                               "${GEN}/scene/textures/moquette_neon.png"
    DEPENDS "${GEN}/scene/textures/.stamp" "${_moquette}"
    VERBATIM
)
# Pas de `--strength` réduit ici, contrairement aux enseignes et aux affiches :
# une moquette bouclée a un VRAI relief, et c'est la seule texture de ce lot
# dont on veuille que le grain se voie en lumière rasante.
add_custom_command(
    OUTPUT  "${GEN}/materials/moquette_neon${_map_n}"
            "${GEN}/materials/moquette_neon${_map_orm}"
    COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} "${_moquette}" "${GEN}/materials"
    DEPENDS texgen "${_moquette}"
    COMMENT "Materiau PBR : moquette_neon"
    VERBATIM
)
list(APPEND MARQUEE_ART "${_moquette}" "${GEN}/scene/textures/moquette_neon.png"
                        "${GEN}/materials/moquette_neon${_map_n}"
                        "${GEN}/materials/moquette_neon${_map_orm}")

# ---------------------------------------------------------------------------
# LE CREPI DES MURS
# ---------------------------------------------------------------------------
#
# Meme geste que la moquette, sur l'autre plan. `mur_gris.jpg` habillait 197 m2
# — la plus grande surface VERTICALE de la salle — et deux mesures disent
# qu'elle ne pouvait pas se lire comme un mur.
#
# LE GRAIN. 600 px pour 0,70 m, donc 1,17 mm par texel. `salle.room.json`
# justifiait ce 0,70 en affirmant un « grain visible de trois a quatre texels »,
# soit 3,5 mm. Ouverte et agrandie, la tache dominante de cette photo fait dix a
# douze texels : 12 a 14 mm. Ce n'est pas un crepi ecrase, c'est du tyrolien
# projete. La capture du couloir le montre sans mesure : a un metre, les murs
# sont en pop-corn.
#
# LE CONTRASTE. Ecart-type de luminance 32,3 sur 255. Une peinture mate ne varie
# pas comme ca en ALBEDO — ce qu'on voit sur un mur est du RELIEF, donc de
# l'ombre, et l'ombre vient des lampes de la piece. Un albedo a 32 rejoue la
# meme ombre partout, a une frequence que l'ecran ne resout pas : a trois
# metres, une tache de 12 mm tombe sous deux pixels et devient du grésil.
#
# Le mip n'y est pour rien et fonctionne — la capture du couloir montre le grain
# grossir correctement quand on s'approche. Ce qui manquait n'est pas un filtre
# mais une texture dont le spectre s'arrete AVANT Nyquist, et c'est ce que
# `murart` dessine : maille la plus fine a quatre texels, spectre en U (la dent
# du crepi, un creux, la moucheture du rouleau), reflectance identique a la
# quatrieme decimale sur les trois canaux.
set(_crepi "${GEN}/sideart/mur_crepi.png")
add_custom_command(
    OUTPUT  "${_crepi}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sideart"
    COMMAND murart "${_crepi}"
    DEPENDS murart
    COMMENT "Crepi des murs : mur_crepi.png"
    VERBATIM
)
add_custom_command(
    OUTPUT  "${GEN}/scene/textures/mur_crepi.png"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/scene/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_crepi}"
                               "${GEN}/scene/textures/mur_crepi.png"
    DEPENDS "${GEN}/scene/textures/.stamp" "${_crepi}"
    VERBATIM
)
# `--strength` RELEVE, et c'est la consequence directe du reglage precedent :
# `texgen` derive la normale du contraste local de l'albedo, et on vient de
# ramener ce contraste de 32,3 a 7,2 d'ecart-type. A force egale, la normale
# serait quatre fois plus plate que celle de la photo — on aurait remplace un
# mur en gravier par un mur en plastique. Le relief passe donc dans la NORMALE,
# ou il a toujours du etre, et l'albedo reste ce qu'il doit etre : presque uni.
add_custom_command(
    OUTPUT  "${GEN}/materials/mur_crepi${_map_n}"
            "${GEN}/materials/mur_crepi${_map_orm}"
    COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} --strength=2.2
                   "${_crepi}" "${GEN}/materials"
    DEPENDS texgen "${_crepi}"
    COMMENT "Materiau PBR : mur_crepi"
    VERBATIM
)
list(APPEND MARQUEE_ART "${_crepi}" "${GEN}/scene/textures/mur_crepi.png"
                        "${GEN}/materials/mur_crepi${_map_n}"
                        "${GEN}/materials/mur_crepi${_map_orm}")

# Les images de tiers, retirées de TOUTES les listes : compte, dé-cuisson et
# copie. Un `retiredTextures` ne suffirait pas — il déclare qu'une image n'est
# plus employée, il n'empêche pas de la livrer.
#
# Les huit affiches, plus les DEUX ÉCRANS D'ATTRACT DE FLAPPY, qui sont des
# captures du jeu de 2020 montrant l'oiseau et les tuyaux de Flappy Bird. Ce
# sont les deux seules captures de ce lot à contenir de l'art emprunté : les
# cinq autres (`snake_font`, `tetris_font`, `asteroid_font`, `demineur_font`,
# `shooter_font`) montrent des jeux dont l'art appartient bien au projet de
# 2020, et elles restent. Ouvertes une par une pour le vérifier.
set(TEXTURES_TIERS poster_1.png poster_2.png poster_3.jpg poster_4.jpg
                   poster_5.jpg poster_6.jpg poster_7.jpg poster_8.jpg
                   flappy_easy_font.jpg flappy_hard_font.jpg
                   floor.jpg)

file(GLOB _room_textures "${SRC_ROOM}/textures/*.jpg" "${SRC_ROOM}/textures/*.png")
foreach(_p IN LISTS TEXTURES_TIERS)
    list(REMOVE_ITEM _room_textures "${SRC_ROOM}/textures/${_p}")
endforeach()

file(GLOB _cc0_textures  "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.jpg"
                         "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.png")
list(LENGTH _room_textures _n_room)
list(LENGTH _cc0_textures  _n_cc0)
# +14 : l'art latéral, la sérigraphie de panneau, les HUIT enseignes de borne,
# l'enseigne au néon de la salle, le bloc de secours du sas et les DEUX ÉCRANS
# D'ATTENTE de Pac-Man et Piano, dessinés juste au-dessus.
#
# Il valait +9. Les cinq de plus sont les quatre enseignes qui manquaient
# — FLAPPY, TETRIS, ASTEROID, SNAKE — et « bloc_sortie.png ». Les cinq images
# qu'elles remplacent sortent du décor en même temps : les quatre atlas de
# caisson et « exit.png ». Le total accordé ne bouge donc que de +5, celui des
# cinq planches ajoutées, et les cinq retirées sont dans `retiredTextures`.
#
# Les deux écrans font sortir `coming_soon.jpg` et `chargement.png` du décor :
# elles passent dans `retiredTextures` avec leur motif, et le total accordé ne
# bouge donc que de +2, celui des deux planches ajoutées.
#
# Ce compte est un INVARIANT vérifié par roomgen, pas une constante décorative :
# ajouter une texture sans le mettre à jour arrête le build avec « 84 pour 83
# présentes ». C'est exactement ce qui vient d'arriver en ajoutant le néon, et
# c'est le comportement voulu — une texture générée que la salle n'emploie pas,
# ou une texture employée qui n'est pas générée, sont deux pannes muettes.
# +25 : les huit affiches murales dessinees s'ajoutent aux quatorze planches
# deja generees, puis les DEUX ECRANS D'ATTRACT et le TAPIS DU HALL. Les onze
# images qu'elles remplacent — huit affiches, deux captures, une moquette —
# sortent du compte en meme temps, retirees de `_room_textures` : le total ne
# bouge pas. Trois planches en plus, trois fichiers en moins, la salle en
# emploie toujours autant.
#
# +27, et les DEUX raisons sont a lire ensemble parce qu'elles sont arrivees par
# deux chemins differents et qu'elles ont failli s'annuler : chacun des deux
# lots a monte ce compte de 25 a 26, pour une planche differente. Une resolution
# distraite aurait garde 26 et perdu une planche sans que rien ne le dise —
# sauf ce controle, qui l'aurait justement dit.
#
#   +1 LE CREPI DES MURS. Il ne compense PAS `mur_gris.jpg` dans ce compte-ci,
#   et c'est la difference entre les deux controles : `roomgen` compte ce que la
#   salle DECLARE — employe ou retire — donc l'image remplacee reste au total en
#   passant dans `retiredTextures`, et la planche dessinee s'ajoute. Le controle
#   de COPIE, lui, cesse de l'emporter dans le paquet.
#
#   +1 `affiche_lots`, la neuvieme planche de `posterart`, qui est la face de la
#   vitrine et non une affiche murale. Elle ne remplace aucun fichier — elle
#   remplace un EMPLOI : le meuble portait `affiche_6`, l'avertissement
#   « lumieres clignotantes », qui reste accroche au mur ou il doit etre.
#
# Ce controle a fait exactement son travail sur ces deux lots : chaque planche
# ajoutee sans le compte a arrete le build (« 92 pour 91 presentes »). C'est le
# comportement voulu.
math(EXPR ROOM_TEXTURE_COUNT "${_n_room} + ${_n_cc0} + 27")

add_custom_command(
    OUTPUT  "${GEN}/scene/salle.gltf" "${GEN}/scene/salle.bin"
            "${GEN}/scene/salle.lights.json" "${GEN}/scene/salle.scene.json"
    COMMAND roomgen "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json"
                    "${GEN}/scene/salle.gltf"
                    "--textures=${SRC_ROOM}/textures"
                    "--textures=${CMAKE_CURRENT_SOURCE_DIR}/cc0"
                    "--textures=${GEN}/sideart"
                    "--expect-textures=${ROOM_TEXTURE_COUNT}"
                    # Racine des modèles CC0 instanciés dans la salle.
                    "--assets=${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS roomgen "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json"
            ${CC0_MODELS} ${BLENDER_MODELS} "${SIDEART}" "${PANELART}"
            ${MARQUEE_ART}
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
# Voir les blocs « LES AFFICHES DES MURS » et « LES ECRANS D'ATTENTE » : ces dix-la
# sont des oeuvres tierces ou des captures qui en contiennent, et ne doivent ni
# etre de-cuites, ni etre copiees.
foreach(_p IN LISTS TEXTURES_TIERS)
    list(REMOVE_ITEM LEGACY_TEXTURES "${SRC_ROOM}/textures/${_p}")
endforeach()

# ---------------------------------------------------------------------------
# CE QUE LA SALLE DECLARE RETIRE, LE PAQUET NE L'EMPORTE PLUS
# ---------------------------------------------------------------------------
#
# `retiredTextures` de `salle.room.json` nomme les images que la salle n'emploie
# PLUS, chacune avec le motif de son retrait. C'etait une declaration a l'usage
# de `roomgen`, qui verifie qu'une texture presente mais inutilisee l'est
# DELIBEREMENT. La copie, elle, ne la lisait pas : les dix-huit images retirees
# partaient dans le paquet comme les autres.
#
# Deux d'entre elles rendent la chose grave plutot que negligente. Les atlas de
# caisson `flappy.jpg` et `tetris.jpg` portent, mis a plat, l'OISEAU de Flappy
# Bird et le mot TETRIS — une oeuvre d'auteur et une marque deposee. Retires du
# decor il y a deux versions, ils continuaient d'etre livres. Une declaration
# que le build n'honore pas ne protege de rien.
#
# La liste n'est PAS recopiee ici : une seconde liste est une liste qui derive.
# Et le compte de `--expect-textures` ne bouge pas — `roomgen` compte ce que la
# salle DECLARE, employe ou retire ; celui-ci decide de ce que le paquet
# EMPORTE. Les deux controles regardent deux choses differentes.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scene/salle.room.json" _room_desc_json)
string(JSON _n_retired ERROR_VARIABLE _retired_err
       LENGTH "${_room_desc_json}" retiredTextures)
if(_retired_err)
    message(FATAL_ERROR
        "assets/scene/salle.room.json : « retiredTextures » illisible (${_retired_err}).\n"
        "  Sans elle, le build recopierait dans le paquet des images que la salle "
        "declare retirees — dont deux qui portent une oeuvre tierce.")
endif()
math(EXPR _retired_last "${_n_retired} - 1")
foreach(_i RANGE ${_retired_last})
    string(JSON _f GET "${_room_desc_json}" retiredTextures ${_i} file)
    list(REMOVE_ITEM LEGACY_TEXTURES "${SRC_ROOM}/textures/${_f}")
    # Gardee pour la purge des gravats, plus bas : une image retiree laisse
    # derriere elle sa copie et ses cartes de-cuites.
    list(APPEND RETIRED_TEXTURES "${_f}")
endforeach()
message(STATUS "Textures declarees retirees, non copiees : ${_n_retired}")

# Un seul répertoire pour TOUS les albédos rapportés, y compris ceux des modèles.
# Poly Haven les range dans un sous-dossier par modèle ; on les remonte ici parce
# que la chaîne (copie, puis normale et ORM dérivées par `texgen`) est la même
# pour tous, et qu'un chemin par modèle ne ferait qu'ajouter des cas.
file(GLOB CC0_TEXTURES    "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.jpg"
                          "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.png")
set(ROOM_TEXTURES ${LEGACY_TEXTURES} ${CC0_TEXTURES})

#
# La réflectance visée, texture par texture — la DÉ-CUISSON.
#
# Mesuré sur les sources de 2020, en réflectance linéaire : la moquette est à
# 0,029, le plafond et l'estrade à 0,002, les poutres à 0,008. Du bitume frais
# en fait 0,04, du charbon 0,04. Ces surfaces ne sont pas sombres, elles sont
# impossibles — et ce sont les plus grandes de la salle.
#
# Ce n'est pas une faute de l'auteur : son moteur n'éclairait rien, donc l'ombre
# devait être PEINTE dans la texture pour qu'une salle d'arcade tamisée
# ressemble à une salle d'arcade tamisée. Un moteur PBR l'éclaire par-dessus :
# la pénombre est appliquée deux fois, et aucune quantité de lumière ne rattrape
# une réflectance de deux pour mille. C'est l'explication, en une ligne, des deux
# reproches faits à l'image — « les pièces sont trop sombres » ET « les textures
# mal choisies ». C'est le même défaut.
#
# La preuve par l'exception : les toilettes étaient la SEULE pièce lisible de la
# salle (médiane 105 sur 255, contre 8 pour la borne de classement). Leur faïence
# murale est à 0,872, parce que l'auteur ne l'a pas assombrie.
#
# Trois règles pour cette table :
#
# 1. **Seules les textures de 2020 y figurent.** Les cartes CC0 de Poly Haven
#    sont des couleurs de base authentiques : un canapé sombre est vraiment à
#    0,055, et le « corriger » l'abîmerait.
# 2. **Les images ne sont pas des matériaux.** Écrans de jeu, marquees,
#    affiches, enseignes et `chargement.png` sont des images qu'on regarde, pas
#    des surfaces qu'on éclaire. Elles n'ont rien de cuit, elles restent
#    intactes — et c'est pour ça que la table est explicite plutôt qu'une règle
#    par défaut.
# 3. **Les cibles sont physiques**, pas esthétiques : 0,10 pour une moquette
#    commerciale sombre, 0,42 pour une dalle de plafond à motif foncé, 0,32 pour
#    de la porcelaine de lavabo. Ce qui est trop clair pour la salle se règle
#    ensuite avec la LUMIÈRE, qui est faite pour ça.
#
# Les textures déjà correctes n'y sont pas : `mur_gris` (0,339), `mur_brique`
# (0,248), `desk` (0,264), `cuir_rouge` (0,705), `marbre_toilettes` (0,813) sont
# laissées telles quelles.
#
set(ALBEDO_plafond            0.42)   # 0,002 — la plus grande surface de la salle
set(ALBEDO_moquette           0.10)   # 0,029
set(ALBEDO_moquette_noire     0.055)  # 0,002 — le plus sombre qu'une moquette puisse être
# `floor` n'y est plus : le tapis du hall est desormais DESSINE par
# `tools/moquetteart` et sort deja de-cuit, a 0,1145 contre les 0,1061 que la
# de-cuisson produisait. La cible reste ecrite ici parce qu'elle documente ce
# qu'une moquette d'arcade doit reflechir, et c'est elle que l'outil vise.
#   ALBEDO_floor            0.11    # 0,039 avant de-cuisson
set(ALBEDO_gris_foncer        0.09)   # 0,015 — le cadre des dix-neuf bornes
set(ALBEDO_bois_noir          0.075)  # 0,016
set(ALBEDO_bois_moins_noir    0.12)   # 0,011
set(ALBEDO_bois               0.17)   # 0,070
set(ALBEDO_marron_foncer      0.10)   # 0,004
set(ALBEDO_porte_exterieur    0.13)   # 0,018
set(ALBEDO_carllage_toilette  0.20)   # 0,019 — un carrelage de sol
set(ALBEDO_lavabo             0.32)   # 0,019 — de la porcelaine à deux pour cent
set(ALBEDO_bordeaux           0.10)   # 0,026
set(ALBEDO_bordeau_uni        0.10)   # 0,016
set(ALBEDO_bleu               0.14)   # 0,065
set(ALBEDO_billard_table      0.14)   # 0,082 — drap de billard
set(ALBEDO_pilonne_rouge      0.16)   # 0,105
set(ALBEDO_Radio              0.16)   # 0,117

# ---------------------------------------------------------------------------
# L'aplanissement : les textures PAVÉES qui portent un éclairage cuit
# ---------------------------------------------------------------------------
#
# `--albedo` corrige la MOYENNE d'une texture ; il ne voit pas le dégradé qui
# la traverse. Or un dégradé dans une image pavée se répète à chaque carreau :
# on ne lit plus une surface, on lit une grille de taches claires, et cet
# éclairage-là ne vient d'aucune lampe de la salle.
#
# La sélection s'est faite en DEUX temps, et le second n'est pas facultatif.
#
# D'abord une mesure : amplitude basse fréquence rapportée à la moyenne (image
# ramenée à 16 x 16, écart max-min sur moyenne). Six textures pavées sur
# vingt-deux dépassaient 0,70 ; toutes les autres étaient sous 0,40.
#
# Puis un COUP D'ŒIL sur chacune des six, et il en a écarté trois — la mesure
# seule se trompe, et il faut dire comment :
#
#   `desk` (1,19) est un motif vectoriel de triangles, plat, SANS le moindre
#   éclairage cuit. Son amplitude vient de ses couleurs : des triangles bleu
#   nuit à côté de triangles jaunes. L'aplanir ne retirait aucune lumière, ça
#   délavait le dessin — mesuré, 0,49 -> 0,65 à la grille 6 x 6, donc pire.
#
#   `bois_moins_noir` (0,79) et `carllage_toilette` (0,71) sont presque noirs
#   (moyennes 0,124 et 0,161). À cette luminance l'amplitude relative explose
#   pour un écart absolu minuscule, et le gain n'amplifierait que le bruit de
#   compression JPEG.
#
# La leçon, pour la prochaine texture ajoutée : **cette mesure trie, elle ne
# décide pas.** Un dégradé d'éclairage se voit — il est doux, unidirectionnel
# ou radial. Un motif aussi.
#
set(FLATTEN_bois_noir   1.0)   # vignette radiale nette : centre clair, coins sombres
set(FLATTEN_bois        0.7)   # chêne, coin haut-gauche assombri

set(GENERATED_MAPS "${SIDEART}" "${SIDEART_RUNTIME}"
                   "${GEN}/materials/flanc_borne${_map_n}"
                   "${GEN}/materials/flanc_borne${_map_orm}"
                   "${PANELART}" "${PANELART_RUNTIME}"
                   "${GEN}/materials/panneau_borne${_map_n}"
                   "${GEN}/materials/panneau_borne${_map_orm}"
                   ${MARQUEE_ART})
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

    # La dé-cuisson, quand cette texture en a une de déclarée. `texgen` produit
    # alors `<nom>_c.png`, que le moteur préfère à l'image du glTF — voir
    # `engine/scene/ns_scene.c`, résolution des triplets.
    set(texgen_outputs "${GEN}/materials/${tex_name}${_map_n}"
                       "${GEN}/materials/${tex_name}${_map_orm}")
    set(albedo "")
    set(flatten "")
    if(DEFINED ALBEDO_${tex_name})
        set(albedo "--albedo=${ALBEDO_${tex_name}}")
    endif()
    if(DEFINED FLATTEN_${tex_name})
        set(flatten "--flatten=${FLATTEN_${tex_name}}")
    endif()
    # L'un ou l'autre suffit à produire `<nom>_c.png` : une texture aplanie mais
    # dont la moyenne était déjà correcte doit quand même remplacer l'image du
    # glTF, sinon l'aplanissement ne servirait qu'aux cartes.
    if(DEFINED ALBEDO_${tex_name} OR DEFINED FLATTEN_${tex_name})
        list(APPEND texgen_outputs "${GEN}/materials/${tex_name}_c.png")
    endif()

    add_custom_command(
        OUTPUT  ${texgen_outputs}
        COMMAND texgen --max-map=${NINETEEN_MAX_MAP} ${_bc} ${upscale} ${albedo} ${flatten} "${tex}" "${GEN}/materials"
        DEPENDS texgen "${tex}"
        COMMENT "Matériau PBR : ${tex_name}"
        VERBATIM
    )
    list(APPEND GENERATED_MAPS ${texgen_outputs})

    # Le diffus d'origine est copié tel quel à côté de ses cartes dérivées :
    # le glTF y fait référence sous textures/<nom>.
    add_custom_command(
        OUTPUT  "${GEN}/scene/textures/${tex_name}${CMAKE_MATCH_0}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${tex}" "${GEN}/scene/textures/"
        DEPENDS "${GEN}/scene/textures/.stamp" "${tex}"
        VERBATIM
    )
endforeach()

# Copie groupée des diffus (plus simple et plus rapide qu'une règle par fichier).
# LE REPERTOIRE EST VIDE AVANT D'ETRE REMPLI, et ce n'est pas de la coquetterie.
#
# `copy_if_different` ajoute, il ne retire jamais. Une image qu'on cesse de
# copier reste donc indefiniment dans l'arbre de build — et de la, dans le
# PAQUET, puisque l'installation emporte le repertoire entier. C'est ainsi que
# deux images orphelines se sont retrouvees livrees sans que rien ne les nomme.
#
# Ce serait un defaut d'hygiene si les huit `poster_*` n'en faisaient pas un
# defaut JURIDIQUE : cesser de les copier ne suffisait pas a les faire sortir du
# paquet, leurs exemplaires d'hier y seraient restes. Un build incremental
# aurait donc continue de livrer des oeuvres tierces sous droit exclusif, en
# silence, pendant qu'on croyait le probleme regle.
#
# Le prix est de recopier une cinquantaine d'images quand une seule change.
# Cette regle ne se declenche que si une TEXTURE SOURCE a bouge, ce qui
# n'arrive presque jamais ; les planches dessinees, elles, ont leurs propres
# regles et sont recopiees apres celle-ci — d'ou la dependance ajoutee a chacune.
add_custom_command(
    OUTPUT "${GEN}/scene/textures/.stamp"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${GEN}/scene/textures"
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

# `stepgen` écrit dans le MÊME répertoire, juste après la copie : la banque de
# pas par matériau et les trois nappes d'ambiance sont synthétisées à chaque
# build plutôt que commitées. Elles sortent ici et pas dans un répertoire à part
# — contrairement à `sideart` — parce qu'aucun outil ne vérifie l'exhaustivité de
# `sounds/` comme `roomgen` le fait pour les textures : il n'y a donc rien à
# fausser en mélangeant les deux origines, et le moteur n'a qu'un chemin à
# connaître.
add_custom_command(
    OUTPUT "${GEN}/sounds/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOM_SOUNDS} "${GEN}/sounds/"
    COMMAND stepgen "${GEN}/sounds"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/sounds/.stamp"
    DEPENDS ${ROOM_SOUNDS} stepgen
    COMMENT "Copie des sons, puis synthèse des pas et des nappes d'ambiance"
    VERBATIM
)

# ---------------------------------------------------------------------------
# 3c. Les mini-jeux : planches de sprites et sons
# ---------------------------------------------------------------------------
#
# Les COTES sont reprises telles quelles de 2020 — oiseau 17 x 12, tuyau
# 26 x 160, chiffre 12 x 18, ciel 144 x 256 — parce que c'est ce que le code de
# 2020 découpait et que le portage le reprend au pixel près. Les IMAGES, elles,
# ne le sont plus : voir le bloc « LES PLANCHES DES MINI-JEUX » plus bas. Six
# des huit `.png` de ce répertoire sortent du paquet ; seul `chiffre.png` — dix
# chiffres carrés, sans marque ni personnage — est encore copié, et `spriteart`
# fournit les quatre planches que le jeu affiche.
#
# Le GLOB a été remplacé par une liste NOMMÉE, et c'est le point : c'est lui qui
# emportait `medals.png`, `scoreBoard.png` et `high_score.png` — trois images
# que le portage n'affiche même pas, dont une porte encore le filigrane d'une
# banque d'images. Un glob livre ce qu'on n'a pas regardé.
set(ENVOL_ART "${SRC_LEGACY}/games/3_flappy_bird/Textures/chiffre.png")
file(GLOB ENVOL_SOUND "${SRC_LEGACY}/games/3_flappy_bird/Sounds/*.wav")

add_custom_command(
    OUTPUT "${GEN}/games/envol/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/flappy"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ENVOL_ART} "${GEN}/games/envol/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ENVOL_SOUND} "${GEN}/games/envol/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/envol/.stamp"
    DEPENDS ${ENVOL_ART} ${ENVOL_SOUND}
            "${GEN}/games/envol/cerf.png" "${GEN}/games/envol/pylones.png"
            "${GEN}/games/envol/passerelle.png" "${GEN}/games/envol/ciels.png"
    COMMENT "Copie des sons d'Envol et de son atlas de chiffres"
    VERBATIM
)

# Snake. Même principe, avec une particularité : `config.h` de 2020 emprunte
# DEUX sons à d'autres jeux — l'explosion d'Asteroid pour la bombe, le game over
# de Tetris. Un glob sur son propre répertoire ne les prendrait pas, et le jeu
# tournerait muet sur ses deux moments les plus forts. Ils sont donc copiés
# explicitement, et renommés pour que `snake.c` les cherche à un seul endroit.
#
# ET UNE LISTE NOMMEE PLUTOT QU'UN GLOB. Le glob copiait les QUATORZE fichiers
# du repertoire ; `snake.c` en lit HUIT. Un glob livre ce que personne n'a
# nomme, et c'est ainsi qu'une image portant un filigrane « (C)123RF » s'est
# retrouvee dans le paquet d'une autre borne — trois de ses fichiers n'etaient
# meme pas affiches.
#
# Les six orphelins, et ce qu'ils sont : `chiffre.png` ne sert plus depuis que
# le score s'ecrit avec la fonte du moteur (ses contre-formes faisaient 1,2 px
# sur la dalle et tous les chiffres y etaient le meme rectangle blanc). Les cinq
# autres — `appear`, `disapear`, `rainbow`, `slow`, et l'`explo` du repertoire —
# sont les sons des POTIONS et de la bombe, que le portage n'a jamais branches :
# l'interface d'un mini-jeu n'offre que trois crochets (`sound_blip`,
# `sound_score`, `sound_die`) et Snake en emploie deux. Ce n'est donc pas un
# defaut de copie mais un manque de JEU, ecrit ici pour qu'il se retrouve : les
# sons existent, le jeu a les evenements, il manque un crochet.
set(SNAKE_ART
    "${SRC_LEGACY}/games/2_snake/Textures/backgroundSnake.png"
    "${SRC_LEGACY}/games/2_snake/Textures/hud.png"
    "${SRC_LEGACY}/games/2_snake/Textures/snake.png"
    "${SRC_LEGACY}/games/2_snake/Textures/fruits.png"
    "${SRC_LEGACY}/games/2_snake/Textures/anim.png"
    "${SRC_LEGACY}/games/2_snake/Textures/basket.png")
set(SNAKE_SOUND
    "${SRC_LEGACY}/games/2_snake/Sound/get.wav")

add_custom_command(
    OUTPUT "${GEN}/games/snake/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/snake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SNAKE_ART} "${GEN}/games/snake/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SNAKE_SOUND} "${GEN}/games/snake/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/5_tetris/Sound/gameover.wav" "${GEN}/games/snake/gameover.wav"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/snake/.stamp"
    DEPENDS ${SNAKE_ART} ${SNAKE_SOUND}
            "${SRC_LEGACY}/games/7_asteroid/Sound/explo.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/gameover.wav"
    COMMENT "Copie des planches et sons de Snake"
    VERBATIM
)

# Démineur : une seule planche, et aucun son propre — l'original empruntait déjà
# les siens ailleurs.
add_custom_command(
    OUTPUT "${GEN}/games/demineur/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/demineur"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/6_demineur/demineur.png" "${GEN}/games/demineur/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/demineur/.stamp"
    DEPENDS "${SRC_LEGACY}/games/6_demineur/demineur.png"
    COMMENT "Copie de la planche du Démineur"
    VERBATIM
)

# APLOMB — ex-« tetris ». Le RÉPERTOIRE DE DESTINATION change avec le jeu
# (`games/aplomb/`), la SOURCE non : `legacy/` est l'archive de 2020 et on ne la
# réécrit jamais.
#
# `bricks.png` reste, et c'est un choix regardé plutôt que subi : ouverte et
# agrandie, c'est une rangée de huit carrés biseautés de huit couleurs. Ni
# personnage, ni logo, ni lettrage — rien qui appartienne à quelqu'un de
# reconnaissable. Seul le NOM du jeu posait un problème, et il est réglé.
add_custom_command(
    OUTPUT "${GEN}/games/aplomb/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/aplomb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/5_tetris/Textures/bricks.png"
            "${SRC_LEGACY}/games/5_tetris/Sound/line.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/rotate.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/gameover.wav"
            "${GEN}/games/aplomb/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/aplomb/.stamp"
    DEPENDS "${SRC_LEGACY}/games/5_tetris/Textures/bricks.png"
            "${SRC_LEGACY}/games/5_tetris/Sound/line.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/rotate.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/gameover.wav"
    COMMENT "Copie de la planche et des sons d'Aplomb"
    VERBATIM
)

add_custom_command(
    OUTPUT "${GEN}/games/asteroid/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/asteroid"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/7_asteroid/Textures/background.png"
            "${SRC_LEGACY}/games/7_asteroid/Sound/shoot1.wav"
            "${SRC_LEGACY}/games/7_asteroid/Sound/explo.wav"
            "${SRC_LEGACY}/games/7_asteroid/Sound/big_explo.wav"
            "${GEN}/games/asteroid/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/asteroid/.stamp"
    DEPENDS "${SRC_LEGACY}/games/7_asteroid/Textures/background.png"
            "${SRC_LEGACY}/games/7_asteroid/Sound/shoot1.wav"
            "${SRC_LEGACY}/games/7_asteroid/Sound/explo.wav"
            "${SRC_LEGACY}/games/7_asteroid/Sound/big_explo.wav"
    COMMENT "Copie du fond et des sons d'Asteroid"
    VERBATIM
)

# ---------------------------------------------------------------------------
# LES PLANCHES DES MINI-JEUX, et la raison JURIDIQUE de les dessiner
# ---------------------------------------------------------------------------
#
# Même geste que pour les affiches des murs, et même raison. Neuf planches de
# `legacy/games/` que le build COPIAIT étaient des oeuvres de tiers. Ouvertes
# une par une, agrandies au plus proche sur un damier pour voir l'alpha :
#
#   1_pacman/enemy.png    les huit FANTOMES de PAC-MAN — silhouette a jupe
#                         ondulee, yeux a pupille decalee        Bandai Namco
#   1_pacman/pacman.png   les quatre PAC-MAN, disque et part de
#                         camembert                              Bandai Namco
#   3_flappy_bird/birds.png        l'OISEAU de Flappy Bird, trois teintes et
#                                  trois battements
#   3_flappy_bird/pipes.png        ses TUYAUX a embouchure
#   3_flappy_bird/backgrounds.png  son CIEL jour/nuit, sa ville, sa haie
#   3_flappy_bird/sol.png          son SOL raye
#   3_flappy_bird/medals.png       ses MEDAILLES, l'oiseau grave dessus
#   3_flappy_bird/scoreBoard.png   son TABLEAU de fin de partie
#   3_flappy_bird/high_score.png   une image de banque d'images AVEC LE
#                                  FILIGRANE « ©123RF » ENCORE DESSUS
#
# Les trois dernieres n'etaient meme pas EMPLOYEES par le portage : le glob
# `*.png` les emportait, et elles partaient dans le paquet. C'est exactement le
# cas des affiches — ce qui compte n'est pas ce que le jeu affiche, c'est ce que
# le build COPIE.
#
# La ligne est la meme que pour les affiches : une MECANIQUE ne s'approprie pas
# — manger des pastilles en fuyant quatre poursuivants, franchir des ouvertures
# en battant des ailes — un NOM et un PERSONNAGE, si. Les regles de `games/`
# n'ont pas bouge d'une ligne ; les figures, oui.
#
# `spriteart` les redessine AUX MEMES COTES (17 x 12 pour le mobile de Flappy,
# 26 x 160 pour un obstacle, 144 x 256 pour un ciel) : `games/flappy/flappy.c`
# decoupe ses atlas au pixel pres, et il n'a donc pas une ligne a changer. Un
# remplacement d'art qui obligerait a rouvrir la logique du jeu est un
# remplacement qu'on ne referait pas.
#
# Les fichiers restent dans `legacy/`, qui est l'archive de 2020 et n'est pas
# distribuee. Ce qui change ici, c'est ce que le build COPIE.
set(SPRITE_ART "")
foreach(spec "dedale:heros" "dedale:creatures"
             "envol:cerf" "envol:pylones" "envol:passerelle" "envol:ciels")
    string(REPLACE ":" ";" _p "${spec}")
    list(GET _p 0 _jeu)
    list(GET _p 1 _nom)
    add_custom_command(
        OUTPUT  "${GEN}/games/${_jeu}/${_nom}.png"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/${_jeu}"
        COMMAND spriteart "--planche=${_jeu}_${_nom}" "${GEN}/games/${_jeu}/${_nom}.png"
        DEPENDS spriteart
        COMMENT "Planche de sprites : ${_jeu}/${_nom}.png"
        VERBATIM
    )
    list(APPEND SPRITE_ART "${GEN}/games/${_jeu}/${_nom}.png")
endforeach()

# Le jeu de labyrinthe n'a plus AUCUNE planche copiee : ses deux images sont
# dessinees, et il n'y a rien d'autre a prendre dans `1_pacman/`. Le repertoire
# est cree par les commandes ci-dessus ; le jalon ne sert qu'a le rattacher a la
# cible d'assets.
add_custom_command(
    OUTPUT "${GEN}/games/dedale/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/dedale"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/dedale/.stamp"
    DEPENDS "${GEN}/games/dedale/heros.png" "${GEN}/games/dedale/creatures.png"
    COMMENT "Planches dessinees de Dedale"
    VERBATIM
)

add_custom_command(
    OUTPUT "${GEN}/games/shooter/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/shooter"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/4_shooter/Textures/background.png"
            "${GEN}/games/shooter/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/shooter/.stamp"
    DEPENDS "${SRC_LEGACY}/games/4_shooter/Textures/background.png"
    COMMENT "Copie du fond de Shooter"
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

# ---------------------------------------------------------------------------
# Le PERSONNAGE, copie a cote du jeu
# ---------------------------------------------------------------------------
# Les autres modeles ne sont PAS copies : roomgen les lit dans l'arbre source et
# cuit leur geometrie dans `salle.gltf`. Le personnage, lui, est lu A
# L'EXECUTION — son squelette s'anime, on ne peut pas le cuire — il doit donc
# se trouver a cote du jeu comme n'importe quelle donnee.
add_custom_command(
    OUTPUT  "${GEN}/models/personnage/personnage.glb"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/models/personnage"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/personnage/personnage.glb"
            "${GEN}/models/personnage/personnage.glb"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/models/personnage/personnage.glb"
    COMMENT "Personnage : maillage pese, squelette et animation"
    VERBATIM
)

# ---------------------------------------------------------------------------
# LES GRAVATS : ce qu'un arbre de build GARDE d'une image retirée
# ---------------------------------------------------------------------------
#
# Retirer une image d'une liste de copie ne la retire PAS de l'arbre de build :
# `copy_if_different` copie, il n'efface pas. Un arbre qui a déjà construit la
# version d'avant garde donc `games/pacman/enemy.png`, `games/flappy/birds.png`,
# `materials/poster_7_n.nstex`… et `room/CMakeLists.txt` installe
# `${GEN}/games`, `${GEN}/materials` et `${GEN}/scene` EN BLOC. Le paquet
# emporterait ce qu'on vient de sortir, sans que rien ne le signale — et le
# contrôle qu'on croit avoir passé, on ne l'a pas passé.
#
# Vérifié sur cet arbre-ci avant d'écrire ces lignes : il contenait encore les
# seize cartes dérivées des huit affiches retirées il y a deux versions.
#
# Le jalon dépend de CE FICHIER : la purge se rejoue exactement quand la liste
# change, c'est-à-dire quand on retire quelque chose. Un `add_custom_target`
# tournerait à chaque build et ne dirait jamais « rien à faire ».
# Les six fichiers que le glob de Snake copiait sans que rien ne les lise.
set(ASSETS_RETIRES
    "games/snake/chiffre.png" "games/snake/appear.wav" "games/snake/disapear.wav"
    "games/snake/explo.wav" "games/snake/rainbow.wav" "games/snake/slow.wav"
    # Les personnages de PAC-MAN et le repertoire de son ancien identifiant.
    "games/pacman"
    # L'ancien identifiant du jeu d'empilement.
    "games/tetris"
    # Les planches de Flappy Bird, et l'ancien identifiant du jeu qui les
    # employait. Le repertoire entier part : le jeu s'appelle « envol », et rien
    # de ce que `games/flappy/` contenait n'est encore lu.
    "games/flappy"
    # Les enseignes, ecrans et captures d'attract des TROIS jeux debaptises.
    "scene/textures/flappy_easy_font.jpg" "scene/textures/flappy_hard_font.jpg"
    "sideart/marquee_pacman.png" "sideart/marquee_tetris.png" "sideart/marquee_flappy.png"
    "sideart/ecran_pacman.png"
    # Le tapis du hall, et la carte de-cuite qu'il avait produite.
    "scene/textures/floor.jpg" "materials/floor_c.png"
    "scene/textures/marquee_pacman.png" "scene/textures/marquee_tetris.png"
    "scene/textures/marquee_flappy.png" "scene/textures/ecran_pacman.png"
    "sideart/ecran_flappy_facile.png" "sideart/ecran_flappy_dur.png"
    "scene/textures/ecran_flappy_facile.png" "scene/textures/ecran_flappy_dur.png"
)
# Les cartes DERIVEES. Une normale deduite d'une affiche sous droit reste une
# oeuvre derivee de cette affiche : elle sort avec elle. Les quatre suffixes
# sont listes parce que `NINETEEN_BC_MAPS` peut avoir change depuis le build qui
# les a produites — on efface les deux formes, pas seulement celle du jour.
# Les DIX-HUIT images que `salle.room.json` declare retirees laissent le meme
# sillage : leur copie dans `scene/textures/` et leurs cartes dans `materials/`.
# On les prend de la meme source que l'exclusion de copie — les recopier ici
# serait une seconde liste, donc une liste qui derive. Deux d'entre elles sont
# les atlas de caisson qui portent l'oiseau de Flappy Bird et le mot TETRIS.
set(_retires_bases "")
foreach(_f IN LISTS RETIRED_TEXTURES)
    list(APPEND ASSETS_RETIRES "scene/textures/${_f}")
    get_filename_component(_b "${_f}" NAME_WE)
    list(APPEND _retires_bases "${_b}")
    list(APPEND ASSETS_RETIRES "materials/${_b}_c.png")
endforeach()
foreach(_base poster_1 poster_2 poster_3 poster_4 poster_5 poster_6 poster_7 poster_8
              flappy_easy_font flappy_hard_font
              marquee_pacman marquee_tetris marquee_flappy ecran_pacman
              ecran_flappy_facile ecran_flappy_dur floor
              ${_retires_bases})
    list(APPEND ASSETS_RETIRES "materials/${_base}_n.nstex" "materials/${_base}_orm.nstex"
                               "materials/${_base}_n.png"   "materials/${_base}_orm.png")
endforeach()

set(PURGE_COMMANDS "")
foreach(_r IN LISTS ASSETS_RETIRES)
    list(APPEND PURGE_COMMANDS COMMAND ${CMAKE_COMMAND} -E rm -rf "${GEN}/${_r}")
endforeach()

add_custom_command(
    OUTPUT  "${GEN}/.retires.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}"
    ${PURGE_COMMANDS}
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/.retires.stamp"
    DEPENDS "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Purge des assets retires (oeuvres tierces) de l'arbre de build"
    VERBATIM
)

add_custom_target(nineteen_assets ALL
    DEPENDS
        "${GEN}/.retires.stamp"
        "${GEN}/models/personnage/personnage.glb"
        "${GEN}/scene/salle.gltf"
        "${GEN}/scene/salle.nsbvh"
        "${GEN}/scene/salle.lights.json"
        "${GEN}/scene/salle.scene.json"
        "${GEN}/sounds/.stamp"
        "${GEN}/games/envol/.stamp"
        "${GEN}/games/snake/.stamp"
        "${GEN}/games/demineur/.stamp"
        "${GEN}/games/aplomb/.stamp"
        "${GEN}/games/asteroid/.stamp"
        "${GEN}/games/dedale/.stamp"
        "${GEN}/games/shooter/.stamp"
        "${GEN}/scene/cabinets.json"
        "${GEN}/scene/textures/.stamp"
        ${SPRITE_ART}
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
