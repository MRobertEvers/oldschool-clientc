#ifndef SRC_UITREE_INV_VIEW_H
#define SRC_UITREE_INV_VIEW_H

#include "uitree.h"

#include <stdbool.h>

#define UITREE_INV_SLOT_ICON_SIZE 32

struct UITreeInvGridLayout
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
UITree_InvViewGridRect(
    int bx,
    int by,
    struct UITreeInvGridLayout const* layout,
    int slot,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

bool
UITree_InvViewHitTestRect(int px, int py, int x, int y, int w, int h);

/** Hit-test grid; returns slot index or -1. */
int
UITree_InvViewGridHitTest(
    int bx,
    int by,
    struct UITreeInvGridLayout const* layout,
    int px,
    int py);

int
UITree_InvViewGridSlotLimit(struct UITreeInvGridLayout const* layout);

#endif
