#include "platform_x_io.h"
#include "mem/torirs_mem.h"
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
#include "platform_window.h"
#endif
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
#include "platform_x_io_ondemand.h"
#endif

#include "asyncio.h"
#include "platform/platform_x_http.h"

/* The launch service's items (raid seam37; see answer_launch_item). These
 * three lines belong in platform_x_io.h, which is outside that seam's files:
 * main.c repeats the prototype. */
#define PLATFORM_X_IO_LAUNCH_PREFIX "launch/"
typedef char* (*PlatformX_IO_LaunchAnswerFn)(
    void* context, char const* verb, char const* body, int body_size, int* out_size);
void PlatformX_IO_SetLaunchAnswer(PlatformX_IO_LaunchAnswerFn answer, void* context);

/* The account server's items (game/task_accounts_io.h; see answer_auth_item). */
#define PLATFORM_X_IO_AUTH_PREFIX "auth/"

/* The player's files, as a folder (see answer_files_item). */
#define PLATFORM_X_IO_FILES_PREFIX "files/"


#include <assert.h>
#include <rscache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "log/torirs_log.h"

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/wait.h>
#endif

#ifdef _WIN32
#include <direct.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
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
 * Four was measured on a BOOT, where a group is requested once per id it
 * contains and the repeat run is a burst rather than a long tail. In play the
 * tail is what matters: a rebuild reads the loc, underlay, overlay, sequence
 * and texture groups, an interface opening reads enums, structs, params and
 * sprites, an inventory update reads the obj group, and with four slots those
 * evict each other inside one frame. The loser is bzip2'd again on its next
 * touch -- 0.6 MB compressed for the loc or obj group -- which is the stutter
 * behind doors, item icons and first-open interfaces. Eight holds a rebuild's
 * set with room for the UI's. Raise it with TORIRS_DAT2_ARCHIVE_SLOTS (up to
 * DAT2_ARCHIVE_CACHE_SLOTS) for a workload that keeps more groups open; the
 * split-group cache above it (DAT2_GROUP_CACHE_BUDGET) has the same story.
 */
#define DAT2_ARCHIVE_CACHE_SLOTS_DEFAULT 8

struct Dat2ArchiveCacheSlot
{
    struct RSCache_Dat2DiskArchive* archive;
    uint64_t last_used;
};

/*
 * A read parked on a remote source.
 *
 * One table for both remote backings, because to the queue they are the same
 * thing: an item whose answer is coming later. `archive`/`group` are the
 * source's own address -- the resolved JS5 archive and group, or the dat1
 * table and the resolved archive id -- so servicing never re-resolves.
 *
 * Grown, not fixed. The fixed 64 it replaced was a ceiling on how much of the
 * client's work could be on the wire at once, and the executor now hands over
 * a whole region's reads in one pass; past the ceiling every further read was
 * FAILED outright ("pending IO table full"), which read as a corrupt cache.
 */
enum RemoteSource
{
    REMOTE_SOURCE_JS5 = 1,
    REMOTE_SOURCE_ON_DEMAND = 2,
    /* One row per group of a PREFETCH item, all naming the same item, whose
     * pending count is the number of rows left. No row loads anything: the
     * group is in the disk the JS5 client filled, which is all a prefetch
     * asked for. */
    REMOTE_SOURCE_JS5_PREFETCH = 3,
};

struct RemotePendingItem
{
    int in_use;
    int source;
    /* The item this row answers into. It lives in its task, which the runner
     * will not resume, and so cannot free, while the item is pending. */
    struct ToriRS_IOItem* item;
    int archive;
    int group;
};

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
#endif
    /* Reads parked on either remote source. @see RemotePendingItem. */
    struct RemotePendingItem* pending;
    int pending_count;
    int pending_cap;
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
    struct PlatformX_IO* px = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(struct PlatformX_IO));
    char const* server = getenv("TORIRS_IO_SERVER");

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
    ToriMem_Free(ToriMem_Net(), px->config_dir);
    px->config_dir = ToriMem_Strdup(ToriMem_Net(), config_path);
}

void
PlatformX_IO_InitScriptPath(
    struct PlatformX_IO* px,
    const char* script_path)
{
    assert(px);
    assert(script_path);
    ToriMem_Free(ToriMem_Net(), px->script_dir);
    px->script_dir = ToriMem_Strdup(ToriMem_Net(), script_path);
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
    ToriMem_Free(ToriMem_Net(), px->pending);
    for( int i = 0; i < DAT2_ARCHIVE_CACHE_SLOTS; i++ )
        RSCache_Dat2DiskArchiveFree(px->archive_cache[i].archive);
    ToriMem_Free(ToriMem_Net(), px->config_dir);
    ToriMem_Free(ToriMem_Net(), px->script_dir);
    ToriMem_Free(ToriMem_Net(), px);
}

/*
 * The archive cache LENDS its archives: a request that hits gets the cached
 * archive itself with a holder added (RSCache_Dat2DiskArchiveRetain), and
 * the task's RSCache_Dat2DiskArchiveFree is that holder letting go. It used
 * to hand out a full copy per request, which at world entry meant the
 * 2.5 MB loc config group copied once for each of ~1,000 loc lookups
 * waiting on the platform at the same time -- 2.7 GB live, and on a phone
 * the low-memory killer. Consumers read the archive only, so sharing is
 * sound; the cache's own hold is released by eviction like any other.
 */

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

void
PlatformX_IO_InvalidateArchive(
    struct PlatformX_IO* px,
    int index,
    int archive)
{
    int dropped = 0;

    assert(px);
    assert(index >= 0);
    assert(index < 255);
    assert(archive >= 0);
    /* The decompressed copy: released, not freed -- a task still holding it
     * (the cache lends, see above) keeps it until its own Free. */
    for( int i = 0; i < px->archive_cache_slots; i++ )
    {
        struct Dat2ArchiveCacheSlot* slot = &px->archive_cache[i];
        if( !slot->archive || slot->archive->table_id != index ||
            slot->archive->archive_id != archive )
            continue;
        RSCache_Dat2DiskArchiveFree(slot->archive);
        slot->archive = NULL;
        slot->last_used = 0;
        dropped++;
    }
    {
        const char* js5_note = "";
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
        /* The stored copy: the JS5 client re-primes the index's reference
         * table, and a group whose CRC moved stops validating and is fetched
         * again. */
        if( px->js5 )
        {
            PlatformXIOJs5Cache_Invalidate(px->js5, index);
            js5_note = "; js5 re-prime of 255/255 and the index's reference table queued";
        }
#endif
        if( getenv("TORIRS_CACHE_INVALIDATE_DEBUG") )
            TORIRS_REPORT("cache invalidate: io %d/%d: %d decompressed cop%s dropped%s\n", index,
                       archive, dropped, dropped == 1 ? "y" : "ies", js5_note);
    }
}

void
PlatformX_IO_ArchiveInvalidateHook(
    void* user,
    int index,
    int archive)
{
    assert(user);
    PlatformX_IO_InvalidateArchive((struct PlatformX_IO*)user, index, archive);
}

int
PlatformX_IO_ArchiveReprimePendingHook(
    void* user,
    int index)
{
    assert(user);
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    {
        struct PlatformX_IO const* px = user;
        if( px->js5 )
            return PlatformXIOJs5Cache_ReprimePending(px->js5, index);
    }
#else
    (void)index;
#endif
    return 0;
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

    /* NET: the bytes become an IO item's data (ToriRS_IO_ClearItem). */
    void* data = ToriMem_Alloc(ToriMem_Net(), size > 0 ? (size_t)size : 1u);

    if( size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size )
    {
        ToriMem_Free(ToriMem_Net(), data);
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
 * NOTHING IS WRITTEN BACK. Leg 2's bytes are handed to the caller and
 * forgotten, so every miss is a fresh request in the next session. That is
 * deliberate, and it is the more expensive of the two choices on purpose: a
 * local copy is exactly how a device ends up running plugins nobody has
 * looked at in weeks. The Android lane spent 2026-09-19 loading a
 * performance overlay from a copy pushed on 2026-09-03, whose Lua predated
 * the V2 plugin definition the client had since started requiring -- it
 * failed to load, silently, and the only symptom was an overlay that was not
 * there. A read that always asks the server cannot go stale. The cost is one
 * round trip per path per session, on a LAN, for files measured in kilobytes.
 *
 * Every native lane has leg 2, not only the browser. The desktop can be run
 * from somewhere other than the tree it was built in, and a deployment can
 * ship the binary without the script/ and config/ trees beside it; Android
 * cannot put a tree beside the binary at all, because the .so lives inside
 * an APK and its data root is whatever was last pushed. Off unless a route
 * is named -- TORIRS_IO_SERVER, or `[io] host=/port=` in the boot manifest --
 * so a client with a local tree never starts dialling and a client without
 * one never starts guessing.
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
            /* The body is NET's, and so is an IO item's data: it becomes the
             * item's payload as it is (ToriRS_IO_ClearItem). */
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
 * THE LAUNCH SERVICE'S ITEMS (raid seam37; src/platform/launch_sessions.h,
 * docs/minigames/raid_loop/SEAM_TRIAGE_2026-10-06j.md DESIGN (1)).
 *
 * A request to the embedded IO server's launch service travels the plugin
 * channel as a SCRIPT item whose path is `launch/<verb>` and whose `data` /
 * `data_size` LEND the request body (key=value lines) the way FILE_WRITE lends
 * its bytes: the task that queued it keeps and frees the body, and the answer
 * replaces it in the item (a malloc'd text the item owns, freed by
 * ToriRS_IO_ClearItem like any SCRIPT payload). A SCRIPT item rather than a
 * new kind: every executor already carries SCRIPT items (asyncio.h's item
 * layout and its browser ABI are unchanged), and one that does not know the
 * prefix answers a script that is not there, which the caller reads as "no
 * launch service".
 *
 * Answered IN THIS PROCESS by the host's service (main.c sets it: the leader
 * client is the spawner, the owner's "both servers embedded"). A process with
 * none -- the standalone io_server, a test binary -- answers `unsupported:`
 * without touching a disk or a socket, so the HTTP leg never sees the item.
 */
static PlatformX_IO_LaunchAnswerFn g_launch_answer;
static void* g_launch_answer_context;

void
PlatformX_IO_SetLaunchAnswer(
    PlatformX_IO_LaunchAnswerFn answer,
    void* context)
{
    g_launch_answer = answer;
    g_launch_answer_context = context;
}

static int
item_is_launch(struct ToriRS_IOItem const* item)
{
    return item->kind == TORIRS_IOK_SCRIPT &&
           strncmp(item->u.script.path, PLATFORM_X_IO_LAUNCH_PREFIX,
                   sizeof(PLATFORM_X_IO_LAUNCH_PREFIX) - 1) == 0;
}

static int
answer_launch_item(struct ToriRS_IOItem* item)
{
    char const* verb = item->u.script.path + sizeof(PLATFORM_X_IO_LAUNCH_PREFIX) - 1;
    char const* body = (char const*)item->data;
    int const body_size = item->data_size;
    char* answer;
    int answer_size = 0;

    assert(body_size >= 0);
    if( body_size > 0 )
        assert(body);
    if( g_launch_answer )
        answer = g_launch_answer(g_launch_answer_context, verb, body, body_size, &answer_size);
    else
    {
        static char const none[] = "unsupported: no launch service in this process (only a "
                                   "desktop client's embedded IO server launches clients)\n";
        /* NET: an IO item's data (ToriRS_IO_ClearItem). */
        answer = (char*)ToriMem_Alloc(ToriMem_Net(), sizeof(none));
        memcpy(answer, none, sizeof(none));
        answer_size = (int)sizeof(none) - 1;
    }
    assert(answer);
    item->data = answer;
    item->data_size = answer_size;
    item->error_code = 0;
    return 0;
}

/*
 * THE PLAYER'S FILES, AS A FOLDER (plugin/task_plugin_io.h
 * CreateTask_PluginFiles: the screenshot plugin's gallery and its settings).
 *
 * A SCRIPT item whose path is `files/<verb>`, lending a key=value body the
 * way a launch item does and answered in place of it. The answer's first
 * line is the verdict -- `ok`, `cancelled`, `unsupported: <why>` or
 * `error: <why>` -- and what follows it is the verb's own:
 *
 *   files/list         dir=   every .png at or below it, one a line:
 *                             <path below dir>\t<mtime, s>\t<bytes>. A dir
 *                             that does not exist yet is an empty list: no
 *                             picture has been taken, which is an answer.
 *   files/reveal       dir=   made when absent, then shown in the file
 *                             manager; with path=, that one picture opened
 *                             in the platform's viewer instead.
 *   files/pick_folder  start= the platform's own folder chooser; the line
 *                             after `ok` is the folder chosen.
 *   files/download     the browser's verb. A desktop has the folder.
 *
 * `dir` is a client-file path spelled as a FILE_WRITE's is -- relative to the
 * working directory, or absolute -- because the files it names were written
 * that way. The browser executor answers the same verbs over its record
 * database (platform_web_io.js), which is what lets one plugin say "your
 * pictures" on both lanes without knowing which it is on.
 *
 * The chooser and the file manager are modal from the frame's point of view:
 * this executor answers inside Process, so the frame waits while the player
 * chooses. That is what every native folder dialog does to its application,
 * and it is asked for only by a click.
 */
static PlatformX_IO_FolderPickerFn g_folder_picker;

void
PlatformX_IO_SetFolderPicker(PlatformX_IO_FolderPickerFn picker)
{
    g_folder_picker = picker;
}

static int
item_is_files(struct ToriRS_IOItem const* item)
{
    return item->kind == TORIRS_IOK_SCRIPT &&
           strncmp(item->u.script.path, PLATFORM_X_IO_FILES_PREFIX,
                   sizeof(PLATFORM_X_IO_FILES_PREFIX) - 1) == 0;
}

/* The answer text, grown as it is written. NET: an IO item's data. */
struct FilesAnswer
{
    char* text;
    int size;
    int capacity;
};

static void
files_answer_add(
    struct FilesAnswer* answer,
    char const* fmt,
    ...)
{
    va_list args;
    int need;

    assert(answer);
    assert(fmt);
    va_start(args, fmt);
    need = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    assert(need >= 0);
    if( answer->size + need + 1 > answer->capacity )
    {
        int capacity = answer->capacity ? answer->capacity : 256;

        while( answer->size + need + 1 > capacity )
            capacity *= 2;
        answer->text = (char*)ToriMem_Realloc(ToriMem_Net(), answer->text, (size_t)capacity);
        assert(answer->text);
        answer->capacity = capacity;
    }
    va_start(args, fmt);
    vsnprintf(answer->text + answer->size, (size_t)(answer->capacity - answer->size), fmt, args);
    va_end(args);
    answer->size += need;
}

/* `key=` from the lent body, or "" when the body does not say. */
static void
files_body_value(
    char const* body,
    int body_size,
    char const* key,
    char* out,
    size_t out_size)
{
    size_t const key_len = strlen(key);
    char const* end = body + body_size;
    char const* line = body;

    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    while( body && line < end )
    {
        char const* eol = memchr(line, '\n', (size_t)(end - line));

        if( !eol )
            eol = end;
        if( (size_t)(eol - line) > key_len && strncmp(line, key, key_len) == 0 &&
            line[key_len] == '=' )
        {
            size_t len = (size_t)(eol - line) - key_len - 1;

            if( len > 0 && line[key_len + 1 + len - 1] == '\r' )
                len--;
            if( len >= out_size )
                len = out_size - 1;
            memcpy(out, line + key_len + 1, len);
            out[len] = '\0';
            return;
        }
        line = eol + 1;
    }
}

static int
files_name_is_png(char const* name)
{
    size_t const len = strlen(name);

    return len > 4 && (strcmp(name + len - 4, ".png") == 0 || strcmp(name + len - 4, ".PNG") == 0);
}

/* Depth 6 is player/category and four more: deeper than any destination the
 * screenshot plugin composes, shallow enough that a folder pointed at a home
 * directory is not a walk of the whole disk. */
#define FILES_LIST_DEPTH_MAX 6

#if defined(_WIN32)
static void
files_list_walk(
    struct FilesAnswer* answer,
    char const* root,
    char const* below,
    int depth)
{
    char pattern[TORIRS_IOITEM_MAX_PATH * 2];
    WIN32_FIND_DATAA found;
    HANDLE find;

    snprintf(pattern, sizeof(pattern), "%s/%s%s*", root, below, below[0] ? "/" : "");
    find = FindFirstFileA(pattern, &found);
    if( find == INVALID_HANDLE_VALUE )
        return;
    do
    {
        char rel[TORIRS_IOITEM_MAX_PATH * 2];

        if( found.cFileName[0] == '.' )
            continue;
        snprintf(rel, sizeof(rel), "%s%s%s", below, below[0] ? "/" : "", found.cFileName);
        if( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
        {
            if( depth < FILES_LIST_DEPTH_MAX )
                files_list_walk(answer, root, rel, depth + 1);
        }
        else if( files_name_is_png(found.cFileName) )
        {
            /* FILETIME is 100 ns ticks since 1601; the answer is Unix seconds. */
            unsigned long long const ticks =
                ((unsigned long long)found.ftLastWriteTime.dwHighDateTime << 32) |
                found.ftLastWriteTime.dwLowDateTime;
            long long const mtime = (long long)(ticks / 10000000ULL) - 11644473600LL;

            files_answer_add(answer, "%s\t%lld\t%lu\n", rel, mtime,
                             (unsigned long)found.nFileSizeLow);
        }
    } while( FindNextFileA(find, &found) );
    FindClose(find);
}
#else
static void
files_list_walk(
    struct FilesAnswer* answer,
    char const* root,
    char const* below,
    int depth)
{
    char path[TORIRS_IOITEM_MAX_PATH * 2];
    DIR* dir;
    struct dirent* entry;

    snprintf(path, sizeof(path), "%s%s%s", root, below[0] ? "/" : "", below);
    dir = opendir(path);
    if( !dir )
        return;
    while( (entry = readdir(dir)) != NULL )
    {
        char rel[TORIRS_IOITEM_MAX_PATH * 2];
        char full[TORIRS_IOITEM_MAX_PATH * 3];
        struct stat st;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(rel, sizeof(rel), "%s%s%s", below, below[0] ? "/" : "", entry->d_name);
        snprintf(full, sizeof(full), "%s/%s", root, rel);
        if( stat(full, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            if( depth < FILES_LIST_DEPTH_MAX )
                files_list_walk(answer, root, rel, depth + 1);
        }
        else if( S_ISREG(st.st_mode) && files_name_is_png(entry->d_name) )
            files_answer_add(answer, "%s\t%lld\t%lld\n", rel, (long long)st.st_mtime,
                             (long long)st.st_size);
    }
    closedir(dir);
}
#endif

/* Every directory in `dir`, made. The reveal's folder may not exist before
 * the first picture, and a file manager asked to show nothing shows an error. */
static void
files_mkdirs(char const* dir)
{
    char path[TORIRS_IOITEM_MAX_PATH];

    snprintf(path, sizeof(path), "%s/", dir);
    mkdir_parent(path);
}

/* A path handed to a shell between single quotes: every quote in it closed,
 * escaped and reopened. */
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(TORIRS_PLATFORM_ANDROID) && \
    !defined(TORIRS_PLATFORM_IOS)
static void
files_shell_quote(
    char const* in,
    char* out,
    size_t out_size)
{
    size_t used = 0;

    assert(out_size > 2);
    out[used++] = '\'';
    for( ; *in && used + 5 < out_size; in++ )
    {
        if( *in == '\'' )
        {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        }
        else
            out[used++] = *in;
    }
    out[used++] = '\'';
    out[used] = '\0';
}
#endif

/* `target` shown by the platform: a folder in its file manager, a picture in
 * its viewer. `dir` is made first only when the target IS the folder. */
static void
files_reveal(
    struct FilesAnswer* answer,
    char const* dir,
    char const* file)
{
    char const* const target = file[0] ? file : dir;

    if( !file[0] )
        files_mkdirs(dir);
    dir = target;
#if defined(_WIN32)
    {
        char full[MAX_PATH];
        INT_PTR shown;

        if( !GetFullPathNameA(dir, sizeof(full), full, NULL) )
            snprintf(full, sizeof(full), "%s", dir);
        for( char* c = full; *c; c++ )
            if( *c == '/' )
                *c = '\\';
        shown = (INT_PTR)ShellExecuteA(NULL, "open", full, NULL, NULL, SW_SHOWNORMAL);
        if( shown <= 32 )
            files_answer_add(answer, "error: Explorer would not open %s (%d)\n", full, (int)shown);
        else
            files_answer_add(answer, "ok\n");
    }
#elif defined(__ANDROID__) || defined(TORIRS_PLATFORM_ANDROID) || defined(TORIRS_PLATFORM_IOS)
    (void)dir;
    files_answer_add(answer, "unsupported: this device has no file manager to hand a folder to\n");
#else
    {
        char quoted[TORIRS_IOITEM_MAX_PATH * 4 + 8];
        char command[TORIRS_IOITEM_MAX_PATH * 4 + 64];
#if defined(__APPLE__)
        char const* opener = "open";
#else
        char const* opener = "xdg-open";
#endif
        files_shell_quote(dir, quoted, sizeof(quoted));
        /* Backgrounded and silenced: xdg-open may stay in the foreground for
         * as long as the file manager it started does. */
        snprintf(command, sizeof(command), "%s %s >/dev/null 2>&1 &", opener, quoted);
        if( system(command) != 0 )
            files_answer_add(answer, "error: %s could not be run\n", opener);
        else
            files_answer_add(answer, "ok\n");
    }
#endif
}

#if defined(_WIN32)
static int CALLBACK
files_pick_folder_start(
    HWND window,
    UINT message,
    LPARAM param,
    LPARAM data)
{
    (void)param;
    if( message == BFFM_INITIALIZED && data )
        SendMessageA(window, BFFM_SETSELECTIONA, TRUE, data);
    return 0;
}

/* SHBrowseForFolder rather than IFileOpenDialog: the win32 lane's floor is
 * XP, and the one dialog both lanes have is this one. */
static int
files_pick_folder_windows(
    char const* start,
    char* out,
    int out_size)
{
    BROWSEINFOA info;
    LPITEMIDLIST chosen;
    char path[MAX_PATH];
    int picked = 0;
    HRESULT const com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    memset(&info, 0, sizeof(info));
    info.hwndOwner = GetForegroundWindow();
    info.lpszTitle = "Choose the folder screenshots are saved in";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    info.lpfn = files_pick_folder_start;
    info.lParam = (LPARAM)(start && start[0] ? start : NULL);
    chosen = SHBrowseForFolderA(&info);
    if( chosen )
    {
        if( SHGetPathFromIDListA(chosen, path) )
        {
            snprintf(out, (size_t)out_size, "%s", path);
            picked = 1;
        }
        CoTaskMemFree(chosen);
    }
    if( SUCCEEDED(com) )
        CoUninitialize();
    return picked;
}
#elif !defined(__APPLE__) && !defined(__ANDROID__) && !defined(TORIRS_PLATFORM_ANDROID) && \
    !defined(TORIRS_PLATFORM_IOS)
/* zenity, then kdialog: whichever desktop this is has one of them, and a
 * client has no toolkit of its own to draw a chooser with. -1 when neither
 * could be run. */
static int
files_pick_folder_unix(
    char const* start,
    char* out,
    int out_size)
{
    static char const* const pickers[] = {
        "zenity --file-selection --directory --title='Choose the folder screenshots are saved in' "
        "--filename=%s/ 2>/dev/null",
        "kdialog --getexistingdirectory %s 2>/dev/null",
    };
    char quoted[TORIRS_IOITEM_MAX_PATH * 4 + 8];

    files_shell_quote(start && start[0] ? start : ".", quoted, sizeof(quoted));
    for( size_t i = 0; i < sizeof(pickers) / sizeof(pickers[0]); i++ )
    {
        char command[TORIRS_IOITEM_MAX_PATH * 4 + 160];
        char line[TORIRS_IOITEM_MAX_PATH] = { 0 };
        FILE* pipe;
        int status;

        snprintf(command, sizeof(command), pickers[i], quoted);
        pipe = popen(command, "r");
        if( !pipe )
            continue;
        if( !fgets(line, sizeof(line), pipe) )
            line[0] = '\0';
        status = pclose(pipe);
        /* 127: the shell found no such program, so ask the next one. Any other
         * status is the player's answer, and an empty line is a cancel. */
        if( WIFEXITED(status) && WEXITSTATUS(status) == 127 )
            continue;
        line[strcspn(line, "\r\n")] = '\0';
        if( !line[0] )
            return 0;
        snprintf(out, (size_t)out_size, "%s", line);
        return 1;
    }
    return -1;
}
#endif

static void
files_pick_folder(
    struct FilesAnswer* answer,
    char const* start)
{
    char chosen[TORIRS_IOITEM_MAX_PATH] = { 0 };
    int picked;

    if( g_folder_picker )
        picked = g_folder_picker(start, chosen, (int)sizeof(chosen));
    else
    {
#if defined(_WIN32)
        picked = files_pick_folder_windows(start, chosen, (int)sizeof(chosen));
#elif !defined(__APPLE__) && !defined(__ANDROID__) && !defined(TORIRS_PLATFORM_ANDROID) && \
    !defined(TORIRS_PLATFORM_IOS)
        picked = files_pick_folder_unix(start, chosen, (int)sizeof(chosen));
#else
        (void)start;
        picked = -1;
#endif
    }
    if( picked < 0 )
        files_answer_add(answer, "unsupported: this client has no folder chooser here%s\n",
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__) || defined(TORIRS_PLATFORM_ANDROID)
                         ""
#else
                         " (install zenity or kdialog)"
#endif
        );
    else if( picked == 0 || !chosen[0] )
        files_answer_add(answer, "cancelled\n");
    else
    {
        /* One spelling of a path on every lane: the destination is checked
         * against [A-Za-z0-9._- /:], and a backslash is not in it. */
        for( char* c = chosen; *c; c++ )
            if( *c == '\\' )
                *c = '/';
        files_answer_add(answer, "ok\n%s\n", chosen);
    }
}

static int
answer_files_item(struct ToriRS_IOItem* item)
{
    char const* verb = item->u.script.path + sizeof(PLATFORM_X_IO_FILES_PREFIX) - 1;
    char const* body = (char const*)item->data;
    int const body_size = item->data_size;
    char value[TORIRS_IOITEM_MAX_PATH];
    struct FilesAnswer answer = { 0 };

    assert(body_size >= 0);
    if( body_size > 0 )
        assert(body);
    if( strcmp(verb, "list") == 0 )
    {
        files_body_value(body, body_size, "dir", value, sizeof(value));
        files_answer_add(&answer, "ok\n");
        if( value[0] )
            files_list_walk(&answer, value, "", 0);
    }
    else if( strcmp(verb, "reveal") == 0 )
    {
        char file[TORIRS_IOITEM_MAX_PATH];

        files_body_value(body, body_size, "dir", value, sizeof(value));
        files_body_value(body, body_size, "path", file, sizeof(file));
        if( value[0] )
            files_reveal(&answer, value, file);
        else
            files_answer_add(&answer, "error: files/reveal names no dir=\n");
    }
    else if( strcmp(verb, "pick_folder") == 0 )
    {
        files_body_value(body, body_size, "start", value, sizeof(value));
        files_pick_folder(&answer, value);
    }
    else if( strcmp(verb, "download") == 0 )
        files_answer_add(&answer, "unsupported: a desktop keeps them in a folder; reveal it\n");
    else
        files_answer_add(&answer, "error: no files/%s\n", verb);

    item->data = answer.text;
    item->data_size = answer.size;
    item->error_code = 0;
    return 0;
}

/*
 * THE ACCOUNT SERVER'S ITEMS (game/task_accounts_io.h).
 *
 * A SCRIPT item whose path is `auth/<endpoint>`, lending a form body exactly
 * as a launch item does, answered with the response's JSON in place of the
 * body. The browser posts these to its own origin and the gate's cookie rides
 * along by itself; a desktop client has no origin and no cookie jar, so both
 * are named in the environment:
 *
 *   TORIRS_AUTH_URL      http://host:port of io_server with --gate
 *   TORIRS_GATE_COOKIE   the gate's ticket (torirs_gate=... or just the value)
 *
 * Without TORIRS_AUTH_URL the item is refused, which the sign-in reports as
 * "unable to reach the login server" -- this client has no account server to
 * ask. Plain http only: the desktop transport has no TLS, so this is for a
 * gate on the LAN, never the public one.
 */
static int
item_is_auth(struct ToriRS_IOItem const* item)
{
    return item->kind == TORIRS_IOK_SCRIPT &&
           strncmp(item->u.script.path, PLATFORM_X_IO_AUTH_PREFIX,
                   sizeof(PLATFORM_X_IO_AUTH_PREFIX) - 1) == 0;
}

static int
answer_auth_item(struct ToriRS_IOItem* item)
{
    char const* endpoint = item->u.script.path + sizeof(PLATFORM_X_IO_AUTH_PREFIX) - 1;
    char const* base = getenv("TORIRS_AUTH_URL");
    char const* cookie_env = getenv("TORIRS_GATE_COOKIE");
    char const* route = NULL;
    char host[256];
    char cookie[512];
    int port = 80;
    int status = 0;
    int size = 0;
    char* answer;

    assert(item->data_size >= 0);
    if( strcmp(endpoint, "token") == 0 )
        route = "/oauth/token";
    else if( strcmp(endpoint, "play") == 0 )
        route = "/game/play";
    else if( strcmp(endpoint, "revoke") == 0 )
        route = "/oauth/revoke";
    /* The gate session's own player accounts (io_gate.h /account). */
    else if( strcmp(endpoint, "user") == 0 )
        route = "/account/players";
    else if( strcmp(endpoint, "link") == 0 )
        route = "/account/link";
    else if( strcmp(endpoint, "userplay") == 0 )
        route = "/account/play";
    if( !route || !base || strncmp(base, "http://", 7) != 0 )
    {
        TORIRS_LOG("io: no account server for '%s' (set TORIRS_AUTH_URL=http://host:port)\n",
                   item->u.script.path);
        item->data = NULL;
        item->data_size = 0;
        item->error_code = -1;
        return -1;
    }
    snprintf(host, sizeof(host), "%s", base + 7);
    {
        char* colon = strchr(host, ':');
        char* slash = strchr(host, '/');

        if( slash )
            *slash = '\0';
        if( colon )
        {
            *colon = '\0';
            port = atoi(colon + 1);
        }
    }
    cookie[0] = '\0';
    if( cookie_env && cookie_env[0] )
        snprintf(cookie, sizeof(cookie), "%s%s", strchr(cookie_env, '=') ? "" : "torirs_gate=",
                 cookie_env);
    answer = PlatformX_HttpPostForm(host, port, route, cookie[0] ? cookie : NULL,
                                    (char const*)item->data, item->data_size, &size, &status);
    /* The body was lent; the answer replaces it or nothing does. */
    item->data = answer;
    item->data_size = answer ? size : 0;
    item->error_code = answer ? 0 : -1;
    return answer ? 0 : -1;
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
 * Both remote backings, now: JS5 pulls a group over several frames, and the
 * on-demand wire is a pipeline the executor puts a whole pass of reads on and
 * polls (it used to block per file, which made a region rebuild a line of
 * round trips on the frame thread). Asking this instead of testing for a
 * backing by hand is what lets Process talk about sources.
 */
/* Only compiled where a source can park. A build with neither remote backing
 * has no such source, so Process never asks and the question would be dead
 * code. */
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5) || !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
static int
cache_source_parks(enum CacheSource source)
{
    /* TORIRS_OD_BLOCKING=1 puts the on-demand wire back to one blocking read
     * per file inside LoadItem. The A/B the parked wire was measured with,
     * and the first thing to try if a dat1 world ever misbehaves only when
     * streamed. */
    static int od_blocking = -1;
    if( od_blocking < 0 )
        od_blocking = getenv("TORIRS_OD_BLOCKING") != NULL;
    if( source == CACHE_SOURCE_ON_DEMAND )
        return !od_blocking;
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
            RSCache_Dat2DiskArchiveRetain(cached);
            archive = cached;
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

    /* The cache takes the archive as one holder and the task is another. */
    dat2_archive_cache_put(px, archive);
    RSCache_Dat2DiskArchiveRetain(archive);

    item->data = archive;
    item->data_size = sizeof(struct RSCache_Dat2DiskArchive);
    item->error_code = 0;
    return 0;
}

/* A free row in the parked-read table, growing it when there is none. */
static struct RemotePendingItem*
remote_pending_alloc(struct PlatformX_IO* px)
{
    for( int i = 0; i < px->pending_count; i++ )
        if( !px->pending[i].in_use )
            return &px->pending[i];
    if( px->pending_count == px->pending_cap )
    {
        px->pending_cap = px->pending_cap ? px->pending_cap * 2 : 64;
        px->pending = ToriMem_Realloc(
            ToriMem_Net(), px->pending, (size_t)px->pending_cap * sizeof(*px->pending));
    }
    memset(&px->pending[px->pending_count], 0, sizeof(*px->pending));
    return &px->pending[px->pending_count++];
}

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
static int
js5_queue_cache_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int archive = dat2_resolve_table(px, item->u.cache.table_id);
    int group = item->u.cache.archive_id;
    enum Js5RequestResult request;
    struct RemotePendingItem* pending;

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

    pending = remote_pending_alloc(px);
    pending->in_use = 1;
    pending->source = REMOTE_SOURCE_JS5;
    pending->item = item;
    pending->archive = archive;
    pending->group = group;
    item->pending++;
    return 0;
}

/*
 * A PREFETCH item on the JS5 wire: every group requested, one parked row per
 * group still to come. Answered NOW when nothing had to be asked for.
 *
 * `data_size` is left as the count the caller stated; on this lane a group
 * the server refuses is one the disk simply never gains, and the caller's
 * contract (asyncio.h) is that only a dead transport answers -1.
 */
static int
js5_queue_prefetch_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int archive = dat2_resolve_table(px, item->u.cache.table_id);
    int const* ids = item->data;
    int count = item->u.cache.archive_id;

    assert(ids);
    assert(count > 0);
    item->error_code = 0;
    item->data_size = count;
    if( archive == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        item->data_size = 0;
        return 0;
    }
    for( int i = 0; i < count; i++ )
    {
        enum Js5RequestResult request =
            PlatformXIOJs5Cache_RequestGroup(px->js5, archive, ids[i]);
        struct RemotePendingItem* pending;

        if( request != JS5_REQUEST_QUEUED )
            continue;
        pending = remote_pending_alloc(px);
        pending->in_use = 1;
        pending->source = REMOTE_SOURCE_JS5_PREFETCH;
        pending->item = item;
        pending->archive = archive;
        pending->group = ids[i];
        item->pending++;
    }
    return 0;
}

static void
js5_service_pending(
    struct PlatformX_IO* px,
    int terminal_failure)
{
    for( int i = 0; i < px->pending_count; i++ )
    {
        struct RemotePendingItem* pending = &px->pending[i];
        struct ToriRS_IOItem* item;
        int group_failed;

        if( !pending->in_use ||
            (pending->source != REMOTE_SOURCE_JS5 &&
             pending->source != REMOTE_SOURCE_JS5_PREFETCH) )
            continue;
        group_failed = PlatformXIOJs5Cache_GroupFailed(
            px->js5, pending->archive, pending->group);
        if( !terminal_failure && !group_failed && !PlatformXIOJs5Cache_GroupReady(
                                                        px->js5,
                                                        pending->archive,
                                                        pending->group) )
            continue;

        item = pending->item;
        if( pending->source == REMOTE_SOURCE_JS5_PREFETCH )
        {
            /* Nothing to load: the group is in the disk now, or refused,
             * and either way the row is what held the item. */
            if( terminal_failure )
                item->error_code = -1;
        }
        else if( terminal_failure || group_failed )
            item->error_code = -1;
        else
            load_cache_item_dat2(px, item);
        assert(item->pending > 0);
        item->pending--;
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

#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
/*
 * Put a dat1 read on the on-demand wire, or answer it now.
 *
 * The address is resolved here, once, exactly as the blocking load resolves
 * it (a map read names a SQUARE, and only the versionlist knows the archive).
 * A hydration-cache hit and a jag archive are answered inside Begin; the
 * rest is a row in the parked table until the pump says it has landed.
 */
static int
od_queue_cache_item(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    int table_id = item->u.cache.table_id;
    int archive_id = item->u.cache.archive_id;
    int flags = item->u.cache.flags;
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct RemotePendingItem* pending;

    if( flags == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN || flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY )
    {
        archive_id = dat1_map_archive_id(
            px, archive_id, flags == TORIRS_IO_CACHE_DAT1_MAP_SCENERY, CACHE_SOURCE_ON_DEMAND);
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

    if( PlatformXIOOnDemand_ArchiveLoadBegin(px->dat1_on_demand, table_id, archive_id, &archive) )
    {
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

    pending = remote_pending_alloc(px);
    pending->in_use = 1;
    pending->source = REMOTE_SOURCE_ON_DEMAND;
    pending->item = item;
    pending->archive = table_id;
    pending->group = archive_id;
    item->pending++;
    return 0;
}

/* Move the wire, then answer every parked dat1 read that has landed. */
static void
od_service_pending(struct PlatformX_IO* px)
{
    if( !px->dat1_on_demand )
        return;
    PlatformXIOOnDemand_Pump(px->dat1_on_demand);
    for( int i = 0; i < px->pending_count; i++ )
    {
        struct RemotePendingItem* pending = &px->pending[i];
        struct ToriRS_IOItem* item;
        struct RSCache_Dat1DiskArchive* archive = NULL;

        if( !pending->in_use || pending->source != REMOTE_SOURCE_ON_DEMAND )
            continue;
        if( !PlatformXIOOnDemand_ArchiveLoadPoll(
                px->dat1_on_demand, pending->archive, pending->group, &archive) )
            continue;
        item = pending->item;
        if( archive )
        {
            item->data = archive;
            item->data_size = sizeof(struct RSCache_Dat1DiskArchive);
            item->error_code = 0;
        }
        else
            item->error_code = -1;
        assert(item->pending > 0);
        item->pending--;
        memset(pending, 0, sizeof(*pending));
    }
}
#endif

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
    /* Before the clear too: a launch item lends its request body the same way
     * (the launch service's banner above). */
    if( item_is_launch(item) )
        return answer_launch_item(item);
    if( item_is_auth(item) )
        return answer_auth_item(item);
    if( item_is_files(item) )
        return answer_files_item(item);
    /* A prefetch on a source that answers inside LoadItem is already
     * resident by definition -- the disk is the store -- so every group
     * "landed" without a read. The lent id array stays the caller's. */
    if( item->kind == TORIRS_IOK_CACHE_PREFETCH )
    {
        item->error_code = 0;
        item->data_size = item->u.cache.archive_id;
        return 0;
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

/*
 * Local hits never park: a disk read is answered inside Process. Only a
 * remote miss leaves an item pending, and this is where such an item is
 * answered -- the JS5 client and the on-demand proxy get a turn, and every
 * row whose group has come back fills its item and lets its count down.
 */
void
PlatformX_IO_Pump(struct PlatformX_IO* px)
{
    assert(px);
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
    if( px->js5 )
        PlatformXIO_Js5Pump(px, PlatformWindow_Ticks64());
#endif
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    od_service_pending(px);
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
    struct ToriRS_IOBatch* io)
{
    assert(px);
    assert(io);

    int processed = 0;
    for( int i = 0; i < io->active_count; i++ )
    {
        struct ToriRS_IOItem* item = io->active[i];

        /* A write carries its payload in these two fields — see LoadItem —
         * a prefetch lends its id array the same way, and a launch or an
         * account-server item its request body. */
        if( item->kind != TORIRS_IOK_FILE_WRITE && item->kind != TORIRS_IOK_CACHE_PREFETCH &&
            !item_is_launch(item) && !item_is_auth(item) )
        {
            item->data = NULL;
            item->data_size = 0;
        }
        item->error_code = 0;

#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5) || !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
        /* A source that parks is the only reason this loop does anything other
         * than call LoadItem. Asked as a question about the SOURCE, and both
         * remote backings answer yes: each is put on its wire here, its item
         * marked pending, and answered by Pump once it has landed. */
        if( (item->kind == TORIRS_IOK_CACHE || item->kind == TORIRS_IOK_CACHE_PREFETCH) &&
            cache_source_parks(cache_source_for(px, item)) )
        {
            int queued = -1;
#if !defined(TORIRS_PLATFORM_X_IO_NO_JS5)
            if( cache_source_for(px, item) == CACHE_SOURCE_JS5 )
                queued = item->kind == TORIRS_IOK_CACHE_PREFETCH
                             ? js5_queue_prefetch_item(px, item)
                             : js5_queue_cache_item(px, item);
#endif
#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
            if( cache_source_for(px, item) == CACHE_SOURCE_ON_DEMAND )
            {
                /* No dat1 prefetch: that era's preload has its own wire.
                 * Answered as "all landed" rather than refused, so a profile
                 * that ever asks is merely uncached, not broken. */
                if( item->kind == TORIRS_IOK_CACHE_PREFETCH )
                {
                    item->data_size = item->u.cache.archive_id;
                    queued = 0;
                }
                else
                    queued = od_queue_cache_item(px, item);
            }
#endif
            if( queued == 0 )
                processed++;
        }
        else
#endif
            if( PlatformX_IO_LoadItem(px, item) == 0 )
            processed++;
    }

    ToriRS_IOBatch_Reset(io);

#if !defined(TORIRS_PLATFORM_X_IO_NO_ONDEMAND)
    /* The pass's requests go out now rather than on the next Pump, so a
     * local server's answers are already back when the runner next asks. */
    od_service_pending(px);
#endif

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

int
PlatformXIO_Dat1OnDemandContainerFetchMany(
    struct PlatformX_IO* px,
    int count,
    const int* table_ids,
    const int* archive_ids,
    const int* flags,
    uint8_t** out_data,
    int* out_sizes,
    int* out_formats)
{
    int* resolved;
    int served;

    assert(px);
    assert(count >= 0);
    assert(table_ids);
    assert(archive_ids);
    assert(flags);
    assert(out_data);
    assert(out_sizes);
    assert(out_formats);

    for( int i = 0; i < count; i++ )
    {
        out_data[i] = NULL;
        out_sizes[i] = 0;
        out_formats[i] = 0;
    }
    if( !px->dat1_on_demand )
        return 0;

    /* The same square -> archive resolution as the single fetch, done for
     * every entry before any goes on the wire; a square the world does not
     * ship becomes an id no server has, which the wire answers as absent. */
    resolved = ToriMem_Alloc(ToriMem_Net(), (size_t)(count > 0 ? count : 1) * sizeof(int));
    for( int i = 0; i < count; i++ )
    {
        resolved[i] = archive_ids[i];
        if( flags[i] == TORIRS_IO_CACHE_DAT1_MAP_TERRAIN ||
            flags[i] == TORIRS_IO_CACHE_DAT1_MAP_SCENERY )
            resolved[i] = dat1_map_archive_id(
                px, archive_ids[i], flags[i] == TORIRS_IO_CACHE_DAT1_MAP_SCENERY,
                CACHE_SOURCE_ON_DEMAND);
    }
    served = PlatformXIOOnDemand_ContainerFetchMany(
        px->dat1_on_demand, count, table_ids, resolved, out_data, out_sizes, out_formats);
    ToriMem_Free(ToriMem_Net(), resolved);
    return served;
}
#endif
