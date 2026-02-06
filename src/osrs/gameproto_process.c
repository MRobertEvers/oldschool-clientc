#include "gameproto_process.h"

#include "osrs/script_queue.h"

void
gameproto_process(
    struct GGame* game,
    struct GIOQueue* io)
{
    if( game->packets_lc245_2 )
    {
        struct RevPacket_LC245_2_Item* item = game->packets_lc245_2;
        game->packets_lc245_2 = item->next_nullable;

        switch( item->packet.packet_type )
        {
        case PKTIN_LC245_2_REBUILD_NORMAL:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_REBUILD_NORMAL,
                .u.pkt_rebuild_normal = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        case PKTIN_LC245_2_PLAYER_INFO:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_PLAYER_INFO,
                .u.pkt_player_info = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        case PKTIN_LC245_2_NPC_INFO:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_NPC_INFO,
                .u.pkt_npc_info = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        default:
            break;
        }
    }
}
