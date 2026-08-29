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
    DEPENDS "${SIDEART}"
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
    DEPENDS "${PANELART}"
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
    DEPENDS "${_neon}"
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

set(MARQUEE_shooter  "SHOOTER"  "1.0,0.36,0.14")
set(MARQUEE_demineur "DEMINEUR" "1.0,0.74,0.20")
set(MARQUEE_pacman   "PAC-MAN"  "1.0,0.84,0.16")
set(MARQUEE_piano    "PIANO"    "0.72,0.45,1.0")
foreach(jeu shooter demineur pacman piano)
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
        DEPENDS "${_png}"
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


file(GLOB _room_textures "${SRC_ROOM}/textures/*.jpg" "${SRC_ROOM}/textures/*.png")
file(GLOB _cc0_textures  "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.jpg"
                         "${CMAKE_CURRENT_SOURCE_DIR}/cc0/*.png")
list(LENGTH _room_textures _n_room)
list(LENGTH _cc0_textures  _n_cc0)
# +7 : l'art latéral, la sérigraphie de panneau, les quatre enseignes de borne
# et l'enseigne au néon de la salle, dessinés juste au-dessus.
#
# Ce compte est un INVARIANT vérifié par roomgen, pas une constante décorative :
# ajouter une texture sans le mettre à jour arrête le build avec « 84 pour 83
# présentes ». C'est exactement ce qui vient d'arriver en ajoutant le néon, et
# c'est le comportement voulu — une texture générée que la salle n'emploie pas,
# ou une texture employée qui n'est pas générée, sont deux pannes muettes.
math(EXPR ROOM_TEXTURE_COUNT "${_n_room} + ${_n_cc0} + 7")

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
set(ALBEDO_floor              0.11)   # 0,039
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

# Snake. Même principe, avec une particularité : `config.h` de 2020 emprunte
# DEUX sons à d'autres jeux — l'explosion d'Asteroid pour la bombe, le game over
# de Tetris. Un glob sur son propre répertoire ne les prendrait pas, et le jeu
# tournerait muet sur ses deux moments les plus forts. Ils sont donc copiés
# explicitement, et renommés pour que `snake.c` les cherche à un seul endroit.
file(GLOB SNAKE_ART   "${SRC_LEGACY}/games/2_snake/Textures/*.png")
file(GLOB SNAKE_SOUND "${SRC_LEGACY}/games/2_snake/Sound/*.wav")

add_custom_command(
    OUTPUT "${GEN}/games/snake/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/snake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SNAKE_ART} "${GEN}/games/snake/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SNAKE_SOUND} "${GEN}/games/snake/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/7_asteroid/Sound/explo.wav" "${GEN}/games/snake/explo.wav"
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

add_custom_command(
    OUTPUT "${GEN}/games/tetris/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/tetris"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/5_tetris/Textures/bricks.png"
            "${SRC_LEGACY}/games/5_tetris/Sound/line.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/rotate.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/gameover.wav"
            "${GEN}/games/tetris/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/tetris/.stamp"
    DEPENDS "${SRC_LEGACY}/games/5_tetris/Textures/bricks.png"
            "${SRC_LEGACY}/games/5_tetris/Sound/line.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/rotate.wav"
            "${SRC_LEGACY}/games/5_tetris/Sound/gameover.wav"
    COMMENT "Copie des planches et sons de Tetris"
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

add_custom_command(
    OUTPUT "${GEN}/games/pacman/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN}/games/pacman"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRC_LEGACY}/games/1_pacman/Textures/pacman.png"
            "${SRC_LEGACY}/games/1_pacman/Textures/enemy.png"
            "${GEN}/games/pacman/"
    COMMAND ${CMAKE_COMMAND} -E touch "${GEN}/games/pacman/.stamp"
    DEPENDS "${SRC_LEGACY}/games/1_pacman/Textures/pacman.png"
            "${SRC_LEGACY}/games/1_pacman/Textures/enemy.png"
    COMMENT "Copie des planches de Pac-Man"
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

add_custom_target(nineteen_assets ALL
    DEPENDS
        "${GEN}/models/personnage/personnage.glb"
        "${GEN}/scene/salle.gltf"
        "${GEN}/scene/salle.nsbvh"
        "${GEN}/scene/salle.lights.json"
        "${GEN}/scene/salle.scene.json"
        "${GEN}/sounds/.stamp"
        "${GEN}/games/flappy/.stamp"
        "${GEN}/games/snake/.stamp"
        "${GEN}/games/demineur/.stamp"
        "${GEN}/games/tetris/.stamp"
        "${GEN}/games/asteroid/.stamp"
        "${GEN}/games/pacman/.stamp"
        "${GEN}/games/shooter/.stamp"
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
