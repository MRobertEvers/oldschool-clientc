/*
 * Packed zone key — byte-identical to LostCity ZoneMap.zoneIndex.
 *
 * Lifted out of mock230_zone.c so the pack validator (which loads multiway.csv
 * via mock230_content.c) can link the key packing without the full ZoneMap.
 */

#include "mock230_zone.h"

int
mock230_zone_index(
    int x,
    int z,
    int level)
{
    return ((x >> 3) & 0x7ff) | (((z >> 3) & 0x7ff) << 11) | ((level & 3) << 22);
}
