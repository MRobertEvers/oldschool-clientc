/*
 * When the plugin bridge may reuse what it remembered about the gameframe.
 *
 * Each memo is reused only while every counter it was worked out against is
 * unchanged, and an answer of "no such role" or "no such node" is kept the
 * same way a found one is. Reuse past a moved counter stamps or reports a
 * node that is no longer the one a role names; dropping a memo whose keys
 * have not moved puts a whole-tree resolution back on every frame.
 */

#include "plugin/plugin_frame_binding.h"

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

/* Only its address is used, as the identity of "the tree the pass ran on". */
static char g_tree_a;
static char g_tree_b;
#define TREE_A ((struct UITree const*)(void const*)&g_tree_a)
#define TREE_B ((struct UITree const*)(void const*)&g_tree_b)

static void
fresh(struct PluginFrameBinding* b)
{
    memset(b, 0, sizeof(*b));
}

static void
test_slot_nodes_are_kept_for_one_generation(void)
{
    struct PluginFrameBinding b;

    fresh(&b);
    TEST_ASSERT(
        !PluginFrameBinding_SlotNodesCurrent(&b, 7), "nothing is current for a generation never resolved");

    for( int i = 0; i < TORIRS_HOST_SURFACE_PLACEABLE_COUNT; i++ )
        PluginFrameBinding_SetSlotNode(&b, i, i == 2 ? -1 : 100 + i);
    PluginFrameBinding_SlotNodesResolved(&b, 7);

    TEST_ASSERT(PluginFrameBinding_SlotNodesCurrent(&b, 7), "the generation they were resolved at reuses them");
    TEST_ASSERT(PluginFrameBinding_SlotNode(&b, 0) == 100, "and answers the stored node");
    TEST_ASSERT(PluginFrameBinding_SlotNode(&b, 2) == -1, "a slot with no node is remembered as -1");
    TEST_ASSERT(!PluginFrameBinding_SlotNodesCurrent(&b, 8), "a moved generation must resolve again");
}

static void
test_the_pass_is_skipped_only_while_every_key_holds(void)
{
    struct PluginFrameBinding b;

    fresh(&b);
    TEST_ASSERT(
        !PluginFrameBinding_BoundCurrent(&b, TREE_A, 0, 0, 0), "a binder that never ran has no pass to reuse");

    PluginFrameBinding_RoleIdsInterned(&b, 40);
    PluginFrameBinding_MarkBound(&b, TREE_A, 3, 9);
    TEST_ASSERT(PluginFrameBinding_BoundCurrent(&b, TREE_A, 3, 9, 40), "same tree, generations and roles");

    TEST_ASSERT(!PluginFrameBinding_BoundCurrent(&b, TREE_B, 3, 9, 40), "another tree");
    TEST_ASSERT(!PluginFrameBinding_BoundCurrent(&b, TREE_A, 4, 9, 40), "a rebuilt tree");
    TEST_ASSERT(!PluginFrameBinding_BoundCurrent(&b, TREE_A, 3, 10, 40), "a pushed or recycled node");
    TEST_ASSERT(!PluginFrameBinding_BoundCurrent(&b, TREE_A, 3, 9, 41), "a role table that grew");

    PluginFrameBinding_DropBound(&b);
    TEST_ASSERT(
        !PluginFrameBinding_BoundCurrent(&b, TREE_A, 3, 9, 40), "a dropped pass does not count whatever its keys");
}

static void
test_role_ids_are_interned_per_table_size(void)
{
    struct PluginFrameBinding b;

    fresh(&b);
    PluginFrameBinding_RoleIdsInterned(&b, 5);
    TEST_ASSERT(PluginFrameBinding_RoleIdsCurrent(&b, 5), "interned for this table size");
    TEST_ASSERT(!PluginFrameBinding_RoleIdsCurrent(&b, 6), "a grown table must intern again");

    PluginFrameBinding_SetRoleId(&b, 4, 0, 11);
    PluginFrameBinding_SetRoleId(&b, 4, 1 + 15, 27);
    TEST_ASSERT(PluginFrameBinding_RoleIds(&b, 4)[0] == 11, "the group id");
    TEST_ASSERT(PluginFrameBinding_RoleIds(&b, 4)[16] == 27, "the last member id");

    PluginFrameBinding_ClearRoleIds(&b, 4);
    for( int i = 0; i < 1 + PLUGIN_FRAME_BINDING_MEMBERS; i++ )
        TEST_ASSERT(PluginFrameBinding_RoleIds(&b, 4)[i] == 0, "a cleared slot names no role");

    PluginFrameBinding_SetCompassClickRole(&b, 33);
    TEST_ASSERT(PluginFrameBinding_CompassClickRole(&b) == 33, "the compass click role is kept");
}

static void
test_a_new_stamp_list_replaces_the_last(void)
{
    struct PluginFrameBinding b;
    struct PluginFrameStamp s;

    fresh(&b);
    PluginFrameBinding_PushStamp(&b, 10, 1);
    PluginFrameBinding_PushStamp(&b, 20, 2);
    TEST_ASSERT(PluginFrameBinding_StampCount(&b) == 2, "two stamps kept");
    s = PluginFrameBinding_Stamp(&b, 1);
    TEST_ASSERT(s.node == 20 && s.incarnation == 2, "a stamp keeps its node and incarnation");

    PluginFrameBinding_ClearStamps(&b);
    PluginFrameBinding_PushStamp(&b, 30, 5);
    TEST_ASSERT(PluginFrameBinding_StampCount(&b) == 1, "the next pass's list replaces the last");
    s = PluginFrameBinding_Stamp(&b, 0);
    TEST_ASSERT(s.node == 30 && s.incarnation == 5, "and holds only its own");

    PluginFrameBinding_ClearStamps(&b);
    for( int i = 0; i < PLUGIN_FRAME_STAMP_MAX; i++ )
        PluginFrameBinding_PushStamp(&b, i, (uint64_t)i);
    TEST_ASSERT(PluginFrameBinding_StampCount(&b) == PLUGIN_FRAME_STAMP_MAX, "a full pass fits");
}

static void
test_facet_ids_are_kept_per_table_sizes(void)
{
    struct PluginFrameBinding b;
    struct PluginFacetIds ids;

    fresh(&b);
    TEST_ASSERT(!PluginFrameBinding_FacetIdsCurrent(&b, 0, 0), "never resolved is never current");

    memset(&ids, 0, sizeof(ids));
    ids.varbit_cutscene = -1;
    ids.varp_run_mode = 173;
    ids.role_orb_run = 9;
    PluginFrameBinding_StoreFacetIds(&b, 12, 40, &ids);

    TEST_ASSERT(PluginFrameBinding_FacetIdsCurrent(&b, 12, 40), "same two table sizes reuse them");
    TEST_ASSERT(!PluginFrameBinding_FacetIdsCurrent(&b, 13, 40), "a grown ref table must resolve again");
    TEST_ASSERT(!PluginFrameBinding_FacetIdsCurrent(&b, 12, 41), "a grown role table must resolve again");
    TEST_ASSERT(PluginFrameBinding_FacetIds(&b)->varbit_cutscene == -1, "a missing var is remembered as missing");
    TEST_ASSERT(PluginFrameBinding_FacetIds(&b)->varp_run_mode == 173, "a found one as found");
    TEST_ASSERT(PluginFrameBinding_FacetIds(&b)->role_orb_run == 9, "and the orb role");
}

static void
test_stone_nodes_follow_the_tree_and_the_role_ids(void)
{
    struct PluginFrameBinding b;
    struct PluginFacetIds ids;

    fresh(&b);
    TEST_ASSERT(!PluginFrameBinding_SidetabNodesCurrent(&b, 0, 0), "never resolved is never current");

    PluginFrameBinding_BeginSidetabNodes(&b, 3, 9);
    for( int tab = 0; tab < PLUGIN_FRAME_BINDING_MEMBERS; tab++ )
        PluginFrameBinding_SetSidetabNode(&b, tab, tab == 5 ? 500 : -1);
    TEST_ASSERT(PluginFrameBinding_SidetabNodesCurrent(&b, 3, 9), "current at the counters they were found at");
    TEST_ASSERT(!PluginFrameBinding_SidetabNodesCurrent(&b, 4, 9), "a rebuilt tree finds them again");
    TEST_ASSERT(!PluginFrameBinding_SidetabNodesCurrent(&b, 3, 10), "so does a recycled id");
    TEST_ASSERT(PluginFrameBinding_SidetabOf(&b, 500) == 5, "a node that is a stone answers its tab");
    TEST_ASSERT(PluginFrameBinding_SidetabOf(&b, 501) == -1, "any other node is no stone");

    memset(&ids, 0, sizeof(ids));
    PluginFrameBinding_StoreFacetIds(&b, 1, 1, &ids);
    TEST_ASSERT(
        !PluginFrameBinding_SidetabNodesCurrent(&b, 3, 9),
        "re-resolved role ids drop the stones found through the old ones");
}

static void
test_invalidate_drops_what_names_the_tree(void)
{
    struct PluginFrameBinding b;
    struct PluginFacetIds ids;

    fresh(&b);
    memset(&ids, 0, sizeof(ids));
    PluginFrameBinding_StoreFacetIds(&b, 12, 40, &ids);
    PluginFrameBinding_RoleIdsInterned(&b, 40);
    PluginFrameBinding_MarkBound(&b, TREE_A, 3, 9);
    PluginFrameBinding_BeginSidetabNodes(&b, 3, 9);
    PluginFrameBinding_SlotNodesResolved(&b, 3);

    PluginFrameBinding_Invalidate(&b);
    TEST_ASSERT(!PluginFrameBinding_BoundCurrent(&b, TREE_A, 3, 9, 40), "the binder's pass is gone");
    TEST_ASSERT(b.bound_tree == NULL, "and so is the tree it ran on");
    TEST_ASSERT(!PluginFrameBinding_SidetabNodesCurrent(&b, 3, 9), "the stones are gone");
    TEST_ASSERT(!PluginFrameBinding_SlotNodesCurrent(&b, 3), "the slot nodes are gone");
    TEST_ASSERT(PluginFrameBinding_FacetIdsCurrent(&b, 12, 40), "the facet ids are about tables, and stay");
    TEST_ASSERT(PluginFrameBinding_RoleIdsCurrent(&b, 40), "so are the interned role ids");
}

int
main(void)
{
    test_slot_nodes_are_kept_for_one_generation();
    test_the_pass_is_skipped_only_while_every_key_holds();
    test_role_ids_are_interned_per_table_size();
    test_a_new_stamp_list_replaces_the_last();
    test_facet_ids_are_kept_per_table_sizes();
    test_stone_nodes_follow_the_tree_and_the_role_ids();
    test_invalidate_drops_what_names_the_tree();

    if( g_failures )
    {
        printf("plugin_frame_binding: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("plugin_frame_binding: all checks passed\n");
    return 0;
}
