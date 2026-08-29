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

#define VM_SIDES 12          /* les membres : douze pans suffisent à un tube */
#define VM_DIGIT_SIDES 8     /* un doigt fait 18 mm de large, huit pans le rendent rond */

typedef struct vm_build {
    ns_vertex *verts;
    uint32_t  *indices;
    uint32_t   vertex_count, vertex_cap;
    uint32_t   index_count, index_cap;
    float      zlen;      /* longueur du segment en cours, en mètres */
} vm_build;

static void vm_vertex(vm_build *b, ns_v3 p, ns_v3 n, float u, float v)
{
    if (b->vertex_count >= b->vertex_cap) return;
    ns_vertex *o = &b->verts[b->vertex_count++];
    /*
     * Z est ramené à la convention du shader ICI et nulle part ailleurs : on
     * modélise en millimètres partout au-dessus, et cette seule division dit
     * comment on passe à la longueur normalisée. La normale, elle, ne subit
     * rien — le shader remultiplie par la même longueur, le produit est
     * l'identité, et c'est ce qui autorise des normales hors du plan radial.
     */
    o->position[0] = p.x; o->position[1] = p.y; o->position[2] = p.z / b->zlen;
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

/* ==========================================================================
 * La primitive unique : un tube à section elliptique balayé le long d'une
 * ligne brisée.
 * ==========================================================================
 *
 * Elle remplace les trois constructeurs ad hoc d'avant — prisme effilé, doigt
 * droit, disque — parce qu'ils partageaient tous le même défaut : ils ne
 * savaient faire que du DROIT. Un avant-bras droit passe encore ; quatre doigts
 * droits font une fourchette, et c'est exactement ce qu'on voyait à l'écran.
 *
 * Une ligne brisée coûte le même code et donne la phalange, le pouce opposé, le
 * galbe de la paume et le renflement des jointures. C'est le seul changement de
 * cette passe qui compte vraiment.
 *
 * Le repère : X et Y sont les axes du plan de section, la ligne brisée vit dans
 * le plan YZ (une main fléchit autour de X, comme une vraie), et l'excentrement
 * en X d'un nœud place le doigt sur sa colonne.
 */
typedef struct vm_node {
    float x, y, z;      /* centre de la section, en mètres */
    float rx, ry;       /* demi-largeur et demi-épaisseur, en mètres */
} vm_node;

/*
 * `cap0`/`cap1` : DÔME, PLAT ou RIEN.
 *
 * Rien du tout est le bon choix là où deux segments se rejoignent — un disque
 * plat au coude, c'est un tube coupé net, et c'est ce qu'on voyait. Le dôme
 * ferme un bout libre : le coude, le bout d'un doigt.
 */
typedef enum vm_cap { VM_CAP_NONE = 0, VM_CAP_FLAT, VM_CAP_DOME } vm_cap;

static ns_v3 vm_ring_point(const vm_node *n, ns_v3 u, ns_v3 v, float a)
{
    const float ca = cosf(a), sa = sinf(a);
    return ns_v3_make(n->x + (u.x * ca * n->rx) + (v.x * sa * n->ry),
                      n->y + (u.y * ca * n->rx) + (v.y * sa * n->ry),
                      n->z + (u.z * ca * n->rx) + (v.z * sa * n->ry));
}

static void vm_sweep(vm_build *b, const vm_node *nodes, int count, int sides,
                     vm_cap cap0, vm_cap cap1)
{
    if (count < 2 || sides < 3) return;

    const uint32_t base = b->vertex_count;
    const ns_v3 U = ns_v3_make(1.0f, 0.0f, 0.0f);   /* le plan de flexion est YZ */

    for (int k = 0; k < count; ++k) {
        /*
         * La tangente par différence CENTRÉE aux nœuds intérieurs : prendre le
         * segment aval seul donnerait une cassure d'éclairage à chaque
         * jointure, c'est-à-dire un doigt en tuyaux d'orgue.
         */
        const vm_node *p = &nodes[k > 0 ? k - 1 : 0];
        const vm_node *q = &nodes[k < count - 1 ? k + 1 : count - 1];
        ns_v3 T = ns_v3_norm(ns_v3_make(q->x - p->x, q->y - p->y, q->z - p->z));
        ns_v3 V = ns_v3_cross(T, U);
        const float vl = ns_v3_len(V);
        V = (vl > 1e-5f) ? ns_v3_scale(V, 1.0f / vl) : ns_v3_make(0.0f, 1.0f, 0.0f);

        for (int i = 0; i <= sides; ++i) {
            const float a = (float)i / (float)sides * NS_TAU;
            const ns_v3 pos = vm_ring_point(&nodes[k], U, V, a);
            /* Normale d'une ELLIPSE : (cos/rx, sin/ry), pas (cos, sin). Sans
             * ça l'éclairage d'une paume aplatie est celui d'un cylindre. */
            const float nu = cosf(a) / ns_maxf(nodes[k].rx, 1e-4f);
            const float nv = sinf(a) / ns_maxf(nodes[k].ry, 1e-4f);
            const ns_v3 nrm = ns_v3_norm(ns_v3_add(ns_v3_scale(U, nu), ns_v3_scale(V, nv)));
            vm_vertex(b, pos, nrm, (float)i / (float)sides, (float)k / (float)(count - 1));
        }
    }

    const uint32_t stride = (uint32_t)sides + 1u;
    for (int k = 0; k < count - 1; ++k) {
        for (int i = 0; i < sides; ++i) {
            const uint32_t v0 = base + (uint32_t)k * stride + (uint32_t)i;
            const uint32_t v1 = v0 + stride;
            vm_tri(b, v0, v1, v1 + 1);
            vm_tri(b, v0, v1 + 1, v0 + 1);
        }
    }

    for (int end = 0; end < 2; ++end) {
        const vm_cap cap = end ? cap1 : cap0;
        if (cap == VM_CAP_NONE) continue;

        const int k = end ? count - 1 : 0;
        const vm_node *n = &nodes[k];
        const vm_node *p = &nodes[k > 0 ? k - 1 : 0];
        const vm_node *q = &nodes[k < count - 1 ? k + 1 : count - 1];
        ns_v3 T = ns_v3_norm(ns_v3_make(q->x - p->x, q->y - p->y, q->z - p->z));
        if (!end) T = ns_v3_scale(T, -1.0f);        /* le bout amont regarde en arrière */
        ns_v3 V = ns_v3_cross(T, U);
        const float vl = ns_v3_len(V);
        V = (vl > 1e-5f) ? ns_v3_scale(V, 1.0f / vl) : ns_v3_make(0.0f, 1.0f, 0.0f);

        if (cap == VM_CAP_FLAT) {
            const uint32_t centre = b->vertex_count;
            vm_vertex(b, ns_v3_make(n->x, n->y, n->z), T, 0.5f, 0.5f);
            for (int i = 0; i <= sides; ++i) {
                const float a = (float)i / (float)sides * NS_TAU;
                vm_vertex(b, vm_ring_point(n, U, V, a), T,
                          0.5f + cosf(a) * 0.5f, 0.5f + sinf(a) * 0.5f);
            }
            for (int i = 0; i < sides; ++i) {
                if (end) vm_tri(b, centre, centre + 1u + (uint32_t)i, centre + 2u + (uint32_t)i);
                else     vm_tri(b, centre, centre + 2u + (uint32_t)i, centre + 1u + (uint32_t)i);
            }
            continue;
        }

        /*
         * Le DÔME. Trois anneaux d'un quart de sphère aplati : c'est ce qui
         * transforme un coude coupé net en articulation, et un doigt coupé net
         * en doigt. Le rayon suit celui de la section, donc un bout de doigt
         * est arrondi dans les deux sens comme une pulpe.
         */
        enum { DOME_RINGS = 3 };
        const uint32_t dome = b->vertex_count;
        for (int r = 1; r <= DOME_RINGS; ++r) {
            const float t = (float)r / (float)DOME_RINGS;
            const float ang = t * NS_PI * 0.5f;
            const float sh = sinf(ang), ch = cosf(ang);
            for (int i = 0; i <= sides; ++i) {
                const float a = (float)i / (float)sides * NS_TAU;
                const vm_node ring = { n->x, n->y, n->z, n->rx * ch, n->ry * ch };
                const ns_v3 rim = vm_ring_point(&ring, U, V, a);
                /* La hauteur du dôme suit le PLUS PETIT demi-axe : une paume
                 * plate ne se termine pas par une demi-sphère. */
                const float h = ns_minf(n->rx, n->ry) * sh;
                const ns_v3 pos = ns_v3_add(rim, ns_v3_scale(T, h));
                const float nu = cosf(a) * ch / ns_maxf(n->rx, 1e-4f);
                const float nv = sinf(a) * ch / ns_maxf(n->ry, 1e-4f);
                ns_v3 nrm = ns_v3_add(ns_v3_add(ns_v3_scale(U, nu), ns_v3_scale(V, nv)),
                                      ns_v3_scale(T, sh / ns_maxf(ns_minf(n->rx, n->ry), 1e-4f)));
                nrm = ns_v3_norm(nrm);
                vm_vertex(b, pos, nrm, (float)i / (float)sides, t);
            }
        }
        /* L'anneau du bord appartient au balayage : le dôme s'y raccorde. */
        const uint32_t rim0 = base + (uint32_t)k * stride;
        for (int r = 0; r < DOME_RINGS; ++r) {
            for (int i = 0; i < sides; ++i) {
                const uint32_t a0 = (r == 0) ? rim0 + (uint32_t)i
                                             : dome + (uint32_t)(r - 1) * stride + (uint32_t)i;
                const uint32_t b0 = dome + (uint32_t)r * stride + (uint32_t)i;
                if (end) { vm_tri(b, a0, b0, b0 + 1); vm_tri(b, a0, b0 + 1, a0 + 1); }
                else     { vm_tri(b, a0, b0 + 1, b0); vm_tri(b, a0, a0 + 1, b0 + 1); }
            }
        }
    }
}

/* ==========================================================================
 * Un membre : manche ou avant-bras
 * ========================================================================== */

/*
 * Trois nœuds au lieu de deux, et un galbe au milieu.
 *
 * Un avant-bras n'est pas un cône : il est le plus large au tiers proximal, là
 * où sont les muscles, et le plus fin au poignet. Deux nœuds ne peuvent pas
 * dire ça. Le coût est de treize sommets.
 *
 * Le bout AVAL n'est jamais bouché : c'est là que vient l'articulation
 * suivante, et un disque plat à cet endroit est précisément le « tube coupé
 * net » qu'on reprochait aux bras.
 */
static void vm_limb(vm_build *b, float r0, float rmid, float r1, float flatten)
{
    const vm_node n[3] = {
        { 0.0f, 0.0f,  0.0f,             r0,   r0   * flatten },
        { 0.0f, 0.0f, -b->zlen * 0.34f,  rmid, rmid * flatten },
        { 0.0f, 0.0f, -b->zlen,          r1,   r1   * flatten },
    };
    vm_sweep(b, n, 3, VM_SIDES, VM_CAP_DOME, VM_CAP_NONE);
}

/* ==========================================================================
 * La main
 * ==========================================================================
 *
 * Ce que le propriétaire en a dit : « Le personnage a des bras très mal réalisé
 * pour ne pas dire horrible on dirait que c'est fait par un enfant. » Il avait
 * raison, et la faute était identifiable :
 *
 *   - les quatre doigts étaient des tubes DROITS, partant de la paume vers
 *     l'avant, en éventail. Une main ne fait ça dans aucune circonstance ; à
 *     l'écran c'était une fourchette ;
 *   - la main ne se REFERMAIT sur rien. Posée sur la boule d'un manche, elle la
 *     traversait ;
 *   - le poignet était un ressaut : l'avant-bras finissait à 34 mm de rayon et
 *     la paume commençait à 40, avec un disque plat entre les deux.
 *
 * La main est maintenant modélisée fléchie, une fois pour toutes, et c'est le
 * choix qui rend tout le reste possible. Une main de joueur n'est JAMAIS
 * ouverte : au repos elle est enroulée, sur une boule d'arcade elle l'enveloppe,
 * au-dessus des boutons elle est en suspension au-dessus d'eux. Les trois
 * lectures demandent la même flexion — de trente à cent-cinq degrés cumulés du
 * métacarpe à la dernière phalange — et une seule géométrie les sert.
 *
 * Cotes : main d'adulte. Paume de 98 mm, largeur aux jointures 84 mm,
 * épaisseur 32 mm, poignet 54 x 38 mm. Les longueurs de doigts sont les
 * proportions réelles — majeur le plus long, auriculaire à 80 % de l'index.
 */

/* Flexion cumulée aux trois articulations d'un doigt, en degrés. */
static const float VM_FLEX[3] = { 30.0f, 80.0f, 105.0f };

/* Longueur totale de chaque doigt depuis sa jointure, en mètres, et
 * excentrement en X de la colonne. L'index est en premier. */
static const float VM_FINGER_LEN[4]  = { 0.073f, 0.079f, 0.074f, 0.058f };
static const float VM_FINGER_X[4]    = { 0.0295f, 0.0100f, -0.0100f, -0.0285f };
/* Partage entre les trois phalanges : proximale, moyenne, distale. */
static const float VM_PHALANX[3]     = { 0.45f, 0.31f, 0.24f };

/* Y de la ligne des jointures : la paume est bombée côté dos, creuse côté
 * paume, et les doigts partent de sa face palmaire. */
#define VM_KNUCKLE_Z (-0.098f)

/*
 * Les quatre nœuds d'un doigt — la jointure et les trois articulations.
 *
 * Isolé de la construction du maillage pour UNE raison, et elle vaut la
 * fonction : `ns_viewmodel_fingertip()` a besoin du dernier de ces nœuds pour
 * dire à l'IK où le doigt tombe réellement. Recopier le calcul là-bas aurait
 * marché jusqu'à la première retouche de la flexion — après quoi le bras
 * aurait visé un bout de doigt qui n'existe plus, et rien n'aurait signalé la
 * dérive : la main serait simplement passée à côté du bouton.
 */
static void vm_finger_nodes(float sx, int i, vm_node n[4])
{
    const float len = VM_FINGER_LEN[i];
    /*
     * La colonne des jointures n'est pas droite : l'arche métacarpienne place
     * l'index et l'auriculaire plus en retrait que le majeur. C'est ce qui
     * donne à une main vue de dos son contour arrondi plutôt qu'un peigne.
     */
    static const float recul[4] = { 0.006f, 0.000f, 0.003f, 0.011f };
    /* Le rayon décroît de la jointure à la pulpe ; la section est un peu plus
     * large que profonde, comme un vrai doigt. */
    static const float r[4] = { 0.0092f, 0.0086f, 0.0078f, 0.0066f };

    float x = VM_FINGER_X[i] * sx;
    float y = -0.0050f;
    float z = VM_KNUCKLE_Z + recul[i];

    n[0] = (vm_node){ x, y, z, r[0], r[0] * 0.92f };
    for (int k = 0; k < 3; ++k) {
        const float a = VM_FLEX[k] * NS_DEG2RAD;
        const float l = len * VM_PHALANX[k];
        y -= sinf(a) * l;
        z -= cosf(a) * l;
        /* L'éventail : les doigts s'écartent légèrement en s'éloignant. */
        x += VM_FINGER_X[i] * sx * 0.055f;
        n[k + 1] = (vm_node){ x, y, z, r[k + 1], r[k + 1] * 0.92f };
    }
}

ns_v3 ns_viewmodel_fingertip(bool right)
{
    vm_node n[4];
    vm_finger_nodes(right ? 1.0f : -1.0f, 1, n);   /* 1 = le majeur, le plus long */
    return ns_v3_make(n[3].x, n[3].y, n[3].z);
}

static void vm_finger(vm_build *b, float sx, int i)
{
    vm_node n[4];
    vm_finger_nodes(sx, i, n);
    vm_sweep(b, n, 4, VM_DIGIT_SIDES, VM_CAP_NONE, VM_CAP_DOME);
}

static void vm_hand(vm_build *b, bool right)
{
    const float sx = right ? 1.0f : -1.0f;   /* le pouce change de côté */

    /*
     * La PAUME, en cinq sections. Elle part exactement au rayon où l'avant-bras
     * s'arrête — c'est ce qui supprime le ressaut du poignet — s'élargit jusqu'à
     * l'arche métacarpienne, puis se rétrécit un peu avant la ligne des
     * jointures. Elle est aussi CREUSÉE côté paume : le centre de section
     * descend en Y, ce qui donne le creux dans lequel une boule de manche vient
     * se loger.
     */
    const vm_node palm[5] = {
        { 0.0f,  0.0000f,  0.000f, 0.0275f, 0.0190f },   /* le poignet */
        { 0.0f, -0.0030f, -0.026f, 0.0340f, 0.0175f },
        { 0.0f, -0.0055f, -0.055f, 0.0400f, 0.0165f },   /* l'arche, au plus large */
        { 0.0f, -0.0060f, -0.082f, 0.0410f, 0.0158f },
        { 0.0f, -0.0050f, VM_KNUCKLE_Z, 0.0385f, 0.0150f },
    };
    vm_sweep(b, palm, 5, VM_SIDES, VM_CAP_NONE, VM_CAP_DOME);

    /*
     * L'ÉMINENCE THÉNAR — le muscle à la base du pouce. Sans elle la main est
     * une planche : c'est le seul relief qui distingue une paume d'un dos de
     * main quand on la voit de trois quarts, et c'est exactement l'angle sous
     * lequel on voit ses propres mains sur un panneau de commande.
     */
    const vm_node thenar[3] = {
        { 0.020f * sx, -0.004f, -0.008f, 0.0150f, 0.0125f },
        { 0.026f * sx, -0.009f, -0.032f, 0.0175f, 0.0140f },
        { 0.024f * sx, -0.011f, -0.056f, 0.0130f, 0.0105f },
    };
    vm_sweep(b, thenar, 3, VM_DIGIT_SIDES, VM_CAP_DOME, VM_CAP_DOME);

    for (int i = 0; i < 4; ++i) vm_finger(b, sx, i);

    /*
     * Le POUCE : deux phalanges, opposé et fléchi. Il part du thénar, s'écarte
     * de trente-cinq degrés en X, et se referme vers l'axe de la main — c'est
     * l'opposition, et c'est elle qui fait qu'on voit une main qui TIENT
     * quelque chose plutôt qu'une main posée dessus.
     */
    const float ecart = 38.0f * NS_DEG2RAD;
    const float flex[2] = { 26.0f * NS_DEG2RAD, 58.0f * NS_DEG2RAD };
    const float lp[2] = { 0.040f, 0.031f };
    vm_node t[3];
    float x = 0.026f * sx, y = -0.012f, z = -0.050f;
    t[0] = (vm_node){ x, y, z, 0.0125f, 0.0115f };
    for (int k = 0; k < 2; ++k) {
        x += sinf(ecart) * lp[k] * sx;
        y -= sinf(flex[k]) * lp[k];
        z -= cosf(ecart) * cosf(flex[k]) * lp[k];
        t[k + 1] = (vm_node){ x, y, z, (k == 0) ? 0.0108f : 0.0092f,
                                       (k == 0) ? 0.0100f : 0.0086f };
    }
    vm_sweep(b, t, 3, VM_DIGIT_SIDES, VM_CAP_NONE, VM_CAP_DOME);
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
    b.zlen = 1.0f;

    /*
     * Rayons en mètres, à l'échelle d'un adulte, et l'enchaînement des trois
     * cotes de chaque membre est ce qui donne le galbe : le manche est le plus
     * large au biceps, l'avant-bras au tiers proximal.
     *
     * Le POIGNET est la cote qui compte le plus, et c'était la plus fausse.
     * L'avant-bras finissait à 34 mm quand la paume commençait à 40 : la main
     * était plus grosse que le bras qui la portait, avec un disque plat pour
     * faire la jonction. Il finit maintenant à 27,5 mm, exactement le premier
     * nœud de la paume — et le raccord ne se voit plus parce qu'il n'y en a
     * plus.
     */
    struct { float len, r0, rmid, r1, flatten; } limb[NS_VM_SEGMENT_COUNT] = {
        { NS_VM_UPPER_M, 0.058f, 0.055f, 0.046f, 0.94f },   /* manche gauche */
        { NS_VM_FORE_M,  0.046f, 0.048f, 0.0275f, 0.90f },  /* avant-bras gauche */
        { NS_VM_HAND_M,  0.0f, 0.0f, 0.0f, 0.0f },          /* main gauche */
        { NS_VM_UPPER_M, 0.058f, 0.055f, 0.046f, 0.94f },
        { NS_VM_FORE_M,  0.046f, 0.048f, 0.0275f, 0.90f },
        { NS_VM_HAND_M,  0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },                   /* le jeton, à sa vraie taille */
    };

    for (int s = 0; s < NS_VM_SEGMENT_COUNT; ++s) {
        first_index[s] = b.index_count;
        b.zlen = limb[s].len;
        if (s == NS_VM_TOKEN)                            vm_token(&b);
        else if (s == NS_VM_HAND_L || s == NS_VM_HAND_R) vm_hand(&b, s == NS_VM_HAND_R);
        else vm_limb(&b, limb[s].r0, limb[s].rmid, limb[s].r1, limb[s].flatten);
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
