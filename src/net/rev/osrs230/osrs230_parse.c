#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_rebuild_normal.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include <stdint.h>

/*
 * OSRS rev-230 protocol dispatch (the rev-table `parse` slot). The revision
 * selects which versioned packet parser under net/rev/packets/ decodes each
 * canonical name — e.g. REBUILD_NORMAL -> pkt_rebuild_normal_read, and (future)
 * PLAYER_INFO -> pkt_player_info_v5_read, NPC_INFO -> pkt_npc_info_v5_read.
 * Returns 1 (parsed, push), 0 (parsed, skip), <0 (not mine -> shared parser).
 */
int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    (void)rev;
    switch( pkt_name )
    {
    case PKT_NAME_REBUILD_NORMAL:
        return pkt_rebuild_normal_read(data, len, out);

    /* IF_OPENTOP (op 60, 2 bytes): interfaceId as p2Alt1 (little-endian short,
     * bytes [lo, hi]). Opens a group as the gameframe root. */
    case PKT_NAME_IF_OPENTOP:
        if( len < 2 )
            return 0;
        out->_if_opentop.interface_id = data[0] | (data[1] << 8);
        return 1;

    /* IF_OPENSUB (op 6, 7 bytes): p1 type, p2Alt2 interfaceId (bytes
     * [hi, lo+128]), p4Alt3 destinationCombinedId (bytes [b2, b3, b0, b1]).
     * RSProt IfOpenSubEncoder. destinationCombinedId = destIface<<16 | destComp. */
    case PKT_NAME_IF_OPENSUB:
        if( len < 7 )
            return 0;
        out->_if_opensub.type = data[0];
        out->_if_opensub.interface_id = (data[1] << 8) | ((data[2] - 128) & 0xff);
        out->_if_opensub.target_uid =
            (data[4] << 24) | (data[3] << 16) | (data[6] << 8) | data[5];
        return 1;

    /* IF_CLOSESUB (op 36, 4 bytes): combinedId as p4 (big-endian packed int).
     * RSProt IfCloseSubEncoder. */
    case PKT_NAME_IF_CLOSESUB:
        if( len < 4 )
            return 0;
        out->_if_closesub.target_uid =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        return 1;

    default:
        return -1; /* fall back to the shared gameproto_parse */
    }
}
