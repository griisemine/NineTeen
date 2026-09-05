#!/bin/sh
# vues-capture.sh — une image par point de vue nomme, dans un dossier.
#
# POURQUOI CE SCRIPT EXISTE, ET POURQUOI IL IMPOSE --headless.
#
# Sans lui, on lance le jeu a la main pour se relire, et on le lance comme on
# joue : en fenetre, au premier plan. Sur une machine dont quelqu'un se sert,
# c'est intrusif — la fenetre prend le focus, elle capte la souris, et le point
# de vue de la capture bouge avec elle. Constate ici : des captures de reference
# sont sorties avec l'ecran des reglages par-dessus la salle, parce que le
# systeme avait envoye un Echap dans une fenetre qui n'aurait pas du avoir le
# focus.
#
# `tools/site-media.py` employait `--headless` depuis toujours. Ce script
# n'invente donc rien : il rend la meme discipline disponible pour une simple
# comparaison avant/apres, pour qu'on n'ait plus de raison de s'en passer.
#
# La taille est FIXE et non celle de l'ecran : deux captures de tailles
# differentes ne se comparent pas, et `tools/vues-diff.py` les refuse.
#
#     tools/vues-capture.sh DOSSIER [CHEMIN_DU_BINAIRE]

dest="${1:?usage: vues-capture.sh DOSSIER [BINAIRE]}"
jeu="${2:-./build/macos-universal/bin/nineteen}"
mkdir -p "$dest"

# LE REPERTOIRE DE DONNEES DU JEU, DETOURNE ICI.
#
# Sans cette ligne, capturer huit vues ecrit huit fois dans le repertoire de
# sauvegarde REEL du proprietaire : un `nineteen --headless --frames=1`, qui ne
# joue rien, y reecrit nineteen.log, settings.cfg ET portefeuille.txt. C'est la
# cause du settings.cfg « qui change tout seul », et ce repertoire contient des
# jetons dont le solde attend une verification.
#
# Rediriger HOME ne protege rien : sur macOS SDL passe par NSHomeDirectory, qui
# l'ignore. Trois intervenants s'y sont fait prendre le meme jour. NINETEEN_USER_DIR
# est le moyen explicite, et il vaut sur les trois plateformes.
#
# Le dossier est cache pour que `"$dest"/*.png` ne le ramasse pas, et il vit sous
# $dest pour qu'effacer un releve efface tout le releve.
NINETEEN_USER_DIR="$dest/.donnees"
export NINETEEN_USER_DIR

# --quality=high EST POSE ICI PARCE QUE LA LIGNE DU DESSUS L'A RENDU NECESSAIRE.
#
# Ce script ne passait pas de palier : il heritait `render.quality = high` du
# settings.cfg du proprietaire. Dans un bac a sable neuf il n'y a pas de fichier,
# donc c'est le defaut COMPILE qui s'appliquerait — `medium` (room/main.c) — et les
# captures changeraient sans que personne l'ait demande. On fige donc le palier
# comme `ambiance-releve.sh` le fait deja, pour la meme raison : mesurer autre
# chose mesurerait un autre jeu.
#
# MESURE, parce qu'un rendu n'est pas reproductible au bit pres — deux lancements
# strictement identiques donnent deux PNG differents. Le bon instrument est celui
# du depot : `tools/vues-diff.py` rend 0,00 % de sous-pixels changes, ecart moyen
# 0,03, max 1, sur la vue « allee » — exactement le bruit de fond de deux
# lancements identiques. Les deux facons annoncent « palier : high » et la meme
# ligne de luminance, mot pour mot : moyenne 16.4, mediane 15, 50.8% sous 16.

# ON VERIFIE LE FICHIER, PAS LE CODE DE SORTIE, et c'est mesure : sur cette
# machine le binaire rend 144 APRES avoir ecrit la capture — le journal porte
# « capture ecrite » et « arret apres 40 images », et le PNG est complet et
# lisible. Un `set -e` naif arretait donc la boucle sur la premiere vue venue,
# en laissant croire a un echec de rendu. La preuve d'une capture est la
# capture ; le code de sortie ne dit rien de plus ici.
manquantes=0
for v in allee bar classement travee billard entree plafond sud; do
    rm -f "$dest/$v.png"
    "$jeu" --headless --view="$v" --frames=40 \
           --width=1600 --height=900 --scale=1.0 --quality=high \
           --screenshot="$dest/$v.png" >"$dest/$v.log" 2>&1 || true

    if [ ! -s "$dest/$v.png" ]; then
        printf '%-12s AUCUNE IMAGE — voir %s\n' "$v" "$dest/$v.log"
        manquantes=$((manquantes + 1))
        continue
    fi
    # La ligne de luminance que le moteur imprime a chaque capture : elle dit
    # ce que l'oeil ne chiffre pas, et c'est elle qui trahit un eclairage
    # deregle sur une vue qu'on croyait ne pas avoir touchee.
    printf '%-12s %s\n' "$v" "$(grep -o 'luminance :.*' "$dest/$v.log" | tail -1)"
done

[ "$manquantes" -eq 0 ] || {
    echo "$manquantes vue(s) sans image : la comparaison serait incomplete."
    exit 1
}
