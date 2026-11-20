/*
 * A dragged item cell draws on top of its interface group, under the group's
 * clip -- not in tree order under its own layer's.
 *
 * Reference: rev 239 class163.method5467 skips the clicked widget and
 * method5464/method5478 draw it once the group's tree is done (sentinel
 * -1412584499), clipped to what the group was drawn under.
 *
 * The scene is the bank: interface 12 mounted into a modal container of the
 * gameframe, its items layer first and the tab strip after it. Dragging an
 * item up onto the "+" tab to make a new tab put the item under the strip and
 * cut it off at the items layer's top edge. A later gameframe sibling stays on
 * top of the dragged item, as the group order says.
 */
#include "test_harness.h"

static int
desc_at(struct UITreeEmitBuffer const* buffer, int32_t node)
{
    for( int i = 0; i < buffer->count; i++ )
        if( buffer->cmds[i].node_index == node && buffer->cmds[i].kind == UITREE_EMIT_RECT )
            return i;
    return -1;
}

void test_held_drag_over_group(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    int const top = 161 << 16;
    int const bank = 12 << 16;
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, top, 0, 0, 765, 503);
    int32_t modal = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, top | 1, 10, 10, 400, 300);
    int32_t cover = UITree_TestPushXy(tree, root, UIELEM_RS_RECT, top | 3, 0, 0, 100, 100);
    int32_t bank_root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, bank, 0, 0, 400, 300);
    int32_t items = UITree_TestPushXy(tree, bank_root, UIELEM_RS_LAYER, bank | 13, 0, 40, 300, 200);
    int32_t cell = UITree_TestPushXy(tree, items, UIELEM_RS_RECT, bank | 20, 10, 10, 36, 32);
    int32_t tabs = UITree_TestPushXy(tree, bank_root, UIELEM_RS_RECT, bank | 10, 0, 0, 200, 40);
    struct UITreeEmitDesc const* d;
    int at_cell, at_tabs, at_cover;

    (void)UITree_InterfaceParentSet(tree, top | 1, 12, 0);
    UITree_Reparent(tree, bank_root, modal);
    tree->layer_clip_compounds = 1;
    UITree_TestHostInit(&host, &state);
    UITree_EmitBufferInit(&buffer);
#define PUBLISH() do { UITree_TestResolve(tree); buffer.count = 0; UITree_EmitWalk(tree, &host, &buffer, -1); } while( 0 )

    /* Nothing armed: tree order, under the items layer's clip. */
    PUBLISH();
    at_cell = desc_at(&buffer, cell);
    at_tabs = desc_at(&buffer, tabs);
    at_cover = desc_at(&buffer, cover);
    TEST_ASSERT(at_cell >= 0 && at_tabs >= 0 && at_cover >= 0, "the cell, the tab strip and the cover draw");
    d = &buffer.cmds[at_cell];
    TEST_ASSERT(at_cell < at_tabs, "at rest the cell draws in tree order, before the strip");
    TEST_ASSERT(d->clip.x == 10 && d->clip.y == 50 && d->clip.w == 300 && d->clip.h == 200,
                "at rest the cell is clipped to the items layer");

    /* Armed and carried 40 px up, onto the strip. */
    state.inv_drag_armed = 1;
    state.inv_drag_component = bank | 20;
    state.inv_drag_dx = 0;
    state.inv_drag_dy = -40;
    PUBLISH();
    at_cell = desc_at(&buffer, cell);
    at_tabs = desc_at(&buffer, tabs);
    at_cover = desc_at(&buffer, cover);
    TEST_ASSERT(at_cell >= 0 && at_tabs >= 0 && at_cover >= 0, "dragging keeps all three");
    d = &buffer.cmds[at_cell];
    printf("HELD_DRAG cell@%d tabs@%d cover@%d y=%d trans=%d clip=%d,%d %dx%d\n",
           at_cell, at_tabs, at_cover, d->y, d->trans, d->clip.x, d->clip.y, d->clip.w, d->clip.h);
    TEST_ASSERT(at_cell > at_tabs, "the dragged cell draws over the tab strip");
    TEST_ASSERT(at_cell < at_cover, "a later gameframe sibling still draws over the bank's dragged cell");
    TEST_ASSERT(d->y == 20 && d->trans == 128, "the dragged cell follows the drag, at alpha 128");
    TEST_ASSERT(d->clip.x == 10 && d->clip.y == 10 && d->clip.w == 400 && d->clip.h == 300,
                "the dragged cell is clipped to the clip the bank was mounted under");
    TEST_ASSERT(buffer.held_drag_count == 0, "nothing is left held after the walk");
#undef PUBLISH
    UITree_EmitBufferFree(&buffer);
    UITree_Free(tree);
}
