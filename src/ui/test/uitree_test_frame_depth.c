#include "test_harness.h"
#include "uitree_frame.h"
#include "uitree_hover.h"

static int rect_pixel(struct UITreeEmitBuffer const* buffer, int x, int y)
{
    int pixel = -1;
    for( int i = 0; i < buffer->count; i++ )
    {
        struct UITreeEmitDesc const* d = &buffer->cmds[i];
        if( d->kind == UITREE_EMIT_RECT && d->filled && x >= d->x && y >= d->y &&
            x < d->x + d->w && y < d->y + d->h ) pixel = d->color;
    }
    return pixel;
}

static int32_t depth_button(struct UITree* tree, int32_t parent, int id, int color)
{
    int32_t node = UITree_TestPushXy(tree, parent, UIELEM_RS_RECT, id, 0, 0, 50, 50);
    tree->components[node].if3 = 1;
    tree->components[node].u.rs_rect.filled = 1;
    tree->components[node].u.rs_rect.color = color;
    UITree_HooksMut(&tree->components[node])->on_click.script_id = 42;
    UITree_HooksMut(&tree->components[node])->on_mouse_over.script_id = 43;
    return node;
}

static void test_native_map_with_other_plugin(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT] = { 0 };
    int group_id = 779;
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, group_id << 16, 0, 0, 765, 503);
    int32_t map = UITree_TestPushXy(tree, root, UIELEM_BUILTIN_MINIMAP, (group_id << 16) | 1, 10, 10, 100, 100);
    int32_t child = depth_button(tree, map, (780 << 16) | 1, 0x00ffff);
    UITree_TestPushXy(tree, root, UIELEM_BUILTIN_WORLD, (group_id << 16) | 2, 0, 0, 200, 200);
    tree->components[map].if3 = 1;
    UITree_TestHostInit(&host, &state);
    state.minimap_scene_id = 123;
    struct UITreeEntityOverlay other_art = { .kind = UITREE_ENTITY_OVERLAY_RECT,
        .x = 10, .y = 10, .w = 100, .h = 100, .color = 0xff123456u };
    struct UITreeRoleOverlayGroup other_plugin = { .node_index = map,
        .node_incarnation = tree->components[map].incarnation,
        .place = UITREE_ROLE_PLACE_BEFORE, .items = &other_art, .item_count = 1 };
    state.role_overlay_groups = &other_plugin;
    state.role_overlay_group_count = state.role_anchor_seen = 1;
    slots[UITREE_FRAME_SLOT_VIEWPORT].all = (struct UITreeFrameRect){ 1, 0, 0, 200, 200 };
    slots[UITREE_FRAME_SLOT_MINIMAP].all = (struct UITreeFrameRect){ 1, 10, 10, 100, 100 };
    slots[UITREE_FRAME_SLOT_MINIMAP].anchor = (struct UITreeFrameAnchor){ UITREE_FRAME_RELATION_OVER, UITREE_FRAME_SLOT_VIEWPORT };
    slots[UITREE_FRAME_SLOT_MINIMAP].overlay = (struct UITreeFrameOverlay){ 1, 913, 10, 10, 0 };
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&buffer);
    for( int disabled = 0; disabled <= 1; disabled++ )
    {
        state.minimap_hidden = disabled;
        UITree_FrameApply(tree, slots, group_id);
        buffer.count = 0;
        UITree_EmitWalk(tree, &host, &buffer, -1);
        int maps = 0, attachments = 0, foreign = 0;
        for( int i = 0; i < buffer.count; i++ )
        {
            maps += buffer.cmds[i].kind == UITREE_EMIT_MINIMAP;
            attachments += buffer.cmds[i].scene_id == 913;
            foreign += buffer.cmds[i].entity_overlays == &other_art;
        }
        int hit = UITree_HitTestInteractive(tree, &host, 20, 20);
        int hover = UITree_FindHoveredComponentIdForRegion(tree, &host, -1, 20, 20, 0, 0, 765, 503);
        printf("NATIVE_MUTATION minimap_disabled=%d maps=%d frame_art=%d other_plugin_art=%d hit=%d hover=%d\n",
               disabled, maps, attachments, foreign, hit, hover);
        TEST_ASSERT(maps == !disabled && attachments == !disabled && foreign == !disabled,
                    "native minimap availability governs the map and both plugins' attached art");
        TEST_ASSERT(disabled ? hit != child && hover == -1 : hit == child,
                    "native minimap availability also governs descendant input and hover");
        TEST_ASSERT(UITree_NodeNativeVisible(tree, &host, map, -1) == !disabled,
                    "the application region gate reads the same native availability");
    }
    UITree_FrameRelease(tree);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    int maps = 0;
    for( int i = 0; i < buffer.count; i++ ) maps += buffer.cmds[i].kind == UITREE_EMIT_MINIMAP;
    TEST_ASSERT(!maps, "frame release cannot re-enable a natively disabled minimap");
    state.minimap_hidden = 0;
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    maps = 0;
    for( int i = 0; i < buffer.count; i++ ) maps += buffer.cmds[i].kind == UITREE_EMIT_MINIMAP;
    TEST_ASSERT(maps == 1, "native re-enable restores the map without a new plugin claim");
    UITree_EmitBufferFree(&buffer);
    UITree_Free(tree);
}

static void test_empty_native_anchor(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT] = { 0 };
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 781 << 16, 0, 0, 765, 503);
    int32_t source = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (781 << 16) | 1, 10, 10, 50, 50);
    int32_t red = depth_button(tree, source, (782 << 16) | 1, 0xff0000);
    int32_t peer = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, (782 << 16) | 2, 10, 10, 50, 50);
    tree->components[peer].u.rs_rect.filled = 1;
    tree->components[peer].u.rs_rect.color = 0x00ff00;
    UITree_HooksMut(&tree->components[peer])->on_click.script_id = 42;
    int32_t target = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (781 << 16) | 3, 10, 10, 50, 50);
    tree->components[source].slot_tag = UITREE_SLOT_CHAT;
    tree->components[target].slot_tag = UITREE_SLOT_MAIN_MODAL;
    /* Another presenter has taken the target's native subtree. Even when it
     * has no drawable payload, the native element still has an order boundary. */
    UITree_SetReplacementHidden(tree, target, tree->components[target].incarnation, 1);
    UITree_TestHostInit(&host, &state);
    slots[UITREE_FRAME_SLOT_CHAT].all = (struct UITreeFrameRect){ 1, 10, 10, 50, 50 };
    slots[UITREE_FRAME_SLOT_MAIN_MODAL].all = slots[UITREE_FRAME_SLOT_CHAT].all;
    slots[UITREE_FRAME_SLOT_CHAT].anchor = (struct UITreeFrameAnchor){ UITREE_FRAME_RELATION_OVER, UITREE_FRAME_SLOT_MAIN_MODAL };
    UITree_TestResolve(tree);
    UITree_FrameApply(tree, slots, 781);
    UITree_EmitBufferInit(&buffer);
    UITree_EmitWalk(tree, &host, &buffer, -1);
    int hit = UITree_HitTestInteractive(tree, &host, 20, 20);
    printf("FRAME_CONTRACT empty_native_anchor pixel=%06x hit=%d source=%d\n", rect_pixel(&buffer, 20, 20), hit, red);
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0xff0000 && hit == red,
                "an empty or externally-replaced native anchor preserves matching paint and input order");
    UITree_EmitBufferFree(&buffer);
    UITree_Free(tree);
}

void test_frame_declared_depth(void)
{
    test_native_map_with_other_plugin();
    test_empty_native_anchor();
    struct UITree* tree = UITree_New(16);
    struct UITreeEmitBuffer buffer;
    struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT] = { 0 };
    struct UITreeHost host;
    struct TestHostState state;
    int const group = 777;
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, group << 16, 0, 0, 765, 503);
    int32_t a = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (group << 16) | 1, 10, 10, 50, 50);
    int32_t red = depth_button(tree, a, (778 << 16) | 1, 0xff0000);
    int32_t b = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (group << 16) | 2, 10, 10, 50, 50);
    int32_t blue = depth_button(tree, b, (778 << 16) | 2, 0x0000ff);
    tree->components[a].if3 = tree->components[b].if3 = 1;
    tree->components[a].slot_tag = UITREE_SLOT_CHAT;
    tree->components[b].slot_tag = UITREE_SLOT_MAIN_MODAL;
    tree->components[a].no_click_through = tree->components[b].no_click_through = 1;
    UITree_TestHostInit(&host, &state);
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&buffer);
    slots[UITREE_FRAME_SLOT_CHAT].all = (struct UITreeFrameRect){ 1, 10, 10, 50, 50 };
    slots[UITREE_FRAME_SLOT_MAIN_MODAL].all = slots[UITREE_FRAME_SLOT_CHAT].all;

    printf("TEST: declared depth shares paint, click, menu and hover order\n");
    UITree_FrameApply(tree, slots, group);
    UITree_EmitWalk(tree, &host, &buffer, -1);
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0x0000ff, "native blue covers red");
    TEST_ASSERT(UITree_HitTestInteractive(tree, &host, 20, 20) == blue, "native blue takes the click");
    for( int relation = UITREE_FRAME_RELATION_OVER; relation <= UITREE_FRAME_RELATION_REPLACE; relation++ )
    {
        slots[UITREE_FRAME_SLOT_CHAT].anchor = (struct UITreeFrameAnchor){ 0 };
        slots[UITREE_FRAME_SLOT_MAIN_MODAL].anchor = (struct UITreeFrameAnchor){ 0 };
        if( relation == UITREE_FRAME_RELATION_BEHIND )
            slots[UITREE_FRAME_SLOT_MAIN_MODAL].anchor =
                (struct UITreeFrameAnchor){ relation, UITREE_FRAME_SLOT_CHAT };
        else
            slots[UITREE_FRAME_SLOT_CHAT].anchor =
                (struct UITreeFrameAnchor){ relation, UITREE_FRAME_SLOT_MAIN_MODAL };
        slots[UITREE_FRAME_SLOT_CHAT].overlay = (struct UITreeFrameOverlay){ 1, 912, 80, 80, 0 };
        UITree_FrameApply(tree, slots, group);
        buffer.count = 0;
        UITree_EmitWalk(tree, &host, &buffer, -1);
        TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0xff0000, "the declared relation paints red on top");
        TEST_ASSERT(buffer.cmds[buffer.count - 1].scene_id == 912,
                    "paint-only attached art follows its surface through depth ordering");
        TEST_ASSERT(UITree_HitTestInteractive(tree, &host, 20, 20) == red, "the visible red button takes the click");
        TEST_ASSERT(UITree_FindHoveredComponentIdForRegion(tree, &host, -1, 20, 20, 0, 0, 765, 503) ==
                    tree->components[red].component_id, "the visible red button receives hover");
        int32_t hits[8];
        int count = UITree_CollectNodesAt(tree, &host, 20, 20, hits, 8);
        TEST_ASSERT(count > 0 && hits[0] == red, "the menu uses the same topmost surface");
        if( relation == UITREE_FRAME_RELATION_REPLACE )
        {
            int blue_painted = 0;
            for( int i = 0; i < buffer.count; i++ ) blue_painted |= buffer.cmds[i].node_index == blue;
            TEST_ASSERT(!blue_painted, "replacement removes the target's paint");
        }
    }
    UITree_ApplyHide(tree, tree->components[a].component_id, 1);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0x0000ff &&
                UITree_HitTestInteractive(tree, &host, 20, 20) == blue,
                "an unavailable replacement source exposes the still-visible native target");
    TEST_ASSERT(!UITree_FrameNodeReplaced(tree, &host, b),
                "other plugins may use the target while its replacement source is natively hidden");
    UITree_ApplyHide(tree, tree->components[a].component_id, 0);
    /* A replacement represents a native target; hiding that target must not
     * resurrect it through either native hover gating or replacement art. */
    UITree_ApplyHide(tree, tree->components[b].component_id, 1);
    UITree_FrameApply(tree, slots, group);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, tree->components[b].component_id);
    int hidden_hit = UITree_HitTestInteractive(tree, &host, 20, 20);
    int hidden_hover = UITree_FindHoveredComponentIdForRegion(tree, &host, -1, 20, 20, 0, 0, 765, 503);
    printf("NATIVE_MUTATION cs2_target_hide rect_pixel=%d hit=%d hover=%d\n",
           rect_pixel(&buffer, 20, 20), hidden_hit, hidden_hover);
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == -1, "native hide outranks replacement paint even with a stale hover id");
    TEST_ASSERT(hidden_hit != red && hidden_hit != blue && hidden_hover == -1,
                "a hidden replacement target has no click or hover regions");
    UITree_ApplyHide(tree, tree->components[b].component_id, 0);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0xff0000, "native show restores the replacement's current presentation");

    tree->components[red].behavior.active_color = 0x00ff00;
    UITree_SetCS1ActiveAt(tree, red, 1);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    printf("NATIVE_MUTATION cs1_active rect_pixel=%d\n", rect_pixel(&buffer, 20, 20));
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0x00ff00, "unclaimed child appearance follows native CS1 active state");
    tree->components[blue].behavior.active_color = 0x123456;
    UITree_SetCS1ActiveAt(tree, blue, 1);
    UITree_FrameRelease(tree);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    TEST_ASSERT(rect_pixel(&buffer, 20, 20) == 0x123456, "release exposes the latest native state, not a saved original");
    TEST_ASSERT(UITree_HitTestInteractive(tree, &host, 20, 20) == blue, "release restores native input order");
    /* IF1 authored hover-gated tooltips and runtime server hiding are
     * distinct. Restating an already-authored hide still establishes a veto. */
    int32_t tooltip = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER,
                          (group << 16) | 3, 100, 10, 50, 50);
    int32_t tip_text = depth_button(tree, tooltip, (778 << 16) | 3, 0xabcdef);
    tree->components[tooltip].behavior.hide = 1;
    tree->components[tooltip].if3 = tree->components[tip_text].if3 = 0;
    UITree_TestResolve(tree);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, tree->components[tooltip].component_id);
    TEST_ASSERT(rect_pixel(&buffer, 110, 20) == 0xabcdef, "authored RS2 tooltip gating retains native hover behavior");
    UITree_ApplyHide(tree, tree->components[tooltip].component_id, 1);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, tree->components[tooltip].component_id);
    printf("NATIVE_MUTATION rs2_server_hide rect_pixel=%d\n", rect_pixel(&buffer, 110, 20));
    TEST_ASSERT(rect_pixel(&buffer, 110, 20) == -1,
                "explicit RS2 server hiding is not undone by authored tooltip gating");
    UITree_ApplyHide(tree, tree->components[tooltip].component_id, 0);
    buffer.count = 0;
    UITree_EmitWalk(tree, &host, &buffer, -1);
    TEST_ASSERT(rect_pixel(&buffer, 110, 20) == 0xabcdef, "RS2 server show clears its visibility veto");
    UITree_EmitBufferFree(&buffer);
    UITree_Free(tree);
}
