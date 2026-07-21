#include "test_harness.h"

/*
 * Dragging a widget that lives INSIDE a scrolled layer. The pickup offset must
 * be taken against the widget's drawn (scroll-shifted) position so the widget
 * does not jump on grab, and the drop target must be resolved at drawn
 * positions. Uses real (group<<16)|file ids and no UITreeScrollState, like the
 * shipping client.
 */

#define TG 550
#define TUID(file) ((TG << 16) | (file))

static int
find_desc_id(struct UITreeEmitBuffer const* buf, int component_id)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].component_id == component_id )
            return i;
    return -1;
}

void
test_drag_scrolled(void)
{
    printf("TEST: drag inside scrolled layer\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Scrollable layer: viewport 200x100, content 400 tall, scrolled by 120. */
    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, TUID(3), 0, 0, 200, 100);
    tree->components[layer].u.rs_layer.scroll_height = 400;

    /* Draggable item at content (20,200) → drawn at (20,80) once scrolled. */
    struct UITreeNodeSpec item;
    memset(&item, 0, sizeof(item));
    item.type = UIELEM_RS_GRAPHIC;
    item.component_id = TUID(9);
    item.x = 20;
    item.y = 200;
    item.width = 40;
    item.height = 30;
    item.u.rs_graphic.scene_id = 1;
    int32_t src = UITree_Push(tree, layer, &item);
    tree->components[src].draggable = 1;
    tree->components[src].drag_dead_zone = 5;
    tree->components[src].drag_dead_time = 3;

    /* Drop target at content (120,160) → drawn at (120,40). */
    int32_t tgt = UITree_TestPushXy(tree, layer, UIELEM_RS_RECT, TUID(11), 120, 160, 40, 30);
    tree->components[tgt].runtime_hooks.on_drag_complete.script_id = 1;

    UITree_TestResolve(tree);
    tree->components[layer].scroll_y = 120;

    /* Press at the item's DRAWN location; grab point is (+10,+5) inside it. */
    struct UIInputState st = { .hovered = -1, .pressed = -1 };
    UITree_InputUpdate(
        &st, tree, &host,
        (struct UIInputEvent){ .kind = UI_INPUT_DOWN, .x = 30, .y = 85 });
    TEST_ASSERT(st.pressed == src, "press picks up item at drawn position");
    TEST_ASSERT(st.drag_source_idx == src, "drag source registered");

    /* Move past the deadzone, tick past the dead time. */
    int const mx = 50;
    int const my = 105;
    for( int i = 0; i < 5; i++ )
        UITree_InputDragTick(&st, tree, &host, mx, my, 1);
    TEST_ASSERT(st.drag_active, "drag activates past deadzone/deadtime");

    /* No jump on grab: the widget's drawn top-left must track
     * mouse - pickup, where pickup was recorded against the DRAWN position
     * (20,80), i.e. pickup = (10,5). */
    TEST_ASSERT(tree->components[src].drag_visual_x == mx - 10, "no horizontal jump");
    TEST_ASSERT(tree->components[src].drag_visual_y == my - 5, "no vertical jump");

    /* Emit places the dragged source at drag_visual on the deferred pass. */
    {
        struct UITreeEmitBuffer buf;
        UITree_EmitBufferInit(&buf);
        UITree_EmitWalk(tree, &host, &buf, -1);
        int di = find_desc_id(&buf, TUID(9));
        TEST_ASSERT(di >= 0, "dragged item emitted");
        if( di >= 0 )
        {
            TEST_ASSERT(buf.cmds[di].x == mx - 10, "emit x at drag_visual_x");
            TEST_ASSERT(buf.cmds[di].y == my - 5, "emit y at drag_visual_y");
        }
        UITree_EmitBufferFree(&buf);
    }

    /* Drop target resolves at its DRAWN position... */
    int drop = UITree_FindDropTarget(tree, 130, 50, TUID(9));
    TEST_ASSERT(drop == TUID(11), "drop target found at drawn position");

    /* ...and NOT at its unscrolled content position (outside the viewport). */
    int drop_unscrolled = UITree_FindDropTarget(tree, 130, 170, TUID(9));
    TEST_ASSERT(drop_unscrolled != TUID(11), "no drop target at unscrolled position");

    UITree_Free(tree);
}
