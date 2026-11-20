/*
 * The shared frame loop through a fake lane (G4).
 *
 * Every lane -- SDL on macOS and Linux, the Android JNI thread, Win32 --
 * drives the same frame_loop_step (main.c); what differs is the lane under it:
 * whether it has a surface, how its clock reads, how it sleeps, and when it
 * stops calling the loop. Android and Windows cannot be run here, so the parts
 * of the loop that answer to those differences are driven by a fake lane on
 * this host, with the same decisions main.c and platform_win32_timing.c make:
 *
 *   G4  a lane with NO SURFACE for N frames, then one again
 *       (platform/surface_plan.h, against a real ToriDraw scene's event
 *       queue): nothing draws without a surface; the queue is spent every
 *       such frame on every lane, so it stays bounded by one frame's events
 *       where the rule before spent it only on Soft3D and a retained (GLES)
 *       lane filled it to its cap and lost the rest; and the retained
 *       renderer is re-baked once, before the first draw back.
 *
 *   make -C src test-loop-lane
 */

#include "platform/surface_plan.h"
#include "toridraw_alloc.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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

/* ---- G4: no surface ------------------------------------------------------ */

/* What a frame's world work emits into the scene's queue: `n` sounds added
 * (one SOUND_LOAD event each), as a busy scene's loads would. */
static void
emit_frame_events(
    struct ToriDraw_Scene* scene,
    int* next_id,
    int n)
{
    for( int i = 0; i < n; i++ )
    {
        int16_t* samples = TORIDRAW_MALLOC(4 * sizeof(int16_t));
        assert(samples);
        memset(samples, 0, 4 * sizeof(int16_t));
        ToriDraw_SceneSoundAdd(scene, (*next_id)++, ToriDraw_SoundNew(samples, 4, 22050, -1, -1, 0, 0));
    }
}

/* The rule before this change (main.c at 92e82e471), for the record: a frame
 * with no surface spent the queue only on a software lane. */
static struct ToriRS_SurfaceFrame
legacy_surface_frame(
    int can_present,
    int retained,
    int redraw)
{
    struct ToriRS_SurfaceFrame frame = { 0, 0, 0 };
    if( !can_present )
        redraw = 0;
    frame.draw = redraw;
    frame.spend = !can_present && !retained && !redraw;
    return frame;
}

struct SurfaceRun
{
    int draws;
    int draws_without_surface;
    int rebakes;
    int rebake_frame;
    int first_draw_back;
    int queue_high;
    int emitted;
    int queued_at_return;
};

/* A lane with no surface on frames [off_from, off_to), `per_frame` events a
 * frame, `frames` frames. The draw, when it happens, drains the queue as a
 * renderer's frame does (ToriDraw_SceneFrameEnd after it). */
static struct SurfaceRun
run_surface_lane(
    int legacy,
    int retained,
    int frames,
    int off_from,
    int off_to,
    int per_frame)
{
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriRS_SurfacePlan plan;
    struct SurfaceRun run;
    int next_id = 1;

    assert(scene);
    memset(&run, 0, sizeof(run));
    run.rebake_frame = -1;
    run.first_draw_back = -1;
    ToriRS_SurfacePlan_Init(&plan);
    for( int f = 0; f < frames; f++ )
    {
        int const can_present = !(f >= off_from && f < off_to);
        struct ToriRS_SurfaceFrame frame;
        struct ToriDraw_EventQueue const* events;

        emit_frame_events(scene, &next_id, per_frame);
        run.emitted += per_frame;
        events = ToriDraw_SceneEvents(scene);
        if( events->count > run.queue_high )
            run.queue_high = events->count;
        if( f == off_to )
            run.queued_at_return = events->count;
        frame = legacy ? legacy_surface_frame(can_present, retained, 1)
                       : ToriRS_SurfacePlan_Frame(&plan, can_present, retained, 1);
        if( frame.rebake )
        {
            /* renderer_rebake: what the queue held is spent, every load is
             * replayed into the restarted renderer. */
            run.rebakes++;
            run.rebake_frame = f;
            ToriDraw_SceneFrameEnd(scene);
            ToriDraw_SceneReemitRendererLoads(scene);
        }
        if( frame.draw )
        {
            run.draws++;
            if( !can_present )
                run.draws_without_surface++;
            if( f >= off_to && run.first_draw_back < 0 )
                run.first_draw_back = f;
            ToriDraw_SceneFrameEnd(scene);
        }
        else if( frame.spend )
            ToriDraw_SceneFrameEnd(scene);
        /* Neither: a retained lane before this change kept the queue. */
    }
    ToriDraw_SceneFree(scene);
    return run;
}

static void
test_no_surface(void)
{
    /* 100 frames with no surface at 1000 events a frame: 100,000 events,
     * more than the queue holds (65,536). */
    int const frames = 130;
    int const off_from = 10;
    int const off_to = 110;
    int const per = 1000;
    struct SurfaceRun run;

    printf("TEST: a lane with no surface for %d frames, then a surface (G4)\n", off_to - off_from);

    /* Before: a retained lane spent nothing while it had no surface. */
    run = run_surface_lane(1, 1, frames, off_from, off_to, per);
    printf("   before, retained lane: queue high-water %d (cap %d), %d draws, %d draws without a surface\n",
        run.queue_high, TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE, run.draws, run.draws_without_surface);
    CHECK(run.queue_high == TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE,
        "the old rule's retained lane did not pile up to the cap (%d)", run.queue_high);

    /* After, retained: bounded by one frame's events; nothing drawn without a
     * surface; one re-bake, on the frame the surface returns, before its draw. */
    run = run_surface_lane(0, 1, frames, off_from, off_to, per);
    printf("   after, retained lane: queue high-water %d, %d draws, %d without a surface, %d re-bake(s) at "
           "frame %d, first draw back at frame %d\n",
        run.queue_high, run.draws, run.draws_without_surface, run.rebakes, run.rebake_frame,
        run.first_draw_back);
    CHECK(run.queue_high <= per, "the queue was not spent while there was no surface (high-water %d)",
        run.queue_high);
    CHECK(run.draws_without_surface == 0, "%d frames drew with no surface", run.draws_without_surface);
    CHECK(run.draws == frames - (off_to - off_from), "drew %d frames", run.draws);
    CHECK(run.rebakes == 1, "re-baked %d times", run.rebakes);
    CHECK(run.rebake_frame == off_to, "re-baked at frame %d, not on the first frame back (%d)",
        run.rebake_frame, off_to);
    CHECK(run.first_draw_back == off_to, "first draw back at frame %d", run.first_draw_back);

    /* After, Soft3D: bounded too, and never re-baked (it reads the scene). */
    run = run_surface_lane(0, 0, frames, off_from, off_to, per);
    printf("   after, software lane: queue high-water %d, %d re-bake(s)\n", run.queue_high, run.rebakes);
    CHECK(run.queue_high <= per, "software lane high-water %d", run.queue_high);
    CHECK(run.rebakes == 0, "a software lane re-baked");
    CHECK(run.draws_without_surface == 0, "a software lane drew with no surface");
}

int
main(void)
{
    test_no_surface();
    if( g_failures )
    {
        printf("loop_lane_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("loop_lane_test: OK\n");
    return 0;
}
