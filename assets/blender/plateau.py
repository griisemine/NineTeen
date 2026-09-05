"""
plateau.py — la garniture du café : un plateau-repas et un gobelet seul.

    /Applications/Blender.app/Contents/MacOS/Blender --background \\
        --python assets/blender/plateau.py -- --out assets/models

Pourquoi ces deux objets existent
---------------------------------
La salle a six bancs, deux mange-debout, un canapé et sa table basse, et il n'y
a RIEN dessus. Une table d'arcade vide se lit comme un meuble de catalogue :
c'est le seul endroit du décor où l'on voit que personne n'est jamais venu.
La photo de référence du propriétaire montre exactement l'inverse — des
plateaux, des gobelets, une table qu'on vient de quitter.

Ce n'est pas de la décoration gratuite. C'est le seul objet du décor qui dise
qu'il y a du MONDE dans cette salle, et une salle d'arcade vide n'a pas la même
ambiance qu'une salle pleine, quelle que soit sa lumière.

Ce qui fait qu'on reconnaît un plateau de fast-food
---------------------------------------------------
Dans l'ordre où l'œil les prend, à trois mètres :

1. **Le rouge du plateau.** Il n'y a aucun autre rouge saturé HORIZONTAL dans
   la salle ; c'est ce qui accroche l'œil avant qu'on distingue ce qu'il porte.
2. **La silhouette du pain du haut.** Un dôme, pas un disque — c'est la seule
   pièce du burger qui se reconnaisse d'aussi loin, et un empilement de
   galettes se lit comme une pile de jetons.
3. **Les frites qui dépassent** de la barquette, inclinées et de hauteurs
   inégales. Une barquette remplie à ras est une boîte rouge.
4. **La paille**, penchée. Verticale, elle se confond avec le bord du gobelet
   et l'objet redevient un cylindre blanc.

Les cotes sont celles d'un vrai plateau : 36 × 26 cm, burger de 9 cm de
diamètre pour 7,5 de haut, gobelet de 40 cl haut de 13. Elles comptent — un
plateau de 50 cm poserait au joueur la question de sa taille à lui, et la
réponse (1,66 m à l'œil) est le réglage le plus chèrement acquis du projet.

Pourquoi ce script n'emploie AUCUN `bpy.ops` de modélisation
------------------------------------------------------------
`billard.py` et `borne.py` passent par les opérateurs, et ils ne tournent donc
qu'en `--background` : `bpy.ops.object.join()` exige un contexte de fenêtre, et
`bpy.context.object` n'existe pas quand le script arrive par le pont MCP —
mesuré, les deux échouent. Ici tout est construit en `bmesh`, qui ne dépend
d'aucun contexte. Le même fichier tourne donc à l'identique au build, dans une
fenêtre ouverte, et à travers le pont — et c'est ce qui permet de RELIRE le
modèle en 3D pendant qu'on l'écrit au lieu de l'exporter à l'aveugle.

Le maillage est TRIANGULÉ ici et non à l'import : le compte annoncé est alors
le compte livré.
"""
import bmesh
import bpy
import math
import mathutils
import os
import random
import sys

# ---------------------------------------------------------------------------
# LES MATÉRIAUX, dans l'ordre où `roomgen` les associera.
#
# L'ordre est un CONTRAT, pas une préférence : `roomgen` associe les primitives
# du glTF aux matériaux de la salle PAR INDICE — `salle.room.json` le fait déjà
# pour le billard, dont les cinq matériaux sont listés dans l'ordre du fichier.
# Un exportateur qui réordonnerait sa table donnerait un pain en carton.
# `exporter` le vérifie sur le fichier écrit plutôt que de faire confiance.
# ---------------------------------------------------------------------------
# HUIT, ET PAS NEUF. `roomgen` n'accepte pas plus de huit matériaux par prop —
# c'est un tableau local de taille fixe, `int32_t by_index[8]` (roomgen.c:4915),
# et il arrête le build proprement au neuvième. La contrainte a été rencontrée,
# pas devinée.
#
# Le neuvième était la PAILLE, et c'est elle qu'on fusionne avec le plateau
# plutôt qu'une autre : les deux sont rouges, et une paille se reconnaît à sa
# forme — un cylindre de 8 mm penché au-dessus d'un gobelet blanc — pas à sa
# nuance. Fusionner la salade et la viande, au contraire, aurait coûté le seul
# vert du modèle.
MATERIAUX = ["rouge", "pain", "salade", "viande",
             "carton", "frite", "gobelet", "couvercle"]
MAT_GOBELET = ["gobelet", "couvercle", "rouge"]

# ---------------------------------------------------------------------------
# Les cotes, en mètres, toutes mesurables sur un vrai plateau.
# ---------------------------------------------------------------------------
PL_L, PL_P = 0.360, 0.260       # plateau : 36 x 26 cm
PL_FOND = 0.006                 # 6 mm de fond
PL_BORD = 0.020                 # 2 cm de rebord : assez pour se lire de loin,
                                # assez peu pour cacher ce qu'on pose dessus
BG_R = 0.045                    # burger : 9 cm de diamètre
GO_H = 0.130                    # gobelet : 13 cm, soit 40 cl
GO_BAS, GO_HAUT = 0.032, 0.042  # évasé, comme tout gobelet en carton
FR_H = 0.090                    # barquette de frites

# La graine est FIXE, et c'est ce qui rend le modèle reproductible au bit près :
# six frites tirées au hasard à chaque exécution donneraient un fichier différent
# à chaque build, et le dépôt verrait un modèle « modifié » sans que personne ne
# l'ait touché. 19 parce que le jeu s'appelle Nineteen.
GRAINE = 19


class Corps:
    """Un maillage en construction, et la table des matériaux qui va avec.

    LE MARQUAGE SE FAIT PAR LES SOMMETS QUE L'OPÉRATION REND, et pas par une
    tranche `bm.faces[avant:]`. La tranche paraît évidente et elle est FAUSSE :
    l'ordre de `bm.faces` est celui du pool mémoire de BMesh, pas celui de la
    création. Mesuré sur ce modèle-ci — 52 faces sur 300 échappaient à leur
    tranche et retombaient sur l'indice 0 par défaut, ce qui donnait un plateau
    de 82 faces au lieu de 30 et une galette de viande de DEUX. Le défaut est
    silencieux : le compte total reste juste, seule la répartition est fausse,
    et sur des matériaux gris personne ne le voit avant de poser les couleurs.

    Les sommets rendus, eux, sont exactement ceux de la primitive, et toutes
    ses faces leur sont attachées puisqu'elle est disjointe du reste.
    """

    def __init__(self, materiaux):
        self.bm = bmesh.new()
        self.materiaux = list(materiaux)

    def _marquer(self, retour, mat):
        indice = self.materiaux.index(mat)
        faces = set()
        for v in retour["verts"]:
            faces.update(v.link_faces)
        for f in faces:
            f.material_index = indice
        return len(faces)

    def boite(self, mat, sx, sy, sz, cx, cy, cz, rz=0.0):
        m = (mathutils.Matrix.Translation((cx, cy, cz))
             @ mathutils.Matrix.Rotation(rz, 4, "Z")
             @ mathutils.Matrix.Diagonal((sx, sy, sz, 1.0)))
        self._marquer(bmesh.ops.create_cube(self.bm, size=1.0, matrix=m), mat)

    def tronc(self, mat, r_bas, r_haut, h, cx, cy, cz, seg=16, ry=0.0, rz=0.0):
        m = (mathutils.Matrix.Translation((cx, cy, cz))
             @ mathutils.Matrix.Rotation(ry, 4, "Y")
             @ mathutils.Matrix.Rotation(rz, 4, "Z"))
        self._marquer(
            bmesh.ops.create_cone(self.bm, cap_ends=True, cap_tris=False,
                                  segments=seg, radius1=r_bas, radius2=r_haut,
                                  depth=h, matrix=m), mat)

    def dome(self, mat, r, aplati, cx, cy, cz, u=16, v=8):
        m = (mathutils.Matrix.Translation((cx, cy, cz))
             @ mathutils.Matrix.Diagonal((1.0, 1.0, aplati, 1.0)))
        self._marquer(
            bmesh.ops.create_uvsphere(self.bm, u_segments=u, v_segments=v,
                                      radius=r, matrix=m), mat)

    def verifier(self, attendu):
        """Le compte de faces par matériau, AVANT triangulation, contre ce qu'on
        a demandé. C'est ce contrôle qui a trouvé la viande à deux faces ; sans
        lui le modèle sortait juste en nombre de triangles et faux en couleurs."""
        vu = {}
        for f in self.bm.faces:
            n = self.materiaux[f.material_index]
            vu[n] = vu.get(n, 0) + 1
        if vu != attendu:
            raise SystemExit("repartition des faces inattendue :\n  vu      %r\n"
                             "  attendu %r" % (vu, attendu))
        return vu


def gobelet(c, cx, cy, cz):
    """Le gobelet et sa paille. Écrit à part : il sert seul ET sur le plateau."""
    c.tronc("gobelet", GO_BAS, GO_HAUT, GO_H, cx, cy, cz + GO_H * 0.5)
    # Le couvercle déborde de 2 mm du bord haut. Sans ce débord il disparaît :
    # deux disques du même diamètre ne se distinguent pas à trois mètres.
    c.tronc("couvercle", GO_HAUT + 0.002, GO_HAUT + 0.002, 0.010,
            cx, cy, cz + GO_H + 0.005)
    # 12 degrés. Verticale, la paille se confond avec le bord du gobelet et
    # l'objet redevient un cylindre blanc.
    c.tronc("rouge", 0.004, 0.004, 0.100, cx + 0.010, cy, cz + GO_H + 0.049,
            seg=8, ry=math.radians(12))


def burger(c, cx, cy, cz):
    """Le burger, quatre pièces. Le pain du haut est un dôme et non un disque :
    c'est la seule silhouette qui se reconnaisse de trois mètres."""
    c.tronc("pain", BG_R, BG_R, 0.018, cx, cy, cz + 0.009)
    c.tronc("salade", BG_R + 0.005, BG_R + 0.005, 0.006, cx, cy, cz + 0.021)
    c.tronc("viande", BG_R + 0.002, BG_R + 0.002, 0.015, cx, cy, cz + 0.031)
    # Aplati à 0,72 : une sphère entière ferait une balle posée sur un steak.
    c.dome("pain", BG_R + 0.002, 0.72, cx, cy, cz + 0.041)


def frites(c, cx, cy, cz, tirage):
    """La barquette et six frites. Le nombre compte : à trois, on compte les
    frites ; à douze, la barquette redevient un bloc jaune."""
    # 45 degres. Un cone a quatre segments sort ARETE EN AVANT : posee sur le
    # plateau, la barquette se lit alors comme un losange, c'est-a-dire comme
    # rien du tout. Tournee d'un huitieme de tour, elle presente une face plate
    # au joueur et redevient une boite.
    c.tronc("carton", 0.042, 0.055, FR_H, cx, cy, cz + FR_H * 0.5, seg=4,
            rz=math.radians(45.0))
    for _ in range(6):
        a = tirage.uniform(0.0, math.tau)
        d = tirage.uniform(0.004, 0.026)
        # Hauteurs inégales : six frites de même longueur forment un peigne, et
        # un peigne ne ressemble à rien.
        h = tirage.uniform(0.060, 0.085)
        c.boite("frite", 0.008, 0.008, h,
                cx + math.cos(a) * d, cy + math.sin(a) * d,
                cz + FR_H - 0.012 + h * 0.5, tirage.uniform(0.0, 1.2))


def plateau(c, tirage):
    """Le plateau, son rebord, et ce qu'on pose dessus."""
    c.boite("rouge", PL_L, PL_P, PL_FOND, 0.0, 0.0, PL_FOND * 0.5)
    demi_l, demi_p = PL_L * 0.5, PL_P * 0.5
    for sx, sy, cx, cy in ((PL_L, 0.014, 0.0, demi_p - 0.007),
                           (PL_L, 0.014, 0.0, -demi_p + 0.007),
                           (0.014, PL_P - 0.028, demi_l - 0.007, 0.0),
                           (0.014, PL_P - 0.028, -demi_l + 0.007, 0.0)):
        c.boite("rouge", sx, sy, PL_BORD, cx, cy, PL_FOND + PL_BORD * 0.5)
    burger(c, -0.085, 0.010, PL_FOND)
    frites(c, 0.030, -0.055, PL_FOND, tirage)
    gobelet(c, 0.120, 0.030, PL_FOND)


def deposer(corps, nom):
    """Triangule, projette des UV, cale l'origine, et pose l'objet dans la scène.

    L'origine n'est pas un détail : `salle.room.json` pose un prop par son
    EMPREINTE AU SOL, et un modèle dont l'origine est au barycentre du maillage
    flotte de la moitié de sa hauteur. L'audit P-02 l'a payé — onze centimètres
    d'air sous une poubelle métallique.
    """
    bm = corps.bm
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method="BEAUTY",
                          ngon_method="BEAUTY")

    # Recentrage sur l'empreinte, base à z = 0.
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    dep = mathutils.Vector((-(min(xs) + max(xs)) * 0.5,
                           -(min(ys) + max(ys)) * 0.5,
                           -min(zs)))
    bmesh.ops.translate(bm, verts=bm.verts[:], vec=dep)

    # Une projection cubique, au mètre. Les matériaux de la garniture sont des
    # aplats — mais un glTF sans coordonnées de texture ferait échouer l'import
    # côté moteur, qui les lit sans les rendre facultatives. Le pas d'un mètre
    # est arbitraire ET sans effet : aucun de ces neuf matériaux ne porte
    # d'image.
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for boucle in f.loops:
            co = boucle.vert.co
            u, v = (co.y, co.z) if ax == 0 else ((co.x, co.z) if ax == 1 else (co.x, co.y))
            boucle[uv].uv = (u, v)

    me = bpy.data.meshes.new(nom)
    bm.to_mesh(me)
    bm.free()
    for m in corps.materiaux:
        mat = bpy.data.materials.get(m) or bpy.data.materials.new(m)
        me.materials.append(mat)
    ob = bpy.data.objects.new(nom, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def vider():
    """Vide la scène sans `read_factory_settings`, que le bac à sable du pont
    MCP refuse — mesuré. Retirer les données une à une marche partout."""
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    for ma in list(bpy.data.materials):
        bpy.data.materials.remove(ma)


def exporter(chemin, attendus):
    os.makedirs(os.path.dirname(chemin), exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=chemin,
        export_format="GLTF_SEPARATE",
        export_yup=True,
        export_normals=True,
        export_texcoords=True,
        export_materials="EXPORT",
        export_apply=True,
        use_selection=False,
    )
    # L'ordre des matériaux DANS LE FICHIER, vérifié et non supposé : voir le
    # commentaire de MATERIAUX. Le même contrôle existe dans `billard.py`.
    import json
    with open(chemin, "r", encoding="utf-8") as f:
        noms = [m["name"] for m in json.load(f).get("materials", [])]
    if noms != attendus:
        raise SystemExit("ordre des materiaux inattendu : %r au lieu de %r"
                         % (noms, attendus))
    return noms


# LA RÉPARTITION ATTENDUE DES FACES, avant triangulation. Ce n'est pas une
# constante décorative : c'est l'oracle qui a trouvé le défaut de marquage (voir
# `Corps`). Un cône à n segments donne n + 2 faces, un cube 6, la sphère 16 x 8
# en donne 128 — les trois se recomptent à la main, et c'est pour ça qu'ils sont
# écrits ici plutôt que relevés sur une exécution.
ATTENDU = {
    "plateau_repas": {"rouge": 40,        # 5 boîtes (30) + la paille (10)
                      "pain": 146,         # 1 cône (18) + 1 dôme (128)
                      "salade": 18, "viande": 18,
                      "carton": 6,         # cône à 4 segments
                      "frite": 36,         # 6 boîtes
                      "gobelet": 18, "couvercle": 18},
    "gobelet": {"gobelet": 18, "couvercle": 18, "rouge": 10},
}


def construire(racine):
    """Les deux modèles. Rend la liste (nom, triangles, matériaux)."""
    rapport = []
    for nom, mats, faire in (
            ("plateau_repas", MATERIAUX,
             lambda c: plateau(c, random.Random(GRAINE))),
            ("gobelet", MAT_GOBELET,
             lambda c: gobelet(c, 0.0, 0.0, 0.0))):
        vider()
        corps = Corps(mats)
        faire(corps)
        corps.verifier(ATTENDU[nom])
        ob = deposer(corps, nom)
        ordre = exporter(os.path.join(racine, nom, nom + ".gltf"), mats)
        rapport.append((nom, len(ob.data.polygons), ordre))
        print("PLATEAU %s : %d triangles, materiaux %s"
              % (nom, len(ob.data.polygons), ", ".join(ordre)))
    return rapport


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    racine = "assets/models"
    if "--out" in argv:
        racine = argv[argv.index("--out") + 1]
    construire(os.path.abspath(racine))


if __name__ == "__main__":
    main()
