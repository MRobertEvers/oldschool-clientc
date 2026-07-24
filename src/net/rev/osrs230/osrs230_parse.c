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
    default:
        return -1; /* fall back to the shared gameproto_parse */
    }
}
