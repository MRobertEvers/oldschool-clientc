#include "gameproto_process.h"

#include "jbase37.h"
#include "osrs/game.h"
#include "osrs/gameproto_exec.h"
#include "osrs/player_stats.h"
#include "osrs/script_queue.h"
#include "osrs/varp_varbit_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
            script_queue_push(&game->script_queue, &args);
            break;
        case PKTIN_LC245_2_PLAYER_INFO:
            args = (struct ScriptArgs){
                .tag = SCRIPT_PKT_PLAYER_INFO,
                .u.player_info = { .data = item->packet._player_info.data,
                                  .length = item->packet._player_info.length },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        default:
            break;
        }
    }
}
