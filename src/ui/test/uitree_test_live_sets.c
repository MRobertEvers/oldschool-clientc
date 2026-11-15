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
            set_has(&tree->opkeys, (int32_t)i) == (UITree_OpKeys(c)->has_bindings != 0),
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
    uint32_t dirty_before = tree->dirty_gen;
    UITree_SetBehavior(tree, a, &behavior);
    TEST_ASSERT(tree->client_code.count == 1, "client_code set after SetBehavior");
    TEST_ASSERT(tree->dirty_gen != dirty_before, "SetBehavior invalidates emit identity");
    behavior.client_code = 0;
    UITree_SetBehavior(tree, a, &behavior);
    TEST_ASSERT(tree->client_code.count == 0, "client_code cleared");
    {
        uint32_t const visibility_identity_before = tree->dirty_gen;
        behavior.hide = 1;
        UITree_SetBehavior(tree, a, &behavior);
        TEST_ASSERT(
            tree->dirty_gen != visibility_identity_before,
            "SetBehavior hide invalidates reachability identity");
        behavior.hide = 0;
        UITree_SetBehavior(tree, a, &behavior);
    }

    /* World singleton. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_WORLD;
    spec.component_id = (60 << 16) | 1;
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

/* The timer pass runs in the reference walk's order (UITree_SortNodesPreorder).
 * The shape is toplevel_osm's: safezone holds gameframe (whose side panel
 * mounts the stats tab and its tooltip layer) and, after it, mouseover. The
 * tooltip's timer stamps a varc that mouseover's timer reads the same cycle,
 * so the tooltip has to come first even though mouseover was made first. */
void
test_timer_preorder(void)
{
    struct UITree* tree = UITree_New(0);
    struct UITreeNodeSpec spec;
    int32_t control, safezone, gameframe, mouseover, side, stats, tooltip, cc;
    int32_t order[6];

    printf("TEST: timer pre-order\n");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.width = 100;
    spec.height = 100;
    spec.component_id = (601 << 16) | 0;
    control = UITree_Push(tree, -1, &spec);
    spec.component_id = (601 << 16) | 19;
    safezone = UITree_Push(tree, control, &spec);
    spec.component_id = (601 << 16) | 20;
    gameframe = UITree_Push(tree, safezone, &spec);
    spec.component_id = (601 << 16) | 132;
    mouseover = UITree_Push(tree, safezone, &spec);
    /* Pushed after mouseover, so a later slot: slot order would put it last. */
    spec.component_id = (601 << 16) | 116;
    side = UITree_Push(tree, gameframe, &spec);
    spec.component_id = (320 << 16) | 0;
    stats = UITree_Push(tree, side, &spec);
    spec.component_id = (320 << 16) | 1;
    tooltip = UITree_Push(tree, stats, &spec);
    cc = UITree_CcCreate(tree, mouseover, (601 << 16) | 132, 6, 0);
    TEST_ASSERT(control >= 0 && tooltip > mouseover && cc >= 0, "tree built");

    order[0] = cc;
    order[1] = mouseover;
    order[2] = tooltip;
    order[3] = safezone;
    order[4] = stats;
    order[5] = gameframe;
    UITree_SortNodesPreorder(tree, order, 6);
    TEST_ASSERT(order[0] == safezone, "an ancestor comes before its subtree");
    TEST_ASSERT(order[1] == gameframe, "earlier sibling's subtree first");
    TEST_ASSERT(order[2] == stats, "mounted root before its children");
    TEST_ASSERT(order[3] == tooltip, "stats tooltip before mouseover");
    TEST_ASSERT(order[4] == mouseover, "mouseover after the gameframe subtree");
    TEST_ASSERT(order[5] == cc, "a cc child after its parent");

    /* The cached order the timer pass reads. Armed mouseover-first, the way
     * boot arms the toplevel before a tap arms the tooltip. */
    {
        int32_t const armed[3] = { mouseover, tooltip, gameframe };
        int32_t const* walk = NULL;
        int n;
        int32_t leaf;
        for( int k = 0; k < 3; k++ )
        {
            UITree_HooksMut(&tree->components[armed[k]])->on_timer.script_id = 42;
            UITree_SyncHookMembership(tree, armed[k]);
        }
        n = UITree_TimerWalkOrder(tree, &walk);
        TEST_ASSERT(n == 3, "three timers");
        TEST_ASSERT(
            walk[0] == gameframe && walk[1] == tooltip && walk[2] == mouseover,
            "timer pass in walk order");
        TEST_ASSERT(!tree->timer_walk_order_stale, "sorted once");

        /* Re-arming the same timer -- every cycle -- is not a change. */
        UITree_SyncHookMembership(tree, tooltip);
        TEST_ASSERT(!tree->timer_walk_order_stale, "re-arm keeps the order");

        /* The overlay's per-cycle rebuild: a fresh leaf under mouseover. */
        UITree_CcDeleteAll(tree, mouseover);
        leaf = UITree_CcCreate(tree, mouseover, (601 << 16) | 132, 6, 1);
        TEST_ASSERT(leaf >= 0, "leaf created");
        TEST_ASSERT(!tree->timer_walk_order_stale, "a fresh leaf keeps the order");

        /* Moves. Under the side panel mouseover follows stats, so this one
         * re-sorts to the same order; the two after it change it. */
        UITree_Reparent(tree, mouseover, side);
        TEST_ASSERT(tree->timer_walk_order_stale, "a move marks it stale");
        n = UITree_TimerWalkOrder(tree, &walk);
        TEST_ASSERT(
            n == 3 && walk[0] == gameframe && walk[1] == tooltip && walk[2] == mouseover,
            "moved timer re-sorted after the earlier sibling's subtree");
        UITree_Reparent(tree, mouseover, gameframe);
        UITree_Reparent(tree, side, safezone);
        n = UITree_TimerWalkOrder(tree, &walk);
        TEST_ASSERT(
            n == 3 && walk[0] == gameframe && walk[1] == mouseover && walk[2] == tooltip,
            "a moved subtree carries its timers");

        /* Clearing one drops it from the order. */
        UITree_HooksMut(&tree->components[tooltip])->on_timer.script_id = 0;
        UITree_SyncHookMembership(tree, tooltip);
        n = UITree_TimerWalkOrder(tree, &walk);
        TEST_ASSERT(n == 2 && walk[0] == gameframe && walk[1] == mouseover, "cleared timer gone");
    }

    UITree_Free(tree);
}
