#include "test_harness.h"

/*
 * Obj icon emit placement. Reference (widgets-gl type-5 itemId path) draws the
 * 36x32 item icon at the widget rect with no draw-time centering; INV_SLOT
 * blits native at the slot origin plus the parent grid's per-slot offsets.
 */

static int
find_desc_idx(struct UITreeEmitBuffer const* buf, int component_id)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].component_id == component_id )
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

    /* Inv grid with per-slot offsets; slot 1 child at (10,10). */
    int32_t grid = UITree_TestPushXy(tree, -1, UIELEM_INV_GRID, 901, 200, 0, 100, 100);
    tree->components[grid].u.inv_grid.inv_slot_offset_x[1] = 5;
    tree->components[grid].u.inv_grid.inv_slot_offset_y[1] = 3;

    struct UITreeNodeSpec slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = UIELEM_INV_SLOT;
    slot.component_id = 902;
    slot.x = 10;
    slot.y = 10;
    slot.width = 36;
    slot.height = 32;
    slot.u.inv_slot.inv_source_id = 1;
    slot.u.inv_slot.slot = 1;
    int32_t si = UITree_Push(tree, grid, &slot);
    tree->components[si].item_id = 995;
    tree->components[si].item_count = 1000;
    tree->components[si].item_scene_id = 43;

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

    /* INV_SLOT: native blit at slot origin + per-slot grid offsets. */
    int siD = find_desc_idx(&buf, 902);
    TEST_ASSERT(siD >= 0, "inv slot emitted");
    if( siD >= 0 )
    {
        TEST_ASSERT(buf.cmds[siD].kind == UITREE_EMIT_INV_SLOT, "inv slot kind");
        TEST_ASSERT(buf.cmds[siD].x == 200 + 10 + 5, "slot x + offset_x[1]");
        TEST_ASSERT(buf.cmds[siD].y == 0 + 10 + 3, "slot y + offset_y[1]");
        TEST_ASSERT(buf.cmds[siD].w == 0 && buf.cmds[siD].h == 0, "native blit");
    }

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}
