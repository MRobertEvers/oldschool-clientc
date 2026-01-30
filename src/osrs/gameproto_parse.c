#include "gameproto_parse.h"

#include "packetin.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"

// clang-format off
#include "gameproto_lc254.u.c"
// clang-format on

int
gameproto_parse_lc245_2(
    struct GGame* game,
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevPacket_LC245_2* packet)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    packet->packet_type = packet_type;

    static int g_already = 0;

    switch( packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
    {
        printf("PKTIN_LC245_2_REBUILD_NORMAL\n");
        packet->_map_rebuild.zonex = g2(&buffer);
        packet->_map_rebuild.zonez = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_NPC_INFO:
    {
        printf("PKTIN_LC245_2_NPC_INFO\n");
        uint8_t* commandstream = malloc(data_size);
        memcpy(commandstream, data, data_size);
        packet->_npc_info.length = data_size;
        packet->_npc_info.data = commandstream;
        return 1;
    }
    case PKTIN_LC245_2_PLAYER_INFO:
    {
        printf("PKTIN_LC245_2_PLAYER_INFO\n");
        uint8_t* commandstream = malloc(data_size);
        memcpy(commandstream, data, data_size);
        packet->_player_info.length = data_size;
        packet->_player_info.data = commandstream;
        return 1;
    }
    default:
        printf("Unknown packet type: %d\n", packet_type);
        break;
    }

    return 0;
}