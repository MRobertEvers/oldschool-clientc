#ifndef SRC_UITREE_HOVER_H
#define SRC_UITREE_HOVER_H

#include "uitree.h"
#include "uitree_host.h"
#include "uitree_scroll.h"

#include <stdbool.h>
#include <stdint.h>

struct UIHoverRouting
{
    int32_t hovered_node;
    int over_main_com_id;
    int over_side_com_id;
    int over_chat_com_id;
    int over_main_com_id_prev;
    int over_side_com_id_prev;
    int over_chat_com_id_prev;
    int32_t minimenu_node;
    int32_t chat_node;
};

void
UITree_HoverRoutingReset(struct UIHoverRouting* routing);

void
UITree_HoverRoutingBeginFrame(struct UIHoverRouting* routing);

bool
UITree_HoverRoutingCommitFrame(struct UIHoverRouting* routing);

struct UITreeHoverIds
UITree_HoverRoutingToIds(struct UIHoverRouting const* routing);

/** Deepest cache component id under cursor in a rectangular region (-1 if none). */
int
UITree_FindHoveredComponentIdForRegion(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t root_index,
    int mouse_x,
    int mouse_y,
    int region_x,
    int region_y,
    int region_w,
    int region_h);

#endif
