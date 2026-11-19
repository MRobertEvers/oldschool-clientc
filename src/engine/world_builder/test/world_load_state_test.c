/*
 * Whether there is a world, has a load been tried, is one running and who
 * asked for it, driven through the events the client's load paths raise.
 * `make -C src test-world-load-state`.
 */
#include "engine/world_builder/world_load_state.h"

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

static void
fresh(struct WorldLoadState* state)
{
    memset(state, 0, sizeof(*state));
}

static void
test_a_zeroed_state_has_no_world_and_tried_nothing(void)
{
    struct WorldLoadState state;

    fresh(&state);
    TEST_ASSERT(!WorldLoadState_Active(&state), "no world");
    TEST_ASSERT(!WorldLoadState_Attempted(&state), "nothing tried");
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "nothing running");
    TEST_ASSERT(!WorldLoadState_ServerDriven(&state), "nothing owed");
    TEST_ASSERT(!WorldLoadState_ServerLoadBlanking(&state), "nothing blanking");
}

static void
test_a_local_load_blanks_and_owes_nothing(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_BeginLocal(&state);
    TEST_ASSERT(WorldLoadState_Attempted(&state), "a local load is an attempt");
    TEST_ASSERT(WorldLoadState_Inflight(&state), "and blanks the viewport");
    TEST_ASSERT(!WorldLoadState_ServerDriven(&state), "and owes the server nothing");
    TEST_ASSERT(!WorldLoadState_ServerLoadBlanking(&state), "so the frame does not wait on the server");
    TEST_ASSERT(!WorldLoadState_Active(&state), "no world until it completes");

    WorldLoadState_Land(&state);
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "landed: unblanked");
    TEST_ASSERT(!WorldLoadState_Active(&state), "landing alone is not a world");
    WorldLoadState_Complete(&state);
    TEST_ASSERT(WorldLoadState_Active(&state), "complete: there is a world");
    TEST_ASSERT(WorldLoadState_Attempted(&state), "and the attempt stays latched");
}

static void
test_a_blocking_rebuild_blanks_and_owes_the_ack(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_BeginRebuild(&state, 0);
    TEST_ASSERT(WorldLoadState_Attempted(&state), "a rebuild is an attempt");
    TEST_ASSERT(WorldLoadState_Inflight(&state), "a blocking rebuild blanks");
    TEST_ASSERT(WorldLoadState_ServerDriven(&state), "and owes the ack");
    TEST_ASSERT(WorldLoadState_ServerLoadBlanking(&state), "the frame waits on the server's scene");

    WorldLoadState_Land(&state);
    TEST_ASSERT(!WorldLoadState_ServerLoadBlanking(&state), "landed: not blanking");
    TEST_ASSERT(WorldLoadState_ServerDriven(&state), "the ack is still owed until sent");
    WorldLoadState_Complete(&state);
    WorldLoadState_Acknowledged(&state);
    TEST_ASSERT(!WorldLoadState_ServerDriven(&state), "sent: owed no more");
    TEST_ASSERT(WorldLoadState_Active(&state), "there is a world");
}

static void
test_a_background_rebuild_keeps_the_scene_up_until_it_stalls(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_BeginRebuild(&state, 1);
    TEST_ASSERT(WorldLoadState_ServerDriven(&state), "owes the ack");
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "but leaves the old scene up");
    WorldLoadState_Stall(&state);
    TEST_ASSERT(WorldLoadState_Inflight(&state), "a stall blanks it");
    TEST_ASSERT(WorldLoadState_ServerLoadBlanking(&state), "and the frame waits");
}

static void
test_an_incomplete_load_owes_nothing_and_keeps_the_old_world(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_BeginLocal(&state);
    WorldLoadState_Land(&state);
    WorldLoadState_Complete(&state);
    WorldLoadState_BeginRebuild(&state, 0);
    WorldLoadState_Land(&state);
    WorldLoadState_Fail(&state);
    TEST_ASSERT(!WorldLoadState_ServerDriven(&state), "a failed load acks nothing");
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "and is not running");
    TEST_ASSERT(WorldLoadState_Active(&state), "the world that was there stays");
}

static void
test_a_cancel_unblanks_only_an_idle_job(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_BeginRebuild(&state, 1);
    WorldLoadState_Stall(&state);
    WorldLoadState_Cancel(&state, 0);
    TEST_ASSERT(!WorldLoadState_ServerDriven(&state), "a cancel acks nothing");
    TEST_ASSERT(WorldLoadState_Inflight(&state), "a job not yet idle stays blanked");
    WorldLoadState_Cancel(&state, 1);
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "an idle job unblanks");
}

static void
test_a_repeat_owes_the_ack_without_blanking(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_BeginRepeat(&state);
    TEST_ASSERT(WorldLoadState_ServerDriven(&state), "a repeat acks like the server's rebuild");
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "and keeps the scene up");
    TEST_ASSERT(!WorldLoadState_Attempted(&state), "and is not a new attempt");
}

static void
test_the_editor_forgets_and_a_window_without_a_world_latches(void)
{
    struct WorldLoadState state;

    fresh(&state);
    WorldLoadState_Attempt(&state);
    TEST_ASSERT(WorldLoadState_Attempted(&state), "latched without a load");
    TEST_ASSERT(!WorldLoadState_Inflight(&state), "and nothing runs");
    WorldLoadState_ForgetAttempt(&state);
    TEST_ASSERT(!WorldLoadState_Attempted(&state), "forgotten");
}

int
main(void)
{
    test_a_zeroed_state_has_no_world_and_tried_nothing();
    test_a_local_load_blanks_and_owes_nothing();
    test_a_blocking_rebuild_blanks_and_owes_the_ack();
    test_a_background_rebuild_keeps_the_scene_up_until_it_stalls();
    test_an_incomplete_load_owes_nothing_and_keeps_the_old_world();
    test_a_cancel_unblanks_only_an_idle_job();
    test_a_repeat_owes_the_ack_without_blanking();
    test_the_editor_forgets_and_a_window_without_a_world_latches();
    if( g_failures )
    {
        printf("world_load_state_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("world_load_state_test: OK\n");
    return 0;
}
