#!/bin/sh
#
# paquets.sh — les trois paquets Linux, depuis UNE seule installation.
#
# À lancer DANS le conteneur de packaging/linux/Dockerfile.build :
#
#   docker build -f packaging/linux/Dockerfile.build -t nineteen-build:22.04 packaging/linux
#   docker run --rm -v "$PWD:/src" nineteen-build:22.04 packaging/linux/paquets.sh
#
# Le lancer sur la machine hôte marche aussi, si elle a les mêmes paquets — mais
# alors l'AppImage porte la glibc de CETTE machine, et l'intérêt du conteneur
# était justement d'en fixer la version. Voir le commentaire du Dockerfile.
#
# CE QUI SORT, dans build/linux-x64/paquets/ :
#   nineteen-17.0.0-linux-<arch>.tar.gz   à déballer où l'on veut
#   nineteen_17.0.0_<arch>.deb            apt install ./…deb
#   Nineteen-17.0.0-<arch>.AppImage       chmod +x, puis on lance
#   SIGNATURE-linux.txt                   l'état de signature et les SHA-256
#
# `<arch>` EST CELLE DE LA MACHINE QUI FABRIQUE, jamais celle qu'on espère. Sur
# un runner GitHub c'est `x86_64` / `amd64` ; sur un Mac Apple Silicon, où
# Docker est natif arm64, c'est `aarch64` / `arm64` — et ces paquets-là ne
# s'installent sur aucun PC. Le nom du répertoire, `linux-x64`, dit donc autre
# chose que son contenu selon l'endroit : c'est le nom du préréglage CMake, pas
# une promesse d'architecture.
#
# POURQUOI L'APPIMAGE N'EST PAS UN GÉNÉRATEUR CPACK
# -------------------------------------------------
# Il n'en existe pas dans CPack, et les greffons qui prétendent le contraire
# reconstruisent en fait un AppDir à la main comme ci-dessous. Autant l'écrire :
# un AppDir, c'est `cmake --install` sous `AppDir/usr` plus trois fichiers à la
# racine, et `appimagetool` par-dessus. Vingt lignes qu'on peut lire valent
# mieux qu'une dépendance qu'on ne peut pas déboguer le jour où elle casse.

set -eu

RACINE="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-$RACINE/build/linux-x64}"
# SORTIE est reglable : la composition Docker la pointe sur le repertoire que le
# serveur sert, pour que les paquets fabriques ici deviennent telechargeables
# sans qu'on ait a les recopier. Voir `telechargements/` et docker-compose.yml.
SORTIE="${SORTIE:-$BUILD/paquets}"
APPDIR="$BUILD/AppDir"
VERSION_SOURCE="$(sed -n 's/^ *VERSION *\([0-9][0-9.]*\)$/\1/p' "$RACINE/CMakeLists.txt" | head -1)"

cd "$RACINE"

# --------------------------------------------------------------------------
# 0. Y a-t-il seulement quelque chose a faire ?
# --------------------------------------------------------------------------
# La composition Docker relance ce script a CHAQUE `docker compose up`. Sans
# cette garde, elle recompilerait et recompresserait 500 Mio de paquets a chaque
# demarrage de la pile, pour reproduire a l'octet pres ce qui est deja la.
#
# La garde est volontairement bete : elle regarde si les trois paquets de LA
# VERSION DECLAREE existent deja dans la sortie. Elle ne compare aucune date de
# source, parce qu'une comparaison de dates qui se trompe dans un sens livre un
# paquet perime, et personne ne s'en apercevrait. Changer la version, ou vider
# le repertoire, refabrique.
if [ "${NINETEEN_PAQUETS_SI_ABSENT:-0}" = "1" ] && [ -n "$VERSION_SOURCE" ]; then
    ARCH_ICI="$(uname -m)"
    DEJA=1
    for motif in \
        "$SORTIE/Nineteen-${VERSION_SOURCE}-${ARCH_ICI}.AppImage" \
        "$SORTIE/nineteen-${VERSION_SOURCE}-linux-${ARCH_ICI}.tar.gz" \
        "$SORTIE/SIGNATURE-linux.txt"
    do
        [ -f "$motif" ] || DEJA=0
    done
    if [ "$DEJA" = "1" ]; then
        echo "paquets $VERSION_SOURCE deja presents dans $SORTIE, rien a refaire."
        ls -la "$SORTIE"
        exit 0
    fi
fi

echo "=== Ce qui construit ==="
ldd --version | head -1
cc --version | head -1
cmake --version | head -1
glslangValidator --version 2>/dev/null | head -1 || true
echo

# ---------------------------------------------------------------------------
# 1. Compiler
# ---------------------------------------------------------------------------
# `NINETEEN_SDL3_SRC` permet de réutiliser une copie de SDL3 déjà sur le disque
# — c'est ce qui rend la fabrication hors ligne, et ce qui évite un clone de
# 108 Mio à chaque essai pendant qu'on met ce script au point.
CONFIGURE="-DCMAKE_BUILD_TYPE=Release"
if [ -n "${NINETEEN_SDL3_SRC:-}" ]; then
    CONFIGURE="$CONFIGURE -DFETCHCONTENT_SOURCE_DIR_SDL3=$NINETEEN_SDL3_SRC"
fi

# LE DOMAINE, CUIT DANS LE BINAIRE, ET C'EST ICI QUE LA FENETRE S'OUVRE.
#
# `-DNINETEEN_SERVER_URL=` pose le defaut compile : le joueur qui lance le jeu
# sans rien taper vise DEJA le bon serveur. Sans lui, on livrait un binaire et
# une consigne, « ajoutez --server=http://... », que la moitie des gens ne
# lisent pas et que l'autre moitie tape de travers.
#
# Le moment compte : ce defaut ne peut pas etre pose apres coup sur un paquet
# deja fabrique. C'est pour cela que la composition Docker fabrique le jeu APRES
# qu'on lui a donne son adresse publique, et pas avant.
#
# Le joueur garde le dernier mot : la config, la variable d'environnement et
# `--server=` battent ce defaut, dans cet ordre. Voir engine/net/ns_online.h.
if [ -n "${NINETEEN_SERVER_URL:-}" ]; then
    CONFIGURE="$CONFIGURE -DNINETEEN_SERVER_URL=$NINETEEN_SERVER_URL"
    echo "serveur cuit dans le binaire : $NINETEEN_SERVER_URL"
else
    echo "serveur cuit dans le binaire : aucun (le jeu demarrera hors ligne)"
fi

# shellcheck disable=SC2086
cmake -S "$RACINE" -B "$BUILD" -G Ninja $CONFIGURE

# `--target nineteen` PAR DÉFAUT, et pas la cible complète : les 36 exécutables
# de tests ne partent dans aucun paquet. Mesuré : 106 cibles à construire au
# lieu de 716.
#
# `NINETEEN_TESTS=1` demande l'inverse — tout construire et lancer `ctest` — et
# c'est ce que fait le workflow de release. La raison n'est pas la ceinture et
# les bretelles : la suite tourne alors sur la VIEILLE glibc, celle du paquet,
# et non sur celle du runner. C'est là qu'un défaut de portabilité se voit. Il
# s'en est trouvé un en écrivant ce script : sur cette base 22.04 le projet ne
# compilait pas du tout, la glibc 2.35 cachant `getaddrinfo` et `popen` sous
# `-std=c11` là où celle d'ubuntu-24.04 les expose — CI verte, conteneur rouge,
# même code. Le correctif est dans cmake/CompilerWarnings.cmake, qui explique
# la mécanique.
#
# `SDL_VIDEO_DRIVER=offscreen` : ni écran ni carte graphique ici. Le test
# `render` se déclare ignoré (code 77) s'il ne trouve pas de GPU, ce qui se voit
# dans le rapport au lieu de disparaître.
if [ "${NINETEEN_TESTS:-0}" = "1" ]; then
    cmake --build "$BUILD" -j "$(nproc)"
    SDL_VIDEO_DRIVER=offscreen ctest --test-dir "$BUILD" --output-on-failure
else
    cmake --build "$BUILD" -j "$(nproc)" --target nineteen
fi

# ---------------------------------------------------------------------------
# 2. .tar.gz et .deb — CPack sait les faire
# ---------------------------------------------------------------------------
# ON N'EFFACE QUE CE QU'ON PRODUIT, et cette precision a ete payee deux fois.
#
# `rm -rf "$SORTIE"` echouerait sous la composition Docker, ou la sortie est un
# point de montage : on ne demonte pas un volume avec `rm`. Et `rm -rf
# "$SORTIE"/*` emporterait deux choses qui ne nous appartiennent pas : le
# LISEZ-MOI versionne du repertoire, et surtout le .dmg ou le .exe que
# l'exploitant y a depose a la main. Aucune machine ne fabrique les trois
# plateformes, donc une fabrication Linux qui efface le paquet macOS d'a cote
# est exactement le defaut a ne pas avoir.
mkdir -p "$SORTIE"
rm -rf "$SORTIE"/_CPack_Packages
rm -f "$SORTIE"/*.AppImage "$SORTIE"/*.deb "$SORTIE"/*.rpm "$SORTIE"/*.tar.gz \
      "$SORTIE"/SIGNATURE-linux.txt "$SORTIE"/*.asc
cpack --config "$BUILD/CPackConfig.cmake" -B "$SORTIE"
# CPack laisse son arbre de travail derriere lui. Il ne genait pas quand la
# sortie etait un repertoire de build ; ici c'est un repertoire servi au public.
rm -rf "$SORTIE"/_CPack_Packages

# ---------------------------------------------------------------------------
# 3. L'AppImage — un AppDir, puis appimagetool
# ---------------------------------------------------------------------------
rm -rf "$APPDIR"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

# AppRun est le point d'entrée : c'est lui que le runtime AppImage exécute après
# avoir monté l'image. `readlink -f` parce que l'AppImage se monte sous un
# /tmp/.mount_XXXX différent à chaque lancement — un chemin en dur ne peut pas
# marcher, et un `$0` non résolu casse dès qu'on lance l'AppImage par un lien.
#
# On exécute le VRAI binaire, `usr/lib/nineteen/nineteen`, et non le lien
# `usr/bin/nineteen` : les deux marchent (SDL résout /proc/self/exe, qui suit le
# lien), mais passer par le lien ferait croire que le lien est nécessaire.
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
ICI="$(dirname "$(readlink -f "$0")")"
exec "$ICI/usr/lib/nineteen/nineteen" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# `appimagetool` lit le .desktop et l'icône À LA RACINE de l'AppDir, pas dans
# `usr/share`. Il les y cherche par leur nom, et s'arrête sans rien produire
# s'ils manquent. Ce sont des copies et non des liens : un lien relatif vers
# `usr/share/...` marcherait, mais certains outils qui inspectent une AppImage
# lisent le fichier racine sans suivre les liens.
cp "$APPDIR/usr/share/applications/nineteen.desktop" "$APPDIR/nineteen.desktop"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/nineteen.png" "$APPDIR/nineteen.png"

VERSION="$(sed -n 's/^set(CPACK_PACKAGE_VERSION "\(.*\)")$/\1/p' "$BUILD/CPackConfig.cmake" | head -1)"

# L'architecture est LUE, jamais supposée. Sur un Mac Apple Silicon le conteneur
# est aarch64 ; sur un runner GitHub il est x86_64. Le nom de fichier d'une
# AppImage porte cette architecture par convention, et `appimagetool` refuse de
# travailler si `$ARCH` ne correspond pas à ce qu'il trouve dans l'AppDir.
ARCH="$(uname -m)"
export ARCH
APPIMAGE="$SORTIE/Nineteen-${VERSION}-${ARCH}.AppImage"

# `--appimage-extract-and-run` (via APPIMAGE_EXTRACT_AND_RUN=1, posé par le
# Dockerfile) : sans FUSE dans le conteneur, appimagetool ne peut pas se monter
# lui-même. Ça ne change RIEN à l'AppImage produite, qui, elle, se monte par
# FUSE chez le joueur — ou se déballe avec la même option s'il n'en a pas.
RUNTIME=""
if [ -n "${APPIMAGE_RUNTIME_FILE:-}" ]; then
    RUNTIME="--runtime-file $APPIMAGE_RUNTIME_FILE"
fi
# shellcheck disable=SC2086
appimagetool $RUNTIME "$APPDIR" "$APPIMAGE"

# L'AppImage est relancée ICI, depuis le fichier fini, avant qu'on annonce
# quoi que ce soit. `--appimage-extract-and-run` parce que le conteneur n'a pas
# FUSE ; le joueur, lui, l'a. Une AppImage qui ne démarre pas est le genre de
# paquet qu'on ne découvre qu'après l'avoir publié.
chmod +x "$APPIMAGE"
"$APPIMAGE" --appimage-extract-and-run --help | head -2

# ---------------------------------------------------------------------------
# 4. Signature — facultative, et le manifeste qui dit laquelle a été prise
# ---------------------------------------------------------------------------
# RIEN À SIGNER, AU SENS OÙ MACOS ET WINDOWS L'ENTENDENT. Aucun noyau Linux ne
# refuse un binaire non signé, aucun bureau n'affiche d'avertissement : les
# trois paquets ci-dessus s'installent et démarrent tels quels. Ce que la
# signature apporte ici est autre chose, et plus modeste — de quoi vérifier
# qu'un fichier vient bien de ce dépôt et n'a pas été remplacé en route, sans
# avoir à croire GitHub sur parole.
#
# `apt` NE LA VÉRIFIE PAS : il ne contrôle les signatures que pour les paquets
# venus d'un dépôt, jamais pour un `apt install ./fichier.deb`. La signature
# détachée s'adresse donc à qui la cherche, pas au joueur — et c'est pour ça
# qu'elle est facultative et qu'aucune étape n'échoue sans elle.
#
# Les sommes SHA-256, elles, sont écrites DANS LES DEUX CAS : la page de
# téléchargement du site les promet depuis longtemps et personne ne les
# produisait.
MANIFESTE="$SORTIE/SIGNATURE-linux.txt"

if [ -n "${NINETEEN_SIGN_GPG_KEY:-}" ] && command -v gpg >/dev/null 2>&1; then
    # Un trousseau JETABLE, dans le répertoire de sortie et pas dans
    # `~/.gnupg` : ce script tourne dans un conteneur partagé avec l'arbre de
    # sources par un montage, et une clé privée importée dans le trousseau de
    # l'utilisateur y resterait après la fabrication.
    GNUPGHOME="$(mktemp -d)"
    export GNUPGHOME
    chmod 700 "$GNUPGHOME"
    # La clé arrive en base64 parce qu'un secret GitHub est une chaîne d'une
    # seule ligne : une clé ASCII-armored y perdrait ses sauts de ligne.
    printf '%s' "$NINETEEN_SIGN_GPG_KEY" | base64 -d | gpg --batch --quiet --import

    # `--pinentry-mode loopback` : sans lui gpg cherche un terminal pour
    # demander la phrase de passe, n'en trouve pas dans un conteneur, et rend
    # une erreur qui ne dit pas laquelle.
    for f in "$SORTIE"/*.tar.gz "$SORTIE"/*.deb "$SORTIE"/*.AppImage; do
        [ -f "$f" ] || continue
        gpg --batch --yes --quiet --pinentry-mode loopback \
            --passphrase "${NINETEEN_SIGN_GPG_PASSPHRASE:-}" \
            --detach-sign --armor "$f"
    done
    ETAT="SIGNE (GPG, signature detachee .asc a cote de chaque fichier)"
    GESTE="gpg --verify nineteen_*.deb.asc nineteen_*.deb"
    CLE="$(gpg --batch --list-secret-keys --with-colons | awk -F: '/^fpr:/ {print $10; exit}')"
    gpg --batch --armor --export > "$SORTIE/nineteen-signature.pub.asc"
    rm -rf "$GNUPGHOME"
    unset GNUPGHOME
else
    # PAS D'ÉCHEC ICI, et c'est le point de tout ce bloc : sans clé on fabrique
    # le paquet quand même, et on l'écrit dans le manifeste plutôt que de
    # laisser croire.
    ETAT="NON SIGNE"
    GESTE="Rien de particulier : les paquets Linux s'installent et demarrent tels quels."
    CLE=""
    if [ -n "${NINETEEN_SIGN_GPG_KEY:-}" ]; then
        echo "signature : NINETEEN_SIGN_GPG_KEY est posee mais gpg est absent de cette image"
    fi
fi

{
    echo "Nineteen -- paquets Linux"
    echo
    echo "Etat : $ETAT"
    # Un `if` et non `[ … ] && echo` : sous `set -e`, savoir si l'echec d'un
    # test en tete de liste ET arrete le script demande de connaitre une regle
    # POSIX que personne ne devrait avoir a retrouver pour relire ce fichier.
    if [ -n "$CLE" ]; then
        echo "Empreinte de la cle : $CLE"
    fi
    echo
    echo "Architecture : $ARCH -- LUE sur la machine de fabrication, jamais supposee."
    echo "Un paquet aarch64 ne s'installe pas sur un PC x86_64, et inversement."
    echo
    ( cd "$SORTIE" && sha256sum ./*.tar.gz ./*.deb ./*.AppImage 2>/dev/null )
    echo
    echo "Ce que le joueur doit faire :"
    echo "  $GESTE"
    echo
    echo "Verifier soi-meme :"
    echo "  sha256sum -c SIGNATURE-linux.txt --ignore-missing"
    echo
    # LE NOMBRE DE LIGNES N'EST PAS ECRIT, et c'est delibere : sha256sum
    # compte les lignes de CE fichier-ci, donc toute retouche de ce texte
    # changerait le chiffre qu'il annonce. Un nombre qui se dement lui-meme au
    # premier mot ajoute vaut moins que pas de nombre du tout.
    echo "  Elle rend OK pour chaque fichier present, puis un avertissement"
    echo "  'lines are improperly formatted' qui compte les lignes de ce"
    echo "  texte-ci -- sha256sum n'a pas a les comprendre. Elle sort en 0."
} > "$MANIFESTE"

# ---------------------------------------------------------------------------
# 5. Lisibles par qui les sert
# ---------------------------------------------------------------------------
# Ce script tourne en root dans un conteneur, et le serveur qui sert ces
# fichiers tourne en `nonroot` dans un autre. Sans ce chmod, la page listerait
# des paquets que le telechargement rendrait en 404.
chmod a+r "$SORTIE"/* 2>/dev/null || true
chmod a+rx "$SORTIE" 2>/dev/null || true

# ---------------------------------------------------------------------------
# 6. Ce qu'on vient de fabriquer, en chiffres
# ---------------------------------------------------------------------------
echo
echo "=== Signature ==="
cat "$MANIFESTE"
echo
echo "=== Paquets ==="
ls -la "$SORTIE"
echo
echo "=== Dependances declarees par le .deb ==="
dpkg-deb -f "$SORTIE"/nineteen_*.deb Depends Recommends Installed-Size
echo
echo "=== glibc exigee par le binaire livre ==="
# C'est CE chiffre, et non la version de la glibc de l'image, qui decide de ce
# sur quoi l'AppImage demarre : les symboles versionnes sont resolus au
# chargement, sans repli.
objdump -T "$APPDIR/usr/lib/nineteen/nineteen" \
    | sed -n 's/.*GLIBC_\([0-9.]*\).*/\1/p' | sort -uV | tail -1
echo
echo "=== Ce que le binaire demande au systeme ==="
ldd "$APPDIR/usr/lib/nineteen/nineteen"
