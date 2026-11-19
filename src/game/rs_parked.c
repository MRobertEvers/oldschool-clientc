#include "game/rs_parked.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * The lookup is a slot array (entries never move between a put and their
 * free, except when the array grows) with a chained hash on (kind, key) for
 * Park_Put's replacement and Park_Find, and a free list through the same
 * chain field.
 *
 * Wants. An entry that wants an asset is a waiter in the assets table whose
 * handle is the entry's (index, generation) and whose function and context
 * are the lookup's own. The table only ever calls that function from
 * Assets_Land / Assets_LandFor, and the function only marks entries ready:
 * applying happens in Park_Land, so it does not matter which caller landed
 * the asset or where in the frame it was.
 *
 * A waiter whose entry was freed, re-wanted or readied some other way is
 * stale. It is not hunted down at once (a cancel of eighty icons would scan
 * the table eighty times): the lookup counts them and drops them all in one
 * Assets_DropWaiters at the next Park_Land. Until then a stale waiter that
 * arrives finds its generation moved on and is ignored.
 *
 * Under an asset kind landed ALL (asset_requests.c), every want of the
 * lookup's for one asset collapses into the first entry's waiter: an arrival
 * readies every live entry wanting that asset, and a stale waiter is kept
 * while any live entry still counts on it.
 */

#define NIL (-1)

struct ParkSlot
{
    struct ParkEntry e;
    uint32_t gen;
    /* Hash chain while used; free list while not. */
    int32_t next;
    uint8_t used;
    /* A want of this entry is registered in the assets table and has not
     * arrived. */
    uint8_t waiter_live;
};

struct ParkKindRow
{
    char const* name;
    ParkLandFn land;
    void* ctx;
    struct ParkPolicy policy;
    uint8_t registered;
};

struct ParkPending
{
    int32_t index;
    uint32_t gen;
    uint32_t order;
};

struct Park
{
    struct Assets* assets;
    ParkClockFn clock;
    void* clock_data;

    struct ParkKindRow kinds[PARK_KIND_MAX];

    struct ParkSlot* slots;
    int cap;
    int count;
    int32_t free_head;
    /* cap buckets (a power of two). */
    int32_t* buckets;

    uint32_t next_order;
    int ready_count;
    int timed_count;
    /* The earliest ready_at of the timed entries (valid when timed_count). */
    uint32_t next_due;
    int stale_waiters;
    bool landing;
    struct ParkStats stats;

    struct ParkPending* pending;
    int pending_cap;
};

#define PARK_INITIAL_CAP 64

/* ---- hashing --------------------------------------------------------------- */

static uint32_t
mix(uint32_t h,
    uint32_t v)
{
    h ^= v;
    h *= 16777619u;
    return h;
}

static uint32_t
key_hash(
    int kind,
    struct ParkKey key)
{
    uint32_t h = 2166136261u;

    h = mix(h, (uint32_t)kind);
    h =
        mix(h,
            (uint32_t)key.shape | ((uint32_t)key.sub << 8) | ((uint32_t)key.level << 16) |
                ((uint32_t)key.layer << 24));
    h = mix(h, (uint32_t)key.id);
    h = mix(h, (uint32_t)key.x);
    h = mix(h, (uint32_t)key.z);
    h = mix(h, key.gen);
    return h;
}

static int32_t*
bucket_of(
    struct Park* park,
    int kind,
    struct ParkKey key)
{
    return &park->buckets[key_hash(kind, key) & (uint32_t)(park->cap - 1)];
}

static void
rebuild_buckets(struct Park* park)
{
    for( int i = 0; i < park->cap; i++ )
        park->buckets[i] = NIL;
    park->free_head = NIL;
    for( int i = park->cap - 1; i >= 0; i-- )
    {
        struct ParkSlot* s = &park->slots[i];
        if( s->used )
        {
            int32_t* b = bucket_of(park, s->e.kind, s->e.key);
            s->next = *b;
            *b = i;
        }
        else
        {
            s->next = park->free_head;
            park->free_head = i;
        }
    }
}

static void
grow(struct Park* park)
{
    int old_cap = park->cap;
    int new_cap = old_cap ? old_cap * 2 : PARK_INITIAL_CAP;

    /* Full is an assert: an entry silently not parked is a target that never
     * shows, with nothing anywhere saying why. */
    assert(new_cap <= PARK_ENTRIES_MAX && "parked lookup full");
    park->slots =
        ToriMem_Realloc(ToriMem_Process(), park->slots, (size_t)new_cap * sizeof(*park->slots));
    assert(park->slots);
    memset(park->slots + old_cap, 0, (size_t)(new_cap - old_cap) * sizeof(*park->slots));
    park->buckets =
        ToriMem_Realloc(ToriMem_Process(), park->buckets, (size_t)new_cap * sizeof(*park->buckets));
    assert(park->buckets);
    park->cap = new_cap;
    rebuild_buckets(park);
}

/* ---- slots ----------------------------------------------------------------- */

static struct ParkSlot*
slot_of(
    struct Park const* park,
    struct ParkRef ref)
{
    struct ParkSlot* s;

    if( ref.index < 0 || ref.index >= park->cap )
        return NULL;
    s = &park->slots[ref.index];
    if( !s->used || s->gen != ref.gen )
        return NULL;
    return s;
}

static struct ParkRef
ref_of(
    struct Park const* park,
    struct ParkSlot const* s)
{
    struct ParkRef ref;
    ref.index = (int32_t)(s - park->slots);
    ref.gen = s->gen;
    return ref;
}

static struct ParkKindRow const*
kind_row(
    struct Park const* park,
    int kind)
{
    assert(kind > 0);
    assert(kind < PARK_KIND_MAX);
    assert(park->kinds[kind].registered && "parked kind not registered");
    return &park->kinds[kind];
}

/* The entry's registered want, if any, is no longer counted on. */
static void
forget_waiter(
    struct Park* park,
    struct ParkSlot* s)
{
    if( s->waiter_live )
    {
        s->waiter_live = 0;
        park->stale_waiters++;
    }
}

static void
leave_ready(
    struct Park* park,
    struct ParkSlot* s)
{
    if( s->e.state == PARK_ENTRY_READY )
    {
        assert(park->ready_count > 0);
        park->ready_count--;
    }
    s->e.state = PARK_ENTRY_WAITING;
}

static void
leave_timed(
    struct Park* park,
    struct ParkSlot* s)
{
    if( s->e.ready_at )
    {
        assert(park->timed_count > 0);
        park->timed_count--;
        s->e.ready_at = 0;
    }
}

static void
make_ready(
    struct Park* park,
    struct ParkSlot* s,
    enum ParkReason reason,
    enum AssetOutcome outcome)
{
    leave_timed(park, s);
    if( s->e.state != PARK_ENTRY_READY )
        park->ready_count++;
    s->e.state = PARK_ENTRY_READY;
    s->e.reason = (uint8_t)reason;
    s->e.outcome = (uint8_t)outcome;
}

static void
slot_free(
    struct Park* park,
    struct ParkSlot* s)
{
    int32_t idx = (int32_t)(s - park->slots);
    int32_t* link = bucket_of(park, s->e.kind, s->e.key);

    while( *link != idx )
    {
        assert(*link != NIL);
        link = &park->slots[*link].next;
    }
    *link = s->next;

    forget_waiter(park, s);
    leave_ready(park, s);
    leave_timed(park, s);
    s->used = 0;
    s->gen++;
    s->next = park->free_head;
    park->free_head = idx;
    assert(park->count > 0);
    park->count--;
}

/* ---- the assets table's waiter ------------------------------------------- */

static bool
wants_asset(
    struct ParkSlot const* s,
    struct AssetKey key)
{
    return s->used && s->waiter_live && s->e.wants.kind == key.kind && s->e.wants.id == key.id &&
           s->e.wants.variant == key.variant;
}

static void
arrive(
    struct Park* park,
    struct ParkSlot* s,
    enum AssetOutcome outcome)
{
    s->waiter_live = 0;
    make_ready(park, s, PARK_REASON_ASSET, outcome);
}

static void
park_asset_land(
    void* ctx,
    struct AssetKey key,
    struct AssetWaiterHandle handle,
    enum AssetOutcome outcome,
    uint32_t late_ms)
{
    struct Park* park = ctx;

    (void)late_ms;
    assert(park);
    if( Assets_Policy((enum AssetKind)key.kind)->landed == ASSET_LANDED_ALL )
    {
        /* One call for every entry wanting it. */
        for( int i = 0; i < park->cap; i++ )
            if( wants_asset(&park->slots[i], key) )
                arrive(park, &park->slots[i], outcome);
        return;
    }
    {
        struct ParkRef ref;
        struct ParkSlot* s;

        ref.index = handle.v[0];
        ref.gen = (uint32_t)handle.v[1];
        s = slot_of(park, ref);
        /* Stale: freed, re-wanted or readied since. */
        if( !s || !wants_asset(s, key) )
            return;
        arrive(park, s, outcome);
    }
}

AssetLandFn
Park_AssetLandFn(void)
{
    return park_asset_land;
}

static bool
waiter_is_stale(
    struct AssetWaiterHandle handle,
    struct AssetKey key,
    void* arg)
{
    struct Park* park = arg;
    struct ParkRef ref;
    struct ParkSlot* s;

    ref.index = handle.v[0];
    ref.gen = (uint32_t)handle.v[1];
    s = slot_of(park, ref);
    if( s && wants_asset(s, key) )
        return false;
    if( Assets_Policy((enum AssetKind)key.kind)->landed == ASSET_LANDED_ALL )
    {
        /* The collapsed waiter carries every live entry wanting the asset. */
        for( int i = 0; i < park->cap; i++ )
            if( wants_asset(&park->slots[i], key) )
                return false;
    }
    return true;
}

static void
drop_stale_waiters(struct Park* park)
{
    if( park->stale_waiters == 0 )
        return;
    (void)Assets_DropWaiters(park->assets, park_asset_land, park, waiter_is_stale, park);
    park->stale_waiters = 0;
}

static bool
waiter_any(
    struct AssetWaiterHandle handle,
    struct AssetKey key,
    void* arg)
{
    (void)handle;
    (void)key;
    (void)arg;
    return true;
}

/* ---- the lookup ------------------------------------------------------------ */

struct Park*
Park_New(
    struct Assets* assets,
    ParkClockFn clock,
    void* clock_data)
{
    struct Park* park;

    assert(assets);
    assert(clock);
    park = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*park));
    assert(park);
    park->assets = assets;
    park->clock = clock;
    park->clock_data = clock_data;
    park->free_head = NIL;
    park->next_order = 1;
    grow(park);
    return park;
}

void
Park_Free(struct Park* park)
{
    if( !park )
        return;
    /* No waiter may outlive the context it names. */
    (void)Assets_DropWaiters(park->assets, park_asset_land, park, waiter_any, NULL);
    ToriMem_Free(ToriMem_Process(), park->slots);
    ToriMem_Free(ToriMem_Process(), park->buckets);
    ToriMem_Free(ToriMem_Process(), park->pending);
    ToriMem_Free(ToriMem_Process(), park);
}

void
Park_RegisterKind(
    struct Park* park,
    int kind,
    char const* name,
    ParkLandFn land,
    void* ctx,
    struct ParkPolicy const* policy)
{
    struct ParkKindRow* row;

    assert(park);
    assert(kind > 0);
    assert(kind < PARK_KIND_MAX);
    assert(name);
    assert(land);
    assert(policy);
    row = &park->kinds[kind];
    assert(!row->registered && "parked kind registered twice");
    row->name = name;
    row->land = land;
    row->ctx = ctx;
    row->policy = *policy;
    row->registered = 1;
}

bool
Park_KindRegistered(
    struct Park const* park,
    int kind)
{
    assert(park);
    assert(kind > 0);
    assert(kind < PARK_KIND_MAX);
    return park->kinds[kind].registered != 0;
}

char const*
Park_KindName(
    struct Park const* park,
    int kind)
{
    assert(park);
    return kind_row(park, kind)->name;
}

struct ParkRef
Park_Put(
    struct Park* park,
    int kind,
    struct ParkKey key,
    uint32_t scene_serial,
    void const* what,
    size_t what_size)
{
    struct ParkKindRow const* row;
    struct ParkSlot* s;
    int32_t idx;
    int32_t* b;

    assert(park);
    assert(key.shape != PARK_KEY_NONE);
    assert(what_size <= PARK_WHAT_MAX);
    if( what_size > 0 )
        assert(what);
    row = kind_row(park, kind);

    if( row->policy.newer == PARK_NEWER_REPLACE )
    {
        int32_t n = *bucket_of(park, kind, key);
        while( n != NIL )
        {
            int32_t next = park->slots[n].next;
            if( park->slots[n].e.kind == kind && ParkKey_Equal(park->slots[n].e.key, key) )
            {
                slot_free(park, &park->slots[n]);
                park->stats.replaced++;
            }
            n = next;
        }
    }

    if( park->free_head == NIL )
        grow(park);
    idx = park->free_head;
    s = &park->slots[idx];
    park->free_head = s->next;

    memset(&s->e, 0, sizeof(s->e));
    s->e.kind = (uint16_t)kind;
    s->e.state = PARK_ENTRY_WAITING;
    s->e.key = key;
    s->e.scene_serial = scene_serial;
    s->e.order = park->next_order++;
    s->e.arrived_cycle = park->clock(park->clock_data);
    s->e.wants.kind = -1;
    s->e.wants.id = -1;
    s->e.wants.variant = 0;
    s->e.what_size = (uint32_t)what_size;
    if( what_size > 0 )
        memcpy(s->e.what.bytes, what, what_size);
    s->used = 1;
    s->waiter_live = 0;

    b = bucket_of(park, kind, key);
    s->next = *b;
    *b = idx;
    park->count++;
    park->stats.put++;
    return ref_of(park, s);
}

enum AssetState
Park_Want(
    struct Park* park,
    struct ParkRef ref,
    struct AssetKey asset)
{
    struct ParkSlot* s;
    struct AssetWaiter waiter;
    enum AssetState state;

    assert(park);
    s = slot_of(park, ref);
    assert(s && "Park_Want on an entry that is gone");
    forget_waiter(park, s);
    leave_ready(park, s);
    leave_timed(park, s);
    s->e.wants = asset;

    waiter.land = park_asset_land;
    waiter.ctx = park;
    memset(&waiter.handle, 0, sizeof(waiter.handle));
    waiter.handle.v[0] = ref.index;
    waiter.handle.v[1] = (int32_t)ref.gen;
    waiter.handle.v[2] = s->e.kind;
    state = Assets_Want(park->assets, asset, &waiter);
    /* The table may have grown nothing of ours, but a resident check can run
     * a loader that lands nothing: `s` is still ours either way. */
    s = slot_of(park, ref);
    assert(s);
    if( state == ASSET_STATE_HERE )
        make_ready(park, s, PARK_REASON_ASSET, ASSET_OUTCOME_LANDED);
    else if( state == ASSET_STATE_FAILED )
        make_ready(park, s, PARK_REASON_ASSET, ASSET_OUTCOME_FAILED);
    else
        s->waiter_live = 1;
    return state;
}

void
Park_Ready(
    struct Park* park,
    struct ParkRef ref)
{
    struct ParkSlot* s;

    assert(park);
    s = slot_of(park, ref);
    assert(s && "Park_Ready on an entry that is gone");
    if( s->e.state == PARK_ENTRY_READY )
        return;
    forget_waiter(park, s);
    make_ready(park, s, PARK_REASON_READY, ASSET_OUTCOME_LANDED);
}

void
Park_ReadyAt(
    struct Park* park,
    struct ParkRef ref,
    uint32_t cycle)
{
    struct ParkSlot* s;

    assert(park);
    assert(cycle > 0);
    s = slot_of(park, ref);
    assert(s && "Park_ReadyAt on an entry that is gone");
    forget_waiter(park, s);
    leave_ready(park, s);
    if( !s->e.ready_at )
        park->timed_count++;
    s->e.ready_at = cycle;
    if( park->timed_count == 1 || cycle < park->next_due )
        park->next_due = cycle;
}

struct ParkEntry const*
Park_Get(
    struct Park const* park,
    struct ParkRef ref)
{
    struct ParkSlot const* s;

    assert(park);
    s = slot_of(park, ref);
    return s ? &s->e : NULL;
}

struct ParkRef
Park_Find(
    struct Park const* park,
    int kind,
    struct ParkKey key)
{
    struct ParkRef best = { NIL, 0 };
    uint32_t best_order = 0;
    int32_t n;

    assert(park);
    (void)kind_row(park, kind);
    n = park->buckets[key_hash(kind, key) & (uint32_t)(park->cap - 1)];
    for( ; n != NIL; n = park->slots[n].next )
    {
        struct ParkSlot const* s = &park->slots[n];
        if( s->e.kind != kind || !ParkKey_Equal(s->e.key, key) )
            continue;
        if( best.index == NIL || s->e.order > best_order )
        {
            best = ref_of(park, s);
            best_order = s->e.order;
        }
    }
    return best;
}

void
Park_Remove(
    struct Park* park,
    struct ParkRef ref)
{
    struct ParkSlot* s;

    assert(park);
    s = slot_of(park, ref);
    if( !s )
        return;
    slot_free(park, s);
    park->stats.cancelled++;
}

/* ---- scopes ---------------------------------------------------------------- */

static bool
in_scope(
    struct ParkEntry const* e,
    struct ParkScope const* scope)
{
    if( scope->kind == PARK_SCOPE_ALL )
        return true;
    if( scope->kind == PARK_SCOPE_SCENE )
        return e->scene_serial == (uint32_t)scope->id;
    if( scope->kind == PARK_SCOPE_VIEW )
        return e->key.shape == PARK_KEY_VIEW && e->key.id == scope->id;
    if( scope->kind == PARK_SCOPE_ENTITY )
        return e->key.shape == PARK_KEY_ENTITY && e->key.sub == scope->sub &&
               e->key.id == scope->id;
    if( scope->kind == PARK_SCOPE_GROUP )
        return e->key.shape == PARK_KEY_COMPONENT &&
               (int32_t)((uint32_t)e->key.id >> 16) == scope->id;
    if( scope->kind == PARK_SCOPE_KIND )
        return e->kind == scope->id;
    if( scope->kind == PARK_SCOPE_KEY )
        return e->kind == scope->id && ParkKey_Equal(e->key, scope->key);
    assert(0 && "unknown parked scope");
    return false;
}

static void
check_scope(struct ParkScope const* scope)
{
    /* Scene serial 0 is "not in a scene": cancelling it would cancel every
     * kind that never belonged to one. */
    if( scope->kind == PARK_SCOPE_SCENE )
        assert(scope->id != 0);
    if( scope->kind == PARK_SCOPE_KIND || scope->kind == PARK_SCOPE_KEY )
    {
        assert(scope->id > 0);
        assert(scope->id < PARK_KIND_MAX);
    }
}

int
Park_Cancel(
    struct Park* park,
    struct ParkScope scope)
{
    int cancelled = 0;

    assert(park);
    check_scope(&scope);
    for( int i = 0; i < park->cap; i++ )
    {
        struct ParkSlot* s = &park->slots[i];
        if( !s->used || !in_scope(&s->e, &scope) )
            continue;
        slot_free(park, s);
        cancelled++;
    }
    park->stats.cancelled += (uint32_t)cancelled;
    return cancelled;
}

int
Park_ReadyWhere(
    struct Park* park,
    struct ParkScope scope)
{
    int readied = 0;

    assert(park);
    check_scope(&scope);
    for( int i = 0; i < park->cap; i++ )
    {
        struct ParkSlot* s = &park->slots[i];
        if( !s->used || s->e.state == PARK_ENTRY_READY || !in_scope(&s->e, &scope) )
            continue;
        forget_waiter(park, s);
        make_ready(park, s, PARK_REASON_READY, ASSET_OUTCOME_LANDED);
        readied++;
    }
    return readied;
}

/* ---- landing --------------------------------------------------------------- */

static void
ready_due(
    struct Park* park,
    uint32_t now)
{
    uint32_t next = 0;

    if( park->timed_count == 0 || now < park->next_due )
        return;
    for( int i = 0; i < park->cap; i++ )
    {
        struct ParkSlot* s = &park->slots[i];
        if( !s->used || !s->e.ready_at )
            continue;
        if( s->e.ready_at <= now )
            make_ready(park, s, PARK_REASON_TIME, ASSET_OUTCOME_LANDED);
        else if( !next || s->e.ready_at < next )
            next = s->e.ready_at;
    }
    park->next_due = next;
}

static int
pending_cmp(
    void const* a,
    void const* b)
{
    uint32_t oa = ((struct ParkPending const*)a)->order;
    uint32_t ob = ((struct ParkPending const*)b)->order;
    return oa < ob ? -1 : oa > ob ? 1 : 0;
}

int
Park_Land(
    struct Park* park,
    int budget)
{
    uint32_t now;
    int pending = 0;
    int calls = 0;

    assert(park);
    assert(budget > 0);
    assert(!park->landing && "Park_Land is not re-entrant");
    drop_stale_waiters(park);
    now = park->clock(park->clock_data);
    ready_due(park, now);
    if( park->ready_count == 0 )
        return 0;

    /* This call's arrivals, taken first and in arrival order: one that
     * becomes ready while they land waits for the next call. */
    if( park->ready_count > park->pending_cap )
    {
        park->pending_cap = park->ready_count;
        park->pending = ToriMem_Realloc(
            ToriMem_Process(), park->pending, (size_t)park->pending_cap * sizeof(*park->pending));
        assert(park->pending);
    }
    for( int i = 0; i < park->cap; i++ )
    {
        struct ParkSlot const* s = &park->slots[i];
        if( !s->used || s->e.state != PARK_ENTRY_READY )
            continue;
        park->pending[pending].index = i;
        park->pending[pending].gen = s->gen;
        park->pending[pending].order = s->e.order;
        pending++;
    }
    assert(pending == park->ready_count);
    qsort(park->pending, (size_t)pending, sizeof(*park->pending), pending_cmp);

    park->landing = true;
    for( int p = 0; p < pending && calls < budget; p++ )
    {
        struct ParkRef ref = { park->pending[p].index, park->pending[p].gen };
        struct ParkSlot* s = slot_of(park, ref);
        struct ParkKindRow const* row;
        struct ParkEntry copy;
        struct ParkArrival arrival;
        uint32_t late;
        enum ParkLandResult result;

        /* Cancelled, replaced or re-wanted by a landing before it. */
        if( !s || s->e.state != PARK_ENTRY_READY )
            continue;
        row = kind_row(park, s->e.kind);
        late = now >= s->e.arrived_cycle ? now - s->e.arrived_cycle : 0;

        if( (s->e.reason == PARK_REASON_ASSET && s->e.outcome == ASSET_OUTCOME_DROPPED) ||
            (s->e.reason == PARK_REASON_ASSET && s->e.outcome == ASSET_OUTCOME_FAILED &&
             row->policy.failed == PARK_FAILED_DROP) ||
            (row->policy.late == PARK_LATE_DROP_AFTER && late > row->policy.late_limit_cycles) )
        {
            slot_free(park, s);
            park->stats.dropped++;
            continue;
        }

        arrival.reason = (enum ParkReason)s->e.reason;
        arrival.outcome = (enum AssetOutcome)s->e.outcome;
        arrival.late_cycles = row->policy.late == PARK_LATE_LAND_SHORTENED ? late : 0;
        copy = s->e;
        /* Waiting for nothing while its landing runs: the landing may want
         * the next link, set a time, or neither (done). */
        leave_ready(park, s);

        result = row->land(row->ctx, park, ref, &copy, &arrival);
        calls++;
        park->stats.landed++;

        s = slot_of(park, ref);
        if( result == PARK_LAND_DONE )
        {
            /* Unless the landing freed it itself. */
            if( s )
                slot_free(park, s);
            continue;
        }
        assert(result == PARK_LAND_KEEP);
        assert(s && "a landing kept an entry it freed");
        assert(
            (s->waiter_live || s->e.ready_at || s->e.state == PARK_ENTRY_READY) &&
            "a landing kept an entry that waits for nothing");
    }
    park->landing = false;
    return calls;
}

bool
Park_Landing(struct Park const* park)
{
    assert(park);
    return park->landing;
}

/* ---- counts ---------------------------------------------------------------- */

struct ParkStats
Park_Stats(struct Park const* park)
{
    assert(park);
    return park->stats;
}

int
Park_Count(
    struct Park const* park,
    int kind)
{
    int count = 0;

    assert(park);
    if( kind == 0 )
        return park->count;
    for( int i = 0; i < park->cap; i++ )
        if( park->slots[i].used && park->slots[i].e.kind == kind )
            count++;
    return count;
}

int
Park_ReadyCount(struct Park const* park)
{
    int count;
    uint32_t now;

    assert(park);
    count = park->ready_count;
    if( park->timed_count == 0 )
        return count;
    now = park->clock(park->clock_data);
    if( now < park->next_due )
        return count;
    for( int i = 0; i < park->cap; i++ )
        if( park->slots[i].used && park->slots[i].e.ready_at && park->slots[i].e.ready_at <= now )
            count++;
    return count;
}
