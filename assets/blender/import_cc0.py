"""
import_cc0.py — un modèle CC0 de Poly Haven, rendu utilisable par ce jeu.

    /Applications/Blender.app/Contents/MacOS/Blender --background \\
        --python assets/blender/import_cc0.py -- \\
        --in <source.gltf> --out assets/models/<nom>/<nom>.gltf --triangles 2500

Pourquoi une étape, et pas un import direct
-------------------------------------------
Les modèles de Poly Haven sont taillés pour le rendu hors ligne : quatorze mille
triangles pour un tabouret, huit mille pour un canapé. `assets/cc0/LICENSES.md`
disait que c'était « ce qui limite le mobilier importé à trois modèles », et la
raison invoquée — un rastériseur logiciel qui cessait de composer l'image
au-delà de 160 000 sommets — était celle du CONTENEUR de développement, pas
celle du jeu. Sur un vrai GPU la salle en porte 149 000 et rend en 6 ms.

La vraie raison de dégrossir reste, et elle est plus simple : un pot de fleurs vu
à trois mètres n'a pas besoin de la définition d'un gros plan de cinéma. Le
décimateur de Blender fait ça très bien, à l'export, une fois — ce qui règle
aussi le poste « un décimateur de maillage » que le journal réclamait depuis A2b
sans qu'il faille en écrire un en C.

Ce qui est gardé, ce qui est jeté
---------------------------------
On garde la géométrie décimée, les normales et les UV. On JETTE les matériaux du
modèle : la salle décide de la matière, comme pour tous les autres props, et
c'est ce qui garde une seule table de matériaux — donc un seul endroit où régler
l'aspect du décor. Seul l'albédo est versionné à côté ; la normale et l'ORM sont
dérivées au build par `texgen`, comme pour le reste.

Le décimateur en mode COLLAPSE et non « non-planaire » : sur un objet organique
— une plante, un fauteuil — le second garde les faces planes et détruit les
courbes, ce qui est exactement l'inverse de ce qu'on veut. Collapse répartit la
perte sur toute la surface.
"""
import bpy
import math
import os
import sys


def arg(name, default=None):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


def bbox_monde(ob):
    pts = [ob.matrix_world @ v.co for v in ob.data.vertices]
    if not pts:
        return None
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    return lo, hi


def garder_un_objet(meshes):
    """Ne garder qu'UN objet, quand le fichier en contient plusieurs côte à côte.

    Poly Haven livre souvent plusieurs VARIANTES dans le même glTF — une
    poubelle propre et une rouillée, deux appliques, un couvercle ouvert et un
    fermé — simplement posées les unes à côté des autres. Les fusionner donne un
    objet deux fois trop large, et c'est mesuré : la poubelle sortait à 1,78 m
    de large et l'applique à 1,22 m, pour des objets qui en font 0,4 et 0,25.

    On garde donc l'objet le plus proche de l'origine, PLUS tout ce qui touche sa
    boîte — un poste de radio a un corps et des haut-parleurs, et ceux-là se
    chevauchent. Ce qui est posé à côté est écarté.
    """
    boxes = [(o, bbox_monde(o)) for o in meshes]
    boxes = [(o, b) for (o, b) in boxes if b]
    if len(boxes) <= 1:
        return [o for (o, _b) in boxes]

    def dist2(b):
        c = [(b[0][k] + b[1][k]) * 0.5 for k in range(3)]
        return c[0] * c[0] + c[2] * c[2]      # distance horizontale à l'origine

    boxes.sort(key=lambda ob: dist2(ob[1]))
    gardes = [boxes[0]]
    change = True
    while change:
        change = False
        for cand in list(boxes):
            if cand in gardes:
                continue
            for g in gardes:
                a, b = cand[1], g[1]
                if all(a[0][k] <= b[1][k] + 0.02 and a[1][k] >= b[0][k] - 0.02
                       for k in range(3)):
                    gardes.append(cand)
                    change = True
                    break

    if len(gardes) < len(boxes):
        print("IMPORT variantes écartées : %d objet(s) sur %d"
              % (len(boxes) - len(gardes), len(boxes)))
    for (o, _b) in boxes:
        if (o, _b) not in gardes:
            bpy.data.objects.remove(o, do_unlink=True)
    return [o for (o, _b) in gardes]


def main():
    src = arg("--in")
    out = arg("--out")
    budget = int(arg("--triangles", "2500"))
    echelle = float(arg("--scale", "1.0"))
    if not src or not out:
        raise SystemExit("usage : --in <source.gltf> --out <sortie.gltf> "
                         "[--triangles N]")
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.abspath(src))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("aucun maillage dans %s" % src)

    meshes = garder_un_objet(meshes)

    # Tout fusionner : la salle instancie UN objet, pas une hiérarchie.
    bpy.ops.object.select_all(action="DESELECT")
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    me = ob.data
    avant = len(me.polygons)

    # Un seul emplacement de matériau : la salle le remplira.
    me.materials.clear()
    mat = bpy.data.materials.get("import") or bpy.data.materials.new("import")
    me.materials.append(mat)
    for f in me.polygons:
        f.material_index = 0

    # Triangulation d'abord : décimer des quads puis trianguler donne un compte
    # final imprévisible, et c'est le compte final qui est le budget.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY", ngon_method="BEAUTY")
    bpy.ops.object.mode_set(mode="OBJECT")
    tris = len(ob.data.polygons)

    if tris > budget:
        d = ob.modifiers.new("decimation", "DECIMATE")
        d.decimate_type = "COLLAPSE"
        d.ratio = float(budget) / float(tris)
        # Les coutures d'UV et les bords ouverts sont PRÉSERVÉS : sans ça, le
        # décimateur ferme les trous et fait glisser la texture d'un îlot sur
        # l'autre, ce qui se voit tout de suite sur un objet texturé.
        d.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=d.name)

    # Normales lissées : la décimation crée des facettes, et un objet facetté à
    # trois mètres se lit comme un objet en papier plié.
    bpy.ops.object.shade_smooth()
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40.0))
    except (AttributeError, RuntimeError):
        pass

    if not me.uv_layers:
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(60.0), island_margin=0.02)
        bpy.ops.object.mode_set(mode="OBJECT")

    if echelle != 1.0:
        # Un modèle dont la cote réelle est fausse se corrige ICI, une fois, et
        # pas par un facteur dans la description de la salle : c'est le modèle
        # qui est à la mauvaise échelle, pas son emploi.
        ob.scale = (echelle, echelle, echelle)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    apres = len(ob.data.polygons)
    print("IMPORT %s : %d -> %d triangles" % (os.path.basename(out), avant, apres))

    bpy.ops.export_scene.gltf(
        filepath=out,
        export_format="GLTF_SEPARATE",
        export_yup=True,
        export_normals=True,
        export_texcoords=True,
        export_materials="EXPORT",
        export_apply=True,
        use_selection=False,
        export_image_format="NONE",   # les textures sont gérées à part
    )


main()
