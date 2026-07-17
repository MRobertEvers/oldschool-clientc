#include "platform_x_io.h"

#include "asyncio.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PlatformX_IO
{
    struct RSCache_Dat2Disk* dat2_disk;
    struct RSCache_Dat1Disk* dat1_disk;
    char* config_dir;
    char* script_dir;
};

struct PlatformX_IO*
PlatformX_IO_New(void)
{
    struct PlatformX_IO* px = malloc(sizeof(struct PlatformX_IO));
    assert(px);
    memset(px, 0, sizeof(struct PlatformX_IO));
    return px;
}

void
PlatformX_IO_InitDat2Disk(
    struct PlatformX_IO* px,
    struct RSCache_Dat2Disk* disk)
{
    assert(px);
    assert(disk);
    px->dat2_disk = disk;
}

void
PlatformX_IO_InitDat1Disk(
    struct PlatformX_IO* px,
    struct RSCache_Dat1Disk* disk)
{
    assert(px);
    assert(disk);
    px->dat1_disk = disk;
}

void
PlatformX_IO_InitConfigPath(
    struct PlatformX_IO* px,
    const char* config_path)
{
    assert(px);
    assert(config_path);
    px->config_dir = strdup(config_path);
}

void
PlatformX_IO_InitScriptPath(
    struct PlatformX_IO* px,
    const char* script_path)
{
    assert(px);
    assert(script_path);
    px->script_dir = strdup(script_path);
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
dat2_cache_table_supported(int table_id)
{
    return table_id == RSCACHE_DAT2_DISK_TABLE_MODELS ||
           table_id == RSCACHE_DAT2_DISK_TABLE_INTERFACES ||
           table_id == RSCACHE_DAT2_DISK_TABLE_CLIENTSCRIPT ||
           table_id == RSCACHE_DAT2_DISK_TABLE_CONFIGS ||
           table_id == RSCACHE_DAT2_DISK_TABLE_MAPS ||
           table_id == RSCACHE_DAT2_DISK_TABLE_TEXTURES ||
           table_id == RSCACHE_DAT2_DISK_TABLE_SPRITES ||
           table_id == RSCACHE_DAT2_DISK_TABLE_FONTS;
}

static int
load_cache_item_dat2(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int table_id = item->u.cache.table_id;
    int archive_id = item->u.cache.archive_id;
    struct RSCache_Dat2DiskArchive* archive = NULL;

    assert(px->dat2_disk);

    if( !dat2_cache_table_supported(table_id) )
    {
        item->error_code = -1;
        return -1;
    }

    {
        uint32_t* xtea_key = NULL;
        /* Loc (lX_Z) map archives are XTEA-encrypted; terrain (mX_Z) keys are null. */
        if( table_id == RSCACHE_DAT2_DISK_TABLE_MAPS )
            xtea_key = RSCache_Dat2DiskArchiveXteaKey(px->dat2_disk, table_id, archive_id);
        archive = RSCache_Dat2DiskArchiveNewLoadDecrypted(
            px->dat2_disk, table_id, archive_id, xtea_key);
    }
    if( !archive )
    {
        item->error_code = -1;
        return -1;
    }

    RSCache_Dat2DiskArchiveInitMetadataFromTable(px->dat2_disk->tables[table_id], archive);

    item->data = archive;
    item->data_size = sizeof(struct RSCache_Dat2DiskArchive);
    item->error_code = 0;
    return 0;
}

static int
load_cache_item_dat1(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int table_id = item->u.cache.table_id;
    int archive_id = item->u.cache.archive_id;
    struct RSCache_Dat1DiskArchive* archive = NULL;

    assert(px->dat1_disk);

    if( table_id != RSCACHE_DAT1_DISK_TABLE_MODELS &&
        table_id != RSCACHE_DAT1_DISK_TABLE_CONFIGS )
    {
        item->error_code = -1;
        return -1;
    }

    archive = RSCache_Dat1DiskArchiveNewLoad(px->dat1_disk, table_id, archive_id);
    if( !archive )
    {
        item->error_code = -1;
        return -1;
    }

    item->data = archive;
    item->data_size = sizeof(struct RSCache_Dat1DiskArchive);
    item->error_code = 0;
    return 0;
}

static int
load_cache_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    if( item->u.cache.flags == TORIRS_IO_CACHE_DAT1 )
        return load_cache_item_dat1(px, item);
    return load_cache_item_dat2(px, item);
}

static int
load_reference_table_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int table_id = item->u.reference_table.table_id;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_ReferenceTable* table = NULL;

    assert(px->dat2_disk);

    archive = RSCache_Dat2DiskArchiveNewReferenceTableLoad(px->dat2_disk, table_id);
    if( !archive )
    {
        item->error_code = -1;
        return -1;
    }

    table = RSCache_ReferenceTableNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !table )
    {
        item->error_code = -1;
        return -1;
    }

    item->data = table;
    item->data_size = sizeof(struct RSCache_ReferenceTable);
    item->error_code = 0;
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
    case TORIRS_IOK_REFERENCE_TABLE:
        return load_reference_table_item(px, item);
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
    for( int i = 0; i < io->active_count; i++ )
    {
        struct ToriRS_IOItem* item = &io->io_slots[io->active[i]];

        if( PlatformX_IO_LoadItem(px, item) == 0 )
            processed++;
    }

    ToriRS_IO_ResetActive(io);

    return processed;
}
