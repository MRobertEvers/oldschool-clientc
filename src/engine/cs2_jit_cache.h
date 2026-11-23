#ifndef CS2_JIT_CACHE_H
#define CS2_JIT_CACHE_H

/*
 * THE PLATFORM CACHE OF JITTED SCRIPTS (docs/CS2_JIT.md section 4).
 *
 * The VM builds a script's overlay in memory when the script loads
 * (cs2vm2/cs2vm2_jit.h). This module keeps those overlays between runs: one
 * PACK per cache, dialect and jit format version, read once at boot and
 * written back behind the frame, through the platform's IO queue as a
 * derived record (asyncio.h TORIRS_IOK_DERIVED_*). The VM does no IO and
 * never waits for any of this: a script that loads before the pack has
 * landed, or whose entry is missing, stale or corrupt, has its overlay built
 * in memory exactly as with no cache at all.
 *
 *   boot          CS2JitCache_New, then Task_CS2JitPackLoad (task_cs2_jit_pack.h)
 *                 on the parallel queue: DERIVED_READ, then _PackLanded/_PackAbsent
 *   script load   CacheProvider_ClientScriptJit (below), between the decode
 *                 and CacheProvider_ClientScriptAdd
 *   each frame    CS2JitCache_MaybeStore: when something new was built and
 *                 STORE_MS has passed, Task_CS2JitPackStore on the background
 *                 queue: plan, serialize in units, DERIVED_WRITE, _StoreDone
 *
 * The state functions here take no task and do no IO, so the unit test drives
 * them directly with in-memory packs. Nothing read from storage is ever
 * asserted on: a bad pack, entry or blob is a miss, counted by reason.
 *
 * Conventions (CLAUDE.md): a NULL pointer or a call out of order is an
 * assert; storage content and IO answers are runtime states, never asserted.
 */

#include "cs2vm2/cs2vm2_dialect.h"
#include "cs2vm2/cs2vm2_jit.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct CacheProvider;
struct CS2JitCache;

/* How a store finds a script by id: the provider's map in the client
 * (task_cs2_jit_pack.c), an array in a test. NULL: not loaded. */
typedef struct CS2VM2_Script* (*CS2JitScriptLookup)(
    void* user,
    int script_id);

/* The pack's first four bytes, 'C' 'J' 'P' 'K' read little-endian. */
#define CS2JIT_PACK_MAGIC 0x4B504A43u
#define CS2JIT_PACK_VERSION 1u
#define CS2JIT_PACK_HEADER_SIZE 40u
#define CS2JIT_PACK_INDEX_ENTRY_SIZE 24u
/* Entries past this size are left out of a store (and counted). */
#define CS2JIT_PACK_MAX_BYTES (16u * 1024u * 1024u)
/* The pack's derived-record name fits an IO item's (TORIRS_IOITEM_DERIVED_NAME_MAX). */
#define CS2JIT_PACK_NAME_MAX 64
/* A store serializes this many scripts between two "is the slice over?"s. */
#define CS2JIT_STORE_UNIT_SCRIPTS 64
/* After this many failed writes the cache stops storing for the session. */
#define CS2JIT_STORE_FAILURES_MAX 3
/* TORIRS_CS2_JIT_STORE_MS's default: the write-behind delay. */
#define CS2JIT_STORE_MS_DEFAULT 10000u

enum CS2JitPackState
{
    /* No read issued yet (CS2JitCache_ReadIssued not called). */
    CS2JIT_PACK_UNREAD = 0,
    /* The DERIVED_READ is on its way. */
    CS2JIT_PACK_READING,
    /* A pack landed and its header and index were good. */
    CS2JIT_PACK_LANDED,
    /* No pack: the first run, a cleared store, a read error. */
    CS2JIT_PACK_ABSENT,
    /* A pack landed and its header or index was bad (pack_reject says why):
     * treated as absent, and replaced by the next store. */
    CS2JIT_PACK_BAD,

    CS2JIT_PACK_STATE_COUNT
};

/* Why a landed pack was treated as absent. */
enum CS2JitPackReject
{
    CS2JIT_PACK_OK = 0,
    CS2JIT_PACK_REJECT_TRUNCATED,
    CS2JIT_PACK_REJECT_MAGIC,
    CS2JIT_PACK_REJECT_VERSION,
    CS2JIT_PACK_REJECT_HEADER_SIZE,
    CS2JIT_PACK_REJECT_FORMAT,
    CS2JIT_PACK_REJECT_DIALECT,
    CS2JIT_PACK_REJECT_SIZE,
    CS2JIT_PACK_REJECT_RESERVED,
    CS2JIT_PACK_REJECT_CHECKSUM,
    CS2JIT_PACK_REJECT_ORDER,

    CS2JIT_PACK_REJECT_COUNT
};

/*
 * Counted always (integer adds at script load and per store); the *_ns
 * fields only while timing is on (CS2JitCache_SetTiming, TORIRS_CS2_JIT_STATS=1).
 * docs/CS2_JIT.md section 5 says what each is for.
 */
struct CS2JitCacheStats
{
    /* Every script handed to CS2JitCache_Prepare, and its opcodes. */
    int prepared;
    int64_t prepared_ops;
    /* Built in memory because the pack read had not resolved yet. */
    int before_pack;
    /* CS2VM2_JitBuild calls that built an overlay, and their opcodes. */
    int built;
    int64_t built_ops;
    /* CS2VM2_JitBuild refusals, by enum CS2VM2_JitBuildResult. */
    int refused[CS2VM2_JIT_BUILD_RESULT_COUNT];
    /* Resolved pack, script id in its index (hits) or not (misses). */
    int index_hits;
    int index_misses;
    /* CS2VM2_JitFingerprint calls (at Prepare and at store planning). */
    int fingerprinted;
    int64_t fingerprint_ops;
    /* Overlays attached from the pack, and their opcodes. */
    int attached;
    int64_t attached_ops;
    /* Entries that were not attached, by enum CS2VM2_JitAttachResult
     * (STALE includes an index fingerprint that already differs). */
    int rejected[CS2VM2_JIT_ATTACH_RESULT_COUNT];

    /* How the boot read resolved (enum CS2JitPackState / CS2JitPackReject):
     * LANDED, ABSENT or BAD and why. Unlike pack_state, a later store does
     * not change it. */
    int read_state;
    int read_reject;
    /* The resident pack: as read, or as last written by a store. */
    int pack_state;
    int pack_reject;
    int64_t pack_bytes;
    int pack_entries;
    /* Index entries whose blob lies outside the pack: ignored. */
    int pack_entries_ignored;

    /* Stores that wrote, bytes written, and the entries in them. */
    int stores;
    int64_t store_bytes;
    int store_entries_new;
    int store_entries_carried;
    /* Dirty scripts a store found the pack already holding unchanged. */
    int store_skipped_unchanged;
    /* Planned stores that found nothing to write. */
    int stores_unchanged;
    /* Entries left out because the pack would pass CS2JIT_PACK_MAX_BYTES. */
    int store_entries_dropped;
    int store_failures;

    /* Static overlay totals over every overlay built or attached
     * (CS2VM2_JitGetInfo). */
    int64_t fused_group_pcs;
    int64_t declared_local_pcs;
    int64_t constant_string_pcs;

    /* Timers (0 unless timing is on). */
    uint64_t build_ns;
    uint64_t fingerprint_ns;
    uint64_t attach_ns;
    uint64_t pack_parse_ns;
    uint64_t serialize_ns;
    uint64_t pack_read_wait_ns;
    uint64_t store_wait_ns;
};

/*
 * A cache for scripts of `dialect` (NULL means OSRS, as on CS2VM2_Script).
 * Owns nothing but its own state; the pack name is derived from the dialect
 * tag and CS2VM2_JIT_FORMAT_VERSION ("cs2jit-<tag 8 hex>-v<N>.pack").
 */
struct CS2JitCache*
CS2JitCache_New(struct CS2VM2_Dialect const* dialect);

/* A deallocator: NULL is a no-op. Also forgets the cache as the active one
 * if it is (CS2JitCache_SetActive). */
void
CS2JitCache_Free(struct CS2JitCache* cache);

/* The derived-record name the pack is read from and written to. */
char const*
CS2JitCache_PackName(struct CS2JitCache const* cache);

/* Timers on or off (off by default: a clock read in wasm is a JS call). */
void
CS2JitCache_SetTiming(
    struct CS2JitCache* cache,
    bool on);

/* The ceiling a store keeps the pack under (CS2JIT_PACK_MAX_BYTES unless a
 * test lowers it to exercise the cut without a 16 MB working set). */
void
CS2JitCache_SetMaxPackBytes(
    struct CS2JitCache* cache,
    size_t max_bytes);

/* The write-behind delay after the first new overlay (TORIRS_CS2_JIT_STORE_MS). */
void
CS2JitCache_SetStoreMs(
    struct CS2JitCache* cache,
    uint64_t store_ms);

/*
 * TORIRS_CS2_JIT_CACHE=0: no pack is read or written, every overlay is built
 * in memory, and the cache only counts and times those builds (so the cold
 * cost has a `cs2jit:` line too). The pack reads as ABSENT from here on.
 * Instead of the boot read: asserts UNREAD.
 */
void
CS2JitCache_SetMemoryOnly(struct CS2JitCache* cache);

/* ---- the boot read ------------------------------------------------------ */

/* The DERIVED_READ has been queued (Task_CS2JitPackLoad). Asserts UNREAD. */
void
CS2JitCache_ReadIssued(struct CS2JitCache* cache);

/*
 * The read answered `size` bytes. The cache keeps its own copy (the caller
 * still clears its IO item), validates the header and index, and resolves:
 * LANDED, or BAD (counted, treated as absent). Asserts READING.
 */
void
CS2JitCache_PackLanded(
    struct CS2JitCache* cache,
    void const* bytes,
    int size);

/* The read answered "no record" or failed. Asserts READING. */
void
CS2JitCache_PackAbsent(struct CS2JitCache* cache);

/* LANDED, ABSENT or BAD: lookups and stores may proceed. */
bool
CS2JitCache_Resolved(struct CS2JitCache const* cache);

/*
 * One loaded script, before CacheProvider_ClientScriptAdd: give it an overlay
 * from the pack when the pack has a valid one for it, otherwise build one in
 * memory (and, if built, remember its id for the next store). Never waits,
 * never does IO. Asserts `cache`, `script`, that the script has no overlay
 * yet, and that its dialect is the cache's.
 */
void
CS2JitCache_Prepare(
    struct CS2JitCache* cache,
    struct CS2VM2_Script* script);

/* ---- write-behind --------------------------------------------------------- */

/*
 * Is a store wanted at `now_ms`: the read has resolved, something was built
 * since the last store, no store is in flight, storing has not been stopped,
 * and STORE_MS has passed since this function first saw something to store
 * (its one side effect is noting that time). CS2JitCache_MaybeStore
 * (task_cs2_jit_pack.h) asks it once per frame.
 */
bool
CS2JitCache_StoreWanted(
    struct CS2JitCache* cache,
    uint64_t now_ms);

/*
 * The store's three steps, run by the store task (or a test).
 *
 * StoreBegin marks a store in flight. StorePlan takes the ids built since the
 * last store, fingerprints each script (looked up through `lookup`), drops
 * those the pack already holds unchanged, merges the rest with the resident
 * pack's entries into a sorted index, sizes the new pack and allocates it.
 * It returns that size, or 0 when nothing changed: the store is then over
 * (no StoreDone). StoreUnit fills the next `max_scripts` entries (the header
 * and index first) and returns true once the pack is complete; scripts are
 * looked up again by id in each unit, so no script pointer is held between
 * units. StoreBytes lends the finished pack for the DERIVED_WRITE.
 * StoreDone(true) makes it the resident pack; StoreDone(false) frees it,
 * puts the ids back for the next store and counts the failure.
 */
void
CS2JitCache_StoreBegin(struct CS2JitCache* cache);

size_t
CS2JitCache_StorePlan(
    struct CS2JitCache* cache,
    CS2JitScriptLookup lookup,
    void* lookup_user);

bool
CS2JitCache_StoreUnit(
    struct CS2JitCache* cache,
    CS2JitScriptLookup lookup,
    void* lookup_user,
    int max_scripts);

void*
CS2JitCache_StoreBytes(
    struct CS2JitCache* cache,
    int* out_size);

void
CS2JitCache_StoreDone(
    struct CS2JitCache* cache,
    bool ok);

/* A store is between StoreBegin and its end. */
bool
CS2JitCache_StoreInFlight(struct CS2JitCache const* cache);

/* Ids built since the last store (before planning dedupes them). */
int
CS2JitCache_DirtyCount(struct CS2JitCache const* cache);

/* ---- accounting ------------------------------------------------------------ */

void
CS2JitCache_Stats(
    struct CS2JitCache const* cache,
    struct CS2JitCacheStats* out);

/* Print one "cs2jit:" line (docs/CS2_JIT.md section 5) through TORIRS_REPORT. */
void
CS2JitCache_Report(
    struct CS2JitCache const* cache,
    char const* why);

/* Whether timing is on (TORIRS_CS2_JIT_STATS): the owner reports after each
 * store and at shutdown only then. */
bool
CS2JitCache_Timing(struct CS2JitCache const* cache);

/* The process's cache for an on-demand report (the web lane's exported
 * torirs_cs2jit_report). NULL clears it. */
void
CS2JitCache_SetActive(struct CS2JitCache* cache);

/* Report the active cache, if there is one; returns whether there was. */
bool
CS2JitCache_ReportActive(char const* why);

char const*
CS2JitCache_PackStateName(int state);

char const*
CS2JitCache_PackRejectName(int reject);

/*
 * THE LOAD-SITE DISPATCH (docs/CS2_JIT.md 4.1), called by the clientscript
 * load task between CS2VM2_ScriptCopyFromRSCache and
 * CacheProvider_ClientScriptAdd:
 *
 *   provider->cs2jit_enabled == 0   nothing (TORIRS_CS2_JIT=0: today's VM)
 *   provider->cs2jit == NULL        CS2VM2_JitBuild (no cache: tests, or
 *                                   TORIRS_CS2_JIT_CACHE=0)
 *   otherwise                       CS2JitCache_Prepare
 *
 * Defined here rather than in cache_provider.c so that the provider's own
 * standalone links (test-cache-provider-invalidate, ...) need nothing new.
 */
void
CacheProvider_ClientScriptJit(
    struct CacheProvider* provider,
    struct CS2VM2_Script* script);

#endif /* CS2_JIT_CACHE_H */
