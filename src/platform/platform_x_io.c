#include "platform_x_io.h"

#include "asyncio.h"

#include <assert.h>
#include <rscache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * LRU of decompressed dat2 archives. Group archives (configs, texture defs)
 * are requested once per contained id; without this every request re-runs
 * bzip2 on the whole group (interface 100 spent ~6s decompressing the obj
 * config group 219 times). Hits return a deep copy because callers own and
 * free the archive they receive.
 */
#define DAT2_ARCHIVE_CACHE_SLOTS 32

struct Dat2ArchiveCacheSlot
{
    struct RSCache_Dat2DiskArchive* archive;
    uint64_t last_used;
};

struct PlatformX_IO
{
    struct RSCache_Dat2Disk* dat2_disk;
    struct RSCache_Dat1Disk* dat1_disk;
    char* config_dir;
    char* script_dir;

    struct Dat2ArchiveCacheSlot archive_cache[DAT2_ARCHIVE_CACHE_SLOTS];
    uint64_t archive_cache_clock;
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

    for( int i = 0; i < DAT2_ARCHIVE_CACHE_SLOTS; i++ )
        RSCache_Dat2DiskArchiveFree(io->archive_cache[i].archive);
    free(io->config_dir);
    free(io->script_dir);
    free(io);
}

static struct RSCache_Dat2DiskArchive*
dat2_archive_clone(const struct RSCache_Dat2DiskArchive* src)
{
    struct RSCache_Dat2DiskArchive* dst = malloc(sizeof(*dst));
    if( !dst )
        return NULL;
    *dst = *src;
    dst->data = NULL;
    dst->file_ids = NULL;

    if( src->data && src->data_size > 0 )
    {
        dst->data = malloc((size_t)src->data_size);
        if( !dst->data )
            goto error;
        memcpy(dst->data, src->data, (size_t)src->data_size);
    }
    if( src->file_ids && src->file_count > 0 )
    {
        dst->file_ids = malloc((size_t)src->file_count * sizeof(int));
        if( !dst->file_ids )
            goto error;
        memcpy(dst->file_ids, src->file_ids, (size_t)src->file_count * sizeof(int));
    }
    return dst;

error:
    RSCache_Dat2DiskArchiveFree(dst);
    return NULL;
}

static struct RSCache_Dat2DiskArchive*
dat2_archive_cache_get(
    struct PlatformX_IO* px,
    int table_id,
    int archive_id)
{
    for( int i = 0; i < DAT2_ARCHIVE_CACHE_SLOTS; i++ )
    {
        struct Dat2ArchiveCacheSlot* slot = &px->archive_cache[i];
        if( slot->archive && slot->archive->table_id == table_id &&
            slot->archive->archive_id == archive_id )
        {
            slot->last_used = ++px->archive_cache_clock;
            return slot->archive;
        }
    }
    return NULL;
}

/* Takes ownership of `archive` (evicting the LRU slot's entry if needed). */
static void
dat2_archive_cache_put(
    struct PlatformX_IO* px,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct Dat2ArchiveCacheSlot* lru = &px->archive_cache[0];
    for( int i = 0; i < DAT2_ARCHIVE_CACHE_SLOTS; i++ )
    {
        struct Dat2ArchiveCacheSlot* slot = &px->archive_cache[i];
        if( !slot->archive )
        {
            lru = slot;
            break;
        }
        if( slot->last_used < lru->last_used )
            lru = slot;
    }
    RSCache_Dat2DiskArchiveFree(lru->archive);
    lru->archive = archive;
    lru->last_used = ++px->archive_cache_clock;
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
           table_id == RSCACHE_DAT2_DISK_TABLE_FONTS ||
           /* Animation tables: sequence frames (idx0), framemaps/skeletons (idx1),
            * skeletal AnimMaya (idx22). */
           table_id == RSCACHE_DAT2_DISK_TABLE_ANIMATIONS ||
           table_id == RSCACHE_DAT2_DISK_TABLE_SKELETONS ||
           table_id == RSCACHE_DAT2_DISK_TABLE_ANIMAYAS ||
           /* World map area configs ("details" / "compositemap"). */
           table_id == RSCACHE_DAT2_DISK_TABLE_WORLDMAP;
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
        static int trace_enabled = -1;
        if( trace_enabled < 0 )
            trace_enabled = getenv("TORIRS_IO_TRACE") != NULL;
        if( trace_enabled )
            fprintf(stderr, "io_trace: dat2 table=%d archive=%d\n", table_id, archive_id);
    }

    {
        struct RSCache_Dat2DiskArchive* cached =
            dat2_archive_cache_get(px, table_id, archive_id);
        if( cached )
        {
            archive = dat2_archive_clone(cached);
            if( !archive )
            {
                item->error_code = -1;
                return -1;
            }
            item->data = archive;
            item->data_size = sizeof(struct RSCache_Dat2DiskArchive);
            item->error_code = 0;
            return 0;
        }
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

    /* An idx record with no reference-table entry (hand-patched cache) is a
     * missing archive, not a fatal error. */
    if( !RSCache_Dat2DiskArchiveInitMetadataFromTable(px->dat2_disk->tables[table_id], archive) )
    {
        fprintf(
            stderr,
            "dat2 archive %d in table %d absent from reference table\n",
            archive_id,
            table_id);
        RSCache_Dat2DiskArchiveFree(archive);
        item->error_code = -1;
        return -1;
    }

    {
        struct RSCache_Dat2DiskArchive* master = dat2_archive_clone(archive);
        if( master )
            dat2_archive_cache_put(px, master);
    }

    item->data = archive;
    item->data_size = sizeof(struct RSCache_Dat2DiskArchive);
    item->error_code = 0;
    return 0;
}

/* Region -> map archive id, from the versionlist map_index the disk decoded at
 * open (LostCity OnDemand). Returns -1 for a square the cache does not ship —
 * legitimate at the edges of the built world, so callers report rather than
 * assert. */
static int
dat1_map_archive_id(
    struct PlatformX_IO* px,
    int map_square_id,
    int want_scenery)
{
    struct RSCache_MapSquares* squares = px->dat1_disk ? px->dat1_disk->map_squares : NULL;

    if( !squares )
        return -1;
    for( int i = 0; i < squares->squares_count; i++ )
    {
        if( squares->squares[i].map_id != map_square_id )
            continue;
        return want_scenery ? squares->squares[i].loc_archive_id
                            : squares->squares[i].terrain_archive_id;
    }
    return -1;
}

static int
load_cache_item_dat1(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int table_id = item->u.cache.table_id;
    int archive_id = item->u.cache.archive_id;
    int flags = item->u.cache.flags;
    struct RSCache_Dat1DiskArchive* archive = NULL;

    assert(px->dat1_disk);

    if( flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN || flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY )
    {
        archive_id = dat1_map_archive_id(
            px, archive_id, flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY);
        if( archive_id < 0 )
        {
            item->error_code = -1;
            return -1;
        }
    }
    else if(
        table_id != RSCACHE_DAT1_DISK_TABLE_MODELS &&
        table_id != RSCACHE_DAT1_DISK_TABLE_CONFIGS &&
        table_id != RSCACHE_DAT1_DISK_TABLE_ANIMATIONS )
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
    if( item->u.cache.flags == TORIRS_IO_CACHE_DAT1 ||
        item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN ||
        item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY )
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
