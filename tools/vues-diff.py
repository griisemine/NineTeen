#!/usr/bin/env python3
"""vues-diff.py — compare deux jeux de captures, et dit ce qui a bougé.

POURQUOI CET OUTIL EXISTE
-------------------------
« Sans la moindre régression graphique » est une promesse qu'on ne peut pas
tenir à l'œil sur huit points de vue à chaque modification : on regarde celui
qu'on vient de corriger, et on ne rouvre pas les sept autres. Le défaut classique
d'un lot d'éclairage est justement celui-là — on règle le bar, et l'allée
s'assombrit de dix pour cent sans que personne ne le voie avant le joueur.

CE QU'IL MESURE, ET CE QU'IL NE MESURE PAS
------------------------------------------
Il rend, pour chaque point de vue, la PART DE SOUS-PIXELS qui ont visiblement
changé — plus de huit niveaux sur 255. C'est une mesure grossière et c'est voulu :
elle ne dit pas si le changement est beau, elle dit s'il y en a un et sur quelle
étendue. Zéro prouve qu'une vue n'a pas bougé ; trente pour cent ne prouve rien
d'autre que « allez regarder celle-là ».

L'écart moyen est affiché à côté mais ne décide de rien, et il y a une raison
mesurée à ça : voir `ecart`.

Il ne remplace donc pas l'œil. Il dit OÙ le poser.

POURQUOI IL DÉCODE LE PNG A LA MAIN
------------------------------------
Cette machine n'a ni PIL, ni numpy, ni ImageMagick — vérifié. Le décodage tient
en trente lignes pour ce dont on a besoin (8 bits, RGB ou RGBA), et il vaut mieux
que d'ajouter une dépendance à un dépôt qui n'en a aucune pour ses outils.

Les images sont d'abord réduites par `sips`, qui est fourni avec macOS. Ce n'est
pas une économie de temps : c'est ce qui rend la mesure ROBUSTE. Comparer deux
rendus au pixel près ferait remonter le bruit d'échantillonnage de chaque image,
et tout serait « différent ». À 96 pixels de large, il reste la composition, les
masses et la lumière — c'est-à-dire ce dont on parle quand on dit régression.

    python3 tools/vues-diff.py REFERENCE COURANT
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

# 96 pixels de large. Assez pour qu'une affiche déplacée de dix centimètres se
# voie, assez peu pour que le bruit d'un rendu stochastique ne compte pas.
LARGEUR = 96


def reduire(source, vers):
    """Réduit avec `sips`, l'outil que macOS fournit déjà."""
    subprocess.run(
        ["sips", "-Z", str(LARGEUR), source, "--out", vers],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


def lire_png(chemin):
    """Rend (largeur, hauteur, octets RGB). 8 bits par canal seulement."""
    with open(chemin, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{chemin} n'est pas un PNG")

    i, idat, larg, haut, canaux = 8, b"", 0, 0, 0
    while i < len(data):
        (taille,) = struct.unpack(">I", data[i:i + 4])
        genre = data[i + 4:i + 8]
        corps = data[i + 8:i + 8 + taille]
        if genre == b"IHDR":
            larg, haut, profondeur, couleur = struct.unpack(">IIBB", corps[:10])
            if profondeur != 8 or couleur not in (2, 6):
                raise ValueError(f"{chemin} : profondeur {profondeur}, couleur {couleur}")
            canaux = 3 if couleur == 2 else 4
        elif genre == b"IDAT":
            idat += corps
        elif genre == b"IEND":
            break
        i += 12 + taille

    brut = zlib.decompress(idat)
    pas = larg * canaux
    sortie = bytearray(larg * haut * 3)
    precedente = bytearray(pas)
    j = 0
    for y in range(haut):
        filtre = brut[j]
        j += 1
        ligne = bytearray(brut[j:j + pas])
        j += pas
        # Les cinq filtres de la norme PNG. Aucun n'est optionnel : `sips` en
        # emploie plusieurs dans une meme image.
        for x in range(pas):
            a = ligne[x - canaux] if x >= canaux else 0
            b = precedente[x]
            c = precedente[x - canaux] if x >= canaux else 0
            if filtre == 1:
                ligne[x] = (ligne[x] + a) & 0xFF
            elif filtre == 2:
                ligne[x] = (ligne[x] + b) & 0xFF
            elif filtre == 3:
                ligne[x] = (ligne[x] + (a + b) // 2) & 0xFF
            elif filtre == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                ligne[x] = (ligne[x] + pred) & 0xFF
        precedente = ligne
        for x in range(larg):
            sortie[(y * larg + x) * 3:(y * larg + x) * 3 + 3] = \
                ligne[x * canaux:x * canaux + 3]
    return larg, haut, bytes(sortie)


# Un écart de moins de 8 niveaux sur 255 ne se voit pas sur un écran ordinaire,
# et c'est aussi l'ordre de grandeur du bruit entre deux rendus du même arbre.
# En deçà, on ne compte pas.
SEUIL_VISIBLE = 8


def ecart(a, b):
    """Rend (part de sous-pixels visiblement changés, écart moyen, écart max).

    LA PART EST LA MESURE QUI COMPTE, et l'écart moyen ne l'est pas — c'est une
    correction, faite après avoir étalonné l'outil contre un contrôle négatif.
    Cette salle est SOMBRE : luminance moyenne mesurée entre 14 et 69 selon le
    point de vue. Deux images entièrement différentes n'y rendent qu'un écart
    moyen de 4,7 sur 255, qu'un seuil naïf classait « léger ». Autrement dit,
    l'outil aurait laissé passer le remplacement complet d'une vue.

    La part de sous-pixels franchement changés ne souffre pas de ça : elle vaut
    zéro pour deux images identiques, et le même contrôle négatif la met à
    plusieurs dizaines de pour cent.
    """
    la, ha, pa = lire_png(a)
    lb, hb, pb = lire_png(b)
    if (la, ha) != (lb, hb):
        return None, None, None
    total = 0
    pire = 0
    changes = 0
    for i in range(len(pa)):
        d = abs(pa[i] - pb[i])
        total += d
        if d > pire:
            pire = d
        if d >= SEUIL_VISIBLE:
            changes += 1
    return 100.0 * changes / len(pa), total / len(pa), pire


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    ref, cur = sys.argv[1], sys.argv[2]

    noms = sorted(n for n in os.listdir(ref) if n.endswith(".png"))
    if not noms:
        print(f"aucune image dans {ref}")
        return 2

    tmp = tempfile.mkdtemp(prefix="vues-diff-")
    print(f"{'vue':<14} {'% changé':>9} {'moyen':>8} {'max':>5}   verdict")
    print("-" * 56)

    pires = []
    for nom in noms:
        a, b = os.path.join(ref, nom), os.path.join(cur, nom)
        if not os.path.exists(b):
            print(f"{nom[:-4]:<14} {'—':>9} {'—':>8} {'—':>5}   ABSENTE du jeu courant")
            pires.append((nom, None))
            continue
        pa, pb = os.path.join(tmp, "a_" + nom), os.path.join(tmp, "b_" + nom)
        reduire(a, pa)
        reduire(b, pb)
        part, moyen, maxi = ecart(pa, pb)
        if part is None:
            print(f"{nom[:-4]:<14} {'—':>9} {'—':>8} {'—':>5}   TAILLES DIFFERENTES")
            continue
        # Les seuils disent OU poser l'oeil ; ils ne prononcent pas de verdict
        # esthetique. Ils sont etalonnes sur un controle negatif — deux vues
        # entierement interverties — et non choisis au juge.
        verdict = ("identique" if part < 0.5 else
                   "leger" if part < 3.0 else
                   "VISIBLE" if part < 12.0 else "FRANC")
        print(f"{nom[:-4]:<14} {part:>8.2f}% {moyen:>8.2f} {maxi:>5}   {verdict}")
        pires.append((nom, part))

    print()
    bouges = [(n, m) for n, m in pires if m is not None and m >= 3.0]
    if bouges:
        print("À REGARDER À L'ŒIL, dans cet ordre :")
        for n, m in sorted(bouges, key=lambda t: -t[1]):
            print(f"  {n}  ({m:.1f}% de sous-pixels changés)")
    else:
        print("Aucune vue n'a bougé au-delà du bruit de rendu.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
