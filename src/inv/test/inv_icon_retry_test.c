/*
 * When the next inventory obj-icon reconcile pass may run
 * (inv/inv_icon_retry.h).
 *
 * The doubling wait and its ceiling, one pass in flight at a time, a pass
 * that resolved nothing holding the next one off by the wait, a pass that
 * resolved something clearing it, and a changed inventory lifting the hold
 * while the wait it had stays.
 *
 * Run: make -C src test-inv-icon-retry
 */

#include "inv/inv_icon_retry.h"

#include <stdio.h>
#include <stdlib.h>
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
test_next_wait(void)
{
    TEST_ASSERT(InvIconRetry_NextWait(0) == 500u, "the first wait is 500 ms");
    TEST_ASSERT(InvIconRetry_NextWait(500u) == 1000u, "then it doubles");
    TEST_ASSERT(InvIconRetry_NextWait(2000u) == 4000u, "up to the ceiling");
    TEST_ASSERT(InvIconRetry_NextWait(4000u) == 4000u, "and stays there");
    TEST_ASSERT(InvIconRetry_NextWait(3000u) == 4000u, "a doubling past it is cut to it");
}

static void
test_one_in_flight(void)
{
    struct InvIconRetry r;

    memset(&r, 0, sizeof(r));
    TEST_ASSERT(InvIconRetry_Due(&r, 0), "a zeroed retry is due at once");
    InvIconRetry_Begin(&r);
    TEST_ASSERT(InvIconRetry_InFlight(&r), "a begun pass is in flight");
    TEST_ASSERT(!InvIconRetry_Due(&r, 1000000u), "and no other pass is due");
    InvIconRetry_NotePass(&r, 1, 10);
    TEST_ASSERT(InvIconRetry_InFlight(&r), "noting the pass does not end it");
    InvIconRetry_End(&r);
    TEST_ASSERT(!InvIconRetry_InFlight(&r), "ended");
    TEST_ASSERT(InvIconRetry_Due(&r, 10), "a pass that resolved something lets the next run now");
}

static void
test_backoff(void)
{
    struct InvIconRetry r;

    memset(&r, 0, sizeof(r));
    InvIconRetry_Begin(&r);
    InvIconRetry_NotePass(&r, 0, 1000);
    InvIconRetry_End(&r);
    TEST_ASSERT(r.wait_ms == 500u && r.next_pass_ms == 1500u, "a pass that resolved nothing waits 500 ms");
    TEST_ASSERT(!InvIconRetry_Due(&r, 1499), "not due before the wait is up");
    TEST_ASSERT(InvIconRetry_Due(&r, 1500), "due when it is");

    InvIconRetry_Begin(&r);
    InvIconRetry_NotePass(&r, 0, 1500);
    InvIconRetry_End(&r);
    TEST_ASSERT(r.wait_ms == 1000u && r.next_pass_ms == 2500u, "a second miss doubles the wait");

    InvIconRetry_Lift(&r);
    TEST_ASSERT(InvIconRetry_Due(&r, 1600), "a changed inventory lifts the hold");
    TEST_ASSERT(r.wait_ms == 1000u, "and keeps the wait");

    InvIconRetry_Begin(&r);
    InvIconRetry_NotePass(&r, 0, 1600);
    InvIconRetry_End(&r);
    TEST_ASSERT(r.wait_ms == 2000u && r.next_pass_ms == 3600u, "so the next miss doubles from it");

    InvIconRetry_Begin(&r);
    InvIconRetry_NotePass(&r, 3, 3600);
    InvIconRetry_End(&r);
    TEST_ASSERT(r.wait_ms == 0 && r.next_pass_ms == 0, "a pass that resolved something clears the wait");
    TEST_ASSERT(InvIconRetry_Due(&r, 3600), "and the next pass is due");
}

int
main(void)
{
    test_next_wait();
    test_one_in_flight();
    test_backoff();
    if( g_failures )
    {
        printf("inv_icon_retry: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("inv_icon_retry: all passed\n");
    return 0;
}
