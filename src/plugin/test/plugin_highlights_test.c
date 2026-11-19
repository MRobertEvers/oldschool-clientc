/*
 * The resolved highlight lists: their bound, and when the kept loc list must
 * be rebuilt from scratch.
 *
 * A list that ran past its bound would write over whatever follows it; a loc
 * list rebuilt when nothing it depends on moved puts the ~23k-entity scenery
 * walk back on every frame, and one NOT rebuilt when the highlight revision
 * moved keeps drawing a highlight that was turned off.
 */

#include "plugin/plugin_highlights.h"

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

static struct ToriRS_HighlightItem
an_item(int element_id)
{
    struct ToriRS_HighlightItem item;
    memset(&item, 0, sizeof(item));
    item.kind = TORIRS_HIGHLIGHT_LOC;
    item.element_id = element_id;
    item.size_x = 1;
    item.size_z = 1;
    return item;
}

/* Static: two lists of 256 items each are too big to want on the stack. */
static struct PluginHighlights g_hl;

static void
fresh(void)
{
    memset(&g_hl, 0, sizeof(g_hl));
}

static void
test_the_walked_list_fills_and_stops(void)
{
    struct ToriRS_HighlightItem item;
    struct ToriRS_HighlightItem* out;

    fresh();
    for( int i = 0; i < PLUGIN_HIGHLIGHTS_MAX; i++ )
    {
        item = an_item(i);
        TEST_ASSERT(PluginHighlights_Push(&g_hl, &item) != NULL, "a push below the bound is kept");
    }
    item = an_item(9999);
    TEST_ASSERT(PluginHighlights_Push(&g_hl, &item) == NULL, "a push at the bound is refused");
    TEST_ASSERT(PluginHighlights_Count(&g_hl) == PLUGIN_HIGHLIGHTS_MAX, "and does not count");
    TEST_ASSERT(PluginHighlights_Item(&g_hl, 7)->element_id == 7, "items keep their order");

    PluginHighlights_Clear(&g_hl);
    TEST_ASSERT(PluginHighlights_Count(&g_hl) == 0, "a clear empties it");
    item = an_item(3);
    out = PluginHighlights_Push(&g_hl, &item);
    TEST_ASSERT(out != NULL && out != &item, "a push hands back the kept copy");
    if( out )
        out->overhead_height = 42;
    TEST_ASSERT(PluginHighlights_Item(&g_hl, 0)->overhead_height == 42, "which the caller may finish");
}

static void
test_the_loc_list_fills_and_stops(void)
{
    struct ToriRS_HighlightItem item;

    fresh();
    for( int i = 0; i < PLUGIN_HIGHLIGHTS_MAX; i++ )
    {
        item = an_item(i);
        TEST_ASSERT(PluginHighlights_LocPush(&g_hl, &item) != NULL, "a loc push below the bound is kept");
    }
    item = an_item(9999);
    TEST_ASSERT(PluginHighlights_LocPush(&g_hl, &item) == NULL, "a loc push at the bound is refused");
    TEST_ASSERT(PluginHighlights_LocCount(&g_hl) == PLUGIN_HIGHLIGHTS_MAX, "and does not count");
    TEST_ASSERT(PluginHighlights_Count(&g_hl) == 0, "the walked list is a separate list");

    PluginHighlights_LocClear(&g_hl);
    TEST_ASSERT(PluginHighlights_LocCount(&g_hl) == 0, "a loc clear empties it");
}

static void
test_the_loc_list_is_rebuilt_only_when_its_inputs_move(void)
{
    fresh();
    TEST_ASSERT(PluginHighlights_LocNeedsFull(&g_hl, 0, false), "a list never built must be built");

    PluginHighlights_LocBuilt(&g_hl, 5);
    TEST_ASSERT(!PluginHighlights_LocNeedsFull(&g_hl, 5, false), "same revision, scenery tracked: kept");
    TEST_ASSERT(PluginHighlights_LocNeedsFull(&g_hl, 6, false), "a moved revision rebuilds");
    TEST_ASSERT(PluginHighlights_LocNeedsFull(&g_hl, 5, true), "untracked scenery changes rebuild");

    PluginHighlights_LocClear(&g_hl);
    TEST_ASSERT(
        !PluginHighlights_LocNeedsFull(&g_hl, 5, false),
        "emptying the list is part of a rebuild, not a reason for one");
}

static void
test_forget_drops_one_element_and_keeps_the_order(void)
{
    struct ToriRS_HighlightItem item;
    int const ids[] = { 1, 2, 1, 3, 2, 1 };

    fresh();
    for( int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++ )
    {
        item = an_item(ids[i]);
        PluginHighlights_LocPush(&g_hl, &item);
    }

    PluginHighlights_LocForget(&g_hl, 1);
    TEST_ASSERT(PluginHighlights_LocCount(&g_hl) == 3, "every entry for the element is dropped");
    TEST_ASSERT(PluginHighlights_LocItem(&g_hl, 0)->element_id == 2, "the rest keep their order");
    TEST_ASSERT(PluginHighlights_LocItem(&g_hl, 1)->element_id == 3, "the rest keep their order");
    TEST_ASSERT(PluginHighlights_LocItem(&g_hl, 2)->element_id == 2, "the rest keep their order");

    PluginHighlights_LocForget(&g_hl, 77);
    TEST_ASSERT(PluginHighlights_LocCount(&g_hl) == 3, "forgetting an absent element changes nothing");
}

int
main(void)
{
    test_the_walked_list_fills_and_stops();
    test_the_loc_list_fills_and_stops();
    test_the_loc_list_is_rebuilt_only_when_its_inputs_move();
    test_forget_drops_one_element_and_keeps_the_order();

    if( g_failures )
    {
        printf("plugin_highlights: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_highlights: all checks passed\n");
    return 0;
}
