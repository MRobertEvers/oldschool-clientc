#ifndef SERVER_H
#define SERVER_H

struct Server
{
    int player_pos_tile_x;
    int player_pos_tile_z;

    int player_pathing_tile_x;
    int player_pathing_tile_z;

    uint64_t tick_ms;
    uint64_t next_tick_ms;
};

struct Server*
server_new(void);
void
server_free(struct Server* server);

void
server_step(
    struct Server* server,
    int timestamp_ms);

#endif