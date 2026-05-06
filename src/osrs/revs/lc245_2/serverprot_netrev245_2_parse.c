#include "osrs/revs/lc245_2/serverprot_netrev245_2_parse.h"

#include "osrs/core/serverprot_core_parse.h"
#include "osrs/core/revision.h"
#include "osrs/revs/lc245_2/revision_lc245_2.h"
#include "osrs/packetin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osrs/rscache/bitbuffer.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/wordpack.h"

// clang-format off
#include "osrs/revs/lc254/gameproto_rev254_lc254.u.c"
// clang-format on

int
serverprot_netrev245_2_parse(
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevPacket_LC245_2* packet)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, (int8_t*)data, data_size);

    packet->packet_type = packet_type;

    switch( packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
    {
        printf("PKTIN_LC245_2_REBUILD_NORMAL\n");
        serverprot_core_parse_maprebuild_v1(
            data,
            data_size,
            &packet->_map_rebuild.zonex,
            &packet->_map_rebuild.zonez);
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

        {
            int nz = 0;
            for( int i = 0; i < packet->_update_inv_full.size; i++ )
            {
                if( packet->_update_inv_full.obj_ids[i] > 0 )
                    nz++;
            }
            printf(
                "PKTIN_LC245_2_UPDATE_INV_FULL component_id=%d size=%d non_zero_slots=%d",
                packet->_update_inv_full.component_id,
                packet->_update_inv_full.size,
                nz);
            int const max_show = 12;
            for( int i = 0; i < packet->_update_inv_full.size && i < max_show; i++ )
            {
                printf(
                    " [%d]wire=%d cnt=%d",
                    i,
                    packet->_update_inv_full.obj_ids[i],
                    packet->_update_inv_full.obj_counts[i]);
            }
            if( packet->_update_inv_full.size > max_show )
                printf(" ...(%d more slots)", packet->_update_inv_full.size - max_show);
            printf("\n");
        }

        return 1;
    }
    case PKTIN_LC245_2_IF_SETTAB:
    {
        struct WireIn_IfSettab_v1 w;
        serverprot_core_parse_if_settab_v1(data, data_size, &w);
        packet->_if_settab.component_id = w.component_id;
        packet->_if_settab.tab_id       = w.tab_id;
        printf("PKTIN_LC245_2_IF_SETTAB: component_id=%d, tab_id=%d\n",
            w.component_id, w.tab_id);
        return 1;
    }
    case PKTIN_LC245_2_IF_OPENCHAT:
    {
        struct WireIn_IfOpen_v1 w;
        serverprot_core_parse_if_open_v1(data, data_size, &w);
        packet->_if_openchat.component_id = w.component_id;
        printf("PKTIN_LC245_2_IF_OPENCHAT: component_id=%d\n", w.component_id);
        return 1;
    }
    case PKTIN_LC245_2_IF_CLOSE:
    {
        printf("PKTIN_LC245_2_IF_CLOSE\n");
        serverprot_core_parse_if_close_v1(data, data_size);
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
        struct WireIn_VarpSmall_v1 w;
        serverprot_core_parse_varp_small_v1(data, data_size, &w);
        packet->_varp_small.variable = w.variable;
        packet->_varp_small.value    = w.value;
        printf("PKTIN_LC245_2_VARP_SMALL: variable=%d value=%d\n", w.variable, w.value);
        return 1;
    }
    case PKTIN_LC245_2_VARP_LARGE:
    {
        struct WireIn_VarpLarge_v1 w;
        serverprot_core_parse_varp_large_v1(data, data_size, &w);
        packet->_varp_large.variable = w.variable;
        packet->_varp_large.value    = w.value;
        printf("PKTIN_LC245_2_VARP_LARGE: variable=%d value=%d\n", w.variable, w.value);
        return 1;
    }
    case PKTIN_LC245_2_RESET_CLIENT_VARCACHE:
        /* VARP_SYNC: no payload, sync all vars to server authoritative set */
        return 1;
    case PKTIN_LC245_2_UPDATE_STAT:
    {
        struct WireIn_UpdateStat_v1 w;
        serverprot_core_parse_update_stat_v1(data, data_size, &w);
        packet->_update_stat.stat  = w.stat;
        packet->_update_stat.xp    = w.xp;
        packet->_update_stat.level = w.level;
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_RUNENERGY:
    {
        struct WireIn_UpdateRunenergy_v1 w;
        serverprot_core_parse_update_runenergy_v1(data, data_size, &w);
        packet->_update_run_energy.run_energy = w.run_energy;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETCOLOUR:
    {
        struct WireIn_IfSetcolour_v1 w;
        serverprot_core_parse_if_setcolour_v1(data, data_size, &w);
        packet->_if_setcolour.component_id = w.component_id;
        packet->_if_setcolour.colour       = w.colour;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETHIDE:
    {
        struct WireIn_IfSethide_v1 w;
        serverprot_core_parse_if_sethide_v1(data, data_size, &w);
        packet->_if_sethide.component_id = w.component_id;
        packet->_if_sethide.hide         = w.hide;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETOBJECT:
    {
        struct WireIn_IfSetobject_v1 w;
        serverprot_core_parse_if_setobject_v1(data, data_size, &w);
        packet->_if_setobject.component_id = w.component_id;
        packet->_if_setobject.obj_id       = w.obj_id;
        packet->_if_setobject.zoom         = w.zoom;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETMODEL:
    {
        struct WireIn_IfSetmodel_v1 w;
        serverprot_core_parse_if_setmodel_v1(data, data_size, &w);
        packet->_if_setmodel.component_id = w.component_id;
        packet->_if_setmodel.model_id     = w.model_id;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETANIM:
    {
        struct WireIn_IfSetanim_v1 w;
        serverprot_core_parse_if_setanim_v1(data, data_size, &w);
        packet->_if_setanim.component_id = w.component_id;
        packet->_if_setanim.anim_id      = w.anim_id;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETPLAYERHEAD:
    {
        struct WireIn_IfSetplayerhead_v1 w;
        serverprot_core_parse_if_setplayerhead_v1(data, data_size, &w);
        packet->_if_setplayerhead.component_id = w.component_id;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETTEXT:
    {
        struct WireIn_IfSettext_v1 w;
        serverprot_core_parse_if_settext_v1(data, data_size, &w);
        packet->_if_settext.component_id = w.component_id;
        packet->_if_settext.text         = w.text;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETNPCHEAD:
    {
        struct WireIn_IfSetnpchead_v1 w;
        serverprot_core_parse_if_setnpchead_v1(data, data_size, &w);
        packet->_if_setnpchead.component_id = w.component_id;
        packet->_if_setnpchead.npc_id       = w.npc_id;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETPOSITION:
    {
        struct WireIn_IfSetposition_v1 w;
        serverprot_core_parse_if_setposition_v1(data, data_size, &w);
        packet->_if_setposition.component_id = w.component_id;
        packet->_if_setposition.x            = w.x;
        packet->_if_setposition.z            = w.z;
        return 1;
    }
    case PKTIN_LC245_2_IF_SETSCROLLPOS:
    {
        struct WireIn_IfSetscrollpos_v1 w;
        serverprot_core_parse_if_setscrollpos_v1(data, data_size, &w);
        packet->_if_setscrollpos.component_id = w.component_id;
        packet->_if_setscrollpos.pos          = w.pos;
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
    case PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        printf("PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS\n");
        struct WireIn_UpdateZone_v1 w;
        serverprot_core_parse_update_zone_v1(data, data_size, &w);
        packet->_update_zone_partial_follows.base_x = w.base_x;
        packet->_update_zone_partial_follows.base_z = w.base_z;
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS:
    {
        printf("PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS\n");
        struct WireIn_UpdateZone_v1 w;
        serverprot_core_parse_update_zone_v1(data, data_size, &w);
        packet->_update_zone_full_follows.base_x = w.base_x;
        packet->_update_zone_full_follows.base_z = w.base_z;
        return 1;
    }
    case PKTIN_LC245_2_LOC_ADD_CHANGE:
    {
        printf("PKTIN_LC245_2_LOC_ADD_CHANGE\n");
        struct WireIn_LocAddChange_v1 w;
        serverprot_core_parse_loc_add_change_v1(data, data_size, &w);
        packet->_loc_add_change.pos    = w.pos;
        packet->_loc_add_change.info   = w.info;
        packet->_loc_add_change.loc_id = w.loc_id;
        return 1;
    }
    case PKTIN_LC245_2_LOC_DEL:
    {
        printf("PKTIN_LC245_2_LOC_DEL\n");
        struct WireIn_LocDel_v1 w;
        serverprot_core_parse_loc_del_v1(data, data_size, &w);
        packet->_loc_del.pos  = w.pos;
        packet->_loc_del.info = w.info;
        return 1;
    }
    case PKTIN_LC245_2_LOC_ANIM:
    {
        printf("PKTIN_LC245_2_LOC_ANIM\n");
        struct WireIn_LocAnim_v1 w;
        serverprot_core_parse_loc_anim_v1(data, data_size, &w);
        packet->_loc_anim.pos    = w.pos;
        packet->_loc_anim.info   = w.info;
        packet->_loc_anim.seq_id = w.seq_id;
        return 1;
    }
    case PKTIN_LC245_2_OBJ_ADD:
    {
        struct WireIn_ObjAdd_v1 w;
        serverprot_core_parse_obj_add_v1(data, data_size, &w);
        packet->_obj_add.pos    = w.pos;
        packet->_obj_add.obj_id = w.obj_id;
        packet->_obj_add.count  = w.count;
        printf("PKTIN_LC245_2_OBJ_ADD: pos=%d, obj_id=%d, count=%d\n",
            w.pos, w.obj_id, w.count);
        return 1;
    }
    case PKTIN_LC245_2_OBJ_DEL:
    {
        struct WireIn_ObjDel_v1 w;
        serverprot_core_parse_obj_del_v1(data, data_size, &w);
        packet->_obj_del.pos    = w.pos;
        packet->_obj_del.obj_id = w.obj_id;
        printf("PKTIN_LC245_2_OBJ_DEL: pos=%d, obj_id=%d\n", w.pos, w.obj_id);
        return 1;
    }
    case PKTIN_LC245_2_OBJ_REVEAL:
    {
        struct WireIn_ObjReveal_v1 w;
        serverprot_core_parse_obj_reveal_v1(data, data_size, &w);
        packet->_obj_reveal.pos      = w.pos;
        packet->_obj_reveal.obj_id   = w.obj_id;
        packet->_obj_reveal.count    = w.count;
        packet->_obj_reveal.receiver = w.receiver;
        printf("PKTIN_LC245_2_OBJ_REVEAL: pos=%d, obj_id=%d, count=%d, receiver=%d\n",
            w.pos, w.obj_id, w.count, w.receiver);
        return 1;
    }
    case PKTIN_LC245_2_OBJ_COUNT:
    {
        struct WireIn_ObjCount_v1 w;
        serverprot_core_parse_obj_count_v1(data, data_size, &w);
        packet->_obj_count.pos       = w.pos;
        packet->_obj_count.obj_id    = w.obj_id;
        packet->_obj_count.old_count = w.old_count;
        packet->_obj_count.new_count = w.new_count;
        printf("PKTIN_LC245_2_OBJ_COUNT: pos=%d, obj_id=%d, old_count=%d, new_count=%d\n",
            w.pos, w.obj_id, w.old_count, w.new_count);
        return 1;
    }
    case PKTIN_LC245_2_LOC_MERGE:
    {
        printf("PKTIN_LC245_2_LOC_MERGE\n");
        struct WireIn_LocMerge_v1 w;
        serverprot_core_parse_loc_merge_v1(data, data_size, &w);
        packet->_loc_merge.pos    = w.pos;
        packet->_loc_merge.info   = w.info;
        packet->_loc_merge.loc_id = w.loc_id;
        packet->_loc_merge.start  = w.start;
        packet->_loc_merge.end    = w.end;
        packet->_loc_merge.pid    = w.pid;
        packet->_loc_merge.east   = w.east;
        packet->_loc_merge.south  = w.south;
        packet->_loc_merge.west   = w.west;
        packet->_loc_merge.north  = w.north;
        return 1;
    }
    case PKTIN_LC245_2_MAP_PROJANIM:
    {
        printf("PKTIN_LC245_2_MAP_PROJANIM\n");
        struct WireIn_MapProjAnim_v1 w;
        serverprot_core_parse_map_projanim_v1(data, data_size, &w);
        packet->_map_projanim.pos         = w.pos;
        packet->_map_projanim.dx_offset   = w.dx_offset;
        packet->_map_projanim.dz_offset   = w.dz_offset;
        packet->_map_projanim.target      = w.target;
        packet->_map_projanim.spotanim    = w.spotanim;
        packet->_map_projanim.src_height  = w.src_height;
        packet->_map_projanim.dst_height  = w.dst_height;
        packet->_map_projanim.start_delay = w.start_delay;
        packet->_map_projanim.end_delay   = w.end_delay;
        packet->_map_projanim.peak        = w.peak;
        packet->_map_projanim.arc         = w.arc;
        return 1;
    }
    case PKTIN_LC245_2_MAP_ANIM:
    {
        printf("PKTIN_LC245_2_MAP_ANIM\n");
        struct WireIn_MapAnim_v1 w;
        serverprot_core_parse_map_anim_v1(data, data_size, &w);
        packet->_map_anim.pos    = w.pos;
        packet->_map_anim.id     = w.id;
        packet->_map_anim.height = w.height;
        packet->_map_anim.delay  = w.delay;
        return 1;
    }
    case PKTIN_LC245_2_CAM_LOOKAT:
    {
        struct WireIn_CamLookAt_v1 w;
        serverprot_core_parse_cam_lookat_v1(data, data_size, &w);
        packet->_cam_lookat.local_x = w.local_x;
        packet->_cam_lookat.local_z = w.local_z;
        packet->_cam_lookat.height  = w.height;
        return 1;
    }
    case PKTIN_LC245_2_CAM_MOVETO:
    {
        struct WireIn_CamMoveTo_v1 w;
        serverprot_core_parse_cam_moveto_v1(data, data_size, &w);
        packet->_cam_moveto.local_x = w.local_x;
        packet->_cam_moveto.local_z = w.local_z;
        packet->_cam_moveto.height  = w.height;
        return 1;
    }
    case PKTIN_LC245_2_CAM_SHAKE:
    {
        struct WireIn_CamShake_v1 w;
        serverprot_core_parse_cam_shake_v1(data, data_size, &w);
        packet->_cam_shake.axis       = w.axis;
        packet->_cam_shake.amplitude  = w.amplitude;
        packet->_cam_shake.frequency  = w.frequency;
        packet->_cam_shake.speed      = w.speed;
        return 1;
    }
    case PKTIN_LC245_2_CAM_RESET:
        serverprot_core_parse_cam_reset_v1(data, data_size);
        return 1;
    case PKTIN_LC245_2_IF_OPENMAIN:
    {
        struct WireIn_IfOpen_v1 w;
        serverprot_core_parse_if_open_v1(data, data_size, &w);
        packet->_if_openmain.component_id = w.component_id;
        return 1;
    }
    case PKTIN_LC245_2_IF_OPENSIDE:
    {
        struct WireIn_IfOpen_v1 w;
        serverprot_core_parse_if_open_v1(data, data_size, &w);
        packet->_if_openside.component_id = w.component_id;
        return 1;
    }
    case PKTIN_LC245_2_IF_OPENMAIN_SIDE:
    {
        struct WireIn_IfOpenMainSide_v1 w;
        serverprot_core_parse_if_open_main_side_v1(data, data_size, &w);
        packet->_if_openmain_side.main_component_id = w.main_component_id;
        packet->_if_openmain_side.side_component_id = w.side_component_id;
        return 1;
    }
    case PKTIN_LC245_2_HINT_ARROW:
    {
        struct WireIn_HintArrow_v1 w;
        serverprot_core_parse_hint_arrow_v1(data, data_size, &w);
        packet->_hint_arrow.type   = w.type;
        packet->_hint_arrow.id     = w.id;
        packet->_hint_arrow.z      = w.z;
        packet->_hint_arrow.height = w.height;
        return 1;
    }
    case PKTIN_LC245_2_RESET_ANIMS:
        serverprot_core_parse_reset_anims_v1(data, data_size);
        return 1;
    case PKTIN_LC245_2_UPDATE_PID:
    {
        struct WireIn_UpdatePid_v1 w;
        serverprot_core_parse_update_pid_v1(data, data_size, &w);
        packet->_update_pid.local_player_index = w.local_player_index;
        packet->_update_pid.unused             = w.unused;
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_RUNWEIGHT:
    {
        struct WireIn_UpdateRunweight_v1 w;
        serverprot_core_parse_update_runweight_v1(data, data_size, &w);
        packet->_update_runweight.run_weight = w.run_weight;
        return 1;
    }
    case PKTIN_LC245_2_UNSET_MAP_FLAG:
        serverprot_core_parse_unset_map_flag_v1(data, data_size);
        return 1;
    case PKTIN_LC245_2_UPDATE_INV_STOP_TRANSMIT:
    {
        packet->_update_inv_stop_transmit.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_INV_PARTIAL:
    {
        packet->_update_inv_partial.component_id = g2(&buffer);
        /* count is derived from remaining bytes; allocate conservatively */
        int max_entries = (data_size - 2) / 5 + 1;
        packet->_update_inv_partial.entries =
            (struct PktUpdateInvPartialEntry*)malloc(max_entries * sizeof(struct PktUpdateInvPartialEntry));
        int n = 0;
        while( buffer.position < data_size )
        {
            int slot = g1(&buffer);
            int obj_id_raw = g2(&buffer);
            int count = g1(&buffer);
            if( count == 255 )
                count = g4(&buffer);
            packet->_update_inv_partial.entries[n].slot = slot;
            /* 0-based for obj_icon_get / inv_sync; wire 0 (empty) -> -1 */
            packet->_update_inv_partial.entries[n].obj_id = obj_id_raw - 1;
            packet->_update_inv_partial.entries[n].count = count;
            n++;
        }
        packet->_update_inv_partial.count = n;
        return 1;
    }
    case PKTIN_LC245_2_SET_MULTIWAY:
    {
        struct WireIn_SetMultiway_v1 w;
        serverprot_core_parse_set_multiway_v1(data, data_size, &w);
        packet->_set_multiway.multiway = w.multiway;
        return 1;
    }
    case PKTIN_LC245_2_SET_PLAYER_OP:
    {
        struct WireIn_SetPlayerOp_v1 w;
        serverprot_core_parse_set_player_op_v1(data, data_size, &w);
        packet->_set_player_op.op_index  = w.op_index;
        packet->_set_player_op.priority  = w.priority;
        packet->_set_player_op.op_text   = w.op_text;
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_ENCLOSED:
    {
        /* Contains sub-zone packets; store the zone base like partial_follows. */
        struct WireIn_UpdateZone_v1 w;
        serverprot_core_parse_update_zone_v1(data, data_size, &w);
        packet->_update_zone_partial_follows.base_x = w.base_x;
        packet->_update_zone_partial_follows.base_z = w.base_z;
        return 1;
    }
    case PKTIN_LC245_2_TUT_FLASH:
    {
        packet->_tut_flash.tab_id = g1(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_TUT_OPEN:
    {
        packet->_tut_open.component_id = g2(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_LOGOUT:
    case PKTIN_LC245_2_P_COUNTDIALOG:
    case PKTIN_LC245_2_FINISH_TRACKING:
    case PKTIN_LC245_2_ENABLE_TRACKING:
    case PKTIN_LC245_2_LAST_LOGIN_INFO:
    {
        /* Zero-payload or ignored-payload packets; nothing to decode. */
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_REBOOT_TIMER:
    {
        packet->_reboot_timer.ticks = g2(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_FRIENDLIST:
    {
        packet->_update_friendlist.username = g8(&buffer);
        packet->_update_friendlist.world    = g1(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_UPDATE_IGNORELIST:
    {
        int count = data_size / 8;
        if( count <= 0 )
        {
            packet->_update_ignorelist.usernames = NULL;
            packet->_update_ignorelist.count     = 0;
            return 1;
        }
        int64_t* names = (int64_t*)malloc((size_t)count * sizeof(int64_t));
        if( !names )
            return 0;
        for( int i = 0; i < count; i++ )
            names[i] = g8(&buffer);
        packet->_update_ignorelist.usernames = names;
        packet->_update_ignorelist.count     = count;
        return 1;
    }
    case PKTIN_LC245_2_SYNTH_SOUND:
    {
        packet->_synth_sound.id    = g2(&buffer);
        packet->_synth_sound.loops = g1(&buffer);
        packet->_synth_sound.delay = g2(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_MIDI_SONG:
    {
        packet->_midi_song.id = g2(&buffer);
        return 1;
    }
    case PKTIN_LC245_2_MIDI_JINGLE:
    {
        packet->_midi_jingle.delay = g2(&buffer);
        packet->_midi_jingle.id    = g2(&buffer);
        return 1;
    }
    default:
        printf("[serverprot_netrev245_2_parse] Unknown packet type: %d\n", packet_type);
        break;
    }

    return 0;
}

void
gameproto_free_lc245_2_item(struct RevPacket_LC245_2_Item* item)
{
    if( !item )
        return;

    struct RevPacket_LC245_2* p = &item->packet;
    switch( p->packet_type )
    {
    case PKTIN_LC245_2_NPC_INFO:
        free(p->_npc_info.data);
        p->_npc_info.data = NULL;
        p->_npc_info.length = 0;
        break;
    case PKTIN_LC245_2_PLAYER_INFO:
        free(p->_player_info.data);
        p->_player_info.data = NULL;
        p->_player_info.length = 0;
        break;
    case PKTIN_LC245_2_UPDATE_INV_FULL:
        free(p->_update_inv_full.obj_ids);
        free(p->_update_inv_full.obj_counts);
        p->_update_inv_full.obj_ids = NULL;
        p->_update_inv_full.obj_counts = NULL;
        p->_update_inv_full.size = 0;
        break;
    case PKTIN_LC245_2_IF_SETTEXT:
        free(p->_if_settext.text);
        p->_if_settext.text = NULL;
        break;
    case PKTIN_LC245_2_MESSAGE_GAME:
        free(p->_message_game.text);
        p->_message_game.text = NULL;
        break;
    case PKTIN_LC245_2_SET_PLAYER_OP:
        free(p->_set_player_op.op_text);
        p->_set_player_op.op_text = NULL;
        break;
    case PKTIN_LC245_2_MESSAGE_PRIVATE:
        free(p->_message_private.text);
        p->_message_private.text = NULL;
        break;
    case PKTIN_LC245_2_UPDATE_INV_PARTIAL:
        free(p->_update_inv_partial.entries);
        p->_update_inv_partial.entries = NULL;
        p->_update_inv_partial.count = 0;
        break;
    case PKTIN_LC245_2_UPDATE_IGNORELIST:
        free(p->_update_ignorelist.usernames);
        p->_update_ignorelist.usernames = NULL;
        p->_update_ignorelist.count = 0;
        break;
    default:
        break;
    }
}

static void
push_packet_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    assert(game && game->revision.kind == REVISION_KIND_LC245_2 && game->revision.impl);
    gameproto_rev245_2_enqueue((struct RevisionLC245_2*)game->revision.impl, packet);
}

int
serverprot_netrev245_2_parse_and_enqueue(
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n)
{
    struct RevPacket_LC245_2 packet;
    memset(&packet, 0, sizeof(struct RevPacket_LC245_2));

    if( !serverprot_netrev245_2_parse(opcode, data, n, &packet) )
        return 0;

    push_packet_lc245_2(game, &packet);
    return 1;
}