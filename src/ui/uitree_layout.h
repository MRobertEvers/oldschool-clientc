#ifndef SRC_UITREE_LAYOUT_H
#define SRC_UITREE_LAYOUT_H

#include "uitree.h"

#include <stdint.h>

#define UITREE_LAYOUT_ROOT_W 765
#define UITREE_LAYOUT_ROOT_H 503
#define UITREE_SIDEBAR_PANEL_W 190
#define UITREE_SIDEBAR_PANEL_H 261

static inline int
UITree_MulShift14(int a, int b)
{
    return (int)(((int64_t)a * (int64_t)b) >> 14);
}

void
UITree_LayoutInvalidate(struct UITree* tree);

void
UITree_LayoutResolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h);

void
UITree_LayoutGetBounds(
    struct UITreeElemPosition const* position,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

#endif
