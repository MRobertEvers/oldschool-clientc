#ifndef MAPS_DAT_H
#define MAPS_DAT_H

#include "../cache_dat.h"

struct CacheDatArchive*
map_terrain_archive_new_load_from_cache_dat(
    struct CacheDat* cache_dat,
    int map_x,
    int map_y);

struct CacheDatArchive*
map_loc_archive_new_load_from_cache_dat(
    struct CacheDat* cache_dat,
    int map_x,
    int map_y);

#endif