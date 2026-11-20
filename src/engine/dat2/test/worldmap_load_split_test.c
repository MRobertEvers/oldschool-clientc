/*
 * The world map's loader reads on the asset queue and decodes on the compute
 * queue.
 *
 * task_dat2_worldmap_load.c: the map editor's Square map (and the game's
 * world map, through the assets table) loads table 19 with ONE task on the
 * provider's asset queue -- the App's parallel queue, which is drained every
 * frame and pays its processor time out of that frame. Decoding every map
 * area, its icons and its overview PNG is 8.8-8.9 ms of processor time in one
 * step against the osrs242 cache, which the frame contract's compute check
 * names ("parallel task Dat2WorldMapLoad computed 8.79 ms in one step") and
 * which aborted the map editor under TORIRS_QUEUE_CONTRACT_STRICT=1 the moment
 * its Square map panel opened. Against the real cache this checks:
 *
 *   - the decode is a task on the compute queue, never on the asset queue;
 *   - it stops when its slice is spent (it runs across several slices);
 *   - THE LONGEST STEP: with a real clock and every background slice already
 *     over when it is given, each background step is one bounded unit of
 *     work, and the longest read step on the asset queue stays under the
 *     compute check's threshold (4 ms of thread processor time);
 *   - the provider holds the world map afterwards, every area of it, with the
 *     overview textures attached;
 *   - nothing is left on either queue.
 *
 *   make -C src test-worldmap-load
 */

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_types.h"
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

/* This thread's processor time in microseconds: the runners' own step
 * measurement (longest_step_us) is then the work a step did, not the time a
 * loaded machine took to give it. */
static uint64_t
cpu_now(void* data)
{
    struct timespec ts;

    (void)data;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* The compute check's threshold on the parallel queue (task_runner.h,
 * TORIRS_QUEUE_CONTRACT_COMPUTE_MS default), and the bound a step of the
 * background decode is held to. A step is one area or one overview PNG (two
 * when its turn's grace lets it past the first slice check); every area and
 * every PNG but one is under 0.3 ms, and that one -- area 0's overview, 389 KB,
 * the whole surface -- is a single PngDecode_Rgb of 4.4-4.5 ms that no step
 * boundary can cut. That bound is an optimised build's (OPT=1; an OPT=0 build
 * decodes the same PNG in about 10 ms), so an unoptimised build reports the
 * longest background step and does not hold it to one. */
#define LONGEST_READ_STEP_US 4000
#define LONGEST_DECODE_STEP_US 6000

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
    TaskRunner_SetClock(runner, cpu_now, NULL);
    /* Measures every step (longest_step_us) without naming any. */
    runner->slow_task_us = UINT64_MAX / 2;
}

static void
runner_free(struct TaskRunner* runner)
{
    ToriRS_TaskQueue_Free(runner->queue);
    ToriRS_IOBatch_Free(runner->io);
}

int
main(
    int argc,
    char** argv)
{
    char const* root = argc > 1 ? argv[1] : "..";
    char path[1024];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct PlatformX_IO* px;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct TaskRunner parallel;
    struct TaskRunner background;
    struct ToriRS_WorldMapAreas* areas;
    int frames = 0;
    int steps = 0;
    int decode_on_asset_queue = 0;
    int decode_seen_frames = 0;
    int textured = 0;

    printf("TEST: cache.osrs242 world map: reads on the asset queue, the decode on the compute queue\n");
    snprintf(path, sizeof(path), "%s/%s", root, "cache.osrs242");
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP %s: not present\n", path);
        return 0;
    }
    if( !RSCache_ProfileByName("osrs242", &profile) )
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

    /* Where the editor's Square map puts it (app_map_editor.c), and where the
     * assets table's loader puts it (app_assets.c): the asset queue. */
    ToriRS_TaskQueue_Add(parallel.queue, CreateTask_WorldMapLoad(provider));

    /* Frames: the asset queue drained, then the compute queue given a slice
     * that is already over (guarantee 1: every ready task still gets one
     * step, so each background step is one unit of its work). */
    while( (parallel.queue->head || background.queue->head) && frames < 100000 )
    {
        (void)TaskRunner_PumpToStandstill(&parallel, 1 << 20, &steps);
        if( queue_has(parallel.queue, "Dat2WorldMapDecode") )
            decode_on_asset_queue = 1;
        if( queue_has(background.queue, "Dat2WorldMapDecode") )
            decode_seen_frames++;
        (void)TaskRunner_RunUntil(&background, cpu_now(NULL));
        frames++;
    }

    areas = CacheProvider_WorldMapGet(provider);
    for( int i = 0; areas && i < areas->count; i++ )
        if( areas->areas[i].overview_pixels )
            textured++;
    printf("   %d frame(s); the decode on the compute queue across %d; %d area(s), %d with an overview\n",
        frames, decode_seen_frames, areas ? areas->count : 0, textured);
    printf("   longest read step on the parallel queue %.3f ms (%s); longest background step %.3f ms (%s)\n",
        (double)parallel.longest_step_us / 1000.0, parallel.longest_step_name,
        (double)background.longest_step_us / 1000.0,
        background.longest_step_name[0] ? background.longest_step_name : "-");

    CHECK(!parallel.queue->head && !background.queue->head, "work left after %d frames", frames);
    CHECK(!decode_on_asset_queue, "the decode ran on the asset queue");
    CHECK(decode_seen_frames > 1,
        "the decode was on the compute queue for %d frame(s), not sliced across several",
        decode_seen_frames);
    CHECK(parallel.longest_step_us < LONGEST_READ_STEP_US,
        "a step on the asset queue computed %.3f ms (the compute check's threshold is %.3f ms)",
        (double)parallel.longest_step_us / 1000.0, LONGEST_READ_STEP_US / 1000.0);
#if defined(__OPTIMIZE__)
    CHECK(background.longest_step_us < LONGEST_DECODE_STEP_US,
        "a background step took %.3f ms (bound %.3f ms)", (double)background.longest_step_us / 1000.0,
        LONGEST_DECODE_STEP_US / 1000.0);
#endif
    CHECK(areas && areas->count > 1, "the provider holds no world map (%d areas)", areas ? areas->count : 0);
    CHECK(textured > 0, "no area has its overview texture");

    dat2_buildcache_free(bc);
    runner_free(&parallel);
    runner_free(&background);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    if( g_failures )
    {
        printf("worldmap_load_split_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("worldmap_load_split_test: OK\n");
    return 0;
}
