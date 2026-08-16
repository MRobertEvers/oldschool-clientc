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

int
uitree_host(struct UITreeHost const* host, struct UITreeHostRequest* req)
{
    assert(req);

    if( host && host->request )
        return host->request(host->user, req);

    switch( req->kind )
    {
    case UITREE_HOST_IS_ACTIVE:
    case UITREE_HOST_GET_CROSS_ACTIVE:
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
    case UITREE_HOST_SCENE_SPRITE_HAS:
    case UITREE_HOST_SCENE_FONT_HAS:
    case UITREE_HOST_SCENE_MODEL_HAS:
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        return 0;
    case UITREE_HOST_APPLY_BUTTON_CLICK:
    case UITREE_HOST_SET_SELECTED_TAB:
    case UITREE_HOST_GET_MINIMAP_ANCHOR:
    case UITREE_HOST_GET_WORLD_MAP_SIZE:
    case UITREE_HOST_GET_CROSS_POSITION:
    case UITREE_HOST_GET_MINIMENU_LAYOUT:
    case UITREE_HOST_SET_INV_SOURCE_SLOT:
        return 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
    case UITREE_HOST_GET_SELECTED_TAB:
    case UITREE_HOST_GET_CAMERA_YAW:
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
    case UITREE_HOST_GET_MINIMENU_HOVERED_OPTION:
        return 0;
    }
    return 0;
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
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        return uitree_host(host, &req) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        return uitree_host(host, &req) == component->u.sidebar.tabno;
    }

    if( component->type == UIELEM_BUILTIN_CROSS )
    {
        assert(host);

        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CROSS_ACTIVE };
        return uitree_host(host, &req) != 0;
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU )
    {
        assert(host);

        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return uitree_host(host, &req) != 0;
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
    assert(component);
    assert(component);
    assert(host);

    struct UITreeHostRequest req = {
        .kind = UITREE_HOST_IS_ACTIVE,
        .u.is_active.component = component,
    };
    return uitree_host(host, &req) != 0;
}

char const*
uitree_component_text_source_host(
    struct UITreeHost const* host,
    struct StaticUIComponent const* component)
{
    assert(component);
    if( component->type != UIELEM_RS_TEXT )
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
    assert(tree);

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
    {
        int tabno = component->u.tab_icon.tabno;
        if( uitree_sidebar_componentno_for_tab(tree, tabno) >= 0 )
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_SET_SELECTED_TAB,
                .u.set_selected_tab.tabno = tabno,
            };
            uitree_host(host, &req);
        }
        return;
    }
    case UIELEM_BUILTIN_REDSTONE_TAB:
    {
        int tabno = component->u.redstone_tab.tabno;
        if( uitree_sidebar_componentno_for_tab(tree, tabno) >= 0 )
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_SET_SELECTED_TAB,
                .u.set_selected_tab.tabno = tabno,
            };
            uitree_host(host, &req);
        }
        return;
    }
    case UIELEM_BUILTIN_CHAT_BUTTON:
        ui_chat_button_handle_click(host->user, component);
        return;
    default:
        break;
    }

    if( uitree_component_is_clickable(component) )
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_APPLY_BUTTON_CLICK,
            .u.apply_button_click.component = component,
        };
        uitree_host(host, &req);
    }
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
    assert(src);
    if( src[0] == '\0' )
        return src;

    if( component->type != UIELEM_RS_TEXT )
        return src;

    size_t di = 0;

    for( size_t i = 0; src[i] != '\0' && di + 1 < scratch_size; i++ )
    {
        if( src[i] == '%' && src[i + 1] >= '1' && src[i + 1] <= '5' )
        {
            int script_idx = src[i + 1] - '1';
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_EVAL_TEXT_PLACEHOLDER,
                .u.eval_text_placeholder.component = component,
                .u.eval_text_placeholder.script_idx = script_idx,
            };
            int val = uitree_host(host, &req);
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
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        return uitree_host(host, &req) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_TAB_ICONS )
        return true;

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
        return false;

    if( component->type == UIELEM_BUILTIN_CHAT )
        return false;

    if( component->type == UIELEM_BUILTIN_CROSS )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CROSS_ACTIVE };
        return uitree_host(host, &req) != 0;
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return uitree_host(host, &req) != 0;
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
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CAMERA_YAW };
        return uitree_host(host, &req);
    }

    return 0;
}
