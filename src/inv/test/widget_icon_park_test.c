/*
 * The widget obj-icon park (inv/widget_icon_park.h).
 *
 * One row per cell however often the script rewrites it, and a release that
 * takes the due rows from the head up to its burst while the rest keep their
 * order.
 *
 * Run: make -C src test-widget-icon-park
 */

#include "inv/widget_icon_park.h"

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
park(struct WidgetIconPark* p, int com_id, int obj_id, uint64_t due_ms)
{
    bool found;
    struct WidgetIconRequest* row = WidgetIconPark_Row(p, com_id, &found);

    row->obj_id = obj_id;
    row->count = 1;
    row->due_ms = due_ms;
    row->wait_ms = 0;
}

static void
test_one_row_per_cell(void)
{
    struct WidgetIconPark* p = WidgetIconPark_New();
    bool found = true;
    struct WidgetIconRequest* row;

    TEST_ASSERT(WidgetIconPark_Count(p) == 0, "a new park is empty");
    row = WidgetIconPark_Row(p, 7, &found);
    TEST_ASSERT(!found, "a cell with no row gets one");
    TEST_ASSERT(row->com_id == 7, "keyed by its component");
    row->obj_id = 100;
    row = WidgetIconPark_Row(p, 7, &found);
    TEST_ASSERT(found, "the same cell finds its row");
    TEST_ASSERT(row->obj_id == 100, "with what was written to it");
    TEST_ASSERT(WidgetIconPark_Count(p) == 1, "a rewritten cell is still one row");

    /* Past the first allocation: rows keep their contents across the growth. */
    for( int i = 0; i < 100; i++ )
        park(p, 1000 + i, i, 0);
    TEST_ASSERT(WidgetIconPark_Count(p) == 101, "every new cell is a row");
    row = WidgetIconPark_Row(p, 1050, &found);
    TEST_ASSERT(found && row->obj_id == 50, "a row survives the park growing");
    WidgetIconPark_Free(p);
    WidgetIconPark_Free(NULL);
}

static void
test_release_takes_due_rows_from_the_head(void)
{
    struct WidgetIconPark* p = WidgetIconPark_New();
    struct WidgetIconRequest out[3];
    int n;

    park(p, 1, 11, 0);
    park(p, 2, 12, 500); /* put back by a miss: not due yet */
    park(p, 3, 13, 0);
    park(p, 4, 14, 0);
    park(p, 5, 15, 0);

    n = WidgetIconPark_TakeDue(p, 100, 3, out);
    TEST_ASSERT(n == 3, "a burst takes at most its bound");
    TEST_ASSERT(out[0].com_id == 1 && out[1].com_id == 3 && out[2].com_id == 4, "the due rows, in order");
    TEST_ASSERT(WidgetIconPark_Count(p) == 2, "the rest stay parked");
    TEST_ASSERT(p->icons[0].com_id == 2 && p->icons[1].com_id == 5, "in their places in line");

    n = WidgetIconPark_TakeDue(p, 100, 3, out);
    TEST_ASSERT(n == 1 && out[0].com_id == 5, "the waiting row is passed over again");
    n = WidgetIconPark_TakeDue(p, 500, 3, out);
    TEST_ASSERT(n == 1 && out[0].com_id == 2, "and released once its wait is up");
    TEST_ASSERT(WidgetIconPark_Count(p) == 0, "leaving the park empty");
    WidgetIconPark_Free(p);
}

int
main(void)
{
    test_one_row_per_cell();
    test_release_takes_due_rows_from_the_head();
    if( g_failures )
    {
        printf("widget_icon_park: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("widget_icon_park: all checks passed\n");
    return 0;
}
