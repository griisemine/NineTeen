/*
 * ns_particles.h — la poussière en suspension.
 *
 * Pourquoi ça compte, et pourquoi c'est ici
 * -----------------------------------------
 * Le brouillard volumétrique rend la lumière visible dans l'air ; la poussière
 * lui donne du **grain**. Un cône sous un plafonnier sans particules est une
 * forme lisse, et une forme lisse se lit comme un effet. Avec quelques centaines
 * de grains qui dérivent dedans, il se lit comme de l'air.
 *
 * C'est aussi le second pipeline du moteur à employer le mélange alpha — le
 * premier étant la couche 2D — et le premier à le faire **avec test de
 * profondeur** : une poussière derrière une borne doit disparaître derrière
 * elle.
 *
 * Ce que ce système fait, et ce qu'il ne fait pas
 * ----------------------------------------------
 * Simulation sur le CPU, quads construits face caméra sur le CPU, un seul
 * tirage. À quelques milliers de particules c'est le bon compromis : un tampon
 * d'instances et une disposition d'attributs par instance coûteraient plus en
 * complexité qu'ils ne rapportent, et le budget d'une salle d'arcade n'est pas
 * celui d'une tempête de neige.
 *
 * Pas de collision, pas de tri, pas de fondu doux contre la géométrie. Le tri
 * ne manque pas parce que les grains sont additifs entre eux — leur ordre ne
 * change pas le résultat ; le fondu doux, lui, manque un peu quand une poussière
 * rase un caisson, et c'est dit plutôt que caché.
 */
#ifndef NS_PARTICLES_H
#define NS_PARTICLES_H

#include "ns_math.h"
#include "ns_rhi.h"
#include "ns_scene.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ns_particles ns_particles;

/*
 * Une zone d'émission. Les grains y naissent au hasard et y dérivent ; sortis de
 * la boîte, ils y reviennent par le côté opposé plutôt que de mourir. C'est ce
 * qui donne une densité stable sans avoir à gérer une durée de vie, et ça se
 * justifie physiquement : la poussière d'une pièce fermée ne s'en va pas.
 */
typedef struct ns_particle_zone {
    ns_aabb bounds;
    float   density;     /* grains par mètre cube */
    float   drift[3];    /* dérive moyenne, m/s — un courant d'air */
    float   size;        /* diamètre en mètres */
    float   color[3];
    float   brightness;
} ns_particle_zone;

#define NS_MAX_PARTICLE_ZONES 8

/*
 * `r` peut valoir NULL : on obtient alors la simulation SEULE, sans pipeline ni
 * tampon GPU, et `ns_particles_draw` ne fait rien. C'est ce qui permet de tester
 * le recyclage aux bornes de zone et le déterminisme là où il n'y a pas de carte
 * graphique — l'intégration continue de macOS et de Windows, notamment.
 */
ns_particles *ns_particles_create(ns_rhi *r, SDL_GPUTextureFormat target_format,
                                  SDL_GPUTextureFormat depth_format,
                                  uint32_t max_particles);
void          ns_particles_destroy(ns_rhi *r, ns_particles *p);

/* Remplace les zones et relance la population. Appelé au chargement de la salle. */
void ns_particles_set_zones(ns_particles *p, const ns_particle_zone *zones, uint32_t count,
                            uint64_t seed);

/* Facteur de densité global, 0 à 1 : c'est le levier du palier de qualité.
 * À 0 rien n'est simulé ni dessiné — pas seulement rien d'affiché. */
void ns_particles_set_density(ns_particles *p, float factor);

/*
 * Les lumières de la salle, pour éclairer les grains.
 *
 * C'est LE point qui décide si la poussière se lit comme de la poussière ou
 * comme de la neige : un grain n'est visible que s'il est DANS un faisceau. Sans
 * cet éclairage, tous les grains brillent pareil, y compris dans les coins
 * noirs, et l'œil y voit des flocons collés sur l'image.
 *
 * Le calcul est fait sur le CPU, au dessin, sur les grains qu'on garde. Trois
 * mille grains contre quarante lumières font cent vingt mille distances par
 * image — moins que ce que coûte une seule ligne de pixels du brouillard
 * volumétrique. Le faire dans le shader demanderait de lier le tampon des
 * lumières à l'étage sommet, pour la même chose.
 */
void ns_particles_set_lights(ns_particles *p, const ns_light_gpu *lights, uint32_t count);

/* Avance la simulation d'un pas fixe. */
void ns_particles_tick(ns_particles *p, float dt);

/*
 * Dessine dans `target`, en testant la profondeur contre `depth` SANS l'écrire :
 * une poussière ne doit pas masquer celle qui est derrière, et surtout elle ne
 * doit pas entrer dans la profondeur que le lancer de rayons relira.
 */
void ns_particles_draw(ns_rhi *r, ns_particles *p, const ns_m4 *view_proj,
                       ns_v3 camera_position, ns_v3 camera_right, ns_v3 camera_up,
                       SDL_GPUTexture *target, SDL_GPUTexture *depth,
                       uint32_t width, uint32_t height);

uint32_t ns_particles_live(const ns_particles *p);

/*
 * L'emprise réellement occupée par les grains vivants.
 *
 * Sert à deux choses, et la seconde justifie à elle seule la fonction : montrer
 * le nuage dans une vue de débogage, et VÉRIFIER le recyclage. Les grains
 * dérivent sans jamais mourir — sortis de leur boîte ils y rentrent par la face
 * opposée. Si ce recyclage se trompe d'axe ou de borne, le nuage s'échappe
 * lentement de sa zone : la population reste identique, rien n'est signalé, et
 * on ne s'en aperçoit qu'en jouant, quand la poussière a quitté les faisceaux.
 * Comparer cette emprise aux zones déclarées est le seul contrôle qui l'attrape.
 *
 * Renvoie false — et laisse `out` intact — s'il n'y a aucun grain.
 */
bool ns_particles_cloud_bounds(const ns_particles *p, ns_aabb *out);

#endif /* NS_PARTICLES_H */
