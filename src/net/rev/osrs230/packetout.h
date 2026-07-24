#ifndef SRC_NET_REV_OSRS230_PACKETOUT_H
#define SRC_NET_REV_OSRS230_PACKETOUT_H

/*
 * OSRS rev 230 client->server opcodes (RSProt GameClientProt(Id).kt). The mock
 * server ignores client game packets, so only the handful the client emits
 * unprompted are mapped; the rest return -1 (net_out.c makes them no-ops). The
 * login trio (14/16) is built by the osrs230 login driver, not via this table.
 */

#include "net/rev/pktnames.h"

static inline int
packetout_code_osrs230(int pkt_out_name)
{
    switch( pkt_out_name )
    {
    case PKTOUT_NAME_NO_TIMEOUT:
        return 0; /* NO_TIMEOUT */
    case PKTOUT_NAME_MAP_BUILD_COMPLETE:
        return 54; /* MAP_BUILD_COMPLETE */
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
        return 55;
    case PKTOUT_NAME_MOVE_GAMECLICK:
        return 86;
    default:
        return -1;
    }
}

#endif
