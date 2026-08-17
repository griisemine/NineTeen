/*
 * ns_viewmodel.c — la géométrie des bras, générée au démarrage.
 *
 * Pourquoi générée en C et pas par `tools/geo_shapes`
 * ---------------------------------------------------
 * Ce générateur-là produit de la géométrie **cuite en espace monde** pour un
 * fichier glTF, sans notion de segment ni de pivot, et il vit dans les outils de
 * build. Le viewmodel a besoin de l'inverse : des morceaux nommés, chacun avec sa
 * plage d'indices, transformés à chaque image. Cent cinquante lignes ici évitent
 * d'inventer un format de fichier et un chargeur pour trois cents triangles.
 *
 * Des prismes à douze pans plutôt que des boîtes chanfreinées : un bras est rond.
 * Un prisme dont les normales tournent avec l'anneau se lit comme un cylindre,
 * pour le même nombre de triangles qu'une boîte chanfreinée — et sans arête vive,
 * qui est ce qui trahit le plus une géométrie générée.
 */
#include "ns_viewmodel.h"

#include "ns_core.h"
#include "ns_scene.h"

#include <string.h>

void ns_viewmodel_pose_clear(ns_viewmodel_pose *p)
{
    if (!p) return;
    memset(p, 0, sizeof *p);
    for (int i = 0; i < NS_VM_SEGMENT_COUNT; ++i) {
        p->segment[i] = ns_m4_identity();
        p->length[i] = 1.0f;
    }
}

/* --------------------------------------------------------------------------
 * Construction
 * -------------------------------------------------------------------------- */

#define VM_SIDES 12

typedef struct vm_build {
    ns_vertex *verts;
    uint32_t  *indices;
    uint32_t   vertex_count, vertex_cap;
    uint32_t   index_count, index_cap;
} vm_build;

static void vm_vertex(vm_build *b, ns_v3 p, ns_v3 n, float u, float v)
{
    if (b->vertex_count >= b->vertex_cap) return;
    ns_vertex *o = &b->verts[b->vertex_count++];
    o->position[0] = p.x; o->position[1] = p.y; o->position[2] = p.z;
    o->normal[0] = n.x; o->normal[1] = n.y; o->normal[2] = n.z;
    o->uv[0] = u; o->uv[1] = v;
    /* Aucune normal map sur le viewmodel : la tangente doit être finie et
     * orthogonale, pas exacte. Une tangente nulle rendrait la base dégénérée si
     * un shader venait à s'en servir. */
    const ns_v3 t = (fabsf(n.y) < 0.9f) ? ns_v3_norm(ns_v3_cross(ns_v3_make(0, 1, 0), n))
                                        : ns_v3_make(1, 0, 0);
    o->tangent[0] = t.x; o->tangent[1] = t.y; o->tangent[2] = t.z; o->tangent[3] = 1.0f;
}

static void vm_tri(vm_build *b, uint32_t a, uint32_t c, uint32_t d)
{
    if (b->index_count + 3 > b->index_cap) return;
    b->indices[b->index_count++] = a;
    b->indices[b->index_count++] = c;
    b->indices[b->index_count++] = d;
}

/*
 * Prisme effilé le long de −Z local, de z = 0 à z = −1, de rayon `r0` à `r1`.
 * `flatten` aplatit la section en Y : une main n'est pas un tube.
 *
 * La longueur est normalisée à 1 : c'est la matrice de pose qui donne la vraie,
 * ce qui permet de rallonger un avant-bras sans reconstruire le maillage.
 */
static void vm_prism(vm_build *b, float r0, float r1, float flatten)
{
    const uint32_t base = b->vertex_count;

    for (int i = 0; i <= VM_SIDES; ++i) {
        const float a = (float)i / (float)VM_SIDES * NS_TAU;
        const float ca = cosf(a), sa = sinf(a);
        /* Normale radiale, corrigée de l'aplatissement — sinon l'éclairage d'une
         * main plate est celui d'un cylindre. */
        const ns_v3 n = ns_v3_norm(ns_v3_make(ca, sa / ns_maxf(flatten, 0.05f), 0.0f));
        const float u = (float)i / (float)VM_SIDES;
        vm_vertex(b, ns_v3_make(ca * r0, sa * r0 * flatten, 0.0f), n, u, 0.0f);
        vm_vertex(b, ns_v3_make(ca * r1, sa * r1 * flatten, -1.0f), n, u, 1.0f);
    }
    for (int i = 0; i < VM_SIDES; ++i) {
        const uint32_t v0 = base + (uint32_t)(i * 2);
        vm_tri(b, v0, v0 + 1, v0 + 3);
        vm_tri(b, v0, v0 + 3, v0 + 2);
    }

    /* Bouchons. Sans eux, une main vue de bout est un tube creux. */
    for (int end = 0; end < 2; ++end) {
        const float z = end ? -1.0f : 0.0f;
        const float r = end ? r1 : r0;
        const ns_v3 n = ns_v3_make(0.0f, 0.0f, end ? -1.0f : 1.0f);
        const uint32_t centre = b->vertex_count;
        vm_vertex(b, ns_v3_make(0.0f, 0.0f, z), n, 0.5f, 0.5f);
        for (int i = 0; i <= VM_SIDES; ++i) {
            const float a = (float)i / (float)VM_SIDES * NS_TAU;
            vm_vertex(b, ns_v3_make(cosf(a) * r, sinf(a) * r * flatten, z), n,
                      0.5f + cosf(a) * 0.5f, 0.5f + sinf(a) * 0.5f);
        }
        for (int i = 0; i < VM_SIDES; ++i) {
            if (end) vm_tri(b, centre, centre + 1 + (uint32_t)i, centre + 2 + (uint32_t)i);
            else     vm_tri(b, centre, centre + 2 + (uint32_t)i, centre + 1 + (uint32_t)i);
        }
    }
}

/* Le jeton : un disque épais, couché dans le plan XY local, épaisseur sur Z.
 * Modélisé à sa vraie taille (24 mm de diamètre, 2 mm d'épaisseur) plutôt qu'à
 * l'unité — il n'a aucune raison d'être mis à l'échelle par la pose. */
static void vm_token(vm_build *b)
{
    const float R = 0.012f, H = 0.001f;
    const uint32_t base = b->vertex_count;

    for (int i = 0; i <= VM_SIDES; ++i) {
        const float a = (float)i / (float)VM_SIDES * NS_TAU;
        const ns_v3 n = ns_v3_make(cosf(a), sinf(a), 0.0f);
        vm_vertex(b, ns_v3_make(cosf(a) * R, sinf(a) * R, H), n, (float)i / VM_SIDES, 0.0f);
        vm_vertex(b, ns_v3_make(cosf(a) * R, sinf(a) * R, -H), n, (float)i / VM_SIDES, 1.0f);
    }
    for (int i = 0; i < VM_SIDES; ++i) {
        const uint32_t v0 = base + (uint32_t)(i * 2);
        vm_tri(b, v0, v0 + 1, v0 + 3);
        vm_tri(b, v0, v0 + 3, v0 + 2);
    }
    for (int face = 0; face < 2; ++face) {
        const float z = face ? -H : H;
        const ns_v3 n = ns_v3_make(0.0f, 0.0f, face ? -1.0f : 1.0f);
        const uint32_t centre = b->vertex_count;
        vm_vertex(b, ns_v3_make(0.0f, 0.0f, z), n, 0.5f, 0.5f);
        for (int i = 0; i <= VM_SIDES; ++i) {
            const float a = (float)i / (float)VM_SIDES * NS_TAU;
            vm_vertex(b, ns_v3_make(cosf(a) * R, sinf(a) * R, z), n,
                      0.5f + cosf(a) * 0.5f, 0.5f + sinf(a) * 0.5f);
        }
        for (int i = 0; i < VM_SIDES; ++i) {
            if (face) vm_tri(b, centre, centre + 1 + (uint32_t)i, centre + 2 + (uint32_t)i);
            else      vm_tri(b, centre, centre + 2 + (uint32_t)i, centre + 1 + (uint32_t)i);
        }
    }
}

/*
 * Construit les sept segments dans un seul couple de tampons, et renvoie la
 * plage d'indices de chacun. Un tampon par segment coûterait sept liaisons par
 * image pour trois cents triangles.
 */
void ns_viewmodel_build(ns_vertex *verts, uint32_t vert_cap, uint32_t *out_vert_count,
                        uint32_t *indices, uint32_t index_cap, uint32_t *out_index_count,
                        uint32_t first_index[NS_VM_SEGMENT_COUNT],
                        uint32_t index_count[NS_VM_SEGMENT_COUNT])
{
    vm_build b;
    memset(&b, 0, sizeof b);
    b.verts = verts; b.vertex_cap = vert_cap;
    b.indices = indices; b.index_cap = index_cap;

    /* Rayons en mètres, à l'échelle d'un adulte. La manche est plus large que
     * l'avant-bras : c'est du tissu, et c'est ce qui la distingue au premier
     * coup d'œil. */
    struct { float r0, r1, flatten; } shape[NS_VM_SEGMENT_COUNT] = {
        { 0.055f, 0.048f, 1.00f },   /* manche gauche */
        { 0.044f, 0.034f, 1.00f },   /* avant-bras gauche */
        { 0.036f, 0.030f, 0.55f },   /* main gauche, aplatie */
        { 0.055f, 0.048f, 1.00f },
        { 0.044f, 0.034f, 1.00f },
        { 0.036f, 0.030f, 0.55f },
        { 0.0f, 0.0f, 0.0f },        /* le jeton a sa propre construction */
    };

    for (int s = 0; s < NS_VM_SEGMENT_COUNT; ++s) {
        first_index[s] = b.index_count;
        if (s == NS_VM_TOKEN) vm_token(&b);
        else                  vm_prism(&b, shape[s].r0, shape[s].r1, shape[s].flatten);
        index_count[s] = b.index_count - first_index[s];
    }

    *out_vert_count = b.vertex_count;
    *out_index_count = b.index_count;

    if (b.vertex_count >= b.vertex_cap || b.index_count >= b.index_cap) {
        /* Tronqué : le maillage serait incomplet, et un segment ouvert se voit
         * bien plus qu'il ne se diagnostique. */
        NS_ERROR("viewmodel : tampons trop petits (%u/%u sommets, %u/%u indices)",
                 b.vertex_count, b.vertex_cap, b.index_count, b.index_cap);
    }
}
