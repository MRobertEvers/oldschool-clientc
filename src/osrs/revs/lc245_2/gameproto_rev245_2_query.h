#ifndef OSRS_REVS_LC245_2_GAMEPROTO_REV245_2_QUERY_H
#define OSRS_REVS_LC245_2_GAMEPROTO_REV245_2_QUERY_H

#include "osrs/game.h"
#include "osrs/revs/lc245_2/gameproto_rev245_2_packets.h"

void
revpacket_lc245_2_packet_process(
    struct GGame* game,
    struct RevPacket_LC245_2* packet);

#endif /* OSRS_REVS_LC245_2_GAMEPROTO_REV245_2_QUERY_H */