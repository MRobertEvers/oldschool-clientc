#ifndef UITREE_H
#define UITREE_H

#include "osrs/clientscript_vm.h"
#include "osrs/revconfig/uitree_optionset.h"
#include "osrs/revconfig/uitree_textpool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct GGame;

#define UI_INVENTORY_MAX_ITEMS 128
/** Matches build-cache / interface.c inv slot offset arrays (first 20 slots). */
#define UI_INV_SLOT_OFFSET_MAX 20

struct UIInventoryItem
{
    int obj_id;    /* 1-based (0 = empty slot) */
    int obj_count; /* stack quantity (1 for non-stackable) */
    int scene_id;  /* UIScene element id for pre-loaded obj icon; -1 if none */
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

/** Revision-agnostic button kind (mirrors CacheDatConfigComponentButtonType). */
enum UIButtonKind
{
    UI_BUTTON_NONE = 0,
    UI_BUTTON_OK = 1,
    UI_BUTTON_TARGET = 2,
    UI_BUTTON_CLOSE = 3,
    UI_BUTTON_TOGGLE = 4,
    UI_BUTTON_SELECT = 5,
    UI_BUTTON_CONTINUE = 6,
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
    UIELEM_BUILTIN_HOVER_TOOLTIP = 10,
    UIELEM_BUILTIN_MINIMENU = 11,
    UIELEM_BUILTIN_CROSSHAIR = 12,
    UIELEM_BUILTIN_CHAT_MESSAGES = 13,
    //
    UIELEM_RS_TEXT = 14,
    UIELEM_RS_GRAPHIC = 15,
    UIELEM_RS_MODEL = 16,
    UIELEM_RS_INV = 17,
    UIELEM_RS_LAYER = 18,
    UIELEM_RS_RECT = 19,
    UIELEM_BUILTIN_CHAT_INPUT = 20,
    UIELEM_BUILTIN_CHAT_PRIVACY = 21,
    UIELEM_BUILTIN_COLLISIONMAP_OVERLAY = 22,
    /** IF_OPENCHAT: RS subtree parent; children rebuilt from gamecache (same pattern as sidebar).
     */
    UIELEM_BUILTIN_CHAT_DIALOG = 23,
    /** IF_OPENSIDE / IF_OPENMAIN_SIDE: RS subtree parent over sidebar overlay rect. */
    UIELEM_BUILTIN_SIDEBAR_OVERLAY = 24,
    /** IF_OPENOVERLAY / IF_OPENMAIN viewport modal: RS subtree over world rect (Client.ts mainOverlayId). */
    UIELEM_BUILTIN_VIEWPORT_OVERLAY = 25,
    /** TYPE_INV_TEXT: text labels per slot; uses same `u.rs_inv` grid as UIELEM_RS_INV. */
    UIELEM_RS_INV_TEXT = 26,
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
    /** Layout arg dirty=true in INI; forces this node to always emit draw commands. */
    uint8_t always_dirty;
    /** Set each frame before traversal; true if this node should emit draw commands. */
    uint8_t is_dirty;
    /** Runtime hide from IF_SETHIDE (mirrors CacheDatConfigComponent.hide). */
    uint8_t is_hidden;
    int32_t parent;       /* -1 = root or root-chain node */
    int32_t first_child;  /* -1 = leaf */
    int32_t next_sibling; /* -1 = last sibling */
    int component_id;     /* CacheDatConfigComponent id for RS nodes; -1 for builtins */

    struct StaticUIElemPosition position;

    /* ── Revision-agnostic fields (copied from CacheDatConfigComponent at load time) ── */

    enum UIButtonKind button_kind;
    int click_mask; /* targetMask for op/use/swap actions */
    uint8_t interactable;
    uint8_t usable;
    uint8_t swappable;
    uint8_t draggable;

    /* Script bytecode + comparators (owned by UITree; evaluated by ClientScriptVM). */
    struct UIScriptBytecode* scripts; /* [scripts_count]; UITree owns each .code array */
    int scripts_count;
    uint8_t* script_comparator; /* per-script comparator byte */
    int* script_operand;        /* per-script operand int */

    /* Inventory right-click menu strings — UITree-owned heap copies. */
    char* iop[5];
    char* option;
    char* target_verb;
    char* target_text;

    /* Anim / seq state. */
    int anim_id;
    int active_anim_id;
    int seq_frame;
    int seq_cycle;

    /* Misc. */
    int alpha;       /* 0-255 transparency (0=opaque in OSRS convention) */
    int overlayer;   /* sibling layer index for hover priority */
    int client_code; /* 0 if none; matched by VarClientCodeKind on var writes */

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
            int font_id;
        } hover_tooltip;
        struct
        {
            int font_id; /* UIScene font id; resolved at INI load (default b12 then p11). */
        } minimenu;
        struct
        {
            /** UIScene element id for the `cross` sprite atlas (frames 0..7); resolved at load. */
            int scene_id;
            /** Pixels subtracted from click x/y for sprite top-left; from INI `hotspot_offset`
             * (default 8). */
            int hotspot_offset;
        } crosshair;
        struct
        {
            int tabno;
            int componentno;
            int inv_index; /* UIInventoryPool index; -1 if none */
        } sidebar;

        struct
        {
            int font_id; /* UIScene font id; resolved at INI load (default first of p11,p12,b12,q8).
                          */
        } chat_messages;
        struct
        {
            int font_id;
        } chat_input;
        struct
        {
            int font_id;
        } chat_privacy;
        struct
        {
            int reserved;
        } chat_dialog;
        struct
        {
            int reserved;
        } sidebar_overlay;
        struct
        {
            int reserved;
        } viewport_overlay;
        struct
        {
            /** Resolved uiscene font id at draw time; may be -1 until lazy resolve. */
            int font_id;
            /** OSRS font index 0..3 (p11,p12,b12,q8). */
            int font_idx;
            int color;
            int active_color;
            int over_color;
            int active_over_color;
            int center;
            int shadowed;
            /** Heap copy; owned by UITree, freed in uitree_free. */
            char const* text;
            /** Heap copy of activeText; may be NULL. */
            char const* active_text;
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
            /** From GameCacheComponent zoom/xan/yan; TYPE_MODEL / IF_SETOBJECT framing (Client.ts).
             */
            int model_zoom;
            int model_xan;
            int model_yan;
            /** Last sequence id baked into scene2_element animation buffers; -1 = none / stale. */
            int rs_model_cached_sequence_id;
        } rs_model;
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
            /** Total scrollable height (cache `scroll`); viewport is position.height. */
            int scroll_height;
            /** Initial hide from cache at build time; use is_hidden for runtime IF_SETHIDE. */
            int hide;
        } rs_layer;
        struct
        {
            int color;
            int active_color;
            int over_color;
            int active_over_color;
            int alpha;
            uint8_t fill;
        } rs_rect;

    } u;

    /** When `type == UIELEM_RS_INV_TEXT` (Client.ts TYPE_INV_TEXT); ignored otherwise. */
    int inv_text_font_id;
    int inv_text_color;
    int inv_text_center;
    int inv_text_shadowed;
    /** TYPE_INV sibling under the same layer: shares inv_pool + server slot updates. -1 = none. */
    int32_t inv_text_peer_inv_component_id;
};

#define UIFRAME_LAYER_STACK_MAX 16

/** One RS_LAYER frame in the UI traversal: cumulative scroll for descendants + clip rect. */
struct UILayerFrameEntry
{
    int scroll_y_total;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    /** If set, draw scrollbar after layer content, before POP_CLIP (Client.ts drawInterface order).
     */
    int scrollbar_layer_component_id;
    int scrollbar_scroll_height;
};

struct UITree
{
    struct StaticUIComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
    int32_t root_index; /* first root in root sibling chain; -1 if empty */
    /** Incremented on every `uitree_push_*` / node add; used to invalidate UI dirty caches. */
    uint32_t generation;

    /** RS_LAYER scroll/clip stack during LibToriRS_FrameNextCommand traversal (-1 = empty). */
    struct UILayerFrameEntry ui_layer_stack[UIFRAME_LAYER_STACK_MAX];
    int ui_layer_stack_top;

    /** Expanded UI text strings for TORIRS_GFX_DRAW_FONT (lifetime through frame). */
    struct UITreeTextPool text_pool;

    /** RS scrollbar: iface component id being dragged, or -1. */
    int ui_scrollbar_drag_component_id;
    /** UIScene element ids for `scrollbar0` / `scrollbar1` caps; set when UI loads, -1 if missing.
     */
    int ui_scrollbar0_element_id;
    int ui_scrollbar1_element_id;

    /** Minimap overlay sprites (rev cache names); resolved at UI load, -1 if missing. */
    int ui_minimap_mapdots0_element_id;
    int ui_minimap_mapdots1_element_id;
    int ui_minimap_mapdots3_element_id;
    int ui_minimap_mapdots4_element_id;
    int ui_minimap_mapmarker2_element_id;
    int ui_minimap_mapmarker_element_id;

    /** Per-frame UI hover context menu (distinct from game->option_set / world pick). */
    struct UITreeOptionSet uitree_optionset;
    /** UITree node index that produced uitree_optionset, or -1. */
    int32_t hover_node_index;
    /** Last hover pick for TYPE_INV / sidebar overlay cache inv (-1 = none). */
    int hover_inv_component_id;
    int hover_inv_slot;
};

/**
 * Linear scan for a node with matching component_id.
 * Returns the node index, or -1 if not found.
 * O(N) — call once per IF_SET* packet, never from the render loop.
 */
int32_t
uitree_find_by_component_id(
    const struct UITree* tree,
    int component_id);

/**
 * Linear scan for a UIELEM_RS_INV or UIELEM_RS_INV_TEXT node matching component_id.
 * Returns the inv_index from node.u.rs_inv.inv_index, or -1 if not found.
 * Optionally writes the node array index into out_node_idx (may be NULL).
 * O(N) — call once per packet, never from the render loop.
 */
int
uitree_find_inv_index_by_component_id(
    const struct UITree* tree,
    int component_id,
    int32_t* out_node_idx);

struct GameCacheComponent;

/** Inventory minimenu lines from build-cache inv component (UPDATE_INV_* / IF_OPENSIDE path). */
void
uitree_fill_inv_slot_options_from_cache_inv(
    struct GGame* game,
    struct GameCacheComponent* inv,
    int slot,
    struct UITreeOptionSet* os);

char const*
uitree_component_type_str(enum StaticUIComponentType type);

struct UITree*
uitree_new(uint32_t hint);

void
uitree_free(struct UITree* tree);

/** Mark every component dirty for the upcoming frame traversal. */
void
uitree_mark_all_dirty(struct UITree* tree);

/** Advance seq_frame/seq_cycle for UIELEM_RS_MODEL (Client.ts animateInterface cadence). */
void
uitree_step_rs_model_animations(
    struct GGame* game,
    int cycles_elapsed);

/** Ensure RS_MODEL Scene2 element holds DashFrame/DashFramemap for the currently resolved sequence.
 * Call after refresh or when scripts may have changed sequence binding. */
void
uitree_rs_model_ensure_sequence_precached(
    struct GGame* game,
    struct StaticUIComponent* c);

struct GameCacheSequence;
struct DashModel;

/** Client IfType: seq id from modelAnim/modelAnim2 + readyanim for resolved head mesh. */
int
uitree_interface_resolved_sequence_id(
    struct GGame* game,
    struct GameCacheComponent* gcc,
    bool active,
    int effective_model_type,
    int effective_model_id);

/** Client getTempModel: primary seq.frames[] then seq.iframes[] morph.
 * When `rs_model_ui_comp` is non-NULL and its Scene2 element has precached frames, uses those;
 * otherwise allocates frames per call (legacy path). */
void
uitree_interface_apply_sequence_keyframe_to_model(
    struct GGame* game,
    struct DashModel* model,
    struct GameCacheSequence* seq,
    int frame_index,
    struct StaticUIComponent* rs_model_ui_comp_nullable);

/** Fresh chat-head DashModel for soft3d (model1/2, dual iframe, lighting). Caller frees. */
struct DashModel*
uitree_chat_head_dashmodel_for_frame(
    struct GGame* game,
    struct GameCacheComponent* gcc,
    struct StaticUIComponent* ui_comp);

/** Log every node (index, type, tree links, layout, component_id). */
void
uitree_print_nodes(struct UITree const* tree);

/** DFS-print node with matching `component_id` and descendants to stderr (debug). */
void
uitree_debug_log_subtree_for_component_id(struct UITree const* tree, int component_id);

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

/**
 * Finds or appends an empty inventory named `if_<component_id>` for RS interface inv
 * components under sidebars with no named `inv=` (magic spell rows, prayer orbs, etc.).
 */
int
uitree_inv_pool_find_or_append_by_component_id(
    struct UIInventoryPool* pool,
    int component_id);

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
    int y,
    int width,
    int height,
    uint8_t level_mask);

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
uitree_push_hover_tooltip(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_minimenu(
    struct UITree* tree,
    int32_t parent_index);

int32_t
uitree_push_crosshair(
    struct UITree* tree,
    int32_t parent_index);

int32_t
uitree_push_collisionmap_overlay(
    struct UITree* tree,
    int32_t parent_index);

int32_t
uitree_push_chat_messages(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_chat_input(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_chat_privacy(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_builtin_chat_dialog(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_builtin_sidebar_overlay(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_builtin_viewport_overlay(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height);

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

/** Detach RS children from a sidebar node (first_child = -1). Orphaned indices remain in the
 * dense array until a full uitree_free; safe for bounded IF_SETTAB updates.
 * The detached subtree is marked is_hidden so stray iteration cannot draw it. */
void
uitree_clear_sidebar_children(
    struct UITree* tree,
    int32_t sidebar_idx);

void
uitree_clear_chat_dialog_children(
    struct UITree* tree,
    int32_t chat_dialog_idx);

void
uitree_clear_sidebar_overlay_children(
    struct UITree* tree,
    int32_t sidebar_overlay_idx);

void
uitree_clear_viewport_overlay_children(
    struct UITree* tree,
    int32_t viewport_overlay_idx);

/** Returns 0 if no duplicate BUILTIN_SIDEBAR tabno in 0..13; else logs to stderr and returns -1.
 * Revisions may omit wire tabs (e.g. LC245_2 skips 7); missing indices are allowed. */
int
uitree_validate_sidebar_tab_layout(struct UITree const* tree);

/** Stamp hide on every node with matching cache component_id (RS subtree). */
void
uitree_set_component_hidden(
    struct UITree* tree,
    int component_id,
    int hidden);

int32_t
uitree_push_rs_layer(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scroll_height,
    int hide,
    int x,
    int y,
    int width,
    int height);

int32_t
uitree_push_rs_rect(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int color,
    int active_color,
    int over_color,
    int active_over_color,
    int alpha,
    int fill,
    int x,
    int y,
    int width,
    int height);

/** `text` / `active_text` copied (strdup); font resolved lazily at draw from font_idx. */
int32_t
uitree_push_rs_text(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int font_idx,
    int color,
    int active_color,
    int over_color,
    int active_over_color,
    int center,
    int shadowed,
    char const* text,
    char const* active_text,
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
    int const* inv_slot_offset_x,
    int const* inv_slot_offset_y,
    int const* inv_slot_bg_scene_id,
    int const* inv_slot_bg_atlas_index,
    int x,
    int y,
    int width,
    int height);

/** Same grid as `uitree_push_rs_inv` but `type` is UIELEM_RS_INV_TEXT; set inv_text_* after push. */
int32_t
uitree_push_rs_inv_text(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int inv_index,
    int cols,
    int rows,
    int margin_x,
    int margin_y,
    int const* inv_slot_offset_x,
    int const* inv_slot_offset_y,
    int const* inv_slot_bg_scene_id,
    int const* inv_slot_bg_atlas_index,
    int x,
    int y,
    int width,
    int height);

/** region: -1=none, 0=arrow_up, 1=arrow_down, 2=track, 3=grip. Needs game->iface for scroll pos. */
struct UITreeScrollbarHit
{
    int32_t layer_idx;
    int component_id;
    int layer_x;
    int layer_y;
    int layer_width;
    int layer_height;
    int scroll_height;
    int scroll_pos;
    int max_scroll;
    int region;
};

bool
uitree_find_scrollbar_at(
    struct GGame* game,
    int mouse_x,
    int mouse_y,
    struct UITreeScrollbarHit* out);

/** Deepest RS_LAYER (by tree depth) under (mx,my) with scroll; -1 if none. */
int32_t
uitree_innermost_scroll_layer_at(
    struct GGame* game,
    int mouse_x,
    int mouse_y);

/** Hit-test UI at (pick_mx,pick_my) and rebuild tree->uitree_optionset (world leaves stay empty).
 */
void
uitree_sync_hover_option_set(
    struct GGame* game,
    int pick_mx,
    int pick_my);

#endif
