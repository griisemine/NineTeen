# CPackNineteen.cmake — ce qui change d'un générateur à l'autre.
#
# CPack relit ce fichier UNE FOIS PAR GÉNÉRATEUR, avec `CPACK_GENERATOR` déjà
# positionné. C'est le seul endroit où l'on peut écrire « le .deb sous /usr, le
# .tar.gz à plat » sans que l'un contamine l'autre : posées dans le
# CMakeLists.txt, ces variables seraient globales et le dernier `set()` gagnerait
# pour les deux.
#
# Il est désigné par `CPACK_PROJECT_CONFIG_FILE` (CMakeLists.txt, bloc
# « Empaquetage »). Les variables lues ici — `CPACK_PACKAGE_VERSION`,
# `CPACK_NINETEEN_ARCH`, `CPACK_NINETEEN_SOURCE_DIR` — viennent du CPackConfig
# généré à la configuration.

# ---------------------------------------------------------------------------
# macOS — le .dmg
# ---------------------------------------------------------------------------
if(CPACK_GENERATOR STREQUAL "DragNDrop")
    set(CPACK_PACKAGE_FILE_NAME "Nineteen-${CPACK_PACKAGE_VERSION}-macOS-universal")

    # PAS de répertoire racine. Ailleurs il évite de déverser `bin/` dans le
    # répertoire courant au déballage ; ici il enterrerait `Nineteen.app` d'un
    # cran dans la fenêtre du .dmg, où le geste attendu est de glisser l'icône
    # sur le raccourci /Applications qui est juste à côté.
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)

    set(CPACK_DMG_VOLUME_NAME "Nineteen ${CPACK_PACKAGE_VERSION}")

    # UDZO : image compressée en zlib, lisible par tout macOS depuis 10.1.
    # Mesuré sur 17.0.0 : 168,8 Mio d'image (177 001 095 octets) pour 237,1 Mio
    # de `Nineteen.app`, dont 227,6 Mio d'assets. Le gain est modeste parce que
    # les cartes de normales et d'ORM sont DÉJÀ en blocs compressés (BC5/BC1) :
    # on ne compresse une deuxième fois que les glTF et les WAV.
    set(CPACK_DMG_FORMAT "UDZO")

    # Le raccourci vers /Applications est créé par défaut
    # (`CPACK_DMG_DISABLE_APPLICATIONS_SYMLINK` existe pour le retirer, et on ne
    # le pose pas) : c'est LUI qui fait de ce .dmg un « glisser-déposer » et non
    # un dossier à recopier à la main.

# ---------------------------------------------------------------------------
# Linux — l'archive
# ---------------------------------------------------------------------------
elseif(CPACK_GENERATOR STREQUAL "TGZ")
    set(CPACK_PACKAGE_FILE_NAME
        "nineteen-${CPACK_PACKAGE_VERSION}-linux-${CPACK_NINETEEN_ARCH}")

    # Un répertoire racine : déballer chez soi ne doit pas déverser `bin/`,
    # `lib/` et `share/` dans le répertoire courant.
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)

    # À plat, sans `/usr` : cette archive se déballe où l'on veut, et le lien
    # `bin/nineteen -> ../lib/nineteen/nineteen` est relatif pour cette raison.
    set(CPACK_PACKAGING_INSTALL_PREFIX "")

# ---------------------------------------------------------------------------
# Linux — le paquet Debian
# ---------------------------------------------------------------------------
elseif(CPACK_GENERATOR STREQUAL "DEB")
    # `DEB-DEFAULT` donne `nineteen_17.0.0_amd64.deb` : la convention Debian,
    # avec l'architecture que `dpkg --print-architecture` rapporte et non celle
    # que le noyau annonce. C'est le nom que les outils attendent ; en imposer un
    # autre casse la complétion d'`apt` et la lecture d'un dépôt.
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/GriiseMine/NineTeen")

    # LES DÉPENDANCES SONT MESURÉES, PAS DEVINÉES.
    #
    # `dpkg-shlibdeps` lit les `NEEDED` de l'ELF livré et remonte à la version
    # exacte de chaque paquet qui les fournit. Une liste écrite à la main serait
    # fausse le jour où l'on lie une bibliothèque de plus, et fausse en silence :
    # le .deb s'installerait et le jeu s'arrêterait au premier `dlopen`.
    #
    # Ce que ça donne ici, mesuré dans un conteneur ubuntu:22.04 :
    #   Depends: libc6 (>= 2.34)
    # et rien d'autre. SDL3 est lié STATIQUEMENT (cmake/Dependencies.cmake), et
    # ce qu'il utilise du système — X11, Wayland, ALSA, PulseAudio, Vulkan — il
    # l'ouvre par `dlopen` au démarrage. Ces bibliothèques-là n'apparaissent donc
    # pas dans les `NEEDED` et NE PEUVENT PAS être déduites : c'est une limite
    # réelle de la méthode, pas un oubli. Le jeu les cherche à l'exécution et dit
    # laquelle manque, plutôt que de refuser de s'installer.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    # Recommends plutôt que Depends, pour la même raison : ce sont les paquets
    # que SDL ouvrira, et un poste de jeu les a déjà. En faire des Depends
    # tirerait tout Wayland sur une machine qui tourne sous X11, et l'inverse.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
        "libvulkan1, libx11-6, libxext6, libwayland-client0, libasound2 | libasound2t64")

    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"La salle d'arcade : huit jeux, dix-neuf bornes
 Une salle d'arcade en trois dimensions que l'on parcourt a pied, et dont les
 bornes se jouent pour de vrai : flappy, snake, tetris, demineur, asteroid,
 pacman, piano, shooter. Rendu Vulkan, moteur maison, aucun compte ni serveur
 requis.")

# ---------------------------------------------------------------------------
# Windows — l'installateur NSIS
# ---------------------------------------------------------------------------
#
# RIEN DE CE BLOC N'A ÉTÉ EXÉCUTÉ SUR UNE MACHINE WINDOWS. Ce qui a été vérifié
# depuis le Mac : que `cpack -G NSIS` accepte la configuration et écrit un
# `CPackConfig.cmake` où chacune de ces variables apparaît avec la bonne valeur.
# Le `.exe` sort des runners `windows-2022` de GitHub (NSIS 3.10 préinstallé).
elseif(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_PACKAGE_FILE_NAME "Nineteen-${CPACK_PACKAGE_VERSION}-windows-x64")
    set(CPACK_NSIS_PACKAGE_NAME "Nineteen ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "Nineteen")

    # SANS DROITS ADMINISTRATEUR, et c'est ce qui a décidé du reste.
    #
    # Le modèle NSIS de CPack pose `RequestExecutionLevel admin` en dur
    # (Modules/Internal/CPack/NSIS.template.in:41). `CPACK_NSIS_DEFINES` est
    # inséré DEUX LIGNES plus bas (ligne 43), et NSIS garde la dernière valeur
    # d'un attribut de script : le redéclarer ici est le seul moyen de le
    # renverser sans copier tout le modèle.
    #
    # Pourquoi ça compte : un jeu n'a aucune raison de demander l'élévation. Sur
    # un poste d'école ou d'entreprise, la demander c'est ne pas s'installer du
    # tout. `$LOCALAPPDATA\Programs` est inscriptible par l'utilisateur, et le
    # modèle écrit sa clé de désinstallation dans `SHCTX`, qui vaut HKCU tant
    # qu'on n'a pas demandé `SetShellVarContext all` — donc le désinstalleur
    # apparaît bien dans « Applications et fonctionnalités » de CET utilisateur.
    set(CPACK_NSIS_DEFINES "RequestExecutionLevel user")
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\Programs")

    # Le raccourci du menu Démarrer. `nineteen` est le nom du .exe SANS
    # extension, cherché dans `bin\` (CPACK_NSIS_EXECUTABLES_DIRECTORY, défaut).
    set(CPACK_PACKAGE_EXECUTABLES "nineteen;Nineteen")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "nineteen.exe")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\nineteen.exe")

    set(CPACK_NSIS_MUI_ICON "${CPACK_NINETEEN_SOURCE_DIR}/packaging/windows/nineteen.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CPACK_NINETEEN_SOURCE_DIR}/packaging/windows/nineteen.ico")

    # L'installateur lui-même est net sur un écran à forte densité. Sans ça
    # Windows l'agrandit d'un facteur entier et le texte bave.
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)

    # /SOLID lzma : les assets sont 150 Mio de fichiers qui se ressemblent
    # beaucoup ; le mode solide les compresse comme un seul flux.
    set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")

    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/GriiseMine/NineTeen")
    set(CPACK_NSIS_HELP_LINK "https://github.com/GriiseMine/NineTeen")
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")

    # LA PAGE DE LICENCE EST SANS ACCENTS, ET C'EST VOULU.
    #
    # Le modèle NSIS de CPack ne déclare pas `Unicode true` : makensis compile
    # alors un installateur ANSI, qui affiche un fichier UTF-8 en mojibake. On ne
    # peut pas vérifier le rendu depuis ce Mac — il n'y a ni makensis ni wine —
    # donc on ne prend pas le pari : le texte n'emploie que de l'ASCII, où les
    # deux encodages coïncident. Il renvoie à `LICENSES.md`, qui lui est complet
    # et accentué, et qui est installé à côté du jeu.
    set(CPACK_RESOURCE_FILE_LICENSE
        "${CPACK_NINETEEN_SOURCE_DIR}/packaging/windows/LICENCE.txt")
endif()
