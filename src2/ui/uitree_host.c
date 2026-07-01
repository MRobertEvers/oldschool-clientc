#include "uitree_host.h"

#include "ui_behavior.h"
#include "ui_chat_minimenu.h"
#include "ui_scroll.h"
#include "uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
uitree_host_init(struct UITreeHost* host)
{
    assert(host);
    memset(host, 0, sizeof(*host));
}

bool
uitree_component_visible_host(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host)
{
    assert(component);

    if( component->type == UIELEM_BUILTIN_REDSTONE_TAB )
    {
        assert(host);
        assert(host->get_selected_tab);
        return host->get_selected_tab(host->user) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
    {
        assert(host);
        assert(host->get_selected_tab);
        return host->get_selected_tab(host->user) == component->u.sidebar.tabno;
    }

    if( component->type == UIELEM_BUILTIN_CROSS )
    {
        return host && host->get_cross_active && host->get_cross_active(host->user);
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU )
    {
        return host && host->get_minimenu_visible && host->get_minimenu_visible(host->user);
    }

    return uitree_component_visible_by_hover_ids(component, hover_ids);
}

bool
uitree_component_hit_test_visible_host(
    struct StaticUIComponent const* component,
    int hovered_component_id,
    struct UITreeHost const* host)
{
    assert(component);
    (void)host;

    if( component->type == UIELEM_BUILTIN_TAB_ICONS ||
        component->type == UIELEM_BUILTIN_REDSTONE_TAB )
        return true;

    return uitree_component_visible_by_id(component, hovered_component_id);
}

bool
uitree_component_is_active_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component)
{
    if( !component || !component->behavior.script_comparator )
        return false;
    if( host && host->is_active )
        return host->is_active(host->user, component);
    return false;
}

char const*
uitree_component_text_source_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component)
{
    if( !component || component->type != UIELEM_RS_TEXT )
        return NULL;

    bool active = uitree_component_is_active_host(host, component);
    if( active && component->u.rs_text.text_active && component->u.rs_text.text_active[0] != '\0' )
        return component->u.rs_text.text_active;
    return component->u.rs_text.text;
}

int
uitree_component_text_color_host(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host,
    int base_color)
{
    if( !component )
        return base_color;

    int color = base_color;
    bool hovered = uitree_component_hovered_by_ids(component->component_id, hover_ids);
    bool active = uitree_component_is_active_host(host, component);

    if( active )
        color = component->behavior.active_color ? component->behavior.active_color : color;
    if( hovered )
    {
        if( active && component->behavior.active_over_color != 0 )
            color = component->behavior.active_over_color;
        else if( !active && component->behavior.over_color != 0 )
            color = component->behavior.over_color;
    }
    return color;
}

bool
uitree_component_is_clickable_host(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host)
{
    assert(component);

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
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host,
    int base_color)
{
    assert(host);
    assert(component);

    int color = base_color;
    bool hovered = uitree_component_hovered_by_ids(component->component_id, hover_ids);
    bool active = uitree_component_is_active_host(host, component);

    if( active )
        color = component->behavior.active_color ? component->behavior.active_color : color;
    if( hovered )
    {
        if( active && component->behavior.active_over_color != 0 )
            color = component->behavior.active_over_color;
        else if( !active && component->behavior.over_color != 0 )
            color = component->behavior.over_color;
    }
    return color;
}

static int
uitree_sidebar_componentno_for_tab(struct UITree const* tree, int tabno)
{
    if( !tree )
        return -1;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->type == UIELEM_BUILTIN_SIDEBAR && c->u.sidebar.tabno == tabno )
            return c->u.sidebar.componentno;
    }
    return -1;
}

void
uitree_behavior_handle_click_host(
    struct UITreeHost* host,
    struct UITree const* tree,
    int32_t clicked_index)
{
    assert(host);
    assert(tree);
    if( clicked_index < 0 || (uint32_t)clicked_index >= tree->component_count )
        return;

    struct StaticUIComponent const* component = &tree->components[clicked_index];

    switch( component->type )
    {
    case UIELEM_BUILTIN_TAB_ICONS:
        if( host->set_selected_tab )
        {
            int tabno = component->u.tab_icon.tabno;
            if( uitree_sidebar_componentno_for_tab(tree, tabno) >= 0 )
                host->set_selected_tab(host->user, tabno);
        }
        return;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        if( host->set_selected_tab )
        {
            int tabno = component->u.redstone_tab.tabno;
            if( uitree_sidebar_componentno_for_tab(tree, tabno) >= 0 )
                host->set_selected_tab(host->user, tabno);
        }
        return;
    case UIELEM_BUILTIN_CHAT_BUTTON:
        ui_chat_button_handle_click(host->user, component);
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
    assert(host);
    assert(component);
    assert(scratch);
    assert(scratch_size > 0);

    char const* src = uitree_component_text_source_host(host, component);
    if( !src || src[0] == '\0' )
        return src;

    if( component->type != UIELEM_RS_TEXT || !host->eval_text_placeholder )
        return src;
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
    assert(component);
    assert(host);

    if( component->type == UIELEM_BUILTIN_REDSTONE_TAB )
    {
        assert(host->get_selected_tab);
        return host->get_selected_tab(host->user) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_TAB_ICONS )
        return true;

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
        return false;

    if( component->type == UIELEM_BUILTIN_CHAT )
        return false;

    if( component->type == UIELEM_BUILTIN_CROSS )
    {
        return host->get_cross_active && host->get_cross_active(host->user);
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU )
    {
        return host->get_minimenu_visible && host->get_minimenu_visible(host->user);
    }

    if( component->type == UIELEM_RS_LAYER )
    {
        if( uitree_scroll_layer_needs_vertical(component) ||
            uitree_scroll_layer_needs_horizontal(component) )
            return true;
        return false;
    }

    return true;
}

int
uitree_component_sprite_rotation(
    struct StaticUIComponent const* component,
    struct UITreeHost const* host)
{
    assert(component);
    assert(host);

    if( component->type == UIELEM_BUILTIN_COMPASS || component->type == UIELEM_BUILTIN_MINIMAP )
        return host->get_camera_yaw(host->user);

    return 0;
}
