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
        case PKTIN_LC245_2_IF_OPENCHAT:
        {
            /* Client.ts IF_OPENCHAT: resetInterfaceAnimation(comId); sidebar=-1; chat=comId;
             * viewport=-1; chatbackInputOpen=false */
            int com_id = item->packet._if_openchat.component_id;
            game->sidebar_interface_id = -1;
            game->chat_interface_id = com_id;
            game->viewport_interface_id = -1;
            game->chat_input_focused = 0;
            break;
        }
        case PKTIN_LC245_2_IF_CLOSE:
        {
            /* Client.ts IF_CLOSE: sidebar=-1; chat=-1; chatbackInputOpen=false; viewport=-1 */
            game->sidebar_interface_id = -1;
            game->chat_interface_id = -1;
            game->viewport_interface_id = -1;
            game->chat_input_focused = 0;
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
        case PKTIN_LC245_2_UPDATE_STAT:
        {
            int stat = item->packet._update_stat.stat;
            int xp = item->packet._update_stat.xp;
            int level = item->packet._update_stat.level;
            if( stat >= 0 && stat < PLAYER_STAT_COUNT )
            {
                game->player_stat_xp[stat] = xp;
                game->player_stat_effective_level[stat] = level;
                game->player_stat_base_level[stat] = player_stats_xp_to_level(xp);
            }
            break;
        }
        case PKTIN_LC245_2_UPDATE_RUNENERGY:
        {
            game->player_run_energy = item->packet._update_run_energy.run_energy;
            break;
        }
        case PKTIN_LC245_2_IF_SETCOLOUR:
            gameproto_exec_if_setcolour(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETHIDE:
            gameproto_exec_if_sethide(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETOBJECT:
            gameproto_exec_if_setobject(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETMODEL:
            gameproto_exec_if_setmodel(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETANIM:
            gameproto_exec_if_setanim(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETPLAYERHEAD:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_IF_SETPLAYERHEAD,
                .u.pkt_if_setplayerhead = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        case PKTIN_LC245_2_IF_SETTEXT:
            gameproto_exec_if_settext(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETNPCHEAD:
        {
            struct ScriptArgs args = {
                .tag = SCRIPT_PKT_IF_SETNPCHEAD,
                .u.pkt_if_setnpchead = { .item = item, .io = io },
            };
            script_queue_push(&game->script_queue, &args);
            break;
        }
        case PKTIN_LC245_2_IF_SETPOSITION:
            gameproto_exec_if_setposition(game, &item->packet);
            break;
        case PKTIN_LC245_2_IF_SETSCROLLPOS:
            gameproto_exec_if_setscrollpos(game, &item->packet);
            break;
        case PKTIN_LC245_2_MESSAGE_GAME:
        {
            const char* text = item->packet._message_game.text;
            if( text )
            {
                game_add_message(game, 0, text, "");
                free((void*)text);
            }
            break;
        }
        case PKTIN_LC245_2_MESSAGE_PRIVATE:
        {
            char sender_buf[GAME_CHAT_SENDER_LEN];
            base37tostr(
                (uint64_t)item->packet._message_private.from,
                sender_buf,
                sizeof(sender_buf));
            int staff_mod = item->packet._message_private.staff_mod;
            int type = (staff_mod >= 2) ? 7 : (staff_mod == 1 ? 7 : 3);
            if( item->packet._message_private.text )
            {
                game_add_message(
                    game,
                    type,
                    item->packet._message_private.text,
                    sender_buf);
                free(item->packet._message_private.text);
            }
            break;
        }
        case PKTIN_LC245_2_CHAT_FILTER_SETTINGS:
        {
            game->chat_public_mode = item->packet._chat_filter_settings.chat_public_mode;
            game->chat_private_mode = item->packet._chat_filter_settings.chat_private_mode;
            game->chat_trade_mode = item->packet._chat_filter_settings.chat_trade_mode;
            break;
        }
        default:
            break;
        }
    }
}
