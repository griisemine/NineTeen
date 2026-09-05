#!/usr/bin/env python3
"""ambiance.py — chiffre l'AMBIANCE d'une capture, pas sa ressemblance.

POURQUOI CET OUTIL EXISTE
-------------------------
`tools/vues-diff.py` répond à « est-ce que ça a bougé ». Il ne répond pas à
« est-ce que ça ressemble à une salle d'arcade ». Or c'est cette question-là
qu'on pose quand on compare le rendu à une image de référence, et on y
répondait à l'œil — c'est-à-dire qu'on ne se mettait jamais d'accord.

Le propriétaire a fourni une photo de ce que la salle DOIT donner : sombre, mais
traversée de néon rose et cyan, saturée, avec des cœurs de source qui brûlent.
Le rendu d'alors était sombre ET ambré ET désaturé. Les trois mots comptent, et
aucun des deux outils existants ne les distinguait : une salle deux fois plus
claire mais toujours ambrée aurait montré un « écart » énorme sans se rapprocher
d'un pas de la photo.

CE QU'EST CETTE PHOTO, ET CE QU'ELLE N'EST PAS
----------------------------------------------
C'est un AGRANDISSEMENT D'UNE CAPTURE DU JEU LUI-MÊME, et ça se démontre : son
HUD affiche « JETONS 100072 / TICKETS 19856 / SERIE 1 J », c'est-à-dire au
chiffre près le portefeuille du propriétaire ; le cadre de fenêtre est celui de
macOS et porte le titre « Nineteen » ; les frontons nomment DEMINEUR et SNAKE ;
la moquette est celle de `moquetteart`.

Il faut donc en viser la LUMIÈRE et non les LÉGENDES. L'agrandissement a inventé
du texte : l'enseigne du fond y lit « RETRO CAFE » là où le jeu écrit NINETEEN,
et le tableau du bar y porte des mots qui ne sont d'aucune langue. Reproduire
ces légendes reviendrait à copier une hallucination dans le produit. Les
consignes ci-dessous ne portent donc que sur des grandeurs photométriques, qui
sont ce que l'agrandissement n'a PAS inventé : il a rehaussé une lumière, il ne
l'a pas fabriquée.

CE QU'IL MESURE
---------------
Cinq nombres, et chacun correspond à une phrase qu'on prononce devant l'image :

  clair     la MÉDIANE de luminance. La moyenne ne vaut rien ici : un seul néon
            à 255 sur 5 % de l'image déplace la moyenne de 12 points sans que
            la salle s'éclaire. La médiane dit où est le gros de l'image.
  brule     la part de pixels au-dessus de 200. C'est le NOMBRE DE SOURCES
            VISIBLES — marquees, tubes, écrans. Une salle d'arcade en a
            beaucoup ; un entrepôt n'en a aucune.
  couleur   la saturation moyenne (max-min sur max), pondérée par la luminance.
            La pondération n'est pas un raffinement : sans elle, le noir du
            plafond — bruit d'un rendu stochastique, donc légèrement teinté —
            pèse autant qu'un tube de néon, et toute salle sombre ressort
            « colorée ».
  teintes   la masse de chaque famille de teinte parmi les pixels qui comptent
            (assez clairs ET assez saturés). C'est ce qui sépare l'ambré du
            néon : deux images de même médiane et de même saturation peuvent
            être l'une un tungstène, l'autre un rose fluorescent.
  contraste l'écart interquartile de luminance. Une salle d'arcade est faite de
            masses noires et de sources vives ; un aplat gris a la même médiane
            et ne ressemble à rien.

CE QU'IL NE MESURE PAS
----------------------
La composition, le cadrage, la géométrie, le dessin. Il ne dit pas si l'image
est belle. Il dit si la LUMIÈRE est la bonne — et c'est précisément la partie
qu'on n'arrivait pas à trancher de mémoire d'une capture à l'autre.

    python3 tools/ambiance.py CAPTURE.png [AUTRE.png ...]
    python3 tools/ambiance.py --cible CAPTURE.png    (compare aux consignes)
"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module

_diff = import_module("vues-diff".replace("-", "_")) if False else None

# On réutilise le décodeur de `vues-diff.py` plutôt que d'en écrire un second :
# un bug de filtre PNG corrigé à un seul endroit vaut mieux que deux copies qui
# divergent. Le nom du module porte un tiret, que `import` refuse ; on le charge
# donc par son chemin.
import importlib.util as _ilu

_spec = _ilu.spec_from_file_location(
    "vues_diff", os.path.join(os.path.dirname(os.path.abspath(__file__)), "vues-diff.py")
)
_vd = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(_vd)
lire_png = _vd.lire_png

# 320 pixels de large, et non les 96 de `vues-diff.py`. La raison est mesurée :
# à 96, un tube de néon d'un pixel d'épaisseur se moyenne avec le mur derrière
# et perd les deux tiers de sa saturation — l'outil sous-estimait alors « brule »
# d'un facteur trois. 320 garde les sources fines et reste sous la seconde.
LARGEUR = 320

# Un pixel « compte » pour la teinte s'il est assez clair pour être vu et assez
# saturé pour avoir une couleur. Les deux seuils sont étalonnés sur la salle :
# sous 40 de luminance on est dans les masses noires du plafond, et sous 0,18 de
# saturation on est sur du gris que l'œil ne nomme pas.
CLAIR_MINI = 40
SATURE_MINI = 0.18

# Les familles de teinte, en degrés. Elles ne découpent pas le cercle en parts
# égales : on nomme les couleurs qu'on voit dans une salle d'arcade, et le rose
# du néon couvre à lui seul deux fois plus de cercle que le vert.
FAMILLES = (
    ("rouge",  345, 15),
    ("ambre",   15, 65),   # tungstène, marquees jaunes, bois
    ("vert",    65, 165),
    ("cyan",   165, 205),  # tubes froids
    ("bleu",   205, 265),
    ("rose",   265, 345),  # magenta, violet, fuchsia — le néon de la photo
)


def reduire(source):
    """Réduit avec `sips`, et rend le chemin du réduit (temporaire).

    LA CAPTURE ABSENTE EST NOMMÉE ICI, et pas trois appels plus loin. `sips`
    n'écrit rien quand son entrée n'existe pas, et le décodeur échouait alors
    sur le TEMPORAIRE — la trace accusait un fichier de /var/folders que
    personne n'a demandé, au lieu de la capture manquante. Or le cas normal est
    justement celui-là : une vue dont le rendu a échoué laisse un PNG absent, et
    c'est le nom de cette vue qu'on veut lire.
    """
    if not os.path.isfile(source):
        raise SystemExit("capture introuvable : %s" % source)
    sortie = tempfile.mktemp(suffix=".png")
    subprocess.run(
        ["sips", "-Z", str(LARGEUR), source, "--out", sortie],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    if not os.path.isfile(sortie):
        raise SystemExit("sips n'a rien écrit pour %s" % source)
    return sortie


def teinte(r, v, b):
    """Teinte en degrés, et saturation par la définition HSV (max-min)/max."""
    haut, bas = max(r, v, b), min(r, v, b)
    if haut == 0:
        return 0.0, 0.0
    delta = haut - bas
    s = delta / haut
    if delta == 0:
        return 0.0, 0.0
    if haut == r:
        h = 60.0 * (((v - b) / delta) % 6.0)
    elif haut == v:
        h = 60.0 * (((b - r) / delta) + 2.0)
    else:
        h = 60.0 * (((r - v) / delta) + 4.0)
    return h % 360.0, s


def famille(h):
    for nom, debut, fin in FAMILLES:
        if debut > fin:                    # la famille qui enjambe 0°
            if h >= debut or h < fin:
                return nom
        elif debut <= h < fin:
            return nom
    return "rouge"


def mesurer(chemin):
    petit = reduire(chemin)
    try:
        larg, haut, px = lire_png(petit)
    finally:
        os.unlink(petit)

    lum = []
    somme_sat, poids_sat = 0.0, 0.0
    masses = {nom: 0 for nom, _, _ in FAMILLES}
    comptes = 0
    brule = 0

    for i in range(0, len(px), 3):
        r, v, b = px[i], px[i + 1], px[i + 2]
        # Luminance de Rec. 601. Le choix ne change rien aux conclusions —
        # vérifié contre Rec. 709 sur les huit vues : la médiane bouge de moins
        # d'un niveau — mais il faut en fixer une pour que deux mesures se
        # comparent.
        y = 0.299 * r + 0.587 * v + 0.114 * b
        lum.append(y)
        if y > 200:
            brule += 1
        h, s = teinte(r, v, b)
        somme_sat += s * y
        poids_sat += y
        if y >= CLAIR_MINI and s >= SATURE_MINI:
            masses[famille(h)] += 1
            comptes += 1

    lum.sort()
    n = len(lum)
    q1, med, q3 = lum[n // 4], lum[n // 2], lum[(3 * n) // 4]
    return {
        "clair": med,
        "q1": q1,
        "q3": q3,
        "contraste": q3 - q1,
        "brule": 100.0 * brule / n,
        "couleur": (somme_sat / poids_sat) if poids_sat else 0.0,
        "colores": 100.0 * comptes / n,
        "teintes": {k: (100.0 * v / comptes if comptes else 0.0) for k, v in masses.items()},
    }


# LES CONSIGNES, LUES SUR LA PHOTO DE RÉFÉRENCE FOURNIE PAR LE PROPRIÉTAIRE.
#
# Ce ne sont pas des goûts : chacune est ce que la photo montre et que le rendu
# de 17.1.0 ne montrait pas. Les valeurs de départ, mesurées sur les huit vues
# nommées avant tout changement, sont rappelées entre parenthèses — c'est ce qui
# rend la consigne discutable plutôt qu'assénée.
#
#   clair      25 à 85   la salle est sombre, pas noire — ET PAS ÉCLAIRÉE.
#   brule      <= 3 %    on voit les sources SANS que leur cœur écrête.
#   contraste  42 à 68   les masses noires restent, sans durcir. (départ : 22 à 61)
#   couleur    >= 0.35   la lumière est colorée. (départ : 0,17 à 0,24)
#   rose+cyan  >= 30     le néon existe. (départ : 4 à 9)
#   ambre      <= 22 %   et il n'est pas noyé sous la lumière chaude.
#
# Les valeurs ci-dessus viennent des pixels de la photo. Le bloc qui suit dit
# lesquelles sont opposables et lesquelles ne le sont pas.
#
# Le « rose + cyan » est la consigne qui porte tout le reste : c'est elle qu'on
# ne pouvait pas atteindre en montant simplement l'exposition, et c'est donc
# elle qui force à poser des sources au lieu de tourner un bouton.
#
# LES BORNES HAUTES ONT ÉTÉ AJOUTÉES APRÈS COUP, et il faut dire pourquoi
# plutôt que de faire comme si elles avaient toujours été là. La première
# version n'imposait que des minimums, et c'était un défaut de l'instrument :
# une consigne « au moins 55 » est satisfaite par une salle à 93, c'est-à-dire
# par exactement le supermarché que la description de la salle dit ne pas
# vouloir. Le premier lot de néon l'a montré tout de suite — trois vues sur
# huit sont montées entre 88 et 93, dont le couloir d'entrée à 93 pour 0,0 % de
# rose et de cyan. La photo de référence est une pièce SOMBRE traversée de
# sources vives ; « le plus clair possible » n'en est pas une lecture.
#
# CE BLOC A ÉTÉ RÉÉCRIT LE 6 SEPTEMBRE 2026 : ON A ENFIN LES PIXELS.
#
# La version précédente se terminait par « les deux bornes hautes sont
# constatées, pas mesurées […] pas d'une lecture de la photo de référence — dont
# on n'a que l'image, jamais les pixels d'origine ». Cette phrase n'est plus
# vraie. La photo est maintenant dans le dépôt, `docs/ambiance-reference.png`,
# et la commande qui suit reproduit chacun des nombres de ce bloc :
#
#     python3 tools/ambiance.py docs/ambiance-reference.png
#
# C'est la capture que le propriétaire a jointe, recadrée pour retirer le cadre
# de la fenêtre (x=36, y=44, 958 x 538 dans l'original de 1024 x 628) : mesurer
# la bordure du bureau ferait mentir la médiane de 6,5 points vers le bas —
# 30,7 avec le cadre, 37,2 sans. Le réencodage PNG est sans perte, vérifié : les
# cinq mesures sont identiques avant et après.
#
# CE QUI DÉPEND DU CADRAGE ET CE QUI N'EN DÉPEND PAS. C'est la distinction que
# la version précédente n'avait pas les moyens de faire, et elle change les
# consignes. Cinq découpes de la référence — l'image entière, ses deux moitiés
# verticales, ses deux moitiés horizontales :
#
#              entière  gauche  droite  haut   bas    amplitude
#   clair       37,2     30,4    40,2   44,8   27,0   x 1,7
#   brule        0,98     2,28    0,98   2,53   0,01  x 253
#   contraste   50,9     46,2    56,2   51,2   48,1   x 1,2
#   couleur      0,411    0,378   0,437  0,451  0,354 x 1,3
#
# `clair` et `brule` bougent avec ce qu'on cadre : une moitié basse sans source
# lumineuse tombe à 0,01 % d'écrêtage. Le même rendu du jeu, mêmes réglages,
# donne 38,3 de médiane à un poste de borne et 79,7 à un autre. En faire des
# consignes revenait à noter le cadreur, pas la salle — et c'est exactement ce
# que faisait la borne basse « brule >= 2 % » : elle RÉCOMPENSAIT l'écrêtage.
#
# `contraste` et `couleur`, eux, tiennent à 20 et 30 % près sur les cinq
# découpes. Ce sont eux qui décrivent la salle, et ce sont eux qui condamnent le
# rendu du 6 septembre : contraste 100,4 et 102,2 sur deux cadrages, contre 46 à
# 56 pour la référence ; couleur 0,283 et 0,306 contre 0,354 au plus bas de la
# référence. Le rendu est deux fois plus contrasté et un tiers moins coloré, et
# les deux vont ensemble — un pixel poussé au blanc perd sa teinte, donc écrêter
# désature.
#
# LA SIGNATURE DE TEINTE, elle, est stable sur les cinq découpes, et c'est la
# consigne la plus directement actionnable :
#
#              rose      rouge     ambre     cyan     vert
#   référence  27 à 50   17 à 38   13 à 20   4 à 7    0,3 à 1,7
#   le rendu   12 à 21    5 à 18   27 à 52   3 à 24   0,6 à 27
#
# La référence est ROSE ; le rendu est AMBRE, et il pousse jusqu'à 27 % de vert
# là où la référence n'en a pas. D'où la borne haute sur l'ambre : elle ne dit
# pas « moins de lumière chaude », elle dit « pas de lumière chaude DOMINANTE ».
#
# Les tolérances autour de la référence sont, elles, un choix, et il est dit :
# les bornes sont l'amplitude constatée sur les cinq découpes, élargie d'environ
# 10 %. Un seul cliché ne fait pas une loi ; il fait une cible autrement mieux
# fondée que l'œil de celui qui règle.
CIBLES = (
    # Cadrage-dépendant : bornes très larges, présentes seulement pour attraper
    # l'accident — une salle noire ou un supermarché. Ne pas les resserrer.
    ("clair",     25.0,  85.0, "médiane de luminance (dépend du cadrage)"),
    ("brule",      0.0,   3.0, "part au-dessus de 200 (référence : 0,01 à 2,53)"),
    # Descriptifs de la salle : stables au cadrage, donc opposables.
    ("contraste", 42.0,  68.0, "écart interquartile (référence : 46,2 à 56,2)"),
    ("couleur",    0.35, None, "saturation pondérée (référence : 0,354 à 0,451)"),
    ("neon",      30.0,  None, "masse rose + cyan (référence : 33,7 à 54,2)"),
    ("ambre",      0.0,  22.0, "part ambre (référence : 13,3 à 20,1)"),
)


def valeur(m, cle):
    if cle == "neon":
        return m["teintes"]["rose"] + m["teintes"]["cyan"]
    # `ambre` est une famille de teintes et non une mesure de premier niveau :
    # sans ce détour, la consigne lirait `m["ambre"]` et lèverait un KeyError.
    if cle in m["teintes"]:
        return m["teintes"][cle]
    return m[cle]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    juger = "--cible" in sys.argv
    if not args:
        print(__doc__.strip().splitlines()[-2].strip())
        return 2

    echecs = 0
    for chemin in args:
        m = mesurer(chemin)
        t = m["teintes"]
        print(f"\n{os.path.basename(chemin)}")
        print(f"  clair {m['clair']:5.1f}   contraste {m['contraste']:5.1f}"
              f"   brule {m['brule']:5.2f}%   couleur {m['couleur']:.3f}"
              f"   colores {m['colores']:5.1f}%")
        print("  teintes  " + "  ".join(f"{k} {t[k]:4.1f}%" for k, _, _ in FAMILLES))
        if juger:
            for cle, bas, haut, quoi in CIBLES:
                v = valeur(m, cle)
                if v < bas:
                    verdict, borne = "SOUS", f"< {bas:5.2f}"
                elif haut is not None and v > haut:
                    verdict, borne = "SUR ", f"> {haut:5.2f}"
                else:
                    verdict = "OK  "
                    borne = (f"dans [{bas:.2f} ; {haut:.2f}]" if haut is not None
                             else f">= {bas:5.2f}")
                echecs += 0 if verdict == "OK  " else 1
                print(f"    {verdict} {cle:10s} {v:6.2f} {borne:20s} {quoi}")
    return 1 if echecs else 0


if __name__ == "__main__":
    sys.exit(main())
