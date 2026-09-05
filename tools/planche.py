#!/usr/bin/env python3
"""planche.py — assemble des captures en une planche-contact.

POURQUOI CET OUTIL EXISTE
-------------------------
On juge une salle sur HUIT points de vue, et on les regarde un par un. C'est le
défaut que `tools/vues-diff.py` décrit déjà pour les régressions : on rouvre
celui qu'on vient de corriger, jamais les sept autres. Pour une régression, un
nombre suffit à dire où regarder. Pour une AMBIANCE, non : il faut voir les huit
ensemble, parce que ce qu'on juge est justement leur cohérence — une salle dont
deux vues sont roses et six ambrées n'est pas « à 25 % refaite », elle est
incohérente, et aucune mesure vue par vue ne le dit.

Une planche coûte une image à regarder au lieu de huit.

POURQUOI IL ENCODE LE PNG À LA MAIN
-----------------------------------
Cette machine n'a ni PIL, ni numpy, ni ImageMagick — vérifié, comme
`vues-diff.py` l'avait déjà constaté. `sips` réduit mais n'assemble pas.
L'encodeur tient en quinze lignes pour ce dont on a besoin : 8 bits, RGB, un
seul filtre. Le décodeur, lui, est emprunté à `vues-diff.py` plutôt que recopié.

    python3 tools/planche.py SORTIE.png CAPTURE.png [CAPTURE.png ...]
"""

import importlib.util as _ilu
import os
import struct
import subprocess
import sys
import tempfile
import zlib

_spec = _ilu.spec_from_file_location(
    "vues_diff", os.path.join(os.path.dirname(os.path.abspath(__file__)), "vues-diff.py")
)
_vd = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(_vd)
lire_png = _vd.lire_png

# 400 pixels de large par vignette. Une planche de huit tient alors en 1600 de
# large sur quatre lignes — la taille à laquelle un écran la montre en entier.
# À 200, les marquees des bornes deviennent illisibles et on ne juge plus que
# les masses ; à 800, la planche ne tient plus dans un écran et on recommence à
# faire défiler, c'est-à-dire à regarder les vues une par une.
LARGEUR = 400
COLONNES = 4
MARGE = 6
FOND = (18, 18, 20)


def reduire(source):
    # La capture absente est nommée ICI. `sips` n'écrit rien quand son entrée
    # manque, et le décodeur échouait alors sur le TEMPORAIRE : la trace
    # accusait un fichier de /var/folders au lieu de la vue qui n'a pas rendu.
    if not os.path.isfile(source):
        raise SystemExit("capture introuvable : %s" % source)
    sortie = tempfile.mktemp(suffix=".png")
    subprocess.run(["sips", "-Z", str(LARGEUR), source, "--out", sortie],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.isfile(sortie):
        raise SystemExit("sips n'a rien écrit pour %s" % source)
    return sortie


def ecrire_png(chemin, larg, haut, px):
    """Écrit un PNG 8 bits RGB, filtre 0 sur toutes les lignes.

    Le filtre 0 (aucun) n'est pas un raccourci paresseux : une planche-contact
    est un fichier de travail qu'on regarde et qu'on jette. zlib ramène malgré
    tout la mosaïque de 1600 x 1800 sous le mégaoctet, et choisir un filtre par
    ligne coûterait plus de code que ce que la place économisée vaut.
    """
    brut = bytearray()
    pas = larg * 3
    for y in range(haut):
        brut.append(0)
        brut += px[y * pas:(y + 1) * pas]

    def morceau(genre, corps):
        return (struct.pack(">I", len(corps)) + genre + corps
                + struct.pack(">I", zlib.crc32(genre + corps) & 0xFFFFFFFF))

    with open(chemin, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(morceau(b"IHDR", struct.pack(">IIBBBBB", larg, haut, 8, 2, 0, 0, 0)))
        f.write(morceau(b"IDAT", zlib.compress(bytes(brut), 6)))
        f.write(morceau(b"IEND", b""))


def main():
    if len(sys.argv) < 3:
        print("usage : planche.py SORTIE.png CAPTURE.png [...]")
        return 2
    sortie, sources = sys.argv[1], sys.argv[2:]

    vignettes = []
    for s in sources:
        petit = reduire(s)
        try:
            vignettes.append(lire_png(petit))
        finally:
            os.unlink(petit)

    vl = max(v[0] for v in vignettes)
    vh = max(v[1] for v in vignettes)
    lignes = (len(vignettes) + COLONNES - 1) // COLONNES
    larg = COLONNES * vl + (COLONNES + 1) * MARGE
    haut = lignes * vh + (lignes + 1) * MARGE

    px = bytearray(FOND * (larg * haut))
    for i, (vw, vhh, vp) in enumerate(vignettes):
        cx = MARGE + (i % COLONNES) * (vl + MARGE)
        cy = MARGE + (i // COLONNES) * (vh + MARGE)
        for y in range(vhh):
            d = ((cy + y) * larg + cx) * 3
            px[d:d + vw * 3] = vp[y * vw * 3:(y + 1) * vw * 3]

    ecrire_png(sortie, larg, haut, bytes(px))
    print("planche : %d vignettes, %dx%d, %d octets"
          % (len(vignettes), larg, haut, os.path.getsize(sortie)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
