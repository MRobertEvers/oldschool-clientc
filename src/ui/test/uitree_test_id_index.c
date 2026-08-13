#include "test_harness.h"

#include <stdlib.h>

/* UITree_FindByComponentId is served by an incrementally-maintained id->index
 * map (UITree_Push folds new ids in; a reclaim leaves it stale for the next
 * lookup to rebuild). These tests pin the map to the linear scan it replaced —
 * including the cases where the two could diverge: free-list slot reuse handing
 * a *lower* index to a later push, and duplicate ids split across a dynamic and
 * a non-dynamic node. */

/* The oracle for UITree_FindChildBySubid: the sibling-list scan that the
 * child_key_max ceiling short-circuits. */
static int32_t
find_child_linear(
    struct UITree const* tree,
    int32_t parent_index,
    int sub_id)
{
    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct UITreeComponent const* comp = &tree->components[child];
        if( comp->dynamic && comp->dynamic_child_index == sub_id )
            return child;
        if( !comp->dynamic && (comp->component_id & 0xFFFF) == (sub_id & 0xFFFF) )
            return child;
    }
    return -1;
}

static void
check_subids(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int highest,
    char const* where)
{
    /* Past the highest key as well as inside it: the fast path only ever
     * answers "no such child", so the misses are what can go wrong. */
    for( int sub = -1; sub <= highest + 4; sub++ )
    {
        int32_t const got = UITree_FindChildBySubid(tree, parent_index, parent_component_id, sub);
        int32_t const want = find_child_linear(tree, parent_index, sub);
        if( got != want )
        {
            fprintf(
                stderr,
                "FAIL: FindChildBySubid(%d) = %d, scan says %d (%s)\n",
                sub,
                (int)got,
                (int)want,
                where);
            g_failures++;
            return;
        }
    }
}

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

/* The submenu block hangs off menu_options by pointer and is owned per component,
 * so the interesting cases are the ownership ones: a copy must not alias the
 * source, and reclaim/free must release it exactly once (run under ASan to check
 * that half). */
void
test_menu_submenus(void)
{
    printf("TEST: op submenu storage\n");

    int const iface = 161;
    int const parent_id = (iface << 16) | 30;
    struct UITree* tree = UITree_New(8);
    TEST_ASSERT(tree != NULL, "UITree_New");

    int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 100, 100);
    int32_t row = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, 0);
    TEST_ASSERT(parent >= 0 && row >= 0, "push parent + row");

    struct UITreeMenuOptions* opts = UITree_MenuOptionsMut(&tree->components[row]);
    TEST_ASSERT(opts->submenus == NULL, "no block until a submenu is set");
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts, 1, 1)[0] == '\0', "unset entry reads empty");

    TEST_ASSERT(UITree_MenuSubmenuSetEntry(opts, 2, 3, "Withdraw-10"), "set entry");
    TEST_ASSERT(opts->submenus != NULL, "block allocated on first write");
    TEST_ASSERT(strcmp(UITree_MenuSubmenuEntry(opts, 2, 3), "Withdraw-10") == 0, "entry reads back");
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts, 2, 4)[0] == '\0', "neighbour entry still empty");
    TEST_ASSERT(!UITree_MenuSubmenuSetEntry(opts, 0, 1, "x"), "op index 0 rejected");
    TEST_ASSERT(
        !UITree_MenuSubmenuSetEntry(opts, UITREE_SUBMENU_OP_SLOTS + 1, 1, "x"),
        "op index past the end rejected");

    /* CC_COPY must give the copy its own block. */
    int32_t copy = UITree_CcCopy(tree, parent, parent_id, 0, 1);
    TEST_ASSERT(copy >= 0, "cc_copy row");
    {
        struct UITreeMenuOptions* dst = UITree_MenuOptionsMut(&tree->components[copy]);
        TEST_ASSERT(dst->submenus != NULL, "copy carries the submenus");
        TEST_ASSERT(dst->submenus != UITree_MenuOptionsMut(&tree->components[row])->submenus, "copy is not an alias");
        TEST_ASSERT(strcmp(UITree_MenuSubmenuEntry(dst, 2, 3), "Withdraw-10") == 0, "copied entry");
        TEST_ASSERT(UITree_MenuSubmenuSetEntry(dst, 2, 3, "Withdraw-X"), "write to the copy");
        TEST_ASSERT(
            strcmp(UITree_MenuSubmenuEntry(UITree_MenuOptions(&tree->components[row]), 2, 3),
                   "Withdraw-10") == 0,
            "source unchanged by the copy's write");
    }

    /* Clearing one op keeps the block; clearing all of them drops it. */
    UITree_MenuSubmenuClear(opts, 2);
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts, 2, 3)[0] == '\0', "cleared op entry");
    UITree_MenuSubmenuSetEntry(opts, 1, 1, "Ok");
    UITree_MenuSubmenuClear(opts, 0);
    TEST_ASSERT(opts->submenus == NULL, "clear-all releases the block");
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts, 1, 1)[0] == '\0', "reads empty after release");

    /* Reclaim (with a live block on the copy) then tree free: no leak, no double
     * free. */
    UITree_CcDeleteAll(tree, parent);
    UITree_Free(tree);
}

/* UITree_FindChildBySubid answers a miss from the parent's key ceiling instead of
 * walking the sibling list. Every case here compares against the scan, since the
 * failure mode is a false "no such child". */
void
test_child_subid(void)
{
    printf("TEST: child lookup by sub-id\n");

    int const iface = 161;
    int const parent_id = (iface << 16) | 20;
    struct UITree* tree = UITree_New(8);
    TEST_ASSERT(tree != NULL, "UITree_New");

    int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 100, 100);
    TEST_ASSERT(parent >= 0, "push parent");

    /* Cache-baked children match on the low half of their uid. */
    for( int i = 0; i < 6; i++ )
        UITree_TestPushXy(tree, parent, UIELEM_RS_RECT, (iface << 16) | (30 + i), 0, i, 8, 8);
    check_subids(tree, parent, parent_id, 40, "static children only");

    /* The rebuild pattern: a run of ascending cc_create rows. Each row's own
     * lookup must miss before the create and hit after it. */
    for( int sub = 0; sub < 24; sub++ )
    {
        TEST_ASSERT(
            UITree_FindChildBySubid(tree, parent, parent_id, sub) < 0
                || find_child_linear(tree, parent, sub) >= 0,
            "pre-create lookup agrees with the scan");
        int32_t idx = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, sub);
        TEST_ASSERT(idx >= 0, "cc_create row");
        TEST_ASSERT(
            UITree_FindChildBySubid(tree, parent, parent_id, sub) == idx, "row found after create");
    }
    check_subids(tree, parent, parent_id, 30, "after ascending creates");

    /* Replace-in-slot: creating an existing sub_id reuses the slot rather than
     * adding a second row (the ceiling must not hide the existing one). */
    {
        int const before = 0;
        int32_t first = UITree_FindChildBySubid(tree, parent, parent_id, 7);
        TEST_ASSERT(first >= 0, "row 7 present");
        int32_t again = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_TEXT, 7);
        TEST_ASSERT(again >= 0, "re-create row 7");
        int count = 0;
        for( int32_t child = tree->components[parent].first_child; child >= 0;
             child = tree->components[child].next_sibling )
            if( tree->components[child].dynamic && tree->components[child].dynamic_child_index == 7 )
                count++;
        TEST_ASSERT(count == 1, "re-create replaced instead of duplicating");
        (void)before;
    }
    check_subids(tree, parent, parent_id, 30, "after replace-in-slot");

    /* Deleting the highest row lowers the ceiling; the rest must still be found. */
    {
        int32_t top = UITree_FindChildBySubid(tree, parent, parent_id, 23);
        TEST_ASSERT(top >= 0, "top row present");
        UITree_CcDeleteAll(tree, parent);
        check_subids(tree, parent, parent_id, 30, "after deleteall");
        for( int sub = 0; sub < 24; sub++ )
            TEST_ASSERT(
                UITree_FindChildBySubid(tree, parent, parent_id, sub) == find_child_linear(tree, parent, sub),
                "dynamic rows gone, statics still matched");
    }

    /* A sub_id wider than 16 bits must not use the ceiling: the scan compares
     * non-dynamic children masked, so it can match a much lower key. */
    {
        int32_t wide_hit = UITree_FindChildBySubid(tree, parent, parent_id, 0x10000 | 30);
        TEST_ASSERT(
            wide_hit == find_child_linear(tree, parent, 0x10000 | 30),
            "wide sub_id still matches a masked static child");
        TEST_ASSERT(wide_hit >= 0, "wide sub_id found the static child");
    }

    /* Reparenting a high-keyed child into a fresh parent moves the ceiling with
     * it, and out of the old one. */
    {
        int32_t other = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, (iface << 16) | 21, 0, 0, 50, 50);
        int32_t moved = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, 99);
        TEST_ASSERT(other >= 0 && moved >= 0, "push other parent + row");
        UITree_Reparent(tree, moved, other);
        TEST_ASSERT(
            UITree_FindChildBySubid(tree, other, (iface << 16) | 21, 99) == moved,
            "moved row found under the new parent");
        TEST_ASSERT(
            UITree_FindChildBySubid(tree, parent, parent_id, 99) < 0,
            "moved row no longer under the old parent");
        check_subids(tree, other, (iface << 16) | 21, 105, "after reparent (new parent)");
        check_subids(tree, parent, parent_id, 105, "after reparent (old parent)");
    }

    UITree_Free(tree);
}
