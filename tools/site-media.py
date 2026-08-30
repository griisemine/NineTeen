#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
site-media.py — régénère TOUT le média du site, en une commande.

    python3 tools/site-media.py --binaire build/macos-universal/bin/nineteen

POURQUOI CET OUTIL EXISTE
-------------------------
Le site montrait deux images, produites à la main, et deux versions de retard :
il annonçait « version 15 », « quinze bornes » et « 64 sources lumineuses »
alors que la salle en a dix-neuf et vingt-six. Une image faite à la main n'est
refaite par personne ; une image qu'une commande régénère l'est à chaque fois
qu'on y pense. Tout ce que le site affiche vient donc d'ici, et rien n'y entre
autrement — le dépôt a déjà dû retirer treize images tierces de son paquet.

POURQUOI DANS `tools/` ET NON `packaging/`
------------------------------------------
`tools/` contient déjà tout ce qui FABRIQUE des données à partir du dépôt —
`texgen`, `roomgen`, `spriteart`, `marqueeart`. `packaging/` contient ce qui
emballe un build déjà fait (icône, `.desktop`). Le média du site est une donnée
dérivée de la scène et du moteur : sa place est ici.

CE QU'IL REFUSE DE PRODUIRE
---------------------------
Une capture noire est déjà entrée dans ce dépôt sans que personne ne la voie.
Chaque PNG produit est donc relu avant d'être encodé, et le script s'arrête net
si la définition n'est pas exactement celle demandée, ou si l'image est noire ou
unie. Les seuils sont des CHIFFRES MESURÉS sur les vues de cette salle, pas des
valeurs de principe : voir `verifier_image`.

DÉPENDANCES
-----------
`ffmpeg` et `ffprobe` (mesurés ici en 9.0.1), et le binaire du jeu. Rien d'autre :
pas de module Python tiers, la lecture des pixels passe par ffmpeg.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = os.path.join(RACINE, "assets", "scene", "salle.room.json")
CMAKE = os.path.join(RACINE, "CMakeLists.txt")
SORTIE_DEFAUT = os.path.join(RACINE, "server", "internal", "web", "assets")


# ============================================================================
# Sortie console
# ============================================================================

def info(msg):
    print(f"  {msg}", flush=True)


def etape(msg):
    print(f"\n\033[1m{msg}\033[0m", flush=True)


class Echec(Exception):
    """Tout échec est fatal : un média à moitié régénéré est pire qu'un média
    périmé, parce qu'il a l'air d'être à jour."""


# ============================================================================
# Lecture des sources — AUCUN chiffre n'est écrit à la main dans ce fichier
# ============================================================================

def lire_version():
    """La version du projet, lue dans `CMakeLists.txt`.

    C'est LA source : le serveur Go en gardait une copie à la main, restée à
    15.0.0 quand le projet était en 17.0.0, et le site affichait donc « Version
    15.0.0 » sur sa page de téléchargement.
    """
    with open(CMAKE, encoding="utf-8") as f:
        for ligne in f:
            depouillee = ligne.strip()
            if depouillee.startswith("VERSION"):
                morceaux = depouillee.split()
                if len(morceaux) >= 2 and morceaux[1][0].isdigit():
                    return morceaux[1]
    raise Echec(f"version introuvable dans {CMAKE}")


def lire_scene():
    with open(SCENE, encoding="utf-8") as f:
        return json.load(f)


# Le libellé français de chaque jeu. La scène ne porte que l'identifiant du
# moteur ; l'accent de « Démineur » et la casse d'affichage sont une question de
# présentation, et c'est la seule table de ce fichier qui ne soit pas mesurée.
LIBELLES = {
    "envol": "Envol",
    "aplomb": "Aplomb",
    "asteroid": "Asteroid",
    "demineur": "Démineur",
    "snake": "Snake",
    "shooter": "Shooter",
    "dedale": "Dédale",
    "piano": "Piano",
    "leaderboard": "Classement",
}

DIFFICULTES = {"easy": "facile", "hard": "difficile", "normal": "normal"}


# ============================================================================
# Vérification d'une image — le contrôle qui manquait
# ============================================================================

def dimensions(chemin):
    sortie = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "csv=p=0", chemin],
        capture_output=True, text=True)
    if sortie.returncode != 0:
        raise Echec(f"ffprobe a refusé {chemin} : {sortie.stderr.strip()}")
    try:
        l, h = sortie.stdout.strip().split(",")[:2]
        return int(l), int(h)
    except ValueError:
        raise Echec(f"définition illisible pour {chemin} : {sortie.stdout!r}")


def luminance(chemin):
    """Moyenne, médiane, part de pixels sombres et amplitude, en une passe.

    Les pixels sont relus par ffmpeg en niveaux de gris : c'est la même mesure
    que celle que le moteur journalise à chaque capture (`ns_rhi.c`), mais faite
    ICI, sur le fichier écrit, donc indépendante du format du journal.
    """
    brut = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", chemin, "-f", "rawvideo", "-pix_fmt", "gray", "-"],
        capture_output=True).stdout
    if not brut:
        raise Echec(f"image illisible : {chemin}")

    hist = [0] * 256
    for octet in brut:
        hist[octet] += 1
    total = len(brut)

    somme = sum(v * n for v, n in enumerate(hist))
    acc, mediane = 0, 0
    for v in range(256):
        acc += hist[v]
        if acc * 2 >= total:
            mediane = v
            break
    bas = next(v for v in range(256) if hist[v])
    haut = next(v for v in range(255, -1, -1) if hist[v])
    return {
        "moyenne": somme / total,
        "mediane": mediane,
        "sombre": 100.0 * sum(hist[:16]) / total,
        "amplitude": haut - bas,
    }


def verifier_image(chemin, largeur, hauteur):
    """Refuse une capture ratée, bruyamment.

    LES SEUILS SONT MESURÉS. Sur les vues de cette salle, la plus sombre des
    captures de recette relevées en écrivant cet outil donne une moyenne de 45,9
    et une médiane de 31 (`orbite`, cap 90) ; la vue `allee` donne 48,2 et 25 ;
    une borne vue de près, 64,1 et 38. Les seuils retenus — moyenne 10, médiane
    3, 92 % de pixels sous 16 — sont donc à plus de quatre fois sous la plus
    sombre image LÉGITIME connue : ils n'attrapent pas une salle mal éclairée,
    ils attrapent une image qui n'a pas été rendue du tout.

    L'amplitude attrape l'autre panne, qu'un seuil de noirceur laisse passer :
    une image UNIE, de n'importe quelle couleur — un ciel de fond seul, une
    cible jamais dessinée. Un rendu réel de cette salle n'a jamais moins de 200
    de dynamique.
    """
    l, h = dimensions(chemin)
    if (l, h) != (largeur, hauteur):
        raise Echec(f"{os.path.basename(chemin)} : {l}x{h} au lieu de {largeur}x{hauteur}")

    m = luminance(chemin)
    if m["moyenne"] < 10.0 or m["mediane"] < 3 or m["sombre"] > 92.0:
        raise Echec(
            f"{os.path.basename(chemin)} : image NOIRE — moyenne {m['moyenne']:.1f}, "
            f"médiane {m['mediane']}, {m['sombre']:.1f}% sous 16")
    if m["amplitude"] < 32:
        raise Echec(
            f"{os.path.basename(chemin)} : image UNIE — amplitude {m['amplitude']}")
    return m


# ============================================================================
# Le moteur
# ============================================================================

class Moteur:
    def __init__(self, binaire, rapide):
        self.binaire = os.path.abspath(binaire)
        if not os.path.isfile(self.binaire):
            raise Echec(f"binaire introuvable : {self.binaire}\n"
                        "  compiler d'abord : cmake --build build/macos-universal -j8")
        self.rapide = rapide
        self.secondes = 0.0

    def _lancer(self, args):
        debut = time.time()
        # cwd=RACINE : le moteur cherche `nineteen.env` et ses assets à partir du
        # dossier courant. Le lancer d'ailleurs le fait tomber sur une autre
        # configuration, donc sur d'autres images.
        r = subprocess.run([self.binaire] + args, cwd=RACINE,
                           capture_output=True, text=True)
        self.secondes += time.time() - debut
        if r.returncode != 0:
            raise Echec("le moteur a échoué :\n  " + " ".join(args) +
                        "\n" + r.stderr[-2000:])
        return r

    def capture(self, sortie, largeur, hauteur, args, qualite="high"):
        """Une image fixe, vérifiée."""
        if self.rapide:
            qualite = "medium"
        self._lancer([
            "--headless", f"--screenshot={sortie}",
            f"--width={largeur}", f"--height={hauteur}",
            "--frames=8", f"--quality={qualite}", "--scale=1.0", "--no-hud",
        ] + args)
        if not os.path.isfile(sortie):
            raise Echec(f"le moteur n'a écrit aucune image : {sortie}")
        return verifier_image(sortie, largeur, hauteur)

    def sequence(self, prefixe, largeur, hauteur, images, fps, args, qualite="high"):
        """Une suite d'images numérotées, dont un échantillon est vérifié.

        Vérifier les 360 images d'un plan coûterait plus cher que de les rendre
        (une relecture ffmpeg par image). On en contrôle donc la PREMIÈRE, la
        DERNIÈRE et une sur dix : une capture noire l'est parce que le rendu n'a
        pas eu lieu, ce qui ne frappe pas une image sur cinquante.
        """
        if self.rapide:
            qualite = "low"
            images = max(24, images // 4)
        self._lancer([
            "--headless", f"--sequence={prefixe}", f"--sequence-fps={fps}",
            f"--frames={images}", f"--width={largeur}", f"--height={hauteur}",
            f"--quality={qualite}", "--scale=1.0", "--no-hud",
        ] + args)

        fichiers = [f"{prefixe}{i:04d}.png" for i in range(images)]
        manquants = [f for f in fichiers if not os.path.isfile(f)]
        if manquants:
            raise Echec(f"séquence incomplète : {len(manquants)} image(s) manquante(s), "
                        f"la première est {os.path.basename(manquants[0])}")

        a_verifier = {0, images - 1} | set(range(0, images, 10))
        for i in sorted(a_verifier):
            verifier_image(fichiers[i], largeur, hauteur)
        return fichiers


# ============================================================================
# Encodage
# ============================================================================

def ffmpeg(args):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"] + args,
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise Echec("ffmpeg a échoué :\n  " + " ".join(args) + "\n" + r.stderr[-2000:])


def palindrome(fichiers, dossier):
    """Aller-retour : le plan revient à son image de départ, donc il BOUCLE.

    Une orbite à sens unique saute visiblement au raccord, et sur une page où la
    vidéo tourne en `loop` ce saut se lit comme un défaut d'encodage — on le
    cherche dans le codec alors qu'il est dans le mouvement. Le retour coûte zéro
    rendu : ce sont les mêmes images, relues à l'envers.
    """
    lien = os.path.join(dossier, "boucle")
    os.makedirs(lien, exist_ok=True)
    suite = fichiers + fichiers[-2:0:-1]
    for i, source in enumerate(suite):
        os.link(source, os.path.join(lien, f"b{i:05d}.png"))
    return os.path.join(lien, "b%05d.png"), len(suite)


def encoder_video(motif, base, fps, largeur, hauteur, crf_h264=31, crf_vp9=46,
                  debit_vp9="0"):
    """h.264 pour la compatibilité, VP9 pour le poids. Les deux en `yuv420p`.

    `-movflags +faststart` place l'index en tête du fichier : sans lui le
    navigateur télécharge tout le mp4 avant d'afficher la première image, ce qui
    annule l'intérêt de `preload="metadata"`.

    LES DEUX ÉCHELLES DE CRF NE SONT PAS LA MÊME. À qualité comparable, VP9
    demande un nombre plus élevé que x264 : réglé au même 27 que h.264, le
    premier essai sortait un webm de 2,87 Mio contre 1,48 pour le mp4 — le
    format censé peser MOINS pesait le double, et le navigateur qui le préfère
    payait le plus cher.

    LE DÉBRUITAGE N'EST PAS UN EFFET, C'EST CE QUI REND LA PAGE SERVABLE. Cette
    salle est sombre, et un rendu sombre est plein de grain : tramage du dégradé,
    poussière en suspension, bruit du lancer de rayons. Ce grain change à chaque
    image, donc aucun codage inter-images ne le prédit — il se paie plein tarif,
    à chaque image. Mesuré : le plan d'ambiance de 1280x720 sortait à 3,39 Mio en
    h.264 et 4,65 en VP9 sans débruitage. `hqdn3d` moyenne le grain dans le temps
    sans toucher aux contours des enseignes, qui sont ce qu'on regarde.
    """
    mp4 = f"{base}.mp4"
    webm = f"{base}.webm"
    filtres = f"hqdn3d=3:2:6:6,scale={largeur}:{hauteur}:flags=lanczos"
    ffmpeg(["-framerate", str(fps), "-i", motif,
            "-c:v", "libx264", "-preset", "slow", "-crf", str(crf_h264),
            "-pix_fmt", "yuv420p", "-an", "-vf", filtres,
            "-movflags", "+faststart", mp4])
    # `-b:v 0` = qualité constante ; une valeur non nulle passe libvpx en
    # « qualité contrainte », où le CRF reste la cible mais le débit devient un
    # PLAFOND. C'est ce qu'il faut pour le plan d'accueil, qui est la seule
    # vidéo que tout visiteur télécharge : on lui impose un budget plutôt que
    # d'espérer que le codeur soit raisonnable. Sans ce plafond, mesuré, le webm
    # de ce plan sortait à 2,59 Mio contre 1,71 pour le mp4.
    ffmpeg(["-framerate", str(fps), "-i", motif,
            "-c:v", "libvpx-vp9", "-crf", str(crf_vp9), "-b:v", debit_vp9,
            "-deadline", "good", "-cpu-used", "2", "-row-mt", "1",
            "-pix_fmt", "yuv420p", "-an", "-vf", filtres, webm])
    return mp4, webm


def encoder_affiche(source, base, largeur, hauteur):
    """L'affiche du lecteur : AVIF, et JPEG en repli.

    Pas de WebP : le ffmpeg de cette machine (9.0.1) n'embarque aucun encodeur
    WebP — `ffmpeg -encoders | grep webp` ne renvoie rien. AVIF couvre les mêmes
    navigateurs récents, JPEG couvre tout le reste, et un troisième format
    n'ajouterait qu'un fichier à tenir à jour.
    """
    avif = f"{base}.avif"
    jpg = f"{base}.jpg"
    echelle = f"scale={largeur}:{hauteur}:flags=lanczos"
    ffmpeg(["-i", source, "-vf", echelle, "-c:v", "libsvtav1", "-crf", "40",
            "-frames:v", "1", avif])
    ffmpeg(["-i", source, "-vf", echelle, "-c:v", "mjpeg", "-q:v", "6",
            "-frames:v", "1", jpg])
    return avif, jpg


def encoder_jpeg(source, sortie, largeur, hauteur, qualite=6):
    ffmpeg(["-i", source, "-vf", f"scale={largeur}:{hauteur}:flags=lanczos",
            "-c:v", "mjpeg", "-q:v", str(qualite), "-frames:v", "1", sortie])
    return sortie


# ============================================================================
# Le plan de tournage
# ============================================================================

# Les vues de la salle retenues pour la galerie. Chaque nom DOIT exister dans
# `captures` de `salle.room.json` — le script le vérifie, parce qu'un nom de vue
# inconnu ne fait pas échouer le moteur : il rend simplement la vue par défaut,
# et huit vues identiques passeraient inaperçues.
VUES = [
    ("allee", "L'allée centrale, entre les deux rangées de bornes."),
    ("bar", "Le comptoir, l'enseigne au néon et le tableau des scores."),
    ("classement", "La borne de classement et son tableau des meilleurs scores."),
    ("travee", "La travée ouest : trois bornes de trois quarts, la corniche."),
    ("billard", "Le billard, au fond de la salle."),
    ("entree", "Le sas d'entrée, vu depuis le couloir."),
    ("plafond", "Le plafond lumineux et ses poutres."),
    ("sud", "Le vide sud, la poche la plus profonde du hall."),
]


def plan_bornes(scene):
    """Les DIX-NEUF bornes, dans l'ordre de leur emplacement.

    La liste vient de la scène et de nulle part ailleurs : ajouter une borne à
    `salle.room.json` doit suffire à la faire apparaître sur le site.
    """
    bornes = sorted(scene["cabinets"], key=lambda c: c["slot"])
    plan = []
    for c in bornes:
        jeu = c.get("game", "")
        plan.append({
            "nom": c["name"],
            "slot": c["slot"],
            "jeu": jeu,
            "libelle": LIBELLES.get(jeu, jeu.capitalize()),
            "difficulte": DIFFICULTES.get(c.get("difficulty", "normal"), "normal"),
            "fichier": f"borne-{c['slot']:02d}.jpg",
        })
    return plan


def plan_jeux(scene):
    """Un jeu, une boucle — et la borne choisie pour le filmer.

    On prend la borne au plus petit emplacement pour chaque jeu, donc toujours la
    même : deux exécutions du script filment le même meuble sous le même angle.
    """
    par_jeu = {}
    for c in sorted(scene["cabinets"], key=lambda c: c["slot"]):
        jeu = c.get("game", "")
        if jeu in ("", "leaderboard"):
            continue
        par_jeu.setdefault(jeu, c)
    return [{
        "jeu": jeu,
        "libelle": LIBELLES.get(jeu, jeu.capitalize()),
        "borne": c["name"],
        "base": f"jeu-{jeu}",
    } for jeu, c in sorted(par_jeu.items(), key=lambda kv: kv[1]["slot"])]


# ============================================================================
# Programme
# ============================================================================

def poids(chemin):
    return os.path.getsize(chemin)


def humain(octets):
    return f"{octets / 1024:.0f} Kio" if octets < 1024 * 1024 else f"{octets / 1048576:.2f} Mio"


def main():
    ap = argparse.ArgumentParser(description="Régénère le média du site Nineteen.")
    ap.add_argument("--binaire", default="build/macos-universal/bin/nineteen",
                    help="le binaire du jeu")
    ap.add_argument("--sortie", default=SORTIE_DEFAUT,
                    help="le dossier d'assets du serveur")
    ap.add_argument("--rapide", action="store_true",
                    help="plans courts et qualité basse : pour vérifier la chaîne, "
                         "PAS pour produire le média du site")
    args = ap.parse_args()

    for outil in ("ffmpeg", "ffprobe"):
        if shutil.which(outil) is None:
            raise Echec(f"{outil} est introuvable dans le PATH")

    moteur = Moteur(args.binaire, args.rapide)
    scene = lire_scene()
    version = lire_version()

    noms_vues = {c["name"] for c in scene["captures"]}
    inconnues = [n for n, _ in VUES if n not in noms_vues]
    if inconnues:
        raise Echec("vue(s) inconnue(s) de la scène : " + ", ".join(inconnues))

    dossier_media = os.path.join(args.sortie, "media")
    dossier_img = os.path.join(args.sortie, "img")
    os.makedirs(dossier_media, exist_ok=True)
    os.makedirs(dossier_img, exist_ok=True)

    manifeste = {
        "_": "PRODUIT PAR tools/site-media.py — ne pas modifier à la main.",
        "version": version,
        "bornes": [],
        "jeux": [],
        "vues": [],
        "salle": {},
    }
    produits = []

    with tempfile.TemporaryDirectory(prefix="nineteen-media-") as tmp:

        # ---------------------------------------------------------------- salle
        etape("Le plan d'ambiance — orbite lente")
        prefixe = os.path.join(tmp, "salle-")
        # Cap 90 : c'est le seul des quatre quadrants où l'axe de la salle est
        # symétrique dans le cadre — les deux rangées de bornes en enfilade,
        # l'enseigne NINETEEN au fond. Vérifié en capturant 0, 90, 200 et 270.
        # 150 images à l'aller, 298 après l'aller-retour, soit 9,9 s de boucle.
        # Mesuré : l'orbite tourne de 5,3 degrés par seconde de simulation, donc
        # ce plan balaie 27 degrés — assez pour que le mouvement se lise, assez
        # peu pour rester dans le quadrant symétrique repéré au cap 90.
        #
        # C'est LA vidéo que tout visiteur télécharge, puisqu'elle démarre seule :
        # elle est donc la seule dont le poids compte vraiment, et le budget tenu
        # ici est de 1,5 Mio par format.
        images = moteur.sequence(prefixe, 1280, 720, 150, 30,
                                 ["--view=orbite", "--camera=orbit", "--angle=90"])
        motif, total = palindrome(images, tmp)
        info(f"{len(images)} images rendues, {total} après aller-retour")
        mp4, webm = encoder_video(motif, os.path.join(dossier_media, "salle"), 30, 1280, 720,
                                  crf_h264=33, crf_vp9=48, debit_vp9="1000k")
        avif, jpg = encoder_affiche(images[0], os.path.join(dossier_media, "salle-affiche"),
                                    1280, 720)
        for f in (mp4, webm, avif, jpg):
            produits.append(f)
            info(f"{os.path.basename(f):28s} {humain(poids(f)):>10s}")
        manifeste["salle"] = {
            "mp4": "/media/salle.mp4", "webm": "/media/salle.webm",
            "affiche_avif": "/media/salle-affiche.avif",
            "affiche_jpg": "/media/salle-affiche.jpg",
            "duree": round(total / 30.0, 1),
            "alt": "Orbite lente dans la salle d'arcade : les deux rangées de bornes "
                   "allumées, le plafond lumineux, la moquette à motifs néon.",
        }
        shutil.rmtree(os.path.join(tmp, "boucle"))

        # ----------------------------------------------------------------- jeux
        etape("Les huit jeux — une boucle par jeu, jouée dans sa borne")
        for j in plan_jeux(scene):
            prefixe = os.path.join(tmp, f"{j['jeu']}-")
            # `--play-at` reste EN 3D : on voit l'enseigne, la dalle qui joue et
            # les mains sur les commandes. Le jeu en plein écran montrerait mieux
            # ses pixels, mais plus rien de la salle — et c'est la salle qu'on
            # vend ici.
            # Huit boucles ne se téléchargent que si on les regarde — elles sont
            # en `preload="none"`. Elles peuvent donc être un peu plus généreuses
            # que le plan d'accueil, mais restent en 640x360 : à cette taille la
            # dalle du jeu tient un tiers du cadre, ce qui suffit à voir jouer.
            images = moteur.sequence(prefixe, 854, 480, 120, 30,
                                     [f"--play-at={j['borne']}"])
            motif, total = palindrome(images, tmp)
            mp4, webm = encoder_video(motif, os.path.join(dossier_media, j["base"]),
                                      30, 640, 360)
            avif, jpg = encoder_affiche(images[len(images) // 2],
                                        os.path.join(dossier_media, j["base"] + "-affiche"),
                                        640, 360)
            for f in (mp4, webm, avif, jpg):
                produits.append(f)
            info(f"{j['libelle']:12s} {humain(poids(mp4)):>10s} mp4 "
                 f"{humain(poids(webm)):>10s} webm")
            manifeste["jeux"].append({
                "id": j["jeu"], "nom": j["libelle"], "borne": j["borne"],
                "mp4": f"/media/{j['base']}.mp4", "webm": f"/media/{j['base']}.webm",
                "affiche_avif": f"/media/{j['base']}-affiche.avif",
                "affiche_jpg": f"/media/{j['base']}-affiche.jpg",
                "duree": round(total / 30.0, 1),
                "alt": f"La borne {j['libelle']} en cours de partie, vue depuis les "
                       f"commandes : l'enseigne au néon et le jeu dans la dalle.",
            })
            shutil.rmtree(os.path.join(tmp, "boucle"))

        # --------------------------------------------------------------- bornes
        etape("Les dix-neuf bornes — une photo par enseigne")
        for b in plan_bornes(scene):
            brut = os.path.join(tmp, f"{b['nom']}.png")
            moteur.capture(brut, 960, 540, [f"--play-at={b['nom']}"])
            sortie = os.path.join(dossier_img, b["fichier"])
            encoder_jpeg(brut, sortie, 640, 360)
            produits.append(sortie)
            manifeste["bornes"].append({
                "slot": b["slot"], "nom": b["nom"], "jeu": b["jeu"],
                "libelle": b["libelle"], "difficulte": b["difficulte"],
                "image": f"/img/{b['fichier']}",
                "alt": f"Borne {b['slot']} : enseigne {b['libelle']}, "
                       f"le jeu tournant dans la dalle.",
            })
        info(f"{len(manifeste['bornes'])} bornes, "
             f"{humain(sum(poids(os.path.join(dossier_img, b['fichier'])) for b in plan_bornes(scene)))} au total")

        # ---------------------------------------------------------------- vues
        etape("La salle en plusieurs vues")
        for nom, legende in VUES:
            brut = os.path.join(tmp, f"vue-{nom}.png")
            mesure = moteur.capture(brut, 1600, 900, [f"--view={nom}"])
            sortie = os.path.join(dossier_img, f"vue-{nom}.jpg")
            encoder_jpeg(brut, sortie, 1280, 720)
            produits.append(sortie)
            info(f"{nom:12s} {humain(poids(sortie)):>10s}  "
                 f"médiane {mesure['mediane']:3d}, moyenne {mesure['moyenne']:5.1f}")
            manifeste["vues"].append({
                "nom": nom, "image": f"/img/vue-{nom}.jpg",
                "legende": legende,
                "alt": legende + " Rendu par le moteur du jeu.",
            })

    # -------------------------------------------------------------- manifeste
    #
    # Le manifeste est ce qui empêche la page de mentir : le script y écrit ce
    # qu'il a RÉELLEMENT produit, le site le lit pour bâtir ses galeries, et le
    # test Go le confronte à la scène. Le site ne peut donc plus annoncer quinze
    # bornes quand la scène en déclare dix-neuf.
    manifeste["compte"] = {
        "bornes": len(manifeste["bornes"]),
        "jeux": len(manifeste["jeux"]),
        "luminaires": len(scene["lights"]),
        "vues_nommees": len(scene["captures"]),
        "props": len(scene["props"]),
    }
    chemin_manifeste = os.path.join(dossier_media, "manifeste.json")
    with open(chemin_manifeste, "w", encoding="utf-8") as f:
        json.dump(manifeste, f, ensure_ascii=False, indent=2, sort_keys=False)
        f.write("\n")
    produits.append(chemin_manifeste)

    # ------------------------------------------------------------------ bilan
    etape("Bilan")
    total = sum(poids(f) for f in produits)
    videos = sum(poids(f) for f in produits if f.endswith((".mp4", ".webm")))
    images = sum(poids(f) for f in produits if f.endswith((".jpg", ".avif")))
    info(f"{len(produits)} fichiers, {humain(total)} au total")
    info(f"  vidéos  {humain(videos)}")
    info(f"  images  {humain(images)}")
    info(f"rendu : {moteur.secondes:.0f} s de moteur")
    info(f"version annoncée : {version}")
    print()


if __name__ == "__main__":
    try:
        main()
    except Echec as e:
        print(f"\n\033[31mÉCHEC\033[0m — {e}\n", file=sys.stderr)
        sys.exit(1)
