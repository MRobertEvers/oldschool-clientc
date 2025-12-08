#include "libg.h"

#include "osrs/gio.h"

#include <stdlib.h>
#include <string.h>

struct GGame
{
    bool running;

    struct GIOQueue* queue;
};

struct GGame*
libg_game_new(void)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));
    game->queue = gioq_new();
    game->running = true;
    return game;
}

void
libg_game_free(struct GGame* game)
{
    gioq_free(game->queue);
    free(game);
}

void
libg_game_step(struct GGame* game, struct GIOQueue* queue, struct GInput* input)
{
    // IO
    struct GIOMessage message = { 0 };
    while( gioq_poll(game->queue, &message) )
    {
        switch( message.kind )
        {
        case GIO_REQ_ASSET:
            break;
        }
    }

    if( input->quit )
    {
        game->running = false;
        return;
    }
}

bool
libg_game_is_running(struct GGame* game)
{
    return game->running;
}