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
#   nineteen-17.0.0-linux-x86_64.tar.gz   à déballer où l'on veut
#   nineteen_17.0.0_amd64.deb             apt install ./…deb
#   Nineteen-17.0.0-x86_64.AppImage       chmod +x, puis on lance
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
SORTIE="$BUILD/paquets"
APPDIR="$BUILD/AppDir"

cd "$RACINE"

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
rm -rf "$SORTIE"
cpack --config "$BUILD/CPackConfig.cmake" -B "$SORTIE"

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
# 4. Ce qu'on vient de fabriquer, en chiffres
# ---------------------------------------------------------------------------
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
