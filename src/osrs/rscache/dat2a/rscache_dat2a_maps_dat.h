#ifndef RSCACHE_RSCACHEDAT2A_MAPSDAT_H
#define RSCACHE_RSCACHEDAT2A_MAPSDAT_H

#include "../dat1disk/rscache_dat1disk.h"

struct RSCacheDat1Disk_Archive*
RSCacheDat2A_MapTerrainArchiveNewLoadFromCacheDat(
    struct RSCacheDat1Disk* cache_dat,
    int map_x,
    int map_y);

struct RSCacheDat1Disk_Archive*
RSCacheDat2A_MapLocArchiveNewLoadFromCacheDat(
    struct RSCacheDat1Disk* cache_dat,
    int map_x,
    int map_y);

#endif