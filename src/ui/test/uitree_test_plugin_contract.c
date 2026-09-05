#include "test_harness.h"
#include "uitree_interact.h"

/* Contract conformance cases use the production tree mutation/getter paths.
 * The revision harness remains the end-to-end acceptance gate in M3/M6. */
void test_plugin_contract_native(void)
{
    struct UITree* tree = UITree_New(16);
    int root = UITree_TestPushXy(tree,-1,UIELEM_RS_LAYER,0x120000,0,0,100,100);
    int node = UITree_TestPushXy(tree,root,UIELEM_RS_GRAPHIC,0x120001,0,0,20,20);
    UITree_SetHideAt(tree,node,1);
    UITree_ApplyObject(tree,0x120001,995,5,1,0,0);
    TEST_ASSERT(tree->components[node].native_hide, "content update cannot clear native hiding");
    UITree_SetSizeModesAt(tree,node,100,100,1,1);
    UITree_LayoutResolve(tree,0,0,100,100);
    TEST_ASSERT(tree->components[node].position.abs_w == 0, "fixture resolves to zero width");
    TEST_ASSERT(UITree_GetLayoutWidth(tree,0x120001) == 0, "computed zero is not replaced by requested width");
    TEST_ASSERT(UITree_GetLayoutHeight(tree,0x120001) == 0, "computed zero height readback");
    int field = UITree_CcCreate(tree,root,0x120000,12,4);
    int id = tree->components[field].component_id;
    uint64_t incarnation = tree->components[field].incarnation;
    struct UITreeNodeRef ref = UITree_RefAt(tree, field);
    TEST_ASSERT(UITree_ResolveRef(tree, ref) == field, "live reference resolves");
    struct UITree* other = UITree_New(1);
    for( int i = 0; i <= field; ++i )
        UITree_TestPushXy(other,-1,UIELEM_RS_TEXT,id+i,0,0,10,10);
    TEST_ASSERT(other->components[field].incarnation == ref.incarnation,
                "cross-tree fixture has matching slot and incarnation");
    TEST_ASSERT(UITree_ResolveRef(other, ref) < 0, "reference cannot cross tree instances");
    UITree_Free(other);
    UITree_InputSetFocusId(tree,id);
    UITree_StageDragPickup(tree, field, 3, 4);
    UITree_CcDelete(tree,field);
    TEST_ASSERT(UITree_ResolveRef(tree, ref) < 0, "deleted reference is stale");
    tree->next_incarnation = UINT32_MAX; /* exercise widening past the old wrap boundary */
    tree->next_dynamic_uid = (uint16_t)(id & 0xffff); /* exercise the allocator's reuse boundary */
    int replacement = UITree_CcCreate(tree,root,0x120000,12,4);
    TEST_ASSERT(tree->components[replacement].component_id == id, "native id was recycled");
    TEST_ASSERT(tree->components[replacement].incarnation > UINT32_MAX, "incarnation survives 32-bit boundary");
    TEST_ASSERT(tree->components[replacement].incarnation != incarnation, "replacement is another incarnation");
    TEST_ASSERT(UITree_InputFocusId(tree) < 0, "old focus cannot move to a recycled component id");
    tree->components[replacement].drag_render_area_uid = 0x120000;
    struct UIInteraction interact;
    UIInteraction_Init(&interact);
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);
    struct UIInteractOut out;
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);
    TEST_ASSERT(UITree_InteractConsumePendingDragPickup(&interact, tree, &host, input, &out) == 0,
                "queued pickup cannot target recycled component id");
    TEST_ASSERT(!interact.input_state.drag_active && !tree->pending_drag_pickup,
                "stale pickup is cancelled");
    TEST_ASSERT(UITree_ResolveRef(tree, ref) < 0, "reused reference stays stale");
    UITree_InputSetFocusId(tree,id);
    TEST_ASSERT(UITree_InputFocusId(tree) == id, "explicit focus can acquire the replacement");
    uint32_t generation = tree->generation;
    TEST_ASSERT(!UITree_Reparent(tree, root, field), "reject descendant cycle");
    TEST_ASSERT(!UITree_Reparent(tree, root, root), "reject self cycle");
    TEST_ASSERT(tree->generation == generation && tree->components[root].parent == -1,
                "rejected reparent has no partial topology mutation");
    struct UITreeNodeRef replacement_ref = UITree_RefAt(tree, replacement);
    TEST_ASSERT(UITree_Reparent(tree, replacement, -1), "move subtree to root");
    TEST_ASSERT(UITree_ResolveRef(tree, replacement_ref) == replacement,
                "native reparent preserves identity");
    TEST_ASSERT(UITree_InputFocusId(tree) == id, "valid native reparent preserves focus");

    UITree_SetMountHiddenAt(tree, root, 1);
    TEST_ASSERT(!UITree_ComponentVisibleById(&tree->components[root], 0x120000),
                "mount suppression cannot be escaped by hover");
    TEST_ASSERT(!UITree_NodeNativeVisible(tree, NULL, node, -1), "unmounted ancestor blocks paint");
    TEST_ASSERT(!UITree_NodeNativeInputPresent(tree, NULL, node), "unmounted ancestor blocks input");
    UITree_SetHideAt(tree, root, 1);
    UITree_SetMountHiddenAt(tree, root, 0);
    TEST_ASSERT(tree->components[root].native_hide, "mount cannot clear later server hide");
    TEST_ASSERT(!UITree_NodeNativeVisible(tree, NULL, root, -1), "mounted native-hidden root stays hidden");
    UITree_SetMountHiddenAt(tree, root, 1);
    UITree_SetHideAt(tree, root, 0);
    TEST_ASSERT(!UITree_NodeNativeVisible(tree, NULL, root, -1), "native unhide cannot mount a group");
    struct UITreeBehavior behavior = {0};
    UITree_SetBehavior(tree, root, &behavior);
    TEST_ASSERT(tree->components[root].mount_hidden, "behavior replacement does not acquire mount authority");
    UITree_SetMountHiddenAt(tree, root, 0);
    TEST_ASSERT(UITree_NodeNativeVisible(tree, NULL, root, -1), "mount exposes current native visibility");
    UITree_Free(tree);
}
