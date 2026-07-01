#ifndef UITREE_LAYOUT_H
#define UITREE_LAYOUT_H

#include "uitree.h"

#include <stdint.h>

#define UITREE_LAYOUT_ROOT_W 765
#define UITREE_LAYOUT_ROOT_H 503
#define UITREE_SIDEBAR_PANEL_W 190
#define UITREE_SIDEBAR_PANEL_H 261
#define UITREE_RS_LAYOUT_UNITS 16384

void
uitree_layout_invalidate(struct UITree* tree);

void
uitree_layout_resolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h);

void
uitree_layout_get_bounds(
    struct StaticUIElemPosition const* position,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

#endif
