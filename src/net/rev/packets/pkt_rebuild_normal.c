#include "pkt_rebuild_normal.h"

#include "net/rev/revpacket.h"

#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * REBUILD_NORMAL wire (RSProt RebuildNormalEncoder.encode):
 *   p2      worldArea
 *   p2Alt2  zoneX      = writeByte(v>>8); writeByte(v+128)
 *   p2      zoneZ
 *   p2      keyCount   (number of map squares)
 *   keyCount * 4 * p4  XTEA key ints
 * The map squares the keys correspond to are implicit (the client's scene grid
 * around the zone); region_ids is left NULL and resolved at world-load.
 */
int
pkt_rebuild_normal_read(uint8_t const* data, int len, struct RevPacket* out)
{
    struct RSCache_Buffer buf = { .data = (uint8_t*)data, .size = (uint32_t)len, .position = 0 };

    memset(&out->_map_rebuild, 0, sizeof(out->_map_rebuild));

    (void)g2(&buf); /* worldArea (unused) */

    /* zoneX via p2Alt2: high byte then (low + 128). */
    int high = g1(&buf);
    int low = (g1(&buf) - 128) & 0xff;
    int zone_x = (high << 8) | low;
    int zone_z = g2(&buf);
    int key_count = g2(&buf);

    out->_map_rebuild.zonex = zone_x;
    out->_map_rebuild.zonez = zone_z;
    out->_map_rebuild.region_count = key_count;

    if( key_count > 0 )
    {
        out->_map_rebuild.region_keys = malloc((size_t)key_count * 4 * sizeof(int32_t));
        if( out->_map_rebuild.region_keys )
        {
            for( int i = 0; i < key_count * 4; i++ )
                out->_map_rebuild.region_keys[i] = (int32_t)g4(&buf);
        }
    }

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "pkt_rebuild_normal: zoneX=%d zoneZ=%d keys=%d\n",
            zone_x,
            zone_z,
            key_count);

    return 1;
}
