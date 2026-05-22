#include "cachelib_platform.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "cachelib_internal.h"
#include "src/osrs/rscache/cache.h"
#include "src/osrs/rscache/cache_dat.h"
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
    snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", directory);

    int mode = cache->mode;

    if( mode == CACHE_MODE_DAT1 )
    {
        cache->u.cache_dat1 = cache_dat_new_from_directory(directory);
        if( !cache->u.cache_dat1 )
            return 0;
    }
    else if( mode == CACHE_MODE_DAT2 )
    {
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
cache_dat1_load_io(
    struct CacheLib* cache,
    struct CacheLib_IORequest* request)
{
    struct CacheDat* cache_dat1 = cache->u.cache_dat1;
    void* data = NULL;

    data = cache_dat_archive_new_load(cache_dat1, request->table_id, request->archive_id);

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