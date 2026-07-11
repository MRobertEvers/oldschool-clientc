#include "platform_x_io_reactor.h"

#include "osrs/rscache/dat2disk/dat2disk.h"
#include "platform_x/cachelib.h"
#include "platform_x/cachelib_internal.h"
#include "platform_x/cachelib_platform.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_PATH "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/revconfig/configs"
#define SCRIPT_PATH "/Users/matthewevers/Documents/git_repos/3draster/src2/revs/scripts"

struct LibToriPlatformX_IOReactor
{
    struct RSCacheDat2DiskLib* cache;
};

struct LibToriPlatformX_IOReactor*
LibToriPlatformX_IOReactorNew(struct RSCacheDat2DiskLib* cache)
{
    struct LibToriPlatformX_IOReactor* reactor = malloc(sizeof(struct LibToriPlatformX_IOReactor));
    if( !reactor )
        return NULL;

    reactor->cache = cache;
    return reactor;
}

void
LibToriPlatformX_IOReactorFree(struct LibToriPlatformX_IOReactor* reactor)
{
    if( !reactor )
        return;
    free(reactor);
}

static int
read_whole_file(
    const char* path,
    void** out_data,
    int* out_size)
{
    FILE* fp = fopen(path, "rb");
    if( !fp )
        return -1;

    if( fseek(fp, 0, SEEK_END) != 0 )
    {
        fclose(fp);
        return -1;
    }

    long size = ftell(fp);
    if( size < 0 )
    {
        fclose(fp);
        return -1;
    }

    if( fseek(fp, 0, SEEK_SET) != 0 )
    {
        fclose(fp);
        return -1;
    }

    void* data = malloc((size_t)size);
    if( !data )
    {
        fclose(fp);
        return -1;
    }

    if( size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size )
    {
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_data = data;
    *out_size = (int)size;
    return 0;
}

static int
load_cache_item(
    struct LibToriPlatformX_IOReactor* reactor,
    struct LibToriRS_IOQueueItem* item)
{
    assert(reactor);
    if( !reactor->cache )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    struct CacheLib_IORequest request;
    request.table_id = item->u.cache.table_id;
    request.archive_id = item->u.cache.archive_id;
    request.flags = item->u.cache.flags;

    void* data = cachelib_platform_load_io(reactor->cache, &request);
    if( !data )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    item->data = data;
    item->error_code = 0;
    item->status = TORIRSIO_STAT_DONE;
    return 0;
}

static int
load_config_item(
    struct LibToriRS_IOQueueItem* item,
    const char* path)
{
    void* data = NULL;
    int data_size = 0;

    char resolved_path[LIBTORIRS_IOQUEUE_PATH_MAX];
    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", CONFIG_PATH, path);

    if( read_whole_file(resolved_path, &data, &data_size) != 0 )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    item->data = data;
    item->data_size = data_size;
    item->error_code = 0;
    item->status = TORIRSIO_STAT_DONE;
    return 0;
}

static int
load_script_item(
    struct LibToriRS_IOQueueItem* item,
    const char* path)
{
    void* data = NULL;
    int data_size = 0;

    char resolved_path[LIBTORIRS_IOQUEUE_PATH_MAX];
    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", SCRIPT_PATH, path);

    if( read_whole_file(resolved_path, &data, &data_size) != 0 )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    item->data = data;
    item->data_size = data_size;
    item->error_code = 0;
    item->status = TORIRSIO_STAT_DONE;
    return 0;
}

static int
load_reference_table_item(
    struct LibToriPlatformX_IOReactor* reactor,
    struct LibToriRS_IOQueueItem* item)
{
    struct RSCacheDat2Disk* cache_dat2;

    if( !reactor || !reactor->cache )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    if( reactor->cache->mode != CACHE_MODE_DAT2 )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    cache_dat2 = cachelib_dat2_disk(reactor->cache);
    if( !cache_dat2 )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    void* data =
        RSCacheDat2Disk_ArchiveNewReferenceTableLoad(cache_dat2, item->u.reference_table.table_id);
    if( !data )
    {
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }

    item->data = data;
    item->error_code = 0;
    item->status = TORIRSIO_STAT_DONE;
    return 0;
}

int
LibToriPlatformX_IOReactorLoadItem(
    struct LibToriPlatformX_IOReactor* reactor,
    struct LibToriRS_IOQueueItem* item)
{
    if( !item )
        return -1;

    item->data = NULL;
    item->data_size = 0;
    item->error_code = 0;
    item->status = TORIRSIO_STAT_YIELD;

    switch( item->kind )
    {
    case TORIRSIO_KIND_CACHE:
        return load_cache_item(reactor, item);
    case TORIRSIO_KIND_CONFIG_FILE:
        return load_config_item(item, item->u.config_file.path);
    case TORIRSIO_KIND_SCRIPT:
        return load_script_item(item, item->u.script.path);
    case TORIRSIO_KIND_REFERENCE_TABLE:
        return load_reference_table_item(reactor, item);
    default:
        item->error_code = -1;
        item->status = TORIRSIO_STAT_DONE;
        return -1;
    }
}

int
LibToriPlatformX_IOReactorProcess(
    struct LibToriPlatformX_IOReactor* reactor,
    struct LibToriRS_IOQueue* queue)
{
    if( !queue )
        return -1;

    int processed = 0;
    for( int i = queue->read_head; i < queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* item = &queue->items[i];
        if( item->status == TORIRSIO_STAT_DONE )
            continue;

        if( LibToriPlatformX_IOReactorLoadItem(reactor, item) == 0 )
            processed++;
    }

    return processed;
}
