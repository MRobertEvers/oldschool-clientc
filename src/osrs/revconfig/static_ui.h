#ifndef STATIC_INTERFACE_H
#define STATIC_INTERFACE_H

#include <stdint.h>

enum StaticUIComponentType
{
    UIELEM_COMPASS = 1,
    UIELEM_MINIMAP = 2,
    // Builtin - when the server specifies a tab etc, it will update
    // the specified builtin sidebar.
    // IF_SETTAB, IF_OPENSIDE
    UIELEM_BUILTIN_SIDEBAR = 3,
    // IF_OPENCHAT,
    UIELEM_BUILTIN_CHAT = 4,
    // Usually coincides with "WORLD" but in resizable it's different.
    // Bank Interfaces
    // IF_OPENMAIN
    UIELEM_BUILTIN_VIEWPORT = 5,
    UIELEM_WORLD = 6,
    UIELEM_SPRITE = 7,
    UIELEM_TAB_REDSTONES = 8,
    // Tutorial flash
    UIELEM_BUILTIN_TAB_ICONS = 9,
    UIELEM_CHAT_MODES = 10,
    UIELEM_CHAT_INPUT = 11,
    UIELEM_CHAT_HISTORY = 12,

};

enum StaticUIElemPositionKind
{
    UIPOS_XY = 1,
    UIPOS_RELATIVE = 2,
};

struct StaticUIElemPosition
{
    enum StaticUIElemPositionKind kind;
    int x;
    int y;

    int relative_flags;
    int left;
    int top;
    int right;
    int bottom;
};

struct StaticUIComponent
{
    enum StaticUIComponentType type;
    struct StaticUIElemPosition position;
    int sprite_id;
};

struct StaticUIBuffer
{
    struct StaticUIComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
};

struct StaticUIBuffer*
static_ui_buffer_new(uint32_t hint);

void
static_ui_buffer_free(struct StaticUIBuffer* buffer);

void
static_ui_buffer_push_xy(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int x,
    int y);

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
    int left);

#endif