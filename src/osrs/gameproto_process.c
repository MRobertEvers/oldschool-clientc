#include "gameproto_process.h"

#include "osrs/gameproto_exec.h"
#include "osrs/script_queue.h"
#include "osrs/varp_varbit_manager.h"

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
        case PKTIN_LC245_2_UPDATE_INV_FULL:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_UPDATE_INV_FULL,
                .u.pkt_update_inv_full = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        case PKTIN_LC245_2_IF_SETTAB:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_IF_SETTAB,
                .u.pkt_if_settab = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        case PKTIN_LC245_2_IF_SETTAB_ACTIVE:
        {
            // IF_SETTAB_ACTIVE doesn't need async loading, execute directly
            gameproto_exec_if_settab_active(game, &item->packet);
            break;
        }
        case PKTIN_LC245_2_VARP_SMALL:
        {
            varp_varbit_apply_small(
                &game->varp_varbit,
                item->packet._varp_small.variable,
                item->packet._varp_small.value);
            break;
        }
        case PKTIN_LC245_2_VARP_LARGE:
        {
            varp_varbit_apply_large(
                &game->varp_varbit,
                item->packet._varp_large.variable,
                item->packet._varp_large.value);
            break;
        }
        case PKTIN_LC245_2_RESET_CLIENT_VARCACHE:
        {
            varp_varbit_apply_sync(&game->varp_varbit);
            break;
        }
        default:
            break;
        }
    }
}
