/*
 * A dat1 sound effect's loader reads on the asset queue and renders on the
 * compute queue (engine/dat1/task_dat1_sound_load.c), as the dat2 loader does
 * (engine/dat2/test/sound_load_split_test.c).
 *
 * The bank behind every dat1 effect (`sounds.dat`) is decoded once; rendering
 * one effect runs its FM program sample by sample, which is processor work
 * of the kind the parallel queue (whose steps the frame pays for) must not
 * do in one step. Against the real cache254.lostcity this checks:
 *
 *   - the longest effect in the bank is rendered on the compute queue, never
 *     on the asset queue, and stops when its slice is spent (several slices);
 *   - every effect the bank holds is resident once its load ends, and it is
 *     the one-call render's sound, byte for byte, with the same lead-in
 *     (ToriRS_SoundFromRSCache on a bank decoded apart);
 *   - THE LONGEST STEP, with every background slice already over when it is
 *     given, in thread CPU time: the asset queue's stays under a bound;
 *   - nothing is left on either queue.
 *
 *   make -C src test-dat1-sound-load
 */

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/torirs_sound_from_rscache.h"
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

/* The bound an asset-queue step is held to, natively (the dat2 test's). */
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
    struct RSCache_Dat1Disk* disk;
    struct PlatformX_IO* px;
    struct Dat1BuildCache* bc;
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
    snprintf(path, sizeof(path), "%s/cache254.lostcity", root);
    lane->disk = RSCache_Dat1DiskNewFromDirectory(path);
    if( !lane->disk )
    {
        printf("   SKIP %s: not present\n", path);
        return 0;
    }
    if( !RSCache_ProfileByName("lc254", &lane->profile) )
        abort();
    lane->px = PlatformX_IO_New();
    assert(lane->px);
    PlatformX_IO_InitDat1Disk(lane->px, lane->disk);
    runner_init(&lane->parallel, lane->px, TASK_RUNNER_ROLE_PARALLEL, now);
    runner_init(&lane->background, lane->px, TASK_RUNNER_ROLE_BACKGROUND, now);
    /* Measures every parallel step (longest_step_us) without naming any. */
    lane->parallel.slow_task_us = UINT64_MAX / 2;
    lane->bc = dat1_buildcache_new();
    lane->provider = dat1_buildcache_as_provider(lane->bc);
    CacheProvider_SetProfile(lane->provider, &lane->profile);
    CacheProvider_SetAssetQueue(lane->provider, lane->parallel.queue);
    CacheProvider_SetComputeQueue(lane->provider, lane->background.queue);
    return 1;
}

static void
lane_close(struct Lane* lane)
{
    dat1_buildcache_free(lane->bc);
    runner_free(&lane->parallel);
    runner_free(&lane->background);
    PlatformX_IO_Free(lane->px);
    RSCache_Dat1DiskFree(lane->disk);
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

/* Load `id` through the provider until both queues are idle. */
static void
load(
    struct Lane* lane,
    int id)
{
    struct ToriRS_Task* task = CreateTask_SoundLoad(lane->provider, id);
    int frames = 0;

    if( !task )
        return;
    ToriRS_TaskQueue_Add(lane->parallel.queue, task);
    while( (lane->parallel.queue->head || lane->background.queue->head) && frames < 1000000 )
    {
        frame(lane, TaskRunner_Now(&lane->background) + 1000000);
        frames++;
    }
}

/* The bank, decoded apart from the provider's, by a second provider (`ref`):
 * the reference renders start from untrimmed records, as the provider's first
 * load of each does. A load of an id no bank holds decodes the bank and
 * renders nothing. The bank is `ref`'s. */
static struct RSCache_SoundBank*
reference_bank(struct Lane* ref)
{
    struct RSCache_SoundBank* bank;

    load(ref, 65000);
    bank = dat1_buildcache_get_sound_bank(ref->bc);
    assert(bank);
    assert(bank->count <= 65000);
    return bank;
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

/* The effect the bank holds with the longest render. */
static int
longest_effect(struct RSCache_SoundBank const* bank)
{
    int best = -1;
    int best_ms = -1;

    for( int id = 0; id < bank->count; id++ )
    {
        int ms;

        if( !bank->effects[id] )
            continue;
        ms = RSCache_SoundEffectDurationMs(bank->effects[id]);
        if( ms > best_ms )
        {
            best_ms = ms;
            best = id;
        }
    }
    return best;
}

/* MODE A: the fake clock. The longest effect is spread over frames, on the
 * compute queue, and lands as the one-call render. */
static void
test_the_render_is_a_sliced_background_job(char const* root)
{
    struct Lane lane;
    struct Lane ref;
    struct RSCache_SoundBank* bank;
    struct ToriRS_Sound* reference;
    int frames = 0;
    int render_frames = 0;
    int rendered_on_asset_queue = 0;
    int id;

    printf("TEST: the longest dat1 effect: read on the asset queue, rendered on the compute queue in slices\n");
    if( !lane_open(&lane, root, fake_now) || !lane_open(&ref, root, fake_now) )
        return;
    bank = reference_bank(&ref);
    id = longest_effect(bank);
    assert(id >= 0);
    ToriRS_TaskQueue_Add(lane.parallel.queue, CreateTask_SoundLoad(lane.provider, id));
    while( (lane.parallel.queue->head || lane.background.queue->head) && frames < 100000 )
    {
        int steps = 0;

        (void)TaskRunner_PumpToStandstill(&lane.parallel, 1 << 20, &steps);
        if( queue_has(lane.parallel.queue, "Dat1SoundRender") )
            rendered_on_asset_queue = 1;
        if( queue_has(lane.background.queue, "Dat1SoundRender") )
            render_frames++;
        (void)TaskRunner_RunUntil(&lane.background, g_now_us + 2000);
        frames++;
    }
    CHECK(!lane.parallel.queue->head && !lane.background.queue->head, "work left after %d frames", frames);
    CHECK(!rendered_on_asset_queue, "the render ran on the asset queue");
    CHECK(render_frames > 1, "effect %d's render was on the compute queue for %d frame(s), not sliced", id,
        render_frames);
    reference = ToriRS_SoundFromRSCache(&lane.profile, id, bank->effects[id]);
    CHECK(CacheProvider_SoundHas(lane.provider, id), "effect %d is not resident", id);
    CHECK(same_sound(CacheProvider_SoundGet(lane.provider, id), reference),
        "effect %d is not the one-call render's", id);
    printf("   effect %d (%d ms): %d frame(s); the render on the compute queue across %d\n", id,
        RSCache_SoundEffectDurationMs(bank->effects[id]), frames, render_frames);
    ToriRS_SoundFree(reference);
    lane_close(&ref);
    lane_close(&lane);
}

/* Every effect of the bank, through the loader, against the one-call render. */
static void
test_every_effect_is_the_one_call_render(char const* root)
{
    struct Lane lane;
    struct Lane ref;
    struct RSCache_SoundBank* bank;
    int effects = 0;
    int silent = 0;
    int differ = 0;

    printf("TEST: every dat1 effect through the loader is the one-call render, byte for byte\n");
    if( !lane_open(&lane, root, fake_now) || !lane_open(&ref, root, fake_now) )
        return;
    bank = reference_bank(&ref);
    for( int id = 0; id < bank->count; id++ )
    {
        struct ToriRS_Sound* reference;

        if( !bank->effects[id] )
            continue;
        effects++;
        load(&lane, id);
        reference = ToriRS_SoundFromRSCache(&lane.profile, id, bank->effects[id]);
        if( !reference )
        {
            silent++;
            CHECK(!CacheProvider_SoundHas(lane.provider, id), "silent effect %d is resident", id);
            continue;
        }
        if( !same_sound(CacheProvider_SoundGet(lane.provider, id), reference) )
        {
            if( differ < 5 )
                printf("   effect %d differs from the one-call render\n", id);
            differ++;
        }
        ToriRS_SoundFree(reference);
    }
    CHECK(effects > 0, "the bank holds no effect");
    CHECK(differ == 0, "%d of %d effects differ from the one-call render", differ, effects);
    CHECK(!lane.parallel.queue->head && !lane.background.queue->head, "work left");
    printf("   %d effects (%d silent), %d differ\n", effects, silent, differ);
    lane_close(&ref);
    lane_close(&lane);
}

/* MODE B: every background slice already over when it is given, so each
 * background step is one unit; the runners measure the longest step, over
 * every effect the bank holds. */
static void
test_the_longest_step(char const* root)
{
    struct Lane lane;
    struct Lane ref;
    struct RSCache_SoundBank* bank;
    uint64_t worst_read_us = 0;
    uint64_t worst_render_us = 0;
    int worst_read_id = -1;
    int worst_render_id = -1;

    printf("TEST: the longest step of either queue, over every dat1 effect (thread CPU time)\n");
    if( !lane_open(&lane, root, cpu_now) || !lane_open(&ref, root, cpu_now) )
        return;
    bank = reference_bank(&ref);
    /* The bank's one-time decode is a read of its own; take it first. */
    load(&lane, 65000);
    printf("   the bank's one-time decode (sounds.dat): longest asset-queue step %.3f ms\n",
        (double)lane.parallel.longest_step_us / 1000.0);
    for( int id = 0; id < bank->count; id++ )
    {
        int frames = 0;

        if( !bank->effects[id] )
            continue;
        lane.parallel.longest_step_us = 0;
        lane.background.longest_step_us = 0;
        ToriRS_TaskQueue_Add(lane.parallel.queue, CreateTask_SoundLoad(lane.provider, id));
        while( (lane.parallel.queue->head || lane.background.queue->head) && frames < 1000000 )
        {
            frame(&lane, cpu_now(NULL));
            frames++;
        }
        CHECK(CacheProvider_SoundHas(lane.provider, id), "effect %d is not resident", id);
        if( lane.parallel.longest_step_us > worst_read_us )
        {
            worst_read_us = lane.parallel.longest_step_us;
            worst_read_id = id;
        }
        if( lane.background.longest_step_us > worst_render_us )
        {
            worst_render_us = lane.background.longest_step_us;
            worst_render_id = id;
        }
    }
    printf("LONGEST STEP: asset queue %.3f ms (effect %d), compute queue %.3f ms (effect %d)\n",
        (double)worst_read_us / 1000.0, worst_read_id, (double)worst_render_us / 1000.0, worst_render_id);
    CHECK(worst_read_us < LONGEST_READ_STEP_US, "an asset-queue step took %.3f ms (effect %d; bound %.3f ms)",
        (double)worst_read_us / 1000.0, worst_read_id, LONGEST_READ_STEP_US / 1000.0);
    lane_close(&ref);
    lane_close(&lane);
}

int
main(
    int argc,
    char** argv)
{
    char const* root = argc > 1 ? argv[1] : "..";

    setvbuf(stdout, NULL, _IONBF, 0);
    test_the_render_is_a_sliced_background_job(root);
    test_every_effect_is_the_one_call_render(root);
    test_the_longest_step(root);
    if( g_failures )
    {
        printf("dat1_sound_load_split_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("dat1_sound_load_split_test: OK\n");
    return 0;
}
