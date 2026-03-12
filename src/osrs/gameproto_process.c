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
        default:
            break;
        }
    }
}
