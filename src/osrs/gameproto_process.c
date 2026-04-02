#include "gameproto_process.h"

#include "osrs/game.h"
#include "osrs/gameproto_exec.h"
#include "osrs/gameproto_parse.h"
#include "osrs/script_queue.h"

#include <stdlib.h>

void
gameproto_process(struct GGame* game)
{
    struct ScriptArgs args;
    if( game->packets_lc245_2 )
    {
        struct RevPacket_LC245_2_Item* item = game->packets_lc245_2;
        game->packets_lc245_2 = item->next_nullable;

        switch( item->packet.packet_type )
        {
        case PKTIN_LC245_2_REBUILD_NORMAL:
            args = (struct ScriptArgs){
                .tag = SCRIPT_PKT_REBUILD_NORMAL,
                .u.rebuild_normal = { .zonex = item->packet._map_rebuild.zonex,
                                     .zonez = item->packet._map_rebuild.zonez },
            };
            {
                struct ScriptQueueItem* qi = script_queue_push(&game->script_queue, &args);
                if( qi )
                    qi->lc245_2_packet_to_free = item;
            }
            break;
        case PKTIN_LC245_2_PLAYER_INFO:
            args = (struct ScriptArgs){
                .tag = SCRIPT_PKT_PLAYER_INFO,
                .u.player_info = { .data = item->packet._player_info.data,
                                  .length = item->packet._player_info.length },
            };
            {
                struct ScriptQueueItem* qi = script_queue_push(&game->script_queue, &args);
                if( qi )
                    qi->lc245_2_packet_to_free = item;
            }
            break;
        case PKTIN_LC245_2_NPC_INFO:
            args = (struct ScriptArgs){
                .tag = SCRIPT_PKT_NPC_INFO,
                .u.npc_info = { .data = item->packet._npc_info.data,
                               .length = item->packet._npc_info.length },
            };
            {
                struct ScriptQueueItem* qi = script_queue_push(&game->script_queue, &args);
                if( qi )
                    qi->lc245_2_packet_to_free = item;
            }
            break;
        case PKTIN_LC245_2_IF_SETTAB:
            args = (struct ScriptArgs){
                .tag = SCRIPT_PKT_IF_SETTAB,
                .u.lc245_packet = { .item = item },
            };
            {
                struct ScriptQueueItem* qi = script_queue_push(&game->script_queue, &args);
                if( qi )
                    qi->lc245_2_packet_to_free = item;
            }
            break;
        case PKTIN_LC245_2_UPDATE_INV_FULL:
            args = (struct ScriptArgs){
                .tag = SCRIPT_PKT_UPDATE_INV_FULL,
                .u.lc245_packet = { .item = item },
            };
            {
                struct ScriptQueueItem* qi = script_queue_push(&game->script_queue, &args);
                if( qi )
                    qi->lc245_2_packet_to_free = item;
            }
            break;
        default:
            gameproto_exec_lc245_2(game, &item->packet);
            gameproto_free_lc245_2_item(item);
            free(item);
            break;
        }
    }
}
