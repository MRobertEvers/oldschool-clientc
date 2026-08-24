#include "test_harness.h"

#include "uitree_interact.h"

static int
find_desc(struct UITreeEmitBuffer const* buf, int component_id)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].component_id == component_id )
            return i;
    return -1;
}

static int
out_has_script(struct UIInteractOut const* out, int script_id)
{
    for( int i = 0; i < out->intent_count; i++ )
        if( out->intents[i].hook && out->intents[i].hook->script_id == script_id )
            return 1;
    return 0;
}

/*
 * A dragged composite widget (parent + child sprites, e.g. a scrollbar thumb
 * with end caps) must move as one unit, and its hitbox must follow the cursor.
 * Regression coverage for: (a) emit shifting the whole subtree by the drag
 * delta, (b) hit-testing reading the dragged position rather than abs_*.
 */
void
test_drag_composite(void)
{
    printf("TEST: composite drag / hitbox\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Draggable "thumb" graphic with a child "end cap" graphic. */
    struct UITreeNodeSpec parent;
    memset(&parent, 0, sizeof(parent));
    parent.type = UIELEM_RS_GRAPHIC;
    parent.component_id = 700;
    parent.x = 100;
    parent.y = 100;
    parent.width = 40;
    parent.height = 40;
    parent.u.rs_graphic.scene_id = 1;
    int32_t pi = UITree_Push(tree, -1, &parent);
    tree->components[pi].draggable = 1;

    struct UITreeNodeSpec cap;
    memset(&cap, 0, sizeof(cap));
    cap.type = UIELEM_RS_GRAPHIC;
    cap.component_id = 701;
    cap.x = 0;
    cap.y = 30;
    cap.width = 40;
    cap.height = 10;
    cap.u.rs_graphic.scene_id = 2;
    UITree_Push(tree, pi, &cap);

    UITree_TestResolve(tree);

    /* Baseline positions before dragging. */
    struct UITreeEmitBuffer buf;
    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);
    int dp = find_desc(&buf, 700);
    int dc = find_desc(&buf, 701);
    TEST_ASSERT(dp >= 0 && dc >= 0, "thumb + cap emitted pre-drag");
    int px0 = buf.cmds[dp].x, py0 = buf.cmds[dp].y;
    int cx0 = buf.cmds[dc].x, cy0 = buf.cmds[dc].y;
    UITree_EmitBufferFree(&buf);

    /* Pick up the thumb and move it by (+50, +40). */
    UITree_SetComponentDragActive(tree, pi, 1);
    tree->components[pi].drag_behavior = 0; /* deferred (picked-up) drag */
    tree->components[pi].drag_visual_x = px0 + 50;
    tree->components[pi].drag_visual_y = py0 + 40;
    tree->components[pi].drag_visual_trans = -1;

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);
    dp = find_desc(&buf, 700);
    dc = find_desc(&buf, 701);
    TEST_ASSERT(dp >= 0 && dc >= 0, "thumb + cap emitted during drag");
    TEST_ASSERT(buf.cmds[dp].x == px0 + 50 && buf.cmds[dp].y == py0 + 40, "thumb follows drag");
    /* The end cap (child) must move by the SAME delta, not stay behind. */
    TEST_ASSERT(buf.cmds[dc].x == cx0 + 50 && buf.cmds[dc].y == cy0 + 40, "end cap follows drag");
    UITree_EmitBufferFree(&buf);

    /* Hitbox follows the drag: the dragged location hits the thumb... */
    int32_t hit = UITree_HitTestInteractive(tree, &host, px0 + 50 + 5, py0 + 40 + 5);
    TEST_ASSERT(hit >= 0 && tree->components[hit].component_id == 700,
                "dragged hitbox is under the cursor");
    /* ...and the original (vacated) location no longer hits the thumb. */
    int32_t hit_old = UITree_HitTestInteractive(tree, &host, px0 + 5, py0 + 5);
    TEST_ASSERT(hit_old < 0 || tree->components[hit_old].component_id != 700,
                "vacated location no longer hits thumb");

    UITree_Free(tree);
}

/*
 * Scrollbar-style drag (drag_behavior==1): InteractFrame must promote the drag
 * and emit on_drag while the button is merely held — not only after the input
 * layer's 5px IsDragging threshold. event_mouse is relative to the drag render
 * area (the track = cc_setdraggable(bar, 0)), matching ~scrollbar_vertical_drag:
 * caps use event_mousey+16, so bar-relative coords leave a detached +16px rect.
 */
void
test_drag_scrollbar_ondrag_held(void)
{
    printf("TEST: scrollbar on_drag while held (no input IsDragging)\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Scrollbar bar (parent of track + thumb). Live CS2: cc_setdraggable(bar, 0). */
    int32_t bar = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 100, 100, 16, 100);
    tree->components[bar].if3 = 1;

    /* Track = dynamic child 0 (same encoding as ~scrollbar_vertical). */
    int32_t track = UITree_CcCreate(tree, bar, 900, 5, 0);
    TEST_ASSERT(track >= 0, "cc_create track as child 0");
    TEST_ASSERT(tree->components[track].dynamic_child_index == 0, "track subid 0");
    tree->components[track].u.rs_graphic.scene_id = 1;
    TEST_ASSERT(UITree_ApplyPosition(tree, tree->components[track].component_id, 0, 16),
                "track y=16");
    TEST_ASSERT(UITree_ApplySize(tree, tree->components[track].component_id, 16, 68),
                "track size");

    /* Thumb at y=16 inside the bar (top of track). */
    struct UITreeNodeSpec thumb_spec;
    memset(&thumb_spec, 0, sizeof(thumb_spec));
    thumb_spec.type = UIELEM_RS_GRAPHIC;
    thumb_spec.component_id = 902;
    thumb_spec.x = 0;
    thumb_spec.y = 16;
    thumb_spec.width = 16;
    thumb_spec.height = 20;
    thumb_spec.u.rs_graphic.scene_id = 2;
    int32_t thumb = UITree_Push(tree, bar, &thumb_spec);
    tree->components[thumb].draggable = 1;
    tree->components[thumb].drag_behavior = 1;
    tree->components[thumb].drag_dead_zone = 0;
    tree->components[thumb].drag_dead_time = 0;
    /* Live CS2 encoding before/alongside host eager resolve. */
    tree->components[thumb].drag_render_area_uid = 900; /* bar */
    tree->components[thumb].drag_render_area_child_index = 0;
    UITree_HooksMut(&tree->components[thumb])->on_drag.script_id = 35;

    UITree_TestResolve(tree);

    {
        int32_t area = UITree_ResolveDragRenderArea(tree, &tree->components[thumb]);
        TEST_ASSERT(area == track, "ResolveDragRenderArea is track (child 0), not bar");
    }

    struct UIInteraction interact;
    UIInteraction_Init(&interact);
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);
    /* Large input deadzone so IsDragging stays false — the bug we regress. */
    LibToriRS_Input_SetDragThresholds(input, 100, 0);

    int const press_x = 108;
    int const press_y = 126; /* inside thumb at bar(100,100)+thumb(0,16)+(8,10) */

    /* Press on the thumb. */
    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, press_x, press_y);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, press_x, press_y);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
        TEST_ASSERT(interact.input_state.drag_source_idx == thumb, "press arms drag source");
        TEST_ASSERT(!interact.input_state.drag_active, "zero deadzone still needs movement");
    }

    /* Hold + move 2px — under the 100px input deadzone, so IsDragging is false.
     * IsMouseHeld is true; UITree deadzone 0 must still promote and fire on_drag. */
    int const move_x = press_x;
    int const move_y = press_y + 2;
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseMove(input, move_x, move_y);
    LibToriRS_Input_End(input);
    TEST_ASSERT(!LibToriRS_Input_IsDragging(input, TORIRSM_LEFT), "input IsDragging still false");
    TEST_ASSERT(LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "button held");

    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        TEST_ASSERT(interact.input_state.drag_active, "drag_active with IsMouseHeld only");
        TEST_ASSERT(out.intent_count >= 1, "on_drag intent emitted");
        int found = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].component_id != 902 || !out.intents[i].hook )
                continue;
            if( out.intents[i].hook->script_id != 35 )
                continue;
            found = 1;
            TEST_ASSERT(out.intents[i].has_event_mouse, "on_drag carries event mouse");
            int track_y = 0, track_x = 0, tw = 0, th = 0;
            int bar_y = 0, bar_x = 0, bw = 0, bh = 0;
            UITree_LayoutGetBounds(
                &tree->components[track].position, &track_x, &track_y, &tw, &th);
            UITree_LayoutGetBounds(
                &tree->components[bar].position, &bar_x, &bar_y, &bw, &bh);
            int expect_y = tree->components[thumb].drag_visual_y - track_y;
            TEST_ASSERT(
                out.intents[i].event_mouse_y == expect_y,
                "event_mouse_y is track-relative");
            /* Near thumb-top (+2px): track-relative ≈ 2; bar-relative would be ≈ 18.
             * Script 35 does cap_y = event + 16 — only track-relative aligns caps. */
            TEST_ASSERT(
                out.intents[i].event_mouse_y < 8,
                "event_mouse_y near 0 at track top (not bar-relative ~16)");
            TEST_ASSERT(
                out.intents[i].event_mouse_y + 16 ==
                    tree->components[thumb].drag_visual_y - bar_y,
                "event+16 equals middle top in bar coords");
            break;
        }
        TEST_ASSERT(found, "on_drag intent targets thumb script 35");
    }

    UITree_Free(tree);
}

/*
 * Scrollbar thumbs use drag_behavior==1 (in-place, opaque). Behavior 0 defers a
 * translucent ghost on the drag pass while sibling caps stay on the normal pass
 * — the "second rectangle" users saw when behavior/event_mouse were wrong.
 * Caps positioned like script 35 (event_mousey+16 with track-relative event)
 * must share the middle's drawn Y.
 */
void
test_drag_scrollbar_inplace_emit(void)
{
    printf("TEST: scrollbar in-place emit (no deferred ghost)\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t bar = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 100, 100, 16, 100);
    tree->components[bar].if3 = 1;

    int32_t track = UITree_CcCreate(tree, bar, 900, 5, 0);
    TEST_ASSERT(track >= 0, "track child 0");
    TEST_ASSERT(UITree_ApplyPosition(tree, tree->components[track].component_id, 0, 16),
                "track pos");
    TEST_ASSERT(UITree_ApplySize(tree, tree->components[track].component_id, 16, 68),
                "track size");
    tree->components[track].u.rs_graphic.scene_id = 1;

    struct UITreeNodeSpec mid_spec;
    memset(&mid_spec, 0, sizeof(mid_spec));
    mid_spec.type = UIELEM_RS_GRAPHIC;
    mid_spec.component_id = 902;
    mid_spec.x = 0;
    mid_spec.y = 16;
    mid_spec.width = 16;
    mid_spec.height = 24;
    mid_spec.u.rs_graphic.scene_id = 2;
    int32_t middle = UITree_Push(tree, bar, &mid_spec);

    /* Sibling cap (not a child of the middle) — script 35 moves these. */
    struct UITreeNodeSpec cap_spec;
    memset(&cap_spec, 0, sizeof(cap_spec));
    cap_spec.type = UIELEM_RS_GRAPHIC;
    cap_spec.component_id = 903;
    cap_spec.x = 0;
    cap_spec.y = 16;
    cap_spec.width = 16;
    cap_spec.height = 5;
    cap_spec.u.rs_graphic.scene_id = 3;
    int32_t cap = UITree_Push(tree, bar, &cap_spec);
    TEST_ASSERT(cap >= 0, "sibling cap");

    UITree_TestResolve(tree);

    int const drag_y = 100 + 16 + 20; /* middle top 20px down the track */
    int track_y = 0, track_x = 0, tw = 0, th = 0;
    int bar_y = 0, bar_x = 0, bw = 0, bh = 0;
    UITree_LayoutGetBounds(&tree->components[track].position, &track_x, &track_y, &tw, &th);
    UITree_LayoutGetBounds(&tree->components[bar].position, &bar_x, &bar_y, &bw, &bh);
    int const event_y = drag_y - track_y; /* track-relative */
    int const cap_bar_y = event_y + 16;   /* script 35 */

    TEST_ASSERT(UITree_ApplyPosition(tree, 903, 0, cap_bar_y), "cap at event+16");
    UITree_TestResolve(tree);

    UITree_SetComponentDragActive(tree, middle, 1);
    tree->components[middle].drag_behavior = 1;
    tree->components[middle].drag_visual_x = 100;
    tree->components[middle].drag_visual_y = drag_y;
    tree->components[middle].drag_visual_trans = -1;

    {
        struct UITreeEmitBuffer buf;
        UITree_EmitBufferInit(&buf);
        UITree_EmitWalk(tree, &host, &buf, -1);
        int mid_count = 0;
        int mid_i = -1;
        int cap_i = -1;
        for( int i = 0; i < buf.count; i++ )
        {
            if( buf.cmds[i].component_id == 902 )
            {
                mid_count++;
                mid_i = i;
            }
            if( buf.cmds[i].component_id == 903 )
                cap_i = i;
        }
        TEST_ASSERT(mid_count == 1, "behavior 1 draws middle once (not deferred duplicate)");
        TEST_ASSERT(mid_i >= 0 && cap_i >= 0, "middle + cap emitted");
        TEST_ASSERT(buf.cmds[mid_i].y == drag_y, "middle at drag_visual");
        TEST_ASSERT(buf.cmds[mid_i].trans != 128, "scrollbar middle not ghosted");
        TEST_ASSERT(buf.cmds[cap_i].y == buf.cmds[mid_i].y, "cap Y matches middle (event+16)");
        UITree_EmitBufferFree(&buf);
    }

    /* Control: deferred pickup ghosts the middle (trans 128). */
    tree->components[middle].drag_behavior = 0;
    tree->components[middle].drag_visual_trans = 128;
    {
        struct UITreeEmitBuffer buf;
        UITree_EmitBufferInit(&buf);
        UITree_EmitWalk(tree, &host, &buf, -1);
        int mid_i = find_desc(&buf, 902);
        TEST_ASSERT(mid_i >= 0, "deferred middle still emitted on drag pass");
        TEST_ASSERT(buf.cmds[mid_i].trans == 128, "deferred middle is ghosted");
        UITree_EmitBufferFree(&buf);
    }

    UITree_Free(tree);
}

/*
 * Real XP-drops setup scrollbar geometry (iface 137): bar 165, track inset 16
 * with height 133, thumb ~35, list visible 165 / scroll_height 625.
 * event_mouse_y must span ~0..98 (travel) and map to scroll ~0..460.
 */
void
test_drag_scrollbar_137_geometry(void)
{
    printf("TEST: scrollbar 137 geometry event_mouse → scroll\n");

    int const bar_h = 165;
    int const track_h = bar_h - 32; /* 133 */
    int const thumb_h = 35;
    int const travel = track_h - thumb_h; /* 98 */
    int const list_h = 165;
    int const scroll_h = 625;
    int const scroll_range = scroll_h - list_h; /* 460 */

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t bar = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 100, 100, 16, bar_h);
    tree->components[bar].if3 = 1;

    int32_t track = UITree_CcCreate(tree, bar, 900, 5, 0);
    TEST_ASSERT(track >= 0, "track");
    TEST_ASSERT(UITree_ApplyPosition(tree, tree->components[track].component_id, 0, 16),
                "track y");
    TEST_ASSERT(UITree_ApplySize(tree, tree->components[track].component_id, 16, track_h),
                "track size");

    struct UITreeNodeSpec thumb_spec;
    memset(&thumb_spec, 0, sizeof(thumb_spec));
    thumb_spec.type = UIELEM_RS_GRAPHIC;
    thumb_spec.component_id = 902;
    thumb_spec.x = 0;
    thumb_spec.y = 16;
    thumb_spec.width = 16;
    thumb_spec.height = thumb_h;
    thumb_spec.u.rs_graphic.scene_id = 2;
    int32_t thumb = UITree_Push(tree, bar, &thumb_spec);
    tree->components[thumb].draggable = 1;
    tree->components[thumb].drag_behavior = 1;
    tree->components[thumb].drag_dead_zone = 0;
    tree->components[thumb].drag_dead_time = 0;
    tree->components[thumb].drag_render_area_uid = tree->components[track].component_id;
    tree->components[thumb].drag_render_area_child_index = -1;
    UITree_HooksMut(&tree->components[thumb])->on_drag.script_id = 35;

    UITree_TestResolve(tree);

    struct UIInteraction interact;
    UIInteraction_Init(&interact);
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);
    LibToriRS_Input_SetDragThresholds(input, 100, 0);

    int const press_x = 108;
    int const press_y = 100 + 16 + 1; /* near thumb top → pickup ≈ 0 */

    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, press_x, press_y);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, press_x, press_y);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
    }

    /* Mouse at track bottom: with top grab, thumb top reaches travel. */
    int const move_y = 100 + 16 + travel + 1;
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseMove(input, press_x, move_y);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        TEST_ASSERT(interact.input_state.drag_active, "drag active at travel end");
        int found = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( !out.intents[i].has_event_mouse || !out.intents[i].hook ||
                out.intents[i].hook->script_id != 35 )
                continue;
            found = 1;
            int ey = out.intents[i].event_mouse_y;
            TEST_ASSERT(ey >= travel - 2 && ey <= travel + 2, "event_y near travel 98");
            int scroll = ey * scroll_range / (travel > 0 ? travel : 1);
            TEST_ASSERT(
                scroll >= scroll_range - 5 && scroll <= scroll_range + 5,
                "derived scroll near 460");
            break;
        }
        TEST_ASSERT(found, "on_drag at bottom");
    }

    UITree_Free(tree);
}

/*
 * cc_dragpickup from scrollbar_vertical_jump seeds the thumb under the cursor
 * and, while the button is still held, leaves it as the live drag source so the
 * gesture continues without a second press.
 */
void
test_drag_cc_dragpickup_seeds(void)
{
    printf("TEST: cc_dragpickup jump keeps thumb as live drag source\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t bar = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 100, 100, 16, 100);
    tree->components[bar].if3 = 1;

    int32_t track = UITree_CcCreate(tree, bar, 900, 5, 0);
    TEST_ASSERT(UITree_ApplyPosition(tree, tree->components[track].component_id, 0, 16),
                "track pos");
    TEST_ASSERT(UITree_ApplySize(tree, tree->components[track].component_id, 16, 68),
                "track size");

    struct UITreeNodeSpec thumb_spec;
    memset(&thumb_spec, 0, sizeof(thumb_spec));
    thumb_spec.type = UIELEM_RS_GRAPHIC;
    thumb_spec.component_id = 902;
    thumb_spec.x = 0;
    thumb_spec.y = 16;
    thumb_spec.width = 16;
    thumb_spec.height = 20;
    thumb_spec.u.rs_graphic.scene_id = 2;
    int32_t thumb = UITree_Push(tree, bar, &thumb_spec);
    tree->components[thumb].draggable = 1;
    tree->components[thumb].drag_behavior = 1;
    tree->components[thumb].drag_render_area_uid = tree->components[track].component_id;
    UITree_HooksMut(&tree->components[thumb])->on_drag.script_id = 35;
    UITree_HooksMut(&tree->components[thumb])->on_drag_complete.script_id = 35;
    UITree_HooksMut(&tree->components[track])->on_click.script_id = 34;

    UITree_TestResolve(tree);

    struct UIInteraction interact;
    UIInteraction_Init(&interact);
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);

    int const mx = 108;
    int const my = 100 + 16 + 40; /* empty track below thumb */

    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, mx, my);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, mx, my);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
        TEST_ASSERT(out.intent_count >= 1, "press-click intent");
        int found_click = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].hook && out.intents[i].hook->script_id == 34 )
                found_click = 1;
        }
        TEST_ASSERT(found_click, "track on_click on press");
        TEST_ASSERT(!interact.input_state.drag_active, "no drag yet before pickup");
    }

    tree->pending_drag_pickup = 1;
    tree->pending_drag_pickup_id = 902;
    tree->pending_drag_pickup_x = 0;
    tree->pending_drag_pickup_y = 10;

    {
        struct UIInteractOut out;
        int n = UITree_InteractConsumePendingDragPickup(
            &interact, tree, &host, input, &out);
        TEST_ASSERT(n >= 1, "pickup produced on_drag");
        TEST_ASSERT(LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "still held");
        TEST_ASSERT(interact.input_state.drag_active, "held pickup stays active");
        TEST_ASSERT(interact.input_state.drag_source_idx == thumb, "thumb is the source");
        int found_drag = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].hook && out.intents[i].hook->script_id == 35 &&
                out.intents[i].has_event_mouse )
                found_drag = 1;
        }
        TEST_ASSERT(found_drag, "on_drag from pickup");
        TEST_ASSERT(!tree->pending_drag_pickup, "pending cleared");
    }

    /* The gesture continues from the jump without a fresh press. */
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseMove(input, mx, my + 6);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        TEST_ASSERT(interact.input_state.drag_active, "drag still live after move");
        int found = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].hook && out.intents[i].hook->script_id == 35 )
                found = 1;
        }
        TEST_ASSERT(found, "on_drag while held");
    }

    UITree_Free(tree);
}

/* A press-owned hook stays attached to the component that received mouse-down:
 * click-repeat runs while held, and release still runs after the cursor leaves
 * the component. */
void
test_press_repeat_and_release(void)
{
    printf("TEST: press click-repeat / release ownership\n");

    struct UITree* tree = UITree_New(8);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t button = UITree_TestPushXy(
        tree, -1, UIELEM_RS_GRAPHIC, 980, 100, 100, 40, 40);
    UITree_HooksMut(&tree->components[button])->on_click_repeat.script_id = 801;
    UITree_HooksMut(&tree->components[button])->on_release.script_id = 802;
    UITree_TestResolve(tree);

    struct UIInteraction interact;
    UIInteraction_Init(&interact);
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);

    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, 110, 112);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 110, 112);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
        int found_repeat = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].component_id == 980 && out.intents[i].hook &&
                out.intents[i].hook->script_id == 801 )
            {
                found_repeat = 1;
                TEST_ASSERT(out.intents[i].has_event_mouse, "repeat has event mouse");
                TEST_ASSERT(out.intents[i].event_mouse_x == 10, "repeat mouse x is relative");
                TEST_ASSERT(out.intents[i].event_mouse_y == 12, "repeat mouse y is relative");
            }
        }
        TEST_ASSERT(found_repeat, "on_click_repeat emitted while pressed");
    }

    /* Release well outside the button. The hook still belongs to component 980. */
    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseMove(input, 250, 260);
    LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 250, 260);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        int found_release = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].component_id == 980 && out.intents[i].hook &&
                out.intents[i].hook->script_id == 802 )
            {
                found_release = 1;
                TEST_ASSERT(out.intents[i].has_event_mouse, "release has event mouse");
                TEST_ASSERT(out.intents[i].event_mouse_x == 150, "release mouse x is relative");
                TEST_ASSERT(out.intents[i].event_mouse_y == 160, "release mouse y is relative");
            }
        }
        TEST_ASSERT(found_release, "on_release emitted for original press owner");
    }

    UITree_Free(tree);
}

/* Effective frame suppression is display:none for pointer ownership too. A
 * target can become frame-hidden between two input frames when a plugin frame
 * is applied/reasserted; that transition cancels the gesture rather than
 * continuing to dispatch held/release/drag hooks into invisible native UI. */
void
test_frame_hidden_cancels_active_input(void)
{
    printf("TEST: frame-hidden cancels active press / drag ownership\n");

    /* A plugin hit surface owns the physical press from its down edge through
     * release. Native hooks and drag state beneath it never get an edge to
     * inherit, including for an opaque zero-op plugin region. */
    {
        struct UITree* tree = UITree_New(4);
        struct TestHostState hs;
        struct UITreeHost host;
        struct UIInteraction interact;
        struct LibToriRS_Input storage;
        struct LibToriRS_Input* input;
        struct UIInteractOut out;
        int32_t button;

        printf("TEST: external plugin capture owns full left gesture\n");
        UITree_TestHostInit(&host, &hs);
        button = UITree_TestPushXy(
            tree, -1, UIELEM_RS_GRAPHIC, 989, 100, 100, 40, 40);
        tree->components[button].draggable = 1;
        tree->components[button].drag_dead_zone = 0;
        tree->components[button].drag_dead_time = 0;
        UITree_HooksMut(&tree->components[button])->on_click.script_id = 811;
        UITree_HooksMut(&tree->components[button])->on_hold.script_id = 812;
        UITree_HooksMut(&tree->components[button])->on_click_repeat.script_id = 813;
        UITree_HooksMut(&tree->components[button])->on_release.script_id = 814;
        UITree_HooksMut(&tree->components[button])->on_drag.script_id = 815;
        UITree_HooksMut(&tree->components[button])->on_drag_complete.script_id = 816;
        UITree_TestResolve(tree);
        UIInteraction_Init(&interact);
        input = LibToriRS_Input_Init(&storage, 0);

        LibToriRS_Input_Begin(input, 0);
        LibToriRS_Input_PushMouseMove(input, 110, 110);
        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 110, 110);
        LibToriRS_Input_End(input);
        UITree_InteractFrameWithPointerCapture(
            &interact, tree, &host, input, 0, 1, &out);
        TEST_ASSERT(out.intent_count == 0, "captured down emits no native pointer hook");
        TEST_ASSERT(
            interact.input_state.pressed < 0 && interact.input_state.drag_source_idx < 0,
            "captured down arms no native press or drag");

        LibToriRS_Input_Begin(input, 20);
        LibToriRS_Input_PushMouseMove(input, 125, 125);
        LibToriRS_Input_End(input);
        UITree_InteractFrameWithPointerCapture(
            &interact, tree, &host, input, 20, 1, &out);
        TEST_ASSERT(out.intent_count == 0, "captured hold emits no hold/repeat/drag hook");
        TEST_ASSERT(!tree->components[button].drag_active, "captured hold has no drag visual");

        LibToriRS_Input_Begin(input, 40);
        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 125, 125);
        LibToriRS_Input_End(input);
        UITree_InteractFrameWithPointerCapture(
            &interact, tree, &host, input, 40, 1, &out);
        TEST_ASSERT(out.intent_count == 0, "captured release emits no release/complete hook");
        TEST_ASSERT(!out.left_click_miss, "captured release cannot fall through to world");
        TEST_ASSERT(out.cancelled_pointer_click, "captured release is marked native-consumed");

        UITree_Free(tree);
    }

    /* Press ownership: stop repeats immediately, then swallow mouse-up without
     * either releasing the hidden widget or leaking a click to the world. */
    {
        struct UITree* tree = UITree_New(4);
        struct TestHostState hs;
        struct UITreeHost host;
        struct UIInteraction interact;
        struct LibToriRS_Input storage;
        struct LibToriRS_Input* input;
        struct UIInteractOut out;
        int32_t button;

        UITree_TestHostInit(&host, &hs);
        button = UITree_TestPushXy(
            tree, -1, UIELEM_RS_GRAPHIC, 990, 100, 100, 40, 40);
        UITree_HooksMut(&tree->components[button])->on_click_repeat.script_id = 821;
        UITree_HooksMut(&tree->components[button])->on_release.script_id = 822;
        UITree_TestResolve(tree);
        UIInteraction_Init(&interact);
        input = LibToriRS_Input_Init(&storage, 0);

        LibToriRS_Input_Begin(input, 0);
        LibToriRS_Input_PushMouseMove(input, 110, 110);
        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 110, 110);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
        TEST_ASSERT(interact.input_state.pressed == button, "visible button owns the press");
        TEST_ASSERT(out_has_script(&out, 821), "visible press emits click-repeat");

        tree->components[button].frame_hidden = 1;
        LibToriRS_Input_Begin(input, 20);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        TEST_ASSERT(!out_has_script(&out, 821), "hidden press owner emits no click-repeat");
        TEST_ASSERT(!out_has_script(&out, 822), "hiding does not synthesize on-release");
        TEST_ASSERT(
            interact.input_state.pressed < 0 && interact.input_state.drag_source_idx < 0 &&
                !interact.input_state.deferred_click,
            "hiding cancels all stored press ownership");

        LibToriRS_Input_Begin(input, 40);
        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 110, 110);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 40, &out);
        TEST_ASSERT(!out_has_script(&out, 821), "cancelled mouse-up emits no repeat");
        TEST_ASSERT(!out_has_script(&out, 822), "cancelled mouse-up emits no hidden release");
        TEST_ASSERT(!out.left_click_miss, "cancelled UI press does not leak into a world click");
        TEST_ASSERT(
            out.cancelled_pointer_click,
            "cancelled UI release is fenced from app-level plugin hit regions");

        /* Deletion/rebuild is the other display:none transition: CS2 may put
         * the same component id back into the exact array slot while the
         * physical button is still held. Incarnation, rather than either
         * reusable number, keeps that new occupant from inheriting the press. */
        tree->components[button].frame_hidden = 0;
        LibToriRS_Input_Begin(input, 60);
        LibToriRS_Input_PushMouseMove(input, 110, 110);
        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 110, 110);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 60, &out);
        TEST_ASSERT(interact.input_state.pressed == button, "rebuilt fixture owns fresh press");
        tree->components[button].incarnation++;
        UITree_HooksMut(&tree->components[button])->on_click_repeat.script_id = 823;

        LibToriRS_Input_Begin(input, 80);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 80, &out);
        TEST_ASSERT(!out_has_script(&out, 823), "recycled same-id node inherits no held hook");
        TEST_ASSERT(interact.input_state.pressed < 0, "recycle cancels stored press identity");

        LibToriRS_Input_Begin(input, 100);
        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 110, 110);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 100, &out);
        TEST_ASSERT(!out.left_click_miss, "recycled press release remains swallowed");
        TEST_ASSERT(
            out.cancelled_pointer_click,
            "recycled release is fenced from app-level plugin hit regions");

        UITree_Free(tree);
    }

    /* Active drag ownership: retire both state-machine and component render
     * state as soon as the source disappears. Cancellation is not a drop, so
     * it emits neither onDrag nor onDragComplete/onRelease. */
    {
        struct UITree* tree = UITree_New(4);
        struct TestHostState hs;
        struct UITreeHost host;
        struct UIInteraction interact;
        struct LibToriRS_Input storage;
        struct LibToriRS_Input* input;
        struct UIInteractOut out;
        int32_t source;

        UITree_TestHostInit(&host, &hs);
        source = UITree_TestPushXy(
            tree, -1, UIELEM_RS_GRAPHIC, 991, 100, 100, 40, 40);
        tree->components[source].draggable = 1;
        tree->components[source].drag_dead_zone = 0;
        tree->components[source].drag_dead_time = 0;
        UITree_HooksMut(&tree->components[source])->on_drag.script_id = 831;
        UITree_HooksMut(&tree->components[source])->on_drag_complete.script_id = 832;
        UITree_HooksMut(&tree->components[source])->on_release.script_id = 833;
        UITree_TestResolve(tree);
        UIInteraction_Init(&interact);
        input = LibToriRS_Input_Init(&storage, 0);

        LibToriRS_Input_Begin(input, 0);
        LibToriRS_Input_PushMouseMove(input, 110, 110);
        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 110, 110);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
        TEST_ASSERT(interact.input_state.drag_source_idx == source, "visible source arms drag");

        LibToriRS_Input_Begin(input, 20);
        LibToriRS_Input_PushMouseMove(input, 120, 120);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        TEST_ASSERT(interact.input_state.drag_active, "visible source begins active drag");
        TEST_ASSERT(tree->components[source].drag_active, "drag render state is active");
        TEST_ASSERT(out_has_script(&out, 831), "visible source emits on-drag");

        tree->components[source].frame_hidden = 1;
        LibToriRS_Input_Begin(input, 40);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 40, &out);
        TEST_ASSERT(!out_has_script(&out, 831), "hidden drag source emits no on-drag");
        TEST_ASSERT(!out_has_script(&out, 832), "hiding does not complete the drag");
        TEST_ASSERT(!out_has_script(&out, 833), "hiding does not release the drag source");
        TEST_ASSERT(
            !interact.input_state.drag_active && interact.input_state.drag_source_idx < 0 &&
                interact.input_state.pressed < 0,
            "hiding cancels active drag and press ownership");
        TEST_ASSERT(
            !tree->components[source].drag_active && tree->drag_active_nodes == 0,
            "hiding clears deferred drag render state");

        LibToriRS_Input_Begin(input, 60);
        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 120, 120);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 60, &out);
        TEST_ASSERT(!out_has_script(&out, 832), "cancelled drag mouse-up does not complete");
        TEST_ASSERT(!out_has_script(&out, 833), "cancelled drag mouse-up does not release");
        TEST_ASSERT(!out.left_click_miss, "cancelled drag does not leak into a world click");

        UITree_Free(tree);
    }

    /* A drop target is sampled while the button is held. Mouse-up does not
     * run another drag tick, so retain that exact incarnation rather than
     * resolving its component id after a synchronous same-id rebuild. */
    {
        struct UITree* tree = UITree_New(4);
        struct TestHostState hs;
        struct UITreeHost host;
        struct UIInteraction interact;
        struct LibToriRS_Input storage;
        struct LibToriRS_Input* input;
        struct UIInteractOut out;
        int32_t source;
        int32_t target;
        uint32_t target_incarnation;
        int found_complete = 0;

        UITree_TestHostInit(&host, &hs);
        source = UITree_TestPushXy(
            tree, -1, UIELEM_RS_GRAPHIC, 991, 10, 10, 30, 30);
        target = UITree_TestPushXy(
            tree, -1, UIELEM_RS_RECT, 992, 100, 10, 40, 40);
        tree->components[source].draggable = 1;
        tree->components[source].drag_dead_zone = 0;
        tree->components[source].drag_dead_time = 0;
        tree->components[target].behavior.click_mask |= UITREE_FLAG_DRAG_ON;
        UITree_HooksMut(&tree->components[source])->on_drag_complete.script_id = 842;
        UITree_TestResolve(tree);
        UIInteraction_Init(&interact);
        input = LibToriRS_Input_Init(&storage, 0);

        LibToriRS_Input_Begin(input, 0);
        LibToriRS_Input_PushMouseMove(input, 20, 20);
        LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, 20, 20);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);

        LibToriRS_Input_Begin(input, 20);
        LibToriRS_Input_PushMouseMove(input, 110, 20);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        target_incarnation = tree->components[target].incarnation;
        TEST_ASSERT(
            interact.input_state.drag_target_idx == target &&
                interact.input_state.drag_target_incarnation == target_incarnation,
            "held drag stamps the exact drop-target occupant");

        tree->components[target].incarnation++;
        LibToriRS_Input_Begin(input, 40);
        LibToriRS_Input_PushMouseUp(input, TORIRSM_LEFT, 110, 20);
        LibToriRS_Input_End(input);
        UITree_InteractFrame(&interact, tree, &host, input, 40, &out);
        for( int i = 0; i < out.intent_count; i++ )
        {
            struct UIIntent const* intent = &out.intents[i];
            if( !intent->hook || intent->hook->script_id != 842 )
                continue;
            found_complete = 1;
            TEST_ASSERT(
                intent->has_drag_target_identity &&
                    intent->drag_target_node_index == target &&
                    intent->drag_target_node_incarnation == target_incarnation &&
                    intent->drag_target_node_incarnation !=
                        tree->components[target].incarnation,
                "release carries the historical target instead of blessing its replacement");
        }
        TEST_ASSERT(found_complete, "release emits the source's drag-complete intent");

        UITree_Free(tree);
    }
}
