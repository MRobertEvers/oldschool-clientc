/*
 * The hover-footprint outline's state (editor/hover_footprint.h).
 *
 * A zeroed outline is off with nothing seeded. A positive env mode starts it
 * on at that mode, 0 or a negative one starts it off with mode 1 to restore,
 * and the toggle swaps between off and the mode it restores without
 * forgetting it.
 *
 * Run: make -C src test-hover-footprint
 */

#include "editor/hover_footprint.h"

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

static void
test_footprint(void)
{
    struct HoverFootprint footprint = { 0 };

    TEST_ASSERT(footprint.live == 0, "the outline starts off");
    TEST_ASSERT(footprint.mode == 0, "nothing seeded yet");

    HoverFootprint_Seed(&footprint, 0);
    TEST_ASSERT(footprint.live == 0, "unset is off");
    TEST_ASSERT(footprint.mode == 1, "unset arms mode 1");
    TEST_ASSERT(HoverFootprint_Toggle(&footprint) == 1, "the toggle turns mode 1 on");
    TEST_ASSERT(HoverFootprint_Toggle(&footprint) == 0, "and off again");

    HoverFootprint_Seed(&footprint, -4);
    TEST_ASSERT(footprint.live == 0, "a negative mode is off");
    TEST_ASSERT(footprint.mode == 1, "with mode 1 to restore");

    HoverFootprint_Seed(&footprint, 1234);
    TEST_ASSERT(footprint.live == 1234, "a loc id mode starts on");
    TEST_ASSERT(HoverFootprint_Toggle(&footprint) == 0, "the toggle turns it off");
    TEST_ASSERT(footprint.mode == 1234, "the mode is kept while off");
    TEST_ASSERT(HoverFootprint_Toggle(&footprint) == 1234, "and back on at the same id");
}

int
main(void)
{
    test_footprint();
    if( g_failures )
    {
        printf("hover_footprint_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("hover_footprint_test: ok\n");
    return 0;
}
