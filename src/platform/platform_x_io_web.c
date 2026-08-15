/*
 * Asynchronous IO backend for the browser.
 *
 * The native backend answers an item by reading the cache off disk inside
 * PlatformX_IO_Process. A browser cannot: there is no disk, and the one channel
 * that can reach the bytes (fetch) does not return them until some later turn of
 * the JS event loop. So this backend *starts* reads and reports what is still
 * outstanding, and PlatformX_IO_Pending keeps the scheduler from resuming a task
 * whose slot has not been filled yet. Nothing above the platform layer changes:
 * a task still queues, yields, and finds an archive in its slot when it wakes.
 *
 * The shape of one read, end to end:
 *
 *   Process()             encode the item into the outgoing batch, remember
 *                         (req_id -> io, slot), return
 *   [next frame] JS pump  copies the batch out of wasm memory, POSTs it to the
 *                         IO server, and eventually calls back in
 *   response_submit()     decode, fill the slots, drop the pending records
 *   TaskRunner_Step       Pending() now says 0, the task resumes
 *
 * Batching is what makes that affordable. Everything a task queued before it
 * yielded goes out in one request, so a frame costs one round trip rather than
 * one per archive.
 *
 * The response cache is the other half of that. A dat2 config group is fetched
 * once per id it contains — the obj group was requested 219 times in one boot —
 * and each of those would otherwise be a round trip, which on this design is a
 * whole frame. Cached entries are kept as the encoded response record and
 * re-applied through the same decoder, so a hit and a miss build the identical
 * item and there is no second materialization path to keep in step.
 */

#include "platform/platform_x_io.h"

#include "platform/io_wire.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform_x_io_web.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
/* Building this file natively is useful for testing the queueing logic in a
 * plain debugger; the JS pump simply never runs and nothing completes. */
#define EM_JS(ret, name, args, body)                                                               \
    static ret name args                                                                           \
    {                                                                                              \
    }
#define EMSCRIPTEN_KEEPALIVE
#endif

/* Two ToriRS_IO instances of TORIRS_IO_MAX_ITEMS slots each is the most that
 * can be outstanding, because a runner never queues more before it yields. */
#define WEB_IO_MAX_PENDING (TORIRS_IO_MAX_ITEMS * 2)

#define WEB_IO_CACHE_SLOTS 1024
/* Bytes of encoded response kept resident. Enough to hold the group archives a
 * boot re-reads (the obj config group alone was requested 219 times in one
 * native boot, and each of those is a round trip here), and small enough that
 * it is not itself a reason the heap grows. */
#define WEB_IO_CACHE_BUDGET (32 * 1024 * 1024)

struct WebCacheKey
{
    int32_t kind;
    int32_t table_id;
    int32_t archive_id;
    int32_t flags;
};

struct WebPending
{
    int in_use;
    uint32_t req_id;
    struct ToriRS_IO* io;
    int slot;
    struct WebCacheKey key;
    int cacheable;
};

struct WebCacheEntry
{
    int used;
    struct WebCacheKey key;
    /* One encoded response record, exactly as it arrived. */
    uint8_t* record;
    int record_len;
    uint64_t last_used;
};

struct PlatformX_IO
{
    /* Kept only so a misconfigured boot is diagnosable; the server owns the
     * real ones and every path resolution happens there. */
    char* config_dir;
    char* script_dir;

    /* Which cache these requests are about. Sent in every batch header so one
     * server can answer clients booting different generations. (Not to be
     * confused with `cache` below, which is the response cache.) */
    struct IOWireCache cache_id;

    struct IOWireBuf out;
    int out_count;

    struct WebPending pending[WEB_IO_MAX_PENDING];
    int pending_count;
    uint32_t next_req_id;

    struct WebCacheEntry cache[WEB_IO_CACHE_SLOTS];
    uint64_t cache_clock;
    int64_t cache_bytes;

    /* Counters, surfaced to the page through torirs_io_stats. */
    int stat_requests;
    int stat_cache_hits;
    int stat_completed;
    int stat_failed;
};

/*
 * The browser calls in through plain exported functions, which have no place to
 * carry a context pointer, so the backend is a singleton. That is not a
 * compromise: App owns exactly one PlatformX_IO and shares it between both task
 * pipelines, so a second instance would already be a bug.
 */
static struct PlatformX_IO* g_web_io = NULL;

/*
 * May a read block the frame that issued it?
 *
 * Boot wants yes: it is a serial chain of several hundred archives, and a
 * non-blocking read costs it a whole turn of the event loop each, so the
 * blocking pump is the difference between a boot measured in seconds and one
 * measured in minutes. Nothing is on screen to stutter yet.
 *
 * A live client wants no. A synchronous XMLHttpRequest freezes the main thread
 * for far longer than the request takes -- in a trace of this client the nine
 * blocking reads cost 39.9ms of frozen main thread, worst single block 17.0ms,
 * against a measured HTTP round trip of at most 3.55ms. One of those is a
 * dropped frame however fast the server answers, and they land exactly when
 * something new happens: the first time an npc's hit sound is played, the id is
 * not resident, and the fetch stalls the frame the hitsplat appears on.
 *
 * The asynchronous path is not a fallback here, it is the other half of the
 * same design: Pending() parks the task either way, and the frame loop already
 * switches its pacing to the event loop while reads are outstanding, so a
 * backlog still drains at event-loop rate rather than display rate. This is the
 * same configuration the page's `?io_sync=0` selects, applied to the half of
 * the session that has frames to protect.
 *
 * Default 1 so a host that never calls the setter behaves as it always did.
 */
static int g_web_io_blocking_reads = 1;

EM_JS(void, torirs_web_io_pump_js, (void), {
    if( Module.torirsIO && Module.torirsIO.pump )
        Module.torirsIO.pump();
});

/*
 * Synchronous round trip, called from inside Process below. Returns 1 when the
 * page took the batch and answered it before returning — the slots are already
 * filled by the time this comes back, so Pending() is 0 and the parked task
 * resumes on this same scheduler pass rather than next frame.
 *
 * Re-entrant into wasm: the page calls torirs_io_response_submit from within
 * this call. That is why Process encodes every request and clears the active
 * list BEFORE pumping — nothing here may run while that list is being walked.
 */
EM_JS(int, torirs_web_io_pump_sync_js, (void), {
    if( Module.torirsIO && Module.torirsIO.pumpSync )
        return Module.torirsIO.pumpSync() ? 1 : 0;
    return 0;
});

void
PlatformXIO_Web_Pump(void)
{
    torirs_web_io_pump_js();
}

void
PlatformXIO_Web_SetBlockingReads(int allowed)
{
    g_web_io_blocking_reads = allowed ? 1 : 0;
}

int
PlatformXIO_Web_PendingTotal(void)
{
    return g_web_io ? g_web_io->pending_count : 0;
}

struct PlatformX_IO*
PlatformX_IO_New(void)
{
    struct PlatformX_IO* px = calloc(1, sizeof(struct PlatformX_IO));
    assert(px);
    IOWireBuf_Init(&px->out);
    px->next_req_id = 1;
    g_web_io = px;
    return px;
}

void
PlatformX_IO_InitDat2Disk(
    struct PlatformX_IO* px,
    struct RSCache_Dat2Disk* disk)
{
    /* There is no local disk in the browser. App_Init does not call this under
     * TORIRS_PLATFORM_WEB; the definition exists so the header stays one
     * interface rather than two. */
    (void)px;
    (void)disk;
}

void
PlatformX_IO_InitDat1Disk(
    struct PlatformX_IO* px,
    struct RSCache_Dat1Disk* disk)
{
    (void)px;
    (void)disk;
}

void
PlatformX_IO_InitCacheId(
    struct PlatformX_IO* px,
    int epoch,
    int game,
    int revision,
    unsigned int quirks,
    const char* dir)
{
    assert(px);
    px->cache_id.epoch = (int32_t)epoch;
    px->cache_id.game = (int32_t)game;
    px->cache_id.revision = (int32_t)revision;
    px->cache_id.quirks = (uint32_t)quirks;
    snprintf(px->cache_id.dir, sizeof(px->cache_id.dir), "%s", dir ? dir : "");
}

void
PlatformX_IO_InitConfigPath(
    struct PlatformX_IO* px,
    const char* config_path)
{
    assert(px);
    free(px->config_dir);
    px->config_dir = config_path ? strdup(config_path) : NULL;
}

void
PlatformX_IO_InitScriptPath(
    struct PlatformX_IO* px,
    const char* script_path)
{
    assert(px);
    free(px->script_dir);
    px->script_dir = script_path ? strdup(script_path) : NULL;
}

void
PlatformX_IO_Free(struct PlatformX_IO* px)
{
    if( !px )
        return;
    for( int i = 0; i < WEB_IO_CACHE_SLOTS; i++ )
        free(px->cache[i].record);
    IOWireBuf_Free(&px->out);
    free(px->config_dir);
    free(px->script_dir);
    if( g_web_io == px )
        g_web_io = NULL;
    free(px);
}

/* --- response cache ------------------------------------------------------ */

static int
cache_key_eq(
    struct WebCacheKey const* a,
    struct WebCacheKey const* b)
{
    return a->kind == b->kind && a->table_id == b->table_id &&
           a->archive_id == b->archive_id && a->flags == b->flags;
}

/* Fills `out` and returns 1 when this item's answer is worth remembering.
 * Config/script reads are one-shot boot files and are not cached. */
static int
cache_key_for(
    struct ToriRS_IOItem const* item,
    struct WebCacheKey* out)
{
    memset(out, 0, sizeof(*out));
    out->kind = (int32_t)item->kind;
    if( item->kind == TORIRS_IOK_CACHE )
    {
        out->table_id = item->u.cache.table_id;
        out->archive_id = item->u.cache.archive_id;
        out->flags = item->u.cache.flags;
        return 1;
    }
    if( item->kind == TORIRS_IOK_REFERENCE_TABLE )
    {
        out->table_id = item->u.reference_table.table_id;
        return 1;
    }
    return 0;
}

static struct WebCacheEntry*
cache_find(
    struct PlatformX_IO* px,
    struct WebCacheKey const* key)
{
    for( int i = 0; i < WEB_IO_CACHE_SLOTS; i++ )
    {
        struct WebCacheEntry* entry = &px->cache[i];
        if( entry->used && cache_key_eq(&entry->key, key) )
        {
            entry->last_used = ++px->cache_clock;
            return entry;
        }
    }
    return NULL;
}

static void
cache_evict_one(struct PlatformX_IO* px)
{
    struct WebCacheEntry* lru = NULL;
    for( int i = 0; i < WEB_IO_CACHE_SLOTS; i++ )
    {
        struct WebCacheEntry* entry = &px->cache[i];
        if( !entry->used )
            continue;
        if( !lru || entry->last_used < lru->last_used )
            lru = entry;
    }
    if( !lru )
        return;
    px->cache_bytes -= lru->record_len;
    free(lru->record);
    memset(lru, 0, sizeof(*lru));
}

static void
cache_put(
    struct PlatformX_IO* px,
    struct WebCacheKey const* key,
    uint8_t const* record,
    int record_len)
{
    struct WebCacheEntry* slot = NULL;
    uint8_t* copy;

    if( record_len <= 0 )
        return;
    /* A single entry larger than the whole budget would evict everything and
     * still not fit; skip it rather than thrash. */
    if( record_len > WEB_IO_CACHE_BUDGET )
        return;

    while( px->cache_bytes + record_len > WEB_IO_CACHE_BUDGET )
        cache_evict_one(px);

    for( int i = 0; i < WEB_IO_CACHE_SLOTS; i++ )
    {
        if( !px->cache[i].used )
        {
            slot = &px->cache[i];
            break;
        }
    }
    if( !slot )
    {
        cache_evict_one(px);
        for( int i = 0; i < WEB_IO_CACHE_SLOTS && !slot; i++ )
            if( !px->cache[i].used )
                slot = &px->cache[i];
        if( !slot )
            return;
    }

    copy = malloc((size_t)record_len);
    if( !copy )
        return;
    memcpy(copy, record, (size_t)record_len);

    slot->used = 1;
    slot->key = *key;
    slot->record = copy;
    slot->record_len = record_len;
    slot->last_used = ++px->cache_clock;
    px->cache_bytes += record_len;
}

/* Re-apply a stored record. Returns 1 when the item was filled. */
static int
cache_serve(
    struct PlatformX_IO* px,
    struct WebCacheKey const* key,
    struct ToriRS_IOItem* item)
{
    struct WebCacheEntry* entry = cache_find(px, key);
    struct IOWireReader reader;
    struct IOWireResponse resp;

    if( !entry )
        return 0;
    IOWireReader_Init(&reader, entry->record, entry->record_len);
    if( IOWire_ReadResponse(&reader, &resp) != 0 )
        return 0;
    if( IOWire_ApplyResponse(&resp, item) != 0 )
        return 0;
    px->stat_cache_hits++;
    return 1;
}

/* --- request queueing ---------------------------------------------------- */

static struct WebPending*
pending_alloc(struct PlatformX_IO* px)
{
    for( int i = 0; i < WEB_IO_MAX_PENDING; i++ )
        if( !px->pending[i].in_use )
            return &px->pending[i];
    return NULL;
}

int
PlatformX_IO_Pending(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io)
{
    int count = 0;
    assert(px);
    for( int i = 0; i < WEB_IO_MAX_PENDING; i++ )
        if( px->pending[i].in_use && px->pending[i].io == io )
            count++;
    return count;
}

/* Synchronous single-item load has no meaning here — there is nothing to read
 * from. Kept because the header declares it; it reports failure rather than
 * pretending, so a caller that reaches for it fails loudly instead of silently
 * receiving an empty archive. */
int
PlatformX_IO_LoadItem(
    struct PlatformX_IO* px,
    struct ToriRS_IOItem* item)
{
    (void)px;
    assert(item);
    fprintf(stderr, "web io: synchronous LoadItem is not available in the browser\n");
    item->data = NULL;
    item->data_size = 0;
    item->error_code = -1;
    return -1;
}

/* Read or write a client-owned file against the browser's own filesystem.
 * Same contract as the native backend's two handlers, including the borrowed
 * write payload. Returns 0 when the item was answered. */
static int
web_client_file(struct ToriRS_IOItem* item)
{
    FILE* fp;

    if( item->kind == TORIRS_IOK_FILE_WRITE )
    {
        item->error_code = 0;
        fp = fopen(item->u.file.path, "wb");
        if( !fp )
        {
            item->error_code = -1;
            return -1;
        }
        if( item->data_size > 0 &&
            fwrite(item->data, 1, (size_t)item->data_size, fp) != (size_t)item->data_size )
            item->error_code = -1;
        fclose(fp);
        return item->error_code;
    }

    item->data = NULL;
    item->data_size = 0;
    item->error_code = 0;
    fp = fopen(item->u.file.path, "rb");
    if( !fp )
    {
        item->error_code = -1;
        return -1;
    }
    {
        long size;
        void* data;

        if( fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
            fseek(fp, 0, SEEK_SET) != 0 )
        {
            fclose(fp);
            item->error_code = -1;
            return -1;
        }
        data = malloc((size_t)size > 0 ? (size_t)size : 1);
        if( !data || (size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size) )
        {
            free(data);
            fclose(fp);
            item->error_code = -1;
            return -1;
        }
        fclose(fp);
        item->data = data;
        item->data_size = (int)size;
    }
    return 0;
}

int
PlatformX_IO_Process(
    struct PlatformX_IO* px,
    struct ToriRS_IO* io)
{
    int served = 0;

    assert(px);
    assert(io);

    for( int i = 0; i < io->active_count; i++ )
    {
        int slot_id = io->active[i];
        struct ToriRS_IOItem* item = &io->io_slots[slot_id];
        struct WebCacheKey key;
        int cacheable = cache_key_for(item, &key);
        struct WebPending* pending;

        /*
         * Client-owned files never cross the wire.
         *
         * They are the player's settings, and they are device-local by
         * definition: sending them to the IO server would put one shared copy
         * on a machine that may be answering several browsers, and read them
         * back into a client that never chose them. The browser's own
         * filesystem is where a browser's device settings belong, so this
         * backend serves them itself — Emscripten's FS is synchronous, so they
         * are answered before Process returns, exactly like a cache hit.
         *
         * Persistence beyond the tab is the page's business: a MEMFS-only
         * build simply forgets them, the same way it forgets everything else.
         */
        if( item->kind == TORIRS_IOK_FILE_READ || item->kind == TORIRS_IOK_FILE_WRITE )
        {
            if( web_client_file(item) == 0 )
                served++;
            continue;
        }

        item->data = NULL;
        item->data_size = 0;
        item->error_code = 0;

        if( cacheable && cache_serve(px, &key, item) )
        {
            served++;
            continue;
        }

        pending = pending_alloc(px);
        if( !pending )
        {
            /* Cannot happen with the slot arithmetic above, but a silently
             * dropped request would hang the boot forever, so it fails the
             * item instead. */
            fprintf(stderr, "web io: pending table full, failing request\n");
            item->error_code = -1;
            continue;
        }

        if( px->out_count == 0 )
            IOWire_BatchBegin(&px->out, &px->cache_id);

        pending->in_use = 1;
        pending->req_id = px->next_req_id++;
        pending->io = io;
        pending->slot = slot_id;
        pending->key = key;
        pending->cacheable = cacheable;
        px->pending_count++;

        IOWire_WriteRequest(&px->out, pending->req_id, item);
        px->out_count++;
        px->stat_requests++;
    }

    ToriRS_IO_ResetActive(io);

    /*
     * Hand the batch over now, not next frame.
     *
     * A pipeline is serial — one read outstanding, then a park — so a frame
     * that can only collect one answer makes a boot cost one frame per archive
     * while the client's wall-clock logic ticks keep queueing more work behind
     * them. Answering here instead makes this backend behave like the
     * synchronous one: requests in, data out, before Process returns.
     *
     * The page decides whether it can do that (see the harness's pumpSync).
     * When it cannot, the requests stay pending and the frame-gated path
     * delivers them later; the Pending() gate covers both.
     *
     * The host decides whether it *wants* it: blocking is a boot tool, and a
     * live client would rather wait a turn of the event loop than freeze the
     * frame a new sound or model arrived on. See g_web_io_blocking_reads.
     */
    if( px->pending_count > 0 && g_web_io_blocking_reads )
        torirs_web_io_pump_sync_js();

    return served;
}

/* --- browser entry points ------------------------------------------------ */

/*
 * The JS side reads the outgoing batch straight out of the wasm heap, so the
 * contract is: read len, read ptr, copy, then call _taken. Memory growth can
 * move the heap between calls, which is why the pointer is fetched fresh each
 * time rather than cached on the JS side.
 */

EMSCRIPTEN_KEEPALIVE int
torirs_io_request_len(void)
{
    if( !g_web_io || g_web_io->out_count == 0 )
        return 0;
    return g_web_io->out.len;
}

EMSCRIPTEN_KEEPALIVE void*
torirs_io_request_ptr(void)
{
    if( !g_web_io || g_web_io->out_count == 0 )
        return NULL;
    return g_web_io->out.data;
}

EMSCRIPTEN_KEEPALIVE void
torirs_io_request_taken(void)
{
    if( !g_web_io )
        return;
    IOWireBuf_Reset(&g_web_io->out);
    g_web_io->out_count = 0;
}

/** Scratch for one response batch. JS writes the bytes here, then submits. */
EMSCRIPTEN_KEEPALIVE void*
torirs_io_response_alloc(int size)
{
    if( size <= 0 )
        return NULL;
    return malloc((size_t)size);
}

static void
pending_release(struct PlatformX_IO* px, struct WebPending* pending)
{
    memset(pending, 0, sizeof(*pending));
    px->pending_count--;
}

EMSCRIPTEN_KEEPALIVE void
torirs_io_response_submit(
    void* data,
    int size)
{
    struct PlatformX_IO* px = g_web_io;
    struct IOWireReader reader;
    int count;

    if( !px || !data )
    {
        free(data);
        return;
    }

    IOWireReader_Init(&reader, data, size);
    count = IOWire_BatchRead(&reader, NULL);
    if( count < 0 )
    {
        fprintf(stderr, "web io: malformed response batch (%d bytes)\n", size);
        free(data);
        return;
    }

    for( int i = 0; i < count; i++ )
    {
        int record_start = reader.off;
        struct IOWireResponse resp;
        struct WebPending* pending = NULL;

        if( IOWire_ReadResponse(&reader, &resp) != 0 )
        {
            fprintf(stderr, "web io: truncated response record %d/%d\n", i, count);
            break;
        }

        for( int p = 0; p < WEB_IO_MAX_PENDING; p++ )
        {
            if( px->pending[p].in_use && px->pending[p].req_id == resp.req_id )
            {
                pending = &px->pending[p];
                break;
            }
        }
        if( !pending )
        {
            /* A duplicate or a leftover from a batch that already timed out. */
            continue;
        }

        if( IOWire_ApplyResponse(&resp, &pending->io->io_slots[pending->slot]) == 0 )
        {
            px->stat_completed++;
            if( pending->cacheable )
                cache_put(
                    px, &pending->key, reader.data + record_start, reader.off - record_start);
        }
        else
        {
            px->stat_failed++;
        }
        pending_release(px, pending);
    }

    free(data);
}

/**
 * Fail everything outstanding.
 *
 * Called by the harness when a batch could not be delivered. A dropped request
 * would park the task queue forever with no diagnostic; an errored slot makes
 * the waiting task take its own failure path, which does print one.
 */
EMSCRIPTEN_KEEPALIVE void
torirs_io_fail_pending(void)
{
    struct PlatformX_IO* px = g_web_io;
    int failed = 0;

    if( !px )
        return;
    for( int i = 0; i < WEB_IO_MAX_PENDING; i++ )
    {
        struct WebPending* pending = &px->pending[i];
        struct ToriRS_IOItem* item;
        if( !pending->in_use )
            continue;
        item = &pending->io->io_slots[pending->slot];
        item->data = NULL;
        item->data_size = 0;
        item->error_code = -1;
        pending_release(px, pending);
        failed++;
        px->stat_failed++;
    }
    if( failed )
        fprintf(stderr, "web io: failed %d outstanding request(s)\n", failed);
}

/** requests, cache hits, completions, failures, in-flight — for the page HUD. */
EMSCRIPTEN_KEEPALIVE void
torirs_io_stats(int* out)
{
    assert(out);
    if( !g_web_io )
    {
        memset(out, 0, 5 * sizeof(int));
        return;
    }
    out[0] = g_web_io->stat_requests;
    out[1] = g_web_io->stat_cache_hits;
    out[2] = g_web_io->stat_completed;
    out[3] = g_web_io->stat_failed;
    out[4] = g_web_io->pending_count;
}
