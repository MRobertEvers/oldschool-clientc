#include "osrs/revs/lc245_2/serverprot_netrev245_2_parse.h"

#include "osrs/core/serverprot_core_parse.h"
#include "osrs/core/serverprot.h"
#include "osrs/core/revision.h"
#include "osrs/revs/lc245_2/revision_lc245_2.h"
#include "osrs/packetin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
serverprot_netrev245_2_parse(
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevServerProtPacket* packet)
{
    switch( packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
    {
        packet->packet_type = SERVERPROT_REBUILD_NORMAL_V1;
        printf("PKTIN_LC245_2_REBUILD_NORMAL\n");
        return serverprot_core_parse_maprebuild_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_NPC_INFO:
    {
        packet->packet_type = SERVERPROT_NPC_INFO_V1;
        printf("PKTIN_LC245_2_NPC_INFO\n");
        return serverprot_core_parse_npc_info_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_PLAYER_INFO:
    {
        packet->packet_type = SERVERPROT_PLAYER_INFO_V1;
        printf("PKTIN_LC245_2_PLAYER_INFO\n");
        return serverprot_core_parse_player_info_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_INV_FULL:
    {
        packet->packet_type = SERVERPROT_UPDATE_INV_FULL_V1;
        if( !serverprot_core_parse_update_inv_full_v1(data, data_size, packet) )
            return 0;
        {
            int nz = 0;
            for( int i = 0; i < packet->u.update_inv_full_v1.size; i++ )
            {
                if( packet->u.update_inv_full_v1.obj_ids[i] > 0 )
                    nz++;
            }
            printf(
                "PKTIN_LC245_2_UPDATE_INV_FULL component_id=%d size=%d non_zero_slots=%d",
                packet->u.update_inv_full_v1.component_id,
                packet->u.update_inv_full_v1.size,
                nz);
            int const max_show = 12;
            for( int i = 0; i < packet->u.update_inv_full_v1.size && i < max_show; i++ )
            {
                printf(
                    " [%d]wire=%d cnt=%d",
                    i,
                    packet->u.update_inv_full_v1.obj_ids[i],
                    packet->u.update_inv_full_v1.obj_counts[i]);
            }
            if( packet->u.update_inv_full_v1.size > max_show )
                printf(" ...(%d more slots)", packet->u.update_inv_full_v1.size - max_show);
            printf("\n");
        }
        return 1;
    }
    case PKTIN_LC245_2_IF_SETTAB:
    {
        packet->packet_type = SERVERPROT_IF_SETTAB_V1;
        if( !serverprot_core_parse_if_settab_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_IF_SETTAB: component_id=%d, tab_id=%d\n",
            packet->u.if_settab_v1.component_id, packet->u.if_settab_v1.tab_id);
        return 1;
    }
    case PKTIN_LC245_2_IF_OPENCHAT:
    {
        packet->packet_type = SERVERPROT_IF_OPENCHAT_V1;
        if( !serverprot_core_parse_if_open_chat_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_IF_OPENCHAT: component_id=%d\n", packet->u.if_openchat_v1.component_id);
        return 1;
    }
    case PKTIN_LC245_2_IF_CLOSE:
    {
        packet->packet_type = SERVERPROT_IF_CLOSE_V1;
        printf("PKTIN_LC245_2_IF_CLOSE\n");
        return serverprot_core_parse_if_close_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETTAB_ACTIVE:
    {
        packet->packet_type = SERVERPROT_IF_SETTAB_ACTIVE_V1;
        if( !serverprot_core_parse_if_settab_active_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_IF_SETTAB_ACTIVE: tab_id=%d\n", packet->u.if_settab_active_v1.tab_id);
        return 1;
    }
    case PKTIN_LC245_2_VARP_SMALL:
    {
        packet->packet_type = SERVERPROT_VARP_SMALL_V1;
        if( !serverprot_core_parse_varp_small_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_VARP_SMALL: variable=%d value=%d\n",
            packet->u.varp_small_v1.variable, packet->u.varp_small_v1.value);
        return 1;
    }
    case PKTIN_LC245_2_VARP_LARGE:
    {
        packet->packet_type = SERVERPROT_VARP_LARGE_V1;
        if( !serverprot_core_parse_varp_large_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_VARP_LARGE: variable=%d value=%d\n",
            packet->u.varp_large_v1.variable, packet->u.varp_large_v1.value);
        return 1;
    }
    case PKTIN_LC245_2_RESET_CLIENT_VARCACHE:
        packet->packet_type = SERVERPROT_VARP_SYNC_V1;
        return serverprot_core_parse_varp_sync_v1(data, data_size, packet);
    case PKTIN_LC245_2_UPDATE_STAT:
    {
        packet->packet_type = SERVERPROT_UPDATE_STAT_V1;
        return serverprot_core_parse_update_stat_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_RUNENERGY:
    {
        packet->packet_type = SERVERPROT_UPDATE_RUNENERGY_V1;
        return serverprot_core_parse_update_runenergy_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETCOLOUR:
    {
        packet->packet_type = SERVERPROT_IF_SETCOLOUR_V1;
        return serverprot_core_parse_if_setcolour_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETHIDE:
    {
        packet->packet_type = SERVERPROT_IF_SETHIDE_V1;
        return serverprot_core_parse_if_sethide_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETOBJECT:
    {
        packet->packet_type = SERVERPROT_IF_SETOBJECT_V1;
        return serverprot_core_parse_if_setobject_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETMODEL:
    {
        packet->packet_type = SERVERPROT_IF_SETMODEL_V1;
        return serverprot_core_parse_if_setmodel_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETANIM:
    {
        packet->packet_type = SERVERPROT_IF_SETANIM_V1;
        return serverprot_core_parse_if_setanim_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETPLAYERHEAD:
    {
        packet->packet_type = SERVERPROT_IF_SETPLAYERHEAD_V1;
        return serverprot_core_parse_if_setplayerhead_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETTEXT:
    {
        packet->packet_type = SERVERPROT_IF_SETTEXT_V1;
        return serverprot_core_parse_if_settext_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETNPCHEAD:
    {
        packet->packet_type = SERVERPROT_IF_SETNPCHEAD_V1;
        return serverprot_core_parse_if_setnpchead_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETPOSITION:
    {
        packet->packet_type = SERVERPROT_IF_SETPOSITION_V1;
        return serverprot_core_parse_if_setposition_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_SETSCROLLPOS:
    {
        packet->packet_type = SERVERPROT_IF_SETSCROLLPOS_V1;
        return serverprot_core_parse_if_setscrollpos_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_MESSAGE_GAME:
    {
        packet->packet_type = SERVERPROT_MESSAGE_GAME_V1;
        return serverprot_core_parse_message_game_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_MESSAGE_PRIVATE:
    {
        packet->packet_type = SERVERPROT_MESSAGE_PRIVATE_V1;
        return serverprot_core_parse_message_private_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_CHAT_FILTER_SETTINGS:
    {
        packet->packet_type = SERVERPROT_CHAT_FILTER_SETTINGS_V1;
        return serverprot_core_parse_chat_filter_settings_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        packet->packet_type = SERVERPROT_UPDATE_ZONE_PARTIAL_FOLLOWS_V1;
        printf("PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS\n");
        return serverprot_core_parse_update_zone_partial_follows_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS:
    {
        packet->packet_type = SERVERPROT_UPDATE_ZONE_FULL_FOLLOWS_V1;
        printf("PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS\n");
        return serverprot_core_parse_update_zone_full_follows_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_LOC_ADD_CHANGE:
    {
        packet->packet_type = SERVERPROT_LOC_ADD_CHANGE_V1;
        printf("PKTIN_LC245_2_LOC_ADD_CHANGE\n");
        return serverprot_core_parse_loc_add_change_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_LOC_DEL:
    {
        packet->packet_type = SERVERPROT_LOC_DEL_V1;
        printf("PKTIN_LC245_2_LOC_DEL\n");
        return serverprot_core_parse_loc_del_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_LOC_ANIM:
    {
        packet->packet_type = SERVERPROT_LOC_ANIM_V1;
        printf("PKTIN_LC245_2_LOC_ANIM\n");
        return serverprot_core_parse_loc_anim_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_OBJ_ADD:
    {
        packet->packet_type = SERVERPROT_OBJ_ADD_V1;
        if( !serverprot_core_parse_obj_add_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_OBJ_ADD: pos=%d, obj_id=%d, count=%d\n",
            packet->u.obj_add_v1.pos, packet->u.obj_add_v1.obj_id, packet->u.obj_add_v1.count);
        return 1;
    }
    case PKTIN_LC245_2_OBJ_DEL:
    {
        packet->packet_type = SERVERPROT_OBJ_DEL_V1;
        if( !serverprot_core_parse_obj_del_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_OBJ_DEL: pos=%d, obj_id=%d\n",
            packet->u.obj_del_v1.pos, packet->u.obj_del_v1.obj_id);
        return 1;
    }
    case PKTIN_LC245_2_OBJ_REVEAL:
    {
        packet->packet_type = SERVERPROT_OBJ_REVEAL_V1;
        if( !serverprot_core_parse_obj_reveal_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_OBJ_REVEAL: pos=%d, obj_id=%d, count=%d, receiver=%d\n",
            packet->u.obj_reveal_v1.pos,
            packet->u.obj_reveal_v1.obj_id,
            packet->u.obj_reveal_v1.count,
            packet->u.obj_reveal_v1.receiver);
        return 1;
    }
    case PKTIN_LC245_2_OBJ_COUNT:
    {
        packet->packet_type = SERVERPROT_OBJ_COUNT_V1;
        if( !serverprot_core_parse_obj_count_v1(data, data_size, packet) )
            return 0;
        printf("PKTIN_LC245_2_OBJ_COUNT: pos=%d, obj_id=%d, old_count=%d, new_count=%d\n",
            packet->u.obj_count_v1.pos,
            packet->u.obj_count_v1.obj_id,
            packet->u.obj_count_v1.old_count,
            packet->u.obj_count_v1.new_count);
        return 1;
    }
    case PKTIN_LC245_2_LOC_MERGE:
    {
        packet->packet_type = SERVERPROT_LOC_MERGE_V1;
        printf("PKTIN_LC245_2_LOC_MERGE\n");
        return serverprot_core_parse_loc_merge_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_MAP_PROJANIM:
    {
        packet->packet_type = SERVERPROT_MAP_PROJANIM_V1;
        printf("PKTIN_LC245_2_MAP_PROJANIM\n");
        return serverprot_core_parse_map_projanim_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_MAP_ANIM:
    {
        packet->packet_type = SERVERPROT_MAP_ANIM_V1;
        printf("PKTIN_LC245_2_MAP_ANIM\n");
        return serverprot_core_parse_map_anim_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_CAM_LOOKAT:
    {
        packet->packet_type = SERVERPROT_CAM_LOOKAT_V1;
        return serverprot_core_parse_cam_lookat_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_CAM_MOVETO:
    {
        packet->packet_type = SERVERPROT_CAM_MOVETO_V1;
        return serverprot_core_parse_cam_moveto_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_CAM_SHAKE:
    {
        packet->packet_type = SERVERPROT_CAM_SHAKE_V1;
        return serverprot_core_parse_cam_shake_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_CAM_RESET:
        packet->packet_type = SERVERPROT_CAM_RESET_V1;
        return serverprot_core_parse_cam_reset_v1(data, data_size, packet);
    case PKTIN_LC245_2_IF_OPENMAIN:
    {
        packet->packet_type = SERVERPROT_IF_OPENMAIN_V1;
        return serverprot_core_parse_if_open_main_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_OPENSIDE:
    {
        packet->packet_type = SERVERPROT_IF_OPENSIDE_V1;
        return serverprot_core_parse_if_open_side_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_IF_OPENMAIN_SIDE:
    {
        packet->packet_type = SERVERPROT_IF_OPENMAIN_SIDE_V1;
        return serverprot_core_parse_if_open_main_side_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_HINT_ARROW:
    {
        packet->packet_type = SERVERPROT_HINT_ARROW_V1;
        return serverprot_core_parse_hint_arrow_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_RESET_ANIMS:
        packet->packet_type = SERVERPROT_RESET_ANIMS_V1;
        return serverprot_core_parse_reset_anims_v1(data, data_size, packet);
    case PKTIN_LC245_2_UPDATE_PID:
    {
        packet->packet_type = SERVERPROT_UPDATE_PID_V1;
        return serverprot_core_parse_update_pid_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_RUNWEIGHT:
    {
        packet->packet_type = SERVERPROT_UPDATE_RUNWEIGHT_V1;
        return serverprot_core_parse_update_runweight_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UNSET_MAP_FLAG:
        packet->packet_type = SERVERPROT_UNSET_MAP_FLAG_V1;
        return serverprot_core_parse_unset_map_flag_v1(data, data_size, packet);
    case PKTIN_LC245_2_UPDATE_INV_STOP_TRANSMIT:
    {
        packet->packet_type = SERVERPROT_UPDATE_INV_STOP_TRANSMIT_V1;
        return serverprot_core_parse_update_inv_stop_transmit_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_INV_PARTIAL:
    {
        packet->packet_type = SERVERPROT_UPDATE_INV_PARTIAL_V1;
        return serverprot_core_parse_update_inv_partial_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_SET_MULTIWAY:
    {
        packet->packet_type = SERVERPROT_SET_MULTIWAY_V1;
        return serverprot_core_parse_set_multiway_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_SET_PLAYER_OP:
    {
        packet->packet_type = SERVERPROT_SET_PLAYER_OP_V1;
        return serverprot_core_parse_set_player_op_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_ENCLOSED:
    {
        /* Handled by serverprot_netrev245_2_parse_and_enqueue directly; should not reach here. */
        return 0;
    }
    case PKTIN_LC245_2_TUT_FLASH:
    {
        packet->packet_type = SERVERPROT_TUT_FLASH_V1;
        return serverprot_core_parse_tut_flash_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_TUT_OPEN:
    {
        packet->packet_type = SERVERPROT_TUT_OPEN_V1;
        return serverprot_core_parse_tut_open_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_LOGOUT:
        packet->packet_type = SERVERPROT_LOGOUT_V1;
        return serverprot_core_parse_logout_v1(data, data_size, packet);
    case PKTIN_LC245_2_P_COUNTDIALOG:
        packet->packet_type = SERVERPROT_P_COUNTDIALOG_V1;
        return serverprot_core_parse_p_countdialog_v1(data, data_size, packet);
    case PKTIN_LC245_2_FINISH_TRACKING:
        packet->packet_type = SERVERPROT_FINISH_TRACKING_V1;
        return serverprot_core_parse_finish_tracking_v1(data, data_size, packet);
    case PKTIN_LC245_2_ENABLE_TRACKING:
        packet->packet_type = SERVERPROT_ENABLE_TRACKING_V1;
        return serverprot_core_parse_enable_tracking_v1(data, data_size, packet);
    case PKTIN_LC245_2_LAST_LOGIN_INFO:
        packet->packet_type = SERVERPROT_LAST_LOGIN_INFO_V1;
        return serverprot_core_parse_last_login_info_v1(data, data_size, packet);
    case PKTIN_LC245_2_UPDATE_REBOOT_TIMER:
    {
        packet->packet_type = SERVERPROT_UPDATE_REBOOT_TIMER_V1;
        return serverprot_core_parse_update_reboot_timer_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_FRIENDLIST:
    {
        packet->packet_type = SERVERPROT_UPDATE_FRIENDLIST_V1;
        return serverprot_core_parse_update_friendlist_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_UPDATE_IGNORELIST:
    {
        packet->packet_type = SERVERPROT_UPDATE_IGNORELIST_V1;
        return serverprot_core_parse_update_ignorelist_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_SYNTH_SOUND:
    {
        packet->packet_type = SERVERPROT_SYNTH_SOUND_V1;
        return serverprot_core_parse_synth_sound_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_MIDI_SONG:
    {
        packet->packet_type = SERVERPROT_MIDI_SONG_V1;
        return serverprot_core_parse_midi_song_v1(data, data_size, packet);
    }
    case PKTIN_LC245_2_MIDI_JINGLE:
    {
        packet->packet_type = SERVERPROT_MIDI_JINGLE_V1;
        return serverprot_core_parse_midi_jingle_v1(data, data_size, packet);
    }
    default:
        printf("[serverprot_netrev245_2_parse] Unknown packet type: %d\n", packet_type);
        break;
    }

    return 0;
}

void
gameproto_free_lc245_2_item(struct RevServerProtPacket_Item* item)
{
    if( !item )
        return;

    struct RevServerProtPacket* p = &item->packet;
    switch( p->packet_type )
    {
    case SERVERPROT_NPC_INFO_V1:
        free(p->u.npc_info_v1.data);
        p->u.npc_info_v1.data = NULL;
        p->u.npc_info_v1.length = 0;
        break;
    case SERVERPROT_PLAYER_INFO_V1:
        free(p->u.player_info_v1.data);
        p->u.player_info_v1.data = NULL;
        p->u.player_info_v1.length = 0;
        break;
    case SERVERPROT_UPDATE_INV_FULL_V1:
        free(p->u.update_inv_full_v1.obj_ids);
        free(p->u.update_inv_full_v1.obj_counts);
        p->u.update_inv_full_v1.obj_ids = NULL;
        p->u.update_inv_full_v1.obj_counts = NULL;
        p->u.update_inv_full_v1.size = 0;
        break;
    case SERVERPROT_IF_SETTEXT_V1:
        free(p->u.if_settext_v1.text);
        p->u.if_settext_v1.text = NULL;
        break;
    case SERVERPROT_MESSAGE_GAME_V1:
        free(p->u.message_game_v1.text);
        p->u.message_game_v1.text = NULL;
        break;
    case SERVERPROT_SET_PLAYER_OP_V1:
        free(p->u.set_player_op_v1.op_text);
        p->u.set_player_op_v1.op_text = NULL;
        break;
    case SERVERPROT_MESSAGE_PRIVATE_V1:
        free(p->u.message_private_v1.text);
        p->u.message_private_v1.text = NULL;
        break;
    case SERVERPROT_UPDATE_INV_PARTIAL_V1:
        free(p->u.update_inv_partial_v1.entries);
        p->u.update_inv_partial_v1.entries = NULL;
        p->u.update_inv_partial_v1.count = 0;
        break;
    case SERVERPROT_UPDATE_IGNORELIST_V1:
        free(p->u.update_ignorelist_v1.usernames);
        p->u.update_ignorelist_v1.usernames = NULL;
        p->u.update_ignorelist_v1.count = 0;
        break;
    default:
        break;
    }
}

static void
push_packet_lc245_2(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    assert(game && game->revision.kind == REVISION_KIND_LC245_2 && game->revision.impl);
    gameproto_rev245_2_enqueue((struct RevisionLC245_2*)game->revision.impl, packet);
}

/* Return payload size (bytes AFTER the inner opcode byte) for zone sub-packet opcodes.
 * Inner zone sub-opcodes use the same byte values as the rev245_2 outer standalone packet
 * opcodes (PKTIN_LC245_2_*), not the rev254 / ServerProt.ts values. */
static int
zone_inner_payload_size(int zone_opcode)
{
    switch( zone_opcode )
    {
    case PKTIN_LC245_2_LOC_ADD_CHANGE: return 4;  /* pos(1)+info(1)+id(2) */
    case PKTIN_LC245_2_LOC_DEL:        return 2;  /* pos(1)+info(1) */
    case PKTIN_LC245_2_LOC_ANIM:       return 4;  /* pos(1)+info(1)+seq(2) */
    case PKTIN_LC245_2_LOC_MERGE:      return 14;
    case PKTIN_LC245_2_OBJ_ADD:        return 5;  /* pos(1)+id(2)+cnt(2) */
    case PKTIN_LC245_2_OBJ_DEL:        return 3;  /* pos(1)+id(2) */
    case PKTIN_LC245_2_OBJ_REVEAL:     return 7;
    case PKTIN_LC245_2_OBJ_COUNT:      return 7;
    case PKTIN_LC245_2_MAP_ANIM:       return 6;
    case PKTIN_LC245_2_MAP_PROJANIM:   return 15;
    default:  return -1;
    }
}

/* Parse one inner zone sub-packet and populate *out. Returns 1 on success, 0 on unknown opcode. */
static int
parse_zone_inner(
    int zone_opcode,
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    switch( zone_opcode )
    {
    case PKTIN_LC245_2_LOC_ADD_CHANGE:
        out->packet_type = SERVERPROT_LOC_ADD_CHANGE_V1;
        return serverprot_core_parse_loc_add_change_v1(data, n, out);
    case PKTIN_LC245_2_LOC_DEL:
        out->packet_type = SERVERPROT_LOC_DEL_V1;
        return serverprot_core_parse_loc_del_v1(data, n, out);
    case PKTIN_LC245_2_LOC_ANIM:
        out->packet_type = SERVERPROT_LOC_ANIM_V1;
        return serverprot_core_parse_loc_anim_v1(data, n, out);
    case PKTIN_LC245_2_LOC_MERGE:
        out->packet_type = SERVERPROT_LOC_MERGE_V1;
        return serverprot_core_parse_loc_merge_v1(data, n, out);
    case PKTIN_LC245_2_OBJ_ADD:
        out->packet_type = SERVERPROT_OBJ_ADD_V1;
        return serverprot_core_parse_obj_add_v1(data, n, out);
    case PKTIN_LC245_2_OBJ_DEL:
        out->packet_type = SERVERPROT_OBJ_DEL_V1;
        return serverprot_core_parse_obj_del_v1(data, n, out);
    case PKTIN_LC245_2_OBJ_REVEAL:
        out->packet_type = SERVERPROT_OBJ_REVEAL_V1;
        return serverprot_core_parse_obj_reveal_v1(data, n, out);
    case PKTIN_LC245_2_OBJ_COUNT:
        out->packet_type = SERVERPROT_OBJ_COUNT_V1;
        return serverprot_core_parse_obj_count_v1(data, n, out);
    case PKTIN_LC245_2_MAP_ANIM:
        out->packet_type = SERVERPROT_MAP_ANIM_V1;
        return serverprot_core_parse_map_anim_v1(data, n, out);
    case PKTIN_LC245_2_MAP_PROJANIM:
        out->packet_type = SERVERPROT_MAP_PROJANIM_V1;
        return serverprot_core_parse_map_projanim_v1(data, n, out);
    default:
        printf("[zone_enclosed] unknown inner opcode: %d\n", zone_opcode);
        return 0;
    }
}

/* Fan-out an UPDATE_ZONE_PARTIAL_ENCLOSED payload: enqueue the base header packet first
 * (so exec sets zone_base_x/z), then parse and enqueue each inner zone sub-packet. */
static int
serverprot_netrev245_2_parse_zone_enclosed(
    struct GGame* game,
    uint8_t* data,
    int n)
{
    if( n < 2 )
        return 0;

    /* Enqueue the header so the exec sets zone_base_* before inner packets execute. */
    struct RevServerProtPacket base_pkt;
    memset(&base_pkt, 0, sizeof(base_pkt));
    base_pkt.packet_type = SERVERPROT_UPDATE_ZONE_PARTIAL_ENCLOSED_V1;
    base_pkt.u.update_zone_partial_follows_v1.base_x = data[0];
    base_pkt.u.update_zone_partial_follows_v1.base_z = data[1];
    push_packet_lc245_2(game, &base_pkt);

    int pos = 2;
    while( pos < n )
    {
        int zone_opcode = (uint8_t)data[pos++];
        int payload_size = zone_inner_payload_size(zone_opcode);
        if( payload_size < 0 )
        {
            printf("[zone_enclosed] unknown opcode %d at pos %d, aborting inner loop\n",
                zone_opcode, pos - 1);
            break;
        }
        if( pos + payload_size > n )
        {
            printf("[zone_enclosed] truncated inner packet opcode=%d need=%d have=%d\n",
                zone_opcode, payload_size, n - pos);
            break;
        }

        struct RevServerProtPacket inner_pkt;
        memset(&inner_pkt, 0, sizeof(inner_pkt));
        if( parse_zone_inner(zone_opcode, data + pos, payload_size, &inner_pkt) )
        {
            if( zone_opcode == PKTIN_LC245_2_LOC_ADD_CHANGE
                || zone_opcode == PKTIN_LC245_2_LOC_DEL )
            {
                printf(
                    "[zone_loc] ENCLOSED queued inner opcode=%d (after header sets zone_base)\n",
                    zone_opcode);
            }
            push_packet_lc245_2(game, &inner_pkt);
        }

        pos += payload_size;
    }

    return 1;
}

int
serverprot_netrev245_2_parse_and_enqueue(
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n)
{
    /* UPDATE_ZONE_PARTIAL_ENCLOSED carries nested zone sub-packets; fan them out individually
     * so each inner packet goes through the normal exec path with the correct zone base. */
    if( opcode == PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_ENCLOSED )
        return serverprot_netrev245_2_parse_zone_enclosed(game, data, n);

    struct RevServerProtPacket packet;
    memset(&packet, 0, sizeof(struct RevServerProtPacket));

    if( !serverprot_netrev245_2_parse(opcode, data, n, &packet) )
        return 0;

    push_packet_lc245_2(game, &packet);
    return 1;
}
