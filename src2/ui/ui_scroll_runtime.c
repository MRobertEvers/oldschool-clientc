#include "ui_scroll_runtime.h"

#include <string.h>

static bool
ui_scroll_runtime_drag_kind_is_grip(enum UITreeScrollbarHitKind kind)
{
    switch( kind )
    {
    case UITREE_SCROLLBAR_V_GRIP:
    case UITREE_SCROLLBAR_V_TRACK:
    case UITREE_SCROLLBAR_H_GRIP:
    case UITREE_SCROLLBAR_H_TRACK:
        return true;
    default:
        return false;
    }
}

void
ui_scroll_runtime_reset(struct UIScrollRuntime* rt)
{
    if( !rt )
        return;
    memset(rt->scroll_x, 0, sizeof(rt->scroll_x));
    memset(rt->scroll_y, 0, sizeof(rt->scroll_y));
    rt->drag_layer_id = -1;
    rt->drag_kind = UITREE_SCROLLBAR_NONE;
    rt->drag_anchor = 0;
    rt->grabbed = false;
    rt->scrollbar0_scene_id = -1;
    rt->scrollbar1_scene_id = -1;
}

struct UITreeScrollState
ui_scroll_runtime_view(struct UIScrollRuntime const* rt)
{
    struct UITreeScrollState view = { 0 };
    if( !rt )
        return view;
    view.scroll_x = (int*)rt->scroll_x;
    view.scroll_y = (int*)rt->scroll_y;
    return view;
}

void
ui_scroll_runtime_begin_drag(
    struct UIScrollRuntime* rt,
    int layer_id,
    enum UITreeScrollbarHitKind kind,
    int anchor)
{
    if( !rt )
        return;
    rt->drag_layer_id = layer_id;
    rt->drag_kind = kind;
    rt->drag_anchor = anchor;
    rt->grabbed = true;
}

void
ui_scroll_runtime_end_drag(struct UIScrollRuntime* rt)
{
    if( !rt )
        return;
    rt->drag_layer_id = -1;
    rt->drag_kind = UITREE_SCROLLBAR_NONE;
    rt->drag_anchor = 0;
    rt->grabbed = false;
}

bool
ui_scroll_runtime_is_grip_dragging(struct UIScrollRuntime const* rt)
{
    return rt && rt->grabbed && rt->drag_layer_id >= 0 &&
           ui_scroll_runtime_drag_kind_is_grip(rt->drag_kind);
}
