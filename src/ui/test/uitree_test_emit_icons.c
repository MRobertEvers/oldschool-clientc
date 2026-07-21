#include "test_harness.h"
#include "uitree_inv_view.h"

/*
 * Obj icon emit placement. Reference (widgets-gl type-5 itemId path) draws the
 * 36x32 item icon at the widget rect with no draw-time centering; RS_INV expands
 * slots at emit (host GET_INV_SOURCE_SLOT) at grid_rect + per-slot offsets.
 */

static int
find_desc_idx(struct UITreeEmitBuffer const* buf, int component_id)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].component_id == component_id )
            return i;
    return -1;
}

static int
find_inv_slot_desc(struct UITreeEmitBuffer const* buf, int component_id, int slot)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].component_id == component_id && buf->cmds[i].inv_slot == slot )
            return i;
    return -1;
}

void
test_emit_icons(void)
{
    printf("TEST: obj icon emit placement\n");

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    /* Type-5 graphic with item overlay: 36x32 widget at (100,50). */
    struct UITreeNodeSpec gfx;
    memset(&gfx, 0, sizeof(gfx));
    gfx.type = UIELEM_RS_GRAPHIC;
    gfx.component_id = 900;
    gfx.x = 100;
    gfx.y = 50;
    gfx.width = 36;
    gfx.height = 32;
    gfx.u.rs_graphic.scene_id = 7; /* SETGRAPHIC chrome; item overlay wins */
    int32_t gi = UITree_Push(tree, -1, &gfx);
    tree->components[gi].item_id = 4151;
    tree->components[gi].item_count = 1;
    tree->components[gi].item_scene_id = 42;
    tree->components[gi].item_atlas_index = 0;

    /* RS_INV grid: slot 1 at col1/row0 with per-slot offsets. */
    int32_t grid = UITree_TestPushXy(tree, -1, UIELEM_RS_INV, 901, 200, 0, 100, 100);
    tree->components[grid].u.rs_inv.inv_source_id = 1;
    tree->components[grid].u.rs_inv.cols = 4;
    tree->components[grid].u.rs_inv.rows = 7;
    tree->components[grid].u.rs_inv.margin_x = 0;
    tree->components[grid].u.rs_inv.margin_y = 0;
    tree->components[grid].u.rs_inv.inv_slot_offset_x[1] = 5;
    tree->components[grid].u.rs_inv.inv_slot_offset_y[1] = 3;
    for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
        tree->components[grid].u.rs_inv.inv_slot_bg_scene_id[i] = -1;

    hs.inv_source_id = 1;
    hs.inv_slots[1].obj_id = 995;
    hs.inv_slots[1].obj_count = 1000;
    hs.inv_slots[1].scene_id = 43;
    hs.inv_slots[1].atlas_index = 0;
    hs.inv_slot_valid[1] = 1;

    UITree_TestResolve(tree);

    struct UITreeEmitBuffer buf;
    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);

    /* RS_GRAPHIC item overlay: exact widget rect, no centering shift. */
    int di = find_desc_idx(&buf, 900);
    TEST_ASSERT(di >= 0, "item graphic emitted");
    if( di >= 0 )
    {
        TEST_ASSERT(buf.cmds[di].kind == UITREE_EMIT_SPRITE, "item overlay is sprite");
        TEST_ASSERT(buf.cmds[di].scene_id == 42, "item overlay uses item scene id");
        TEST_ASSERT(buf.cmds[di].x == 100, "item overlay at widget x (no centering)");
        TEST_ASSERT(buf.cmds[di].y == 50, "item overlay at widget y (no centering)");
        TEST_ASSERT(buf.cmds[di].w == 36 && buf.cmds[di].h == 32, "item overlay widget size");
    }

    /* RS_INV: host-backed slot 1 at grid_rect + offsets (no INV_SLOT child). */
    int siD = find_inv_slot_desc(&buf, 901, 1);
    TEST_ASSERT(siD >= 0, "rs_inv slot 1 emitted");
    if( siD >= 0 )
    {
        int expect_x = 200 + UITREE_INV_SLOT_ICON_SIZE + 5; /* col1 * 32 + offset */
        int expect_y = 0 + 3;
        TEST_ASSERT(buf.cmds[siD].kind == UITREE_EMIT_SPRITE, "inv cell is sprite");
        TEST_ASSERT(buf.cmds[siD].scene_id == 43, "inv cell scene");
        TEST_ASSERT(buf.cmds[siD].x == expect_x, "slot x + offset_x[1]");
        TEST_ASSERT(buf.cmds[siD].y == expect_y, "slot y + offset_y[1]");
        TEST_ASSERT(
            buf.cmds[siD].w == UITREE_INV_SLOT_ICON_SIZE &&
                buf.cmds[siD].h == UITREE_INV_SLOT_ICON_SIZE,
            "32x32 icon");
    }

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}
