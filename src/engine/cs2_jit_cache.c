/*
 * The platform cache of jitted scripts: the state machine behind the two pack
 * tasks (task_cs2_jit_pack.c). docs/CS2_JIT.md section 4; the header says
 * what each call promises.
 *
 * Everything that reaches this file from storage -- the pack's header, its
 * index, every blob -- is treated as untrusted: a bad header makes the pack
 * absent, a bad index entry is ignored, and a bad blob is the VM's to reject
 * (CS2VM2_JitAttachBlob re-proves every code byte). None of it is asserted.
 */

#include "engine/cs2_jit_cache.h"

#include "engine/cache_provider.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* One index entry, as parsed from a pack (only entries whose blob lies
 * inside the pack are kept). */
struct CS2JitPackEntry
{
    int32_t script_id;
    uint64_t fingerprint;
    uint32_t offset;
    uint32_t length;
    /* Its blob was rejected this session (corrupt, illegal, ...): the next
     * store writes a fresh one even though the fingerprint still matches. */
    bool bad;
};

/* One entry of the pack a store is writing. */
struct CS2JitPlanEntry
{
    int32_t script_id;
    uint64_t fingerprint;
    uint32_t length;
    uint32_t out_offset;
    /* >= 0: carried byte for byte from the resident pack's entry of this
     * index. -1: serialized fresh from the loaded script. */
    int carried_from;
};

struct CS2JitCache
{
    uint32_t dialect_tag;
    char pack_name[CS2JIT_PACK_NAME_MAX];
    bool timing;
    uint64_t store_ms;
    size_t max_pack_bytes;

    int state;
    int reject;
    /* The resident pack (ToriMem_Script): what lookups attach from and what
     * a store carries forward. NULL until one landed or was written. */
    uint8_t* pack;
    size_t pack_size;
    struct CS2JitPackEntry* entries;
    int entry_count;

    /* Ids built in memory since the last store's plan took them. */
    int* dirty;
    int dirty_count;
    int dirty_cap;
    bool dirty_seen;
    uint64_t dirty_seen_ms;

    bool in_flight;
    bool stopped;
    /* CS2JitCache_SetMemoryOnly: counts and times builds, no pack at all. */
    bool memory_only;
    int failures;

    /* The store in progress. */
    int* plan_ids;
    int plan_id_count;
    struct CS2JitPlanEntry* plan;
    int plan_count;
    int plan_next;
    int plan_new;
    int plan_carried;
    bool plan_header_written;
    uint8_t* store_buf;
    size_t store_size;

    uint64_t read_t0;
    uint64_t store_t0;
    struct CS2JitCacheStats stats;
};

static struct CS2JitCache* g_cs2jit_active;

/* ---- small helpers -------------------------------------------------------- */

static uint64_t
cs2jit_now_ns(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return ((uint64_t)now.tv_sec * 1000000000ull) + (uint64_t)now.tv_nsec;
}

static uint64_t
cs2jit_clock(struct CS2JitCache const* cache)
{
    return cache->timing ? cs2jit_now_ns() : 0;
}

static uint64_t
cs2jit_since(
    struct CS2JitCache const* cache,
    uint64_t start)
{
    return cache->timing ? cs2jit_now_ns() - start : 0;
}

static uint32_t
rd_u16(uint8_t const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t
rd_u32(uint8_t const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
rd_u64(uint8_t const* p)
{
    return (uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32);
}

static void
wr_u16(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void
wr_u32(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void
wr_u64(
    uint8_t* p,
    uint64_t v)
{
    wr_u32(p, (uint32_t)v);
    wr_u32(p + 4, (uint32_t)(v >> 32));
}

static int
cmp_int(
    void const* a,
    void const* b)
{
    int const x = *(int const*)a;
    int const y = *(int const*)b;
    return (x > y) - (x < y);
}

/*
 * The index checksum: CS2VM2_JitHash64 seeded with the magic over the
 * header's first 24 bytes followed by the index (docs/CS2_JIT.md 4.3). The
 * two are not adjacent in the pack, so they are hashed as one stream copied
 * out; the index is 24 bytes a script, a few hundred KB at the very most.
 */
static uint64_t
pack_index_checksum(
    uint8_t const* header,
    uint8_t const* index,
    size_t index_size)
{
    uint8_t* stream = ToriMem_Alloc(ToriMem_Script(), 24 + index_size);
    uint64_t hash;

    memcpy(stream, header, 24);
    if( index_size > 0 )
        memcpy(stream + 24, index, index_size);
    hash = CS2VM2_JitHash64(stream, 24 + index_size, CS2JIT_PACK_MAGIC);
    ToriMem_Free(ToriMem_Script(), stream);
    return hash;
}

static void
pack_drop(struct CS2JitCache* cache)
{
    ToriMem_Free(ToriMem_Script(), cache->pack);
    ToriMem_Free(ToriMem_Script(), cache->entries);
    cache->pack = NULL;
    cache->pack_size = 0;
    cache->entries = NULL;
    cache->entry_count = 0;
}

/*
 * Validate a pack's header and index and, when they are good, keep the index
 * entries whose blob lies inside the pack. Returns CS2JIT_PACK_OK or the
 * first reason it is not one; nothing is kept on a rejection.
 */
static int
pack_parse(
    struct CS2JitCache* cache,
    uint8_t const* bytes,
    size_t size,
    struct CS2JitPackEntry** out_entries,
    int* out_count,
    int* out_ignored)
{
    uint32_t count;
    size_t index_size;
    size_t blobs_at;
    struct CS2JitPackEntry* entries;
    int kept = 0;
    int ignored = 0;

    *out_entries = NULL;
    *out_count = 0;
    *out_ignored = 0;
    if( size < CS2JIT_PACK_HEADER_SIZE )
        return CS2JIT_PACK_REJECT_TRUNCATED;
    if( rd_u32(bytes) != CS2JIT_PACK_MAGIC )
        return CS2JIT_PACK_REJECT_MAGIC;
    if( rd_u16(bytes + 4) != CS2JIT_PACK_VERSION )
        return CS2JIT_PACK_REJECT_VERSION;
    if( rd_u16(bytes + 6) != CS2JIT_PACK_HEADER_SIZE )
        return CS2JIT_PACK_REJECT_HEADER_SIZE;
    if( rd_u32(bytes + 8) != CS2VM2_JIT_FORMAT_VERSION )
        return CS2JIT_PACK_REJECT_FORMAT;
    if( rd_u32(bytes + 12) != cache->dialect_tag )
        return CS2JIT_PACK_REJECT_DIALECT;
    if( rd_u32(bytes + 20) != 0 )
        return CS2JIT_PACK_REJECT_RESERVED;
    if( rd_u64(bytes + 32) != (uint64_t)size )
        return CS2JIT_PACK_REJECT_SIZE;
    count = rd_u32(bytes + 16);
    if( count > (size - CS2JIT_PACK_HEADER_SIZE) / CS2JIT_PACK_INDEX_ENTRY_SIZE )
        return CS2JIT_PACK_REJECT_TRUNCATED;
    index_size = (size_t)count * CS2JIT_PACK_INDEX_ENTRY_SIZE;
    blobs_at = CS2JIT_PACK_HEADER_SIZE + index_size;
    if( pack_index_checksum(bytes, bytes + CS2JIT_PACK_HEADER_SIZE, index_size) != rd_u64(bytes + 24) )
        return CS2JIT_PACK_REJECT_CHECKSUM;
    for( uint32_t i = 1; i < count; i++ )
    {
        int32_t const prev = (int32_t)rd_u32(bytes + CS2JIT_PACK_HEADER_SIZE + ((size_t)(i - 1) * 24));
        int32_t const here = (int32_t)rd_u32(bytes + CS2JIT_PACK_HEADER_SIZE + ((size_t)i * 24));
        if( here <= prev )
            return CS2JIT_PACK_REJECT_ORDER;
    }

    entries = ToriMem_Calloc(ToriMem_Script(), count > 0 ? count : 1, sizeof(*entries));
    for( uint32_t i = 0; i < count; i++ )
    {
        uint8_t const* row = bytes + CS2JIT_PACK_HEADER_SIZE + ((size_t)i * 24);
        uint64_t const offset = rd_u32(row + 16);
        uint64_t const length = rd_u32(row + 20);

        /* A blob outside the pack, overlapping the index, or too short to be
         * one is ignored: that script is built in memory and the next store
         * replaces the entry. */
        if( rd_u32(row + 4) != 0 || offset < blobs_at || length < CS2VM2_JIT_BLOB_HEADER_SIZE ||
            offset + length > (uint64_t)size )
        {
            ignored++;
            continue;
        }
        entries[kept].script_id = (int32_t)rd_u32(row);
        entries[kept].fingerprint = rd_u64(row + 8);
        entries[kept].offset = (uint32_t)offset;
        entries[kept].length = (uint32_t)length;
        kept++;
    }
    *out_entries = entries;
    *out_count = kept;
    *out_ignored = ignored;
    return CS2JIT_PACK_OK;
}

static struct CS2JitPackEntry*
pack_find(
    struct CS2JitCache const* cache,
    int script_id)
{
    int lo = 0;
    int hi = cache->entry_count - 1;

    while( lo <= hi )
    {
        int const mid = lo + ((hi - lo) / 2);
        int32_t const id = cache->entries[mid].script_id;

        if( id == script_id )
            return &cache->entries[mid];
        if( id < script_id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static void
dirty_add(
    struct CS2JitCache* cache,
    int script_id)
{
    if( cache->dirty_count == cache->dirty_cap )
    {
        cache->dirty_cap = cache->dirty_cap ? cache->dirty_cap * 2 : 64;
        cache->dirty = ToriMem_Realloc(ToriMem_Script(), cache->dirty, (size_t)cache->dirty_cap * sizeof(int));
        assert(cache->dirty);
    }
    cache->dirty[cache->dirty_count++] = script_id;
}

static void
note_overlay(
    struct CS2JitCache* cache,
    struct CS2VM2_Script const* script)
{
    struct CS2VM2_JitInfo info;

    CS2VM2_JitGetInfo(script, &info);
    cache->stats.fused_group_pcs += info.fused_group_pcs;
    cache->stats.declared_local_pcs += info.declared_local_pcs;
    cache->stats.constant_string_pcs += info.constant_string_pcs;
}

/* Build in memory; a script that got an overlay is remembered for the next
 * store. A refusal is a state of the script, counted and left plain. */
static void
build_and_mark(
    struct CS2JitCache* cache,
    struct CS2VM2_Script* script)
{
    uint64_t const t0 = cs2jit_clock(cache);
    enum CS2VM2_JitBuildResult const result = CS2VM2_JitBuild(script);

    cache->stats.build_ns += cs2jit_since(cache, t0);
    if( result != CS2VM2_JIT_BUILT )
    {
        cache->stats.refused[result]++;
        return;
    }
    cache->stats.built++;
    cache->stats.built_ops += script->op_count;
    note_overlay(cache, script);
    if( !cache->memory_only )
        dirty_add(cache, script->script_id);
}

static uint64_t
fingerprint(
    struct CS2JitCache* cache,
    struct CS2VM2_Script const* script)
{
    uint64_t const t0 = cs2jit_clock(cache);
    uint64_t const fp = CS2VM2_JitFingerprint(script);

    cache->stats.fingerprint_ns += cs2jit_since(cache, t0);
    cache->stats.fingerprinted++;
    cache->stats.fingerprint_ops += script->op_count;
    return fp;
}

/* ---- lifetime ------------------------------------------------------------- */

struct CS2JitCache*
CS2JitCache_New(struct CS2VM2_Dialect const* dialect)
{
    struct CS2JitCache* cache = ToriMem_Calloc(ToriMem_Script(), 1, sizeof(*cache));

    assert(cache);
    cache->dialect_tag = CS2VM2_JitDialectTag(dialect);
    snprintf(
        cache->pack_name,
        sizeof(cache->pack_name),
        "cs2jit-%08x-v%u.pack",
        (unsigned)cache->dialect_tag,
        (unsigned)CS2VM2_JIT_FORMAT_VERSION);
    cache->store_ms = CS2JIT_STORE_MS_DEFAULT;
    cache->max_pack_bytes = CS2JIT_PACK_MAX_BYTES;
    cache->state = CS2JIT_PACK_UNREAD;
    return cache;
}

void
CS2JitCache_Free(struct CS2JitCache* cache)
{
    if( !cache )
        return;
    if( g_cs2jit_active == cache )
        g_cs2jit_active = NULL;
    pack_drop(cache);
    ToriMem_Free(ToriMem_Script(), cache->dirty);
    ToriMem_Free(ToriMem_Script(), cache->plan_ids);
    ToriMem_Free(ToriMem_Script(), cache->plan);
    ToriMem_Free(ToriMem_Script(), cache->store_buf);
    ToriMem_Free(ToriMem_Script(), cache);
}

char const*
CS2JitCache_PackName(struct CS2JitCache const* cache)
{
    assert(cache);
    return cache->pack_name;
}

void
CS2JitCache_SetTiming(
    struct CS2JitCache* cache,
    bool on)
{
    assert(cache);
    cache->timing = on;
}

bool
CS2JitCache_Timing(struct CS2JitCache const* cache)
{
    assert(cache);
    return cache->timing;
}

void
CS2JitCache_SetMaxPackBytes(
    struct CS2JitCache* cache,
    size_t max_bytes)
{
    assert(cache);
    assert(max_bytes >= CS2JIT_PACK_HEADER_SIZE);
    cache->max_pack_bytes = max_bytes;
}

void
CS2JitCache_SetStoreMs(
    struct CS2JitCache* cache,
    uint64_t store_ms)
{
    assert(cache);
    cache->store_ms = store_ms;
}

void
CS2JitCache_SetMemoryOnly(struct CS2JitCache* cache)
{
    assert(cache);
    assert(cache->state == CS2JIT_PACK_UNREAD);
    cache->memory_only = true;
    cache->state = CS2JIT_PACK_ABSENT;
    cache->stats.read_state = CS2JIT_PACK_ABSENT;
}

/* ---- the boot read ------------------------------------------------------- */

void
CS2JitCache_ReadIssued(struct CS2JitCache* cache)
{
    assert(cache);
    assert(cache->state == CS2JIT_PACK_UNREAD);
    cache->state = CS2JIT_PACK_READING;
    cache->read_t0 = cs2jit_clock(cache);
}

void
CS2JitCache_PackLanded(
    struct CS2JitCache* cache,
    void const* bytes,
    int size)
{
    uint64_t t0;
    struct CS2JitPackEntry* entries;
    int count;
    int ignored;
    int reject;

    assert(cache);
    assert(cache->state == CS2JIT_PACK_READING);
    assert(size >= 0);
    assert(bytes || size == 0);
    cache->stats.pack_read_wait_ns += cs2jit_since(cache, cache->read_t0);
    t0 = cs2jit_clock(cache);
    reject = pack_parse(cache, bytes, (size_t)size, &entries, &count, &ignored);
    if( reject != CS2JIT_PACK_OK )
    {
        cache->state = CS2JIT_PACK_BAD;
        cache->reject = reject;
        cache->stats.read_state = CS2JIT_PACK_BAD;
        cache->stats.read_reject = reject;
        cache->stats.pack_parse_ns += cs2jit_since(cache, t0);
        TORIRS_LOG("cs2jit: pack %s ignored (%s)\n", cache->pack_name, CS2JitCache_PackRejectName(reject));
        return;
    }
    pack_drop(cache);
    cache->pack = ToriMem_Memdup(ToriMem_Script(), bytes, (size_t)size);
    cache->pack_size = (size_t)size;
    cache->entries = entries;
    cache->entry_count = count;
    cache->stats.pack_entries_ignored += ignored;
    cache->state = CS2JIT_PACK_LANDED;
    cache->stats.read_state = CS2JIT_PACK_LANDED;
    cache->stats.pack_parse_ns += cs2jit_since(cache, t0);
}

void
CS2JitCache_PackAbsent(struct CS2JitCache* cache)
{
    assert(cache);
    assert(cache->state == CS2JIT_PACK_READING);
    cache->stats.pack_read_wait_ns += cs2jit_since(cache, cache->read_t0);
    cache->state = CS2JIT_PACK_ABSENT;
    cache->stats.read_state = CS2JIT_PACK_ABSENT;
}

bool
CS2JitCache_Resolved(struct CS2JitCache const* cache)
{
    assert(cache);
    return cache->state == CS2JIT_PACK_LANDED || cache->state == CS2JIT_PACK_ABSENT ||
           cache->state == CS2JIT_PACK_BAD;
}

/* ---- a script load ----------------------------------------------------------- */

void
CS2JitCache_Prepare(
    struct CS2JitCache* cache,
    struct CS2VM2_Script* script)
{
    struct CS2JitPackEntry* entry;
    enum CS2VM2_JitAttachResult result;
    uint64_t fp;
    uint64_t t0;

    assert(cache);
    assert(script);
    assert(!script->jit);
    assert(CS2VM2_JitDialectTag(script->dialect) == cache->dialect_tag);

    cache->stats.prepared++;
    cache->stats.prepared_ops += script->op_count;

    /* The read has not resolved: build now rather than wait. A build is one
     * pass over the script; the read is a disk or IndexedDB round trip. The
     * id is remembered, and the store finds the pack already holding it
     * unchanged if it does. */
    if( !CS2JitCache_Resolved(cache) )
    {
        cache->stats.before_pack++;
        build_and_mark(cache, script);
        return;
    }

    /* TORIRS_CS2_JIT_CACHE=0: there is no pack to look in. */
    if( cache->memory_only )
    {
        build_and_mark(cache, script);
        return;
    }

    entry = pack_find(cache, script->script_id);
    if( !entry )
    {
        cache->stats.index_misses++;
        build_and_mark(cache, script);
        return;
    }
    cache->stats.index_hits++;

    fp = fingerprint(cache, script);
    /* The index already says the entry is for other content: stale without
     * paying the validation pass. */
    if( fp != entry->fingerprint )
    {
        cache->stats.rejected[CS2VM2_JIT_REJECT_STALE]++;
        build_and_mark(cache, script);
        return;
    }

    t0 = cs2jit_clock(cache);
    result = CS2VM2_JitAttachBlob(script, fp, cache->pack + entry->offset, entry->length);
    cache->stats.attach_ns += cs2jit_since(cache, t0);
    if( result == CS2VM2_JIT_ATTACHED )
    {
        cache->stats.attached++;
        cache->stats.attached_ops += script->op_count;
        note_overlay(cache, script);
        return;
    }
    cache->stats.rejected[result]++;
    entry->bad = true;
    build_and_mark(cache, script);
}

/* ---- write-behind ----------------------------------------------------------- */

bool
CS2JitCache_StoreWanted(
    struct CS2JitCache* cache,
    uint64_t now_ms)
{
    assert(cache);
    if( !CS2JitCache_Resolved(cache) || cache->in_flight || cache->stopped || cache->memory_only ||
        cache->dirty_count == 0 )
        return false;
    if( !cache->dirty_seen )
    {
        cache->dirty_seen = true;
        cache->dirty_seen_ms = now_ms;
    }
    return now_ms - cache->dirty_seen_ms >= cache->store_ms;
}

bool
CS2JitCache_StoreInFlight(struct CS2JitCache const* cache)
{
    assert(cache);
    return cache->in_flight;
}

int
CS2JitCache_DirtyCount(struct CS2JitCache const* cache)
{
    assert(cache);
    return cache->dirty_count;
}

void
CS2JitCache_StoreBegin(struct CS2JitCache* cache)
{
    assert(cache);
    assert(!cache->in_flight);
    assert(CS2JitCache_Resolved(cache));
    cache->in_flight = true;
    cache->store_t0 = cs2jit_clock(cache);
}

static void
store_end(struct CS2JitCache* cache)
{
    ToriMem_Free(ToriMem_Script(), cache->plan);
    ToriMem_Free(ToriMem_Script(), cache->plan_ids);
    cache->plan = NULL;
    cache->plan_count = 0;
    cache->plan_next = 0;
    cache->plan_ids = NULL;
    cache->plan_id_count = 0;
    cache->plan_new = 0;
    cache->plan_carried = 0;
    cache->plan_header_written = false;
    cache->stats.store_wait_ns += cs2jit_since(cache, cache->store_t0);
    cache->in_flight = false;
}

size_t
CS2JitCache_StorePlan(
    struct CS2JitCache* cache,
    CS2JitScriptLookup lookup,
    void* lookup_user)
{
    struct CS2JitPlanEntry* fresh;
    int fresh_count = 0;
    int unique = 0;
    int a = 0;
    int b = 0;
    size_t blobs = 0;
    size_t total;
    int kept;

    assert(cache);
    assert(lookup);
    assert(cache->in_flight);
    assert(!cache->plan);

    /* Take the ids; whatever is built from here on waits for the next store. */
    cache->plan_ids = cache->dirty;
    cache->plan_id_count = cache->dirty_count;
    cache->dirty = NULL;
    cache->dirty_count = 0;
    cache->dirty_cap = 0;
    cache->dirty_seen = false;

    if( cache->plan_id_count > 0 )
    {
        qsort(cache->plan_ids, (size_t)cache->plan_id_count, sizeof(int), cmp_int);
        for( int i = 0; i < cache->plan_id_count; i++ )
            if( unique == 0 || cache->plan_ids[unique - 1] != cache->plan_ids[i] )
                cache->plan_ids[unique++] = cache->plan_ids[i];
        cache->plan_id_count = unique;
    }

    fresh = ToriMem_Calloc(ToriMem_Script(), (size_t)(unique > 0 ? unique : 1), sizeof(*fresh));
    for( int i = 0; i < unique; i++ )
    {
        int const id = cache->plan_ids[i];
        struct CS2VM2_Script* script = lookup(lookup_user, id);
        struct CS2JitPackEntry const* old;
        uint64_t fp;

        /* A refused script has no overlay to keep. */
        if( !script || !script->jit )
            continue;
        fp = fingerprint(cache, script);
        old = pack_find(cache, id);
        if( old && old->fingerprint == fp && !old->bad )
        {
            cache->stats.store_skipped_unchanged++;
            continue;
        }
        fresh[fresh_count].script_id = id;
        fresh[fresh_count].fingerprint = fp;
        fresh[fresh_count].length = (uint32_t)CS2VM2_JitBlobSize(script);
        fresh[fresh_count].carried_from = -1;
        fresh_count++;
    }

    if( fresh_count == 0 )
    {
        /* A session whose read was merely slow, or that loaded nothing new:
         * nothing is written. */
        ToriMem_Free(ToriMem_Script(), fresh);
        cache->stats.stores_unchanged++;
        store_end(cache);
        return 0;
    }

    /* The sorted union: the resident entries carried byte for byte, a fresh
     * blob wherever this session built one. */
    cache->plan =
        ToriMem_Calloc(ToriMem_Script(), (size_t)(cache->entry_count + fresh_count), sizeof(*cache->plan));
    while( a < cache->entry_count || b < fresh_count )
    {
        struct CS2JitPlanEntry* out = &cache->plan[cache->plan_count];

        if( b >= fresh_count || (a < cache->entry_count && cache->entries[a].script_id < fresh[b].script_id) )
        {
            out->script_id = cache->entries[a].script_id;
            out->fingerprint = cache->entries[a].fingerprint;
            out->length = cache->entries[a].length;
            out->carried_from = a;
            a++;
        }
        else
        {
            if( a < cache->entry_count && cache->entries[a].script_id == fresh[b].script_id )
                a++; /* replaced */
            *out = fresh[b];
            b++;
        }
        cache->plan_count++;
    }
    ToriMem_Free(ToriMem_Script(), fresh);

    /* The ceiling, in id order: an entry that would take the pack past it is
     * left out (and counted); the ones after it may still fit. */
    kept = 0;
    for( int i = 0; i < cache->plan_count; i++ )
    {
        size_t const header = CS2JIT_PACK_HEADER_SIZE + ((size_t)(kept + 1) * CS2JIT_PACK_INDEX_ENTRY_SIZE);

        if( header + blobs + cache->plan[i].length > cache->max_pack_bytes )
        {
            cache->stats.store_entries_dropped++;
            continue;
        }
        blobs += cache->plan[i].length;
        cache->plan[kept++] = cache->plan[i];
    }
    cache->plan_count = kept;

    total = CS2JIT_PACK_HEADER_SIZE + ((size_t)kept * CS2JIT_PACK_INDEX_ENTRY_SIZE);
    for( int i = 0; i < kept; i++ )
    {
        cache->plan[i].out_offset = (uint32_t)total;
        total += cache->plan[i].length;
        if( cache->plan[i].carried_from >= 0 )
            cache->plan_carried++;
        else
            cache->plan_new++;
    }
    cache->store_buf = ToriMem_Alloc(ToriMem_Script(), total);
    cache->store_size = total;
    cache->plan_next = 0;
    cache->plan_header_written = false;
    return total;
}

static void
store_write_header(struct CS2JitCache* cache)
{
    uint8_t* const out = cache->store_buf;
    uint8_t* const index = out + CS2JIT_PACK_HEADER_SIZE;

    memset(out, 0, CS2JIT_PACK_HEADER_SIZE);
    wr_u32(out + 0, CS2JIT_PACK_MAGIC);
    wr_u16(out + 4, CS2JIT_PACK_VERSION);
    wr_u16(out + 6, CS2JIT_PACK_HEADER_SIZE);
    wr_u32(out + 8, CS2VM2_JIT_FORMAT_VERSION);
    wr_u32(out + 12, cache->dialect_tag);
    wr_u32(out + 16, (uint32_t)cache->plan_count);
    wr_u32(out + 20, 0);
    wr_u64(out + 32, (uint64_t)cache->store_size);
    for( int i = 0; i < cache->plan_count; i++ )
    {
        uint8_t* const row = index + ((size_t)i * CS2JIT_PACK_INDEX_ENTRY_SIZE);

        wr_u32(row + 0, (uint32_t)cache->plan[i].script_id);
        wr_u32(row + 4, 0);
        wr_u64(row + 8, cache->plan[i].fingerprint);
        wr_u32(row + 16, cache->plan[i].out_offset);
        wr_u32(row + 20, cache->plan[i].length);
    }
    wr_u64(
        out + 24,
        pack_index_checksum(out, index, (size_t)cache->plan_count * CS2JIT_PACK_INDEX_ENTRY_SIZE));
}

bool
CS2JitCache_StoreUnit(
    struct CS2JitCache* cache,
    CS2JitScriptLookup lookup,
    void* lookup_user,
    int max_scripts)
{
    int done = 0;

    assert(cache);
    assert(lookup);
    assert(cache->in_flight);
    assert(cache->store_buf);
    assert(max_scripts > 0);

    if( !cache->plan_header_written )
    {
        store_write_header(cache);
        cache->plan_header_written = true;
    }
    while( cache->plan_next < cache->plan_count && done < max_scripts )
    {
        struct CS2JitPlanEntry const* entry = &cache->plan[cache->plan_next];
        uint8_t* const out = cache->store_buf + entry->out_offset;

        if( entry->carried_from >= 0 )
        {
            struct CS2JitPackEntry const* old = &cache->entries[entry->carried_from];
            memcpy(out, cache->pack + old->offset, entry->length);
        }
        else
        {
            /* Looked up again: the plan holds ids, never a script pointer
             * across the task's yields. A loaded script is never replaced or
             * freed while the provider lives (CacheProvider_ClientScriptAdd
             * keeps the first decode), so it is the one the plan sized. */
            struct CS2VM2_Script const* script = lookup(lookup_user, entry->script_id);
            uint64_t const t0 = cs2jit_clock(cache);

            assert(script);
            assert(script->jit);
            assert(CS2VM2_JitBlobSize(script) == entry->length);
            CS2VM2_JitSerialize(script, entry->fingerprint, out, entry->length);
            cache->stats.serialize_ns += cs2jit_since(cache, t0);
        }
        cache->plan_next++;
        done++;
    }
    return cache->plan_next == cache->plan_count;
}

void*
CS2JitCache_StoreBytes(
    struct CS2JitCache* cache,
    int* out_size)
{
    assert(cache);
    assert(out_size);
    assert(cache->in_flight);
    assert(cache->store_buf);
    assert(cache->plan_header_written);
    assert(cache->plan_next == cache->plan_count);
    *out_size = (int)cache->store_size;
    return cache->store_buf;
}

void
CS2JitCache_StoreDone(
    struct CS2JitCache* cache,
    bool ok)
{
    assert(cache);
    assert(cache->in_flight);
    assert(cache->store_buf);

    if( ok )
    {
        struct CS2JitPackEntry* entries =
            ToriMem_Calloc(ToriMem_Script(), (size_t)(cache->plan_count > 0 ? cache->plan_count : 1), sizeof(*entries));

        for( int i = 0; i < cache->plan_count; i++ )
        {
            entries[i].script_id = cache->plan[i].script_id;
            entries[i].fingerprint = cache->plan[i].fingerprint;
            entries[i].offset = cache->plan[i].out_offset;
            entries[i].length = cache->plan[i].length;
            /* A carried entry whose blob was rejected while this store was
             * in flight keeps its mark: the bytes just written are the same
             * corrupt ones, and the next store must replace them although
             * the fingerprint still matches. A fresh blob starts clean. */
            if( cache->plan[i].carried_from >= 0 )
                entries[i].bad = cache->entries[cache->plan[i].carried_from].bad;
        }
        cache->stats.stores++;
        cache->stats.store_bytes += (int64_t)cache->store_size;
        cache->stats.store_entries_new += cache->plan_new;
        cache->stats.store_entries_carried += cache->plan_carried;
        /* What was written is what is resident now: the next store carries
         * it forward, and a later load attaches from it. */
        pack_drop(cache);
        cache->pack = cache->store_buf;
        cache->pack_size = cache->store_size;
        cache->entries = entries;
        cache->entry_count = cache->plan_count;
        cache->state = CS2JIT_PACK_LANDED;
        cache->reject = CS2JIT_PACK_OK;
        cache->failures = 0;
    }
    else
    {
        cache->stats.store_failures++;
        cache->failures++;
        ToriMem_Free(ToriMem_Script(), cache->store_buf);
        /* The ids go back for the next try, after STORE_MS. */
        for( int i = 0; i < cache->plan_id_count; i++ )
            dirty_add(cache, cache->plan_ids[i]);
        if( cache->failures >= CS2JIT_STORE_FAILURES_MAX )
        {
            cache->stopped = true;
            TORIRS_ERR("cs2jit: %d writes of %s failed; not storing again this session\n",
                cache->failures, cache->pack_name);
        }
    }
    cache->store_buf = NULL;
    cache->store_size = 0;
    store_end(cache);
}

/* ---- accounting ------------------------------------------------------------- */

void
CS2JitCache_Stats(
    struct CS2JitCache const* cache,
    struct CS2JitCacheStats* out)
{
    assert(cache);
    assert(out);
    *out = cache->stats;
    out->pack_state = cache->state;
    out->pack_reject = cache->reject;
    out->pack_bytes = (int64_t)cache->pack_size;
    out->pack_entries = cache->entry_count;
}

char const*
CS2JitCache_PackStateName(int state)
{
    static char const* const names[CS2JIT_PACK_STATE_COUNT] = {
        "unread", "reading", "landed", "absent", "bad",
    };
    assert(state >= 0);
    assert(state < CS2JIT_PACK_STATE_COUNT);
    return names[state];
}

char const*
CS2JitCache_PackRejectName(int reject)
{
    static char const* const names[CS2JIT_PACK_REJECT_COUNT] = {
        "ok", "truncated", "magic", "version", "header_size", "format",
        "dialect", "size", "reserved", "checksum", "order",
    };
    assert(reject >= 0);
    assert(reject < CS2JIT_PACK_REJECT_COUNT);
    return names[reject];
}

static double
ms(uint64_t ns)
{
    return (double)ns / 1e6;
}

void
CS2JitCache_Report(
    struct CS2JitCache const* cache,
    char const* why)
{
    struct CS2JitCacheStats s;
    char rejected[128];
    size_t used = 0;

    assert(cache);
    assert(why);
    CS2JitCache_Stats(cache, &s);
    rejected[0] = '\0';
    for( int r = 1; r < CS2VM2_JIT_ATTACH_RESULT_COUNT; r++ )
        used += (size_t)snprintf(rejected + used, sizeof(rejected) - used, "%s%d", r > 1 ? "," : "", s.rejected[r]);
    TORIRS_REPORT(
        "cs2jit: why=%s prepared=%d/%lld built=%d/%lld/%.3fms before_pack=%d refused=%d,%d,%d "
        "hits=%d misses=%d fp=%d/%lld/%.3fms attached=%d/%lld/%.3fms rejected=%s "
        "read=%s/%s pack=%s/%lld/%d/parse %.3fms/wait %.3fms ignored=%d "
        "stores=%d/%lld/%.3fms new=%d carried=%d unchanged=%d skipped=%d dropped=%d failures=%d "
        "serialize=%.3fms fused=%lld ldpd=%lld cstr=%lld\n",
        why,
        s.prepared,
        (long long)s.prepared_ops,
        s.built,
        (long long)s.built_ops,
        ms(s.build_ns),
        s.before_pack,
        s.refused[CS2VM2_JIT_REFUSED_EMPTY],
        s.refused[CS2VM2_JIT_REFUSED_DIALECT],
        s.refused[CS2VM2_JIT_REFUSED_BRANCH_RANGE],
        s.index_hits,
        s.index_misses,
        s.fingerprinted,
        (long long)s.fingerprint_ops,
        ms(s.fingerprint_ns),
        s.attached,
        (long long)s.attached_ops,
        ms(s.attach_ns),
        rejected,
        CS2JitCache_PackStateName(s.read_state),
        CS2JitCache_PackRejectName(s.read_reject),
        CS2JitCache_PackStateName(s.pack_state),
        (long long)s.pack_bytes,
        s.pack_entries,
        ms(s.pack_parse_ns),
        ms(s.pack_read_wait_ns),
        s.pack_entries_ignored,
        s.stores,
        (long long)s.store_bytes,
        ms(s.store_wait_ns),
        s.store_entries_new,
        s.store_entries_carried,
        s.stores_unchanged,
        s.store_skipped_unchanged,
        s.store_entries_dropped,
        s.store_failures,
        ms(s.serialize_ns),
        (long long)s.fused_group_pcs,
        (long long)s.declared_local_pcs,
        (long long)s.constant_string_pcs);
}

void
CS2JitCache_SetActive(struct CS2JitCache* cache)
{
    g_cs2jit_active = cache;
}

bool
CS2JitCache_ReportActive(char const* why)
{
    assert(why);
    if( !g_cs2jit_active )
        return false;
    CS2JitCache_Report(g_cs2jit_active, why);
    return true;
}

/* ---- the load-site dispatch ------------------------------------------------- */

void
CacheProvider_ClientScriptJit(
    struct CacheProvider* provider,
    struct CS2VM2_Script* script)
{
    assert(provider);
    assert(script);
    if( !provider->cs2jit_enabled )
        return;
    if( !provider->cs2jit )
    {
        (void)CS2VM2_JitBuild(script);
        return;
    }
    CS2JitCache_Prepare(provider->cs2jit, script);
}
