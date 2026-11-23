/*
 * test-cs2-jit-cache: the platform cache's state machine (engine/cs2_jit_cache.c)
 * with in-memory packs and no IO (docs/CS2_JIT.md section 6).
 *
 * Each "boot" is a fresh CS2JitCache and freshly decoded scripts, the way a
 * client session would have them; a pack a store produced is fed to the next
 * boot as if the platform had read it back.
 */

#include "engine/cache_provider.h"
#include "engine/cs2_jit_cache.h"
#include "engine/test/cs2_jit_test_scripts.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A whole store, in units of `unit` scripts. Returns the pack (a copy the
 * caller frees) or NULL when the plan found nothing to write. */
static uint8_t*
store_all(
    struct CS2JitCache* cache,
    struct ct_set* set,
    int unit,
    bool ok,
    size_t* out_size)
{
    size_t planned;
    uint8_t* copy = NULL;
    void* bytes;
    int size;
    int units = 0;

    *out_size = 0;
    CS2JitCache_StoreBegin(cache);
    planned = CS2JitCache_StorePlan(cache, ct_lookup, set);
    if( planned == 0 )
    {
        CT_CHECK(!CS2JitCache_StoreInFlight(cache), "an empty plan ends the store");
        return NULL;
    }
    while( !CS2JitCache_StoreUnit(cache, ct_lookup, set, unit) )
        units++;
    CT_CHECK(units >= (set->count / unit) - 1, "the store ran in units of %d (%d more)", unit, units);
    bytes = CS2JitCache_StoreBytes(cache, &size);
    CT_CHECK((size_t)size == planned, "the pack is the planned size (%d vs %zu)", size, planned);
    copy = ToriMem_Memdup(ToriMem_Script(), bytes, (size_t)size);
    *out_size = (size_t)size;
    CS2JitCache_StoreDone(cache, ok);
    CT_CHECK(!CS2JitCache_StoreInFlight(cache), "StoreDone ends the store");
    return copy;
}

static struct CS2JitCache*
boot_with(
    uint8_t const* pack,
    size_t size)
{
    struct CS2JitCache* cache = CS2JitCache_New(NULL);
    CS2JitCache_ReadIssued(cache);
    if( pack )
        CS2JitCache_PackLanded(cache, pack, (int)size);
    else
        CS2JitCache_PackAbsent(cache);
    return cache;
}

static void
prepare_all(
    struct CS2JitCache* cache,
    struct ct_set* set)
{
    for( int i = 0; i < set->count; i++ )
        CS2JitCache_Prepare(cache, &set->scripts[i]);
}

static struct CS2JitCacheStats
stats_of(struct CS2JitCache const* cache)
{
    struct CS2JitCacheStats s;
    CS2JitCache_Stats(cache, &s);
    return s;
}

static int const k_ids[] = { 3, 9, 14, 200, 1201 };
#define K_IDS ((int)(sizeof(k_ids) / sizeof(k_ids[0])))

static void
load_set(
    struct ct_set* set,
    int changed_id,
    int variant)
{
    for( int i = 0; i < K_IDS; i++ )
        ct_add(set, k_ids[i], k_ids[i] == changed_id ? variant : 0);
}

/* The first session's pack: every script built, then stored. */
static uint8_t*
first_pack(size_t* size)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache = boot_with(NULL, 0);
    uint8_t* pack;

    load_set(&set, -1, 0);
    prepare_all(cache, &set);
    CT_CHECK(!CS2JitCache_StoreWanted(cache, 0), "first sight of something to store");
    CT_CHECK(CS2JitCache_StoreWanted(cache, 1000000), "a store is wanted after STORE_MS");
    pack = store_all(cache, &set, 2, true, size);
    CS2JitCache_Free(cache);
    ct_set_free(&set);
    return pack;
}

static void
test_miss_builds_then_hit_attaches(void)
{
    struct ct_set one = { 0 };
    struct ct_set two = { 0 };
    struct CS2JitCache* cache = boot_with(NULL, 0);
    struct CS2JitCacheStats s;
    uint8_t* pack;
    size_t size;

    load_set(&one, -1, 0);
    prepare_all(cache, &one);
    s = stats_of(cache);
    CT_CHECK(s.prepared == K_IDS && s.built == K_IDS && s.attached == 0, "boot 1 builds all");
    CT_CHECK(CS2JitCache_DirtyCount(cache) == K_IDS, "every built id is dirty");
    for( int i = 0; i < one.count; i++ )
        CT_CHECK(one.scripts[i].jit != NULL, "script %d has an overlay", one.scripts[i].script_id);

    /* STORE_MS from the first time the frame saw something to store. */
    CT_CHECK(!CS2JitCache_StoreWanted(cache, 5000), "not before STORE_MS (first sight at 5000)");
    CT_CHECK(!CS2JitCache_StoreWanted(cache, 14999), "not before STORE_MS");
    CT_CHECK(CS2JitCache_StoreWanted(cache, 15000), "at STORE_MS");
    pack = store_all(cache, &one, 2, true, &size);
    s = stats_of(cache);
    CT_CHECK(pack != NULL && s.stores == 1 && s.store_entries_new == K_IDS, "one store, all new");
    CT_CHECK(CS2JitCache_DirtyCount(cache) == 0, "the store took the dirty set");
    CT_CHECK(!CS2JitCache_StoreWanted(cache, 1u << 30), "nothing to store after it");
    CT_CHECK(s.pack_entries == K_IDS, "the written pack is resident (%d entries)", s.pack_entries);

    /* Boot 2: the same scripts decoded afresh attach, build nothing, and carry
     * exactly the code boot 1 built. */
    CS2JitCache_Free(cache);
    cache = boot_with(pack, size);
    load_set(&two, -1, 0);
    prepare_all(cache, &two);
    s = stats_of(cache);
    CT_CHECK(s.pack_state == CS2JIT_PACK_LANDED, "pack landed (%s)", CS2JitCache_PackStateName(s.pack_state));
    CT_CHECK(s.attached == K_IDS && s.built == 0, "boot 2 attaches all (attached %d built %d)", s.attached, s.built);
    CT_CHECK(s.index_hits == K_IDS && s.fingerprinted == K_IDS, "one fingerprint per hit");
    for( int i = 0; i < two.count; i++ )
        CT_CHECK(ct_same_code(&one.scripts[i], &two.scripts[i]), "identical code for %d", two.scripts[i].script_id);
    CT_CHECK(CS2JitCache_DirtyCount(cache) == 0, "nothing new to store");
    CT_CHECK(!CS2JitCache_StoreWanted(cache, 1u << 30), "no store in a warm session");

    ToriMem_Free(ToriMem_Script(), pack);
    CS2JitCache_Free(cache);
    ct_set_free(&one);
    ct_set_free(&two);
}

static void
test_before_pack_and_slow_read(void)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache;
    struct CS2JitCacheStats s;
    uint8_t* pack;
    uint8_t* again;
    size_t size;
    size_t again_size;

    pack = first_pack(&size);
    cache = CS2JitCache_New(NULL);
    CS2JitCache_ReadIssued(cache);
    load_set(&set, -1, 0);
    prepare_all(cache, &set);
    s = stats_of(cache);
    CT_CHECK(s.before_pack == K_IDS && s.built == K_IDS, "the read outstanding: built, never waited");
    CT_CHECK(!CS2JitCache_StoreWanted(cache, 1u << 30), "no store before the read resolves");

    /* The slow read lands: what was built equals what it holds, so the store
     * writes nothing. */
    CS2JitCache_PackLanded(cache, pack, (int)size);
    CT_CHECK(CS2JitCache_StoreWanted(cache, 0) == false, "first sight");
    CT_CHECK(CS2JitCache_StoreWanted(cache, 1u << 30), "wanted once resolved");
    again = store_all(cache, &set, 64, true, &again_size);
    s = stats_of(cache);
    CT_CHECK(again == NULL, "nothing written");
    CT_CHECK(s.stores == 0 && s.stores_unchanged == 1 && s.store_skipped_unchanged == K_IDS,
        "the plan skipped all (stores %d unchanged %d skipped %d)", s.stores, s.stores_unchanged,
        s.store_skipped_unchanged);
    ToriMem_Free(ToriMem_Script(), pack);
    CS2JitCache_Free(cache);
    ct_set_free(&set);
}

static void
test_stale_entry_replaced_and_merge_carries(void)
{
    struct ct_set set = { 0 };
    struct ct_set next = { 0 };
    struct CS2JitCache* cache;
    struct CS2JitCacheStats s;
    uint8_t* pack;
    uint8_t* merged;
    size_t size;
    size_t merged_size;
    uint32_t count;

    pack = first_pack(&size);
    cache = boot_with(pack, size);
    /* Script 14 changed under the same id, and a new script 77 appears; 200
     * and 1201 are not loaded this session at all. */
    ct_add(&set, 3, 0);
    ct_add(&set, 14, 5);
    ct_add(&set, 77, 0);
    prepare_all(cache, &set);
    s = stats_of(cache);
    CT_CHECK(s.attached == 1, "3 attaches");
    CT_CHECK(s.rejected[CS2VM2_JIT_REJECT_STALE] == 1, "14 is stale");
    CT_CHECK(s.index_misses == 1, "77 is a miss");
    CT_CHECK(s.built == 2, "14 and 77 are built");
    CT_CHECK(set.scripts[1].jit != NULL, "a stale entry still runs jitted");
    (void)CS2JitCache_StoreWanted(cache, 0);
    merged = store_all(cache, &set, 1, true, &merged_size);
    s = stats_of(cache);
    CT_CHECK(merged != NULL, "a store");
    CT_CHECK(s.store_entries_new == 2 && s.store_entries_carried == K_IDS - 1,
        "2 new, %d carried (new %d carried %d)", K_IDS - 1, s.store_entries_new, s.store_entries_carried);

    /* Sorted and unique, and the carried entries byte for byte. */
    count = ct_rd32(merged + 16);
    CT_CHECK(count == (uint32_t)K_IDS + 1, "6 entries (%u)", count);
    for( uint32_t i = 1; i < count; i++ )
        CT_CHECK((int32_t)ct_rd32(merged + 40 + (i * 24)) > (int32_t)ct_rd32(merged + 40 + ((i - 1) * 24)),
            "index sorted and unique at %u", i);
    for( uint32_t i = 0; i < count; i++ )
    {
        int32_t const id = (int32_t)ct_rd32(merged + 40 + (i * 24));
        uint32_t const off = ct_rd32(merged + 40 + (i * 24) + 16);
        uint32_t const len = ct_rd32(merged + 40 + (i * 24) + 20);
        uint32_t const old_count = ct_rd32(pack + 16);

        if( id != 200 && id != 1201 && id != 3 && id != 9 )
            continue;
        for( uint32_t j = 0; j < old_count; j++ )
            if( (int32_t)ct_rd32(pack + 40 + (j * 24)) == id )
                CT_CHECK(len == ct_rd32(pack + 40 + (j * 24) + 20) &&
                             memcmp(merged + off, pack + ct_rd32(pack + 40 + (j * 24) + 16), len) == 0,
                    "%d carried byte for byte", id);
    }

    /* The next boot attaches all six, the changed one included. */
    CS2JitCache_Free(cache);
    cache = boot_with(merged, merged_size);
    load_set(&next, 14, 5);
    ct_add(&next, 77, 0);
    prepare_all(cache, &next);
    s = stats_of(cache);
    CT_CHECK(s.attached == K_IDS + 1 && s.built == 0, "all attach (attached %d built %d)", s.attached, s.built);

    ToriMem_Free(ToriMem_Script(), pack);
    ToriMem_Free(ToriMem_Script(), merged);
    CS2JitCache_Free(cache);
    ct_set_free(&set);
    ct_set_free(&next);
}

/* One header corruption: the whole pack is absent, every script built. */
static void
check_bad_pack(
    uint8_t const* pack,
    size_t size,
    int expect,
    char const* what)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache = boot_with(pack, size);
    struct CS2JitCacheStats s;

    load_set(&set, -1, 0);
    prepare_all(cache, &set);
    s = stats_of(cache);
    CT_CHECK(s.pack_state == CS2JIT_PACK_BAD && s.pack_reject == expect, "%s: rejected as %s (got %s/%s)", what,
        CS2JitCache_PackRejectName(expect), CS2JitCache_PackStateName(s.pack_state),
        CS2JitCache_PackRejectName(s.pack_reject));
    CT_CHECK(s.built == K_IDS && s.attached == 0 && s.index_hits == 0, "%s: all built", what);
    /* And a store replaces it with a good one. */
    (void)CS2JitCache_StoreWanted(cache, 0);
    CT_CHECK(CS2JitCache_StoreWanted(cache, 1u << 30), "%s: the next store replaces it", what);
    CS2JitCache_Free(cache);
    ct_set_free(&set);
}

/* Two ids swapped, checksum recomputed: still refused. */
static void
swap_first_rows(
    uint8_t* pack,
    size_t size)
{
    uint8_t row[24];

    memcpy(row, pack + 40, 24);
    memcpy(pack + 40, pack + 64, 24);
    memcpy(pack + 64, row, 24);
    ct_pack_rechecksum(pack, size);
}

static void
duplicate_first_id(
    uint8_t* pack,
    size_t size)
{
    ct_wr32(pack + 64, ct_rd32(pack + 40));
    ct_pack_rechecksum(pack, size);
}

static void
test_pack_header_rejections(void)
{
    size_t size;
    uint8_t* pack = first_pack(&size);
    uint8_t* bad = ToriMem_Alloc(ToriMem_Script(), size + 1);

#define MUTATE(expect, what, stmt)                                                                 \
    do                                                                                             \
    {                                                                                              \
        size_t bad_size = size;                                                                    \
        memcpy(bad, pack, size);                                                                   \
        stmt;                                                                                      \
        check_bad_pack(bad, bad_size, expect, what);                                               \
    } while( 0 )

    MUTATE(CS2JIT_PACK_REJECT_TRUNCATED, "39 bytes", bad_size = 39);
    MUTATE(CS2JIT_PACK_REJECT_TRUNCATED, "0 bytes", bad_size = 0);
    MUTATE(CS2JIT_PACK_REJECT_MAGIC, "magic", bad[0] ^= 1);
    MUTATE(CS2JIT_PACK_REJECT_VERSION, "pack version", bad[4] = 2);
    MUTATE(CS2JIT_PACK_REJECT_HEADER_SIZE, "header size", bad[6] = 41);
    MUTATE(CS2JIT_PACK_REJECT_FORMAT, "jit format version", ct_wr32(bad + 8, CS2VM2_JIT_FORMAT_VERSION + 1));
    MUTATE(CS2JIT_PACK_REJECT_DIALECT, "dialect", bad[12] ^= 0x80);
    MUTATE(CS2JIT_PACK_REJECT_RESERVED, "reserved", bad[20] = 1);
    MUTATE(CS2JIT_PACK_REJECT_SIZE, "a trailing byte", (bad[size] = 0, bad_size = size + 1));
    MUTATE(CS2JIT_PACK_REJECT_SIZE, "a short file", bad_size = size - 1);
    MUTATE(CS2JIT_PACK_REJECT_TRUNCATED, "an index longer than the file", (ct_wr32(bad + 16, 1u << 20)));
    MUTATE(CS2JIT_PACK_REJECT_CHECKSUM, "an index byte", bad[40 + 9] ^= 1);
    MUTATE(CS2JIT_PACK_REJECT_CHECKSUM, "the checksum", bad[24] ^= 1);
    MUTATE(CS2JIT_PACK_REJECT_ORDER, "unsorted ids", swap_first_rows(bad, size));
    MUTATE(CS2JIT_PACK_REJECT_ORDER, "a duplicate id", duplicate_first_id(bad, size));
#undef MUTATE
    ToriMem_Free(ToriMem_Script(), bad);
    ToriMem_Free(ToriMem_Script(), pack);
}

static void
test_bad_entries(void)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache;
    struct CS2JitCacheStats s;
    size_t size;
    uint8_t* pack = first_pack(&size);
    uint8_t* copy = ToriMem_Memdup(ToriMem_Script(), pack, size);
    uint32_t const blob_off = ct_rd32(copy + 40 + 24 + 16); /* entry 1: id 9 */
    uint8_t* stored = NULL;
    size_t stored_size;

    /* Entry 0 (id 3) points past the end: ignored. Entry 1 (id 9): one code
     * byte flipped, its own checksum now wrong. */
    ct_wr32(copy + 40 + 20, (uint32_t)size);
    ct_pack_rechecksum(copy, size);
    copy[blob_off + CS2VM2_JIT_BLOB_HEADER_SIZE] ^= 0x01;
    cache = boot_with(copy, size);
    load_set(&set, -1, 0);
    prepare_all(cache, &set);
    s = stats_of(cache);
    CT_CHECK(s.pack_state == CS2JIT_PACK_LANDED, "the header is fine");
    CT_CHECK(s.pack_entries_ignored == 1 && s.pack_entries == K_IDS - 1, "one entry ignored");
    CT_CHECK(s.index_misses == 1, "the ignored entry is a miss");
    CT_CHECK(s.rejected[CS2VM2_JIT_REJECT_CHECKSUM] == 1, "the flipped blob is a checksum reject");
    CT_CHECK(s.attached == K_IDS - 2 && s.built == 2, "the rest attach (attached %d built %d)", s.attached, s.built);
    for( int i = 0; i < set.count; i++ )
        CT_CHECK(set.scripts[i].jit != NULL, "%d runs jitted", set.scripts[i].script_id);
    (void)CS2JitCache_StoreWanted(cache, 0);
    stored = store_all(cache, &set, 64, true, &stored_size);
    s = stats_of(cache);
    CT_CHECK(stored && s.store_entries_new == 2, "both replaced at the next store (new %d)", s.store_entries_new);
    ToriMem_Free(ToriMem_Script(), stored);
    CS2JitCache_Free(cache);
    ct_set_free(&set);

    /* A blob made under another format version, inside a good pack. */
    memcpy(copy, pack, size);
    ct_wr32(copy + blob_off + 4, CS2VM2_JIT_FORMAT_VERSION + 1);
    cache = boot_with(copy, size);
    load_set(&set, -1, 0);
    prepare_all(cache, &set);
    s = stats_of(cache);
    CT_CHECK(s.rejected[CS2VM2_JIT_REJECT_VERSION] == 1 && s.built == 1, "a stale-version blob is rebuilt");
    CS2JitCache_Free(cache);
    ct_set_free(&set);

    ToriMem_Free(ToriMem_Script(), copy);
    ToriMem_Free(ToriMem_Script(), pack);
}

/* The blob offset of `id` in a pack's index, or 0 when it has no entry. */
static uint32_t
pack_blob_offset(
    uint8_t const* pack,
    int32_t id)
{
    uint32_t const count = ct_rd32(pack + 16);
    for( uint32_t i = 0; i < count; i++ )
        if( (int32_t)ct_rd32(pack + 40 + (i * 24)) == id )
            return ct_rd32(pack + 40 + (i * 24) + 16);
    return 0;
}

/*
 * A blob rejected while a store is in flight is still rewritten by the next
 * store. The store that was in flight carries the corrupt bytes over (its plan
 * was made before the reject), and the reject's mark must survive that
 * store's StoreDone into the resident index it builds: otherwise the next
 * plan finds the fingerprint equal, skips the script as unchanged, and every
 * later session rejects and rebuilds it again.
 */
static void
test_bad_entry_during_store(void)
{
    struct ct_set set = { 0 };
    struct ct_set last = { 0 };
    struct CS2JitCache* cache;
    struct CS2JitCacheStats s;
    size_t size;
    uint8_t* pack = first_pack(&size);
    uint32_t const blob_off = pack_blob_offset(pack, 200);
    uint8_t* mid = NULL;
    uint8_t* healed = NULL;
    size_t healed_size;
    size_t planned;
    void* bytes;
    int bytes_size;

    CT_CHECK(blob_off != 0, "200 is in the first pack");
    /* One code byte of 200's blob flipped: its blob checksum fails, the pack
     * index stays valid. */
    pack[blob_off + CS2VM2_JIT_BLOB_HEADER_SIZE] ^= 0x01;

    /* Session 2: 3 changed, so a store begins before 200 has loaded. */
    cache = boot_with(pack, size);
    ct_add(&set, 3, 5);
    prepare_all(cache, &set);
    (void)CS2JitCache_StoreWanted(cache, 0);
    CT_CHECK(CS2JitCache_StoreWanted(cache, 1u << 30), "a store for the changed 3");
    CS2JitCache_StoreBegin(cache);
    planned = CS2JitCache_StorePlan(cache, ct_lookup, &set);
    CT_CHECK(planned > 0, "the plan writes 3");

    /* 200 loads inside the store window: rejected, rebuilt, marked dirty. */
    CS2JitCache_Prepare(cache, ct_add(&set, 200, 0));
    s = stats_of(cache);
    CT_CHECK(s.rejected[CS2VM2_JIT_REJECT_CHECKSUM] == 1, "200's blob is a checksum reject");
    CT_CHECK(set.scripts[1].jit != NULL, "200 runs jitted from a build");
    CT_CHECK(CS2JitCache_DirtyCount(cache) == 1, "200 waits for the next store");

    while( !CS2JitCache_StoreUnit(cache, ct_lookup, &set, 64) )
        ;
    bytes = CS2JitCache_StoreBytes(cache, &bytes_size);
    mid = ToriMem_Memdup(ToriMem_Script(), bytes, (size_t)bytes_size);
    CS2JitCache_StoreDone(cache, true);
    CT_CHECK(pack_blob_offset(mid, 200) != 0 &&
                 mid[pack_blob_offset(mid, 200) + CS2VM2_JIT_BLOB_HEADER_SIZE] ==
                     pack[blob_off + CS2VM2_JIT_BLOB_HEADER_SIZE],
        "the in-flight store carried 200's corrupt blob (its plan predates the reject)");

    /* The next store must replace it. */
    (void)CS2JitCache_StoreWanted(cache, 0);
    CT_CHECK(CS2JitCache_StoreWanted(cache, 1u << 30), "a store for 200");
    healed = store_all(cache, &set, 64, true, &healed_size);
    s = stats_of(cache);
    CT_CHECK(healed != NULL, "the next store writes (stores_unchanged %d, skipped %d)", s.stores_unchanged,
        s.store_skipped_unchanged);
    CT_CHECK(s.store_skipped_unchanged == 0, "200 is not skipped as unchanged");
    CS2JitCache_Free(cache);

    /* Session 3 boots from what was written last and attaches 200. */
    if( healed )
    {
        cache = boot_with(healed, healed_size);
        CS2JitCache_Prepare(cache, ct_add(&last, 200, 0));
        s = stats_of(cache);
        CT_CHECK(s.attached == 1 && s.rejected[CS2VM2_JIT_REJECT_CHECKSUM] == 0,
            "the healed pack attaches 200 (attached %d, checksum rejects %d)", s.attached,
            s.rejected[CS2VM2_JIT_REJECT_CHECKSUM]);
        CS2JitCache_Free(cache);
    }

    ToriMem_Free(ToriMem_Script(), healed);
    ToriMem_Free(ToriMem_Script(), mid);
    ToriMem_Free(ToriMem_Script(), pack);
    ct_set_free(&set);
    ct_set_free(&last);
}

static void
test_ceiling(void)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache = boot_with(NULL, 0);
    struct CS2JitCacheStats s;
    uint8_t* pack;
    size_t size;

    ct_add(&set, 0, 0); /* 6 ops */
    ct_add(&set, 1, 0); /* 10 ops */
    ct_add(&set, 2, 0); /* 14 ops */
    prepare_all(cache, &set);
    /* Room for the header, two index rows and two blobs (6 + 10 ops). */
    CS2JitCache_SetMaxPackBytes(cache, 40 + (2 * 24) + (2 * CS2VM2_JIT_BLOB_HEADER_SIZE) + 6 + 10);
    (void)CS2JitCache_StoreWanted(cache, 0);
    pack = store_all(cache, &set, 64, true, &size);
    s = stats_of(cache);
    CT_CHECK(pack && ct_rd32(pack + 16) == 2, "two entries fit");
    CT_CHECK(s.store_entries_dropped == 1, "the third is left out and counted");
    CT_CHECK(size <= 40 + (2 * 24) + (2 * CS2VM2_JIT_BLOB_HEADER_SIZE) + 16, "under the ceiling (%zu)", size);
    ToriMem_Free(ToriMem_Script(), pack);
    CS2JitCache_Free(cache);
    ct_set_free(&set);
}

static void
test_write_failures_stop(void)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache = boot_with(NULL, 0);
    struct CS2JitCacheStats s;
    uint64_t now = 0;
    size_t size;

    load_set(&set, -1, 0);
    prepare_all(cache, &set);
    for( int attempt = 0; attempt < CS2JIT_STORE_FAILURES_MAX; attempt++ )
    {
        (void)CS2JitCache_StoreWanted(cache, now);
        now += CS2JIT_STORE_MS_DEFAULT;
        CT_CHECK(CS2JitCache_StoreWanted(cache, now), "attempt %d wanted", attempt);
        ToriMem_Free(ToriMem_Script(), store_all(cache, &set, 64, false, &size));
        CT_CHECK(CS2JitCache_DirtyCount(cache) == K_IDS, "a failed write keeps the ids");
        CT_CHECK(!CS2JitCache_StoreWanted(cache, now), "and waits STORE_MS again");
    }
    s = stats_of(cache);
    CT_CHECK(s.store_failures == CS2JIT_STORE_FAILURES_MAX && s.stores == 0, "three failures counted");
    CT_CHECK(!CS2JitCache_StoreWanted(cache, now + (10 * CS2JIT_STORE_MS_DEFAULT)), "then no more stores");
    CS2JitCache_Free(cache);
    ct_set_free(&set);
}

static void
test_memory_only_and_dispatch(void)
{
    struct ct_set set = { 0 };
    struct CS2JitCache* cache = CS2JitCache_New(NULL);
    struct CacheProvider provider;
    struct CS2JitCacheStats s;

    CS2JitCache_SetMemoryOnly(cache);
    load_set(&set, -1, 0);
    memset(&provider, 0, sizeof(provider));

    /* TORIRS_CS2_JIT=0: no overlay at all. */
    provider.cs2jit_enabled = 0; /* CacheProvider_SetCS2Jit, without linking the provider */
    provider.cs2jit = cache;
    CacheProvider_ClientScriptJit(&provider, &set.scripts[0]);
    CT_CHECK(set.scripts[0].jit == NULL, "disabled: no overlay");
    /* No cache: built, uncounted. */
    provider.cs2jit_enabled = 1;
    provider.cs2jit = NULL;
    CacheProvider_ClientScriptJit(&provider, &set.scripts[0]);
    CT_CHECK(set.scripts[0].jit != NULL, "no cache: built in memory");
    /* TORIRS_CS2_JIT_CACHE=0: built and counted, never stored. */
    provider.cs2jit = cache;
    for( int i = 1; i < set.count; i++ )
        CacheProvider_ClientScriptJit(&provider, &set.scripts[i]);
    s = stats_of(cache);
    CT_CHECK(s.built == K_IDS - 1 && s.pack_state == CS2JIT_PACK_ABSENT, "memory only: counted builds");
    CT_CHECK(CS2JitCache_DirtyCount(cache) == 0 && !CS2JitCache_StoreWanted(cache, 1u << 30), "memory only: no store");
    CS2JitCache_Free(cache);
    ct_set_free(&set);
}

int
main(void)
{
    test_miss_builds_then_hit_attaches();
    test_before_pack_and_slow_read();
    test_stale_entry_replaced_and_merge_carries();
    test_pack_header_rejections();
    test_bad_entries();
    test_bad_entry_during_store();
    test_ceiling();
    test_write_failures_stop();
    test_memory_only_and_dispatch();
    return ct_finish("cs2_jit_cache_test");
}
