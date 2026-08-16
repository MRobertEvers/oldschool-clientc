#ifndef UI_HOVER_ROUTING_H
#define UI_HOVER_ROUTING_H

#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

/** Per-frame hover ids and cached builtin node indices for overlay routing. */
struct UIHoverRouting
{
    /** Deepest UITree node index under the cursor (-1 = none). */
    int32_t hovered_node;
    /** Client.ts overMainComId / overSideComId / overChatComId (-1 = none). */
    int over_main_com_id;
    int over_side_com_id;
    int over_chat_com_id;
    /** Previous-frame over ids; used to detect hover-driven UI dirty. */
    int over_main_com_id_prev;
    int over_side_com_id_prev;
    int over_chat_com_id_prev;
    /** UITree index of UIELEM_BUILTIN_MINIMENU (-1 until resolved). */
    int32_t minimenu_node;
    /** UITree index of UIELEM_BUILTIN_CHAT (-1 until resolved). */
    int32_t chat_node;
};

void
ui_hover_routing_reset(struct UIHoverRouting* routing);

void
ui_hover_routing_begin_frame(struct UIHoverRouting* routing);

bool
ui_hover_routing_commit_frame(struct UIHoverRouting* routing);

struct UITreeHoverIds
ui_hover_routing_to_ids(struct UIHoverRouting const* routing);

#endif
