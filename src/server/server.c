

#include "server.h"

struct Server*
server_new(void)
{
    struct Server* server = malloc(sizeof(struct Server));
    memset(server, 0, sizeof(struct Server));

    server->next_tick_ms = 1000 / 60;
    server->timestamp_ms = 0;

    return server;
}

void
server_free(struct Server* server)
{
    free(server);
}

void
server_step(
    struct Server* server,
    int timestamp_ms)
{
    if( server->timestamp_ms < server->next_tick_ms )
    {
        return;
    }

    server->timestamp_ms = timestamp_ms;
    server->next_tick_ms += 1000 / 60;

    server->player_pos_tile_x++;
    server->player_pos_tile_z++;
}