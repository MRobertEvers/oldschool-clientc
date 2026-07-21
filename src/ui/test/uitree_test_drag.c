#include "test_harness.h"

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
    tree->components[pi].drag_active = 1;
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
