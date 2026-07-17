#ifndef TORIRS_MAP_FROM_RSCACHE_H
#define TORIRS_MAP_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_MapTerrain*
ToriRS_MapTerrainFromRSCache(
    int map_x,
    int map_z,
    const struct RSCache_MapTerrain* src);

struct ToriRS_MapLocs*
ToriRS_MapLocsFromRSCache(const struct RSCache_MapLocs* src);

#endif
