#include "static_ui.h"

#include <stdlib.h>
#include <string.h>

static struct StaticUIComponent*
push_element(struct StaticUIBuffer* buffer)
{
    if( buffer->component_count >= buffer->component_capacity )
    {
        if( buffer->component_capacity == 0 )
            buffer->component_capacity = 16;

        uint32_t new_capacity =
            buffer->component_capacity == 0 ? 16 : buffer->component_capacity * 2;
        struct StaticUIComponent* new_components =
            realloc(buffer->components, new_capacity * sizeof(struct StaticUIComponent));
        if( !new_components )
            return NULL;
        buffer->components = new_components;
        buffer->component_capacity = new_capacity;
    }

    struct StaticUIComponent* component = &buffer->components[buffer->component_count++];
    return component;
}

char const*
static_ui_component_type_str(enum StaticUIComponentType type)
{
    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
        return "compass";
    case UIELEM_BUILTIN_MINIMAP:
        return "minimap";
    case UIELEM_BUILTIN_WORLD:
        return "world";
    case UIELEM_BUILTIN_SIDEBAR:
        return "sidebar";
    case UIELEM_BUILTIN_CHAT:
        return "chat";
    case UIELEM_BUILTIN_SPRITE:
        return "sprite";
    case UIELEM_BUILTIN_REDSTONE_TAB:
        return "redstone_tab";
    case UIELEM_BUILTIN_TAB_ICONS:
        return "tab_icons";
    case UIELEM_RS_TEXT:
        return "rs_text";
    case UIELEM_RS_GRAPHIC:
        return "rs_graphic";
    case UIELEM_RS_MODEL:
        return "rs_model";
    case UIELEM_RS_INV:
        return "rs_inv";
    case UIELEM_RS_LAYER:
        return "rs_layer";
    }
    return "unknown";
}

struct StaticUIBuffer*
static_ui_buffer_new(uint32_t hint)
{
    struct StaticUIBuffer* buffer = malloc(sizeof(struct StaticUIBuffer));
    if( !buffer )
        return NULL;
    memset(buffer, 0, sizeof(struct StaticUIBuffer));
    return buffer;
}

void
static_ui_buffer_free(struct StaticUIBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->components);
    free(buffer);
}

void
static_ui_buffer_push_sprite_xy(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    component->component_id = -1;
    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
}

#define STATIC_UI_RELATIVE_FLAG_LEFT 1
#define STATIC_UI_RELATIVE_FLAG_TOP 2
#define STATIC_UI_RELATIVE_FLAG_RIGHT 4
#define STATIC_UI_RELATIVE_FLAG_BOTTOM 8

void
static_ui_buffer_push_sprite_relative(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int atlas_index,
    int flags,
    int top,
    int right,
    int bottom,
    int left,
    int width,
    int height)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->component_id = -1;
    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_RELATIVE;
    component->position.relative_flags = flags;
    if( (flags & STATIC_UI_RELATIVE_FLAG_LEFT) != 0 )
        component->position.left = left;
    if( (flags & STATIC_UI_RELATIVE_FLAG_TOP) != 0 )
        component->position.top = top;
    if( (flags & STATIC_UI_RELATIVE_FLAG_RIGHT) != 0 )
        component->position.right = right;
    if( (flags & STATIC_UI_RELATIVE_FLAG_BOTTOM) != 0 )
        component->position.bottom = bottom;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
}

void
static_ui_buffer_push_world(
    struct StaticUIBuffer* buffer,
    int x,
    int y)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->component_id = -1;
    component->type = UIELEM_BUILTIN_WORLD;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
}

void
static_ui_buffer_push_compass(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->component_id = -1;
    component->type = UIELEM_BUILTIN_COMPASS;
    component->position.kind = UIPOS_XY;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
}

void
static_ui_buffer_push_minimap(
    struct StaticUIBuffer* buffer,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->component_id = -1;
    component->type = UIELEM_BUILTIN_MINIMAP;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
}

void
static_ui_buffer_push_redstone_tab(
    struct StaticUIBuffer* buffer,
    int tabno,
    int sprite_id,
    int atlas_index,
    int sprite_active_id,
    int atlas_active_index,
    int x,
    int y,
    int width,
    int height)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;
    memset(component, 0, sizeof(struct StaticUIComponent));
    component->component_id = -1;
    component->type = UIELEM_BUILTIN_REDSTONE_TAB;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.redstone_tab.tabno = tabno;
    component->u.redstone_tab.scene_id = sprite_id;
    component->u.redstone_tab.atlas_index = atlas_index;
    component->u.redstone_tab.scene_id_active = sprite_active_id;
    component->u.redstone_tab.atlas_index_active = atlas_active_index;
}

void
static_ui_buffer_push_builtin_sidebar(
    struct StaticUIBuffer* buffer,
    int tabno,
    int componentno,
    int x,
    int y,
    int width,
    int height)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;
    memset(component, 0, sizeof(struct StaticUIComponent));
    component->component_id = -1;
    component->type = UIELEM_BUILTIN_SIDEBAR;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sidebar.tabno = tabno;
    component->u.sidebar.componentno = componentno;
}
