#include "ui_hover_routing.h"

void
ui_hover_routing_reset(struct UIHoverRouting* routing)
{
    if( !routing )
        return;
    routing->hovered_node = -1;
    routing->over_main_com_id = -1;
    routing->over_side_com_id = -1;
    routing->over_chat_com_id = -1;
    routing->over_main_com_id_prev = -1;
    routing->over_side_com_id_prev = -1;
    routing->over_chat_com_id_prev = -1;
    routing->minimenu_node = -1;
    routing->chat_node = -1;
}

void
ui_hover_routing_begin_frame(struct UIHoverRouting* routing)
{
    if( !routing )
        return;
    routing->hovered_node = -1;
    routing->over_main_com_id = -1;
    routing->over_side_com_id = -1;
    routing->over_chat_com_id = -1;
}

bool
ui_hover_routing_commit_frame(struct UIHoverRouting* routing)
{
    if( !routing )
        return false;

    if( routing->over_main_com_id != routing->over_main_com_id_prev ||
        routing->over_side_com_id != routing->over_side_com_id_prev ||
        routing->over_chat_com_id != routing->over_chat_com_id_prev )
    {
        routing->over_main_com_id_prev = routing->over_main_com_id;
        routing->over_side_com_id_prev = routing->over_side_com_id;
        routing->over_chat_com_id_prev = routing->over_chat_com_id;
        return true;
    }
    return false;
}

struct UITreeHoverIds
ui_hover_routing_to_ids(struct UIHoverRouting const* routing)
{
    struct UITreeHoverIds ids = {
        .main_com_id = -1,
        .side_com_id = -1,
        .chat_com_id = -1,
    };
    if( !routing )
        return ids;
    ids.main_com_id = routing->over_main_com_id;
    ids.side_com_id = routing->over_side_com_id;
    ids.chat_com_id = routing->over_chat_com_id;
    return ids;
}
