"""
billard.py — la table de billard, construite sous Blender.

    /Applications/Blender.app/Contents/MacOS/Blender --background \\
        --python assets/blender/billard.py -- --out assets/models/billard/billard.gltf

Pourquoi elle est refaite
-------------------------
Elle était onze boîtes empilées, et ça se voyait : pas de poche, pas de
mouluration, des pieds carrés qui montaient tout droit, et surtout un placage
de bois pavé à 0,80 m posé sur des bandes de 6 cm d'épaisseur — d'où des
« bandes » qui se lisaient comme une PILE DE PLANCHES, le fil du bois étiré en
rayures sur toute la longueur. C'était l'objet le plus raté de la salle, et il
occupe le centre du seul coin qui ait sa propre lampe.

Ce qui fait qu'on reconnaît une table de billard
------------------------------------------------
Dans l'ordre où l'œil les prend, à trois mètres :

1. **Les six poches.** C'est le seul détail dont l'absence se remarque
   immédiatement — une table sans poche est un meuble vert.
2. **Le profil des bandes.** Un caoutchouc de bande n'est pas un rectangle :
   c'est un triangle dont le nez, à 37 mm du drap, avance sur le tapis. C'est
   ce nez qui fait l'ombre portée sur le drap, et cette ombre-là dit « bande ».
3. **La mouluration du châssis**, qui met le plateau à l'ombre de son propre
   débord.
4. **Les pieds tronconiques**, et non des poteaux.
5. **Les repères de visée** sur les chapeaux, trois par demi-longueur.

Les cotes sont celles que la salle occupe déjà — 2,36 × 1,36 hors tout, drap à
0,784 m — pour que ni le contrôle de chevauchement ni la collision ne bougent.
C'est une table de sept pieds, ce qu'était déjà l'empilement de boîtes.

Le maillage est TRIANGULÉ ici et non à l'import : le compte annoncé est alors
le compte livré.
"""
import bpy
import bmesh
import math
import os
import sys

# ---------------------------------------------------------------------------
# Les cotes, en mètres. Toutes mesurables sur une vraie table de sept pieds.
# ---------------------------------------------------------------------------
L_HORS = 2.36          # longueur hors tout
W_HORS = 1.36          # largeur hors tout
Z_DRAP = 0.784         # hauteur du drap
L_JEU = 2.10           # longueur de jeu, entre nez de bandes
W_JEU = 1.10           # largeur de jeu

BANDE = (L_HORS - L_JEU) * 0.5      # 0,13 : largeur d'une bande + son chapeau
NEZ = 0.037            # hauteur du nez de bande au-dessus du drap
CHAPEAU_Z = 0.062      # épaisseur du chapeau, au-dessus du drap
EP_ARDOISE = 0.022     # l'ardoise sous le drap

CHASSIS_H = 0.185      # le bandeau sous le plateau
CHASSIS_DEB = 0.012    # son retrait sous le débord du chapeau

PIED_H = Z_DRAP - EP_ARDOISE - CHASSIS_H   # 0,577
PIED_A = 0.155         # côté du pied au sol
PIED_B = 0.125         # côté du pied sous le châssis
PIED_X = L_HORS * 0.5 - 0.155
PIED_Y = W_HORS * 0.5 - 0.155

POCHE_COIN = 0.115     # rayon de la gueule d'une poche de coin
POCHE_MIL = 0.120      # celle du milieu, toujours un peu plus large

BEVEL = 0.004
BEVEL_SEG = 1
SMOOTH_ANGLE = math.radians(38.0)

MATERIAUX = [
    "bois",        # 0  le châssis, les pieds, les chapeaux
    "drap",        # 1  le tapis et la couverture des bandes
    "poche",       # 2  le cuir des poches et leur filet
    "metal",       # 3  les pièces d'angle et les cercles de poche
    "repere",      # 4  les visées incrustées dans les chapeaux
]
MI = {nom: i for i, nom in enumerate(MATERIAUX)}

_pieces = []


def piece(nom, verts, faces, mats):
    """Une pièce, avec un indice de matériau par face.

    Les CINQ emplacements sont déclarés sur chaque pièce, dans le même ordre :
    `join()` réassocie les faces par emplacement, et une pièce qui n'en
    déclarerait que trois verrait tous ses indices retomber — c'est-à-dire une
    table d'une seule matière.
    """
    me = bpy.data.meshes.new(nom)
    me.from_pydata(verts, [], faces)
    me.update()
    for nm in MATERIAUX:
        me.materials.append(bpy.data.materials[nm])
    for i, f in enumerate(me.polygons):
        f.material_index = mats[i] if isinstance(mats, list) else mats
    ob = bpy.data.objects.new(nom, me)
    bpy.context.collection.objects.link(ob)
    _pieces.append(ob)
    return ob


def boite(nom, cx, cy, cz, sx, sy, sz, mat):
    """Une boîte centrée. X longueur, Y largeur, Z hauteur — la convention de
    Blender, et celle que l'export en Y-haut convertira."""
    hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
    v = [(cx + sx_ * hx, cy + sy_ * hy, cz + sz_ * hz)
         for sz_ in (-1, 1) for sy_ in (-1, 1) for sx_ in (-1, 1)]
    f = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
         (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return piece(nom, v, f, mat)


def prisme(nom, section, axe, a, b, decalage, mat):
    """Extrude une section 2D le long d'un axe, de `a` à `b`.

    `section` est une liste de couples dans le plan perpendiculaire. C'est ce
    qui permet de donner aux bandes leur VRAI profil — un triangle au nez
    avancé — au lieu d'une boîte : sans lui, une bande est une planche, et
    quatre planches posées autour d'un drap vert ne font pas une table.
    """
    n = len(section)
    verts = []
    for t in (a, b):
        for (u, w) in section:
            if axe == "x":
                verts.append((t, u + decalage[0], w + decalage[1]))
            else:
                verts.append((u + decalage[0], t, w + decalage[1]))
    faces, mats = [], []
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
        mats.append(mat)
    faces.append(tuple(range(n - 1, -1, -1)))
    mats.append(mat)
    faces.append(tuple(range(n, 2 * n)))
    mats.append(mat)
    # L'orientation dépend du sens de parcours ; on la corrige après coup.
    ob = piece(nom, verts, faces, mats)
    return ob


def anneau(nom, cx, cy, cz, r_int, r_ext, h, mat, seg=16):
    """Un anneau plat — le cercle métallique d'une poche."""
    v, f, m = [], [], []
    for z in (cz, cz + h):
        for k in range(seg):
            a = 2.0 * math.pi * k / seg
            v.append((cx + r_int * math.cos(a), cy + r_int * math.sin(a), z))
        for k in range(seg):
            a = 2.0 * math.pi * k / seg
            v.append((cx + r_ext * math.cos(a), cy + r_ext * math.sin(a), z))
    base_hi = 2 * seg
    for k in range(seg):
        k1 = (k + 1) % seg
        # dessus
        f.append((base_hi + k, base_hi + k1, base_hi + seg + k1, base_hi + seg + k)); m.append(mat)
        # dessous
        f.append((seg + k, seg + k1, k1, k)); m.append(mat)
        # chants
        f.append((k, k1, base_hi + k1, base_hi + k)); m.append(mat)
        f.append((base_hi + seg + k, base_hi + seg + k1, seg + k1, seg + k)); m.append(mat)
    return piece(nom, v, f, m)


def cone_filet(nom, cx, cy, cz, r, profondeur, mat, seg=14):
    """Le filet d'une poche : un tronc de cône ouvert, qui descend sous le
    plateau. On ne voit jamais son fond ; ce qu'on voit, c'est qu'il y a un
    TROU, et un trou a besoin de parois."""
    v, f, m = [], [], []
    for k in range(seg):
        a = 2.0 * math.pi * k / seg
        v.append((cx + r * math.cos(a), cy + r * math.sin(a), cz))
    for k in range(seg):
        a = 2.0 * math.pi * k / seg
        v.append((cx + r * 0.55 * math.cos(a), cy + r * 0.55 * math.sin(a), cz - profondeur))
    for k in range(seg):
        k1 = (k + 1) % seg
        f.append((k, k1, seg + k1, seg + k)); m.append(mat)
    # Un fond, sinon on voit à travers la table.
    f.append(tuple(range(2 * seg - 1, seg - 1, -1))); m.append(mat)
    return piece(nom, v, f, m)


# ---------------------------------------------------------------------------
# Les pièces
# ---------------------------------------------------------------------------
def pieds():
    """Quatre pieds tronconiques à section carrée.

    Un poteau droit se lit comme un pied de table de jardin. La fuite — 155 mm
    au sol, 125 sous le châssis — est ce qui donne son assise à une table qui
    porte trois cents kilos d'ardoise.
    """
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = sx * PIED_X, sy * PIED_Y
            a, b = PIED_A * 0.5, PIED_B * 0.5
            v = [(cx - a, cy - a, 0.0), (cx + a, cy - a, 0.0),
                 (cx + a, cy + a, 0.0), (cx - a, cy + a, 0.0),
                 (cx - b, cy - b, PIED_H), (cx + b, cy - b, PIED_H),
                 (cx + b, cy + b, PIED_H), (cx - b, cy + b, PIED_H)]
            f = [(3, 2, 1, 0), (4, 5, 6, 7),
                 (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
            piece("pied_%d_%d" % (sx, sy), v, f, MI["bois"])


def chassis():
    """Le bandeau sous le plateau, et l'ardoise qu'il porte.

    Le bandeau est en RETRAIT de 12 mm sous le débord du chapeau : c'est ce
    retrait qui met le plateau à l'ombre de lui-même, et une table de billard
    sans cette ligne d'ombre paraît posée sur une caisse.
    """
    z0 = PIED_H
    boite("chassis", 0.0, 0.0, z0 + CHASSIS_H * 0.5,
          L_HORS - 2.0 * CHASSIS_DEB, W_HORS - 2.0 * CHASSIS_DEB, CHASSIS_H,
          MI["bois"])
    boite("ardoise", 0.0, 0.0, Z_DRAP - EP_ARDOISE * 0.5,
          L_HORS, W_HORS, EP_ARDOISE, MI["bois"])


def drap():
    """Le drap, et la retombée qu'il fait sur le chant de l'ardoise.

    Une plaque verte posée à plat se lit comme du carton. Le drap d'une table
    est TENDU sur l'ardoise et retombe sur son chant : deux millimètres de vert
    visibles sous le chapeau, et c'est ce qui donne son épaisseur au plateau.
    """
    boite("drap", 0.0, 0.0, Z_DRAP - 0.004,
          L_HORS - 0.004, W_HORS - 0.004, 0.010, MI["drap"])


def _profil_bande():
    """Le profil d'un caoutchouc de bande, dans le plan (largeur, hauteur).

    Mesuré sur une bande de tournoi et ramené aux cotes d'ici : la base fait
    toute la largeur disponible, le nez avance au trois quarts de la hauteur, et
    le dessus repart en arrière pour aller sous le chapeau. C'est un triangle,
    pas un rectangle, et c'est la première chose qu'on voit d'une table.
    """
    b = BANDE - 0.012          # la bande, sous le chapeau
    return [
        (0.0, 0.0),            # le nez, côté jeu, au ras du drap
        (0.0, NEZ),            # le nez, en haut
        (b * 0.55, NEZ + 0.006),
        (b, NEZ + 0.010),      # sous le chapeau
        (b, 0.0),
    ]


def bandes():
    """Les six bandes : deux longues d'un côté... et quatre demi-longues de
    l'autre, coupées par les deux poches du milieu. Une table de sept pieds n'a
    PAS quatre bandes, elle en a six — et c'est ce découpage-là qui place les
    poches du milieu."""
    sec = _profil_bande()
    d = POCHE_COIN + 0.028     # le retrait d'une bande à un coin
    dm = POCHE_MIL + 0.024     # son retrait à une poche du milieu

    # Les deux petits côtés : une bande chacun, entre deux poches de coin.
    for sx in (-1, 1):
        x = sx * (L_JEU * 0.5)
        # section posée dans le plan (y, z), extrudée en x
        pts = [(u, w) for (w, u) in ((a, b) for (a, b) in sec)]
        verts, faces, mats = [], [], []
        n = len(sec)
        for t in (x, x + sx * (BANDE - 0.012)):
            for (u, w) in sec:
                verts.append((t, 0.0, 0.0))  # remplacé juste après
        verts = []
        for t in (x, x + sx * (BANDE - 0.012)):
            for (u, w) in sec:
                verts.append((t, u, w))
        del pts
        # On extrude en Y : la section (u=largeur de bande, w=hauteur) tourne.
        verts = []
        for y in (-(W_JEU * 0.5 - d), (W_JEU * 0.5 - d)):
            for (u, w) in sec:
                verts.append((x + sx * u, y, Z_DRAP + w))
        for i in range(n):
            j = (i + 1) % n
            faces.append((i, j, n + j, n + i)); mats.append(MI["drap"])
        faces.append(tuple(range(n - 1, -1, -1))); mats.append(MI["drap"])
        faces.append(tuple(range(n, 2 * n))); mats.append(MI["drap"])
        piece("bande_x%d" % sx, verts, faces, mats)

    # Les deux grands côtés : deux demi-bandes chacun.
    for sy in (-1, 1):
        y = sy * (W_JEU * 0.5)
        for sx in (-1, 1):
            x0 = sx * (L_JEU * 0.5 - d)
            x1 = sx * dm
            n = len(sec)
            verts, faces, mats = [], [], []
            for x in (x0, x1):
                for (u, w) in sec:
                    verts.append((x, y + sy * u, Z_DRAP + w))
            for i in range(n):
                j = (i + 1) % n
                faces.append((i, j, n + j, n + i)); mats.append(MI["drap"])
            faces.append(tuple(range(n - 1, -1, -1))); mats.append(MI["drap"])
            faces.append(tuple(range(n, 2 * n))); mats.append(MI["drap"])
            piece("bande_y%d_%d" % (sy, sx), verts, faces, mats)


def chapeaux():
    """Les chapeaux de bois, par SEGMENTS, avec les trous des poches entre eux.

    Découper six trous dans un cadre demanderait des opérations booléennes ;
    poser huit segments qui laissent les trous là où il faut n'en demande
    aucune, et donne un maillage propre. C'est la même décision que celle prise
    pour le pan de mur percé de la salle.
    """
    zc = Z_DRAP + CHAPEAU_Z * 0.5
    b0 = L_JEU * 0.5          # bord intérieur
    hw = W_HORS * 0.5
    hl = L_HORS * 0.5
    d = POCHE_COIN + 0.028
    dm = POCHE_MIL + 0.024

    # Les deux petits côtés, en un morceau chacun.
    for sx in (-1, 1):
        boite("chapeau_x%d" % sx, sx * (b0 + BANDE * 0.5), 0.0, zc,
              BANDE, W_HORS - 2.0 * d, CHAPEAU_Z, MI["bois"])

    # Les deux grands côtés, en deux morceaux chacun.
    for sy in (-1, 1):
        for sx in (-1, 1):
            x0, x1 = sx * (hl - d), sx * dm
            boite("chapeau_y%d_%d" % (sy, sx), (x0 + x1) * 0.5,
                  sy * (W_JEU * 0.5 + BANDE * 0.5), zc,
                  abs(x1 - x0), BANDE, CHAPEAU_Z, MI["bois"])

    # Les quatre pièces d'angle, en métal : c'est ce qui tient une poche de coin
    # sur une vraie table, et ça accroche la lampe.
    for sx in (-1, 1):
        for sy in (-1, 1):
            boite("angle_%d_%d" % (sx, sy), sx * (hl - 0.055), sy * (hw - 0.055),
                  zc, 0.110, 0.110, CHAPEAU_Z * 0.92, MI["metal"])


def poches():
    """Les six poches : un cercle métallique, un filet, un trou."""
    hl, hw = L_HORS * 0.5, W_HORS * 0.5
    coins = [(sx * (hl - 0.078), sy * (hw - 0.078), POCHE_COIN)
             for sx in (-1, 1) for sy in (-1, 1)]
    milieux = [(0.0, sy * (hw - 0.052), POCHE_MIL) for sy in (-1, 1)]
    for i, (x, y, r) in enumerate(coins + milieux):
        anneau("cercle_%d" % i, x, y, Z_DRAP + CHAPEAU_Z - 0.006,
               r, r + 0.016, 0.008, MI["metal"])
        cone_filet("filet_%d" % i, x, y, Z_DRAP + CHAPEAU_Z - 0.006,
                   r, 0.130, MI["poche"])


def reperes():
    """Les visées : trois par demi-longueur sur les grands côtés, une par
    demi-largeur sur les petits. Ce sont elles qui font qu'un chapeau de bois
    n'est pas une planche."""
    z = Z_DRAP + CHAPEAU_Z + 0.0005
    hw = W_HORS * 0.5 - BANDE * 0.5
    hl = L_HORS * 0.5 - BANDE * 0.5
    pas_x = L_JEU / 8.0
    for sy in (-1, 1):
        for k in (-3, -2, -1, 1, 2, 3):
            boite("visee_y%d_%d" % (sy, k), k * pas_x, sy * hw, z,
                  0.020, 0.020, 0.002, MI["repere"])
    pas_y = W_JEU / 4.0
    for sx in (-1, 1):
        for k in (-1, 1):
            boite("visee_x%d_%d" % (sx, k), sx * hl, k * pas_y, z,
                  0.020, 0.020, 0.002, MI["repere"])


# ---------------------------------------------------------------------------
def assembler():
    ob = _pieces[0]
    bpy.ops.object.select_all(action="DESELECT")
    for p in _pieces:
        p.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = "billard"

    # Les normales d'abord : `prisme` et les sections extrudées peuvent sortir
    # retournées selon le sens de parcours, et une face retournée s'éclaire
    # comme si elle regardait le sol.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")

    m = ob.modifiers.new("chanfrein", "BEVEL")
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(35.0)
    m.width = BEVEL
    m.segments = BEVEL_SEG
    m.material = -1
    for mod in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=mod.name)

    bpy.ops.object.shade_smooth()
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=SMOOTH_ANGLE)
    except (AttributeError, RuntimeError):
        pass

    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()

    deplier(ob)
    return ob


# Le pas de pavage de chaque matière, en mètres.
#
# Il est ICI et non dans `salle.room.json`, et c'est une contrainte du moteur,
# pas une préférence : `geo_import` emploie les UV DU FICHIER telles quelles.
# La clé `uvMetres` d'un matériau n'a donc aucun effet sur un modèle importé —
# ce qui s'est vu tout de suite, le drap sortant en marbrures turquoise et les
# chapeaux en tôle ondulée, parce que `smart_project` range des îlots sans
# aucune idée de l'échelle du monde.
PAS = {
    "bois":   0.45,   # le fil d'un placage de chêne
    "drap":   1.30,   # une feutrine ne montre pas de motif : le pas est large
    "poche":  0.25,
    "metal":  0.18,
    "repere": 0.06,
}


def deplier(ob):
    """Une projection cubique À L'ÉCHELLE DU MONDE, puis un pas par matière.

    `cube_project` avec `cube_size=1` donne des UV en MÈTRES : une face de
    45 cm couvre 0,45 d'unité. Il ne reste qu'à diviser par le pas voulu, face
    par face, pour que chaque matière se pave à sa propre cote — ce que
    `uvMetres` ferait pour une géométrie générée, et qu'aucune option ne fait
    pour un modèle importé.

    C'est aussi ce qui rend le résultat indépendant de la taille de la table :
    doubler une cote ne dilate pas le bois, elle en met deux fois plus.
    """
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=1.0, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")

    me = ob.data
    uv = me.uv_layers.active.data
    for f in me.polygons:
        k = 1.0 / PAS[MATERIAUX[f.material_index]]
        for li in f.loop_indices:
            u, v = uv[li].uv
            uv[li].uv = (u * k, v * k)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = "assets/models/billard/billard.gltf"
    if "--out" in argv:
        out = argv[argv.index("--out") + 1]
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    for nom in MATERIAUX:
        bpy.data.materials.new(nom)

    pieds()
    chassis()
    drap()
    bandes()
    chapeaux()
    poches()
    reperes()

    ob = assembler()
    print("BILLARD triangles %d" % len(ob.data.polygons))

    bpy.ops.export_scene.gltf(
        filepath=out,
        export_format="GLTF_SEPARATE",
        export_yup=True,
        export_normals=True,
        export_texcoords=True,
        export_materials="EXPORT",
        export_apply=True,
        use_selection=False,
    )

    # L'ordre des matériaux DANS LE FICHIER, vérifié et non supposé : `roomgen`
    # associe les primitives par INDICE, et un exportateur qui réordonnerait sa
    # table donnerait une table en drap de bois.
    import json
    with open(out, "r", encoding="utf-8") as f:
        doc = json.load(f)
    noms = [m["name"] for m in doc.get("materials", [])]
    if noms != MATERIAUX:
        raise SystemExit("ordre des materiaux inattendu : %r" % (noms,))
    print("BILLARD materiaux %s" % ", ".join(noms))


main()
