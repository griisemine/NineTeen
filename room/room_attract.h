/*
 * room_attract.h — les DIX-NEUF écrans qui jouent tout seuls.
 *
 * Le défaut qu'il corrige
 * -----------------------
 * « le jeu ne respire aucune âme n'y atmosphère ». Une salle d'arcade, la nuit,
 * c'est dix-neuf tubes qui bougent dans le noir. Celle-ci en avait dix-huit
 * FIGÉS : chaque borne non jouée affichait un JPEG de son écran-titre, toujours
 * la même image, sans une variation. On traversait une allée de posters
 * rétroéclairés.
 *
 * Ce n'est pas une question de goût. La description de la salle déclare, depuis
 * qu'elle existe, que « l'essentiel de la lumière vient des DIX-NEUF ÉCRANS DE
 * BORNES » — c'est le principe d'éclairage de toute la pièce. Une source de
 * lumière qui ne varie jamais ne peut pas donner l'ambiance qu'on lui demande.
 *
 * Ce que c'est
 * ------------
 * Un vrai attract mode : chaque borne fait tourner SA partie, jouée par le
 * pilote automatique du jeu — le même que `--autoplay`, celui qui sert déjà aux
 * captures et à l'intégration continue. Quand la partie meurt, elle repart avec
 * une autre graine. C'est exactement ce que faisaient les bornes d'époque, et
 * pour la même raison : montrer le jeu à quelqu'un qui passe.
 *
 * Ce que ça coûte, et pourquoi c'est tenable
 * ------------------------------------------
 * Deux dépenses distinctes, et il ne faut pas les confondre :
 *
 *   - **simuler** dix-neuf parties. Un état de jeu est une valeur pure de
 *     quelques centaines d'octets et un pas est une poignée d'opérations : les
 *     dix-neuf sont avancées à chaque image, et ça ne se mesure pas.
 *   - **dessiner** dix-neuf dalles. Là, chaque dalle est une passe de rendu, et
 *     dix-neuf passes par image ne sont pas gratuites. On en redessine donc
 *     `ROOM_ATTRACT_PER_FRAME` par image, à tour de rôle.
 *
 * Le tour de rôle ne se voit pas, et c'est mesurable : à 120 Hz et quatre
 * dalles par image, une borne donnée se rafraîchit toutes les cinq images, soit
 * 24 Hz. C'est le cadence d'un film, sur un écran de 62 cm vu à deux mètres, en
 * arrière-plan. La partie qu'on JOUE, elle, garde sa dalle à part et son
 * rafraîchissement à chaque image — c'est `screen_rt` dans `main.c`, et ce
 * module n'y touche pas.
 */
#ifndef ROOM_ATTRACT_H
#define ROOM_ATTRACT_H

#include "ns_render.h"
#include "ns_rhi.h"
#include "ns_scene.h"
#include "ns_sprite.h"

/*
 * Dalles redessinées par image. Quatre : au-delà, on paie des passes de rendu
 * pour un rafraîchissement que personne ne peut voir ; en deçà de deux, les
 * bornes du fond se mettent à saccader visiblement quand on les longe.
 */
#define ROOM_ATTRACT_PER_FRAME 4

typedef struct room_attract room_attract;

/*
 * Prépare une démo par borne qui déclare un jeu porté ET `attract`.
 *
 * Une borne dont le jeu n'est pas porté garde son image fixe : dix-neuf bornes
 * déclarent huit jeux, et « pas encore porté » est un état normal, pas une
 * erreur. Rend NULL si rien n'a pu être préparé — l'appelant continue alors
 * exactement comme avant.
 */
room_attract *room_attract_create(ns_rhi *rhi, const ns_scene *scene);
void room_attract_destroy(ns_rhi *rhi, room_attract *a);

/*
 * Avancer et dessiner sont SÉPARÉS, comme tout le reste du jeu.
 *
 * `tick` va dans la boucle à pas fixe — les démos avancent à 120 Hz, du même
 * pas que la partie du joueur, ce qui est la seule façon qu'elles aient la
 * bonne vitesse quel que soit le nombre d'images par seconde. `draw` va dans la
 * section de rendu, où sont les passes.
 *
 * Les mêler aurait donné des démos dont la vitesse dépend du matériel : à
 * 30 images par seconde elles auraient tourné quatre fois trop lentement, sur
 * une machine rapide trop vite. C'est exactement le défaut que le pas fixe de
 * ce jeu existe pour éviter.
 *
 * `skip_material` est la dalle occupée par la partie en cours, ou −1. On ne
 * peut pas se contenter de ne pas la redessiner : il faut aussi ne pas la
 * DÉCLARER, sans quoi la démo écraserait la partie du joueur à l'image
 * suivante. C'est le seul piège de ce module.
 */
void room_attract_tick(room_attract *a, float dt, int32_t skip_material);
void room_attract_draw(ns_rhi *rhi, ns_sprite *sprites, room_attract *a,
                       ns_renderer *rd, int32_t skip_material);

/* Nombre de démos réellement en vie. Pour le journal et pour les tests. */
int room_attract_count(const room_attract *a);

#endif /* ROOM_ATTRACT_H */
