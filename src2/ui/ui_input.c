#include "ui_input.h"

#include "uitree_host.h"
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

static bool
uitree_rs_node_is_decorative_passthrough(struct StaticUIComponent const* component)
{
    if( component->behavior.button_type != 0 || component->behavior.client_code != 0 )
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
uitree_component_is_pass_through(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host)
{
    if( !component )
        return true;

    switch( component->type )
    {
    case UIELEM_BUILTIN_WORLD:
    case UIELEM_RS_LAYER:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
        return true;
    case UIELEM_BUILTIN_CROSS:
        return !host || !host->get_cross_active || !host->get_cross_active(host->user);
    case UIELEM_BUILTIN_MINIMENU:
        return !host || !host->get_minimenu_visible || !host->get_minimenu_visible(host->user);
    case UIELEM_RS_GRAPHIC:
    case UIELEM_RS_TEXT:
    case UIELEM_RS_INV_TEXT:
    case UIELEM_RS_RECT:
    case UIELEM_RS_MODEL:
    case UIELEM_RS_LINE:
        return uitree_rs_node_is_decorative_passthrough(component);
    default:
        return false;
    }
}

static int32_t
uitree_hit_test_interactive_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t node_index,
    int px,
    int py)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    struct StaticUIComponent const* component = &tree->components[node_index];

    int32_t hit = -1;
    if( uitree_point_in_component(&component->position, px, py) &&
        !uitree_component_is_pass_through(component, host) &&
        uitree_component_hit_test_visible_host(component, -1, host) )
        hit = node_index;

    bool recurse_children = true;
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host && host->get_selected_tab )
    {
        if( host->get_selected_tab(host->user) != component->u.sidebar.tabno )
            recurse_children = false;
    }

    if( recurse_children )
    {
        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int32_t child_hit = uitree_hit_test_interactive_recursive(tree, host, child, px, py);
            if( child_hit >= 0 )
                hit = child_hit;
        }
    }

    return hit;
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
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int32_t root_hit = uitree_hit_test_recursive(tree, root, px, py);
        if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

int32_t
uitree_hit_test_interactive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    if( !tree || tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int32_t root_hit = uitree_hit_test_interactive_recursive(tree, host, root, px, py);
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
