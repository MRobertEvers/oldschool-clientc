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
    struct UITree* replacement_tree = UITree_New(16);
    int other_root = UITree_TestPushXy(replacement_tree, -1, UIELEM_RS_LAYER, 0x120000, 0, 0, 100, 100);
    int other_node = UITree_TestPushXy(replacement_tree, other_root, UIELEM_RS_GRAPHIC, 0x120001, 0, 0, 20, 20);
    TEST_ASSERT(!UITree_SetReplacementHidden(replacement_tree, other_node,
                tree->components[node].incarnation, 1), "retained incarnation cannot affect another tree");
    UITree_Free(replacement_tree);
    uint64_t incarnation = tree->components[field].incarnation;
    struct UITreeNodeRef ref = UITree_RefAt(tree, field);
    TEST_ASSERT(UITree_ResolveRef(tree, ref) == field, "live reference resolves");
    struct UITree* other = UITree_New(1);
    for( int i = 0; i <= field; ++i )
        UITree_TestPushXy(other,-1,UIELEM_RS_TEXT,id+i,0,0,10,10);
    /* Even a malformed reference with a colliding nonce cannot cross trees. */
    other->components[field].incarnation = ref.incarnation;
    TEST_ASSERT(UITree_ResolveRef(other, ref) < 0, "reference cannot cross tree instances");
    UITree_Free(other);
    UITree_InputSetFocusId(tree,id);
    UITree_StageDragPickup(tree, field, 3, 4);
    UITree_CcDelete(tree,field);
    TEST_ASSERT(UITree_ResolveRef(tree, ref) < 0, "deleted reference is stale");
    tree->next_dynamic_uid = (uint16_t)(id & 0xffff); /* exercise the allocator's reuse boundary */
    int replacement = UITree_CcCreate(tree,root,0x120000,12,4);
    /* Exercise wide tokens without billions of allocations. */
    tree->components[replacement].incarnation |= UINT64_C(1) << 32;
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

void test_plugin_contract_copy(void)
{
    struct UITree* tree = UITree_New(8);
    int parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x220000, 0, 0, 100, 100);
    int source = UITree_CcCreate(tree, parent, 0x220000, 0, 0);
    int source_id = tree->components[source].component_id;
    int script_ops[] = {1, 2, 3};
    int* scripts[] = {script_ops};
    int lengths[] = {3};
    struct UITreeBehavior behavior = {.scripts_count=1, .scripts=scripts, .scripts_lengths=lengths};
    UITree_SetBehavior(tree, source, &behavior);
    UITree_SetHideAt(tree, source, 1);
    UITree_ApplyClickMask(tree, source_id, 0x123456);
    UITree_ApplyColour(tree, source_id, 0x125678);
    UITree_ApplyFillColour(tree, source_id, 0xabcdef);
    UITree_ApplyText(tree, source_id, "live source");
    UITree_ApplyComponentParam(tree, source_id, 100, 42, NULL);
    UITree_ApplyComponentParam(tree, source_id, 101, 0, "source parameter");
    UITree_SetScrollSizeAt(tree, source, 200, 300);
    UITree_SetScrollPosAt(tree, source, 12, 23);
    tree->components[source].frame_hidden = 1;
    tree->components[source].replacement_paint_hidden = 1;
    int copy = UITree_CcCopy(tree, parent, 0x220000, 0, 1);
    TEST_ASSERT(copy >= 0, "copy succeeds");
    if( copy < 0 ) { UITree_Free(tree); return; }
    int copy_id = tree->components[copy].component_id;
    TEST_ASSERT(tree->components[copy].behavior.scripts_count == 1 &&
                tree->components[copy].behavior.scripts != tree->components[source].behavior.scripts &&
                tree->components[copy].behavior.scripts[0][2] == 3,
                "copy owns native behavior scripts");
    TEST_ASSERT(tree->cs1_script_nodes == 2, "copied behavior joins CS1 evaluation");
    TEST_ASSERT(tree->components[copy].native_hide, "copy preserves native hide");
    TEST_ASSERT(tree->components[copy].behavior.click_mask == 0x123456, "copy preserves native click mask");
    TEST_ASSERT(tree->components[copy].colour == 0x125678 && tree->components[copy].fill_colour == 0xabcdef,
                "copy preserves script-visible colors");
    TEST_ASSERT(tree->components[copy].data_text && strcmp(tree->components[copy].data_text, "live source") == 0,
                "copy preserves generic component text");
    int value = 0;
    TEST_ASSERT(UITree_ComponentParamGet(tree, copy_id, 100, &value) && value == 42,
                "copy preserves integer parameters");
    char const* text = UITree_ComponentParamGetStr(tree, copy_id, 101);
    TEST_ASSERT(text && strcmp(text, "source parameter") == 0, "copy preserves string parameters");
    TEST_ASSERT(tree->components[copy].scroll_x == 12 && tree->components[copy].scroll_y == 23,
                "copy preserves native scrolling");
    TEST_ASSERT(!tree->components[copy].frame_hidden && !tree->components[copy].replacement_paint_hidden,
                "copy cannot acquire source presentation claims");
    UITree_ApplyText(tree, source_id, "changed");
    UITree_ApplyComponentParam(tree, source_id, 101, 0, "changed");
    UITree_CcDelete(tree, source);
    TEST_ASSERT(tree->cs1_script_nodes == 1, "source deletion preserves copied CS1 registration");
    text = UITree_ComponentParamGetStr(tree, copy_id, 101);
    TEST_ASSERT(text && strcmp(text, "source parameter") == 0, "copy owns parameters after source deletion");
    TEST_ASSERT(tree->components[copy].data_text && strcmp(tree->components[copy].data_text, "live source") == 0,
                "copy owns generic text after source deletion");
    struct UITreeNodeSpec spec = {.type=UIELEM_RS_INV, .component_id=0x220001,
        .dynamic=1, .dynamic_child_index=2};
    int inv = UITree_Push(tree, parent, &spec);
    UITree_InvSlotsMut(&tree->components[inv])->offset_x[0] = 17;
    int inv_copy = UITree_CcCopy(tree, parent, 0x220000, 2, 3);
    TEST_ASSERT(inv_copy >= 0 && tree->components[inv_copy].u.rs_inv.slots !=
                tree->components[inv].u.rs_inv.slots, "copy owns inventory slot data");
    UITree_CcDelete(tree, inv);
    TEST_ASSERT(UITree_InvSlots(&tree->components[inv_copy])->offset_x[0] == 17,
                "inventory copy survives source deletion");
    UITree_Free(tree);
}
