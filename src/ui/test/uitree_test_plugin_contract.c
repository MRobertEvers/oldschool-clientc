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
    TEST_ASSERT(!tree->components[copy].frame_hidden,
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

void test_live_widget_geometry(void)
{
    struct UITree* tree = UITree_New(8);
    int root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x340000, 0, 0, 300, 200);
    int node = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, 0x340001, 10, 20, 30, 40);
    struct UITreeNodeRef ref = UITree_RefAt(tree, node);
    UITree_GeometryAuditEnable(tree);
    TEST_ASSERT(UITree_WidgetSetPosition(tree, ref, 10, 80, 90), "widget position setter accepts current ref");
    TEST_ASSERT(UITree_WidgetSetSize(tree, ref, 20, 0, 50), "widget size setter accepts zero width");
    UITree_EnsureLayout(tree);
    TEST_ASSERT(tree->components[node].position.abs_x == 80 && tree->components[node].position.abs_w == 0,
                "widget geometry reaches native layout including zero");
    TEST_ASSERT(tree->components[node].position.x == 10 && tree->components[node].position.width == 30,
                "widget edits do not overwrite native inputs");
    UITree_SetPositionAt(tree, node, 11, 22);
    UITree_SetSizeAt(tree, node, 44, 55);
    UITree_EnsureLayout(tree);
    TEST_ASSERT(tree->components[node].position.abs_x == 80 && tree->components[node].position.abs_w == 0,
                "native updates preserve forced geometry");
    UITree_WidgetSetPosition(tree, ref, 20, 100, 110);
    UITree_EnsureLayout(tree);
    TEST_ASSERT(tree->components[node].position.abs_x == 100, "last widget setter wins");
    UITree_WidgetReset(tree, ref, 20);
    UITree_EnsureLayout(tree);
    TEST_ASSERT(tree->components[node].position.abs_x == 80 && tree->components[node].position.abs_w == 44,
                "reset releases only its owner and exposes current native size");
    UITree_WidgetResetOwner(tree, 10);
    UITree_EnsureLayout(tree);
    TEST_ASSERT(tree->components[node].position.abs_x == 11 && tree->components[node].position.abs_h == 55,
                "widget cleanup exposes latest native geometry");
    TEST_ASSERT(UITree_GeometryAuditCheck(tree, "widget tests"), "widget edits preserve native audit invariants");
    int dyn = UITree_CcCreate(tree, root, 0x340000, 3, 7);
    struct UITreeNodeRef old = UITree_RefAt(tree, dyn);
    UITree_WidgetSetPosition(tree, old, 10, 1, 2);
    int copy = UITree_CcCopy(tree, root, 0x340000, 7, 8);
    TEST_ASSERT(copy >= 0 && !tree->components[copy].widget_geometry, "native copy does not inherit plugin owners");
    UITree_CcDelete(tree, dyn);
    UITree_CcCreate(tree, root, 0x340000, 3, 7);
    TEST_ASSERT(!UITree_WidgetSetPosition(tree, old, 10, 3, 4), "stale widget setter cannot edit recycled node");
    UITree_Free(tree);
}

void test_live_widget_visibility(void)
{
    struct UITree* tree=UITree_New(8);
    int root=UITree_TestPushXy(tree,-1,UIELEM_RS_LAYER,0x360000,0,0,300,200);
    int child=UITree_TestPushXy(tree,root,UIELEM_RS_RECT,0x360001,10,20,30,40);
    struct UITreeBehavior behavior={.button_type=1};
    UITree_SetBehavior(tree,child,&behavior);
    struct UITreeNodeRef ref=UITree_RefAt(tree,root);
    struct TestHostState hs;struct UITreeHost host;
    UITree_TestHostInit(&host,&hs);UITree_TestResolve(tree);
    TEST_ASSERT(UITree_HitTestInteractive(tree,&host,20,30)==child,"visibility fixture is interactive");
    struct UITreeEmitBuffer emit;UITree_EmitBufferInit(&emit);
    uint32_t generation=tree->generation,dirty=tree->dirty_gen;
    TEST_ASSERT(UITree_WidgetSetHidden(tree,ref,10,true),"plugin can hide a live subtree");
    TEST_ASSERT(tree->generation==generation && tree->dirty_gen!=dirty,
        "presentation hide invalidates readers without fabricating a topology event");
    TEST_ASSERT(!tree->components[root].behavior.hide && !tree->components[root].native_hide,
        "plugin hide preserves native hiding inputs");
    TEST_ASSERT(UITree_HitTestInteractive(tree,&host,20,30)!=child &&
        !UITree_NodeNativeInputPresent(tree,&host,child),"hidden widget descendants reject input and native operations");
    UITree_EmitWalk(tree,&host,&emit,-1);
    bool painted=false;
    for( int i=0;i<emit.count;++i ) if( emit.cmds[i].component_id==0x360001 ) painted=true;
    TEST_ASSERT(!painted,"hidden widget subtree is absent from paint");
    UITree_SetColourAt(tree,child,0x123456);
    TEST_ASSERT(UITree_WidgetSetHidden(tree,ref,20,false) && !tree->components[root].widget_hidden,
        "the latest presentation hide setter wins");
    UITree_WidgetResetOwner(tree,20);
    TEST_ASSERT(tree->components[root].widget_hidden,"removing the latest writer exposes the remaining hide edit");
    UITree_SetHideAt(tree,root,1);
    UITree_WidgetSetHidden(tree,ref,10,false);
    TEST_ASSERT(!UITree_NodeNativeVisible(tree,&host,child,-1),"plugin show cannot override current native server hiding");
    UITree_WidgetResetOwner(tree,10);
    TEST_ASSERT(!UITree_NodeNativeVisible(tree,&host,child,-1),"disable cannot restore an obsolete native visibility snapshot");
    UITree_SetHideAt(tree,root,0);
    TEST_ASSERT(UITree_NodeNativeVisible(tree,&host,child,-1) && tree->components[child].u.rs_rect.color==0x123456,
        "reset reveals the current native content and visibility");
    tree->components[root].if3=1;
    UITree_SetHideAt(tree,root,1);UITree_WidgetSetHidden(tree,ref,10,false);
    TEST_ASSERT(!UITree_NodeNativeVisible(tree,&host,child,-1),"IF3 server hide also vetoes a plugin show");
    UITree_WidgetResetOwner(tree,10);UITree_SetHideAt(tree,root,0);
    int dynamic=UITree_CcCreate(tree,root,0x360000,3,7);
    UITree_WidgetSetHidden(tree,UITree_RefAt(tree,dynamic),10,true);
    int copy=UITree_CcCopy(tree,root,0x360000,7,8);
    TEST_ASSERT(copy>=0 && !tree->components[copy].widget_hidden && !tree->components[copy].widget_geometry,
        "native copying does not inherit presentation hiding or its owners");
    int own=UITree_WidgetCreateText(tree,ref,10,"owned",0);
    TEST_ASSERT(own>=0 && !UITree_WidgetSetHidden(tree,UITree_RefAt(tree,own),20,true),
        "another plugin cannot hide an owned widget");
    struct UITreeNodeRef own_ref=UITree_RefAt(tree,own);
    UITree_SetTextAt(tree,own,"outlined");
    tree->components[own].u.rs_text.shadowed=1;
    TEST_ASSERT(UITree_WidgetSetTextOutline(tree,own_ref,10,true),"owner can outline its text");
    struct UITreeEmitDesc text_desc;
    TEST_ASSERT(UITree_EmitFill(tree,&host,&tree->components[own],own,-1,&text_desc) &&
        text_desc.text_shadowed==2,"outline style reaches the native text descriptor");
    TEST_ASSERT(!UITree_WidgetSetTextOutline(tree,own_ref,20,false),"another owner cannot change text presentation");
    UITree_WidgetReset(tree,own_ref,10);
    TEST_ASSERT(UITree_EmitFill(tree,&host,&tree->components[own],own,-1,&text_desc) &&
        text_desc.text_shadowed==1,"outline reset restores the current native shadow style");
    UITree_EmitBufferFree(&emit);UITree_Free(tree);
    tree=UITree_New(8);
    UITree_TestPushXy(tree,-1,UIELEM_RS_LAYER,0x360000,0,0,300,200);
    TEST_ASSERT(!UITree_WidgetSetHidden(tree,ref,10,true),"retained hide cannot target a replacement tree");
    UITree_Free(tree);
}

void test_widget_sidebar_group(void)
{
    struct UITree* tree=UITree_New(8);
    int root=UITree_TestPushXy(tree,-1,UIELEM_RS_LAYER,0x350000,0,0,300,300);
    int modal=UITree_TestPushXy(tree,root,UIELEM_RS_LAYER,0x350001,0,0,100,100);
    tree->components[modal].slot_tag=UITREE_SLOT_SIDE_MODAL;
    int group=UITree_TestPushXy(tree,root,UIELEM_RS_LAYER,0x350002,0,0,100,100);
    int hidden=UITree_TestPushXy(tree,group,UIELEM_RS_LAYER,0x350003,0,0,100,100);
    tree->components[hidden].slot_tag=UITREE_SLOT_SIDE_MODAL;
    tree->components[hidden].frame_member_plus1=2;
    UITree_SetHideAt(tree,hidden,1);
    int active=UITree_TestPushXy(tree,group,UIELEM_RS_LAYER,0x350004,0,0,100,100);
    tree->components[active].slot_tag=UITREE_SLOT_SIDE_MODAL;
    tree->components[active].frame_member_plus1=1;
    TEST_ASSERT(UITree_FrameSlotGroupNode(tree,UITREE_FRAME_SLOT_SIDEBAR)==group,
                "sidebar widget resolves member parent, not modal or hidden tab");
    UITree_WidgetSetPosition(tree,UITree_RefAt(tree,group),1,12,14);
    UITree_EnsureLayout(tree);
    TEST_ASSERT(tree->components[active].position.abs_x==12 && tree->components[hidden].position.abs_x==12,
                "moving sidebar group moves every native tab together");
    UITree_Reparent(tree,active,root);
    TEST_ASSERT(UITree_FrameSlotGroupNode(tree,UITREE_FRAME_SLOT_SIDEBAR)<0,
                "sidebar helper rejects incompatible member topology");
    UITree_Free(tree);
}

void test_owned_widgets(void)
{
    struct UITree* t=UITree_New(16);
    int root=UITree_TestPushXy(t,-1,UIELEM_RS_LAYER,0x360000,0,0,300,300);
    int native=UITree_TestPushXy(t,root,UIELEM_RS_TEXT,0x360002,0,0,20,20);
    struct UITreeNodeRef parent=UITree_RefAt(t,root);
    int dynamic=UITree_CcCreate(t,root,0x360000,4,0);
    int a=UITree_WidgetCreateText(t,parent,1,"caption",1);
    int b=UITree_WidgetCreateText(t,parent,2,"caption",1);
    struct UITreeNodeRef ar=UITree_RefAt(t,a), br=UITree_RefAt(t,b), nr=UITree_RefAt(t,native);
    TEST_ASSERT(a>=0 && b>=0 && a!=b,"owned widget keys are isolated by owner");
    TEST_ASSERT(UITree_WidgetCreateText(t,parent,1,"caption",1)==a,"owned create is idempotent by parent and key");
    TEST_ASSERT(t->components[root].child_key_max<=2,"owned widgets do not pollute native child keys");
    TEST_ASSERT(UITree_FindChildBySubid(t,root,0x360000,65535)<0,"anonymous owned widget has no native sub-id");
    int children[8];
    TEST_ASSERT(UITree_CollectDynamicChildIndices(t,0x360000,0,children,8)==1 && children[0]==0,
                "native child iteration excludes owned widgets");
    TEST_ASSERT(!UITree_WidgetRemove(t,nr,1),"owned remove cannot delete native widgets");
    TEST_ASSERT(!UITree_WidgetRemove(t,br,1),"owned remove cannot delete another plugin's widget");
    TEST_ASSERT(!UITree_Reparent(t,native,a),"native widget cannot enter an owned subtree");
    TEST_ASSERT(UITree_WidgetCreateText(t,ar,2,"foreign",1)<0,"another owner cannot create in an owned subtree");
    TEST_ASSERT(!UITree_WidgetSetPosition(t,ar,2,1,2),"another owner cannot reposition an owned widget");
    UITree_CcDeleteAll(t,root);
    TEST_ASSERT(t->components[dynamic].freed && UITree_ResolveRef(t,ar)==a && UITree_ResolveRef(t,br)==b,
                "native deleteall preserves owned siblings");
    UITree_ClearChildren(t,root);
    TEST_ASSERT(UITree_ResolveRef(t,nr)<0 && UITree_ResolveRef(t,ar)==a,
                "native slot replacement preserves attached owned controls");
    UITree_SetHideAt(t,root,1);
    UITree_SetTextAt(t,a,"live update");
    TEST_ASSERT(UITree_NodeOrAncestorDisplayHidden(t,a),"native hiding remains authoritative over owned text updates");
    UITree_WidgetResetOwner(t,1);
    TEST_ASSERT(UITree_ResolveRef(t,ar)<0 && UITree_ResolveRef(t,br)==b,"owner teardown preserves other owners");
    int layer=UITree_CcCreate(t,root,0x360000,0,2);
    int nested=UITree_WidgetCreateText(t,UITree_RefAt(t,layer),2,"nested",1);
    struct UITreeNodeRef nested_ref=UITree_RefAt(t,nested);
    UITree_CcDeleteAll(t,root);
    TEST_ASSERT(UITree_ResolveRef(t,nested_ref)<0,"closing a native subtree invalidates owned descendants");
    UITree_Free(t);
}

/* Owned-control operations: the label lives in the native menu option storage,
 * the registration serial takes part in the action signature, and nothing
 * native or foreign can be armed. */
void test_owned_widget_operations(void)
{
    struct UITree* t=UITree_New(16);
    int root=UITree_TestPushXy(t,-1,UIELEM_RS_LAYER,0x360000,0,0,300,300);
    int native=UITree_TestPushXy(t,root,UIELEM_RS_TEXT,0x360002,0,0,20,20);
    int own=UITree_WidgetCreateText(t,UITree_RefAt(t,root),1,"button",1);
    struct UITreeNodeRef ref=UITree_RefAt(t,own);
    uint64_t unarmed=UITree_ActionSignatureAt(t,own);
    char too_long[UITREE_MENU_OPTION_LEN+1];
    memset(too_long,'x',sizeof(too_long)-1);too_long[sizeof(too_long)-1]=0;
    TEST_ASSERT(own>=0,"owned control fixture created");
    TEST_ASSERT(!UITree_WidgetSetOperation(t,UITree_RefAt(t,native),1,5,"Toggle"),"a native widget never takes a plugin operation");
    TEST_ASSERT(!UITree_WidgetSetOperation(t,ref,2,5,"Toggle"),"another owner cannot arm an owned control");
    TEST_ASSERT(!UITree_WidgetSetOperation(t,ref,1,5,""),"an armed operation needs a label");
    TEST_ASSERT(!UITree_WidgetSetOperation(t,ref,1,5,too_long),"the label must fit native menu option storage");
    TEST_ASSERT(!t->components[own].plugin_op_serial && !UITree_MenuOptions(&t->components[own])->option[0],
        "rejected arming leaves the control unarmed");
    TEST_ASSERT(UITree_WidgetSetOperation(t,ref,1,5,"Toggle"),"the owner arms its control");
    TEST_ASSERT(strcmp(UITree_MenuOptions(&t->components[own])->option,"Toggle")==0 && t->components[own].plugin_op_serial==5,
        "operation label and registration are stored on the node");
    uint64_t armed=UITree_ActionSignatureAt(t,own);
    TEST_ASSERT(armed!=unarmed,"arming changes the native action signature");
    TEST_ASSERT(UITree_WidgetSetOperation(t,ref,1,6,"Toggle"),"the owner replaces its listener");
    TEST_ASSERT(UITree_ActionSignatureAt(t,own)!=armed,"replacing the listener retires rows built for the old one");
    TEST_ASSERT(UITree_WidgetSetOperation(t,ref,1,0,""),"the owner removes its operation");
    TEST_ASSERT(!UITree_MenuOptions(&t->components[own])->option[0] && !t->components[own].plugin_op_serial,
        "removal clears label and registration");
    TEST_ASSERT(UITree_ActionSignatureAt(t,own)==unarmed,"a disarmed control has its original signature");
    UITree_WidgetRemove(t,ref,1);
    TEST_ASSERT(!UITree_WidgetSetOperation(t,ref,1,7,"Toggle"),"a stale reference cannot be armed");
    int again=UITree_WidgetCreateText(t,UITree_RefAt(t,root),1,"button",1);
    TEST_ASSERT(again>=0 && !t->components[again].plugin_op_serial && !UITree_MenuOptions(&t->components[again])->option[0],
        "a recreated control starts unarmed");
    UITree_Free(t);
}

/* Owned image controls: keyed graphic children, owner-checked image and
 * opacity setters, and the release sweep that blanks a freed scene id on
 * plugin-owned graphics only. */
void test_owned_image_widgets(void)
{
    struct UITree* t=UITree_New(16);
    int root=UITree_TestPushXy(t,-1,UIELEM_RS_LAYER,0x360000,0,0,300,300);
    int native=UITree_TestPushXy(t,root,UIELEM_RS_GRAPHIC,0x360002,0,0,20,20);
    UITree_SetGraphicAt(t,native,7001,0);
    struct UITreeNodeRef parent=UITree_RefAt(t,root);
    int image=UITree_WidgetCreateGraphic(t,parent,1,"camera");
    int text=UITree_WidgetCreateText(t,parent,1,"label",0);
    TEST_ASSERT(image>=0 && t->components[image].type==UIELEM_RS_GRAPHIC && t->components[image].plugin_owner==1 &&
        t->components[image].component_id==-1,"owned image child is a plugin-owned graphic without a component id");
    TEST_ASSERT(UITree_WidgetCreateGraphic(t,parent,1,"camera")==image,"owned image create is idempotent by key");
    TEST_ASSERT(UITree_WidgetCreateGraphic(t,parent,2,"camera")!=image,"owned image keys are isolated by owner");
    struct UITreeNodeRef ref=UITree_RefAt(t,image);
    TEST_ASSERT(!UITree_WidgetSetGraphic(t,ref,2,7001,20,20),"another owner cannot set an owned image");
    TEST_ASSERT(!UITree_WidgetSetGraphic(t,UITree_RefAt(t,native),1,7001,20,20),"a native graphic never takes a plugin image");
    TEST_ASSERT(!UITree_WidgetSetGraphic(t,UITree_RefAt(t,text),1,7001,20,20),"an owned text control is not an image control");
    TEST_ASSERT(!UITree_WidgetSetGraphic(t,ref,1,7001,5000,20),"image size is bounded");
    TEST_ASSERT(UITree_WidgetSetGraphic(t,ref,1,7001,24,16),"the owner installs its image");
    UITree_LayoutResolve(t,0,0,300,300);
    TEST_ASSERT(t->components[image].u.rs_graphic.scene_id==7001 && t->components[image].position.abs_w==24 &&
        t->components[image].position.abs_h==16,"image scene and control size are installed together");
    TEST_ASSERT(!UITree_WidgetSetTransparency(t,ref,2,100),"another owner cannot change an owned control's opacity");
    TEST_ASSERT(!UITree_WidgetSetTransparency(t,UITree_RefAt(t,native),1,100),"native opacity is not a plugin field here");
    TEST_ASSERT(UITree_WidgetSetTransparency(t,ref,1,170) && t->components[image].trans==170,"the owner sets its control's transparency");
    TEST_ASSERT(UITree_WidgetClearGraphic(t,7001)==1 && t->components[image].u.rs_graphic.scene_id==0 &&
        t->components[native].u.rs_graphic.scene_id==7001,"releasing an image blanks only plugin-owned graphics showing it");
    TEST_ASSERT(UITree_WidgetClearGraphic(t,7001)==0,"a second sweep finds nothing");
    UITree_WidgetRemove(t,ref,1);
    TEST_ASSERT(!UITree_WidgetSetGraphic(t,ref,1,7001,24,16),"a removed image control is stale");
    UITree_Free(t);
}
