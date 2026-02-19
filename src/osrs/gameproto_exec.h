#ifndef GAMEPROTO_EXEC_H
#define GAMEPROTO_EXEC_H

#include "game.h"
#include "packets/revpacket_lc245_2.h"
#include "world.h"

void
gameproto_exec_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_rebuild_normal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_update_inv_full(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_settab(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_settab_active(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setcolour(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_sethide(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setobject(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setmodel(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setanim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setplayerhead(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_settext(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setnpchead(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setposition(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setscrollpos(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_obj_add(
    struct GGame* game,
    struct RevPacket_LC245_2* packet,
    int zone_base_x,
    int zone_base_z);

void
gameproto_exec_obj_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_obj_reveal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_obj_count(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_loc_add_change(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_loc_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

/* World equivalents (struct World* instead of struct GGame*) */

void
gameproto_exec_lc245_2_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_npc_info_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_rebuild_normal_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_player_info_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_update_inv_full_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_settab_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_settab_active_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setcolour_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_sethide_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setobject_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setmodel_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setanim_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setplayerhead_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_settext_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setnpchead_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setposition_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_if_setscrollpos_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_obj_add_world(
    struct World* world,
    struct RevPacket_LC245_2* packet,
    int zone_base_x,
    int zone_base_z);

void
gameproto_exec_obj_del_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_obj_reveal_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_obj_count_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_loc_add_change_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_loc_del_world(
    struct World* world,
    struct RevPacket_LC245_2* packet);

#endif