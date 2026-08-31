# signature.cmake — « signé » ou « non signé », décidé À L'EMPAQUETAGE et écrit
# dans le paquet.
#
# POURQUOI C'EST UN CROCHET CPACK ET NON UNE ÉTAPE DU WORKFLOW
# -----------------------------------------------------------
# La signature doit s'appliquer à `Nineteen.app` PENDANT que CPack l'a mise en
# scène, avant que `hdiutil` ne la scelle dans l'image : après, le .dmg est en
# lecture seule et il est trop tard. Le seul point d'accroche à cet instant-là
# est `CPACK_PRE_BUILD_SCRIPTS`. En faire une étape de `release.yml` obligerait à
# refaire le .dmg à la main hors de CPack, et surtout la commande écrite partout
# ailleurs — `cpack --config build/macos-universal/CPackConfig.cmake` — ne
# produirait plus le même paquet que la CI. Ici elle le produit.
#
# CPack appelle CE FICHIER DEUX FOIS, et c'est voulu :
#   1. en PRE  — la mise en scène existe, le .dmg pas encore : on signe le
#                bundle et on y dépose le fichier à lire ;
#   2. en POST — le .dmg existe et son chemin est dans `CPACK_PACKAGE_FILES` :
#                on notarise, on agrafe, et on écrit le manifeste.
# `CPACK_PACKAGE_FILES` n'est défini qu'au second appel : c'est lui qui distingue
# les deux passes, sans second fichier ni variable à tenir d'accord.
#
# LES DEUX CHEMINS, ET CE QUI LES DÉCIDE
# --------------------------------------
# `NINETEEN_SIGN_APPLE_IDENTITY` — et elle seule. Posée, on signe avec elle ;
# vide ou absente, on signe AD HOC et le paquet part non signé EN LE DISANT.
# Aucun des deux chemins n'échoue faute de secret : c'est la demande du
# propriétaire du dépôt, et un workflow qui casse parce qu'un secret manque est
# exactement ce qu'il ne faut pas livrer.
#
# Les noms de ces variables et ce qu'elles valent sont dans `.env.example`,
# section « Signature des paquets ». Ils ne sont pas répétés ici.

if(NOT APPLE)
    return()
endif()

# ---------------------------------------------------------------------------
# Ce que l'environnement dit
# ---------------------------------------------------------------------------
set(_identite "$ENV{NINETEEN_SIGN_APPLE_IDENTITY}")
set(_notaire_id "$ENV{NINETEEN_NOTARIZE_APPLE_ID}")
set(_notaire_equipe "$ENV{NINETEEN_NOTARIZE_TEAM_ID}")
set(_notaire_mdp "$ENV{NINETEEN_NOTARIZE_PASSWORD}")

if(_identite STREQUAL "")
    set(_signe FALSE)
else()
    set(_signe TRUE)
endif()

if(_signe AND NOT _notaire_id STREQUAL ""
          AND NOT _notaire_equipe STREQUAL ""
          AND NOT _notaire_mdp STREQUAL "")
    set(_notarise TRUE)
else()
    set(_notarise FALSE)
endif()

# LES DEUX DEMI-CONFIGURATIONS, QUI SONT DES FAUTES DE FRAPPE ET NON DES CHOIX.
#
# On AVERTIT sans échouer. Manquer un secret est le cas nominal et ne doit rien
# casser ; en poser la moitié est autre chose — quelqu'un a voulu signer et
# repartira avec un paquet ad hoc sans savoir pourquoi. Le silence serait ici la
# pire des deux réponses, et l'échec la deuxième pire : il ne livrerait aucun
# paquet là où il en manque un attribut.
if(NOT _signe AND NOT "$ENV{NINETEEN_SIGN_APPLE_P12}" STREQUAL "")
    message(WARNING
        "signature : un certificat est fourni (NINETEEN_SIGN_APPLE_P12) mais "
        "NINETEEN_SIGN_APPLE_IDENTITY est vide. C'est l'identité qui décide, et "
        "le paquet part donc AD HOC. La poser : le nom exact que rend "
        "`security find-identity -v -p codesigning`.")
endif()
if(_signe AND NOT _notarise)
    message(WARNING
        "signature : identité posée, notarisation INCOMPLÈTE — il faut les trois "
        "de NINETEEN_NOTARIZE_APPLE_ID, _TEAM_ID et _PASSWORD. Signer sans "
        "notariser ne retire PAS l'avertissement de macOS : le joueur verra le "
        "même refus qu'avec un paquet non signé.")
endif()

# ---------------------------------------------------------------------------
# PASSE 1 — le bundle, avant que le .dmg ne se referme dessus
# ---------------------------------------------------------------------------
if(NOT DEFINED CPACK_PACKAGE_FILES)
    set(_app "${CPACK_TEMPORARY_DIRECTORY}/Nineteen.app")
    if(NOT IS_DIRECTORY "${_app}")
        message(FATAL_ERROR
            "signature : « ${_app} » n'existe pas. La mise en scène de CPack a "
            "changé de forme ; sans elle rien n'est signé et le .dmg partirait "
            "muet.")
    endif()

    if(_signe)
        # `--options runtime` : le « hardened runtime », que la notarisation
        # EXIGE. Le poser seulement le jour de la notarisation serait le poser
        # trop tard — c'est un attribut de la signature, pas du .dmg.
        # `--timestamp` : sans horodatage signé, la signature meurt avec le
        # certificat au lieu de survivre à son expiration.
        message(STATUS "signature : identité « ${_identite} »")
        execute_process(
            COMMAND codesign --force --options runtime --timestamp
                             --sign "${_identite}" "${_app}"
            RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR
                "signature : codesign a échoué (${_r}) avec une identité "
                "POSÉE. On n'enchaîne pas sur l'ad hoc : quelqu'un a demandé "
                "une signature et repartir non signé en silence lui livrerait "
                "le contraire de ce qu'il a demandé.")
        endif()
    else()
        # AD HOC — ET CE N'EST PAS UN CONTOURNEMENT DE GATEKEEPER.
        #
        # Ce que ça règle : sur Apple Silicon, un Mach-O arm64 SANS signature
        # du tout n'est pas chargé — le noyau le tue. Mesuré sur cette machine
        # (Darwin 27, arm64), même arbre, quarantaine retirée :
        #   `codesign --remove-signature` puis lancement -> tué, code 137
        #   signature ad hoc         puis lancement -> code 0, l'aide s'affiche
        # L'éditeur de liens en pose déjà une automatiquement, mais elle ne
        # couvre QUE le Mach-O : `codesign -dv` disait alors « Info.plist=not
        # bound, Sealed Resources=none », et `syspolicy_check distribution` —
        # l'outil d'Apple — rendait « File is not signed at all » en Fatal. Le
        # `codesign --sign -` ci-dessous scelle le bundle ENTIER : Info.plist
        # lié, 333 fichiers scellés, et cette erreur fatale disparaît.
        #
        # CE QUE ÇA NE RÈGLE PAS, ET IL FAUT LE LIRE : Gatekeeper refuse
        # toujours. Mesuré sur la même machine, application ad hoc + attribut
        # `com.apple.quarantine` posé comme le pose Safari :
        #   `spctl -a -vv` -> rejected
        #   lancement      -> tué, code 137
        # Le joueur voit « Apple could not verify "Nineteen" is free of malware
        # ». SEULES la signature Developer ID ET la notarisation retirent ce
        # message, et aucune des deux n'est délivrée par un compte développeur
        # gratuit — vérifié ici en signant avec l'identité « Apple Development »
        # de ce Mac : `spctl -a -vv` a répondu « rejected » quand même.
        #
        # D'où le fichier écrit juste en dessous : tant que ce chemin est le
        # chemin principal, le geste doit voyager DANS le .dmg.
        message(STATUS "signature : AD HOC — aucune identité dans l'environnement")
        execute_process(
            COMMAND codesign --force --sign - "${_app}"
            RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR
                "signature : l'ad hoc a échoué (${_r}). Sans elle le binaire "
                "arm64 ne se charge pas du tout, quarantaine ou pas.")
        endif()
    endif()

    # `--verify --strict` plutôt que la confiance : `codesign` rend 0 sur des
    # bundles que le noyau refuse ensuite, et l'écart ne se verrait qu'au
    # lancement chez le joueur.
    execute_process(
        COMMAND codesign --verify --strict --verbose=2 "${_app}"
        RESULT_VARIABLE _r ERROR_VARIABLE _sortie)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "signature : le bundle ne se vérifie pas — ${_sortie}")
    endif()
    message(STATUS "signature : ${_sortie}")

    # -----------------------------------------------------------------------
    # Le fichier que le joueur voit en ouvrant le .dmg
    # -----------------------------------------------------------------------
    # NOMMÉ POUR ÊTRE PREMIER. `A-LIRE-...` passe avant `Applications`,
    # `JOUER.md`, `LICENSES.md` et `Nineteen.app` dans l'ordre alphabétique,
    # donc en tête de la fenêtre du volume en vue par liste.
    #
    # PAS D'IMAGE DE FOND, et c'est une décision, pas un oubli :
    # `CPACK_DMG_BACKGROUND_IMAGE` fait piloter le Finder par AppleScript pour
    # écrire un `.DS_Store`. Sur un runner GitHub il n'y a pas de session
    # graphique pour répondre, et l'étape se bloquerait ou échouerait — c'est-
    # à-dire casserait la release, ce que ce travail existe justement pour
    # éviter. Le nom du fichier est le levier qui ne dépend de personne.
    #
    # Son texte suit le chemin RÉELLEMENT pris, plutôt que d'annoncer d'avance
    # ce que le paquet sera : un lisez-moi qui parle de quarantaine dans un
    # paquet notarisé serait faux le jour où le certificat arrive.
    #
    # LA CONDITION EST `_notarise`, PAS `_signe`, et l'écart n'est pas un
    # détail : signé sans notarisation, macOS refuse EXACTEMENT COMME s'il
    # n'était pas signé du tout. Mesuré sur cette machine avec une identité
    # « Apple Development » réelle, chaîne complète jusqu'à Apple Root CA :
    # `spctl -a -vv` a répondu « rejected », et le lancement sous quarantaine
    # a été tué (code 137). Un lisez-moi rassurant dans ce paquet-là enverrait
    # le joueur au mur sans même la ligne qui l'en sort.
    if(_notarise)
        file(WRITE "${CPACK_TEMPORARY_DIRECTORY}/A-LIRE-AVANT-D-OUVRIR.txt"
"Nineteen ${CPACK_PACKAGE_VERSION}

Glisser Nineteen sur le raccourci Applications, puis ouvrir normalement.

Ce paquet est signe et notarise par Apple. Si macOS affiche malgre tout un
avertissement, c'est qu'il a ete modifie apres sa fabrication : ne pas
l'ouvrir, et le retelecharger depuis
https://github.com/GriiseMine/NineTeen/releases

Ce qu'il faut pour jouer : un Mac Apple Silicon ou Intel sous macOS 11 ou
plus recent. Le jeu n'ouvre aucune connexion et ne demande aucun compte.
Le reste est dans JOUER.md, a cote.
")
    else()
        # SANS ACCENTS. Ce fichier s'ouvre chez le joueur dans TextEdit, avec
        # l'encodage que TextEdit devine ; l'ASCII est le seul jeu ou la devinette
        # ne peut pas se tromper. C'est la meme raison que la licence NSIS
        # (packaging/CPackNineteen.cmake).
        #
        # Le message d'Apple est recopie EN ANGLAIS et mot pour mot : c'est
        # celui que le proprietaire du depot a recu sur sa machine, et le joueur
        # doit reconnaitre sa fenetre dans ce texte.
        #
        # UNE SEULE marche a suivre, celle qui a ete MESUREE ici (lancement tue
        # avec l'attribut, code 0 apres l'avoir retire). Le bouton « Ouvrir quand
        # meme » de Reglages Systeme existe et fait la meme chose, mais il n'a
        # pas pu etre essaye depuis un script : on ne l'ecrit donc pas dans le
        # paquet, ou une marche a suivre fausse coute plus cher qu'une marche a
        # suivre austere.
        file(WRITE "${CPACK_TEMPORARY_DIRECTORY}/A-LIRE-AVANT-D-OUVRIR.txt"
"Nineteen ${CPACK_PACKAGE_VERSION}

CE PAQUET N'EST PAS SIGNE PAR APPLE, ET MACOS VA REFUSER DE L'OUVRIR.

Au premier lancement vous verrez :

    Apple could not verify \"Nineteen\" is free of malware that may harm
    your Mac or compromise your privacy.

Le jeu n'est ni casse ni dangereux. Le certificat qui fait disparaitre ce
message n'est delivre qu'aux comptes developpeur Apple payants, et ce
projet n'en a pas encore. C'est ecrit la, dans le paquet, plutot que dans
une documentation que personne n'ouvre.

CE QU'IL FAUT FAIRE -- une seule fois
-------------------------------------

  1. Glisser Nineteen sur le raccourci Applications, comme d'habitude.

  2. Ouvrir le Terminal (Applications > Utilitaires > Terminal).

  3. Coller cette ligne exactement, puis Entree :

        xattr -dr com.apple.quarantine /Applications/Nineteen.app

     Elle ne demande pas de mot de passe et ne rend rien : c'est normal.

  4. Ouvrir Nineteen. Il demarre, et les fois suivantes aussi.

Si vous avez copie le jeu ailleurs que dans Applications, remplacez le
chemin de la ligne 3 par le sien.

CE QUE CETTE LIGNE FAIT
-----------------------

Elle retire l'etiquette \"telecharge depuis Internet\" que le navigateur a
posee sur le fichier. C'est cette etiquette, et rien d'autre, qui declenche
le refus. Elle ne desactive aucune protection du systeme et ne vaut que
pour ce dossier-la.

Ce qu'il faut pour jouer : un Mac Apple Silicon ou Intel sous macOS 11 ou
plus recent. Le jeu n'ouvre aucune connexion et ne demande aucun compte.
Le reste est dans JOUER.md, a cote.
")
    endif()

    return()
endif()

# ---------------------------------------------------------------------------
# PASSE 2 — le .dmg existe
# ---------------------------------------------------------------------------
# `CPACK_PACKAGE_FILES` est une liste : DragNDrop n'en produit qu'un, mais on
# ne l'écrit pas en dur pour autant.
foreach(_dmg IN LISTS CPACK_PACKAGE_FILES)

    if(_signe)
        # Le .dmg PORTE la signature en plus du bundle qu'il contient : c'est
        # l'image que l'utilisateur télécharge, et c'est elle que `stapler`
        # agrafe. Signer l'un sans l'autre laisserait la moitié du chemin nue.
        execute_process(
            COMMAND codesign --force --timestamp --sign "${_identite}" "${_dmg}"
            RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "signature : codesign sur le .dmg a échoué (${_r})")
        endif()
    endif()

    if(_notarise)
        # `--wait` : sans lui `notarytool` rend la main sur un identifiant de
        # soumission et l'agrafage juste en dessous échouerait sur un ticket qui
        # n'existe pas encore. L'attente est de plusieurs minutes chez Apple.
        message(STATUS "signature : notarisation en cours (équipe ${_notaire_equipe})")
        execute_process(
            COMMAND xcrun notarytool submit "${_dmg}"
                    --apple-id "${_notaire_id}"
                    --team-id "${_notaire_equipe}"
                    --password "${_notaire_mdp}"
                    --wait
            RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR
                "signature : la notarisation a échoué (${_r}). "
                "`xcrun notarytool log <id>` dit ce qu'Apple a refusé.")
        endif()
        # AGRAFER, sinon la notarisation ne sert que tant qu'Apple répond : un
        # Mac hors ligne redemande le ticket au réseau et retombe sur le refus.
        execute_process(
            COMMAND xcrun stapler staple "${_dmg}"
            RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "signature : l'agrafage a échoué (${_r})")
        endif()
    endif()

    # -----------------------------------------------------------------------
    # Le manifeste
    # -----------------------------------------------------------------------
    # POURQUOI UN MANIFESTE ET PAS UN SUFFIXE DANS LE NOM DE FICHIER.
    #
    # Un `-non-signe` dans le nom serait plus voyant, et c'est justement le
    # problème : il devrait DISPARAÎTRE le jour où le certificat arrive. Or le
    # site bâtit ses liens de téléchargement à partir du seul numéro de version
    # (`server/internal/web/assets/app.js`), et ce dépôt a déjà publié trois
    # boutons qui rendaient 404 pour avoir supposé un nom. Un nom qui change
    # avec un secret casserait les mêmes liens, mais seulement le jour de la
    # bascule, et sur la release.
    #
    # Le nom reste donc STABLE, et l'information voyage à trois endroits que
    # l'on ne peut pas confondre :
    #   - ce manifeste, écrit dans LES DEUX cas, jamais absent ;
    #   - `A-LIRE-AVANT-D-OUVRIR.txt`, DANS le .dmg, que le joueur ouvre ;
    #   - le paquet lui-même : `codesign -dv` rend « Signature=adhoc » ou
    #     nomme l'autorité. Celui-là ne peut pas être séparé du fichier.
    if(_signe AND _notarise)
        set(_etat "SIGNE ET NOTARISE")
        set(_geste "Rien a faire : ouvrir et glisser dans Applications.")
    elseif(_signe)
        set(_etat "SIGNE, NON NOTARISE")
        set(_geste
"Gatekeeper refusera quand meme : la notarisation manque.
  xattr -dr com.apple.quarantine /Applications/Nineteen.app")
    else()
        set(_etat "NON SIGNE (ad hoc)")
        set(_geste
"macOS affiche « Apple could not verify \"Nineteen\" is free of malware ».
  Une fois, apres avoir glisse le jeu dans Applications :
      xattr -dr com.apple.quarantine /Applications/Nineteen.app")
    endif()

    file(SHA256 "${_dmg}" _somme)
    get_filename_component(_nom "${_dmg}" NAME)

    # À CÔTÉ DU .DMG LIVRÉ, et non à côté de celui que CPack vient de graver.
    # Les deux ne sont PAS au même endroit : `CPACK_PACKAGE_FILES` désigne
    # l'image dans `_CPack_Packages/Darwin/DragNDrop/`, que CPack recopie
    # ENSUITE vers le répertoire de `-B`. Le manifeste écrit à côté de la
    # première finissait dans un répertoire de travail que personne ne publie —
    # mesuré, il y était. La somme, elle, est juste : c'est le même fichier.
    if(CPACK_PACKAGE_DIRECTORY)
        set(_ou "${CPACK_PACKAGE_DIRECTORY}")
    else()
        get_filename_component(_ou "${_dmg}" DIRECTORY)
    endif()

    # Les sommes SHA-256 sont promises depuis longtemps sur la page de
    # telechargement du site (« Sommes SHA-256 publiees avec chaque version »)
    # et n'etaient produites nulle part. Elles le sont ici.
    file(WRITE "${_ou}/SIGNATURE-macos.txt"
"Nineteen ${CPACK_PACKAGE_VERSION} -- paquet macOS

Etat : ${_etat}

${_nom}
  sha256 = ${_somme}

Ce que le joueur doit faire :
  ${_geste}

Verifier soi-meme, sans faire confiance a ce fichier :
  shasum -a 256 ${_nom}
  hdiutil attach ${_nom}
  codesign -dv --verbose=4 /Volumes/*/Nineteen.app
  spctl -a -vv /Volumes/*/Nineteen.app
")
    message(STATUS "signature : ${_etat} — manifeste dans ${_ou}/SIGNATURE-macos.txt")
endforeach()
