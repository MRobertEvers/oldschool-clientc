#include "engine/asset_requests.h"

#include "mem/torirs_mem.h"
#include "task_trigger.h"

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
 *               cycle the sequence started on.
 *  SPOTANIM     one load per graphic however many entities wear it (was a
 *               flag per slot, and a prefetch per packet that never asked
 *               whether one was in flight); a load that ends without it is
 *               retried after a back-off while something still wears it.
 *  NPC_BODY     one load dresses every npc of the type (was RS_NpcBodyLands);
 *               the next spawn of a cold type asks again.
 *  PLAYER_HELD  as NPC_BODY: the per-frame body reconcile dresses everyone.
 *  PLAYER_TRANSMOG  as NPC_BODY.
 *  OBJ_MODEL    a ground stack goes back four times for a model the loader
 *               says is still missing, then draws nothing, as the reference
 *               does (was APP_PLACEHOLDER_ATTEMPT_MAX): `fail_attempts`, which
 *               its task counts as it awaits again (app/app_tasks_ground.c).
 *  INV_ICONS    an icon whose chain did not come waits 500 ms doubling to
 *               4 s and asks again, never giving up (was InvIconRetry; the
 *               kind has no entries, only this row).
 *  IF_GROUP     a missing pack is reported by the open; asked again next open.
 *  SOUND        asked once while in flight (was load_requested, and the
 *               ambient bed asked every tick); how late a sound may still
 *               play is the audio queue's own rule (game/rs_audio.c, the
 *               reference's delays[i] >= -10), not the table's.
 *  MUSIC        a second song replaces the first: one loader of the kind, the
 *               newer starting when the older's ends, and a song that finishes
 *               loading after a newer request is not installed (was
 *               request_loading, cleared under a running load). The install
 *               task that awaits it is app/app_tick.c's Task_MusicInstall.
 *  SKYBOX       a new skybox replaces the old; a failed load is retried with
 *               back-off (was asked once per want and never again).
 *  REF_TABLE    loaded once while every map square waits on it (was
 *               reference_table_loading); a failure releases them all, and the
 *               next square to ask tries again.
 *  CONFIG       an idk or obj config an appearance or a held item names: one
 *               read however many player bodies wait on it (each body's fetch
 *               queued its own); a record the cache does not hold is asked
 *               again by the next fetch that names it.
 */
static struct AssetPolicy const g_asset_policy[ASSET_KIND_COUNT] = {
    [ASSET_KIND_MODEL] = { "model", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_TEXTURE] = { "texture", ASSET_FAIL_REMEMBER, 0, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_SPRITE] = { "sprite", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_SEQUENCE] = { "sequence", ASSET_FAIL_REMEMBER, 0, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_SPOTANIM] = { "spotanim", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_NPC_BODY] = { "npc_body", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_PLAYER_HELD] = { "player_held", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_PLAYER_TRANSMOG] = { "player_transmog", ASSET_FAIL_GIVE_UP, 1, 0, 0,
                                     ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_OBJ_MODEL] = { "obj_model", ASSET_FAIL_GIVE_UP, 4, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_INV_ICONS] = { "inv_icons", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_IF_GROUP] = { "if_group", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_SOUND] = { "sound", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_MUSIC] = { "music", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_NEWER_REPLACE },
    [ASSET_KIND_SKYBOX] = { "skybox", ASSET_FAIL_BACKOFF, 0, 500, 4000, ASSET_NEWER_REPLACE },
    [ASSET_KIND_REF_TABLE] = { "ref_table", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_NEWER_KEEP_BOTH },
    [ASSET_KIND_CONFIG] = { "config", ASSET_FAIL_GIVE_UP, 1, 0, 0, ASSET_NEWER_KEEP_BOTH },
    /* A read that came back empty (a dropped JS5 socket, a browser storage
     * error) is asked for again: its region would bake grey for good. The
     * world map's own request table re-asked after two seconds. */
    [ASSET_KIND_WORLDMAP_GEOGRAPHY] = { "worldmap_geography", ASSET_FAIL_BACKOFF, 0, 500, 4000,
                                        ASSET_NEWER_KEEP_BOTH },
};

/* A node is one awaiting task's count on an entry's attempt. */
enum
{
    NODE_FREE = 0,
    NODE_JOIN,
    /* A join released by Assets_Unjoin: still linked, skipped, freed where it
     * is next met. */
    NODE_DEAD,
};

struct AssetNode
{
    int32_t next;
    uint8_t type;
    struct AssetKey key;
    int* join;
    /* NODE_JOIN from Assets_AwaitOutcome: written when the attempt ends. */
    enum AssetOutcome* result;
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

    struct AssetLoader loaders[ASSET_KIND_COUNT];
    int kind_loading[ASSET_KIND_COUNT];
    uint32_t loads_started[ASSET_KIND_COUNT];

    uint64_t now_ms;
    uint32_t serial;
    /* Assets_Close: shutting down, ends are ignored. */
    int closed;
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
    (void)key;
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

/* ---- join nodes --------------------------------------------------------- */

static void
nodes_grow(struct Assets* assets)
{
    int old_cap = assets->node_cap;
    int new_cap = old_cap ? old_cap * 2 : 64;

    assert(new_cap <= ASSETS_JOINS_MAX * 2 && "assets joins full");
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

/* Release every join counting this entry's attempt: the attempt ended, with
 * `outcome` (written to an awaiter that asked for it). Dead nodes are freed on
 * the way; the entry's list is empty afterwards. */
static void
entry_release_joins(
    struct Assets* assets,
    int idx,
    enum AssetOutcome outcome)
{
    struct AssetEntry* e = &assets->entries[idx];
    int32_t n = e->head;

    while( n != NIL )
    {
        int32_t next = assets->nodes[n].next;

        assert(assets->nodes[n].type == NODE_JOIN || assets->nodes[n].type == NODE_DEAD);
        if( assets->nodes[n].type == NODE_JOIN && assets->nodes[n].join )
        {
            assert(*assets->nodes[n].join > 0);
            (*assets->nodes[n].join)--;
            if( assets->nodes[n].result )
                *assets->nodes[n].result = outcome;
            /* The task parked on the join wakes when this takes it to 0
             * (task_trigger.h); nothing runs here. */
            ToriRS_CountDropped(assets->nodes[n].join);
        }
        node_release(assets, n);
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
 * is still wanted, unless it is waiting out a back-off (the pump or the next
 * want starts it then). */
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
        if( e->retry_at > assets->now_ms )
            continue;
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

/* A newer request of a REPLACE kind: every other entry of the kind is
 * replaced. One whose loader runs (it cannot be stopped mid-read) is marked
 * and kept until it ends, its awaiters released DROPPED then; the rest go now,
 * their awaiters released DROPPED at once. */
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
        entry_release_joins(assets, idx, ASSET_OUTCOME_DROPPED);
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
    if( assets->closed )
        return;
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
        entry_release_joins(assets, idx, ASSET_OUTCOME_DROPPED);
        entry_remove_at(assets, idx);
        start_next_of_kind(assets, (enum AssetKind)key.kind);
        return;
    }

    if( ok )
    {
        entry_release_joins(assets, idx, ASSET_OUTCOME_LANDED);
        entry_remove_at(assets, idx);
        start_next_of_kind(assets, (enum AssetKind)key.kind);
        return;
    }

    e->attempts++;
    if( policy->fail == ASSET_FAIL_REMEMBER )
    {
        entry_release_joins(assets, idx, ASSET_OUTCOME_FAILED);
        assets->entries[idx].state = ASSET_STATE_FAILED;
    }
    else if( policy->fail == ASSET_FAIL_BACKOFF )
    {
        entry_release_joins(assets, idx, ASSET_OUTCOME_FAILED);
        e = &assets->entries[idx];
        e->wait_ms = Assets_BackoffNext(policy, e->wait_ms);
        e->retry_at = assets->now_ms + e->wait_ms;
        e->state = ASSET_STATE_WANTED;
    }
    else
    {
        assert(policy->fail == ASSET_FAIL_GIVE_UP);
        assert(policy->fail_attempts >= 1);
        /* The attempt's awaiters are told; one that wants another attempt
         * awaits again, counting its own (`fail_attempts`). */
        entry_release_joins(assets, idx, ASSET_OUTCOME_FAILED);
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
    if( assets->closed )
        return;
    idx = entry_find(assets, key);
    assert(idx >= 0 && "a load was abandoned that the table did not start");
    e = &assets->entries[idx];
    assert(e->state == ASSET_STATE_LOADING);
    assets->kind_loading[key.kind]--;
    entry_release_joins(assets, idx, ASSET_OUTCOME_DROPPED);
    e = &assets->entries[idx];
    if( e->superseded )
    {
        entry_remove_at(assets, idx);
        start_next_of_kind(assets, (enum AssetKind)key.kind);
        return;
    }
    e->state = ASSET_STATE_WANTED;
    e->retry_at = 0;
}

void
Assets_Fail(
    struct Assets* assets,
    struct AssetKey key)
{
    int idx;

    assert(assets);
    check_key(key);
    assert(Assets_Policy((enum AssetKind)key.kind)->fail == ASSET_FAIL_REMEMBER);
    idx = entry_find(assets, key);
    if( idx < 0 )
        idx = entry_insert(assets, key);
    assert(assets->entries[idx].state != ASSET_STATE_LOADING);
    if( assets->entries[idx].state == ASSET_STATE_FAILED )
        return;
    entry_release_joins(assets, idx, ASSET_OUTCOME_FAILED);
    assets->entries[idx].attempts++;
    assets->entries[idx].state = ASSET_STATE_FAILED;
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
    return assets;
}

void
Assets_Free(struct Assets* assets)
{
    if( !assets )
        return;
    ToriMem_Free(ToriMem_Process(), assets->entries);
    ToriMem_Free(ToriMem_Process(), assets->nodes);
    ToriMem_Free(ToriMem_Process(), assets);
}

void
Assets_Close(struct Assets* assets)
{
    assert(assets);
    assets->closed = 1;
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
    entry_release_joins(assets, idx, ASSET_OUTCOME_LANDED);
    entry_remove_at(assets, idx);
    return true;
}

static void
add_join(
    struct Assets* assets,
    int idx,
    int* join,
    enum AssetOutcome* result)
{
    int32_t n = node_alloc(assets);

    assets->nodes[n].type = NODE_JOIN;
    assets->nodes[n].key = assets->entries[idx].key;
    assets->nodes[n].join = join;
    assets->nodes[n].result = result;
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

/* REPLACE kinds: `idx` (an entry of `key`) is the newest request -- wanted
 * or awaited again before a replaced loader of it ended, it is the newest
 * once more -- and every other entry of the kind is replaced. Returns the
 * entry's index afresh (a replaced entry's removal moves entries). */
static int
make_newest(
    struct Assets* assets,
    struct AssetKey key,
    int idx)
{
    assert(Assets_Policy((enum AssetKind)key.kind)->newer == ASSET_NEWER_REPLACE);
    assets->entries[idx].superseded = 0;
    supersede_others(assets, key);
    idx = entry_find(assets, key);
    assert(idx >= 0);
    return idx;
}

enum AssetState
Assets_Want(
    struct Assets* assets,
    struct AssetKey key)
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
            idx = make_newest(assets, key, idx);
    }
    maybe_start(assets, key, false);
    idx = entry_find(assets, key);
    if( idx < 0 )
        return ASSET_STATE_NONE;
    return (enum AssetState)assets->entries[idx].state;
}

/* Assets_Await and Assets_AwaitOutcome. `result` NULL: nobody asked. */
static int
await_key(
    struct Assets* assets,
    struct AssetKey key,
    int* join,
    enum AssetOutcome* result)
{
    int idx;

    check_key(key);
    idx = entry_find(assets, key);
    if( idx >= 0 && entry_land_if_resident(assets, idx) )
    {
        if( result )
            *result = ASSET_OUTCOME_LANDED;
        return 0;
    }
    if( idx < 0 )
    {
        if( key_resident(assets, key) )
        {
            if( result )
                *result = ASSET_OUTCOME_LANDED;
            return 0;
        }
        if( Assets_Policy((enum AssetKind)key.kind)->newer == ASSET_NEWER_REPLACE )
            supersede_others(assets, key);
        idx = entry_insert(assets, key);
    }
    else if( assets->entries[idx].state == ASSET_STATE_FAILED )
    {
        if( result )
            *result = ASSET_OUTCOME_FAILED;
        return 0;
    }
    else if( Assets_Policy((enum AssetKind)key.kind)->newer == ASSET_NEWER_REPLACE )
        idx = make_newest(assets, key, idx);
    /* Until the attempt ends: not here. */
    if( result )
        *result = ASSET_OUTCOME_DROPPED;
    add_join(assets, idx, join, result);
    /* A task is waiting: a back-off does not hold it. */
    maybe_start(assets, key, true);
    return 1;
}

int
Assets_Await(
    struct Assets* assets,
    struct AssetKey key,
    int* join)
{
    assert(assets);
    assert(join);
    return await_key(assets, key, join, NULL);
}

int
Assets_AwaitOutcome(
    struct Assets* assets,
    struct AssetKey key,
    int* join,
    enum AssetOutcome* outcome)
{
    assert(assets);
    assert(join);
    assert(outcome);
    return await_key(assets, key, join, outcome);
}

void
Assets_Unjoin(
    struct Assets* assets,
    int* join)
{
    assert(assets);
    assert(join);
    if( assets->closed )
    {
        *join = 0;
        ToriRS_CountDropped(join);
        return;
    }
    for( int i = 0; i < assets->node_cap; i++ )
        if( assets->nodes[i].type == NODE_JOIN && assets->nodes[i].join == join )
        {
            assets->nodes[i].type = NODE_DEAD;
            assets->nodes[i].join = NULL;
            assets->nodes[i].result = NULL;
        }
    *join = 0;
    ToriRS_CountDropped(join);
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

bool
Assets_Due(
    struct Assets const* assets,
    struct AssetKey key)
{
    struct AssetEntry const* e;
    int idx;

    assert(assets);
    check_key(key);
    idx = entry_find(assets, key);
    if( idx < 0 )
        return true;
    e = &assets->entries[idx];
    if( e->state != ASSET_STATE_WANTED || e->superseded || e->retry_at > assets->now_ms )
        return false;
    return Assets_Policy((enum AssetKind)key.kind)->newer != ASSET_NEWER_REPLACE ||
           assets->kind_loading[key.kind] == 0;
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
Assets_JoinCount(
    struct Assets const* assets,
    enum AssetKind kind)
{
    int count = 0;

    assert(assets);
    for( int i = 0; i < assets->node_cap; i++ )
        if( assets->nodes[i].type == NODE_JOIN && assets->nodes[i].join &&
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
