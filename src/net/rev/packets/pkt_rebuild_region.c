#include "pkt_rebuild_region.h"

#include "net/bitbuffer.h"
#include "net/rev/revpacket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * REBUILD_REGION wire:
 *   p2      worldArea
 *   p2Alt2  zoneX      = writeByte(v>>8); writeByte(v+128)
 *   p2      zoneZ
 *   -- bit mode --
 *   for level 0..3, zoneX 0..12, zoneZ 0..12:
 *       gBit(1)             has a source?
 *       gBit(26) if so      rotation<<1 | srcZoneZ<<3 | srcZoneX<<14 | srcLevel<<24
 *   -- byte mode, rounded up --
 *   p2      keyCount
 *   keyCount * 4 * p4  XTEA key ints
 *
 * The descriptor loop order and the 26-bit field layout are the client's own,
 * not this pairing's invention: they are what every OSRS-era client reads into
 * `instanceTemplateChunks`, and what 2009scape's `BuildDynamicScene` and
 * Kronos's `sendRegion` write. The one thing worth noticing is that srcZoneX
 * gets 10 bits where srcZoneZ gets 11, so a *source* zone must have x < 1024
 * (map square x < 128) while a destination is the loop position and has no such
 * limit. That asymmetry is why an instance can live at map x >= 100 and still
 * copy from anywhere real.
 *
 * The keys are read and discarded for the same reason REBUILD_NORMAL's are: this
 * client gets its XTEA keys from xteas.json beside the cache, so the wire copy
 * is only there to keep the two ends' idea of the payload length in agreement.
 */
int
pkt_rebuild_region_read(uint8_t const* data, int len, struct RevPacket* out)
{
    struct Net_BitBuffer bits;
    int32_t* zones;
    int zone_x;
    int zone_z;
    int key_count;
    int set_count = 0;
    int pos;

    memset(&out->_map_rebuild, 0, sizeof(out->_map_rebuild));

    /* worldArea, zoneX (p2Alt2: high byte then low+128), zoneZ. */
    if( len < 6 )
        return 0;
    zone_x = (data[2] << 8) | ((data[3] - 128) & 0xff);
    zone_z = (data[4] << 8) | data[5];

    zones = calloc(PKT_MAP_REBUILD_ZONES, sizeof(*zones));
    if( !zones )
        return 0;

    Net_BitBufferInit(&bits, data + 6, len - 6);
    for( int i = 0; i < PKT_MAP_REBUILD_ZONES; i++ )
    {
        if( !Net_BitBufferGbits(&bits, 1) )
            continue;
        zones[i] = (int32_t)Net_BitBufferGbits(&bits, 26);
        set_count++;
    }

    pos = 6 + Net_BitBufferBytePos(&bits);
    key_count = pos + 2 <= len ? (data[pos] << 8) | data[pos + 1] : 0;

    out->_map_rebuild.zonex = zone_x;
    out->_map_rebuild.zonez = zone_z;
    out->_map_rebuild.zones = zones;

    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "pkt_rebuild_region: zoneX=%d zoneZ=%d zones=%d keys=%d\n",
            zone_x,
            zone_z,
            set_count,
            key_count);

    return 1;
}
