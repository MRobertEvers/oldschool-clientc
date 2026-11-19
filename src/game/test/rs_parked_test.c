/*
 * The parked lookup (game/rs_parked.h), driven by fake kinds, a fake clock and
 * a real assets table with fake loaders.
 *
 * Put and Find; newest replaces on one key, and keep-both; keys that differ
 * only by generation are different targets; a want that lands through the
 * assets table at the landing point and never before it; arrival order
 * whatever order the assets arrive in; the release budget; a replaced or
 * cancelled entry whose asset still arrives (nothing lands, the stale waiter
 * is dropped); every cancel scope; a chain (a landing that wants the next
 * link); an asset already here (ready at once, lands at the next landing,
 * not inside the call that found it); the failed and late policies; owner
 * readiness and timed entries; an asset landed ALL; growth; a landing that
 * parks more; the free with waiters registered.
 *
 * Run: make -C src test-parked
 */

#include "game/rs_parked.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- the fake clock and loader ------------------------------------------ */

static uint32_t g_now = 100;

static uint32_t
fake_clock(void* data)
{
    (void)data;
    return g_now;
}

#define FAKE_RESIDENT_MAX 16

struct FakeLoader
{
    int starts;
    int resident_ids[FAKE_RESIDENT_MAX];
    int resident_count;
};

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct FakeLoader* fake = data;
    (void)assets;
    (void)key;
    fake->starts++;
    return ASSET_START_LOADING;
}

static bool
fake_resident(
    void* data,
    struct AssetKey key)
{
    struct FakeLoader* fake = data;
    for( int i = 0; i < fake->resident_count; i++ )
        if( fake->resident_ids[i] == key.id )
            return true;
    return false;
}

static struct FakeLoader g_fake;

static struct Assets*
assets_new(void)
{
    struct Assets* assets = Assets_New();
    struct AssetLoader loader = { fake_start, fake_resident, &g_fake };

    memset(&g_fake, 0, sizeof(g_fake));
    Assets_SetLoader(assets, ASSET_KIND_MODEL, &loader);
    Assets_SetLoader(assets, ASSET_KIND_IF_GROUP, &loader);
    Assets_SetLoader(assets, ASSET_KIND_NPC_BODY, &loader);
    return assets;
}

/* An arrival as the client's landing point lands it: the assets table's
 * landings for the lookup, then the lookup's. */
static int
land_point(
    struct Assets* assets,
    struct Park* park,
    int budget)
{
    (void)Assets_LandFor(assets, Park_AssetLandFn(), park);
    return Park_Land(park, budget);
}

/* ---- fake kinds ---------------------------------------------------------- */

enum
{
    KIND_A = PARK_KINDS_TEST_FIRST,
    KIND_KEEP_BOTH,
    KIND_FAIL_DROP,
    KIND_LATE_DROP,
    KIND_LATE_SHORT,
    KIND_CHAIN,
    KIND_SPAWNER,
};

struct What
{
    int tag;
    int value;
};

#define LOG_MAX 512

struct Log
{
    int count;
    int tag[LOG_MAX];
    int kind[LOG_MAX];
    enum ParkReason reason[LOG_MAX];
    enum AssetOutcome outcome[LOG_MAX];
    uint32_t late[LOG_MAX];
};

static struct Log g_log;

static void
log_landing(
    struct ParkEntry const* entry,
    struct ParkArrival const* arrival)
{
    struct What const* what = Park_What(entry);
    if( g_log.count < LOG_MAX )
    {
        g_log.tag[g_log.count] = what->tag;
        g_log.kind[g_log.count] = entry->kind;
        g_log.reason[g_log.count] = arrival->reason;
        g_log.outcome[g_log.count] = arrival->outcome;
        g_log.late[g_log.count] = arrival->late_cycles;
    }
    g_log.count++;
}

static enum ParkLandResult
land_record(
    void* ctx,
    struct Park* park,
    struct ParkRef ref,
    struct ParkEntry const* entry,
    struct ParkArrival const* arrival)
{
    (void)ctx;
    (void)park;
    (void)ref;
    log_landing(entry, arrival);
    return PARK_LAND_DONE;
}

/* The first landing wants model 900 + tag; the second is done. */
static enum ParkLandResult
land_chain(
    void* ctx,
    struct Park* park,
    struct ParkRef ref,
    struct ParkEntry const* entry,
    struct ParkArrival const* arrival)
{
    struct What const* what = Park_What(entry);
    (void)ctx;
    log_landing(entry, arrival);
    if( entry->wants.id != 900 + what->tag )
    {
        Park_Want(park, ref, AssetKey_Make(ASSET_KIND_MODEL, 900 + what->tag, 0));
        return PARK_LAND_KEEP;
    }
    return PARK_LAND_DONE;
}

/* Parks 100 more entries from inside its landing: the lookup grows under it. */
static enum ParkLandResult
land_spawner(
    void* ctx,
    struct Park* park,
    struct ParkRef ref,
    struct ParkEntry const* entry,
    struct ParkArrival const* arrival)
{
    (void)ctx;
    (void)ref;
    for( int i = 0; i < 100; i++ )
    {
        struct What w = { 5000 + i, 0 };
        Park_Put(park, KIND_A, ParkKey_Id(5000 + i, 0), 0, &w, sizeof(w));
    }
    log_landing(entry, arrival);
    return PARK_LAND_DONE;
}

static struct Park*
park_new(struct Assets* assets)
{
    struct Park* park = Park_New(assets, fake_clock, NULL);
    struct ParkPolicy plain = { PARK_NEWER_REPLACE, PARK_LATE_LAND, 0, PARK_FAILED_LAND };
    struct ParkPolicy both = { PARK_NEWER_KEEP_BOTH, PARK_LATE_LAND, 0, PARK_FAILED_LAND };
    struct ParkPolicy fail_drop = { PARK_NEWER_REPLACE, PARK_LATE_LAND, 0, PARK_FAILED_DROP };
    struct ParkPolicy late_drop = { PARK_NEWER_REPLACE, PARK_LATE_DROP_AFTER, 5, PARK_FAILED_LAND };
    struct ParkPolicy late_short = {
        PARK_NEWER_REPLACE, PARK_LATE_LAND_SHORTENED, 0, PARK_FAILED_LAND
    };

    memset(&g_log, 0, sizeof(g_log));
    Park_RegisterKind(park, KIND_A, "a", land_record, NULL, &plain);
    Park_RegisterKind(park, KIND_KEEP_BOTH, "keep_both", land_record, NULL, &both);
    Park_RegisterKind(park, KIND_FAIL_DROP, "fail_drop", land_record, NULL, &fail_drop);
    Park_RegisterKind(park, KIND_LATE_DROP, "late_drop", land_record, NULL, &late_drop);
    Park_RegisterKind(park, KIND_LATE_SHORT, "late_short", land_record, NULL, &late_short);
    Park_RegisterKind(park, KIND_CHAIN, "chain", land_chain, NULL, &plain);
    Park_RegisterKind(park, KIND_SPAWNER, "spawner", land_spawner, NULL, &plain);
    return park;
}

static struct ParkRef
put(struct Park* park,
    int kind,
    struct ParkKey key,
    int tag)
{
    struct What w = { tag, tag * 10 };
    return Park_Put(park, kind, key, 0, &w, sizeof(w));
}

static struct AssetKey
model(int id)
{
    return AssetKey_Make(ASSET_KIND_MODEL, id, 0);
}

/* ---- tests ---------------------------------------------------------------- */

static void
test_put_find_replace(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkKey key = ParkKey_Component((161 << 16) | 4, 7);
    struct ParkRef first = put(park, KIND_A, key, 1);
    struct ParkRef second;
    struct ParkEntry const* e;

    TEST_ASSERT(Park_KindRegistered(park, KIND_A), "a registered kind is registered");
    TEST_ASSERT(!Park_KindRegistered(park, PARK_KINDS_GROUND_FIRST), "an unregistered kind is not");
    TEST_ASSERT(strcmp(Park_KindName(park, KIND_A), "a") == 0, "the kind keeps its name");
    TEST_ASSERT(Park_Find(park, KIND_A, key).index == first.index, "Find answers the parked entry");
    TEST_ASSERT(
        Park_Find(park, KIND_KEEP_BOTH, key).index < 0,
        "another kind on the same key is not pending");
    e = Park_Get(park, first);
    TEST_ASSERT(e && e->arrived_cycle == 100, "the entry records the cycle it arrived on");
    TEST_ASSERT(e && e->wants.kind < 0, "a new entry wants nothing");
    TEST_ASSERT(e && ((struct What const*)Park_What(e))->value == 10, "what to apply is copied in");

    g_now = 104;
    second = put(park, KIND_A, key, 2);
    TEST_ASSERT(Park_Get(park, first) == NULL, "the older entry for the key is freed");
    TEST_ASSERT(Park_Count(park, KIND_A) == 1, "one entry per target");
    TEST_ASSERT(
        Park_Find(park, KIND_A, key).index == second.index &&
            Park_Find(park, KIND_A, key).gen == second.gen,
        "Find answers the newer entry");
    TEST_ASSERT(Park_Stats(park).replaced == 1, "the replacement is counted");
    TEST_ASSERT(
        Park_Get(park, second)->arrived_cycle == 104, "the newer entry arrives on its own cycle");
    TEST_ASSERT(Park_Get(park, second)->order > 1, "a later put has a later order");

    /* The same component under a newer close serial is another target: the
     * interface was closed and reopened, the old entry is not this one. */
    put(park, KIND_A, ParkKey_Component((161 << 16) | 4, 8), 3);
    TEST_ASSERT(
        Park_Count(park, KIND_A) == 2, "a key that differs by generation is another target");
    /* An npc and a player in the same slot are different targets too. */
    put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 12, 1), 4);
    put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_PLAYER, 12, 1), 5);
    TEST_ASSERT(Park_Count(park, KIND_A) == 4, "entity type is part of the key");

    /* Keep both: every put is its own entry; Find answers the newest. */
    {
        struct ParkKey k = ParkKey_Projectile(3, 44);
        struct ParkRef a = put(park, KIND_KEEP_BOTH, k, 10);
        struct ParkRef b = put(park, KIND_KEEP_BOTH, k, 11);
        TEST_ASSERT(Park_Get(park, a) && Park_Get(park, b), "keep-both keeps both");
        TEST_ASSERT(
            Park_Find(park, KIND_KEEP_BOTH, k).index == b.index, "Find answers the newest of them");
    }
    /* Remove of an entry that is gone is a no-op. */
    Park_Remove(park, first);
    TEST_ASSERT(Park_Count(park, 0) == 6, "removing a gone entry removes nothing");
    g_now = 100;
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_want_lands_at_landing_point(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkRef ref = put(park, KIND_A, ParkKey_Id(1, 0), 1);
    struct ParkRef other = put(park, KIND_A, ParkKey_Id(2, 0), 2);

    TEST_ASSERT(Park_Want(park, ref, model(50)) == ASSET_STATE_LOADING, "a cold asset is loading");
    TEST_ASSERT(Park_Want(park, other, model(50)) == ASSET_STATE_LOADING, "a second want joins it");
    TEST_ASSERT(g_fake.starts == 1, "one load for both entries");
    TEST_ASSERT(Park_Get(park, ref)->wants.id == 50, "the entry says what it wants");
    TEST_ASSERT(Park_Land(park, 64) == 0, "nothing lands before the asset arrives");

    Assets_Finished(assets, model(50), true);
    TEST_ASSERT(Park_ReadyCount(park) == 0, "an arrival is not ready until the table lands it");
    /* Somebody else landing models (another owner's point) only readies. */
    TEST_ASSERT(
        Assets_Land(assets, ASSET_KIND_BIT(ASSET_KIND_MODEL)) == 2, "the table lands both waiters");
    TEST_ASSERT(g_log.count == 0, "the asset table's landing applies nothing");
    TEST_ASSERT(Park_ReadyCount(park) == 2, "both entries are ready");
    TEST_ASSERT(Park_Land(park, 64) == 2, "the landing point lands both");
    TEST_ASSERT(g_log.count == 2 && g_log.tag[0] == 1 && g_log.tag[1] == 2, "in arrival order");
    TEST_ASSERT(
        g_log.reason[0] == PARK_REASON_ASSET && g_log.outcome[0] == ASSET_OUTCOME_LANDED,
        "landed as an arrived asset");
    TEST_ASSERT(Park_Count(park, 0) == 0, "a landed entry is freed");
    TEST_ASSERT(Park_Get(park, ref) == NULL, "its ref is gone");
    TEST_ASSERT(Park_Stats(park).landed == 2, "both counted");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_arrival_order_and_budget(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkRef refs[6];

    for( int i = 0; i < 6; i++ )
    {
        refs[i] = put(park, KIND_A, ParkKey_View(0, 3200 + i, 3200, 0, 2, 1), i);
        Park_Want(park, refs[i], model(100 + i));
    }
    /* The assets come back backwards. */
    for( int i = 5; i >= 0; i-- )
        Assets_Finished(assets, model(100 + i), true);
    TEST_ASSERT(land_point(assets, park, 2) == 2, "the budget bounds a landing");
    TEST_ASSERT(
        g_log.count == 2 && g_log.tag[0] == 0 && g_log.tag[1] == 1,
        "the oldest arrivals land first, whatever order the assets came in");
    TEST_ASSERT(Park_ReadyCount(park) == 4, "the rest stay ready");
    TEST_ASSERT(land_point(assets, park, 2) == 2, "the next landing takes the next two");
    TEST_ASSERT(g_log.tag[2] == 2 && g_log.tag[3] == 3, "still in order");
    TEST_ASSERT(land_point(assets, park, 64) == 2, "and the last two");
    TEST_ASSERT(g_log.tag[4] == 4 && g_log.tag[5] == 5, "in order to the end");
    TEST_ASSERT(Park_Count(park, 0) == 0, "all freed");

    /* An entry that is not ready does not hold back a later one that is. */
    memset(&g_log, 0, sizeof(g_log));
    refs[0] = put(park, KIND_A, ParkKey_Id(1, 0), 20);
    Park_Want(park, refs[0], model(200));
    refs[1] = put(park, KIND_A, ParkKey_Id(2, 0), 21);
    Park_Want(park, refs[1], model(201));
    Assets_Finished(assets, model(201), true);
    TEST_ASSERT(
        land_point(assets, park, 64) == 1 && g_log.tag[0] == 21,
        "a slow asset does not delay another target's");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_replaced_and_cancelled_while_loading(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkKey key = ParkKey_View(0, 3222, 3218, 0, 2, 1);
    struct ParkRef add = put(park, KIND_A, key, 1);
    struct ParkRef del;

    Park_Want(park, add, model(60));
    /* A delete for the same tile overtakes the add still loading. */
    del = put(park, KIND_A, key, 2);
    Park_Ready(park, del);
    TEST_ASSERT(Park_Get(park, add) == NULL, "the add is replaced");
    TEST_ASSERT(land_point(assets, park, 64) == 1 && g_log.tag[0] == 2, "the delete lands");
    TEST_ASSERT(g_log.reason[0] == PARK_REASON_READY, "readied by its owner");
    TEST_ASSERT(
        Assets_WaiterCount(assets, ASSET_KIND_MODEL) == 0,
        "the replaced add's waiter was dropped at the landing");
    Assets_Finished(assets, model(60), true);
    TEST_ASSERT(land_point(assets, park, 64) == 0, "the add's model arriving lands nothing");
    TEST_ASSERT(g_log.count == 1, "the add never lands");

    /* A cancelled entry whose asset arrives before the next landing. */
    memset(&g_log, 0, sizeof(g_log));
    add = put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 9, 3), 3);
    Park_Want(park, add, model(61));
    TEST_ASSERT(
        Park_Cancel(park, ParkScope_Entity(PARK_ENTITY_NPC, 9)) == 1, "the removed npc's entry");
    /* The slot is reused at once: the stale handle must not reach its new
     * owner when the cancelled entry's asset arrives. */
    {
        struct ParkRef next = put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 9, 4), 4);
        TEST_ASSERT(
            next.index == add.index && next.gen != add.gen, "the slot is reused, a generation on");
        add = next;
    }
    Park_Want(park, add, model(62));
    Assets_Finished(assets, model(61), true);
    TEST_ASSERT(
        land_point(assets, park, 64) == 0,
        "a cancelled entry's asset lands nothing, not its slot's next entry");
    Assets_Finished(assets, model(62), true);
    TEST_ASSERT(land_point(assets, park, 64) == 1 && g_log.tag[0] == 4, "its own asset does");
    TEST_ASSERT(Park_Stats(park).cancelled == 1, "the cancel is counted");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_cancel_scopes(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct What w = { 0, 0 };

    Park_Put(park, KIND_A, ParkKey_View(0, 1, 1, 0, 0, 1), 5, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_View(0, 1, 2, 0, 0, 1), 5, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_View(1, 1, 1, 0, 0, 1), 6, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_PLAYER, 3, 1), 0, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 3, 1), 0, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_Component((160 << 16) | 1, 2), 0, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_Component((160 << 16) | 2, 2), 0, &w, sizeof(w));
    Park_Put(park, KIND_A, ParkKey_Component((161 << 16) | 1, 2), 0, &w, sizeof(w));
    Park_Put(park, KIND_KEEP_BOTH, ParkKey_Projectile(1, 1), 0, &w, sizeof(w));
    Park_Put(park, KIND_KEEP_BOTH, ParkKey_Projectile(1, 1), 0, &w, sizeof(w));
    Park_Put(park, KIND_LATE_SHORT, ParkKey_Id(1, 0), 0, &w, sizeof(w));
    TEST_ASSERT(Park_Count(park, 0) == 11, "eleven parked");

    TEST_ASSERT(
        Park_Cancel(park, ParkScope_Scene(5)) == 2, "a rebuild cancels its scene's entries");
    TEST_ASSERT(Park_Cancel(park, ParkScope_View(1)) == 1, "a view torn down cancels its entries");
    TEST_ASSERT(
        Park_Cancel(park, ParkScope_Entity(PARK_ENTITY_PLAYER, 3)) == 1,
        "a removed player cancels the player's, not the npc's in the same slot");
    TEST_ASSERT(
        Park_Cancel(park, ParkScope_Group(160)) == 2, "a closed interface cancels its components'");
    TEST_ASSERT(
        Park_Cancel(park, ParkScope_Key(KIND_KEEP_BOTH, ParkKey_Projectile(1, 1))) == 2,
        "a key cancels every entry of the kind on it");
    TEST_ASSERT(Park_Cancel(park, ParkScope_Kind(KIND_LATE_SHORT)) == 1, "a kind cancels its own");
    TEST_ASSERT(Park_Count(park, 0) == 2, "the npc's and the other interface's are left");
    TEST_ASSERT(Park_Cancel(park, ParkScope_All()) == 2, "a logout cancels everything");
    TEST_ASSERT(Park_Count(park, 0) == 0, "nothing parked");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_chain_and_here(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkRef ref = put(park, KIND_CHAIN, ParkKey_Id(1, 0), 1);

    /* First link: a config, already here. */
    g_fake.resident_ids[g_fake.resident_count++] = 70;
    TEST_ASSERT(Park_Want(park, ref, model(70)) == ASSET_STATE_HERE, "a resident asset is here");
    TEST_ASSERT(g_log.count == 0, "nothing lands inside the want that found it");
    TEST_ASSERT(Park_ReadyCount(park) == 1, "it is ready at once");
    TEST_ASSERT(land_point(assets, park, 64) == 1, "it lands at the landing point");
    TEST_ASSERT(
        Park_Get(park, ref) && Park_Get(park, ref)->wants.id == 901,
        "the landing wanted the next link and kept the entry");
    TEST_ASSERT(land_point(assets, park, 64) == 0, "the next link has not arrived");
    Assets_Finished(assets, model(901), true);
    TEST_ASSERT(land_point(assets, park, 64) == 1, "the last link lands");
    TEST_ASSERT(Park_Get(park, ref) == NULL, "and the entry is freed");

    /* A next link already here waits for the next landing. */
    memset(&g_log, 0, sizeof(g_log));
    g_fake.resident_ids[g_fake.resident_count++] = 902;
    ref = put(park, KIND_CHAIN, ParkKey_Id(2, 0), 2);
    Park_Ready(park, ref);
    TEST_ASSERT(land_point(assets, park, 64) == 1, "one landing per call for a chain found here");
    TEST_ASSERT(Park_ReadyCount(park) == 1, "the resident next link is ready for the next call");
    TEST_ASSERT(land_point(assets, park, 64) == 1 && g_log.count == 2, "and lands there");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_failed_and_late(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct AssetKey pack = AssetKey_Make(ASSET_KIND_IF_GROUP, 162, 0);
    struct ParkRef a = put(park, KIND_A, ParkKey_Id(1, 0), 1);
    struct ParkRef b = put(park, KIND_FAIL_DROP, ParkKey_Id(1, 0), 2);

    /* IF_GROUP gives up after one failed attempt (asset_requests.c). */
    Park_Want(park, a, pack);
    Park_Want(park, b, pack);
    Assets_Finished(assets, pack, false);
    TEST_ASSERT(land_point(assets, park, 64) == 1, "only the land-on-failure kind lands");
    TEST_ASSERT(
        g_log.tag[0] == 1 && g_log.outcome[0] == ASSET_OUTCOME_FAILED, "and is told it failed");
    TEST_ASSERT(Park_Stats(park).dropped == 1, "the drop-on-failure entry is dropped");
    TEST_ASSERT(Park_Count(park, 0) == 0, "both freed");

    /* Late: dropped after 5 cycles from arrival; shortened passes the wait. */
    memset(&g_log, 0, sizeof(g_log));
    g_now = 200;
    a = put(park, KIND_LATE_DROP, ParkKey_Id(1, 0), 3);
    b = put(park, KIND_LATE_SHORT, ParkKey_Id(1, 0), 4);
    Park_Want(park, a, model(80));
    Park_Want(park, b, model(80));
    g_now = 207;
    Assets_Finished(assets, model(80), true);
    TEST_ASSERT(
        land_point(assets, park, 64) == 1, "the late-drop entry past its deadline does not land");
    TEST_ASSERT(
        g_log.tag[0] == 4 && g_log.late[0] == 7,
        "the shortened one is told how late, from arrival");
    TEST_ASSERT(Park_Stats(park).dropped == 2, "the deadline drop is counted");

    memset(&g_log, 0, sizeof(g_log));
    a = put(park, KIND_LATE_DROP, ParkKey_Id(2, 0), 5);
    Park_Want(park, a, model(81));
    g_now = 212;
    Assets_Finished(assets, model(81), true);
    TEST_ASSERT(
        land_point(assets, park, 64) == 1 && g_log.tag[0] == 5 && g_log.late[0] == 0,
        "within its deadline it lands, told nothing of lateness");
    g_now = 100;
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_ready_and_timed(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkRef zone1 = put(park, KIND_A, ParkKey_View(2, 10, 10, 0, 0, 1), 1);
    struct ParkRef zone2 = put(park, KIND_A, ParkKey_View(2, 11, 10, 0, 0, 1), 2);
    struct ParkRef root = put(park, KIND_A, ParkKey_View(0, 11, 10, 0, 0, 1), 3);
    struct ParkRef merge;

    (void)zone1;
    (void)zone2;
    TEST_ASSERT(land_point(assets, park, 64) == 0, "entries waiting on their owner do not land");
    TEST_ASSERT(
        Park_ReadyWhere(park, ParkScope_View(2)) == 2, "view 2 is built: its entries are ready");
    TEST_ASSERT(
        land_point(assets, park, 64) == 2 && g_log.tag[0] == 1 && g_log.tag[1] == 2,
        "they land in arrival order");
    TEST_ASSERT(Park_Get(park, root) != NULL, "the root view's entry still waits");

    /* A merge with a time to land. */
    memset(&g_log, 0, sizeof(g_log));
    g_now = 300;
    merge = put(park, KIND_A, ParkKey_View(1, 5, 5, 0, 2, 1), 4);
    Park_ReadyAt(park, merge, 305);
    TEST_ASSERT(Park_ReadyCount(park) == 0, "not due yet");
    g_now = 304;
    TEST_ASSERT(land_point(assets, park, 64) == 0, "not before its cycle");
    g_now = 305;
    TEST_ASSERT(Park_ReadyCount(park) == 1, "due at its cycle");
    TEST_ASSERT(
        land_point(assets, park, 64) == 1 && g_log.reason[0] == PARK_REASON_TIME, "lands by time");
    g_now = 100;
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_landed_all(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    /* NPC_BODY is landed ALL: one call dresses every npc of the type. */
    struct AssetKey body = AssetKey_Make(ASSET_KIND_NPC_BODY, 3010, 3010);
    struct ParkRef a = put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 1, 1), 1);
    struct ParkRef b = put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 2, 1), 2);
    struct ParkRef c = put(park, KIND_A, ParkKey_Entity(PARK_ENTITY_NPC, 3, 1), 3);

    Park_Want(park, a, body);
    Park_Want(park, b, body);
    Park_Want(park, c, body);
    TEST_ASSERT(
        Assets_WaiterCount(assets, ASSET_KIND_NPC_BODY) == 1,
        "the lookup's wants collapse into one");
    /* The npc whose waiter carries the others is removed. */
    Park_Cancel(park, ParkScope_Entity(PARK_ENTITY_NPC, 1));
    TEST_ASSERT(land_point(assets, park, 64) == 0, "nothing arrived yet");
    TEST_ASSERT(
        Assets_WaiterCount(assets, ASSET_KIND_NPC_BODY) == 1,
        "the collapsed waiter is kept while others count on it");
    Assets_Finished(assets, body, true);
    TEST_ASSERT(land_point(assets, park, 64) == 2, "both remaining npcs land");
    TEST_ASSERT(g_log.tag[0] == 2 && g_log.tag[1] == 3, "in arrival order");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_growth_and_landing_that_parks(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);
    struct ParkRef spawner;

    for( int i = 0; i < 300; i++ )
        put(park, KIND_A, ParkKey_Id(i, 0), i);
    TEST_ASSERT(Park_Count(park, KIND_A) == 300, "the lookup grows");
    for( int i = 0; i < 300; i += 37 )
    {
        struct ParkRef r = Park_Find(park, KIND_A, ParkKey_Id(i, 0));
        TEST_ASSERT(
            r.index >= 0 && ((struct What const*)Park_What(Park_Get(park, r)))->tag == i,
            "every entry is found after growth");
    }
    Park_Cancel(park, ParkScope_All());

    spawner = put(park, KIND_SPAWNER, ParkKey_Id(1, 0), 77);
    for( int i = 0; i < 60; i++ )
        put(park, KIND_A, ParkKey_Id(1000 + i, 0), 1000 + i);
    Park_Ready(park, spawner);
    TEST_ASSERT(land_point(assets, park, 64) == 1, "the spawner lands");
    TEST_ASSERT(
        g_log.count == 1 && g_log.tag[0] == 77,
        "its entry copy survived the lookup growing under it");
    TEST_ASSERT(Park_Count(park, KIND_A) == 160, "what it parked is parked");
    TEST_ASSERT(Park_Get(park, spawner) == NULL, "the spawner is freed");
    Park_Free(park);
    Assets_Free(assets);
}

static void
test_free_drops_waiters(void)
{
    struct Assets* assets = assets_new();
    struct Park* park = park_new(assets);

    for( int i = 0; i < 5; i++ )
        Park_Want(park, put(park, KIND_A, ParkKey_Id(i, 0), i), model(400 + i));
    TEST_ASSERT(Assets_WaiterCount(assets, ASSET_KIND_COUNT) == 5, "five waiters registered");
    Park_Free(park);
    TEST_ASSERT(
        Assets_WaiterCount(assets, ASSET_KIND_COUNT) == 0, "the free leaves no waiter behind");
    Assets_Finished(assets, model(400), true);
    TEST_ASSERT(
        Assets_Land(assets, ASSET_KIND_ALL) == 0, "and nothing is called on a freed lookup");
    Assets_Free(assets);
}

int
main(void)
{
    test_put_find_replace();
    test_want_lands_at_landing_point();
    test_arrival_order_and_budget();
    test_replaced_and_cancelled_while_loading();
    test_cancel_scopes();
    test_chain_and_here();
    test_failed_and_late();
    test_ready_and_timed();
    test_landed_all();
    test_growth_and_landing_that_parks();
    test_free_drops_waiters();
    if( g_failures )
    {
        printf("rs_parked_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("rs_parked_test: all passed\n");
    return 0;
}
