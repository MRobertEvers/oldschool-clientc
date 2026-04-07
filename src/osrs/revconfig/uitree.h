#ifndef UITREE_H
#define UITREE_H

#include <stdint.h>

/**
 * "builtin" components are historically components that would've been hardcoded into the client.
 */
enum StaticUIComponentType
{
    // Historically things that were hardcoded into the client.
    UIELEM_BUILTIN_COMPASS = 1,
    UIELEM_BUILTIN_MINIMAP = 2,
    UIELEM_BUILTIN_SIDEBAR = 3,
    UIELEM_BUILTIN_CHAT = 4,
    UIELEM_BUILTIN_WORLD = 6,
    UIELEM_BUILTIN_SPRITE = 7,
    UIELEM_BUILTIN_REDSTONE_TAB = 8,
    UIELEM_BUILTIN_TAB_ICONS = 9,
    //
    UIELEM_RS_TEXT = 14,
    UIELEM_RS_GRAPHIC = 15,
    UIELEM_RS_MODEL = 16,
    UIELEM_RS_INV = 17,
    UIELEM_RS_LAYER = 18,
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
        } sprite;
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
        } minimap;
        struct
        {
            int tabno;
            int componentno;
        } sidebar;

        struct
        {
            int font_id;
        } rs_text;
        struct
        {
            int scene_id;
            int atlas_index;
        } rs_graphic;
        struct
        {
            int scene2_element_id;
        } rs_model;
        struct
        {
            int x;
            int y;
        } rs_inv;
        struct
        {
            int x;
            int y;
        } rs_layer;

    } u;
};

struct UITree
{
    struct StaticUIComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
};

char const*
uitree_component_type_str(enum StaticUIComponentType type);

struct UITree*
uitree_new(uint32_t hint);

void
uitree_free(struct UITree* tree);

void
uitree_push_sprite_xy(
    struct UITree* tree,
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
uitree_push_sprite_relative(
    struct UITree* tree,
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
uitree_push_world(
    struct UITree* tree,
    int x,
    int y);

void
uitree_push_compass(
    struct UITree* tree,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y);

void
uitree_push_minimap(
    struct UITree* tree,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y);

/* tabno: which selected_tab value activates this component. scene_id/-1 for absent sprite. */
void
uitree_push_redstone_tab(
    struct UITree* tree,
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
uitree_push_builtin_sidebar(
    struct UITree* tree,
    int tabno,
    int componentno,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_sidebar_component(
    struct UITree* tree,
    int tabno,
    int componentno,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_layer(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_text(
    struct UITree* tree,
    int component_id,
    int font_id,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_graphic(
    struct UITree* tree,
    int component_id,
    int scene_id,
    int atlas_index,
    int scene_id_active,
    int atlas_index_active,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_model(
    struct UITree* tree,
    int component_id,
    int scene2_element_id,
    int x,
    int y,
    int width,
    int height);

void
uitree_push_rs_inv(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int width,
    int height);

#endif