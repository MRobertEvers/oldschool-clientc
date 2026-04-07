#ifndef UITREE_H
#define UITREE_H

#include <stdint.h>

#define UI_INVENTORY_MAX_ITEMS 128

struct UIInventoryItem
{
    int obj_id;
    int scene_id; /* UIScene element id for pre-loaded obj icon; -1 if none */
    int atlas_index;
};

struct UIInventory
{
    char name[64];
    struct UIInventoryItem items[UI_INVENTORY_MAX_ITEMS];
    int item_count;
};

struct UIInventoryPool
{
    struct UIInventory* inventories;
    int count;
    int capacity;
};

/**
 * "builtin" components are historically components that would've been hardcoded into the client.
 */
enum StaticUIComponentType
{
    /* Historically things that were hardcoded into the client. */
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
    int32_t parent;        /* -1 = root or root-chain node */
    int32_t first_child;   /* -1 = leaf */
    int32_t next_sibling;  /* -1 = last sibling */
    int component_id;      /* CacheDatConfigComponent id for RS nodes; -1 for builtins */

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
            int atlas_index;   /* inactive sprite atlas index */
            int scene_id_active; /* active sprite scene id; -1 if absent */
            int atlas_index_active;
            int tabno; /* which selected_tab value activates this tab */
        } redstone_tab;
        struct
        {
            int scene_id;
        } minimap;
        struct
        {
            int tabno;
            int componentno;
            int inv_index; /* UIInventoryPool index; -1 if none */
        } sidebar;

        struct
        {
            int font_id;
            int color;
            int center;
            int shadowed;
            char const* text;
        } rs_text;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
        } rs_graphic;
        struct
        {
            int scene2_element_id;
        } rs_model;
        struct
        {
            int inv_index;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
        } rs_inv;
        struct
        {
            int reserved;
        } rs_layer;

    } u;
};

struct UITree
{
    struct StaticUIComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
    int32_t root_index; /* first root in root sibling chain; -1 if empty */
};

char const*
uitree_component_type_str(enum StaticUIComponentType type);

struct UITree*
uitree_new(uint32_t hint);

void
uitree_free(struct UITree* tree);

struct UIInventoryPool*
uitree_inv_pool_new(int capacity);

void
uitree_inv_pool_free(struct UIInventoryPool* pool);

/** Returns pool index or -1 if not found. */
int
uitree_inv_pool_find_by_name(struct UIInventoryPool* pool, char const* name);

/** Appends a copy of inv; returns new index or -1. */
int
uitree_inv_pool_append(struct UIInventoryPool* pool, struct UIInventory const* inv);

#define STATIC_UI_RELATIVE_FLAG_LEFT 1
#define STATIC_UI_RELATIVE_FLAG_TOP 2
#define STATIC_UI_RELATIVE_FLAG_RIGHT 4
#define STATIC_UI_RELATIVE_FLAG_BOTTOM 8

int32_t
uitree_push_sprite_xy(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_sprite_relative(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int flags,
    int top,
    int right,
    int bottom,
    int left,
    int width,
    int height);

int32_t
uitree_push_world(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y);

int32_t
uitree_push_compass(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y);

int32_t
uitree_push_minimap(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y);

int32_t
uitree_push_redstone_tab(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int sprite_id,
    int atlas_index,
    int sprite_active_id,
    int atlas_active_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_builtin_sidebar(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int componentno,
    int inv_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_sidebar_component(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int componentno,
    int inv_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_rs_layer(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_rs_text(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int font_id,
    int color,
    int center,
    int shadowed,
    char const* text,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_rs_graphic(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scene_id,
    int atlas_index,
    int scene_id_active,
    int atlas_index_active,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_rs_model(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scene2_element_id,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_rs_inv(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int inv_index,
    int cols,
    int rows,
    int margin_x,
    int margin_y,
    int x,
    int y,
    int width,
    int height);

#endif
