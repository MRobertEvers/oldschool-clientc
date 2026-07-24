#include "test_harness.h"

#include <stdlib.h>

/* UITree_FindByComponentId is served by an incrementally-maintained id->index
 * map (UITree_Push folds new ids in; a reclaim leaves it stale for the next
 * lookup to rebuild). These tests pin the map to the linear scan it replaced —
 * including the cases where the two could diverge: free-list slot reuse handing
 * a *lower* index to a later push, and duplicate ids split across a dynamic and
 * a non-dynamic node. */

/* The oracle: first dynamic node with this id, else the lowest-index node. */
static int32_t
find_linear(
    struct UITree const* tree,
    int component_id)
{
    int32_t fallback = -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].component_id != component_id )
            continue;
        if( tree->components[i].dynamic )
            return (int32_t)i;
        if( fallback < 0 )
            fallback = (int32_t)i;
    }
    return fallback;
}

static void
check_all_ids(
    struct UITree* tree,
    char const* where)
{
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        int const id = tree->components[i].component_id;
        if( id < 0 )
            continue;
        if( UITree_FindByComponentId(tree, id) != find_linear(tree, id) )
        {
            fprintf(
                stderr,
                "FAIL: id index disagrees with linear scan for id %d (%s)\n",
                id,
                where);
            g_failures++;
            return;
        }
    }
    TEST_ASSERT(UITree_FindByComponentId(tree, 0x7ffffff) < 0, "absent id misses");
}

void
test_id_index(void)
{
    printf("TEST: component id index\n");

    struct UITree* tree = UITree_New(4);
    TEST_ASSERT(tree != NULL, "UITree_New");

    int const parent_id = (161 << 16) | 10;
    int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 200, 200);
    TEST_ASSERT(parent >= 0, "push parent");

    /* Growth past the initial map capacity, with a lookup between every push so
     * the incremental insert path (not a rebuild) is what fills the map. */
    for( int i = 0; i < 64; i++ )
    {
        int32_t idx = UITree_TestPushXy(
            tree, parent, UIELEM_RS_RECT, (161 << 16) | (100 + i), 0, i, 10, 10);
        TEST_ASSERT(idx >= 0, "push static child");
        TEST_ASSERT(
            UITree_FindByComponentId(tree, (161 << 16) | (100 + i)) == idx,
            "find static child right after push");
    }
    check_all_ids(tree, "after static pushes");

    /* Children land in push order, i.e. the tail hint appends rather than
     * prepends or drops nodes. */
    {
        int count = 0;
        int32_t child = tree->components[parent].first_child;
        while( child >= 0 )
        {
            TEST_ASSERT(
                tree->components[child].component_id == ((161 << 16) | (100 + count)),
                "child list is in push order");
            count++;
            child = tree->components[child].next_sibling;
        }
        TEST_ASSERT(count == 64, "all children linked");
    }

    /* cc_create allocates dynamic uids by probing the index, then pushes — the
     * loop that made the rebuild quadratic. Ids must stay unique. */
    for( int sub = 0; sub < 40; sub++ )
    {
        int32_t idx = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, sub);
        TEST_ASSERT(idx >= 0, "cc_create child");
        TEST_ASSERT(
            UITree_FindChildBySubid(tree, parent, parent_id, sub) == idx,
            "cc_create child found by subid");
    }
    check_all_ids(tree, "after cc_create");

    /* Reclaim + re-create: the free list hands slots back out of order, so a
     * later push can occupy a lower index than an existing entry for the same
     * id. The map must still pick the linear winner. */
    UITree_CcDeleteAll(tree, parent);
    TEST_ASSERT(
        UITree_FindChildBySubid(tree, parent, parent_id, 0) < 0, "deleteall dropped dynamics");
    check_all_ids(tree, "after deleteall");

    for( int sub = 0; sub < 40; sub++ )
    {
        int32_t idx = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_TEXT, sub);
        TEST_ASSERT(idx >= 0, "cc_create after reclaim");
    }
    check_all_ids(tree, "after recreate");

    /* Static children survived the dynamic-only reclaim and are still the
     * lowest-index holders of their ids. */
    for( int i = 0; i < 64; i++ )
    {
        int const id = (161 << 16) | (100 + i);
        TEST_ASSERT(UITree_FindByComponentId(tree, id) == find_linear(tree, id), "static id kept");
    }

    /* Duplicate id, dynamic vs non-dynamic: the dynamic node wins regardless of
     * which one was pushed (and indexed) first. */
    {
        int const dup = (161 << 16) | 4242;
        int32_t plain = UITree_TestPushXy(tree, parent, UIELEM_RS_RECT, dup, 0, 0, 5, 5);
        TEST_ASSERT(plain >= 0, "push duplicate static");
        TEST_ASSERT(UITree_FindByComponentId(tree, dup) == plain, "static holds the id");

        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_RECT;
        spec.component_id = dup;
        spec.dynamic = 1;
        spec.dynamic_child_index = 999;
        int32_t dyn = UITree_Push(tree, parent, &spec);
        TEST_ASSERT(dyn >= 0, "push duplicate dynamic");
        TEST_ASSERT(UITree_FindByComponentId(tree, dup) == dyn, "dynamic wins the duplicate id");
        TEST_ASSERT(find_linear(tree, dup) == dyn, "oracle agrees dynamic wins");
    }
    check_all_ids(tree, "after duplicate ids");

    UITree_Free(tree);

    /* The one ordering the map cannot infer from insertion order: a recycled
     * slot gives a *later* push a *lower* index than the entry already stored
     * for that id, and both are non-dynamic (so "dynamic wins" does not decide
     * it). The linear scan returns the lower index, so the map must too. */
    tree = UITree_New(4);
    TEST_ASSERT(tree != NULL, "UITree_New (recycled-slot case)");
    {
        int const dup = (161 << 16) | 77;
        int32_t owner = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 100, 100);
        /* Slot 1: a dynamic child, i.e. the one CC_DELETEALL will recycle. */
        int32_t recycled = UITree_CcCreate(tree, owner, parent_id, UIELEM_RS_RECT, 0);
        int32_t filler = UITree_TestPushXy(tree, owner, UIELEM_RS_RECT, (161 << 16) | 78, 0, 0, 5, 5);
        int32_t high = UITree_TestPushXy(tree, owner, UIELEM_RS_RECT, dup, 0, 10, 5, 5);
        TEST_ASSERT(owner >= 0 && recycled >= 0 && filler >= 0 && high >= 0, "push nodes");
        TEST_ASSERT(recycled < high, "recycled slot is below the id holder");

        UITree_CcDeleteAll(tree, owner);
        /* Rebuild now, so the next push takes the incremental-insert path with
         * the freed (lower) slot rather than being folded in by a rebuild. */
        TEST_ASSERT(UITree_FindByComponentId(tree, dup) == high, "high holds the id");

        int32_t reused = UITree_TestPushXy(tree, owner, UIELEM_RS_RECT, dup, 0, 20, 5, 5);
        TEST_ASSERT(reused == recycled, "push reused the freed slot");
        TEST_ASSERT(find_linear(tree, dup) == reused, "oracle picks the lower index");
        TEST_ASSERT(
            UITree_FindByComponentId(tree, dup) == reused, "index picks the lower index too");
    }
    UITree_Free(tree);
}
