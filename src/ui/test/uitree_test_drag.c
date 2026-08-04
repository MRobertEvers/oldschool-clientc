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
 * cc_dragpickup from scrollbar_vertical_jump is a one-shot: force one on_drag
 * (+ complete) then clear the source — track click must not leave a held drag.
 */
void
test_drag_cc_dragpickup_seeds(void)
{
    printf("TEST: cc_dragpickup one-shot jump (no held drag from track)\n");

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
        TEST_ASSERT(n >= 2, "pickup produced on_drag + complete");
        TEST_ASSERT(!interact.input_state.drag_active, "one-shot clears drag_active");
        TEST_ASSERT(interact.input_state.drag_source_idx < 0, "one-shot clears source");
        TEST_ASSERT(LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT), "still held");
        int found_drag = 0;
        int found_complete = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( !out.intents[i].hook || out.intents[i].hook->script_id != 35 )
                continue;
            if( out.intents[i].has_event_mouse )
                found_drag = 1;
            else
                found_complete = 1;
        }
        /* Both hooks are script 35; complete may also carry event mouse from the
         * jump frame — count intents instead. */
        TEST_ASSERT(found_drag, "on_drag from pickup");
        TEST_ASSERT(out.intent_count >= 2, "complete follows on_drag");
        (void)found_complete;
        (void)thumb;
        TEST_ASSERT(!tree->pending_drag_pickup, "pending cleared");
    }

    UITree_Free(tree);
}

/*
 * Full-size noclickthrough track drawn after the thumb steals HitTestInteractive.
 * Press on the thumb area must still arm the thumb (dragTryPickup before onclick),
 * not fire the track's jump onclick.
 */
void
test_drag_thumb_under_track_noclickthrough(void)
{
    printf("TEST: thumb press wins over noclickthrough track on top\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t bar = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 100, 100, 16, 100);
    tree->components[bar].if3 = 1;

    /* Thumb first, then track on top (wrong z for ~scrollbar_vertical, but the
     * failure mode when replace-append puts the track last). */
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
    tree->components[thumb].drag_render_area_uid = 900;
    UITree_HooksMut(&tree->components[thumb])->on_drag.script_id = 35;

    int32_t track = UITree_CcCreate(tree, bar, 900, 5, 0);
    TEST_ASSERT(UITree_ApplyPosition(tree, tree->components[track].component_id, 0, 16),
                "track pos");
    TEST_ASSERT(UITree_ApplySize(tree, tree->components[track].component_id, 16, 68),
                "track size");
    tree->components[track].no_click_through = 1;
    UITree_HooksMut(&tree->components[track])->on_click.script_id = 34;

    UITree_TestResolve(tree);

    int const press_x = 108;
    int const press_y = 126; /* inside thumb */
    TEST_ASSERT(
        UITree_HitTestInteractive(tree, &host, press_x, press_y) == track,
        "interactive hit is track on top");

    struct UIInteraction interact;
    UIInteraction_Init(&interact);
    struct LibToriRS_Input storage;
    struct LibToriRS_Input* input = LibToriRS_Input_Init(&storage, 0);

    LibToriRS_Input_Begin(input, 0);
    LibToriRS_Input_PushMouseMove(input, press_x, press_y);
    LibToriRS_Input_PushMouseDown(input, TORIRSM_LEFT, press_x, press_y);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 0, &out);
        TEST_ASSERT(interact.input_state.drag_source_idx == thumb, "press arms thumb");
        TEST_ASSERT(interact.input_state.deferred_click, "draggable defers click");
        int found_track_click = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].hook && out.intents[i].hook->script_id == 34 )
                found_track_click = 1;
        }
        TEST_ASSERT(!found_track_click, "track onclick suppressed over thumb");
    }

    LibToriRS_Input_Begin(input, 20);
    LibToriRS_Input_PushMouseMove(input, press_x, press_y + 3);
    LibToriRS_Input_End(input);
    {
        struct UIInteractOut out;
        UITree_InteractFrame(&interact, tree, &host, input, 20, &out);
        TEST_ASSERT(interact.input_state.drag_active, "thumb drag promotes");
        int found = 0;
        for( int i = 0; i < out.intent_count; i++ )
        {
            if( out.intents[i].hook && out.intents[i].hook->script_id == 35 )
                found = 1;
        }
        TEST_ASSERT(found, "on_drag from thumb");
    }

    UITree_Free(tree);
}
