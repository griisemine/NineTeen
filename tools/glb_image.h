/*
 * glb_image.h — remplacer l'IMAGE embarquée d'un GLB sans toucher au reste.
 *
 * Pourquoi c'est un fichier à part et pas trois fonctions dans `skinart.c` :
 * cgltf embarque SA PROPRE copie de jsmn, et `tool_json.h` embarque la nôtre.
 * Les deux dans la même unité de compilation redéfinissent `jsmn_parse`. Le
 * découpage n'est donc pas un goût d'architecture, c'est la seule façon
 * d'employer les deux — l'outil lit la géométrie avec cgltf et recoud le
 * fichier avec jsmn, chacun dans son unité.
 *
 * Ce que la fonction fait, et ce qu'elle refuse de faire
 * -----------------------------------------------------
 * Elle NE réémet PAS le glTF. Réécrire ce fichier voudrait dire réécrire aussi
 * ses pistes d'animation, ses accesseurs et ses nœuds à matrice, c'est-à-dire
 * risquer de perdre en route exactement ce que `ns_skin` a mis du temps à lire
 * correctement.
 *
 * Elle tire parti de ce que l'image est le DERNIER morceau du bloc binaire :
 * remplacer son contenu ne déplace alors aucun accesseur, et il suffit de
 * corriger deux entiers dans le JSON — la longueur de sa vue de tampon, et
 * celle du tampon. Ces deux entiers sont repérés par leur JETON JSON, pas par
 * une recherche de texte.
 *
 * Et elle VÉRIFIE que l'image est bien la dernière au lieu de le supposer. Si
 * un jour ce n'est plus vrai, elle s'arrête : produire un modèle dont les
 * sommets liraient l'image serait un défaut qu'on ne trouverait qu'à l'écran.
 */
#ifndef NS_GLB_IMAGE_H
#define NS_GLB_IMAGE_H

#include <stddef.h>

/*
 * Recopie `entree` vers `sortie` en y substituant `image` (`taille` octets) à
 * l'image embarquée. `mime` vaut « image/jpeg » ou « image/png » et DOIT
 * correspondre au contenu — le type déclaré dans le fichier est ce que le
 * moteur croira.
 *
 * `entree` et `sortie` peuvent désigner le même fichier : la source est lue
 * entièrement avant que quoi que ce soit ne soit écrit.
 *
 * S'arrête net en cas de problème, comme tous les outils de ce répertoire.
 */
void glb_image_replace(const char *entree, const char *sortie,
                       const void *image, size_t taille, const char *mime);

#endif /* NS_GLB_IMAGE_H */
