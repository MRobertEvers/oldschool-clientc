#include "buildcachedat_loader.h"

#include "osrs/rscache/tables/maps.h"

void
buildcachedat_loader_cache_map_scenery(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data)
{
    int mapx = param_b >> 16;
    int mapz = param_b & 0xFFFF;
    struct CacheMapLocs* locs = map_locs_new_from_decode(data, data_size);
    locs->_chunk_mapx = mapx;
    locs->_chunk_mapz = mapz;
    buildcachedat_add_scenery(buildcachedat, mapx, mapz, locs);
}