#include "platform_x_io.h"
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
#include "platform_x_io_js5.h"
#include "platform_x_io_js5_cache.h"
#include "platform_sdl2.h"
#endif

#include "asyncio.h"

#if defined(TORIRS_WEB_CACHE_IDB)
#include "platform/dat2_web_store.h"
#endif

#include <assert.h>
#include <rscache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/*
 * LRU of decompressed dat2 archives. Group archives (configs, texture defs)
 * are requested once per contained id; without this every request re-runs
 * bzip2 on the whole group (interface 100 spent ~6s decompressing the obj
 * config group 219 times). Hits return a deep copy because callers own and
 * free the archive they receive.
 */
#define DAT2_ARCHIVE_CACHE_SLOTS 32
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
#define JS5_PENDING_SLOTS (TORIRS_IO_MAX_ITEMS * 2)
#endif

struct Dat2ArchiveCacheSlot
{
    struct RSCache_Dat2DiskArchive* archive;
    uint64_t last_used;
};

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
struct Js5PendingItem
{
    int in_use;
    struct ToriRS_IO* io;
    int slot;
    int archive;
    int group;
};
#endif

struct PlatformX_IO
{
    struct RSCache_Dat2Disk* dat2_disk;
    struct RSCache_Dat1Disk* dat1_disk;
    char* config_dir;
    char* script_dir;

    struct Dat2ArchiveCacheSlot archive_cache[DAT2_ARCHIVE_CACHE_SLOTS];
    uint64_t archive_cache_clock;

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    struct PlatformXIOJs5Cache* js5;
    struct Js5PendingItem js5_pending[JS5_PENDING_SLOTS];
#endif
};

#if defined(TORIRS_WEB_CACHE_IDB)
/* The frame loop reaches the two host calls below without a handle (they stand
 * in for the wire backend's, which is a singleton for the same reason: the JS
 * side has no place to carry a context pointer). App owns exactly one
 * PlatformX_IO and shares it between both pipelines, so this is not a
 * restriction the design would otherwise have avoided. */
static struct PlatformX_IO* g_web_px = NULL;
#endif

struct PlatformX_IO*
PlatformX_IO_New(void)
{
    struct PlatformX_IO* px = malloc(sizeof(struct PlatformX_IO));
    assert(px);
    memset(px, 0, sizeof(struct PlatformX_IO));
#if defined(TORIRS_WEB_CACHE_IDB)
    g_web_px = px;
#endif
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

/* Nothing to record: this backend was given the open disk itself. */
void
PlatformX_IO_InitCacheId(
    struct PlatformX_IO* px,
    int epoch,
    int game,
    int revision,
    unsigned int quirks,
    const char* dir)
{
    (void)px;
    (void)epoch;
    (void)game;
    (void)revision;
    (void)quirks;
    (void)dir;
}

void
PlatformX_IO_Free(struct PlatformX_IO* px)
{
    if( !px )
        return;

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    PlatformXIOJs5Cache_Free(px->js5);
#endif
    for( int i = 0; i < DAT2_ARCHIVE_CACHE_SLOTS; i++ )
        RSCache_Dat2DiskArchiveFree(px->archive_cache[i].archive);
    free(px->config_dir);
    free(px->script_dir);
#if defined(TORIRS_WEB_CACHE_IDB)
    if( g_web_px == px )
        g_web_px = NULL;
#endif
    free(px);
}

static struct RSCache_Dat2DiskArchive*
dat2_archive_clone(const struct RSCache_Dat2DiskArchive* src)
{
    struct RSCache_Dat2DiskArchive* dst = malloc(sizeof(*dst));
    assert(dst);
    *dst = *src;
    dst->data = NULL;
    dst->file_ids = NULL;

    if( src->data && src->data_size > 0 )
    {
        dst->data = malloc((size_t)src->data_size);
        assert(dst->data);
        memcpy(dst->data, src->data, (size_t)src->data_size);
    }
    if( src->file_ids && src->file_count > 0 )
    {
        dst->file_ids = malloc((size_t)src->file_count * sizeof(int));
        assert(dst->file_ids);
        memcpy(dst->file_ids, src->file_ids, (size_t)src->file_count * sizeof(int));
    }
    return dst;
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
    assert(data);

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

/*
 * A file the client owns, read whole from the path as given.
 *
 * Absent is not an error the platform reports differently from unreadable —
 * both are error_code -1 and an empty answer. Whether "no file" means a first
 * launch or a problem is the caller's judgement, not the disk layer's.
 */
static int
read_client_file_item(struct ToriRS_IOItem* item)
{
    void* data = NULL;
    int data_size = 0;

#if defined(TORIRS_WEB_CACHE_IDB)
    /*
     * The browser's filesystem is MEMFS: it exists for the life of the tab and
     * nothing more. A client file is the player's saved options, which is
     * precisely the thing that must outlive a reload — writing it to MEMFS
     * reproduces the "the music setting does not save" defect rs_prefs.c was
     * written to fix, one layer lower down. So on this host the durable store
     * answers, and the virtual filesystem is not consulted at all.
     */
    {
        uint8_t* bytes = NULL;
        int size = 0;
        int found = Dat2WebStore_FileRead(item->u.file.path, &bytes, &size);

        if( found != 1 )
        {
            item->error_code = -1;
            return -1;
        }
        item->data = bytes;
        item->data_size = size;
        item->error_code = 0;
        return 0;
    }
#endif

    if( read_whole_file(item->u.file.path, &data, &data_size) != 0 )
    {
        item->error_code = -1;
        return -1;
    }
    item->data = data;
    item->data_size = data_size;
    item->error_code = 0;
    return 0;
}

/*
 * `mkdir -p` over the directory part of a path, if it has one. A configured
 * path pointing somewhere nested otherwise fails at fopen with a message that
 * reads like a permissions problem.
 */
static void
mkdir_parent(char const* path)
{
    char dir[TORIRS_IOITEM_MAX_PATH];
    size_t used = 0;
    size_t last_sep = 0;

    for( char const* scan = path; *scan; scan++ )
    {
        if( used + 1 >= sizeof(dir) )
            return;
        if( *scan == '/' || *scan == '\\' )
            last_sep = used;
        dir[used++] = *scan;
    }
    if( last_sep == 0 )
        return; /* a bare filename, or an absolute path's root */
    for( size_t i = 1; i <= last_sep; i++ )
    {
        if( i == last_sep || dir[i] == '/' || dir[i] == '\\' )
        {
            char saved = dir[i];

            dir[i] = '\0';
#ifdef _WIN32
            _mkdir(dir);
#else
            mkdir(dir, 0755);
#endif
            dir[i] = saved;
        }
    }
}

/*
 * Write-then-rename, so an interrupted write leaves the previous file rather
 * than a truncated one. For settings that is the difference between losing a
 * change and losing every setting the player ever made.
 */
static int
write_client_file_item(struct ToriRS_IOItem* item)
{
    char temp[TORIRS_IOITEM_MAX_PATH + 8];
    FILE* fp;

    if( !item->u.file.path[0] )
    {
        item->error_code = -1;
        return -1;
    }

#if defined(TORIRS_WEB_CACHE_IDB)
    /* See read_client_file_item. The write-then-rename below buys durability
     * against an interrupted write; a single keyed put is already atomic, so
     * the store replaces the whole dance rather than emulating it. */
    item->error_code = Dat2WebStore_FileWrite(
                           item->u.file.path, (const uint8_t*)item->data, item->data_size) == 0
                           ? 0
                           : -1;
    return item->error_code;
#endif

    mkdir_parent(item->u.file.path);
    snprintf(temp, sizeof(temp), "%s.tmp", item->u.file.path);
    fp = fopen(temp, "wb");
    if( !fp )
    {
        fprintf(stderr, "io: cannot write %s\n", temp);
        item->error_code = -1;
        return -1;
    }
    if( item->data_size > 0 &&
        fwrite(item->data, 1, (size_t)item->data_size, fp) != (size_t)item->data_size )
    {
        fclose(fp);
        remove(temp);
        item->error_code = -1;
        return -1;
    }
    fclose(fp);
#ifdef _WIN32
    /* ISO C rename() is allowed to reject an existing destination, and the
     * Windows CRT does. MoveFileEx supplies the replace-existing semantics
     * this write-then-rename path requires while keeping the swap atomic. */
    if( !MoveFileExA(
            temp,
            item->u.file.path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) )
#else
    if( rename(temp, item->u.file.path) != 0 )
#endif
    {
        fprintf(stderr, "io: cannot replace %s\n", item->u.file.path);
        remove(temp);
        item->error_code = -1;
        return -1;
    }
    item->error_code = 0;
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

/*
 * A plugin script, or the manifest that names them.
 *
 * Same split as read_client_file_item, and for the same reason: on the browser
 * lane the "filesystem" is MEMFS, which starts empty and forgets everything
 * when the tab closes. A script read against it fails, task_plugin_io.c
 * deliberately treats a missing manifest as the ordinary case, and the plugin
 * roster quietly shows only the statically linked C plugins -- which is
 * exactly how this was found.
 *
 * So the durable store answers instead. The page puts the manifest and every
 * script it names there before main() (torirs_host.js: `plugins.load`), keyed
 * by the SAME joined path this builds, so the two cannot disagree about where
 * a script lives. Nothing is baked into the module and nothing is opened by
 * name; the request still arrives through the IO queue exactly as it does on
 * the desktop.
 */
static int
read_script_item(struct PlatformX_IO* px, struct ToriRS_IOItem* item)
{
#if defined(TORIRS_WEB_CACHE_IDB)
    char path[TORIRS_IOITEM_MAX_PATH * 2];
    uint8_t* bytes = NULL;
    int size = 0;

    /* The store is keyed by the whole path, not by (dir, name): a key that
     * dropped the root would collide the moment two roots held a file of the
     * same name, and the page has to be able to spell the key too. */
    if( px->script_dir && px->script_dir[0] )
        snprintf(path, sizeof(path), "%s/%s", px->script_dir, item->u.script.path);
    else
        snprintf(path, sizeof(path), "%s", item->u.script.path);

    if( Dat2WebStore_FileRead(path, &bytes, &size) != 1 )
    {
        item->error_code = -1;
        return -1;
    }
    item->data = bytes;
    item->data_size = size;
    item->error_code = 0;
    return 0;
#else
    return load_file_item(item, px->script_dir, item->u.script.path);
#endif
}


/*
 * Turn the logical table a caller queued into the on-disk id THIS cache uses.
 *
 * Queued dat2 items name a table by role (RSCACHE_DAT2_TABLE_*), not by number, and the
 * number is settled here — the one place that holds the open cache and therefore knows its
 * epoch. Ids are not portable between branches: 19 is OldSchool's worldmap and RS2's objs,
 * 26 is RS2's materials and nothing in OldSchool. Resolving anywhere else means a caller has
 * to know which cache is open in order to ask for a table.
 *
 * Returns RSCACHE_DAT2_DISK_TABLE_ABSENT when this cache's branch has no such table. That is
 * a refusal to read, and deliberately so: the id a wrong-branch read would land on usually
 * *exists* and decodes into something else entirely, which surfaces far from here.
 *
 * (This replaced an allow-list of named table ids, a shape that caused the same bug three
 * times — D18, then the RS2 tables 16..22, then materials 26. An id missing from the list was
 * refused before any read, so the caller reported "decode failed" or "no materials table"
 * with nothing anywhere saying the read had been rejected. Resolution can still refuse, but
 * only for a table the open cache genuinely does not have.)
 */
static int
dat2_resolve_table(
    struct PlatformX_IO* px,
    int logical_table)
{
    if( logical_table < 0 || logical_table >= RSCACHE_DAT2_TABLE_COUNT )
        return RSCACHE_DAT2_DISK_TABLE_ABSENT;
    return RSCache_Dat2DiskTableId(px->dat2_disk, (enum RSCache_Dat2Table)logical_table);
}

static int
load_cache_item_dat2(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int logical_table = item->u.cache.table_id;
    int table_id = dat2_resolve_table(px, logical_table);
    int archive_id = item->u.cache.archive_id;
    struct RSCache_Dat2DiskArchive* archive = NULL;

    assert(px->dat2_disk);

    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        fprintf(
            stderr,
            "dat2: logical table %d has no table in this cache's branch (game %s)\n",
            logical_table,
            RSCache_GameName(px->dat2_disk->profile.game));
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
        /* Loc (lX_Z) map archives are XTEA-encrypted on OldSchool below 237 and
         * on RS2 dat2 from 414. Terrain (mX_Z) is never keyed. The identity
         * gate — not the key file — decides; applying a key to plain data
         * corrupts. */
        if( logical_table == RSCACHE_DAT2_TABLE_MAPS )
        {
            const struct RSCache* profile = RSCache_Dat2DiskProfile(px->dat2_disk);
            assert(profile && "Dat2DiskSetProfile required before map archive IO");
            if( RSCache_MapLocsEncrypted(profile) )
                xtea_key = RSCache_Dat2DiskArchiveXteaKey(px->dat2_disk, table_id, archive_id);
        }
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

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
static struct Js5PendingItem*
js5_pending_alloc(struct PlatformX_IO* px)
{
    for( int i = 0; i < JS5_PENDING_SLOTS; i++ )
        if( !px->js5_pending[i].in_use )
            return &px->js5_pending[i];
    return NULL;
}

static int
js5_queue_cache_item(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io,
    int slot)
{
    struct ToriRS_IOItem* item = &io->io_slots[slot];
    int archive = dat2_resolve_table(px, item->u.cache.table_id);
    int group = item->u.cache.archive_id;
    enum Js5RequestResult request;
    struct Js5PendingItem* pending;

    if( archive == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return load_cache_item_dat2(px, item);

    request = PlatformXIOJs5Cache_RequestGroup(px->js5, archive, group);
    if( request == JS5_REQUEST_ALREADY_READY )
        return load_cache_item_dat2(px, item);
    if( request == JS5_REQUEST_ERROR )
    {
        item->error_code = -1;
        return -1;
    }

    pending = js5_pending_alloc(px);
    if( !pending )
    {
        fprintf(stderr, "js5: pending IO table full, failing %d/%d\n", archive, group);
        item->error_code = -1;
        return -1;
    }
    pending->in_use = 1;
    pending->io = io;
    pending->slot = slot;
    pending->archive = archive;
    pending->group = group;
    return 0;
}

static void
js5_service_pending(
    struct PlatformX_IO* px,
    int terminal_failure)
{
    for( int i = 0; i < JS5_PENDING_SLOTS; i++ )
    {
        struct Js5PendingItem* pending = &px->js5_pending[i];
        struct ToriRS_IOItem* item;
        int group_failed;

        if( !pending->in_use )
            continue;
        group_failed = PlatformXIOJs5Cache_GroupFailed(
            px->js5, pending->archive, pending->group);
        if( !terminal_failure && !group_failed && !PlatformXIOJs5Cache_GroupReady(
                                                        px->js5,
                                                        pending->archive,
                                                        pending->group) )
            continue;

        item = &pending->io->io_slots[pending->slot];
        if( terminal_failure || group_failed )
            item->error_code = -1;
        else
            load_cache_item_dat2(px, item);
        memset(pending, 0, sizeof(*pending));
    }
}
#endif

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
    int table_id = dat2_resolve_table(px, item->u.reference_table.table_id);
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_ReferenceTable* table = NULL;

    assert(px->dat2_disk);

    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        item->error_code = -1;
        return -1;
    }

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

    /* Before the clear below: a write is the one item that arrives carrying
     * data, and zeroing those two fields here would hand the platform an empty
     * file to write. */
    if( item->kind == TORIRS_IOK_FILE_WRITE )
    {
        item->error_code = 0;
        return write_client_file_item(item);
    }

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
        return read_script_item(px, item);
    case TORIRS_IOK_REFERENCE_TABLE:
        return load_reference_table_item(px, item);
    case TORIRS_IOK_FILE_READ:
        return read_client_file_item(item);
    default:
        item->error_code = -1;
        return -1;
    }
}

/* Local hits remain synchronous. A JS5 miss parks only the ToriRS_IO instance
 * that requested it; bounded pumping here also keeps native TaskRunner_Drain
 * live when there is no outer frame loop yet. */
int
PlatformX_IO_Pending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io)
{
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    int count = 0;

    assert(px);
    if( px->js5 )
        PlatformXIO_Js5Pump(px, PlatformSDL2_Ticks64());
    for( int i = 0; i < JS5_PENDING_SLOTS; i++ )
        if( px->js5_pending[i].in_use && px->js5_pending[i].io == io )
            count++;
    return count;
#else
    (void)px;
    (void)io;
    return 0;
#endif
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
        int slot = io->active[i];
        struct ToriRS_IOItem* item = &io->io_slots[slot];

        /* A write carries its payload in these two fields — see LoadItem. */
        if( item->kind != TORIRS_IOK_FILE_WRITE )
        {
            item->data = NULL;
            item->data_size = 0;
        }
        item->error_code = 0;

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
        if( px->js5 && item->kind == TORIRS_IOK_CACHE &&
            item->u.cache.flags == TORIRS_IO_CACHE_DAT2 )
        {
            if( js5_queue_cache_item(px, io, slot) == 0 )
                processed++;
        }
        else
#endif
            if( PlatformX_IO_LoadItem(px, item) == 0 )
            processed++;
    }

    ToriRS_IO_ResetActive(io);

    return processed;
}

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
int
PlatformXIO_Js5Enable(
    struct PlatformX_IO* px,
    const struct Js5Config* config)
{
    assert(px);
    if( !px->dat2_disk || px->js5 )
        return -1;
    assert(config);
    px->js5 = PlatformXIOJs5Cache_New(px->dat2_disk, config);
    return px->js5 ? 0 : -1;
}

int
PlatformXIO_Js5Pump(
    struct PlatformX_IO* px,
    uint64_t now_ms)
{
    int result;

    assert(px);
    if( !px->js5 )
        return -1;
    result = PlatformXIOJs5Cache_Tick(px->js5, now_ms);
    js5_service_pending(px, result < 0);
    if( result < 0 )
        return -1;
    return PlatformXIOJs5Cache_MetadataReady(px->js5) ? 1 : 0;
}

bool
PlatformXIO_Js5MetadataReady(const struct PlatformX_IO* px)
{
    return px && PlatformXIOJs5Cache_MetadataReady(px->js5);
}

enum Js5Error
PlatformXIO_Js5LastError(const struct PlatformX_IO* px)
{
    return px ? PlatformXIOJs5Cache_LastError(px->js5) : JS5_ERROR_ARGUMENT;
}

void
PlatformXIO_Js5GetProgress(
    const struct PlatformX_IO* px,
    struct Js5Progress* progress)
{
    PlatformXIOJs5Cache_GetProgress(px ? px->js5 : NULL, progress);
}
#endif

#if defined(TORIRS_WEB_CACHE_IDB)
/*
 * The frame loop's host-facing calls.
 *
 * They are declared in platform_x_io_web.h and implemented by the wire backend
 * on the other web lane. Their contract is about the *host*, not about where
 * the bytes come from — "give the asynchronous side a turn", "how many reads
 * are outstanding", "may a read block the frame" — so this backend answers them
 * too, and frame_loop_step needs no idea which cache source it was built
 * against.
 *
 * The pacing one matters more here than on the wire lane. While JS5 has reads
 * in flight the loop must run from the event loop rather than from
 * requestAnimationFrame: a WebSocket delivers between turns of the event loop,
 * so a display-rate loop would cap the download at one round trip per frame.
 */
void
PlatformXIO_Web_Pump(void)
{
    if( g_web_px && g_web_px->js5 )
        PlatformXIO_Js5Pump(g_web_px, PlatformSDL2_Ticks64());
}

int
PlatformXIO_Web_PendingTotal(void)
{
    int count = 0;

    if( !g_web_px )
        return 0;
    for( int i = 0; i < JS5_PENDING_SLOTS; i++ )
        if( g_web_px->js5_pending[i].in_use )
            count++;
    return count;
}

/*
 * Nothing to answer here. This lane has no synchronous read to suppress: JS5
 * arrives over a WebSocket, which cannot deliver inside the call that asked for
 * it, so every read on this backend is already the non-blocking kind.
 */
void
PlatformXIO_Web_SetBlockingReads(int allowed)
{
    (void)allowed;
}
#endif
