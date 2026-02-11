#include "gameproto_parse.h"

#include "packetin.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "wordpack.h"

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
    rsbuf_init(&buffer, (int8_t*)data, data_size);

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
    case PKTIN_LC245_2_IF_OPENCHAT:
    {
        packet->_if_openchat.component_id = g2(&buffer);
        printf("PKTIN_LC245_2_IF_OPENCHAT: component_id=%d\n", packet->_if_openchat.component_id);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_CLOSE:
    {
        printf("PKTIN_LC245_2_IF_CLOSE\n");
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETTAB_ACTIVE:
    {
        packet->_if_settab_active.tab_id = g1(&buffer);
        printf("PKTIN_LC245_2_IF_SETTAB_ACTIVE: tab_id=%d\n", packet->_if_settab_active.tab_id);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_VARP_SMALL:
    {
        packet->_varp_small.variable = g2(&buffer);
        packet->_varp_small.value = g1b(&buffer);
        printf(
            "PKTIN_LC245_2_VARP_SMALL: variable=%d value=%d\n",
            packet->_varp_small.variable,
            packet->_varp_small.value);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_VARP_LARGE:
    {
        packet->_varp_large.variable = g2(&buffer);
        packet->_varp_large.value = g4(&buffer);
        printf(
            "PKTIN_LC245_2_VARP_LARGE: variable=%d value=%d\n",
            packet->_varp_large.variable,
            packet->_varp_large.value);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_RESET_CLIENT_VARCACHE:
        /* VARP_SYNC: no payload, sync all vars to server authoritative set */
        return 1;
    case PKTIN_LC245_2_UPDATE_STAT:
    {
        packet->_update_stat.stat = g1(&buffer);
        packet->_update_stat.xp = g4(&buffer);
        packet->_update_stat.level = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_RUNENERGY:
    {
        packet->_update_run_energy.run_energy = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETCOLOUR:
    {
        packet->_if_setcolour.component_id = g2(&buffer);
        packet->_if_setcolour.colour = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETHIDE:
    {
        packet->_if_sethide.component_id = g2(&buffer);
        packet->_if_sethide.hide = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETOBJECT:
    {
        packet->_if_setobject.component_id = g2(&buffer);
        packet->_if_setobject.obj_id = g2(&buffer);
        packet->_if_setobject.zoom = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETMODEL:
    {
        packet->_if_setmodel.component_id = g2(&buffer);
        packet->_if_setmodel.model_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETANIM:
    {
        packet->_if_setanim.component_id = g2(&buffer);
        packet->_if_setanim.anim_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETPLAYERHEAD:
    {
        packet->_if_setplayerhead.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETTEXT:
    {
        packet->_if_settext.component_id = g2(&buffer);
        packet->_if_settext.text = gstringnewline(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETNPCHEAD:
    {
        packet->_if_setnpchead.component_id = g2(&buffer);
        packet->_if_setnpchead.npc_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETPOSITION:
    {
        packet->_if_setposition.component_id = g2(&buffer);
        packet->_if_setposition.x = g2b(&buffer);
        packet->_if_setposition.z = g2b(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_IF_SETSCROLLPOS:
    {
        packet->_if_setscrollpos.component_id = g2(&buffer);
        packet->_if_setscrollpos.pos = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_MESSAGE_GAME:
    {
        packet->_message_game.text = gstringnewline(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_MESSAGE_PRIVATE:
    {
        if( data_size < 13 )
            return 0;
        packet->_message_private.from = g8(&buffer);
        packet->_message_private.message_id = g4(&buffer);
        packet->_message_private.staff_mod = g1(&buffer);
        packet->_message_private.text = wordpack_unpack(&buffer, data_size - 13);
        if( !packet->_message_private.text )
            return 0;
        return 1;
    }
    case PKTIN_LC245_2_CHAT_FILTER_SETTINGS:
    {
        packet->_chat_filter_settings.chat_public_mode = g1(&buffer);
        packet->_chat_filter_settings.chat_private_mode = g1(&buffer);
        packet->_chat_filter_settings.chat_trade_mode = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    default:
        printf("Unknown packet type: %d\n", packet_type);
        break;
    }

    return 0;
}