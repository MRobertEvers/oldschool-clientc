#include "uitree_hover.h"

#include "uitree_layout.h"

#include <assert.h>
#include <stddef.h>

void
UITree_HoverRoutingReset(struct UIHoverRouting* routing)
{
    assert(routing);

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
UITree_HoverRoutingBeginFrame(struct UIHoverRouting* routing)
{
    assert(routing);

    routing->hovered_node = -1;
    routing->over_main_com_id = -1;
    routing->over_side_com_id = -1;
    routing->over_chat_com_id = -1;
}

bool
UITree_HoverRoutingCommitFrame(struct UIHoverRouting* routing)
{
    assert(routing);

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
UITree_HoverRoutingToIds(struct UIHoverRouting const* routing)
{
    struct UITreeHoverIds ids = {
        .main_com_id = -1,
        .side_com_id = -1,
        .chat_com_id = -1,
    };
    assert(routing);

    ids.main_com_id = routing->over_main_com_id;
    ids.side_com_id = routing->over_side_com_id;
    ids.chat_com_id = routing->over_chat_com_id;
    return ids;
}

static bool
layer_blocks_hover_find(struct UITreeComponent const* component)
{
    return component &&
           (component->type == UIELEM_RS_LAYER || component->type == UIELEM_BUILTIN_SIDEBAR) &&
           component->behavior.hide;
}

static void
find_hovered_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_index,
    int mouse_x,
    int mouse_y,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    int* out_hovered_component_id)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 &&
        !UITree_PointInClip(mouse_x, mouse_y, clip) )
        return;

    struct UITreeComponent const* component = &tree->components[node_index];

    if( layer_blocks_hover_find(component) )
        return;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    bool const mouse_in_bounds = UITree_PointInScrolledBounds(
        mouse_x, mouse_y, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    if( mouse_in_bounds && component->component_id >= 0 )
    {
        /* IF1 over-layer / colourOver redirect (TS addComponentOptions). */
        if( component->behavior.over_layer_id >= 0 )
            *out_hovered_component_id = component->behavior.over_layer_id;
        else if( component->behavior.over_color != 0 )
            *out_hovered_component_id = component->component_id;
        /* CS2/IF3 addition: components with hover scripts must also report as
         * hovered so on_mouse_over / on_mouse_leave dispatch (main loop). */
        else if( component->runtime_hooks.on_mouse_over.script_id > 0 ||
                 component->runtime_hooks.on_mouse_leave.script_id > 0 )
            *out_hovered_component_id = component->component_id;
    }

    bool recurse_children = mouse_in_bounds;
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            recurse_children = false;
    }

    if( !recurse_children )
        return;

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };

    if( component->type == UIELEM_RS_LAYER )
    {
        UITree_ScrollIntersectClip(&child_clip, bx, by, bw, bh);
        if( scroll && UITree_ScrollLayerNeedsHorizontal(component) &&
            component->component_id >= 0 )
        {
            int sx = 0;
            UITree_ScrollGetPos(scroll, component->component_id, &sx, NULL);
            child_scroll_x += sx;
        }
        if( scroll && UITree_ScrollLayerNeedsVertical(component) &&
            component->component_id >= 0 )
        {
            int sy = 0;
            UITree_ScrollGetPos(scroll, component->component_id, NULL, &sy);
            child_scroll_y += sy;
        }
    }

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        find_hovered_recursive(
            tree,
            host,
            scroll,
            child,
            mouse_x,
            mouse_y,
            child_scroll_x,
            child_scroll_y,
            &child_clip,
            out_hovered_component_id);
    }
}

int
UITree_FindHoveredComponentIdForRegion(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t root_index,
    int mouse_x,
    int mouse_y,
    int region_x,
    int region_y,
    int region_w,
    int region_h)
{
    assert(tree);

    int hovered_component_id = -1;

    if( region_w <= 0 || region_h <= 0 )
        return -1;

    if( mouse_x < region_x || mouse_y < region_y ||
        mouse_x >= region_x + region_w || mouse_y >= region_y + region_h )
        return -1;

    if( root_index >= 0 )
    {
        if( (uint32_t)root_index >= tree->component_count )
            return -1;
        find_hovered_recursive(
            tree, host, scroll, root_index,
            mouse_x, mouse_y, 0, 0, NULL,
            &hovered_component_id);
        return hovered_component_id;
    }

    if( tree->root_index < 0 )
        return -1;

    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
    {
        find_hovered_recursive(
            tree, host, scroll, root,
            mouse_x, mouse_y, 0, 0, NULL,
            &hovered_component_id);
    }
    return hovered_component_id;
}
