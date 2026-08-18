/*
 * ns_sprite.h — la couche 2D.
 *
 * Elle manquait complètement : `engine/sprite/` était un répertoire vide, et
 * c'est ce qui bloquait à la fois les mini-jeux, l'invite d'interaction et le
 * moindre affichage de score. Le moteur savait dessiner une salle en PBR avec du
 * lancer de rayons, et pas un rectangle.
 *
 * Ce qu'elle est, et ce qu'elle n'est pas
 * ---------------------------------------
 * Un empileur de quads texturés, en **pixels logiques**, vers une **cible
 * choisie par l'appelant**. Rien d'autre. Pas de widgets, pas de disposition,
 * pas d'événements : ce sont des choses qu'on écrit quand on sait de quoi on a
 * besoin, et aujourd'hui on a besoin de dessiner Flappy Bird.
 *
 * La cible redirigeable est le point qui compte. Le **même code de jeu** dessine
 * la tuile de 256 x 320 d'un écran de borne et le plein écran d'une partie en
 * cours ; il n'y a pas deux chemins de rendu à garder d'accord, seulement une
 * taille logique différente passée à `ns_sprite_begin`.
 *
 * Le mélange alpha
 * ----------------
 * C'est le **premier pipeline du moteur** à l'activer. Tout le reste est opaque,
 * et la seule transparence existante était un `discard` sous 0,35 dans le
 * G-buffer. Le mélange est déclaré ici, dans le pipeline ; le shader ne fait que
 * produire une couleur.
 *
 * Le texte
 * --------
 * Une fonte bitmap 5 x 7 est **intégrée au binaire** et cuite en atlas au
 * démarrage. Ce n'est pas un pis-aller en attendant `stb_truetype` : une fonte
 * vectorielle rendue à 7 pixels de haut sur l'écran d'une borne produit une
 * bouillie grise, là où une fonte bitmap dessinée pour cette taille reste nette.
 * C'est le choix qu'on ferait de toute façon pour un jeu d'arcade.
 */
#ifndef NS_SPRITE_H
#define NS_SPRITE_H

#include "ns_rhi.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ns_sprite ns_sprite;

/*
 * `target_format` doit être celui de la texture dans laquelle on dessinera. Un
 * pipeline SDL est lié à son format de cible ; en changer demande un autre
 * pipeline, et le découvrir à l'exécution donne un écran noir sans message.
 */
ns_sprite *ns_sprite_create(ns_rhi *r, SDL_GPUTextureFormat target_format);
void       ns_sprite_destroy(ns_rhi *r, ns_sprite *s);

/* Ouvre un lot. `width` et `height` sont la taille LOGIQUE : les coordonnées
 * qu'on donnera ensuite sont dans ce repère, origine en haut à gauche. */
void ns_sprite_begin(ns_sprite *s, float width, float height);

/* Choisit la texture des quads suivants. NULL = un blanc uni, pour les aplats.
 * Un changement de texture coupe le lot ; grouper par texture est donc payant,
 * et c'est la seule optimisation que l'appelant ait à connaître. */
void ns_sprite_texture(ns_sprite *s, const ns_texture *tex);

/* Un quad. `rgba` multiplie la texture ; passer NULL vaut blanc opaque. */
void ns_sprite_quad(ns_sprite *s, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, const float rgba[4]);

/* Un aplat. Repose la texture blanche : à employer sans se soucier de l'état. */
void ns_sprite_rect(ns_sprite *s, float x, float y, float w, float h, const float rgba[4]);

/*
 * Du texte, en majuscules et chiffres — la fonte couvre l'ASCII imprimable.
 * `scale` est le facteur de la fonte 5 x 7 : à 2, un caractère fait 10 x 14.
 * Renvoie la largeur écrite, ce qui évite d'appeler `ns_sprite_text_width` juste
 * après pour centrer.
 */
float ns_sprite_text(ns_sprite *s, float x, float y, float scale,
                     const float rgba[4], const char *text);
float ns_sprite_text_width(const char *text, float scale);
float ns_sprite_text_height(float scale);

/*
 * Ferme le lot et le dessine dans `target`. `clear_rgba` non nul efface d'abord,
 * ce qui évite une passe de plus pour le cas courant du jeu qui occupe tout
 * l'écran.
 *
 * À appeler entre `ns_rhi_begin_frame` et `ns_rhi_end_frame`, hors de toute
 * autre passe de rendu.
 */
void ns_sprite_end(ns_rhi *r, ns_sprite *s, SDL_GPUTexture *target,
                   uint32_t target_width, uint32_t target_height,
                   const float clear_rgba[4]);

/* Nombre de quads et de lots du dernier `end` — pour le journal et le banc. */
uint32_t ns_sprite_quad_count(const ns_sprite *s);
uint32_t ns_sprite_batch_count(const ns_sprite *s);

#endif /* NS_SPRITE_H */
