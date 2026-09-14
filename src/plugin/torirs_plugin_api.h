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
struct ToriRS_PluginDef;

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
    /**
     * True when the server has STATED this skill: every number above is a
     * reading it sent. False means there is no reading yet -- the pre-login
     * table is a fresh account's, not an empty one, so a tracker that seeded
     * itself from it would take the login burst for one enormous gain.
     *
     * `skill()` still answers false for an unstated skill, as it always has.
     * The two false answers are told apart by this snapshot: an unstated
     * skill comes back with its own `index` and `name` and `stated` false,
     * while a skill index this client does not have comes back zeroed with
     * `index` -1.
     */
    bool stated;
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
     * Query a host/platform/engine fact by stable name. Defined names are:
     *
     * - `touch`: the application is currently using touch UI/input policy;
     * - `web`: this is the Emscripten web lane;
     * - `browser`: this build supports the embedded BROWSER chrome transport;
     * - `widgets.geometry`: the widget API reports live geometry;
     * - `scripts.callbacks`, `cs2_scripts`: this client runs CS2 UI logic;
     * - `highlight_groups`: CS2 UI logic AND the profile declares the
     *   hover-tile highlight script;
     * - `varbit:<name>`, `varp:<name>`: this revision declares that var;
     * - `server_tick`: on_server_tick is raised (every lane);
     * - `server_tick.fenced`: the wire carries an end-of-tick packet, so the
     *   tick is a real fence rather than the player-info edge;
     * - `loot_events`: loot/ground-item events are raised;
     * - `item_bonuses`: the loaded item records carry equipment-bonus params;
     * - `native_orbs`: the live interface resolves the native run orb;
     * - `if_settab`: the wire carries the server's set-tab packet;
     * - `tab_select`: `frame`/sidebar tab selection can act on this lane.
     *
     * Every answer is one expression over an ENGINE FACT -- a packet the wire
     * table carries, a ref the profile declares, a role the tree resolves, a
     * param the loaded cache carries -- never a revision or lane name.
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
    /** The box the LANE authored for one numbered MEMBER of `surface`,
     *  relative to the surface's own block. `member` is the role's own
     *  numbering, never a position in a list, and -1 is refused: the surface
     *  as a whole is surface_native_size's question and has no offset inside
     *  itself to report. False when the lane has no such member or when any
     *  box between it and the block is a proportion rather than pixels. */
    bool (*surface_member_native_box)(
        struct ToriRS_Api* api,
        int surface,
        int member,
        int* out_x,
        int* out_y,
        int* out_width,
        int* out_height);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 2])(void);
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
    /**
     * Give ONE row a new identity without re-declaring the page.
     *
     * For when a row's input identity changed but the page's row sequence did
     * not: a custom well whose y-to-item mapping moved, a row that now means
     * a different thing. The host mints that row a fresh serial, so a click
     * authored against the old picture is refused, and every other row keeps
     * its identity, the page keeps its scroll, and every other retained
     * custom run survives.
     *
     * Do NOT call this after changing only a caption or a value -- set_text
     * and set_value already state those in place, and reminting an identity
     * costs the row's presentation node for nothing. Use `invalidate` when
     * the page's SET of rows changed; that is what a rebuild is for.
     */
    enum ToriRS_Result (*reidentify)(
        struct ToriRS_Api* api,
        char const* id);
    void (*reserved_v2[TORIRS_API_V2_MODULE_RESERVED_SLOTS - 6])(void);
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

/* ------------------------------------------------------------------------ */
/* Porcelain: the retained, reconciling plugin layer                         */
/* ------------------------------------------------------------------------ */

/*
 * Porcelain is a LIBRARY, not an engine. It is a client of this API and of
 * nothing else: it holds no host state, owns no authority, and every verb
 * below expands into the same `api->widgets.*`, `api->assets.*` and
 * `api->config.*` calls a raw plugin would write by hand. It exists because
 * the record says every plugin that wrote those calls by hand got the
 * readiness convergence, the reconcile and the refusal reporting wrong in its
 * own way.
 *
 * It is reached through this function-pointer table so that C and Lua call
 * the same entry points and the Lua inventory test sees it the way it sees
 * every other namespace. `api->porcelain` is NULL in a host that did not
 * install the layer (see PluginHost_SetPorcelain); a plugin that needs it
 * checks once at start.
 *
 * Three rules the whole table obeys:
 *   - Steady state costs nothing. An unchanged description makes zero engine
 *     calls, zero allocations and one hash compare per key.
 *   - Motion does not go through describe. Animated properties take the
 *     direct path (`set`), and one revalidate is issued per frame for all of
 *     it.
 *   - Nothing is silent. Every result that is not OK or PENDING is a finding
 *     with a detail.
 */

struct Porcelain;
struct ToriRS_PorcelainDescribe;

/*
 * The portable element vocabulary. A plugin names what it wants to sit
 * beside, over or in place of; Porcelain resolves the name to whatever role
 * the current lane binds, and answers BOUND, ABSENT or PENDING. There is no
 * lane name anywhere in the resolution: an element a lane does not have
 * answers ABSENT by data.
 *
 * PORCELAIN_EL_NONE is first so that a zeroed struct PorcelainItem means "no
 * `visible_with`, no explicit depth target" rather than "the viewport". The
 * plan's sketch started the enum at VIEWPORT and had no way to say
 * "unstated"; every optional element field in this file needs one.
 */
enum PorcelainElementKind
{
    PORCELAIN_EL_NONE = 0,
    PORCELAIN_EL_VIEWPORT,
    PORCELAIN_EL_MINIMAP,
    PORCELAIN_EL_COMPASS,
    PORCELAIN_EL_ORBS,
    PORCELAIN_EL_ORB,
    PORCELAIN_EL_CHAT,
    PORCELAIN_EL_CHAT_BAR,
    PORCELAIN_EL_CHAT_BACKING,
    PORCELAIN_EL_CHAT_INPUT,
    PORCELAIN_EL_CHAT_FILTER,
    PORCELAIN_EL_REPORT_BUTTON,
    PORCELAIN_EL_PUBLIC_CHAT_BUTTON,
    PORCELAIN_EL_SIDEBAR,
    PORCELAIN_EL_TAB,
    PORCELAIN_EL_PANEL,
    PORCELAIN_EL_MODAL,
    PORCELAIN_EL_LANE_CHROME,
    /** The clipping root the adapter already knows; implicit parent of AT_*. */
    PORCELAIN_EL_FRAME_ROOT,
    PORCELAIN_EL_CANVAS,
    PORCELAIN_EL_SAFE,
    PORCELAIN_EL_USABLE,
    /** The escape hatch: a role name this lane declares, spelled by hand. */
    PORCELAIN_EL_ROLE,
    PORCELAIN_EL_KIND_COUNT
};

/** ORB members, in the order the profiles declare their roles. */
enum PorcelainOrb
{
    PORCELAIN_ORB_HITPOINTS = 0,
    PORCELAIN_ORB_PRAYER,
    PORCELAIN_ORB_RUN,
    PORCELAIN_ORB_SPEC,
    PORCELAIN_ORB_COUNT
};

/*
 * One element. `member` numbers the families that have members (ORB,
 * CHAT_FILTER, LANE_CHROME); `role` is the tab NAME for TAB, the panel NAME
 * for PANEL, and the literal role name for ROLE. Everything else leaves both
 * zero.
 */
struct PorcelainElement
{
    enum PorcelainElementKind kind;
    int member;
    char const* role;
};

#define PORCELAIN_EL(kind_suffix)                                                      \
    ((struct PorcelainElement){PORCELAIN_EL_##kind_suffix, 0, NULL})
#define PORCELAIN_ORB_EL(member_index)                                                 \
    ((struct PorcelainElement){PORCELAIN_EL_ORB, (member_index), NULL})
#define PORCELAIN_CHAT_FILTER_EL(member_index)                                         \
    ((struct PorcelainElement){PORCELAIN_EL_CHAT_FILTER, (member_index), NULL})
#define PORCELAIN_TAB_EL(tab_name)                                                     \
    ((struct PorcelainElement){PORCELAIN_EL_TAB, 0, (tab_name)})
#define PORCELAIN_PANEL_EL(panel_name)                                                 \
    ((struct PorcelainElement){PORCELAIN_EL_PANEL, 0, (panel_name)})
#define PORCELAIN_ROLE_EL(role_name)                                                   \
    ((struct PorcelainElement){PORCELAIN_EL_ROLE, 0, (role_name)})
#define PORCELAIN_CHROME_EL(member_index)                                              \
    ((struct PorcelainElement){PORCELAIN_EL_LANE_CHROME, (member_index), NULL})

enum PorcelainBind
{
    /** Expected, not resolved yet. Retried automatically; never a finding. */
    PORCELAIN_PENDING = 0,
    PORCELAIN_BOUND,
    /** A fact about this lane. One finding, then silence. */
    PORCELAIN_ABSENT
};

/*
 * A copy of the host's per-watch state buffer, stamped at the fence. Every
 * field but `bind` and `stamp` is ToriRS_WidgetState's, restated here so a
 * Porcelain plugin never has to hold a widget reference to read one.
 */
struct PorcelainElementState
{
    enum PorcelainBind bind;
    struct ToriRS_WidgetRef ref;
    uint64_t incarnation;
    /** Drawn canvas space. */
    struct ToriRS_WidgetBounds box;
    /** Native-parent-local, unscrolled. */
    struct ToriRS_WidgetBounds local;
    /** Paints this frame: every veto folded in. */
    bool presented;
    /** The node's own hide bit (a CS2 if_sethide or a dat1 IF_SETTAB). */
    bool own_hidden;
    /** The engine's native suppression. */
    bool native_hidden;
    bool input_present;
    /** A CHANGE token, never an identity. */
    uint32_t graphic_token;
    uint64_t text_hash;
    /** Lane-derived facets. Zero from every adapter that has not filled them. */
    uint32_t facets;
    /** Porcelain's fence counter when this state last changed. */
    uint32_t stamp;
};

/** @see PorcelainElementState::facets. */
enum PorcelainFacet
{
    PORCELAIN_FACET_GIVEN = 1u << 0,
    PORCELAIN_FACET_SELECTED = 1u << 1,
    PORCELAIN_FACET_FLASHING = 1u << 2,
    PORCELAIN_FACET_DRAWN = 1u << 3,
    PORCELAIN_FACET_ORIENTED = 1u << 4,
    PORCELAIN_FACET_WALKABLE = 1u << 5,
    PORCELAIN_FACET_ACTIVE = 1u << 6,
    PORCELAIN_FACET_HIDDEN_BY_CUTSCENE = 1u << 7
};

/*
 * Where an item goes. Porcelain OWNS the box for every one of these: the
 * engine's anchor carries ordering and visibility and NO geometry, so
 * REPLACE, INSIDE and BESIDE all copy the target's parent-local box (plus the
 * centring offset) onto the control at every fence the target's box moved.
 * Controls are created as SIBLINGS under the target's parent, because a
 * REPLACE from a child of the target is ANCHOR_INVALID.
 *
 * There is deliberately no placement by parenting.
 */
enum PorcelainPlacementKind
{
    PORCELAIN_REPLACE = 0,
    PORCELAIN_INSIDE,
    PORCELAIN_BESIDE,
    PORCELAIN_AT_ELEMENT,
    PORCELAIN_AT_CANVAS,
    PORCELAIN_AT_USABLE,
    /*
     * A CHILD of the element, in the element's own coordinates, with NO
     * anchor.
     *
     * INSIDE makes a sibling under the target's parent and anchors it OVER,
     * which REPLACE needs and a corner ornament does not. One live anchor is
     * what makes UITree_FrameHasDepth true, and the ledger prices that at
     * 13.5 ms a frame on osrs239 -- so a mode that cost no anchor before the
     * layer existed must still cost none through it. Corner and offsets are
     * read exactly as INSIDE reads them.
     */
    PORCELAIN_WITHIN
};

/** INSIDE corners. Carried in PorcelainPlacement::corner_or_side. */
enum PorcelainCorner
{
    PORCELAIN_TOP_LEFT = 0,
    PORCELAIN_TOP_RIGHT,
    PORCELAIN_BOTTOM_LEFT,
    PORCELAIN_BOTTOM_RIGHT,
    PORCELAIN_CENTRE
};

/** BESIDE sides. Carried in PorcelainPlacement::corner_or_side. */
enum PorcelainSide
{
    PORCELAIN_LEFT = 0,
    PORCELAIN_RIGHT,
    PORCELAIN_ABOVE,
    PORCELAIN_BELOW
};

struct PorcelainPlacement
{
    enum PorcelainPlacementKind kind;
    /** The element the item belongs to. AT_CANVAS/AT_USABLE use FRAME_ROOT. */
    struct PorcelainElement on;
    /** INSIDE: a PorcelainCorner. BESIDE: a PorcelainSide. Else unread. */
    int corner_or_side;
    int dx, dy;
    /** Optional: sit over (or behind) THIS element instead of `on`. */
    struct PorcelainElement depth;
    bool behind;
};

enum PorcelainItemKind
{
    PORCELAIN_ITEM_CONTROL = 0,
    PORCELAIN_ITEM_PIECE,
    PORCELAIN_ITEM_TEXT,
    PORCELAIN_ITEM_BLOCKER
};

typedef void (*PorcelainOpFn)(struct ToriRS_Api* api, void* user, char const* key);

/*
 * One described item. The kind picks the fields that are read; everything
 * else is ignored, so a zeroed struct plus three fields is a legal item.
 */
struct PorcelainItem
{
    /** Stable identity. A key not re-described is removed. */
    char const* key;
    /** Asset name or derived key. NULL means "exists, draws nothing" -- on a
     *  Control and a Blocker that is an invisible hit box, which is what the
     *  two frame providers ship a 1x1 transparent PNG to fake today. */
    char const* image;
    struct PorcelainPlacement place;
    /** 0 = natural (the target's size). MANDATORY on Text: no verb measures a
     *  string in the widget's face, so a text box cannot be derived. */
    int w, h;
    /** 255 opaque .. 0 invisible. @see PORCELAIN_OPACITY_DEFAULT */
    int opacity;
    char const* text;
    uint32_t rgb;
    /** 0 left, 1 centre, 2 right. Centre is the default the shipped
     *  performance readout is pinned to. */
    int align;
    bool outline;
    char const* op_label;
    PorcelainOpFn on_op;
    void* user;
    /** A hit box at all. False = paint only, no menu row, no click. */
    bool hit;
    /** Op armed AND hit box live. False = drawn, inert, no menu row. */
    bool enabled;
    /** An extra presented-gate. OVER inherits nothing, so a plate over the
     *  compass says visible_with = COMPASS and is hidden in step with it. */
    struct PorcelainElement visible_with;
};

/** `opacity` 0 in a zeroed item means opaque, not invisible. */
#define PORCELAIN_OPACITY_DEFAULT 0

/** A member keeps its block-relative offset when the block moves. */
#define PORCELAIN_KEEP_RELATIVE INT32_MIN

/** The direct per-frame path's field selector. @see ToriRS_PorcelainApi::set */
enum PorcelainMotionMask
{
    PORCELAIN_MOTION_X = 1u << 0,
    PORCELAIN_MOTION_Y = 1u << 1,
    PORCELAIN_MOTION_OPACITY = 1u << 2,
    PORCELAIN_MOTION_IMAGE = 1u << 3,
    /** A per-frame readout is a STRING that changes every frame. Without
     *  this the direct path could not carry one, and a frame counter had to
     *  re-describe and invalidate to move a number. */
    PORCELAIN_MOTION_TEXT = 1u << 4,
    PORCELAIN_MOTION_RGB = 1u << 5
};

struct PorcelainMotion
{
    int32_t x, y;
    int opacity;
    char const* image;
    char const* text;
    uint32_t rgb;
    /** Which fields above to read. @see PorcelainMotionMask */
    unsigned mask;
};

/*
 * Findings. Every refused operation, absent element, terminal asset state,
 * unsupported capability and lost arbitration is recorded here, coalesced on
 * (verb, element, result), and traced once at birth under
 * TORIRS_TRACE_NATIVE_UI as
 *
 *   PORCELAIN_FINDING plugin= verb= element= result= detail= expected=
 *                     first_frame= count=
 *
 * Silence is the class the record says hurt most: a plugin that ignores a
 * return value has no consequence until a screenshot is zoomed.
 */
enum PorcelainFindingResult
{
    PORCELAIN_FINDING_OK = 0,
    /** A described element this lane does not have. */
    PORCELAIN_FINDING_ABSENT,
    /** Declared absent through expect_absent, and absent. Ignored by the gate. */
    PORCELAIN_FINDING_ABSENT_EXPECTED,
    /** Declared absent through expect_absent, and it BOUND. A failure: a stale
     *  declaration must fail loudly in both directions. */
    PORCELAIN_FINDING_ABSENT_UNEXPECTEDLY_PRESENT,
    /** An engine setter answered something that was not OK or PENDING. */
    PORCELAIN_FINDING_REFUSED,
    /** Another plugin owns this aspect of this element. `detail` names it. */
    PORCELAIN_FINDING_ARBITRATION_LOST,
    PORCELAIN_FINDING_ASSET_MISSING,
    PORCELAIN_FINDING_ASSET_ERROR,
    PORCELAIN_FINDING_DERIVED_FAILED,
    /** A per-plugin budget: items, findings, images, config value length. */
    PORCELAIN_FINDING_BUDGET,
    /** A capability this lane does not answer. `detail` names the feature. */
    PORCELAIN_FINDING_UNSUPPORTED
};

struct PorcelainFinding
{
    char const* verb;
    struct PorcelainElement element;
    /** A PorcelainFindingResult. */
    int result;
    /** The winner of an arbitration, the asset name, the refused route label. */
    char const* detail;
    /** Declared through expect_absent: ignored by the clean gate. */
    bool expected;
    uint32_t first_frame, count;
};

/** The aspects arbitration hands out, one owner per element per aspect. */
enum PorcelainAspect
{
    PORCELAIN_ASPECT_REPLACE = 0,
    PORCELAIN_ASPECT_MOVE_POSITION,
    PORCELAIN_ASPECT_MOVE_SIZE,
    PORCELAIN_ASPECT_HIDE,
    PORCELAIN_ASPECT_SKIN_ART,
    PORCELAIN_ASPECT_SKIN_MASK,
    PORCELAIN_ASPECT_OPACITY,
    PORCELAIN_ASPECT_COUNT
};

/*
 * The six inputs a description depends on. Describe re-runs when one of them
 * moved and NEVER per frame. ELEMENT and ASSET move by themselves, from the
 * watch-state callback and from an asset state transition Porcelain observes
 * at the fence; the other four are the plugin forwarding its own host events,
 * because a library cannot install callbacks into a definition the host
 * already registered.
 */
enum PorcelainInput
{
    PORCELAIN_INPUT_ELEMENT = 0,
    PORCELAIN_INPUT_ASSET,
    PORCELAIN_INPUT_CONFIG,
    PORCELAIN_INPUT_SCREEN,
    PORCELAIN_INPUT_CANVAS,
    PORCELAIN_INPUT_EXPLICIT,
    PORCELAIN_INPUT_COUNT
};

enum PorcelainCadence
{
    PORCELAIN_LOGIC_TICK = 0,
    PORCELAIN_SERVER_TICK,
    PORCELAIN_FRAME,
    PORCELAIN_CADENCE_COUNT
};

/** @see ToriRS_PorcelainApi::when_ready. A bit set. */
enum PorcelainReady
{
    PORCELAIN_READY_GAME = 1u << 0,
    PORCELAIN_READY_WORLD = 1u << 1,
    /** A skill has been STATED. Not "the table is populated". */
    PORCELAIN_READY_STATS = 1u << 2,
    PORCELAIN_READY_PLAYER = 1u << 3,
    /** Every derived image this plugin asked for exists. */
    PORCELAIN_READY_DERIVED = 1u << 4
};

enum PorcelainAssetState
{
    PORCELAIN_ASSET_READY = 0,
    /** Web: fetched on demand. Retried; never a finding. */
    PORCELAIN_ASSET_PENDING,
    /** Terminal, remembered, one finding. */
    PORCELAIN_ASSET_MISSING,
    /** Terminal, remembered, one finding. */
    PORCELAIN_ASSET_ERROR
};

enum PorcelainDerivedState
{
    PORCELAIN_DERIVED_READY = 0,
    PORCELAIN_DERIVED_PENDING,
    /** Terminal and one finding. A Skin whose image is FAILED is not emitted:
     *  the masks that never built left a square minimap in a round window. */
    PORCELAIN_DERIVED_FAILED
};

/** RuneLite's four thresholds. @see ToriRS_PorcelainApi::tier */
struct PorcelainTiers
{
    int64_t low, medium, high, insane;
};

enum PorcelainTier
{
    PORCELAIN_TIER_NONE = 0,
    PORCELAIN_TIER_LOW,
    PORCELAIN_TIER_MEDIUM,
    PORCELAIN_TIER_HIGH,
    PORCELAIN_TIER_INSANE
};

/** @see ToriRS_PorcelainApi::setting */
enum PorcelainSettingFlags
{
    /** The row's checkbox is drawn as 1 - varbit: a varbit of 0 is ticked. */
    PORCELAIN_SETTING_INVERTED = 1u << 0
};

/* ------------------------------------------------------------------------ */
/* The overlay verbs                                                        */
/* ------------------------------------------------------------------------ */

/*
 * What one draw callback may draw on, and where an element sits on it.
 *
 * `bounds` and `clip` are the callback's own, pass-local: the engine sets a
 * draw region for all three passes (DRAW_WORLD, DRAW_CANVAS, PANEL_DRAW), and
 * a builder coordinate is relative to it.
 *
 * `usable` and `element` are CANVAS-space answers, and they are only in the
 * pass's own coordinates when the pass IS the canvas -- which the world and
 * canvas passes are, at origin zero, and a panel well is not. `canvas_space`
 * says which, rather than leaving a caller to clamp a canvas rectangle
 * against a well's local one and flip its tooltip over the minimap.
 */
struct PorcelainDrawContext
{
    struct ToriRS_Rect bounds;
    struct ToriRS_Rect clip;
    /** The usable canvas. Zero when this pass is not in canvas space. */
    struct ToriRS_Rect usable;
    /** The stated element's box. Zero when none was stated, it is not bound,
     *  or this pass is not in canvas space. */
    struct ToriRS_Rect element;
    bool element_bound;
    bool canvas_space;
};

/*
 * Which container a hovered cell belongs to.
 *
 * An inventory cell, a worn slot and a bank cell are three different hovers,
 * and a key built from the obj alone makes a bank tooltip out of an inventory
 * one. The three named rungs are the ones the portable vocabulary can answer
 * -- PANEL("inventory"), PANEL("equipment"), PANEL("bank") -- and a lane
 * whose profile declares no such panel answers OTHER, carrying
 * `container_id`, so two containers this vocabulary cannot name are still two
 * keys.
 */
enum PorcelainContainer
{
    /** The row is not about a container cell at all. */
    PORCELAIN_CONTAINER_NONE = 0,
    PORCELAIN_CONTAINER_INV,
    PORCELAIN_CONTAINER_WORN,
    PORCELAIN_CONTAINER_BANK,
    PORCELAIN_CONTAINER_OTHER
};

/** The hovered cell, as the menu build answered it. @see PorcelainContainer */
struct PorcelainHover
{
    int obj;
    enum PorcelainContainer container;
    /** The cell's own `(interface << 16) | component`. Part of every key. */
    int container_id;
    int slot;
    /** The Porcelain frame this was stamped in. */
    uint32_t frame;
};

/*
 * The CS2 caption hook's three states.
 *
 * SUPPRESSING: the lane has the hook, the cache's own label widgets are being
 * hidden, and the plugin's own captions stand in for them.
 * FORMATTING: the hook has fired once, so the natives carry the plugin's
 * fields and the suppression is handed back.
 * ABSENT: this lane has no such hook. ONE finding, and the callback never
 * fires -- an overlay that quietly did nothing is the class this replaces.
 */
enum PorcelainNativeOverlayState
{
    PORCELAIN_NATIVE_OVERLAY_ABSENT = 0,
    PORCELAIN_NATIVE_OVERLAY_SUPPRESSING,
    PORCELAIN_NATIVE_OVERLAY_FORMATTING
};

/* ------------------------------------------------------------------------ */
/* Panels: the row model                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Which of a plugin's two faces one description covers.
 *
 * The page and the settings form are two destinations, and the host clears
 * and re-declares when it changes which one it is showing. A description that
 * covers a face is emitted there; a face it does not cover is left to the
 * host, which presents the form generated from the plugin's config schema --
 * exactly what a plugin with no settings handler has always got.
 */
enum PorcelainFace
{
    PORCELAIN_FACE_PAGE = 1u << 0,
    PORCELAIN_FACE_SETTINGS = 1u << 1
};
#define PORCELAIN_FACE_BOTH (PORCELAIN_FACE_PAGE | PORCELAIN_FACE_SETTINGS)

/*
 * A row kind. This is the panel vocabulary every executor presents natively,
 * named once here so a Porcelain plugin never reaches for the host's widget
 * enum and never picks between `builder->node` and the eight shorthands.
 */
enum PorcelainRowKind
{
    PORCELAIN_ROW_HEADING = 0,
    PORCELAIN_ROW_PARAGRAPH,
    PORCELAIN_ROW_LABEL,
    PORCELAIN_ROW_KEY_VALUE,
    PORCELAIN_ROW_TOGGLE,
    PORCELAIN_ROW_SELECT,
    PORCELAIN_ROW_BUTTON,
    PORCELAIN_ROW_ACTION_ROW,
    PORCELAIN_ROW_SEPARATOR,
    PORCELAIN_ROW_PROGRESS,
    PORCELAIN_ROW_CUSTOM,
    PORCELAIN_ROW_KIND_COUNT
};

/** What a person did to one row. `kind` is a ToriRS_PanelActionKind. */
struct PorcelainRowAction
{
    char const* key;
    int kind;
    int value;
    /** Never NULL. For a SELECT pick this is the stable VALUE, not a label. */
    char const* text;
    /** CUSTOM-well-local logical coordinates; 0 for an ordinary row. */
    int x, y;
};

typedef void (*PorcelainRowActionFn)(struct ToriRS_Api* api, void* user,
                                     struct PorcelainRowAction const* action);
/** One CUSTOM well's paint pass, routed by key from the plugin's on_ui_draw. */
typedef void (*PorcelainRowPaintFn)(struct ToriRS_Api* api, void* user, char const* key,
                                    struct ToriRS_Graphics* draw);

/*
 * One described row.
 *
 * A zeroed PorcelainRow is a legal, live HEADING with no text -- which is why
 * the inert spelling is `disabled` and not the plan's `enabled`. A row model
 * whose zero value is an inert button would have reproduced, in the verb that
 * replaces it, exactly the silent no-op the host fix H2 had to remove.
 *
 * `label` is DECLARATION IDENTITY for KEY_VALUE, TOGGLE, SELECT and
 * ACTION_ROW, because the host builds those from `label` and its patch path
 * cannot restate one; changing it is a rebuild and Porcelain says so. For
 * HEADING, PARAGRAPH, LABEL and BUTTON the string travels as the row's text,
 * which IS patched, so either spelling works and neither costs a rebuild.
 */
struct PorcelainRow
{
    /** Stable identity, and the id every action and setter names. */
    char const* key;
    enum PorcelainRowKind kind;
    char const* label;
    char const* text;
    /** TOGGLE: the checked state. PROGRESS: the bar. Unread elsewhere. */
    int value;
    /** SELECT. Copied: the caller's array may be a stack local. */
    struct ToriRS_SelectOption const* options;
    int option_count;
    /** CUSTOM: the well's logical height. */
    int height;
    /** BUTTON: drawn dim, and the HOST refuses the activation -- which is
     *  the half that holds for every presenter rather than only the one that
     *  draws, so Porcelain states the flag and does not gate the route. */
    bool disabled;
    /**
     * CUSTOM: the y-to-item identity.
     *
     * The strip is ONE control, so a click is arithmetic on the order that
     * was PAINTED. When that order changes, the row takes a new input
     * identity and a click queued against the old picture is refused --
     * without the page, the scroll, or any other row's retained run moving.
     * A value change must NOT be in this hash, or every readout costs an
     * identity.
     */
    uint64_t hit_key;
    /**
     * CUSTOM: what the next paint will draw.
     *
     * A well is one control and its picture is retained, so a readout that
     * changed inside it moves no property the host can see: the height is the
     * same, the identity is the same, and nothing repaints. This is the hash
     * of whatever the paint reads, and a change to it is one panel.redraw on
     * that row -- which is how a tracker restates six figures twice a second
     * without touching the page.
     */
    uint64_t paint_key;
    PorcelainRowActionFn on_action;
    PorcelainRowPaintFn paint;
    void* user;
};

/**
 * Which way a root runs its sidebar tabs. @see ToriRS_PorcelainApi::tab_group
 *
 * Both, and not one, because the two shipped roots disagree: 548 and 161 lay
 * their stones out in horizontal ROWS and 601 stacks them in two vertical
 * COLUMNS, and a frame that asks the wrong way round gets one group holding
 * everything. A root answers both questions; the frame asks the one its own
 * rail is shaped like.
 */
enum PorcelainTabAxis
{
    PORCELAIN_TAB_ROWS = 0,
    PORCELAIN_TAB_COLUMNS
};

typedef void (*PorcelainDescribeFn)(struct ToriRS_PorcelainDescribe* describe, void* user);
/*
 * `elapsed_ms` is the REAL time since this timer last fired, not the interval
 * it asked for. A frames-per-second figure is drawn_delta * 1000 / elapsed,
 * and a clock that assumed its nominal interval printed a number that was
 * wrong by however much the frame budget slipped.
 */
typedef void (*PorcelainTickFn)(struct ToriRS_Api* api, void* user, uint64_t elapsed_ms);
typedef void (*PorcelainReadyFn)(struct ToriRS_Api* api, void* user, unsigned what);
typedef void (*PorcelainEdgeFn)(struct ToriRS_Api* api, void* user, bool down);
/** Fill `argb` (w*h pixels) and return true. False is a terminal FAILED. */
typedef bool (*PorcelainPaintFn)(struct ToriRS_Api* api, void* user, uint32_t* argb, int w, int h);
/** The lane's own caption script, once the latch has handed the natives back.
 *  False is a parse refusal: one finding, the latch unchanged. */
typedef bool (*PorcelainScriptFn)(struct ToriRS_Api* api, void* user,
                                  struct ToriRS_ScriptEvent const* event);
/** Parse a shipped data file. False is one finding; the bytes are released
 *  either way, because a table is read once and lives in the plugin. */
typedef bool (*PorcelainParseFn)(struct ToriRS_Api* api, void* user, void const* data,
                                 size_t size);

/*
 * The describe builder. Handed to the describe function and legal only inside
 * it; every verb on it asserts that. Items are applied in DESCRIPTION ORDER
 * and a later item is over an earlier one by default -- which is the linear
 * anchor chain both frame providers build by hand today and the sketch's
 * "OVER or BEHIND one element" could not state.
 */
struct ToriRS_PorcelainDescribe
{
    uint32_t struct_size;
    void* implementation;
    /** The handle this run belongs to, for the element and helper verbs. */
    struct Porcelain* porcelain;

    /** image + op + hit box. */
    void (*control)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item);
    /** image, no op, no hit box. */
    void (*piece)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item);
    /** text in an explicit box. */
    void (*text)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item);
    /** an invisible, armed hit box. */
    void (*blocker)(struct ToriRS_PorcelainDescribe* describe, char const* key, char const* label,
                    struct PorcelainPlacement place, int w, int h, PorcelainOpFn on_op, void* user);

    /* Element edits. Retained while described, released when not
     * re-described -- moves included: there is no pre-claim snapshot in the
     * engine, so the diff is the only thing that undoes a move. */
    void (*move)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                 struct ToriRS_WidgetBounds box, int anchor_modes);
    /** A presentation hide. Never an unhide: native hide is authoritative. */
    void (*hide)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element);
    /** Independent halves: either may be NULL to leave that half alone. */
    void (*skin)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                 char const* image, char const* mask);
    void (*opacity)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                    int opacity);
    /** This plugin's feature cannot run on this lane. One finding, no items. */
    void (*unsupported)(struct ToriRS_PorcelainDescribe* describe, char const* reason);

    /* Panel rows. Described in order; the ordered (key, kind, identity label)
     * sequence IS the page's declaration, and a change to it is the one
     * legitimate rebuild. Everything else is a setter on the row it names. */
    void (*row)(struct ToriRS_PorcelainDescribe* describe, struct PorcelainRow const* row);
    /** Mint ONE row a new serial: growth without a page rebuild. Legal only
     *  for a key described in this same run. */
    void (*reidentify)(struct ToriRS_PorcelainDescribe* describe, char const* key);
};

/*
 * The namespace. Every verb takes the plugin's Porcelain handle, so one
 * process may hold many and arbitration between them is by open order, which
 * is the host's event order.
 */
struct ToriRS_PorcelainApi
{
    uint32_t struct_size;

    /* ---------------------------------------------------------- lifecycle */
    struct Porcelain* (*open)(struct ToriRS_Api* api, struct ToriRS_PluginDef const* def,
                              void* state);
    /** Removes every owned control, releases every image, resets every edit,
     *  and relinquishes every claim. NULL is accepted: this is a deallocator. */
    void (*close)(struct Porcelain* porcelain);
    /** One describe function per plugin. Replacing it invalidates. */
    void (*describe)(struct Porcelain* porcelain, PorcelainDescribeFn fn, void* user);
    /** Re-run describe at the next fence. == note(PORCELAIN_INPUT_EXPLICIT). */
    void (*invalidate)(struct Porcelain* porcelain);
    /** One of this plugin's inputs moved. @see PorcelainInput */
    void (*note)(struct Porcelain* porcelain, enum PorcelainInput input);
    /** Reconcile this plugin: re-describe if an input moved, diff, stage the
     *  setters. Called once per frame, from on_frame_start. */
    void (*fence)(struct Porcelain* porcelain);
    /** Flush every fenced plugin with EXACTLY ONE widgets.revalidate. Called
     *  once per frame after the last plugin has fenced; a `fence` that finds
     *  an unflushed epoch flushes it first and records a budget finding. */
    void (*commit)(struct ToriRS_Api* api);
    /** Drop every claim this plugin holds, at the fence, before arbitration
     *  runs again. A frame provider whose offer is released calls this, or a
     *  provider switch hands the outgoing provider all eight surfaces. */
    void (*relinquish)(struct Porcelain* porcelain);

    /* ----------------------------------------------- elements and counting */
    /** True when BOUND. Legal inside describe, where it also registers the
     *  watch dependency that makes the describe re-run when the element
     *  changes. `out` is filled whatever the bind state. */
    bool (*element)(struct Porcelain* porcelain, struct PorcelainElement element,
                    struct PorcelainElementState* out);
    /** How many members this lane has: CHAT_FILTER 8 or 4, TAB 14 or 13,
     *  LANE_CHROME 0..2, ORB 4 or 0. Lane DATA, never a lineage guess. */
    int (*count)(struct Porcelain* porcelain, enum PorcelainElementKind family);

    /* --------------------------------------------- the direct motion path */
    /** From on_frame_start. The key must be in the applied description. Each
     *  field is compare-then-set; the single revalidate is `commit`'s. */
    enum ToriRS_Result (*set)(struct Porcelain* porcelain, char const* key,
                              struct PorcelainMotion const* motion);

    /* ------------------------------------------------------------ findings */
    int (*findings)(struct Porcelain* porcelain, struct PorcelainFinding* out, int capacity);
    /** Declare that this lane does not have `element`. Legal at any time --
     *  the honest source is the element STATE, which is PENDING at on_start,
     *  so a plugin that only wants to excuse a REAL absence must be able to
     *  say so at the fence where the absence is first reported. Still
     *  bidirectional: a declared-absent element that BINDS is a failure. */
    void (*expect_absent)(struct Porcelain* porcelain, struct PorcelainElement element,
                          char const* why);
    /** Declare that `feature` cannot run on this lane. One expected finding,
     *  and every later UNSUPPORTED finding naming that feature is expected
     *  too -- which is what makes "declare rather than go quiet" reachable
     *  for a limitation that is not an element absence known at on_start. */
    void (*expect_unsupported)(struct Porcelain* porcelain, char const* feature,
                               char const* why);
    bool (*has)(struct Porcelain* porcelain, char const* capability);
    /** False turns the feature off and records ONE finding naming it. */
    bool (*require)(struct Porcelain* porcelain, char const* capability, char const* feature);

    /* ------------------------------------------------------------- helpers */
    /** Pure. Strictly greater at every threshold; a threshold at or below zero
     *  disables its tier. Both shipped plugins disagreed with the reference
     *  and with each other on exactly these two rules. */
    int (*tier)(struct PorcelainTiers const* tiers, int64_t value);
    /** The CALLER's own four keys. No cross-plugin config read exists. */
    bool (*tiers_from_config)(struct Porcelain* porcelain, struct PorcelainTiers* out);
    /** Measure before joining. Over the value ceiling: refuse, leave the
     *  stored list unchanged, one finding. Never truncate -- a tag list cut
     *  mid-id stores a wrong species and reads back as one. */
    bool (*config_list_add)(struct Porcelain* porcelain, char const* key, char const* item);
    /** Subject and intended operation both frozen into the retained menu row,
     *  so a recycled slot cannot retarget or invert it. */
    uint32_t (*menu_tag)(int subject, int op);
    /** A named varbit read as a setting. Absent is OFF with ONE finding
     *  across many reads. @see PorcelainSettingFlags */
    bool (*setting)(struct Porcelain* porcelain, char const* varbit_name, unsigned flags);
    /** The same named row read as a NUMBER -- a slider, a count, a coord --
     *  spelled `varbit:<name>`, `varp:<name>` or a bare name for a varbit.
     *  `absent` is the CALLER's answer for a row this profile does not
     *  declare, and it is always that feature's OFF answer: a value read off
     *  a var that does not exist would be a silent zero. The resolved id is
     *  memoised for the handle's life -- what a profile declares cannot
     *  change under it, and the shipped builtins were re-asking per spawn. */
    int (*setting_value)(struct Porcelain* porcelain, char const* name, int absent);
    /** A key edge named by a config key, whose value may be a name, a decimal
     *  key code or a single character. ABSENT on a touch lane, with one
     *  finding: the feature reports itself off instead of appearing to work.
     *  The edge comes from note_key, not from a fence poll -- a poll cannot
     *  see a press that opens and closes inside one frame. */
    bool (*key_edge)(struct Porcelain* porcelain, char const* config_key, PorcelainEdgeFn fn,
                     void* user);
    /** The plugin forwarding its own on_key. */
    void (*note_key)(struct Porcelain* porcelain, int key, bool down);
    /** Requested on first use, released after several unused describe runs.
     *  Terminal states are remembered and are one finding. */
    struct ToriRS_ImageRef (*image)(struct Porcelain* porcelain, char const* name,
                                    enum PorcelainAssetState* out_state);
    /** The picture's own size, so an item can ask for it instead of reaching
     *  past the layer to api->assets for the handle the layer just gave it.
     *  False while the asset is not READY. */
    bool (*image_size)(struct Porcelain* porcelain, char const* name, int* out_width,
                       int* out_height);
    struct ToriRS_ModelRef (*model)(struct Porcelain* porcelain, char const* name,
                                    enum PorcelainAssetState* out_state);
    /** Painted at most once per (key, hash of inputs). Keys never include a
     *  host revision: an icon_revision that bumps on every miss never settles. */
    struct ToriRS_ImageRef (*derived)(struct Porcelain* porcelain, char const* key,
                                      void const* inputs, size_t inputs_len, int w, int h,
                                      PorcelainPaintFn paint, void* user,
                                      enum PorcelainDerivedState* out_state);
    /** Fires once when every named bit holds, and again after a re-login. */
    void (*when_ready)(struct Porcelain* porcelain, unsigned what, PorcelainReadyFn fn,
                       void* user);
    void (*every)(struct Porcelain* porcelain, enum PorcelainCadence cadence, PorcelainTickFn fn,
                  void* user);
    /** == every(PORCELAIN_SERVER_TICK, ...). The server tick fires on every
     *  lane; there is no synthesised cadence. */
    void (*every_server_tick)(struct Porcelain* porcelain, PorcelainTickFn fn, void* user);
    /** Re-registering the same (fn, user) RE-INTERVALS in place; it does not
     *  append. A user dragging a refresh slider leaked a timer slot per
     *  change and then hit the budget. */
    void (*every_ms)(struct Porcelain* porcelain, int ms, PorcelainTickFn fn, void* user);
    /** Drop the timer registered for this (fn, user). */
    void (*cancel_every)(struct Porcelain* porcelain, PorcelainTickFn fn, void* user);
    /** The plugin forwarding its own tick callback. FRAME fires from `fence`
     *  and needs no forwarding. */
    void (*tick)(struct Porcelain* porcelain, enum PorcelainCadence cadence);

    /* ------------------------------------------------------- the overlays */
    /** The drawable rect of the pass now running, plus one element's box in
     *  the pass's own coordinates. False when the pass set no region. */
    bool (*draw_context)(struct Porcelain* porcelain, struct ToriRS_Graphics* draw,
                         struct PorcelainElement element, struct PorcelainDrawContext* out);
    /** menu.add, with the refusal turned into a finding. The bool is still
     *  returned, because a caller that must stop adding rows still may. */
    bool (*menu_add)(struct Porcelain* porcelain, struct ToriRS_MenuBuildEvent* menu,
                     char const* text, uint32_t action_id);
    /** From on_menu_build. Stamps the hovered cell on the hover pass, and
     *  arms the menu budget. A right-click pass is not a hover. */
    void (*note_menu)(struct Porcelain* porcelain, struct ToriRS_MenuBuildEvent const* menu);
    /** The hovered cell, if the last menu build was this frame or the last.
     *  The one-frame liveness window, written once. */
    bool (*hover)(struct Porcelain* porcelain, struct PorcelainHover* out);
    /** The suppress-then-format latch over a lane's own caption script. */
    void (*native_overlay)(struct Porcelain* porcelain, char const* labels_role,
                           char const* callback, PorcelainScriptFn fn, void* user);
    /** From on_script_callback. Drives the latch and routes to the plugin. */
    void (*note_script)(struct Porcelain* porcelain, struct ToriRS_ScriptEvent const* event);
    /** Read, parse and release a shipped data file, once. One finding when it
     *  is absent, errored, or the parse refuses it. */
    bool (*table)(struct Porcelain* porcelain, char const* asset, PorcelainParseFn parse,
                  void* user);
    /** One announcement per (kind, subject) per frame: a stack of twelve
     *  bones spawning is one line, not twelve. */
    void (*notify)(struct Porcelain* porcelain, char const* kind, int subject, char const* text);
    /* --------------------------------------------------------------- panels */
    /** on_start ONLY: register the shared pane and say which faces this
     *  plugin's description covers. A refused registration is a finding. */
    void (*panel)(struct Porcelain* porcelain, char const* icon_asset, int width,
                  unsigned faces);
    /** The plugin forwarding on_ui_build. Emits the described rows for this
     *  face, or nothing at all where the face is not one it covers. */
    void (*panel_build)(struct Porcelain* porcelain, struct ToriRS_PanelBuilder* builder,
                        int view);
    /** The plugin forwarding on_ui_action. True when a described row took it. */
    bool (*panel_action)(struct Porcelain* porcelain,
                         struct ToriRS_PanelActionEvent const* event);
    /** The plugin forwarding on_ui_draw. True when a described CUSTOM row
     *  painted it. */
    bool (*panel_draw)(struct Porcelain* porcelain, char const* node,
                       struct ToriRS_Graphics* draw);

    /* ------------------------------------- the refusals and the partners */
    /** draw->world_hull with both of its refusals made loud. The engine's
     *  world_hull was declared to answer a result and answered OK
     *  unconditionally; what that cost was half the outlines in a mass of
     *  tagged npcs, gone, with the plugin still reporting itself armed.
     *  False means nothing was drawn and a finding says which refusal. */
    bool (*hull)(struct Porcelain* porcelain, struct ToriRS_Graphics* draw, int element_id,
                 uint32_t rgb, int alpha, int shape);
    /** draw->world_tile with its budget refusal made loud. A tile marker is
     *  drawn per tile of a footprint, so the 512-item frame allotment is
     *  reached by multiplication and not by accident; the host has logged
     *  that for a while and nothing could read it. */
    bool (*tile)(struct Porcelain* porcelain, struct ToriRS_Graphics* draw, int tile_x,
                 int tile_z, int level, uint32_t fill_rgb, uint32_t outline_rgb, int alpha);
    /** A plugin's OWN finding, coalesced on (verb, element, result) like
     *  every other. `result` is a PorcelainFindingResult and may not be OK. */
    void (*finding)(struct Porcelain* porcelain, char const* verb,
                    struct PorcelainElement element, int result, char const* detail);
    /** The inverse of menu_tag. Without it every consumer spells the 16 by
     *  hand, and raising it would silently re-target every retained row. */
    void (*menu_untag)(uint32_t tag, int* out_subject, int* out_op);
    /** Is this key held NOW, asked where the question is asked. `key` is the
     *  edge form's VALUE vocabulary -- a name, a decimal code, or a single
     *  character -- not a config key. */
    bool (*key_down)(struct Porcelain* porcelain, char const* key);
    /** Take one item out of a stored list. Absent is true and costs no
     *  write. */
    bool (*config_list_remove)(struct Porcelain* porcelain, char const* key, char const* item);
    /** State the whole list: sorted, deduplicated, refused rather than
     *  truncated. */
    bool (*config_list_set)(struct Porcelain* porcelain, char const* key,
                            char const* const* items, int count);
    /* ---- Frames ---------------------------------------------------------
     *
     * A frame provider publishes its offers in ToriRS_PluginDef.frames, as it
     * always did -- the host resolves auto/native before the provider starts,
     * so registration order cannot change which offer is asked for. What
     * these two add is the half the plugins wrote by hand: one describe
     * function per offer, and a reconcile that owns the release.
     */

    /** Bind `offer_id` -- an id this plugin's definition publishes -- to the
     *  description of that frame. Boot only; a second call for the same id
     *  replaces the binding. `canvas`, `min_width` and `min_height` are what
     *  the offer said, restated here so a description can be checked against
     *  the canvas it was written for. */
    void (*frame)(struct Porcelain* porcelain, char const* offer_id, int canvas,
                  int min_width, int min_height, PorcelainDescribeFn fn, void* user);

    /** The plugin forwarding its own on_gameframe. Returns a
     *  ToriRS_FrameBuildResult and writes event->reason.
     *
     *  Not installed by the layer: Porcelain is a library reached from the
     *  plugin's own callbacks and ToriRS_PluginDef is const, so the event is
     *  handed over rather than intercepted. A RELEASE (event->active false)
     *  runs a describe that stages nothing, which is what takes the frame's
     *  moves, hides, skins and owned art back off -- the shipped providers
     *  both leave a dressing behind on a provider switch because their
     *  undress runs from on_frame_start and they never get another one. */
    int (*frame_event)(struct Porcelain* porcelain,
                       struct ToriRS_GameframeEvent const* event);

    /** The canvas a frame may lay out in: the frame root's box, less a
     *  LANE_CHROME strip that is PRESENTED and spans a full edge. False
     *  before anything of this lane has bound. */
    bool (*usable)(struct Porcelain* porcelain, struct ToriRS_WidgetBounds* out);

    /** The box the LANE authored for `element`, before any plugin edit.
     *
     *  By ELEMENT and not by a surface number: the plugin-private enum passed
     *  straight into frame.surface_native_size as the API's TORIRS_SURFACE_*
     *  is a live defect, and the two numberings differ. A member element
     *  answers block-relative x and y; a whole surface answers 0, 0. */
    bool (*native_size)(struct Porcelain* porcelain, struct PorcelainElement element,
                        struct ToriRS_WidgetBounds* out);

    /** The number THIS lane's own icon set gives `tab`, or -1 where the lane
     *  numbers no panel there. The files are the frame's own art; what the
     *  lanes disagree about is the numbering. */
    int (*lane_icon)(struct Porcelain* porcelain, struct PorcelainElement tab);

    /** How many rows (or columns) of tab stones this root lays out. */
    int (*tab_group_count)(struct Porcelain* porcelain, int axis);
    /** The tabs of one group, in the root's own order. Returns how many were
     *  written; a capacity smaller than the group is a budget finding. */
    int (*tab_group)(struct Porcelain* porcelain, int axis, int group,
                     struct PorcelainElement* out, int capacity);
    /** The tab this root hangs outside every group (164's and 601's logout),
     *  or an element of kind PORCELAIN_EL_NONE where there is none. */
    struct PorcelainElement (*tab_detached)(struct Porcelain* porcelain);

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
    /* The Porcelain layer, or NULL in a host that did not install it. A
     * pointer and not an embedded namespace for the same reason `client` and
     * `game` are: the table is a static const belonging to a LIBRARY, not to
     * the runtime, so the host only has to know where it is. */
    struct ToriRS_PorcelainApi const* porcelain;
    uintptr_t reserved_v2[TORIRS_DESCRIPTOR_V2_RESERVED_WORDS - 3];
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
