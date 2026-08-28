"""
borne.py — la borne d'arcade, construite sous Blender.

    /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python assets/blender/borne.py -- --out assets/models/borne/borne.gltf

Pourquoi un SCRIPT et pas un .blend
-----------------------------------
Un .blend est un binaire opaque : on ne peut ni le relire, ni voir ce qui a
changé entre deux versions, ni corriger une cote sans rouvrir Blender. Toutes
les cotes de la borne sont ici, nommées, avec la mesure d'où elles sortent. Le
.gltf exporté est versionné comme un résultat de build — reconstructible à tout
moment par la ligne ci-dessus.

Le modèle de référence, et pourquoi on ne l'importe pas
------------------------------------------------------
La référence est https://free3d.com/fr/3d-model/arcade-cabinet-5523.html.
Fait vérifié : ce modèle **coûte 19 $ et sa licence est « Royalty Free —
Editorial only »**. « Editorial only » interdit l'usage dans un produit ; il
n'est donc pas distribuable dans un jeu, même acheté. On le REPRODUIT à partir
de ses images publiques, on ne l'importe pas. Aucun octet du modèle payant
n'entre dans ce dépôt.

D'où viennent les cotes
-----------------------
Douze vues de référence (dont une filaire et une arrière) ont été mesurées au
pixel : pour chaque ligne de l'image, le premier et le dernier pixel SATURÉ
(le caisson est bleu/magenta, le fond est blanc, l'ombre est grise — la
saturation les sépare proprement). La vue de profil donne 918 px du sol au
sommet, convertis par la hauteur hors tout.

Les cotes marquées « mesurée » sortent de là. Celles marquées « contrainte »
sont imposées par la salle et n'étaient pas négociables :

  - la LARGEUR (0,72) et la PROFONDEUR (0,88) sont celles que les dix-neuf
    bornes occupent déjà dans `salle.room.json`. Les changer déplacerait la
    salle entière et ferait mentir le contrôle de chevauchement de `roomgen`.
    La référence est proportionnellement plus élancée (rapport hauteur/
    profondeur 3,05 contre 2,54 ici) : c'est la FORME qui est reprise, pas
    l'élancement.
  - la HAUTEUR DU PANNEAU DE COMMANDE est tenue à moins d'un centimètre de
    celle d'avant. La référence la place à 1,02 m ; l'ajuster librement aurait
    défait le réglage de hauteur de B14b et déplacé la pose des bras de 4 cm.

Le repère
---------
Blender est Z-haut ; le moteur est Y-haut avec +Z vers l'avant de la borne.
L'export glTF applique (X, Y, Z) -> (X, Z, -Y). On construit donc ici avec
+X = largeur, -Y = AVANT, +Z = haut, et `av(z)` fait la conversion en un seul
endroit. Le déterminant de cette rotation vaut +1 : pas de miroir, ce que
`geo_import` refuserait.
"""

import bpy
import bmesh
import json
import math
import os
import sys

# =====================================================================
# Les cotes
# =====================================================================

W = 0.72          # largeur hors tout            (contrainte : la salle)
D = 0.88          # profondeur hors tout          (contrainte : la salle)
H = 1.88          # hauteur au sommet du marquee  (contrainte : la salle)
HW = W * 0.5
HD = D * 0.5

# Le jonc de chant. Un T-molding réel fait 3/4" (19 mm) de large, donc un
# bourrelet de 9,5 mm de rayon. C'est LE détail qui manquait le plus : sur la
# référence il court sur tous les chants du caisson et c'est la première chose
# qu'on reconnaît d'une borne d'arcade.
TM_R = 0.0095
TM_SEG = 3        # segments de l'arrondi : 3 suffisent à 30 cm de l'oeil

# Le chanfrein général. Une arête vive à 30 cm de l'oeil est exactement ce qui
# fait « modèle 3D pas fini » ; 3 mm suffisent à accrocher un liseré de lumière.
BEVEL = 0.003
BEVEL_SEG = 2
BEVEL_ANGLE = math.radians(35.0)

SMOOTH_ANGLE = math.radians(38.0)

# --- le profil, mesuré ------------------------------------------------
#
# Toutes les cotes ci-dessous sont en mètres dans le repère de la borne :
# z positif vers l'AVANT, y vers le haut, origine au sol au centre.

PLINTHE_H = 0.10          # la plinthe noire, en retrait au sol
# Le dessous de la plinthe ne touche pas tout à fait le sol.
#
# Le tapis technique fait 8 mm d'épaisseur, et six bornes sont posées dessus :
# une plinthe qui descend à zéro MORD dedans, et le contrôle de chevauchement de
# `roomgen` le refuse — à raison, c'est son travail. La borne paramétrique s'en
# tirait par accident, sa boîte de socle étant construite sans face inférieure
# et biseautée de 6 mm, ce qui lui laissait juste assez de garde pour passer
# sous la tolérance de 5 mm. Autant l'écrire.
#
# 6 mm ne se voient pas : la plinthe est en retrait de 3 cm et dans son ombre.
PLINTHE_Y0 = 0.006
Z_FACE_BAS = 0.300        # mesurée : la face avant basse, verticale

# La gorge. Sous le panneau de commande la face avant ne fait PAS un
# décrochement droit : elle remonte en courbe concave puis convexe — une doucine.
# Mesures (hauteur -> avancée) : 0,719->0,300  0,760->0,310  0,801->0,349
# 0,842->0,406  0,883->0,428. Rapportée à t dans [0,1] cette suite vaut
# 0,016 0,078 0,234 0,383 0,633 0,828 0,945 : c'est un smootherstep
# (6t^5-15t^4+10t^3), à quelques centièmes près, et non une droite.
GORGE_Y0 = 0.720
GORGE_Y1 = 0.883
GORGE_Z1 = 0.428
GORGE_N = 9               # échantillons de la doucine

Z_NEZ = 0.434             # mesurée : le nez du panneau, le point le plus avancé
Y_NEZ = 0.972             # ajustée : voir « contrainte » ci-dessus

# Le panneau de commande. La référence le donne à 23° de l'horizontale ; on
# retient 19°, ce qui pose sa surface à 0,99 m sous le manche — la hauteur
# d'avant, à 4 mm près.
Z_PANNEAU_FOND = 0.185
Y_PANNEAU_FOND = 1.052

# La dalle et son cadre. Mesurée : la face recule de 0,192 à 0,054 entre
# 1,108 et 1,457, soit 21,6° depuis la verticale.
Z_CADRE_BAS = 0.170
Y_CADRE_BAS = 1.098
Z_ECRAN_HAUT = 0.050
Y_ECRAN_HAUT = 1.430
ECRAN_INCL = math.radians(21.6)

# La casquette haut-parleurs. Elle SURPLOMBE : mesurée, elle repart en avant de
# 0,054 à 0,207 entre 1,457 et 1,579. Le modèle paramétrique la faisait pencher
# dans l'autre sens (0,04 -> -0,04), ce qui donnait au haut de la borne un
# profil de pupitre au lieu d'une casquette — c'est une des raisons pour
# lesquelles elle se lisait comme une armoire.
Z_HP_HAUT = 0.205
Y_HP_HAUT = 1.565

# Le marquee. Mesurée : face avant à 0,360, de 1,590 à 1,780, soit 19 cm de
# haut. Il n'est PAS une boîte rapportée : les deux flancs montent de part et
# d'autre et c'est leur chant qui l'encadre — d'où le jonc magenta qui fait le
# tour de l'enseigne sur les vues de référence.
Z_MARQUEE = 0.360
Y_MARQUEE_BAS = 1.590
Y_MARQUEE_HAUT = 1.780
Z_SOMMET = 0.300
Y_SOMMET = 1.850

# Le dessus redescend vers l'arrière. La référence donne 25° ; on retient 12,7°
# (arrière à 1,70 m) parce qu'à 25° sur une profondeur de 0,88 le dos tomberait
# à 1,61 m et la borne perdrait un quart de son volume arrière — la référence
# est plus profonde que ce que la salle autorise.
Y_DOS = 1.700

# --- la dalle d'écran -------------------------------------------------
# `roomgen` la pose lui-même : elle reçoit la texture de la partie au runtime et
# doit rester un `geo_panel` d'un matériau cloné par borne. Le script ne fait
# que dire OÙ, pour que la cote ne soit pas écrite deux fois.
ECRAN_W = 0.62
ECRAN_H = 0.349           # 16:9, comme la cible de rendu 512x288
ECRAN_Y = 1.290

# --- la quincaillerie -------------------------------------------------
PORTE_W = 0.250           # mesurée : 103 px de 0,00258 m
PORTE_H = 0.550           # mesurée : 276 px
PORTE_Y = 0.130           # bas de la porte
PORTE_EP = 0.020          # en SAILLIE sur la face : mesurée à l'ombre portée

MANCHE_DX = 0.155         # écartement des deux postes depuis l'axe
BOULE_R = 0.021

BOUTON_R = 0.0165
BOUTON_PAS_X = 0.052
BOUTON_PAS_Z = 0.052

# =====================================================================
# Les matériaux
#
# Séparés et nommés pour que la salle les pilote : la couleur vient de
# `salle.room.json`, pas d'ici. C'est ce qui permet aux dix-neuf bornes de
# garder leur teinte par jeu avec un seul maillage.
#
# L'ORDRE compte : il est écrit tel quel dans `borne.ancres.json` et c'est par
# ce tableau que `roomgen` associe chaque primitive du glTF à un matériau de la
# salle. Ajouter un matériau se fait EN FIN DE LISTE.
# =====================================================================

MATERIAUX = [
    "caisson",      # 0  la laque du meuble, teintée par jeu
    "flanc",        # 1  les deux bouchons de l'extrusion : la sérigraphie
    "tmolding",     # 2  le jonc de chant
    "noir",         # 3  bandeau, cadre d'écran, casquette
    "grille",       # 4  les grilles de haut-parleur, et le chrome
    "panneau",      # 5  le stratifié du panneau de commande
    "marquee",      # 6  l'enseigne
    "monnayeur",    # 7  la tôle de la porte à monnaie
    "bouton_a",     # 8
    "bouton_b",     # 9
    "manche_bleu",  # 10 le poste de gauche
    "manche_rouge", # 11 le poste de droite
    "chrome",       # 12 tige de manche, rondelle, serrures
]
MI = {nom: i for i, nom in enumerate(MATERIAUX)}


def av(z):
    """Avant de la borne -> Y de Blender. L'export glTF renverra +Z."""
    return -z


# =====================================================================
# Petits utilitaires de maillage
# =====================================================================

_pieces = []


def piece(nom, verts, faces, mats, poids=None):
    """Ajoute une pièce. `mats` donne un indice de matériau par face ;
    `poids` (facultatif) un poids de chanfrein par arête, désigné par le
    couple de sommets — c'est ce qui marque les chants qui reçoivent le jonc."""
    me = bpy.data.meshes.new(nom)
    me.from_pydata(verts, [], faces)
    me.update()
    # Les DOUZE emplacements, dans le même ordre sur chaque pièce. C'est ce qui
    # fait que la fusion conserve les indices : `join()` réassocie les faces par
    # emplacement, et une pièce sans matériau voit tous ses indices retomber à
    # zéro — c'est-à-dire une borne d'une seule couleur.
    for nm in MATERIAUX:
        me.materials.append(bpy.data.materials[nm])
    for i, f in enumerate(me.polygons):
        f.material_index = mats[i] if isinstance(mats, list) else mats
    ob = bpy.data.objects.new(nom, me)
    bpy.context.collection.objects.link(ob)
    if poids:
        attr = me.attributes.new("bevel_weight_edge", "FLOAT", "EDGE")
        want = {frozenset(p) for p in poids}
        for i, e in enumerate(me.edges):
            attr.data[i].value = 1.0 if frozenset(e.vertices) in want else 0.0
    _pieces.append(ob)
    return ob


def boite(nom, cx, cy, cz, sx, sy, sz, mat):
    """Une boîte centrée, en cotes de borne (cz vers l'avant)."""
    hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
    v = []
    for sgz in (-1, 1):
        for sgy in (-1, 1):
            for sgx in (-1, 1):
                v.append((cx + sgx * hx, av(cz + sgz * hz), cy + sgy * hy))
    f = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
         (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return piece(nom, v, f, mat)


def cylindre(nom, cx, cy, cz, r0, r1, h, mat, seg=12, pitch=0.0, chapeau=True,
             fond=True):
    """Un cylindre debout (axe Y), éventuellement basculé de `pitch` autour de X
    — les pastilles et les manches suivent la pente du panneau."""
    v, f = [], []
    cp, sp = math.cos(pitch), math.sin(pitch)

    def pose(px, py, pz):
        # rotation autour de X : (y, z) -> (y cos - z sin, y sin + z cos)
        ry = py * cp - pz * sp
        rz = py * sp + pz * cp
        return (cx + px, av(cz + rz), cy + ry)

    for k in range(seg):
        a = 2.0 * math.pi * k / seg
        ca, sa = math.cos(a), math.sin(a)
        v.append(pose(r0 * ca, 0.0, r0 * sa))
    for k in range(seg):
        a = 2.0 * math.pi * k / seg
        ca, sa = math.cos(a), math.sin(a)
        v.append(pose(r1 * ca, h, r1 * sa))
    for k in range(seg):
        n = (k + 1) % seg
        f.append((k, n, seg + n, seg + k))
    if chapeau:
        f.append(tuple(range(seg, 2 * seg)))
    if fond:
        f.append(tuple(range(seg - 1, -1, -1)))
    return piece(nom, v, f, mat, None)


def revolution(nom, cx, cy, cz, profil, mat, seg=12):
    """Fait tourner un profil (r, y) autour de l'axe Y. Le profil est CONTINU :
    les normales se lissent au seuil d'angle et la boule n'a pas les trois
    anneaux qu'un empilement de troncs de cône lui donnait."""
    n = len(profil)
    v, f = [], []
    for k in range(seg):
        a = 2.0 * math.pi * k / seg
        ca, sa = math.cos(a), math.sin(a)
        for (r, y) in profil:
            v.append((cx + r * ca, av(cz + r * sa), cy + y))
    for k in range(seg):
        kn = (k + 1) % seg
        for i in range(n - 1):
            a0 = k * n + i
            a1 = k * n + i + 1
            b0 = kn * n + i
            b1 = kn * n + i + 1
            f.append((a0, a1, b1, b0))
    return piece(nom, v, f, mat, None)


# =====================================================================
# Le profil du caisson
# =====================================================================

def smootherstep(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def profil_caisson():
    """La silhouette, du pied avant au pied arrière, en (z, y, matériau du
    segment qui PART de ce point). Le dernier segment referme le polygone."""
    p = [(Z_FACE_BAS, PLINTHE_H, "caisson"),
         (Z_FACE_BAS, GORGE_Y0, "caisson")]

    # La doucine. Concave puis convexe : c'est elle qui remplace le
    # décrochement droit d'avant, et c'est le second défaut de silhouette que
    # les images reprochaient à la borne paramétrique.
    for k in range(1, GORGE_N + 1):
        t = k / float(GORGE_N)
        y = GORGE_Y0 + (GORGE_Y1 - GORGE_Y0) * t
        z = Z_FACE_BAS + (GORGE_Z1 - Z_FACE_BAS) * smootherstep(t)
        p.append((z, y, "caisson"))

    p += [
        (Z_NEZ, Y_NEZ, "panneau"),                    # le nez, puis la tôle
        (Z_PANNEAU_FOND, Y_PANNEAU_FOND, "noir"),     # -> lèvre du cadre
        (Z_CADRE_BAS, Y_CADRE_BAS, "noir"),           # -> face du cadre d'écran
        (Z_ECRAN_HAUT, Y_ECRAN_HAUT, "noir"),         # -> la casquette
        (Z_HP_HAUT, Y_HP_HAUT, "noir"),               # -> le dessous du marquee
        (Z_MARQUEE, Y_MARQUEE_BAS, "marquee"),        # -> la face du marquee
        (Z_MARQUEE, Y_MARQUEE_HAUT, "noir"),          # -> le chanfrein du sommet
        (Z_SOMMET, Y_SOMMET, "caisson"),              # -> le dessus, vers l'arrière
        (-HD, Y_DOS, "caisson"),                      # -> le dos, vertical
        (-HD, PLINTHE_H, "caisson"),                  # -> le dessous (caché)
    ]
    return p


def caisson():
    """L'extrusion du profil sur la largeur. Les deux bouchons SONT les deux
    flancs, et un flanc de borne porte une sérigraphie que le caisson n'a pas :
    ils reçoivent donc leur propre matériau."""
    p = profil_caisson()
    n = len(p)
    verts = []
    for (z, y, _m) in p:
        verts.append((-HW, av(z), y))
    for (z, y, _m) in p:
        verts.append((HW, av(z), y))

    faces, mats = [], []
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
        mats.append(MI[p[i][2]])

    # Les bouchons. Le dernier segment (le dessous) est caché par la plinthe.
    faces.append(tuple(range(n - 1, -1, -1)))
    mats.append(MI["flanc"])
    faces.append(tuple(range(n, 2 * n)))
    mats.append(MI["flanc"])

    # Les chants qui portent le jonc : tout le pourtour des deux flancs SAUF le
    # dessous, qui est sous la plinthe et où aucune borne réelle n'en met.
    poids = []
    for i in range(n - 1):
        poids.append((i, i + 1))
        poids.append((n + i, n + i + 1))

    return piece("caisson", verts, faces, mats, poids)


# =====================================================================
# Les pièces rapportées
# =====================================================================

def plinthe():
    """En retrait de 3 cm : c'est l'ombre de ce retrait qui fait qu'une borne
    « pose » sur le sol au lieu d'y être posée."""
    boite("plinthe", 0.0, (PLINTHE_Y0 + PLINTHE_H) * 0.5,
          (Z_FACE_BAS - HD) * 0.5 - 0.015,
          W - 0.06, PLINTHE_H - PLINTHE_Y0, (Z_FACE_BAS + HD) - 0.06,
          MI["noir"])


def grilles_hp():
    """Les deux grilles rondes, en cercles CONCENTRIQUES.

    Ce qui fait lire une grille n'est pas sa couleur — elle est noire sur du
    noir — mais son MÉTAL : elle accroche une lumière que la plaque mate
    absorbe. D'où un matériau à part, et des anneaux en relief plutôt qu'une
    texture, parce qu'à 60 cm de l'oeil la silhouette des anneaux se voit.
    """
    # La casquette court de (Z_ECRAN_HAUT, Y_ECRAN_HAUT) à (Z_HP_HAUT, Y_HP_HAUT).
    dz = Z_HP_HAUT - Z_ECRAN_HAUT
    dy = Y_HP_HAUT - Y_ECRAN_HAUT
    lg = math.hypot(dz, dy)
    # La normale de la casquette, tournée vers l'avant et le haut.
    nz, ny = dy / lg, -dz / lg
    pitch = math.atan2(nz, ny)
    cz = (Z_ECRAN_HAUT + Z_HP_HAUT) * 0.5
    cy = (Y_ECRAN_HAUT + Y_HP_HAUT) * 0.5

    for cote in (-1, 1):
        x = cote * 0.185
        # Quatre anneaux concentriques, 16 côtés. Sur la référence on en compte
        # six ou sept ; quatre se lisent pareil à un mètre et coûtent 40 % de
        # moins — la salle en porte trente-huit.
        #
        # Ce sont des BANDES OUVERTES, pas des disques : quatre disques pleins
        # empilés au même endroit ne montrent que le plus grand, et la grille
        # redevient la plaque morte qu'on lui reprochait. Sans fond ni chapeau,
        # chaque anneau est une nervure de 3,5 mm qui accroche sa propre
        # lumière — c'est ce relief qui fait lire les cercles concentriques.
        for k in range(4):
            r = 0.017 + k * 0.011
            cylindre("grille_%d_%d" % (cote, k),
                     x, cy + ny * 0.002, cz + nz * 0.002,
                     r, r, 0.0035, MI["grille"], seg=16, pitch=pitch,
                     chapeau=False, fond=False)


def enseigne():
    """L'enseigne, en saillie de 6 mm sur la face du marquee. Le caisson lumineux
    n'est plus une boîte rapportée : il fait partie du profil, et ce sont les
    deux flancs qui l'encadrent — comme sur une vraie borne."""
    cy = (Y_MARQUEE_BAS + Y_MARQUEE_HAUT) * 0.5
    h = (Y_MARQUEE_HAUT - Y_MARQUEE_BAS) - 0.030
    boite("enseigne", 0.0, cy, Z_MARQUEE + 0.003, W - 0.055, h, 0.006,
          MI["marquee"])


def porte_monnayeur():
    """La porte à monnaie, EN SAILLIE sur la face basse.

    Mesurée sur la vue de face : un cadre de 0,25 x 0,55 qui occupe presque
    toute la face avant basse, avec en haut deux fentes à insert ROUGE, à
    droite un bouton de retour chromé, au milieu deux trappes de renvoi
    carrées, et en dessous la porte de caisse et sa serrure.

    Ce qui fait lire une porte à monnaie à deux mètres n'est pas sa forme mais
    son CHROME : les pièces brillantes accrochent la lumière quand le noir
    l'absorbe.
    """
    zf = Z_FACE_BAS
    cy = PORTE_Y + PORTE_H * 0.5

    # Le cadre en saillie.
    boite("porte", 0.0, cy, zf + PORTE_EP * 0.5, PORTE_W, PORTE_H, PORTE_EP,
          MI["monnayeur"])
    zs = zf + PORTE_EP        # la face de la porte

    # Le bloc monnayeur : les deux fentes, en haut.
    y_fente = PORTE_Y + PORTE_H - 0.064
    boite("bloc_fentes", 0.0, y_fente, zs + 0.004, 0.150, 0.070, 0.008,
          MI["monnayeur"])
    for cote in (-1, 1):
        x = cote * 0.038
        # L'insert rouge, et la fente noire à sa gauche.
        boite("insert_%d" % cote, x + 0.006, y_fente, zs + 0.010,
              0.034, 0.048, 0.006, MI["bouton_a"])
        boite("fente_%d" % cote, x - 0.017, y_fente, zs + 0.010,
              0.005, 0.044, 0.006, MI["noir"])

    # Les deux trappes de renvoi, au milieu.
    for cote in (-1, 1):
        boite("renvoi_%d" % cote, cote * 0.026, PORTE_Y + PORTE_H - 0.185,
              zs + 0.002, 0.044, 0.044, 0.004, MI["noir"])

    # La porte de caisse, en dessous.
    boite("caisse", 0.0, PORTE_Y + 0.135, zs + 0.002,
          PORTE_W - 0.030, 0.250, 0.005, MI["monnayeur"])

    # Les deux serrures chromées : ce sont elles qu'on voit.
    for (yy, rr) in ((PORTE_Y + PORTE_H - 0.105, 0.007),
                     (PORTE_Y + 0.075, 0.007)):
        cylindre("serrure_%.2f" % yy, 0.088, yy, zs + 0.004, rr, rr, 0.005,
                 MI["chrome"], seg=10, pitch=math.radians(90.0))


def panneau_y(z):
    """La hauteur de la surface du panneau de commande à l'avancée `z`.

    Elle est CALCULÉE depuis les deux bouts du segment plutôt qu'écrite à la
    main : c'est la seule forme qui reste juste quand on retouche la pente, et
    c'est de cette même fonction que sortent les ancres du manche et des
    boutons — ils ne peuvent donc pas flotter au-dessus de la tôle ni s'y
    enfoncer, ce qui est arrivé aux deux versions précédentes.
    """
    t = (Z_NEZ - z) / (Z_NEZ - Z_PANNEAU_FOND)
    return Y_NEZ + (Y_PANNEAU_FOND - Y_NEZ) * t


PANNEAU_PITCH = math.atan2(Y_PANNEAU_FOND - Y_NEZ, Z_NEZ - Z_PANNEAU_FOND)


def commandes():
    """Deux postes de jeu : deux manches à boule et deux blocs de six boutons.

    La borne paramétrique n'avait qu'un manche et quatre pastilles. La
    référence en montre deux de chaque côté, et c'est ce qui dit qu'on joue à
    DEUX sur une borne d'arcade.
    """
    ancres = {}

    # Pas de tôle rapportée : le stratifié du panneau EST le segment du profil
    # qui va du nez au fond, et ce segment porte déjà le matériau « panneau ».
    # Une plaque en plus ferait double emploi — et la première version la posait
    # à plat avant de la faire basculer par la rotation de l'objet, c'est-à-dire
    # autour de l'origine du monde et non de la plaque : elle partait à un mètre
    # devant la borne, en travers. Ce qui est déjà dans le profil se décrit dans
    # le profil.
    z_manche = Z_NEZ - 0.042
    for (cote, mat) in ((-1, "manche_bleu"), (1, "manche_rouge")):
        x = cote * MANCHE_DX
        y0 = panneau_y(z_manche)
        # La rondelle anti-poussière, à demi enfoncée : 6 mm sous la surface,
        # ce qui la fait tenir au lieu de flotter.
        #
        # 24 mm de rayon, et c'est une correction : elle en faisait 34, soit un
        # disque de 68 mm quand la boule qui le coiffe en fait 42. Dans la
        # pénombre de l'allée les deux se confondaient en une seule masse
        # sombre, et le manche se lisait comme un champignon posé sur la tôle.
        # Sur la référence la rondelle est un simple jonc sous la boule.
        cylindre("rondelle_%d" % cote, x, y0 - 0.006, z_manche,
                 0.024, 0.021, 0.007, MI["chrome"], pitch=PANNEAU_PITCH)
        # La tige est CHROMÉE sur la référence, pas de la couleur de la boule —
        # et pas non plus du métal sombre des grilles, où elle disparaissait.
        cylindre("tige_%d" % cote, x, y0 + 0.000, z_manche,
                 0.009, 0.008, 0.062, MI["chrome"], pitch=PANNEAU_PITCH,
                 chapeau=False)
        # La boule : un demi-cercle continu, très légèrement aplati en bas là
        # où elle coiffe sa tige — une boule d'arcade est moulée sur son insert.
        prof = []
        for k in range(7):
            a = -math.pi * 0.5 + math.pi * (k / 6.0)
            prof.append((math.cos(a) * BOULE_R,
                         math.sin(a) * BOULE_R * (0.86 if math.sin(a) < 0 else 1.0)))
        yb = y0 + 0.062 + BOULE_R * 0.86
        revolution("boule_%d" % cote, x, yb, z_manche, prof, MI[mat])
        if cote < 0:
            # Le SOMMET de la boule : le point qu'on touche, comme `panneau`
            # désigne le dessus des pastilles et `coin` la fente. La pose des
            # bras y ajoute la paume — c'est elle qui sait de quelle longueur
            # est une main.
            ancres["stick"] = [x, yb + BOULE_R, z_manche]

    # Les boutons : deux rangées de trois par poste, décalées comme sur la
    # référence. Ils sont VIFS : quatre pastilles bordeaux sur un panneau
    # bordeaux étaient invisibles, et un bouton d'arcade a précisément pour
    # fonction de se trouver sans être cherché.
    # Les deux blocs partent à DROITE de leur manche, pas en miroir l'un de
    # l'autre : sur la référence les six pastilles du poste de gauche sont à
    # droite de la boule bleue, exactement comme celles du poste de droite le
    # sont de la boule rouge. C'est la main droite qui appuie, dans les deux cas.
    sx = sy = sz = 0.0
    n = 0
    for cote in (-1, 1):
        base = cote * MANCHE_DX + 0.075
        for rang in range(2):
            z = Z_NEZ - 0.030 - rang * BOUTON_PAS_Z
            for col in range(3):
                x = base + col * BOUTON_PAS_X + rang * 0.012
                y = panneau_y(z)
                mat = "bouton_b" if (rang == 0 and col == 1) else "bouton_a"
                cylindre("bouton_%d_%d_%d" % (cote, rang, col),
                         x, y - 0.003, z, BOUTON_R, BOUTON_R * 0.85, 0.012,
                         MI[mat], pitch=PANNEAU_PITCH)
                # L'ancre désigne le bloc du poste de GAUCHE : c'est celui dont
                # le manche porte l'ancre `stick`. Les deux mains du joueur
                # doivent tomber sur le MÊME poste — main gauche au manche,
                # main droite aux pastilles.
                if cote < 0:
                    sx += x
                    sy += y
                    sz += z
                    n += 1
    # Le doigt touche le DESSUS des pastilles, pas leur centre.
    ancres["panel"] = [sx / n, sy / n + 0.012, sz / n]
    return ancres


# =====================================================================
# Assemblage
# =====================================================================

def assembler():
    ob = _pieces[0]
    bpy.ops.object.select_all(action="DESELECT")
    for p in _pieces:
        p.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = "borne"

    # Les transformations locales (la tôle inclinée) entrent dans le maillage.
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    # 1) Le jonc de chant. Un modificateur de biseau en mode POIDS ne touche que
    #    les arêtes marquées — le pourtour des deux flancs — et donne aux faces
    #    créées son propre matériau. C'est exactement un T-molding : un
    #    bourrelet demi-rond de 9,5 mm sur le chant, et il reste DANS l'emprise
    #    de la borne au lieu de l'élargir, ce qui ferait mentir le contrôle de
    #    chevauchement de la salle.
    m = ob.modifiers.new("tmolding", "BEVEL")
    m.limit_method = "WEIGHT"
    m.width = TM_R
    m.segments = TM_SEG
    m.material = MI["tmolding"]
    m.harden_normals = False

    # 2) Le chanfrein général, sur tout ce qui reste vif.
    m = ob.modifiers.new("chanfrein", "BEVEL")
    m.limit_method = "ANGLE"
    m.angle_limit = BEVEL_ANGLE
    m.width = BEVEL
    m.segments = BEVEL_SEG
    m.material = -1

    for mod in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=mod.name)

    # Normales lissées au seuil d'angle : les cylindres et la boule s'arrondissent,
    # les arêtes du caisson restent nettes.
    bpy.ops.object.shade_smooth()
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=SMOOTH_ANGLE)
    except (AttributeError, RuntimeError):
        mod = ob.modifiers.new("lissage", "NODES")
        bpy.ops.object.modifier_remove(modifier=mod.name)

    # Triangulation : le glTF ne transporte que des triangles, autant les
    # produire ici pour que le compte annoncé soit le compte livré.
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()

    # UV non chevauchants. `island_margin` écarte les îlots : sans marge, deux
    # îlots voisins bavent l'un sur l'autre dès qu'une texture est filtrée.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    return ob


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = "assets/models/borne/borne.gltf"
    if "--out" in argv:
        out = argv[argv.index("--out") + 1]
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    for nom in MATERIAUX:
        bpy.data.materials.new(nom)

    caisson()
    plinthe()
    grilles_hp()
    enseigne()
    porte_monnayeur()
    ancres = commandes()

    ob = assembler()
    tris = len(ob.data.polygons)
    print("BORNE triangles %d" % tris)

    bpy.ops.export_scene.gltf(
        filepath=out,
        export_format="GLTF_SEPARATE",
        export_yup=True,
        export_normals=True,
        export_tangents=True,
        export_texcoords=True,
        export_materials="EXPORT",
        export_apply=True,
        use_selection=False,
    )

    # L'ordre des matériaux DANS LE FICHIER, vérifié et non supposé.
    #
    # `roomgen` associe les primitives du glTF à celles de la salle par INDICE.
    # Si l'exportateur réordonnait un jour sa table, chaque pièce prendrait la
    # matière de sa voisine — un jonc de chant couleur bouton, un écran couleur
    # plinthe — et rien ne le signalerait : la salle se contenterait d'être
    # fausse. Mieux vaut casser le build ici.
    with open(out, encoding="utf-8") as fp:
        ordre = [m["name"] for m in json.load(fp)["materials"]]
    if ordre != MATERIAUX:
        raise SystemExit(
            "l'export a réordonné les matériaux :\n  attendu %s\n  obtenu  %s"
            % (MATERIAUX, ordre))

    # Les ancres, et l'ordre des matériaux. Elles sortent des MÊMES cotes que la
    # géométrie : c'est ce qui garantit qu'elles ne peuvent pas dériver d'elle.
    # `roomgen` les lit plutôt que de les recopier — une cote écrite deux fois
    # est une cote qui finira par mentir.
    ancres["screen"] = [0.0, ECRAN_Y, 0.0]
    ancres["screenSize"] = [ECRAN_W, ECRAN_H]
    ancres["screenTilt"] = ECRAN_INCL
    # La fente de gauche de la porte à monnaie : une borne a UN monnayeur.
    ancres["coin"] = [-0.038 - 0.017, PORTE_Y + PORTE_H - 0.064,
                      Z_FACE_BAS + PORTE_EP + 0.013]
    ancres["materials"] = MATERIAUX
    ancres["triangles"] = tris
    ancres["_"] = ("Écrit par assets/blender/borne.py. Ne pas retoucher à la "
                   "main : les cotes viennent du script, qui produit aussi le "
                   "glTF. « materials » donne l'ordre attendu par roomgen.")

    # La dalle est posée par roomgen sur la face du cadre, à sa hauteur.
    # La face court de (Z_CADRE_BAS, Y_CADRE_BAS) à (Z_ECRAN_HAUT, Y_ECRAN_HAUT) :
    # on l'interpole, puis on ajoute la marge que l'inclinaison réclame — le bord
    # HAUT de la dalle recule de sin(incl) x h/2, et sans cette marge il
    # repasserait DANS le meuble, ce qui a déjà coûté le tiers haut de l'image.
    t = (ECRAN_Y - Y_CADRE_BAS) / (Y_ECRAN_HAUT - Y_CADRE_BAS)
    zf = Z_CADRE_BAS + (Z_ECRAN_HAUT - Z_CADRE_BAS) * t
    ancres["screen"][2] = zf + math.sin(ECRAN_INCL) * ECRAN_H * 0.5 + 0.012

    with open(os.path.join(os.path.dirname(out), "borne.ancres.json"), "w",
              encoding="utf-8") as fp:
        json.dump(ancres, fp, indent=2, ensure_ascii=False)
        fp.write("\n")
    print("BORNE ancres ecrites")


main()
