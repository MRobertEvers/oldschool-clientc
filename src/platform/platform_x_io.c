#include "platform_x_io.h"

#include "asyncio.h"
#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PlatformX_IO
{
    struct RSCache_Disk* disk;
    char* config_dir;
    char* script_dir;
};

struct PlatformX_IO*
PlatformX_IO_New(
    struct RSCache_Disk* disk,
    const char* config_dir,
    const char* script_dir)
{
    assert(disk);
    assert(config_dir);
    assert(script_dir);

    struct PlatformX_IO* px = malloc(sizeof(struct PlatformX_IO));
    if( !px )
        return NULL;

    px->disk = disk;
    px->config_dir = strdup(config_dir);
    px->script_dir = strdup(script_dir);
    if( !px->config_dir || !px->script_dir )
    {
        free(px->config_dir);
        free(px->script_dir);
        free(px);
        return NULL;
    }

    return px;
}

void
PlatformX_IO_Free(struct PlatformX_IO* io)
{
    if( !io )
        return;

    free(io->config_dir);
    free(io->script_dir);
    free(io);
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
load_file_item(
    struct ToriRS_IOItem* item,
    const char* base_dir,
    const char* path)
{
    void* data = NULL;
    int data_size = 0;

    char resolved_path[TORIRS_IOITEM_MAX_PATH];
    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", base_dir, path);

    if( read_whole_file(resolved_path, &data, &data_size) != 0 )
    {
        item->error_code = -1;
        return -1;
    }

    item->data = data;
    item->data_size = data_size;
    item->error_code = 0;
    return 0;
}

static int
cache_table_supported(int table_id)
{
    return table_id == RSCACHE_DISK_TABLE_MODELS || table_id == RSCACHE_DISK_TABLE_INTERFACES;
}

static int
load_cache_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    assert(px->disk);

    int table_id = item->u.cache.table_id;
    int archive_id = item->u.cache.archive_id;

    if( !cache_table_supported(table_id) )
    {
        item->error_code = -1;
        return -1;
    }

    struct RSCache_DiskArchive* archive =
        RSCache_DiskArchiveNewLoad(px->disk, table_id, archive_id);
    if( !archive )
    {
        item->error_code = -1;
        return -1;
    }

    void* data = malloc((size_t)archive->data_size);
    if( !data )
    {
        RSCache_DiskArchiveFree(archive);
        item->error_code = -1;
        return -1;
    }

    if( archive->data_size > 0 )
        memcpy(data, archive->data, (size_t)archive->data_size);

    item->data = data;
    item->data_size = archive->data_size;
    item->error_code = 0;

    RSCache_DiskArchiveFree(archive);
    return 0;
}

int
PlatformX_IO_LoadItem(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    assert(px);
    assert(item);

    item->data = NULL;
    item->data_size = 0;
    item->error_code = 0;

    switch( item->kind )
    {
    case TORIRS_IOK_CACHE:
        return load_cache_item(px, item);
    case TORIRS_IOK_CONFIG_FILE:
        return load_file_item(item, px->config_dir, item->u.config_file.path);
    case TORIRS_IOK_SCRIPT:
        return load_file_item(item, px->script_dir, item->u.script.path);
    default:
        item->error_code = -1;
        return -1;
    }
}

int
PlatformX_IO_Process(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io)
{
    assert(px);
    assert(io);

    int processed = 0;
    for( int i = 0; i < io->live_no; i++ )
    {
        struct ToriRS_IOItem* item = &io->io_slots[i];
        if( item->data != NULL || item->error_code != 0 )
            continue;

        if( PlatformX_IO_LoadItem(px, item) == 0 )
            processed++;
    }

    return processed;
}
