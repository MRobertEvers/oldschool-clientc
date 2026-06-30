#include "uitree_host.h"

#include "ui_behavior.h"
#include "uitree_layout.h"

#include <stdio.h>
#include <string.h>

void
uitree_host_init(struct UITreeHost* host)
{
    if( host )
        memset(host, 0, sizeof(*host));
}

bool
uitree_component_visible_host(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int32_t hovered_component,
    struct UITreeHost const* host)
{
    if( !component )
        return false;

    if( component->type == UIELEM_BUILTIN_REDSTONE_TAB )
    {
        if( !host || !host->get_selected_tab )
            return false;
        return host->get_selected_tab(host->user) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
    {
        if( !host || !host->get_selected_tab )
            return true;
        return host->get_selected_tab(host->user) == component->u.sidebar.tabno;
    }

    return uitree_component_visible(component, component_index, hovered_component);
}

bool
uitree_component_is_clickable_host(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host)
{
    if( !component )
        return false;

    switch( component->type )
    {
    case UIELEM_BUILTIN_TAB_ICONS:
    case UIELEM_BUILTIN_REDSTONE_TAB:
        return true;
    default:
        break;
    }

    (void)host;
    return uitree_component_is_clickable(component);
}

int
uitree_component_rect_color_host(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int32_t hovered_component,
    struct UITreeHost const* host,
    int base_color)
{
    struct UITreeBehaviorHost behavior_host = { 0 };
    if( host && host->is_active )
    {
        /* Legacy CSVM path unused when host provides is_active; rect color uses behavior fields. */
        (void)behavior_host;
    }
    return uitree_component_rect_color(
        component, component_index, hovered_component, NULL, base_color);
}

void
uitree_behavior_handle_click_host(
    struct UITreeHost* host,
    struct UITree const* tree,
    int32_t clicked_index)
{
    if( !host || !tree || clicked_index < 0 ||
        (uint32_t)clicked_index >= tree->component_count )
        return;

    struct StaticUIComponent const* component = &tree->components[clicked_index];

    switch( component->type )
    {
    case UIELEM_BUILTIN_TAB_ICONS:
        if( host->set_selected_tab )
            host->set_selected_tab(host->user, component->u.tab_icon.tabno);
        return;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        if( host->set_selected_tab )
            host->set_selected_tab(host->user, component->u.redstone_tab.tabno);
        return;
    default:
        break;
    }

    if( uitree_component_is_clickable(component) && host->apply_button_click )
        host->apply_button_click(host->user, component);
}

char const*
uitree_expand_text_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component,
    char* scratch,
    size_t scratch_size)
{
    if( !component || component->type != UIELEM_RS_TEXT || !component->u.rs_text.text )
        return component && component->type == UIELEM_RS_TEXT ? component->u.rs_text.text : "";

    if( !host || !host->eval_text_placeholder || !scratch || scratch_size < 2 )
        return component->u.rs_text.text;

    char const* src = component->u.rs_text.text;
    size_t di = 0;

    for( size_t i = 0; src[i] != '\0' && di + 1 < scratch_size; i++ )
    {
        if( src[i] == '%' && src[i + 1] >= '1' && src[i + 1] <= '5' )
        {
            int script_idx = src[i + 1] - '1';
            int val = host->eval_text_placeholder(host->user, component, script_idx);
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", val);
            if( n < 0 )
                n = 0;
            for( int j = 0; j < n && di + 1 < scratch_size; j++ )
                scratch[di++] = num[j];
            i++;
            continue;
        }
        scratch[di++] = src[i];
    }
    scratch[di] = '\0';
    return scratch;
}

bool
uitree_component_should_emit(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host)
{
    if( !component )
        return false;

    if( component->type == UIELEM_BUILTIN_REDSTONE_TAB )
    {
        if( !host || !host->get_selected_tab )
            return false;
        return host->get_selected_tab(host->user) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_TAB_ICONS )
        return true;

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
        return false;

    if( component->type == UIELEM_RS_LAYER )
        return false;

    return true;
}

int
uitree_component_sprite_rotation(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host)
{
    if( !component || !host || !host->get_camera_yaw )
        return 0;

    if( component->type == UIELEM_BUILTIN_COMPASS || component->type == UIELEM_BUILTIN_MINIMAP )
        return host->get_camera_yaw(host->user);

    return 0;
}
