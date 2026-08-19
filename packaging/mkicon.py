#!/usr/bin/env python3
"""
mkicon — dessine l'icône du jeu.

Pourquoi un script et pas un PNG déposé là. Un binaire commité sans sa recette
est un asset qu'on ne peut plus corriger : on ne sait ni d'où il vient, ni
comment le refaire en 512 si un jour il en faut du 512. Ici la recette EST le
fichier, elle tient en une page, et `nineteen.png` se régénère par
`python3 packaging/mkicon.py`.

Ce qu'elle dessine : la silhouette de notre borne — le profil en gradins de
`build_cabinet`, vu de côté — avec sa dalle allumée en chaud sur un fond sombre.
C'est ce qui la rend reconnaissable à 32 pixels, où un logo texte ne le serait
pas.

Aucune dépendance : l'encodeur PNG tient en dix lignes de zlib.
"""
import struct
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


def main():
    rows = []
    for py in range(SIZE):
        row = bytearray()
        for px in range(SIZE):
            x = (px + 0.5) / SIZE
            y = (py + 0.5) / SIZE

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

    png("packaging/nineteen.png", rows, SIZE)
    print("packaging/nineteen.png : %dx%d" % (SIZE, SIZE))


if __name__ == "__main__":
    main()
