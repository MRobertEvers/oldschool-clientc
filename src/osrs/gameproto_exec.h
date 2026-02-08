#ifndef GAMEPROTO_EXEC_H
#define GAMEPROTO_EXEC_H

#include "game.h"
#include "packets/revpacket_lc245_2.h"

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

#endif