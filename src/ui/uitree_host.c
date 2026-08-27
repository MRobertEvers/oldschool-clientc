#include "uitree_host.h"

#include "uitree_scroll.h"

#include <assert.h>
#include <string.h>

_Static_assert(UITREE_HOST_INPUT_DOMAIN_COUNT > 0, "host input stamp needs a domain");
_Static_assert(UITREE_HOST_INPUT_DOMAIN_COUNT < 32, "host input mask is uint32_t");

void
UITree_HostInit(struct UITreeHost* host)
{
    assert(host);
    memset(host, 0, sizeof(*host));
}

UITreeHostInputMask
UITree_HostRequestInputMask(enum UITreeHostRequestKind kind)
{
    UITreeHostInputMask const client = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE);
    UITreeHostInputMask const camera = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CAMERA);
    UITreeHostInputMask const pointer = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_POINTER);
    UITreeHostInputMask const inventory = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY);
    UITreeHostInputMask const assets = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS);
    UITreeHostInputMask const world = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_WORLD);
    UITreeHostInputMask const animation = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ANIMATION);
    UITreeHostInputMask const overlays = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_OVERLAYS);

    switch( kind )
    {
    /* Commands mutate the host but contribute no value to the current emit.
     * Their authoritative implementations must bump the affected input epoch. */
    case UITREE_HOST_APPLY_BUTTON_CLICK:
    case UITREE_HOST_SET_SELECTED_TAB:
    case UITREE_HOST_SET_INV_SOURCE_SLOT:
    case UITREE_HOST_CYCLE_CHAT_FILTER_MODE:
    case UITREE_HOST_BEGIN_OVERLAYS:
    case UITREE_HOST_SET_ROLE_OVERLAY_CLIP:
    case UITREE_HOST_TITLE_ACTION:
        return 0;

    /* CS1 can read both ordinary client variables and inventory counts. */
    case UITREE_HOST_IS_ACTIVE:
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
        return client | inventory;

    case UITREE_HOST_GET_SELECTED_TAB:
    case UITREE_HOST_GET_TAB_ENABLED:
    case UITREE_HOST_GET_CHAT_FILTER_MODE:
    case UITREE_HOST_GET_CHAT_STATE:
    case UITREE_HOST_GET_IF_EVENTS:
        return client;

    case UITREE_HOST_GET_CAMERA_YAW:
        return camera;

    case UITREE_HOST_GET_CROSS_ACTIVE:
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
    case UITREE_HOST_GET_CROSS_POSITION:
        return pointer | animation;

    case UITREE_HOST_GET_MINIMENU_VISIBLE:
    case UITREE_HOST_GET_MINIMENU_STATE:
    case UITREE_HOST_GET_HOVERTEXT_STATE:
        return pointer | client;

    case UITREE_HOST_MEASURE_TEXT:
    case UITREE_HOST_SCENE_SPRITE_HAS:
    case UITREE_HOST_SCENE_FONT_HAS:
    case UITREE_HOST_SCENE_MODEL_HAS:
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
    case UITREE_HOST_GET_INV_COUNT_FONT:
    case UITREE_HOST_GET_OBJ_NAME:
    case UITREE_HOST_GET_OBJ_ICON_PLAIN:
    case UITREE_HOST_GET_OBJ_ICON_BORDERED:
        return assets;

    case UITREE_HOST_GET_INV_SOURCE_SLOT:
    case UITREE_HOST_GET_INV_SELECTION:
        return inventory;

    case UITREE_HOST_GET_INV_DRAG:
        return pointer | inventory;

    case UITREE_HOST_GET_INV_SELECT_ICON:
        return inventory | assets;

    case UITREE_HOST_GET_MINIMAP_STATE:
        return camera | world | assets;

    case UITREE_HOST_GET_MINIMAP_HIDDEN:
        return client | world;

    case UITREE_HOST_GET_MULTIWAY:
        return world;

    case UITREE_HOST_GET_REBOOT_TIMER:
    case UITREE_HOST_GET_TAB_FLASH_HIDDEN:
        return client | animation;

    /* Which screen is up, what is typed and what the server replied are all
     * client state; the caret's blink is the clock, which is why the field
     * line also reads animation. Without that bit the gate retains a frame
     * whose caret should have flipped and the cursor freezes. */
    case UITREE_HOST_GET_TITLE_SCREEN:
    case UITREE_HOST_GET_TITLE_MESSAGE:
    case UITREE_HOST_GET_TITLE_PROGRESS:
        return client;

    case UITREE_HOST_GET_TITLE_FIELD:
        return client | animation;

    case UITREE_HOST_GET_MINIMAP_DOTS:
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        return camera | world | overlays;

    case UITREE_HOST_GET_CANVAS_OVERLAYS:
    case UITREE_HOST_GET_ROLE_OVERLAY_GROUPS:
    case UITREE_HOST_GET_FRAME_OVERLAYS:
        return overlays;

    case UITREE_HOST_GET_WORLDMAP_TILES:
    case UITREE_HOST_GET_WORLDMAP_OVERVIEW:
        return camera | world | assets | overlays;

    case UITREE_HOST_GET_DEBUG_OVERLAY:
        return overlays | assets;

    case UITREE_HOST_REQUEST_COUNT:
        break;
    }

    /* A new request which has not been classified must make retention more
     * conservative, never make a stale list look reusable. */
    return UITREE_HOST_INPUT_ALL;
}

void
UITree_HostInputsChanged(struct UITreeHost* host, UITreeHostInputMask changed)
{
    assert(host);

    changed &= UITREE_HOST_INPUT_ALL;
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
    {
        uint64_t* epoch;

        if( !(changed & UITREE_HOST_INPUT_BIT(domain)) )
            continue;
        epoch = &host->input_epoch[domain];
        (*epoch)++;
        /* Keep zero as the initial value. Skipping it also makes a debugger's
         * all-zero stamp unambiguously mean "never changed". */
        if( *epoch == 0 )
            (*epoch)++;
    }
}

bool
UITree_HostPublishInputSignature(
    struct UITreeHost* host,
    enum UITreeHostInputDomain domain,
    uint64_t signature)
{
    UITreeHostInputMask bit;

    assert(host);
    if( domain < 0 || domain >= UITREE_HOST_INPUT_DOMAIN_COUNT )
        return false;
    bit = UITREE_HOST_INPUT_BIT(domain);
    if( (host->input_signature_valid & bit) && host->input_signature[domain] == signature )
        return false;

    host->input_signature[domain] = signature;
    host->input_signature_valid |= bit;
    UITree_HostInputsChanged(host, bit);
    return true;
}

void
UITree_HostInputStampCapture(
    struct UITreeHost const* host,
    UITreeHostInputMask dependencies,
    struct UITreeHostInputStamp* out)
{
    assert(out);

    memset(out, 0, sizeof(*out));
    out->source = host;
    out->dependencies = dependencies & UITREE_HOST_INPUT_ALL;
    if( !host )
        return;
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
        if( out->dependencies & UITREE_HOST_INPUT_BIT(domain) )
            out->epoch[domain] = host->input_epoch[domain];
}

bool
UITree_HostInputStampIsCurrent(
    struct UITreeHostInputStamp const* stamp,
    struct UITreeHost const* host)
{
    assert(stamp);

    if( stamp->source != host )
        return false;
    if( !host )
        return true;
    for( int domain = 0; domain < UITREE_HOST_INPUT_DOMAIN_COUNT; domain++ )
        if( (stamp->dependencies & UITREE_HOST_INPUT_BIT(domain)) &&
            stamp->epoch[domain] != host->input_epoch[domain] )
            return false;
    return true;
}

int
UITree_Host(struct UITreeHost const* host, struct UITreeHostRequest* req)
{
    assert(req);

    if( host && host->observed_input_mask )
        *host->observed_input_mask |= UITree_HostRequestInputMask(req->kind);

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
    case UITREE_HOST_GET_OBJ_ICON_BORDERED:
    /* Hostless answers, all three "no": the server has taken nothing away, the
     * player is not in a multi-combat zone, and no update is pending. Each is
     * the state a tree with no session is genuinely in, not a placeholder. */
    case UITREE_HOST_GET_MINIMAP_HIDDEN:
    case UITREE_HOST_GET_MULTIWAY:
    case UITREE_HOST_GET_REBOOT_TIMER:
    /* A tree with no session is not on the title screen, has nothing typed,
     * no reply to show and no bar running -- all four are the honest answer
     * rather than a placeholder, and each makes its widget draw nothing. */
    case UITREE_HOST_GET_TITLE_FIELD:
    case UITREE_HOST_GET_TITLE_MESSAGE:
    case UITREE_HOST_GET_TITLE_PROGRESS:
    case UITREE_HOST_TITLE_ACTION:
    case UITREE_HOST_GET_MINIMAP_DOTS:
    case UITREE_HOST_BEGIN_OVERLAYS:
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
    case UITREE_HOST_GET_ROLE_OVERLAY_GROUPS:
    case UITREE_HOST_SET_ROLE_OVERLAY_CLIP:
    case UITREE_HOST_GET_FRAME_OVERLAYS:
    case UITREE_HOST_GET_WORLDMAP_TILES:
    case UITREE_HOST_GET_WORLDMAP_OVERVIEW:
    case UITREE_HOST_GET_DEBUG_OVERLAY:
    case UITREE_HOST_GET_IF_EVENTS:
        return 0;
    case UITREE_HOST_APPLY_BUTTON_CLICK:
    case UITREE_HOST_SET_SELECTED_TAB:
    case UITREE_HOST_SET_INV_SOURCE_SLOT:
        return 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
    case UITREE_HOST_GET_SELECTED_TAB:
    case UITREE_HOST_GET_CAMERA_YAW:
    case UITREE_HOST_GET_TAB_ENABLED:
    /* 0 = "not hidden", which is the right answer for a tree with no host:
     * nothing is flashing, so nothing is blinked out. */
    case UITREE_HOST_GET_TAB_FLASH_HIDDEN:
    case UITREE_HOST_GET_CHAT_FILTER_MODE:
    case UITREE_HOST_CYCLE_CHAT_FILTER_MODE:
    case UITREE_HOST_GET_CHAT_STATE:
    case UITREE_HOST_GET_OBJ_NAME:
        return 0;
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
    case UITREE_HOST_GET_MINIMAP_STATE:
    case UITREE_HOST_GET_INV_COUNT_FONT:
    /* -1 is "not on the title screen", which 0 could not say: 0 is a real
     * screen (the front menu). */
    case UITREE_HOST_GET_TITLE_SCREEN:
        return -1;
    case UITREE_HOST_REQUEST_COUNT:
        return 0;
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
