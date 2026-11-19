/*
 * How a session the player or the server chose to end gets ended.
 *
 * The rules a logout runs on, with no socket and no App: the click only
 * asks; the tick that drains the request arms a wait at the reference's 250
 * cycles; nothing counts that wait down, so a logout nobody answers leaves
 * the session up for good; a second click while waiting changes nothing; with
 * no session to answer, the request is the ending; and a closed socket while
 * a logout is waiting -- or still undrained -- is the answer, not a lost
 * connection. And the world hop: parked by the packet, taken exactly once.
 *
 * Run: make -C src test-session-end
 */

#include "net/session_end.h"

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

static struct SessionEnd
fresh(void)
{
    struct SessionEnd end;

    memset(&end, 0x5a, sizeof(end));
    SessionEnd_Reset(&end);
    return end;
}

static void
test_a_reset_has_nothing_to_do(void)
{
    struct SessionEnd end = fresh();
    int world = -7;

    printf("TEST: a reset has no request, no wait and no hop\n");

    TEST_ASSERT(!SessionEnd_LogoutPending(&end), "no logout pending");
    TEST_ASSERT(SessionEnd_Step(&end, true) == SESSION_END_IDLE, "nothing to drain");
    TEST_ASSERT(SessionEnd_Step(&end, false) == SESSION_END_IDLE,
                "nothing to drain with no session either");
    TEST_ASSERT(end.logout_wait_cycles == 0, "no wait armed");
    TEST_ASSERT(!SessionEnd_TakeHop(&end, &world), "no hop to take");
    TEST_ASSERT(world == -7, "the out world was left untouched");
}

static void
test_the_click_only_asks(void)
{
    struct SessionEnd end = fresh();

    printf("TEST: the click asks, and the tick arms the wait\n");

    SessionEnd_RequestLogout(&end);
    TEST_ASSERT(end.logout_requested, "the request is raised");
    TEST_ASSERT(end.logout_wait_cycles == 0, "the click itself arms nothing");
    TEST_ASSERT(SessionEnd_LogoutPending(&end), "an undrained request is a pending logout");

    TEST_ASSERT(SessionEnd_Step(&end, true) == SESSION_END_WAIT_ARMED,
                "the tick that drained the request armed the wait and ended nothing");
    TEST_ASSERT(end.logout_wait_cycles == SESSION_END_LOGOUT_WAIT_CYCLES,
                "armed at the reference's 250 cycles");
    TEST_ASSERT(end.logout_wait_cycles == 250, "the reference's number");
    TEST_ASSERT(!end.logout_requested, "the request was drained, not left to re-arm");
    TEST_ASSERT(SessionEnd_LogoutPending(&end), "the armed wait is a pending logout");
}

static void
test_a_wait_nobody_answers_never_ends(void)
{
    struct SessionEnd end = fresh();
    int ended_on_cycle = -1;
    int rearmed = 0;

    printf("TEST: a logout nothing answers leaves the session up\n");

    SessionEnd_RequestLogout(&end);
    SessionEnd_Step(&end, true);
    for( int cycle = 1; cycle <= 4 * SESSION_END_LOGOUT_WAIT_CYCLES && ended_on_cycle < 0; cycle++ )
    {
        enum SessionEndAction action = SessionEnd_Step(&end, true);

        if( action == SESSION_END_LOGOUT )
            ended_on_cycle = cycle;
        if( action == SESSION_END_WAIT_ARMED )
            rearmed++;
        /* A second click half way through: it ends nothing here either. */
        if( cycle == 2 * SESSION_END_LOGOUT_WAIT_CYCLES )
            SessionEnd_RequestLogout(&end);
    }

    TEST_ASSERT(ended_on_cycle < 0, "nothing but the server ended the session");
    TEST_ASSERT(rearmed == 0, "the second click did not re-arm the wait");
    TEST_ASSERT(end.logout_wait_cycles == SESSION_END_LOGOUT_WAIT_CYCLES,
                "the wait is still armed and was never counted down");
    TEST_ASSERT(!end.logout_requested, "the second click was drained");
    TEST_ASSERT(SessionEnd_LogoutPending(&end),
                "a socket closing now is still read as the logout");
}

static void
test_no_session_means_no_wait(void)
{
    struct SessionEnd end = fresh();

    printf("TEST: with no session to answer, the request is the ending\n");

    SessionEnd_RequestLogout(&end);
    TEST_ASSERT(SessionEnd_Step(&end, false) == SESSION_END_LOGOUT,
                "the session ends on the tick that drained the request");
    TEST_ASSERT(end.logout_wait_cycles == 0, "nothing was armed to wait for");
    TEST_ASSERT(!end.logout_requested, "the request was drained");
}

static void
test_the_ending_clears_the_wait(void)
{
    struct SessionEnd end = fresh();
    int world = -7;

    printf("TEST: the session ending clears the request, the wait and the hop\n");

    SessionEnd_RequestLogout(&end);
    SessionEnd_Step(&end, true);
    SessionEnd_RequestLogout(&end);
    SessionEnd_Hop(&end, 302);
    SessionEnd_Reset(&end);

    TEST_ASSERT(!SessionEnd_LogoutPending(&end),
                "a socket closing after the ending is a lost connection again");
    TEST_ASSERT(end.logout_wait_cycles == 0, "the wait is gone");
    TEST_ASSERT(!SessionEnd_TakeHop(&end, &world), "a hop behind the logout moves nowhere");
    TEST_ASSERT(world == -7, "the out world was left untouched");
}

static void
test_a_hop_is_taken_once(void)
{
    struct SessionEnd end = fresh();
    int world = -1;

    printf("TEST: a LOGOUT_TRANSFER is parked and taken exactly once\n");

    SessionEnd_Hop(&end, 301);
    SessionEnd_Hop(&end, 330);
    TEST_ASSERT(!SessionEnd_LogoutPending(&end), "a hop is not a logout");
    TEST_ASSERT(SessionEnd_TakeHop(&end, &world), "the hop is there to take");
    TEST_ASSERT(world == 330, "the later transfer named the world");
    world = -1;
    TEST_ASSERT(!SessionEnd_TakeHop(&end, &world), "taken once, not every frame");
    TEST_ASSERT(world == -1, "the out world was left untouched");
}

int
main(void)
{
    test_a_reset_has_nothing_to_do();
    test_the_click_only_asks();
    test_a_wait_nobody_answers_never_ends();
    test_no_session_means_no_wait();
    test_the_ending_clears_the_wait();
    test_a_hop_is_taken_once();

    if( g_failures )
    {
        printf("session_end: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("session_end: all checks passed\n");
    return 0;
}
