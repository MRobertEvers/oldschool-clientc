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
    UIELEM_REDSTONE_TAB = 8,
    // Tutorial flash
    UIELEM_BUILTIN_TAB_ICONS = 9,
    UIELEM_CHAT_MODES = 10,
    UIELEM_CHAT_INPUT = 11,
    UIELEM_CHAT_HISTORY = 12,
    UIELEM_SIDEBAR_COMPONENT = 13,

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
    int anchor_x;
    int anchor_y;
    int left;
    int top;
    int right;
    int bottom;
    int width;
    int height;
};

struct StaticUIComponent
{
    enum StaticUIComponentType type;
    struct StaticUIElemPosition position;
    union
    {
        struct
        {
            int scene_id;
            int atlas_index;
        } sprite; /* UIELEM_SPRITE, UIELEM_COMPASS, UIELEM_MINIMAP, UIELEM_WORLD */
        struct
        {
            int scene_id;        /* inactive sprite scene id; -1 if absent */
            int atlas_index;     /* inactive sprite atlas index */
            int scene_id_active; /* active sprite scene id; -1 if absent */
            int atlas_index_active;
            int tabno;  /* which selected_tab value activates this tab */
        } redstone_tab; /* UIELEM_REDSTONE_TAB */
        struct
        {
            int scene_id;
        } minimap; /* UIELEM_BUILTIN_SIDEBAR */
        struct
        {
            int tabno;
            int componentno;
        } sidebar_component; /* UIELEM_SIDEBAR_COMPONENT */
    } u;
};

struct StaticUIBuffer
{
    struct StaticUIComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
};

char const*
static_ui_component_type_str(enum StaticUIComponentType type);

struct StaticUIBuffer*
static_ui_buffer_new(uint32_t hint);

void
static_ui_buffer_free(struct StaticUIBuffer* buffer);

void
static_ui_buffer_push_sprite_xy(
    struct StaticUIBuffer* buffer,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height);

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
    int height);

void
static_ui_buffer_push_world(
    struct StaticUIBuffer* buffer,
    int x,
    int y);

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
    int anchor_y);

void
static_ui_buffer_push_minimap(
    struct StaticUIBuffer* buffer,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y);

/* tabno: which selected_tab value activates this component. scene_id/-1 for absent sprite. */
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
    int height);

void
static_ui_buffer_push_builtin_sidebar(
    struct StaticUIBuffer* buffer,
    int x,
    int y,
    int width,
    int height);

void
static_ui_buffer_push_sidebar_component(
    struct StaticUIBuffer* buffer,
    int tabno,
    int componentno,
    int x,
    int y,
    int width,
    int height);

#endif