/*
 * The aboard-view trace latch (world/wev_aboard_trace.h).
 *
 * A zeroed latch has traced the root view, so the root is never a change on
 * the first pass. Any other view is a change exactly once, and coming back to
 * the root after it is a change again.
 *
 * Run: make -C src test-wev-aboard-trace
 */

#include "world/wev_aboard_trace.h"

#include "world/worldview.h"

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

int
main(void)
{
    struct WevAboardTrace trace;

    memset(&trace, 0, sizeof(trace));
    TEST_ASSERT(!WevAboardTrace_Changed(&trace, WORLDVIEW_ROOT), "root is not a change at start");
    TEST_ASSERT(WevAboardTrace_Changed(&trace, 3), "boarding view 3 is a change");
    TEST_ASSERT(trace.traced_view == 3, "and is remembered");
    TEST_ASSERT(!WevAboardTrace_Changed(&trace, 3), "still aboard 3 is not");
    TEST_ASSERT(WevAboardTrace_Changed(&trace, 5), "another view is");
    TEST_ASSERT(WevAboardTrace_Changed(&trace, WORLDVIEW_ROOT), "back to root is");
    TEST_ASSERT(!WevAboardTrace_Changed(&trace, WORLDVIEW_ROOT), "once");
    if( g_failures )
    {
        printf("wev_aboard_trace_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("wev_aboard_trace_test: ok\n");
    return 0;
}
