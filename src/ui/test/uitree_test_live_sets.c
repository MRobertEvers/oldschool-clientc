#include "test_harness.h"

#include <stdlib.h>

static int
ancestor_in_group(
    struct UITree const* tree,
    int32_t idx,
    int group_id)
{
    while( idx >= 0 && (uint32_t)idx < tree->component_count )
    {
        struct UITreeComponent const* c = &tree->components[idx];
        if( !c->freed && c->component_id >= 0 &&
            ((c->component_id >> 16) & 0xffff) == group_id )
            return 1;
        idx = c->parent;
    }
    return 0;
}

static int
group_set_covers_node(
    struct UITree const* tree,
    int group_id,
    int32_t want)
{
    struct UITreeNodeSet const* gset = UITree_GroupNodes(tree, group_id);
    int gi;
    if( !gset )
        return 0;
    for( gi = 0; gi < gset->count; gi++ )
    {
        int32_t stack[128];
        int sp = 0;
        stack[sp++] = gset->slots[gi];
        while( sp > 0 )
        {
            int32_t cur = stack[--sp];
            int32_t child;
            if( cur == want )
                return 1;
            for( child = tree->components[cur].first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                if( sp < (int)(sizeof(stack) / sizeof(stack[0])) )
                    stack[sp++] = child;
            }
        }
    }
    return 0;
}

static int
set_has(struct UITreeNodeSet const* set, int32_t slot)
{
    int si;
    for( si = 0; si < set->count; si++ )
        if( set->slots[si] == slot )
            return 1;
    return 0;
}

static void
oracle_verify(struct UITree const* tree)
{
    uint32_t i;
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        TEST_ASSERT(
            set_has(&tree->models, (int32_t)i) == (c->type == UIELEM_RS_MODEL),
            "models set");
        TEST_ASSERT(
            set_has(&tree->scroll_layers, (int32_t)i) == (c->type == UIELEM_RS_LAYER),
            "scroll_layers set");
        TEST_ASSERT(
            set_has(&tree->client_code, (int32_t)i) == (c->behavior.client_code > 0),
            "client_code set");
        TEST_ASSERT(
            set_has(&tree->timer_hooks, (int32_t)i) ==
                (c->runtime_hooks && c->runtime_hooks->on_timer.script_id > 0),
            "timer_hooks set");
        TEST_ASSERT(
            set_has(&tree->resize_hooks, (int32_t)i) ==
                (c->runtime_hooks && c->runtime_hooks->on_resize.script_id > 0),
            "resize_hooks set");
        TEST_ASSERT(
            set_has(&tree->opkeys, (int32_t)i) == (c->op_keys.has_bindings != 0),
            "opkeys set");
        if( c->component_id >= 0 )
        {
            int group = (c->component_id >> 16) & 0xffff;
            struct UITreeNodeSet const* gset = UITree_GroupNodes(tree, group);
            TEST_ASSERT(gset != NULL, "group bucket exists");
            TEST_ASSERT(
                gset && set_has(gset, (int32_t)i),
                "group set contains own-id node");
        }
        if( c->type == UIELEM_BUILTIN_WORLD )
            TEST_ASSERT(tree->world_index == (int32_t)i, "world singleton");
        if( c->type == UIELEM_BUILTIN_WORLDMAP )
            TEST_ASSERT(tree->worldmap_index == (int32_t)i, "worldmap singleton");
    }
}

void
test_live_node_sets(void)
{
    struct UITree* tree = UITree_New(0);
    struct UITreeNodeSpec spec;
    struct UITreeBehavior behavior;
    int32_t root;
    int32_t a;
    int32_t b;
    int32_t world;
    int32_t idless;
    int round;

    printf("TEST: live node sets\n");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (50 << 16) | 0;
    spec.width = 100;
    spec.height = 100;
    root = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(root >= 0, "root");
    TEST_ASSERT(tree->scroll_layers.count == 1, "root layer in scroll set");

    /* Duplicate component_id: both model slots must appear in the live set. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_MODEL;
    spec.component_id = (50 << 16) | 7;
    a = UITree_Push(tree, root, &spec);
    b = UITree_Push(tree, root, &spec);
    TEST_ASSERT(a >= 0 && b >= 0 && a != b, "two model pushes");
    TEST_ASSERT(tree->models.count == 2, "duplicate-id models both listed");
    TEST_ASSERT(
        set_has(&tree->models, a) && set_has(&tree->models, b),
        "slot iteration visits both duplicate ids");

    /* client_code membership via SetBehavior. */
    memset(&behavior, 0, sizeof(behavior));
    behavior.client_code = 328;
    behavior.over_layer_id = -1;
    UITree_SetBehavior(tree, a, &behavior);
    TEST_ASSERT(tree->client_code.count == 1, "client_code set after SetBehavior");
    behavior.client_code = 0;
    UITree_SetBehavior(tree, a, &behavior);
    TEST_ASSERT(tree->client_code.count == 0, "client_code cleared");

    /* World singleton. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_WORLD;
    spec.component_id = (0x7FFE << 16) | 1;
    world = UITree_Push(tree, -1, &spec);
    TEST_ASSERT(tree->world_index == world, "world_index set on push");

    /* Id-less child under group 50. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_RECT;
    spec.component_id = -1;
    idless = UITree_Push(tree, root, &spec);
    TEST_ASSERT(idless >= 0, "id-less child");
    TEST_ASSERT(ancestor_in_group(tree, idless, 50), "ancestor predicate");
    TEST_ASSERT(
        group_set_covers_node(tree, 50, idless),
        "group-set subtree covers id-less descendant");

    /* Randomized push / reclaim / rehook vs oracle. */
    for( round = 0; round < 40; round++ )
    {
        int action = round % 5;
        if( action == 0 )
        {
            memset(&spec, 0, sizeof(spec));
            spec.type = (round & 1) ? UIELEM_RS_MODEL : UIELEM_RS_RECT;
            spec.component_id = (50 << 16) | (100 + round);
            (void)UITree_Push(tree, root, &spec);
        }
        else if( action == 1 )
        {
            int32_t idx = UITree_CcCreate(tree, root, (50 << 16) | 0, 6, round);
            if( idx >= 0 )
            {
                int chars[1] = { 'a' };
                int codes[1] = { 1 };
                (void)UITree_ApplyOpKey(tree, tree->components[idx].component_id, 1, chars, codes, 1);
            }
        }
        else if( action == 2 )
        {
            int32_t idx = UITree_CcCreate(tree, root, (50 << 16) | 0, 6, round + 50);
            if( idx >= 0 )
            {
                struct UITreeRuntimeHooks* hooks =
                    UITree_HooksMut(&tree->components[idx]);
                hooks->on_timer.script_id = 42;
                hooks->on_resize.script_id = 43;
                UITree_SyncHookMembership(tree, idx);
            }
        }
        else if( action == 3 )
        {
            UITree_CcDeleteAll(tree, root);
        }
        else
        {
            while( tree->timer_hooks.count > 0 )
                UITree_FreeHooksAt(tree, tree->timer_hooks.slots[0]);
        }
        oracle_verify(tree);
    }

    TEST_ASSERT(!tree->components[root].freed, "root survives deleteall");
    TEST_ASSERT(tree->world_index == world, "world singleton survives");

    UITree_Clear(tree);
    TEST_ASSERT(tree->models.count == 0, "clear empties models");
    TEST_ASSERT(tree->world_index < 0, "clear clears world singleton");
    TEST_ASSERT(!UITree_GroupPresent(tree, 50), "clear drops groups");

    UITree_Free(tree);
}
