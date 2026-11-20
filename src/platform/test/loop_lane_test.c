/*
 * The shared frame loop through a fake lane (G4, G5).
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
 *   G5  a Windows lane's sleeps (platform/sleep_plan.h, the steps
 *       PlatformWin32Timing_SleepUntilMs carries out) on a fake clock where
 *       Sleep(n) returns a millisecond late: the 1 ms nap is a spin of
 *       Sleep(0) rechecks on the Sleep plan and one blocking wait with the
 *       high-resolution timer; and a lane that stops calling the loop for two
 *       seconds (a modal resize): the frame clock as main.c composes it
 *       (the GameShell pacer, then FrameClockStep_Pay at 5 cycles a frame).
 *
 *   make -C src test-loop-lane
 */

#include "pacer.h"
#include "perf/frame_clock.h"
#include "platform/sleep_plan.h"
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

/* ---- G5: Windows sleeps -------------------------------------------------- */

struct FakeWin
{
    uint64_t now_us;
    int sleeps;
    int yields;
    int timer_waits;
};

/* PlatformWin32Timing_SleepUntilMs, carried out on a fake Windows: Sleep(n)
 * returns 1 ms late (timeBeginPeriod(1)), Sleep(0) comes back in 20 us, the
 * high-resolution timer 30 us after its due time. */
static void
fake_sleep_until(
    struct FakeWin* win,
    uint64_t deadline_ms,
    int precise)
{
    uint64_t const began_us = win->now_us;

    for( int guard = 0; guard < 1000000; guard++ )
    {
        struct ToriRS_SleepStep const step = ToriRS_SleepPlan_Next(win->now_us, deadline_ms, began_us, precise);
        if( step.kind == TORIRS_SLEEP_STEP_DONE )
            return;
        if( step.kind == TORIRS_SLEEP_STEP_TIMER_US )
        {
            win->timer_waits++;
            win->now_us += step.amount + 30;
        }
        else if( step.kind == TORIRS_SLEEP_STEP_SLEEP_MS )
        {
            win->sleeps++;
            win->now_us += step.amount * 1000u + 1000u;
        }
        else
        {
            win->yields++;
            win->now_us += 20;
        }
    }
    assert(0);
}

static void
test_windows_sleeps(void)
{
    printf("TEST: a Windows lane's sleeps on a fake clock (G5)\n");
    for( int precise = 0; precise < 2; precise++ )
    {
        struct FakeWin win;
        uint64_t deadline;

        /* The loop's nap: SleepUntil(now + TORIRS_LOOP_NAP_MS), 300 us into
         * a millisecond. */
        memset(&win, 0, sizeof(win));
        win.now_us = 5000300;
        deadline = win.now_us / 1000 + TORIRS_LOOP_NAP_MS;
        fake_sleep_until(&win, deadline, precise);
        printf("   %s: a %u ms nap took %d Sleep(n), %d Sleep(0), %d timer wait(s), ended %llu us past it\n",
            precise ? "high-resolution timer" : "Sleep plan", TORIRS_LOOP_NAP_MS, win.sleeps, win.yields,
            win.timer_waits, (unsigned long long)(win.now_us - deadline * 1000));
        /* Before: the Sleep plan's nap was all Sleep(0) rechecks, a spin --
         * 94-100% of a core on Windows 11 with the timer off. Now one
         * Sleep(1), which may end a millisecond late, as a nap is allowed. */
        if( !precise )
        {
            CHECK(win.yields == 0 && win.sleeps == 1, "the Sleep plan's nap spun: %d yields, %d sleeps",
                win.yields, win.sleeps);
            CHECK(win.now_us >= deadline * 1000, "the Sleep plan's nap woke before its deadline");
            CHECK(win.now_us - deadline * 1000 <= 1500, "the Sleep plan's nap overslept by %llu us",
                (unsigned long long)(win.now_us - deadline * 1000));
        }
        else
            CHECK(win.yields == 0 && win.sleeps == 0 && win.timer_waits == 1,
                "the timer's nap spun: %d yields, %d sleeps, %d waits", win.yields, win.sleeps,
                win.timer_waits);

        /* A 50 fps frame deadline, 2 ms of work in. */
        memset(&win, 0, sizeof(win));
        win.now_us = 7002000;
        deadline = 7020;
        fake_sleep_until(&win, deadline, precise);
        printf("   %s: an 18 ms frame wait took %d Sleep(n), %d Sleep(0), %d timer wait(s), ended %llu us "
               "past it\n",
            precise ? "high-resolution timer" : "Sleep plan", win.sleeps, win.yields, win.timer_waits,
            (unsigned long long)(win.now_us - deadline * 1000));
        CHECK(win.now_us >= deadline * 1000, "woke before the deadline");
        CHECK(win.now_us - deadline * 1000 < 1000, "overshot the frame deadline by a millisecond or more");
        if( precise )
            CHECK(win.yields == 0, "the timer's frame wait spun %d times", win.yields);
    }
}

/* ---- G5: a lane that stops calling the loop for two seconds -------------- */

static void
test_a_two_second_stall(void)
{
    struct ToriRS_Pacer pacer;
    struct FrameClockStep step;
    uint64_t now = 100000;
    int cycles_before = 0;
    int cycles_after = 0;
    int max_after = 0;
    int frames_owed = 0;
    int spins = 0;

    printf("TEST: a lane that stops calling the loop for 2 s, as a modal resize does (G5)\n");
    /* main.c's native default: the GameShell pacer, 20 ms, mindel 1; App_RunOnce
     * pays FrameClockStep_Pay(logic_now, 20 ms, 5). */
    ToriRS_Pacer_Init(&pacer, TORIRS_PACER_GAMESHELL, 20, 1);
    memset(&step, 0, sizeof(step));
    for( int f = 0; f < 200; f++ )
    {
        uint64_t frame_start;
        uint64_t logic_now;
        uint64_t wait_until;
        int ticks;

        if( f == 100 )
            now += 2000; /* the modal loop: no frame for two seconds */
        frame_start = now;
        logic_now = ToriRS_Pacer_BeginFrame(&pacer, now);
        ticks = FrameClockStep_Pay(&step, logic_now, 20, 5);
        now += 2; /* the frame's work */
        wait_until = ToriRS_Pacer_WaitDeadline(&pacer, frame_start, now);
        if( wait_until <= now )
            spins++;
        if( f < 100 )
            cycles_before += ticks;
        else
        {
            cycles_after += ticks;
            if( ticks > max_after )
                max_after = ticks;
            if( ticks > 1 )
                frames_owed++;
        }
        now = wait_until > now ? wait_until : now;
    }
    printf("   100 frames before: %d cycles; 100 frames after the stall: %d cycles, at most %d in a frame, "
           "%d frame(s) above one; %d frame(s) that did not wait\n",
        cycles_before, cycles_after, max_after, frames_owed, spins);
    /* The design (frame_clock.c): the first frame after a stall pays at most
     * APP_MAX_CATCHUP_TICKS (5) and the rest is dropped -- no lurch. Before
     * the pacer re-seeded on a stall, its ten-iteration estimator paid the
     * stall again over the next ten frames: 140 cycles, ten frames above one. */
    CHECK(max_after <= 5, "a frame ran %d cycles after the stall", max_after);
    CHECK(frames_owed <= 1, "%d frames after the stall ran more than one cycle: the stall was replayed",
        frames_owed);
    CHECK(cycles_after <= 100 + 4, "the stall was replayed: %d cycles in the 100 frames after it",
        cycles_after);
    CHECK(spins == 0, "%d frames did not wait at all", spins);
}

int
main(void)
{
    test_no_surface();
    test_windows_sleeps();
    test_a_two_second_stall();
    if( g_failures )
    {
        printf("loop_lane_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("loop_lane_test: OK\n");
    return 0;
}
