#ifndef UI_INV_SLOT_VIEW_H
#define UI_INV_SLOT_VIEW_H

#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_INV_SLOT_ICON_SIZE 32

struct UIInvGridLayout
{
    int cols;
    int rows;
    int margin_x;
    int margin_y;
    int const* offset_x;
    int const* offset_y;
};

/** Compute top-left of slot `slot` within grid origin (bx, by). */
void
ui_inv_slot_view_grid_rect(
    int bx,
    int by,
    struct UIInvGridLayout const* layout,
    int slot,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/** Centered icon position inside (bx, by, bw, bh). */
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
    int* out_h);

bool
ui_inv_slot_view_hit_test_rect(
    int px,
    int py,
    int x,
    int y,
    int w,
    int h);

/** Hit-test grid; returns slot index or -1. */
int
ui_inv_slot_view_grid_hit_test(
    int bx,
    int by,
    struct UIInvGridLayout const* layout,
    int px,
    int py);

int
ui_inv_slot_view_grid_slot_limit(struct UIInvGridLayout const* layout);

#endif
