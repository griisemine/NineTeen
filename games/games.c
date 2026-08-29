#include "games.h"

#include "flappy/flappy.h"
#include "demineur/demineur.h"
#include "asteroid/asteroid.h"
#include "pacman/pacman.h"
#include "piano/piano.h"
#include "shooter/shooter.h"
#include "tetris/tetris.h"
#include "snake/snake.h"

#include <SDL3/SDL.h>

/*
 * La table. UNE ligne par jeu porté.
 *
 * L'ordre est celui du classement, donc celui dans lequel on veut que les jeux
 * se présentent — pas l'ordre de portage.
 *
 * Les jeux NON portés n'apparaissent pas : une entrée pour un jeu absent
 * donnerait une borne qui promet une partie et n'en lance aucune, et un
 * classement surtout vide. `room/main.c` le dit franchement quand on approche
 * d'une borne dont le jeu n'est pas encore là.
 */
static const ns_game_api *const g_games[] = {
    &g_flappy_api,
    &g_snake_api,
    &g_demineur_api, &g_tetris_api, &g_asteroid_api, &g_pacman_api, &g_piano_api, &g_shooter_api,
};

uint64_t ns_game_state_hash(const ns_game_api *api, const void *state)
{
    if (!api || !state) return 0;
    const unsigned char *p = (const unsigned char *)state;
    uint64_t h = 1469598103934665603ull;          /* décalage de base FNV-1a 64 */
    for (size_t i = 0; i < api->state_size; ++i) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;                     /* le nombre premier FNV */
    }
    return h;
}

int ns_game_count(void) { return (int)(sizeof g_games / sizeof g_games[0]); }

const ns_game_api *ns_game_at(int index)
{
    if (index < 0 || index >= ns_game_count()) return NULL;
    return g_games[index];
}

const ns_game_api *ns_game_find(const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < ns_game_count(); ++i) {
        if (SDL_strcasecmp(g_games[i]->id, id) == 0) return g_games[i];
    }
    return NULL;
}
