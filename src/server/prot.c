#include "prot.h"

#include "../osrs/rscache/rsbuf.h"

#include <assert.h>

int
prot_tile_click_encode(
    struct ProtTileClick* tile_click,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    p1(&buffer, PROT_KIND_TILE_CLICK);

    p2(&buffer, tile_click->x);
    p2(&buffer, tile_click->z);

    return buffer.position;
}

int
prot_tile_click_decode(
    struct ProtTileClick* tile_click,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    int kind = g1(&buffer);
    assert(kind == PROT_KIND_TILE_CLICK);

    tile_click->x = g2(&buffer);
    tile_click->z = g2(&buffer);

    return buffer.position;
}

int
client_prot_player_move_encode(
    struct ClientProtPlayerMove* player_move,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    p1(&buffer, CLIENT_PROT_PLAYER_MOVE);

    p2(&buffer, player_move->x);
    p2(&buffer, player_move->z);
    p2(&buffer, player_move->pathing_x);
    p2(&buffer, player_move->pathing_z);

    return buffer.position;
}

int
client_prot_player_move_decode(
    struct ClientProtPlayerMove* player_move,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    int kind = g1(&buffer);
    assert(kind == CLIENT_PROT_PLAYER_MOVE);

    player_move->x = g2(&buffer);
    player_move->z = g2(&buffer);
    player_move->pathing_x = g2(&buffer);
    player_move->pathing_z = g2(&buffer);

    return buffer.position;
}

int
client_prot_player_encode(
    struct ClientProtPlayer* player,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);
}

int
client_prot_player_decode(
    struct ClientProtPlayer* player,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);
}

static const int g_packet_sizes[PROT_KIND_MAX] = {
    [PROT_KIND_TILE_CLICK] = sizeof(struct ProtTileClick),
    [PROT_KIND_CONNECT] = sizeof(struct ProtConnect),
};

static const int g_client_packet_sizes[CLIENT_PROT_MAX] = {
    [CLIENT_PROT_PLAYER_MOVE] = sizeof(struct ClientProtPlayerMove),
    [CLIENT_PROT_PLAYER] = sizeof(struct ClientProtPlayer),
};

int
prot_get_packet_size(enum ProtKind kind)
{
    if( kind >= PROT_KIND_MAX )
    {
        return 0;
    }
    return g_packet_sizes[kind];
}

int
client_prot_get_packet_size(enum ClientProtKind kind)
{
    if( kind >= CLIENT_PROT_MAX )
    {
        return 0;
    }
    return g_client_packet_sizes[kind];
}