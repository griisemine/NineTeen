/*
 * geo_shapes.h — générateurs de géométrie paramétrique, au-dessus de `geo_mesh`.
 *
 * Cinq générateurs suffisent à bâtir la salle : la boîte chanfreinée, le plan
 * subdivisé, le panneau à UV explicite, l'extrusion de profil et le pan de mur.
 * Tout le reste — bornes, piliers, poutres, moulures, mobilier — est une
 * composition de ces cinq-là, assemblée par `roomgen`.
 *
 * **Le chanfrein n'est pas un ornement.** Une arête vive ne capte aucune lumière
 * spéculaire : elle se lit comme un trait mort, et c'est ce qui fait qu'une
 * géométrie générée « sonne creux ». Une arête chanfreinée de 5 à 20 mm produit
 * un liseré qui suit les sources. C'est, à soi seul, la plus grande différence
 * perçue entre « généré » et « modélisé », pour trois quads par arête.
 *
 * **Les UV se calculent depuis la taille réelle**, jamais depuis les indices de
 * sommet : chaque générateur prend un `geo_uv` qui dit combien de mètres fait une
 * répétition de texture. C'est la correction directe du plafond de 2020, dont
 * les UV bouclaient une fois tous les 17,9 m — soit 0,056 répétition par mètre,
 * ce qui se lit comme un aplat quelle que soit la texture.
 *
 * Conventions communes :
 *
 *   - Unités : le **mètre**. Y vers le haut, main droite.
 *   - Les coordonnées de plan sont des `ns_v2` où `.x` est X et `.y` est **Z**.
 *   - L'enroulement est direct vu de l'extérieur. Le rendu n'élimine pas les
 *     faces arrière (`SDL_GPU_CULLMODE_NONE`), donc une erreur d'enroulement ne
 *     se verrait pas à l'image — mais `geo_signed_volume` la verrait, et le BVH
 *     aussi. On le tient donc, sans compter sur l'image pour le signaler.
 *   - Les générateurs produisent des normales **par face**. Le lissage et les
 *     tangentes viennent après, par `geo_smooth_normals` puis
 *     `geo_generate_tangents`, dans cet ordre (voir geo_mesh.h).
 */
#ifndef NS_GEO_SHAPES_H
#define NS_GEO_SHAPES_H

#include "geo_mesh.h"

/* ========================================================================== */
/* Paramétrage des UV                                                         */
/* ========================================================================== */

/*
 * `metres_per_tile` : côté, en mètres, d'une répétition de la texture. 1,5 pour
 * une moquette, 0,60 pour une dalle de plafond, 4,0 pour un grand mur uni.
 *
 * `fit` : la face couvre exactement [0,1]² au lieu de se répéter. Réservé aux
 * textures qui **ne sont pas pavables** — une affiche, une photo de marbre, un
 * marquee. Le mode est déclaré par matériau dans la description de salle, jamais
 * deviné : deviner est précisément l'erreur que cette reconstruction supprime.
 */
typedef struct geo_uv {
    float metres_per_tile;
    float offset_u, offset_v;
    bool  fit;
} geo_uv;

static inline geo_uv geo_uv_tile(float metres_per_tile)
{
    geo_uv uv;
    uv.metres_per_tile = (metres_per_tile > 1e-6f) ? metres_per_tile : 1.0f;
    uv.offset_u = uv.offset_v = 0.0f;
    uv.fit = false;
    return uv;
}

static inline geo_uv geo_uv_fit(void)
{
    geo_uv uv = { 1.0f, 0.0f, 0.0f, true };
    return uv;
}

/* ========================================================================== */
/* Boîte chanfreinée                                                          */
/* ========================================================================== */

/*
 * Masque de faces. On ne génère pas le dessous d'une plinthe ni l'arrière d'un
 * panneau plaqué : ce sont des triangles que le BVH parcourt et que personne ne
 * voit. `GEO_FACE_SIDES` sert aux fûts (piliers, poteaux) dont le dessus et le
 * dessous sont couverts par autre chose.
 */
enum {
    GEO_FACE_PX  = 1u << 0,
    GEO_FACE_NX  = 1u << 1,
    GEO_FACE_PY  = 1u << 2,
    GEO_FACE_NY  = 1u << 3,
    GEO_FACE_PZ  = 1u << 4,
    GEO_FACE_NZ  = 1u << 5,
    GEO_FACE_ALL = 0x3Fu,
    GEO_FACE_SIDES = GEO_FACE_PX | GEO_FACE_NX | GEO_FACE_PZ | GEO_FACE_NZ,
    GEO_FACE_NO_BOTTOM = GEO_FACE_ALL & ~GEO_FACE_NY
};

/*
 * Boîte **posée sur le sol** : elle occupe X ∈ [−sx/2, +sx/2], Y ∈ [0, sy],
 * Z ∈ [−sz/2, +sz/2]. C'est la convention utile pour un générateur de salle —
 * on place un objet par son empreinte au sol, pas par son centre — et elle évite
 * la faute de frappe classique du demi-hauteur oublié.
 *
 * Le chanfrein est écrêté à un tiers de la plus petite dimension. Une arête
 * absente du masque n'est pas chanfreinée : le biseau relie deux faces, s'il en
 * manque une il n'y a rien à relier.
 */
void geo_box(geo_mesh *m, ns_v3 size, float chamfer, uint32_t faces,
             const geo_uv *uv, int32_t material);

/* ========================================================================== */
/* Cylindre                                                                   */
/* ========================================================================== */

/*
 * Cylindre (ou cône tronqué) **posé sur Y = 0**, comme `geo_box` : il occupe
 * Y de 0 à `height`, centré en X/Z. Même convention, pour la même raison — on
 * place un objet par son empreinte au sol.
 *
 * Pourquoi il manquait, et pourquoi c'est un manque et pas un oubli : jusqu'ici
 * tout ce qui était rond dans la salle était une boîte chanfreinée, et ça
 * tenait tant qu'on la voyait de loin. Un bouton d'arcade de 38 mm regardé à
 * soixante centimètres — ce que fait le joueur dès qu'il pose les mains sur les
 * commandes — est un carré, et se lit comme tel. Les tabourets, les gobelets,
 * les tubes et les pieds de table posent la même question.
 *
 * `r_top` différent de `r_bottom` donne un cône tronqué : c'est ce qui fait un
 * bouton bombé plutôt qu'un palet, et un pied de tabouret qui s'évase.
 *
 * Les UV : la face latérale est dépliée en bande (u = tour, v = hauteur, à
 * l'échelle de `uv`), les faces planes sont projetées en disque. Un cylindre a
 * une couture — un tour complet ne peut pas se replier sans elle — et elle est
 * placée en −X, du côté qu'on regarde le moins.
 */
void geo_cylinder(geo_mesh *m, float r_bottom, float r_top, float height,
                  int sides, bool cap_bottom, bool cap_top,
                  const geo_uv *uv, int32_t material);

/* ========================================================================== */
/* Surface de révolution                                                      */
/* ========================================================================== */

/*
 * Fait tourner un profil autour de l'axe +Y. Le profil est une suite de couples
 * (rayon, hauteur), du BAS vers le haut ; un rayon nul ferme la forme en pointe.
 *
 * Pourquoi ça manquait, et pourquoi un empilement de cylindres ne suffit pas.
 * La boule d'un manche d'arcade était faite de trois troncs de cône empilés — un
 * raccord, un ventre, une calotte — à dix côtés. Ça donne un écrou, pas une
 * boule : les trois arêtes horizontales entre les tronçons sont vives, elles
 * accrochent chacune un liseré, et l'œil lit trois anneaux au lieu d'une sphère.
 * C'est l'objet que le joueur a le plus près des yeux pendant toute une partie,
 * et le seul qu'il touche.
 *
 * Une révolution résout les deux à la fois : les anneaux successifs viennent
 * d'un profil CONTINU, donc les normales se lissent d'elles-mêmes par le seuil
 * d'angle de `geo_mesh`, et il n'y a plus d'arête à accrocher.
 *
 * Les UV : `u` fait le tour (à l'échelle de `uv`), `v` suit la longueur d'arc
 * cumulée du profil — c'est ce qui empêche une texture de s'étirer là où le
 * profil est raide et de se tasser là où il est plat.
 *
 * La couture est en −X, comme celle de `geo_cylinder`, et pour la même raison.
 */
void geo_revolve(geo_mesh *m, const float *profile_ry, int count, int sides,
                 const geo_uv *uv, int32_t material);

/* ========================================================================== */
/* Plan subdivisé                                                             */
/* ========================================================================== */

/*
 * Plan horizontal à Y = 0, centré en X/Z. `face_up` false retourne la normale —
 * c'est ainsi qu'on obtient un plafond.
 *
 * La subdivision ne sert pas à la silhouette mais à l'éclairage : le brouillard
 * volumétrique et la remontée bilatérale (A5) échantillonnent par sommet, et un
 * sol de 22 m en deux triangles ne leur donne aucun point d'appui. Un quad tous
 * les deux mètres suffit.
 */
void geo_plane(geo_mesh *m, float size_x, float size_z, int subdiv_x, int subdiv_z,
               bool face_up, const geo_uv *uv, int32_t material);

/*
 * Rectangle libre, orienté par sa normale et son axe « droite », avec un
 * rectangle d'UV **explicite**. C'est le générateur des affiches, des enseignes,
 * des marquees et — surtout — des écrans de bornes, où l'UV doit tomber
 * exactement sur l'image et où toute répétition serait une faute.
 */
void geo_panel(geo_mesh *m, ns_v3 centre, ns_v3 normal, ns_v3 right,
               float width, float height,
               float u0, float v0, float u1, float v1, int32_t material);

/* ========================================================================== */
/* Extrusion de profil                                                        */
/* ========================================================================== */

/*
 * Balaie un profil 2D le long d'un chemin 3D : plinthes, corniches, cimaises,
 * chambranles, nez de comptoir, et plus tard le flanc galbé d'une borne.
 *
 * Le repère à chaque point du chemin est (droite, haut, tangente) avec
 * `haut = +Y` : le profil se dessine donc dans un plan **vertical**
 * perpendiculaire au chemin, `x` du profil vers `cross(+Y, tangente)` et `y` du
 * profil vers le haut. Un profil listé **dans le sens direct** donne des normales
 * tournées vers l'extérieur.
 *
 * Les angles sont mitrés : au joint, le repère prend la tangente moyenne et le
 * profil est étiré de 1/cos(θ/2) le long de « droite », ce qui conserve la
 * section vue de face — c'est ce que fait une boîte à onglet. Au-delà de ~150°
 * l'étirement est écrêté et l'outil avertit, parce qu'un retour d'équerre trop
 * fermé n'a pas d'onglet raisonnable et doit être découpé en deux moulures.
 *
 * `profile_closed` ferme le contour du profil (une moulure pleine) ; `path_closed`
 * boucle le chemin (une corniche qui fait le tour d'une pièce). Un profil fermé
 * sur un chemin ouvert reçoit deux bouchons en éventail.
 */
void geo_profile_extrude(geo_mesh *m,
                         const ns_v2 *profile, size_t profile_count, bool profile_closed,
                         const ns_v3 *path, size_t path_count, bool path_closed,
                         const geo_uv *uv, int32_t material);

/*
 * La même, avec un matériau PROPRE aux bouchons — et un cadrage d'UV pour eux.
 *
 * Elle existe pour une raison précise : sur une borne d'arcade, les deux
 * bouchons de l'extrusion SONT les deux flancs, et un flanc de borne porte une
 * sérigraphie qui n'a rien à voir avec la peinture du caisson. Les traiter
 * comme le reste obligeait à choisir entre un caisson qui porte la trame
 * partout et un flanc nu.
 *
 * `cap_uv_fit` cadre les UV des bouchons sur la boîte englobante du profil,
 * de (0,0) à (1,1) : une planche dessinée pour un flanc s'y pose entière, quelle
 * que soit la taille de la borne. Sans lui, les UV restent en mètres par
 * répétition comme partout ailleurs — ce qu'il faut pour une moulure.
 */
void geo_profile_extrude_capped(geo_mesh *m,
                                const ns_v2 *profile, size_t profile_count, bool profile_closed,
                                const ns_v3 *path, size_t path_count, bool path_closed,
                                const geo_uv *uv, int32_t material,
                                int32_t cap_material, bool cap_uv_fit);

/* ========================================================================== */
/* Pan de mur                                                                 */
/* ========================================================================== */

/*
 * Une ouverture dans un pan : baie de porte, passage, fenêtre de comptoir.
 *
 * `offset` est l'abscisse curviligne du bord gauche **le long de la ligne
 * médiane du pan**, cumulée depuis son premier point. `sill` est la hauteur de
 * l'allège (0 pour une porte), `head` celle du linteau.
 */
typedef struct geo_opening {
    char  name[64];
    float offset;
    float width;
    float sill;
    float head;
} geo_opening;

/*
 * Description d'un pan de mur.
 *
 * **Règle d'orientation, à retenir une fois** : listez les points de sorte qu'en
 * marchant de l'un au suivant, **l'intérieur soit à votre gauche**. Alors
 * `material_inner` habille la face intérieure et `material_outer` l'extérieure.
 * C'est la seule convention à tenir ; tout le reste s'en déduit.
 */
typedef struct geo_wall_desc {
    const char        *name;          /* pour les messages d'erreur, jamais NULL */
    const ns_v2       *points;        /* ligne médiane, en plan (x, z) */
    size_t             point_count;
    bool               closed;

    float              height;
    float              thickness;

    const geo_opening *openings;
    size_t             opening_count;

    geo_uv             uv;
    int32_t            material_inner;
    int32_t            material_outer;
    int32_t            material_reveal;   /* tableaux, arase, joues d'extrémité */

    bool               cap_top;       /* arase supérieure (inutile sous un plafond plein) */
    bool               cap_ends;      /* joues aux deux bouts d'une polyligne ouverte */
} geo_wall_desc;

/*
 * Émet un pan de mur.
 *
 * **Aucune opération booléenne.** Un pan est une ligne 2D plus une liste
 * d'ouvertures, et l'on émet une suite de panneaux rectangulaires dans l'ordre où
 * un dessinateur les tracerait — trumeau, sous-allège, linteau, trumeau — plus
 * les quatre tableaux de chaque baie. Que des quads : rien à départager, aucune
 * coplanarité, aucun cas limite de CSG.
 *
 * Toute description incohérente **arrête l'outil en nommant le pan et
 * l'ouverture** : ouverture à cheval sur deux segments, débordant du pan,
 * chevauchant sa voisine, allège au-dessus du linteau. Le symptôme, sinon,
 * serait un accesseur glTF de longueur nulle refusé par `cgltf_validate()` dans
 * `bvhbake` — trois étapes plus loin, dans un message parlant d'octets.
 */
void geo_wall_run(geo_mesh *m, const geo_wall_desc *d);

#endif /* NS_GEO_SHAPES_H */
