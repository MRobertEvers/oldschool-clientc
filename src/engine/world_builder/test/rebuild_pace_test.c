/*
 * The paced catch-up after a background rebuild's commit, driven with a made
 * up clock and frame serial. `make -C src test-rebuild-pace`.
 */
#include "engine/world_builder/rebuild_pace.h"

#include <stdio.h>
#include <string.h>

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

enum
{
    BUDGET_US = 4000,
    WINDOW_US = 1000000,
    COMMIT_AT = 50000,
    COMMIT_FRAME = 10,
};

static void
armed(struct RebuildPace* pace)
{
    memset(pace, 0x5a, sizeof(*pace));
    RebuildPace_Reset(pace);
    RebuildPace_Configure(pace, BUDGET_US, WINDOW_US);
    RebuildPace_Arm(pace, COMMIT_AT, COMMIT_FRAME);
}

static void
test_a_reset_pace_is_not_pacing(void)
{
    struct RebuildPace pace;
    int ended = 7;

    memset(&pace, 0x5a, sizeof(pace));
    RebuildPace_Reset(&pace);
    TEST_ASSERT(!RebuildPace_Pacing(&pace), "a reset pace is not pacing");
    TEST_ASSERT(RebuildPace_Deadline(&pace, 123, 1, &ended) == 0, "no deadline when not pacing");
    TEST_ASSERT(ended == 0, "nothing ended");
    RebuildPace_NoteStop(&pace, 123);
    TEST_ASSERT(!RebuildPace_Pacing(&pace), "a stop does not start pacing");
}

static void
test_a_zero_budget_never_arms(void)
{
    struct RebuildPace pace;

    RebuildPace_Reset(&pace);
    RebuildPace_Configure(&pace, 0, WINDOW_US);
    RebuildPace_Arm(&pace, COMMIT_AT, COMMIT_FRAME);
    TEST_ASSERT(!RebuildPace_Pacing(&pace), "budget 0 turns pacing off");
}

static void
test_the_commit_frame_gets_a_short_slice_and_pops_nothing(void)
{
    struct RebuildPace pace;
    uint64_t deadline;
    int ended;

    armed(&pace);
    TEST_ASSERT(RebuildPace_Pacing(&pace), "armed");
    TEST_ASSERT(pace.stat_pop_stops == 0, "arm clears the pop stops");
    TEST_ASSERT(pace.stat_settle_stops == 0, "arm clears the settle stops");
    deadline = RebuildPace_Deadline(&pace, COMMIT_AT + 100, COMMIT_FRAME, &ended);
    TEST_ASSERT(deadline == COMMIT_AT + REBUILD_PACE_COMMIT_FRAME_US, "the commit frame's slice");
    TEST_ASSERT(ended == 0, "the window is open");
    TEST_ASSERT(RebuildPace_HoldsPop(&pace, deadline, COMMIT_AT + 100, COMMIT_FRAME) == 1, "no pop on the commit frame");
    TEST_ASSERT(pace.stat_pop_stops == 1, "the held pop is counted");
}

static void
test_a_later_frame_gets_one_budget_from_its_first_ask(void)
{
    struct RebuildPace pace;
    uint64_t deadline;
    int ended;

    armed(&pace);
    deadline = RebuildPace_Deadline(&pace, COMMIT_AT + 20000, COMMIT_FRAME + 1, &ended);
    TEST_ASSERT(deadline == COMMIT_AT + 20000 + BUDGET_US, "a budget from the first ask");
    deadline = RebuildPace_Deadline(&pace, COMMIT_AT + 23000, COMMIT_FRAME + 1, &ended);
    TEST_ASSERT(deadline == COMMIT_AT + 20000 + BUDGET_US, "a second ask keeps the frame's deadline");
}

static void
test_a_frame_always_gets_its_first_packet(void)
{
    struct RebuildPace pace;
    uint64_t const frame = COMMIT_FRAME + 1;
    uint64_t deadline;
    int ended;

    armed(&pace);
    deadline = RebuildPace_Deadline(&pace, COMMIT_AT + 20000, frame, &ended);
    TEST_ASSERT(RebuildPace_HoldsPop(&pace, deadline, deadline + 1, frame) == 0, "the first packet goes past the deadline");
    TEST_ASSERT(pace.pop_frame == frame, "the frame has popped");
    TEST_ASSERT(RebuildPace_HoldsPop(&pace, deadline, deadline - 1, frame) == 0, "inside the budget");
    TEST_ASSERT(RebuildPace_HoldsPop(&pace, deadline, deadline, frame) == 1, "the second packet past the deadline waits");
    TEST_ASSERT(pace.stat_pop_stops == 1, "one stop counted");
}

static void
test_the_window_ends_and_says_so(void)
{
    struct RebuildPace pace;
    int ended;

    armed(&pace);
    RebuildPace_NoteSettleStop(&pace);
    RebuildPace_NoteSettleStop(&pace);
    TEST_ASSERT(RebuildPace_Deadline(&pace, COMMIT_AT + WINDOW_US, COMMIT_FRAME + 30, &ended) == 0, "no deadline past the window");
    TEST_ASSERT(ended == 1, "the end is reported once");
    TEST_ASSERT(!RebuildPace_Pacing(&pace), "pacing is over");
    TEST_ASSERT(pace.stat_settle_stops == 2, "the counts outlive the window for the report");
    TEST_ASSERT(RebuildPace_Deadline(&pace, COMMIT_AT + WINDOW_US + 1, COMMIT_FRAME + 31, &ended) == 0, "still none");
    TEST_ASSERT(ended == 0, "not reported twice");
}

static void
test_a_stop_lingers_up_to_the_hard_limit(void)
{
    struct RebuildPace pace;
    uint64_t const late = COMMIT_AT + WINDOW_US - 10;
    uint64_t const near_hard = COMMIT_AT + REBUILD_PACE_HARD_LIMIT_US - 10;

    armed(&pace);
    RebuildPace_NoteStop(&pace, COMMIT_AT + 1000);
    TEST_ASSERT(pace.until_us == COMMIT_AT + WINDOW_US, "an early stop does not shorten the window");
    RebuildPace_NoteStop(&pace, late);
    TEST_ASSERT(pace.until_us == late + REBUILD_PACE_STOP_LINGER_US, "a late stop lingers past the window");
    RebuildPace_NoteStop(&pace, near_hard);
    TEST_ASSERT(pace.until_us == COMMIT_AT + REBUILD_PACE_HARD_LIMIT_US, "never past the hard limit");
}

static void
test_a_window_longer_than_the_hard_limit_is_the_limit(void)
{
    struct RebuildPace pace;

    RebuildPace_Reset(&pace);
    RebuildPace_Configure(&pace, BUDGET_US, (uint64_t)REBUILD_PACE_HARD_LIMIT_US * 2);
    RebuildPace_Arm(&pace, COMMIT_AT, COMMIT_FRAME);
    TEST_ASSERT(pace.hard_until_us == pace.until_us, "the hard limit is at least the window");
}

int
main(void)
{
    test_a_reset_pace_is_not_pacing();
    test_a_zero_budget_never_arms();
    test_the_commit_frame_gets_a_short_slice_and_pops_nothing();
    test_a_later_frame_gets_one_budget_from_its_first_ask();
    test_a_frame_always_gets_its_first_packet();
    test_the_window_ends_and_says_so();
    test_a_stop_lingers_up_to_the_hard_limit();
    test_a_window_longer_than_the_hard_limit_is_the_limit();

    if( g_failures )
    {
        printf("rebuild_pace: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rebuild_pace: all checks passed\n");
    return 0;
}
