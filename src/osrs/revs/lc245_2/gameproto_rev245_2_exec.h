#ifndef GAMEPROTO_EXEC_H
#define GAMEPROTO_EXEC_H

#include "osrs/game.h"
#include "osrs/revs/lc245_2/gameproto_rev245_2_packets.h"
#include "osrs/world.h"

void
gameproto_rev245_2_exec_dispatch(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_npc_info_raw(
    struct GGame* game,
    void* data,
    int length);

void
gameproto_rev245_2_exec_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_rebuild_normal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_player_info_raw(
    struct GGame* game,
    void* data,
    int length);

void
gameproto_rev245_2_exec_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_inv_full(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_settab(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_settab_active(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setcolour(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_sethide(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setobject(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setmodel(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setanim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setplayerhead(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_settext(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setnpchead(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setposition(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_setscrollpos(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_obj_add(
    struct GGame* game,
    struct RevPacket_LC245_2* packet,
    int zone_base_x,
    int zone_base_z);

void
gameproto_rev245_2_exec_obj_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_obj_reveal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_obj_count(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_loc_add_change(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_loc_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

/* --- New exec functions --- */

void
gameproto_rev245_2_exec_if_openchat(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_openmain(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_openside(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_openmain_side(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_if_close(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_inv_stop_transmit(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_inv_partial(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_cam_lookat(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_cam_moveto(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_cam_shake(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_cam_reset(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_unset_map_flag(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_runweight(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_runenergy(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_stat(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_hint_arrow(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_reset_anims(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_pid(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_varp_small(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_varp_large(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_varp_sync(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_zone_partial_follows(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_zone_full_follows(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_update_zone_partial_enclosed(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_loc_anim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_loc_merge(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_map_anim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_map_projanim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_set_multiway(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_rev245_2_exec_set_player_op(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

/* World-level rebuild helper (entity carryover + base tile update).
 * Used by gameproto_rev245_2_exec_rebuild_normal after Lua preload completes. */
void
gameproto_rev245_2_exec_rebuild_normal_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

#endif