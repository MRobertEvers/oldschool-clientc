#ifndef GAMEPROTO_LC254_U_C
#define GAMEPROTO_LC254_U_C

// clang-format off
#include "gameproto_packets.u.c"
// clang-format on

static void
lc254_process(
    struct GGame* game,
    int packet_type,
    uint8_t* data,
    int data_size)
{
    switch( packet_type )
    {
    case PKTIN_LC254_REBUILD_NORMAL:
        gameproto_packet_maprebuild8_z16_x16(game, data, data_size);
        break;
    default:
        break;
    }
}

#endif