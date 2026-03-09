#include "static_ui.h"

#include <stdlib.h>

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
static_ui_buffer_push_xy(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int x,
    int y)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    component->type = UIELEM_SPRITE;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->sprite_id = sprite_id;
}

#define STATIC_UI_RELATIVE_FLAG_LEFT 1
#define STATIC_UI_RELATIVE_FLAG_TOP 2
#define STATIC_UI_RELATIVE_FLAG_RIGHT 4
#define STATIC_UI_RELATIVE_FLAG_BOTTOM 8

void
static_ui_buffer_push_relative(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int flags,
    int top,
    int right,
    int bottom,
    int left)
{
    struct StaticUIComponent* component = push_element(buffer);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->type = UIELEM_SPRITE;
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
    component->sprite_id = sprite_id;
}
