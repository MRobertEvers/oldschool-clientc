/*
 * Packed zone key — byte-identical to LostCity ZoneMap.zoneIndex.
 *
 * Lifted out of torirs_server_zone.c so the pack validator (which loads multiway.csv
 * via torirs_server_content.c) can link the key packing without the full ZoneMap.
 */

#include "torirs_server_zone.h"

int
ToriRSServer_ZoneIndex(
    int x,
    int z,
    int level)
{
    return ((x >> 3) & 0x7ff) | (((z >> 3) & 0x7ff) << 11) | ((level & 3) << 22);
}
