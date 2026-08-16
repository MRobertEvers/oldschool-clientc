#include "ui_inv_slot_view.h"
#include <assert.h>

void
ui_inv_slot_view_grid_rect(
    int bx,
    int by,
    struct UIInvGridLayout const* layout,
    int slot,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int const icon = UI_INV_SLOT_ICON_SIZE;
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

void
ui_inv_slot_view_centered_rect(
    int bx,
    int by,
    int bw,
    int bh,
    int icon_size,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    if( icon_size <= 0 )
        icon_size = UI_INV_SLOT_ICON_SIZE;

    int x = bx + (bw > icon_size ? (bw - icon_size) / 2 : 0);
    int y = by + (bh > icon_size ? (bh - icon_size) / 2 : 0);

    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_w )
        *out_w = icon_size;
    if( out_h )
        *out_h = icon_size;
}

bool
ui_inv_slot_view_hit_test_rect(
    int px,
    int py,
    int x,
    int y,
    int w,
    int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int
ui_inv_slot_view_grid_hit_test(
    int bx,
    int by,
    struct UIInvGridLayout const* layout,
    int px,
    int py)
{
    assert(layout);

    int const limit = ui_inv_slot_view_grid_slot_limit(layout);
    for( int slot = 0; slot < limit; slot++ )
    {
        int sx = 0;
        int sy = 0;
        int sw = 0;
        int sh = 0;
        ui_inv_slot_view_grid_rect(bx, by, layout, slot, &sx, &sy, &sw, &sh);
        if( ui_inv_slot_view_hit_test_rect(px, py, sx, sy, sw, sh) )
            return slot;
    }
    return -1;
}

int
ui_inv_slot_view_grid_slot_limit(struct UIInvGridLayout const* layout)
{
    assert(layout);

    int cols = layout->cols > 0 ? layout->cols : 4;
    int rows = layout->rows > 0 ? layout->rows : 7;
    int total = cols * rows;
    if( total > UI_INV_SLOT_OFFSET_MAX )
        total = UI_INV_SLOT_OFFSET_MAX;
    return total;
}
