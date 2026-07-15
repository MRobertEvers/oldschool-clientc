#ifndef UITREE_H
#define UITREE_H

#include <stdbool.h>
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

/** Client.ts overMainComId / overSideComId / overChatComId. -1 = none in each region. */
struct UITreeHoverIds
{
    int main_com_id;
    int side_com_id;
    int chat_com_id;
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
    UIELEM_BUILTIN_CHAT_BUTTON = 12,
    //
    UIELEM_RS_TEXT = 14,
    UIELEM_RS_GRAPHIC = 15,
    UIELEM_RS_MODEL = 16,
    UIELEM_INV_GRID = 17,
    UIELEM_RS_LAYER = 18,
    UIELEM_RS_RECT = 19,
    UIELEM_RS_LINE = 20,
    UIELEM_RS_INV_TEXT = 21,
    UIELEM_INV_SLOT = 22,
    /** CS2 cc_create / if_setobject dynamic item icon (objbox). */
    UIELEM_CC_OBJ = 23,
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

    /** IF3 position/size modes (-1 = use simple x/y/width/height). */
    int8_t x_mode;
    int8_t y_mode;
    int8_t width_mode;
    int8_t height_mode;
    int aspect_w;
    int aspect_h;

    /** Resolved absolute bounds (set by uitree_layout_resolve). */
    int abs_x;
    int abs_y;
    int abs_w;
    int abs_h;
    uint8_t layout_resolved;
};

/** Runtime CS2 hooks installed by IF_SETON* / CC_SETON* (not cache bake-time hooks). */
struct UITreeRuntimeScriptHook
{
    int script_id;
    int argc;
    int argv[16];
};

struct StaticUIRuntimeHooks
{
    struct UITreeRuntimeScriptHook on_click;
    struct UITreeRuntimeScriptHook on_hold;
    struct UITreeRuntimeScriptHook on_mouse_over;
    struct UITreeRuntimeScriptHook on_mouse_leave;
    struct UITreeRuntimeScriptHook on_mouse_repeat;
    struct UITreeRuntimeScriptHook on_drag;
    struct UITreeRuntimeScriptHook on_drag_complete;
    struct UITreeRuntimeScriptHook on_scroll_wheel;
    struct UITreeRuntimeScriptHook on_key;
    struct UITreeRuntimeScriptHook on_op;
    struct UITreeRuntimeScriptHook on_timer;
    struct UITreeRuntimeScriptHook on_var_transmit;
    struct UITreeRuntimeScriptHook on_inv_transmit;
    struct UITreeRuntimeScriptHook on_misc_transmit;
};

struct StaticUIBehavior
{
    /** Number of embedded CS1/CS2 scripts (Client.ts scripts[]). */
    int scripts_count;
    /** Per-script bytecode; evaluated by getIfVar / getIfActive. Heap-owned. */
    int** scripts;
    /** Per-script length; 0 = scan until opcode 0. */
    int* scripts_lengths;
    /** Per-script comparator opcode (1==, 2<, 3>, 4!=). Required for getIfActive. */
    int* script_comparator;
    /** Per-script comparison operand (Client.ts scriptOperand). */
    int* script_operand;

    /** Client.ts hide: layer skipped unless hovered_component_id == component_id. */
    uint8_t hide;

    /** 0 = CS1 (cs1vm), 1 = CS2-kind inline (inactive). */
    uint8_t script_kind;

    /** Client.ts buttonType: OK/TARGET/CLOSE/TOGGLE/SELECT/CONTINUE; 0 = none. */
    int button_type;
    /** Client.ts clientCode: hardcoded client-side handler id. */
    int client_code;

    /** Client.ts clickMask / IF_SETCLICKMASK runtime override (IF1/IF3 packed click bits). */
    int32_t click_mask;

    /** Client.ts overLayerId (dat1 overlayer / dat2 linkedComponentId).
     *  When the mouse is over this component, hover tracking reports this id
     *  instead of component_id — used to reveal hidden tooltip layers. -1 = none. */
    int over_layer_id;

    /** Client.ts colourOver: hover tint when inactive (rect/text). 0 = none. */
    int over_color;
    /** Client.ts colour2: fill/text colour when getIfActive is true. 0 = keep base. */
    int active_color;
    /** Client.ts colour2Over: hover tint when active. 0 = none. */
    int active_over_color;
};

#define UITREE_MENU_OPTION_SLOTS 5
#define UITREE_MENU_OPTION_LEN 32
#define UITREE_SUBMENU_OP_SLOTS 10
#define UITREE_SUBMENU_ENTRY_SLOTS 32

struct StaticUIMenuSubmenuOptions
{
    char ops[UITREE_SUBMENU_OP_SLOTS][UITREE_SUBMENU_ENTRY_SLOTS][UITREE_MENU_OPTION_LEN];
};

struct StaticUIMenuOptions
{
    char option[UITREE_MENU_OPTION_LEN];
    char ops[UITREE_MENU_OPTION_SLOTS][UITREE_MENU_OPTION_LEN];
    /** 0 = derive action at click time from slot index or button_type. */
    int option_action;
    int op_actions[UITREE_MENU_OPTION_SLOTS];
    struct StaticUIMenuSubmenuOptions submenus;
};

#define UITREE_CHAT_OP_TEMPLATE_LEN 64

struct StaticUIChatMinimenuConfig
{
    char op_report_abuse[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_report_abuse_action;
    char op_add_ignore[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_add_ignore_action;
    char op_add_friend[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_add_friend_action;
    char op_accept_trade[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_accept_trade_action;
    char op_accept_duel[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_accept_duel_action;
};

enum StaticUIChatButtonFilter
{
    STATIC_UI_CHAT_BUTTON_PUBLIC = 0,
    STATIC_UI_CHAT_BUTTON_PRIVATE,
    STATIC_UI_CHAT_BUTTON_TRADE,
    STATIC_UI_CHAT_BUTTON_REPORT,
};

struct StaticUIChatButtonConfig
{
    enum StaticUIChatButtonFilter filter;
    char label[64];
    int label_y;
    int mode_y;
    int font_id;
    int center;
    int shadowed;
    char mode_label[4][16];
    int mode_color[4];
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
    /** 1 when created by CS2 cc_create (removed by cc_deleteall). */
    uint8_t dynamic;
    /** CC_CREATE childIndex under parent; -1 for static nodes. */
    int dynamic_child_index;

    /** CS2 cc_setobject / if_setobject override (any widget type; mirrors TS itemId). */
    int item_id;
    int item_count;
    int item_scene_id;
    int item_atlas_index;

    /** CC_SETTRANS / IF_SETTRANS opacity (0-255; 255 = fully transparent). */
    int trans;
    /** CC_SETNOCLICKTHROUGH / IF_SETNOCLICKTHROUGH. */
    uint8_t no_click_through;
    /** CC_SETDRAGGABLE / IF_SETDRAGGABLE. */
    uint8_t draggable;
    /** CC_SETDRAGGABLEBEHAVIOR / IF_SETDRAGGABLEBEHAVIOR. */
    int drag_behavior;
    /** CC_SETDRAGDEADZONE / IF_SETDRAGDEADZONE (pixels; 0 = client default 5). */
    uint8_t drag_dead_zone;
    /** CC_SETDRAGDEADTIME / IF_SETDRAGDEADTIME (client-clock ticks; 0 = no time gate). */
    uint8_t drag_dead_time;
    /** IF_SETMODELTRANSPARENT (CS2 runtime). */
    uint8_t model_transparent;

    struct StaticUIBehavior behavior;
    /** CS2 runtime hooks (CC_/IF_SETON*); empty script_id means unset. */
    struct StaticUIRuntimeHooks runtime_hooks;
    /** IF_SETTARGETPRIORITY / target priority for combat targeting UI. */
    int target_priority;
    /** Scroll position (runtime; distinct from scroll_width/height size). */
    int scroll_x;
    int scroll_y;
    struct StaticUIElemPosition position;
    struct StaticUIMenuOptions menu_options;
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
            int inv_source_id; /* UIInvDataService source; UI_INV_SOURCE_INVALID if none */
        } sidebar;

        struct
        {
            int font_id;
            /** Client.ts colour: inactive text colour (0xRRGGBB). */
            int color;
            int center;
            int y_align;
            int line_height;
            int shadowed;
            /** Client.ts text: inactive label. Heap copy; freed in uitree_free. */
            char const* text;
            /** Client.ts text2: label when getIfActive; empty/null = keep text. */
            char const* text_active;
        } rs_text;
        struct
        {
            /** Client.ts graphic: inactive sprite. */
            int scene_id;
            int atlas_index;
            /** Client.ts graphic2: sprite when getIfActive. -1 = no active variant. */
            int scene_id_active;
            int atlas_index_active;
            /** Draw nothing but keep bounds for hit-testing (IF3 empty graphic). */
            uint8_t graphic_hitbox_only;
            /** dat2 tiled: repeat sprite across IF3 layout rect. */
            uint8_t tiled;
            int outline;
            int graphic_shadow;
            uint8_t flip_h;
            uint8_t flip_v;
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
            /** Opaque handle into UIInvDataService; UI_INV_SOURCE_INVALID if none. */
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int inv_slot_offset_x[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_offset_y[UI_INV_SLOT_OFFSET_MAX];
            /** UIScene element per slot for invSlotGraphic; -1 if none. */
            int inv_slot_bg_scene_id[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_bg_atlas_index[UI_INV_SLOT_OFFSET_MAX];
        } inv_grid;
        struct
        {
            int inv_source_id;
            int slot;
            uint8_t center_icon;
        } inv_slot;
        struct
        {
            int obj_id;
            int obj_count;
            int scene_id;
            int atlas_index;
            uint8_t center_icon;
        } cc_obj;
        struct
        {
            /** Client.ts scrollHeight / dat1 scroll. 0 = no vertical scroll. */
            int scroll_height;
            /** dat2 scrollWidth. 0 = no horizontal scroll. */
            int scroll_width;
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
            struct StaticUIChatMinimenuConfig minimenu;
        } chat;
        struct StaticUIChatButtonConfig chat_button;
        struct
        {
            int color;
            int line_width;
            int horizontal;
        } rs_line;
        struct
        {
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int font_id;
            int color;
            int center;
            int shadowed;
        } rs_inv_text;

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
    /** Next uid low-16 for cc_create (0x8000..0xffff); 0 = uninitialized. */
    uint16_t next_dynamic_uid;
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
    /** When set, node is CS2 cc_create child (cc_deleteall removes). */
    uint8_t dynamic;
    /** CC_CREATE childIndex; only when dynamic is set. */
    int dynamic_child_index;
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
            int inv_source_id;
        } sidebar;
        struct
        {
            int font_id;
            int color;
            int center;
            int y_align;
            int line_height;
            int shadowed;
            char const* text;
            char const* text_active;
        } rs_text;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
            uint8_t graphic_hitbox_only;
            uint8_t tiled;
            int outline;
            int graphic_shadow;
            uint8_t flip_h;
            uint8_t flip_v;
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
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int const* inv_slot_offset_x;
            int const* inv_slot_offset_y;
            int const* inv_slot_bg_scene_id;
            int const* inv_slot_bg_atlas_index;
        } inv_grid;
        struct
        {
            int inv_source_id;
            int slot;
            uint8_t center_icon;
        } inv_slot;
        struct
        {
            int obj_id;
            int obj_count;
            int scene_id;
            int atlas_index;
            uint8_t center_icon;
        } cc_obj;
        struct
        {
            /** Client.ts scrollHeight / dat1 scroll. 0 = no vertical scroll. */
            int scroll_height;
            /** dat2 scrollWidth. 0 = no horizontal scroll. */
            int scroll_width;
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
            struct StaticUIChatMinimenuConfig minimenu;
        } chat;
        struct StaticUIChatButtonConfig chat_button;
        struct
        {
            int color;
            int line_width;
            int horizontal;
        } rs_line;
        struct
        {
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int font_id;
            int color;
            int center;
            int shadowed;
        } rs_inv_text;
    } u;
    struct StaticUIMenuOptions menu_options;
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

/** Mark one node dirty for the upcoming frame traversal. */
void
uitree_mark_node_dirty(
    struct UITree* tree,
    int32_t idx);

/** Linear search for a baked RS node by cache component id; -1 if not found. */
int32_t
uitree_find_by_component_id(
    struct UITree const* tree,
    int component_id);

#define UITREE_WALK_STACK_MAX 64

/** Preorder DFS cursor. Descends into first_child only when current_visible is true. */
void
uitree_walk_advance(
    struct UITree const* tree,
    int32_t* io_current,
    int32_t* stack,
    int* io_stack_top,
    int stack_max,
    bool current_visible);

/** Apply CS2 IF/CC hide to a node (by packed component id). Returns true if updated. */
bool
uitree_apply_hide(
    struct UITree* tree,
    int component_id,
    int hide);

/** Apply CS2 IF_SETCLICKMASK to a node (by packed component id). Returns true if updated. */
bool
uitree_apply_click_mask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask);

/** Apply CS2 IF/CC text to RS_TEXT node. Returns true if updated. */
bool
uitree_apply_text(
    struct UITree* tree,
    int component_id,
    char const* text);

/** Apply CS2 IF/CC graphic id to RS_GRAPHIC node. Returns true if updated. */
bool
uitree_apply_graphic(
    struct UITree* tree,
    int component_id,
    int graphic_id);

/** Apply CS2 IF/CC colour to RS_TEXT or RS_RECT node. Returns true if updated. */
bool
uitree_apply_colour(
    struct UITree* tree,
    int component_id,
    int colour);

/** Apply CS2 IF_SETPOSITION / IF_SETSIZE to a node's layout fields. */
bool
uitree_apply_position(
    struct UITree* tree,
    int component_id,
    int x,
    int y);

bool
uitree_apply_size(
    struct UITree* tree,
    int component_id,
    int width,
    int height);

bool
uitree_apply_position_modes(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int x_mode,
    int y_mode);

bool
uitree_apply_size_modes(
    struct UITree* tree,
    int component_id,
    int width,
    int height,
    int width_mode,
    int height_mode);

bool
uitree_apply_graphic_tiled(
    struct UITree* tree,
    int component_id,
    int tiled);

bool
uitree_apply_graphic_outline(
    struct UITree* tree,
    int component_id,
    int outline);

bool
uitree_apply_graphic_shadow(
    struct UITree* tree,
    int component_id,
    int shadow_colour);

int
uitree_get_layout_width(
    struct UITree const* tree,
    int component_id);

int
uitree_get_layout_height(
    struct UITree const* tree,
    int component_id);

/** Apply CS2 IF_SETSCROLLSIZE to RS_LAYER scroll fields. */
bool
uitree_apply_scroll_size(
    struct UITree* tree,
    int component_id,
    int scroll_width,
    int scroll_height);

/** Apply CS2 CC_/IF_SETOBJECT to a node (may set UIELEM_CC_OBJ payload). */
bool
uitree_apply_object(
    struct UITree* tree,
    int component_id,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index);

bool
uitree_apply_model(
    struct UITree* tree,
    int component_id,
    int model_id);

bool
uitree_apply_model_transparent(
    struct UITree* tree,
    int component_id,
    int transparent);

bool
uitree_apply_op_base(
    struct UITree* tree,
    int component_id,
    char const* text);

bool
uitree_get_op(
    struct UITree const* tree,
    int component_id,
    int active_component,
    int index,
    char* out_buf,
    int out_len);

bool
uitree_get_op_base(
    struct UITree const* tree,
    int component_id,
    int active_component,
    char* out_buf,
    int out_len);

bool
uitree_clear_ops(
    struct UITree* tree,
    int component_id,
    int active_component);

bool
uitree_apply_op_submenu(
    struct UITree* tree,
    int component_id,
    int active_component,
    int op_index,
    int sub_index,
    char const* text);

bool
uitree_clear_op_submenu(
    struct UITree* tree,
    int component_id,
    int active_component,
    int op_index);

bool
uitree_get_text(
    struct UITree const* tree,
    int component_id,
    int active_component,
    char* out_buf,
    int out_len);

/** Find child of parent_index with comsubid (low 16 bits of component_id). */
int32_t
uitree_find_child_by_subid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id);

/** CS2 cc_create: push dynamic child under parent; returns node index or -1. */
int32_t
uitree_cc_create(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int widget_type,
    int sub_id);

/** CS2 cc_deleteall: detach dynamic children of parent. */
void
uitree_cc_delete_all(
    struct UITree* tree,
    int32_t parent_index);

/** Collect dynamic child_index values under parent (sorted ascending). Returns count. */
int
uitree_collect_dynamic_child_indices(
    struct UITree const* tree,
    int parent_component_id,
    int start_index,
    int* out_indices,
    int out_cap);

bool
uitree_apply_scroll_pos(
    struct UITree* tree,
    int component_id,
    int scroll_x,
    int scroll_y);

bool
uitree_apply_text_font(
    struct UITree* tree,
    int component_id,
    int font_id);

bool
uitree_apply_text_align(
    struct UITree* tree,
    int component_id,
    int h_align,
    int v_align,
    int line_height);

bool
uitree_apply_text_shadow(
    struct UITree* tree,
    int component_id,
    int shadowed);

bool
uitree_apply_model_angle(
    struct UITree* tree,
    int component_id,
    int xan,
    int yan,
    int zoom);

bool
uitree_apply_target_priority(
    struct UITree* tree,
    int component_id,
    int priority);

bool
uitree_apply_runtime_hook(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook* slot,
    int script_id,
    int const* argv,
    int argc);

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

/** Create a node from spec. rs_text.text and rs_text.text_active are strdup'd. Returns new index or -1. */
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
