#ifndef REV_PACKET_LC245_2_QUERY_C
#define REV_PACKET_LC245_2_QUERY_C

#include "revpacket_lc245_2_query.h"

void
revpacket_lc245_2_packet_process(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    switch( packet->packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
        // revpacket_lc245_2_rebuild_normal(game, packet);
        break;
    default:
        break;
    }
}

#endif