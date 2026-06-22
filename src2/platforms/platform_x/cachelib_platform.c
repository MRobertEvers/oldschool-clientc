#include "cachelib_platform.h"

#include "cachelib_client.h"
#include "cachelib_internal.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat1a/dat1a_version_list_mapsquare.h"
#include "osrs/rscache/shared/shared_xtea_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int
cachelib_platform_init(
    struct RSCacheDat2DiskLib* cache,
    char const* directory)
{
    char xtea_path[256];

    int mode = cache->mode;

    if( mode == CACHE_MODE_DAT1 )
    {
        cache->u.cache_dat1 = RSCacheDat1Disk_NewFromDirectory(directory);
        if( !cache->u.cache_dat1 )
            return 0;
    }
    else if( mode == CACHE_MODE_DAT2 )
    {
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", directory);
        int xtea_keys_count = RSCacheShared_XteaConfigLoadKeys(xtea_path);
        if( xtea_keys_count == -1 )
        {
            fprintf(stderr, "Failed to load xtea keys from: %s\n", xtea_path);
            return 0;
        }

        cache->u.cache_dat2 = RSCacheDat2Disk_NewFromDirectory(directory);
        if( !cache->u.cache_dat2 )
            return 0;
    }
    else
    {
        assert(false && "Invalid cache mode");
        return 0;
    }

    return 1;
}

static void*
cache_dat1_load_map_chunk(
    struct RSCacheDat2DiskLib* cache,
    int archive_id,
    int flag)
{
    int map_id = archive_id;
    int map_x = CACHELIB_MAPCHUNK_MAPX(map_id);
    int map_z = CACHELIB_MAPCHUNK_MAPZ(map_id);

    int cache_map_id = RSCacheDat1A_MapSquareId(map_x, map_z);

    struct RSCacheDat1A_MapSquare* map_square = NULL;
    struct RSCacheDat1Disk* cache_dat = cache->u.cache_dat1;

    for( int i = 0; i < cache_dat->map_squares->squares_count; i++ )
    {
        if( cache_dat->map_squares->squares[i].map_id == cache_map_id )
        {
            map_square = &cache_dat->map_squares->squares[i];
            break;
        }
    }

    if( !map_square )
    {
        printf("Failed to load map %d, %d\n", map_x, map_z);
        return NULL;
    }

    switch( flag )
    {
    case CACHELIB_MAPCHUNK_TERRAIN:
        return RSCacheDat1Disk_ArchiveNewLoad(
            cache_dat, RSCacheDat1Disk_Table_Maps, map_square->terrain_archive_id);
    case CACHELIB_MAPCHUNK_SCENERY:
        return RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Maps, map_square->loc_archive_id);
    default:
        assert(false && "Invalid flag");
        return NULL;
    }
}

static void*
cache_dat1_load_io(
    struct RSCacheDat2DiskLib* cache,
    struct RSCacheDat2DiskLib_IORequest* request)
{
    struct RSCacheDat1Disk* cache_dat1 = cache->u.cache_dat1;
    void* data = NULL;

    if( request->table_id == RSCacheDat1Disk_Table_Maps )
    {
        data = cache_dat1_load_map_chunk(cache, request->archive_id, request->flags);
    }
    else
    {
        data = RSCacheDat1Disk_ArchiveNewLoad(cache_dat1, request->table_id, request->archive_id);
    }

    if( !data )
        return NULL;

    return data;
}

static void*
cache_dat2_load_io(
    struct RSCacheDat2DiskLib* cache,
    struct RSCacheDat2DiskLib_IORequest* request)
{
    struct RSCacheDat2Disk* cache_dat2 = cache->u.cache_dat2;
    void* data = NULL;

    uint32_t* xtea_key = NULL;
    if( request->table_id == RSCacheDat2Disk_Table_Maps )
    {
        xtea_key = RSCacheDat2Disk_ArchiveXteaKey(cache_dat2, request->table_id, request->archive_id);
    }
    data = RSCacheDat2Disk_ArchiveNewLoadDecrypted(
        cache_dat2, request->table_id, request->archive_id, xtea_key);

    return data;
}

void*
cachelib_platform_load_io(
    struct RSCacheDat2DiskLib* cache,
    struct RSCacheDat2DiskLib_IORequest* request)
{
    switch( cache->mode )
    {
    case CACHE_MODE_DAT1:
        return cache_dat1_load_io(cache, request);
    case CACHE_MODE_DAT2:
        return cache_dat2_load_io(cache, request);
    default:
        return NULL;
    }
}