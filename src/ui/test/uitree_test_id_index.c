#include "ui/uitree_checkpoint.h"
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

    /* The last answer is kept beside the map. The shape that exercises it is a
     * container rebuild: the script asks for the group's root, the container
     * and the row in turn, and every row replaces the one before it in the
     * same slot. An answer held from before a replace must never outlive the
     * node it named, and a tree that has answered nothing must not answer -1
     * with node 0. */
    tree = UITree_New(4);
    TEST_ASSERT(tree != NULL, "UITree_New (last answer)");
    TEST_ASSERT(UITree_FindByComponentId(tree, -1) < 0, "-1 misses on an empty tree");
    TEST_ASSERT(UITree_FindByComponentId(tree, 0) < 0, "id 0 misses on an empty tree");
    {
        int const root_id = 161 << 16;
        int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, root_id, 0, 0, 300, 300);
        int32_t box = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, parent_id, 0, 0, 200, 200);
        int held[8];
        int mismatches = 0;
        TEST_ASSERT(root >= 0 && box >= 0, "push root + container");
        TEST_ASSERT(UITree_FindByComponentId(tree, -1) < 0, "-1 misses on a filled tree");

        for( int i = 0; i < 8; i++ )
            held[i] = -1;
        for( int round = 0; round < 6; round++ )
        {
            for( int sub = 0; sub < 48; sub++ )
            {
                mismatches += UITree_FindByComponentId(tree, root_id) != root;
                mismatches += UITree_FindByComponentId(tree, parent_id) != box;
                int32_t row = UITree_CcCreate(tree, box, parent_id, UIELEM_RS_RECT, sub);
                int const row_id = row >= 0 ? tree->components[row].component_id : -1;
                mismatches += row < 0;
                mismatches += UITree_FindByComponentId(tree, row_id) != row;
                mismatches += UITree_FindByComponentId(tree, row_id) != find_linear(tree, row_id);
                mismatches += UITree_FindByComponentId(tree, parent_id) != box;
                /* Ids held from earlier rows and rounds: some name live rows,
                 * some name rows since replaced, some have been handed out
                 * again. Whatever they are, the answer is the scan's. */
                for( int i = 0; i < 8; i++ )
                    mismatches +=
                        UITree_FindByComponentId(tree, held[i]) != find_linear(tree, held[i]);
                held[(round * 48 + sub) % 8] = row_id;
            }
            check_all_ids(tree, "container rebuild round");
            if( round == 3 )
            {
                UITree_CcDeleteAll(tree, box);
                for( int i = 0; i < 8; i++ )
                    mismatches +=
                        UITree_FindByComponentId(tree, held[i]) != find_linear(tree, held[i]);
            }
        }
        TEST_ASSERT(mismatches == 0, "the last answer agrees with the scan through a rebuild");
    }
    UITree_Free(tree);
}

/* Two shortcuts sit beside the id map and must never change an answer:
 * cc_find hands the lookup the dynamic child it found (UITree_NoteDynamicChild),
 * and cc_create's allocator skips asking the map about a low id no live node
 * has. Each is exact only under a condition; these are the cases on both
 * sides of it. */
void
test_id_shortcuts(void)
{
    printf("TEST: id lookup shortcuts\n");

    /* Counted across the whole test: a tree that built its id map, its count
     * table, its pools and its string cells must give every block back when it
     * is freed. (The 64 KB count table was once freed by a checkpoint's
     * teardown and not by the tree's own.) */
    intptr_t const blocks_before = ToriMem_Ui()->live_blocks;

    int const iface = 161;
    int const other = 300;
    int const parent_id = (iface << 16) | 50;
    struct UITree* tree = UITree_New(8);
    TEST_ASSERT(tree != NULL, "UITree_New");
    int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 100, 100);
    int32_t far = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, (other << 16) | 1, 0, 0, 100, 100);
    TEST_ASSERT(parent >= 0 && far >= 0, "push two interfaces");

    /* A static node already holds a low id in the dynamic range, in this
     * interface and in another one. The allocator must not hand this
     * interface's out; the other interface's is no obstacle. */
    int const taken = (iface << 16) | 0x8003;
    int32_t squatter = UITree_TestPushXy(tree, parent, UIELEM_RS_RECT, taken, 0, 0, 5, 5);
    int32_t elsewhere = UITree_TestPushXy(tree, far, UIELEM_RS_RECT, (other << 16) | 0x8001, 0, 0, 5, 5);
    TEST_ASSERT(squatter >= 0 && elsewhere >= 0, "push the two squatters");
    TEST_ASSERT(UITree_FindByComponentId(tree, taken) == squatter, "map is built");

    int clash = 0;
    int mismatch = 0;
    int32_t rows[12];
    for( int sub = 0; sub < 12; sub++ )
    {
        rows[sub] = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, sub);
        int const id = rows[sub] >= 0 ? tree->components[rows[sub]].component_id : -1;
        clash += id == taken;
        mismatch += rows[sub] < 0 || UITree_FindByComponentId(tree, id) != rows[sub];
        for( int earlier = 0; earlier < sub; earlier++ )
            clash += tree->components[rows[earlier]].component_id == id;
    }
    TEST_ASSERT(clash == 0, "no dynamic child was given an id a live node holds");
    TEST_ASSERT(mismatch == 0, "every new child is found by its id");
    TEST_ASSERT(UITree_FindByComponentId(tree, taken) == squatter, "the squatter still answers for its id");
    check_all_ids(tree, "after allocation around a squatter");

    /* Rows replaced over and over: the ids released are free again, the ids
     * in use are not, and the map agrees with the scan throughout. */
    for( int round = 0; round < 5; round++ )
    {
        for( int sub = 0; sub < 12; sub++ )
        {
            int32_t r = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, sub);
            int const id = r >= 0 ? tree->components[r].component_id : -1;
            mismatch += r < 0 || UITree_FindByComponentId(tree, id) != find_linear(tree, id);
            clash += id == taken;
        }
        check_all_ids(tree, "rows replaced");
    }
    TEST_ASSERT(clash == 0 && mismatch == 0, "replacement keeps ids unique and found");

    /* cc_find's note: after it, the child is found, and it is the scan's answer. */
    {
        int32_t child = UITree_FindChildBySubid(tree, parent, parent_id, 7);
        TEST_ASSERT(child >= 0, "find a row by sub-id");
        int const id = tree->components[child].component_id;
        (void)UITree_FindByComponentId(tree, parent_id); /* something else was asked last */
        UITree_NoteDynamicChild(tree, child);
        TEST_ASSERT(UITree_FindByComponentId(tree, id) == child, "noted child is found");
        TEST_ASSERT(find_linear(tree, id) == child, "and is the scan's answer");
        UITree_NoteDynamicChild(tree, -1);
        UITree_NoteDynamicChild(tree, squatter); /* not dynamic: ignored */
        TEST_ASSERT(UITree_FindByComponentId(tree, taken) == squatter, "a static node is not noted");
    }

    /* Two dynamic nodes with one id: the lower index answers, and noting the
     * other must not change that. */
    {
        int const dup = (iface << 16) | 0x9100;
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_RECT;
        spec.component_id = dup;
        spec.dynamic = 1;
        spec.dynamic_child_index = 500;
        int32_t first = UITree_Push(tree, parent, &spec);
        spec.dynamic_child_index = 501;
        int32_t second = UITree_Push(tree, parent, &spec);
        TEST_ASSERT(first >= 0 && second >= 0 && first != second, "push two dynamic nodes with one id");
        int32_t const winner = find_linear(tree, dup);
        int32_t const loser = winner == first ? second : first;
        TEST_ASSERT(UITree_FindByComponentId(tree, dup) == winner, "the scan's winner answers");
        UITree_NoteDynamicChild(tree, loser);
        TEST_ASSERT(UITree_FindByComponentId(tree, dup) == winner, "noting the other node changes nothing");
        /* Nor does noting an unrelated dynamic child while a duplicate exists. */
        int32_t child = UITree_FindChildBySubid(tree, parent, parent_id, 3);
        UITree_NoteDynamicChild(tree, child);
        TEST_ASSERT(
            child >= 0 && UITree_FindByComponentId(tree, tree->components[child].component_id) == child,
            "an unrelated child is still found");
        check_all_ids(tree, "with a dynamic duplicate");
    }
    /* Text, options and handlers too, some released into the pools, some live. */
    {
        int const args[2] = { 1, 2 };
        for( int sub = 100; sub < 140; sub++ )
        {
            int32_t r = UITree_CcCreate(tree, parent, parent_id, 4, sub);
            (void)UITree_SetTextAt(tree, r, "label");
            UITree_StringSetC(&UITree_MenuOptionsMutIn(tree, &tree->components[r])->ops[0], "Use");
            UITree_HookSetIn(
                &tree->hook_argv_pool, &UITree_HooksMutIn(tree, &tree->components[r])->on_click, 9, args, 2, 0, NULL, 0);
        }
        for( int sub = 100; sub < 120; sub++ )
            (void)UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, sub);
        struct UITree* cp = UITree_CheckpointTake(tree);
        UITree_CheckpointRestore(tree, cp);
        UITree_CheckpointFree(cp);
        (void)UITree_FindByComponentId(tree, parent_id); /* the restored tree rebuilds its map */
    }
    TEST_ASSERT(tree->id_high_counts != NULL, "the tree has its id count table");
    UITree_Free(tree);
    TEST_ASSERT(ToriMem_Ui()->live_blocks == blocks_before, "a freed tree leaves no block behind");
}

/* A reclaimed node's options and handler blocks go to the tree's pool and
 * come back, zeroed, for the next node that needs one. What must hold: a
 * recycled block carries nothing of the node that had it; the pool is bounded;
 * and a tree torn down with blocks pooled frees them (the leak audit and ASan
 * see that half). Under ASan nothing is pooled, so the counts are checked
 * only where free lists are on. */
void
test_block_pool(void)
{
    printf("TEST: released-block pool\n");

    int const iface = 161;
    int const parent_id = (iface << 16) | 40;
    int const pooling = UITREE_POOLS_ON;
    int const args[3] = { 7, 8, 9 };
    char const* const strs[1] = { "label" };
    struct UITree* tree = UITree_New(8);
    TEST_ASSERT(tree != NULL, "UITree_New");

    int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, parent_id, 0, 0, 100, 100);
    TEST_ASSERT(parent >= 0, "push parent");
    TEST_ASSERT(tree->menu_options_pool.count == 0 && tree->hooks_pool.count == 0, "pools start empty");
    TEST_ASSERT(tree->hook_argv_pool.count == 0, "argument pool starts empty");

    /* Row 0: options with a submenu, and a handler with int and string
     * arguments. */
    int32_t row = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, 0);
    TEST_ASSERT(row >= 0, "create row");
    TEST_ASSERT(!UITree_HasMenuOptions(&tree->components[row]), "a new row has no options block");
    struct UITreeMenuOptions* opts = UITree_MenuOptionsMutIn(tree, &tree->components[row]);
    TEST_ASSERT(opts != NULL, "options block on first write");
    UITree_StringSetC(&opts->ops[0], "Withdraw-1");
    UITree_StringSetC(&opts->option, "Bank");
    TEST_ASSERT(UITree_MenuSubmenuSetEntry(opts, 2, 3, "Withdraw-10", strlen("Withdraw-10")), "set a submenu entry");
    struct UITreeRuntimeHooks* hooks = UITree_HooksMutIn(tree, &tree->components[row]);
    TEST_ASSERT(hooks != NULL, "handler block on first write");
    UITree_HookSet(&hooks->on_click, 1234, args, 3, 0, strs, 1);
    TEST_ASSERT(hooks->on_click.script_id == 1234, "handler stored");

    /* Replace the row: its blocks are released. */
    int32_t row2 = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, 0);
    TEST_ASSERT(row2 >= 0, "replace row");
    TEST_ASSERT(!UITree_HasMenuOptions(&tree->components[row2]), "the replacement has no options block");
    TEST_ASSERT(tree->components[row2].runtime_hooks == NULL, "the replacement has no handler block");
    if( pooling )
    {
        TEST_ASSERT(tree->menu_options_pool.count == 1, "the options block was pooled");
        TEST_ASSERT(tree->hooks_pool.count == 1, "the handler block was pooled");
        TEST_ASSERT(tree->hook_argv_pool.count == 1, "the handler's argument list was pooled");
    }

    /* The recycled blocks say nothing. */
    struct UITreeMenuOptions* opts2 = UITree_MenuOptionsMutIn(tree, &tree->components[row2]);
    struct UITreeRuntimeHooks* hooks2 = UITree_HooksMutIn(tree, &tree->components[row2]);
    if( pooling )
    {
        TEST_ASSERT(opts2 == opts, "the options block came back from the pool");
        TEST_ASSERT(hooks2 == hooks, "the handler block came back from the pool");
        TEST_ASSERT(tree->menu_options_pool.count == 0 && tree->hooks_pool.count == 0, "pools emptied");
    }
    {
        struct UITreeMenuOptions zero_opts;
        struct UITreeRuntimeHooks zero_hooks;
        memset(&zero_opts, 0, sizeof(zero_opts));
        memset(&zero_hooks, 0, sizeof(zero_hooks));
        /* Empty as a block of UITree_Strings is empty: zero lengths and no
         * actions; the bytes past a zero length are not part of a string. */
        int empty = UITree_StringEmpty(&opts2->option) && UITree_StringEmpty(&opts2->target_verb) &&
                    UITree_StringEmpty(&opts2->target_base) && opts2->option_action == 0 &&
                    opts2->submenus == NULL;
        for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
            empty = empty && UITree_StringEmpty(&opts2->ops[i]) && opts2->ops[i].text[0] == '\0' &&
                    opts2->op_actions[i] == 0;
        (void)zero_opts;
        TEST_ASSERT(empty, "recycled options block is empty");
        TEST_ASSERT(memcmp(hooks2, &zero_hooks, sizeof(zero_hooks)) == 0, "recycled handler block is zeroed");
    }
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts2, 2, 3)[0] == '\0', "no submenu survives");

    /* A released argument list serves a handler with a different count, and
     * carries none of the old values past the new count. */
    {
        int const one[1] = { 42 };
        int const five[5] = { 1, 2, 3, 4, 5 };
        int many[UITREE_HOOK_ARGV_SMALL + 3];
        for( int i = 0; i < UITREE_HOOK_ARGV_SMALL + 3; i++ )
            many[i] = 100 + i;
        UITree_HookSetIn(&tree->hook_argv_pool, &hooks2->on_click, 500, one, 1, 0, NULL, 0);
        if( pooling )
            TEST_ASSERT(tree->hook_argv_pool.count == 0, "the argument list came back from the pool");
        TEST_ASSERT(hooks2->on_click.argc == 1 && UITree_HookArg(&hooks2->on_click, 0) == 42, "one argument reads back");
        TEST_ASSERT(hooks2->on_click.argv[1] == 0 && hooks2->on_click.argv[2] == 0, "the recycled list is zeroed past the count");
        UITree_HookSetIn(&tree->hook_argv_pool, &hooks2->on_click, 501, five, 5, 0, NULL, 0);
        TEST_ASSERT(hooks2->on_click.argc == 5 && UITree_HookArg(&hooks2->on_click, 4) == 5, "five arguments read back in the same slot");
        if( pooling )
            TEST_ASSERT(tree->hook_argv_pool.count == 0, "replacing a handler reuses its own list");
        /* A long list is exact and is never pooled. */
        UITree_HookSetIn(&tree->hook_argv_pool, &hooks2->on_timer, 502, many, UITREE_HOOK_ARGV_SMALL + 3, 0, NULL, 0);
        TEST_ASSERT(UITree_HookArg(&hooks2->on_timer, UITREE_HOOK_ARGV_SMALL + 2) == 100 + UITREE_HOOK_ARGV_SMALL + 2, "a long list reads back");
        UITree_HookClearTo(&tree->hook_argv_pool, &hooks2->on_timer);
        TEST_ASSERT(tree->hook_argv_pool.count == 0, "a long list is freed, not pooled");
        /* Set without a pool, cleared into one: the sizes agree. */
        UITree_HookSet(&hooks2->on_timer, 503, five, 5, 0, NULL, 0);
        UITree_HookClearTo(&tree->hook_argv_pool, &hooks2->on_timer);
        if( pooling )
            TEST_ASSERT(tree->hook_argv_pool.count == 1, "a list set without a pool can be pooled");
    }

    /* The second time through the same accessor returns the same block. */
    TEST_ASSERT(UITree_MenuOptionsMutIn(tree, &tree->components[row2]) == opts2, "accessor is stable");
    TEST_ASSERT(UITree_HooksMutIn(tree, &tree->components[row2]) == hooks2, "handler accessor is stable");

    /* Short text lives in the tree's string pool; a long string does not. A
     * recycled cell carries nothing of the last string. */
    {
        /* 4 is the cache's text widget type (cc_create's own numbering). */
        int32_t label = UITree_CcCreate(tree, parent, parent_id, 4, 900);
        TEST_ASSERT(label >= 0 && tree->components[label].type == UIELEM_RS_TEXT, "create a text row");
        TEST_ASSERT(UITree_SetTextAt(tree, label, "Withdraw-All-but-one"), "set short text");
        char const* const first = tree->components[label].u.rs_text.text;
        TEST_ASSERT(strcmp(first, "Withdraw-All-but-one") == 0, "short text reads back");
        if( pooling )
            TEST_ASSERT(tree->string_pool.live == 1, "the short string is in a cell");
        TEST_ASSERT(UITree_SetTextAt(tree, label, "Hi"), "replace with shorter text");
        TEST_ASSERT(strcmp(tree->components[label].u.rs_text.text, "Hi") == 0, "shorter text reads back");
        if( pooling )
            TEST_ASSERT(tree->string_pool.live == 1, "the replaced string's cell was released");
        TEST_ASSERT(
            UITree_SetTextAt(tree, label, "A label that is certainly longer than the short size"),
            "set long text");
        TEST_ASSERT(
            strcmp(tree->components[label].u.rs_text.text,
                "A label that is certainly longer than the short size") == 0,
            "long text reads back");
        TEST_ASSERT(tree->string_pool.live == 0, "a long string is not in a cell");
        TEST_ASSERT(UITree_SetTextAt(tree, label, "Ok"), "back to short text");
        if( pooling )
        {
            char const* const again = tree->components[label].u.rs_text.text;
            TEST_ASSERT(tree->string_pool.live == 1, "short again: one cell in use");
            TEST_ASSERT(again[3] == '\0' && again[20] == '\0', "a recycled cell is zeroed past its text");
        }
        /* Thirty-one characters is the longest short string; thirty-two is long. */
        TEST_ASSERT(UITree_SetTextAt(tree, label, "1234567890123456789012345678901"), "31 characters");
        TEST_ASSERT(strlen(tree->components[label].u.rs_text.text) == 31, "31 characters read back");
        if( pooling )
            TEST_ASSERT(tree->string_pool.live == 1, "31 characters fit a cell");
        TEST_ASSERT(UITree_SetTextAt(tree, label, "12345678901234567890123456789012"), "32 characters");
        TEST_ASSERT(strlen(tree->components[label].u.rs_text.text) == 32, "32 characters read back");
        TEST_ASSERT(tree->string_pool.live == 0, "32 characters do not");

        /* More strings than one chunk holds, then all released. */
        {
            int const rows = (int)UITREE_STRING_CHUNK_CELLS + 50;
            int bad = 0;
            for( int sub = 0; sub < rows; sub++ )
            {
                char want[24];
                int32_t r = UITree_CcCreate(tree, parent, parent_id, 4, 1000 + sub);
                snprintf(want, sizeof(want), "row %d", sub);
                bad += r < 0 || !UITree_SetTextAt(tree, r, want);
            }
            for( int sub = 0; sub < rows; sub++ )
            {
                char want[24];
                int32_t r = UITree_FindChildBySubid(tree, parent, parent_id, 1000 + sub);
                snprintf(want, sizeof(want), "row %d", sub);
                bad += r < 0 || strcmp(tree->components[r].u.rs_text.text, want) != 0;
            }
            TEST_ASSERT(bad == 0, "every string across two chunks reads back");
            if( pooling )
                TEST_ASSERT(tree->string_pool.live == (uint32_t)rows, "one cell a row");
            UITree_CcDeleteAll(tree, parent);
            TEST_ASSERT(tree->string_pool.live == 0, "every cell released with its row");
        }
    }

    /* The pool is bounded: more rows than it holds, all released at once. */
    {
        int const rows = (int)UITREE_BLOCK_POOL_MAX + 40;
        for( int sub = 0; sub < rows; sub++ )
        {
            int32_t r = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, sub);
            TEST_ASSERT(r >= 0, "create many rows");
            UITree_StringSetC(&UITree_MenuOptionsMutIn(tree, &tree->components[r])->ops[1], "Examine");
            UITree_HookSet(&UITree_HooksMutIn(tree, &tree->components[r])->on_timer, 77, args, 2, 0, NULL, 0);
        }
        UITree_CcDeleteAll(tree, parent);
        TEST_ASSERT(tree->menu_options_pool.count <= UITREE_BLOCK_POOL_MAX, "options pool is bounded");
        TEST_ASSERT(tree->hooks_pool.count <= UITREE_BLOCK_POOL_MAX, "handler pool is bounded");
        if( pooling )
        {
            TEST_ASSERT(tree->menu_options_pool.count == UITREE_BLOCK_POOL_MAX, "options pool filled");
            TEST_ASSERT(tree->hooks_pool.count == UITREE_BLOCK_POOL_MAX, "handler pool filled");
        }
    }

    /* A checkpoint's copy does not share the live tree's pooled blocks, and a
     * string that was in a cell survives the round trip. */
    {
        int32_t kept = UITree_CcCreate(tree, parent, parent_id, 4, 77);
        TEST_ASSERT(kept >= 0 && UITree_SetTextAt(tree, kept, "kept label"), "a short string before the checkpoint");
        struct UITree* cp = UITree_CheckpointTake(tree);
        TEST_ASSERT(cp != NULL, "checkpoint");
        TEST_ASSERT(cp->menu_options_pool.count == 0 && cp->menu_options_pool.head == NULL, "copy has an empty options pool");
        TEST_ASSERT(cp->hooks_pool.count == 0 && cp->hooks_pool.head == NULL, "copy has an empty handler pool");
        TEST_ASSERT(cp->hook_argv_pool.count == 0 && cp->hook_argv_pool.head == NULL, "copy has an empty argument pool");
        TEST_ASSERT(cp->string_pool.chunks == NULL && cp->string_pool.live == 0, "copy has an empty string pool");
        UITree_CheckpointRestore(tree, cp);
        TEST_ASSERT(tree->menu_options_pool.count == 0 && tree->hooks_pool.count == 0, "a restored tree starts its pools again");
        TEST_ASSERT(tree->string_pool.chunks == NULL && tree->string_pool.live == 0, "a restored tree starts its string pool again");
        kept = UITree_FindChildBySubid(tree, parent, parent_id, 77);
        TEST_ASSERT(
            kept >= 0 && strcmp(tree->components[kept].u.rs_text.text, "kept label") == 0,
            "the string reads back after the restore");
        TEST_ASSERT(UITree_SetTextAt(tree, kept, "changed"), "and can be replaced");
        TEST_ASSERT(strcmp(tree->components[kept].u.rs_text.text, "changed") == 0, "the replacement reads back");
        UITree_CheckpointFree(cp);
    }

    /* Torn down with blocks pooled. */
    {
        int32_t r = UITree_CcCreate(tree, parent, parent_id, UIELEM_RS_RECT, 0);
        (void)UITree_MenuOptionsMutIn(tree, &tree->components[r]);
        (void)UITree_HooksMutIn(tree, &tree->components[r]);
        UITree_CcDeleteAll(tree, parent);
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

    TEST_ASSERT(UITree_MenuSubmenuSetEntry(opts, 2, 3, "Withdraw-10", strlen("Withdraw-10")), "set entry");
    TEST_ASSERT(opts->submenus != NULL, "block allocated on first write");
    TEST_ASSERT(strcmp(UITree_MenuSubmenuEntry(opts, 2, 3), "Withdraw-10") == 0, "entry reads back");
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts, 2, 4)[0] == '\0', "neighbour entry still empty");
    TEST_ASSERT(!UITree_MenuSubmenuSetEntry(opts, 0, 1, "x", strlen("x")), "op index 0 rejected");
    TEST_ASSERT(
        !UITree_MenuSubmenuSetEntry(opts, UITREE_SUBMENU_OP_SLOTS + 1, 1, "x", strlen("x")),
        "op index past the end rejected");

    /* CC_COPY must give the copy its own block. */
    int32_t copy = UITree_CcCopy(tree, parent, parent_id, 0, 1);
    TEST_ASSERT(copy >= 0, "cc_copy row");
    {
        struct UITreeMenuOptions* dst = UITree_MenuOptionsMut(&tree->components[copy]);
        TEST_ASSERT(dst->submenus != NULL, "copy carries the submenus");
        TEST_ASSERT(dst->submenus != UITree_MenuOptions(&tree->components[row])->submenus, "copy is not an alias");
        TEST_ASSERT(strcmp(UITree_MenuSubmenuEntry(dst, 2, 3), "Withdraw-10") == 0, "copied entry");
        TEST_ASSERT(UITree_MenuSubmenuSetEntry(dst, 2, 3, "Withdraw-X", strlen("Withdraw-X")), "write to the copy");
        TEST_ASSERT(
            strcmp(UITree_MenuSubmenuEntry(UITree_MenuOptions(&tree->components[row]), 2, 3),
                   "Withdraw-10") == 0,
            "source unchanged by the copy's write");
    }

    /* Clearing one op keeps the block; clearing all of them drops it. */
    UITree_MenuSubmenuClear(opts, 2);
    TEST_ASSERT(UITree_MenuSubmenuEntry(opts, 2, 3)[0] == '\0', "cleared op entry");
    UITree_MenuSubmenuSetEntry(opts, 1, 1, "Ok", strlen("Ok"));
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
