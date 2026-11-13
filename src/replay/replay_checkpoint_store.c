/*
 * The replay's session checkpoints by tick (replay/replay_checkpoint_store.h).
 */
#include "replay/replay_checkpoint_store.h"

#include "mem/torirs_mem.h"
#include "replay/replay_checkpoint.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"

struct ReplayCkptStoreEntry
{
    int tick;
    uint64_t clock_ms;
    size_t bytes;
    struct ReplayCheckpoint* cp;
};

struct ReplayCheckpointStore
{
    size_t budget;
    size_t bytes;
    /* Ascending by tick. */
    struct ReplayCkptStoreEntry* entries;
    int count;
    int cap;
};

struct ReplayCheckpointStore*
ReplayCheckpointStore_New(size_t budget_bytes)
{
    struct ReplayCheckpointStore* store = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*store));

    assert(store);
    store->budget = budget_bytes;
    return store;
}

void
ReplayCheckpointStore_Clear(struct ReplayCheckpointStore* store)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
        ReplayCheckpoint_Free(store->entries[i].cp);
    store->count = 0;
    store->bytes = 0;
}

void
ReplayCheckpointStore_Free(struct ReplayCheckpointStore* store)
{
    if( !store )
        return;
    ReplayCheckpointStore_Clear(store);
    ToriMem_Free(ToriMem_Session(), store->entries);
    ToriMem_Free(ToriMem_Session(), store);
}

size_t
ReplayCheckpointStore_Budget(struct ReplayCheckpointStore const* store)
{
    assert(store);
    return store->budget;
}

int
ReplayCheckpointStore_Wants(
    struct ReplayCheckpointStore const* store,
    int tick)
{
    assert(store);
    if( store->budget == 0 || tick < 1 )
        return 0;
    for( int i = 0; i < store->count; i++ )
        if( abs(store->entries[i].tick - tick) < REPLAY_CKPT_STORE_SPACING )
            return 0;
    return 1;
}

static void
store_remove(
    struct ReplayCheckpointStore* store,
    int i)
{
    store->bytes -= store->entries[i].bytes;
    ReplayCheckpoint_Free(store->entries[i].cp);
    memmove(&store->entries[i], &store->entries[i + 1], (size_t)(store->count - i - 1) * sizeof(store->entries[0]));
    store->count--;
}

/* The interior checkpoint whose removal costs least (the header's weight). */
static int
store_cheapest(
    struct ReplayCheckpointStore const* store,
    int playhead)
{
    int best = -1;
    double best_cost = 0.0;

    for( int i = 1; i + 1 < store->count; i++ )
    {
        int const dist = abs(store->entries[i].tick - playhead);
        double const gap = (double)(store->entries[i + 1].tick - store->entries[i - 1].tick);
        double const cost = gap / (double)(dist > REPLAY_CKPT_STORE_SPACING ? dist : REPLAY_CKPT_STORE_SPACING);

        if( best < 0 || cost < best_cost )
        {
            best = i;
            best_cost = cost;
        }
    }
    return best;
}

void
ReplayCheckpointStore_Add(
    struct ReplayCheckpointStore* store,
    int tick,
    uint64_t clock_ms,
    struct ReplayCheckpoint* cp,
    int playhead)
{
    int at = 0;

    assert(store);
    assert(cp);
    if( !ReplayCheckpointStore_Wants(store, tick) )
    {
        ReplayCheckpoint_Free(cp);
        return;
    }
    if( store->count == store->cap )
    {
        int const cap = store->cap ? store->cap * 2 : 16;

        store->entries = ToriMem_Realloc(ToriMem_Session(), store->entries, (size_t)cap * sizeof(store->entries[0]));
        assert(store->entries);
        store->cap = cap;
    }
    while( at < store->count && store->entries[at].tick < tick )
        at++;
    memmove(&store->entries[at + 1], &store->entries[at], (size_t)(store->count - at) * sizeof(store->entries[0]));
    store->entries[at].tick = tick;
    store->entries[at].clock_ms = clock_ms;
    store->entries[at].cp = cp;
    store->entries[at].bytes = ReplayCheckpoint_Bytes(cp);
    store->count++;
    store->bytes += store->entries[at].bytes;
    while( store->bytes > store->budget )
    {
        int const victim = store->count > 2 ? store_cheapest(store, playhead) : 0;

        /* Two left and still over: the budget holds one; keep the newest. */
        store_remove(store, victim >= 0 ? victim : 0);
        if( store->count == 0 )
            break;
    }
}

struct ReplayCheckpoint const*
ReplayCheckpointStore_AtOrBefore(
    struct ReplayCheckpointStore const* store,
    int tick,
    int* out_tick,
    uint64_t* out_clock_ms)
{
    int found = -1;

    assert(store);
    for( int i = 0; i < store->count && store->entries[i].tick <= tick; i++ )
        found = i;
    if( found < 0 )
        return NULL;
    if( out_tick )
        *out_tick = store->entries[found].tick;
    if( out_clock_ms )
        *out_clock_ms = store->entries[found].clock_ms;
    return store->entries[found].cp;
}

int
ReplayCheckpointStore_Count(struct ReplayCheckpointStore const* store)
{
    assert(store);
    return store->count;
}

size_t
ReplayCheckpointStore_Bytes(struct ReplayCheckpointStore const* store)
{
    assert(store);
    return store->bytes;
}

int
ReplayCheckpointStore_TickAt(
    struct ReplayCheckpointStore const* store,
    int i)
{
    assert(store);
    assert(i >= 0);
    assert(i < store->count);
    return store->entries[i].tick;
}
