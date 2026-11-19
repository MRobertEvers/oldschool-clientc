/*
 * The assets table (engine/asset_requests.h), driven by fake loaders.
 *
 * One load for many waiters; an awaiting task and wanting entries sharing one
 * load; every policy of the one table: give up after N, back off forever,
 * remember a failure; land late, land with the delay shortened, drop after a
 * deadline; a newer request replacing the older; landing the one entry or
 * every entry. Plus the cases of the trackers it replaced, moved here:
 * RS_NpcBodyLands (one load per (base, type), keyed on both), SeqLoadTracker
 * and the tree's seq_loads (no dedupe lost to a full table), AsyncPending-
 * Textures and texture_failed (no untracked load when full; failures of any
 * id remembered), AsyncPendingSeqBinds (a dropped element's binds never
 * land), InvIconRetry (the doubling wait, one pass in flight, a lift that
 * keeps the wait), and the music guard (one loader of the kind, the older
 * song never installed).
 *
 * Run: make -C src test-asset-requests
 */

#include "engine/asset_requests.h"

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

/* ---- fake loaders ------------------------------------------------------- */

#define FAKE_MAX_STARTS 256

struct FakeLoader
{
    struct AssetKey started[FAKE_MAX_STARTS];
    int start_count;
    /* What the next start answers. */
    enum AssetStart answer;
    /* Finish inside start (a loader answered at once), with this result. */
    int sync;
    bool sync_ok;
    /* Resident ids, for the resident hook. */
    int resident_ids[16];
    int resident_count;
};

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct FakeLoader* fake = data;

    if( fake->start_count < FAKE_MAX_STARTS )
        fake->started[fake->start_count] = key;
    fake->start_count++;
    if( fake->sync )
    {
        Assets_Finished(assets, key, fake->sync_ok);
        return ASSET_START_LOADING;
    }
    return fake->answer;
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

static struct Assets*
table_with(
    enum AssetKind kind,
    struct FakeLoader* fake)
{
    struct Assets* assets = Assets_New();
    struct AssetLoader loader = { fake_start, fake_resident, fake };

    memset(fake, 0, sizeof(*fake));
    fake->answer = ASSET_START_LOADING;
    Assets_SetLoader(assets, kind, &loader);
    return assets;
}

/* ---- a recording waiter ------------------------------------------------- */

#define LANDS_MAX 128

struct Lands
{
    int count;
    struct AssetKey key[LANDS_MAX];
    int32_t tag[LANDS_MAX];
    enum AssetOutcome outcome[LANDS_MAX];
    uint32_t late[LANDS_MAX];
};

static void
record_land(
    void* ctx,
    struct AssetKey key,
    struct AssetWaiterHandle handle,
    enum AssetOutcome outcome,
    uint32_t late_ms)
{
    struct Lands* lands = ctx;
    if( lands->count < LANDS_MAX )
    {
        lands->key[lands->count] = key;
        lands->tag[lands->count] = handle.v[0];
        lands->outcome[lands->count] = outcome;
        lands->late[lands->count] = late_ms;
    }
    lands->count++;
}

static struct AssetWaiter
waiter(
    struct Lands* lands,
    int32_t tag)
{
    struct AssetWaiter w;
    memset(&w, 0, sizeof(w));
    w.land = record_land;
    w.ctx = lands;
    w.handle.v[0] = tag;
    return w;
}

/* ---- one load, many waiters --------------------------------------------- */

static void
test_one_load_many_waiters(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MODEL, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_MODEL, 100, 0);
    struct Lands lands;
    struct AssetWaiter w1, w2, w3;

    memset(&lands, 0, sizeof(lands));
    w1 = waiter(&lands, 1);
    w2 = waiter(&lands, 2);
    w3 = waiter(&lands, 3);
    TEST_ASSERT(Assets_Want(assets, key, &w1) == ASSET_STATE_LOADING, "the first want loads");
    TEST_ASSERT(Assets_Want(assets, key, &w2) == ASSET_STATE_LOADING, "the second rides along");
    TEST_ASSERT(Assets_Want(assets, key, NULL) == ASSET_STATE_LOADING, "a prefetch rides along");
    TEST_ASSERT(Assets_Want(assets, key, &w3) == ASSET_STATE_LOADING, "the third rides along");
    TEST_ASSERT(fake.start_count == 1, "one load for every waiter");
    TEST_ASSERT(Assets_InFlight(assets, key), "in flight");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 0, "nothing lands before the load ends");

    Assets_Finished(assets, key, true);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "a landed asset has no entry");
    TEST_ASSERT(lands.count == 0, "landing waits for Assets_Land");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_BIT(ASSET_KIND_TEXTURE)) == 0,
        "another kind's landing point lands none of it");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_BIT(ASSET_KIND_MODEL)) == 3, "three landings");
    TEST_ASSERT(lands.tag[0] == 1 && lands.tag[1] == 2 && lands.tag[2] == 3,
        "landed in the order they asked");
    TEST_ASSERT(lands.outcome[0] == ASSET_OUTCOME_LANDED, "landed");
    TEST_ASSERT(Assets_Serial(assets) == 1, "the serial counts the end");

    /* Resident: answered without an entry or a load. */
    fake.resident_ids[fake.resident_count++] = 200;
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, 200, 0), &w1) ==
                    ASSET_STATE_HERE,
        "a resident asset is HERE");
    TEST_ASSERT(fake.start_count == 1, "and loads nothing");
    TEST_ASSERT(Assets_WaiterCount(assets, ASSET_KIND_COUNT) == 0, "and registers nothing");

    /* A loader with nothing to load: the waiter lands at the next landing. */
    fake.answer = ASSET_START_HERE;
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, 300, 0), &w2);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_MODEL, 300, 0)) == ASSET_STATE_NONE,
        "a start answering HERE ends the entry");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1, "and lands its waiter");

    /* A loader answered at once. */
    fake.sync = 1;
    fake.sync_ok = true;
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, 400, 0), &w3);
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_MODEL) == 0, "a synchronous load leaves nothing");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1, "and its waiter lands");
    Assets_Free(assets);
}

/* ---- an awaiting task and wanting entries share a load ---------------- */

static void
test_await_and_want_share(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_SEQUENCE, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_SEQUENCE, 808, 0);
    struct Lands lands;
    struct AssetWaiter w = waiter(&lands, 7);
    int join = 0;
    int join2 = 0;

    memset(&lands, 0, sizeof(lands));
    w = waiter(&lands, 7);
    TEST_ASSERT(Assets_Want(assets, key, &w) == ASSET_STATE_LOADING, "a visual want loads");
    TEST_ASSERT(Assets_Await(assets, key, &join) == 1, "a required await is counted");
    TEST_ASSERT(join == 1, "on the task's join");
    TEST_ASSERT(Assets_Await(assets, key, &join2) == 1, "a second task too");
    TEST_ASSERT(fake.start_count == 1, "all three share one load");

    Assets_Finished(assets, key, true);
    TEST_ASSERT(join == 0 && join2 == 0, "the awaiters are released when the load ends");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1, "the want lands at the landing point");

    /* A failed attempt releases the awaiters too: a task always resumes. */
    join = 0;
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 9, 0), &join);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 9, 0), false);
    TEST_ASSERT(join == 0, "a failed attempt releases its awaiter");

    /* A task freed early stops counting. */
    join = 0;
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 10, 0), &join);
    Assets_Unjoin(assets, &join);
    TEST_ASSERT(join == 0, "unjoined");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 10, 0), true);
    TEST_ASSERT(join == 0, "the load's end no longer touches the freed count");

    /* An abandoned loader (its task freed mid-read) is not a failure: the
     * entry is wanted again and a pump restarts it for its waiter. */
    memset(&lands, 0, sizeof(lands));
    join = 0;
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0), &w);
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0), &join);
    fake.start_count = 0;
    Assets_Abandoned(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0));
    TEST_ASSERT(join == 0, "an abandoned load releases its awaiters");
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0)) ==
                    ASSET_STATE_WANTED,
        "and is wanted again");
    TEST_ASSERT(Assets_Attempts(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0)) == 0,
        "without counting an attempt");
    Assets_Pump(assets);
    TEST_ASSERT(fake.start_count == 1, "the pump restarts it");
    Assets_Free(assets);
}

/* ---- fail policies ------------------------------------------------------ */

static void
test_give_up_after_n(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_OBJ_MODEL, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_OBJ_MODEL, 995, 3);
    struct Lands lands;
    struct AssetWaiter w;

    memset(&lands, 0, sizeof(lands));
    w = waiter(&lands, 1);
    TEST_ASSERT(Assets_Policy(ASSET_KIND_OBJ_MODEL)->fail_attempts == 4,
        "a ground stack goes back four times");
    Assets_Want(assets, key, &w);
    for( int i = 0; i < 3; i++ )
    {
        Assets_Finished(assets, key, false);
        TEST_ASSERT(Assets_InFlight(assets, key), "a waiter still waits: asked again at once");
    }
    TEST_ASSERT(fake.start_count == 4, "four loads");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 0, "nothing lands while it retries");
    Assets_Finished(assets, key, false);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "given up: the entry is gone");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1, "the waiter lands");
    TEST_ASSERT(lands.outcome[0] == ASSET_OUTCOME_FAILED, "FAILED");
    Assets_Want(assets, key, &w);
    TEST_ASSERT(fake.start_count == 5, "a later want starts a fresh count");
    TEST_ASSERT(Assets_Attempts(assets, key) == 0, "from zero");

    /* With nobody waiting, a failure ends the episode at once. */
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1, 1), NULL);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1, 1), false);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1, 1)) ==
                    ASSET_STATE_NONE,
        "a prefetch that failed is forgotten");

    /* A start that cannot make a load counts as a failed attempt. */
    fake.answer = ASSET_START_FAILED;
    memset(&lands, 0, sizeof(lands));
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 2, 1), &w);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 2, 1)) ==
                    ASSET_STATE_NONE,
        "four failed starts give up");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1 && lands.outcome[0] == ASSET_OUTCOME_FAILED,
        "and land FAILED");
    Assets_Free(assets);
}

static void
test_backoff_forever(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_INV_ICONS, &fake);
    struct AssetPolicy const* policy = Assets_Policy(ASSET_KIND_INV_ICONS);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_INV_ICONS, 0, 0);
    uint64_t now = 1000;

    /* The doubling wait and its ceiling (was InvIconRetry_NextWait). */
    TEST_ASSERT(Assets_BackoffNext(policy, 0) == 500u, "the first wait is 500 ms");
    TEST_ASSERT(Assets_BackoffNext(policy, 500u) == 1000u, "then it doubles");
    TEST_ASSERT(Assets_BackoffNext(policy, 2000u) == 4000u, "up to the ceiling");
    TEST_ASSERT(Assets_BackoffNext(policy, 4000u) == 4000u, "and stays there");
    TEST_ASSERT(Assets_BackoffNext(policy, 3000u) == 4000u, "a doubling past it is cut to it");

    /* One pass in flight at a time. */
    Assets_SetClock(assets, now);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(Assets_InFlight(assets, key), "a pass is in flight");
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 1, "and no other starts");

    /* A pass that resolved nothing holds the next off. */
    Assets_Finished(assets, key, false);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_WANTED, "backed off, still wanted");
    Assets_SetClock(assets, now + 499);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 1, "not before the wait is up");
    Assets_SetClock(assets, now + 500);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 2, "due when it is");
    Assets_Finished(assets, key, false);
    Assets_SetClock(assets, now + 500 + 999);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 2, "a second miss doubles the wait");

    /* A changed inventory lifts the hold and keeps the wait. */
    Assets_Lift(assets, key);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 3, "lifted: runs now");
    Assets_Finished(assets, key, false);
    Assets_SetClock(assets, now + 500 + 999 + 1999);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 3, "the kept wait doubled again (2000 ms)");
    Assets_SetClock(assets, now + 500 + 999 + 2000);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(fake.start_count == 4, "and ran when it was up");

    /* Never gives up. */
    now = Assets_Clock(assets);
    for( int i = 0; i < 20; i++ )
    {
        Assets_Finished(assets, key, false);
        now += 5000;
        Assets_SetClock(assets, now);
        Assets_Want(assets, key, NULL);
    }
    TEST_ASSERT(Assets_InFlight(assets, key), "still asking after twenty misses");

    /* A pass that resolved something lets the next run at once, with the wait
     * forgotten. */
    Assets_Finished(assets, key, true);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "resolved: the entry is gone");
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(Assets_InFlight(assets, key), "the next pass runs at once");
    Assets_Finished(assets, key, false);
    Assets_SetClock(assets, now + 500);
    Assets_Pump(assets);
    Assets_Want(assets, key, NULL);
    TEST_ASSERT(Assets_InFlight(assets, key), "and its first miss waits 500 ms again");
    Assets_Finished(assets, key, true);

    /* A back-off with a waiter is restarted by the pump; an awaiting task does
     * not wait out the back-off. */
    {
        struct Lands lands;
        struct AssetWaiter w;
        struct AssetKey m = AssetKey_Make(ASSET_KIND_INV_ICONS, 0, 1);
        int join = 0;
        int before;

        memset(&lands, 0, sizeof(lands));
        w = waiter(&lands, 4);
        Assets_Want(assets, m, &w);
        Assets_Finished(assets, m, false);
        before = fake.start_count;
        Assets_Pump(assets);
        TEST_ASSERT(fake.start_count == before, "the pump honours the wait");
        Assets_SetClock(assets, Assets_Clock(assets) + 500);
        Assets_Pump(assets);
        TEST_ASSERT(fake.start_count == before + 1, "and restarts it when it is up");
        Assets_Finished(assets, m, false);
        Assets_Await(assets, m, &join);
        TEST_ASSERT(fake.start_count == before + 2, "an await starts it now");
        Assets_Finished(assets, m, true);
        TEST_ASSERT(join == 0 && Assets_Land(assets, ASSET_KIND_ALL) == 1, "both released");
    }
    Assets_Free(assets);
}

static void
test_remembered_failure(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_TEXTURE, &fake);
    struct Lands lands;
    struct AssetWaiter w;
    int join = 0;

    memset(&lands, 0, sizeof(lands));
    w = waiter(&lands, 1);
    /* Every id, not only the first 2048 (texture_failed[2048] forgot the rest). */
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), &w);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), false);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0)) ==
                    ASSET_STATE_FAILED,
        "a texture past 2047 is remembered as failed");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1 && lands.outcome[0] == ASSET_OUTCOME_FAILED,
        "its waiter lands FAILED");
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), &w) ==
                    ASSET_STATE_FAILED,
        "a later want answers FAILED");
    TEST_ASSERT(Assets_Await(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), &join) == 0,
        "a later await does not wait");
    TEST_ASSERT(fake.start_count == 1, "and nothing is asked again");
    TEST_ASSERT(Assets_WaiterCount(assets, ASSET_KIND_COUNT) == 0, "nothing registered");

    /* Landed but unusable (the pixels would not convert): failed from then. */
    Assets_Fail(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 77, 0));
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 77, 0), NULL) ==
                    ASSET_STATE_FAILED,
        "a texture failed after landing is remembered");
    TEST_ASSERT(fake.start_count == 1, "and never loaded");

    /* More textures than the old pending list held: every one tracked, none
     * loaded twice (AsyncPendingTextures queued past 512 untracked). */
    for( int i = 0; i < 1500; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 10000 + i, 0), NULL);
    for( int i = 0; i < 1500; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 10000 + i, 0), NULL);
    TEST_ASSERT(fake.start_count == 1 + 1500, "1500 textures, 1500 loads");
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_TEXTURE) == 1502, "all tracked");
    Assets_Free(assets);
}

/* ---- late policies ------------------------------------------------------ */

static void
test_late_policies(void)
{
    struct FakeLoader model_fake;
    struct FakeLoader seq_fake;
    struct FakeLoader sound_fake;
    struct Assets* assets = table_with(ASSET_KIND_MODEL, &model_fake);
    struct AssetLoader seq_loader = { fake_start, NULL, &seq_fake };
    struct AssetLoader sound_loader = { fake_start, NULL, &sound_fake };
    struct Lands lands;
    struct AssetWaiter w;

    memset(&seq_fake, 0, sizeof(seq_fake));
    memset(&sound_fake, 0, sizeof(sound_fake));
    seq_fake.answer = ASSET_START_LOADING;
    sound_fake.answer = ASSET_START_LOADING;
    Assets_SetLoader(assets, ASSET_KIND_SEQUENCE, &seq_loader);
    Assets_SetLoader(assets, ASSET_KIND_SOUND, &sound_loader);
    memset(&lands, 0, sizeof(lands));
    w = waiter(&lands, 1);

    /* Land anyway. */
    Assets_SetClock(assets, 100);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, 1, 0), &w);
    Assets_SetClock(assets, 60100);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_MODEL, 1, 0), true);
    Assets_Land(assets, ASSET_KIND_ALL);
    TEST_ASSERT(lands.outcome[0] == ASSET_OUTCOME_LANDED, "a model a minute late still lands");

    /* Land with the delay shortened: the waiter is told how late it is. */
    Assets_SetClock(assets, 1000);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 1, 0), &w);
    Assets_SetClock(assets, 1340);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 1, 0), true);
    Assets_Land(assets, ASSET_KIND_ALL);
    TEST_ASSERT(lands.outcome[1] == ASSET_OUTCOME_LANDED && lands.late[1] == 340,
        "a sequence lands told it is 340 ms late");

    /* Drop after a deadline: 200 ms (ten client cycles). */
    TEST_ASSERT(Assets_Policy(ASSET_KIND_SOUND)->late == ASSET_LATE_DROP_AFTER &&
                    Assets_Policy(ASSET_KIND_SOUND)->late_limit_ms == 200,
        "a sound more than ten cycles late is dropped");
    Assets_SetClock(assets, 2000);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SOUND, 1, 0), &w);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SOUND, 2, 0), &w);
    Assets_SetClock(assets, 2150);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SOUND, 1, 0), true);
    Assets_Land(assets, ASSET_KIND_ALL);
    TEST_ASSERT(lands.outcome[2] == ASSET_OUTCOME_LANDED, "150 ms late: played");
    Assets_SetClock(assets, 2201);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SOUND, 2, 0), true);
    Assets_Land(assets, ASSET_KIND_ALL);
    TEST_ASSERT(lands.outcome[3] == ASSET_OUTCOME_DROPPED, "201 ms late: dropped");

    /* A sound wanted every cycle while it loads is one load (the ambient bed
     * queued a new one every tick, A2). */
    for( int i = 0; i < 50; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_SOUND, 3, 0), NULL);
    TEST_ASSERT(sound_fake.start_count == 3, "fifty ticks of wanting: one load");
    Assets_Free(assets);
}

/* ---- newest replaces ---------------------------------------------------- */

static void
test_newest_replaces(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MUSIC, &fake);
    struct AssetKey a = AssetKey_Make(ASSET_KIND_MUSIC, 76, 0);
    struct AssetKey b = AssetKey_Make(ASSET_KIND_MUSIC, 77, 0);
    struct AssetKey c = AssetKey_Make(ASSET_KIND_MUSIC, 78, 0);
    struct Lands lands;
    struct AssetWaiter w;

    memset(&lands, 0, sizeof(lands));
    w = waiter(&lands, 1);
    Assets_Want(assets, a, &w);
    TEST_ASSERT(Assets_InFlight(assets, a), "song A loading");
    w.handle.v[0] = 2;
    TEST_ASSERT(Assets_Want(assets, b, &w) == ASSET_STATE_WANTED, "song B waits for A's loader");
    TEST_ASSERT(fake.start_count == 1, "no second loader while one runs");
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1 && lands.tag[0] == 1 &&
                    lands.outcome[0] == ASSET_OUTCOME_DROPPED,
        "A's request is dropped at once");

    Assets_Finished(assets, a, true);
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 0, "A's late arrival lands nothing");
    TEST_ASSERT(Assets_State(assets, a) == ASSET_STATE_NONE, "A is forgotten");
    TEST_ASSERT(Assets_InFlight(assets, b) && fake.start_count == 2, "B starts when A's ends");

    /* A third while B loads, then B again before B's loader ends: B is the
     * newest once more and C is dropped without ever loading. */
    w.handle.v[0] = 3;
    Assets_Want(assets, c, &w);
    TEST_ASSERT(Assets_State(assets, c) == ASSET_STATE_WANTED, "C waits");
    w.handle.v[0] = 4;
    Assets_Want(assets, b, &w);
    TEST_ASSERT(Assets_State(assets, c) == ASSET_STATE_NONE, "C is replaced by B again");
    Assets_Finished(assets, b, true);
    lands.count = 0;
    Assets_Land(assets, ASSET_KIND_ALL);
    TEST_ASSERT(lands.count == 3, "B's first waiter dropped, C dropped, B's newest landed");
    TEST_ASSERT(lands.tag[2] == 4 && lands.outcome[2] == ASSET_OUTCOME_LANDED,
        "the newest request lands");
    TEST_ASSERT(fake.start_count == 2, "C never loaded");

    /* A newer request when the older failed: the newer still starts. */
    Assets_Want(assets, a, NULL);
    Assets_Want(assets, c, NULL);
    Assets_Finished(assets, a, false);
    TEST_ASSERT(Assets_InFlight(assets, c), "C starts after A's failed load");
    Assets_Free(assets);
}

/* A6: a failed skybox load was never asked for again (a request flag held it
 * until the next CAM_SKYBOX). Wanted every frame while not resident, it is
 * retried after the back-off; a newer skybox replaces one still loading. */
static void
test_skybox_retried_and_replaced(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_SKYBOX, &fake);
    struct AssetKey sky = AssetKey_Make(ASSET_KIND_SKYBOX, 51000, 0);
    struct AssetKey other = AssetKey_Make(ASSET_KIND_SKYBOX, 51001, 0);

    Assets_SetClock(assets, 0);
    for( int frame = 0; frame < 10; frame++ )
        Assets_Want(assets, sky, NULL);
    TEST_ASSERT(fake.start_count == 1, "ten frames of wanting: one load");
    Assets_Finished(assets, sky, false);
    Assets_SetClock(assets, 499);
    Assets_Want(assets, sky, NULL);
    TEST_ASSERT(fake.start_count == 1, "not retried inside the back-off");
    Assets_SetClock(assets, 500);
    Assets_Want(assets, sky, NULL);
    TEST_ASSERT(fake.start_count == 2, "a failed skybox is retried");
    Assets_Want(assets, other, NULL);
    TEST_ASSERT(fake.start_count == 2, "a newer skybox waits for the older loader");
    Assets_Finished(assets, sky, true);
    TEST_ASSERT(Assets_InFlight(assets, other) && fake.start_count == 3,
        "and starts when it ends; the older one is not kept");
    TEST_ASSERT(Assets_State(assets, sky) == ASSET_STATE_NONE, "the replaced one is gone");
    Assets_Free(assets);
}

/* ---- landing one or all ------------------------------------------------- */

static void
test_land_one_or_all(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_NPC_BODY, &fake);
    struct Lands walk;
    struct Lands other;
    struct AssetWaiter w;

    memset(&walk, 0, sizeof(walk));
    memset(&other, 0, sizeof(other));
    /* Three npcs of one type spawn while its body loads: one load, one walk. */
    for( int i = 0; i < 3; i++ )
    {
        w = waiter(&walk, i);
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3029), &w);
    }
    TEST_ASSERT(fake.start_count == 1, "one body load per type");
    TEST_ASSERT(Assets_WaiterCount(assets, ASSET_KIND_NPC_BODY) == 1, "one landing for all three");
    /* The key is (type, base): the same type under another base is its own. */
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3030), NULL);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3028, 3029), NULL);
    TEST_ASSERT(fake.start_count == 3, "the base and the type are both the key");
    /* Another owner is landed in its own right. */
    w = waiter(&other, 9);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3029), &w);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3029), true);
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 2, "one call per owner");
    TEST_ASSERT(walk.count == 1 && other.count == 1, "each owner once");
    TEST_ASSERT(Assets_InFlight(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3030)),
        "ending one type leaves the others in flight");

    /* ONE: every waiter its own call (see test_one_load_many_waiters). */
    TEST_ASSERT(Assets_Policy(ASSET_KIND_MODEL)->landed == ASSET_LANDED_ONE, "models land one by one");
    Assets_Free(assets);
}

/* ---- dropping waiters ---------------------------------------------------- */

static bool
match_element(
    struct AssetWaiterHandle handle,
    struct AssetKey key,
    void* arg)
{
    (void)key;
    return handle.v[0] == *(int*)arg;
}

static void
test_drop_waiters(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_SEQUENCE, &fake);
    struct Lands lands;
    struct AssetWaiter w;
    int element = 41;

    memset(&lands, 0, sizeof(lands));
    /* Two binds for element 41 on two seqs, one for 42. */
    w = waiter(&lands, 41);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 1, 0), &w);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 2, 0), &w);
    w = waiter(&lands, 42);
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 1, 0), &w);
    /* Seq 2 lands before the element goes: the arrived bind is dropped too. */
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 2, 0), true);
    TEST_ASSERT(Assets_DropWaiters(assets, record_land, &lands, match_element, &element) == 2,
        "both of 41's binds dropped");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 1, 0), true);
    TEST_ASSERT(Assets_Land(assets, ASSET_KIND_ALL) == 1, "only 42's bind lands");
    TEST_ASSERT(lands.tag[0] == 42, "42");
    TEST_ASSERT(Assets_WaiterCount(assets, ASSET_KIND_COUNT) == 0, "nothing left");
    Assets_Free(assets);
}

/* ---- growth -------------------------------------------------------------- */

static void
test_growth(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MODEL, &fake);
    struct Lands lands;
    struct AssetWaiter w;
    int ok = 1;

    memset(&lands, 0, sizeof(lands));
    w = waiter(&lands, 0);
    /* Far past every old fixed table (64, 512, 1024): dedupe never stops. */
    for( int i = 0; i < 5000; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0), (i % 3) ? NULL : &w);
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_MODEL) == 5000, "5000 entries");
    for( int i = 0; i < 5000; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0), NULL);
    TEST_ASSERT(fake.start_count == 5000, "and still one load each");
    /* Remove every other one (backward-shift deletes), then find the rest. */
    for( int i = 0; i < 5000; i += 2 )
        Assets_Finished(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0), true);
    for( int i = 0; i < 5000; i++ )
    {
        bool loading = Assets_InFlight(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0));
        if( loading != ((i % 2) == 1) )
            ok = 0;
    }
    TEST_ASSERT(ok, "every survivor found after the deletes");
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_COUNT) == 2500, "2500 left");
    Assets_Free(assets);
}

int
main(void)
{
    ToriMem_Init();
    test_one_load_many_waiters();
    test_await_and_want_share();
    test_give_up_after_n();
    test_backoff_forever();
    test_remembered_failure();
    test_late_policies();
    test_newest_replaces();
    test_skybox_retried_and_replaced();
    test_land_one_or_all();
    test_drop_waiters();
    test_growth();
    if( g_failures )
    {
        printf("asset_requests_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("asset_requests_test: all passed\n");
    ToriMem_Shutdown();
    return 0;
}
