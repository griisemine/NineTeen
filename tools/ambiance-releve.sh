#!/bin/sh
# ambiance-releve.sh — capture, mesure et assemble, en une commande.
#
# POURQUOI CE SCRIPT EXISTE
# -------------------------
# La comparaison a une image de reference se fait en trois temps : capturer les
# huit vues, les mesurer, les regarder ensemble. Fait a la main, l'enchainement
# se rate d'une fois sur deux — on oublie une vue, on compare une capture en
# medium a une capture en high, on mesure l'ancienne planche. Chacune de ces
# erreurs a ete commise pendant la mise au point du neon, et chacune coute une
# conclusion fausse avant qu'on s'en apercoive.
#
# Les REGLAGES DE CAPTURE sont figes ici, et c'est le point du script :
#
#   --quality=high --scale=1.0    ce que le proprietaire a dans sa configuration
#                                 (render.quality = high, render.scale = 1) :
#                                 mesurer autre chose mesurerait un autre jeu
#   --frames=40                   l'exposition automatique et les particules ont
#                                 besoin de converger ; a 4 images la mesure
#                                 bouge encore de plusieurs points
#   --offline                     sans lui, l'ecran de connexion au comptoir se
#                                 pose PAR-DESSUS la salle et l'on mesure un menu
#   --headless                    le proprietaire se sert de sa machine ; une
#                                 fenetre au premier plan lui vole la souris
#
#     tools/ambiance-releve.sh DOSSIER [BINAIRE]

dest="${1:?usage: ambiance-releve.sh DOSSIER [BINAIRE]}"
jeu="${2:-./build/bin/nineteen}"
racine="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$dest"

# Les memes huit vues que `vues-capture.sh`, et deliberement les memes : deux
# listes qui divergent donneraient deux verdicts sur la meme salle.
vues="allee bar classement travee billard entree plafond sud"

manquantes=0
for v in $vues; do
    rm -f "$dest/$v.png"
    "$jeu" --headless --offline --view="$v" --frames=40 \
           --width=1024 --height=576 --scale=1.0 --quality=high \
           --screenshot="$dest/$v.png" >"$dest/$v.log" 2>&1 || true
    if [ ! -s "$dest/$v.png" ]; then
        printf '%-12s AUCUNE IMAGE — voir %s\n' "$v" "$dest/$v.log"
        manquantes=$((manquantes + 1))
    fi
done

[ "$manquantes" -eq 0 ] || {
    echo "$manquantes vue(s) sans image : le releve serait incomplet."
    exit 1
}

python3 "$racine/tools/ambiance.py" --cible "$dest"/*.png
etat=$?
python3 "$racine/tools/planche.py" "$dest/planche.png" \
        "$dest/allee.png" "$dest/travee.png" "$dest/billard.png" "$dest/sud.png" \
        "$dest/bar.png" "$dest/classement.png" "$dest/plafond.png" "$dest/entree.png"

echo
echo "planche : $dest/planche.png"
# LE CODE DE SORTIE SUIT LA MESURE, PAS L'ASSEMBLAGE. La planche reussit
# toujours ; c'est `ambiance.py --cible` qui a un avis, et c'est le sien qu'on
# veut voir passer dans un `&&`.
exit "$etat"
