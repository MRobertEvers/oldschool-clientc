/*
 * The loc lane's tickets (world/loc_lane.h): taken from 1 in queue order,
 * each one's turn the one after the last released, and whether one is still
 * pending asked across the counters' wrap.
 *
 * Run: make -C src test-loc-lane
 */

#include "world/loc_lane.h"

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
test_tickets_apply_in_the_order_they_were_taken(void)
{
    struct LocLane lane;
    uint32_t add;
    uint32_t del;

    memset(&lane, 0, sizeof(lane));
    TEST_ASSERT(LocLane_Newest(&lane) == 0, "no ticket before the first");
    add = LocLane_Take(&lane);
    del = LocLane_Take(&lane);
    TEST_ASSERT(add == 1, "the first ticket is 1: 0 means none");
    TEST_ASSERT(del == 2, "the next is 2");
    TEST_ASSERT(LocLane_Newest(&lane) == del, "the newest is the last taken");

    TEST_ASSERT(LocLane_IsTurn(&lane, add), "the add goes first");
    TEST_ASSERT(!LocLane_IsTurn(&lane, del), "the delete waits behind it");
    TEST_ASSERT(LocLane_Pending(&lane, add), "the add is pending");
    TEST_ASSERT(LocLane_Pending(&lane, del), "and so is the delete");

    LocLane_Release(&lane);
    TEST_ASSERT(LocLane_Applied(&lane) == 1, "one released");
    TEST_ASSERT(!LocLane_Pending(&lane, add), "the add has landed");
    TEST_ASSERT(LocLane_IsTurn(&lane, del), "now the delete's turn");
    TEST_ASSERT(LocLane_Pending(&lane, del), "but it has not landed");

    LocLane_Release(&lane);
    TEST_ASSERT(!LocLane_Pending(&lane, del), "the delete has landed");
    TEST_ASSERT(!LocLane_IsTurn(&lane, del), "and its turn is over");
}

static void
test_pending_holds_across_the_wrap(void)
{
    struct LocLane lane;
    uint32_t last;
    uint32_t wrapped;

    memset(&lane, 0, sizeof(lane));
    lane.enqueued = 0xfffffffeu;
    lane.applied = 0xfffffffeu;
    last = LocLane_Take(&lane);
    wrapped = LocLane_Take(&lane);
    TEST_ASSERT(last == 0xffffffffu, "the last ticket before the wrap");
    TEST_ASSERT(wrapped == 0u, "the counter wraps");
    TEST_ASSERT(LocLane_Pending(&lane, last), "pending before the wrap");
    TEST_ASSERT(LocLane_Pending(&lane, wrapped), "pending past the wrap");
    LocLane_Release(&lane);
    TEST_ASSERT(!LocLane_Pending(&lane, last), "released");
    TEST_ASSERT(LocLane_Pending(&lane, wrapped), "the one past the wrap still waits");
    TEST_ASSERT(LocLane_IsTurn(&lane, wrapped), "and it is its turn");
}

int
main(void)
{
    test_tickets_apply_in_the_order_they_were_taken();
    test_pending_holds_across_the_wrap();
    if( g_failures )
    {
        printf("loc_lane_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("loc_lane_test: ok\n");
    return 0;
}
