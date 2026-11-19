/*
 * The mounts kept for a top that is not the live root (ui/uitree_kept_mounts.h).
 *
 * Adding until full, taking one out (the last fills its place), and a detach:
 * the live records first, the kept ones no live record names after them, and
 * what a full table forgets reported in order.
 *
 * Run: make -C src test-uitree-kept-mounts
 */

#include "ui/uitree_kept_mounts.h"

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
test_add_until_full(void)
{
    struct UITreeKeptMounts kept;

    memset(&kept, 0x44, sizeof(kept));
    UITreeKeptMounts_Clear(&kept);
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == 0, "a cleared table keeps nothing");
    for( int i = 0; i < UITREE_INTERFACE_PARENT_MAX; i++ )
        TEST_ASSERT(UITreeKeptMounts_Add(&kept, 100 + i, 200 + i, 1), "a table with room keeps the mount");
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == UITREE_INTERFACE_PARENT_MAX, "every one is kept");
    TEST_ASSERT(!UITreeKeptMounts_Add(&kept, 999, 999, 1), "a full table refuses");
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == UITREE_INTERFACE_PARENT_MAX, "and keeps no more");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, 3)->container_uid == 103, "records stay in the order added");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, 3)->group_id == 203, "with their group");
}

static void
test_take_moves_the_last_into_the_gap(void)
{
    struct UITreeKeptMounts kept;
    struct UITreeInterfaceParent taken;

    UITreeKeptMounts_Clear(&kept);
    UITreeKeptMounts_Add(&kept, 10, 162, 1);
    UITreeKeptMounts_Add(&kept, 11, 629, 0);
    UITreeKeptMounts_Add(&kept, 12, 399, 3);

    taken = UITreeKeptMounts_Take(&kept, 10);
    TEST_ASSERT(taken.container_uid == 10 && taken.group_id == 162 && taken.type == 1, "the record comes out whole");
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == 2, "and is no longer kept");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, 0)->container_uid == 12, "the last record takes its place");

    taken = UITreeKeptMounts_Take(&kept, 77);
    TEST_ASSERT(taken.container_uid == 77 && taken.group_id == 0, "a slot with nothing kept answers group 0");
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == 2, "and takes nothing");
}

static void
test_detach_puts_the_live_tree_first(void)
{
    struct UITreeKeptMounts kept;
    struct UITreeInterfaceParent live[2] = { { 20, 162, 1 }, { 21, 548, 0 } };
    struct UITreeInterfaceParent forgotten[UITREE_INTERFACE_PARENT_MAX];
    int n;

    UITreeKeptMounts_Clear(&kept);
    UITreeKeptMounts_Add(&kept, 21, 111, 0); /* the live tree names this slot */
    UITreeKeptMounts_Add(&kept, 30, 892, 1); /* only the old top has this one */

    n = UITreeKeptMounts_Detach(&kept, live, 2, forgotten);
    TEST_ASSERT(n == 0, "a table with room forgets nothing");
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == 3, "the live two and the one no live slot names");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, 0)->container_uid == 20, "live records first");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, 1)->group_id == 548, "the live record wins a slot both name");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, 2)->container_uid == 30, "a kept record no move claims stays");
}

static void
test_detach_into_a_full_table_reports_what_it_forgot(void)
{
    struct UITreeKeptMounts kept;
    struct UITreeInterfaceParent live[UITREE_INTERFACE_PARENT_MAX];
    struct UITreeInterfaceParent forgotten[UITREE_INTERFACE_PARENT_MAX];
    int n;

    for( int i = 0; i < UITREE_INTERFACE_PARENT_MAX - 1; i++ )
    {
        live[i].container_uid = 1000 + i;
        live[i].group_id = 1;
        live[i].type = 1;
    }
    UITreeKeptMounts_Clear(&kept);
    UITreeKeptMounts_Add(&kept, 1, 501, 1);
    UITreeKeptMounts_Add(&kept, 2, 502, 1);
    UITreeKeptMounts_Add(&kept, 3, 503, 1);

    n = UITreeKeptMounts_Detach(&kept, live, UITREE_INTERFACE_PARENT_MAX - 1, forgotten);
    TEST_ASSERT(UITreeKeptMounts_Count(&kept) == UITREE_INTERFACE_PARENT_MAX, "the table fills");
    TEST_ASSERT(UITreeKeptMounts_At(&kept, UITREE_INTERFACE_PARENT_MAX - 1)->group_id == 501, "the first kept one fits");
    TEST_ASSERT(n == 2, "the other two are forgotten");
    TEST_ASSERT(forgotten[0].group_id == 502 && forgotten[1].group_id == 503, "and reported in order");
}

int
main(void)
{
    test_add_until_full();
    test_take_moves_the_last_into_the_gap();
    test_detach_puts_the_live_tree_first();
    test_detach_into_a_full_table_reports_what_it_forgot();
    if( g_failures )
    {
        printf("uitree_kept_mounts: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("uitree_kept_mounts: all checks passed\n");
    return 0;
}
