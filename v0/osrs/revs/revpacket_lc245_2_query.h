#ifndef REV_PACKET_LC245_2_QUERY_H
#define REV_PACKET_LC245_2_QUERY_H

#include "osrs/game.h"
#include "osrs/packets/revpacket_lc245_2.h"

void
revpacket_lc245_2_packet_process(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

#endif