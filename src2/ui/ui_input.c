#include "ui_input.h"
#include "uitree_layout.h"

bool
uitree_point_in_component(
    struct StaticUIElemPosition const* position,
    int px,
    int py)
{
    if( !position )
        return false;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    uitree_layout_get_bounds(position, &x, &y, &w, &h);
    if( w <= 0 || h <= 0 )
        return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

int32_t
uitree_hit_test_recursive(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    struct StaticUIComponent const* component = &tree->components[node_index];

    int32_t hit = -1;
    if( uitree_point_in_component(&component->position, px, py) )
        hit = node_index;

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t child_hit = uitree_hit_test_recursive(tree, child, px, py);
        if( child_hit >= 0 )
            hit = child_hit;
    }

    return hit;
}

int32_t
uitree_hit_test(
    struct UITree const* tree,
    int px,
    int py)
{
    if( !tree || tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
    {
        int32_t root_hit = uitree_hit_test_recursive(tree, root, px, py);
        if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

struct UIInputResult
uitree_input_update(
    struct UIInputState* state,
    struct UITree const* tree,
    struct UIInputEvent event)
{
    struct UIInputResult result = {
        .hovered = state ? state->hovered : -1,
        .prev_hovered = state ? state->hovered : -1,
        .clicked = -1,
        .hover_changed = false,
    };

    if( !state || !tree )
        return result;

    int32_t const prev_hovered = state->hovered;

    switch( event.kind )
    {
    case UI_INPUT_MOVE:
        state->hovered = uitree_hit_test(tree, event.x, event.y);
        break;

    case UI_INPUT_DOWN:
        state->hovered = uitree_hit_test(tree, event.x, event.y);
        state->pressed = state->hovered;
        break;

    case UI_INPUT_UP:
    {
        int32_t const up_hit = uitree_hit_test(tree, event.x, event.y);
        state->hovered = up_hit;
        if( state->pressed >= 0 && state->pressed == up_hit )
            result.clicked = up_hit;
        state->pressed = -1;
        break;
    }
    }

    result.hovered = state->hovered;
    result.prev_hovered = prev_hovered;
    result.hover_changed = state->hovered != prev_hovered;
    return result;
}
