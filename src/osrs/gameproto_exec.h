#ifndef GAMEPROTO_EXEC_H
#define GAMEPROTO_EXEC_H

#include "game.h"
#include "packets/revpacket_lc245_2.h"
#include "query_engine.h"

void
gameproto_exec_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

#endif