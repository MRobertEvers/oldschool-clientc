#ifndef TORIRS_PLUGIN_API_H
#define TORIRS_PLUGIN_API_H

/* Public plugin API major 3. One existing host/runtime serves all plugins.
 * Live UI is authored through the widget API (torirs_plugin_contract.h) and
 * the application panel; a gameframe is PROVIDED through on_gameframe.
 * No previous binary ABI is accepted or preserved by this aggregate. */

#include "plugin/torirs_plugin_types.h"
#include "plugin/torirs_plugin_contract.h"

#include <stddef.h>
#include <stdint.h>

#define TORIRS_PLUGIN_API_MAJOR 3u
#define TORIRS_PLUGIN_API_MINOR 0u

/* Bytes of a semantic role / widget name and of a caption, terminator
 * included. Shared by the host's watch table and the roster labels. */
#define TORIRS_UI_NAME_MAX 128
#define TORIRS_UI_LABEL_MAX 128
#define TORIRS_FRAME_REASON_MAX 160
#define TORIRS_API_V2_MODULE_RESERVED_SLOTS 8
#define TORIRS_DESCRIPTOR_V2_RESERVED_WORDS 8

/* Existing module padding remains during the coordinated source migration.
 * Major 3 does not preserve the previous aggregate layout or binary ABI. */
#define TORIRS_API_V2_MODULE_RESERVED                                                   \
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS])(void)

struct ToriRS_Api;
struct ToriRS_Graphics;
struct ToriRS_DrawContext;
struct ToriRS_PanelBuilder;
struct ToriRS_ClientApi;
struct ToriRS_GameApi;

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

/* Resource references are uniformly zero-invalid, positive opaque tokens. A
 * zeroed state or descriptor therefore owns no accidental resource. Slot,
 * plugin-instance and incarnation encoding is a host detail and
 * never leaks into plugin code. */
/* struct ToriRS_ImageRef lives in torirs_plugin_contract.h beside the widget API that takes it. */

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
 * provision and asynchronous assets, use their narrower enums below. */
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
/* Frames                                                                   */
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

/* The live surfaces a lane lays out. The numbering is public only as the
 * argument of ToriRS_FrameApi::surface_native_size; a provided frame moves
 * the surfaces themselves through the widget API. */
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

/** Callback-scoped drawing coordinates. Bounds are local to the callback;
 * builders translate them to the underlying canvas/panel surface. */
struct ToriRS_DrawContext
{
    uint32_t struct_size;
    struct ToriRS_Rect bounds;
    struct ToriRS_Rect clip;
};

/* Callback-scoped graphics context, like Overlay.render(Graphics2D). It draws
 * primitives only; live UI is authored through widgets. Never retain it. */
struct ToriRS_Graphics
{
    uint32_t struct_size;
    void* implementation;

    void (*rect)(
        struct ToriRS_Graphics* draw,
        struct ToriRS_Rect rect,
        uint32_t rgb,
        int alpha);
    void (*line)(
        struct ToriRS_Graphics* draw,
        int x0,
        int y0,
        int x1,
        int y1,
        uint32_t rgb,
        int alpha);
    void (*text)(
        struct ToriRS_Graphics* draw,
        int x,
        int y,
        char const* text,
        uint32_t rgb);
    void (*image)(
        struct ToriRS_Graphics* draw,
        struct ToriRS_ImageRef image,
        int x,
        int y,
        int alpha);
    enum ToriRS_Result (*world_tile)(
        struct ToriRS_Graphics* draw,
        int tile_x,
        int tile_z,
        int level,
        uint32_t fill_rgb,
        uint32_t outline_rgb,
        int alpha);
    enum ToriRS_Result (*world_hull)(
        struct ToriRS_Graphics* draw,
        int element_id,
        uint32_t rgb,
        int alpha,
        int shape);
    /** Blit at native size while intersecting with a canvas-space clip. */
    void (*image_clip)(
        struct ToriRS_Graphics* draw,
        struct ToriRS_ImageRef image,
        int x,
        int y,
        struct ToriRS_Rect clip,
        int alpha);
    /** Current callback's local drawable bounds and clip. */
    bool (*context)(
        struct ToriRS_Graphics* draw,
        struct ToriRS_DrawContext* out);
};

/* A NULL id terminates an offer array. Only the fields for the chosen canvas
 * policy are meaningful: width/height for FIXED, min_* for WINDOW. An offer
 * is served by the definition's on_gameframe callback. */
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
    /* FrameOffer arrays are NULL-id terminated and therefore fixed-stride. */
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS];
};

#define TORIRS_FRAME_OFFER_REQUIRED_SIZE                                                 \
    ((uint32_t)(offsetof(struct ToriRS_FrameOffer, min_height) +                         \
                sizeof(((struct ToriRS_FrameOffer*)0)->min_height)))

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

struct ToriRS_CoreApi
{
    uint32_t struct_size;
    void (*log)(
        struct ToriRS_Api* api,
        char const* format,
        ...);
    void (*notify)(
        struct ToriRS_Api* api,
        char const* text);
    int (*screen)(struct ToriRS_Api* api);
    uint64_t (*frame_ms)(struct ToriRS_Api* api);
    uint64_t (*frame_work_us)(struct ToriRS_Api* api);
    bool (*lane)(
        struct ToriRS_Api* api,
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
        struct ToriRS_Api* api,
        char const* name);
    char const* (*plugin_id)(struct ToriRS_Api* api);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 1])(void);
};

struct ToriRS_ConfigApi
{
    uint32_t struct_size;
    bool (*has)(
        struct ToriRS_Api* api,
        char const* key);
    bool (*get_bool)(
        struct ToriRS_Api* api,
        char const* key,
        bool* out);
    bool (*get_int)(
        struct ToriRS_Api* api,
        char const* key,
        int* out);
    bool (*get_color)(
        struct ToriRS_Api* api,
        char const* key,
        uint32_t* out_rgb);
    bool (*get_string)(
        struct ToriRS_Api* api,
        char const* key,
        char const** out_value);
    enum ToriRS_Result (*set)(
        struct ToriRS_Api* api,
        char const* key,
        char const* value);
    TORIRS_API_V2_MODULE_RESERVED;
};

struct ToriRS_WorldApi
{
    uint32_t struct_size;
    /** Current scene southwest corner in absolute tiles. Available immediately
     * when enabling in a loaded world; false while no world exists. */
    bool (*scene_origin)(struct ToriRS_Api* api, int* tile_x, int* tile_z);
    bool (*local_player)(
        struct ToriRS_Api* api,
        struct ToriRS_PlayerSnapshot* out);
    int (*npc_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_NpcSnapshot* out);
    bool (*npc_by_slot)(
        struct ToriRS_Api* api,
        int slot,
        struct ToriRS_NpcSnapshot* out);
    int (*player_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_PlayerSnapshot* out);
    int (*item_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_GroundItemSnapshot* out);
    int (*scenery_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_ScenerySnapshot* out);
    TORIRS_API_V2_MODULE_RESERVED;
};

struct ToriRS_InputApi
{
    uint32_t struct_size;
    bool (*key_held)(
        struct ToriRS_Api* api,
        int key);
    bool (*pointer)(
        struct ToriRS_Api* api,
        int* out_x,
        int* out_y);
    bool (*hover_tile)(
        struct ToriRS_Api* api,
        int* out_x,
        int* out_z,
        int* out_level);
    bool (*hover_entity)(
        struct ToriRS_Api* api,
        struct ToriRS_HoverTarget* out);
    void (*text_input)(
        struct ToriRS_Api* api,
        bool enabled);
    void (*chat_focus)(
        struct ToriRS_Api* api,
        bool focused);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 1])(void);
};

/** Menu additions belong to the current on_menu_build dispatch. Text and the
 * plugin-defined action ID are retained by the host. Disable revokes them.
 * IDs describe the plugin's intended operation; a recycled native slot is
 * not a stable identity. Native operations require checked action references. */
struct ToriRS_MenuApi
{
    uint32_t struct_size;
    bool (*add)(struct ToriRS_Api* api, struct ToriRS_MenuBuildEvent* menu,
        char const* text, uint32_t action_id);
};

struct ToriRS_FrameApi
{
    uint32_t struct_size;
    int (*offer_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_FrameOfferInfo* out);
    void (*selection)(
        struct ToriRS_Api* api,
        struct ToriRS_FrameSelection* out);
    enum ToriRS_Result (*select)(
        struct ToriRS_Api* api,
        char const* id);
    void (*invalidate)(struct ToriRS_Api* api);
    bool (*surface_native_size)(
        struct ToriRS_Api* api,
        int surface,
        int* out_width,
        int* out_height);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 1])(void);
};

/* The pixels themselves are emitted through ToriRS_Graphics. This module
 * contains stable helpers that are meaningful outside one draw callback. */
struct ToriRS_DrawApi
{
    uint32_t struct_size;
    bool (*project)(
        struct ToriRS_Api* api,
        int fine_x,
        int fine_z,
        int height,
        int* out_x,
        int* out_y);
    int (*element_height)(
        struct ToriRS_Api* api,
        int element_id);
    int (*hsl_from_rgb)(
        struct ToriRS_Api* api,
        uint32_t rgb);
    uint32_t (*hsl_to_rgb)(
        struct ToriRS_Api* api,
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

struct ToriRS_AssetsApi
{
    uint32_t struct_size;
    /** Start or join a byte request and return its current authoritative state. */
    enum ToriRS_AssetState (*request)(
        struct ToriRS_Api* api,
        char const* name);
    bool (*bytes)(
        struct ToriRS_Api* api,
        char const* name,
        void const** out_data,
        size_t* out_size);
    enum ToriRS_Result (*save)(
        struct ToriRS_Api* api,
        char const* name,
        void const* data,
        size_t size);
    void (*release)(
        struct ToriRS_Api* api,
        char const* name);
    /** Start/join and decode an image. `out` is zero on terminal failure. */
    enum ToriRS_AssetState (*image)(
        struct ToriRS_Api* api,
        char const* name,
        struct ToriRS_ImageRef* out);
    bool (*image_size)(
        struct ToriRS_Api* api,
        struct ToriRS_ImageRef image,
        int* out_width,
        int* out_height);
    void (*image_release)(
        struct ToriRS_Api* api,
        struct ToriRS_ImageRef image);
    /** Start/join and decode a model. `out` is zero on terminal failure. */
    enum ToriRS_AssetState (*model)(
        struct ToriRS_Api* api,
        char const* name,
        struct ToriRS_ModelRef* out);
    void (*model_release)(
        struct ToriRS_Api* api,
        struct ToriRS_ModelRef model);
    enum ToriRS_Result (*screenshot)(
        struct ToriRS_Api* api,
        char const* destination,
        char const* name,
        char* out_path,
        size_t out_path_size);
    bool (*image_pixels)(
        struct ToriRS_Api* api,
        struct ToriRS_ImageRef image,
        uint32_t* out_argb,
        size_t capacity,
        size_t* out_count);
    enum ToriRS_AssetState (*image_compose)(
        struct ToriRS_Api* api,
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

struct ToriRS_SceneApi
{
    uint32_t struct_size;
    enum ToriRS_Result (*mesh_create)(
        struct ToriRS_Api* api,
        struct ToriRS_MeshRef* out);
    void (*mesh_destroy)(
        struct ToriRS_Api* api,
        struct ToriRS_MeshRef mesh);
    enum ToriRS_Result (*mesh_vertex)(
        struct ToriRS_Api* api,
        struct ToriRS_MeshRef mesh,
        int x,
        int y,
        int z);
    enum ToriRS_Result (*mesh_face)(
        struct ToriRS_Api* api,
        struct ToriRS_MeshRef mesh,
        int a,
        int b,
        int c,
        int hsl,
        int alpha);
    enum ToriRS_Result (*instance_create)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef* out);
    void (*instance_destroy)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance);
    enum ToriRS_Result (*instance_model)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        struct ToriRS_ModelRef model);
    enum ToriRS_Result (*instance_position)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        int tile_x,
        int tile_z,
        int level,
        int height,
        int yaw);
    void (*instance_active)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        bool active);
    enum ToriRS_Result (*instance_mesh)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        struct ToriRS_MeshRef mesh);
    enum ToriRS_Result (*instance_cache_model)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        int kind,
        int id);
    enum ToriRS_Result (*instance_recolor)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        int from_hsl,
        int to_hsl);
    void (*instance_clear_recolors)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance);
    enum ToriRS_Result (*instance_animation)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        int sequence_id,
        bool loop);
    enum ToriRS_Result (*instance_light)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance,
        int ambient,
        int contrast);
    bool (*instance_ready)(
        struct ToriRS_Api* api,
        struct ToriRS_SceneInstanceRef instance);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 7])(void);
};

struct ToriRS_PanelApi
{
    uint32_t struct_size;
    enum ToriRS_Result (*request)(
        struct ToriRS_Api* api,
        struct ToriRS_PanelDescriptor const* description);
    void (*invalidate)(struct ToriRS_Api* api);
    void (*attention)(
        struct ToriRS_Api* api,
        bool wanted);
    enum ToriRS_Result (*set_text)(
        struct ToriRS_Api* api,
        char const* id,
        char const* text);
    enum ToriRS_Result (*set_value)(
        struct ToriRS_Api* api,
        char const* id,
        int value);
    enum ToriRS_Result (*set_height)(
        struct ToriRS_Api* api,
        char const* id,
        int preferred_height);
    enum ToriRS_Result (*set_options)(
        struct ToriRS_Api* api,
        char const* id,
        char const* value,
        struct ToriRS_SelectOption const* options,
        int option_count);
    void (*redraw)(
        struct ToriRS_Api* api,
        char const* id);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 5])(void);
};

/* Explicit escape hatch for lane-specific plugins. Nothing in the widget or
 * frame modules exposes cache component ids or numeric cache ops. */
struct ToriRS_CacheApi
{
    uint32_t struct_size;
    int (*frame_root)(struct ToriRS_Api* api);
    int (*varbit)(
        struct ToriRS_Api* api,
        int id);
    int (*varp)(
        struct ToriRS_Api* api,
        int id);
    bool (*component_rect)(
        struct ToriRS_Api* api,
        int component_id,
        struct ToriRS_Rect* out);
    bool (*invoke)(
        struct ToriRS_Api* api,
        int component_id,
        int op);
    bool (*named_id)(
        struct ToriRS_Api* api,
        char const* kind,
        char const* name,
        int* out_id);
    int (*tab_active)(struct ToriRS_Api* api);
    bool (*tab_enabled)(struct ToriRS_Api* api, int tab);
    /** Native navigation is allowed from action callbacks, not from layout,
     * draw or background updates that might fight a server/script closure. */
    bool (*tab_select)(struct ToriRS_Api* api, int tab);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 4])(void);
};

/** Client-owned settings and process facts, separate from plugin config. */
struct ToriRS_ClientApi
{
    uint32_t struct_size;
    bool (*display_get)(
        struct ToriRS_Api* api,
        int setting,
        int* out_value,
        int* out_min,
        int* out_max);
    enum ToriRS_Result (*display_set)(
        struct ToriRS_Api* api,
        int setting,
        int value);
    int (*feature_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_FeatureInfo* out);
    bool (*feature_get)(
        struct ToriRS_Api* api,
        char const* key,
        int* out_value);
    enum ToriRS_Result (*feature_set)(
        struct ToriRS_Api* api,
        char const* key,
        int value);
    int (*world_cycle)(struct ToriRS_Api* api);
    bool (*datestamp)(
        struct ToriRS_Api* api,
        char* out,
        size_t out_size);
    uint32_t (*setting_color)(
        struct ToriRS_Api* api,
        int varp_id,
        uint32_t fallback);
    size_t (*memory_bytes)(struct ToriRS_Api* api);
    void (*disable_self)(
        struct ToriRS_Api* api,
        char const* reason);
    TORIRS_API_V2_MODULE_RESERVED;
};

/** Player/game data that is neither raw cache state nor scene ownership. */
struct ToriRS_GameApi
{
    uint32_t struct_size;
    bool (*skill)(
        struct ToriRS_Api* api,
        int index,
        struct ToriRS_SkillSnapshot* out);
    int (*run_energy)(struct ToriRS_Api* api);
    int (*inventory_size)(struct ToriRS_Api* api, int inventory);
    bool (*inventory_slot)(
        struct ToriRS_Api* api,
        int inventory,
        int slot,
        int* out_obj_id,
        int* out_count);
    bool (*item_info)(
        struct ToriRS_Api* api,
        int obj_id,
        struct ToriRS_ItemInfo* out);
    enum ToriRS_AssetState (*item_image)(
        struct ToriRS_Api* api,
        int obj_id,
        int count,
        int style,
        struct ToriRS_ImageRef* out);
    int (*highlight_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_HighlightItem* out);
    int (*loot_source_next)(
        struct ToriRS_Api* api,
        int iterator,
        struct ToriRS_LootSource* out);
    int (*loot_row_next)(
        struct ToriRS_Api* api,
        int source_id,
        int iterator,
        struct ToriRS_LootRow* out);
    char const* (*entity_part)(
        struct ToriRS_Api* api,
        int kind,
        int a,
        int b,
        int c,
        int d,
        char* buffer,
        size_t capacity);
    enum ToriRS_Result (*entity_look)(
        struct ToriRS_Api* api,
        char const* part,
        struct ToriRS_EntityAppearance const* look);
    enum ToriRS_Result (*entity_ops)(
        struct ToriRS_Api* api,
        char const* part,
        int mode,
        char const* const* operations,
        int operation_count,
        uint32_t action_id);
    uint64_t (*loot_revision)(struct ToriRS_Api* api);
    bool (*loot_source_clear)(
        struct ToriRS_Api* api,
        int source_id);
    TORIRS_API_V2_MODULE_RESERVED;
};

/* The coordinated major-3 migration may change this aggregate. */
struct ToriRS_Api
{
    uint32_t struct_size;
    uint32_t major_version;
    uint32_t minor_version;
    void* instance;

    struct ToriRS_WidgetApi widgets;
    struct ToriRS_ScriptApi scripts;

    struct ToriRS_CoreApi core;
    struct ToriRS_ConfigApi config;
    struct ToriRS_WorldApi world;
    struct ToriRS_InputApi input;
    struct ToriRS_MenuApi menu;
    struct ToriRS_FrameApi frame;
    struct ToriRS_DrawApi draw;
    struct ToriRS_AssetsApi assets;
    struct ToriRS_SceneApi scene;
    struct ToriRS_PanelApi panel;
    struct ToriRS_CacheApi cache;
    /* Minor-1 modules consume two pointer-sized reserved words without moving
     * any field known to 2.0. Check minor_version before requiring either. */
    struct ToriRS_ClientApi const* client;
    struct ToriRS_GameApi const* game;
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS - 2];
};

/* ------------------------------------------------------------------------ */
/* Callback table and definition                                            */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginCallbacks
{
    uint32_t struct_size;

    void (*on_start)(
        struct ToriRS_Api* api,
        void* state);
    void (*on_stop)(
        struct ToriRS_Api* api,
        void* state);
    void (*on_frame_start)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_FrameEvent const* event);
    void (*on_logic_tick)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_TickEvent const* event);
    void (*on_server_tick)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_TickEvent const* event);
    void (*on_world_loaded)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_WorldLoadedEvent const* event);
    void (*on_script_callback)(struct ToriRS_Api*,void* state,struct ToriRS_ScriptEvent const*);
    void (*on_screen_changed)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_ScreenChangedEvent const* event);
    void (*on_npc_spawn)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_NpcSnapshot const* npc);
    void (*on_npc_retype)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_NpcSnapshot const* npc);
    void (*on_npc_despawn)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_NpcSnapshot const* npc);
    void (*on_item_spawn)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_GroundItemSnapshot const* item);
    void (*on_item_changed)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_GroundItemSnapshot const* item);
    void (*on_item_despawn)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_GroundItemSnapshot const* item);
    void (*on_config_changed)(
        struct ToriRS_Api* api,
        void* state,
        char const* key);
    void (*on_asset)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_AssetEvent const* event);
    void (*on_chat_message)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_ChatMessageEvent const* event);
    void (*on_game_event)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_GameEvent const* event);
    enum ToriRS_CallbackResult (*on_key)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_KeyEvent const* event);
    enum ToriRS_CallbackResult (*on_menu_build)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_MenuBuildEvent* event);
    enum ToriRS_CallbackResult (*on_menu_select)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_MenuSelectEvent const* event);
    void (*on_draw_world)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_Graphics* draw);
    void (*on_draw_canvas)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_Graphics* draw);
    void (*on_ui_build)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_PanelBuilder* panel,
        int view);
    void (*on_ui_action)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_PanelActionEvent const* event);
    void (*on_ui_draw)(
        struct ToriRS_Api* api,
        void* state,
        char const* node,
        struct ToriRS_Graphics* draw);
    /** Frame provision through the widget API; see ToriRS_GameframeEvent.
     *  Every offer in ToriRS_PluginDef.frames is served by this callback. */
    enum ToriRS_FrameBuildResult (*on_gameframe)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_GameframeEvent const* event);
    void (*on_ui_layout)(
        struct ToriRS_Api* api,
        void* state,
        struct ToriRS_PanelLayoutEvent const* event);
};

#define TORIRS_PLUGIN_CALLBACKS_REQUIRED_SIZE ((uint32_t)sizeof(uint32_t))

enum ToriRS_PluginDefFlags
{
    TORIRS_PLUGIN_DISABLED_BY_DEFAULT = 1u << 0,
    TORIRS_PLUGIN_ESSENTIAL = 1u << 1,
    TORIRS_PLUGIN_RUNTIME_HOST = 1u << 2,
    TORIRS_PLUGIN_HIDDEN = 1u << 3,
};

struct ToriRS_PluginDef
{
    uint32_t struct_size;
    char const* id;
    char const* title;
    char const* version;
    size_t state_size;
    struct ToriRS_ConfigSchema const* config;
    struct ToriRS_FrameOffer const* frames;

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
#define TORIRS_PLUGIN_DEF_REQUIRED_SIZE                                                \
    ((uint32_t)(offsetof(struct ToriRS_PluginDef, callbacks) +                         \
                TORIRS_PLUGIN_CALLBACKS_REQUIRED_SIZE))

#endif
