/*
 * The music install as a parallel task that awaits (app/app_tick.c,
 * Task_MusicInstall), on an App with the parallel queue, the assets table,
 * the pending index and a fake ASSET_KIND_MUSIC loader:
 *
 *   - a request the player holds is one task and one load, however many ticks
 *     ask (TakeLoadRequest answers every tick while it is held);
 *   - a newer song cancels the older task, which exits at once; the older
 *     loader runs to its end, and the newer load starts after it (one loader
 *     of the kind: two never grow the shared soundbank at once);
 *   - a song the table answers FAILED (no loader could be made) is told to
 *     the player, which stops asking for it;
 *   - a loader freed mid-read ends the task with the request still held, and
 *     the next tick's task starts the load again;
 *   - no device: no task;
 *   - every task ends and unlists itself.
 *
 *   make -C src test-music-install
 */

#include "app.h"
#include "app/app_internal.h"
#include "asyncio.h"
#include "audio/torirs_music.h"
#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- the fixture: an App with the queue, the tables and a provider ------- */

#define LOADS_MAX 64

struct Loads
{
    struct AssetKey keys[LOADS_MAX];
    int count;
};

static struct Loads g_loads;

static enum AssetStart
fake_music_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    assert(g_loads.count < LOADS_MAX);
    g_loads.keys[g_loads.count++] = key;
    return ASSET_START_LOADING;
}

static int
loads_of(int song_id)
{
    int n = 0;
    for( int i = 0; i < g_loads.count; i++ )
        if( g_loads.keys[i].kind == ASSET_KIND_MUSIC && g_loads.keys[i].id == song_id )
            n++;
    return n;
}

struct Fixture
{
    struct App app;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct ToriRS_IOBatch* io;
};

static void
fixture_init(struct Fixture* fx)
{
    memset(fx, 0, sizeof(*fx));
    memset(&g_loads, 0, sizeof(g_loads));
    fx->provider.vtable = &fx->vtable;
    CacheProvider_InitEngineCaches(&fx->provider);
    fx->app.provider = &fx->provider;
    fx->app.frame_runner.queue = ToriRS_TaskQueue_New();
    App_AssetsInit(&fx->app);
    App_PendingInit(&fx->app);
    /* No residency: every song reaches its loader (as asset_resident_leaf). */
    App_AssetsSetLoader(&fx->app, ASSET_KIND_MUSIC, fake_music_start, NULL);
    RS_Audio_Init(&fx->app.audio);
    ToriRS_Music_Init(&fx->app.audio.music);
    fx->io = ToriRS_IOBatch_New();
}

static void
fixture_free(struct Fixture* fx)
{
    TEST_ASSERT(fx->app.frame_runner.queue->head == NULL, "every task ended");
    TEST_ASSERT(RS_Pending_Listed(fx->app.pending) == 0, "and unlisted itself");
    TEST_ASSERT(Assets_JoinCount(fx->app.assets, ASSET_KIND_COUNT) == 0, "and counts on nothing");
    ToriRS_TaskQueue_Free(fx->app.frame_runner.queue);
    App_PendingFree(&fx->app);
    App_AssetsFree(&fx->app);
    ToriRS_IOBatch_Free(fx->io);
    RS_Audio_Shutdown(&fx->app.audio);
    CacheProvider_FreeEngineCaches(&fx->provider);
}

/* Step the parallel queue until nothing more can progress, as a frame does. */
static void
pump(struct Fixture* fx)
{
    for( int pass = 0; pass < 8; pass++ )
    {
        struct ToriRS_Task* task = fx->app.frame_runner.queue->head;
        while( task )
        {
            struct ToriRS_Task* next = task->next;
            /* A parked task is not run: only what it parked on readies it. */
            if( !task->parked )
                (void)ToriRS_TaskQueue_RunTask(fx->app.frame_runner.queue, fx->io, task);
            task = next;
        }
    }
}

static int
queued(struct Fixture const* fx)
{
    int n = 0;
    for( struct ToriRS_Task* t = fx->app.frame_runner.queue->head; t; t = t->next )
        n++;
    return n;
}

/* A logic tick's music step, then the frame's pump. */
static void
frame(struct Fixture* fx)
{
    app_music_tick(&fx->app);
    pump(fx);
}

static void
request(
    struct Fixture* fx,
    int song_id)
{
    ToriRS_Music_Request(&fx->app.audio.music, song_id, TORIRS_MUSIC_SOURCE_TRACK, true, 0, 0);
}

static struct AssetKey
track(int song_id)
{
    return AssetKey_Make(ASSET_KIND_MUSIC, song_id, (int)TORIRS_MUSIC_SOURCE_TRACK);
}

/* ---- the cases ---------------------------------------------------------- */

static void
test_one_task_one_load(void)
{
    struct Fixture fx;
    int song = -1;

    fixture_init(&fx);
    request(&fx, 10);
    for( int i = 0; i < 20; i++ )
        frame(&fx);
    TEST_ASSERT(app_music_install_pending(&fx.app, &song) && song == 10, "a task installs song 10");
    TEST_ASSERT(queued(&fx) == 1, "one task, however many ticks ask");
    TEST_ASSERT(loads_of(10) == 1, "and one load");
    TEST_ASSERT(Assets_InFlight(fx.app.assets, track(10)), "in flight");

    /* The loader installs the song itself; its end releases the task. */
    Assets_Finished(fx.app.assets, track(10), true);
    pump(&fx);
    TEST_ASSERT(!app_music_install_pending(&fx.app, NULL), "the task ended with the load");
    fixture_free(&fx);
}

static void
test_newer_song_replaces(void)
{
    struct Fixture fx;
    int song = -1;

    fixture_init(&fx);
    request(&fx, 10);
    frame(&fx);
    TEST_ASSERT(loads_of(10) == 1, "song 10 loading");

    request(&fx, 11);
    frame(&fx);
    TEST_ASSERT(app_music_install_pending(&fx.app, &song) && song == 11, "the task is song 11's");
    TEST_ASSERT(queued(&fx) == 1, "the older task exited at once (a cancel wakes it)");
    TEST_ASSERT(loads_of(11) == 0, "no second loader while song 10's runs");
    TEST_ASSERT(Assets_State(fx.app.assets, track(11)) == ASSET_STATE_WANTED, "song 11 waits");
    for( int i = 0; i < 5; i++ )
        frame(&fx);
    TEST_ASSERT(queued(&fx) == 1 && loads_of(11) == 0, "and keeps waiting, one task");

    /* Song 10's loader ends (the player refuses its install itself): 11
     * starts. */
    Assets_Finished(fx.app.assets, track(10), true);
    TEST_ASSERT(loads_of(11) == 1, "song 11 starts when 10's loader ends");
    pump(&fx);
    TEST_ASSERT(app_music_install_pending(&fx.app, &song) && song == 11, "its task still waits");

    /* Back to 10 before 11's loader ends: 10 waits for 11's. */
    request(&fx, 10);
    frame(&fx);
    TEST_ASSERT(app_music_install_pending(&fx.app, &song) && song == 10, "song 10's task again");
    TEST_ASSERT(loads_of(10) == 1, "not loaded beside 11's loader");
    Assets_Finished(fx.app.assets, track(11), true);
    TEST_ASSERT(loads_of(10) == 2, "and loaded after it");
    Assets_Finished(fx.app.assets, track(10), true);
    pump(&fx);
    TEST_ASSERT(!app_music_install_pending(&fx.app, NULL), "nothing pending");
    fixture_free(&fx);
}

static void
test_failed_song_told_to_player(void)
{
    struct Fixture fx;
    int const failed_before = 0;

    fixture_init(&fx);
    request(&fx, 12);
    frame(&fx);
    TEST_ASSERT(fx.app.audio.music.has_request, "the request is held while it loads");
    TEST_ASSERT(fx.app.audio.music.songs_failed == failed_before, "nothing failed yet");
    Assets_Finished(fx.app.assets, track(12), false);
    pump(&fx);
    TEST_ASSERT(!app_music_install_pending(&fx.app, NULL), "the task ended");
    TEST_ASSERT(!fx.app.audio.music.has_request, "the player stopped asking");
    TEST_ASSERT(fx.app.audio.music.songs_failed == failed_before + 1, "and counted the failure");
    for( int i = 0; i < 5; i++ )
        frame(&fx);
    TEST_ASSERT(loads_of(12) == 1 && queued(&fx) == 0, "and is not asked again");
    fixture_free(&fx);
}

static void
test_abandoned_load_asked_again(void)
{
    struct Fixture fx;

    fixture_init(&fx);
    request(&fx, 13);
    frame(&fx);
    TEST_ASSERT(loads_of(13) == 1, "song 13 loading");
    Assets_Abandoned(fx.app.assets, track(13));
    pump(&fx);
    TEST_ASSERT(!app_music_install_pending(&fx.app, NULL), "an abandoned load ends the task");
    TEST_ASSERT(fx.app.audio.music.has_request, "with the request still held");
    frame(&fx);
    TEST_ASSERT(app_music_install_pending(&fx.app, NULL), "the next tick adds a task");
    TEST_ASSERT(loads_of(13) == 2, "which starts the load again");
    Assets_Finished(fx.app.assets, track(13), true);
    pump(&fx);
    fixture_free(&fx);
}

static void
test_no_device_no_task(void)
{
    struct Fixture fx;

    fixture_init(&fx);
    RS_Audio_SetDevicePresent(&fx.app.audio, false);
    request(&fx, 14);
    for( int i = 0; i < 3; i++ )
        frame(&fx);
    TEST_ASSERT(!app_music_install_pending(&fx.app, NULL) && loads_of(14) == 0,
        "no device: the request waits, nothing loads");
    RS_Audio_SetDevicePresent(&fx.app.audio, true);
    frame(&fx);
    TEST_ASSERT(app_music_install_pending(&fx.app, NULL) && loads_of(14) == 1,
        "the device back: the song in force is fetched");
    Assets_Finished(fx.app.assets, track(14), true);
    pump(&fx);
    fixture_free(&fx);
}

/* A logout cancels every pending task: the install exits, the loader runs on
 * to its end and counts into nothing. */
static void
test_cancel_all(void)
{
    struct Fixture fx;

    fixture_init(&fx);
    request(&fx, 15);
    frame(&fx);
    TEST_ASSERT(queued(&fx) == 1, "one task");
    (void)RS_Pending_Cancel(fx.app.pending, RS_PendingScope_All());
    pump(&fx);
    TEST_ASSERT(queued(&fx) == 0, "cancelled: it exited");
    TEST_ASSERT(Assets_JoinCount(fx.app.assets, ASSET_KIND_MUSIC) == 0, "and counts on nothing");
    Assets_Finished(fx.app.assets, track(15), true);
    fixture_free(&fx);
}

int
main(void)
{
    ToriMem_Init();
    test_one_task_one_load();
    test_newer_song_replaces();
    test_failed_song_told_to_player();
    test_abandoned_load_asked_again();
    test_no_device_no_task();
    test_cancel_all();
    if( g_failures )
    {
        printf("music_install_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("music_install_test: all passed\n");
    return 0;
}
