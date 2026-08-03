#include "uitree_host.h"

#include "uitree_scroll.h"

#include <assert.h>
#include <string.h>

void
UITree_HostInit(struct UITreeHost* host)
{
    assert(host);
    memset(host, 0, sizeof(*host));
}

int
UITree_Host(struct UITreeHost const* host, struct UITreeHostRequest* req)
{
    assert(req);

    if( host && host->request )
        return host->request(host->user, req);

    switch( req->kind )
    {
    case UITREE_HOST_IS_ACTIVE:
    case UITREE_HOST_GET_CROSS_ACTIVE:
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
    case UITREE_HOST_GET_CROSS_POSITION:
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
    case UITREE_HOST_GET_MINIMENU_STATE:
    case UITREE_HOST_GET_HOVERTEXT_STATE:
    case UITREE_HOST_MEASURE_TEXT:
    case UITREE_HOST_SCENE_SPRITE_HAS:
    case UITREE_HOST_SCENE_FONT_HAS:
    case UITREE_HOST_SCENE_MODEL_HAS:
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
    case UITREE_HOST_GET_INV_DRAG:
    case UITREE_HOST_GET_INV_SELECT_ICON:
    case UITREE_HOST_GET_INV_SELECTION:
    case UITREE_HOST_GET_OBJ_ICON_PLAIN:
    case UITREE_HOST_GET_MINIMAP_DOTS:
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        return 0;
    case UITREE_HOST_APPLY_BUTTON_CLICK:
    case UITREE_HOST_SET_SELECTED_TAB:
    case UITREE_HOST_SET_INV_SOURCE_SLOT:
        return 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
    case UITREE_HOST_GET_SELECTED_TAB:
    case UITREE_HOST_GET_CAMERA_YAW:
    case UITREE_HOST_GET_TAB_ENABLED:
    case UITREE_HOST_GET_CHAT_FILTER_MODE:
    case UITREE_HOST_CYCLE_CHAT_FILTER_MODE:
    case UITREE_HOST_GET_CHAT_STATE:
    case UITREE_HOST_GET_OBJ_NAME:
        return 0;
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
    case UITREE_HOST_GET_MINIMAP_STATE:
    case UITREE_HOST_GET_INV_COUNT_FONT:
        return -1;
    }
    return 0;
}

bool
UITree_ComponentVisibleHost(
    struct UITreeComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeHost const* host)
{
    assert(component);

    if( component->type == UIELEM_BUILTIN_REDSTONE_TAB )
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        return UITree_Host(host, &req) == component->u.redstone_tab.tabno;
    }

    if( component->type == UIELEM_BUILTIN_SIDEBAR )
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        return UITree_Host(host, &req) == component->u.sidebar.tabno;
    }

    if( component->type == UIELEM_BUILTIN_CROSS )
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CROSS_ACTIVE };
        return UITree_Host(host, &req) != 0;
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU )
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return UITree_Host(host, &req) != 0;
    }

    return UITree_ComponentVisibleByHoverIds(component, hover_ids);
}

bool
UITree_ComponentHitTestVisibleHost(
    struct UITreeComponent const* component,
    int hovered_component_id,
    struct UITreeHost const* host)
{
    assert(component);
    (void)host;

    if( component->type == UIELEM_BUILTIN_TAB_ICONS ||
        component->type == UIELEM_BUILTIN_REDSTONE_TAB )
        return true;

    return UITree_ComponentVisibleById(component, hovered_component_id);
}

bool
UITree_ComponentIsActiveHost(
    struct UITreeHost const* host,
    struct UITreeComponent const* component)
{
    assert(component);
    assert(host);

    struct UITreeHostRequest req = {
        .kind = UITREE_HOST_IS_ACTIVE,
        .u.is_active.component = component,
    };
    return UITree_Host(host, &req) != 0;
}

bool
UITree_ComponentShouldEmit(
    struct UITreeComponent const* component,
    struct UITreeHost const* host)
{
    assert(component);
    assert(host);

    if( component->type == UIELEM_BUILTIN_REDSTONE_TAB )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        return UITree_Host(host, &req) == component->u.redstone_tab.tabno;
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
        return UITree_Host(host, &req) != 0;
    }

    if( component->type == UIELEM_BUILTIN_MINIMENU )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return UITree_Host(host, &req) != 0;
    }

    if( component->type == UIELEM_RS_LAYER )
    {
        if( UITree_ScrollLayerNeedsVertical(component) ||
            UITree_ScrollLayerNeedsHorizontal(component) )
            return true;
        return false;
    }

    return true;
}

int
UITree_ComponentSpriteRotation(
    struct UITreeComponent const* component,
    struct UITreeHost const* host)
{
    assert(component);

    if( component->type == UIELEM_BUILTIN_COMPASS || component->type == UIELEM_BUILTIN_MINIMAP )
    {
        if( !host )
            return 0;
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CAMERA_YAW };
        return UITree_Host(host, &req);
    }

    return 0;
}
