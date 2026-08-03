#include "test_harness.h"

#include <stdlib.h>

static uint32_t
count_hook_blocks(struct UITree const* tree)
{
    uint32_t n = 0;
    for( uint32_t i = 0; i < tree->component_count; i++ )
        if( !tree->components[i].freed && tree->components[i].runtime_hooks )
            n++;
    return n;
}

static int
free_list_len(struct UITree const* tree)
{
    int n = 0;
    for( int32_t i = tree->free_head; i >= 0; i = tree->components[i].free_next )
        n++;
    return n;
}

/* Simulate IF_CLOSESUB's hook teardown: free every runtime_hooks block in
 * group. Remount's onLoad reallocates them; closed panels must not retain them. */
static void
free_hooks_for_group(struct UITree* tree, int group_id)
{
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        if( c->freed || !c->runtime_hooks )
            continue;
        if( ((c->component_id >> 16) & 0xffff) != group_id )
            continue;
        UITree_HooksFree(c);
    }
    tree->hook_index_stale = 1;
}

void
test_open_close_steady(void)
{
    struct UITree* tree = UITree_New(0);
    struct UITreeNodeSpec root_spec;
    struct UITreeNodeSpec panel_spec;
    struct UITreeNodeSpec leaf_spec;
    int32_t root;
    int32_t panel;
    uint32_t baseline_count;
    uint32_t baseline_hooks;
    int cycle;

    printf("TEST: open/close steady-state\n");

    memset(&root_spec, 0, sizeof(root_spec));
    root_spec.type = UIELEM_RS_LAYER;
    root_spec.component_id = (161 << 16) | 0;
    root_spec.width = 765;
    root_spec.height = 503;
    root = UITree_Push(tree, -1, &root_spec);
    TEST_ASSERT(root >= 0, "root push");

    memset(&panel_spec, 0, sizeof(panel_spec));
    panel_spec.type = UIELEM_RS_LAYER;
    panel_spec.component_id = (12 << 16) | 0;
    panel_spec.width = 400;
    panel_spec.height = 300;
    panel = UITree_Push(tree, root, &panel_spec);
    TEST_ASSERT(panel >= 0, "panel push");

    /* Mix of models (anim index) and hooked leaves (hook-block reclaim). */
    for( int i = 0; i < 40; i++ )
    {
        int32_t idx;
        memset(&leaf_spec, 0, sizeof(leaf_spec));
        leaf_spec.type = (i % 4 == 0) ? UIELEM_RS_MODEL : UIELEM_RS_RECT;
        leaf_spec.component_id = (12 << 16) | (i + 1);
        leaf_spec.width = 16;
        leaf_spec.height = 16;
        idx = UITree_Push(tree, panel, &leaf_spec);
        TEST_ASSERT(idx >= 0, "leaf push");
        if( i % 2 == 0 )
        {
            struct UITreeRuntimeHooks* hooks = UITree_HooksMut(&tree->components[idx]);
            hooks->on_timer.script_id = 1000 + i;
            hooks->on_click.script_id = 2000 + i;
            tree->hook_index_stale = 1;
        }
    }

    {
        int models = UITree_EnsureModelIndex(tree);
        int timers = UITree_EnsureHookIndexes(tree);
        TEST_ASSERT(models == 10, "model index counts only RS_MODEL nodes");
        TEST_ASSERT(timers == 20, "timer index from hooked leaves");
        TEST_ASSERT(tree->model_node_count == 10, "model_node_count");
        /* Anim walk must not stride the full 42-node array. */
        TEST_ASSERT(
            tree->model_node_count < (int)tree->component_count,
            "model index smaller than component_count");
    }

    baseline_count = tree->component_count;
    baseline_hooks = count_hook_blocks(tree);
    TEST_ASSERT(baseline_hooks == 20, "hook blocks allocated");

    /* Open/close cycle: free hooks on "close", re-allocate on "open". */
    for( cycle = 0; cycle < 8; cycle++ )
    {
        free_hooks_for_group(tree, 12);
        TEST_ASSERT(count_hook_blocks(tree) == 0, "close frees all group hook blocks");

        for( uint32_t i = 0; i < tree->component_count; i++ )
        {
            struct UITreeComponent* c = &tree->components[i];
            if( c->freed || ((c->component_id >> 16) & 0xffff) != 12 )
                continue;
            if( (c->component_id & 0xffff) == 0 )
                continue;
            if( ((c->component_id & 0xffff) - 1) % 2 != 0 )
                continue;
            {
                struct UITreeRuntimeHooks* hooks = UITree_HooksMut(c);
                hooks->on_timer.script_id = 1000;
                hooks->on_click.script_id = 2000;
            }
        }
        tree->hook_index_stale = 1;
        TEST_ASSERT(
            count_hook_blocks(tree) == baseline_hooks,
            "remount restores hook-block count");
        TEST_ASSERT(
            tree->component_count == baseline_count,
            "hide-reuse keeps component_count flat across open/close");
    }

    /* CC_DELETEALL + CC_CREATE rebuild: free-list recycles, count stays flat. */
    {
        uint32_t after_rebuild;
        uint32_t before_dyn;
        int free_after_delete;
        for( int i = 0; i < 40; i++ )
        {
            int32_t idx = UITree_CcCreate(tree, panel, (12 << 16) | 0, 3, i);
            TEST_ASSERT(idx >= 0, "seed dynamic children");
        }
        before_dyn = tree->component_count;
        UITree_CcDeleteAll(tree, panel);
        free_after_delete = free_list_len(tree);
        TEST_ASSERT(free_after_delete >= 40, "deleteall puts dynamics on free-list");
        for( int i = 0; i < 40; i++ )
        {
            int32_t idx = UITree_CcCreate(tree, panel, (12 << 16) | 0, 3, i);
            TEST_ASSERT(idx >= 0, "cc_create after deleteall");
        }
        after_rebuild = tree->component_count;
        TEST_ASSERT(
            after_rebuild == before_dyn,
            "cc_deleteall+cc_create does not grow component_count");
        TEST_ASSERT(free_list_len(tree) == free_after_delete - 40, "free-list consumed by create");
    }

    /* Prove the hook-free assertion can fail: leave blocks allocated and the
     * closed-state count must be non-zero (documents the invariant). */
    {
        struct UITreeRuntimeHooks* hooks =
            UITree_HooksMut(&tree->components[panel]);
        hooks->on_timer.script_id = 1;
        TEST_ASSERT(count_hook_blocks(tree) >= 1, "mutation can leave a hook block");
        free_hooks_for_group(tree, 12);
        TEST_ASSERT(count_hook_blocks(tree) == 0, "and close clears it again");
    }

    UITree_Free(tree);
}
