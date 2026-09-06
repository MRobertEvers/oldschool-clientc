#include "test_harness.h"
#include "uitree_interact.h"
#include "uitree_obj_cell.h"

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

void test_native_geometry_audit(void)
{
    struct UITree* tree = UITree_New(8);
    UITree_GeometryAuditEnable(tree);
    int root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x300000, 0, 0, 100, 100);
    int node = UITree_CcCreate(tree, root, 0x300000, 3, 0);
    UITree_SetPositionAt(tree, node, 10, 20);
    UITree_SetSizeAt(tree, node, 30, 40);
    UITree_LayoutResolve(tree, 0, 0, 100, 100);
    TEST_ASSERT(UITree_GeometryAuditCheck(tree, "test-valid"), "typed geometry and layout pass audit");
    tree->components[node].position.x = 11;
    TEST_ASSERT(!UITree_GeometryAuditCheck(tree, "test-direct"), "audit catches unclassified native position");
    tree->components[node].position.x = 10;
    tree->components[node].position.abs_x += 1;
    TEST_ASSERT(UITree_GeometryAuditCheck(tree, "test-derived"), "derived coordinates do not masquerade as native writes");
    UITree_SetScrollSizeAt(tree, root, 200, 200);
    UITree_SetScrollPosAt(tree, root, 20, 30);
    TEST_ASSERT(UITree_GeometryAuditCheck(tree, "test-scroll"), "typed scrolling passes audit");
    tree->components[root].scroll_y = 31;
    TEST_ASSERT(!UITree_GeometryAuditCheck(tree, "test-direct-scroll"), "audit catches unclassified scrolling");
    tree->components[root].scroll_y = 30;
    int copy = UITree_CcCopy(tree, root, 0x300000, 0, 1);
    TEST_ASSERT(copy >= 0 && UITree_GeometryAuditCheck(tree, "test-copy"), "copy constructor seals native geometry");
    UITree_CcDelete(tree, node);
    UITree_CcCreate(tree, root, 0x300000, 3, 0);
    TEST_ASSERT(UITree_GeometryAuditCheck(tree, "test-reuse"), "recycled slot gets a fresh audit record");
    UITree_Free(tree);
}

void test_native_object_swap_state(void)
{
    struct UITree* tree = UITree_New(8);
    int root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x310000, 0, 0, 100, 100);
    int a = UITree_CcCreate(tree, root, 0x310000, 2, 0);
    int b = UITree_CcCreate(tree, root, 0x310000, 2, 1);
    UITree_SetXYBoxAt(tree, a, 0, 0, 32, 32);
    UITree_SetXYBoxAt(tree, b, 40, 0, 32, 32);
    UITree_ApplyObject(tree, tree->components[a].component_id, 995, 10, 1, 0, 0);
    UITree_ApplyObject(tree, tree->components[b].component_id, 1333, 1, 2, 0, 0);
    TEST_ASSERT(UITree_ObjCellDynamicSwap(tree, 0x310000, 0, 1), "native item swap executes");
    UITree_LayoutResolve(tree, 0, 0, 100, 100);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);
    struct UITreeEmitBuffer emit;
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);
    int objects[2] = {-1, -1};
    for( int i = 0; i < emit.count; ++i )
    {
        if( emit.cmds[i].kind != UITREE_EMIT_CC_OBJ ) continue;
        if( emit.cmds[i].node_index == a ) objects[0] = emit.cmds[i].obj_id;
        if( emit.cmds[i].node_index == b ) objects[1] = emit.cmds[i].obj_id;
    }
    TEST_ASSERT(objects[0] == 1333 && objects[1] == 995, "drawing observes the swapped native item state");
    UITree_SetHideAt(tree, a, 1);
    UITree_ObjCellDynamicSwap(tree, 0x310000, 0, 1);
    TEST_ASSERT(tree->components[a].native_hide && !tree->components[b].native_hide,
                "item movement cannot transfer native hiding");
    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}

void test_native_hook_slot_ownership(void)
{
    struct UITree* tree = UITree_New(4);
    int a = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 0x320000, 0, 0, 20, 20);
    int b = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 0x320001, 30, 0, 20, 20);
    struct UITreeRuntimeScriptHook* own = &UITree_HooksMut(&tree->components[a])->on_click;
    struct UITreeRuntimeScriptHook* other = &UITree_HooksMut(&tree->components[b])->on_click;
    UITree_HookSet(other, 111, NULL, 0, 0, NULL, 0);
    TEST_ASSERT(!UITree_ApplyRuntimeHook(tree, 0x320000, other, 222, NULL, 0, 0, NULL, 0),
                "hook mutation rejects another component's slot");
    TEST_ASSERT(other->script_id == 111, "rejected hook mutation preserves its actual owner");
    TEST_ASSERT(UITree_ApplyRuntimeHook(tree, 0x320000, own, 333, NULL, 0, 0, NULL, 0),
                "owned hook mutation succeeds");
    uint32_t dirty = tree->dirty_gen;
    UITree_ApplyRuntimeHook(tree, 0x320000, own, 333, NULL, 0, 0, NULL, 0);
    TEST_ASSERT(tree->dirty_gen == dirty, "identical hook binding is quiet");
    UITree_Free(tree);
}

void test_retained_operation_state(void)
{
    struct UITree* tree = UITree_New(4);
    int node = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 0x330000, 0, 0, 30, 30);
    UITree_ApplyOpBase(tree, 0x330000, "First target");
    UITree_ApplyClickMask(tree, 0x330000, 2);
    struct UIMinimenuPick pick = {.kind=UI_MINIMENU_PICK_UI, .id=0x330000};
    UITree_StampMenuPick(tree, node, &pick);
    UITree_SetPositionAt(tree, node, 50, 60);
    UITree_SetColourAt(tree, node, 0x123456);
    UITree_SetTransparencyAt(tree, node, 80);
    TEST_ASSERT(UITree_MenuPickCurrent(tree, &pick), "geometry and skin changes preserve an operation");
    UITree_ApplyRuntimeHook(tree, 0x330000, &UITree_HooksMut(&tree->components[node])->on_timer,
                           999, NULL, 0, 0, NULL, 0);
    TEST_ASSERT(UITree_MenuPickCurrent(tree, &pick), "unrelated timer changes preserve an operation");
    UITree_ApplyOpBase(tree, 0x330000, "Other target");
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &pick), "retained menu rejects changed operation labels");
    UITree_StampMenuPick(tree, node, &pick);
    UITree_ApplyClickMask(tree, 0x330000, 4);
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &pick), "retained menu rejects changed operation masks");
    UITree_StampMenuPick(tree, node, &pick);
    UITree_ApplyRuntimeHook(tree, 0x330000, &UITree_HooksMut(&tree->components[node])->on_op,
                           101, NULL, 0, 0, NULL, 0);
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &pick), "retained menu rejects changed action hooks");
    UITree_StampMenuPick(tree, node, &pick);
    UITree_ApplyComponentParam(tree, 0x330000, 2370, 7, NULL);
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &pick), "retained menu rejects changed native action data");
    UITree_StampMenuPick(tree, node, &pick);
    UITree_ApplyObject(tree, 0x330000, 995, 10, 1, 0, 0);
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &pick), "retained menu rejects changed item state");
    int social = UITree_TestPushXy(tree, -1, UIELEM_RS_TEXT, 0x330001, 0, 0, 30, 30);
    UITree_SetTextAt(tree, social, "First friend");
    UITree_StampMenuPick(tree, social, &pick);
    UITree_SetTextAt(tree, social, "Other friend");
    TEST_ASSERT(!UITree_MenuPickCurrent(tree, &pick), "retained menu rejects changed native text target");
    UITree_Free(tree);
}
