#include "test_harness.h"

void
test_walk_topology(void)
{
    printf("TEST: walk topology / prune\n");

    struct UITree* tree = UITree_New(32);
    TEST_ASSERT(tree != NULL, "UITree_New");

    /* Root A with two children; Root B sibling forest */
    int32_t a = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 1, 0, 0, 100, 100);
    int32_t a0 = UITree_TestPushXy(tree, a, UIELEM_RS_RECT, 2, 0, 0, 10, 10);
    int32_t a1 = UITree_TestPushXy(tree, a, UIELEM_RS_RECT, 3, 20, 0, 10, 10);
    int32_t b = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 4, 200, 0, 50, 50);
    int32_t b0 = UITree_TestPushXy(tree, b, UIELEM_RS_RECT, 5, 0, 0, 10, 10);

    TEST_ASSERT(tree->root_index == a, "first root is A");
    TEST_ASSERT(tree->components[a].next_sibling == b, "A next sibling B");
    TEST_ASSERT(tree->components[a].first_child == a0, "A first child a0");
    TEST_ASSERT(tree->components[a0].next_sibling == a1, "a0 next sibling a1");
    TEST_ASSERT(tree->components[a1].next_sibling < 0, "a1 last sibling");
    TEST_ASSERT(tree->components[a0].parent == a && tree->components[a1].parent == a, "children parent A");
    TEST_ASSERT(tree->components[b].first_child == b0, "B first child b0");

    /* Full preorder with visible=true */
    {
        int32_t stack[UITREE_WALK_STACK_MAX];
        int stack_top = -1;
        int32_t cur = tree->root_index;
        int32_t order[16];
        int n = 0;
        while( cur >= 0 && n < 16 )
        {
            order[n++] = cur;
            UITree_WalkAdvance(tree, &cur, stack, &stack_top, UITREE_WALK_STACK_MAX, true);
        }
        TEST_ASSERT(n == 5, "full walk visits 5");
        TEST_ASSERT(order[0] == a && order[1] == a0 && order[2] == a1, "preorder A subtree");
        TEST_ASSERT(order[3] == b && order[4] == b0, "preorder B subtree");
    }

    /* Prune: invisible A skips a0/a1 but still advances to B */
    {
        int32_t stack[UITREE_WALK_STACK_MAX];
        int stack_top = -1;
        int32_t cur = tree->root_index;
        int32_t order[16];
        int n = 0;
        while( cur >= 0 && n < 16 )
        {
            order[n++] = cur;
            /* When leaving A, pass current_visible=false so children are skipped */
            if( cur == a )
                UITree_WalkAdvance(tree, &cur, stack, &stack_top, UITREE_WALK_STACK_MAX, false);
            else
                UITree_WalkAdvance(tree, &cur, stack, &stack_top, UITREE_WALK_STACK_MAX, true);
        }
        TEST_ASSERT(n == 3, "prune A skips children (visit A,B,b0)");
        TEST_ASSERT(order[0] == a && order[1] == b && order[2] == b0, "prune order A then B subtree");
        int saw_a0 = 0;
        int saw_a1 = 0;
        for( int i = 0; i < n; i++ )
        {
            if( order[i] == a0 )
                saw_a0 = 1;
            if( order[i] == a1 )
                saw_a1 = 1;
        }
        TEST_ASSERT(!saw_a0 && !saw_a1, "pruned children never visited");
    }

    /* Sidebar inactive: do not descend when tab mismatches */
    {
        struct UITree* st = UITree_New(8);
        struct TestHostState hs;
        struct UITreeHost host;
        UITree_TestHostInit(&host, &hs);
        hs.selected_tab = 2;

        struct UITreeNodeSpec side;
        memset(&side, 0, sizeof(side));
        side.type = UIELEM_BUILTIN_SIDEBAR;
        side.component_id = -1;
        side.width = 190;
        side.height = 261;
        side.u.sidebar.tabno = 0; /* inactive */
        int32_t si = UITree_Push(st, -1, &side);
        int32_t child = UITree_TestPushXy(st, si, UIELEM_RS_RECT, 99, 0, 0, 20, 20);

        int32_t stack[UITREE_WALK_STACK_MAX];
        int stack_top = -1;
        int32_t cur = st->root_index;
        int visited_child = 0;
        while( cur >= 0 )
        {
            if( cur == child )
                visited_child = 1;
            bool descend = true;
            if( st->components[cur].type == UIELEM_BUILTIN_SIDEBAR )
            {
                struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
                if( UITree_Host(&host, &req) != st->components[cur].u.sidebar.tabno )
                    descend = false;
            }
            UITree_WalkAdvance(st, &cur, stack, &stack_top, UITREE_WALK_STACK_MAX, descend);
        }
        TEST_ASSERT(!visited_child, "inactive sidebar children not walked");
        UITree_Free(st);
    }

    /* FindByComponentId prefers dynamic */
    {
        int32_t parent = a;
        int32_t dyn = UITree_CcCreate(tree, parent, 1, 3, 7);
        TEST_ASSERT(dyn >= 0, "cc_create");
        TEST_ASSERT(tree->components[dyn].dynamic == 1, "dynamic flag");
        int32_t found = UITree_FindByComponentId(tree, tree->components[dyn].component_id);
        TEST_ASSERT(found == dyn, "find prefers dynamic uid");
    }

    /* InterfaceParent mounts emit LAST under their container, even when the
     * container gains children after the mount was linked (reference widgets-gl
     * renders mounted interface roots on top of the container's own children). */
    {
        int const container_uid = (500 << 16) | 0;
        int const mount_uid = (600 << 16) | 0;
        int const late_uid = (500 << 16) | 9;
        struct TestHostState hs;
        struct UITreeHost host;
        struct UITreeEmitBuffer buf;
        int mount_at = -1;
        int late_at = -1;
        struct UITree* mt = UITree_New(16);
        int32_t container =
            UITree_TestPushXy(mt, -1, UIELEM_RS_LAYER, container_uid, 0, 0, 200, 200);

        /* Mount linked first, then an ordinary child appended after it. */
        (void)UITree_TestPushXy(mt, container, UIELEM_RS_RECT, mount_uid, 0, 0, 50, 50);
        (void)UITree_InterfaceParentSet(mt, container_uid, 600, 1);
        (void)UITree_TestPushXy(mt, container, UIELEM_RS_RECT, late_uid, 0, 0, 50, 50);

        UITree_TestHostInit(&host, &hs);
        UITree_TestResolve(mt);
        UITree_EmitBufferInit(&buf);
        UITree_EmitWalk(mt, &host, &buf, -1);
        for( int i = 0; i < buf.count; i++ )
        {
            if( buf.cmds[i].component_id == mount_uid )
                mount_at = i;
            else if( buf.cmds[i].component_id == late_uid )
                late_at = i;
        }
        TEST_ASSERT(mount_at >= 0 && late_at >= 0, "mount and late child both emit");
        TEST_ASSERT(mount_at > late_at, "mounted interface root emits after sibling children");

        UITree_EmitBufferFree(&buf);
        UITree_Free(mt);
    }

    UITree_Free(tree);
}
