#include "maps_dat.h"

static int
dat_map_terrain_id(
    struct CacheDat* cache_dat,
    int map_x,
    int map_y)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_VERSION_LIST);
    if( !archive )
    {
        printf("Failed to load map terrain %d, %d (archive_id: %d)\n", map_x, map_y, archive_id);
        return NULL;
    }

    int name_hash = archive_name_hash("map_index");

    return archive->archive_id;
}

struct CacheDatArchive*
map_terrain_archive_new_load_from_cache_dat(
    struct CacheDat* cache_dat,
    int map_x,
    int map_y)
{}

struct CacheDatArchive*
map_loc_archive_new_load_from_cache_dat(
    struct CacheDat* cache_dat,
    int map_x,
    int map_y)
{}