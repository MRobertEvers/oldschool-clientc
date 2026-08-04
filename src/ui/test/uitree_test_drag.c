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
 * area (the track), matching ~scrollbar_vertical_drag's event_mousey math.
 */
void
test_drag_scrollbar_ondrag_held(void)
{
    printf("TEST: scrollbar on_drag while held (no input IsDragging)\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Scrollbar layer parent (not the drag clamp area). */
    int32_t bar = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 100, 100, 16, 100);
    tree->components[bar].if3 = 1;

    /* Track = drag render area child 0; thumb clamps inside it. */
    int32_t track = UITree_TestPushXy(tree, bar, UIELEM_RS_GRAPHIC, 901, 0, 16, 16, 68);
    tree->components[track].u.rs_graphic.scene_id = 1;

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
    tree->components[thumb].drag_render_area_uid = 901; /* track */
    tree->components[thumb].drag_render_area_child_index = -1;
    UITree_HooksMut(&tree->components[thumb])->on_drag.script_id = 35;

    UITree_TestResolve(tree);

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
            /* Thumb top at drag_visual; track screen origin is bar.abs + 16.
             * At rest-ish (+2px), event_mouse_y ≈ drag_visual_y - track_y. */
            int track_y = 0, track_x = 0, tw = 0, th = 0;
            UITree_LayoutGetBounds(
                &tree->components[track].position, &track_x, &track_y, &tw, &th);
            int expect_y = tree->components[thumb].drag_visual_y - track_y;
            TEST_ASSERT(
                out.intents[i].event_mouse_y == expect_y,
                "event_mouse_y is track-relative");
            break;
        }
        TEST_ASSERT(found, "on_drag intent targets thumb script 35");
    }

    UITree_Free(tree);
}
