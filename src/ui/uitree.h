#ifndef SRC_UITREE_H
#define SRC_UITREE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_INV_SLOT_OFFSET_MAX 20
#define UITREE_MENU_OPTION_SLOTS 5
/* 64: option labels carry <col=...>name</col> tags well past 32 chars. */
#define UITREE_MENU_OPTION_LEN 64
#define UITREE_SUBMENU_OP_SLOTS 10
#define UITREE_SUBMENU_ENTRY_SLOTS 32
#define UITREE_CHAT_OP_TEMPLATE_LEN 64
#define UITREE_WALK_STACK_MAX 64
#define UITREE_INV_SOURCE_INVALID (-1)

#define UITREE_RELATIVE_FLAG_LEFT 1
#define UITREE_RELATIVE_FLAG_TOP 2
#define UITREE_RELATIVE_FLAG_RIGHT 4
#define UITREE_RELATIVE_FLAG_BOTTOM 8

/** Client.ts overMainComId / overSideComId / overChatComId. -1 = none. */
struct UITreeHoverIds
{
    int main_com_id;
    int side_com_id;
    int chat_com_id;
};

/**
 * "builtin" components are historically components that would've been
 * hardcoded into the client. RS_* map to cache IF1/IF3 widget types
 * (see ToriRS_ComponentType). CC_OBJ is a CS2-created dynamic child.
 */
enum UITreeComponentType
{
    /* Historically things that were hardcoded into the client. */
    UIELEM_BUILTIN_COMPASS = 1,
    UIELEM_BUILTIN_MINIMAP = 2,
    UIELEM_BUILTIN_SIDEBAR = 3,
    UIELEM_BUILTIN_CHAT = 4,
    UIELEM_BUILTIN_HOVERTEXT = 5,
    UIELEM_BUILTIN_WORLD = 6,
    UIELEM_BUILTIN_SPRITE = 7,
    UIELEM_BUILTIN_REDSTONE_TAB = 8,
    UIELEM_BUILTIN_TAB_ICONS = 9,
    UIELEM_BUILTIN_CROSS = 10,
    UIELEM_BUILTIN_MINIMENU = 11,
    UIELEM_BUILTIN_CHAT_BUTTON = 12,
    UIELEM_BUILTIN_PLAYERMODEL = 13,
    /** Screen-space pass over the world viewport: health bars + hitsplats
     *  (reference drawEntities). Positions come from the host. */
    UIELEM_BUILTIN_ENTITY_OVERLAY = 24,
    UIELEM_RS_TEXT = 14,     /* TYPE_TEXT */
    UIELEM_RS_GRAPHIC = 15,  /* TYPE_GRAPHIC */
    UIELEM_RS_MODEL = 16,    /* TYPE_MODEL */
    UIELEM_RS_INV = 17,      /* TYPE_INV — multi-slot inventory grid */
    UIELEM_RS_LAYER = 18,    /* TYPE_LAYER */
    UIELEM_RS_RECT = 19,     /* TYPE_RECT */
    UIELEM_RS_LINE = 20,     /* TYPE_LINE */
    UIELEM_RS_INV_TEXT = 21, /* TYPE_INV_TEXT */
    // Dynamic object created by CS2.
    UIELEM_CC_OBJ = 23, /* CS2 cc_create / if_setobject objbox */
};

/**
 * Content-slot clientCodes: cache components that mark where a builtin surface
 * belongs, because the pack format has no widget type for it. The gameframes
 * (16, 80, 161, 164, 548, 601) place them as empty layers/graphics.
 *
 *   1336 CONTENT_CHAT              layer     161, 164, 548, 601
 *   1337 CONTENT_WORLD (viewport)  layer     16, 80, 161, 164, 548, 601
 *   1338 CONTENT_MINIMAP           graphic   161, 164, 548, 601
 *   1339 CONTENT_COMPASS           graphic   161, 164, 548, 601, 898
 *   1354 CONTENT_XP_DROPS          layer     80, 161, 164, 548, 601
 *   1400 CONTENT_WORLDMAP          layer     595
 *   1401 CONTENT_WORLDMAP_OVERVIEW layer     595
 *
 * The three that have a builtin behind them are mapped in uitree_build.c; the
 * rest stay plain cache components until their builtin has somewhere to draw.
 */
enum
{
    UITREE_CLIENT_CODE_CONTENT_WORLD = 1337,
    UITREE_CLIENT_CODE_CONTENT_MINIMAP = 1338,
    UITREE_CLIENT_CODE_CONTENT_COMPASS = 1339,
};

/**
 * Interface-slot tag (INI slot=): marks a chrome node as the mount region for
 * a runtime-openable interface (reference mainModalId / sideModalId /
 * chatComId surfaces). The slot manager finds mount nodes by this tag, never
 * by coordinates, so the geometry stays entirely in RevConfig.
 */
enum UITreeSlotTag
{
    UITREE_SLOT_NONE = 0,
    UITREE_SLOT_MAIN_MODAL,
    UITREE_SLOT_MAIN_OVERLAY,
    UITREE_SLOT_SIDE_MODAL,
    UITREE_SLOT_CHAT,
    UITREE_SLOT_TUT,
};

enum UITreeElemPositionKind
{
    UIPOS_XY = 1,
    UIPOS_RELATIVE = 2,
};

struct UITreeElemPosition
{
    enum UITreeElemPositionKind kind;
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

    int8_t x_mode;
    int8_t y_mode;
    int8_t width_mode;
    int8_t height_mode;
    int aspect_w;
    int aspect_h;

    int abs_x;
    int abs_y;
    int abs_w;
    int abs_h;
    uint8_t layout_resolved;
};

#define UITREE_HOOK_ARG_MAX 32

/** %1..%5 are the placeholders the reference client substitutes in text. */
#define UITREE_CS1_VALUE_MAX 5
/** CS1 "infinity" (inv-contains sentinel); rendered as "*" in text. */
#define UITREE_CS1_VALUE_INFINITY 999999999
#define UITREE_HOOK_STR_ARG_MAX 4
#define UITREE_HOOK_STR_ARG_LEN 80

struct UITreeRuntimeScriptHook
{
    int script_id;
    int argc;
    int argv[UITREE_HOOK_ARG_MAX];
    /* Bit i set = arg position i is a string arg; strings fill strv[] in
     * position order (k-th set bit -> strv[k]). argv[i] unused there. */
    uint32_t str_mask;
    int str_argc;
    char strv[UITREE_HOOK_STR_ARG_MAX][UITREE_HOOK_STR_ARG_LEN];
};

struct UITreeRuntimeHooks
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
    struct UITreeRuntimeScriptHook on_resize;
    struct UITreeRuntimeScriptHook on_sub_change;
};

/** click_mask / events bits (OSRS WidgetFlags). */
#define UITREE_FLAG_DRAG_DEPTH_SHIFT 17
#define UITREE_FLAG_DRAG_DEPTH_MASK 0x7
#define UITREE_FLAG_DRAG_ON (1 << 20)

#define UITREE_INTERFACE_PARENT_MAX 32
/** parent_index for UITree_Push: allocate node without linking into the tree. */
#define UITREE_PARENT_UNLINKED (-2)

struct UITreeInterfaceParent
{
    int container_uid;
    int group_id;
    int type; /* 0 modal, 1 overlay, 3 tab/sidemodal */
};

struct UITreeBehavior
{
    /** CS1 (IF1) value scripts: scripts[i] is compared against script_operand[i]
     *  using script_comparator[i] to decide the component's active state. */
    int scripts_count;
    int** scripts;
    int* scripts_lengths;
    /** Sized independently of scripts_count in the cache format. */
    int comparator_count;
    int* script_comparator;
    int* script_operand;
    uint8_t hide;
    uint8_t script_kind;
    int button_type;
    int client_code;
    int32_t click_mask;
    int over_layer_id;
    int over_color;
    int active_color;
    int active_over_color;
};

struct UITreeMenuSubmenuOptions
{
    char ops[UITREE_SUBMENU_OP_SLOTS][UITREE_SUBMENU_ENTRY_SLOTS][UITREE_MENU_OPTION_LEN];
};

struct UITreeMenuOptions
{
    char option[UITREE_MENU_OPTION_LEN];
    char ops[UITREE_MENU_OPTION_SLOTS][UITREE_MENU_OPTION_LEN];
    /** BUTTON_TARGET spell strings (reference IfType.targetVerb/targetBase):
     * "Cast" + "Wind Strike" → the "Cast Wind Strike ..." target-mode verb.
     * Empty for non-spell components. */
    char target_verb[UITREE_MENU_OPTION_LEN];
    char target_base[UITREE_MENU_OPTION_LEN];
    int option_action;
    int op_actions[UITREE_MENU_OPTION_SLOTS];
    struct UITreeMenuSubmenuOptions submenus;
};

/* Op slots 1..10; index 9 is the "typed key" slot the OPT opcode variants use. */
#define UITREE_OPKEY_SLOTS 10
#define UITREE_OPKEY_PAIR_MAX 5

/**
 * Keyboard shortcut bound to one of a component's ops, set by CC/IF_SETOPKEY.
 *
 * A binding matches when a key event carries either the bound character
 * (key_chars[i]) or the bound OSRS key code (key_codes[i]); the reference stores
 * up to five alternatives per slot so one op can answer to several keys.
 */
struct UITreeOpKeyBinding
{
    uint8_t bound;
    uint8_t pair_count;
    uint8_t ignore_held;
    uint8_t rate_enabled;
    int rate;
    int key_chars[UITREE_OPKEY_PAIR_MAX];
    int key_codes[UITREE_OPKEY_PAIR_MAX];
};

struct UITreeOpKeys
{
    struct UITreeOpKeyBinding slots[UITREE_OPKEY_SLOTS];
    /** Mirrors the reference hasKeyBindings: lets the match pass skip nodes. */
    uint8_t has_bindings;
};

struct UITreeChatMinimenuConfig
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

enum UITreeChatButtonFilter
{
    UITREE_CHAT_BUTTON_PUBLIC = 0,
    UITREE_CHAT_BUTTON_PRIVATE,
    UITREE_CHAT_BUTTON_TRADE,
    UITREE_CHAT_BUTTON_REPORT,
};

struct UITreeChatButtonConfig
{
    enum UITreeChatButtonFilter filter;
    char label[64];
    int label_y;
    int mode_y;
    int font_id;
    int center;
    int shadowed;
    char mode_label[4][16];
    int mode_color[4];
};

struct UITreeComponent
{
    enum UITreeComponentType type;
    uint8_t always_dirty;
    uint8_t is_dirty;
    /** Slot is on the tree free-list (CC_DELETEALL reclaim); skipped by array
     *  walks and reused by the next push. */
    uint8_t freed;
    int32_t free_next;
    int32_t parent;
    int32_t first_child;
    int32_t next_sibling;
    int component_id;
    uint8_t dynamic;
    int dynamic_child_index;

    int item_id;
    int item_count;
    int item_scene_id;
    int item_atlas_index;

    int trans;
    uint8_t if3;
    uint8_t no_click_through;
    uint8_t draggable;
    int drag_behavior;
    uint8_t drag_dead_zone;
    uint8_t drag_dead_time;
    uint8_t model_transparent;
    /** CC/IF_SETDRAGGABLE render-area parent uid (-1 = none). */
    int drag_render_area_uid;
    int drag_render_area_child_index;
    /** Visual-only overrides while dragging (do not mutate position.abs_*). */
    uint8_t drag_active;
    int drag_visual_x;
    int drag_visual_y;
    int drag_visual_trans; /* -1 = use component trans; else forced (e.g. 128) */

    struct UITreeBehavior behavior;
    /** Last CS1 evaluation, written by the CS1 eval task and read by the emit
     *  host: emit stays synchronous while the VM's asset yields are serviced by
     *  the task layer. */
    uint8_t cs1_active;
    int cs1_values[UITREE_CS1_VALUE_MAX];
    struct UITreeRuntimeHooks runtime_hooks;
    int target_priority;
    /** enum UITreeSlotTag — nonzero marks this node as a mount region. */
    uint8_t slot_tag;
    int scroll_x;
    int scroll_y;
    struct UITreeElemPosition position;
    struct UITreeMenuOptions menu_options;
    struct UITreeOpKeys op_keys;
    union
    {
        struct
        {
            int scene_id;
            int atlas_index;
            /* COMPASS only: the pack's placeholder graphic, kept as the
             * circular alpha mask (inverted; content shows where it is
             * transparent). 0 = no mask (dat1 RevConfig path). */
            int mask_scene_id;
            int mask_atlas_index;
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
            /* The pack's placeholder graphic, kept as the inverted alpha mask
             * (map shows where it is transparent). 0 = draw unmasked. */
            int mask_scene_id;
            int mask_atlas_index;
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
            /** INI selected= — this tab is the boot-time selection (reference
             *  sideTab default 3, kept in RevConfig rather than C). */
            uint8_t selected;
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
            int zan;
            int x_offset;
            int y_offset;
            uint8_t orthog;
            uint8_t fixed_zoom;
            /* Model animation sequence set via IF/CC_SETMODELANIM, or -1 = none.
             * anim_frame is advanced by the client tick driver; anim_frame_cycle
             * accumulates elapsed 50hz cycles toward the current frame's length. */
            int anim_seq_id;
            int anim_frame;
            int anim_frame_cycle;
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
            int inv_slot_offset_x[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_offset_y[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_bg_scene_id[UI_INV_SLOT_OFFSET_MAX];
            int inv_slot_bg_atlas_index[UI_INV_SLOT_OFFSET_MAX];
        } rs_inv;
        struct
        {
            int obj_id;
            int obj_count;
            int scene_id;
            int atlas_index;
        } cc_obj;
        struct
        {
            int scroll_height;
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
            int font_id;
        } hovertext;
        struct
        {
            struct UITreeChatMinimenuConfig minimenu;
            int font_id; /* INI font= (message + input line font, e.g. p12) */
        } chat;
        struct UITreeChatButtonConfig chat_button;
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
    struct UITreeComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
    int32_t root_index;
    /** Tail of root sibling list — O(1) append while baking large packs. */
    int32_t last_root_index;
    uint32_t generation;
    /** Bumped only when a component_id is assigned or cleared (reclaim) — the id
     *  index depends on ids alone, so topology churn must not invalidate it. */
    uint32_t id_generation;
    /** Head of the reclaimed-slot free-list (chained via component free_next). */
    int32_t free_head;
    /** Lazy id->index acceleration for UITree_FindByComponentId. Open-addressed,
     *  power-of-two, load factor <= 0.5. Rebuilt whenever `id_generation` changes.
     *  Preserves the dynamic-wins / lowest-index tie-break of the linear scan. */
    int32_t* id_index_keys; /* component_id per slot, -1 = empty */
    int32_t* id_index_vals; /* winning component index for that id */
    uint32_t id_index_cap;  /* slot count (power of two, 0 = unallocated) */
    uint32_t id_index_gen;  /* generation the map was last built for */
    uint8_t id_index_valid; /* 0 until first successful build */
    /** Cached layout scratch (see UITree_LayoutResolve). `order`/`depth` depend
     *  only on tree topology and are recomputed only when `generation` changes;
     *  the abs_* buffers are reused across calls to avoid per-frame calloc/free. */
    int* layout_order;
    int* layout_depth;
    int* layout_abs_x;
    int* layout_abs_y;
    int* layout_abs_w;
    int* layout_abs_h;
    uint32_t layout_cap;         /* allocated length of each layout buffer */
    uint32_t layout_order_count; /* live (non-freed) entries in layout_order */
    uint32_t layout_order_gen;   /* generation order/depth were computed for */
    uint8_t layout_order_valid;
    uint16_t next_dynamic_uid;
    /** Mounted sub-interfaces (TS WidgetManager.interfaceParents). */
    struct UITreeInterfaceParent interface_parents[UITREE_INTERFACE_PARENT_MAX];
    int interface_parent_count;
    /** SETANTIDRAG — suppress new drag initiation while set. */
    uint8_t anti_drag;
    /** Set when any node's layout is invalidated (position/size/topology
     *  mutation); cleared by UITree_LayoutResolve. Lets CS2 geometry getters
     *  lazily re-resolve mid-script (reference WidgetManager.ensureLayout —
     *  scripts read computed dims immediately after if_setsize/if_setposition,
     *  e.g. dropdown scrollbar dragger sizing). */
    uint8_t layout_stale;
};

struct UITreeNodeSpec
{
    enum UITreeComponentType type;
    int component_id;

    int x;
    int y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;

    uint8_t always_dirty;
    uint8_t dynamic;
    int dynamic_child_index;
    uint8_t has_position;
    uint8_t slot_tag; /* enum UITreeSlotTag */
    struct UITreeElemPosition position;
    struct UITreeBehavior const* behavior;

    union
    {
        struct
        {
            int scene_id;
            int atlas_index;
            /* COMPASS only: inverted circular mask, see UITreeComponent. */
            int mask_scene_id;
            int mask_atlas_index;
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
            /* The pack's placeholder graphic, kept as the inverted alpha mask
             * (map shows where it is transparent). 0 = draw unmasked. */
            int mask_scene_id;
            int mask_atlas_index;
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
            uint8_t selected; /* INI selected= (see UITreeComponent) */
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
            int zan;
            int x_offset;
            int y_offset;
            uint8_t orthog;
            uint8_t fixed_zoom;
            /* Model animation sequence set via IF/CC_SETMODELANIM, or -1 = none.
             * anim_frame is advanced by the client tick driver; anim_frame_cycle
             * accumulates elapsed 50hz cycles toward the current frame's length. */
            int anim_seq_id;
            int anim_frame;
            int anim_frame_cycle;
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
        } rs_inv;
        struct
        {
            int obj_id;
            int obj_count;
            int scene_id;
            int atlas_index;
        } cc_obj;
        struct
        {
            int scroll_height;
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
            int font_id;
        } hovertext;
        struct
        {
            struct UITreeChatMinimenuConfig minimenu;
            int font_id;
        } chat;
        struct UITreeChatButtonConfig chat_button;
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
    struct UITreeMenuOptions menu_options;
};

char const*
UITree_ComponentTypeStr(enum UITreeComponentType type);

struct UITree*
UITree_New(uint32_t hint);

void
UITree_Free(struct UITree* tree);

void
UITree_MarkAllDirty(struct UITree* tree);

void
UITree_MarkNodeDirty(
    struct UITree* tree,
    int32_t idx);

/** Clear dirty after a successful emit (always_dirty nodes stay emit-eligible). */
void
UITree_ClearNodeDirty(
    struct UITree* tree,
    int32_t idx);

/** True if node should emit this frame: is_dirty || always_dirty. */
bool
UITree_NodeNeedsEmit(struct UITreeComponent const* component);

/** Force dirty on types that redraw every frame (world/minimap/compass/cross/minimenu). */
void
UITree_MarkFrameAlwaysDirtyTypes(struct UITree* tree);

int32_t
UITree_FindByComponentId(
    struct UITree const* tree,
    int component_id);

void
UITree_WalkAdvance(
    struct UITree const* tree,
    int32_t* io_current,
    int32_t* stack,
    int* io_stack_top,
    int stack_max,
    bool current_visible);

void
UITree_SetBehavior(
    struct UITree* tree,
    int32_t idx,
    struct UITreeBehavior const* src);

int32_t
UITree_Push(
    struct UITree* tree,
    int32_t parent_index,
    struct UITreeNodeSpec const* spec);

/** Link an existing unlinked component under parent (-1 = root). */
void
UITree_LinkUnderParent(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index);

/** Move child_index under new_parent_index (-1 = root list). Preserves child's subtree. */
void
UITree_Reparent(
    struct UITree* tree,
    int32_t child_index,
    int32_t new_parent_index);

void
UITree_ClearSidebarChildren(
    struct UITree* tree,
    int32_t sidebar_idx);

/** Detach every child of `owner_idx` (slot mounts: clear before re-bake). */
void
UITree_ClearChildren(
    struct UITree* tree,
    int32_t owner_idx);

int32_t
UITree_FindChildBySubid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id);

int32_t
UITree_CcCreate(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int widget_type,
    int sub_id);

/** CC_COPY: clone the dynamic child at src_sub_id into slot dst_sub_id under the
 *  same parent (replace-in-slot, like UITree_CcCreate). Only the widget's own
 *  payload is cloned — the copy starts with no children. Returns the new node
 *  index, or -1 when the source slot is empty. */
int32_t
UITree_CcCopy(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int src_sub_id,
    int dst_sub_id);

void
UITree_CcDeleteAll(
    struct UITree* tree,
    int32_t parent_index);

int
UITree_CollectDynamicChildIndices(
    struct UITree const* tree,
    int parent_component_id,
    int start_index,
    int* out_indices,
    int out_cap);

bool
UITree_ApplyHide(
    struct UITree* tree,
    int component_id,
    int hide);

bool
UITree_ApplyClickMask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask);

bool
UITree_ApplyText(
    struct UITree* tree,
    int component_id,
    char const* text);

bool
UITree_ApplyGraphic(
    struct UITree* tree,
    int component_id,
    int scene_id,
    int atlas_index);

bool
UITree_ApplyColour(
    struct UITree* tree,
    int component_id,
    int colour);

bool
UITree_ApplyPosition(
    struct UITree* tree,
    int component_id,
    int x,
    int y);

bool
UITree_ApplySize(
    struct UITree* tree,
    int component_id,
    int width,
    int height);

bool
UITree_ApplyPositionModes(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int x_mode,
    int y_mode);

bool
UITree_ApplySizeModes(
    struct UITree* tree,
    int component_id,
    int width,
    int height,
    int width_mode,
    int height_mode);

bool
UITree_ApplyGraphicTiled(
    struct UITree* tree,
    int component_id,
    int tiled);

bool
UITree_ApplyGraphicOutline(
    struct UITree* tree,
    int component_id,
    int outline);

bool
UITree_ApplyGraphicShadow(
    struct UITree* tree,
    int component_id,
    int shadow_colour);

bool
UITree_ApplyScrollSize(
    struct UITree* tree,
    int component_id,
    int scroll_width,
    int scroll_height);

bool
UITree_ApplyScrollPos(
    struct UITree* tree,
    int component_id,
    int scroll_x,
    int scroll_y);

bool
UITree_ApplyObject(
    struct UITree* tree,
    int component_id,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index);

bool
UITree_ApplyModel(
    struct UITree* tree,
    int component_id,
    int model_id);

bool
UITree_ApplyModelTransparent(
    struct UITree* tree,
    int component_id,
    int transparent);

bool
UITree_ApplyModelAngle(
    struct UITree* tree,
    int component_id,
    int xan,
    int yan,
    int zoom);

/** Set a MODEL widget's animation sequence (reference IF_SETANIM / modelAnim);
 * -1 clears it. Resets the frame counters so playback restarts. */
bool
UITree_ApplyModelAnim(
    struct UITree* tree,
    int component_id,
    int anim_seq_id);

bool
UITree_ApplyTextFont(
    struct UITree* tree,
    int component_id,
    int font_id);

bool
UITree_ApplyTextAlign(
    struct UITree* tree,
    int component_id,
    int h_align,
    int v_align,
    int line_height);

bool
UITree_ApplyTextShadow(
    struct UITree* tree,
    int component_id,
    int shadowed);

bool
UITree_ApplyTargetPriority(
    struct UITree* tree,
    int component_id,
    int priority);

/** str_mask bit i marks arg position i as a string; strs[0..str_argc) are the
 * string values in position order. Pass 0/NULL/0 for int-only hooks. */
bool
UITree_ApplyRuntimeHook(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook* slot,
    int script_id,
    int const* argv,
    int argc,
    uint32_t str_mask,
    char const* const* strs,
    int str_argc);

bool
UITree_ApplyOpBase(
    struct UITree* tree,
    int component_id,
    char const* text);

/**
 * CC/IF_SETOPKEY and the SETOPTKEY variants. op_index is 1..10, where 10 is the
 * typed-key slot. pair_count == 0, or a negative first key_char, clears the
 * slot. Returns false when the component or op_index is out of range.
 */
bool
UITree_ApplyOpKey(
    struct UITree* tree,
    int component_id,
    int op_index,
    int const* key_chars,
    int const* key_codes,
    int pair_count);

/** CC/IF_SETOPKEYRATE and the OPT variants. enabled mirrors tick_rate != 0. */
bool
UITree_ApplyOpKeyRate(
    struct UITree* tree,
    int component_id,
    int op_index,
    int rate,
    int enabled);

/** CC/IF_SETOPKEYIGNOREHELD and the OPT variants. */
bool
UITree_ApplyOpKeyIgnoreHeld(
    struct UITree* tree,
    int component_id,
    int op_index);

int
UITree_GetLayoutWidth(
    struct UITree const* tree,
    int component_id);

int
UITree_GetLayoutHeight(
    struct UITree const* tree,
    int component_id);

/** Parent-relative X for CC_GETX / IF_GETX (interfacex UITreeX_GetPosX). */
int
UITree_GetRelativeX(
    struct UITree const* tree,
    int component_id);

/** Parent-relative Y for CC_GETY / IF_GETY (interfacex UITreeX_GetPosY). */
int
UITree_GetRelativeY(
    struct UITree const* tree,
    int component_id);

/** Client.ts hide: only hide nodes are gated on hover id match. */
bool
UITree_ComponentVisibleById(
    struct UITreeComponent const* component,
    int hovered_component_id);

bool
UITree_ComponentHoveredByIds(
    int component_id,
    struct UITreeHoverIds const* hover_ids);

bool
UITree_ComponentVisibleByHoverIds(
    struct UITreeComponent const* component,
    struct UITreeHoverIds const* hover_ids);

bool
UITree_ComponentIsClickable(struct UITreeComponent const* component);

bool
UITree_ComponentHasMenuOptions(struct UITreeComponent const* component);

bool
UITree_TypeIsAlwaysDirtyFrame(enum UITreeComponentType type);

/** InterfaceParent mount table (TS WidgetManager.interfaceParents). */
int
UITree_InterfaceParentFind(
    struct UITree const* tree,
    int container_uid);

int
UITree_InterfaceParentSet(
    struct UITree* tree,
    int container_uid,
    int group_id,
    int type);

void
UITree_InterfaceParentClear(
    struct UITree* tree,
    int container_uid);

int
UITree_InterfaceParentIsMountedGroup(
    struct UITree const* tree,
    int group_id);

/** click_mask bits 17–19. */
static inline int
UITree_ClickMaskDragDepth(int32_t click_mask)
{
    return (click_mask >> UITREE_FLAG_DRAG_DEPTH_SHIFT) & UITREE_FLAG_DRAG_DEPTH_MASK;
}

/**
 * Node index of src's drag render area (clamp + script coordinate space), or
 * -1 when the widget drags freely. cc_setdraggable(parentUid, childIndex)
 * targets parent.children[childIndex]; falls back to the parent.
 */
int32_t
UITree_ResolveDragRenderArea(
    struct UITree const* tree,
    struct UITreeComponent const* src);

/** True if widget can start a drag (TS isWidgetDraggable). */
int
UITree_ComponentIsDraggable(struct UITreeComponent const* c);

/** True if node is a valid drop target (FLAG_DRAG_ON or drag/op handlers). */
int
UITree_ComponentIsDropTarget(struct UITreeComponent const* c);

/**
 * Hit-test for drop under (px,py), excluding exclude_component_id.
 * Visits InterfaceParent mounts after normal children (draw/hit order).
 * Returns component_id or -1.
 */
int
UITree_FindDropTarget(
    struct UITree const* tree,
    int px,
    int py,
    int exclude_component_id);

/** Ancestor hide check for InvTransmit gating. */
int
UITree_ComponentOrAncestorHidden(
    struct UITree const* tree,
    int component_id);

#endif
