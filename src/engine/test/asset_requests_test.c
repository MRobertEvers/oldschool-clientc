/*
 * The assets table (engine/asset_requests.h), driven by fake loaders.
 *
 * Every case in the awaiting form: a task counts a load on a join and is
 * released when the attempt ends (there is no callback waiter). One load for
 * many awaiters; awaiters and prefetching wants sharing one load; every
 * policy of the one table: give up (the owner awaiting again up to its
 * count), back off forever, remember a failure; a newer request replacing the
 * older, with an awaiter of a replaced loading entry kept to its loader's end
 * and asking again for a replaced one making it the newest once more. Plus
 * the cases of the trackers it replaced, moved here: RS_NpcBodyLands (one
 * load per (base, type), keyed on both), SeqLoadTracker and the tree's
 * seq_loads (no dedupe lost to a full table), AsyncPendingTextures and
 * texture_failed (no untracked load when full; failures of any id
 * remembered), AsyncPendingSeqBinds (a task that went away is never written
 * to), InvIconRetry (the doubling wait, one pass in flight, a lift that keeps
 * the wait), and the music guard (one loader of the kind, the older song's
 * awaiter told DROPPED). And what an awaiting task learns when its join drops
 * (Assets_AwaitOutcome).
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

/* ---- one load, many awaiters -------------------------------------------- */

static void
test_one_load_many_awaiters(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MODEL, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_MODEL, 100, 0);
    int join[3] = { 0, 0, 0 };
    enum AssetOutcome outcome[3];

    TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join[0], &outcome[0]) == 1, "the first await loads");
    TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join[1], &outcome[1]) == 1, "the second rides along");
    TEST_ASSERT(Assets_Want(assets, key) == ASSET_STATE_LOADING, "a prefetch rides along");
    TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join[2], &outcome[2]) == 1, "the third rides along");
    TEST_ASSERT(fake.start_count == 1, "one load for every awaiter");
    TEST_ASSERT(Assets_InFlight(assets, key), "in flight");
    TEST_ASSERT(Assets_JoinCount(assets, ASSET_KIND_MODEL) == 3, "three counted");
    TEST_ASSERT(join[0] == 1 && join[1] == 1 && join[2] == 1, "nothing released before the load ends");

    Assets_Finished(assets, key, true);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "a landed asset has no entry");
    TEST_ASSERT(join[0] == 0 && join[1] == 0 && join[2] == 0, "every awaiter released at the end");
    TEST_ASSERT(outcome[0] == ASSET_OUTCOME_LANDED && outcome[1] == ASSET_OUTCOME_LANDED &&
                    outcome[2] == ASSET_OUTCOME_LANDED,
        "and told it landed");
    TEST_ASSERT(Assets_JoinCount(assets, ASSET_KIND_COUNT) == 0, "nothing left counted");
    TEST_ASSERT(Assets_Serial(assets) == 1, "the serial counts the end");

    /* Resident: answered without an entry or a load. */
    fake.resident_ids[fake.resident_count++] = 200;
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, 200, 0)) == ASSET_STATE_HERE,
        "a resident asset is HERE");
    TEST_ASSERT(Assets_Await(assets, AssetKey_Make(ASSET_KIND_MODEL, 200, 0), &join[0]) == 0 &&
                    join[0] == 0,
        "and is not waited for");
    TEST_ASSERT(fake.start_count == 1, "and loads nothing");

    /* A loader with nothing to load: the awaiter is released at once. */
    fake.answer = ASSET_START_HERE;
    Assets_AwaitOutcome(assets, AssetKey_Make(ASSET_KIND_MODEL, 300, 0), &join[1], &outcome[1]);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_MODEL, 300, 0)) == ASSET_STATE_NONE,
        "a start answering HERE ends the entry");
    TEST_ASSERT(join[1] == 0 && outcome[1] == ASSET_OUTCOME_LANDED, "and releases its awaiter LANDED");

    /* A loader answered at once. */
    fake.sync = 1;
    fake.sync_ok = true;
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, 400, 0)) == ASSET_STATE_NONE,
        "a synchronous load leaves no entry");
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_MODEL) == 0, "nothing left");
    Assets_Free(assets);
}

/* ---- awaiters and wants share a load ----------------------------------- */

static void
test_await_and_want_share(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_SEQUENCE, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_SEQUENCE, 808, 0);
    int join = 0;
    int join2 = 0;

    TEST_ASSERT(Assets_Want(assets, key) == ASSET_STATE_LOADING, "a prefetch loads");
    TEST_ASSERT(Assets_Await(assets, key, &join) == 1, "a required await is counted");
    TEST_ASSERT(join == 1, "on the task's join");
    TEST_ASSERT(Assets_Await(assets, key, &join2) == 1, "a second task too");
    TEST_ASSERT(fake.start_count == 1, "all three share one load");

    Assets_Finished(assets, key, true);
    TEST_ASSERT(join == 0 && join2 == 0, "the awaiters are released when the load ends");

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
    TEST_ASSERT(Assets_JoinCount(assets, ASSET_KIND_SEQUENCE) == 0, "and forgotten");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 10, 0), true);
    TEST_ASSERT(join == 0, "the load's end no longer touches the freed count");

    /* An abandoned loader (its task freed mid-read) is not a failure: the
     * entry is wanted again, and the next ask restarts it. A5: the maps
     * reference table's loading flag was set by the square reading it and
     * never cleared when that square's task was freed mid-read, so its
     * siblings waited forever; an abandoned load releases them instead. */
    join = 0;
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0), &join);
    fake.start_count = 0;
    Assets_Abandoned(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0));
    TEST_ASSERT(join == 0, "an abandoned load releases its awaiters");
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0)) ==
                    ASSET_STATE_WANTED,
        "and is wanted again");
    TEST_ASSERT(Assets_Attempts(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0)) == 0,
        "without counting an attempt");
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0));
    TEST_ASSERT(fake.start_count == 1, "the next want restarts it");

    /* Shutting down: a loader freed with its queue ends nothing, and its
     * parent's join (freed with the same queue) is not touched. */
    join = 7;
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 12, 0), &join);
    Assets_Close(assets);
    Assets_Abandoned(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 12, 0));
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 11, 0), true);
    TEST_ASSERT(join == 8, "a closed table releases no join");
    Assets_Free(assets);
}

/* ---- fail policies ------------------------------------------------------ */

/* GIVE_UP: an attempt that fails releases its awaiter FAILED and the entry is
 * forgotten; the owner (a ground stack's task) goes back by awaiting again,
 * counting its own attempts up to the kind's `fail_attempts`. */
static void
test_give_up_after_n(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_OBJ_MODEL, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_OBJ_MODEL, 995, 3);
    enum AssetOutcome outcome = ASSET_OUTCOME_LANDED;
    int const attempts_max = Assets_Policy(ASSET_KIND_OBJ_MODEL)->fail_attempts;
    int join = 0;
    int attempts = 0;

    TEST_ASSERT(attempts_max == 4, "a ground stack goes back four times");
    for( ;; )
    {
        TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join, &outcome) == 1, "each attempt is awaited");
        attempts++;
        Assets_Finished(assets, key, false);
        TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "and released FAILED");
        TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "the entry forgotten");
        if( attempts >= attempts_max )
            break;
    }
    TEST_ASSERT(fake.start_count == 4, "four loads");

    /* With nobody waiting, a failure ends the episode at once. */
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1, 1));
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1, 1), false);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1, 1)) ==
                    ASSET_STATE_NONE,
        "a prefetch that failed is forgotten");

    /* A start that cannot make a load counts as a failed attempt: the
     * awaiter is released FAILED before the await returns. */
    fake.answer = ASSET_START_FAILED;
    outcome = ASSET_OUTCOME_LANDED;
    Assets_AwaitOutcome(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 2, 1), &join, &outcome);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "a failed start releases FAILED");
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 2, 1)) ==
                    ASSET_STATE_NONE,
        "and leaves no entry");
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
    TEST_ASSERT(Assets_Due(assets, key), "a fresh table is due at once");
    Assets_Want(assets, key);
    TEST_ASSERT(Assets_InFlight(assets, key), "a pass is in flight");
    TEST_ASSERT(!Assets_Due(assets, key), "and no other pass is due");
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 1, "and no other starts");

    /* A pass that resolved nothing holds the next off. */
    Assets_Finished(assets, key, false);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_WANTED, "backed off, still wanted");
    Assets_SetClock(assets, now + 499);
    TEST_ASSERT(!Assets_Due(assets, key), "not due before the wait is up");
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 1, "not before the wait is up");
    Assets_SetClock(assets, now + 500);
    TEST_ASSERT(Assets_Due(assets, key), "due when it is");
    Assets_SetClock(assets, now + 500);
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 2, "due when it is");
    Assets_Finished(assets, key, false);
    Assets_SetClock(assets, now + 500 + 999);
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 2, "a second miss doubles the wait");

    /* A changed inventory lifts the hold and keeps the wait. */
    Assets_Lift(assets, key);
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 3, "lifted: runs now");
    Assets_Finished(assets, key, false);
    Assets_SetClock(assets, now + 500 + 999 + 1999);
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 3, "the kept wait doubled again (2000 ms)");
    Assets_SetClock(assets, now + 500 + 999 + 2000);
    Assets_Want(assets, key);
    TEST_ASSERT(fake.start_count == 4, "and ran when it was up");

    /* Never gives up. */
    now = Assets_Clock(assets);
    for( int i = 0; i < 20; i++ )
    {
        Assets_Finished(assets, key, false);
        now += 5000;
        Assets_SetClock(assets, now);
        Assets_Want(assets, key);
    }
    TEST_ASSERT(Assets_InFlight(assets, key), "still asking after twenty misses");

    /* A pass that resolved something lets the next run at once, with the wait
     * forgotten. */
    Assets_Finished(assets, key, true);
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "resolved: the entry is gone");
    Assets_Want(assets, key);
    TEST_ASSERT(Assets_InFlight(assets, key), "the next pass runs at once");
    Assets_Finished(assets, key, false);
    Assets_SetClock(assets, now + 500);
    Assets_Want(assets, key);
    TEST_ASSERT(Assets_InFlight(assets, key), "and its first miss waits 500 ms again");
    Assets_Finished(assets, key, true);

    /* A want honours the back-off; an awaiting task does not wait it out. */
    {
        struct AssetKey m = AssetKey_Make(ASSET_KIND_INV_ICONS, 0, 1);
        int join = 0;
        int before;

        Assets_Want(assets, m);
        Assets_Finished(assets, m, false);
        before = fake.start_count;
        Assets_Want(assets, m);
        TEST_ASSERT(fake.start_count == before, "a want honours the wait");
        Assets_Await(assets, m, &join);
        TEST_ASSERT(fake.start_count == before + 1 && join == 1, "an await starts it now");
        Assets_Finished(assets, m, true);
        TEST_ASSERT(join == 0, "and is released");
    }
    Assets_Free(assets);
}

static void
test_remembered_failure(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_TEXTURE, &fake);
    enum AssetOutcome outcome = ASSET_OUTCOME_LANDED;
    int join = 0;

    /* Every id, not only the first 2048 (texture_failed[2048] forgot the rest). */
    Assets_AwaitOutcome(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), &join, &outcome);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), false);
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0)) ==
                    ASSET_STATE_FAILED,
        "a texture past 2047 is remembered as failed");
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "its awaiter is released FAILED");
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0)) ==
                    ASSET_STATE_FAILED,
        "a later want answers FAILED");
    TEST_ASSERT(Assets_Await(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 5000, 0), &join) == 0,
        "a later await does not wait");
    TEST_ASSERT(fake.start_count == 1, "and nothing is asked again");
    TEST_ASSERT(Assets_JoinCount(assets, ASSET_KIND_COUNT) == 0, "nothing counted");

    /* Landed but unusable (the pixels would not convert): failed from then,
     * and an awaiter still counted on it is released FAILED. */
    Assets_Fail(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 77, 0));
    TEST_ASSERT(Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 77, 0)) == ASSET_STATE_FAILED,
        "a texture failed after landing is remembered");
    TEST_ASSERT(fake.start_count == 1, "and never loaded");

    /* More textures than the old pending list held: every one tracked, none
     * loaded twice (AsyncPendingTextures queued past 512 untracked). */
    for( int i = 0; i < 1500; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 10000 + i, 0));
    for( int i = 0; i < 1500; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_TEXTURE, 10000 + i, 0));
    TEST_ASSERT(fake.start_count == 1 + 1500, "1500 textures, 1500 loads");
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_TEXTURE) == 1502, "all tracked");
    Assets_Free(assets);
}

/* A sound wanted every cycle while it loads is one load (the ambient bed
 * queued a new one every tick, A2). */
static void
test_sound_one_load(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_SOUND, &fake);

    for( int i = 0; i < 50; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_SOUND, 3, 0));
    TEST_ASSERT(fake.start_count == 1, "fifty ticks of wanting: one load");
    Assets_Free(assets);
}

/* ---- newest replaces ---------------------------------------------------- */

/*
 * The music guard, as the music install awaits it (app/app_tick.c,
 * Task_MusicInstall): one loader of the kind; a newer song waits for the
 * older loader to end, which cannot be stopped mid-read; the older song's
 * awaiter is kept to that end and told DROPPED (its install task exits; the
 * player refuses the older install itself); a song wanted again before its
 * replaced loader ended is the newest once more.
 */
static void
test_newest_replaces(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MUSIC, &fake);
    struct AssetKey a = AssetKey_Make(ASSET_KIND_MUSIC, 76, 0);
    struct AssetKey b = AssetKey_Make(ASSET_KIND_MUSIC, 77, 0);
    struct AssetKey c = AssetKey_Make(ASSET_KIND_MUSIC, 78, 0);
    int join_a = 0, join_b1 = 0, join_b2 = 0, join_c = 0;
    enum AssetOutcome out_a, out_b1, out_b2, out_c;

    TEST_ASSERT(Assets_Policy(ASSET_KIND_MUSIC)->newer == ASSET_NEWER_REPLACE, "music replaces");
    Assets_AwaitOutcome(assets, a, &join_a, &out_a);
    TEST_ASSERT(Assets_InFlight(assets, a), "song A loading");
    Assets_AwaitOutcome(assets, b, &join_b1, &out_b1);
    TEST_ASSERT(Assets_State(assets, b) == ASSET_STATE_WANTED && join_b1 == 1,
        "song B is counted and waits for A's loader");
    TEST_ASSERT(fake.start_count == 1, "no second loader while one runs");
    TEST_ASSERT(join_a == 1, "A's awaiter is kept until A's loader ends");

    Assets_Finished(assets, a, true);
    TEST_ASSERT(join_a == 0 && out_a == ASSET_OUTCOME_DROPPED,
        "and released DROPPED then, though the load landed: it was replaced");
    TEST_ASSERT(Assets_State(assets, a) == ASSET_STATE_NONE, "A is forgotten");
    TEST_ASSERT(Assets_InFlight(assets, b) && fake.start_count == 2, "B starts when A's ends");
    TEST_ASSERT(join_b1 == 1, "B's awaiter waits for B");

    /* A third while B loads, then B again before B's loader ends: B is the
     * newest once more and C is dropped without ever loading. */
    Assets_AwaitOutcome(assets, c, &join_c, &out_c);
    TEST_ASSERT(Assets_State(assets, c) == ASSET_STATE_WANTED && join_c == 1, "C waits");
    Assets_AwaitOutcome(assets, b, &join_b2, &out_b2);
    TEST_ASSERT(Assets_State(assets, c) == ASSET_STATE_NONE, "C is replaced by B again");
    TEST_ASSERT(join_c == 0 && out_c == ASSET_OUTCOME_DROPPED,
        "C's awaiter, on an entry with no loader, is released DROPPED at once");
    Assets_Finished(assets, b, true);
    TEST_ASSERT(join_b1 == 0 && join_b2 == 0 && out_b1 == ASSET_OUTCOME_LANDED &&
                    out_b2 == ASSET_OUTCOME_LANDED,
        "B, the newest again, lands for both of its awaiters");
    TEST_ASSERT(fake.start_count == 2, "C never loaded");

    /* A replaced loading entry wanted again (a prefetch rather than an
     * await) is the newest once more, too. */
    Assets_Await(assets, a, &join_a);
    Assets_Want(assets, c);
    Assets_Want(assets, a);
    TEST_ASSERT(Assets_State(assets, c) == ASSET_STATE_NONE, "a want makes A the newest again");
    Assets_Finished(assets, a, true);
    TEST_ASSERT(join_a == 0 && Assets_JoinCount(assets, ASSET_KIND_COUNT) == 0, "and A lands");

    /* A newer request when the older failed: the newer still starts. */
    Assets_Await(assets, a, &join_a);
    Assets_AwaitOutcome(assets, c, &join_c, &out_c);
    Assets_Finished(assets, a, false);
    TEST_ASSERT(join_a == 0, "A's awaiter released");
    TEST_ASSERT(Assets_InFlight(assets, c), "C starts after A's failed load");
    Assets_Finished(assets, c, true);
    TEST_ASSERT(join_c == 0 && out_c == ASSET_OUTCOME_LANDED, "and lands");

    /* An awaiter that goes away while its replaced loader runs (the install
     * task cancelled by a newer song) is never written to again. */
    Assets_AwaitOutcome(assets, a, &join_a, &out_a);
    Assets_AwaitOutcome(assets, b, &join_b1, &out_b1);
    Assets_Unjoin(assets, &join_a);
    out_a = ASSET_OUTCOME_FAILED;
    Assets_Finished(assets, a, true);
    TEST_ASSERT(join_a == 0 && out_a == ASSET_OUTCOME_FAILED, "a cancelled awaiter is untouched");
    TEST_ASSERT(Assets_InFlight(assets, b), "and the newer song starts");
    Assets_Abandoned(assets, b);
    TEST_ASSERT(join_b1 == 0 && out_b1 == ASSET_OUTCOME_DROPPED, "an abandoned song is DROPPED");
    TEST_ASSERT(Assets_State(assets, b) == ASSET_STATE_WANTED, "and wanted again");
    Assets_AwaitOutcome(assets, b, &join_b1, &out_b1);
    TEST_ASSERT(Assets_InFlight(assets, b), "an await restarts it");
    Assets_Finished(assets, b, true);
    TEST_ASSERT(join_b1 == 0 && out_b1 == ASSET_OUTCOME_LANDED, "and it lands");
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
        Assets_Want(assets, sky);
    TEST_ASSERT(fake.start_count == 1, "ten frames of wanting: one load");
    Assets_Finished(assets, sky, false);
    Assets_SetClock(assets, 499);
    Assets_Want(assets, sky);
    TEST_ASSERT(fake.start_count == 1, "not retried inside the back-off");
    Assets_SetClock(assets, 500);
    Assets_Want(assets, sky);
    TEST_ASSERT(fake.start_count == 2, "a failed skybox is retried");
    Assets_Want(assets, other);
    TEST_ASSERT(fake.start_count == 2, "a newer skybox waits for the older loader");
    Assets_Finished(assets, sky, true);
    TEST_ASSERT(Assets_InFlight(assets, other) && fake.start_count == 3,
        "and starts when it ends; the older one is not kept");
    TEST_ASSERT(Assets_State(assets, sky) == ASSET_STATE_NONE, "the replaced one is gone");
    Assets_Free(assets);
}

/* ---- one load per (type, base) ----------------------------------------- */

static void
test_npc_body_keyed_on_both(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_NPC_BODY, &fake);
    int join[3] = { 0, 0, 0 };
    int other = 0;

    /* Three npcs of one type spawn while its body loads: one load. */
    for( int i = 0; i < 3; i++ )
        Assets_Await(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3029), &join[i]);
    TEST_ASSERT(fake.start_count == 1, "one body load per type");
    /* The key is (type, base): the same type under another base is its own. */
    Assets_Want(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3030));
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3028, 3029), &other);
    TEST_ASSERT(fake.start_count == 3, "the base and the type are both the key");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3029), true);
    TEST_ASSERT(join[0] == 0 && join[1] == 0 && join[2] == 0, "one end releases all three");
    TEST_ASSERT(other == 1, "and not the other type's");
    TEST_ASSERT(Assets_InFlight(assets, AssetKey_Make(ASSET_KIND_NPC_BODY, 3029, 3030)),
        "ending one type leaves the others in flight");
    Assets_Free(assets);
}

/* ---- configs ------------------------------------------------------------ */

/* A7: every player-body fetch queued its own idk and obj config loads, so a
 * crowd in one appearance (or one player's blocks while the first fetch ran)
 * read each config once per fetch. Through the table it is one read; the
 * variant (kit or obj) is part of the key because the two share no id space. */
static void
test_configs_one_read(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_CONFIG, &fake);
    int join_a = 0;
    int join_b = 0;

    for( int body = 0; body < 5; body++ )
    {
        Assets_Await(
            assets, AssetKey_Make(ASSET_KIND_CONFIG, 1205, ASSET_CONFIG_OBJ), body ? &join_b : &join_a);
        Assets_Await(
            assets, AssetKey_Make(ASSET_KIND_CONFIG, 18, ASSET_CONFIG_IDK), body ? &join_b : &join_a);
    }
    TEST_ASSERT(fake.start_count == 2, "five body fetches, two config reads");
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_CONFIG, 18, ASSET_CONFIG_OBJ), &join_b);
    TEST_ASSERT(fake.start_count == 3, "obj 18 is not kit 18");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_CONFIG, 1205, ASSET_CONFIG_OBJ), true);
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_CONFIG, 18, ASSET_CONFIG_IDK), true);
    TEST_ASSERT(join_a == 0, "the first fetch is released");
    TEST_ASSERT(join_b == 1, "the others wait only on obj 18");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_CONFIG, 18, ASSET_CONFIG_OBJ), false);
    TEST_ASSERT(join_b == 0, "and are released when it fails");
    TEST_ASSERT(Assets_State(assets, AssetKey_Make(ASSET_KIND_CONFIG, 18, ASSET_CONFIG_OBJ)) ==
                    ASSET_STATE_NONE,
        "a missing config is forgotten, asked again by the next fetch");
    Assets_Free(assets);
}

/* AsyncPendingSeqBinds kept duplicates on purpose: the same task awaiting
 * twice on one join is counted twice and released by the one end. */
static void
test_same_join_twice(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_SEQUENCE, &fake);
    int join = 0;

    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 100, 0), &join);
    Assets_Await(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 100, 0), &join);
    TEST_ASSERT(fake.start_count == 1, "one load");
    TEST_ASSERT(join == 2 && Assets_JoinCount(assets, ASSET_KIND_SEQUENCE) == 2, "two counts held");
    Assets_Finished(assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 100, 0), true);
    TEST_ASSERT(join == 0, "both released");
    Assets_Free(assets);
}

/* ---- growth -------------------------------------------------------------- */

static void
test_growth(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MODEL, &fake);
    int join = 0;
    int ok = 1;

    /* Far past every old fixed table (64, 512, 1024): dedupe never stops. */
    for( int i = 0; i < 5000; i++ )
    {
        if( i % 3 )
            Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0));
        else
            Assets_Await(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0), &join);
    }
    TEST_ASSERT(Assets_Count(assets, ASSET_KIND_MODEL) == 5000, "5000 entries");
    for( int i = 0; i < 5000; i++ )
        Assets_Want(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0));
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
    for( int i = 1; i < 5000; i += 2 )
        Assets_Finished(assets, AssetKey_Make(ASSET_KIND_MODEL, i * 7, 0), true);
    TEST_ASSERT(join == 0, "the awaiting task is released once all its loads ended");
    Assets_Free(assets);
}

/* ---- what an awaiter learns -------------------------------------------- */

/* After the join drops, the awaiting task knows how the attempt ended: here,
 * failed (and whether the table goes back for it), or abandoned. Written at
 * once when nothing is counted. A task that went away (Assets_Unjoin) is
 * never written to again. */
static void
test_await_outcome(void)
{
    struct FakeLoader fake;
    struct Assets* assets = table_with(ASSET_KIND_MODEL, &fake);
    struct AssetKey key = AssetKey_Make(ASSET_KIND_MODEL, 41, 0);
    enum AssetOutcome outcome = ASSET_OUTCOME_LANDED;
    int join = 0;

    TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join, &outcome) == 1 && join == 1,
        "counted on the join");
    TEST_ASSERT(outcome == ASSET_OUTCOME_DROPPED, "not here while the attempt runs");
    Assets_Finished(assets, key, true);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_LANDED, "landed: here");

    /* Already resident: nothing counted, and the answer is written at once. */
    fake.resident_ids[fake.resident_count++] = 42;
    outcome = ASSET_OUTCOME_DROPPED;
    TEST_ASSERT(Assets_AwaitOutcome(assets, AssetKey_Make(ASSET_KIND_MODEL, 42, 0), &join,
                    &outcome) == 0 && join == 0,
        "a resident asset is not waited for");
    TEST_ASSERT(outcome == ASSET_OUTCOME_LANDED, "and the task is told it is here");

    /* A failed attempt of a back-off kind: FAILED, and the table will go
     * back for it (the task may await again). */
    key = AssetKey_Make(ASSET_KIND_MODEL, 43, 0);
    Assets_AwaitOutcome(assets, key, &join, &outcome);
    Assets_Finished(assets, key, false);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "the attempt failed");
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_WANTED, "and a retry is due");
    TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join, &outcome) == 1,
        "awaiting again starts the retry at once");
    Assets_Finished(assets, key, true);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_LANDED, "the second await landed");

    /* An abandoned load (its loader freed): DROPPED, not FAILED. */
    key = AssetKey_Make(ASSET_KIND_MODEL, 44, 0);
    Assets_AwaitOutcome(assets, key, &join, &outcome);
    Assets_Abandoned(assets, key);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_DROPPED, "abandoned: not here, not failed");

    /* A task that went away is never written to. */
    key = AssetKey_Make(ASSET_KIND_MODEL, 45, 0);
    Assets_AwaitOutcome(assets, key, &join, &outcome);
    Assets_Unjoin(assets, &join);
    outcome = ASSET_OUTCOME_FAILED;
    Assets_Finished(assets, key, true);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "an unjoined outcome is untouched");
    Assets_Free(assets);

    /* A remembered failure answers at once; give-up after N ends FAILED with
     * the entry gone. */
    assets = table_with(ASSET_KIND_TEXTURE, &fake);
    key = AssetKey_Make(ASSET_KIND_TEXTURE, 7, 0);
    Assets_AwaitOutcome(assets, key, &join, &outcome);
    Assets_Finished(assets, key, false);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "a texture that failed");
    outcome = ASSET_OUTCOME_LANDED;
    TEST_ASSERT(Assets_AwaitOutcome(assets, key, &join, &outcome) == 0 &&
                    outcome == ASSET_OUTCOME_FAILED,
        "is remembered: answered FAILED at once");
    Assets_Free(assets);

    assets = table_with(ASSET_KIND_NPC_BODY, &fake);
    key = AssetKey_Make(ASSET_KIND_NPC_BODY, 3, 3);
    Assets_AwaitOutcome(assets, key, &join, &outcome);
    Assets_Finished(assets, key, false);
    TEST_ASSERT(join == 0 && outcome == ASSET_OUTCOME_FAILED, "an npc body given up on");
    TEST_ASSERT(Assets_State(assets, key) == ASSET_STATE_NONE, "the entry is gone");
    Assets_Free(assets);
}

int
main(void)
{
    ToriMem_Init();
    test_one_load_many_awaiters();
    test_await_and_want_share();
    test_give_up_after_n();
    test_backoff_forever();
    test_remembered_failure();
    test_sound_one_load();
    test_newest_replaces();
    test_skybox_retried_and_replaced();
    test_npc_body_keyed_on_both();
    test_configs_one_read();
    test_same_join_twice();
    test_growth();
    test_await_outcome();
    if( g_failures )
    {
        printf("asset_requests_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("asset_requests_test: all passed\n");
    ToriMem_Shutdown();
    return 0;
}
