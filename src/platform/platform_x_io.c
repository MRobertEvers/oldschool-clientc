#include "platform_x_io.h"
/*
 * Two remote cache backings, two flags, because two callers want different
 * halves. JS5 is the dat2 one and drags in the SDL2 clock its pump is paced
 * by; on-demand is the dat1 one and needs nothing but a socket. io_server
 * wants the second without the first -- it PROXIES a LostCity cache to the
 * browser, which is the whole reason it links this file -- so a single flag
 * naming "every networked source" cannot express what it is asking for.
 */
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
#include "platform_x_io_js5.h"
#include "platform_x_io_js5_cache.h"
#include "platform_sdl2.h"
#endif
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
#include "platform_x_io_ondemand.h"
#endif

#include "asyncio.h"
#include "platform/platform_x_http.h"


#include <assert.h>
#include <rscache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "log/torirs_log.h"

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
/*
 * How many of those slots hold an archive unless told otherwise.
 *
 * The win the cache exists for comes from the few groups in flight at once --
 * a group is requested once per id it contains, so the repeat run is a burst,
 * not a long tail. Over a 400-frame boot, 4 slots decompress no more than 32
 * do and give back 0.6 MB of retained archives. Raise it with
 * TORIRS_DAT2_ARCHIVE_SLOTS (up to DAT2_ARCHIVE_CACHE_SLOTS) for a workload
 * that keeps more groups open.
 */
#define DAT2_ARCHIVE_CACHE_SLOTS_DEFAULT 4
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
    /* Slots actually used, <= DAT2_ARCHIVE_CACHE_SLOTS. The array stays a fixed
     * member (it is 8 bytes a slot); only how many of them are allowed to hold
     * an archive is tunable, because the archives are what cost megabytes. */
    int archive_cache_slots;
    uint64_t archive_cache_clock;

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    struct PlatformXIOJs5Cache* js5;
    struct Js5PendingItem js5_pending[JS5_PENDING_SLOTS];
#endif
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    /* The dat1 counterpart of dat2's js5 client: the cache lives on a LostCity
     * server rather than on this machine. Set instead of dat1_disk, never
     * beside it. */
    struct PlatformXIOOnDemand* dat1_on_demand;
#endif

    /*
     * Where stored_file_read's second leg asks, or "" for a client that has no
     * io_server and should not go looking for one.
     *
     * From TORIRS_IO_SERVER (`host` or `host:port`). An environment variable
     * rather than a manifest key because it is a property of the DEPLOYMENT --
     * the same manifest is run from a full tree on a developer's machine and
     * from a bare binary beside a server -- and those two want different
     * answers from one file.
     */
    char io_server_host[256];
    /* Set when TORIRS_IO_SERVER named it, so a later manifest does not. */
    int io_server_from_env;
    int io_server_port;

    /* Set when that second leg found nobody home. Stays zero for a client with
     * no io_server configured, which then answers "reachable" always -- true,
     * because it has nothing it could fail to reach. */
    int transport_down;
};


/* TORIRS_DAT2_ARCHIVE_SLOTS narrows the LRU without a rebuild. An unusable
 * value falls back to the compiled default rather than asserting: this reads a
 * user's environment, not a caller's argument. */
static int
dat2_archive_cache_slots(void)
{
    char const* env = getenv("TORIRS_DAT2_ARCHIVE_SLOTS");
    long n;

    if( !env || env[0] == '\0' )
        return DAT2_ARCHIVE_CACHE_SLOTS_DEFAULT;
    n = strtol(env, NULL, 10);
    if( n < 1 || n > DAT2_ARCHIVE_CACHE_SLOTS )
        return DAT2_ARCHIVE_CACHE_SLOTS_DEFAULT;
    return (int)n;
}

struct PlatformX_IO*
PlatformX_IO_New(void)
{
    struct PlatformX_IO* px = malloc(sizeof(struct PlatformX_IO));
    char const* server = getenv("TORIRS_IO_SERVER");

    assert(px);
    memset(px, 0, sizeof(struct PlatformX_IO));
    px->archive_cache_slots = dat2_archive_cache_slots();

    /* `host` or `host:port`; the port defaults to io_server's own. Parsed here
     * rather than at each use so an unparseable value fails once, visibly, at
     * startup instead of once per read. */
    px->io_server_port = 8088;
    if( server && server[0] )
    {
        char const* colon = strrchr(server, ':');
        if( colon && colon[1] )
        {
            int port = atoi(colon + 1);
            size_t host_len = (size_t)(colon - server);
            if( port > 0 && port <= 65535 && host_len < sizeof(px->io_server_host) )
            {
                memcpy(px->io_server_host, server, host_len);
                px->io_server_host[host_len] = '\0';
                px->io_server_port = port;
            }
        }
        else
            snprintf(px->io_server_host, sizeof(px->io_server_host), "%s", server);

        if( px->io_server_host[0] )
        {
            /* Remembered so a manifest cannot overwrite a deliberate one-off.
             * TORIRS_IO_SERVER is what someone reaches for to point a client
             * at a different server for one run; a manifest read afterwards
             * would silently undo that. */
            px->io_server_from_env = 1;
            TORIRS_REPORT("io: files not found locally will be asked of %s:%d\n",
                px->io_server_host,
                px->io_server_port);
        }
    }
    return px;
}

/*
 * Where to ask for a file this disk does not have.
 *
 * Called after the boot manifest is read, so a world can state its own file
 * server the way it states its game server. TORIRS_IO_SERVER wins: it is the
 * older spelling and the one a one-off debugging run uses, and a manifest
 * quietly overriding it would make that run lie about which server answered.
 *
 * An empty host is "say nothing", not "turn it off" -- the manifest simply had
 * no opinion, and a value from the environment stands.
 */
void
PlatformX_IO_InitIoServer(struct PlatformX_IO* px, const char* host, int port)
{
    assert(px);

    if( px->io_server_from_env )
        return;
    if( !host || !host[0] )
        return;

    snprintf(px->io_server_host, sizeof(px->io_server_host), "%s", host);
    if( port > 0 && port <= 65535 )
        px->io_server_port = port;

    TORIRS_REPORT("io: files not found locally will be asked of %s:%d\n",
        px->io_server_host,
        px->io_server_port);
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
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    /* One dat1 source. The remote one refuses an open disk from its side;
     * this is the same rule read the other way round. */
    assert(!px->dat1_on_demand);
#endif
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
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    PlatformXIOOnDemand_Free(px->dat1_on_demand);
#endif
    for( int i = 0; i < DAT2_ARCHIVE_CACHE_SLOTS; i++ )
        RSCache_Dat2DiskArchiveFree(px->archive_cache[i].archive);
    free(px->config_dir);
    free(px->script_dir);
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
    for( int i = 0; i < px->archive_cache_slots; i++ )
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
    for( int i = 0; i < px->archive_cache_slots; i++ )
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
 *
 * LOCAL ONLY, deliberately, and this is the one read that does not get
 * stored_file_read's second leg. These are the player's own files — saved
 * options, a plugin's saved assets — and asking a server for one would put a
 * single shared copy in front of every browser that machine answers, then read
 * back settings its user never chose. io_server refuses them by KIND for the
 * same reason (io_server_main.c), so the two ends agree: this is the client's,
 * and it never leaves.
 */
static int
read_client_file_item(struct ToriRS_IOItem* item)
{
    void* data = NULL;
    int data_size = 0;


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


    mkdir_parent(item->u.file.path);
    snprintf(temp, sizeof(temp), "%s.tmp", item->u.file.path);
    fp = fopen(temp, "wb");
    if( !fp )
    {
        TORIRS_ERR("io: cannot write %s\n", temp);
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
        TORIRS_ERR("io: cannot replace %s\n", item->u.file.path);
        remove(temp);
        item->error_code = -1;
        return -1;
    }
    item->error_code = 0;
    return 0;
}

/*
 * One file under one root, LOCAL STORE FIRST AND SERVER SECOND.
 *
 * Both legs are here, in one function, because the order between them is the
 * policy and splitting it per lane is how the two drifted apart before: the
 * browser lane had a store and no second leg, the desktop lane had a file and
 * no second leg, and neither could say why a file it did not have was missing.
 *
 * LEG 1 -- the local store. On the desktop that is the filesystem; on the
 * browser it is the record database, which is the same thing for this
 * purpose: a durable local copy the read can be answered from without leaving
 * the process. Keyed by the WHOLE joined path, never by (dir, name) -- a key
 * that dropped the root would collide the moment two roots held a file of the
 * same name, and the page has to be able to spell the key too.
 *
 * LEG 2 -- the server, for a path leg 1 did not have. The page stages what it
 * can name in advance (torirs_host.js: `boot.load`, `plugins.load`) and that
 * covers the manifest, the INIs it points at and the scripts it lists. It
 * cannot cover a plugin's ASSETS: those are named by the plugin, at runtime,
 * in code the page never reads. Without this leg they were unreachable in the
 * browser by construction, however healthy the server -- the plugin was
 * loaded, its data simply had no route.
 *
 * Bytes that arrive are written into the store on the way through, so a path
 * costs at most one round trip per session and the next read is leg 1 again.
 *
 * The desktop has no leg 2 and needs none: nothing sits between it and its
 * disk. The read either finds the file or does not, which is exactly what the
 * caller is told.
 */
static int
stored_file_read(
    struct PlatformX_IO* px,
    const char* base_dir,
    const char* path,
    void** out_data,
    int* out_size)
{
    char resolved[TORIRS_IOITEM_MAX_PATH * 2];

    assert(px);
    assert(path);
    assert(out_data);
    assert(out_size);

    if( base_dir && base_dir[0] )
        snprintf(resolved, sizeof(resolved), "%s/%s", base_dir, path);
    else
        snprintf(resolved, sizeof(resolved), "%s", path);

    if( read_whole_file(resolved, out_data, out_size) == 0 )
        return 0;

    /*
     * LEG 2: ask io_server for what this disk did not have.
     *
     * The desktop has a filesystem, which is why leg 1 is a real answer here
     * and not a formality -- but "has a filesystem" is not the same as "has the
     * file". A client run from somewhere other than the tree it was built in,
     * or a deployment that ships the binary without the script/ and config/
     * trees beside it, misses every one of these and has no way to recover:
     * a missing plugin manifest is deliberately silent (task_plugin_io.c), so
     * the roster comes up holding only the statically linked C plugins with
     * nothing anywhere saying why.
     *
     * So the desktop gets the same second leg the browser has, for the same
     * reason and against the same route. Off unless TORIRS_IO_SERVER names one:
     * a client with a local tree must not start dialling, and a client without
     * one should not start guessing.
     */
    if( px->io_server_host[0] )
    {
        char route[TORIRS_IOITEM_MAX_PATH * 2 + 8];
        char* body;
        int size = 0;
        int status = 0;

        snprintf(route, sizeof(route), "/boot/%s", resolved);
        body = PlatformX_HttpGetStatus(
            px->io_server_host, px->io_server_port, route, &size, &status);

        /* Reachability is decided here and nowhere else, because this is the
         * only place that learns it: a server answering 404 proves it is THERE
         * and clears the flag exactly as bytes would. Only silence raises it. */
        px->transport_down = status == 0;
        if( body )
        {
            *out_data = body;
            *out_size = size;
            return 0;
        }
    }

    return -1;
}

static int
load_file_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item,
    const char* base_dir,
    const char* path)
{
    void* data = NULL;
    int data_size = 0;

    if( stored_file_read(px, base_dir, path, &data, &data_size) != 0 )
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
 * A plugin script, the manifest that names them, or a shipped plugin asset.
 *
 * Nothing to decide here: stored_file_read is local-first and io_server-second
 * for every file kind, which is the whole of what this used to spell out twice.
 */
static int
read_script_item(struct PlatformX_IO* px, struct ToriRS_IOItem* item)
{
    return load_file_item(px, item, px->script_dir, item->u.script.path);
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

/*
 * Where a cache item's bytes come from.
 *
 * There are two remote backings and they are PEERS: each is a cache living on
 * a server instead of on this machine, one per container format. They did not
 * read as peers, because they were added at different depths -- JS5 intercepted
 * up in Process, before LoadItem was even called, while the dat1 on-demand
 * handle was tested four calls further down, inside the dat1 loader's archive
 * read. "Is this read local?" therefore had two answers in two places, and
 * neither function was in a position to state the rule.
 *
 * Naming the source makes the rule one line, and leaves exactly one real
 * difference between the two backings: whether the answer can be given
 * synchronously. That is what cache_source_parks reports, and it is a property
 * OF THE SOURCE rather than a special case in the caller.
 */
enum CacheSource
{
    /** The open disk on this machine answers -- dat2 or dat1. */
    CACHE_SOURCE_DISK,
    /** dat2 groups from a JS5 server, into the disk that client fills. */
    CACHE_SOURCE_JS5,
    /** dat1 archives from a LostCity server, 2004 on-demand protocol. */
    CACHE_SOURCE_ON_DEMAND,
};

/* Which container this item is phrased in. The dat2 case is every flag that is
 * not one of the three dat1 ones (asyncio.h defines exactly four). */
static int
cache_item_is_dat1(struct ToriRS_IOItem const* item)
{
    return item->u.cache.flags == TORIRS_IO_CACHE_DAT1 ||
           item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN ||
           item->u.cache.flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY;
}

/*
 * One rule, both containers: a remote backing answers when one is configured.
 *
 * Configuring one excludes the matching local disk, which is what makes this a
 * choice rather than a preference -- PlatformXIO_Dat1OnDemandEnable refuses if
 * a dat1 disk is already open, and the JS5 client is handed the dat2 disk it
 * fills, so there is never a second opinion to reconcile.
 *
 * A build with no JS5 has neither handle to test and every read is local, so
 * this collapses to a constant rather than carrying a dead branch.
 */
static enum CacheSource
cache_source_for(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem const* item)
{
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    if( cache_item_is_dat1(item) )
        return px->dat1_on_demand ? CACHE_SOURCE_ON_DEMAND : CACHE_SOURCE_DISK;
#endif
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    if( !cache_item_is_dat1(item) )
        return px->js5 ? CACHE_SOURCE_JS5 : CACHE_SOURCE_DISK;
#endif
    (void)px;
    (void)item;
    return CACHE_SOURCE_DISK;
}

/*
 * Must the caller park on this source rather than be answered inside LoadItem?
 *
 * The one asymmetry left between the two remote backings, and a real one: JS5
 * pulls a group over several frames and the task has to wait, while the
 * on-demand handle blocks until its archive arrives and so behaves exactly
 * like a disk from here. Asking this instead of testing for JS5 by hand is
 * what lets Process talk about sources; the day the dat1 handle grows the same
 * behaviour, it answers yes here and nothing else moves.
 */
/* Only compiled where a source can park. A build with no JS5 has no such
 * source, so Process never asks and the question would be dead code. */
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
static int
cache_source_parks(enum CacheSource source)
{
    return source == CACHE_SOURCE_JS5;
}
#endif

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
        TORIRS_LOG("dat2: logical table %d has no table in this cache's branch (game %s)\n",
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
            TORIRS_LOG("io_trace: dat2 table=%d archive=%d\n", table_id, archive_id);
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
    if( !RSCache_Dat2DiskArchiveInitMetadataFromTable(
            RSCache_Dat2DiskReferenceTable(px->dat2_disk, table_id), archive) )
    {
        TORIRS_LOG("dat2 archive %d in table %d absent from reference table\n",
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
        TORIRS_ERR("js5: pending IO table full, failing %d/%d\n", archive, group);
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
    int want_scenery,
    enum CacheSource source)
{
    struct RSCache_MapSquares* squares = NULL;

#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    /* Same table, other source: the server's versionlist, decoded when the
     * on-demand handle opened. Selected by source rather than by falling back
     * off a NULL disk, so this reads the same way the archive load below does. */
    if( source == CACHE_SOURCE_ON_DEMAND )
        squares = PlatformXIOOnDemand_MapSquares(px->dat1_on_demand);
    else
#else
    (void)source;
#endif
        squares = px->dat1_disk ? px->dat1_disk->map_squares : NULL;

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
    struct ToriRS_IOItem* item,
    enum CacheSource source)
{
    int table_id = item->u.cache.table_id;
    int archive_id = item->u.cache.archive_id;
    int flags = item->u.cache.flags;
    struct RSCache_Dat1DiskArchive* archive = NULL;

    /* Exactly one dat1 source is configured, so a source that is not the
     * on-demand handle must be a disk. Reading through a NULL disk is what
     * this catches, and it now says so without a per-build spelling: the
     * on-demand arm is only reachable when that handle exists, because that is
     * the only way cache_source_for names it. */
    assert(source == CACHE_SOURCE_ON_DEMAND || px->dat1_disk);

    if( flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN || flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY )
    {
        archive_id = dat1_map_archive_id(
            px, archive_id, flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY, source);
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

#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    if( source == CACHE_SOURCE_ON_DEMAND )
        archive = PlatformXIOOnDemand_ArchiveLoad(px->dat1_on_demand, table_id, archive_id);
    else
#endif
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

/*
 * One cache read, dispatched on its source.
 *
 * Both remote backings appear here, at the same level, which is the whole
 * point of the enum: the container split that used to be the only thing this
 * function said is now one arm of it rather than the shape of it.
 */
static int
load_cache_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    enum CacheSource const source = cache_source_for(px, item);

    switch( source )
    {
    case CACHE_SOURCE_JS5:
        /*
         * Only ever reached for a group that is already resident: Process
         * parks everything else on the JS5 client and resumes it here once the
         * bytes have landed in the disk. So this is a disk read, and it is the
         * same one CACHE_SOURCE_DISK does -- what differs is who filled it.
         */
        return load_cache_item_dat2(px, item);
    case CACHE_SOURCE_ON_DEMAND:
        return load_cache_item_dat1(px, item, source);
    case CACHE_SOURCE_DISK:
        return cache_item_is_dat1(item) ? load_cache_item_dat1(px, item, source)
                                        : load_cache_item_dat2(px, item);
    }

    /* No default above, so a source added without an arm is a compiler
     * warning rather than a silent fall-through to the wrong container. */
    item->error_code = -1;
    return -1;
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
        return load_file_item(px, item, px->config_dir, item->u.config_file.path);
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

/*
 * Is the read in THIS slot still coming?
 *
 * The per-task half of Pending above, and the reason a task waiting on a JS5
 * miss no longer holds up every other task's turn. A synchronous read has
 * already landed by the time anyone can ask, so on a cache with no JS5
 * attached this is always no -- which is exactly what it was before the runner
 * learned to ask.
 */
int
PlatformX_IO_SlotPending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io,
    int slot)
{
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    assert(px);
    for( int i = 0; i < JS5_PENDING_SLOTS; i++ )
        if( px->js5_pending[i].in_use && px->js5_pending[i].io == io &&
            px->js5_pending[i].slot == slot )
            return 1;
    return 0;
#else
    (void)px;
    (void)io;
    (void)slot;
    return 0;
#endif
}

/*
 * Whatever stored_file_read's second leg last found.
 *
 * On the desktop there is no second leg, so this is yes for the life of the
 * process -- see the header for why that is an answer and not a stub. On the
 * browser lane it is the page's server, and the flag moves only on evidence: a
 * request that went unanswered raises it, and any answer at all -- bytes, or
 * an honest "no such file" -- clears it.
 *
 * Yes until proven otherwise, which is the right default for the frames before
 * anything has been asked for. A client that started with its plugin UI
 * switched off waiting for proof of life would never get it: the proof is a
 * request, and the requests are made by the things that UI leads to.
 */
int
PlatformX_IO_ServerReachable(struct PlatformX_IO* px)
{
    assert(px);
    return !px->transport_down;
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
        /* A source that parks is the only reason this loop does anything other
         * than call LoadItem. Asked as a question about the SOURCE, so the
         * on-demand backing is covered by the same sentence rather than being
         * handled somewhere LoadItem cannot see. */
        if( item->kind == TORIRS_IOK_CACHE &&
            cache_source_parks(cache_source_for(px, item)) )
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

#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
int
PlatformXIO_Dat1OnDemandEnable(
    struct PlatformX_IO* px,
    const char* host,
    int game_port,
    int web_port,
    const char* cache_dir)
{
    assert(px);
    assert(host);
    if( px->dat1_disk || px->dat1_on_demand )
        return -1;
    px->dat1_on_demand =
        PlatformXIOOnDemand_New(host, game_port, web_port, cache_dir);
    return px->dat1_on_demand ? 0 : -1;
}

int
PlatformXIO_Dat1OnDemandJagChecksums(
    struct PlatformX_IO* px,
    int32_t out[9])
{
    assert(px);
    assert(out);
    if( !px->dat1_on_demand )
        return -1;
    return PlatformXIOOnDemand_JagChecksums(px->dat1_on_demand, out);
}

int
PlatformXIO_Dat1OnDemandJagChecksumsRefresh(
    struct PlatformX_IO* px,
    int32_t out[9])
{
    assert(px);
    assert(out);
    if( !px->dat1_on_demand )
        return -1;
    return PlatformXIOOnDemand_JagChecksumsRefresh(px->dat1_on_demand, out);
}

uint8_t*
PlatformXIO_Dat1OnDemandContainerFetch(
    struct PlatformX_IO* px,
    int table_id,
    int archive_id,
    int flags,
    int* out_format,
    int* out_size)
{
    assert(px);
    assert(out_format);
    assert(out_size);
    if( !px->dat1_on_demand )
        return NULL;

    /*
     * The same resolution load_cache_item_dat1 does, and deliberately the same
     * code: a map read names a SQUARE and the archive holding it is a fact
     * only the server's versionlist knows. A proxy that skipped this would
     * pass the square id through as an archive id and serve the wrong file --
     * silently, because any archive decodes into something.
     */
    if( flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN || flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY )
    {
        archive_id = dat1_map_archive_id(
            px, archive_id, flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY, CACHE_SOURCE_ON_DEMAND);
        if( archive_id < 0 )
            return NULL;
    }

    return PlatformXIOOnDemand_ContainerFetch(
        px->dat1_on_demand, table_id, archive_id, out_format, out_size);
}
#endif
