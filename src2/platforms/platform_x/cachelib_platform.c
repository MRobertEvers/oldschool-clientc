#include "cachelib_platform.h"

#include "cachelib_client.h"
#include "cachelib_internal.h"
#include "src/osrs/rscache/cache.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "src/osrs/rscache/xtea_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int
cachelib_platform_init(
    struct CacheLib* cache,
    char const* directory)
{
    char xtea_path[256];

    int mode = cache->mode;

    if( mode == CACHE_MODE_DAT1 )
    {
        cache->u.cache_dat1 = cache_dat_new_from_directory(directory);
        if( !cache->u.cache_dat1 )
            return 0;
    }
    else if( mode == CACHE_MODE_DAT2 )
    {
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", directory);
        int xtea_keys_count = xtea_config_load_keys(xtea_path);
        if( xtea_keys_count == -1 )
        {
            fprintf(stderr, "Failed to load xtea keys from: %s\n", xtea_path);
            return 0;
        }

        cache->u.cache_dat2 = cache_new_from_directory(directory);
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
    struct CacheLib* cache,
    int archive_id,
    int flag)
{
    int map_id = archive_id;
    int map_x = CACHELIB_MAPCHUNK_MAPX(map_id);
    int map_z = CACHELIB_MAPCHUNK_MAPZ(map_id);

    int cache_map_id = cache_map_square_id(map_x, map_z);

    struct CacheMapSquare* map_square = NULL;
    struct CacheDat* cache_dat = cache->u.cache_dat1;

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
        return cache_dat_archive_new_load(
            cache_dat, CACHE_DAT_MAPS, map_square->terrain_archive_id);
    case CACHELIB_MAPCHUNK_SCENERY:
        return cache_dat_archive_new_load(cache_dat, CACHE_DAT_MAPS, map_square->loc_archive_id);
    default:
        assert(false && "Invalid flag");
        return NULL;
    }
}

static void*
cache_dat1_load_io(
    struct CacheLib* cache,
    struct CacheLib_IORequest* request)
{
    struct CacheDat* cache_dat1 = cache->u.cache_dat1;
    void* data = NULL;

    if( request->table_id == CACHE_DAT_MAPS )
    {
        data = cache_dat1_load_map_chunk(cache, request->archive_id, request->flags);
    }
    else
    {
        data = cache_dat_archive_new_load(cache_dat1, request->table_id, request->archive_id);
    }

    if( !data )
        return NULL;

    return data;
}

static void*
cache_dat2_load_io(
    struct CacheLib* cache,
    struct CacheLib_IORequest* request)
{
    struct Cache* cache_dat2 = cache->u.cache_dat2;
    void* data = NULL;

    uint32_t* xtea_key = NULL;
    if( request->table_id == CACHE_MAPS )
    {
        xtea_key = cache_archive_xtea_key(cache_dat2, request->table_id, request->archive_id);
    }
    data = cache_archive_new_load_decrypted(
        cache_dat2, request->table_id, request->archive_id, xtea_key);

    return data;
}

void*
cachelib_platform_load_io(
    struct CacheLib* cache,
    struct CacheLib_IORequest* request)
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