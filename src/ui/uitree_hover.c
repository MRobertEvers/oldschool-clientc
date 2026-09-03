#include "uitree_hover.h"

#include "perf/torirs_perf.h"
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

static void
find_hovered_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t node_index,
    int mouse_x,
    int mouse_y,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeScrollClip const* surface,
    int* out_hovered_component_id)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_HOVER, 1);

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 &&
        !UITree_PointInClip(mouse_x, mouse_y, clip) )
        return;

    struct UITreeComponent const* component = &tree->components[node_index];

    /* Match hit-test / emit: any hidden node is pruned (no self-report, no
     * children). IF_SETHIDE on type=5 spell icons must stop on_mouse_repeat
     * from firing — otherwise a later hidden Lumbridge icon overwrites a
     * visible jewellery-enchant sibling (last-match-wins). IF1 overlayer
     * tooltips stay correct: the visible cell redirects via over_layer_id;
     * the hidden tooltip layer never needs to self-report. */
    if( component->behavior.hide || component->frame_hidden ||
        component->replacement_hidden || component->projection_hidden )
        return;

    /* Inactive sidebar tabs contribute nothing — gate FIRST (like the emit
     * walk), before this node can self-report as hovered via over_layer_id /
     * over_color / hover hooks below. */
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            return;
    }

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    bool const mouse_in_bounds = UITree_PointInScrolledBounds(
        mouse_x, mouse_y, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    /* field4189/noClickThrough clears mouse events queued by widgets rendered
     * underneath before this widget contributes its own events. Preserve that
     * ordering here: discard the previous hover, then let this node/children
     * become hovered below. */
    if( !component->replacement_input_hidden && mouse_in_bounds &&
        component->no_click_through )
        *out_hovered_component_id = -1;

    if( !component->replacement_input_hidden && mouse_in_bounds &&
        component->component_id >= 0 )
    {
        /* IF1 over-layer / colourOver redirect (TS addComponentOptions). */
        if( component->behavior.over_layer_id >= 0 )
            *out_hovered_component_id = component->behavior.over_layer_id;
        else if( component->behavior.over_color != 0 )
            *out_hovered_component_id = component->component_id;
        /* CS2/IF3 addition: components with hover scripts must also report as
         * hovered so on_mouse_over / on_mouse_leave / on_mouse_repeat dispatch
         * (main loop). */
        else if( UITree_Hooks(component)->on_mouse_over.script_id > 0 ||
                 UITree_Hooks(component)->on_mouse_leave.script_id > 0 ||
                 UITree_Hooks(component)->on_mouse_repeat.script_id > 0 )
            *out_hovered_component_id = component->component_id;
    }

    /* Inactive sidebar tabs already returned above; recursion only depends on
     * the mouse being inside this node's bounds. */
    if( !mouse_in_bounds )
        return;

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };
    struct UITreeScrollClip child_surface = surface ? *surface : (struct UITreeScrollClip){ 0 };

    /* Same shared clip rule as the emit walk (UITree_LayerChildClip), so hover
     * matches drawn pixels: own bounds ∩ enclosing surface, never compounded
     * with ancestor layers. Screen coords (emit clips at x - scroll_off). */
    {
        struct UITreeScrollClip cc, cs;
        /* Collapsed clipping layer: nothing under it is drawn, so nothing under
         * it can be hit either (same rule as emit_walk_node). */
        if( UITree_LayerCullsChildren(component, bw, bh) )
            return;
        if( UITree_LayerChildClip(
                component, surface, bx - scroll_off_x, by - scroll_off_y, bw, bh, &cc, &cs) )
        {
            child_clip = cc;
            child_surface = cs;
        }
    }
    if( component->type == UIELEM_RS_LAYER )
    {
        int effective_scroll_x;
        int effective_scroll_y;
        UITree_ScrollGetClamped(component, &effective_scroll_x, &effective_scroll_y);
        if( UITree_ScrollLayerNeedsHorizontal(component) )
            child_scroll_x += effective_scroll_x;
        if( UITree_ScrollLayerNeedsVertical(component) )
            child_scroll_y += effective_scroll_y;
    }

    /* Rendering visits InterfaceParent roots after ordinary children and does
     * not apply the host's own scroll to them. Hover must use that same order
     * and origin or a mounted control can draw in one place and fire hover
     * hooks in another. Type-0 capture also clears hover selected underneath
     * the host before the mounted subtree gets its turn. */
    int const mount_rec = UITree_InterfaceParentFind(tree, component->component_id);
    int const has_mounts = mount_rec >= 0;
    int const mount_type = has_mounts ? tree->interface_parents[mount_rec].type : -1;
    for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
    {
        if( mount_sweep == 1 && mount_type == 0 )
            *out_hovered_component_id = -1;

        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int const is_mount =
                has_mounts &&
                UITree_ChildMountType(
                    tree, component->component_id, &tree->components[child]) >= 0;
            if( is_mount != mount_sweep )
                continue;
            find_hovered_recursive(
                tree,
                host,
                child,
                mouse_x,
                mouse_y,
                is_mount ? scroll_off_x : child_scroll_x,
                is_mount ? scroll_off_y : child_scroll_y,
                &child_clip,
                &child_surface,
                out_hovered_component_id);
        }
    }
}

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
            tree, host, root_index,
            mouse_x, mouse_y, 0, 0, NULL, NULL,
            &hovered_component_id);
        return hovered_component_id;
    }

    if( tree->root_index < 0 )
        return -1;

    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
    {
        if( !UITree_RootIsDisplayable(tree, root) )
            continue;
        find_hovered_recursive(
            tree, host, root,
            mouse_x, mouse_y, 0, 0, NULL, NULL,
            &hovered_component_id);
    }
    return hovered_component_id;
}
