#!/usr/bin/env python3
"""
extract_aplomb_pieces.py — recopie la table des pièces de 2020, exactement.

`legacy/games/5_tetris/pieces.h` déclare
`PIECES[2 difficultés][2 tailles][7 pièces][4 rotations][10][10]`, soit
**11 200 entiers** écrits à la main. Les recopier à la main serait la garantie
d'une erreur qu'on ne verrait qu'en jouant — une rotation fausse sur une pièce
sur vingt-huit ne se remarque pas sur une capture.

Ce script produit `games/aplomb/aplomb_pieces.h` à partir du fichier d'origine.
Il n'est PAS appelé par le build : le résultat est versionné, parce qu'ajouter
un interpréteur Python à une chaîne de build en C serait un prix disproportionné
pour une table qui ne changera plus. Ce qui garantit qu'elle reste fidèle est le
condensé du fichier source, vérifié par `tests/test_aplomb.c` : si
`pieces.h` change, le test le dit et on relance ce script.

    python3 tools/extract_aplomb_pieces.py
"""
import hashlib
import os
import re
import sys

SRC = os.path.join(os.path.dirname(__file__), "..", "legacy", "games", "5_tetris", "pieces.h")
DST = os.path.join(os.path.dirname(__file__), "..", "games", "aplomb", "aplomb_pieces.h")

D, S, P, R, N = 2, 2, 7, 4, 10

raw = open(SRC, "rb").read()
digest = hashlib.sha256(raw).hexdigest()
text = raw.decode("utf-8", "replace")

body = text[text.index("{", text.index("const char PIECES")):]
nums = [int(x) for x in re.findall(r"\b\d\b", body)]
if len(nums) != D * S * P * R * N * N:
    sys.exit("pieces.h : %d entiers, %d attendus" % (len(nums), D * S * P * R * N * N))

rows, pivots = [], []
i = 0
for d in range(D):
    for s in range(S):
        for p in range(P):
            for r in range(R):
                mask, pivot = [], (0, 0)
                for y in range(N):
                    bits = 0
                    for x in range(N):
                        v = nums[i + y * N + x]
                        if v:
                            bits |= 1 << x
                        if v == 2:
                            pivot = (x, y)
                    mask.append(bits)
                i += N * N
                rows.append(mask)
                pivots.append(pivot)

out = []
out.append("/*\n"
           " * aplomb_pieces.h — la table des pièces de 2020, recopiée exactement.\n"
           " *\n"
           " * PRODUIT PAR `tools/extract_aplomb_pieces.py`. Ne pas modifier à la main :\n"
           " * relancer le script.\n"
           " *\n"
           " * Source : legacy/games/5_tetris/pieces.h\n"
           " * sha256 : %s\n"
           " *\n"
           " * Onze mille deux cents entiers écrits à la main en 2020 — deux difficultés,\n"
           " * deux tailles, sept pièces, quatre rotations, sur une grille de 10 x 10. Les\n"
           " * recopier à la main serait la garantie d'une erreur invisible : une rotation\n"
           " * fausse sur une pièce sur vingt-huit ne se voit pas sur une capture, elle se\n"
           " * découvre en jouant.\n"
           " *\n"
           " * Deux constats de la lecture, mesurés et non supposés :\n"
           " *   - les deux difficultés n'ont AUCUNE forme en commun : les 56 diffèrent ;\n"
           " *   - une pièce géante EST la pièce normale doublée — les 56 le sont — mais\n"
           " *     son PIVOT ne l'est pas : dans 42 cas sur 56 il n'est pas au double de\n"
           " *     celui de la pièce normale. Dériver les géantes par un doublement\n"
           " *     donnerait la bonne forme et la MAUVAISE rotation.\n"
           " *\n"
           " * Encodage : une ligne par entier 16 bits, le bit x valant « case pleine ».\n"
           " * Le pivot est la case marquée « 2 » dans la source ; c'est autour d'elle que\n"
           " * tourne la pièce.\n"
           " */\n"
           "#ifndef NS_APLOMB_PIECES_H\n"
           "#define NS_APLOMB_PIECES_H\n\n"
           "#include <stdint.h>\n\n"
           "#define APL_DIFFICULTIES %d\n"
           "#define APL_SIZES        %d\n"
           "#define APL_PIECES       %d\n"
           "#define APL_ROTATIONS    %d\n"
           "#define APL_GRID         %d\n\n"
           "#define APL_PIECES_SOURCE_SHA256 \"%s\"\n\n"
           "/* [difficulté][taille][pièce][rotation][ligne] */\n"
           "static const uint16_t APL_SHAPE[APL_DIFFICULTIES][APL_SIZES][APL_PIECES]"
           "[APL_ROTATIONS][APL_GRID] = {\n"
           % (digest, D, S, P, R, N, digest))

k = 0
for d in range(D):
    out.append("{\n")
    for s in range(S):
        out.append("  {\n")
        for p in range(P):
            out.append("    {\n")
            for r in range(R):
                out.append("      { " + ", ".join("0x%03X" % b for b in rows[k]) + " },\n")
                k += 1
            out.append("    },\n")
        out.append("  },\n")
    out.append("},\n")
out.append("};\n\n")

out.append("/* Le pivot, en (x, y) sur la même grille. */\n"
           "static const uint8_t APL_PIVOT[APL_DIFFICULTIES][APL_SIZES][APL_PIECES]"
           "[APL_ROTATIONS][2] = {\n")
k = 0
for d in range(D):
    out.append("{\n")
    for s in range(S):
        out.append("  {\n")
        for p in range(P):
            out.append("    {")
            for r in range(R):
                out.append(" { %d, %d }," % pivots[k])
                k += 1
            out.append(" },\n")
        out.append("  },\n")
    out.append("},\n")
out.append("};\n\n#endif /* NS_APLOMB_PIECES_H */\n")

open(DST, "w").write("".join(out))
print("écrit %s (%d formes, source sha256 %s)" % (DST, k, digest[:16]))
