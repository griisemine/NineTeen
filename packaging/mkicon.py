#!/usr/bin/env python3
"""
mkicon — dessine l'icône du jeu, dans les trois formats que veulent les trois
installateurs.

Pourquoi un script et pas un PNG déposé là. Un binaire commité sans sa recette
est un asset qu'on ne peut plus corriger : on ne sait ni d'où il vient, ni
comment le refaire en 512 si un jour il en faut du 512. Ici la recette EST le
fichier, elle tient en une page, et tout se régénère par
`python3 packaging/mkicon.py --tout` (sur un Mac, pour l'`.icns`).

Ce qu'elle dessine : la silhouette de notre borne — le profil en gradins de
`build_cabinet`, vu de côté — avec sa dalle allumée en chaud sur un fond sombre.
C'est ce qui la rend reconnaissable à 32 pixels, où un logo texte ne le serait
pas.

CE QUI SORT, ET POUR QUI
------------------------
  packaging/nineteen.png          256x256, l'icône Linux (.desktop) ET les
                                  octets embarqués dans `engine/rhi/ns_rhi_icon.h`
                                  pour l'icône de fenêtre SDL. NE PAS la changer
                                  sans régénérer ce header : le tableau C est une
                                  copie de ces octets exacts.
  packaging/macos/nineteen.icns   l'icône du `Nineteen.app` dans le .dmg
  packaging/windows/nineteen.ico  l'icône de l'installateur NSIS et du raccourci

POURQUOI ON REND CHAQUE TAILLE, PLUTÔT QUE DE RÉDUIRE LE 256
------------------------------------------------------------
Un `.icns` complet demande jusqu'à 1024x1024 : partir du 256 obligerait à
AGRANDIR, et `sips -z 1024 1024` d'un 256 rend un bord de borne flou sur quatre
pixels. Le dessin étant procédural, chaque taille se rend à sa résolution
native — c'est exact, et ça ne coûte rien (le 1024 se compresse à quelques Kio,
la forme n'a que des aplats).

Le rendu du 256 n'a PAS bougé : `python3 packaging/mkicon.py` reproduit
`packaging/nineteen.png` octet pour octet, ce qui garde `ns_rhi_icon.h` valide.

Aucune dépendance : l'encodeur PNG tient en dix lignes de zlib, l'encodeur ICO
en trente. `iconutil` (macOS) fait le seul pas qu'on ne peut pas faire soi-même.
"""
import os
import struct
import subprocess
import sys
import zlib

SIZE = 256


def srgb(v):
    return max(0, min(255, int(v * 255.0 + 0.5)))


def png(path, pixels, size):
    """Encode du RGBA sans filtre — une icône ne pèse rien, la compression suffit."""
    raw = b"".join(b"\x00" + bytes(pixels[y]) for y in range(size))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def inside_cabinet(x, y):
    """
    Le profil de la borne, en coordonnées 0..1 (x vers la droite, y vers le bas).

    Les neuf gradins de `build_cabinet` ramenés à cinq : à cette taille, les
    quatre autres se ferment. Ce qui doit survivre, c'est ce qui fait reconnaître
    une borne — le marquee en surplomb, le retrait sous l'écran, l'avancée du
    panneau de commande.
    """
    if not (0.20 <= x <= 0.80):
        return False
    if y < 0.10 or y > 0.94:
        return False
    if y < 0.22:                      # marquee en surplomb
        return 0.20 <= x <= 0.80
    if y < 0.30:                      # panneau haut-parleurs, en retrait
        return 0.26 <= x <= 0.74
    if y < 0.56:                      # l'écran, en retrait
        return 0.24 <= x <= 0.76
    if y < 0.66:                      # le panneau de commande, qui ressort
        return 0.20 <= x <= 0.80
    return 0.26 <= x <= 0.74          # le caisson


def render(size):
    """Le dessin, en RGBA, à la taille demandée. Une ligne = un `bytearray`."""
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            x = (px + 0.5) / size
            y = (py + 0.5) / size

            # Fond : un dégradé sombre et chaud, coins arrondis.
            corner = 0.10
            dx = max(corner - x, x - (1.0 - corner), 0.0)
            dy = max(corner - y, y - (1.0 - corner), 0.0)
            if (dx * dx + dy * dy) ** 0.5 > corner:
                row += bytes((0, 0, 0, 0))
                continue

            r, g, b = 0.10 + 0.06 * (1.0 - y), 0.055 + 0.03 * (1.0 - y), 0.075
            a = 1.0

            if inside_cabinet(x, y):
                # Le caisson : bleu nuit, plus clair vers le haut.
                r, g, b = 0.13, 0.16, 0.30
                if 0.10 <= y < 0.22:            # marquee allumé, chaud
                    r, g, b = 1.00, 0.72, 0.30
                elif 0.32 <= y < 0.54 and 0.28 <= x <= 0.72:
                    # La dalle : c'est elle qui doit accrocher l'œil.
                    t = (y - 0.32) / 0.22
                    r, g, b = 0.30 + 0.60 * t, 0.80 - 0.25 * t, 0.95 - 0.35 * t
                elif 0.58 <= y < 0.64 and 0.24 <= x <= 0.76:
                    r, g, b = 0.72, 0.20, 0.18   # le panneau de commande

            row += bytes((srgb(r), srgb(g), srgb(b), srgb(a)))
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# .ico — Windows
# ---------------------------------------------------------------------------
#
# POURQUOI DU BMP NON COMPRESSÉ, ET PAS DU PNG DANS L'ICO.
#
# Depuis Vista un `.ico` peut porter ses grandes tailles en PNG. Mesuré, sur ce
# dessin : les six mêmes tailles en PNG pèsent 4 074 octets, contre 370 070 en
# BMP — un facteur 91. Le PNG est pourtant refusé ici, et pour une raison qui
# n'est pas esthétique : l'icône est consommée par `makensis`, qu'on ne peut PAS
# lancer sur ce Mac (ni NSIS, ni mingw, ni wine), et les versions anciennes de
# makensis rejettent l'icône PNG avec « invalid icon file ». Un gain de 358 Kio
# contre le risque que la release Windows s'arrête sur une icône, sans qu'on
# puisse le vérifier avant de pousser la balise : mauvais marché. Le BMP 32 bits
# est ce que tout lit depuis Windows 95.
#
# Mesuré : packaging/windows/nineteen.ico = 370 070 octets pour six tailles,
# dont 270 376 pour la seule entrée 256x256 (256*256*4 octets de couleur, plus
# son masque de 8 192). C'est le prix du format, pas du dessin.
ICO_SIZES = (16, 32, 48, 64, 128, 256)


def ico_image(rows, size):
    """Une entrée d'.ico : BITMAPINFOHEADER + pixels BGRA + masque AND, de bas en haut."""
    # La hauteur du header vaut DEUX FOIS la hauteur réelle : le format compte
    # l'image couleur et le masque comme un seul bitmap empilé. L'oublier donne
    # une icône coupée en deux, moitié transparente.
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)

    colour = bytearray()
    for y in range(size - 1, -1, -1):          # bas en haut
        row = rows[y]
        for x in range(size):
            r, g, b, a = row[4 * x:4 * x + 4]
            colour += bytes((b, g, r, a))      # BGRA

    # Masque AND : 1 bit par pixel, 0 = opaque. Redondant avec le canal alpha
    # pour tout ce qui sait lire du 32 bits, mais le format l'exige et certains
    # chemins de Windows le lisent encore. Lignes alignées sur 4 octets.
    stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        row = rows[y]
        bits = bytearray(stride)
        for x in range(size):
            if row[4 * x + 3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits

    return header + bytes(colour) + bytes(mask)


def ico(path, sizes=ICO_SIZES):
    images = [(s, ico_image(render(s), s)) for s in sizes]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in images:
        # 0 code la taille 256 : le champ ne fait qu'un octet.
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32,
                           len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


# ---------------------------------------------------------------------------
# .icns — macOS
# ---------------------------------------------------------------------------
#
# Les dix noms attendus par `iconutil`, et rien d'autre : un fichier de plus
# dans le .iconset et l'outil s'arrête sur « Unable to parse ». Le @2x n'est pas
# un agrandissement, c'est un rendu à la taille doublée — donc net sur un écran
# Retina, là où un `sips -z` ne le serait pas.
ICONSET = [
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024),
]


def iconset(directory):
    os.makedirs(directory, exist_ok=True)
    cache = {}
    for name, size in ICONSET:
        if size not in cache:
            cache[size] = render(size)
        png(os.path.join(directory, name), cache[size], size)
    return directory


def icns(path, workdir):
    """`iconutil` n'existe que sur macOS : ailleurs, on le dit et on n'invente rien."""
    setdir = iconset(os.path.join(workdir, "nineteen.iconset"))
    subprocess.run(["iconutil", "-c", "icns", setdir, "-o", path], check=True)
    return os.path.getsize(path)


def main(argv):
    tout = "--tout" in argv

    rows = render(SIZE)
    png("packaging/nineteen.png", rows, SIZE)
    print("packaging/nineteen.png : %dx%d, %d octets"
          % (SIZE, SIZE, os.path.getsize("packaging/nineteen.png")))

    if not tout:
        return 0

    os.makedirs("packaging/windows", exist_ok=True)
    n = ico("packaging/windows/nineteen.ico")
    print("packaging/windows/nineteen.ico : %s, %d octets"
          % ("+".join(str(s) for s in ICO_SIZES), n))

    if sys.platform != "darwin":
        print("packaging/macos/nineteen.icns : IGNORÉ — iconutil n'existe que "
              "sur macOS. Le fichier commité reste celui de la dernière "
              "génération sur un Mac.")
        return 0

    os.makedirs("packaging/macos", exist_ok=True)
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        n = icns("packaging/macos/nineteen.icns", tmp)
    print("packaging/macos/nineteen.icns : 16..1024, %d octets" % n)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
