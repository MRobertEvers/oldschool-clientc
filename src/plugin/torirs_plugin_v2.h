#ifndef TORIRS_PLUGIN_V2_H
#define TORIRS_PLUGIN_V2_H

/*
 * Public plugin API v2 shell.
 *
 * This header deliberately contains no host or engine declarations. A plugin
 * receives one ToriRS_ApiV2 for its instance and calls the embedded modules,
 * for example api->ui.ref(api, name) and api->placement.place(api, ...).
 * Drawing and frame construction use callback-scoped builders instead of a
 * surface token that can accidentally outlive its event.
 *
 * PluginHost_RegisterV2 constructs this table over the existing
 * language-neutral host while bundled plugins migrate incrementally.
 */

#include "plugin/torirs_plugin_types.h"

#include <stddef.h>
#include <stdint.h>

#define TORIRS_PLUGIN_API_V2_MAJOR 2u
#define TORIRS_PLUGIN_API_V2_MINOR 3u

#define TORIRS_UI_NAME_MAX 128
#define TORIRS_UI_ACTION_MAX 48
#define TORIRS_UI_LABEL_MAX 128
#define TORIRS_UI_NAMED_ACTIONS_MAX 8
#define TORIRS_PLACEMENT_RESERVATION_MAX 64
#define TORIRS_FRAME_REASON_MAX 160
#define TORIRS_FRAME_NODES_MAX 48
#define TORIRS_API_V2_MODULE_RESERVED_SLOTS 8
#define TORIRS_DESCRIPTOR_V2_RESERVED_WORDS 8

/* ApiV2 embeds its modules by value. Their size is therefore frozen for major
 * version 2: minor versions replace reserved slots instead of growing a
 * module and shifting every module that follows it. */
#define TORIRS_API_V2_MODULE_RESERVED                                                   \
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS])(void)

struct ToriRS_ApiV2;
struct ToriRS_DrawBuilder;
struct ToriRS_DrawContext;
struct ToriRS_FrameBuilder;
struct ToriRS_PanelBuilder;
struct ToriRS_ClientApiV2;
struct ToriRS_GameApiV2;

/* ------------------------------------------------------------------------ */
/* Small value types                                                        */
/* ------------------------------------------------------------------------ */

struct ToriRS_Rect
{
    int x;
    int y;
    int width;
    int height;
};

/* A semantic-name token. Zero is invalid; a valid token survives UI-tree and
 * frame rebuilds, while ToriRS_UiNodeInfo is only a current snapshot. */
struct ToriRS_UiNodeRef
{
    uint32_t value;
};

/* A composed area token. It is normally obtained from placement.area(), or
 * supplied to a selected frame provider in ToriRS_FrameBuildContext. */
struct ToriRS_PlacementAreaRef
{
    uint32_t value;
};

/* Resource references are uniformly zero-invalid, positive opaque tokens. A
 * zeroed state or descriptor therefore owns no accidental resource. Slot,
 * plugin-instance and incarnation encoding is a host detail and
 * never leaks into plugin code. */
struct ToriRS_ImageRef
{
    int value;
};

struct ToriRS_ModelRef
{
    int value;
};

struct ToriRS_MeshRef
{
    int value;
};

struct ToriRS_SceneInstanceRef
{
    int value;
};

/* Common operation outcomes. APIs with domain-specific state, such as frame
 * building and asynchronous assets, use their narrower enums below. */
enum ToriRS_Result
{
    TORIRS_RESULT_OK = 0,
    TORIRS_RESULT_NOT_FOUND,
    TORIRS_RESULT_PENDING,
    TORIRS_RESULT_UNSUPPORTED,
    TORIRS_RESULT_CONFLICT,
    TORIRS_RESULT_BUDGET,
    TORIRS_RESULT_INVALID,
    TORIRS_RESULT_ERROR,
};

enum ToriRS_CallbackResult
{
    TORIRS_CALLBACK_CONTINUE = 0,
    TORIRS_CALLBACK_CONSUME,
};

/* Stable values are stored and returned; labels are only presentation. */
struct ToriRS_SelectOption
{
    uint32_t struct_size;
    char const* value;
    char const* label;
    bool enabled;
    char const* detail;
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS];
};

#define TORIRS_SELECT_OPTION_REQUIRED_SIZE                                                \
    ((uint32_t)(offsetof(struct ToriRS_SelectOption, detail) +                           \
                sizeof(((struct ToriRS_SelectOption*)0)->detail)))

/* A definition points at one immutable, language-neutral schema. */
struct ToriRS_ConfigSchema
{
    uint32_t struct_size;
    struct ToriRS_ConfigItem const* items;
};

/** One complete player-skill snapshot, including progress-bar thresholds. */
struct ToriRS_SkillSnapshot
{
    uint32_t struct_size;
    int index;
    char name[32];
    int current_level;
    int base_level;
    int xp;
    int level_xp;
    /** Zero when this client has no further level threshold. */
    int next_level_xp;
};

#define TORIRS_SKILL_SNAPSHOT_REQUIRED_SIZE ((uint32_t)sizeof(uint32_t))

/* ------------------------------------------------------------------------ */
/* Named UI                                                                 */
/* ------------------------------------------------------------------------ */

enum ToriRS_UiFacet
{
    TORIRS_UI_FACET_BOUNDS = 1u << 0,
    TORIRS_UI_FACET_APPEARANCE = 1u << 1,
    TORIRS_UI_FACET_ACTIONS = 1u << 2,
    TORIRS_UI_FACET_ALL = (1u << 3) - 1u,
};

enum ToriRS_UiContributionMode
{
    TORIRS_UI_MODIFY = 0,
    TORIRS_UI_PROVIDE_IF_MISSING,
    TORIRS_UI_REPLACE_OR_PROVIDE,
};

enum ToriRS_UiContributionState
{
    TORIRS_UI_CONTRIBUTION_INACTIVE = 0,
    TORIRS_UI_CONTRIBUTION_ACTIVE,
    TORIRS_UI_CONTRIBUTION_TARGET_ABSENT,
    TORIRS_UI_CONTRIBUTION_CONFLICT,
};

enum ToriRS_UiNodeFlags
{
    TORIRS_UI_NODE_VISIBLE = 1u << 0,
    TORIRS_UI_NODE_ENABLED = 1u << 1,
    TORIRS_UI_NODE_BLOCKS_FRAME = 1u << 2,
    TORIRS_UI_NODE_BLOCKS_OVERLAY = 1u << 3,
    /* Select ACTIVE/ACTIVE_HOVER retained art. Owned by APPEARANCE. */
    TORIRS_UI_NODE_ACTIVE = 1u << 4,
};

enum ToriRS_UiPaintOrder
{
    TORIRS_UI_PAINT_BEFORE_PARENT = 0,
    TORIRS_UI_PAINT_AFTER_PARENT,
};

/* Clipping is part of the retained tree relationship, not an executor hint.
 * PARENT uses the resolved parent's clip; BOUNDS starts a clip at this node's
 * own rectangle for its descendants. */
enum ToriRS_UiClip
{
    TORIRS_UI_CLIP_NONE = 0,
    TORIRS_UI_CLIP_PARENT,
    TORIRS_UI_CLIP_BOUNDS,
};

/* Stable visual states shared by frame art and plugin contributions. A zero
 * state image falls back to IDLE; a zero IDLE image means no image. */
enum ToriRS_UiVisualState
{
    TORIRS_UI_VISUAL_IDLE = 0,
    TORIRS_UI_VISUAL_HOVER,
    TORIRS_UI_VISUAL_ACTIVE,
    TORIRS_UI_VISUAL_ACTIVE_HOVER,
    TORIRS_UI_VISUAL_DISABLED,
    TORIRS_UI_VISUAL_STATE_COUNT,
};

enum ToriRS_UiHitRectMode
{
    /* Use the resolved bounds rectangle. This is the zero/default value. */
    TORIRS_UI_HIT_RECT_BOUNDS = 0,
    TORIRS_UI_HIT_RECT_CUSTOM,
};

/* Retained node data used by frame builders and static contributions. Parent
 * is a semantic name, never a component id or UITree index. */
struct ToriRS_UiNode
{
    uint32_t struct_size;
    struct ToriRS_Rect bounds;
    char const* parent;
    int anchor;
    int paint_order;
    uint32_t flags;
    struct ToriRS_ImageRef image;
    char const* label;
    char const* action;

    /* Append-only rich facet data. The common fields above remain convenient
     * shorthands: `image` supplies IDLE when its bit is absent below, and
     * `action` is the one-action set when action_count is zero. */
    int clip;
    uint32_t state_image_mask;
    struct ToriRS_ImageRef state_images[TORIRS_UI_VISUAL_STATE_COUNT];
    int label_x;
    int label_y;
    int hit_rect_mode;
    struct ToriRS_Rect hit_rect;
    uint32_t action_count;
    char const* actions[TORIRS_UI_NAMED_ACTIONS_MAX];
    /* This descriptor is embedded in a strided contribution array. Consume
     * these words for minor-version fields; never grow sizeof(UiNode). */
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS];
};

/* The prefix accepted by V2.0. New readers must never access
 * fields at or after `clip` unless struct_size proves that they exist. */
#define TORIRS_UI_NODE_V2_0_SIZE ((uint32_t)offsetof(struct ToriRS_UiNode, clip))

/* Set struct_size to the caller's capacity before ui.info(). The host writes
 * no more than that prefix and preserves the accepted capacity so the same
 * older buffer is safe to reuse. */
struct ToriRS_UiNodeInfo
{
    uint32_t struct_size;
    struct ToriRS_Rect bounds;
    uint32_t available_facets;
    bool visible;
    bool enabled;
    bool active;

    /* Pointer-free current snapshot of the same three retained facets. */
    struct ToriRS_UiNodeRef parent;
    int anchor;
    int paint_order;
    int clip;
    struct ToriRS_ImageRef state_images[TORIRS_UI_VISUAL_STATE_COUNT];
    char label[TORIRS_UI_LABEL_MAX];
    int label_x;
    int label_y;
    struct ToriRS_Rect hit_rect;
    uint32_t action_count;
    char actions[TORIRS_UI_NAMED_ACTIONS_MAX][TORIRS_UI_ACTION_MAX];
};

#define TORIRS_UI_NODE_INFO_V2_0_SIZE ((uint32_t)offsetof(struct ToriRS_UiNodeInfo, parent))

/* A NULL node name terminates a static contribution array. */
struct ToriRS_UiContribution
{
    uint32_t struct_size;
    char const* node;
    int mode;
    uint32_t facets;
    struct ToriRS_UiNode value;
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS];
};

struct ToriRS_UiContributionInfo
{
    uint32_t struct_size;
    int state;
    uint32_t active_facets;
    char conflict_plugin[TORIRS_PLUGIN_NAME_MAX];
};

#define TORIRS_UI_CONTRIBUTION_INFO_REQUIRED_SIZE ((uint32_t)sizeof(uint32_t))

/* ------------------------------------------------------------------------ */
/* Placement                                                                */
/* ------------------------------------------------------------------------ */

enum ToriRS_PlacementArea
{
    TORIRS_AREA_PLATFORM_SAFE = 0,
    TORIRS_AREA_FRAME_BUILD,
    TORIRS_AREA_OVERLAY_SAFE,
    TORIRS_AREA_RAW_VIEWPORT,
};

enum ToriRS_Anchor
{
    TORIRS_ANCHOR_TOP_LEFT = 0,
    TORIRS_ANCHOR_TOP,
    TORIRS_ANCHOR_TOP_RIGHT,
    TORIRS_ANCHOR_LEFT,
    TORIRS_ANCHOR_CENTER,
    TORIRS_ANCHOR_RIGHT,
    TORIRS_ANCHOR_BOTTOM_LEFT,
    TORIRS_ANCHOR_BOTTOM,
    TORIRS_ANCHOR_BOTTOM_RIGHT,
};

enum ToriRS_Edge
{
    TORIRS_EDGE_TOP = 0,
    TORIRS_EDGE_RIGHT,
    TORIRS_EDGE_BOTTOM,
    TORIRS_EDGE_LEFT,
};

enum ToriRS_PlacementReserveResult
{
    TORIRS_RESERVE_OK = 0,
    TORIRS_RESERVE_NO_SPACE,
    TORIRS_RESERVE_BUDGET,
    TORIRS_RESERVE_INVALID,
};

/* ------------------------------------------------------------------------ */
/* Frames and callback-scoped builders                                      */
/* ------------------------------------------------------------------------ */

enum ToriRS_FrameCanvas
{
    TORIRS_FRAME_CANVAS_FIXED = 0,
    TORIRS_FRAME_CANVAS_WINDOW,
};

enum ToriRS_FrameBuildResult
{
    TORIRS_FRAME_READY = 0,
    TORIRS_FRAME_PENDING,
    TORIRS_FRAME_UNSUPPORTED,
    TORIRS_FRAME_ERROR,
};

enum ToriRS_FrameStatus
{
    TORIRS_FRAME_STATUS_NATIVE = 0,
    TORIRS_FRAME_STATUS_ACTIVE,
    TORIRS_FRAME_STATUS_LOADING,
    TORIRS_FRAME_STATUS_FALLBACK,
};

enum ToriRS_Surface
{
    TORIRS_SURFACE_VIEWPORT = 0,
    TORIRS_SURFACE_MINIMAP,
    TORIRS_SURFACE_SIDEBAR,
    TORIRS_SURFACE_CHAT,
    TORIRS_SURFACE_CHAT_BUTTONS,
    TORIRS_SURFACE_MODAL,
    TORIRS_SURFACE_COMPASS,
    TORIRS_SURFACE_ORBS,
    TORIRS_SURFACE_COUNT,
};

struct ToriRS_FrameSkin
{
    uint32_t struct_size;
    struct ToriRS_ImageRef image;
    struct ToriRS_ImageRef mask;
};

struct ToriRS_FrameScrollbar
{
    uint32_t struct_size;
    struct ToriRS_ImageRef up;
    struct ToriRS_ImageRef down;
    struct ToriRS_ImageRef track;
    /* A single-piece thumb when split_thumb is false. */
    struct ToriRS_ImageRef thumb;
    bool split_thumb;
    struct ToriRS_ImageRef thumb_top;
    struct ToriRS_ImageRef thumb_middle;
    struct ToriRS_ImageRef thumb_bottom;
};

#define TORIRS_FRAME_SCROLLBAR_V2_0_SIZE                                                  \
    ((uint32_t)offsetof(struct ToriRS_FrameScrollbar, split_thumb))

/* One retained decoration tied to a live surface. Coordinates are canvas
 * coordinates and alpha follows the rest of v2: 0 invisible, 255 opaque. */
struct ToriRS_FrameSurfaceOverlay
{
    uint32_t struct_size;
    struct ToriRS_ImageRef image;
    int x;
    int y;
    int alpha;
};

struct ToriRS_FrameBuildContext
{
    uint32_t struct_size;
    char const* offer_id;
    int canvas;
    struct ToriRS_Rect logical_canvas;
    struct ToriRS_PlacementAreaRef available;
    struct ToriRS_LaneInfo lane;
};

/** Callback-scoped drawing coordinates. Bounds are local to the callback;
 * builders translate them to the underlying canvas/panel surface. */
struct ToriRS_DrawContext
{
    uint32_t struct_size;
    struct ToriRS_Rect bounds;
    struct ToriRS_Rect clip;
};

struct ToriRS_DrawBuilder
{
    uint32_t struct_size;
    void* implementation;

    void (*rect)(
        struct ToriRS_DrawBuilder* draw,
        struct ToriRS_Rect rect,
        uint32_t rgb,
        int alpha);
    void (*line)(
        struct ToriRS_DrawBuilder* draw,
        int x0,
        int y0,
        int x1,
        int y1,
        uint32_t rgb,
        int alpha);
    void (*text)(
        struct ToriRS_DrawBuilder* draw,
        int x,
        int y,
        char const* text,
        uint32_t rgb);
    void (*image)(
        struct ToriRS_DrawBuilder* draw,
        struct ToriRS_ImageRef image,
        int x,
        int y,
        int alpha);
    enum ToriRS_Result (*world_tile)(
        struct ToriRS_DrawBuilder* draw,
        int tile_x,
        int tile_z,
        int level,
        uint32_t fill_rgb,
        uint32_t outline_rgb,
        int alpha);
    enum ToriRS_Result (*world_hull)(
        struct ToriRS_DrawBuilder* draw,
        int element_id,
        uint32_t rgb,
        int alpha,
        int shape);
    enum ToriRS_Result (*action_region)(
        struct ToriRS_DrawBuilder* draw,
        struct ToriRS_Rect rect,
        char const* action);
    /** Blit at native size while intersecting with a canvas-space clip. */
    void (*image_clip)(
        struct ToriRS_DrawBuilder* draw,
        struct ToriRS_ImageRef image,
        int x,
        int y,
        struct ToriRS_Rect clip,
        int alpha);
    /** Dynamic action region routed to callbacks.on_canvas_action by id. */
    enum ToriRS_Result (*action_region_id)(
        struct ToriRS_DrawBuilder* draw,
        struct ToriRS_Rect rect,
        char const* action,
        uint32_t action_id);
    /** Current callback's local drawable bounds and clip. */
    bool (*context)(
        struct ToriRS_DrawBuilder* draw,
        struct ToriRS_DrawContext* out);
};

struct ToriRS_FrameBuilder
{
    uint32_t struct_size;
    void* implementation;

    void (*surface)(
        struct ToriRS_FrameBuilder* frame,
        int surface,
        struct ToriRS_Rect rect);
    void (*surface_member)(
        struct ToriRS_FrameBuilder* frame,
        int surface,
        int member,
        struct ToriRS_Rect rect);
    void (*skin)(
        struct ToriRS_FrameBuilder* frame,
        int surface,
        struct ToriRS_FrameSkin const* skin);
    void (*ui_node)(
        struct ToriRS_FrameBuilder* frame,
        char const* name,
        struct ToriRS_UiNode const* node);
    void (*scrollbar)(
        struct ToriRS_FrameBuilder* frame,
        struct ToriRS_FrameScrollbar const* skin);
    void (*reason)(
        struct ToriRS_FrameBuilder* frame,
        char const* reason);
    void (*surface_overlay)(
        struct ToriRS_FrameBuilder* frame,
        int surface,
        struct ToriRS_FrameSurfaceOverlay const* overlay);
};

typedef enum ToriRS_FrameBuildResult (*ToriRS_FrameBuildCallback)(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* context);

typedef void (*ToriRS_FrameDrawCallback)(
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_DrawBuilder* draw);

/* A NULL id terminates an offer array. Only the fields for the chosen canvas
 * policy are meaningful: width/height for FIXED, min_* for WINDOW. */
struct ToriRS_FrameOffer
{
    uint32_t struct_size;
    char const* id;
    char const* title;
    int canvas;
    int width;
    int height;
    int min_width;
    int min_height;
    ToriRS_FrameBuildCallback build;
    ToriRS_FrameDrawCallback draw;
    /* FrameOffer arrays are NULL-id terminated and therefore fixed-stride. */
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS];
};

#define TORIRS_FRAME_OFFER_REQUIRED_SIZE                                                 \
    ((uint32_t)(offsetof(struct ToriRS_FrameOffer, build) +                              \
                sizeof(((struct ToriRS_FrameOffer*)0)->build)))

struct ToriRS_FrameOfferInfo
{
    uint32_t struct_size;
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char title[TORIRS_PLUGIN_TITLE_MAX];
    char provider[TORIRS_PLUGIN_NAME_MAX];
    int canvas;
    int width;
    int height;
    int min_width;
    int min_height;
    bool available;
    char detail[TORIRS_FRAME_REASON_MAX];
};

#define TORIRS_FRAME_OFFER_INFO_REQUIRED_SIZE ((uint32_t)sizeof(uint32_t))

struct ToriRS_FrameSelection
{
    uint32_t struct_size;
    char requested_id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char active_id[TORIRS_PLUGIN_FRAME_ID_MAX];
    int status;
    char reason[TORIRS_FRAME_REASON_MAX];
    uint32_t revision;
};

#define TORIRS_FRAME_SELECTION_REQUIRED_SIZE ((uint32_t)sizeof(uint32_t))

/* ------------------------------------------------------------------------ */
/* Panel builder                                                            */
/* ------------------------------------------------------------------------ */

enum ToriRS_PanelNodeKind
{
    TORIRS_PANEL_HEADING = 0,
    TORIRS_PANEL_PARAGRAPH,
    TORIRS_PANEL_LABEL,
    TORIRS_PANEL_KEY_VALUE,
    TORIRS_PANEL_TOGGLE,
    TORIRS_PANEL_INPUT,
    TORIRS_PANEL_TEXTAREA,
    TORIRS_PANEL_SELECT,
    TORIRS_PANEL_BUTTON,
    TORIRS_PANEL_SEPARATOR,
    TORIRS_PANEL_PROGRESS,
    TORIRS_PANEL_ERROR,
    TORIRS_PANEL_LIST_ROW,
    TORIRS_PANEL_CUSTOM,
    /**
     * A full-width navigation row with no checkbox.
     *
     * `label` is its primary text and optional `text` is a concise live
     * summary. Activating any part of the row reports
     * TORIRS_PANEL_ACTION_ACTIVATE. Use this for a retained list that drills
     * into details; LIST_ROW is the distinct name-plus-switch control used by
     * management screens.
     */
    TORIRS_PANEL_ACTION_ROW,
};

/** General retained panel declaration for uncommon node kinds. */
struct ToriRS_PanelNode
{
    uint32_t struct_size;
    int kind;
    char const* id;
    char const* label;
    char const* text;
    int value;
    int preferred_height;
    struct ToriRS_SelectOption const* options;
    int option_count;
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS];
};

#define TORIRS_PANEL_NODE_REQUIRED_SIZE                                              \
    ((uint32_t)(offsetof(struct ToriRS_PanelNode, value) +                           \
                sizeof(((struct ToriRS_PanelNode*)0)->value)))

struct ToriRS_PanelBuilder
{
    uint32_t struct_size;
    void* implementation;

    void (*heading)(
        struct ToriRS_PanelBuilder* panel,
        char const* text);
    void (*paragraph)(
        struct ToriRS_PanelBuilder* panel,
        char const* text);
    void (*toggle)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        char const* label,
        bool value);
    void (*select)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        char const* label,
        char const* value,
        struct ToriRS_SelectOption const* options,
        int option_count);
    void (*button)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        char const* label,
        bool enabled);
    void (*custom)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        int preferred_height);
    void (*label)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        char const* text);
    void (*key_value)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        char const* label,
        char const* value);
    enum ToriRS_Result (*node)(
        struct ToriRS_PanelBuilder* panel,
        struct ToriRS_PanelNode const* node);
    /**
     * Since API 2.2. A full-width retained navigation row with no checkbox.
     * Check struct_size against TORIRS_PANEL_BUILDER_ACTION_ROW_SIZE before
     * calling when a plugin may run on an older 2.x host.
     */
    void (*action_row)(
        struct ToriRS_PanelBuilder* panel,
        char const* id,
        char const* label,
        char const* text);
};

#define TORIRS_PANEL_BUILDER_ACTION_ROW_SIZE                                      \
    ((uint32_t)(offsetof(struct ToriRS_PanelBuilder, action_row) +                 \
                sizeof(((struct ToriRS_PanelBuilder*)0)->action_row)))

/* ------------------------------------------------------------------------ */
/* Embedded API modules                                                     */
/* ------------------------------------------------------------------------ */

struct ToriRS_CoreApiV2
{
    uint32_t struct_size;
    void (*log)(
        struct ToriRS_ApiV2* api,
        char const* format,
        ...);
    void (*notify)(
        struct ToriRS_ApiV2* api,
        char const* text);
    int (*screen)(struct ToriRS_ApiV2* api);
    uint64_t (*frame_ms)(struct ToriRS_ApiV2* api);
    uint64_t (*frame_work_us)(struct ToriRS_ApiV2* api);
    bool (*lane)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_LaneInfo* out);
    /**
     * Query a host/platform fact by stable name. Defined names are:
     *
     * - `touch`: the application is currently using touch UI/input policy;
     * - `web`: this is the Emscripten web lane;
     * - `browser`: this build supports the embedded BROWSER chrome transport.
     *
     * Unknown names and unavailable capabilities return false. Plugins do not
     * infer these answers from platform preprocessor symbols.
     */
    bool (*capability)(
        struct ToriRS_ApiV2* api,
        char const* name);
    char const* (*plugin_id)(struct ToriRS_ApiV2* api);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 1])(void);
};

struct ToriRS_ConfigApiV2
{
    uint32_t struct_size;
    bool (*has)(
        struct ToriRS_ApiV2* api,
        char const* key);
    bool (*get_bool)(
        struct ToriRS_ApiV2* api,
        char const* key,
        bool* out);
    bool (*get_int)(
        struct ToriRS_ApiV2* api,
        char const* key,
        int* out);
    bool (*get_color)(
        struct ToriRS_ApiV2* api,
        char const* key,
        uint32_t* out_rgb);
    bool (*get_string)(
        struct ToriRS_ApiV2* api,
        char const* key,
        char const** out_value);
    enum ToriRS_Result (*set)(
        struct ToriRS_ApiV2* api,
        char const* key,
        char const* value);
    TORIRS_API_V2_MODULE_RESERVED;
};

struct ToriRS_WorldApiV2
{
    uint32_t struct_size;
    bool (*local_player)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_PlayerSnapshot* out);
    int (*npc_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_NpcSnapshot* out);
    bool (*npc_by_slot)(
        struct ToriRS_ApiV2* api,
        int slot,
        struct ToriRS_NpcSnapshot* out);
    int (*player_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_PlayerSnapshot* out);
    int (*item_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_GroundItemSnapshot* out);
    int (*scenery_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_ScenerySnapshot* out);
    TORIRS_API_V2_MODULE_RESERVED;
};

struct ToriRS_InputApiV2
{
    uint32_t struct_size;
    bool (*key_held)(
        struct ToriRS_ApiV2* api,
        int key);
    bool (*pointer)(
        struct ToriRS_ApiV2* api,
        int* out_x,
        int* out_y);
    bool (*hover_tile)(
        struct ToriRS_ApiV2* api,
        int* out_x,
        int* out_z,
        int* out_level);
    bool (*hover_entity)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_HoverTarget* out);
    void (*text_input)(
        struct ToriRS_ApiV2* api,
        bool enabled);
    void (*chat_focus)(
        struct ToriRS_ApiV2* api,
        bool focused);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 1])(void);
};

struct ToriRS_UiApiV2
{
    uint32_t struct_size;
    struct ToriRS_UiNodeRef (*ref)(
        struct ToriRS_ApiV2* api,
        char const* name);
    bool (*info)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_UiNodeRef node,
        struct ToriRS_UiNodeInfo* out);
    bool (*invoke)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_UiNodeRef node,
        char const* action);
    bool (*contribution_info)(
        struct ToriRS_ApiV2* api,
        char const* node,
        uint32_t facets,
        struct ToriRS_UiContributionInfo* out);
    enum ToriRS_Result (*update)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_UiNodeRef node,
        uint32_t facets,
        struct ToriRS_UiNode const* value);
    bool (*menu_add)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_MenuBuildEvent* menu,
        char const* text,
        uint32_t action_id);
    enum ToriRS_Result (*set_enabled)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_UiNodeRef node,
        bool enabled);
    /**
     * Since API 2.3. Query and invoke the lane-owned action beneath the
     * composed node. These deliberately bypass a plugin's ACTIONS provider,
     * so a replacement can delegate to the cache control it stands for
     * without recursively invoking itself. `action` is semantic (for example
     * `activate`, or `enable`/`disable` for a two-state control); RevConfig
     * owns the live component and numeric operation on each lane. The binding
     * is open vocabulary: node/action become the role
     * `action_<node-with-dots-as-underscores>_<action>`.
     */
    bool (*base_action_available)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_UiNodeRef node,
        char const* action);
    bool (*invoke_base)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_UiNodeRef node,
        char const* action);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 5])(void);
};

struct ToriRS_PlacementApiV2
{
    uint32_t struct_size;
    uint32_t (*revision)(struct ToriRS_ApiV2* api);
    struct ToriRS_PlacementAreaRef (*area)(
        struct ToriRS_ApiV2* api,
        int area);
    bool (*primary)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_PlacementAreaRef area,
        struct ToriRS_Rect* out);
    bool (*place)(
        struct ToriRS_ApiV2* api,
        int area,
        int anchor,
        int width,
        int height,
        int margin,
        struct ToriRS_Rect* out);
    int (*rect_next)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_PlacementAreaRef area,
        int iterator,
        struct ToriRS_Rect* out);
    bool (*contains)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_PlacementAreaRef area,
        struct ToriRS_Rect rect);
    enum ToriRS_PlacementReserveResult (*reserve)(
        struct ToriRS_ApiV2* api,
        char const* name,
        int area,
        int edge,
        int pixels);
    bool (*reservation_rect)(
        struct ToriRS_ApiV2* api,
        char const* name,
        struct ToriRS_Rect* out);
    TORIRS_API_V2_MODULE_RESERVED;
};

struct ToriRS_FrameApiV2
{
    uint32_t struct_size;
    int (*offer_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_FrameOfferInfo* out);
    void (*selection)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_FrameSelection* out);
    enum ToriRS_Result (*select)(
        struct ToriRS_ApiV2* api,
        char const* id);
    void (*invalidate)(struct ToriRS_ApiV2* api);
    bool (*surface_native_size)(
        struct ToriRS_ApiV2* api,
        int surface,
        int* out_width,
        int* out_height);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 1])(void);
};

/* The pixels themselves are emitted through ToriRS_DrawBuilder. This module
 * contains stable helpers that are meaningful outside one draw callback. */
struct ToriRS_DrawApiV2
{
    uint32_t struct_size;
    bool (*project)(
        struct ToriRS_ApiV2* api,
        int fine_x,
        int fine_z,
        int height,
        int* out_x,
        int* out_y);
    int (*element_height)(
        struct ToriRS_ApiV2* api,
        int element_id);
    int (*hsl_from_rgb)(
        struct ToriRS_ApiV2* api,
        uint32_t rgb);
    uint32_t (*hsl_to_rgb)(
        struct ToriRS_ApiV2* api,
        int hsl);
    TORIRS_API_V2_MODULE_RESERVED;
};

/**
 * Authoritative state of one host-owned asynchronous request.
 *
 * PENDING means a later on_asset callback may change the answer. READY means
 * bytes or the decoded resource are usable now. MISSING and ERROR are cached
 * terminal results (file absent versus bytes present but IO/decode failed).
 * INVALID rejects the name before IO; BUDGET means the relevant bounded host
 * table has no slot. Only PENDING/READY image/model results return a nonzero
 * typed reference.
 */
enum ToriRS_AssetState
{
    TORIRS_ASSET_PENDING = 0,
    TORIRS_ASSET_READY,
    TORIRS_ASSET_MISSING,
    TORIRS_ASSET_INVALID,
    TORIRS_ASSET_BUDGET,
    TORIRS_ASSET_ERROR,
};

struct ToriRS_AssetsApiV2
{
    uint32_t struct_size;
    /** Start or join a byte request and return its current authoritative state. */
    enum ToriRS_AssetState (*request)(
        struct ToriRS_ApiV2* api,
        char const* name);
    bool (*bytes)(
        struct ToriRS_ApiV2* api,
        char const* name,
        void const** out_data,
        size_t* out_size);
    enum ToriRS_Result (*save)(
        struct ToriRS_ApiV2* api,
        char const* name,
        void const* data,
        size_t size);
    void (*release)(
        struct ToriRS_ApiV2* api,
        char const* name);
    /** Start/join and decode an image. `out` is zero on terminal failure. */
    enum ToriRS_AssetState (*image)(
        struct ToriRS_ApiV2* api,
        char const* name,
        struct ToriRS_ImageRef* out);
    bool (*image_size)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_ImageRef image,
        int* out_width,
        int* out_height);
    void (*image_release)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_ImageRef image);
    /** Start/join and decode a model. `out` is zero on terminal failure. */
    enum ToriRS_AssetState (*model)(
        struct ToriRS_ApiV2* api,
        char const* name,
        struct ToriRS_ModelRef* out);
    void (*model_release)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_ModelRef model);
    enum ToriRS_Result (*screenshot)(
        struct ToriRS_ApiV2* api,
        char const* destination,
        char const* name,
        char* out_path,
        size_t out_path_size);
    bool (*image_pixels)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_ImageRef image,
        uint32_t* out_argb,
        size_t capacity,
        size_t* out_count);
    enum ToriRS_AssetState (*image_compose)(
        struct ToriRS_ApiV2* api,
        char const* name,
        int width,
        int height,
        uint32_t const* argb,
        struct ToriRS_ImageRef* out);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 2])(void);
};

enum ToriRS_SceneModelKind
{
    TORIRS_SCENE_MODEL_CACHE = 0,
    TORIRS_SCENE_MODEL_SPOTANIM,
};

struct ToriRS_SceneApiV2
{
    uint32_t struct_size;
    enum ToriRS_Result (*mesh_create)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_MeshRef* out);
    void (*mesh_destroy)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_MeshRef mesh);
    enum ToriRS_Result (*mesh_vertex)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_MeshRef mesh,
        int x,
        int y,
        int z);
    enum ToriRS_Result (*mesh_face)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_MeshRef mesh,
        int a,
        int b,
        int c,
        int hsl,
        int alpha);
    enum ToriRS_Result (*instance_create)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef* out);
    void (*instance_destroy)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance);
    enum ToriRS_Result (*instance_model)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        struct ToriRS_ModelRef model);
    enum ToriRS_Result (*instance_position)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        int tile_x,
        int tile_z,
        int level,
        int height,
        int yaw);
    void (*instance_active)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        bool active);
    enum ToriRS_Result (*instance_mesh)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        struct ToriRS_MeshRef mesh);
    enum ToriRS_Result (*instance_cache_model)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        int kind,
        int id);
    enum ToriRS_Result (*instance_recolor)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        int from_hsl,
        int to_hsl);
    void (*instance_clear_recolors)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance);
    enum ToriRS_Result (*instance_animation)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        int sequence_id,
        bool loop);
    enum ToriRS_Result (*instance_light)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance,
        int ambient,
        int contrast);
    bool (*instance_ready)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_SceneInstanceRef instance);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 7])(void);
};

struct ToriRS_PanelApiV2
{
    uint32_t struct_size;
    enum ToriRS_Result (*request)(
        struct ToriRS_ApiV2* api,
        struct ToriRS_PanelDescriptor const* description);
    void (*invalidate)(struct ToriRS_ApiV2* api);
    void (*attention)(
        struct ToriRS_ApiV2* api,
        bool wanted);
    enum ToriRS_Result (*set_text)(
        struct ToriRS_ApiV2* api,
        char const* id,
        char const* text);
    enum ToriRS_Result (*set_value)(
        struct ToriRS_ApiV2* api,
        char const* id,
        int value);
    enum ToriRS_Result (*set_height)(
        struct ToriRS_ApiV2* api,
        char const* id,
        int preferred_height);
    enum ToriRS_Result (*set_options)(
        struct ToriRS_ApiV2* api,
        char const* id,
        char const* value,
        struct ToriRS_SelectOption const* options,
        int option_count);
    void (*redraw)(
        struct ToriRS_ApiV2* api,
        char const* id);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 5])(void);
};

/* Explicit escape hatch for lane-specific plugins. Nothing in the ui, frame,
 * or placement modules exposes cache component ids or numeric cache ops. */
struct ToriRS_CacheApiV2
{
    uint32_t struct_size;
    int (*frame_root)(struct ToriRS_ApiV2* api);
    int (*varbit)(
        struct ToriRS_ApiV2* api,
        int id);
    int (*varp)(
        struct ToriRS_ApiV2* api,
        int id);
    bool (*component_rect)(
        struct ToriRS_ApiV2* api,
        int component_id,
        struct ToriRS_Rect* out);
    bool (*invoke)(
        struct ToriRS_ApiV2* api,
        int component_id,
        int op);
    bool (*named_id)(
        struct ToriRS_ApiV2* api,
        char const* kind,
        char const* name,
        int* out_id);
    int (*tab_active)(struct ToriRS_ApiV2* api);
    bool (*tab_enabled)(struct ToriRS_ApiV2* api, int tab);
    bool (*tab_select)(struct ToriRS_ApiV2* api, int tab);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 4])(void);
};

/** Client-owned settings and process facts, separate from plugin config. */
struct ToriRS_ClientApiV2
{
    uint32_t struct_size;
    bool (*display_get)(
        struct ToriRS_ApiV2* api,
        int setting,
        int* out_value,
        int* out_min,
        int* out_max);
    enum ToriRS_Result (*display_set)(
        struct ToriRS_ApiV2* api,
        int setting,
        int value);
    int (*feature_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_FeatureInfo* out);
    bool (*feature_get)(
        struct ToriRS_ApiV2* api,
        char const* key,
        int* out_value);
    enum ToriRS_Result (*feature_set)(
        struct ToriRS_ApiV2* api,
        char const* key,
        int value);
    int (*world_cycle)(struct ToriRS_ApiV2* api);
    bool (*datestamp)(
        struct ToriRS_ApiV2* api,
        char* out,
        size_t out_size);
    uint32_t (*setting_color)(
        struct ToriRS_ApiV2* api,
        int varp_id,
        uint32_t fallback);
    size_t (*memory_bytes)(struct ToriRS_ApiV2* api);
    void (*disable_self)(
        struct ToriRS_ApiV2* api,
        char const* reason);
    TORIRS_API_V2_MODULE_RESERVED;
};

/** Player/game data that is neither raw cache state nor scene ownership. */
struct ToriRS_GameApiV2
{
    uint32_t struct_size;
    bool (*skill)(
        struct ToriRS_ApiV2* api,
        int index,
        struct ToriRS_SkillSnapshot* out);
    int (*run_energy)(struct ToriRS_ApiV2* api);
    int (*inventory_size)(struct ToriRS_ApiV2* api, int inventory);
    bool (*inventory_slot)(
        struct ToriRS_ApiV2* api,
        int inventory,
        int slot,
        int* out_obj_id,
        int* out_count);
    bool (*item_info)(
        struct ToriRS_ApiV2* api,
        int obj_id,
        struct ToriRS_ItemInfo* out);
    enum ToriRS_AssetState (*item_image)(
        struct ToriRS_ApiV2* api,
        int obj_id,
        int count,
        int style,
        struct ToriRS_ImageRef* out);
    int (*highlight_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_HighlightItem* out);
    int (*loot_source_next)(
        struct ToriRS_ApiV2* api,
        int iterator,
        struct ToriRS_LootSource* out);
    int (*loot_row_next)(
        struct ToriRS_ApiV2* api,
        int source_id,
        int iterator,
        struct ToriRS_LootRow* out);
    char const* (*entity_part)(
        struct ToriRS_ApiV2* api,
        int kind,
        int a,
        int b,
        int c,
        int d,
        char* buffer,
        size_t capacity);
    enum ToriRS_Result (*entity_look)(
        struct ToriRS_ApiV2* api,
        char const* part,
        struct ToriRS_EntityAppearance const* look);
    enum ToriRS_Result (*entity_ops)(
        struct ToriRS_ApiV2* api,
        char const* part,
        int mode,
        char const* const* operations,
        int operation_count,
        uint32_t action_id);
    uint64_t (*loot_revision)(struct ToriRS_ApiV2* api);
    bool (*loot_source_clear)(
        struct ToriRS_ApiV2* api,
        int source_id);
    TORIRS_API_V2_MODULE_RESERVED;
};

/* The layout and sizeof this aggregate are frozen for major version 2.
 * Module minor extensions consume their reserved function slots in place. */
struct ToriRS_ApiV2
{
    uint32_t struct_size;
    uint32_t major_version;
    uint32_t minor_version;
    void* instance;

    struct ToriRS_CoreApiV2 core;
    struct ToriRS_ConfigApiV2 config;
    struct ToriRS_WorldApiV2 world;
    struct ToriRS_InputApiV2 input;
    struct ToriRS_UiApiV2 ui;
    struct ToriRS_PlacementApiV2 placement;
    struct ToriRS_FrameApiV2 frame;
    struct ToriRS_DrawApiV2 draw;
    struct ToriRS_AssetsApiV2 assets;
    struct ToriRS_SceneApiV2 scene;
    struct ToriRS_PanelApiV2 panel;
    struct ToriRS_CacheApiV2 cache;
    /* Minor-1 modules consume two pointer-sized reserved words without moving
     * any field known to 2.0. Check minor_version before requiring either. */
    struct ToriRS_ClientApiV2 const* client;
    struct ToriRS_GameApiV2 const* game;
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS - 2];
};

/* ------------------------------------------------------------------------ */
/* Callback table and definition                                            */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginCallbacks
{
    uint32_t struct_size;

    void (*on_start)(
        struct ToriRS_ApiV2* api,
        void* state);
    void (*on_stop)(
        struct ToriRS_ApiV2* api,
        void* state);
    void (*on_frame_start)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_FrameEvent const* event);
    void (*on_logic_tick)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_TickEvent const* event);
    void (*on_server_tick)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_TickEvent const* event);
    void (*on_world_loaded)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_WorldLoadedEvent const* event);
    void (*on_screen_changed)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_ScreenChangedEvent const* event);
    void (*on_npc_spawn)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_NpcSnapshot const* npc);
    void (*on_npc_retype)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_NpcSnapshot const* npc);
    void (*on_npc_despawn)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_NpcSnapshot const* npc);
    void (*on_item_spawn)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_GroundItemSnapshot const* item);
    void (*on_item_changed)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_GroundItemSnapshot const* item);
    void (*on_item_despawn)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_GroundItemSnapshot const* item);
    void (*on_config_changed)(
        struct ToriRS_ApiV2* api,
        void* state,
        char const* key);
    void (*on_asset)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_AssetEvent const* event);
    void (*on_chat_message)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_ChatMessageEvent const* event);
    void (*on_game_event)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_GameEvent const* event);
    enum ToriRS_CallbackResult (*on_key)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_KeyEvent const* event);
    enum ToriRS_CallbackResult (*on_menu_build)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_MenuBuildEvent* event);
    enum ToriRS_CallbackResult (*on_menu_select)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_MenuSelectEvent const* event);
    void (*on_draw_world)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_DrawBuilder* draw);
    void (*on_draw_canvas)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_DrawBuilder* draw);
    void (*on_ui_build)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_PanelBuilder* panel,
        int view);
    void (*on_ui_action)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_PanelActionEvent const* event);
    void (*on_ui_draw)(
        struct ToriRS_ApiV2* api,
        void* state,
        char const* node,
        struct ToriRS_DrawBuilder* draw);
    void (*on_placement_changed)(
        struct ToriRS_ApiV2* api,
        void* state,
        uint32_t revision);

    /* Retained named-node callbacks. These are deliberately distinct from
     * panel custom wells: `node` survives frame and cache rebuilds. */
    void (*on_ui_node_draw)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_UiNodeRef node,
        struct ToriRS_DrawBuilder* draw);
    enum ToriRS_CallbackResult (*on_ui_node_action)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_UiNodeRef node,
        char const* action);

    /** Action declared by DrawBuilder.action_region_id. */
    enum ToriRS_CallbackResult (*on_canvas_action)(
        struct ToriRS_ApiV2* api,
        void* state,
        uint32_t action_id,
        int operation,
        int x,
        int y);

    void (*on_ui_layout)(
        struct ToriRS_ApiV2* api,
        void* state,
        struct ToriRS_PanelLayoutEvent const* event);
};

#define TORIRS_PLUGIN_CALLBACKS_REQUIRED_SIZE ((uint32_t)sizeof(uint32_t))

enum ToriRS_PluginDefV2Flags
{
    TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT = 1u << 0,
    TORIRS_PLUGIN_V2_ESSENTIAL = 1u << 1,
    TORIRS_PLUGIN_V2_RUNTIME_HOST = 1u << 2,
    TORIRS_PLUGIN_V2_HIDDEN = 1u << 3,
};

struct ToriRS_PluginDefV2
{
    uint32_t struct_size;
    char const* id;
    char const* title;
    char const* version;
    size_t state_size;
    struct ToriRS_ConfigSchema const* config;
    struct ToriRS_FrameOffer const* frames;
    struct ToriRS_UiContribution const* ui_contributions;

    /* Optional policy/ordering fields. Zero is the ordinary default. */
    uint32_t flags;
    int event_priority;
    int draw_order;

    /* Must remain last: this independently sized table may grow in a minor
     * version without moving any definition field known to older binaries. */
    struct ToriRS_PluginCallbacks callbacks;
};

/* A prefix-only definition can end inside its final embedded callback table.
 * The table's own struct_size says exactly how many callback bytes exist; the
 * unread tail is absent/defaulted. */
#define TORIRS_PLUGIN_DEF_V2_REQUIRED_SIZE                                                \
    ((uint32_t)(offsetof(struct ToriRS_PluginDefV2, callbacks) +                         \
                TORIRS_PLUGIN_CALLBACKS_REQUIRED_SIZE))

#endif
