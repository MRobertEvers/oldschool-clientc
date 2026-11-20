#ifndef SRC_PLATFORM_SLEEP_PLAN_H
#define SRC_PLATFORM_SLEEP_PLAN_H

/*
 * How the Windows lanes sleep until a deadline (PlatformWin32Timing_
 * SleepUntilMs), as one step at a time: portable, so the policy is tested on
 * any host through a fake lane (platform/test/loop_lane_test.c) and the
 * Windows file only carries the steps out.
 *
 * Two clocks of sleep exist there:
 *
 *   - Sleep(ms), on every Windows: with timeBeginPeriod(1) it commonly
 *     returns about 1 ms LATE, so the last millisecond before a deadline is
 *     left to Sleep(0) rechecks -- or a 50 fps cap runs at about 46. That
 *     recheck is a spin: a wait that is ALL last millisecond, as the loop's
 *     1 ms nap (TORIRS_LOOP_NAP_MS, main_wait_napping) is, never blocks the
 *     thread at all, where macOS (SDL_Delay) and Android (nanosleep) sleep.
 *   - a high-resolution waitable timer (CreateWaitableTimerExW with
 *     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803 and later):
 *     waits to the deadline to well under a millisecond, so the whole wait,
 *     its last millisecond included, blocks.
 *
 * With the timer the plan is one timer wait to the deadline (and again if it
 * returns early); without it, the Sleep plan above. XP (the win32 lane) and
 * Windows 10 before 1803 keep the Sleep plan.
 *
 * Except for a wait that is a millisecond or less from the moment it was asked
 * for -- the loop's nap is exactly that -- which the Sleep plan sleeps with
 * Sleep(1) instead of spinning on Sleep(0) for all of it. Measured on Windows
 * 11 with the timer switched off (TORIRS_WIN32_PRECISE_TIMER=0): a boot whose
 * reads were deferred napped at 94-100% of a core, against 2-6% on the timer.
 * The nap may end up to about a millisecond late, which it allows ("never a
 * frame late on an answer by more than a nap"); a longer wait keeps the
 * Sleep(0) rechecks for its last millisecond, so a frame deadline is not
 * overshot.
 */

#include <stdint.h>

enum ToriRS_SleepStepKind
{
    /** The deadline has passed: return. */
    TORIRS_SLEEP_STEP_DONE = 0,
    /** Wait `amount` microseconds on the high-resolution timer. */
    TORIRS_SLEEP_STEP_TIMER_US,
    /** Sleep(`amount`) milliseconds (1 or more). */
    TORIRS_SLEEP_STEP_SLEEP_MS,
    /** Sleep(0): give up the slice and recheck -- the spin. */
    TORIRS_SLEEP_STEP_YIELD,
};

struct ToriRS_SleepStep
{
    enum ToriRS_SleepStepKind kind;
    uint64_t amount;
};

/*
 * The next step toward `deadline_ms`, at `now_us` on the same clock (the
 * lane's monotonic microseconds). `began_us`: when the wait was asked for, on
 * that clock. `precise_timer`: the high-resolution waitable timer exists.
 */
static inline struct ToriRS_SleepStep
ToriRS_SleepPlan_Next(
    uint64_t now_us,
    uint64_t deadline_ms,
    uint64_t began_us,
    int precise_timer)
{
    struct ToriRS_SleepStep step = { TORIRS_SLEEP_STEP_DONE, 0 };
    uint64_t const deadline_us = deadline_ms * 1000u;
    uint64_t const now_ms = now_us / 1000u;
    uint64_t remaining_ms;

    if( now_us >= deadline_us )
        return step;
    if( precise_timer )
    {
        step.kind = TORIRS_SLEEP_STEP_TIMER_US;
        step.amount = deadline_us - now_us;
        return step;
    }
    /* The Sleep plan, on the millisecond clock it has always read. */
    if( now_ms >= deadline_ms )
        return step;
    remaining_ms = deadline_ms - now_ms;
    if( remaining_ms > 1 )
    {
        step.kind = TORIRS_SLEEP_STEP_SLEEP_MS;
        step.amount = remaining_ms - 1;
        return step;
    }
    /* A wait that was never longer than this millisecond: the nap. */
    if( began_us >= deadline_us || deadline_us - began_us <= 1000u )
    {
        step.kind = TORIRS_SLEEP_STEP_SLEEP_MS;
        step.amount = 1;
        return step;
    }
    step.kind = TORIRS_SLEEP_STEP_YIELD;
    return step;
}

#endif
