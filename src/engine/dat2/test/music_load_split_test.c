/*
 * A song's loader reads on the asset queue and decodes on the compute queue.
 *
 * task_dat2_music_load.c: the song, instrument and sample tasks run on the
 * provider's asset queue (the App's parallel queue, drained every frame) and
 * only READ there; the sample decode -- milliseconds per sample, which in
 * wasm aborted a strict session at login as a parallel task computing -- is
 * one task on the provider's compute queue (the App's background queue,
 * given time and time-sliced). Against a real cache this checks:
 *
 *   - the decode task is on the compute queue and never on the asset queue;
 *   - it stops between samples when its slice is spent (several slices);
 *   - the song is installed only after it has ended, with every sample the
 *     song plays in the bank: the song still starts with every sample ready;
 *   - nothing is left on either queue.
 *
 *   make -C src test-music-load
 */

#include "asyncio.h"
#include "audio/torirs_music.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* A clock that moves 1 ms each time it is read, so every slice check the
 * decode makes after a sample finds its slice spent. */
static uint64_t g_now_us = 1000000;

static uint64_t
fake_now(void* data)
{
    (void)data;
    g_now_us += 1000;
    return g_now_us;
}

static int
queue_has(
    struct ToriRS_TaskQueue const* queue,
    char const* name)
{
    for( struct ToriRS_Task const* t = queue->head; t; t = t->next )
        if( strcmp(t->name, name) == 0 )
            return 1;
    return 0;
}

static void
runner_init(
    struct TaskRunner* runner,
    struct PlatformX_IO* px,
    int role)
{
    memset(runner, 0, sizeof(*runner));
    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IOBatch_New();
    runner->px = px;
    runner->role = role;
    runner->parallel = 1;
    TaskRunner_SetClock(runner, fake_now, NULL);
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

static void
test_song_reads_here_decodes_there(
    char const* root,
    char const* cache_name,
    char const* profile_name,
    int song_id)
{
    char path[1024];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct PlatformX_IO* px;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct TaskRunner parallel;
    struct TaskRunner background;
    struct ToriRS_MusicPlayer player;
    int requested = -1;
    enum ToriRS_MusicSource source = TORIRS_MUSIC_SOURCE_TRACK;
    int frames = 0;
    int decode_seen_frames = 0;
    int installed_while_decoding = 0;
    int decode_on_asset_queue = 0;
    int steps = 0;

    printf("TEST: %s song %d: reads on the asset queue, the decode on the compute queue\n",
        cache_name, song_id);
    snprintf(path, sizeof(path), "%s/%s", root, cache_name);
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP %s: not present\n", path);
        return;
    }
    if( !RSCache_ProfileByName(profile_name, &profile) )
        abort();
    RSCache_Dat2DiskSetProfile(disk, &profile);
    px = PlatformX_IO_New();
    assert(px);
    PlatformX_IO_InitDat2Disk(px, disk);

    runner_init(&parallel, px, TASK_RUNNER_ROLE_PARALLEL);
    runner_init(&background, px, TASK_RUNNER_ROLE_BACKGROUND);
    bc = dat2_buildcache_new();
    provider = dat2_buildcache_as_provider(bc);
    CacheProvider_SetProfile(provider, &profile);
    CacheProvider_SetAssetQueue(provider, parallel.queue);
    CacheProvider_SetComputeQueue(provider, background.queue);

    ToriRS_Music_Init(&player);
    ToriRS_Music_Request(&player, song_id, TORIRS_MUSIC_SOURCE_TRACK, true, 0, 0);
    CHECK(ToriRS_Music_TakeLoadRequest(&player, &requested, &source), "no load request");
    CHECK(requested == song_id, "the request is for song %d", requested);
    ToriRS_TaskQueue_Add(parallel.queue, CreateTask_MusicLoad(provider, &player, song_id, (int)source));

    /* Frames: the asset queue drained, then the compute queue given a slice,
     * as App_RunOnce does (the floor slice). */
    while( (parallel.queue->head || background.queue->head) && frames < 10000 )
    {
        (void)TaskRunner_PumpToStandstill(&parallel, 1 << 20, &steps);
        if( queue_has(parallel.queue, "Dat2MusicDecode") )
            decode_on_asset_queue = 1;
        if( queue_has(background.queue, "Dat2MusicDecode") )
        {
            decode_seen_frames++;
            if( player.current_song == song_id )
                installed_while_decoding = 1;
        }
        (void)TaskRunner_RunUntil(&background, g_now_us + 2000);
        frames++;
    }

    CHECK(!parallel.queue->head && !background.queue->head, "work left after %d frames", frames);
    CHECK(!decode_on_asset_queue, "the decode ran on the asset queue");
    CHECK(decode_seen_frames > 1,
        "the decode was on the compute queue for %d frame(s), not sliced across several",
        decode_seen_frames);
    CHECK(!installed_while_decoding, "the song was installed while its samples were decoding");
    CHECK(player.current_song == song_id, "the song was not installed (current %d)", player.current_song);
    CHECK(player.song != NULL, "the installed song has no track");
    CHECK(player.vorbis_setup != NULL, "the Vorbis setup was not decoded");
    CHECK(player.retained_count > 0, "the installed song holds no instrument");
    printf("   %d frame(s); the decode on the compute queue across %d of them\n",
        frames, decode_seen_frames);

    ToriRS_Music_Free(&player);
    dat2_buildcache_free(bc);
    runner_free(&parallel);
    runner_free(&background);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
}

int
main(
    int argc,
    char** argv)
{
    char const* root = argc > 1 ? argv[1] : "..";

    test_song_reads_here_decodes_there(root, "cache.osrs239", "osrs239", 0);
    if( g_failures )
    {
        printf("music_load_split_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("music_load_split_test: OK\n");
    return 0;
}
