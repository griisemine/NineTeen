/*
 * ns_clock.c — horloge et boucle à pas fixe.
 *
 * Le cœur du déterminisme. Motif classique de l'accumulateur :
 *
 *   ns_clock_begin_frame(&clk);
 *   while (ns_clock_consume_tick(&clk)) simulate(clk.tick_seconds);
 *   ns_clock_end_frame(&clk);
 *   render(clk.alpha);
 *
 * `simulate` reçoit toujours exactement le même dt. Le rendu, lui, interpole
 * avec alpha entre l'état précédent et l'état courant, donc reste fluide même
 * quand la fréquence d'affichage ne correspond pas à celle de la simulation.
 */
#include "ns_core.h"

#include <SDL3/SDL.h>

void ns_clock_init(ns_clock *c, double tick_hz)
{
    NS_ASSERT(c != NULL);
    NS_ASSERT(tick_hz > 0.0);

    SDL_zerop(c);
    c->tick_seconds = 1.0 / tick_hz;
    c->counter_freq = SDL_GetPerformanceFrequency();
    c->last_counter = SDL_GetPerformanceCounter();

    /* Garde-fou : si une image prend plus de 250 ms (chargement, fenêtre déplacée,
     * point d'arrêt dans un débogueur), on ne rattrape pas tout le retard d'un
     * coup — sinon chaque rattrapage étant lui-même lent, la boucle s'effondre.
     * On accepte de ralentir le temps du jeu plutôt que de figer l'application. */
    c->max_frame_seconds = 0.25;
    c->fps_smoothed = 0.0;
}

void ns_clock_begin_frame(ns_clock *c)
{
    NS_ASSERT(c != NULL);

    const uint64_t now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - c->last_counter) / (double)c->counter_freq;
    c->last_counter = now;

    if (elapsed < 0.0) elapsed = 0.0;                      /* horloge non monotone */
    c->frame_seconds = elapsed;

    if (elapsed > c->max_frame_seconds) elapsed = c->max_frame_seconds;
    c->accumulator += elapsed;

    /* Moyenne glissante : sert à l'affichage et au choix du niveau de qualité
     * du rendu (repli SSR quand le ray tracing coûte trop cher). */
    if (c->frame_seconds > 0.0) {
        const double inst = 1.0 / c->frame_seconds;
        c->fps_smoothed = (c->fps_smoothed <= 0.0) ? inst : c->fps_smoothed * 0.92 + inst * 0.08;
    }

    c->frame_index++;
}

bool ns_clock_consume_tick(ns_clock *c)
{
    NS_ASSERT(c != NULL);
    if (c->accumulator < c->tick_seconds) return false;
    c->accumulator -= c->tick_seconds;
    c->tick_index++;
    return true;
}

void ns_clock_end_frame(ns_clock *c)
{
    NS_ASSERT(c != NULL);
    c->alpha = c->accumulator / c->tick_seconds;
    if (c->alpha < 0.0) c->alpha = 0.0;
    if (c->alpha > 1.0) c->alpha = 1.0;
}

double ns_time_seconds(void)
{
    static uint64_t origin = 0;
    static uint64_t freq   = 0;
    if (freq == 0) {
        freq   = SDL_GetPerformanceFrequency();
        origin = SDL_GetPerformanceCounter();
    }
    return (double)(SDL_GetPerformanceCounter() - origin) / (double)freq;
}
