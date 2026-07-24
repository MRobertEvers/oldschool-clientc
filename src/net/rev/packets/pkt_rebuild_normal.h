#ifndef SRC_NET_REV_PACKETS_PKT_REBUILD_NORMAL_H
#define SRC_NET_REV_PACKETS_PKT_REBUILD_NORMAL_H

/*
 * REBUILD_NORMAL packet parser (OSRS rev-230 form, RSProt RebuildNormalEncoder).
 * One of the versioned packet parsers under net/rev/packets/ (the RSProt "codec"
 * library): a revision module's parse dispatch calls the right version, e.g.
 * osrs230 -> pkt_rebuild_normal_read, and a classic rev keeps the lc layout in
 * gameproto_parse. Fills struct PktMapRebuild (zonex/zonez + XTEA region keys).
 */

#include <stdint.h>

struct RevPacket;

/* Decode the rev-230 REBUILD_NORMAL payload into out->_map_rebuild. Returns 1
 * (parsed) so the caller pushes it to the packet FIFO. */
int
pkt_rebuild_normal_read(uint8_t const* data, int len, struct RevPacket* out);

#endif
