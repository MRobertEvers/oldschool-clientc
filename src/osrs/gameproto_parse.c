#include "gameproto_parse.h"

#include "packetin.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"

// clang-format off
#include "gameproto_lc254.u.c"
// clang-format on

int
gameproto_parse_lc245_2(
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
    case PKTIN_LC245_2_UPDATE_INV_FULL:
    {
        printf("PKTIN_LC245_2_UPDATE_INV_FULL\n");
        packet->_update_inv_full.component_id = g2(&buffer);
        packet->_update_inv_full.size = g1(&buffer);

        // Allocate arrays for obj_ids and obj_counts
        packet->_update_inv_full.obj_ids = malloc(packet->_update_inv_full.size * sizeof(int));
        packet->_update_inv_full.obj_counts = malloc(packet->_update_inv_full.size * sizeof(int));

        for( int i = 0; i < packet->_update_inv_full.size; i++ )
        {
            packet->_update_inv_full.obj_ids[i] = g2(&buffer);

            int count = g1(&buffer);
            if( count == 255 )
            {
                count = g4(&buffer);
            }
            packet->_update_inv_full.obj_counts[i] = count;
        }

        return 1;
    }
    case PKTIN_LC245_2_IF_SETTAB:
    {
        packet->_if_settab.component_id = g2(&buffer);
        packet->_if_settab.tab_id = g1(&buffer);
        printf(
            "PKTIN_LC245_2_IF_SETTAB: component_id=%d, tab_id=%d\n",
            packet->_if_settab.component_id,
            packet->_if_settab.tab_id);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETTAB_ACTIVE:
    {
        packet->_if_settab_active.tab_id = g1(&buffer);
        printf(
            "PKTIN_LC245_2_IF_SETTAB_ACTIVE: tab_id=%d\n",
            packet->_if_settab_active.tab_id);
        assert(buffer.position == data_size);
        return 1;
    }
    default:
        printf("Unknown packet type: %d\n", packet_type);
        break;
    }

    return 0;
}