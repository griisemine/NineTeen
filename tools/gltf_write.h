/*
 * gltf_write.h — écriture glTF 2.0, partagée par les outils hors-ligne.
 *
 * Extrait d'obj2gltf, qui l'avait en ligne dans son `main`, pour que `roomgen`
 * puisse l'utiliser sans réécrire un second sérialiseur glTF. Deux écrivains
 * glTF dans le même dépôt divergeraient en un mois, et l'un des deux
 * produirait des fichiers que `bvhbake` refuse — pour des raisons différentes
 * à chaque fois.
 *
 * Conventions de sortie, imposées par ce que le moteur consomme :
 *
 *   - **Un seul jeu d'accesseurs d'attributs**, partagé par toutes les
 *     primitives (POSITION 0, NORMAL 1, TEXCOORD_0 2, TANGENT 3). Ce n'est pas
 *     un raccourci : `ns_scene_load` compte les sommets une fois par accesseur
 *     *distinct*, et un accesseur par primitive transformait 96 067 sommets en
 *     17,5 millions. Corollaire : la géométrie doit déjà être en espace monde,
 *     et les nœuds portent l'identité — une matrice par nœud s'appliquerait au
 *     pool entier.
 *   - Un bufferView d'indices **par primitive**, ce qui garde les accesseurs
 *     triviaux et permet à chaque primitive d'être un lot de dessin.
 *   - Indices en `uint32` (5125). Le modèle dépasse 65 535 sommets.
 *   - `KHR_materials_emissive_strength` pour séparer teinte et intensité.
 *
 * L'ordre des maillages est conservé tel quel : c'est lui qui rend contigus les
 * lots d'un même objet côté moteur. Ne pas trier par matériau.
 */
#ifndef NS_GLTF_WRITE_H
#define NS_GLTF_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLTF_MAX_NAME 128

/*
 * Sommet de sortie. La disposition est **exactement** celle d'`ns_vertex`
 * (48 octets, entrelacé) : les quatre attributs sont écrits dans des
 * bufferViews séparés, mais les conserver dans cet ordre ici évite d'avoir
 * deux définitions du même sommet qui pourraient dériver.
 */
typedef struct gltf_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4];
} gltf_vertex;

typedef struct gltf_material {
    char  name[GLTF_MAX_NAME];
    float base_color[4];        /* rgba ; a < 0.999 => alphaMode BLEND */
    float metallic;
    float roughness;
    float emissive[3];          /* teinte normalisée */
    float emissive_strength;    /* 0 => aucun bloc émissif écrit */
    int   texture;              /* index dans `textures`, -1 si aucune */
} gltf_material;

typedef struct gltf_primitive {
    const uint32_t *indices;
    size_t          index_count;
    int             material;   /* index dans `materials`, -1 si aucun */
} gltf_primitive;

typedef struct gltf_mesh {
    char                  name[GLTF_MAX_NAME];
    const gltf_primitive *prims;
    size_t                prim_count;
} gltf_mesh;

typedef struct gltf_scene {
    const gltf_vertex   *verts;
    size_t               vert_count;

    const gltf_mesh     *meshes;
    size_t               mesh_count;

    const gltf_material *materials;
    size_t               material_count;

    /* Noms de fichier des textures, sans répertoire. Le préfixe est ajouté à
     * l'écriture pour que l'URI reste relative au glTF. */
    const char *const   *textures;
    size_t               texture_count;

    /* NULL => valeurs par défaut : "textures/", "Nineteen V15", "salle". */
    const char *texture_prefix;
    const char *generator;
    const char *scene_name;
} gltf_scene;

/*
 * Écrit `out_path` et le `.bin` voisin (même base, extension `.bin`).
 * S'arrête net sur erreur : un outil de build qui échoue doit casser le build,
 * pas produire un asset que l'on découvrira incomplet à l'exécution.
 *
 * Renvoie la taille du `.bin` écrit, pour que l'appelant puisse la journaliser
 * avec son propre libellé.
 */
size_t gltf_write(const gltf_scene *s, const char *out_path);

#endif /* NS_GLTF_WRITE_H */
