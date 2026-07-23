#include "uitree_inv_view.h"

#include <assert.h>

void
UITree_InvViewGridRect(
    int bx,
    int by,
    struct UITreeInvGridLayout const* layout,
    int slot,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int const icon = UITREE_INV_SLOT_ICON_SIZE;
    int cols = layout && layout->cols > 0 ? layout->cols : 4;
    int rows = layout && layout->rows > 0 ? layout->rows : 7;
    int margin_x = layout ? layout->margin_x : 0;
    int margin_y = layout ? layout->margin_y : 0;

    int col = slot % cols;
    int row = slot / cols;
    int x = bx + col * (margin_x + icon);
    int y = by + row * (margin_y + icon);

    if( layout && slot >= 0 && slot < UI_INV_SLOT_OFFSET_MAX )
    {
        if( layout->offset_x )
            x += layout->offset_x[slot];
        if( layout->offset_y )
            y += layout->offset_y[slot];
    }

    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_w )
        *out_w = icon;
    if( out_h )
        *out_h = icon;
    (void)rows;
}

bool
UITree_InvViewHitTestRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int
UITree_InvViewGridHitTest(
    int bx,
    int by,
    struct UITreeInvGridLayout const* layout,
    int px,
    int py)
{
    assert(layout);

    int const limit = UITree_InvViewGridSlotLimit(layout);
    for( int slot = 0; slot < limit; slot++ )
    {
        int sx = 0;
        int sy = 0;
        int sw = 0;
        int sh = 0;
        UITree_InvViewGridRect(bx, by, layout, slot, &sx, &sy, &sw, &sh);
        if( UITree_InvViewHitTestRect(px, py, sx, sy, sw, sh) )
            return slot;
    }
    return -1;
}

int
UITree_InvViewGridSlotLimit(struct UITreeInvGridLayout const* layout)
{
    assert(layout);

    int cols = layout->cols > 0 ? layout->cols : 4;
    int rows = layout->rows > 0 ? layout->rows : 7;
    /* The grid has width*height (cols*rows) slots. UI_INV_SLOT_OFFSET_MAX (20)
     * bounds ONLY the per-slot pixel-offset and background arrays (reference
     * invBackgroundX/Y, invBackground, all `if (slot < 20)`); it is NOT a cap
     * on the inventory size. Every inv (e.g. 28-slot backpack, banks) draws all
     * its slots; slots >= 20 simply have no custom offset/background. */
    return cols * rows;
}
