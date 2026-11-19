#include "engine/asset_requests.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * THE ONE POLICY TABLE. A row per kind; what each choice means is in the
 * header. Why each row is what it is:
 *
 *  MODEL        a read that came back empty may be an outage (a dropped JS5
 *               socket, a browser storage error): back off, never give up.
 *  TEXTURE      a texture the cache cannot serve never will: remembered, so a
 *               model rebuilt a hundred times asks once (was texture_failed).
 *  SPRITE       as MODEL.
 *  SEQUENCE     a failed decode registers an empty sentinel, so the cache
 *               itself remembers; the bind catches its frames up from the
 *               cycle the sequence started on, which is what "shortened" is.
 *  SPOTANIM     asked once per graphic (was load_enqueued); the next graphic
 *               asks again.
 *  NPC_BODY     one load dresses every npc of the type (was RS_NpcBodyLands);
 *               the next spawn of a cold type asks again.
 *  PLAYER_HELD  as NPC_BODY: the per-frame body reconcile dresses everyone.
 *  PLAYER_TRANSMOG  as NPC_BODY.
 *  OBJ_MODEL    a ground stack goes back four times for a model the loader
 *               says is still missing, then draws nothing, as the reference
 *               does (was APP_PLACEHOLDER_ATTEMPT_MAX).
 *  INV_ICONS    one pass in flight; a pass that resolved nothing waits 500 ms
 *               doubling to 4 s, and never gives up (was InvIconRetry).
 *  IF_GROUP     a missing pack is reported by the open; asked again next open.
 *  SOUND        asked once while in flight (was load_requested, and the
 *               ambient bed asked every tick); a sound more than ten client
 *               cycles of 20 ms late is dropped (the reference's delays[i] >=
 *               -10).
 *  MUSIC        a second song cancels the first: one loader of the kind, and
 *               a song that finishes loading after a newer request is not
 *               installed (was request_loading, cleared under a running load).
 *  SKYBOX       a new skybox replaces the old; a failed load is retried with
 *               back-off (was asked once per want and never again).
 *  REF_TABLE    loaded once while every map square waits on it (was
 *               reference_table_loading); a failure releases them all, and the
 *               next square to ask tries again.
 */
static struct AssetPolicy const g_asset_policy[ASSET_KIND_COUNT] = {
    [ASSET_KIND_MODEL] = { "model", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_LATE_LAND, 0,
                           ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_TEXTURE] = { "texture", ASSET_FAIL_REMEMBER, 0, 0, 0, ASSET_LATE_LAND, 0,
                             ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_SPRITE] = { "sprite", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_LATE_LAND, 0,
                            ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_SEQUENCE] = { "sequence", ASSET_FAIL_REMEMBER, 0, 0, 0, ASSET_LATE_LAND_SHORTENED,
                              0, ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_SPOTANIM] = { "spotanim", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_LATE_LAND, 0,
                              ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_NPC_BODY] = { "npc_body", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_LATE_LAND, 0,
                              ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ALL },
    [ASSET_KIND_PLAYER_HELD] = { "player_held", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_LATE_LAND, 0,
                                 ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ALL },
    [ASSET_KIND_PLAYER_TRANSMOG] = { "player_transmog", ASSET_FAIL_GIVE_UP, 1, 0, 0,
                                     ASSET_LATE_LAND, 0, ASSET_NEWER_KEEP_BOTH,
                                     ASSET_LANDED_ALL },
    [ASSET_KIND_OBJ_MODEL] = { "obj_model", ASSET_FAIL_GIVE_UP, 4, 0, 0, ASSET_LATE_LAND, 0,
                               ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_INV_ICONS] = { "inv_icons", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_LATE_LAND, 0,
                               ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ALL },
    [ASSET_KIND_IF_GROUP] = { "if_group", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_LATE_LAND, 0,
                              ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_SOUND] = { "sound", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_LATE_DROP_AFTER, 200,
                           ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ONE },
    [ASSET_KIND_MUSIC] = { "music", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_LATE_LAND, 0,
                           ASSET_NEWER_REPLACE, ASSET_LANDED_ONE },
    [ASSET_KIND_SKYBOX] = { "skybox", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_LATE_LAND, 0,
                            ASSET_NEWER_REPLACE, ASSET_LANDED_ONE },
    [ASSET_KIND_REF_TABLE] = { "ref_table", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_LATE_LAND, 0,
                               ASSET_NEWER_KEEP_BOTH, ASSET_LANDED_ALL },
};

enum
{
    NODE_FREE = 0,
    NODE_WANT,
    NODE_JOIN,
    /* A want dropped by Assets_DropWaiters, or a join released by
     * Assets_Unjoin: still linked, skipped, freed where it is next met. */
    NODE_DEAD,
};

struct AssetNode
{
    int32_t next;
    uint8_t type;
    uint8_t outcome;
    struct AssetKey key;
    int* join;
    struct AssetWaiter waiter;
    uint64_t wanted_at;
};

struct AssetEntry
{
    struct AssetKey key;
    uint8_t used;
    uint8_t state; /* enum AssetState: WANTED, LOADING or FAILED */
    /* REPLACE kinds: a newer request replaced this one while its loader ran.
     * Its end is discarded and the newer one starts. */
    uint8_t superseded;
    int32_t attempts;
    uint32_t wait_ms;
    uint64_t retry_at;
    int32_t head;
    int32_t tail;
};

struct Assets
{
    struct AssetEntry* entries;
    int entry_cap; /* a power of two */
    int entry_count;

    struct AssetNode* nodes;
    int node_cap;
    int32_t node_free;

    /* Arrived want waiters, oldest first. */
    int32_t arrived_head;
    int32_t arrived_tail;

    struct AssetLoader loaders[ASSET_KIND_COUNT];
    int kind_loading[ASSET_KIND_COUNT];
    uint32_t loads_started[ASSET_KIND_COUNT];

    uint64_t now_ms;
    uint32_t serial;

    struct AssetKey* scratch;
    int scratch_cap;
};

#define NIL (-1)

/* ---- policy -------------------------------------------------------------- */

struct AssetPolicy const*
Assets_Policy(enum AssetKind kind)
{
    assert(kind >= 0);
    assert(kind < ASSET_KIND_COUNT);
    assert(g_asset_policy[kind].name);
    return &g_asset_policy[kind];
}

uint32_t
Assets_BackoffNext(
    struct AssetPolicy const* policy,
    uint32_t wait_ms)
{
    uint32_t next;

    assert(policy);
    assert(policy->fail == ASSET_FAIL_BACKOFF);
    if( wait_ms == 0 )
        return policy->backoff_first_ms;
    next = wait_ms * 2u;
    return next > policy->backoff_max_ms ? policy->backoff_max_ms : next;
}

/* ---- keys and the entry hash -------------------------------------------- */

static bool
key_eq(
    struct AssetKey a,
    struct AssetKey b)
{
    return a.kind == b.kind && a.id == b.id && a.variant == b.variant;
}

static uint32_t
key_hash(struct AssetKey key)
{
    uint32_t h = (uint32_t)key.id * 0x9E3779B1u;
    h ^= (uint32_t)key.variant * 0x85EBCA77u;
    h ^= (uint32_t)key.kind * 0xC2B2AE3Du;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h;
}

static void
check_key(struct AssetKey key)
{
    assert(key.kind >= 0);
    assert(key.kind < ASSET_KIND_COUNT);
}

static int
entry_find(
    struct Assets const* assets,
    struct AssetKey key)
{
    uint32_t mask = (uint32_t)assets->entry_cap - 1u;
    uint32_t i = key_hash(key) & mask;

    for( ;; )
    {
        struct AssetEntry const* e = &assets->entries[i];
        if( !e->used )
            return -1;
        if( key_eq(e->key, key) )
            return (int)i;
        i = (i + 1u) & mask;
    }
}

static void
entry_place(
    struct AssetEntry* table,
    int cap,
    struct AssetEntry const* src)
{
    uint32_t mask = (uint32_t)cap - 1u;
    uint32_t i = key_hash(src->key) & mask;

    while( table[i].used )
        i = (i + 1u) & mask;
    table[i] = *src;
}

static void
entries_grow(struct Assets* assets)
{
    int new_cap = assets->entry_cap * 2;
    struct AssetEntry* grown;

    /* Full is an assert, never a silent stop of dedupe. */
    assert(new_cap <= ASSETS_ENTRIES_MAX * 2 && "assets table full");
    grown = ToriMem_Calloc(ToriMem_Process(), (size_t)new_cap, sizeof(*grown));
    assert(grown);
    for( int i = 0; i < assets->entry_cap; i++ )
        if( assets->entries[i].used )
            entry_place(grown, new_cap, &assets->entries[i]);
    ToriMem_Free(ToriMem_Process(), assets->entries);
    assets->entries = grown;
    assets->entry_cap = new_cap;
}

static int
entry_insert(
    struct Assets* assets,
    struct AssetKey key)
{
    struct AssetEntry fresh;

    assert(assets->entry_count < ASSETS_ENTRIES_MAX && "assets table full");
    if( (assets->entry_count + 1) * 2 > assets->entry_cap )
        entries_grow(assets);
    memset(&fresh, 0, sizeof(fresh));
    fresh.key = key;
    fresh.used = 1;
    fresh.state = ASSET_STATE_WANTED;
    fresh.head = NIL;
    fresh.tail = NIL;
    entry_place(assets->entries, assets->entry_cap, &fresh);
    assets->entry_count++;
    return entry_find(assets, key);
}

/* Backward-shift delete: no tombstones, so a lookup always stops at the first
 * empty slot. Moves other entries: an index is not held across this. */
static void
entry_remove_at(
    struct Assets* assets,
    int idx)
{
    uint32_t mask = (uint32_t)assets->entry_cap - 1u;
    uint32_t hole = (uint32_t)idx;
    uint32_t i = hole;

    assert(assets->entries[idx].used);
    assert(assets->entries[idx].head == NIL);
    for( ;; )
    {
        uint32_t home;

        i = (i + 1u) & mask;
        if( !assets->entries[i].used )
            break;
        home = key_hash(assets->entries[i].key) & mask;
        /* The entry at i stays when its home lies in the cyclic range
         * (hole, i]: moving it before its home would hide it. */
        if( hole <= i ? (hole < home && home <= i) : (hole < home || home <= i) )
            continue;
        assets->entries[hole] = assets->entries[i];
        hole = i;
    }
    memset(&assets->entries[hole], 0, sizeof(assets->entries[hole]));
    assets->entry_count--;
}

/* ---- waiter nodes ------------------------------------------------------- */

static void
nodes_grow(struct Assets* assets)
{
    int old_cap = assets->node_cap;
    int new_cap = old_cap ? old_cap * 2 : 64;

    assert(new_cap <= ASSETS_WAITERS_MAX * 2 && "assets waiters full");
    assets->nodes = ToriMem_Realloc(
        ToriMem_Process(), assets->nodes, (size_t)new_cap * sizeof(*assets->nodes));
    assert(assets->nodes);
    for( int i = new_cap - 1; i >= old_cap; i-- )
    {
        memset(&assets->nodes[i], 0, sizeof(assets->nodes[i]));
        assets->nodes[i].next = assets->node_free;
        assets->node_free = i;
    }
    assets->node_cap = new_cap;
}

static int32_t
node_alloc(struct Assets* assets)
{
    int32_t n;

    if( assets->node_free == NIL )
        nodes_grow(assets);
    n = assets->node_free;
    assets->node_free = assets->nodes[n].next;
    memset(&assets->nodes[n], 0, sizeof(assets->nodes[n]));
    assets->nodes[n].next = NIL;
    return n;
}

static void
node_release(
    struct Assets* assets,
    int32_t n)
{
    memset(&assets->nodes[n], 0, sizeof(assets->nodes[n]));
    assets->nodes[n].type = NODE_FREE;
    assets->nodes[n].next = assets->node_free;
    assets->node_free = n;
}

static void
entry_append(
    struct Assets* assets,
    int idx,
    int32_t n)
{
    struct AssetEntry* e = &assets->entries[idx];

    assets->nodes[n].next = NIL;
    if( e->tail == NIL )
        e->head = n;
    else
        assets->nodes[e->tail].next = n;
    e->tail = n;
}

static void
arrived_append(
    struct Assets* assets,
    int32_t n,
    enum AssetOutcome outcome)
{
    assets->nodes[n].outcome = (uint8_t)outcome;
    assets->nodes[n].next = NIL;
    if( assets->arrived_tail == NIL )
        assets->arrived_head = n;
    else
        assets->nodes[assets->arrived_tail].next = n;
    assets->arrived_tail = n;
}

static bool
entry_has_want(
    struct Assets const* assets,
    int idx)
{
    for( int32_t n = assets->entries[idx].head; n != NIL; n = assets->nodes[n].next )
        if( assets->nodes[n].type == NODE_WANT )
            return true;
    return false;
}

/* Release every join counting this entry's attempt: the attempt ended. Wants
 * stay. Dead nodes are freed on the way. */
static void
entry_release_joins(
    struct Assets* assets,
    int idx)
{
    struct AssetEntry* e = &assets->entries[idx];
    int32_t n = e->head;
    int32_t keep_head = NIL;
    int32_t keep_tail = NIL;

    while( n != NIL )
    {
        int32_t next = assets->nodes[n].next;
        uint8_t type = assets->nodes[n].type;

        if( type == NODE_JOIN || type == NODE_DEAD )
        {
            if( type == NODE_JOIN && assets->nodes[n].join )
            {
                assert(*assets->nodes[n].join > 0);
                (*assets->nodes[n].join)--;
            }
            node_release(assets, n);
        }
        else
        {
            assets->nodes[n].next = NIL;
            if( keep_tail == NIL )
                keep_head = n;
            else
                assets->nodes[keep_tail].next = n;
            keep_tail = n;
        }
        n = next;
    }
    e->head = keep_head;
    e->tail = keep_tail;
}

/* Move every want of the entry to the arrived list with `outcome`; joins are
 * released. The entry's list is empty afterwards. */
static void
entry_arrive_all(
    struct Assets* assets,
    int idx,
    enum AssetOutcome outcome)
{
    struct AssetEntry* e;
    int32_t n;

    entry_release_joins(assets, idx);
    e = &assets->entries[idx];
    n = e->head;
    while( n != NIL )
    {
        int32_t next = assets->nodes[n].next;
        assert(assets->nodes[n].type == NODE_WANT);
        arrived_append(assets, n, outcome);
        n = next;
    }
    e->head = NIL;
    e->tail = NIL;
}

/* ---- loads --------------------------------------------------------------- */

static void
start_load(
    struct Assets* assets,
    struct AssetKey key);

/* REPLACE kinds: with no loader of the kind running, start the one entry that
 * is still wanted. */
static void
start_next_of_kind(
    struct Assets* assets,
    enum AssetKind kind)
{
    struct AssetKey next;
    bool found = false;

    if( Assets_Policy(kind)->newer != ASSET_NEWER_REPLACE || assets->kind_loading[kind] > 0 )
        return;
    for( int i = 0; i < assets->entry_cap; i++ )
    {
        struct AssetEntry const* e = &assets->entries[i];
        if( !e->used || e->key.kind != (int32_t)kind || e->state != ASSET_STATE_WANTED ||
            e->superseded )
            continue;
        assert(!found && "a REPLACE kind holds one wanted entry");
        next = e->key;
        found = true;
    }
    if( found )
        start_load(assets, next);
}

static void
start_load(
    struct Assets* assets,
    struct AssetKey key)
{
    struct AssetLoader const* loader = &assets->loaders[key.kind];
    int idx = entry_find(assets, key);
    enum AssetStart started;

    assert(idx >= 0);
    assert(assets->entries[idx].state == ASSET_STATE_WANTED);
    assert(loader->start && "no loader for this asset kind");
    assets->entries[idx].state = ASSET_STATE_LOADING;
    assets->kind_loading[key.kind]++;
    assets->loads_started[key.kind]++;
    started = loader->start(loader->data, assets, key);
    if( started == ASSET_START_HERE )
        Assets_Finished(assets, key, true);
    else if( started == ASSET_START_FAILED )
        Assets_Finished(assets, key, false);
    else
        assert(started == ASSET_START_LOADING);
}

/* A newer request of a REPLACE kind: every other entry of the kind loses its
 * waiters (DROPPED); one whose loader runs is kept until it ends, the rest go. */
static void
supersede_others(
    struct Assets* assets,
    struct AssetKey keep)
{
    struct AssetKey gone[8];
    int gone_count = 0;

    for( int i = 0; i < assets->entry_cap; i++ )
    {
        struct AssetEntry* e = &assets->entries[i];
        if( !e->used || e->key.kind != keep.kind || key_eq(e->key, keep) )
            continue;
        if( e->state == ASSET_STATE_LOADING )
        {
            /* Its awaiters are released when the loader ends; its wants are
             * replaced now. */
            int32_t n = e->head;
            int32_t keep_head = NIL;
            int32_t keep_tail = NIL;
            while( n != NIL )
            {
                int32_t next = assets->nodes[n].next;
                if( assets->nodes[n].type == NODE_WANT )
                    arrived_append(assets, n, ASSET_OUTCOME_DROPPED);
                else
                {
                    assets->nodes[n].next = NIL;
                    if( keep_tail == NIL )
                        keep_head = n;
                    else
                        assets->nodes[keep_tail].next = n;
                    keep_tail = n;
                }
                n = next;
            }
            e->head = keep_head;
            e->tail = keep_tail;
            e->superseded = 1;
            continue;
        }
        assert(gone_count < (int)(sizeof(gone) / sizeof(gone[0])));
        gone[gone_count++] = e->key;
    }
    for( int g = 0; g < gone_count; g++ )
    {
        int idx = entry_find(assets, gone[g]);
        assert(idx >= 0);
        entry_arrive_all(assets, idx, ASSET_OUTCOME_DROPPED);
        entry_remove_at(assets, idx);
    }
}

void
Assets_Finished(
    struct Assets* assets,
    struct AssetKey key,
    bool ok)
{
    struct AssetPolicy const* policy;
    struct AssetEntry* e;
    int idx;

    assert(assets);
    check_key(key);
    idx = entry_find(assets, key);
    assert(idx >= 0 && "a load ended that the table did not start");
    e = &assets->entries[idx];
    assert(e->state == ASSET_STATE_LOADING);
    policy = Assets_Policy((enum AssetKind)key.kind);
    assets->kind_loading[key.kind]--;
    assert(assets->kind_loading[key.kind] >= 0);
    assets->serial++;

    if( e->superseded )
    {
        entry_arrive_all(assets, idx, ASSET_OUTCOME_DROPPED);
        entry_remove_at(assets, idx);
        start_next_of_kind(assets, (enum AssetKind)key.kind);
        return;
    }

    if( ok )
    {
        entry_arrive_all(assets, idx, ASSET_OUTCOME_LANDED);
        entry_remove_at(assets, idx);
        start_next_of_kind(assets, (enum AssetKind)key.kind);
        return;
    }

    e->attempts++;
    if( policy->fail == ASSET_FAIL_REMEMBER )
    {
        entry_arrive_all(assets, idx, ASSET_OUTCOME_FAILED);
        assets->entries[idx].state = ASSET_STATE_FAILED;
    }
    else if( policy->fail == ASSET_FAIL_BACKOFF )
    {
        entry_release_joins(assets, idx);
        e = &assets->entries[idx];
        e->wait_ms = Assets_BackoffNext(policy, e->wait_ms);
        e->retry_at = assets->now_ms + e->wait_ms;
        e->state = ASSET_STATE_WANTED;
    }
    else
    {
        assert(policy->fail == ASSET_FAIL_GIVE_UP);
        assert(policy->fail_attempts >= 1);
        entry_release_joins(assets, idx);
        e = &assets->entries[idx];
        if( e->attempts < policy->fail_attempts && e->head != NIL )
        {
            /* Go back for it at once: someone is still waiting. */
            e->state = ASSET_STATE_WANTED;
            start_load(assets, key);
            return;
        }
        entry_arrive_all(assets, idx, ASSET_OUTCOME_FAILED);
        entry_remove_at(assets, idx);
    }
    start_next_of_kind(assets, (enum AssetKind)key.kind);
}

void
Assets_Abandoned(
    struct Assets* assets,
    struct AssetKey key)
{
    struct AssetEntry* e;
    int idx;

    assert(assets);
    check_key(key);
    idx = entry_find(assets, key);
    assert(idx >= 0 && "a load was abandoned that the table did not start");
    e = &assets->entries[idx];
    assert(e->state == ASSET_STATE_LOADING);
    assets->kind_loading[key.kind]--;
    entry_release_joins(assets, idx);
    e = &assets->entries[idx];
    if( e->superseded )
    {
        entry_arrive_all(assets, idx, ASSET_OUTCOME_DROPPED);
        entry_remove_at(assets, idx);
        return;
    }
    e->state = ASSET_STATE_WANTED;
    e->retry_at = 0;
}

/* ---- the table ----------------------------------------------------------- */

struct Assets*
Assets_New(void)
{
    struct Assets* assets = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*assets));

    assert(assets);
    assets->entry_cap = 256;
    assets->entries =
        ToriMem_Calloc(ToriMem_Process(), (size_t)assets->entry_cap, sizeof(*assets->entries));
    assert(assets->entries);
    assets->node_free = NIL;
    assets->arrived_head = NIL;
    assets->arrived_tail = NIL;
    return assets;
}

void
Assets_Free(struct Assets* assets)
{
    if( !assets )
        return;
    ToriMem_Free(ToriMem_Process(), assets->entries);
    ToriMem_Free(ToriMem_Process(), assets->nodes);
    ToriMem_Free(ToriMem_Process(), assets->scratch);
    ToriMem_Free(ToriMem_Process(), assets);
}

void
Assets_SetLoader(
    struct Assets* assets,
    enum AssetKind kind,
    struct AssetLoader const* loader)
{
    assert(assets);
    assert(kind >= 0);
    assert(kind < ASSET_KIND_COUNT);
    assert(loader);
    assert(loader->start);
    assets->loaders[kind] = *loader;
}

void
Assets_SetClock(
    struct Assets* assets,
    uint64_t now_ms)
{
    assert(assets);
    assets->now_ms = now_ms;
}

uint64_t
Assets_Clock(struct Assets const* assets)
{
    assert(assets);
    return assets->now_ms;
}

static bool
key_resident(
    struct Assets const* assets,
    struct AssetKey key)
{
    struct AssetLoader const* loader = &assets->loaders[key.kind];
    return loader->resident && loader->resident(loader->data, key);
}

/* An entry not loading whose asset turned up by another road: land it. */
static bool
entry_land_if_resident(
    struct Assets* assets,
    int idx)
{
    struct AssetKey key = assets->entries[idx].key;

    if( assets->entries[idx].state != ASSET_STATE_WANTED || !key_resident(assets, key) )
        return false;
    entry_arrive_all(assets, idx, ASSET_OUTCOME_LANDED);
    entry_remove_at(assets, idx);
    return true;
}

static void
add_want(
    struct Assets* assets,
    int idx,
    struct AssetWaiter const* waiter)
{
    struct AssetPolicy const* policy;
    int32_t n;

    assert(waiter->land);
    policy = Assets_Policy((enum AssetKind)assets->entries[idx].key.kind);
    if( policy->landed == ASSET_LANDED_ALL )
    {
        /* One call lands every target: a second want from the same owner is
         * already covered. */
        for( int32_t m = assets->entries[idx].head; m != NIL; m = assets->nodes[m].next )
            if( assets->nodes[m].type == NODE_WANT && assets->nodes[m].waiter.land == waiter->land &&
                assets->nodes[m].waiter.ctx == waiter->ctx )
                return;
    }
    n = node_alloc(assets);
    assets->nodes[n].type = NODE_WANT;
    assets->nodes[n].key = assets->entries[idx].key;
    assets->nodes[n].waiter = *waiter;
    assets->nodes[n].wanted_at = assets->now_ms;
    entry_append(assets, idx, n);
}

static void
add_join(
    struct Assets* assets,
    int idx,
    int* join)
{
    int32_t n = node_alloc(assets);

    assets->nodes[n].type = NODE_JOIN;
    assets->nodes[n].key = assets->entries[idx].key;
    assets->nodes[n].join = join;
    (*join)++;
    entry_append(assets, idx, n);
}

/* Start the entry's loader if it is wanted, not loading, its back-off is over
 * (unless `now` overrides it) and its kind is free to load. */
static void
maybe_start(
    struct Assets* assets,
    struct AssetKey key,
    bool now)
{
    int idx = entry_find(assets, key);
    struct AssetEntry const* e;

    if( idx < 0 )
        return;
    e = &assets->entries[idx];
    if( e->state != ASSET_STATE_WANTED || e->superseded )
        return;
    if( !now && e->retry_at > assets->now_ms )
        return;
    if( Assets_Policy((enum AssetKind)key.kind)->newer == ASSET_NEWER_REPLACE &&
        assets->kind_loading[key.kind] > 0 )
        return;
    start_load(assets, key);
}

enum AssetState
Assets_Want(
    struct Assets* assets,
    struct AssetKey key,
    struct AssetWaiter const* waiter)
{
    struct AssetPolicy const* policy;
    int idx;

    assert(assets);
    check_key(key);
    policy = Assets_Policy((enum AssetKind)key.kind);
    idx = entry_find(assets, key);
    if( idx >= 0 && entry_land_if_resident(assets, idx) )
        return ASSET_STATE_HERE;
    if( idx < 0 )
    {
        if( key_resident(assets, key) )
            return ASSET_STATE_HERE;
        if( policy->newer == ASSET_NEWER_REPLACE )
            supersede_others(assets, key);
        idx = entry_insert(assets, key);
    }
    else
    {
        if( assets->entries[idx].state == ASSET_STATE_FAILED )
            return ASSET_STATE_FAILED;
        if( policy->newer == ASSET_NEWER_REPLACE )
        {
            /* Wanted again before its replaced loader ended: it is the newest
             * request once more. */
            assets->entries[idx].superseded = 0;
            supersede_others(assets, key);
            idx = entry_find(assets, key);
            assert(idx >= 0);
        }
    }
    if( waiter )
        add_want(assets, idx, waiter);
    maybe_start(assets, key, false);
    idx = entry_find(assets, key);
    if( idx < 0 )
        return waiter ? ASSET_STATE_LOADING : ASSET_STATE_NONE;
    return (enum AssetState)assets->entries[idx].state;
}

int
Assets_Await(
    struct Assets* assets,
    struct AssetKey key,
    int* join)
{
    int idx;

    assert(assets);
    assert(join);
    check_key(key);
    idx = entry_find(assets, key);
    if( idx >= 0 && entry_land_if_resident(assets, idx) )
        return 0;
    if( idx < 0 )
    {
        if( key_resident(assets, key) )
            return 0;
        if( Assets_Policy((enum AssetKind)key.kind)->newer == ASSET_NEWER_REPLACE )
            supersede_others(assets, key);
        idx = entry_insert(assets, key);
    }
    else if( assets->entries[idx].state == ASSET_STATE_FAILED )
        return 0;
    add_join(assets, idx, join);
    /* A task is waiting: a back-off does not hold it. */
    maybe_start(assets, key, true);
    return 1;
}

void
Assets_Unjoin(
    struct Assets* assets,
    int* join)
{
    assert(assets);
    assert(join);
    for( int i = 0; i < assets->node_cap; i++ )
        if( assets->nodes[i].type == NODE_JOIN && assets->nodes[i].join == join )
        {
            assets->nodes[i].type = NODE_DEAD;
            assets->nodes[i].join = NULL;
        }
    *join = 0;
}

static struct AssetKey*
scratch_keys(
    struct Assets* assets,
    int count)
{
    if( count > assets->scratch_cap )
    {
        assets->scratch_cap = count;
        assets->scratch = ToriMem_Realloc(
            ToriMem_Process(), assets->scratch, (size_t)count * sizeof(*assets->scratch));
        assert(assets->scratch);
    }
    return assets->scratch;
}

void
Assets_Pump(struct Assets* assets)
{
    struct AssetKey* due;
    int due_count = 0;

    assert(assets);
    if( assets->entry_count == 0 )
        return;
    due = scratch_keys(assets, assets->entry_count);
    for( int i = 0; i < assets->entry_cap; i++ )
    {
        struct AssetEntry const* e = &assets->entries[i];
        if( !e->used || e->state != ASSET_STATE_WANTED || e->superseded ||
            e->retry_at > assets->now_ms )
            continue;
        if( !entry_has_want(assets, i) )
            continue;
        due[due_count++] = e->key;
    }
    /* Started after the walk: a start can end at once and move entries. */
    for( int d = 0; d < due_count; d++ )
    {
        struct AssetKey key = assets->scratch[d];
        int idx = entry_find(assets, key);
        if( idx < 0 || entry_land_if_resident(assets, idx) )
            continue;
        maybe_start(assets, key, false);
    }
}

void
Assets_Lift(
    struct Assets* assets,
    struct AssetKey key)
{
    int idx;

    assert(assets);
    check_key(key);
    idx = entry_find(assets, key);
    if( idx < 0 )
        return;
    assets->entries[idx].retry_at = 0;
}

/* Assets_Land and Assets_LandFor: the arrivals of `kind_mask` whose waiter
 * has `land` and `ctx` (`land` NULL: every waiter). A dropped waiter's node
 * is released by either. */
static int
land_arrivals(
    struct Assets* assets,
    uint32_t kind_mask,
    AssetLandFn land,
    void* ctx)
{
    int32_t take_head = NIL;
    int32_t take_tail = NIL;
    int32_t keep_head = NIL;
    int32_t keep_tail = NIL;
    int32_t n;
    int landed = 0;

    assert(assets);
    /* Take this call's arrivals out first: a landing that wants again, and
     * lands at once, waits for the next call. */
    n = assets->arrived_head;
    while( n != NIL )
    {
        int32_t next = assets->nodes[n].next;
        bool take = (kind_mask & ASSET_KIND_BIT(assets->nodes[n].key.kind)) != 0 &&
                    (!land || assets->nodes[n].type != NODE_WANT ||
                        (assets->nodes[n].waiter.land == land && assets->nodes[n].waiter.ctx == ctx));
        int32_t* head = take ? &take_head : &keep_head;
        int32_t* tail = take ? &take_tail : &keep_tail;

        assets->nodes[n].next = NIL;
        if( *tail == NIL )
            *head = n;
        else
            assets->nodes[*tail].next = n;
        *tail = n;
        n = next;
    }
    assets->arrived_head = keep_head;
    assets->arrived_tail = keep_tail;

    n = take_head;
    while( n != NIL )
    {
        struct AssetNode node = assets->nodes[n];
        int32_t next = node.next;
        struct AssetPolicy const* policy = Assets_Policy((enum AssetKind)node.key.kind);
        enum AssetOutcome outcome = (enum AssetOutcome)node.outcome;
        uint64_t late = assets->now_ms > node.wanted_at ? assets->now_ms - node.wanted_at : 0;

        /* Freed before the call: the landing may want again. */
        node_release(assets, n);
        n = next;
        if( node.type != NODE_WANT )
            continue;
        if( outcome == ASSET_OUTCOME_LANDED && policy->late == ASSET_LATE_DROP_AFTER &&
            late > policy->late_limit_ms )
            outcome = ASSET_OUTCOME_DROPPED;
        node.waiter.land(
            node.waiter.ctx,
            node.key,
            node.waiter.handle,
            outcome,
            late > UINT32_MAX ? UINT32_MAX : (uint32_t)late);
        landed++;
    }
    return landed;
}

int
Assets_Land(
    struct Assets* assets,
    uint32_t kind_mask)
{
    return land_arrivals(assets, kind_mask, NULL, NULL);
}

int
Assets_LandFor(
    struct Assets* assets,
    AssetLandFn land,
    void* ctx)
{
    assert(land);
    return land_arrivals(assets, ASSET_KIND_ALL, land, ctx);
}

int
Assets_DropWaiters(
    struct Assets* assets,
    AssetLandFn land,
    void* ctx,
    bool (*match)(
        struct AssetWaiterHandle handle,
        struct AssetKey key,
        void* arg),
    void* arg)
{
    int dropped = 0;

    assert(assets);
    assert(land);
    assert(match);
    for( int i = 0; i < assets->node_cap; i++ )
    {
        struct AssetNode* node = &assets->nodes[i];
        if( node->type != NODE_WANT || node->waiter.land != land || node->waiter.ctx != ctx )
            continue;
        if( !match(node->waiter.handle, node->key, arg) )
            continue;
        node->type = NODE_DEAD;
        dropped++;
    }
    return dropped;
}

enum AssetState
Assets_State(
    struct Assets const* assets,
    struct AssetKey key)
{
    int idx;

    assert(assets);
    check_key(key);
    idx = entry_find(assets, key);
    return idx < 0 ? ASSET_STATE_NONE : (enum AssetState)assets->entries[idx].state;
}

bool
Assets_InFlight(
    struct Assets const* assets,
    struct AssetKey key)
{
    return Assets_State(assets, key) == ASSET_STATE_LOADING;
}

int
Assets_Attempts(
    struct Assets const* assets,
    struct AssetKey key)
{
    int idx;

    assert(assets);
    check_key(key);
    idx = entry_find(assets, key);
    return idx < 0 ? 0 : assets->entries[idx].attempts;
}

uint32_t
Assets_Serial(struct Assets const* assets)
{
    assert(assets);
    return assets->serial;
}

int
Assets_Count(
    struct Assets const* assets,
    enum AssetKind kind)
{
    int count = 0;

    assert(assets);
    if( kind == ASSET_KIND_COUNT )
        return assets->entry_count;
    for( int i = 0; i < assets->entry_cap; i++ )
        if( assets->entries[i].used && assets->entries[i].key.kind == (int32_t)kind )
            count++;
    return count;
}

int
Assets_WaiterCount(
    struct Assets const* assets,
    enum AssetKind kind)
{
    int count = 0;

    assert(assets);
    for( int i = 0; i < assets->node_cap; i++ )
        if( assets->nodes[i].type == NODE_WANT &&
            (kind == ASSET_KIND_COUNT || assets->nodes[i].key.kind == (int32_t)kind) )
            count++;
    return count;
}

uint32_t
Assets_LoadsStarted(
    struct Assets const* assets,
    enum AssetKind kind)
{
    uint32_t total = 0;

    assert(assets);
    if( kind != ASSET_KIND_COUNT )
        return assets->loads_started[kind];
    for( int k = 0; k < ASSET_KIND_COUNT; k++ )
        total += assets->loads_started[k];
    return total;
}
