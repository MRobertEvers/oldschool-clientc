/*
 * A mount's layout: what the mount flag does and does not cost, and what a
 * reader sees right after one.
 *
 * `mount_hidden` is not a layout input (layout_compute_node never reads it),
 * so flipping it must not unresolve the tree. It used to: every group mount
 * and every interface open re-laid out every node, two full passes per group.
 * These pin that a flip recomputes nothing, that the boxes after a mount are
 * exactly what a from-scratch layout of the same tree gives, and that a
 * script's read between two mounts sees current values.
 */
#include "test_harness.h"

#include <stdlib.h>

#define ML_ROOT_ID 0x000a0000
#define ML_TARGET_ID 0x000b0003

struct MountLayoutFixture
{
    int32_t top;    /* the toplevel the groups mount into */
    int32_t target; /* the mount target inside it */
    int32_t group_a;
    int32_t a_inner;
    int32_t a_leaf;
    int32_t group_b;
    int32_t b_leaf;
};

/* A toplevel with a mount target, and two groups baked at the root, hidden
 * as a fresh bake is (Task_GroupMount), with children that lay out against
 * their parents through every mode kind the layout has. */
static void
ml_build(struct UITree* tree, struct MountLayoutFixture* f)
{
    f->top = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x000b0000, 0, 0, 765, 503);
    f->target = UITree_TestPushXy(tree, f->top, UIELEM_RS_LAYER, ML_TARGET_ID, 40, 30, 500, 400);

    f->group_a = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, ML_ROOT_ID, 0, 0, 0, 0);
    UITree_SetSizeModesAt(tree, f->group_a, 0, 0, 1, 1);
    f->a_inner = UITree_TestPushXy(tree, f->group_a, UIELEM_RS_LAYER, ML_ROOT_ID | 1, 12, 8, 30, 40);
    UITree_SetSizeModesAt(tree, f->a_inner, 30, 40, 1, 1);
    UITree_SetPositionModesAt(tree, f->a_inner, 12, 8, 1, 2);
    f->a_leaf = UITree_TestPushXy(tree, f->a_inner, UIELEM_RS_RECT, ML_ROOT_ID | 2, 4, 6, 50, 20);
    UITree_SetPositionModesAt(tree, f->a_leaf, 4, 6, 2, 1);

    f->group_b = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x000c0000, 0, 0, 200, 100);
    f->b_leaf = UITree_TestPushXy(tree, f->group_b, UIELEM_RS_RECT, 0x000c0001, 10, 10, 0, 0);
    UITree_SetSizeModesAt(tree, f->b_leaf, 10, 10, 1, 1);

    UITree_SetMountHiddenAt(tree, f->group_a, 1);
    UITree_SetMountHiddenAt(tree, f->group_b, 1);
    UITree_TestResolve(tree);
}

/* Every live node's box, in slot order. */
static int*
ml_boxes(struct UITree const* tree)
{
    int* out = calloc((size_t)tree->component_count * 5u, sizeof(int));
    assert(out);
    /* tree-walk-exempt: test snapshot */
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeElemPosition const* p = &tree->components[i].position;
        out[i * 5 + 0] = p->abs_x;
        out[i * 5 + 1] = p->abs_y;
        out[i * 5 + 2] = p->abs_w;
        out[i * 5 + 3] = p->abs_h;
        out[i * 5 + 4] = p->layout_resolved;
    }
    return out;
}

/* The boxes as they stand equal the boxes of a from-scratch layout: every
 * node unresolved, the whole tree recomputed. That is what each mount used to
 * do, so it is the value every reader saw before. */
static int
ml_matches_full_layout(struct UITree* tree)
{
    int* now = ml_boxes(tree);
    int* full;
    int same;

    UITree_LayoutInvalidate(tree);
    UITree_TestResolve(tree);
    full = ml_boxes(tree);
    same = memcmp(now, full, (size_t)tree->component_count * 5u * sizeof(int)) == 0;
    free(now);
    free(full);
    return same;
}

/* The mount bookkeeping: unhide, then move under the target (as
 * InterfaceOpen_MountGroupUnder does). */
static void
ml_mount(struct UITree* tree, int32_t group_root, int32_t target)
{
    UITree_SetMountHiddenAt(tree, group_root, 0);
    UITree_Reparent(tree, group_root, target);
}

void
test_mount_layout(void)
{
    printf("TEST: mount layout (a mount flag is not a layout input)\n");

    /* Flipping the flag unresolves nothing, and the pass it asks for computes
     * nothing: a box poisoned behind the layout's back survives it. Before,
     * every node was unresolved and the poison was overwritten. */
    {
        struct UITree* tree = UITree_New(16);
        struct MountLayoutFixture f;
        uint32_t seq;
        int leaf_x;

        ml_build(tree, &f);
        leaf_x = tree->components[f.a_leaf].position.abs_x;

        tree->components[f.b_leaf].position.abs_x = -12345;
        seq = tree->layout_resolve_seq;
        UITree_SetMountHiddenAt(tree, f.group_a, 0);
        UITree_SetMountHiddenAt(tree, f.group_b, 0);
        UITree_SetMountHiddenAt(tree, f.group_a, 1);

        TEST_ASSERT(tree->layout_stale, "a mount flip still asks for a pass");
        TEST_ASSERT(!tree->layout_dirty_overflow, "a mount flip does not ask for a sweep");
        TEST_ASSERT(tree->layout_dirty_count == 0, "a mount flip names no node");
        /* tree-walk-exempt: test check */
        for( uint32_t i = 0; i < tree->component_count; i++ )
            TEST_ASSERT(tree->components[i].position.layout_resolved, "a mount flip unresolves no box");

        UITree_TestResolve(tree);
        TEST_ASSERT(tree->layout_resolve_seq == seq + 1u, "three flips, one pass");
        TEST_ASSERT(!tree->layout_stale, "the pass ran");
        TEST_ASSERT(tree->components[f.b_leaf].position.abs_x == -12345, "the pass computed no node");
        TEST_ASSERT(tree->components[f.a_leaf].position.abs_x == leaf_x, "boxes unchanged");

        UITree_TestResolve(tree);
        TEST_ASSERT(tree->layout_resolve_seq == seq + 1u, "nothing pending: the next resolve skips");
        UITree_Free(tree);
    }

    /* A mount followed at once by a read: the targeted read is current, and
     * after the mount step's resolve every box is what a from-scratch layout
     * of the same tree gives. */
    {
        struct UITree* tree = UITree_New(16);
        struct MountLayoutFixture f;
        int x;
        int w;

        ml_build(tree, &f);
        ml_mount(tree, f.group_a, f.target);
        x = UITree_GetRelativeX(tree, ML_ROOT_ID | 2);
        w = UITree_GetLayoutWidth(tree, ML_ROOT_ID | 1);
        /* group_a fills the 500x400 target; a_inner is 30 less wide, centred on
         * x (mode 1/2); the leaf sits mode 2 from its right edge. */
        TEST_ASSERT(w == 470, "inner width against the mount target");
        TEST_ASSERT(x == (470 - 4 - 50), "leaf x against the mount target");
        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_GetRelativeX(tree, ML_ROOT_ID | 2) == x, "resolve agrees with the chain read");
        TEST_ASSERT(ml_matches_full_layout(tree), "mount + resolve == from-scratch layout");
        UITree_Free(tree);
    }

    /* A script's read between two mounts sees current values: a size set
     * after the first mount is read back before the second, and both groups
     * are right after it. */
    {
        struct UITree* tree = UITree_New(16);
        struct MountLayoutFixture f;

        ml_build(tree, &f);
        ml_mount(tree, f.group_a, f.target);
        UITree_TestResolve(tree);
        UITree_SetSizeAt(tree, f.target, 300, 200);
        TEST_ASSERT(UITree_GetLayoutWidth(tree, ML_ROOT_ID | 1) == 270, "read after a set, before the next mount");
        ml_mount(tree, f.group_b, f.group_a);
        TEST_ASSERT(UITree_GetLayoutWidth(tree, ML_ROOT_ID | 1) == 270, "the second mount keeps the first current");
        TEST_ASSERT(UITree_GetLayoutWidth(tree, 0x000c0001) == 200 - 10, "the second group lays out against the first");
        UITree_TestResolve(tree);
        TEST_ASSERT(ml_matches_full_layout(tree), "two mounts + resolve == from-scratch layout");

        /* And unmounting (hiding at the root again, as the spillover sweep
         * does) changes no box either. */
        UITree_SetMountHiddenAt(tree, f.group_b, 1);
        UITree_TestResolve(tree);
        TEST_ASSERT(ml_matches_full_layout(tree), "a hide + resolve == from-scratch layout");
        UITree_Free(tree);
    }
}
