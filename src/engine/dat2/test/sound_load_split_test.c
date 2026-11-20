/*
 * A sound effect's loader reads on the asset queue and renders on the compute
 * queue (engine/dat2/task_dat2_sound_load.c).
 *
 * Rendering an effect runs its FM program sample by sample: up to 7.9 ms of
 * processor time natively for one osrs242 effect (8787; 24 effects take over
 * the parallel queue's 4 ms compute check), as one step of the asset queue
 * (the App's parallel queue, whose steps the frame pays for). It
 * is a job on the compute queue (the App's background queue), cut into
 * bounded steps. Against the real osrs242 cache this checks:
 *
 *   - the render is on the compute queue, never on the asset queue, and a long
 *     one stops when its slice is spent (several slices);
 *   - the sound is resident once the job ends, and it is the one-call render's
 *     sound, byte for byte (ToriRS_SoundFromRSCache on the same record);
 *   - a short effect is resident at the end of the frame it was read in, its
 *     render done in that frame's background slice: it starts on the cycle it
 *     did when it was rendered on the asset queue;
 *   - THE LONGEST STEP, with every background slice already over when it is
 *     given (so each background step is one unit of work), in thread CPU time:
 *     the longest step of the asset queue and of the compute queue stay under
 *     a bound;
 *   - nothing is left on either queue.
 *
 *   make -C src test-sound-load
 */

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_sound_from_rscache.h"
#include "game/rs_audio.h"
#include "toridraw_scene.h"
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

/* The longest effect render in cache.osrs242 through the loader's own record
 * (after the trim), and three short ones. */
#define LONG_EFFECT 8787
static int const k_short_effects[] = { 2500, 2727, 3220 };

/* A clock that moves 1 ms each time it is read: every slice check finds its
 * slice spent. */
static uint64_t g_now_us = 1000000;

static uint64_t
fake_now(void* data)
{
    (void)data;
    g_now_us += 1000;
    return g_now_us;
}

/* This thread's processor time, us. */
static uint64_t
cpu_now(void* data)
{
    struct timespec ts;

    (void)data;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* The bounds a step is held to, natively. Before the cut the whole render
 * was one step of the asset queue (7.9 ms for LONG_EFFECT). */
#define LONGEST_RENDER_STEP_US 1500
#define LONGEST_READ_STEP_US 1000

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
    int role,
    uint64_t (*now)(void*))
{
    memset(runner, 0, sizeof(*runner));
    runner->queue = ToriRS_TaskQueue_New();
    runner->io = ToriRS_IOBatch_New();
    runner->px = px;
    runner->role = role;
    runner->parallel = 1;
    TaskRunner_SetClock(runner, now, NULL);
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

struct Lane
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct PlatformX_IO* px;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct TaskRunner parallel;
    struct TaskRunner background;
};

static int
lane_open(
    struct Lane* lane,
    char const* root,
    uint64_t (*now)(void*))
{
    char path[1024];

    memset(lane, 0, sizeof(*lane));
    snprintf(path, sizeof(path), "%s/cache.osrs242", root);
    lane->disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !lane->disk )
    {
        printf("   SKIP %s: not present\n", path);
        return 0;
    }
    if( !RSCache_ProfileByName("osrs242", &lane->profile) )
        abort();
    RSCache_Dat2DiskSetProfile(lane->disk, &lane->profile);
    lane->px = PlatformX_IO_New();
    assert(lane->px);
    PlatformX_IO_InitDat2Disk(lane->px, lane->disk);
    runner_init(&lane->parallel, lane->px, TASK_RUNNER_ROLE_PARALLEL, now);
    runner_init(&lane->background, lane->px, TASK_RUNNER_ROLE_BACKGROUND, now);
    /* Measures every parallel step (longest_step_us) without naming any. */
    lane->parallel.slow_task_us = UINT64_MAX / 2;
    lane->bc = dat2_buildcache_new();
    lane->provider = dat2_buildcache_as_provider(lane->bc);
    CacheProvider_SetProfile(lane->provider, &lane->profile);
    CacheProvider_SetAssetQueue(lane->provider, lane->parallel.queue);
    CacheProvider_SetComputeQueue(lane->provider, lane->background.queue);
    return 1;
}

static void
lane_close(struct Lane* lane)
{
    dat2_buildcache_free(lane->bc);
    runner_free(&lane->parallel);
    runner_free(&lane->background);
    PlatformX_IO_Free(lane->px);
    RSCache_Dat2DiskFree(lane->disk);
}

/* One frame, as App_RunOnce has it: the asset queue drained, then the
 * compute queue given a slice ending at `slice_end_us` on the lane's clock. */
static void
frame(
    struct Lane* lane,
    uint64_t slice_end_us)
{
    int steps = 0;

    (void)TaskRunner_PumpToStandstill(&lane->parallel, 1 << 20, &steps);
    (void)TaskRunner_RunUntil(&lane->background, slice_end_us);
}

/* The one-call render of `id`'s record, as the asset queue used to make it. */
static struct ToriRS_Sound*
reference_sound(
    struct Lane* lane,
    int id)
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* group = NULL;
    struct RSCache_SoundEffect* effect;
    struct ToriRS_Sound* sound;
    char* record;
    int size;

    archive = RSCache_Dat2DiskArchiveNewLoad(
        lane->disk, RSCache_Dat2DiskTableId(lane->disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS), id);
    assert(archive);
    record = archive->data;
    size = archive->data_size;
    if( archive->file_count > 1 )
    {
        group = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        assert(group && group->file_count > 0);
        record = group->files[0];
        size = group->file_sizes[0];
    }
    effect = RSCache_SoundEffectNewDecode(&lane->profile, record, size);
    assert(effect);
    sound = ToriRS_SoundFromRSCache(&lane->profile, id, effect);
    RSCache_SoundEffectFree(effect);
    if( group )
        RSCache_FileListFree(group);
    RSCache_Dat2DiskArchiveFree(archive);
    return sound;
}

static int
same_sound(
    struct ToriRS_Sound const* a,
    struct ToriRS_Sound const* b)
{
    return a && b && a->sample_count == b->sample_count && a->sample_rate == b->sample_rate &&
           a->loop_start == b->loop_start && a->loop_end == b->loop_end &&
           a->queue_delay == b->queue_delay && a->pcm && b->pcm &&
           memcmp(a->pcm, b->pcm, (size_t)a->sample_count) == 0;
}

/* MODE A: the fake clock. The long effect is spread over frames, on the
 * compute queue, and lands as the one-call render. */
static void
test_the_render_is_a_sliced_background_job(char const* root)
{
    struct Lane lane;
    int frames = 0;
    int render_frames = 0;
    int rendered_on_asset_queue = 0;
    struct ToriRS_Sound* reference;

    printf("TEST: effect %d: read on the asset queue, rendered on the compute queue in slices\n",
        LONG_EFFECT);
    if( !lane_open(&lane, root, fake_now) )
        return;
    ToriRS_TaskQueue_Add(lane.parallel.queue, CreateTask_SoundLoad(lane.provider, LONG_EFFECT));
    while( (lane.parallel.queue->head || lane.background.queue->head) && frames < 100000 )
    {
        int steps = 0;

        (void)TaskRunner_PumpToStandstill(&lane.parallel, 1 << 20, &steps);
        if( queue_has(lane.parallel.queue, "Dat2SoundRender") )
            rendered_on_asset_queue = 1;
        if( queue_has(lane.background.queue, "Dat2SoundRender") )
            render_frames++;
        (void)TaskRunner_RunUntil(&lane.background, g_now_us + 2000);
        frames++;
    }
    CHECK(!lane.parallel.queue->head && !lane.background.queue->head, "work left after %d frames",
        frames);
    CHECK(!rendered_on_asset_queue, "the render ran on the asset queue");
    CHECK(render_frames > 1, "the render was on the compute queue for %d frame(s), not sliced",
        render_frames);
    reference = reference_sound(&lane, LONG_EFFECT);
    CHECK(CacheProvider_SoundHas(lane.provider, LONG_EFFECT), "the effect is not resident");
    CHECK(same_sound(CacheProvider_SoundGet(lane.provider, LONG_EFFECT), reference),
        "the sound is not the one-call render's");
    printf("   %d frame(s); the render on the compute queue across %d\n", frames, render_frames);
    ToriRS_SoundFree(reference);
    lane_close(&lane);
}

/* A short effect, with a real 2 ms slice: resident the frame it is read. */
static void
test_a_short_effect_lands_the_frame_it_is_read(char const* root)
{
    struct Lane lane;

    printf("TEST: a short effect is resident at the end of the frame it was read in\n");
    if( !lane_open(&lane, root, cpu_now) )
        return;
    for( size_t i = 0; i < sizeof(k_short_effects) / sizeof(k_short_effects[0]); i++ )
    {
        int const id = k_short_effects[i];
        struct ToriRS_Sound* reference;

        ToriRS_TaskQueue_Add(lane.parallel.queue, CreateTask_SoundLoad(lane.provider, id));
        frame(&lane, cpu_now(NULL) + 2000);
        CHECK(CacheProvider_SoundHas(lane.provider, id), "effect %d is not resident after one frame", id);
        reference = reference_sound(&lane, id);
        CHECK(same_sound(CacheProvider_SoundGet(lane.provider, id), reference),
            "effect %d is not the one-call render's", id);
        ToriRS_SoundFree(reference);
        frame(&lane, cpu_now(NULL) + 2000);
        CHECK(!lane.parallel.queue->head && !lane.background.queue->head, "effect %d left work", id);
    }
    lane_close(&lane);
}

/* MODE B: every background slice already over when it is given, so each
 * background step is one unit; the runners measure the longest step. */
static void
test_the_longest_step(char const* root)
{
    struct Lane lane;
    int frames = 0;

    printf("TEST: the longest step of either queue, effect %d (thread CPU time)\n", LONG_EFFECT);
    if( !lane_open(&lane, root, cpu_now) )
        return;
    ToriRS_TaskQueue_Add(lane.parallel.queue, CreateTask_SoundLoad(lane.provider, LONG_EFFECT));
    while( (lane.parallel.queue->head || lane.background.queue->head) && frames < 1000000 )
    {
        frame(&lane, cpu_now(NULL));
        frames++;
    }
    CHECK(CacheProvider_SoundHas(lane.provider, LONG_EFFECT), "the effect is not resident");
    printf("LONGEST STEP: asset queue %.3f ms (%s), compute queue %.3f ms (%s), %d frames\n",
        (double)lane.parallel.longest_step_us / 1000.0, lane.parallel.longest_step_name,
        (double)lane.background.longest_step_us / 1000.0, lane.background.longest_step_name,
        frames);
    CHECK(lane.parallel.longest_step_us < LONGEST_READ_STEP_US,
        "an asset-queue step took %.3f ms (bound %.3f ms)",
        (double)lane.parallel.longest_step_us / 1000.0, LONGEST_READ_STEP_US / 1000.0);
    CHECK(lane.background.longest_step_us < LONGEST_RENDER_STEP_US,
        "a compute-queue step took %.3f ms (bound %.3f ms)",
        (double)lane.background.longest_step_us / 1000.0, LONGEST_RENDER_STEP_US / 1000.0);
    lane_close(&lane);
}


/*
 * G3: a cold effect under catch-up frames.
 *
 * The audio tick counts an effect down once a logic cycle and drops it ten
 * cycles past its moment if its clip is not resident (RS_AUDIO_LATE_LIMIT_
 * TICKS, the reference's `delays[i] >= -10`). The clip is read on the asset
 * queue and rendered on the background queue, one floor slice a frame. A
 * frame that runs late pays up to 5 cycles before its one floor (a page that
 * stalled, a tab woken from a throttle, a phone under load): the countdown
 * then runs 5 cycles per slice of render, and a long render loses the effect
 * to our own scheduling -- the reference's clip, read and rendered
 * synchronously, is resident on the tick it is asked for. r2-web's one
 * dropped effect ('rs_audio: dropping effect 357, 11 ticks late') came after
 * a 0.5 s gap in the page's frames.
 *
 * `cycles_per_frame` cycles, then one 2 ms floor slice of real processor
 * time, until the effect is played or dropped. Returns 1 played, 0 dropped.
 */
static int
cold_effect_under_frames(
    char const* root,
    int cycles_per_frame,
    int* out_frames)
{
    struct Lane lane;
    struct ToriDraw_Scene* scene;
    struct RS_Audio audio;
    struct ToriRS_AudioQueue queue;
    struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
    int frames = 0;
    int played;

    if( !lane_open(&lane, root, cpu_now) )
        return -1;
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    RS_Audio_Init(&audio);
    ToriRS_AudioQueue_Reset(&queue);
    /* The server's SYNTH_SOUND, and the load the audio tick's want starts
     * (Assets_Want: the sound loader on the asset queue). */
    RS_Audio_Synth(&audio, LONG_EFFECT, 1, 0);
    ToriRS_TaskQueue_Add(lane.parallel.queue, CreateTask_SoundLoad(lane.provider, LONG_EFFECT));
    while( audio.count > 0 && frames < 500 )
    {
        for( int c = 0; c < cycles_per_frame && audio.count > 0; c++ )
        {
            RS_Audio_Tick(&audio, lane.provider, NULL, scene, NULL, 0, 0, 0, NULL, &queue);
            (void)ToriRS_AudioQueue_Drain(&queue, commands, TORIRS_AUDIO_QUEUE_MAX);
        }
        frame(&lane, cpu_now(NULL) + 2000);
        frames++;
    }
    played = audio.played;
    *out_frames = frames;
    /* Whatever is left of the load (a dropped effect leaves its render). */
    for( int f = 0; f < 1000 && (lane.parallel.queue->head || lane.background.queue->head); f++ )
        frame(&lane, cpu_now(NULL) + 2000);
    ToriDraw_SceneFree(scene);
    lane_close(&lane);
    return played > 0;
}

static void
test_a_cold_effect_under_catch_up_frames(char const* root)
{
    int frames = 0;
    int played;

    printf("TEST: a cold effect (%d) under catch-up frames: played, not lost to the render (G3)\n",
        LONG_EFFECT);
    played = cold_effect_under_frames(root, 1, &frames);
    if( played < 0 )
        return;
    printf("   one cycle a frame: %s after %d frame(s)\n", played ? "played" : "DROPPED", frames);
    CHECK(played == 1, "the effect was dropped at one cycle a frame");
    played = cold_effect_under_frames(root, 5, &frames);
    printf("   five cycles a frame: %s after %d frame(s)\n", played ? "played" : "DROPPED", frames);
    CHECK(played == 1, "the effect was dropped under catch-up frames (5 cycles to one 2 ms floor): its "
                       "render, not its read, was late");
}

int
main(
    int argc,
    char** argv)
{
    char const* root = argc > 1 ? argv[1] : "..";

    test_the_render_is_a_sliced_background_job(root);
    test_a_short_effect_lands_the_frame_it_is_read(root);
    test_the_longest_step(root);
    test_a_cold_effect_under_catch_up_frames(root);
    if( g_failures )
    {
        printf("sound_load_split_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("sound_load_split_test: OK\n");
    return 0;
}
