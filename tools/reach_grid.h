/*
 * reach_grid.h — où le joueur peut ALLER, mesuré sur une grille en plan.
 *
 * **Pourquoi ce fichier existe.** La salle a été livrée avec son joueur ENFERMÉ :
 * le sas d'entrée était un contour fermé sans la moindre ouverture vers le hall,
 * la description se compilait, les trente-six tests passaient, et le paquet
 * partait avec un joueur scellé dans une boîte de 1,67 x 9,09 m. Rien ne mesurait
 * la seule chose qui compte pour une salle : qu'on puisse y CIRCULER. Chaque
 * contrôle existant regarde un objet — est-il dedans, est-il posé, se pénètre-t-il
 * — et aucun ne regarde le VIDE entre eux.
 *
 * **Le modèle.** Une grille XZ, un remplissage par diffusion depuis le départ du
 * joueur, et une seule question par cellule : le CORPS y tient-il ? Le corps est
 * une capsule, donc un disque en plan ; « il tient » veut dire qu'aucun obstacle
 * n'est à moins de son rayon. C'est exactement la collision du jeu réduite à deux
 * dimensions — `room_camera_tick` glisse une capsule de `personnage.rayon` contre
 * la géométrie, rien d'autre.
 *
 * **Ce qui est séparé, et pourquoi.** Ce fichier ne connaît ni les murs, ni les
 * bornes, ni le format de la description : il reçoit des segments épais et des
 * triangles, il rend des composantes connexes. C'est ce qui le rend testable sur
 * des cas fabriqués — une pièce ouverte, une pièce scellée, une porte à la
 * largeur exacte du joueur, une porte trop étroite — sans salle, sans asset et
 * sans GPU. Un contrôle d'accessibilité qu'on ne peut pas éprouver sur une pièce
 * scellée CONNUE est un contrôle dont on ne sait pas s'il verrait la prochaine.
 *
 * ------------------------------------------------------------------------
 * LE PRIX DE LA DISCRÉTISATION, écrit ici plutôt que découvert plus tard
 * ------------------------------------------------------------------------
 * Un obstacle marque toute cellule qu'il TOUCHE, et la distance se mesure
 * ensuite de centre à centre. Une cellule libre peut donc se croire jusqu'à une
 * demi-cellule trop près d'un obstacle qui, lui, peut être une demi-cellule plus
 * loin que la cellule qui le porte. On rend cette erreur au joueur au lieu de la
 * lui retenir — `reach_grid_solve` exige `distance > rayon − pas` — parce qu'un
 * contrôle qui refuse une porte JOUABLE est un contrôle qu'on retire dans
 * l'heure, et on retombe alors sur le joueur enfermé.
 *
 * Au pas de 5 cm et pour un rayon de 32 cm, cela donne, en largeur de passage :
 *
 *     >= 64 cm   passe toujours   (la porte à la largeur exacte du joueur)
 *      < 44 cm   refusé toujours
 *     entre      dépend de l'alignement de la grille sur le passage
 *
 * La zone grise est le prix du pas de 5 cm. Elle est étroite, elle est du bon
 * côté (on ne refuse jamais un passage réellement franchissable), et elle est
 * écrite : personne n'aura à la redécouvrir en mesurant.
 */
#ifndef NS_REACH_GRID_H
#define NS_REACH_GRID_H

#include "ns_math.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct reach_grid {
    float x0, z0;    /* coin (x min, z min) de la cellule (0, 0) */
    float cell;      /* côté d'une cellule, en mètres */
    int   nx, nz;

    /* 1 : un obstacle touche la cellule. Écrit par les `reach_*_block_*`, et
     * directement par l'appelant quand la règle lui appartient — « hors du
     * bâtiment », par exemple, que ce fichier n'a aucun moyen de connaître. */
    unsigned char *solid;

    /* Rempli par `reach_grid_solve`. */
    unsigned char *walkable;   /* 1 : le corps du joueur tient sur la cellule */
    int           *label;      /* −1 hors des zones libres, sinon la composante */
    int            components;
    /* Le compte de cellules par composante, tenu pendant l'étiquetage. Le
     * rebalayage de la grille à chaque interrogation coûtait le carré du nombre
     * de cibles, sur une grille de 200 000 cellules — un contrôle lent à chaque
     * build finit par se faire retirer du build. */
    size_t        *component_cells;
} reach_grid;

void reach_grid_init(reach_grid *g, float min_x, float min_z,
                     float max_x, float max_z, float cell);
void reach_grid_release(reach_grid *g);

/* Index linéaire. Aucun bornage : les appelants bouclent sur `nx` et `nz`. */
static inline size_t reach_index(const reach_grid *g, int ix, int iz)
{
    return (size_t)iz * (size_t)g->nx + (size_t)ix;
}

void reach_grid_centre(const reach_grid *g, int ix, int iz, float *x, float *z);
/* Faux si le point tombe hors de la grille — ce qui est une information, pas
 * une erreur : un point de vue posé dehors doit se dire, pas se taire. */
bool reach_grid_cell_of(const reach_grid *g, float x, float z, int *ix, int *iz);

/* --------------------------------------------------------------------------
 * Masques
 *
 * Un mur PERCÉ se compose à part : on marque ses pans, on retire ses baies, puis
 * on verse le tout dans la grille. Percer directement dans la grille rouvrirait
 * les murs des voisins qui passent au même endroit — et c'est exactement le cas
 * de cette salle, où deux déclarations de mur suivent le même tracé.
 * -------------------------------------------------------------------------- */
unsigned char *reach_mask_new(const reach_grid *g);
void reach_mask_free(unsigned char *mask);
void reach_grid_add(reach_grid *g, const unsigned char *mask);

/*
 * Un pan d'épaisseur `2 * half_width` de `a` à `b`, prolongé de `cap_a` et
 * `cap_b` à ses deux bouts.
 *
 * Les prolongements servent aux ANGLES : deux rectangles qui se rejoignent
 * laissent une échancrure là où le mur, lui, a un coin d'onglet plein. On
 * prolonge donc d'une demi-épaisseur aux jointures, et de rien du tout aux
 * extrémités libres d'une polyligne — où le mur s'arrête vraiment, et où
 * prolonger rétrécirait un passage de dix centimètres.
 */
void reach_mask_segment(const reach_grid *g, unsigned char *mask,
                        ns_v2 a, ns_v2 b, float half_width,
                        float cap_a, float cap_b);

/* La projection en plan d'un triangle. Les triangles dégénérés — toute face
 * VERTICALE en est un, vue de dessus — sont le cas normal ici, pas une
 * exception : le critère est donc une distance au triangle, qui les traite sans
 * cas particulier, et non une aire signée qui les perdrait. */
void reach_mask_triangle(const reach_grid *g, unsigned char *mask,
                         ns_v2 a, ns_v2 b, ns_v2 c);

/* Une baie : on efface le rectangle de `a` à `b`, sur `half_width` de part et
 * d'autre. Généreux en travers du mur — on est dans le masque de CE mur, et
 * déborder n'y atteint rien d'autre — et juste dans sa longueur. */
void reach_mask_carve(const reach_grid *g, unsigned char *mask,
                      ns_v2 a, ns_v2 b, float half_width);

/* Les mêmes, versés directement : pour un obstacle sans baie. */
void reach_grid_block_segment(reach_grid *g, ns_v2 a, ns_v2 b, float half_width,
                              float cap_a, float cap_b);
void reach_grid_block_triangle(reach_grid *g, ns_v2 a, ns_v2 b, ns_v2 c);

/* --------------------------------------------------------------------------
 * Le DEHORS
 * -------------------------------------------------------------------------- */

/* Un contour fermé du bâtiment, et l'épaisseur du mur qui le suit. */
typedef struct reach_contour {
    const ns_v2 *points;
    size_t       count;
    float        thickness;
} reach_contour;

/* Deux primitives de plan, ici plutôt qu'ailleurs parce que la règle du dehors
 * s'en sert et qu'elle doit être éprouvable sans salle. `roomgen` les emploie
 * aussi : une seconde écriture du théorème de Jordan finirait par diverger de
 * celle-ci d'un cas limite. La convention est celle de tout le plan — `.x` est
 * X, `.y` est **Z**. */
bool  reach_point_in_polygon(const ns_v2 *poly, size_t count, float x, float z);
float reach_point_segment_distance(ns_v2 a, ns_v2 b, float x, float z);

/*
 * Bouche tout ce qui n'est ni DANS l'un des contours, ni dans l'ÉPAISSEUR de son
 * mur. Sans elle, le remplissage sortirait par la porte d'entrée et mesurerait
 * le trottoir : l'aire deviendrait celle du cadre de la grille, c'est-à-dire un
 * grand chiffre parfaitement faux.
 *
 * L'ÉPAISSEUR compte, et ce n'est pas une tolérance de confort. Deux pièces
 * mitoyennes s'aboutent par leurs murs, et leurs lignes médianes ne coïncident
 * pas : dans cette salle elles sont à 6,4 cm l'une de l'autre. La bande entre
 * les deux n'est strictement dans aucun des deux polygones — bouchée, elle
 * referme la baie qui relie les pièces, et deux pièces mitoyennes ne peuvent
 * alors JAMAIS communiquer. C'est un faux positif réel, constaté sur le bloc
 * sanitaire, et corrigé ici.
 *
 * Une cellule prise dans l'épaisseur d'un mur n'est pas la rue : c'est le mur.
 * Si elle n'est pas déjà pleine, c'est qu'une baie y a été percée. La demi-
 * épaisseur, et pas un centimètre de plus : c'est le parement extérieur, et
 * au-delà commence le trottoir.
 */
void reach_grid_close_outside(reach_grid *g, const reach_contour *contours, size_t count);

/*
 * Érode par le rayon du corps, puis étiquette les composantes connexes.
 *
 * Voisinage à QUATRE, pas à huit : deux cellules en diagonale se touchent par un
 * coin, et un joueur ne passe pas par un coin. Après érosion, un passage
 * réellement franchissable fait plusieurs cellules de large et reste connexe à
 * quatre ; ce qui ne l'est pas est une fuite, c'est-à-dire précisément ce que ce
 * contrôle existe pour ne pas laisser passer.
 */
void reach_grid_solve(reach_grid *g, float body_radius);

/* −1 : hors grille, ou le corps n'y tient pas. */
int reach_grid_label_at(const reach_grid *g, float x, float z);

/* La composante libre la plus étendue qui touche le rectangle, et où. −1 si
 * aucune cellule libre n'y tombe. */
int reach_grid_label_in_box(const reach_grid *g, float min_x, float min_z,
                            float max_x, float max_z, float *hit_x, float *hit_z);

size_t reach_grid_cell_count(const reach_grid *g, int label);
float  reach_grid_area(const reach_grid *g, int label);
/* L'emprise d'une composante, pour dire OÙ elle est. Un barycentre seul
 * tomberait dans un trou dès que la composante est en L. */
void   reach_grid_extent(const reach_grid *g, int label,
                         float *min_x, float *min_z, float *max_x, float *max_z);

#endif /* NS_REACH_GRID_H */
