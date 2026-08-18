/*
 * geo_import.h — instancier un glTF dans le maillage de la salle.
 *
 * Pourquoi cet outil existe
 * -------------------------
 * `roomgen` ne connaissait que la boîte et le panneau. C'est très bien pour une
 * borne d'arcade — un caisson EST une boîte, et sa dalle EST un panneau — et
 * c'est mauvais pour tout le reste : un tabouret fait de trois boîtes est trois
 * boîtes, et se lit comme trois boîtes. C'est ce que tu voyais.
 *
 * On ne remplace donc pas les générateurs, on leur ajoute un voisin : ce qui est
 * paramétrique le reste — les bornes portent des métadonnées (écran, panneau,
 * fente) qu'un modèle importé ne déclarerait pas, et c'est toute la valeur
 * d'A3/A4 — et ce qui est du mobilier vient de vrais modèles.
 *
 * Ce que l'import fait, et ce qu'il ne fait pas
 * --------------------------------------------
 * Il APLATIT. Le glTF est lu, ses nœuds sont composés jusqu'à la racine, et ses
 * triangles entrent dans le maillage de la salle en espace monde — exactement
 * comme le fait `geo_box`. Il n'y a donc ni instanciation GPU, ni hiérarchie à
 * l'exécution, ni format supplémentaire à charger : le moteur continue de
 * recevoir une salle et une seule.
 *
 * Il ne fait ni animation, ni squelette, ni matériaux importés : le glTF apporte
 * sa géométrie et ses UV, la salle décide de la matière. C'est ce qui garde une
 * seule table de matériaux, donc un seul endroit où régler l'aspect de la salle,
 * et ça évite de charger trois cartes par modèle pour un tabouret vu à deux
 * mètres.
 *
 * Contraintes, et pourquoi ce sont des erreurs et pas des avertissements
 * ---------------------------------------------------------------------
 * **Échelle uniforme seulement.** Une échelle non uniforme change la main des
 * tangentes sans que rien ne le signale, et l'éclairage devient faux d'une façon
 * qu'on met une heure à attribuer au modèle plutôt qu'au code. Même règle que
 * `geo_mesh_append`, pour la même raison.
 *
 * **Déterminant positif exigé.** Une instance miroir retourne l'orientation des
 * triangles ; le moteur n'élimine pas les faces arrière, donc ça ne se verrait
 * pas tout de suite — seulement plus tard, sur l'éclairage et le BVH.
 */
#ifndef NS_GEO_IMPORT_H
#define NS_GEO_IMPORT_H

#include "geo_mesh.h"

#include <stdint.h>

/*
 * Charge `path` et ajoute ses triangles à `out`, transformés par `x`.
 *
 * `material` s'applique à toutes les primitives. `material_by_index`, s'il est
 * fourni, le remplace primitive par primitive selon l'indice de matériau du
 * glTF — c'est ce qui permet de donner sa couleur au haut-parleur d'un poste de
 * radio sans importer sa table de matériaux.
 *
 * `owner` ne sert qu'aux messages d'erreur : un glTF refusé doit nommer l'objet
 * de la salle qui l'a demandé, pas seulement le fichier.
 *
 * Renvoie le nombre de triangles ajoutés. Toute anomalie est FATALE : un outil
 * de build doit casser le build, pas produire un asset incomplet.
 */
size_t geo_import_gltf(geo_mesh *out, const char *path, const geo_xform *x,
                       int32_t material, const int32_t *material_by_index,
                       size_t material_by_index_count, const char *owner);

/*
 * L'emprise du modèle une fois transformé, sans rien ajouter au maillage.
 *
 * Sert au contrôle de chevauchement et au placement : un tabouret dont on ne
 * connaît pas la hauteur se pose au petit bonheur, et « au petit bonheur » est
 * précisément ce qu'on reproche au décor. Le fichier est relu — c'est deux fois
 * le même travail, et c'est négligeable devant la lisibilité qu'on y gagne.
 */
void geo_import_bounds(const char *path, const geo_xform *x,
                       float out_min[3], float out_max[3], const char *owner);

#endif /* NS_GEO_IMPORT_H */
