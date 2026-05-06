#ifndef GAMENET_REV245_2_EXEC_H
#define GAMENET_REV245_2_EXEC_H

#include "osrs/game.h"
#include "osrs/core/serverprot_packets.h"
#include "osrs/world.h"

void
gamenet_rev245_2_exec_dispatch_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_npc_info_raw_v1(
    struct GGame* game,
    void* data,
    int length);

void
gamenet_rev245_2_exec_npc_info_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_rebuild_normal_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_player_info_raw_v1(
    struct GGame* game,
    void* data,
    int length);

void
gamenet_rev245_2_exec_player_info_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_inv_full_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_settab_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_settab_active_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setcolour_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_sethide_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setobject_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setmodel_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setanim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setplayerhead_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_settext_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setnpchead_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setposition_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_setscrollpos_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_obj_add_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet,
    int zone_base_x,
    int zone_base_z);

void
gamenet_rev245_2_exec_obj_del_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_obj_reveal_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_obj_count_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_loc_add_change_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_loc_del_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

/* --- New exec functions --- */

void
gamenet_rev245_2_exec_if_openchat_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_openmain_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_openside_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_openmain_side_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_if_close_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_inv_stop_transmit_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_inv_partial_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_cam_lookat_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_cam_moveto_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_cam_shake_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_cam_reset_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_unset_map_flag_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_runweight_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_runenergy_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_stat_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_hint_arrow_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_reset_anims_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_pid_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_varp_small_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_varp_large_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_varp_sync_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_zone_partial_follows_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_zone_full_follows_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_update_zone_partial_enclosed_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_loc_anim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_loc_merge_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_map_anim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_map_projanim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_set_multiway_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

void
gamenet_rev245_2_exec_set_player_op_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet);

/* World-level rebuild helper (entity carryover + base tile update).
 * Used by gamenet_rev245_2_exec_rebuild_normal after Lua preload completes. */
void
gamenet_rev245_2_exec_rebuild_normal_world_v1(
    struct World* world,
    struct RevServerProtPacket* packet);

#endif