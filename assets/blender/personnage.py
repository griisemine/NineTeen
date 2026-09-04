"""
personnage.py — le cycle de marche, MESURÉ et corrigé sous Blender.

    /Applications/Blender.app/Contents/MacOS/Blender --background --online-mode \
        --python assets/blender/personnage.py -- \
        --entree assets/models/personnage/personnage.glb \
        --sortie /tmp/personnage-corrige.glb

CE SCRIPT N'EST PAS APPLIQUÉ, ET C'EST LE POINT LE PLUS IMPORTANT
==================================================================
Il produit un cycle de marche mesurablement meilleur, et le jeu livre quand
même le cycle d'origine. La raison est plus bas, section « pourquoi ça ne peut
pas être livré ». Ce fichier existe pour deux choses : garder la MESURE du
défaut, qui a coûté une demi-journée, et garder la correction toute prête pour
le jour où le moteur pourra l'accueillir.

Ce que le cycle livré a de faux, et comment on l'a mesuré
--------------------------------------------------------
Le personnage est CesiumMan (CC-BY 4.0, voir `assets/cc0/LICENSES.md`). Son
cycle a été relu image par image dans Blender, articulation par articulation :

1. **Le pied posé glisse.** Sur une phase d'appui, la vitesse de recul du pied
   porteur devrait être constante. L'écart au trajet à vitesse constante monte
   à **0,258 unité de fichier**, soit 31 cm à l'échelle du jeu. C'est le « pas
   de pied cloué au sol » que `ns_skin.h` décrivait sans le chiffrer.

2. **La jambe est tendue à fond.** La distance hanche-cheville atteint
   **0,5419**, c'est-à-dire EXACTEMENT cuisse + tibia, sur 29 des 96 couples
   (image, jambe). C'est la CAUSE du point 1 : la jambe ne peut pas atteindre
   le pas demandé, alors le pied dérape pour compenser.

3. **La foulée du fichier n'est pas celle du jeu.** `ns_skin` en mesure
   1,306 m là où la salle en emploie 1,550 — les bruits de pas, l'oscillation
   de la tête et le viewmodel lisent tous cette dernière. 16 % d'écart
   permanent, sous le seuil de 33 % qui aurait déclenché l'avertissement.

Ce que la correction obtient
----------------------------
Mesuré sur le cycle produit, et vérifié par le moteur qui l'a chargé :

    glissement du pied posé   0,258 unité  ->  0,000001 par image
    raccord de la boucle      ~1 cm        ->  0,00000000
    extension de la jambe     1,0000       ->  0,9900 au plus
    foulée                    1,306 m      ->  1,550 m, celle de la salle

Le bassin descend de 2,8 cm au plus (1,6 cm en moyenne), soit 1,6 % de la
taille du personnage : c'est ce que fait un marcheur réel quand il allonge le
pas, et c'est le prix pour que la jambe atteigne sans se verrouiller.

POURQUOI ÇA NE PEUT PAS ÊTRE LIVRÉ
===================================
`tests/test_allure.c` tombe, sur cinq contrôles à la fois, et il a raison.

L'accroupi du jeu n'est pas animé : il est DÉRIVÉ. `ns_skin_crouch_calibrate`
part de la « pose de passage » — l'instant où les deux pieds se croisent — et y
ajoute un angle `a` à la cuisse et `-2a` au genou jusqu'à obtenir la bonne
hauteur. Cette dérivation a besoin d'une jambe porteuse QUASI VERROUILLÉE :

    pose de passage        genou porteur   angle d'accroupi trouvé
    cycle d'origine           173,5°              67,5°     <- plausible
    cycle corrigé             151,6°             105,5°     <- genou a 211°

Et l'écart de genou vient d'un écart d'extension de 2,9 % seulement : près de
l'extension totale, l'angle du genou est extrêmement sensible à la longueur.
Or corriger le glissement, c'est précisément dessouder la jambe de sa butée.

Ce n'est donc pas un réglage à trouver. Cinq variantes ont été essayées, avec
`tests/test_allure.c` comme oracle, et toutes échouent de la même façon :

    réserve de flexion 0,99 / 0,995 / 0,999 / 0,9999 / 0,99999   105,2 a 105,6°
    foulée ramenée a 1,30 m (l'ampleur d'origine)                105,2°
    longueur de jambe conservée image par image en appui         106,9°

Une bissection le confirme : le cycle d'ORIGINE repassé par cette même
tuyauterie — même greffe, même plage d'images, même réécriture des clés —
passe le test. C'est bien la correction qui est incompatible, pas l'outil.

Ce qu'il faudrait faire d'abord
-------------------------------
Donner à l'accroupi une cinématique inverse PAR JAMBE, au lieu du pli à angle
imposé. C'est déjà écrit noir sur blanc dans `tests/test_allure.c` :

    « Le mettre à zéro demanderait un accroupi qui respecte le contact de
      CHAQUE pied, donc une cinématique inverse par jambe — c'est écrit dans
      le journal comme la suite possible, ce n'est pas fait ici. »

Le moteur a le module qu'il faut (`engine/anim/ns_ik.c`). Le jour où l'accroupi
s'en sert, ce script se relance et son résultat se livre.

Notes de méthode, pour qui reprendra
------------------------------------
  - Les articulations de ce squelette sont les TÊTES d'os. Les os pointent tous
    vers +x et leur « queue » n'est l'articulation suivante d'aucune d'elles :
    une contrainte IK de Blender, qui vise la queue, est inutilisable ici. D'où
    la résolution analytique à deux segments.
  - Le VOL se raccorde par une Hermite cubique, tangentes égales a la vitesse
    d'appui aux deux bouts. Une transformation affine du trajet d'origine a été
    essayée et elle est fausse : elle amplifie le dépassement du cycle en même
    temps qu'elle allonge l'appui — 17 cm de trop, mesurés.
  - En vol on LÈVE le pied plutôt que de baisser le bassin. Baisser le bassin
    pour l'appui éloigne la cible en vol, ce qui redemande de baisser : la
    descente partait à 19,85 cm. Découplé, elle retombe a 2,8.
  - Le cycle est TOURNÉ dans le temps pour que le croisement des pieds retombe
    où il était. Sans ça la pose de passage se déplace de l'image 7 a l'image 0.
    Décaler les deux pieds l'un par rapport a l'autre a aussi été essayé, et
    c'est faux : ça fait marcher le personnage un pied en avant en permanence,
    et le bassin doit descendre de 25,5 cm.
  - La sortie est le fichier d'ORIGINE dont on ne remplace que les canaux
    d'animation. Un export complet par Blender réécrit le squelette et en CHANGE
    l'ordre des articulations : `ns_skin` repère l'épaule par balayage et
    tombait sur l'os 11 au lieu de l'os 5.
  - `ns_skin` mesure la hauteur du personnage a l'instant 0 de l'ANIMATION, et
    `room/main.c` en tire l'échelle du modèle. Corriger le cycle déplace donc
    l'échelle, dont dépend la foulée visée : le calcul dépend de son propre
    résultat. On itère jusqu'au point fixe — sans ça la foulée livrée vaut
    1,519 m au lieu des 1,550 demandés.
  - `--identite` rejoue le cycle d'origine dans la même tuyauterie sans rien
    corriger. C'est la bissection qui a innocenté l'outil.
"""

import argparse
import json
import math
import os
import struct
import sys
import tempfile

import bpy
from mathutils import Matrix, Vector

# ---------------------------------------------------------------------------
# Les cotes, et d'où elles sortent
# ---------------------------------------------------------------------------

# Le cycle livré : 48 images à 24 images par seconde, soit 2,00 s. C'est la
# durée que le jeu annonce déjà dans son journal ; on ne la change pas.
IMAGES = 48

# La taille visée par la salle, en mètres : `personnage.taille` de
# nineteen.env. `room/main.c` met le modèle à cette taille en le divisant par
# la hauteur qu'il mesure à l'instant 0 du cycle — d'où l'itération plus bas.
TAILLE_M = 1.78

# Point de départ de l'itération : l'échelle mesurée sur le cycle d'origine.
ECHELLE_DEPART = 1.78 / 1.46

# La foulée employée par la salle, en mètres. `STRIDE_DEFAULT` de
# `room/room_camera.c`. C'est la valeur que le cycle doit désormais TENIR.
FOULEE_M = 1.55

# Réserve de flexion du genou — voir l'en-tête pour le tableau mesuré.
RESERVE = 0.99

# Les appuis, mesurés sur le cycle d'origine : à chaque image, le pied porteur
# est celui dont l'ORTEIL est le plus bas. Les intervalles obtenus sont
# continus, le droit passant par le bouclage. Ils sont écrits en clair pour que
# la correction reste reproductible même si le seuil de mesure changeait.
APPUIS = {"G": list(range(13, 38)), "D": list(range(38, 61))}

JAMBES = {
    "G": ("leg_joint_L_1", "leg_joint_L_2", "leg_joint_L_3", "leg_joint_L_5"),
    "D": ("leg_joint_R_1", "leg_joint_R_2", "leg_joint_R_3", "leg_joint_R_5"),
}
RACINE = "Skeleton_torso_joint_1"

# Le lissage de la descente de bassin. Un bassin qui saute d'une image à
# l'autre se voit ; le noyau est appliqué en boucle FERMÉE, et le résultat est
# reborné à chaque tour pour ne jamais passer sous ce que la portée exige.
NOYAU = (0.06, 0.24, 0.40, 0.24, 0.06)
TOURS = 80


def lire_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--entree", required=True)
    p.add_argument("--sortie", required=True)
    # Réglables en ligne de commande pour pouvoir BALAYER : `tests/test_allure.c`
    # est l'oracle, et il vaut mieux le consulter que raisonner sur la fragilité
    # d'une heuristique du moteur.
    p.add_argument("--reserve", type=float, default=RESERVE)
    p.add_argument("--foulee", type=float, default=FOULEE_M)
    p.add_argument("--tours", type=int, default=TOURS)
    # Bissection : rejoue le cycle d'origine dans la MÊME tuyauterie, sans
    # corriger quoi que ce soit. Si un contrôle tombe déjà là, c'est la
    # tuyauterie qui est en cause, pas la correction.
    p.add_argument("--identite", action="store_true")
    return p.parse_args(argv)


def rotation_entre(d0, d1):
    """La rotation qui amène la direction d0 sur d1."""
    d0 = d0.normalized()
    d1 = d1.normalized()
    axe = d0.cross(d1)
    if axe.length < 1e-9:
        return Matrix.Identity(3)
    return Matrix.Rotation(d0.angle(d1), 3, axe.normalized())


def hauteur_posee(mesh, image, scene):
    """La hauteur du maillage PESÉ à une image donnée, comme `ns_skin` la mesure.

    Le moteur applique les matrices d'os puis prend l'étendue verticale ; on
    fait pareil, sur le maillage évalué, sinon on mesurerait la pose de liaison
    qui n'est pas celle qu'il regarde.
    """
    scene.frame_set(image)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = mesh.evaluated_get(dg)
    me = ev.to_mesh()
    bas, haut = 1e9, -1e9
    for v in me.vertices:
        z = (ev.matrix_world @ v.co).z
        bas = min(bas, z)
        haut = max(haut, z)
    ev.to_mesh_clear()
    return haut - bas


def transplanter(origine, greffon, sortie):
    """Réécrit les canaux d'animation de `origine` avec ceux de `greffon`.

    Tout le reste de `origine` est repris tel quel : c'est ce qui garantit que
    l'ordre des articulations, les matrices de liaison et le maillage ne
    bougent pas. Les nœuds sont appariés par leur NOM, les deux fichiers
    portant les mêmes.
    """
    def charger(chemin):
        d = open(chemin, "rb").read()
        off, morceaux = 12, []
        while off < len(d):
            ln, ty = struct.unpack_from("<II", d, off)
            morceaux.append((ty, off + 8, ln))
            off += 8 + ln
        js = json.loads(d[morceaux[0][1]:morceaux[0][1] + morceaux[0][2]])
        binaire = d[morceaux[1][1]:morceaux[1][1] + morceaux[1][2]]
        return js, bytearray(binaire)

    J0, B0 = charger(origine)
    J1, B1 = charger(greffon)

    COMPOSANTES = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}

    def lire(js, binaire, indice):
        acc = js["accessors"][indice]
        bv = js["bufferViews"][acc["bufferView"]]
        base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
        n = COMPOSANTES[acc["type"]]
        pas_ = bv.get("byteStride") or n * 4
        return [struct.unpack_from("<" + "f" * n, binaire, base + k * pas_)
                for k in range(acc["count"])]

    par_nom = {}
    for i, n in enumerate(J0["nodes"]):
        if n.get("name"):
            par_nom[n["name"]] = i

    def ajouter(valeurs, type_):
        """Ajoute un accesseur au buffer d'origine et rend son indice."""
        n = COMPOSANTES[type_]
        while len(B0) % 4:
            B0.append(0)
        debut = len(B0)
        for v in valeurs:
            B0.extend(struct.pack("<" + "f" * n, *v))
        J0["bufferViews"].append({"buffer": 0, "byteOffset": debut,
                                  "byteLength": len(B0) - debut})
        acc = {"bufferView": len(J0["bufferViews"]) - 1, "componentType": 5126,
               "count": len(valeurs), "type": type_}
        # glTF EXIGE min/max sur l'entrée d'un échantillonneur : sans eux, le
        # lecteur ne sait pas quand le clip commence ni finit.
        acc["min"] = [min(v[k] for v in valeurs) for k in range(n)]
        acc["max"] = [max(v[k] for v in valeurs) for k in range(n)]
        J0["accessors"].append(acc)
        return len(J0["accessors"]) - 1

    anim1 = J1["animations"][0]
    canaux, echantillonneurs = [], []
    manquants = []
    for c in anim1["channels"]:
        nom = J1["nodes"][c["target"]["node"]].get("name")
        if nom not in par_nom:
            manquants.append(nom)
            continue
        e = anim1["samplers"][c["sampler"]]
        chemin = c["target"]["path"]
        type_ = "VEC4" if chemin == "rotation" else "VEC3"
        entree = ajouter(lire(J1, B1, e["input"]), "SCALAR")
        sortie_ = ajouter(lire(J1, B1, e["output"]), type_)
        echantillonneurs.append({"input": entree, "interpolation": "LINEAR",
                                 "output": sortie_})
        canaux.append({"sampler": len(echantillonneurs) - 1,
                       "target": {"node": par_nom[nom], "path": chemin}})

    if manquants:
        raise SystemExit("noeuds absents du fichier d'origine : %s" % sorted(set(manquants)))

    J0["animations"] = [{"name": "marche", "channels": canaux,
                         "samplers": echantillonneurs}]
    J0["buffers"][0]["byteLength"] = len(B0)

    brut = json.dumps(J0, separators=(",", ":")).encode("utf-8")
    brut += b" " * ((4 - len(brut) % 4) % 4)
    while len(B0) % 4:
        B0.append(0)
    total = 12 + 8 + len(brut) + 8 + len(B0)
    with open(sortie, "wb") as fp:
        fp.write(struct.pack("<III", 0x46546C67, 2, total))
        fp.write(struct.pack("<II", len(brut), 0x4E4F534A))
        fp.write(brut)
        fp.write(struct.pack("<II", len(B0), 0x004E4942))
        fp.write(B0)
    return len(canaux), total


def construire(arm, scene, monde, echelle, reserve, foulee_m, tours,
               identite=False):
    """Calcule les poses corrigées pour une échelle donnée.

    Rend (poses, ordre, diagnostic). Rien n'est écrit : l'appelant itère sur
    l'échelle avant de retenir un résultat.
    """
    rot = monde.to_3x3()
    rot_inv = rot.inverted()
    cuisse = arm.data.bones["leg_joint_L_1"].length
    tibia = arm.data.bones["leg_joint_L_2"].length
    jambe = cuisse + tibia
    portee = jambe * reserve

    foulee = foulee_m / echelle
    pas = foulee / IMAGES

    P = {c: {k: [] for k in ("hanche", "genou", "cheville", "orteil")} for c in "GD"}
    for f in range(1, IMAGES + 1):
        scene.frame_set(f)
        bpy.context.view_layer.update()
        for c, (h, g, a, t) in JAMBES.items():
            for cle, os_ in (("hanche", h), ("genou", g), ("cheville", a), ("orteil", t)):
                P[c][cle].append(monde @ arm.pose.bones[os_].head)

    # LA LONGUEUR DE CHAQUE JAMBE, IMAGE PAR IMAGE, telle que le cycle
    # d'origine la porte. C'est elle qu'on conserve, et non une fraction fixe
    # de la jambe tendue — voir l'en-tête, section « ce qu'on conserve ».
    # La longueur maximale autorisée à chaque image. Deux variantes ont été
    # essayées en plus de celle-ci — conserver la longueur d'origine partout,
    # puis seulement en appui — et toutes deux échouent au même contrôle tout
    # en coûtant plus cher au bassin (15,5 cm au lieu de 2,8). Voir l'en-tête.
    tendue = {c: [jambe * reserve for _ in range(IMAGES)] for c in "GD"}

    en_appui = {c: set(k % IMAGES for k in grp) for c, grp in APPUIS.items()}

    cible = {c: [0.0] * IMAGES for c in "GD"}
    for c, grp in APPUIS.items():
        debut, fin = grp[0], grp[-1]
        y0 = sum(P[c]["cheville"][k % IMAGES].y - pas * (k - debut) for k in grp) / len(grp)
        for k in grp:
            cible[c][k % IMAGES] = y0 + pas * (k - debut)
        depart, arrivee = y0 + pas * (fin - debut), y0
        duree = (debut + IMAGES) - fin
        tangente = pas * duree
        for k in range(fin + 1, debut + IMAGES):
            u_ = (k - fin) / duree
            cible[c][k % IMAGES] = (
                (2 * u_**3 - 3 * u_**2 + 1) * depart
                + (u_**3 - 2 * u_**2 + u_) * tangente
                + (-2 * u_**3 + 3 * u_**2) * arrivee
                + (u_**3 - u_**2) * tangente)

    # --- la phase : le croisement des pieds doit rester où il était ---
    def croisement(ecarts):
        """L'image, fractionnaire, où l'écart entre les deux pieds s'annule.

        On prend le passage par zéro le plus franc plutôt que le minimum en
        valeur absolue : un minimum se déplace d'une image pour un dixième de
        millimètre, un passage par zéro non.
        """
        meilleur, pente_max = None, 0.0
        for k in range(IMAGES):
            a_, b_ = ecarts[k], ecarts[(k + 1) % IMAGES]
            if (a_ > 0.0) != (b_ > 0.0):
                pente = abs(b_ - a_)
                if pente > pente_max:
                    pente_max = pente
                    meilleur = k + (0.0 if pente < 1e-12 else a_ / (a_ - b_))
        return meilleur

    avant = croisement([P["G"]["cheville"][k].y - P["D"]["cheville"][k].y
                        for k in range(IMAGES)])
    apres = croisement([cible["G"][k] - cible["D"][k] for k in range(IMAGES)])
    decalage = (0 if (avant is None or apres is None)
                else int(round(avant - apres)) % IMAGES)

    exige = [0.0] * IMAGES
    for c in "GD":
        for i in en_appui[c]:
            dy = abs(cible[c][i] - P[c]["hanche"][i].y)
            dx = abs(P[c]["cheville"][i].x - P[c]["hanche"][i].x)
            L = tendue[c][i]
            reste = L * L - dy * dy - dx * dx
            if reste <= 0.0:
                raise SystemExit(
                    "image %d, pied %s : le pas demandé (%.3f) dépasse la longueur "
                    "que la jambe porte a cette image (%.3f). Baisser FOULEE_M."
                    % (i, c, math.hypot(dx, dy), L))
            exige[i] = max(exige[i], P[c]["hanche"][i].z
                           - (P[c]["cheville"][i].z + math.sqrt(reste)))
    exige = [max(0.0, x) for x in exige]
    chute = list(exige)
    for _ in range(tours):
        lisse = [sum(NOYAU[j] * chute[(i + j - 2) % IMAGES] for j in range(5))
                 for i in range(IMAGES)]
        chute = [max(lisse[i], exige[i]) for i in range(IMAGES)]

    hauteur = {c: [P[c]["cheville"][i].z for i in range(IMAGES)] for c in "GD"}
    leve = {c: [0.0] * IMAGES for c in "GD"}
    for c in "GD":
        for i in range(IMAGES):
            if i in en_appui[c]:
                continue
            hanche_z = P[c]["hanche"][i].z - chute[i]
            dy = abs(cible[c][i] - P[c]["hanche"][i].y)
            dx = abs(P[c]["cheville"][i].x - P[c]["hanche"][i].x)
            L = tendue[c][i]
            reste = L * L - dy * dy - dx * dx
            leve[c][i] = max(0.0, (hanche_z - math.sqrt(max(reste, 0.0))) - hauteur[c][i])
        for _ in range(40):
            lisse = [sum(NOYAU[j] * leve[c][(i + j - 2) % IMAGES] for j in range(5))
                     for i in range(IMAGES)]
            leve[c] = [0.0 if i in en_appui[c] else max(lisse[i], leve[c][i])
                       for i in range(IMAGES)]

    ordre = []

    def descendre(os_):
        ordre.append(os_.name)
        for enfant in os_.children:
            descendre(enfant)

    for os_ in arm.data.bones:
        if os_.parent is None:
            descendre(os_)

    def autour(centre_monde, rotation_monde):
        centre = rot_inv @ centre_monde
        return (Matrix.Translation(centre)
                @ (rot_inv @ rotation_monde @ rot).to_4x4()
                @ Matrix.Translation(-centre))

    poses = []
    ecart_max = 0.0
    extension_max = 0.0
    for f in range(1, IMAGES + 1):
        i = f - 1
        scene.frame_set(f)
        bpy.context.view_layer.update()
        if identite:
            poses.append({b: arm.pose.bones[b].matrix.copy() for b in ordre})
            continue

        racine = arm.pose.bones[RACINE]
        m = racine.matrix.copy()
        m.translation = m.translation + (rot_inv @ Vector((0.0, 0.0, -chute[i])))
        racine.matrix = m
        bpy.context.view_layer.update()

        for c, (h, g, a, _t) in JAMBES.items():
            hanche = monde @ arm.pose.bones[h].head
            genou_avant = monde @ arm.pose.bones[g].head
            cheville_avant = monde @ arm.pose.bones[a].head
            vise = Vector((cheville_avant.x, cible[c][i], hauteur[c][i] + leve[c][i]))
            d = (vise - hanche).length
            # Le seuil est le MICRON, et il est mesuré : au cas d'égalité,
            # l'arrondi machine donne 1,6e-9 unité de dépassement. Un micron
            # laisse trois ordres de grandeur avant qu'un vrai défaut passe.
            if d > tendue[c][i] + 1e-6:
                raise SystemExit("image %d, pied %s : hors de portée de %.3e"
                                 % (i, c, d - tendue[c][i]))
            d = min(d, tendue[c][i])
            extension_max = max(extension_max, d / jambe)

            u = (vise - hanche).normalized()
            cosinus = (d * d + cuisse * cuisse - tibia * tibia) / (2.0 * cuisse * d)
            angle = math.acos(max(-1.0, min(1.0, cosinus)))
            ecart = (genou_avant - hanche) - (genou_avant - hanche).dot(u) * u
            normale = (ecart.normalized() if ecart.length > 1e-6
                       else Vector((0.0, 0.0, 1.0)).cross(u).normalized())
            genou = hanche + cuisse * (math.cos(angle) * u + math.sin(angle) * normale)

            pb = arm.pose.bones[h]
            pb.matrix = autour(hanche, rotation_entre(genou_avant - hanche,
                                                      genou - hanche)) @ pb.matrix
            bpy.context.view_layer.update()
            genou_apres = monde @ arm.pose.bones[g].head
            cheville_mi = monde @ arm.pose.bones[a].head
            pb2 = arm.pose.bones[g]
            pb2.matrix = autour(genou_apres,
                                rotation_entre(cheville_mi - genou_apres,
                                               vise - genou_apres)) @ pb2.matrix
            bpy.context.view_layer.update()
            ecart_max = max(ecart_max, ((monde @ arm.pose.bones[a].head) - vise).length)

        poses.append({b: arm.pose.bones[b].matrix.copy() for b in ordre})

    # La rotation dans le temps. Exacte : le cycle est bouclé, décaler l'origine
    # ne perd aucune image et n'en interpole aucune.
    if decalage and not identite:
        poses = [poses[(i - decalage) % IMAGES] for i in range(IMAGES)]

    return poses, ordre, {
        "foulee": foulee, "pas": pas, "chute": chute, "leve": leve,
        "ecart_cible": ecart_max, "extension": extension_max, "jambe": jambe,
        "croisement_avant": avant, "croisement_sans_recalage": apres,
        "decalage": decalage,
    }


def ecrire_action(arm, scene, poses, ordre):
    # L'action d'ORIGINE reste vivante : l'itération sur l'échelle repart d'elle
    # à chaque tour, et la détruire ici a coûté un « StructRNA has been removed »
    # au deuxième tour. Seule une « marche » d'un tour précédent est jetée.
    arm.animation_data.action = None
    precedente = bpy.data.actions.get("marche")
    if precedente is not None:
        bpy.data.actions.remove(precedente)
    action = bpy.data.actions.new("marche")
    arm.animation_data.action = action
    if hasattr(action, "slots"):
        arm.animation_data.action_slot = action.slots.new(id_type="OBJECT",
                                                          name="Armature")
    scene.frame_start, scene.frame_end = 0, IMAGES
    for f in range(0, IMAGES + 1):
        source = poses[f % IMAGES]
        scene.frame_set(f)
        for b in ordre:
            arm.pose.bones[b].matrix = source[b]
            bpy.context.view_layer.update()
        for b in ordre:
            pb = arm.pose.bones[b]
            pb.rotation_mode = "QUATERNION"
            pb.keyframe_insert("location", frame=f)
            pb.keyframe_insert("rotation_quaternion", frame=f)
            pb.keyframe_insert("scale", frame=f)


def main():
    args = lire_args()
    bpy.ops.wm.read_homefile(use_empty=True, use_factory_startup=True)
    bpy.ops.import_scene.gltf(filepath=args.entree)

    arm = bpy.data.objects["Armature"]
    mesh = bpy.data.objects["Cesium_Man"]
    scene = bpy.context.scene
    monde = arm.matrix_world
    origine_action = arm.animation_data.action

    # --- le point fixe de l'échelle ---
    echelle = ECHELLE_DEPART
    for tour in range(8):
        arm.animation_data.action = origine_action
        poses, ordre, diag = construire(arm, scene, monde, echelle,
                                        args.reserve, args.foulee, args.tours,
                                        args.identite)
        ecrire_action(arm, scene, poses, ordre)
        haut = hauteur_posee(mesh, 0, scene)
        suivante = TAILLE_M / haut
        print("PERSONNAGE tour %d : echelle %.5f -> hauteur %.4f -> echelle %.5f"
              % (tour + 1, echelle, haut, suivante))
        if abs(suivante - echelle) < 1e-5:
            echelle = suivante
            break
        echelle = suivante
    else:
        raise SystemExit("l'echelle ne converge pas")

    # --- contrôle, avant d'écrire ---
    releve = {c: [] for c in "GD"}
    for f in range(0, IMAGES + 1):
        scene.frame_set(f)
        bpy.context.view_layer.update()
        for c, (h, _g, a, _t) in JAMBES.items():
            releve[c].append((monde @ arm.pose.bones[h].head,
                              monde @ arm.pose.bones[a].head))

    # Les intervalles d'appui ont suivi la rotation du cycle : les relire à
    # leur place d'avant mesurerait le vol et crierait au glissement.
    glissement = 0.0
    for c, grp in APPUIS.items():
        ys = [releve[c][(k + diag["decalage"]) % IMAGES][1].y for k in grp]
        for k in range(len(ys) - 1):
            glissement = max(glissement, abs((ys[k + 1] - ys[k]) - diag["pas"]))
    boucle = max((releve[c][IMAGES][1] - releve[c][0][1]).length for c in "GD")
    tendu = max((h - a).length / diag["jambe"] for c in "GD" for h, a in releve[c])

    print("PERSONNAGE echelle    %.5f (hauteur a l'instant 0 : %.4f unite)"
          % (echelle, TAILLE_M / echelle))
    print("PERSONNAGE foulee     %.4f unite = %.3f m" % (diag["foulee"], args.foulee))
    print("PERSONNAGE bassin     %.2f cm au plus bas, %.2f cm en moyenne"
          % (max(diag["chute"]) * echelle * 100,
             sum(diag["chute"]) / IMAGES * echelle * 100))
    print("PERSONNAGE pied leve  %.2f cm au plus"
          % (max(max(diag["leve"][c]) for c in "GD") * echelle * 100))
    print("PERSONNAGE cheville   %.6f unite d'ecart a la cible" % diag["ecart_cible"])
    print("PERSONNAGE glissement %.6f unite par image (pas = %.5f)"
          % (glissement, diag["pas"]))
    print("PERSONNAGE boucle     %.8f unite entre l'image 48 et l'image 0" % boucle)
    print("PERSONNAGE jambe      %.4f d'extension au plus (reserve %.2f)"
          % (tendu, args.reserve))
    print("PERSONNAGE croisement image %.2f a l'origine, %.2f construit, "
          "cycle tourne de %d images"
          % (diag["croisement_avant"], diag["croisement_sans_recalage"],
             diag["decalage"]))

    if not args.identite:
        if glissement > 1e-4:
            raise SystemExit("le pied glisse encore de %.6f" % glissement)
        if boucle > 1e-6:
            raise SystemExit("la boucle ne se ferme pas : %.8f" % boucle)
        if tendu > args.reserve + 1e-6:
            raise SystemExit("la jambe depasse la reserve : %.4f" % tendu)

    # --- export, puis greffe dans le fichier d'origine ---
    # L'action d'origine ne doit PAS partir dans le greffon : `ns_skin` lit
    # `animations[0]` et prendrait l'ancien cycle.
    if origine_action is not None:
        bpy.data.actions.remove(origine_action)
    for ob in bpy.data.objects:
        ob.select_set(ob.name in ("Armature", "Cesium_Man", "Z_UP"))
    intermediaire = os.path.join(tempfile.gettempdir(), "nineteen-personnage-greffon.glb")
    bpy.ops.export_scene.gltf(
        filepath=intermediaire,
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_frame_range=True,
        export_animation_mode="ACTIONS",
        export_yup=True,
        export_skins=True,
        export_apply=False,
    )
    os.makedirs(os.path.dirname(os.path.abspath(args.sortie)), exist_ok=True)
    canaux, octets = transplanter(args.entree, intermediaire, args.sortie)
    os.remove(intermediaire)
    print("PERSONNAGE greffe     %d canaux, %d octets" % (canaux, octets))
    print("PERSONNAGE ecrit      %s" % args.sortie)


main()
