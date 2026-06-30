#ifndef UITREE_H
#define UITREE_H

#include <stdint.h>

#define UI_INVENTORY_MAX_ITEMS 128
/** Matches build-cache / interface.c inv slot offset arrays (first 20 slots). */
#define UI_INV_SLOT_OFFSET_MAX 20

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
    UIELEM_BUILTIN_CROSS = 10,
    UIELEM_BUILTIN_MINIMENU = 11,
    //
    UIELEM_RS_TEXT = 14,
    UIELEM_RS_GRAPHIC = 15,
    UIELEM_RS_MODEL = 16,
    UIELEM_RS_INV = 17,
    UIELEM_RS_LAYER = 18,
    UIELEM_RS_RECT = 19,
    UIELEM_RS_LINE = 20,
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

    /** Resolved absolute bounds (set by uitree_layout_resolve). */
    int abs_x;
    int abs_y;
    int abs_w;
    int abs_h;
    uint8_t layout_resolved;
};

struct StaticUIBehavior
{
    int scripts_count;
    int** scripts;           /* CS1 bytecode arrays; heap-owned, freed in uitree_free */
    int* scripts_lengths;    /* per-script length; 0 = scan until opcode 0 */
    int* script_comparator;  /* per-script comparator opcode */
    int* script_operand;     /* per-script operand */
    uint8_t hide;            /* static hide flag from cache LAYER/component */
    uint8_t script_kind;     /* 0 = CS1, 1 = CS2 */
    int button_type;         /* 0=none; OK/TOGGLE/SELECT/CLOSE/CONTINUE */
    int client_code;
    int over_color;          /* hover color for rect/text; 0 = none */
    int active_color;
    int active_over_color;
};

struct StaticUIComponent
{
    enum StaticUIComponentType type;
    /** Layout arg dirty=true in INI; forces this node to always emit draw commands. */
    uint8_t always_dirty;
    /** Set each frame before traversal; true if this node should emit draw commands. */
    uint8_t is_dirty;
    int32_t parent;       /* -1 = root or root-chain node */
    int32_t first_child;  /* -1 = leaf */
    int32_t next_sibling; /* -1 = last sibling */
    int component_id;     /* CacheDatConfigComponent id for RS nodes; -1 for builtins */

    struct StaticUIBehavior behavior;
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
            int tabno; /* which selected_tab value activates this tab */
        } redstone_tab;
        struct
        {
            int scene_id;
        } minimap;
        struct
        {
            uint8_t level_mask;
        } world;
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
            /** Heap copy; owned by UITree, freed in uitree_free. */
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
            int gamecache_model_id;
            int zoom;
            int xan;
            int yan;
        } rs_model;
        struct
        {
            int color;
            int filled;
        } rs_rect;
        struct
        {
            int inv_index;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int inv_slot_offset_x[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_offset_y[UI_INV_SLOT_OFFSET_MAX];
            /** UIScene element per slot for invSlotGraphic; -1 if none. */
            int inv_slot_bg_scene_id[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_bg_atlas_index[UI_INV_SLOT_OFFSET_MAX];
        } rs_inv;
        struct
        {
            int reserved;
        } rs_layer;
        struct
        {
            int scene_id;
            int atlas_index;
            int tabno;
        } tab_icon;
        struct
        {
            int font_id;
        } minimenu;
        struct
        {
            int color;
            int line_width;
            int horizontal;
        } rs_line;

    } u;
};

struct UITree
{
    struct StaticUIComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
    int32_t root_index; /* first root in root sibling chain; -1 if empty */
    /** Incremented on every `uitree_push` / node add; used to invalidate UI dirty caches. */
    uint32_t generation;
};

/** Descriptor for creating a single UITree node via uitree_push(). */
struct UINodeSpec
{
    enum StaticUIComponentType type;
    int component_id;

    int x;
    int y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;

    uint8_t always_dirty;
    /** When set, position is copied verbatim (XY or RELATIVE). */
    uint8_t has_position;
    struct StaticUIElemPosition position;
    /** When non-NULL, deep-copied into the new node's behavior. */
    struct StaticUIBehavior const* behavior;

    union
    {
        struct
        {
            int scene_id;
            int atlas_index;
        } sprite;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
            int tabno;
        } redstone_tab;
        struct
        {
            int scene_id;
        } minimap;
        struct
        {
            uint8_t level_mask;
        } world;
        struct
        {
            int tabno;
            int componentno;
            int inv_index;
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
            int gamecache_model_id;
            int zoom;
            int xan;
            int yan;
        } rs_model;
        struct
        {
            int color;
            int filled;
        } rs_rect;
        struct
        {
            int inv_index;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int const* inv_slot_offset_x;
            int const* inv_slot_offset_y;
            int const* inv_slot_bg_scene_id;
            int const* inv_slot_bg_atlas_index;
        } rs_inv;
        struct
        {
            int reserved;
        } rs_layer;
        struct
        {
            int scene_id;
            int atlas_index;
            int tabno;
        } tab_icon;
        struct
        {
            int font_id;
        } minimenu;
        struct
        {
            int color;
            int line_width;
            int horizontal;
        } rs_line;
    } u;
};

char const*
uitree_component_type_str(enum StaticUIComponentType type);

struct UITree*
uitree_new(uint32_t hint);

void
uitree_free(struct UITree* tree);

/** Mark every component dirty for the upcoming frame traversal. */
void
uitree_mark_all_dirty(struct UITree* tree);

/** Deep-copies script arrays from src into tree->components[idx].behavior. */
void
uitree_set_behavior(
    struct UITree* tree,
    int32_t idx,
    struct StaticUIBehavior const* src);

/** Log every node (index, type, tree links, layout, component_id). */
void
uitree_print_nodes(struct UITree const* tree);

struct UIInventoryPool*
uitree_inv_pool_new(int capacity);

void
uitree_inv_pool_free(struct UIInventoryPool* pool);

/** Returns pool index or -1 if not found. */
int
uitree_inv_pool_find_by_name(
    struct UIInventoryPool* pool,
    char const* name);

/** Appends a copy of inv; returns new index or -1. */
int
uitree_inv_pool_append(
    struct UIInventoryPool* pool,
    struct UIInventory const* inv);

#define STATIC_UI_RELATIVE_FLAG_LEFT 1
#define STATIC_UI_RELATIVE_FLAG_TOP 2
#define STATIC_UI_RELATIVE_FLAG_RIGHT 4
#define STATIC_UI_RELATIVE_FLAG_BOTTOM 8

/** Create a node from spec. rs_text.text is strdup'd. Returns new index or -1. */
int32_t
uitree_push(
    struct UITree* tree,
    int32_t parent_index,
    struct UINodeSpec const* spec);

/** Detach RS children from a sidebar node (first_child = -1). Orphaned indices remain in the
 * dense array until a full uitree_free; safe for bounded IF_SETTAB updates. */
void
uitree_clear_sidebar_children(
    struct UITree* tree,
    int32_t sidebar_idx);

#endif
