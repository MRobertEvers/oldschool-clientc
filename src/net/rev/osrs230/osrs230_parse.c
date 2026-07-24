#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"

#include <rsbuffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * osrs230 per-rev packet parse (the rev-table `parse` slot). Only REBUILD_NORMAL
 * needs a rev-specific layout (RSProt RebuildNormalEncoder): the 230 payload
 * differs from the lc254 rebuild, and it carries per-map-square XTEA keys the
 * dat2 map loader needs. Everything else returns <0 so net.c falls back to the
 * shared gameproto_parse (though osrs230 currently maps only REBUILD_NORMAL to a
 * canonical name, so nothing else reaches here).
 *
 * REBUILD_NORMAL wire (RebuildNormalEncoder.encode):
 *   p2      worldArea
 *   p2Alt2  zoneX      = writeByte(v>>8); writeByte(v+128)
 *   p2      zoneZ
 *   p2      keyCount   (number of map squares)
 *   keyCount * 4 * p4  XTEA key ints
 * The map squares the keys correspond to are implicit (the client's scene grid
 * around the zone); region_ids is left NULL here and resolved at world-load.
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
    if( pkt_name != PKT_NAME_REBUILD_NORMAL )
        return -1; /* not handled here -> shared parser */

    struct RSCache_Buffer b = { .data = (uint8_t*)data, .size = (uint32_t)len, .position = 0 };

    memset(&out->_map_rebuild, 0, sizeof(out->_map_rebuild));

    (void)g2(&b); /* worldArea (unused) */

    /* zoneX via p2Alt2: high byte then (low + 128). */
    int hi = g1(&b);
    int lo = (g1(&b) - 128) & 0xff;
    int zone_x = (hi << 8) | lo;
    int zone_z = g2(&b);
    int key_count = g2(&b);

    out->_map_rebuild.zonex = zone_x;
    out->_map_rebuild.zonez = zone_z;
    out->_map_rebuild.region_count = key_count;

    if( key_count > 0 )
    {
        out->_map_rebuild.region_keys = malloc((size_t)key_count * 4 * sizeof(int32_t));
        if( out->_map_rebuild.region_keys )
        {
            for( int i = 0; i < key_count * 4; i++ )
                out->_map_rebuild.region_keys[i] = (int32_t)g4(&b);
        }
    }

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "osrs230: REBUILD_NORMAL zoneX=%d zoneZ=%d keys=%d\n",
            zone_x,
            zone_z,
            key_count);

    return 1; /* parsed; push to FIFO */
}
