/*
 * geo_mesh.h — tampon de maillage et finition, pour les générateurs de géométrie.
 *
 * Substrat de `roomgen` : on accumule des triangles, on les assemble, puis on
 * finit — soudure, normales, tangentes — avant d'écrire le glTF.
 *
 * Le sommet est celui de `gltf_write.h`, donc celui du moteur (48 octets,
 * position / normale / UV / tangente). Une troisième définition du même sommet
 * finirait par dériver d'un champ.
 *
 * **L'ordre de finition n'est pas négociable** :
 *
 *     append… -> geo_weld -> geo_smooth_normals -> geo_generate_tangents
 *
 * Les tangentes se calculent à partir des UV et des normales : les produire
 * avant que celles-ci soient définitives donne une base fausse, qui ne se voit
 * pas sur une capture fixe mais éclaire les normal maps du mauvais côté.
 */
#ifndef NS_GEO_MESH_H
#define NS_GEO_MESH_H

#include "gltf_write.h"
#include "tools_common.h"

#include "ns_math.h"   /* même convention colonne-majeure que le moteur */

typedef struct geo_tri {
    uint32_t i[3];
    int32_t  material;
} geo_tri;

typedef struct geo_mesh {
    tool_vec verts;    /* gltf_vertex */
    tool_vec tris;     /* geo_tri */
} geo_mesh;

/*
 * Transformation d'instanciation.
 *
 * **Échelle uniforme seulement, et c'est délibéré.** Une échelle négative ou non
 * uniforme inverserait la chiralité des tangentes ; or le rendu désactive
 * l'élimination des faces arrière (`SDL_GPU_CULLMODE_NONE`) et le fragment shader
 * retourne la normale des faces vues de dos, si bien que rien ne signalerait
 * l'erreur — on obtiendrait seulement des normal maps éclairées du mauvais côté,
 * sur les seules instances miroir. Pour obtenir un objet miroir, on passe un
 * paramètre au générateur ; on ne met jamais un signe moins ici.
 */
typedef struct geo_xform {
    ns_v3 origin;
    float yaw, pitch, roll;   /* radians */
    float scale;              /* > 0 ; 0 est traité comme 1 */
} geo_xform;

static const geo_xform GEO_XFORM_IDENTITY = { { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, 0.0f, 1.0f };

void geo_mesh_init(geo_mesh *m);
void geo_mesh_free(geo_mesh *m);

/* Réserve pour éviter les réallocations quand la taille est connue d'avance. */
void geo_mesh_reserve(geo_mesh *m, size_t verts, size_t tris);

size_t geo_mesh_vertex_count(const geo_mesh *m);
size_t geo_mesh_tri_count(const geo_mesh *m);

/* Ajoute un sommet et renvoie son indice. */
uint32_t geo_mesh_push_vertex(geo_mesh *m, ns_v3 position, ns_v3 normal, float u, float v);

/* Ajoute un triangle. Les indices sont relatifs au maillage. */
void geo_mesh_push_tri(geo_mesh *m, uint32_t a, uint32_t b, uint32_t c, int32_t material);

/* Ajoute un quad, en deux triangles (a,b,c) et (a,c,d) — donc a,b,c,d doivent
 * tourner dans le sens direct vu de la face visible. */
void geo_mesh_push_quad(geo_mesh *m, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                        int32_t material);

/* Concatène `src` dans `dst` en lui appliquant `x`. `material_override` >= 0
 * remplace le matériau de tous les triangles ajoutés. */
void geo_mesh_append(geo_mesh *dst, const geo_mesh *src, const geo_xform *x,
                     int32_t material_override);

ns_aabb geo_mesh_bounds(const geo_mesh *m);

/* --------------------------------------------------------------- finition */

/*
 * Fusionne les sommets coïncidents. `epsilon` en mètres ; 1e-4 convient.
 *
 * Ne fusionne que ce qui est identique en position **et** en UV : souder deux
 * sommets qui partagent un coin mais pas leur coordonnée de texture coudrait
 * ensemble deux morceaux d'atlas.
 */
void geo_weld(geo_mesh *m, float epsilon);

/*
 * Recalcule les normales, pondérées par l'aire des triangles — les grandes faces
 * dominent, ce qui est ce que l'œil attend. Deux faces adjacentes ne sont lissées
 * ensemble que si l'angle entre leurs normales est sous `angle_degrees` ; sinon le
 * sommet est dupliqué. Un chanfrein doit rester net, sans quoi il cesse de se lire
 * comme un chanfrein : 1° pour les boîtes et les panneaux, 35° pour les profils
 * extrudés, 60° pour les révolutions.
 */
void geo_smooth_normals(geo_mesh *m, float angle_degrees);

/*
 * Tangentes par la méthode de Lengyel, orthonormalisées, chiralité en w — ce
 * qu'attend glTF. À appeler **par objet** et non sur un pool global : après
 * soudure, un mur et sa plinthe peuvent partager une position, et une accumulation
 * globale moyennerait leurs tangentes de part et d'autre de la frontière de
 * matériau.
 */
void geo_generate_tangents(geo_mesh *m);

/* --------------------------------------------------------------- contrôles */

/* Nombre de triangles d'aire inférieure à `min_area` (m²). Un triangle dégénéré
 * produit une tangente et une normale indéfinies. */
int geo_check_degenerate(const geo_mesh *m, float min_area);

/*
 * Volume signé du maillage, en m³. Pour un volume fermé et correctement enroulé
 * il est positif ; négatif, l'objet est retourné. Proche de zéro pour une surface
 * ouverte, où le contrôle ne veut rien dire — d'où le seuil laissé à l'appelant.
 */
float geo_signed_volume(const geo_mesh *m);

/*
 * Densité de texels, en répétitions de texture par mètre, pour le triangle le
 * moins et le plus dense. Sert de garde-fou : l'ancien plafond bouclait une fois
 * tous les 17,9 m, soit 0,056 répétition par mètre, et se lisait comme un aplat.
 * Renvoie false si le maillage n'a pas d'UV exploitables.
 */
bool geo_uv_density_range(const geo_mesh *m, float *out_min, float *out_max);

/* ------------------------------------------------------------- conversion */

/*
 * Regroupe les triangles par matériau et produit les primitives glTF
 * correspondantes. Les tableaux d'indices sont alloués et doivent être libérés
 * par `geo_primitives_free`. L'ordre des matériaux rencontrés est conservé.
 */
typedef struct geo_primitives {
    gltf_primitive *prims;
    size_t          count;
    uint32_t       *storage;   /* les indices, en un seul bloc */
} geo_primitives;

void geo_build_primitives(const geo_mesh *m, geo_primitives *out);
void geo_primitives_free(geo_primitives *p);

#endif /* NS_GEO_MESH_H */
