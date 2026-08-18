#include "games.h"

#include "flappy/flappy.h"
#include "demineur/demineur.h"
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
    &g_demineur_api,
};

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
