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
            goto skip_append;
        }
        case PKTIN_LC245_2_PLAYER_INFO:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_PLAYER_INFO,
                .u.pkt_player_info = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            goto skip_append;
        }
        case PKTIN_LC245_2_NPC_INFO:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_NPC_INFO,
                .u.pkt_npc_info = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            goto skip_append;
        }
        default:
            break;
        }

        gametask_new_packet(game, io);

        item->next_nullable = NULL;

        if( !game->packets_lc245_2_inflight )
        {
            game->packets_lc245_2_inflight = item;
        }
        else
        {
            struct RevPacket_LC245_2_Item* list = game->packets_lc245_2_inflight;
            while( list->next_nullable )
                list = list->next_nullable;
            list->next_nullable = item;
        }
skip_append:;
    }
}
