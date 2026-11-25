/*
 * A song's loader reads on the asset queue and decodes on the compute queue.
 *
 * task_dat2_music_load.c: the song, instrument and sample tasks run on the
 * provider's asset queue (the App's parallel queue, drained every frame) and
 * only READ there; the track's unpack and the sample decode -- milliseconds
 * of CPU each, which in wasm aborted a strict session at login as a parallel
 * task computing, and then was one 15-16 ms step of the background job -- are
 * jobs on the provider's compute queue (the App's background queue, given
 * time and time-sliced), cut into bounded steps. Against a real cache this
 * checks:
 *
 *   - the unpack and the decode are on the compute queue, never on the asset
 *     queue;
 *   - they stop when their slice is spent (several slices);
 *   - the song is installed only after the decode has ended, with every sample
 *     the song plays in the bank: the song still starts with every sample
 *     ready;
 *   - THE LONGEST STEP: with every background slice already over when it is
 *     given, each step is one bounded unit of work, and its processor time
 *     (thread CPU time, so a loaded machine does not inflate it) stays under
 *     a bound -- as does the longest step of the reads on the asset queue.
 *     The bound is a fraction of what one piece of the uncut work costs in
 *     THIS build (the whole unpack of the largest track, the asset-queue
 *     step it once was, timed on the same clock), so an unoptimised build is
 *     held to the same proportion as a release one;
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
#include <time.h>

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

/* The clock mode B reads: this thread's processor time in microseconds, so
 * the runners' own step measurement (longest_step_us) is the work a step did,
 * not the time a loaded machine took to give it. */
static uint64_t
real_now(void* data)
{
    struct timespec ts;

    (void)data;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* The bounds mode B holds a step to, as fractions of the uncut unpack of
 * UNCUT_TRACK in the same build. Before the cut a decode step was one whole
 * sample (up to 8 ms; 4 ms on these songs, 15-16 ms in wasm) and the track's
 * unpack was a step of the asset queue (2.9 ms natively for UNCUT_TRACK). A
 * cut step is a fraction of a millisecond. Natively these are the 1.5 ms and
 * 1 ms a step was first held to. */
#define UNCUT_TRACK 735
#define LONGEST_DECODE_STEP_DIVISOR 2
#define LONGEST_READ_STEP_DIVISOR 3

/* What the whole unpack of `song_id`'s track costs in this build, in thread
 * CPU time: the least of three, so one interrupted run does not loosen the
 * bound. 0 if the cache is not here. */
static uint64_t
uncut_unpack_us(
    char const* root,
    char const* cache_name,
    char const* profile_name,
    int song_id)
{
    char path[1024];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    uint64_t least = UINT64_MAX;

    snprintf(path, sizeof(path), "%s/%s", root, cache_name);
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
        return 0;
    if( !RSCache_ProfileByName(profile_name, &profile) )
        abort();
    RSCache_Dat2DiskSetProfile(disk, &profile);
    archive = RSCache_Dat2DiskArchiveNewLoad(
        disk, RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MUSIC_TRACKS), song_id);
    assert(archive);
    for( int i = 0; i < 3; i++ )
    {
        uint64_t const start = real_now(NULL);
        struct RSCache_MusicSong* song = RSCache_MusicSongNewDecode(archive->data, archive->data_size);
        uint64_t const spent = real_now(NULL) - start;
        assert(song);
        RSCache_MusicSongFree(song);
        if( spent < least )
            least = spent;
    }
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return least;
}

/* The same clock in a cost clock's shape (TaskProf_SetCostClock): the
 * background runner meters each UNIT on it -- the work between two asks of
 * "is the slice over?", which is what can overrun a slice (G2). */
static uint64_t
real_cost_now(void)
{
    return real_now(NULL);
}

/* The limit the background runner names a unit over (G2): the same bound as
 * a background step, a fraction of this build's uncut unpack, so no verdict
 * depends on the optimiser. main sets it before the timed runs. */
static uint64_t g_unit_limit_us;

/*
 * One cold song through both queues.
 *
 * MODE A (`timed` 0): the fake clock, which finds every slice spent at its
 * first check, so the decode is spread over many frames; checks where each
 * task ran and that the song is installed only once every sample is in.
 *
 * MODE B (`timed` 1): a real clock, and every background slice already over
 * when it is given, so each step of the decode is exactly ONE unit of its work
 * (guarantee 1: a task gets its step whatever the clock says, and stops at
 * its first slice check). The runners' own measurement then says how long the
 * longest unit is, and the longest step of the reads on the parallel queue
 * (the track and the patches, unpacked where they are read): what one frame
 * pays for either.
 */
static void
run_song(
    char const* root,
    char const* cache_name,
    char const* profile_name,
    int song_id,
    int timed,
    uint64_t* out_longest_decode_us,
    uint64_t* out_longest_unit_us,
    uint64_t* out_longest_read_us)
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
    int unpack_seen_frames = 0;
    int steps = 0;

    printf("TEST: %s song %d (%s clock): reads on the asset queue, the decode on the compute queue\n",
        cache_name, song_id, timed ? "real" : "fake");
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
    if( timed )
    {
        TaskRunner_SetClock(&parallel, real_now, NULL);
        TaskRunner_SetClock(&background, real_now, NULL);
        TaskProf_SetCostClock(&background, real_cost_now);
        TaskProf_SetUnitLimit(&background, g_unit_limit_us);
        /* Measures every parallel step (longest_step_us) without naming
         * any: nothing here is slower than this. */
        TaskProf_SetSlowTask(&parallel, UINT64_MAX / 2);
    }
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
    while( (parallel.queue->head || background.queue->head) && frames < 100000 )
    {
        (void)TaskRunner_PumpToStandstill(&parallel, 1 << 20, &steps);
        if( queue_has(parallel.queue, "Dat2MusicDecode") ||
            queue_has(parallel.queue, "Dat2MusicUnpack") )
            decode_on_asset_queue = 1;
        if( queue_has(background.queue, "Dat2MusicUnpack") )
            unpack_seen_frames++;
        if( queue_has(background.queue, "Dat2MusicDecode") )
        {
            decode_seen_frames++;
            if( player.current_song == song_id )
                installed_while_decoding = 1;
        }
        if( timed )
            (void)TaskRunner_RunUntil(&background, real_now(NULL));
        else
            (void)TaskRunner_RunUntil(&background, g_now_us + 2000);
        frames++;
    }

    CHECK(!parallel.queue->head && !background.queue->head, "work left after %d frames", frames);
    CHECK(!decode_on_asset_queue, "the unpack or the decode ran on the asset queue");
    CHECK(unpack_seen_frames >= 1, "the track was not unpacked on the compute queue");
    CHECK(decode_seen_frames > 1,
        "the decode was on the compute queue for %d frame(s), not sliced across several",
        decode_seen_frames);
    CHECK(!installed_while_decoding, "the song was installed while its samples were decoding");
    CHECK(player.current_song == song_id, "the song was not installed (current %d)", player.current_song);
    CHECK(player.song != NULL, "the installed song has no track");
    CHECK(player.vorbis_setup != NULL, "the Vorbis setup was not decoded");
    CHECK(player.retained_count > 0, "the installed song holds no instrument");
    printf("   %d frame(s); the unpack on the compute queue across %d, the decode across %d\n",
        frames, unpack_seen_frames, decode_seen_frames);
    if( timed )
    {
        printf("   longest background step %.3f ms (%s), longest unit %.3f ms (%s); longest read step on the "
               "parallel queue %.3f ms (%s)\n",
            (double)background.prof.longest_step_us / 1000.0, background.prof.longest_step_name,
            (double)background.prof.longest_unit_us / 1000.0, background.prof.longest_unit_name,
            (double)parallel.prof.longest_step_us / 1000.0, parallel.prof.longest_step_name);
        CHECK(background.prof.unit_named_count == 0, "a background unit was over %.3f ms", (double)g_unit_limit_us / 1000.0);
        *out_longest_decode_us = background.prof.longest_step_us;
        *out_longest_unit_us = background.prof.longest_unit_us;
        *out_longest_read_us = parallel.prof.longest_step_us;
    }

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
    /* Four songs, and 735: the largest track in the cache (its whole unpack
     * is the 2.9 ms above). */
    static int const songs[] = { 0, 1, 76, 147, 735 };
    uint64_t longest_decode = 0;
    uint64_t longest_unit = 0;
    uint64_t longest_read = 0;
    uint64_t whole_us;

    whole_us = uncut_unpack_us(root, "cache.osrs239", "osrs239", UNCUT_TRACK);
    g_unit_limit_us = whole_us / LONGEST_DECODE_STEP_DIVISOR;
    run_song(root, "cache.osrs239", "osrs239", 0, 0, NULL, NULL, NULL);
    for( size_t i = 0; i < sizeof(songs) / sizeof(songs[0]); i++ )
    {
        uint64_t decode_us = 0;
        uint64_t unit_us = 0;
        uint64_t read_us = 0;
        run_song(root, "cache.osrs239", "osrs239", songs[i], 1, &decode_us, &unit_us, &read_us);
        if( decode_us > longest_decode )
            longest_decode = decode_us;
        if( unit_us > longest_unit )
            longest_unit = unit_us;
        if( read_us > longest_read )
            longest_read = read_us;
    }
    printf("LONGEST STEP: background %.3f ms (longest unit %.3f ms), reads %.3f ms (native processor time; a "
           "step of a slice already over is its guaranteed turn)\n",
        (double)longest_decode / 1000.0, (double)longest_unit / 1000.0, (double)longest_read / 1000.0);
    if( whole_us )
    {
        printf("UNCUT UNPACK: track %d %.3f ms in this build; bounds %.3f ms (background), %.3f ms (reads)\n",
            UNCUT_TRACK, (double)whole_us / 1000.0,
            (double)(whole_us / LONGEST_DECODE_STEP_DIVISOR) / 1000.0,
            (double)(whole_us / LONGEST_READ_STEP_DIVISOR) / 1000.0);
        CHECK(longest_decode < whole_us / LONGEST_DECODE_STEP_DIVISOR,
            "a background step took %.3f ms (bound %.3f ms, the uncut unpack / %d)",
            (double)longest_decode / 1000.0, (double)(whole_us / LONGEST_DECODE_STEP_DIVISOR) / 1000.0,
            LONGEST_DECODE_STEP_DIVISOR);
        CHECK(longest_read < whole_us / LONGEST_READ_STEP_DIVISOR,
            "a read step took %.3f ms (bound %.3f ms, the uncut unpack / %d)",
            (double)longest_read / 1000.0, (double)(whole_us / LONGEST_READ_STEP_DIVISOR) / 1000.0,
            LONGEST_READ_STEP_DIVISOR);
    }
    if( g_failures )
    {
        printf("music_load_split_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("music_load_split_test: OK\n");
    return 0;
}
