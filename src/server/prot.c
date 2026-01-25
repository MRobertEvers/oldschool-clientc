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
prot_connect_encode(
    struct ProtConnect* connect,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    p1(&buffer, PROT_KIND_CONNECT);
    p2(&buffer, connect->pid);

    return buffer.position;
}

int
prot_connect_decode(
    struct ProtConnect* connect,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    int kind = g1(&buffer);
    assert(kind == PROT_KIND_CONNECT);

    connect->pid = g2(&buffer);

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

    p1(&buffer, CLIENT_PROT_PLAYER);
    p2(&buffer, player->pid);
    p2(&buffer, player->x);
    p2(&buffer, player->z);
    for( int i = 0; i < 12; i++ )
    {
        p2(&buffer, player->slots[i]);
    }
    p2(&buffer, player->walkanim);
    p2(&buffer, player->runanim);
    p2(&buffer, player->walkanim_b);
    p2(&buffer, player->walkanim_r);
    p2(&buffer, player->walkanim_l);
    p2(&buffer, player->turnanim);
    p2(&buffer, player->readyanim);

    return buffer.position;
}

int
client_prot_player_decode(
    struct ClientProtPlayer* player,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    int kind = g1(&buffer);
    assert(kind == CLIENT_PROT_PLAYER);

    player->pid = g2(&buffer);
    player->x = g2(&buffer);
    player->z = g2(&buffer);
    for( int i = 0; i < 12; i++ )
    {
        player->slots[i] = g2(&buffer);
    }
    player->walkanim = g2(&buffer);
    player->runanim = g2(&buffer);
    player->walkanim_b = g2(&buffer);
    player->walkanim_r = g2(&buffer);
    player->walkanim_l = g2(&buffer);
    player->turnanim = g2(&buffer);
    player->readyanim = g2(&buffer);

    return buffer.position;
}

static const int g_packet_sizes[PROT_KIND_MAX] = {
    [PROT_KIND_TILE_CLICK] = 4,
    [PROT_KIND_CONNECT] = 2,
};

static const int g_client_packet_sizes[CLIENT_PROT_MAX] = {
    [CLIENT_PROT_PLAYER_MOVE] = 8,
    [CLIENT_PROT_PLAYER] = 44,
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