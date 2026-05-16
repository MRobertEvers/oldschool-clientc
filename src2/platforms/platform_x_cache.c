#include "platform_x_cache.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "src/osrs/rscache/cache.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/xtea_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct LibToriPlatformX_Cache
{
    struct LibToriRS_Instance* instance;

    int mode;
    union
    {
        struct Cache* cache_dat2;
        struct CacheDat* cache_dat1;
    } u;
};

struct LibToriPlatformX_Cache*
LibToriPlatformX_CacheNew(
    struct LibToriRS_Instance* instance,
    int mode,
    char const* directory)
{
    struct LibToriPlatformX_Cache* cache = malloc(sizeof(struct LibToriPlatformX_Cache));
    char xtea_path[256];
    snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", directory);
    if( !cache )
        return NULL;

    if( mode == CACHE_MODE_DAT1 )
    {
        cache->u.cache_dat1 = cache_dat_new_from_directory(directory);
        if( !cache->u.cache_dat1 )
            return NULL;
    }
    else if( mode == CACHE_MODE_DAT2 )
    {
        int xtea_keys_count = xtea_config_load_keys(xtea_path);
        if( xtea_keys_count == -1 )
        {
            fprintf(stderr, "Failed to load xtea keys from: %s\n", xtea_path);
            return NULL;
        }

        cache->u.cache_dat2 = cache_new_from_directory(directory);
        if( !cache->u.cache_dat2 )
            return NULL;
    }
    else
    {
        assert(false && "Invalid cache mode");
        return NULL;
    }

    return cache;
}

void
LibToriPlatformX_CacheFree(struct LibToriPlatformX_Cache* cache)
{
    if( !cache )
        return;
    free(cache);
}

int
LibToriPlatformX_GetMode(struct LibToriPlatformX_Cache* cache)
{
    if( !cache )
        return -1;
    return cache->mode;
}

static int
cache_dat1_load_io(
    struct LibToriPlatformX_Cache* cache,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !cache || !io_queue )
        return -1;
    struct CacheDat* cache_dat1 = cache->u.cache_dat1;

    for( int i = 0; i < io_queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* io_item = &io_queue->items[i];
        if( io_item->is_resolved )
            continue;
        io_item->is_resolved = true;
        io_item->data =
            cache_dat_archive_new_load(cache_dat1, io_item->table_id, io_item->archive_id);
        printf("Loading archive: %p\n", io_item->data);
    }

    return 0;
}

static int
cache_dat2_load_io(
    struct LibToriPlatformX_Cache* cache,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !cache || !io_queue )
        return -1;
    struct Cache* cache_dat2 = cache->u.cache_dat2;

    for( int i = 0; i < io_queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* io_item = &io_queue->items[i];

        if( io_item->is_resolved )
            continue;
        io_item->is_resolved = true;

        int32_t* xtea_key = NULL;
        if( io_item->table_id == CACHE_MAPS )
        {
            xtea_key = cache_archive_xtea_key(cache_dat2, io_item->table_id, io_item->archive_id);
        }
        io_item->data = cache_archive_new_load_decrypted(
            cache_dat2, io_item->table_id, io_item->archive_id, xtea_key);
    }

    return 0;
}

int
LibToriPlatformX_CacheLoadIO(
    struct LibToriPlatformX_Cache* cache,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !cache || !io_queue )
        return -1;

    switch( cache->mode )
    {
    case CACHE_MODE_DAT1:
        return cache_dat1_load_io(cache, io_queue);
    case CACHE_MODE_DAT2:
        return cache_dat2_load_io(cache, io_queue);
    default:
        return -1;
    }

    return 0;
}