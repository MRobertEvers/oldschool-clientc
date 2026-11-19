/*
 * The task pump's boot and busy-frame counts, and the pending flag.
 *
 * A pump counted as a frame inflates the boot's frame count; a CS2 settle
 * counted as busy reports drip-feeding that did not happen; a pending flag
 * that outlived its pass would keep the frame loop from ever sleeping.
 *
 *   make -C src test-boot-pump-stats
 */
#include "perf/boot_pump_stats.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

enum
{
    TEST_BUDGET = 32,
};

static void
test_reset(void)
{
    struct BootPumpStats stats;

    printf("TEST: reset counts nothing\n");

    memset(&stats, 0x5a, sizeof(stats));
    BootPumpStats_Reset(&stats);
    CHECK(BootPumpStats_BootFrames(&stats) == 0, "boot frames");
    CHECK(BootPumpStats_BootSteps(&stats) == 0, "boot steps");
    CHECK(BootPumpStats_BootFramesCapped(&stats) == 0, "capped");
    CHECK(BootPumpStats_BusyFrames(&stats) == 0, "busy frames");
    CHECK(BootPumpStats_BusySteps(&stats) == 0, "busy steps");
    CHECK(!BootPumpStats_Pending(&stats), "pending");
}

static void
test_boot_frames(void)
{
    struct BootPumpStats stats;

    printf("TEST: boot frames, capped ones, and pumps that are not frames\n");

    BootPumpStats_Reset(&stats);
    BootPumpStats_NoteBootSteps(&stats, 1);
    BootPumpStats_NoteBootSteps(&stats, 1);
    BootPumpStats_NoteFrame(&stats, true, true, false, 2, TEST_BUDGET);
    CHECK(BootPumpStats_BootFrames(&stats) == 1, "one frame, %d", BootPumpStats_BootFrames(&stats));
    CHECK(BootPumpStats_BootFramesCapped(&stats) == 0, "not capped");
    CHECK(BootPumpStats_BootSteps(&stats) == 2, "steps %ld", BootPumpStats_BootSteps(&stats));

    BootPumpStats_NoteFrame(&stats, true, true, false, TEST_BUDGET, TEST_BUDGET);
    CHECK(BootPumpStats_BootFrames(&stats) == 2, "two frames");
    CHECK(BootPumpStats_BootFramesCapped(&stats) == 1, "one capped");

    /* A headless wait's steps count; a pump is not a frame. */
    BootPumpStats_NoteBootSteps(&stats, 2);
    BootPumpStats_NoteFrame(&stats, false, true, false, TEST_BUDGET, TEST_BUDGET);
    CHECK(BootPumpStats_BootFrames(&stats) == 2, "a pump counted a frame");
    CHECK(BootPumpStats_BootFramesCapped(&stats) == 1, "a pump counted a cap");
    CHECK(BootPumpStats_BootSteps(&stats) == 4, "steps %ld", BootPumpStats_BootSteps(&stats));
    CHECK(BootPumpStats_BusyFrames(&stats) == 0, "boot frames are never busy");
}

static void
test_busy_frames(void)
{
    struct BootPumpStats stats;

    printf("TEST: a post-boot frame is busy only when it used the whole budget\n");

    BootPumpStats_Reset(&stats);
    BootPumpStats_NoteFrame(&stats, true, false, false, TEST_BUDGET - 1, TEST_BUDGET);
    CHECK(BootPumpStats_BusyFrames(&stats) == 0, "under budget is not busy");
    BootPumpStats_NoteFrame(&stats, true, false, true, TEST_BUDGET, TEST_BUDGET);
    CHECK(BootPumpStats_BusyFrames(&stats) == 0, "a CS2 settle is not busy");
    BootPumpStats_NoteFrame(&stats, false, false, false, TEST_BUDGET, TEST_BUDGET);
    CHECK(BootPumpStats_BusyFrames(&stats) == 0, "a pump is not busy");
    BootPumpStats_NoteFrame(&stats, true, false, false, TEST_BUDGET, TEST_BUDGET);
    BootPumpStats_NoteFrame(&stats, true, false, false, TEST_BUDGET + 3, TEST_BUDGET);
    CHECK(BootPumpStats_BusyFrames(&stats) == 2, "busy %d", BootPumpStats_BusyFrames(&stats));
    CHECK(
        BootPumpStats_BusySteps(&stats) == 2 * TEST_BUDGET + 3,
        "busy steps %ld",
        BootPumpStats_BusySteps(&stats));
    CHECK(BootPumpStats_BootFrames(&stats) == 0, "post-boot frames are not boot frames");
}

static void
test_pending(void)
{
    struct BootPumpStats stats;

    printf("TEST: pending describes the last pass only\n");

    BootPumpStats_Reset(&stats);
    BootPumpStats_PassBegin(&stats);
    BootPumpStats_SetPending(&stats);
    CHECK(BootPumpStats_Pending(&stats), "left work");
    CHECK(!BootPumpStats_Runnable(&stats), "work that waits is not runnable");
    BootPumpStats_PassBegin(&stats);
    CHECK(!BootPumpStats_Pending(&stats), "a new pass starts clear");
}

static void
test_runnable_and_moved(void)
{
    struct BootPumpStats stats;

    printf("TEST: runnable implies pending, and both clear with moved at a new pass\n");

    BootPumpStats_Reset(&stats);
    BootPumpStats_PassBegin(&stats);
    BootPumpStats_SetRunnable(&stats);
    BootPumpStats_SetMoved(&stats);
    CHECK(BootPumpStats_Pending(&stats), "runnable work is pending work");
    CHECK(BootPumpStats_Runnable(&stats), "runnable");
    CHECK(BootPumpStats_Moved(&stats), "moved");
    BootPumpStats_PassBegin(&stats);
    CHECK(!BootPumpStats_Pending(&stats), "pending clears");
    CHECK(!BootPumpStats_Runnable(&stats), "runnable clears");
    CHECK(!BootPumpStats_Moved(&stats), "moved clears");
}

int
main(void)
{
    test_reset();
    test_boot_frames();
    test_busy_frames();
    test_pending();
    test_runnable_and_moved();
    if( g_failures )
    {
        printf("boot_pump_stats: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("boot_pump_stats: all checks passed\n");
    return 0;
}
