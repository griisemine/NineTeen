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
#
# L'inclinaison et la position sont DÉDUITES de la face du cadre, jamais
# écrites. Elles l'étaient : `ECRAN_INCL = radians(21.6)` d'un côté, et de
# l'autre une face allant de (0,170 ; 1,098) à (0,050 ; 1,430), dont
# l'inclinaison réelle vaut 19,87°. Deux chiffres pour une seule surface, et ils
# n'étaient pas d'accord — la dalle penchait de 1,7° de plus que le meuble qui
# la porte.
#
# Pire, la dalle FLOTTAIT : elle était posée à `sin(21,6°) x h/2 + 12 mm` devant
# la face, soit 76,2 mm dans le vide, et dépassait de son ouverture de 22,2 mm
# par le haut. En vue rasante on voyait la cavité du cadre DERRIÈRE l'image.
#
# On la centre donc dans son ouverture et on la décale le long de la NORMALE de
# la face, ce qui est la seule façon qu'une plaque inclinée reste plaquée.
# La PROFONDEUR du creux. C'est la cote qui règle le reproche « on voit les
# écrans comme planés au-dessus de la borne et non inclus dans la borne » : un
# tube ne se pose pas sur une face, il se REGARDE AU FOND D'UN CAISSON. 35 mm
# suffisent à ce que les quatre parois du creux soient visibles sous tous les
# angles de jeu, et c'est l'ordre de grandeur d'un vrai encadrement de tube.
ECRAN_CREUX = 0.035

_ecran_dz = Z_ECRAN_HAUT - Z_CADRE_BAS
_ecran_dy = Y_ECRAN_HAUT - Y_CADRE_BAS
_ecran_l  = math.hypot(_ecran_dz, _ecran_dy)     # longueur de la face, sur la pente
ECRAN_INCL = math.atan2(-_ecran_dz, _ecran_dy)   # bascule vers l'arrière, en radians

# La normale de la face, dans le plan (z, y) : elle monte en reculant, donc sa
# normale sort vers le joueur et vers le haut.
ECRAN_NZ = _ecran_dy / _ecran_l
ECRAN_NY = -_ecran_dz / _ecran_l

# 16:9, comme la cible de rendu 512x288, et taillée pour laisser un VRAI cadre :
# 26,5 mm en haut et en bas, 84 mm de chaque côté. La dalle remplissait son
# ouverture à 7 mm près, ce qui ne laissait la place d'aucun encadrement — et un
# tube sans encadrement se lit comme une affiche, quel que soit le reste.
ECRAN_H = 0.300
ECRAN_W = ECRAN_H * 16.0 / 9.0                   # 0,5333
ECRAN_Y = (Y_CADRE_BAS + Y_ECRAN_HAUT) * 0.5     # centrée dans son ouverture

# --- la quincaillerie -------------------------------------------------
PORTE_W = 0.250           # mesurée : 103 px de 0,00258 m
PORTE_H = 0.550           # mesurée : 276 px
PORTE_Y = 0.130           # bas de la porte
PORTE_EP = 0.020          # en SAILLIE sur la face : mesurée à l'ombre portée

# --- le panneau de commande, UN SEUL POSTE -----------------------------
#
# Il en portait DEUX, et c'était faux pour trois raisons cumulées, chacune
# suffisante (docs/SPEC-BORNE.md §2) :
#
#   - le moteur n'a qu'un joueur. `games.h` déclare quatre directions et UN
#     bouton d'action ; aucune borne ne peut recevoir deux joueurs ;
#   - le duel de ce jeu est DISTANT, par un relais TCP. Jamais deux joueurs sur
#     le même meuble ;
#   - le poste de droite n'avait aucune ancre. Personne, jamais, ne pouvait le
#     toucher.
#
# Et géométriquement il ne tenait pas : le bouton le plus à droite sortait à
# x = +0,3621 quand la tôle percée s'arrête à ±0,3505 et le jonc de chant à
# ±0,3600. Il était vissé DANS LE VIDE, 11,6 mm hors du meuble — mesuré dans le
# glTF exporté, et visible sur une capture zoomée où la pastille chevauche le
# liseré.
#
# Le layout retenu est celui d'un upright Midway un joueur — Pac-Man, Donkey
# Kong, Galaga — et non un Vewlix ou un Sega P1, qui sont des panneaux de jeu de
# combat : aucun des huit jeux portés ne lit six boutons.
BOULE_R = 0.021

# La ligne de commande, à 75 mm du nez SUR LA PENTE. Un panneau réel garde 60 à
# 90 mm de tôle nue devant la première commande : c'est là que se pose le talon
# de la main. Le modèle en avait 31,5.
Z_COMMANDES = 0.3626      # Z_NEZ - 0.075 * cos(pente)
Z_START     = 0.3007      # 140 mm derrière le nez, sur la pente

MANCHE_X  = -0.085        # le manche, à gauche de l'axe
BOUTON_X  = (0.045, 0.085)   # ACTION, puis le secondaire
START_X   =  0.000        # sur l'axe de la borne

# 30 mm, la cote nommée du matériel réel (Sanwa OBSF-30, Seimitsu PS-14-G). Les
# 33 mm d'avant n'étaient pas faux en soi — c'est la COLLERETTE d'un bouton
# Happ — mais le modèle la peignait de la couleur du capuchon, donc la pastille
# se lisait 10 % trop grosse.
BOUTON_R  = 0.0150
BOUTON_H  = 0.008         # 5 mm de saillie une fois enfoncé de 3 : c'en était 9
START_R   = 0.0120        # 24 mm, un OBSF-24

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
    "manche_bleu",  # 10 la boule du manche
    "manche_rouge", # 11 le bouton START — le matériau que le poste supprimé libère
    "chrome",       # 12 tige de manche, rondelle, serrures
    # Les inserts rouges de la porte à monnaie. Ils portaient `bouton_a` : peindre
    # les boutons d'action en bleu repeignait la porte à monnaie, ce qui n'a
    # aucun sens et se voyait sur les bornes aux teintes vives.
    "insert",       # 13
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
        # None : ce segment n'est PAS émis par l'extrusion. C'est la face du
        # cadre d'écran, et `cadre_ecran()` la reconstruit percée d'une
        # ouverture et creusée derrière. Émettre les deux donnerait une tôle
        # pleine DEVANT le creux — c'est-à-dire exactement l'écran posé par
        # dessus qu'on cherche à supprimer.
        (Z_CADRE_BAS, Y_CADRE_BAS, None),             # -> face du cadre d'écran (percée)
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
        if p[i][2] is None:
            continue          # voir `profil_caisson` : la face du cadre est percée
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

def cadre_ecran():
    """La face du cadre, PERCÉE, et le creux derrière elle.

    Le reproche exact : « On voit les écrans comme planés au-dessus de la borne
    d'arcade et non inclus dans la borne d'arcade — la 3D de la borne devrait
    être creusée pour que l'écran semble incrusté. »

    Il était fondé deux fois. D'abord la dalle FLOTTAIT : elle était décalée en
    Z de `sin(incl) x h/2 + 12 mm`, soit 76 mm devant la face, ce qui n'est le
    décalage d'aucune surface — sur un plan incliné, s'écarter veut dire suivre
    la NORMALE. Ensuite, et c'est le fond du reproche, il n'y avait rien à
    incruster DANS : la face du cadre était une tôle pleine, et la dalle une
    plaque posée devant. Corriger le flottement seul aurait donné une plaque
    plaquée — mieux, mais toujours pas un tube.

    On perce donc la face et on creuse. Quatre bandes d'encadrement dans le plan
    de la face, quatre parois de 35 mm vers l'arrière, un fond. La dalle est
    posée par `roomgen` AU FOND de ce caisson : sous tous les angles de jeu on
    voit les parois du creux autour de l'image, et c'est ça qui fait « écran
    encastré » plutôt que « écran ajouté ».

    Repère : la face court de (Z_CADRE_BAS, Y_CADRE_BAS) à (Z_ECRAN_HAUT,
    Y_ECRAN_HAUT). On travaille dans son plan, en (`t` le long de la face, `x`
    en largeur), et on convertit au dernier moment.
    """
    uz = (Z_ECRAN_HAUT - Z_CADRE_BAS) / _ecran_l    # le long de la face, vers le haut
    uy = (Y_ECRAN_HAUT - Y_CADRE_BAS) / _ecran_l
    mz = (Z_CADRE_BAS + Z_ECRAN_HAUT) * 0.5         # le milieu de la face
    my = (Y_CADRE_BAS + Y_ECRAN_HAUT) * 0.5

    def pt(x, t, d):
        """(largeur, position sur la face depuis son milieu, enfoncement)."""
        z = mz + uz * t - ECRAN_NZ * d
        y = my + uy * t - ECRAN_NY * d
        return (x, av(z), y)

    hw, ht = HW - 0.0095, _ecran_l * 0.5    # le bord de la face, jonc déduit
    ow, ot = ECRAN_W * 0.5, ECRAN_H * 0.5   # le bord de l'ouverture

    # --- les quatre bandes d'encadrement, dans le plan de la face ---
    bandes = [
        ((-hw, -ht), ( hw, -ot)),   # bas
        ((-hw,  ot), ( hw,  ht)),   # haut
        ((-hw, -ot), (-ow,  ot)),   # gauche
        (( ow, -ot), ( hw,  ot)),   # droite
    ]
    for (i, ((x0, t0), (x1, t1))) in enumerate(bandes):
        v = [pt(x0, t0, 0.0), pt(x1, t0, 0.0), pt(x1, t1, 0.0), pt(x0, t1, 0.0)]
        piece("cadre_%d" % i, v, [(0, 1, 2, 3)], MI["noir"], None)

    # --- les quatre parois du creux, et le fond ---
    #
    # L'ordre des sommets est choisi pour que la normale regarde VERS
    # L'INTÉRIEUR du creux : ce sont des parois qu'on voit de face quand on est
    # devant la borne, pas des faces extérieures.
    coins = [(-ow, -ot), (ow, -ot), (ow, ot), (-ow, ot)]
    for k in range(4):
        (xa, ta) = coins[k]
        (xb, tb) = coins[(k + 1) % 4]
        v = [pt(xa, ta, 0.0), pt(xb, tb, 0.0),
             pt(xb, tb, ECRAN_CREUX), pt(xa, ta, ECRAN_CREUX)]
        piece("creux_%d" % k, v, [(3, 2, 1, 0)], MI["noir"], None)

    # Le FOND, 4 mm derrière la dalle. Sans lui on verrait à travers la borne
    # dès que la dalle n'est pas rendue — et c'est le cas au chargement, pendant
    # la fraction de seconde où aucune texture n'est encore posée.
    d = ECRAN_CREUX + 0.004
    v = [pt(-ow, -ot, d), pt(ow, -ot, d), pt(ow, ot, d), pt(-ow, ot, d)]
    piece("fond_ecran", v, [(0, 1, 2, 3)], MI["noir"], None)


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


def porte_arriere():
    """La trappe de service, au dos, et pourquoi le dos en avait besoin.

    La face arrière est le segment (-HD, Y_DOS) -> (-HD, PLINTHE_H) du profil :
    0,72 x 1,60 m de peinture, sans un accident. Ce n'était pas visible tant
    qu'on regardait les bornes de face — mais les deux rangées de l'îlot sont
    DOS À DOS, à 32 cm l'une de l'autre (backs à z 0,853 et 0,532, mesurés sur
    la géométrie produite), et on longe cet intervalle en entrant. La capture
    « arrivée » montrait une dalle verte lisse de la taille d'une porte, qui ne
    ressemblait à rien.

    Un dos de borne réelle porte quatre choses, et pas une de plus : une trappe
    d'accès en saillie, une aération en haut de cette trappe, deux serrures à
    came, et l'embase du cordon secteur en bas. Elles suffisent parce qu'elles
    sont ce que l'oeil cherche : une surface qui s'ouvre, un endroit par où l'air
    sort, un endroit qu'on verrouille, un endroit d'où sort le courant.

    EN SAILLIE ET NON EN RETRAIT. Creuser la trappe demanderait de percer la
    face arrière du caisson, comme `cadre_ecran` perce celle du cadre — soit un
    segment `None` de plus dans le profil et un fond à reconstruire. La saillie
    de 8 mm donne la même lecture : c'est le LISERÉ D'OMBRE qui dit qu'une
    surface est séparée d'une autre, et il se forme aussi bien en avant qu'en
    arrière. Une vraie porte de borne recouvre d'ailleurs son ouverture plutôt
    que de s'y encastrer.
    """
    Z = -HD                       # la face arrière
    # La trappe. 5 cm de dormant de chaque côté, comme sur les vues arrière de
    # référence ; en bas elle s'arrête au-dessus de la plinthe, en haut sous la
    # tablette du moniteur.
    TR_Y0, TR_Y1 = 0.26, 1.34
    TR_W = W - 0.10
    cy = (TR_Y0 + TR_Y1) * 0.5
    boite("dos_trappe", 0.0, cy, Z - 0.004, TR_W, TR_Y1 - TR_Y0, 0.008,
          MI["noir"])

    # L'aération, en haut de la trappe. Cinq lames, et le même raisonnement que
    # les grilles de haut-parleur : ce qui fait lire une grille n'est pas sa
    # couleur mais son MÉTAL, donc du relief et un matériau à part plutôt qu'un
    # aplat. Trois millimètres de saillie suffisent à accrocher un rasant.
    for k in range(5):
        boite("dos_lame_%d" % k, 0.0, 1.28 - k * 0.030, Z - 0.008 - 0.0015,
              0.30, 0.014, 0.003, MI["grille"])

    # Les deux serrures à came, sur les chants de la trappe et à mi-hauteur.
    # `pitch` de 90° couche le cylindre sur l'axe de profondeur : sans lui, une
    # serrure sortirait du dos comme un bouton de porte.
    for cote in (-1, 1):
        cylindre("dos_serrure_%d" % cote, cote * 0.265, cy, Z - 0.010,
                 0.012, 0.012, 0.005, MI["chrome"], seg=12,
                 pitch=math.pi * 0.5)

    # L'embase du cordon, sous la trappe, et le cordon lui-même jusqu'au sol.
    # Décentrée : une prise au milieu du dos est ce que dessine quelqu'un qui
    # n'en a jamais vu.
    boite("dos_embase", 0.200, 0.190, Z - 0.006, 0.075, 0.055, 0.012,
          MI["noir"])
    cylindre("dos_cordon", 0.200, 0.010, Z - 0.012, 0.0055, 0.0055, 0.180,
             MI["noir"], seg=8)


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
              0.034, 0.048, 0.006, MI["insert"])
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
    """UN poste de jeu : un manche à boule, deux boutons, un START.

    Le pourquoi d'un seul poste est dans le bloc de constantes ci-dessus, et le
    détail chiffré dans docs/SPEC-BORNE.md §2. En deux mots : le moteur n'a
    qu'un joueur, le duel de ce jeu est distant, et le second poste n'avait
    aucune ancre — personne ne pouvait le toucher.

    Ce que le poste supprimé libère, ce n'est pas du vide : c'est 242 mm de tôle
    nue à gauche et 250 à droite, et c'est exactement ce à quoi ressemble un
    panneau d'upright un joueur. Sur une Ms. Pac-Man le manche est seul au
    milieu de 56 cm. Cette tôle porte la sérigraphie que `tools/panelart`
    dessine déjà — le titre, « 1 PLAYER », la légende des boutons — et le
    layout d'avant ne lui laissait pas la place.
    """
    ancres = {}

    # Pas de tôle rapportée : le stratifié du panneau EST le segment du profil
    # qui va du nez au fond, et ce segment porte déjà le matériau « panneau ».
    # Ce qui est dans le profil se décrit dans le profil.

    # --- le manche --------------------------------------------------------
    x = MANCHE_X
    y0 = panneau_y(Z_COMMANDES)

    # La rondelle anti-poussière, à demi enfoncée : 6 mm sous la surface, ce qui
    # la fait tenir au lieu de flotter.
    cylindre("rondelle", x, y0 - 0.006, Z_COMMANDES,
             0.024, 0.021, 0.007, MI["chrome"], pitch=PANNEAU_PITCH)

    # La tige : 12 mm de diamètre sur 38 mm visibles.
    #
    # Elle faisait 18 mm de diamètre sur 62 mm — c'est-à-dire un levier de
    # vitesse. Un arbre de manche d'arcade est un tube de 10 à 13 mm, et il ne
    # sort de la rondelle que de 35 à 40 mm : au-delà, la boule culmine trop
    # haut et le poignet du joueur ne peut plus se poser sur la tôle. Elle
    # montait à 101 mm au-dessus du panneau ; elle culmine maintenant à 77.
    cylindre("tige", x, y0, Z_COMMANDES,
             0.006, 0.0055, 0.038, MI["chrome"], pitch=PANNEAU_PITCH,
             chapeau=False)

    # La boule : un demi-cercle continu, très légèrement aplati en bas là où
    # elle coiffe sa tige — une boule d'arcade est moulée sur son insert.
    prof = []
    for k in range(7):
        a = -math.pi * 0.5 + math.pi * (k / 6.0)
        prof.append((math.cos(a) * BOULE_R,
                     math.sin(a) * BOULE_R * (0.86 if math.sin(a) < 0 else 1.0)))
    yb = y0 + 0.038 + BOULE_R * 0.86
    revolution("boule", x, yb, Z_COMMANDES, prof, MI["manche_bleu"])

    # Le SOMMET de la boule : le point qu'on touche, comme `panel` désigne le
    # dessus du bouton et `coin` la fente. La pose des bras y ajoute la paume.
    ancres["stick"] = [x, yb + BOULE_R, Z_COMMANDES]

    # --- les deux boutons -------------------------------------------------
    #
    # DEUX, et le second n'est pas décoratif : `games/demineur/demineur.c`
    # documente lui-même qu'il ne peut pas poser de drapeau « parce que le
    # second bouton qu'il faudrait n'existe pas sur le panneau ». Le panneau en
    # portait douze, et aucun n'était déclaré. En voici un.
    #
    # Entraxe 40 mm : la collerette d'un OBSF-30 fait 35 mm, donc 36 est le
    # plancher physique où deux collerettes se touchent. 40 laisse 10 mm de tôle
    # entre deux capuchons, ce qui les fait lire comme deux boutons distincts à
    # deux mètres — et il n'y a aucune raison de serrer quand il n'y en a que
    # deux.
    for (i, bx) in enumerate(BOUTON_X):
        yb2 = panneau_y(Z_COMMANDES)
        cylindre("bouton_%d" % i, bx, yb2 - 0.003, Z_COMMANDES,
                 BOUTON_R, BOUTON_R * 0.88, BOUTON_H,
                 MI["bouton_a" if i == 0 else "bouton_b"], pitch=PANNEAU_PITCH)

    # `panel` cesse d'être le barycentre de six pastilles — un point qui n'était
    # AUCUN bouton, et sur lequel l'index venait donc se poser entre deux — et
    # devient le dessus de celui qu'on presse.
    ancres["panel"] = [BOUTON_X[0], panneau_y(Z_COMMANDES) + BOUTON_H * 0.5,
                       Z_COMMANDES]

    # --- le START ---------------------------------------------------------
    #
    # Il n'y en avait aucun, et une borne sans START n'existe pas : c'est le
    # bouton que la sérigraphie désigne par « PUSH START », celui qui répond au
    # jeton. Il est plus petit (24 mm contre 30) et en retrait, comme sur tout
    # panneau réel — on ne le presse pas en jouant.
    cylindre("start", START_X, panneau_y(Z_START) - 0.003, Z_START,
             START_R, START_R * 0.88, BOUTON_H,
             MI["manche_rouge"], pitch=PANNEAU_PITCH)

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

    cadrer_les_flancs(ob)
    cadrer_l_enseigne(ob)
    return ob


def cadrer_l_enseigne(ob):
    """L'enseigne du marquee, CADRÉE elle aussi.

    Même cause et même remède que pour les flancs, sur une surface que le joueur
    lit de plus loin encore : `smart_project` posait `nineteen_name.jpg` à une
    échelle arbitraire, et la borne de classement affichait à la place de son nom
    un fragment de tôle agrandi. Le marquee est le panneau qui dit à quoi on
    joue ; c'est la dernière surface qu'on peut laisser au hasard.

    Les deux axes de projection sont CHOISIS, pas écrits : on prend les deux plus
    grandes étendues de la boîte de l'enseigne. Les écrire en dur demanderait de
    se souvenir que ce maillage a Y en profondeur et Z en hauteur, ce qui est
    précisément le genre de convention qu'on se rappelle mal — la première
    version projetait sur x et y et donnait un panneau rayé.
    """
    me = ob.data
    uv = me.uv_layers.active.data
    marq = MI["marquee"]

    faces = [f for f in me.polygons if f.material_index == marq]
    if not faces:
        raise RuntimeError("aucune enseigne a cadrer : la table des materiaux a bouge")

    lo = [1e9] * 3
    hi = [-1e9] * 3
    for f in faces:
        for vi in f.vertices:
            co = me.vertices[vi].co
            for k in range(3):
                lo[k] = min(lo[k], co[k]); hi[k] = max(hi[k], co[k])
    etendue = [hi[k] - lo[k] for k in range(3)]
    axes = sorted(range(3), key=lambda k: etendue[k], reverse=True)[:2]
    au, av = axes[0], axes[1]
    if etendue[au] < 1e-6 or etendue[av] < 1e-6:
        raise RuntimeError("enseigne degeneree")

    for f in faces:
        for li in f.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            u = (co[au] - lo[au]) / etendue[au]
            v = (co[av] - lo[av]) / etendue[av]
            uv[li].uv = (u, v)


def cadrer_les_flancs(ob):
    """Les UV des deux flancs, CADRÉES sur la silhouette de la borne.

    `smart_project` range des îlots ; il ne sait pas qu'une planche dessinée a
    un haut, un bas et des bords. Employé sur les flancs, il y pose la
    sérigraphie à une échelle et à un endroit arbitraires : mesuré sur la
    capture d'albédo de la vue `allee`, le flanc de la borne Flappy ne montrait
    plus la trame de losanges mais deux grandes diagonales, et prenait sa
    couleur au BAS du dégradé de `flanc_borne.png` (0,16) au lieu de la
    parcourir — d'où un flanc presque noir là où le matériau annonce
    (0,43 ; 0,74 ; 0,51).

    C'est le même cadrage que faisait `geo_profile_extrude_capped` avant que la
    carrosserie ne passe sous Blender, et c'est ce qui permet à UNE planche de
    servir des bornes de tailles différentes : une projection plane sur la boîte
    englobante du profil, de (0,0) à (1,1). Les faces du jonc de chant sont
    exclues — elles portent leur propre matériau et n'ont rien à cadrer.
    """
    me = ob.data
    uv = me.uv_layers.active.data
    flanc = MI["flanc"]

    lo_y = lo_z = 1e9
    hi_y = hi_z = -1e9
    faces = [f for f in me.polygons if f.material_index == flanc]
    for f in faces:
        for vi in f.vertices:
            co = me.vertices[vi].co
            lo_y = min(lo_y, co.y); hi_y = max(hi_y, co.y)
            lo_z = min(lo_z, co.z); hi_z = max(hi_z, co.z)
    if not faces or hi_y - lo_y < 1e-6 or hi_z - lo_z < 1e-6:
        raise RuntimeError("aucun flanc a cadrer : la table des materiaux a bouge")

    # Y est la profondeur de la borne, Z sa hauteur — la planche est en
    # portrait, comme le flanc. Le flanc de droite est retourné en U pour que la
    # sérigraphie ne se lise pas en miroir d'un côté de la borne.
    for f in faces:
        droite = me.vertices[f.vertices[0]].co.x > 0.0
        for li in f.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            u = (co.y - lo_y) / (hi_y - lo_y)
            v = (co.z - lo_z) / (hi_z - lo_z)
            uv[li].uv = (1.0 - u if droite else u, v)


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
    cadre_ecran()
    plinthe()
    grilles_hp()
    porte_arriere()
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

    # La dalle, AU FOND DU CREUX que `cadre_ecran()` vient de tailler.
    #
    # Elle était translatée en Z de `sin(incl) x h/2 + 12 mm`, ce qui n'est le
    # décalage d'aucune surface : sur une face inclinée, s'écarter veut dire
    # suivre la NORMALE, pas l'axe des Z. Le résultat était une dalle à 76,2 mm
    # DEVANT son cadre — l'écran « planné au-dessus de la borne » du rapport.
    #
    # Elle recule maintenant de `ECRAN_CREUX` le long de la normale, c'est-à-dire
    # qu'elle est posée au fond du caisson, avec 35 mm de parois tout autour.
    zf = (Z_CADRE_BAS + Z_ECRAN_HAUT) * 0.5      # le milieu de la face
    ancres["screen"][1] = ECRAN_Y - ECRAN_NY * ECRAN_CREUX
    ancres["screen"][2] = zf - ECRAN_NZ * ECRAN_CREUX

    with open(os.path.join(os.path.dirname(out), "borne.ancres.json"), "w",
              encoding="utf-8") as fp:
        json.dump(ancres, fp, indent=2, ensure_ascii=False)
        fp.write("\n")
    print("BORNE ancres ecrites")


main()
