#include "rscache_dat2a_maps_dat.h"

static int
dat_map_terrain_id(
    struct RSCacheDat1Disk* cache_dat,
    int map_x,
    int map_y)
{
    struct RSCacheDat1Disk_Archive* archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_VersionList);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    int name_hash = archive_name_hash("map_index");

    return archive->archive_id;
}

struct RSCacheDat1Disk_Archive*
RSCacheDat2A_MapTerrainArchiveNewLoadFromCacheDat(
    struct RSCacheDat1Disk* cache_dat,
    int map_x,
    int map_y)
{}

struct RSCacheDat1Disk_Archive*
RSCacheDat2A_MapLocArchiveNewLoadFromCacheDat(
    struct RSCacheDat1Disk* cache_dat,
    int map_x,
    int map_y)
{}