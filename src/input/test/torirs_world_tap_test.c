/*
 * A finger's tap on the world held until the pick reaches its pixel
 * (input/torirs_world_tap.h).
 *
 * A mouse click, and a touch the pick already answers, run at once and hold
 * nothing. A touch the pick does not answer is held at its point, the
 * pointer stays there while it is, and it comes due on the first frame whose
 * pick answers it or on the TORIRS_WORLD_TAP_WAIT_FRAMES-th frame, with its
 * point and the frames it waited still readable. A new click replaces a held
 * tap whether the new one runs or is held itself.
 *
 * Run: make -C src test-world-tap
 */

#include "input/torirs_world_tap.h"

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
test_reset_holds_nothing(void)
{
    struct ToriRS_WorldTap tap;
    int x = 0;
    int y = 0;
    memset(&tap, 0x5a, sizeof(tap));
    ToriRS_WorldTapReset(&tap);
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "reset holds no tap");
    TEST_ASSERT(!ToriRS_WorldTapPointer(&tap, 120, 340, &x, &y), "no tap, no held pointer");
    TEST_ASSERT(x == 120 && y == 340, "with no tap the pointer is the mouse");

    struct ToriRS_WorldTap zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    TEST_ASSERT(memcmp(&zeroed, &tap, sizeof(tap)) == 0, "reset is the zeroed struct");
}

static void
test_mouse_and_answered_touch_run_now(void)
{
    struct ToriRS_WorldTap tap;
    ToriRS_WorldTapReset(&tap);

    TEST_ASSERT(
        ToriRS_WorldTapClick(&tap, 10, 20, 0, 0) == 0, "a mouse click runs now, picked or not");
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "a mouse click holds nothing");
    TEST_ASSERT(ToriRS_WorldTapClick(&tap, 10, 20, 0, 1) == 0, "an answered mouse click runs now");
    TEST_ASSERT(ToriRS_WorldTapClick(&tap, 10, 20, 1, 1) == 0, "a touch the pick answers runs now");
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "an answered touch holds nothing");
}

static void
test_unanswered_touch_is_held(void)
{
    struct ToriRS_WorldTap tap;
    int x = 0;
    int y = 0;
    ToriRS_WorldTapReset(&tap);

    TEST_ASSERT(ToriRS_WorldTapClick(&tap, 300, 200, 1, 0) == 1, "an unanswered touch waits");
    TEST_ASSERT(ToriRS_WorldTapHeld(&tap), "it is held");
    TEST_ASSERT(ToriRS_WorldTapX(&tap) == 300 && ToriRS_WorldTapY(&tap) == 200, "at its point");
    TEST_ASSERT(ToriRS_WorldTapFrames(&tap) == 0, "having waited no frame");
    TEST_ASSERT(ToriRS_WorldTapPointer(&tap, 5, 6, &x, &y), "the held pointer is reported");
    TEST_ASSERT(x == 300 && y == 200, "the pointer stays at the tap, not the mouse");
}

static void
test_comes_due_when_answered(void)
{
    struct ToriRS_WorldTap tap;
    ToriRS_WorldTapReset(&tap);
    ToriRS_WorldTapClick(&tap, 300, 200, 1, 0);

    TEST_ASSERT(ToriRS_WorldTapStep(&tap, 0) == 0, "an unanswered frame keeps it waiting");
    TEST_ASSERT(ToriRS_WorldTapHeld(&tap), "still held");
    TEST_ASSERT(ToriRS_WorldTapFrames(&tap) == 1, "one frame waited");
    TEST_ASSERT(ToriRS_WorldTapStep(&tap, 1) == 1, "the frame that answers it makes it due");
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "due releases it");
    TEST_ASSERT(ToriRS_WorldTapFrames(&tap) == 2, "the due frame is counted");
    TEST_ASSERT(
        ToriRS_WorldTapX(&tap) == 300 && ToriRS_WorldTapY(&tap) == 200,
        "its point survives release");
}

static void
test_comes_due_after_the_wait(void)
{
    struct ToriRS_WorldTap tap;
    ToriRS_WorldTapReset(&tap);
    ToriRS_WorldTapClick(&tap, 7, 8, 1, 0);

    for( int i = 1; i < TORIRS_WORLD_TAP_WAIT_FRAMES; i++ )
    {
        TEST_ASSERT(ToriRS_WorldTapStep(&tap, 0) == 0, "waits short of the limit");
        TEST_ASSERT(ToriRS_WorldTapFrames(&tap) == i, "each frame is counted");
    }
    TEST_ASSERT(ToriRS_WorldTapHeld(&tap), "still held one frame short");
    TEST_ASSERT(ToriRS_WorldTapStep(&tap, 0) == 1, "due on the limit's frame, unanswered");
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "released");
    TEST_ASSERT(
        ToriRS_WorldTapFrames(&tap) == TORIRS_WORLD_TAP_WAIT_FRAMES, "waited the whole limit");
}

static void
test_new_click_replaces_held(void)
{
    struct ToriRS_WorldTap tap;
    ToriRS_WorldTapReset(&tap);

    ToriRS_WorldTapClick(&tap, 10, 10, 1, 0);
    ToriRS_WorldTapStep(&tap, 0);
    TEST_ASSERT(
        ToriRS_WorldTapClick(&tap, 20, 20, 0, 0) == 0, "a mouse click after a held tap runs now");
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "and the held tap is gone");

    ToriRS_WorldTapClick(&tap, 10, 10, 1, 0);
    ToriRS_WorldTapStep(&tap, 0);
    ToriRS_WorldTapStep(&tap, 0);
    TEST_ASSERT(ToriRS_WorldTapClick(&tap, 40, 50, 1, 0) == 1, "a second unanswered touch is held");
    TEST_ASSERT(ToriRS_WorldTapX(&tap) == 40 && ToriRS_WorldTapY(&tap) == 50, "at the new point");
    TEST_ASSERT(ToriRS_WorldTapFrames(&tap) == 0, "its wait starts over");

    TEST_ASSERT(ToriRS_WorldTapClick(&tap, 60, 70, 1, 1) == 0, "an answered touch runs now");
    TEST_ASSERT(!ToriRS_WorldTapHeld(&tap), "and replaces the held one");
}

int
main(void)
{
    test_reset_holds_nothing();
    test_mouse_and_answered_touch_run_now();
    test_unanswered_touch_is_held();
    test_comes_due_when_answered();
    test_comes_due_after_the_wait();
    test_new_click_replaces_held();
    if( g_failures )
    {
        printf("torirs_world_tap_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("torirs_world_tap_test: ok\n");
    return 0;
}
