#include "uitree_input.h"

#include "uitree_layout.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

bool
UITree_PointInComponent(
    struct UITreeElemPosition const* position,
    int px,
    int py)
{
    if( !position )
        return false;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    UITree_LayoutGetBounds(position, &x, &y, &w, &h);
    if( w <= 0 || h <= 0 )
        return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool
rs_node_is_decorative_passthrough(struct UITreeComponent const* component)
{
    if( component->behavior.button_type != 0 || component->behavior.client_code != 0 )
        return false;
    if( component->behavior.click_mask != 0 )
        return false;
    if( component->menu_options.option[0] != '\0' )
        return false;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( component->menu_options.ops[i][0] != '\0' )
            return false;
    }
    return true;
}

bool
UITree_ComponentIsPassThrough(
    struct UITreeComponent const* component,
    struct UITreeHost const* host)
{
    assert(component);

    /* Runtime click/op/drag hooks make the node a real click target even if it is a
     * layer or decorative graphic. */
    if( component->runtime_hooks.on_click.script_id > 0 ||
        component->runtime_hooks.on_op.script_id > 0 ||
        component->runtime_hooks.on_drag.script_id > 0 ||
        UITree_ComponentIsDraggable(component) )
        return false;

    switch( component->type )
    {
    case UIELEM_BUILTIN_WORLD:
    case UIELEM_RS_LAYER:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
        return true;
    case UIELEM_BUILTIN_CROSS:
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CROSS_ACTIVE };
        return UITree_Host(host, &req) == 0;
    }
    case UIELEM_BUILTIN_MINIMENU:
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return UITree_Host(host, &req) == 0;
    }
    case UIELEM_INV_GRID:
    case UIELEM_INV_SLOT:
    case UIELEM_RS_INV_TEXT:
        return true;
    case UIELEM_RS_GRAPHIC:
    case UIELEM_RS_TEXT:
    case UIELEM_RS_RECT:
    case UIELEM_RS_MODEL:
    case UIELEM_RS_LINE:
        return rs_node_is_decorative_passthrough(component);
    default:
        return false;
    }
}

/*
 * Returns the topmost interactive hit in the subtree, or -1. When a node with
 * no_click_through geometrically contains the point, *out_blocks is set so the
 * caller discards anything rendered *under* this subtree (mirrors the reference's
 * collectWidgetsAtPoint noClickThrough slice — a modal panel eats click-through).
 */
static int32_t
hit_test_interactive_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_index,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    int* out_blocks)
{
    assert(tree);
    if( out_blocks )
        *out_blocks = 0;
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !UITree_PointInClip(px, py, clip) )
        return -1;

    struct UITreeComponent const* component = &tree->components[node_index];

    /* Match emit: hidden subtrees are not interactive. */
    if( component->behavior.hide )
        return -1;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    bool const point_in_self =
        UITree_PointInScrolledBounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    int32_t hit = -1;
    if( point_in_self && !UITree_ComponentIsPassThrough(component, host) &&
        UITree_ComponentHitTestVisibleHost(component, -1, host) )
        hit = node_index;

    /* A no_click_through node covering the point blocks click-through to nodes
     * rendered underneath it (even if the node itself is a passthrough container). */
    int blocks = (point_in_self && component->no_click_through) ? 1 : 0;

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

    bool recurse_children = true;
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            recurse_children = false;
    }

    if( recurse_children )
    {
        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int child_blocks = 0;
            int32_t child_hit = hit_test_interactive_recursive(
                tree, host, scroll, child, px, py,
                child_scroll_x, child_scroll_y, &child_clip, &child_blocks);
            /* Later siblings render on top. A blocking child also discards this
             * node's own hit and earlier siblings. */
            if( child_blocks )
            {
                hit = child_hit;
                blocks = 1;
            }
            else if( child_hit >= 0 )
                hit = child_hit;
        }
    }

    if( out_blocks )
        *out_blocks = blocks;
    return hit;
}

int32_t
UITree_HitTestRecursive(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    struct UITreeComponent const* component = &tree->components[node_index];

    /* Match emit: hidden subtrees are not interactive. */
    if( component->behavior.hide )
        return -1;

    int32_t hit = -1;
    /* Layers are containers only — do not claim the hit themselves. Empty overlay
     * layers otherwise steal clicks from hooked widgets underneath. */
    if( component->type != UIELEM_RS_LAYER &&
        UITree_PointInComponent(&component->position, px, py) )
        hit = node_index;

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t child_hit = UITree_HitTestRecursive(tree, child, px, py);
        if( child_hit >= 0 )
            hit = child_hit;
    }

    return hit;
}

int32_t
UITree_HitTest(
    struct UITree const* tree,
    int px,
    int py)
{
    assert(tree);
    if( tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
    {
        int32_t root_hit = UITree_HitTestRecursive(tree, root, px, py);
        if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

int32_t
UITree_HitTestInteractive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int px,
    int py)
{
    assert(tree);
    if( tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
    {
        int root_blocks = 0;
        int32_t root_hit = hit_test_interactive_recursive(
            tree, host, scroll, root, px, py, 0, 0, NULL, &root_blocks);
        /* Later roots render on top. A no_click_through root captures the point
         * and discards hits from roots underneath (even if it has no hit itself). */
        if( root_blocks )
            hit = root_hit;
        else if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

struct UIInputResult
UITree_InputUpdate(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    struct UIInputEvent event)
{
    struct UIInputResult result;
    int32_t const prev_hovered = state ? state->hovered : -1;

    memset(&result, 0, sizeof(result));
    result.hovered = state ? state->hovered : -1;
    result.prev_hovered = prev_hovered;
    result.clicked = -1;
    result.drag_source_idx = -1;
    result.drag_source_id = -1;
    result.drag_target_id = -1;

    assert(state);
    assert(tree);

    switch( event.kind )
    {
    case UI_INPUT_MOVE:
        state->hovered = UITree_HitTestInteractive(tree, host, scroll, event.x, event.y);
        break;

    case UI_INPUT_DOWN:
        state->hovered = UITree_HitTestInteractive(tree, host, scroll, event.x, event.y);
        state->pressed = state->hovered;
        state->drag_active = 0;
        state->drag_source_idx = -1;
        state->drag_source_id = -1;
        state->drag_target_id = -1;
        state->deferred_click = 0;
        state->drag_duration = 0;
        state->drag_click_x = event.x;
        state->drag_click_y = event.y;
        state->thresholds_set = 0;
        if( state->pressed >= 0 && (uint32_t)state->pressed < tree->component_count )
        {
            struct UITreeComponent const* c = &tree->components[state->pressed];
            int bx = 0, by = 0, bw = 0, bh = 0;
            UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
            state->drag_pickup_x = event.x - bx;
            state->drag_pickup_y = event.y - by;
            if( !tree->anti_drag && UITree_ComponentIsDraggable(c) )
            {
                /* Defer click until mouseup if drag never starts. */
                state->deferred_click = 1;
                state->drag_source_idx = state->pressed;
                state->drag_source_id = c->component_id;
            }
        }
        break;

    case UI_INPUT_UP:
    {
        int32_t const up_hit = UITree_HitTestInteractive(tree, host, scroll, event.x, event.y);
        state->hovered = up_hit;
        if( state->drag_active )
        {
            result.drag_ended = 1;
            result.drag_source_idx = state->drag_source_idx;
            result.drag_source_id = state->drag_source_id;
            result.drag_target_id = state->drag_target_id;
            if( state->drag_source_idx >= 0 &&
                (uint32_t)state->drag_source_idx < tree->component_count )
            {
                struct UITreeComponent* src = &tree->components[state->drag_source_idx];
                src->drag_active = 0;
                src->drag_visual_trans = -1;
            }
            /* Cancel deferred click. */
            state->deferred_click = 0;
        }
        else if( state->deferred_click && state->pressed >= 0 && state->pressed == up_hit )
        {
            result.clicked = up_hit;
            result.deferred_click_fired = 1;
        }
        else if( !state->deferred_click && state->pressed >= 0 && state->pressed == up_hit )
        {
            result.clicked = up_hit;
        }
        state->pressed = -1;
        state->deferred_click = 0;
        state->drag_active = 0;
        state->drag_duration = 0;
        break;
    }
    }

    result.hovered = state->hovered;
    result.prev_hovered = prev_hovered;
    result.hover_changed = state->hovered != prev_hovered;
    return result;
}

int
UITree_InputDragTick(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    int mouse_x,
    int mouse_y,
    int left_held)
{
    struct UITreeComponent* src;
    int changed = 0;
    int dist;
    int zone;
    int threshold;
    int target_x;
    int target_y;

    assert(state);
    assert(tree);
    (void)host;

    if( !left_held || state->drag_source_idx < 0 ||
        (uint32_t)state->drag_source_idx >= tree->component_count )
    {
        return 0;
    }

    src = &tree->components[state->drag_source_idx];
    if( tree->anti_drag && !state->drag_active )
        return 0;

    zone = src->drag_dead_zone;
    threshold = src->drag_dead_time;
    state->drag_duration++;

    {
        int dx = mouse_x - state->drag_click_x;
        int dy = mouse_y - state->drag_click_y;
        if( dx < 0 )
            dx = -dx;
        if( dy < 0 )
            dy = -dy;
        dist = dx > dy ? dx : dy;
    }

    if( !state->drag_active )
    {
        if( state->drag_duration > threshold && dist > zone )
        {
            state->drag_active = 1;
            src->drag_active = 1;
            src->drag_visual_trans = (src->drag_behavior == 1) ? -1 : 128;
            changed = 1;
        }
        else
            return 0;
    }

    target_x = mouse_x - state->drag_pickup_x;
    target_y = mouse_y - state->drag_pickup_y;

    /* Clamp only with explicit drag parent (flag depth or drag_render_area). */
    {
        int depth = UITree_ClickMaskDragDepth(src->behavior.click_mask);
        int32_t clamp_idx = -1;
        if( src->drag_render_area_uid >= 0 )
            clamp_idx = UITree_FindByComponentId(tree, src->drag_render_area_uid);
        else if( depth > 0 )
        {
            int d;
            clamp_idx = src->parent;
            for( d = 1; d < depth && clamp_idx >= 0; d++ )
                clamp_idx = tree->components[clamp_idx].parent;
        }
        if( clamp_idx >= 0 && (uint32_t)clamp_idx < tree->component_count )
        {
            int cx, cy, cw, ch;
            UITree_LayoutGetBounds(&tree->components[clamp_idx].position, &cx, &cy, &cw, &ch);
            if( target_x < cx )
                target_x = cx;
            if( target_y < cy )
                target_y = cy;
            if( cw > 0 && target_x > cx + cw - 1 )
                target_x = cx + cw - 1;
            if( ch > 0 && target_y > cy + ch - 1 )
                target_y = cy + ch - 1;
        }
    }

    if( src->drag_visual_x != target_x || src->drag_visual_y != target_y )
        changed = 1;
    src->drag_visual_x = target_x;
    src->drag_visual_y = target_y;

    state->drag_target_id =
        UITree_FindDropTarget(tree, mouse_x, mouse_y, state->drag_source_id);

    /* Script mouse coords: position relative to drag parent + scroll. */
    {
        int32_t parent_idx = src->parent;
        int px = 0, py = 0, pw = 0, ph = 0;
        int scroll_x = 0, scroll_y = 0;
        if( src->drag_render_area_uid >= 0 )
            parent_idx = UITree_FindByComponentId(tree, src->drag_render_area_uid);
        if( parent_idx >= 0 && (uint32_t)parent_idx < tree->component_count )
        {
            UITree_LayoutGetBounds(
                &tree->components[parent_idx].position, &px, &py, &pw, &ph);
            scroll_x = tree->components[parent_idx].scroll_x;
            scroll_y = tree->components[parent_idx].scroll_y;
        }
        tree->components[state->drag_source_idx].drag_visual_x = target_x;
        /* Store script-space mouse on the tree via unused fields? Host sets these in main. */
        (void)scroll_x;
        (void)scroll_y;
        (void)px;
        (void)py;
        state->drag_click_x = state->drag_click_x; /* keep original press for threshold */
    }

    return changed || state->drag_active;
}
