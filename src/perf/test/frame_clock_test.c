/*
 * The frame loop's four counters: the fixed-step accumulator, the serials,
 * the fps window and the idle timer.
 *
 * The accumulator is wrong in ways nothing reports: a short frame counted as
 * a whole cycle runs the world faster than real time, a stall paid in full
 * lurches, and a refund that never lands loses simulation time -- on a
 * frame-locked run the refund is a debt the next locked frame must pay once,
 * and only once. The serials only count up, each at its own writer. The fps
 * window must close on the first frame a second after it opened, the drawn
 * count must count every frame, and the idle timer must say once per stretch
 * of no input.
 *
 * Run: make -C src test-frame-clock
 */

#include "perf/frame_clock.h"

#include <stdio.h>

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

#define TICK 20
#define MAX_TICKS 5

static void
test_first_frame_pays_nothing(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 1000, TICK, MAX_TICKS) == 0, "first frame pays 0");
    TEST_ASSERT(FrameClockStep_FrameMs(&clock.step) == 0, "first frame is 0 ms long");
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 1020, TICK, MAX_TICKS) == 1, "20 ms later pays 1");
    TEST_ASSERT(FrameClockStep_FrameMs(&clock.step) == 20, "and is 20 ms long");
}

static void
test_short_frames_carry(void)
{
    struct FrameClock clock;
    int paid = 0;
    uint64_t now = 5000;

    FrameClock_Reset(&clock);
    (void)FrameClockStep_Pay(&clock.step, now, TICK, MAX_TICKS);
    /* 10 ms frames: one cycle every second frame, never one per frame. */
    for( int i = 0; i < 10; i++ )
    {
        now += 10;
        paid += FrameClockStep_Pay(&clock.step, now, TICK, MAX_TICKS);
    }
    TEST_ASSERT(paid == 5, "100 ms of 10 ms frames pays 5 cycles");
}

static void
test_stall_is_clamped_and_dropped(void)
{
    struct FrameClock clock;
    int ticks;

    FrameClock_Reset(&clock);
    (void)FrameClockStep_Pay(&clock.step, 100, TICK, MAX_TICKS);
    (void)FrameClockStep_Pay(&clock.step, 115, TICK, MAX_TICKS); /* 15 ms carried */
    ticks = FrameClockStep_Pay(&clock.step, 115 + 2000, TICK, MAX_TICKS);
    TEST_ASSERT(ticks == MAX_TICKS, "a stall pays the bound");
    TEST_ASSERT(FrameClockStep_FrameMs(&clock.step) == (uint64_t)MAX_TICKS * TICK, "clamped length");
    /* The carry was dropped with the stall: 15 ms more pays nothing. */
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 115 + 2015, TICK, MAX_TICKS) == 0, "carry dropped");
}

static void
test_clock_going_back_pays_nothing(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    (void)FrameClockStep_Pay(&clock.step, 500, TICK, MAX_TICKS);
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 400, TICK, MAX_TICKS) == 0, "time going back pays 0");
    TEST_ASSERT(FrameClockStep_FrameMs(&clock.step) == 0, "and is 0 ms long");
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 420, TICK, MAX_TICKS) == 1, "measured from 400 after");
}

static void
test_skip_does_not_owe(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    (void)FrameClockStep_Pay(&clock.step, 100, TICK, MAX_TICKS);
    FrameClockStep_Skip(&clock.step, 900);
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 920, TICK, MAX_TICKS) == 1, "a skipped stretch is not owed");
}

static void
test_locked_and_refund(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    (void)FrameClockStep_Pay(&clock.step, 100, TICK, MAX_TICKS);
    (void)FrameClockStep_Pay(&clock.step, 115, TICK, MAX_TICKS); /* 15 ms carried */
    TEST_ASSERT(FrameClockStep_PayLocked(&clock.step, 3, TICK) == 3, "locked frame pays k");
    TEST_ASSERT(FrameClockStep_FrameMs(&clock.step) == 60, "locked frame is k cycles long");
    TEST_ASSERT(clock.step.accum_ms == 0.0, "locked frame empties the accumulator");

    FrameClock_Reset(&clock);
    (void)FrameClockStep_Pay(&clock.step, 100, TICK, MAX_TICKS);
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 160, TICK, MAX_TICKS) == 3, "60 ms pays 3");
    FrameClockStep_Refund(&clock.step, 2, TICK);
    TEST_ASSERT(FrameClockStep_Pay(&clock.step, 160, TICK, MAX_TICKS) == 2, "refunded cycles are paid next");
}

static void
test_locked_owed_cycles(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    TEST_ASSERT(FrameClockStep_CyclesOwed(&clock.step) == 0, "nothing owed after a reset");
    TEST_ASSERT(FrameClockStep_PayLocked(&clock.step, 2, TICK) == 2, "k = 2 pays 2");
    /* The fence stopped the frame after one of its two cycles. */
    FrameClockStep_Owe(&clock.step, 1);
    TEST_ASSERT(FrameClockStep_CyclesOwed(&clock.step) == 1, "one owed");
    TEST_ASSERT(FrameClockStep_PayLocked(&clock.step, 2, TICK) == 3, "next locked frame pays k + owed");
    TEST_ASSERT(FrameClockStep_FrameMs(&clock.step) == 60, "and is that many cycles long");
    TEST_ASSERT(FrameClockStep_CyclesOwed(&clock.step) == 0, "the debt is cleared once paid");
    TEST_ASSERT(FrameClockStep_PayLocked(&clock.step, 2, TICK) == 2, "and paid only once");

    /* A debt replaces one not yet paid rather than adding to it. */
    FrameClockStep_Owe(&clock.step, 1);
    FrameClockStep_Owe(&clock.step, 2);
    TEST_ASSERT(FrameClockStep_PayLocked(&clock.step, 1, TICK) == 3, "the last debt is the one paid");
}

static void
test_serials(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    TEST_ASSERT(FrameClockSerial_LogicCycle(&clock.serial) == 0, "logic cycle 0 at reset");
    TEST_ASSERT(FrameClockSerial_RunOnce(&clock.serial) == 0, "frame serial 0 at reset");
    TEST_ASSERT(FrameClockSerial_LastFrameMs(&clock.serial) == 0, "no last frame at reset");
    for( int i = 0; i < 5; i++ )
        FrameClockSerial_NoteLogicCycle(&clock.serial);
    FrameClockSerial_NoteRunOnce(&clock.serial);
    FrameClockSerial_NoteRunOnce(&clock.serial);
    TEST_ASSERT(FrameClockSerial_LogicCycle(&clock.serial) == 5, "five cycles noted");
    TEST_ASSERT(FrameClockSerial_RunOnce(&clock.serial) == 2, "two frames noted");
    FrameClockSerial_NoteFrameAt(&clock.serial, 1234);
    TEST_ASSERT(FrameClockSerial_LastFrameMs(&clock.serial) == 1234, "last frame time kept");
    FrameClockSerial_NoteFrameAt(&clock.serial, 900);
    TEST_ASSERT(FrameClockSerial_LastFrameMs(&clock.serial) == 900, "set, not maxed");
    TEST_ASSERT(FrameClockSerial_LogicCycle(&clock.serial) == 5, "the frame time leaves the cycle");
}

static void
test_fps_window(void)
{
    struct FrameClock clock;

    FrameClock_Reset(&clock);
    TEST_ASSERT(FrameClockFps_Fps(&clock.fps) == 0, "0 before the first window closes");
    TEST_ASSERT(FrameClockFps_Drawn(&clock.fps) == 0, "none drawn at reset");
    /* The window opens at 0: the first frame at 1000 or later closes it. */
    FrameClockFps_NoteDrawn(&clock.fps, 1100);
    TEST_ASSERT(FrameClockFps_Fps(&clock.fps) == 1, "first frame past a second closes the window");
    for( uint64_t t = 1200; t < 2100; t += 100 )
        FrameClockFps_NoteDrawn(&clock.fps, t);
    TEST_ASSERT(FrameClockFps_Fps(&clock.fps) == 1, "window still open at 2000");
    FrameClockFps_NoteDrawn(&clock.fps, 2100);
    TEST_ASSERT(FrameClockFps_Fps(&clock.fps) == 10, "ten frames in the second window");
    TEST_ASSERT(FrameClockFps_Drawn(&clock.fps) == 11, "every drawn frame counted since reset");
}

static void
test_idle_timer_says_once(void)
{
    struct FrameClock clock;
    int said = 0;

    FrameClock_Reset(&clock);
    for( int i = 0; i < FRAME_CLOCK_IDLE_TIMER_FRAMES; i++ )
        said += FrameClockIdle_Note(&clock.idle, 0);
    TEST_ASSERT(said == 0, "not due at the threshold itself");
    TEST_ASSERT(FrameClockIdle_Note(&clock.idle, 0) == 1, "due one frame past it");
    for( int i = 0; i < 100; i++ )
        said += FrameClockIdle_Note(&clock.idle, 0);
    TEST_ASSERT(said == 0, "said once");
    TEST_ASSERT(FrameClockIdle_Note(&clock.idle, 1) == 0, "input resets");
    for( int i = 0; i <= FRAME_CLOCK_IDLE_TIMER_FRAMES; i++ )
        said += FrameClockIdle_Note(&clock.idle, 0);
    TEST_ASSERT(said == 1, "due again after input");
}

int
main(void)
{
    test_first_frame_pays_nothing();
    test_short_frames_carry();
    test_stall_is_clamped_and_dropped();
    test_clock_going_back_pays_nothing();
    test_skip_does_not_owe();
    test_locked_and_refund();
    test_locked_owed_cycles();
    test_serials();
    test_fps_window();
    test_idle_timer_says_once();
    if( g_failures )
    {
        printf("frame_clock_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("frame_clock_test: ok\n");
    return 0;
}
