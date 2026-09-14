/*
 * The server's IF_SETEVENTS store.
 *
 * At rev 230 nothing is clickable until the server says so, which makes this
 * table the arming gate for every click in the client. Its failure mode is
 * always the same and always looks like a different bug: the interface renders
 * perfectly, the right-click menu offers the verb -- that row comes from the
 * cache's own op list -- and the click does nothing at all.
 *
 * What is asserted:
 *
 *   - a declaration covers a RANGE of sub-ids, because that is how the server
 *     arms a list whose entries do not exist yet (the emotes tab arms slots
 *     0..55 of a container that is empty at login).
 *   - arming an interval REPLACES only that interval and leaves the pieces of
 *     an overlapping entry standing on either side. All five overlap shapes
 *     are covered: disjoint, prefix, suffix, strictly inside (which splits one
 *     entry into two), and fully covering.
 *   - `At` and `Lookup` differ, and the difference matters: a declared zero is
 *     a real answer that disables cache-authored ops, and reading it as
 *     "absent" switches every one of them back on.
 *   - clear empties without freeing, which is what IF_OPENTOP does.
 *   - a dynamic child is addressed as (container, index within it) -- the
 *     `combinedId`/`sub` pair the wire carries -- and inherits its container's
 *     armed range only when its index falls inside it.
 *
 * Build and run:
 *   make -C src test-uitree-if-events
 */

#include "ui/uitree_if_events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_a_range_arms_every_slot_in_it(void)
{
    struct UIIfEventTable table;

    memset(&table, 0, sizeof(table));

    /* The emotes tab: one op on fifty-six cells that do not exist yet. */
    UIIfEventTable_Set(&table, 216, 0, 55, 1);
    CHECK(UIIfEventTable_At(&table, 216, 0) == 1, "the first slot is not armed");
    CHECK(UIIfEventTable_At(&table, 216, 55) == 1, "the last slot is not armed");
    CHECK(UIIfEventTable_At(&table, 216, 30) == 1, "a middle slot is not armed");
    CHECK(UIIfEventTable_At(&table, 216, 56) == 0, "the slot past the range is armed");
    CHECK(UIIfEventTable_At(&table, 216, -1) == 0, "the plain-widget slot is armed");
    CHECK(UIIfEventTable_At(&table, 217, 0) == 0, "another component is armed");

    /* A plain widget is a range of one at sub-id -1. */
    UIIfEventTable_Set(&table, 500, -1, -1, 0x40);
    CHECK(UIIfEventTable_At(&table, 500, -1) == 0x40, "a plain widget is not armed");

    /* A reversed interval is normalised, not refused. */
    UIIfEventTable_Set(&table, 900, 9, 3, 7);
    CHECK(UIIfEventTable_At(&table, 900, 5) == 7, "a reversed interval did not arm");

    UIIfEventTable_Free(&table);
    CHECK(table.ranges == NULL && table.count == 0, "free left the table populated");
}

static void
test_setting_an_interval_preserves_what_is_around_it(void)
{
    struct UIIfEventTable table;

    /* Strictly inside: the entry must SPLIT, leaving a piece on each side.
     * This is the shape a container with many independently armed child
     * ranges produces, and the one a naive "delete anything overlapping"
     * silently disarms. */
    memset(&table, 0, sizeof(table));
    UIIfEventTable_Set(&table, 10, 0, 27, 1);
    UIIfEventTable_Set(&table, 10, 10, 12, 2);
    CHECK(UIIfEventTable_At(&table, 10, 9) == 1, "the piece before the hole was lost");
    CHECK(UIIfEventTable_At(&table, 10, 10) == 2, "the new interval did not take");
    CHECK(UIIfEventTable_At(&table, 10, 12) == 2, "the new interval is short");
    CHECK(UIIfEventTable_At(&table, 10, 13) == 1, "the piece after the hole was lost");
    CHECK(UIIfEventTable_At(&table, 10, 27) == 1, "the tail was lost");

    /* Prefix overlap: the old entry keeps only its tail. */
    memset(&table, 0, sizeof(table));
    UIIfEventTable_Set(&table, 10, 5, 20, 1);
    UIIfEventTable_Set(&table, 10, 0, 9, 2);
    CHECK(UIIfEventTable_At(&table, 10, 5) == 2, "the overlap did not take the new value");
    CHECK(UIIfEventTable_At(&table, 10, 10) == 1, "the old tail was lost");

    /* Suffix overlap: the old entry keeps only its head. */
    memset(&table, 0, sizeof(table));
    UIIfEventTable_Set(&table, 10, 5, 20, 1);
    UIIfEventTable_Set(&table, 10, 15, 30, 2);
    CHECK(UIIfEventTable_At(&table, 10, 14) == 1, "the old head was lost");
    CHECK(UIIfEventTable_At(&table, 10, 15) == 2, "the overlap did not take the new value");
    CHECK(UIIfEventTable_At(&table, 10, 30) == 2, "the new interval is short");

    /* Fully covering: the old entry goes entirely. */
    memset(&table, 0, sizeof(table));
    UIIfEventTable_Set(&table, 10, 5, 10, 1);
    UIIfEventTable_Set(&table, 10, 0, 20, 2);
    CHECK(UIIfEventTable_At(&table, 10, 7) == 2, "a covered entry survived");

    /* Disjoint, and a different component: neither is touched. */
    memset(&table, 0, sizeof(table));
    UIIfEventTable_Set(&table, 10, 0, 5, 1);
    UIIfEventTable_Set(&table, 10, 6, 9, 2);
    UIIfEventTable_Set(&table, 11, 0, 5, 3);
    CHECK(UIIfEventTable_At(&table, 10, 3) == 1, "a disjoint earlier entry was disturbed");
    CHECK(UIIfEventTable_At(&table, 10, 7) == 2, "a disjoint later entry did not arm");
    CHECK(UIIfEventTable_At(&table, 11, 3) == 3, "another component was disturbed");

    UIIfEventTable_Free(&table);
}

static void
test_a_declared_zero_is_not_an_absence(void)
{
    struct UIIfEventTable table;
    unsigned events = 0xdead;

    memset(&table, 0, sizeof(table));

    CHECK(!UIIfEventTable_Lookup(&table, 10, -1, &events), "an empty table reported an entry");

    /* The server disabling every op on a widget. `At` cannot distinguish this
     * from "nothing declared", which is why the effective lookup uses
     * presence: read as absent, the widget's cache-authored ops come back. */
    UIIfEventTable_Set(&table, 10, -1, -1, 0);
    CHECK(UIIfEventTable_At(&table, 10, -1) == 0, "a declared zero read as non-zero");
    CHECK(UIIfEventTable_Lookup(&table, 10, -1, &events), "a declared zero read as absent");
    CHECK(events == 0, "the declared zero came back as %u", events);
    CHECK(!UIIfEventTable_Lookup(&table, 11, -1, &events), "an undeclared component reported one");

    /* Clear empties but keeps the storage: IF_OPENTOP drops the arming, and
     * the next root re-arms into the same allocation. */
    {
        struct UIIfEventRange* before = table.ranges;

        UIIfEventTable_Clear(&table);
        CHECK(table.count == 0, "clear left %d entries", table.count);
        CHECK(table.ranges == before, "clear released the storage");
        CHECK(!UIIfEventTable_Lookup(&table, 10, -1, &events), "an entry survived clear");
    }

    UIIfEventTable_Free(&table);
}

/*
 * A two-node tree: a container, and one dynamic child inside it at a known
 * index. Built by hand because the real builder needs a cache, and what is
 * under test is the addressing, not the build.
 */
static struct UITree*
tree_with_dynamic_child(
    int container_com_id,
    int child_com_id,
    int child_index)
{
    struct UITree* tree = calloc(1, sizeof(*tree));

    tree->components = calloc(2, sizeof(*tree->components));
    tree->component_count = 2;

    tree->components[0].component_id = container_com_id;
    tree->components[0].parent = -1;

    tree->components[1].component_id = child_com_id;
    tree->components[1].parent = 0;
    tree->components[1].dynamic = 1;
    tree->components[1].dynamic_child_index = child_index;
    return tree;
}

static void
free_tree(struct UITree* tree)
{
    free(tree->components);
    free(tree);
}

static void
test_a_dynamic_child_is_addressed_by_its_container(void)
{
    struct UIIfEventTable table;
    struct UITree* tree = tree_with_dynamic_child(216, 999999, 30);
    int com = -1;
    int sub = -1;

    memset(&table, 0, sizeof(table));

    /* The wire pair. The child's own id is a runtime allocation the server has
     * never heard of, so sending it means the handler can never match. */
    UIIfEventTable_ButtonTarget(tree, 999999, &com, &sub);
    CHECK(com == 216 && sub == 30, "dynamic child resolved to (%d, %d), want (216, 30)", com, sub);

    /* A static component resolves to itself. */
    UIIfEventTable_ButtonTarget(tree, 216, &com, &sub);
    CHECK(com == 216 && sub == -1, "container resolved to (%d, %d), want (216, -1)", com, sub);

    /* With no tree at all -- which is every call before the first mount. */
    UIIfEventTable_ButtonTarget(NULL, 999999, &com, &sub);
    CHECK(com == 999999 && sub == -1, "a treeless resolve gave (%d, %d)", com, sub);

    /* The container's range arms the child, by index. */
    UIIfEventTable_Set(&table, 216, 0, 55, 1);
    CHECK(
        UIIfEventTable_Effective(&table, tree, 999999) == 1,
        "the child did not inherit its container's arming");

    /* ...and only inside the declared range. A container armed for 0..27 must
     * not arm slot 30, or a bank's 28 slots arm a 29th that is not there. */
    UIIfEventTable_Clear(&table);
    UIIfEventTable_Set(&table, 216, 0, 27, 1);
    CHECK(
        UIIfEventTable_Effective(&table, tree, 999999) == 0,
        "a child outside the declared range was armed");

    /* The child's OWN entry wins over its container's. */
    UIIfEventTable_Clear(&table);
    UIIfEventTable_Set(&table, 216, 0, 55, 1);
    UIIfEventTable_Set(&table, 999999, -1, -1, 8);
    CHECK(
        UIIfEventTable_Effective(&table, tree, 999999) == 8,
        "the child's own entry lost to its container's");

    /* Nothing declared anywhere falls back to the widget's decoded mask. */
    UIIfEventTable_Clear(&table);
    tree->components[0].behavior.click_mask = 0x21;
    CHECK(
        UIIfEventTable_Effective(&table, tree, 216) == 0x21,
        "the decoded click mask was not the fallback");

    /* ...but a declared zero suppresses it, which is the whole point of the
     * presence lookup. */
    UIIfEventTable_Set(&table, 216, -1, -1, 0);
    CHECK(
        UIIfEventTable_Effective(&table, tree, 216) == 0,
        "a declared zero did not suppress the decoded mask");

    UIIfEventTable_Free(&table);
    free_tree(tree);
}

int
main(void)
{
    test_a_range_arms_every_slot_in_it();
    test_setting_an_interval_preserves_what_is_around_it();
    test_a_declared_zero_is_not_an_absence();
    test_a_dynamic_child_is_addressed_by_its_container();

    if( g_failures )
    {
        printf("uitree_if_events_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_if_events_test: OK\n");
    return 0;
}
