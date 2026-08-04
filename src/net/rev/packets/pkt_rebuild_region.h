#ifndef SRC_NET_REV_PACKETS_PKT_REBUILD_REGION_H
#define SRC_NET_REV_PACKETS_PKT_REBUILD_REGION_H

/*
 * REBUILD_REGION packet parser (OSRS rev-230 form, wire opcode 59).
 *
 * The instanced sibling of pkt_rebuild_normal: same header, then a bit-packed
 * 4 x 13 x 13 grid of template-chunk descriptors saying where each destination
 * zone's terrain and scenery is copied from. Fills the same struct PktMapRebuild
 * and sets its `zones` pointer, which is what tells the world load to build an
 * instanced scene.
 */

#include <stdint.h>

struct RevPacket;

/* Decode the rev-230 REBUILD_REGION payload into out->_map_rebuild. Returns 1
 * (parsed) so the caller pushes it to the packet FIFO. */
int
pkt_rebuild_region_read(uint8_t const* data, int len, struct RevPacket* out);

#endif
