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
gameproto_exec_npc_info_raw(
    struct GGame* game,
    void* data,
    int length);

void
gameproto_exec_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_rebuild_normal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

void
gameproto_exec_player_info_raw(
    struct GGame* game,
    void* data,
    int length);

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

#endif