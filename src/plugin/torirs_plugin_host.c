#include "plugin/torirs_plugin_host.h"

#include "log/torirs_log.h"
#include "plugin/torirs_plugin_frame.h"
#include "plugin/torirs_plugin_ui.h"
#include "plugin/torirs_plugin_runtime.h"
#include "revconfig/revconfig.h"
#include "ui/uitree_minimenu.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Plugin action ids start well clear of the four the editors already own
 * (RS_MINIMENU_ACTION_* are CLIENT_BASE + 0..3). Being at or above
 * CLIENT_BASE is what exempts an id from SortPriorityActions' +/-2000 bias;
 * being above 1000 is what keeps a plugin row out of the left-click default
 * (RS_Minimenu_DefaultOptionIndex takes the first action < 1000). Both matter:
 * a plugin must be able to add a row without silently stealing left-click.
 */
#define PLUGIN_MENU_ACTION_BASE (UITREE_MINIMENU_ACTION_CLIENT_BASE + 16)
#define PLUGIN_AREA_COUNT (TORIRS_AREA_RAW_VIEWPORT + 1)
#define PLUGIN_EDGE_COUNT (TORIRS_EDGE_LEFT + 1)

/* Host-internal callback phases. These are dispatch implementation details,
 * not a subscribable ABI: each V2 callback has its own typed table slot. */
enum PluginCallbackKind
{
    PLUGIN_CALLBACK_START = 0,
    PLUGIN_CALLBACK_STOP,
    PLUGIN_CALLBACK_FRAME_START,
    PLUGIN_CALLBACK_LOGIC_TICK,
    PLUGIN_CALLBACK_SERVER_TICK,
    PLUGIN_CALLBACK_WORLD_LOADED,
    PLUGIN_CALLBACK_NPC_SPAWN,
    PLUGIN_CALLBACK_NPC_RETYPE,
    PLUGIN_CALLBACK_NPC_DESPAWN,
    PLUGIN_CALLBACK_KEY,
    PLUGIN_CALLBACK_MENU_BUILD,
    PLUGIN_CALLBACK_MENU_SELECT,
    PLUGIN_CALLBACK_DRAW_WORLD,
    PLUGIN_CALLBACK_DRAW_CANVAS,
    PLUGIN_CALLBACK_CANVAS_CLICK,
    PLUGIN_CALLBACK_CONFIG_CHANGED,
    PLUGIN_CALLBACK_OBJ_SPAWN,
    PLUGIN_CALLBACK_OBJ_COUNT,
    PLUGIN_CALLBACK_OBJ_DESPAWN,
    PLUGIN_CALLBACK_ASSET,
    PLUGIN_CALLBACK_CHAT_MESSAGE,
    PLUGIN_CALLBACK_GAME_EVENT,
    PLUGIN_CALLBACK_UI,
    PLUGIN_CALLBACK_UI_BUILD,
    PLUGIN_CALLBACK_LAYOUT,
    PLUGIN_CALLBACK_PLACEMENT_CHANGED,
    PLUGIN_CALLBACK_DRAW_FRAME,
    PLUGIN_CALLBACK_SCREEN_CHANGE,
    PLUGIN_CALLBACK_PANEL_BUILD,
    PLUGIN_CALLBACK_PANEL_ACTION,
    PLUGIN_CALLBACK_PANEL_LAYOUT,
    PLUGIN_CALLBACK_PANEL_DRAW,
    PLUGIN_CALLBACK_COUNT
};

struct PluginCanvasDispatch
{
    void* surface;
    struct ToriRS_Rect bounds;
};

struct PluginFrameLayout
{
    int width;
    int height;
    int canvas;
};

struct PluginPanelDraw
{
    void* surface;
    char const* id;
    int x;
    int y;
    int width;
    int height;
};

static void
plugin_panel_bump(uint32_t* revision);
static void
plugin_teardown(
    struct ToriRS_PluginHost* host,
    int plugin_index);
static void
plugin_frame_resolve(struct ToriRS_PluginHost* host);
static void
plugin_layout_publish(struct ToriRS_PluginHost* host);
static void
plugin_ui_refresh_base(struct ToriRS_PluginHost* host);
static void
plugin_layout_notifications_run(struct ToriRS_PluginHost* host);
struct PluginUiPresentation;
static void
plugin_ui_present_suppressions(
    struct ToriRS_PluginHost* host,
    struct PluginUiPresentation const* rows,
    int count,
    bool enabled);
static void
plugin_ui_present_reconcile(struct ToriRS_PluginHost* host);
static int
plugin_ui_present_provider(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    char const* provider,
    uint32_t facet);

struct PluginConfigSlot
{
    char key[64];
    char value[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    /* Index into the plugin's declared schema, or -1 for a value read from
     * the ini that no schema claims. Those are kept rather than dropped: the
     * plugin that owns them may not have finished loading, and rewriting the
     * file must not delete a section we merely did not understand yet. */
    int schema_index;
};

#define PLUGIN_UI_CONTRIBUTIONS_MAX 32
#define PLUGIN_UI_PRESENT_TAG_BIT 0x80000000u
#define PLUGIN_UI_PRESENTATIONS_MAX TORIRS_UI_REGISTRY_NODES_MAX

struct PluginV2Instance;
static void plugin_v2_init(struct PluginContext* ctx);
static void plugin_v2_shutdown(struct PluginContext* ctx);
static int plugin_v2_has_event_callback(
    struct ToriRS_PluginDefV2 const* def,
    enum PluginCallbackKind event);
static enum ToriRS_CallbackResult plugin_v2_event(
    struct PluginContext* ctx,
    void* event,
    void* userdata);

struct PluginContext
{
    struct ToriRS_PluginHost* host;
    struct ToriRS_PluginDefV2 const* def;
    int index;
    /* The USER's switch, as the settings file holds it. Never written by
     * anything the plugin itself does -- see `refused`. */
    bool enabled;
    bool running;
    /** Guards teardown re-entry when a visibility/stop callback disables its
     *  own plugin. */
    bool tearing_down;
    /*
     * The plugin looked at the lane and stood down. @see
     * disable_self.
     *
     * A second flag rather than a cleared `enabled`, because the two are
     * different facts with different lifetimes: `enabled` is a preference that
     * outlives the boot and is written to disk, and this is one boot's answer
     * to "can this plugin work here at all". Folding them together would make
     * booting an OldSchool world silently forget the gameframe layout chosen
     * on a 2004 world, since the encoder writes whatever `enabled` holds.
     *
     * Cleared by an explicit enable and by a reload -- both are the user
     * asking for the decision to be taken again -- and by nothing else, so a
     * refusal holds for the rest of the boot without the plugin having to
     * restate it.
     */
    bool refused;
    /* Overlay items pushed this frame, against TORIRS_PLUGIN_DRAW_BUDGET. */
    int draw_used;
    bool draw_clipped;
    char name[TORIRS_PLUGIN_NAME_MAX];
    /* What the panel shows. Derived once per (re)load rather than at every
     * draw, and held here rather than read through the def, because a def may
     * carry no title at all and the derived one needs somewhere to live that
     * outlives the call that built it. */
    char title[TORIRS_PLUGIN_TITLE_MAX];
    char error[160];
    struct PluginConfigSlot config[TORIRS_PLUGIN_CONFIG_MAX];
    int config_count;
    int schema_count;
    /* Engine object handles this plugin holds. Tracked here and not only in
     * the engine because a stopped plugin must take its geometry out of the
     * world with it -- an abandoned beam would burn until the client exited,
     * with nothing left that knows to remove it. */
    int objects[TORIRS_PLUGIN_OBJECT_BUDGET];
    int object_count;
    bool object_clipped;
    /* Authored mesh handles, tracked for the same reason and released after
     * the objects: an object is built FROM a mesh, so a mesh that went first
     * would be pulled out from under geometry still standing in the world. */
    int meshes[TORIRS_PLUGIN_MESH_BUDGET];
    int mesh_count;
    bool mesh_clipped;
    bool ui_contributions_registered;
    struct ToriRS_UiContributionRef ui_contribution_refs[PLUGIN_UI_CONTRIBUTIONS_MAX];
    int ui_contribution_count;
    struct PluginV2Instance* v2;
    void (*reload_handler)(
        struct ToriRS_PluginHost* host,
        int plugin_index,
        void* user);
    void* reload_user;
};

#define PLUGIN_V2_FRAME_UI_MAX TORIRS_PLUGIN_V2_FRAME_NAMED_NODES_MAX

_Static_assert(
    TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX >= TORIRS_PLUGIN_IMAGES_MAX,
    "v2 image tokens cover the host image table");
_Static_assert(
    TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX >= TORIRS_PLUGIN_MODELS_MAX,
    "v2 model tokens cover the host model table");
_Static_assert(
    TORIRS_PLUGIN_V2_MESH_TOKENS_MAX >= TORIRS_PLUGIN_MESH_BUDGET,
    "v2 mesh tokens cover the per-plugin mesh budget");
_Static_assert(
    TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX >= TORIRS_PLUGIN_OBJECT_BUDGET,
    "v2 instance tokens cover the per-plugin object budget");
_Static_assert(TORIRS_PLUGIN_MAX <= 32, "v2 resource namespace covers every plugin index");

struct PluginV2Instance
{
    struct PluginV2FrameNode
    {
        char name[TORIRS_UI_NAME_MAX];
        struct ToriRS_UiNode value;
        char parent[TORIRS_UI_NAME_MAX];
        char label[TORIRS_UI_LABEL_MAX];
        char action[TORIRS_UI_ACTION_MAX];
        char actions[TORIRS_UI_NAMED_ACTIONS_MAX][TORIRS_UI_ACTION_MAX];
    } frame_ui[PLUGIN_V2_FRAME_UI_MAX], frame_ui_candidate[PLUGIN_V2_FRAME_UI_MAX];
    struct ToriRS_PluginDefV2 const* definition;
    struct ToriRS_PluginDefV2 definition_storage;
    struct ToriRS_PluginCallbacks callbacks_storage;
    struct ToriRS_ConfigSchema config_storage;
    struct ToriRS_FrameOffer frame_offers[TORIRS_PLUGIN_FRAME_OFFERS_MAX + 1];
    int frame_count;
    struct PluginV2Runtime runtime;
    bool runtime_initialized_once;
    void* state;
    int frame_ui_count;
    int frame_ui_candidate_count;
    bool frame_ui_candidate_invalid;
    struct ToriRS_ImageRef frame_images[TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX];
    struct ToriRS_ImageRef frame_images_candidate[TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX];
    int frame_image_count;
    int frame_image_candidate_count;
};

struct PluginMenuRoute
{
    int action;
    int plugin;
    uint32_t tag;
};

struct PluginUiPresentation
{
    struct ToriRS_UiNodeRef node;
    struct ToriRS_UiStoredNode value;
    int appearance_plugin;
    int actions_plugin;
    uint32_t action_token;
    uint64_t state_identity;
    int boundary_place;
    bool presentable;
    bool clip_active;
    struct ToriRS_Rect clip;
    char boundary_role[TORIRS_PLUGIN_ROLE_NAME_MAX];
    /** A closer mapped role which is not live yet. Its transition to live is
     * probed from this compact list and triggers one presenter rebuild. */
    char pending_boundary_role[TORIRS_PLUGIN_ROLE_NAME_MAX];
    /** Non-empty only when this semantic node itself resolves to a live lane
     * role. Facet suppression applies here, never to a fallback ancestor. */
    char target_role[TORIRS_PLUGIN_ROLE_NAME_MAX];
};

/*
 * One resident asset.
 *
 * Keyed on (plugin, name) rather than name alone: the namespaces are per
 * plugin on disk, and collapsing them here would let two plugins that both
 * ship a `prices.txt` read each other's.
 */
struct PluginAsset
{
    int plugin;
    char name[TORIRS_PLUGIN_ASSET_NAME_MAX];
    void* data;
    int size;
    /* A read is in flight. The slot is claimed before the IO starts so a
     * second asset_load of the same name joins the first rather than queuing
     * a duplicate read. */
    bool pending;
    /** A completed byte value, including a legitimate zero-byte asset. */
    bool ready;
    /*
     * The read completed and the file was not there.
     *
     * Without this the slot records that a read HAPPENED but not what it
     * found: data stays NULL and pending goes false, which is exactly what an
     * untouched slot looks like, so the next asset_load starts the read again.
     * A plugin that asks from its draw path -- item-stats asks for text.ini
     * and bonuses.txt -- then pays a task, two IO round trips and a log line
     * every tick, forever, for a file that is not going to appear.
     *
     * Cleared by api_asset_save, which writes the very file the load wanted,
     * so this is a cached answer and not a permanent refusal.
     */
    bool missing;
};

/* Standing entity appearance/input ownership. */
#define PLUGIN_ENTITY_CLAIMS_MAX 64
#define PLUGIN_ENTITY_OP_MAX 32
enum PluginEntityScope
{
    PLUGIN_ENTITY_APPEARANCE = 1 << 0,
    PLUGIN_ENTITY_HITBOX = 1 << 1
};
/** Whole surfaces plus every V2 member declaration in one candidate. */
#define TORIRS_PLUGIN_LAYOUT_RECTS_MAX 96
/** A 64x64 PNG has no reason to approach this; cap pathological compressed
 * input before the automatic rail-icon decode spends work on it. */
#define TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX (256 * 1024)

/** One entity a plugin has taken responsibility for presenting or acting on. */
struct PluginEntityClaim
{
    int plugin;
    char part[TORIRS_PLUGIN_ROLE_NAME_MAX];
    int scopes;
    int element_id;
    struct ToriRS_EntityAppearance look;
    uint8_t ops_mode;
    char ops[TORIRS_PLUGIN_REGION_OPS_MAX][PLUGIN_ENTITY_OP_MAX];
    int op_count;
    uint32_t tag;
    uint8_t has_ops;
};

/**
 * Scratch frame declaration. Nothing in this record is visible to the engine
 * until every geometry, image and named-node invariant has passed.
 */
struct PluginLayoutCandidate
{
    struct PluginLayoutRect
    {
        int slot;
        int member;
        int x;
        int y;
        int w;
        int h;
    } rects[TORIRS_PLUGIN_LAYOUT_RECTS_MAX];
    int rect_count;
    struct PluginLayoutSkin
    {
        bool declared;
        int art;
        int mask;
    } skins[TORIRS_HOST_SURFACE_PLACEABLE_COUNT];
    struct PluginLayoutOverlay
    {
        bool declared;
        int image;
        int x;
        int y;
        int trans;
    } overlays[TORIRS_HOST_SURFACE_PLACEABLE_COUNT];
    bool scrollbar_declared;
    int scrollbar[6];
    int scrollbar_count;
    bool viewport_declared;
    bool invalid;
    char reason[TORIRS_FRAME_REASON_MAX];
};

/**
 * Which of the three draw surfaces is open.
 *
 * The values are the engine's `draw_select_canvas` argument, so the order is
 * load-bearing: it names the list a draw verb appends to, and app.c switches
 * on the same numbers.
 */
enum PluginDrawSurface
{
    PLUGIN_DRAW_SURFACE_WORLD = 0,
    PLUGIN_DRAW_SURFACE_CANVAS = 1,
    /** Over the scene, under the interfaces. @see FrameOffer.draw. */
    PLUGIN_DRAW_SURFACE_FRAME = 2,
    /** Panel-local custom region prepared by the application shell. */
    PLUGIN_DRAW_SURFACE_PANEL = TORIRS_PLUGIN_ENGINE_DRAW_PANEL
};

struct ToriRS_PluginHost
{
    struct ToriRS_PluginEngine engine;

    struct PluginContext plugins[TORIRS_PLUGIN_MAX];
    int plugin_count;

    /** Stable callback orders computed once at registration. Event callbacks
     * run high-priority first; drawing runs low-z first. */
    int event_order[TORIRS_PLUGIN_MAX];
    int draw_order[TORIRS_PLUGIN_MAX];
    int callback_count[PLUGIN_CALLBACK_COUNT];

    /* Menu routes live for one build. The hover pass rebuilds the menu every
     * frame, so they are reset per build rather than accumulated. */
    struct PluginMenuRoute routes[TORIRS_PLUGIN_MENU_ROUTES_MAX];
    int route_count;
    int next_action;

    /* The plugin currently being dispatched, so the api knows whose ctx it is
     * serving without every call carrying it separately. */
    int dispatching;
    /** enum PluginCallbackKind for that callback, or -1 outside one. Needed by
     *  panel_request's on_start-only registration rule. */
    int dispatch_event;
    /** engine.screen's answer at the last frame boundary, so the boundary can
     *  tell a change from a steady state. @see PLUGIN_CALLBACK_SCREEN_CHANGE. */
    int last_screen;
    /* Non-NULL only between the open and close of a draw window. */
    void* draw_surface;
    /* Which surface that is -- enum PluginDrawSurface. Read by the two
     * world-only draw verbs, which have nothing to mean on screen/panel. */
    int draw_canvas;

    /* Static frame offers and the committed engine state for the active
     * catalogue entry. */
    struct PluginFrameCatalog frame_catalog;
    struct ToriRS_FrameSelection frame_selection;
    /** Last completely validated and engine-committed offer. */
    int frame_active_entry;
    /** Offer named by the current request, still only a candidate. */
    int frame_target_entry;
    /** One-shot request consumed by the app's next safe layout fence. */
    int frame_layout_requested;
    int frame_preference_loaded;
    int frame_resolving;
    int frame_selection_dirty;

    struct ToriRS_PlacementRegion placement[PLUGIN_AREA_COUNT];
    /** Layout epoch from which `placement` was last resolved. */
    int placement_cache_revision;
    int placement_cache_valid;
    /**
     * Public retained-placement state.  Unlike layout_revision, this advances
     * only when the canonical area sets or a named reservation's assigned box
     * actually changes.
     */
    uint32_t placement_revision;
    uint32_t placement_notified_revision;
    int placement_initialized;
    int placement_notify_pending;
    struct ToriRS_UiRegistry ui_registry;
    /* Rebuilt only when the named registry revision changes. Per-frame paint
     * walks this compact winning-provider list, never the registry. */
    struct PluginUiPresentation ui_presentations[PLUGIN_UI_PRESENTATIONS_MAX];
    int ui_presentation_count;
    /** Interned node -> compact presentation index + 1 (zero absent). */
    int ui_presentation_by_node[TORIRS_UI_REGISTRY_NODES_MAX];
    uint32_t ui_presentation_revision;
    uint32_t ui_presentation_rebuilds;
    uint32_t ui_presentation_change_visits;
    uint32_t ui_presentation_registry_visits;
    uint32_t ui_presentation_role_probe_visits;
    int ui_pending_role_nodes[TORIRS_UI_REGISTRY_NODES_MAX];
    int ui_pending_role_index[TORIRS_UI_REGISTRY_NODES_MAX];
    int ui_pending_role_count;
    /** 0 unknown, 1 absent, 2 non-blocking, 3 hidden. */
    uint8_t ui_node_visibility_state[TORIRS_UI_REGISTRY_NODES_MAX];
    bool ui_presentation_roles_dirty;
    /** Diagnostic copy of consumed registry mutations; never drives state. */
    uint32_t ui_observed_change_facets[TORIRS_UI_REGISTRY_NODES_MAX];
    uint32_t ui_observed_change_revision[TORIRS_UI_REGISTRY_NODES_MAX];
    int ui_observed_change_queue[TORIRS_UI_REGISTRY_NODES_MAX];
    int ui_observed_change_head;
    int ui_observed_change_count;
    uint32_t ui_action_token;
    int ui_tab_active;
    uint32_t ui_tab_enabled_mask;
    bool ui_tab_state_valid;

    /** enum ToriRS_FrameCanvas, and the pinned size for FIXED. */
    int layout_canvas;
    int layout_fixed_w;
    int layout_fixed_h;
    /** Advances across every resolved offer transition. Fences an in-flight
     * frame-build candidate from committing after selection moved. */
    uint32_t frame_selection_epoch;
    /** Non-zero only inside a frame-build callback: surface declarations are
     * legal then and at no other time, for the same reason hit regions are. */
    int layout_declaring;
    int layout_declarer;
    int layout_candidate_entry;
    struct PluginLayoutCandidate layout_candidate;

    struct PluginPlacementReservation
    {
        int plugin;
        int area;
        int edge;
        int pixels;
        int assigned;
        char name[TORIRS_HOST_RESERVATION_NAME_MAX];
        struct ToriRS_PlacementRect rect;
    } placement_reservations[TORIRS_PLUGIN_RESERVES_MAX];

    /** Last successfully resolved named-reservation assignments. */
    struct PluginPlacementResolvedReservation
    {
        int plugin;
        int assigned;
        char name[TORIRS_HOST_RESERVATION_NAME_MAX];
        struct ToriRS_PlacementRect rect;
    } placement_resolved_reservations[TORIRS_PLUGIN_RESERVES_MAX];

    /* Standing entity presentation/action claims. */
    struct PluginEntityClaim entity_claims[PLUGIN_ENTITY_CLAIMS_MAX];

    /** Moves whenever anything about the layout does. @see layout_revision. */
    int layout_revision;
    /** Set while on_placement_changed is being delivered. @see
     *  PluginHost_LayoutChanged. */
    int layout_notifying;
    /** A layout source changed from inside a notification.  It is folded into
     * the current transaction when possible, otherwise drained next frame. */
    int layout_notify_pending;
    /* Non-NULL only during an on_menu_build dispatch. */
    void* menu_cursor;

    struct PluginAsset assets[TORIRS_PLUGIN_ASSETS_MAX];
    int asset_count;

    /*
     * Resident images: one decoded sprite in the scene per slot.
     *
     * A table of its own rather than a flag on the asset above, because the
     * two have different lifetimes and different owners. The asset is BYTES,
     * which the host frees when the plugin stops; an image is a scene entry,
     * which the engine owns and has to be told to drop. A plugin can also
     * legitimately hold an asset it never draws (a price table), and an image
     * of a file it has since re-saved.
     *
     * The slot index IS the handle a plugin holds, and it is stable for the
     * life of the image: nothing compacts this table, because a compaction
     * would silently renumber every handle already handed out.
     */
    struct PluginImage
    {
        /** Owning plugin, or -1 for a free slot. */
        int plugin;
        char asset[TORIRS_PLUGIN_ASSET_NAME_MAX];
        int width;
        int height;
        /** The engine has a scene entry for this slot. */
        bool published;
    } images[TORIRS_PLUGIN_IMAGES_MAX];

    /*
     * Resident shipped models, the same shape as the image table above and for
     * the same reasons: keyed on (plugin, file) so a second load is the same
     * handle, and reclaimed with the plugin so a stopped one leaves no
     * geometry behind.
     */
    struct PluginModel
    {
        /** Owning plugin, or -1 for a free slot. */
        int plugin;
        char asset[TORIRS_PLUGIN_ASSET_NAME_MAX];
        /** The engine holds decoded geometry for this slot. */
        bool published;
    } models[TORIRS_PLUGIN_MODELS_MAX];

    /*
     * The item-icon cache: rasterised inventory icons, shared across plugins,
     * least-recently-asked-for evicted.
     *
     * A CACHE and not an ownership table, which is the difference from
     * `images` above and the whole reason it is separate. An image handle a
     * plugin made is a thing that plugin owns until it releases it; an icon is
     * a picture of a game item that any number of plugins may want and none of
     * them authored, so what the host owes is the picture and not the slot. A
     * plugin never releases one and never should: it asks again.
     *
     * Keyed on the whole triple because all three change the pixels -- the
     * stack digits are baked in, and the border variants are separate renders.
     * Linear, because it is 48 entries scanned by a page build and not by a
     * per-pixel loop, and because a hash over three ints that has to handle
     * eviction is more machinery than the scan costs.
     */
    struct PluginObjIcon
    {
        /** -1 for a free entry. */
        int obj_id;
        int count;
        /** enum ToriRS_ItemIconStyle. */
        int style;
        /** Which plugin's image slot holds the pixels. An icon is per-plugin
         *  in the image table even though the picture is not, because
         *  draw_image resolves a slot against the plugin that owns it. */
        int plugin;
        /** The `images` slot, which is also the handle handed out. */
        int image;
        /** Changes whenever an evicted image slot receives a new icon key. */
        uint64_t revision;
        /**
         * The value of `icon_clock` when this was last asked for. A COUNTER
         * and not frame_ms: two icons fetched in the same millisecond by one
         * page build must still order against each other, or eviction picks
         * between them arbitrarily and can drop the one being drawn.
         */
        uint64_t used;
    } obj_icons[TORIRS_PLUGIN_OBJ_ICONS_MAX];
    uint64_t icon_clock;
    uint64_t icon_revision;

    /*
     * Application plugin panel: many inert rail registrations, ONE mounted
     * page model. Presentation and application-window placement deliberately
     * live above this host; these fields are only the authority/gating layer.
     */
    bool panel_registered[TORIRS_PLUGIN_MAX];
    char panel_title[TORIRS_PLUGIN_MAX][TORIRS_PLUGIN_TITLE_MAX];
    char panel_icon[TORIRS_PLUGIN_MAX][TORIRS_PLUGIN_ASSET_NAME_MAX];
    /** Sandboxed image slot automatically loaded for panel_icon, or -1. */
    int panel_icon_image[TORIRS_PLUGIN_MAX];
    uint32_t panel_icon_revision[TORIRS_PLUGIN_MAX];
    int panel_preferred_width[TORIRS_PLUGIN_MAX];
    bool panel_attention[TORIRS_PLUGIN_MAX];
    int panel_active;
    /**
     * Which FACE the active selection is showing.
     * @see enum ToriRS_PanelView.
     *
     * Part of the SELECTION and not of the plugin, which is why it lives
     * beside panel_active rather than in a per-plugin array: one plugin's page
     * and its settings are two mountings of the same registration, and moving
     * between them tears the model down exactly as moving between two plugins
     * does. A per-plugin "current face" would be a second thing to keep in
     * step with a generation that already says everything.
     */
    int panel_view;
    /** Retained through collapse; cleared only when that entry is removed. */
    int panel_last_selected;
    uint32_t panel_selection_generation;
    uint32_t panel_registry_revision;
    uint32_t panel_model_revision;
    uint64_t panel_last_intent_sequence;

    /* The active page alone owns records. Switching pages empties this array
     * before the new plugin is called, so hidden plugins retain no native/DOM
     * model and cannot consume the shared widget budget. */
    struct ToriRS_PanelWidget panel_widgets[TORIRS_PLUGIN_WIDGETS_MAX];
    struct ToriRS_PluginSelectOption
        panel_select_options[TORIRS_PLUGIN_SELECT_OPTIONS_MAX];
    int panel_select_option_count;
    bool panel_invalidated[TORIRS_PLUGIN_WIDGETS_MAX];
    int panel_widget_count;
    /** One queued entry per current widget slot; flags coalesce in O(1). */
    uint32_t panel_change_flags[TORIRS_PLUGIN_WIDGETS_MAX];
    int panel_change_queue[TORIRS_PLUGIN_WIDGETS_MAX];
    int panel_change_head;
    int panel_change_count;
    bool panel_change_rebuild;
    uint32_t panel_change_visits;
    uint32_t next_widget_serial;
    bool panel_building;
    bool panel_needs_build;
    int panel_transitioning;

    /* Last neutral allocation, for hide notification and custom draw size. */
    bool panel_has_layout;
    int panel_width;
    int panel_height;
    int panel_scale_milli;
    int panel_size_class;
    bool panel_visible;
    bool panel_game_visible;

    bool config_dirty;
};

static int
plugin_frame_owner(struct ToriRS_PluginHost const* host)
{
    struct PluginFrameCatalogEntry const* entry;

    assert(host);
    entry = PluginFrameCatalog_At(&host->frame_catalog, host->frame_active_entry);
    return entry ? entry->plugin : -1;
}

/* ---------------------------------------------------------------- helpers */

static struct PluginContext*
plugin_at(
    struct ToriRS_PluginHost* host,
    int index)
{
    assert(host);
    assert(index >= 0);
    assert(index < host->plugin_count);
    return &host->plugins[index];
}

static struct ToriRS_ConfigItem const*
plugin_schema(struct PluginContext const* ctx)
{
    assert(ctx);
    return ctx->def && ctx->def->config ? ctx->def->config->items : NULL;
}

static bool
plugin_policy(
    struct PluginContext const* ctx,
    uint32_t flag)
{
    assert(ctx);
    assert(ctx->def);
    return (ctx->def->flags & flag) != 0;
}

static bool
plugin_provides_frames(struct PluginContext const* ctx)
{
    assert(ctx);
    return ctx->v2 && ctx->v2->frame_count > 0;
}

static int
plugin_schema_index(
    struct PluginContext const* ctx,
    char const* key)
{
    assert(ctx);
    assert(key);

    struct ToriRS_ConfigItem const* schema = plugin_schema(ctx);
    if( !schema )
        return -1;
    for( int i = 0; schema[i].key; i++ )
    {
        if( strcmp(schema[i].key, key) == 0 )
            return i;
    }
    return -1;
}

/** Config keys are written verbatim to an INI line, so the documented key
 *  alphabet is also the persistence boundary. */
static bool
plugin_config_key_valid(char const* key)
{
    size_t length;

    if( !key )
        return false;
    length = strlen(key);
    if( length == 0 || length >= sizeof(((struct PluginConfigSlot*)0)->key) )
        return false;
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char const c = (unsigned char)key[i];
        if( (c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '_' )
            return false;
    }
    return true;
}

/** Values are one physical INI line. Refusing rather than escaping CR/LF
 *  keeps decode and encode exact inverses and prevents a value from creating
 *  another key or plugin section on the next launch. */
static bool
plugin_config_value_valid(char const* value)
{
    return value && strlen(value) < TORIRS_PLUGIN_CONFIG_VALUE_MAX &&
           !strchr(value, '\r') && !strchr(value, '\n');
}

enum PluginConfigSchemaResult
{
    PLUGIN_CONFIG_SCHEMA_OK = 0,
    PLUGIN_CONFIG_SCHEMA_TOO_LARGE,
    PLUGIN_CONFIG_SCHEMA_INVALID_KEY,
    PLUGIN_CONFIG_SCHEMA_DUPLICATE_KEY,
    PLUGIN_CONFIG_SCHEMA_INVALID_DEFAULT,
    PLUGIN_CONFIG_SCHEMA_INVALID_TYPE,
};

static char const*
plugin_config_schema_result_text(enum PluginConfigSchemaResult result)
{
    switch( result )
    {
        case PLUGIN_CONFIG_SCHEMA_OK: return "valid";
        case PLUGIN_CONFIG_SCHEMA_TOO_LARGE: return "too many items";
        case PLUGIN_CONFIG_SCHEMA_INVALID_KEY: return "an invalid key";
        case PLUGIN_CONFIG_SCHEMA_DUPLICATE_KEY: return "a duplicate key";
        case PLUGIN_CONFIG_SCHEMA_INVALID_DEFAULT: return "an invalid default value";
        case PLUGIN_CONFIG_SCHEMA_INVALID_TYPE: return "an invalid item type";
    }
    return "an unknown error";
}

static enum PluginConfigSchemaResult
plugin_config_schema_validate(
    struct ToriRS_ConfigItem const* schema,
    int* out_count,
    int* out_row)
{
    assert(out_count);
    assert(out_row);

    *out_count = 0;
    *out_row = -1;
    if( !schema )
        return PLUGIN_CONFIG_SCHEMA_OK;

    for( int i = 0; i <= TORIRS_PLUGIN_CONFIG_MAX; i++ )
    {
        struct ToriRS_ConfigItem const* item = &schema[i];
        if( !item->key )
        {
            *out_count = i;
            return PLUGIN_CONFIG_SCHEMA_OK;
        }
        *out_row = i;
        if( i == TORIRS_PLUGIN_CONFIG_MAX )
            return PLUGIN_CONFIG_SCHEMA_TOO_LARGE;
        if( !plugin_config_key_valid(item->key) )
            return PLUGIN_CONFIG_SCHEMA_INVALID_KEY;
        if( item->default_value && !plugin_config_value_valid(item->default_value) )
            return PLUGIN_CONFIG_SCHEMA_INVALID_DEFAULT;
        if( item->type < TORIRS_CONFIG_BOOL || item->type > TORIRS_CONFIG_TEXT )
            return PLUGIN_CONFIG_SCHEMA_INVALID_TYPE;
        for( int previous = 0; previous < i; previous++ )
            if( strcmp(schema[previous].key, item->key) == 0 )
                return PLUGIN_CONFIG_SCHEMA_DUPLICATE_KEY;
    }

    return PLUGIN_CONFIG_SCHEMA_TOO_LARGE;
}

static struct PluginConfigSlot*
plugin_config_slot(
    struct PluginContext* ctx,
    char const* key,
    bool create)
{
    assert(ctx);
    assert(key);

    if( !plugin_config_key_valid(key) )
        return NULL;

    for( int i = 0; i < ctx->config_count; i++ )
    {
        if( strcmp(ctx->config[i].key, key) == 0 )
            return &ctx->config[i];
    }
    if( !create )
        return NULL;
    if( ctx->config_count >= TORIRS_PLUGIN_CONFIG_MAX )
    {
        /* A declared schema cannot reach this -- PluginHost_RegisterV2 refuses
         * one that does not fit. What can is an ini carrying more unclaimed
         * keys than the headroom above the schema, which is a settings file
         * that has outlived several renames. Said out loud because the
         * alternative is a setting that will not stick and no reason given. */
        TORIRS_LOG(
            "plugin: '%s' config store is full (%d); '%s' is not kept\n",
            ctx->name,
            TORIRS_PLUGIN_CONFIG_MAX,
            key);
        return NULL;
    }

    struct PluginConfigSlot* slot = &ctx->config[ctx->config_count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->schema_index = plugin_schema_index(ctx, key);
    return slot;
}

/** Bounded copy into a fixed buffer; NULL reads as "". */
static void
plugin_copy_str(
    char* dst,
    size_t cap,
    char const* src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/** Whether copying `src` with plugin_copy_str would change this fixed field.
 *  Comparing the unbounded source directly makes an already-truncated value
 *  look new forever, which turns one long live label into per-frame deltas. */
static bool
plugin_copy_str_would_change(
    char const* dst,
    size_t cap,
    char const* src)
{
    size_t n;

    assert(dst);
    assert(cap > 0);
    src = src ? src : "";
    n = strlen(src);
    if( n >= cap )
        n = cap - 1;
    return strlen(dst) != n || memcmp(dst, src, n) != 0;
}

/* Seed the store from the schema. Called at registration so a plugin can read
 * its config before any ini has been applied. */
static void
plugin_config_seed(
    struct PluginContext* ctx,
    int schema_count)
{
    assert(ctx);
    assert(schema_count >= 0 && schema_count <= TORIRS_PLUGIN_CONFIG_MAX);

    ctx->schema_count = schema_count;
    struct ToriRS_ConfigItem const* schema = plugin_schema(ctx);
    if( !schema )
        return;
    for( int i = 0; i < schema_count; i++ )
    {
        struct ToriRS_ConfigItem const* item = &schema[i];
        struct PluginConfigSlot* slot = plugin_config_slot(ctx, item->key, true);
        if( !slot )
            continue;
        snprintf(
            slot->value, sizeof(slot->value), "%s", item->default_value ? item->default_value : "");
    }
}

/*
 * Dispatch one event.
 *
 * The subscriber list is snapshotted by index rather than iterated live
 * because a handler is allowed to disable its own plugin (that is how a
 * faulting script leaves the frame), which rewrites the list underneath us.
 * Re-reading the count each step and skipping stale entries is what keeps that
 * from walking off the end.
 */
/* Defined with the lifecycle, far below, because it belongs to it -- the api
 * verb that lets a plugin stand down is the only thing up here that needs it. */
static void
plugin_teardown(
    struct ToriRS_PluginHost* host,
    int plugin_index);
static void
plugin_dispatch_one(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    enum PluginCallbackKind ev,
    void* payload);
static int plugin_ev_is_draw(enum PluginCallbackKind ev);

/* Retained entity appearance/action helpers used by the V2 game module. */
static int
plugin_entity_parse(
    char const* part,
    int* out_a,
    int* out_b,
    int* out_c,
    int* out_d);
static int
plugin_entity_hull_allowed(
    struct ToriRS_PluginHost* host,
    int plugin,
    int element_id);
static enum ToriRS_CallbackResult
plugin_dispatch(
    struct ToriRS_PluginHost* host,
    enum PluginCallbackKind ev,
    void* payload)
{
    assert(host);
    assert(ev >= 0);
    assert(ev < PLUGIN_CALLBACK_COUNT);

    enum ToriRS_CallbackResult verdict = TORIRS_CALLBACK_CONTINUE;
    int const prev_dispatching = host->dispatching;
    int const prev_event = host->dispatch_event;

    int const count = host->plugin_count;
    int const* order = plugin_ev_is_draw(ev) ? host->draw_order : host->event_order;
    for( int i = 0; i < count; i++ )
    {
        int const plugin = order[i];
        struct PluginContext* ctx = &host->plugins[plugin];
        if( !ctx->enabled || !ctx->running ||
            !plugin_v2_has_event_callback(ctx->def, ev) )
            continue;

        host->dispatching = plugin;
        host->dispatch_event = ev;
        verdict = plugin_v2_event(ctx, payload, (void*)(intptr_t)(ev + 1));
        host->dispatching = prev_dispatching;
        host->dispatch_event = prev_event;
        if( verdict == TORIRS_CALLBACK_CONSUME )
            return TORIRS_CALLBACK_CONSUME;

    }
    return TORIRS_CALLBACK_CONTINUE;
}

/*
 * Is `ev` one of the passes whose order is a Z ORDER?
 *
 * On these, running first means being drawn UNDER, so they sort by
 * ToriRS_PluginDefV2::draw_order and the rest sort by `priority`. One list, two
 * keys, chosen here -- the alternative is a second subscription table that
 * only three events use.
 */
static int
plugin_ev_is_draw(enum PluginCallbackKind ev)
{
    return ev == PLUGIN_CALLBACK_DRAW_WORLD || ev == PLUGIN_CALLBACK_DRAW_CANVAS ||
           ev == PLUGIN_CALLBACK_DRAW_FRAME || ev == PLUGIN_CALLBACK_PANEL_DRAW;
}

/* ------------------------------------------------------------ api surface */

static void
api_log(
    struct PluginContext* ctx,
    char const* fmt,
    ...)
{
    assert(ctx);
    assert(fmt);

    /*
     * A plugin's own log(). Narration by definition, and a plugin is free to
     * call it from a per-frame event handler, so it goes through the channel
     * like everything else -- an optimized build must not be paying a syscall
     * per frame for a line nobody is reading.
     */
    va_list args;
    TORIRS_LOG("[%s] ", ctx->name);
    va_start(args, fmt);
    TORIRS_VLOG(fmt, args);
    va_end(args);
    TORIRS_LOGC('\n');
}

/* The plugin header spells enum AppScreen's values again, because a plugin
 * must not include the app's. These are what keep the two from drifting. */
_Static_assert(
    (int)TORIRS_SCREEN_BOOT == 0,
    "plugin screen BOOT");
_Static_assert(
    (int)TORIRS_SCREEN_TITLE == 10,
    "plugin screen TITLE");
_Static_assert(
    (int)TORIRS_SCREEN_CONNECTING == 20,
    "plugin screen CONNECTING");
_Static_assert(
    (int)TORIRS_SCREEN_GAME == 30,
    "plugin screen GAME");

static int
api_screen(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.screen(ctx->host->engine.user);
}

static int
api_world_cycle(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.world_cycle(ctx->host->engine.user);
}

static uint64_t
api_frame_ms(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.frame_ms(ctx->host->engine.user);
}

static uint64_t
api_frame_work_us(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.frame_work_us(ctx->host->engine.user);
}

static int
api_local_player(
    struct PluginContext* ctx,
    struct ToriRS_PlayerSnapshot* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.local_player(ctx->host->engine.user, out);
}

static int
api_npc_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_NpcSnapshot* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.npc_next(ctx->host->engine.user, iter, out);
}

static int
api_npc_by_slot(
    struct PluginContext* ctx,
    int slot,
    struct ToriRS_NpcSnapshot* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.npc_by_slot(ctx->host->engine.user, slot, out);
}

static int
api_player_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_PlayerSnapshot* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.player_next(ctx->host->engine.user, iter, out);
}

static int
api_loc_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_ScenerySnapshot* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.loc_next(ctx->host->engine.user, iter, out);
}

static int
api_highlight_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_HighlightItem* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.highlight_next(ctx->host->engine.user, iter, out);
}

static void
api_notify(
    struct PluginContext* ctx,
    char const* text)
{
    assert(ctx);
    assert(text);
    ctx->host->engine.notify(ctx->host->engine.user, text);
}

static int
api_key_held(
    struct PluginContext* ctx,
    int keycode)
{
    assert(ctx);
    return ctx->host->engine.key_held(ctx->host->engine.user, keycode);
}

static int
api_hover_tile(
    struct PluginContext* ctx,
    int* out_tile_x,
    int* out_tile_z,
    int* out_level)
{
    assert(ctx);
    assert(out_tile_x);
    assert(out_tile_z);
    assert(out_level);
    return ctx->host->engine.hover_tile(ctx->host->engine.user, out_tile_x, out_tile_z, out_level);
}

static int
api_hover_entity(
    struct PluginContext* ctx,
    struct ToriRS_HoverTarget* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.hover_entity(ctx->host->engine.user, out);
}

static int
api_element_height(
    struct PluginContext* ctx,
    int element_id)
{
    assert(ctx);
    return ctx->host->engine.element_height(ctx->host->engine.user, element_id);
}

static int
api_mouse_pos(
    struct PluginContext* ctx,
    int* out_x,
    int* out_y)
{
    assert(ctx);
    return ctx->host->engine.mouse_pos(ctx->host->engine.user, out_x, out_y);
}

/* ------------------------------------------------------------ the gameframe */

/* Both layout entry points deliver to ONE plugin -- the frame's owner -- and
 * the walker that does that is defined with the other window-scoped dispatch,
 * far below. Forward-declared rather than moved, so the plugin-scoped dispatch
 * rules stay written down in one place. */
static void
plugin_dispatch_one(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    enum PluginCallbackKind ev,
    void* payload);

static int
api_frame_offer_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_FrameOfferInfo* out)
{
    struct PluginFrameCatalogEntry const* entry;
    int next;

    assert(ctx);
    assert(out);
    if( iter == INT_MAX )
        return -1;
    next = iter + 1;
    entry = PluginFrameCatalog_At(&ctx->host->frame_catalog, next);
    if( !entry )
        return -1;

    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    snprintf(out->id, sizeof(out->id), "%s", entry->id);
    snprintf(out->title, sizeof(out->title), "%s", entry->title);
    snprintf(out->provider, sizeof(out->provider), "%s", entry->provider);
    out->canvas = entry->canvas == TORIRS_FRAME_CANVAS_FIXED
                      ? TORIRS_FRAME_CANVAS_FIXED
                      : TORIRS_FRAME_CANVAS_WINDOW;
    if( out->canvas == TORIRS_FRAME_CANVAS_FIXED )
    {
        out->width = entry->width;
        out->height = entry->height;
    }
    else
    {
        out->min_width = entry->width;
        out->min_height = entry->height;
    }
    out->available = entry->available != 0;
    return next;
}

static void
api_frame_selection(
    struct PluginContext* ctx,
    struct ToriRS_FrameSelection* out)
{
    struct ToriRS_PluginHost* host;

    assert(ctx);
    assert(out);
    host = ctx->host;
    *out = host->frame_selection;
}

static int
plugin_frame_preference_id_valid(char const* id)
{
    int slash = 0;

    assert(id);
    if( strcmp(id, "auto") == 0 )
        return 1;
    if( !id[0] || strlen(id) >= TORIRS_PLUGIN_FRAME_ID_MAX )
        return 0;
    for( char const* at = id; *at; at++ )
    {
        char const c = *at;
        if( c == '/' )
        {
            slash++;
            if( at == id || !at[1] )
                return 0;
            continue;
        }
        if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' )
            continue;
        return 0;
    }
    return slash == 1;
}

static int
api_frame_select(
    struct PluginContext* ctx,
    char const* id)
{
    struct ToriRS_PluginHost* host;

    assert(ctx);
    assert(id);
    host = ctx->host;
    if( !plugin_frame_preference_id_valid(id) )
        return 0;
    if( host->engine.frame_preference_set &&
        !host->engine.frame_preference_set(host->engine.user, id, 1) )
        return 0;
    if( strcmp(host->frame_selection.requested_id, id) == 0 )
        return 1;

    snprintf(host->frame_selection.requested_id, sizeof(host->frame_selection.requested_id), "%s", id);
    host->frame_selection.revision++;
    /* Invalidate an in-flight frame candidate immediately, without resolving
     * lifecycle until the safe boundary below. */
    host->frame_selection_epoch++;
    host->frame_selection_dirty = 1;
    /* Never resolve lifecycle from inside a plugin callback. The next host
     * frame boundary consumes frame_selection_dirty before dispatching its
     * frame callbacks, so the current callback keeps its state/API alive
     * through return and layout remains separately fenced. */
    return 1;
}

static void
api_frame_invalidate(struct PluginContext* ctx)
{
    struct ToriRS_PluginHost* host;
    struct PluginFrameCatalogEntry const* active;
    struct PluginFrameCatalogEntry const* target;

    assert(ctx);
    host = ctx->host;
    active = PluginFrameCatalog_At(&host->frame_catalog, host->frame_active_entry);
    target = PluginFrameCatalog_At(&host->frame_catalog, host->frame_target_entry);
    if( (!active || active->plugin != ctx->index) &&
        (!target || target->plugin != ctx->index) )
        return;
    if( target && target->plugin == ctx->index &&
        host->frame_target_entry != host->frame_active_entry )
    {
        /* A pending builder is retried only when its provider says its inputs
         * changed. The app consumes this one-shot request at a safe layout
         * fence while the committed provider continues to render. */
        host->frame_selection_dirty = 1;
        return;
    }
    if( active && active->plugin == ctx->index )
        plugin_layout_publish(host);
}

/*
 * Tell the engine what the resolved frame is now, so it can switch the lane's
 * own chrome off (or back on) and pin or unpin the canvas.
 *
 * Called on every transition and on no-ops besides, because the engine's copy
 * of this is what the layout pass reads. Publishing it atomically prevents a
 * plugin frame from drawing over a lane frame the engine still considers live.
 */
static void
plugin_layout_publish(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->placement_cache_valid = 0;
    host->engine.frame_activate(
        host->engine.user,
        plugin_frame_owner(host) >= 0 ? 1 : 0,
        host->layout_canvas,
        host->layout_fixed_w,
        host->layout_fixed_h);
}

static void
plugin_layout_candidate_fail(
    struct ToriRS_PluginHost* host,
    char const* reason)
{
    assert(host);
    host->layout_candidate.invalid = true;
    if( !host->layout_candidate.reason[0] && reason )
        snprintf(
            host->layout_candidate.reason,
            sizeof(host->layout_candidate.reason),
            "%s",
            reason);
}

static int
plugin_layout_rect_valid(int x, int y, int w, int h)
{
    int64_t const right = (int64_t)x + w;
    int64_t const bottom = (int64_t)y + h;

    return w > 0 && h > 0 && right >= INT32_MIN && right <= INT32_MAX &&
           bottom >= INT32_MIN && bottom <= INT32_MAX;
}

static int
host_frame_surface_member(
    struct PluginContext* ctx,
    int slot,
    int member,
    int x,
    int y,
    int w,
    int h)
{
    struct ToriRS_PluginHost* host;
    struct PluginLayoutCandidate* candidate;
    int qx = 0;
    int qy = 0;
    int qw = 0;
    int qh = 0;

    assert(ctx);
    /* The same window test the draw verbs make. A slot placed outside the
     * declaration would land in a table the engine has already applied, so it
     * would take effect a frame late and survive a declaration that never
     * mentioned it -- which is exactly the drift the rebuilt-from-nothing rule
     * exists to prevent. */
    host = ctx->host;
    candidate = &host->layout_candidate;
    assert(host->layout_declaring && "frame surfaces are legal only during frame build");
    assert(host->layout_declarer == ctx->index);
    /* A number a plugin computed, so out of range is input. */
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
    {
        plugin_layout_candidate_fail(host, "The frame declared an unknown surface.");
        return 0;
    }
    if( member < -1 || !plugin_layout_rect_valid(x, y, w, h) )
    {
        plugin_layout_candidate_fail(host, "The frame declared an invalid surface rectangle.");
        return 0;
    }
    for( int i = 0; i < candidate->rect_count; i++ )
        if( candidate->rects[i].slot == slot && candidate->rects[i].member == member )
        {
            plugin_layout_candidate_fail(host, "The frame declared a surface or member twice.");
            return 0;
        }
    if( candidate->rect_count >= TORIRS_PLUGIN_LAYOUT_RECTS_MAX )
    {
        plugin_layout_candidate_fail(host, "The frame declared too many surfaces or members.");
        return 0;
    }
    candidate->rects[candidate->rect_count++] = (struct PluginLayoutRect){
        .slot = slot,
        .member = member,
        .x = x,
        .y = y,
        .w = w,
        .h = h,
    };
    if( slot == TORIRS_HOST_SURFACE_VIEWPORT && member == -1 )
        candidate->viewport_declared = true;

    /* The return remains the old "does this lane have the target" answer;
     * recording is host-local until commit and must not mutate the live tree. */
    if( host->engine.layout_slot_exists )
        return host->engine.layout_slot_exists(host->engine.user, slot, member);
    return member < 0
               ? host->engine.slot_rect(host->engine.user, slot, &qx, &qy, &qw, &qh)
               : host->engine.slot_member_rect(
                     host->engine.user, slot, member, &qx, &qy, &qw, &qh);
}

/* The whole role, which is `member` -1. One entry point with a name for the
 * common case, rather than every caller writing the sentinel. */
static int
host_frame_surface(
    struct PluginContext* ctx,
    int slot,
    int x,
    int y,
    int w,
    int h)
{
    return host_frame_surface_member(ctx, slot, -1, x, y, w, h);
}

static int
api_tab_active(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.tab_active(ctx->host->engine.user);
}

static bool
api_tab_select(
    struct PluginContext* ctx,
    int tabno)
{
    assert(ctx);
    /* A tab number a plugin read off its own stone table. */
    if( tabno < 0 )
        return false;
    return ctx->host->engine.tab_select(ctx->host->engine.user, tabno) ? true : false;
}

static int
api_tab_enabled(
    struct PluginContext* ctx,
    int tabno)
{
    assert(ctx);
    /* A tab number a plugin read off its own stone table, like tab_select's. */
    if( tabno < 0 )
        return 0;
    return ctx->host->engine.tab_enabled(ctx->host->engine.user, tabno);
}

/* -- layout regions ------------------------------------------------------- */

struct PluginRect
{
    int x;
    int y;
    int w;
    int h;
};

/**
 * `box` with `cut` taken out of it, as the largest rectangle that survives.
 *
 * There is no exact rectangular answer to "subtract a rectangle from a
 * rectangle" -- the true result is an L, and an L is not a box a caller can
 * centre anything in. So this does what a person does: it tries the four
 * rectangles left when you slice `box` along each of `cut`'s edges and keeps
 * whichever has the most area.
 *
 * A heuristic, and the right one here because of what the occluders ARE. A
 * minimap in a corner, a chatbox along the bottom, a sidebar down one side:
 * each has an obvious side to cut from, and "the biggest remaining piece"
 * picks it every time. A cut sitting in the middle of the box has no good
 * answer and this gives the least bad one.
 */
/* ---------------------------------------------------------------- exact placement */

static int
plugin_placement_add_named_occluders(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PlacementRegion* cuts,
    uint32_t flag)
{
    assert(host);
    assert(cuts);
    for( int i = 0; i < ToriRS_UiRegistry_NodeCount(&host->ui_registry); i++ )
    {
        struct ToriRS_UiResolvedNode node;
        struct ToriRS_PlacementRect rect;
        struct ToriRS_UiNodeRef const ref = ToriRS_UiRegistry_NodeAt(&host->ui_registry, i);

        if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, ref, &node) )
            continue;
        if( !(node.available_facets & TORIRS_UI_FACET_BOUNDS) ||
            !(node.value.flags & TORIRS_UI_NODE_VISIBLE) || !(node.value.flags & flag) )
            continue;
        rect.x = node.value.bounds.x;
        rect.y = node.value.bounds.y;
        rect.w = node.value.bounds.width;
        rect.h = node.value.bounds.height;
        if( rect.w <= 0 || rect.h <= 0 )
            continue;
        if( ToriRS_PlacementRegion_AddRect(cuts, &rect) != TORIRS_PLACEMENT_OK )
            return 0;
    }
    return 1;
}

static int
plugin_placement_named_before(
    struct ToriRS_PluginHost const* host,
    int lhs,
    int rhs)
{
    struct PluginPlacementReservation const* a;
    struct PluginPlacementReservation const* b;
    int cmp;

    assert(host);
    a = &host->placement_reservations[lhs];
    b = &host->placement_reservations[rhs];
    if( a->edge != b->edge )
        return a->edge < b->edge;
    cmp = strcmp(host->plugins[a->plugin].name, host->plugins[b->plugin].name);
    if( cmp != 0 )
        return cmp < 0;
    return strcmp(a->name, b->name) < 0;
}

static int
plugin_placement_reservation_fragment(
    struct ToriRS_PlacementRegion const* region,
    int edge,
    int pixels,
    struct ToriRS_PlacementRect* out)
{
    int best = -1;
    int count;

    assert(region);
    assert(out);
    count = ToriRS_PlacementRegion_RectCount(region);
    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_PlacementRect candidate;
        int better = best < 0;
        ToriRS_PlacementRegion_RectAt(region, i, &candidate);
        if( (edge == TORIRS_EDGE_LEFT ||
             edge == TORIRS_EDGE_RIGHT) &&
            candidate.w < pixels )
            continue;
        if( (edge == TORIRS_EDGE_TOP ||
             edge == TORIRS_EDGE_BOTTOM) &&
            candidate.h < pixels )
            continue;
        if( best >= 0 )
        {
            int const candidate_far_x = candidate.x + candidate.w;
            int const best_far_x = out->x + out->w;
            int const candidate_far_y = candidate.y + candidate.h;
            int const best_far_y = out->y + out->h;
            if( edge == TORIRS_EDGE_LEFT )
                better = candidate.x < out->x || (candidate.x == out->x && candidate.h > out->h);
            else if( edge == TORIRS_EDGE_RIGHT )
                better = candidate_far_x > best_far_x ||
                         (candidate_far_x == best_far_x && candidate.h > out->h);
            else if( edge == TORIRS_EDGE_TOP )
                better = candidate.y < out->y || (candidate.y == out->y && candidate.w > out->w);
            else
                better = candidate_far_y > best_far_y ||
                         (candidate_far_y == best_far_y && candidate.w > out->w);
        }
        if( better )
        {
            best = i;
            *out = candidate;
        }
    }
    return best >= 0;
}

static int
plugin_placement_apply_named_reservations(
    struct ToriRS_PluginHost* host,
    int area,
    struct ToriRS_PlacementRegion* region,
    struct PluginPlacementResolvedReservation* resolved)
{
    int order[TORIRS_PLUGIN_RESERVES_MAX];
    int count = 0;

    assert(host);
    assert(region);
    assert(resolved);
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        struct PluginPlacementReservation const* reservation =
            &host->placement_reservations[i];
        memset(&resolved[i], 0, sizeof(resolved[i]));
        resolved[i].plugin = -1;
        if( reservation->plugin >= 0 && reservation->area == area && reservation->pixels > 0 )
            order[count++] = i;
    }
    for( int i = 1; i < count; i++ )
    {
        int const value = order[i];
        int at = i;
        while( at > 0 && plugin_placement_named_before(host, value, order[at - 1]) )
        {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = value;
    }

    for( int i = 0; i < count; i++ )
    {
        int const reservation_index = order[i];
        struct PluginPlacementReservation const* reservation =
            &host->placement_reservations[reservation_index];
        struct ToriRS_PlacementRect rect;

        if( !plugin_placement_reservation_fragment(
                region, reservation->edge, reservation->pixels, &rect) )
            continue;
        if( reservation->edge == TORIRS_EDGE_LEFT )
            rect.w = reservation->pixels;
        else if( reservation->edge == TORIRS_EDGE_RIGHT )
        {
            rect.x += rect.w - reservation->pixels;
            rect.w = reservation->pixels;
        }
        else if( reservation->edge == TORIRS_EDGE_TOP )
            rect.h = reservation->pixels;
        else
        {
            rect.y += rect.h - reservation->pixels;
            rect.h = reservation->pixels;
        }
        if( ToriRS_PlacementRegion_SubtractRect(region, &rect, region) != TORIRS_PLACEMENT_OK )
            return 0;
        resolved[reservation_index].plugin = reservation->plugin;
        resolved[reservation_index].assigned = 1;
        resolved[reservation_index].rect = rect;
        snprintf(
            resolved[reservation_index].name,
            sizeof(resolved[reservation_index].name),
            "%s",
            reservation->name);
    }
    return 1;
}

static int
plugin_placement_rect_equal(
    struct ToriRS_PlacementRect const* a,
    struct ToriRS_PlacementRect const* b)
{
    assert(a);
    assert(b);
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

/** Compare the semantic reservation-to-box mapping, not backing-table slots. */
static int
plugin_placement_resolved_equal(
    struct PluginPlacementResolvedReservation const* a,
    struct PluginPlacementResolvedReservation const* b)
{
    int a_count = 0;
    int b_count = 0;

    assert(a);
    assert(b);
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        int found = 0;

        if( a[i].assigned )
            a_count++;
        if( b[i].assigned )
            b_count++;
        if( !a[i].assigned )
            continue;
        for( int j = 0; j < TORIRS_PLUGIN_RESERVES_MAX; j++ )
            if( b[j].assigned && b[j].plugin == a[i].plugin &&
                strcmp(b[j].name, a[i].name) == 0 &&
                plugin_placement_rect_equal(&b[j].rect, &a[i].rect) )
            {
                found = 1;
                break;
            }
        if( !found )
            return 0;
    }
    return a_count == b_count;
}

static uint32_t
plugin_placement_revision_next(uint32_t revision)
{
    revision++;
    return revision ? revision : 1;
}

static int
plugin_placement_rebuild(
    struct ToriRS_PluginHost* host,
    int announce_initial)
{
    struct ToriRS_PlacementRegion canvas;
    struct ToriRS_PlacementRegion os;
    struct ToriRS_PlacementRegion cuts;
    struct ToriRS_PlacementRegion candidate[PLUGIN_AREA_COUNT];
    struct PluginPlacementResolvedReservation resolved[TORIRS_PLUGIN_RESERVES_MAX];
    struct ToriRS_PlacementRect rect;
    int changed;

    assert(host);
    if( host->placement_cache_valid && host->placement_cache_revision == host->layout_revision )
        return 1;
    plugin_ui_refresh_base(host);

    ToriRS_PlacementRegion_Clear(&canvas);
    ToriRS_PlacementRegion_Clear(&os);
    ToriRS_PlacementRegion_Clear(&cuts);
    for( int i = 0; i < PLUGIN_AREA_COUNT; i++ )
        ToriRS_PlacementRegion_Clear(&candidate[i]);
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        memset(&resolved[i], 0, sizeof(resolved[i]));
        resolved[i].plugin = -1;
    }

    if( !host->engine.slot_rect(
            host->engine.user, TORIRS_HOST_SURFACE_CANVAS, &rect.x, &rect.y, &rect.w, &rect.h) ||
        rect.w <= 0 || rect.h <= 0 )
        return 0;
    ToriRS_PlacementRegion_SetRect(&canvas, &rect);

    if( host->engine.platform_safe_next )
    {
        int iter = -1;
        while( (iter = host->engine.platform_safe_next(
                    host->engine.user, iter, &rect.x, &rect.y, &rect.w, &rect.h)) >= 0 )
        {
            if( rect.w <= 0 || rect.h <= 0 )
                continue;
            if( ToriRS_PlacementRegion_AddRect(&os, &rect) != TORIRS_PLACEMENT_OK )
                return 0;
        }
    }
    else if(
        host->engine.platform_safe_rect &&
        host->engine.platform_safe_rect(host->engine.user, &rect.x, &rect.y, &rect.w, &rect.h) && rect.w > 0 &&
        rect.h > 0 )
        ToriRS_PlacementRegion_SetRect(&os, &rect);
    else
        os = canvas;
    if( ToriRS_PlacementRegion_Intersect(
            &canvas, &os, &candidate[TORIRS_AREA_PLATFORM_SAFE]) !=
        TORIRS_PLACEMENT_OK )
        return 0;

    ToriRS_PlacementRegion_Clear(&cuts);
    if( !plugin_placement_add_named_occluders(host, &cuts, TORIRS_UI_NODE_BLOCKS_FRAME) )
        return 0;
    if( ToriRS_PlacementRegion_Subtract(
            &candidate[TORIRS_AREA_PLATFORM_SAFE],
            &cuts,
            &candidate[TORIRS_AREA_FRAME_BUILD]) != TORIRS_PLACEMENT_OK )
        return 0;

    if( host->engine.slot_rect(
            host->engine.user, TORIRS_HOST_SURFACE_VIEWPORT, &rect.x, &rect.y, &rect.w, &rect.h) &&
        rect.w > 0 && rect.h > 0 )
        ToriRS_PlacementRegion_SetRect(&candidate[TORIRS_AREA_RAW_VIEWPORT], &rect);
    else
        candidate[TORIRS_AREA_RAW_VIEWPORT] = canvas;

    if( ToriRS_PlacementRegion_Intersect(
            &candidate[TORIRS_AREA_RAW_VIEWPORT],
            &candidate[TORIRS_AREA_PLATFORM_SAFE],
            &candidate[TORIRS_AREA_OVERLAY_SAFE]) != TORIRS_PLACEMENT_OK )
        return 0;

    ToriRS_PlacementRegion_Clear(&cuts);
    if( !plugin_placement_add_named_occluders(host, &cuts, TORIRS_UI_NODE_BLOCKS_OVERLAY) )
        return 0;
    if( ToriRS_PlacementRegion_Subtract(
            &candidate[TORIRS_AREA_OVERLAY_SAFE],
            &cuts,
            &candidate[TORIRS_AREA_OVERLAY_SAFE]) != TORIRS_PLACEMENT_OK )
        return 0;
    if( !plugin_placement_apply_named_reservations(
            host,
            TORIRS_AREA_OVERLAY_SAFE,
            &candidate[TORIRS_AREA_OVERLAY_SAFE],
            resolved) )
        return 0;

    changed = !host->placement_initialized;
    if( host->placement_initialized )
    {
        for( int i = 0; i < PLUGIN_AREA_COUNT; i++ )
            if( !ToriRS_PlacementRegion_Equals(&host->placement[i], &candidate[i]) )
            {
                changed = 1;
                break;
            }
        if( !changed &&
            !plugin_placement_resolved_equal(host->placement_resolved_reservations, resolved) )
            changed = 1;
    }

    /* The previous complete answer stays live if any operation above fails. */
    memcpy(host->placement, candidate, sizeof(candidate));
    memcpy(
        host->placement_resolved_reservations,
        resolved,
        sizeof(host->placement_resolved_reservations));
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        host->placement_reservations[i].assigned = resolved[i].assigned;
        host->placement_reservations[i].rect = resolved[i].rect;
    }
    host->placement_cache_revision = host->layout_revision;
    host->placement_cache_valid = 1;
    if( changed )
    {
        host->placement_revision = plugin_placement_revision_next(host->placement_revision);
        if( host->placement_initialized || announce_initial )
            host->placement_notify_pending = 1;
        else
            host->placement_notified_revision = host->placement_revision;
    }
    host->placement_initialized = 1;
    return 1;
}

static int
plugin_placement_refresh(struct ToriRS_PluginHost* host)
{
    assert(host);
    /* A callback which mutates a reservation schedules another transaction.
     * Every subscriber in this one continues to observe one frozen retained
     * placement snapshot and revision. */
    if( host->layout_notifying && host->placement_initialized )
        return 1;
    return plugin_placement_rebuild(host, 0);
}

static uint32_t
api_placement_revision(struct PluginContext* ctx)
{
    assert(ctx);
    (void)plugin_placement_refresh(ctx->host);
    return ctx->host->placement_revision;
}

static int
api_placement_rect_next(
    struct PluginContext* ctx,
    int area,
    int iter,
    struct ToriRS_PlacementRect* out)
{
    int next;

    assert(ctx);
    assert(out);
    if( area < 0 || area >= PLUGIN_AREA_COUNT )
        return -1;
    if( !plugin_placement_refresh(ctx->host) )
        return -1;
    if( iter == INT_MAX )
        return -1;
    next = iter + 1;
    if( next < 0 || next >= ToriRS_PlacementRegion_RectCount(&ctx->host->placement[area]) )
        return -1;
    ToriRS_PlacementRegion_RectAt(&ctx->host->placement[area], next, out);
    return next;
}

static int
api_placement_place(
    struct PluginContext* ctx,
    int area,
    int anchor,
    int width,
    int height,
    int margin,
    struct ToriRS_PlacementRect* out)
{
    assert(ctx);
    assert(out);
    if( area < 0 || area >= PLUGIN_AREA_COUNT )
        return 0;
    if( anchor < 0 || anchor >= TORIRS_PLACEMENT_ANCHOR_COUNT || width <= 0 || height <= 0 ||
        margin < 0 )
        return 0;
    if( !plugin_placement_refresh(ctx->host) )
        return 0;
    return ToriRS_PlacementRegion_Place(
               &ctx->host->placement[area], anchor, width, height, margin, out)
               ? 1
               : 0;
}

static int
api_placement_contains(
    struct PluginContext* ctx,
    int area,
    struct ToriRS_PlacementRect const* rect)
{
    assert(ctx);
    assert(rect);
    if( area < 0 || area >= PLUGIN_AREA_COUNT )
        return 0;
    if( !plugin_placement_refresh(ctx->host) )
        return 0;
    return ToriRS_PlacementRegion_ContainsRect(&ctx->host->placement[area], rect) ? 1 : 0;
}

static int
plugin_placement_reservation_name_valid(char const* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= TORIRS_HOST_RESERVATION_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' )
            continue;
        return 0;
    }
    return 1;
}

static int
api_placement_reserve(
    struct PluginContext* ctx,
    char const* name,
    int area,
    int edge,
    int pixels)
{
    struct ToriRS_PluginHost* host;
    int free_row = -1;

    assert(ctx);
    assert(name);
    host = ctx->host;
    if( !plugin_placement_reservation_name_valid(name) )
        return 0;
    if( area != TORIRS_AREA_OVERLAY_SAFE )
        return 0;
    if( edge < 0 || edge >= PLUGIN_EDGE_COUNT || pixels < 0 )
        return 0;

    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        struct PluginPlacementReservation* reservation = &host->placement_reservations[i];
        if( reservation->plugin == ctx->index && strcmp(reservation->name, name) == 0 )
        {
            if( reservation->area == area && reservation->edge == edge &&
                reservation->pixels == pixels )
                return 1;
            if( pixels == 0 )
            {
                memset(reservation, 0, sizeof(*reservation));
                reservation->plugin = -1;
            }
            else
            {
                reservation->area = area;
                reservation->edge = edge;
                reservation->pixels = pixels;
                reservation->assigned = 0;
            }
            PluginHost_LayoutChanged(host);
            return 1;
        }
        if( reservation->plugin < 0 && free_row < 0 )
            free_row = i;
    }

    if( pixels == 0 )
        return 1;
    if( free_row < 0 )
        return 0;
    host->placement_reservations[free_row].plugin = ctx->index;
    host->placement_reservations[free_row].area = area;
    host->placement_reservations[free_row].edge = edge;
    host->placement_reservations[free_row].pixels = pixels;
    snprintf(
        host->placement_reservations[free_row].name,
        sizeof(host->placement_reservations[free_row].name),
        "%s",
        name);
    PluginHost_LayoutChanged(host);
    return 1;
}

static int
api_placement_reservation_rect(
    struct PluginContext* ctx,
    char const* name,
    struct ToriRS_PlacementRect* out)
{
    assert(ctx);
    assert(name);
    assert(out);
    if( !plugin_placement_reservation_name_valid(name) )
        return 0;
    if( !plugin_placement_refresh(ctx->host) )
        return 0;
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        struct PluginPlacementResolvedReservation const* reservation =
            &ctx->host->placement_resolved_reservations[i];
        if( reservation->plugin == ctx->index && reservation->assigned &&
            strcmp(reservation->name, name) == 0 )
        {
            *out = reservation->rect;
            return 1;
        }
    }
    return 0;
}

static int
plugin_ui_contribution_status(
    struct PluginContext* ctx,
    char const* node,
    struct ToriRS_UiContributionStatus* out)
{
    struct ToriRS_UiNodeRef wanted;

    assert(ctx);
    assert(node);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !ctx->ui_contributions_registered || !ctx->def->ui_contributions )
        return 0;

    wanted = ToriRS_UiRegistry_PrivateRef(&ctx->host->ui_registry, ctx->name, node);
    if( wanted.value == 0 )
        return 0;
    for( int i = 0; i < ctx->ui_contribution_count; i++ )
    {
        struct ToriRS_UiNodeRef const declared = ToriRS_UiRegistry_PrivateRef(
            &ctx->host->ui_registry, ctx->name, ctx->def->ui_contributions[i].node);

        if( declared.value != wanted.value ||
            !ToriRS_UiRegistry_ContributionStatus(
                &ctx->host->ui_registry, ctx->ui_contribution_refs[i], out) )
            continue;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------- canonical UI */

static char const*
plugin_ui_base_parent(char const* name)
{
    assert(name);
    if( strcmp(name, "frame.minimap.housing") == 0 ||
        strcmp(name, "frame.compass") == 0 )
        return "frame.minimap";
    if( strcmp(name, "frame.chat.buttons") == 0 )
        return "frame.chat";
    if( strncmp(name, "frame.chat.button.", 18) == 0 )
        return "frame.chat.buttons";
    if( strcmp(name, "frame.sidebar.rail") == 0 ||
        strncmp(name, "frame.sidebar.tab.", 18) == 0 )
        return "frame.sidebar";
    if( strncmp(name, "frame.orb.", 10) == 0 )
        return "frame.orbs";
    if( strcmp(name, "frame.minimap") == 0 || strcmp(name, "frame.chat") == 0 ||
        strcmp(name, "frame.sidebar") == 0 || strcmp(name, "frame.modal") == 0 ||
        strcmp(name, "frame.orbs") == 0 || strcmp(name, "frame.xp.drops") == 0 )
        return "frame.viewport";
    return NULL;
}

static int
plugin_ui_base_add_rect(
    struct ToriRS_UiBaseDeclaration* declarations,
    int count,
    int capacity,
    char const* provider,
    char const* name,
    int x,
    int y,
    int w,
    int h,
    uint32_t flags)
{
    struct ToriRS_UiBaseDeclaration* declaration;

    assert(declarations);
    assert(count >= 0);
    assert(capacity > 0);
    assert(provider);
    assert(name);
    if( w <= 0 || h <= 0 || count >= capacity )
        return count;

    declaration = &declarations[count++];
    memset(declaration, 0, sizeof(*declaration));
    declaration->provider = provider;
    declaration->node = name;
    declaration->facets = TORIRS_UI_FACET_ALL;
    declaration->value.struct_size = sizeof(declaration->value);
    declaration->value.parent = plugin_ui_base_parent(name);
    declaration->value.bounds.x = x;
    declaration->value.bounds.y = y;
    declaration->value.bounds.width = w;
    declaration->value.bounds.height = h;
    declaration->value.anchor = TORIRS_ANCHOR_TOP_LEFT;
    declaration->value.paint_order = TORIRS_UI_PAINT_AFTER_PARENT;
    declaration->value.flags = flags | TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED;
    return count;
}

static void
plugin_ui_refresh_base(struct ToriRS_PluginHost* host)
{
    static struct
    {
        int slot;
        char const* name;
        uint32_t flags;
    } const SURFACE[] = {
        { TORIRS_HOST_SURFACE_VIEWPORT,     "frame.viewport",     0                             },
        { TORIRS_HOST_SURFACE_MINIMAP,      "frame.minimap",      TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { TORIRS_HOST_SURFACE_COMPASS,      "frame.compass",      TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { TORIRS_HOST_SURFACE_CHAT,         "frame.chat",         TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { TORIRS_HOST_SURFACE_CHAT_BUTTONS, "frame.chat.buttons", TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { TORIRS_HOST_SURFACE_SIDEBAR,      "frame.sidebar",      TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { TORIRS_HOST_SURFACE_MODAL,   "frame.modal",        0                             },
        { TORIRS_HOST_SURFACE_ORBS,         "frame.orbs",         TORIRS_UI_NODE_BLOCKS_OVERLAY },
    };
    static char const* const CHAT_BUTTON[] = {
        "frame.chat.button.public",
        "frame.chat.button.private",
        "frame.chat.button.trade",
        "frame.chat.button.report",
    };
    static struct
    {
        char const* role;
        char const* canonical;
        uint32_t flags;
    } const ROLE[] = {
        { "minimap_edge",  "frame.minimap.housing", TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { "orb_hitpoints", "frame.orb.hitpoints",   TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { "orb_prayer",    "frame.orb.prayer",      TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { "orb_run",       "frame.orb.run",         TORIRS_UI_NODE_BLOCKS_OVERLAY },
        { "orb_spec",      "frame.orb.special",     TORIRS_UI_NODE_BLOCKS_OVERLAY },
    };
    struct ToriRS_UiBaseDeclaration declarations[
        TORIRS_PLUGIN_V2_FRAME_NAMED_NODES_MAX + 48];
    char dynamic_names[15][TORIRS_UI_NAME_MAX];
    char profile_role[TORIRS_PLUGIN_ROLE_NAME_MAX];
    char const* provider;
    uint32_t tab_enabled_mask = 0;
    int active_tab = -1;
    int count = 0;

    assert(host);
    if( host->engine.screen(host->engine.user) != TORIRS_SCREEN_GAME )
    {
        host->ui_tab_state_valid = false;
        ToriRS_UiRegistry_ClearBase(&host->ui_registry);
        return;
    }
    provider = host->frame_selection.active_id[0] ? host->frame_selection.active_id : "core/native";
    if( host->engine.tab_active )
        active_tab = host->engine.tab_active(host->engine.user);
    for( int tab = 0; tab < 14; tab++ )
        if( !host->engine.tab_enabled ||
            host->engine.tab_enabled(host->engine.user, tab) )
            tab_enabled_mask |= 1u << tab;
    host->ui_tab_active = active_tab;
    host->ui_tab_enabled_mask = tab_enabled_mask;
    host->ui_tab_state_valid = true;

    for( size_t i = 0; i < sizeof(SURFACE) / sizeof(SURFACE[0]); i++ )
    {
        int x, y, w, h;
        int present = host->engine.slot_rect(
            host->engine.user, SURFACE[i].slot, &x, &y, &w, &h);
        if( !present && SURFACE[i].slot == TORIRS_HOST_SURFACE_VIEWPORT )
            present = host->engine.slot_rect(
                host->engine.user, TORIRS_HOST_SURFACE_CANVAS, &x, &y, &w, &h);
        if( present )
            count = plugin_ui_base_add_rect(
                declarations,
                count,
                (int)(sizeof(declarations) / sizeof(declarations[0])),
                provider,
                SURFACE[i].name,
                x,
                y,
                w,
                h,
                SURFACE[i].flags);
    }

    for( int i = 0; i < (int)(sizeof(CHAT_BUTTON) / sizeof(CHAT_BUTTON[0])); i++ )
    {
        int x, y, w, h;
        if( host->engine.slot_member_rect(
                host->engine.user, TORIRS_HOST_SURFACE_CHAT_BUTTONS, i, &x, &y, &w, &h) )
            count = plugin_ui_base_add_rect(
                declarations,
                count,
                (int)(sizeof(declarations) / sizeof(declarations[0])),
                provider,
                CHAT_BUTTON[i],
                x,
                y,
                w,
                h,
                TORIRS_UI_NODE_BLOCKS_OVERLAY);
    }

    for( size_t i = 0; i < sizeof(ROLE) / sizeof(ROLE[0]); i++ )
    {
        int x, y, w, h;
        if( host->engine.role_rect(host->engine.user, ROLE[i].role, &x, &y, &w, &h) &&
            host->engine.role_visible(host->engine.user, ROLE[i].role) )
            count = plugin_ui_base_add_rect(
                declarations,
                count,
                (int)(sizeof(declarations) / sizeof(declarations[0])),
                provider,
                ROLE[i].canonical,
                x,
                y,
                w,
                h,
                ROLE[i].flags);
    }

    /* The lane-owned popout rail is the first frame-blocking node. Additional
     * RevConfig occluders migrate to named metadata rather than extending a
     * numbered public API; this profile read keeps today's profiles. */
    {
        int x, y, w, h;
        if( host->engine.role_rect(host->engine.user, "lane_chrome_0", &x, &y, &w, &h) &&
            host->engine.role_visible(host->engine.user, "lane_chrome_0") )
            count = plugin_ui_base_add_rect(
                declarations,
                count,
                (int)(sizeof(declarations) / sizeof(declarations[0])),
                "core/native",
                "frame.sidebar.rail",
                x,
                y,
                w,
                h,
                TORIRS_UI_NODE_BLOCKS_FRAME | TORIRS_UI_NODE_BLOCKS_OVERLAY);
    }

    for( int i = 0; i < 14; i++ )
    {
        int x, y, w, h;
        int const before = count;
        snprintf(profile_role, sizeof(profile_role), "sidetab_%d", i);
        if( !host->engine.role_rect(host->engine.user, profile_role, &x, &y, &w, &h) )
            continue;
        snprintf(dynamic_names[i], sizeof(dynamic_names[i]), "frame.sidebar.tab.%d", i);
        count = plugin_ui_base_add_rect(
            declarations,
            count,
            (int)(sizeof(declarations) / sizeof(declarations[0])),
            provider,
            dynamic_names[i],
            x,
            y,
            w,
            h,
            TORIRS_UI_NODE_BLOCKS_OVERLAY);
        if( count > before )
        {
            if( !(tab_enabled_mask & (1u << i)) )
                declarations[count - 1].value.flags &= ~TORIRS_UI_NODE_ENABLED;
            if( active_tab == i )
                declarations[count - 1].value.flags |= TORIRS_UI_NODE_ACTIVE;
        }
    }

    /* A v2 frame can publish semantic furniture directly. It replaces the
     * profile-derived row inferred from the profile role/surface when both name the
     * same object, and otherwise joins the same atomic base transaction. */
    {
        int const owner = plugin_frame_owner(host);
        struct PluginV2Instance* v2 = owner >= 0 ? host->plugins[owner].v2 : NULL;
        if( v2 )
            for( int i = 0; i < v2->frame_ui_count; i++ )
            {
                struct PluginV2FrameNode* source = &v2->frame_ui[i];
                int destination = -1;
                uint32_t runtime_flags = 0;
                bool had_runtime = false;

                for( int j = 0; j < count; j++ )
                    if( strcmp(declarations[j].node, source->name) == 0 )
                    {
                        destination = j;
                        runtime_flags = declarations[j].value.flags &
                                        (TORIRS_UI_NODE_VISIBLE |
                                         TORIRS_UI_NODE_ENABLED |
                                         TORIRS_UI_NODE_ACTIVE);
                        had_runtime = true;
                        break;
                    }
                if( destination < 0 )
                {
                    if( count >= (int)(sizeof(declarations) / sizeof(declarations[0])) )
                    {
                        TORIRS_REPORT("plugin: v2 frame named-UI declaration budget exhausted\n");
                        break;
                    }
                    destination = count++;
                }
                declarations[destination] = (struct ToriRS_UiBaseDeclaration){
                    .provider = provider,
                    .node = source->name,
                    .facets = TORIRS_UI_FACET_ALL,
                    .value = source->value,
                };
                /* Tab availability/selection is live lane state, not frame
                 * build state. Preserve it while replacing geometry/art. */
                if( strncmp(source->name, "frame.sidebar.tab.", 18) == 0 )
                {
                    char* end = NULL;
                    long const tab = strtol(source->name + 18, &end, 10);
                    declarations[destination].value.flags &=
                        ~(uint32_t)(TORIRS_UI_NODE_ENABLED |
                                    TORIRS_UI_NODE_ACTIVE);
                    if( end && !*end && tab >= 0 && tab < 14 )
                    {
                        if( tab_enabled_mask & (1u << tab) )
                            declarations[destination].value.flags |=
                                TORIRS_UI_NODE_ENABLED;
                        if( active_tab == tab )
                            declarations[destination].value.flags |=
                                TORIRS_UI_NODE_ACTIVE;
                    }
                    if( had_runtime )
                    {
                        declarations[destination].value.flags &=
                            ~TORIRS_UI_NODE_VISIBLE;
                        declarations[destination].value.flags |=
                            runtime_flags & TORIRS_UI_NODE_VISIBLE;
                    }
                }
            }
    }

    if( ToriRS_UiRegistry_ReplaceBase(&host->ui_registry, declarations, count) !=
        TORIRS_UI_REGISTRY_OK )
        TORIRS_REPORT("plugin: rejected the resolved named-UI base; keeping the previous tree\n");
}

/** Publish sidebar ACTIVE/ENABLED state only when the lane's live answer moves. */
static void
plugin_ui_tab_state_poll(struct ToriRS_PluginHost* host)
{
    uint32_t enabled_mask = 0;
    int active = -1;

    assert(host);
    if( host->engine.screen(host->engine.user) != TORIRS_SCREEN_GAME )
        return;
    if( host->engine.tab_active )
        active = host->engine.tab_active(host->engine.user);
    for( int tab = 0; tab < 14; tab++ )
        if( !host->engine.tab_enabled ||
            host->engine.tab_enabled(host->engine.user, tab) )
            enabled_mask |= 1u << tab;
    if( !host->ui_tab_state_valid || host->ui_tab_active != active ||
        host->ui_tab_enabled_mask != enabled_mask )
        plugin_ui_refresh_base(host);
}

/**
 * Does the GAMEFRAME exist right now?
 *
 * Every verb below that answers a question about the frame -- by role name, by
 * slot, by component id -- answers "no such thing" when it does not, rather
 * than resolving the name against whatever tree happens to be up.
 *
 * The title screen has a tree and components, some with names also used by a
 * gameframe. A plugin that asks for a rectangle by
 * name and is handed one has no way to tell that it belongs to the login box
 * rather than to the chat frame, so the login screen could receive furniture
 * for a screen nobody is on. "Ask for things by
 * name" is only safe while the names mean what the asker thinks they mean, and
 * off the gameframe they do not.
 *
 * A refusal, not an abort: a plugin polling role_rect every frame across a
 * logout is doing nothing wrong, and this is the answer it should get.
 */
static bool
host_game_screen(struct PluginContext const* ctx)
{
    assert(ctx);
    return ctx->host->engine.screen(ctx->host->engine.user) == TORIRS_SCREEN_GAME;
}

static int
api_slot_native_size(
    struct PluginContext* ctx,
    int slot,
    int* out_w,
    int* out_h)
{
    int w = 0, h = 0;

    if( !host_game_screen(ctx) )
        return 0;

    assert(ctx);
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        return 0;
    if( !ctx->host->engine.slot_native_size(ctx->host->engine.user, slot, &w, &h) )
        return 0;
    if( w <= 0 || h <= 0 )
        return 0;
    if( out_w )
        *out_w = w;
    if( out_h )
        *out_h = h;
    return 1;
}

/*
 * Where a component is. @see component_rect.
 *
 * No range test on the id, unlike the region verbs above: every 32-bit value
 * is a well-formed component id, and "the tree has no such component" is the
 * engine's answer to all of them.
 */
static int
api_component_rect(
    struct PluginContext* ctx,
    int component_id,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;

    if( !host_game_screen(ctx) )
        return 0;

    assert(ctx);

    if( !ctx->host->engine.component_rect(ctx->host->engine.user, component_id, &x, &y, &w, &h) )
        return 0;
    if( w <= 0 || h <= 0 )
        return 0;

    if( out_x )
        *out_x = x;
    if( out_y )
        *out_y = y;
    if( out_w )
        *out_w = w;
    if( out_h )
        *out_h = h;
    return 1;
}

static void
plugin_placement_reservations_drop_plugin(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    int dropped = 0;

    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        struct PluginPlacementReservation* reservation = &host->placement_reservations[i];
        if( reservation->plugin != plugin )
            continue;
        memset(reservation, 0, sizeof(*reservation));
        reservation->plugin = -1;
        dropped = 1;
    }
    if( dropped )
        PluginHost_LayoutChanged(host);
}

static int
api_stat(
    struct PluginContext* ctx,
    int skill,
    int* out_current,
    int* out_base)
{
    assert(ctx);
    /* `skill` is a NUMBER a plugin computed or read out of its own config, so
     * an out-of-range one is bad input rather than a broken contract: the
     * engine answers 0 and leaves the outs alone. */
    return ctx->host->engine.stat(ctx->host->engine.user, skill, out_current, out_base);
}

static int
api_stat_xp(
    struct PluginContext* ctx,
    int skill,
    int* out_xp,
    int* out_level_xp,
    int* out_next_xp)
{
    assert(ctx);
    /* Out of range is bad input rather than a broken contract, for the reason
     * api_stat says: the number came from the plugin. */
    return ctx->host->engine.stat_xp(
        ctx->host->engine.user, skill, out_xp, out_level_xp, out_next_xp);
}

static char const*
api_skill_name(
    struct PluginContext* ctx,
    int skill)
{
    assert(ctx);
    return ctx->host->engine.skill_name(ctx->host->engine.user, skill);
}

static int
api_run_energy(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.run_energy(ctx->host->engine.user);
}

static int
api_project(
    struct PluginContext* ctx,
    int fine_x,
    int fine_z,
    int height,
    int* out_x,
    int* out_y)
{
    assert(ctx);
    assert(out_x);
    assert(out_y);
    return ctx->host->engine.project(ctx->host->engine.user, fine_x, fine_z, height, out_x, out_y);
}

/* -- config accessors -- */

char const*
PluginHost_ConfigGet(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    char const* key)
{
    assert(host);
    assert(key);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);

    struct PluginContext const* ctx = &host->plugins[plugin_index];
    for( int i = 0; i < ctx->config_count; i++ )
    {
        if( strcmp(ctx->config[i].key, key) == 0 )
            return ctx->config[i].value;
    }
    return NULL;
}

static int
api_cfg_has(
    struct PluginContext* ctx,
    char const* key)
{
    assert(ctx);
    assert(key);
    return PluginHost_ConfigGet(ctx->host, ctx->index, key) != NULL;
}

static char const*
api_cfg_str(
    struct PluginContext* ctx,
    char const* key)
{
    assert(ctx);
    assert(key);

    char const* value = PluginHost_ConfigGet(ctx->host, ctx->index, key);
    /* An unknown key is a plugin bug -- it did not declare what it reads --
     * and returning "" would hide it behind a plausible default. */
    assert(value);
    return value;
}

/**
 * A stored value as a number, in revconfig's expression grammar.
 *
 * The same grammar a revconfig profile's numeric keys are in, and for the same
 * reason: plugin_prefs.ini is a file people edit by hand, and the spellings
 * they reach for are the ones the reference and their other clients use --
 * `#FF0000`, `rgb(255, 0, 0)`, `0x8000`, `1 << 4`, `hsl16(0, 7, 64)`. atoi()
 * read every one of those as 0, silently, which is a colour (black) and a
 * plausible id, so a mistyped value looked like a setting that did nothing.
 *
 * @return `fallback` when the value is not one whole expression. Silent,
 * deliberately: a colour key is read on the draw path, so a broken one would
 * print once per frame forever.
 */
static int
plugin_cfg_number(
    char const* value,
    int fallback)
{
    char const* end = NULL;
    int parsed = 0;

    assert(value);

    if( !revconfig_parse_int_expr(value, &end, &parsed) )
        return fallback;
    /* One expression and nothing after it -- "5 apples" is a typo, not a 5. */
    while( *end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' )
        end++;
    if( *end != '\0' )
        return fallback;
    return parsed;
}

static int
api_cfg_bool(
    struct PluginContext* ctx,
    char const* key)
{
    char const* value = api_cfg_str(ctx, key);
    if( value[0] == '\0' )
        return 0;
    if( strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 )
        return 1;
    return plugin_cfg_number(value, 0) != 0;
}

static int
api_cfg_int(
    struct PluginContext* ctx,
    char const* key)
{
    return plugin_cfg_number(api_cfg_str(ctx, key), 0);
}

/**
 * A colour key as 0xRRGGBB.
 *
 * Masked to 24 bits, which is the api's contract and not an oversight: the
 * plugins that blit one supply their own alpha, and rgba()'s fourth channel
 * would arrive here as a top byte they would then have to strip. A plugin that
 * wants the packed ARGB word reads the same key with cfg_int.
 */
static uint32_t
api_cfg_color(
    struct PluginContext* ctx,
    char const* key)
{
    return (uint32_t)plugin_cfg_number(api_cfg_str(ctx, key), 0) & 0xffffffu;
}

bool
PluginHost_ConfigSet(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* key,
    char const* value)
{
    assert(host);
    assert(key);
    assert(value);

    struct PluginContext* ctx = plugin_at(host, plugin_index);
    if( !plugin_config_key_valid(key) || !plugin_config_value_valid(value) )
        return false;
    struct PluginConfigSlot* slot = plugin_config_slot(ctx, key, true);
    if( !slot )
        return false;
    if( strcmp(slot->value, value) == 0 )
        return true;

    snprintf(slot->value, sizeof(slot->value), "%s", value);
    host->config_dirty = true;

    if( ctx->enabled && ctx->running )
    {
        plugin_dispatch_one(
            host, plugin_index, PLUGIN_CALLBACK_CONFIG_CHANGED, slot->key);
    }
    return true;
}

static bool
api_cfg_set(
    struct PluginContext* ctx,
    char const* key,
    char const* value)
{
    assert(ctx);
    assert(key);
    assert(value);
    return PluginHost_ConfigSet(ctx->host, ctx->index, key, value);
}

/* -- the client's own variables -- */

static int
api_varbit(
    struct PluginContext* ctx,
    int varbit_id)
{
    assert(ctx);
    return ctx->host->engine.varbit(ctx->host->engine.user, varbit_id);
}

static int
api_feature_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_FeatureInfo* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.feature_next(ctx->host->engine.user, iter, out);
}

static int
api_feature_get(
    struct PluginContext* ctx,
    char const* key)
{
    assert(ctx);
    assert(key);
    return ctx->host->engine.feature_get(ctx->host->engine.user, key);
}

static bool
api_feature_set(
    struct PluginContext* ctx,
    char const* key,
    int value)
{
    assert(ctx);
    assert(key);
    return ctx->host->engine.feature_set(ctx->host->engine.user, key, value) != 0;
}

static int
api_display_setting(
    struct PluginContext* ctx,
    int setting,
    int* out_value,
    int* out_min,
    int* out_max)
{
    assert(ctx);
    /* A number a plugin computed, so out of range is input rather than a
     * broken contract -- and the answer it gets is the same one a build
     * without that setting gives, which is the answer a page should render
     * the same way either way. */
    if( setting < 0 || setting >= TORIRS_DISPLAY_SETTING_COUNT )
        return 0;
    return ctx->host->engine.display_setting(
        ctx->host->engine.user, setting, out_value, out_min, out_max);
}

static int
api_display_setting_set(
    struct PluginContext* ctx,
    int setting,
    int value)
{
    assert(ctx);
    if( setting < 0 || setting >= TORIRS_DISPLAY_SETTING_COUNT )
        return 0;
    return ctx->host->engine.display_setting_set(ctx->host->engine.user, setting, value);
}

static int
api_varp(
    struct PluginContext* ctx,
    int varp_id)
{
    assert(ctx);
    return ctx->host->engine.varp(ctx->host->engine.user, varp_id);
}

static int
api_cache_id(
    struct PluginContext* ctx,
    char const* kind,
    char const* name)
{
    assert(ctx);
    assert(kind);
    assert(name);
    if( !ctx->host->engine.cache_id )
        return -1;
    return ctx->host->engine.cache_id(ctx->host->engine.user, kind, name);
}

static int
api_lane(
    struct PluginContext* ctx,
    struct ToriRS_LaneInfo* out)
{
    assert(ctx);
    assert(out);

    memset(out, 0, sizeof(*out));
    /* A build with no lane seam answers UNKNOWN rather than refusing the call:
     * every field already holds the "nothing has said" value, so a plugin
     * reads the same thing here as on a boot whose identity has not landed. */
    if( !ctx->host->engine.lane )
        return 0;
    return ctx->host->engine.lane(ctx->host->engine.user, out);
}

static int
api_frame_root(struct PluginContext* ctx)
{
    assert(ctx);
    /* Same shape as lane: a harness that has no gameframe answers "not a
     * cache frame" rather than refusing the call. */
    if( !ctx->host->engine.frame_root )
        return -1;
    return ctx->host->engine.frame_root(ctx->host->engine.user);
}

/*
 * A plugin standing down.
 *
 * The teardown is PluginHost_SetEnabled's, and what it does NOT do is the
 * point: `enabled` is left alone and the store is not marked dirty, so the
 * user's saved switch survives a lane that cannot run the plugin.
 */
static void
api_disable_self(
    struct PluginContext* ctx,
    char const* reason)
{
    struct ToriRS_PluginHost* host;
    int prev;

    assert(ctx);
    assert(reason);
    /* An essential plugin has one state -- the roster draws no switch for it
     * and SetEnabled refuses to clear it -- so a def that declares itself
     * essential and then stands down is that plugin's own bug. */
    assert(!plugin_policy(ctx, TORIRS_PLUGIN_V2_ESSENTIAL));

    host = ctx->host;
    /* Idempotent: a plugin that says so twice, or from a second handler that
     * runs after the one that already did, is not deciding twice. */
    if( ctx->refused )
        return;

    PluginHost_SetError(host, ctx->index, reason);
    /*
     * Saved and restored around the teardown, which ends by clearing it.
     *
     * This is called from inside a dispatch -- init, or a handler -- and the
     * caller goes on running after it returns. Leaving the host with nobody
     * dispatching would make every api verb the rest of that handler tried
     * answer for the wrong plugin.
     */
    prev = host->dispatching;
    plugin_teardown(host, ctx->index);
    host->dispatching = prev;
    ctx->refused = true;
    if( plugin_provides_frames(ctx) )
    {
        PluginFrameCatalog_SetAvailable(&host->frame_catalog, ctx->index, 0);
        host->frame_selection_dirty = 1;
    }
}

static int
api_obj_info(
    struct PluginContext* ctx,
    int obj_id,
    struct ToriRS_ItemInfo* out)
{
    assert(ctx);
    assert(out);
    /* An id the plugin computed, so out of range is bad input rather than a
     * broken contract -- the engine answers 0 and leaves `out` alone. */
    return ctx->host->engine.obj_info(ctx->host->engine.user, obj_id, out);
}

static int
api_inv_slot(
    struct PluginContext* ctx,
    int inv,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    assert(ctx);
    return ctx->host->engine.inv_slot(ctx->host->engine.user, inv, slot, out_obj_id, out_count);
}

static int
api_inv_size(
    struct PluginContext* ctx,
    int inv)
{
    assert(ctx);
    return ctx->host->engine.inv_size(ctx->host->engine.user, inv);
}

static uint32_t
api_setting_color(
    struct PluginContext* ctx,
    int varp_id,
    uint32_t fallback)
{
    int stored;

    assert(ctx);
    /* `varp - 1`, and zero means nobody has chosen: see the api declaration
     * for why the cache stores it offset. A value that survives the offset but
     * is not a colour cannot happen -- the picker writes 24 bits -- so the
     * mask is belt and braces against a var this client mis-decoded rather
     * than against the panel. */
    stored = ctx->host->engine.varp(ctx->host->engine.user, varp_id);
    if( stored <= 0 )
        return fallback;
    return (uint32_t)(stored - 1) & 0x00FFFFFFu;
}

/* -- menu -- */

static int
api_menu_add(
    struct PluginContext* ctx,
    struct ToriRS_MenuBuildEvent* menu,
    char const* text,
    uint32_t tag)
{
    (void)menu;
    assert(ctx);
    assert(menu);
    assert(text);

    struct ToriRS_PluginHost* host = ctx->host;
    /* Calling this outside a menu build is a contract violation, not a
     * tolerated no-op: the row would silently go nowhere. */
    assert(host->menu_cursor);

    if( host->route_count >= TORIRS_PLUGIN_MENU_ROUTES_MAX )
        return 0;

    int const action = PLUGIN_MENU_ACTION_BASE + host->route_count;
    if( !host->engine.menu_add(host->engine.user, host->menu_cursor, text, action) )
        return 0;

    struct PluginMenuRoute* route = &host->routes[host->route_count++];
    route->action = action;
    route->plugin = ctx->index;
    route->tag = tag;
    return 1;
}

/* -- ground items -- */

static int
api_obj_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_GroundItemSnapshot* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.obj_next(ctx->host->engine.user, iter, out);
}

/* -- assets -- */

/*
 * An asset name is a bare filename in the plugin's own namespace.
 *
 * The check is a refusal and not an assert, unlike every other bad argument
 * here, because this one does not come from a programmer: it comes from a
 * script, at runtime, possibly from a string the user typed into a config
 * field. Aborting the client over a typo in someone's Lua is a worse outcome
 * than saying no and logging why -- and the api already models "no", because
 * a missing file has to be reportable anyway.
 */
static bool
plugin_asset_name_ok(
    struct PluginContext* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);

    size_t const len = strlen(name);
    bool ok = len > 0 && len < TORIRS_PLUGIN_ASSET_NAME_MAX;

    for( size_t i = 0; ok && i < len; i++ )
    {
        char const c = name[i];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '.' || c == '_' || c == '-';
    }
    /* Rejected by the character set above, but named separately so the log
     * says what was actually wrong with it. */
    if( ok && strstr(name, "..") )
        ok = false;

    if( !ok )
        TORIRS_ERR(
            "plugin: %s asked for asset '%s'; asset names are bare filenames of "
            "[A-Za-z0-9._-] with no '..', so that one is refused\n",
            ctx->name,
            name);
    return ok;
}

static struct PluginAsset*
plugin_asset_find(
    struct ToriRS_PluginHost* host,
    int plugin,
    char const* name)
{
    assert(host);
    assert(name);
    for( int i = 0; i < host->asset_count; i++ )
    {
        if( host->assets[i].plugin == plugin && strcmp(host->assets[i].name, name) == 0 )
            return &host->assets[i];
    }
    return NULL;
}

/* Free one slot's bytes and compact the table over it. */
static void
plugin_asset_drop(
    struct ToriRS_PluginHost* host,
    struct PluginAsset* slot)
{
    assert(host);
    assert(slot);

    free(slot->data);
    int const at = (int)(slot - host->assets);
    for( int i = at; i < host->asset_count - 1; i++ )
        host->assets[i] = host->assets[i + 1];
    host->asset_count--;
    memset(&host->assets[host->asset_count], 0, sizeof(host->assets[0]));
}

static struct PluginAsset*
plugin_asset_claim(
    struct ToriRS_PluginHost* host,
    int plugin,
    char const* name)
{
    assert(host);
    assert(name);

    struct PluginAsset* slot = plugin_asset_find(host, plugin, name);
    if( slot )
        return slot;
    if( host->asset_count >= TORIRS_PLUGIN_ASSETS_MAX )
        return NULL;

    slot = &host->assets[host->asset_count++];
    memset(slot, 0, sizeof(*slot));
    slot->plugin = plugin;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    return slot;
}

static void
plugin_assets_drop_plugin(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    assert(host);
    for( int i = host->asset_count - 1; i >= 0; i-- )
    {
        if( host->assets[i].plugin == plugin )
            plugin_asset_drop(host, &host->assets[i]);
    }
}

/* -- images -------------------------------------------------------------- */

/**
 * The slot `image` names, if this plugin owns it.
 *
 * A handle is an index into a table every plugin shares, so "is this mine" is
 * a question that has to be asked on every use -- a plugin holding a stale
 * handle across a reload would otherwise be drawing, releasing and resizing
 * another plugin's art. Complained about rather than asserted for the same
 * reason an asset name is: a handle can arrive from a script.
 *
 * @return NULL when the handle is out of range or belongs to someone else.
 */
static struct PluginImage*
plugin_image_owned(
    struct PluginContext* ctx,
    int image)
{
    assert(ctx);

    if( image < 0 || image >= TORIRS_PLUGIN_IMAGES_MAX )
        return NULL;
    if( ctx->host->images[image].plugin != ctx->index )
    {
        TORIRS_LOG("plugin: %s used image handle %d, which it does not own\n", ctx->name, image);
        return NULL;
    }
    return &ctx->host->images[image];
}

/* Read operations obey the same ownership rule as writes/releases. */
static struct PluginImage const*
plugin_image_readable(
    struct PluginContext* ctx,
    int image)
{
    assert(ctx);
    return plugin_image_owned(ctx, image);
}

static void
plugin_image_drop(
    struct ToriRS_PluginHost* host,
    int image)
{
    assert(host);
    assert(image >= 0 && image < TORIRS_PLUGIN_IMAGES_MAX);

    if( host->images[image].published )
        host->engine.image_release(host->engine.user, image);
    memset(&host->images[image], 0, sizeof(host->images[image]));
    host->images[image].plugin = -1;
}

static void
plugin_model_drop(
    struct ToriRS_PluginHost* host,
    int model)
{
    assert(host);
    assert(model >= 0 && model < TORIRS_PLUGIN_MODELS_MAX);

    if( host->models[model].published )
        host->engine.model_release(host->engine.user, model);
    memset(&host->models[model], 0, sizeof(host->models[model]));
    host->models[model].plugin = -1;
}

static void
plugin_models_drop_plugin(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
        if( host->models[i].plugin == plugin )
            plugin_model_drop(host, i);
}

/**
 * Hand a slot's bytes to the engine to decode.
 *
 * A file that will not decode is reported and left unpublished, exactly as an
 * undecodable image is: the plugin named the file, and "that is not a model
 * this client reads" is an answer it has to be able to get. The handle stays
 * valid and every object standing on it simply is not in the scene, which is
 * the same state a pending load leaves them in.
 */
static void
plugin_model_publish(
    struct ToriRS_PluginHost* host,
    int model,
    void const* data,
    int size)
{
    struct PluginModel* slot;

    assert(host);
    assert(model >= 0 && model < TORIRS_PLUGIN_MODELS_MAX);
    assert(data);

    slot = &host->models[model];
    if( !host->engine.model_publish(host->engine.user, model, data, size) )
    {
        TORIRS_LOG(
            "plugin: %s model '%s' would not decode (%d bytes); it draws nothing\n",
            host->plugins[slot->plugin].name,
            slot->asset,
            size);
        return;
    }
    slot->published = true;
}

static void
plugin_images_drop_plugin(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
        if( host->images[i].plugin == plugin )
            plugin_image_drop(host, i);
}

/* ---------------------------------------------------------- the icon cache */

/** Forget one cached icon and free the image slot behind it. Idempotent. */
static void
plugin_obj_icon_drop(
    struct ToriRS_PluginHost* host,
    int entry)
{
    struct PluginObjIcon* row;

    assert(host);
    assert(entry >= 0);
    assert(entry < TORIRS_PLUGIN_OBJ_ICONS_MAX);

    row = &host->obj_icons[entry];
    if( row->image >= 0 )
        plugin_image_drop(host, row->image);
    memset(row, 0, sizeof(*row));
    row->obj_id = -1;
    row->plugin = -1;
    row->image = -1;
}

/**
 * Every icon a plugin was holding, dropped with it.
 *
 * The picture is shared but the SLOT is not -- draw_image resolves a handle
 * against the plugin that owns the slot -- so a stopped plugin's entries have
 * to go, or the image table leaks a quarter of itself per reload.
 */
static void
plugin_obj_icons_drop_plugin(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_OBJ_ICONS_MAX; i++ )
        if( host->obj_icons[i].plugin == plugin )
            plugin_obj_icon_drop(host, i);
}

/** Is this handle one of the icon cache's, rather than the plugin's own? */
static bool
plugin_obj_icon_owns(
    struct ToriRS_PluginHost const* host,
    int image)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_OBJ_ICONS_MAX; i++ )
        if( host->obj_icons[i].plugin >= 0 && host->obj_icons[i].image == image )
            return true;
    return false;
}

/**
 * The client's inventory icon for one item, cached.
 *
 * Three outcomes, and the middle one is the ordinary state rather than an
 * error: a HIT is touched and handed back, a MISS that the engine can build is
 * rasterised into a fresh (or evicted) slot, and a miss the engine cannot
 * build yet -- the objtype or its inventory model is still coming off the
 * cache -- answers -1 so the caller asks again next frame. That is obj_info's
 * contract, and for obj_info's reason: an api verb inside a frame must not
 * start IO and stall it.
 */
static int
api_loot_source_next(
    struct PluginContext* ctx,
    int iter,
    struct ToriRS_LootSource* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.loot_source_next(ctx->host->engine.user, iter, out);
}

static int
api_loot_row_next(
    struct PluginContext* ctx,
    int source_id,
    int iter,
    struct ToriRS_LootRow* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.loot_row_next(ctx->host->engine.user, source_id, iter, out);
}

static int
api_obj_image(
    struct PluginContext* ctx,
    int obj_id,
    int count,
    int style)
{
    struct ToriRS_PluginHost* host;
    int free_entry = -1;
    int victim = -1;
    int free_image = -1;
    int w = 0;
    int h = 0;

    assert(ctx);

    host = ctx->host;
    /* An id and a count are NUMBERS the plugin computed -- off a drop table,
     * out of a container -- so a silly one is bad input rather than a bug in
     * the caller's frame, and the honest answer is "there is no such icon". */
    if( obj_id < 0 || count < 0 || style < 0 || style > TORIRS_ITEM_ICON_SELECTED )
        return -1;
    if( count == 0 )
        count = 1;

    host->icon_clock++;

    for( int i = 0; i < TORIRS_PLUGIN_OBJ_ICONS_MAX; i++ )
    {
        struct PluginObjIcon* row = &host->obj_icons[i];

        if( row->plugin < 0 )
        {
            if( free_entry < 0 )
                free_entry = i;
            continue;
        }
        if( row->plugin == ctx->index && row->obj_id == obj_id && row->count == count &&
            row->style == style )
        {
            row->used = host->icon_clock;
            return row->image;
        }
        if( victim < 0 || row->used < host->obj_icons[victim].used )
            victim = i;
    }

    /*
     * A slot to render into. The cache prefers a free image slot, and takes
     * its own least-recently-used entry's when there is none -- which is what
     * makes the ceiling a cache size rather than a wall a plugin hits and
     * stops drawing at.
     */
    if( free_entry < 0 )
    {
        assert(victim >= 0);
        plugin_obj_icon_drop(host, victim);
        free_entry = victim;
    }
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
        if( host->images[i].plugin < 0 )
        {
            free_image = i;
            break;
        }
    if( free_image < 0 )
    {
        /* The resident image table is full of pictures nothing here owns, so
         * there is nothing to evict that would help. Said out loud once per
         * call is too often; this is the same shape as the compose path's
         * message and is rare enough to be worth seeing at all. */
        TORIRS_LOG(
            "plugin: %s obj icon %d not built, the resident image table is full "
            "(%d)\n",
            ctx->name,
            obj_id,
            TORIRS_PLUGIN_IMAGES_MAX);
        return -1;
    }

    if( !host->engine.obj_image(host->engine.user, free_image, obj_id, count, style, &w, &h) )
        return -1;

    host->images[free_image].plugin = ctx->index;
    /*
     * A synthetic name no asset_load could produce -- the sandbox refuses a
     * ':' -- so the (plugin, name) search image_load and image_compose do can
     * never match a cached icon and hand a plugin's own art this slot.
     */
    snprintf(
        host->images[free_image].asset,
        sizeof(host->images[free_image].asset),
        "obj:%d:%d:%d",
        obj_id,
        count,
        style);
    host->images[free_image].width = w;
    host->images[free_image].height = h;
    host->images[free_image].published = true;

    host->obj_icons[free_entry].obj_id = obj_id;
    host->obj_icons[free_entry].count = count;
    host->obj_icons[free_entry].style = style;
    host->obj_icons[free_entry].plugin = ctx->index;
    host->obj_icons[free_entry].image = free_image;
    host->icon_revision++;
    if( host->icon_revision == 0 )
        host->icon_revision++;
    host->obj_icons[free_entry].revision = host->icon_revision;
    host->obj_icons[free_entry].used = host->icon_clock;
    return free_image;
}

/**
 * Hand a slot's bytes to the engine to decode and publish.
 *
 * A file that will not decode is reported and left unpublished, not asserted:
 * it is a file, the plugin named it, and "that is not a PNG this client reads"
 * is an answer the plugin has to be able to get. The handle stays valid and
 * image_size keeps answering 0, which is the same state a pending load is in
 * -- and deliberately so, because for a caller laying out against it there is
 * nothing to do differently.
 */
static int
plugin_image_publish(
    struct ToriRS_PluginHost* host,
    int image,
    void const* data,
    int size)
{
    struct PluginImage* slot;
    int w = 0;
    int h = 0;

    assert(host);
    assert(image >= 0 && image < TORIRS_PLUGIN_IMAGES_MAX);
    assert(data);

    slot = &host->images[image];
    if( !host->engine.image_publish(host->engine.user, image, data, size, &w, &h) )
    {
        TORIRS_LOG(
            "plugin: %s image '%s' would not decode (%d bytes); it draws nothing\n",
            host->plugins[slot->plugin].name,
            slot->asset,
            size);
        return 0;
    }
    slot->width = w;
    slot->height = h;
    slot->published = true;
    return 1;
}

static int
api_asset_load(
    struct PluginContext* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);

    struct ToriRS_PluginHost* host = ctx->host;
    if( !plugin_asset_name_ok(ctx, name) )
        return 0;

    struct PluginAsset* slot = plugin_asset_find(host, ctx->index, name);
    if( slot && slot->ready )
        return 1;
    /* A second load of an in-flight name joins the first: one read, one event,
     * and both callers see the bytes. */
    if( slot && slot->pending )
        return 0;
    /* Already looked, already not there. The plugin was told the first time --
     * PluginHost_AssetDeliver fires the event with NULL either way -- so
     * re-reading tells it nothing it does not know and costs a task, two IO
     * round trips and a log line for every tick it keeps asking. */
    if( slot && slot->missing )
        return 0;

    slot = plugin_asset_claim(host, ctx->index, name);
    if( !slot )
    {
        TORIRS_LOG(
            "plugin: %s asset '%s' not loaded, the resident asset table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_ASSETS_MAX);
        return 0;
    }

    slot->pending = true;
    slot->ready = false;
    slot->missing = false;
    if( !host->engine.asset_read(host->engine.user, ctx->name, name) )
    {
        slot->pending = false;
        plugin_asset_drop(host, slot);
        return 0;
    }
    return 0;
}

static void const*
api_asset_data(
    struct PluginContext* ctx,
    char const* name,
    int* out_size)
{
    assert(ctx);
    assert(name);

    struct PluginAsset const* slot = plugin_asset_find(ctx->host, ctx->index, name);
    if( out_size )
        *out_size = slot ? slot->size : 0;
    return slot ? slot->data : NULL;
}

static int
api_asset_save(
    struct PluginContext* ctx,
    char const* name,
    void const* data,
    int size)
{
    assert(ctx);
    assert(name);
    assert(data || size == 0);

    struct ToriRS_PluginHost* host = ctx->host;
    if( !plugin_asset_name_ok(ctx, name) )
        return 0;
    if( size < 0 )
        return 0;

    struct PluginAsset* slot = plugin_asset_claim(host, ctx->index, name);
    if( !slot )
    {
        TORIRS_LOG(
            "plugin: %s asset '%s' not saved, the resident asset table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_ASSETS_MAX);
        return 0;
    }

    /* The resident copy is replaced before the write is queued, so a plugin
     * that saves and immediately reads back sees what it wrote rather than
     * what was there before the IO finished. */
    void* copy = NULL;
    if( size > 0 )
    {
        copy = malloc((size_t)size);
        assert(copy);
        memcpy(copy, data, (size_t)size);
    }
    free(slot->data);
    slot->data = copy;
    slot->size = size;
    slot->pending = false;
    slot->ready = true;
    slot->missing = false;

    return host->engine.asset_write(host->engine.user, ctx->name, name, data, size);
}

static void
api_asset_release(
    struct PluginContext* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);

    struct PluginAsset* slot = plugin_asset_find(ctx->host, ctx->index, name);
    /* A read still in flight keeps its slot: dropping it here would leave the
     * delivery with nowhere to land and the plugin with an event it cannot
     * explain. */
    if( slot && !slot->pending )
        plugin_asset_drop(ctx->host, slot);
}

/* -- screenshots -- */

/*
 * A destination the USER named.
 *
 * Looser than plugin_asset_name_ok on purpose: separators are allowed, because
 * "screenshots/levels" is the kind of thing someone types into a settings
 * field and a directory with one component would not be a destination worth
 * configuring. `..` is still refused, so the plugin cannot climb out of
 * whatever the user pointed it at, and a refusal is logged rather than
 * asserted for the same reason it is for asset names -- the string arrives
 * from a config field, not from a programmer.
 */
static bool
plugin_screenshot_dir_ok(
    struct PluginContext* ctx,
    char const* dir)
{
    assert(ctx);
    assert(dir);

    size_t const len = strlen(dir);
    bool ok = len < TORIRS_PLUGIN_SCREENSHOT_DIR_MAX;

    for( size_t i = 0; ok && i < len; i++ )
    {
        char const c = dir[i];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '.' || c == '_' || c == '-' || c == '/' || c == ':' || c == ' ';
    }
    if( ok && strstr(dir, "..") )
        ok = false;

    if( !ok )
        TORIRS_LOG(
            "plugin: %s asked to write a screenshot to '%s'; a destination is a path of "
            "[A-Za-z0-9._- /:] with no '..'\n",
            ctx->name,
            dir);
    return ok;
}

static int
api_screenshot(
    struct PluginContext* ctx,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    assert(ctx);
    assert(name);
    assert(out_path);
    assert(out_path_size > 0);

    /* Emptied first, so a refused request leaves nothing for a caller to read
     * as a destination -- the string is the answer to "where did it go", and
     * "nowhere" has to be sayable. */
    out_path[0] = '\0';
    if( !plugin_asset_name_ok(ctx, name) )
        return 0;
    if( dir && *dir && !plugin_screenshot_dir_ok(ctx, dir) )
        return 0;

    return ctx->host->engine.screenshot(
        ctx->host->engine.user, ctx->name, dir, name, out_path, out_path_size);
}

/*
 * Wall-clock, in the one format a filename can carry.
 *
 * libc rather than an engine call, unlike every other clock here: frame_ms and
 * world_cycle are the CLIENT's clocks and only the engine knows them, while
 * this is the machine's, is the same on both lanes, and has no engine state
 * behind it to fake. A plugin cannot reach it any other way -- the Lua sandbox
 * does not link `os` -- so its absence would be permanent rather than
 * inconvenient.
 */
static int
api_datestamp(
    struct PluginContext* ctx,
    char* out,
    int out_size)
{
    (void)ctx;
    time_t now;
    struct tm local;

    assert(ctx);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    now = time(NULL);
    /* The reentrant form: a plugin handler runs mid-frame and localtime()'s
     * shared buffer is not something to hand around. */
#ifdef _WIN32
    if( localtime_s(&local, &now) != 0 )
#else
    if( !localtime_r(&now, &local) )
#endif
        return 0;
    if( strftime(out, (size_t)out_size, "%Y-%m-%d_%H-%M-%S", &local) == 0 )
        return 0;
    return 1;
}

void
PluginHost_AssetDeliver(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* asset_name,
    void* data,
    int size)
{
    assert(host);
    assert(plugin_name);
    assert(asset_name);

    int const plugin = PluginHost_IndexOf(host, plugin_name);
    if( plugin < 0 )
    {
        /* The plugin was unregistered while its read was in flight. */
        free(data);
        return;
    }

    struct PluginAsset* slot = plugin_asset_find(host, plugin, asset_name);
    if( !slot )
    {
        free(data);
        return;
    }

    slot->pending = false;
    /* What the read FOUND, which is the half the slot used to drop. A miss
     * that leaves data NULL and pending false is otherwise identical to a
     * slot nobody has read yet. */
    slot->missing = (data == NULL);
    slot->ready = (data != NULL);
    if( data )
    {
        free(slot->data);
        slot->data = data;
        slot->size = size;
    }

    /* An image waiting on this file becomes a scene sprite here, before the
     * event goes out -- so a plugin told its asset landed can lay out against
     * image_size in the same handler rather than waiting another frame. */
    if( data )
    {
        for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
            if( host->images[i].plugin == plugin &&
                strcmp(host->images[i].asset, asset_name) == 0 &&
                !(host->panel_registered[plugin] && host->panel_icon_image[plugin] == i &&
                  size > TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX) )
                plugin_image_publish(host, i, slot->data, slot->size);
        for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
            if( host->models[i].plugin == plugin && strcmp(host->models[i].asset, asset_name) == 0 )
                plugin_model_publish(host, i, slot->data, slot->size);
    }

    /* A rail icon is application chrome, but its bytes still arrive through
     * this plugin's sandbox. Every terminal outcome advances the revision:
     * success exposes pixels; missing/malformed exposes the baked fallback. */
    if( host->panel_registered[plugin] && host->panel_icon[plugin][0] &&
        strcmp(host->panel_icon[plugin], asset_name) == 0 )
    {
        plugin_panel_bump(&host->panel_icon_revision[plugin]);
        plugin_panel_bump(&host->panel_registry_revision);
    }

    struct PluginContext* ctx = &host->plugins[plugin];
    if( plugin_provides_frames(ctx) )
        host->frame_selection_dirty = 1;
    if( !ctx->enabled || !ctx->running )
        return;

    struct ToriRS_AssetEvent ev = { asset_name, data ? size : 0, data != NULL };
    plugin_dispatch_one(host, plugin, PLUGIN_CALLBACK_ASSET, &ev);
}

/* -- authored meshes -- */

/* Same rule the object handles are held to: a handle the plugin was never
 * given is a contract violation, and the budget is not. */
static void
plugin_mesh_assert_owned(
    struct PluginContext* ctx,
    int mesh)
{
    assert(ctx);
    for( int i = 0; i < ctx->mesh_count; i++ )
    {
        if( ctx->meshes[i] == mesh )
            return;
    }
    assert(!"plugin mesh handle is not owned by this plugin");
}

static int
api_mesh_create(struct PluginContext* ctx)
{
    assert(ctx);

    struct ToriRS_PluginHost* host = ctx->host;
    if( ctx->mesh_count >= TORIRS_PLUGIN_MESH_BUDGET )
    {
        if( !ctx->mesh_clipped )
        {
            ctx->mesh_clipped = true;
            TORIRS_ERR(
                "plugin: %s is at its %d mesh budget; further mesh_create "
                "calls are refused\n",
                ctx->name,
                TORIRS_PLUGIN_MESH_BUDGET);
        }
        return -1;
    }

    int const mesh = host->engine.mesh_create(host->engine.user);
    if( mesh < 0 )
        return -1;
    ctx->meshes[ctx->mesh_count++] = mesh;
    return mesh;
}

static void
api_mesh_destroy(
    struct PluginContext* ctx,
    int mesh)
{
    plugin_mesh_assert_owned(ctx, mesh);

    for( int i = 0; i < ctx->mesh_count; i++ )
    {
        if( ctx->meshes[i] != mesh )
            continue;
        ctx->meshes[i] = ctx->meshes[ctx->mesh_count - 1];
        ctx->mesh_count--;
        break;
    }
    ctx->host->engine.mesh_destroy(ctx->host->engine.user, mesh);
}

static int
api_mesh_vertex(
    struct PluginContext* ctx,
    int mesh,
    int x,
    int y,
    int z)
{
    plugin_mesh_assert_owned(ctx, mesh);
    return ctx->host->engine.mesh_vertex(ctx->host->engine.user, mesh, x, y, z);
}

static int
api_mesh_face(
    struct PluginContext* ctx,
    int mesh,
    int a,
    int b,
    int c,
    int hsl,
    int alpha)
{
    plugin_mesh_assert_owned(ctx, mesh);
    /* Vertex indices are checked by the engine against the mesh it holds; the
     * transparency is checked here because its ceiling is part of the api
     * contract and not of any mesh's state. */
    assert(alpha >= 0);
    assert(alpha <= TORIRS_PLUGIN_MESH_ALPHA_MAX);
    return ctx->host->engine.mesh_face(ctx->host->engine.user, mesh, a, b, c, hsl, alpha);
}

/* -- world objects -- */

/* Every object entry point past create takes a handle the plugin was given,
 * so a handle it does not own is a contract violation and asserts. The one
 * thing that is NOT a violation is the budget: create returns -1 there,
 * because how many beams a floor needs is a runtime fact. */
static void
plugin_object_assert_owned(
    struct PluginContext* ctx,
    int object)
{
    assert(ctx);
    for( int i = 0; i < ctx->object_count; i++ )
    {
        if( ctx->objects[i] == object )
            return;
    }
    assert(!"plugin object handle is not owned by this plugin");
}

static int
api_object_create(struct PluginContext* ctx)
{
    assert(ctx);

    struct ToriRS_PluginHost* host = ctx->host;
    if( ctx->object_count >= TORIRS_PLUGIN_OBJECT_BUDGET )
    {
        if( !ctx->object_clipped )
        {
            ctx->object_clipped = true;
            TORIRS_ERR(
                "plugin: %s is at its %d world-object budget; further "
                "object_create calls are refused\n",
                ctx->name,
                TORIRS_PLUGIN_OBJECT_BUDGET);
        }
        return -1;
    }

    int const object = host->engine.object_create(host->engine.user);
    if( object < 0 )
        return -1;
    ctx->objects[ctx->object_count++] = object;
    return object;
}

static void
api_object_destroy(
    struct PluginContext* ctx,
    int object)
{
    plugin_object_assert_owned(ctx, object);

    for( int i = 0; i < ctx->object_count; i++ )
    {
        if( ctx->objects[i] != object )
            continue;
        ctx->objects[i] = ctx->objects[ctx->object_count - 1];
        ctx->object_count--;
        break;
    }
    ctx->host->engine.object_destroy(ctx->host->engine.user, object);
}

static void
plugin_objects_destroy_all(
    struct ToriRS_PluginHost* host,
    struct PluginContext* ctx)
{
    assert(host);
    assert(ctx);
    for( int i = 0; i < ctx->object_count; i++ )
        host->engine.object_destroy(host->engine.user, ctx->objects[i]);
    ctx->object_count = 0;
    ctx->object_clipped = false;
}

/* After plugin_objects_destroy_all, never before it: the objects are what hold
 * the meshes up. */
static void
plugin_meshes_destroy_all(
    struct ToriRS_PluginHost* host,
    struct PluginContext* ctx)
{
    assert(host);
    assert(ctx);
    for( int i = 0; i < ctx->mesh_count; i++ )
        host->engine.mesh_destroy(host->engine.user, ctx->meshes[i]);
    ctx->mesh_count = 0;
    ctx->mesh_clipped = false;
}

static void
api_object_set_model(
    struct PluginContext* ctx,
    int object,
    enum ToriRS_HostModelSource source,
    int id)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_model(ctx->host->engine.user, object, (int)source, id);
}

static void
api_object_recolor(
    struct PluginContext* ctx,
    int object,
    int hsl_from,
    int hsl_to)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_recolor(ctx->host->engine.user, object, hsl_from, hsl_to);
}

static void
api_object_clear_recolors(
    struct PluginContext* ctx,
    int object)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_clear_recolors(ctx->host->engine.user, object);
}

static void
api_object_set_anim(
    struct PluginContext* ctx,
    int object,
    int seq_id,
    int loop)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_anim(ctx->host->engine.user, object, seq_id, loop);
}

static void
api_object_set_light(
    struct PluginContext* ctx,
    int object,
    int ambient,
    int contrast)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_light(ctx->host->engine.user, object, ambient, contrast);
}

static void
api_object_set_position(
    struct PluginContext* ctx,
    int object,
    int tile_x,
    int tile_z,
    int level,
    int height,
    int yaw)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_position(
        ctx->host->engine.user, object, tile_x, tile_z, level, height, yaw);
}

static void
api_object_set_active(
    struct PluginContext* ctx,
    int object,
    int active)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_active(ctx->host->engine.user, object, active);
}

static int
api_object_ready(
    struct PluginContext* ctx,
    int object)
{
    plugin_object_assert_owned(ctx, object);
    return ctx->host->engine.object_ready(ctx->host->engine.user, object);
}

/* -- colour -- */

static int
api_hsl_from_rgb(
    struct PluginContext* ctx,
    uint32_t rgb)
{
    assert(ctx);
    return ctx->host->engine.hsl_from_rgb(ctx->host->engine.user, rgb);
}

static uint32_t
api_hsl_to_rgb(
    struct PluginContext* ctx,
    int hsl)
{
    assert(ctx);
    return ctx->host->engine.hsl_to_rgb(ctx->host->engine.user, hsl);
}

/* -- the application plugin panel ----------------------------------------
 *
 * Registration is per plugin; content is not. Only panel_active owns the
 * small array below, which is what makes "one shell, most recently selected
 * plugin only" an invariant of the authority rather than a convention every
 * presenter has to remember.
 */

static int
api_image_load(
    struct PluginContext* ctx,
    char const* name);

static void
plugin_panel_bump(uint32_t* revision)
{
    assert(revision);
    (*revision)++;
    if( *revision == 0 )
        (*revision)++;
}

/** Zero is reserved for "not a widget" in queued presenter work. */
static uint32_t
plugin_widget_next_serial(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->next_widget_serial++;
    if( host->next_widget_serial == 0 )
        host->next_widget_serial++;
    return host->next_widget_serial;
}

/** Forget row deltas after a full page declaration became authoritative. */
static void
plugin_panel_changes_reset(struct ToriRS_PluginHost* host)
{
    assert(host);
    memset(host->panel_change_flags, 0, sizeof(host->panel_change_flags));
    memset(host->panel_change_queue, 0, sizeof(host->panel_change_queue));
    host->panel_change_head = 0;
    host->panel_change_count = 0;
}

/** A changed widget sequence cannot be patched against retained row handles. */
static void
plugin_panel_change_structural(struct ToriRS_PluginHost* host)
{
    assert(host);
    plugin_panel_changes_reset(host);
    host->panel_change_rebuild = true;
}

/** Record one current row without searching the journal. */
static void
plugin_panel_change_widget(
    struct ToriRS_PluginHost* host,
    int widget,
    uint32_t flags)
{
    int tail;

    assert(host);
    assert(widget >= 0 && widget < TORIRS_PLUGIN_WIDGETS_MAX);
    assert(flags != 0);
    if( host->panel_change_rebuild )
        return;
    if( host->panel_change_flags[widget] == 0 )
    {
        assert(host->panel_change_count < TORIRS_PLUGIN_WIDGETS_MAX);
        tail = (host->panel_change_head + host->panel_change_count) %
               TORIRS_PLUGIN_WIDGETS_MAX;
        host->panel_change_queue[tail] = widget;
        host->panel_change_count++;
    }
    host->panel_change_flags[widget] |= flags;
}

static void
plugin_panel_generation_next(struct ToriRS_PluginHost* host)
{
    assert(host);
    plugin_panel_bump(&host->panel_selection_generation);
    host->panel_last_intent_sequence = 0;
}

static int
plugin_panel_id_ok(char const* id)
{
    size_t n;

    if( !id || !id[0] )
        return 0;
    n = strlen(id);
    return n < TORIRS_PLUGIN_WIDGET_ID_MAX;
}

static int
plugin_panel_find(
    struct ToriRS_PluginHost const* host,
    char const* id)
{
    assert(host);
    assert(id);
    for( int i = 0; i < host->panel_widget_count; i++ )
        if( strcmp(host->panel_widgets[i].id, id) == 0 )
            return i;
    return -1;
}

static int
plugin_panel_find_serial(
    struct ToriRS_PluginHost const* host,
    uint32_t serial)
{
    assert(host);
    if( serial == 0 )
        return -1;
    for( int i = 0; i < host->panel_widget_count; i++ )
        if( host->panel_widgets[i].serial == serial )
            return i;
    return -1;
}

static void
plugin_panel_clear_model(struct ToriRS_PluginHost* host)
{
    assert(host);
    if( host->panel_widget_count == 0 )
    {
        host->panel_select_option_count = 0;
        return;
    }
    memset(host->panel_widgets, 0, sizeof(host->panel_widgets));
    memset(host->panel_select_options, 0, sizeof(host->panel_select_options));
    memset(host->panel_invalidated, 0, sizeof(host->panel_invalidated));
    host->panel_widget_count = 0;
    host->panel_select_option_count = 0;
    plugin_panel_change_structural(host);
    plugin_panel_bump(&host->panel_model_revision);
}

static bool
api_panel_request(
    struct PluginContext* ctx,
    struct ToriRS_PanelDescriptor const* desc)
{
    struct ToriRS_PluginHost* host;
    char const* title;
    char const* icon;
    int width;
    bool changed;

    assert(ctx);
    host = ctx->host;

    /* Registration is a lifecycle declaration, not a way for an arbitrary
     * game event to steal a rail slot or open UI. A plugin gets one precise
     * moment to make it: its on_start callback. */
    if( host->dispatching != ctx->index || host->dispatch_event != PLUGIN_CALLBACK_START || !desc )
        return false;

    /* The plugin's OWN title, always: a page cannot rename the plugin it
     * belongs to. @see struct ToriRS_PanelDescriptor. */
    title = ctx->title;
    icon = desc->icon_asset ? desc->icon_asset : "";
    if( icon[0] && !plugin_asset_name_ok(ctx, icon) )
        return false;
    width = desc->preferred_width;
    if( width == 0 )
        width = TORIRS_PANEL_WIDTH_DEFAULT;
    if( width < TORIRS_PANEL_WIDTH_MIN )
        width = TORIRS_PANEL_WIDTH_MIN;
    if( width > TORIRS_PANEL_WIDTH_MAX )
        width = TORIRS_PANEL_WIDTH_MAX;

    changed = !host->panel_registered[ctx->index] ||
              plugin_copy_str_would_change(
                  host->panel_title[ctx->index], sizeof(host->panel_title[ctx->index]), title) ||
              plugin_copy_str_would_change(
                  host->panel_icon[ctx->index], sizeof(host->panel_icon[ctx->index]), icon) ||
              host->panel_preferred_width[ctx->index] != width;
    host->panel_registered[ctx->index] = true;
    plugin_copy_str(host->panel_title[ctx->index], sizeof(host->panel_title[ctx->index]), title);
    plugin_copy_str(host->panel_icon[ctx->index], sizeof(host->panel_icon[ctx->index]), icon);
    host->panel_preferred_width[ctx->index] = width;
    if( changed )
    {
        plugin_panel_bump(&host->panel_registry_revision);
        plugin_panel_bump(&host->panel_icon_revision[ctx->index]);
    }
    /* Icon art follows the ordinary per-plugin asset sandbox and image
     * decoder. Registration merely names it; the host starts the asynchronous
     * load so a plugin does not need platform-specific setup code. */
    if( icon[0] )
    {
        struct PluginAsset const* resident = plugin_asset_find(host, ctx->index, icon);
        host->panel_icon_image[ctx->index] =
            resident && resident->data && resident->size > TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX
                ? -1
                : api_image_load(ctx, icon);
    }
    else
        host->panel_icon_image[ctx->index] = -1;
    return true;
}

static bool
api_panel_widget(
    struct PluginContext* ctx,
    int kind,
    char const* id,
    char const* label)
{
    struct ToriRS_PluginHost* host;
    struct ToriRS_PanelWidget* widget;

    assert(ctx);
    host = ctx->host;
    if( host->panel_active != ctx->index || !host->panel_building ||
        host->dispatching != ctx->index || host->dispatch_event != PLUGIN_CALLBACK_PANEL_BUILD )
        return false;
    if( kind < 0 || kind >= TORIRS_PANEL_WIDGET_COUNT || !plugin_panel_id_ok(id) )
        return false;
    /* Idempotent within one declaration, matching win_widget. */
    if( plugin_panel_find(host, id) >= 0 )
        return true;
    if( host->panel_widget_count >= TORIRS_PLUGIN_WIDGETS_MAX )
    {
        PluginHost_SetError(host, ctx->index, "panel control budget exhausted");
        return false;
    }

    widget = &host->panel_widgets[host->panel_widget_count];
    memset(widget, 0, sizeof(*widget));
    widget->kind = kind;
    widget->selected = -1;
    if( kind == TORIRS_PANEL_WIDGET_CUSTOM )
        widget->preferred_height = TORIRS_PANEL_CUSTOM_HEIGHT_DEFAULT;
    widget->serial = plugin_widget_next_serial(host);
    plugin_copy_str(widget->id, sizeof(widget->id), id);
    plugin_copy_str(widget->label, sizeof(widget->label), label);
    host->panel_invalidated[host->panel_widget_count] = kind == TORIRS_PANEL_WIDGET_CUSTOM;
    host->panel_widget_count++;
    plugin_panel_change_structural(host);
    plugin_panel_bump(&host->panel_model_revision);
    return true;
}

static bool
plugin_panel_mutable(
    struct PluginContext* ctx,
    char const* id,
    int* out_slot)
{
    int slot;

    assert(ctx);
    if( ctx->host->panel_active != ctx->index || !plugin_panel_id_ok(id) )
        return false;
    slot = plugin_panel_find(ctx->host, id);
    if( slot < 0 )
        return false;
    if( out_slot )
        *out_slot = slot;
    return true;
}

static bool
api_panel_set_text(
    struct PluginContext* ctx,
    char const* id,
    char const* text)
{
    struct ToriRS_PanelWidget* widget;
    char const* next = text ? text : "";
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, id, &slot) )
        return false;
    widget = &ctx->host->panel_widgets[slot];
    if( !plugin_copy_str_would_change(widget->text, sizeof(widget->text), next) )
        return true;
    plugin_copy_str(widget->text, sizeof(widget->text), next);
    plugin_panel_bump(&ctx->host->panel_model_revision);
    plugin_panel_change_widget(
        ctx->host, slot, TORIRS_PLUGIN_PANEL_CHANGE_TEXT);
    return true;
}

static bool
api_panel_set_value(
    struct PluginContext* ctx,
    char const* id,
    int value)
{
    struct ToriRS_PanelWidget* widget;
    int old_checked;
    int old_selected;
    int old_value;
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, id, &slot) )
        return false;
    widget = &ctx->host->panel_widgets[slot];
    old_checked = widget->checked;
    old_selected = widget->selected;
    old_value = widget->value;
    widget->value = value;
    if( widget->kind == TORIRS_PANEL_WIDGET_CHECKBOX || widget->kind == TORIRS_PANEL_WIDGET_TOGGLE ||
        widget->kind == TORIRS_PANEL_WIDGET_LIST_ROW )
        widget->checked = value ? 1 : 0;
    if( widget->kind == TORIRS_PANEL_WIDGET_DROPDOWN )
        widget->selected = value;
    if( old_checked != widget->checked || old_selected != widget->selected ||
        old_value != widget->value )
    {
        plugin_panel_bump(&ctx->host->panel_model_revision);
        plugin_panel_change_widget(
            ctx->host, slot, TORIRS_PLUGIN_PANEL_CHANGE_VALUE);
    }
    return true;
}

static bool
api_panel_set_height(
    struct PluginContext* ctx,
    char const* custom_view_id,
    int preferred_height)
{
    struct ToriRS_PanelWidget* widget;
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, custom_view_id, &slot) )
        return false;
    widget = &ctx->host->panel_widgets[slot];
    if( widget->kind != TORIRS_PANEL_WIDGET_CUSTOM )
        return false;
    if( preferred_height == 0 )
        preferred_height = TORIRS_PANEL_CUSTOM_HEIGHT_DEFAULT;
    if( preferred_height < TORIRS_PANEL_CUSTOM_HEIGHT_MIN )
        preferred_height = TORIRS_PANEL_CUSTOM_HEIGHT_MIN;
    if( preferred_height > TORIRS_PANEL_CUSTOM_HEIGHT_MAX )
        preferred_height = TORIRS_PANEL_CUSTOM_HEIGHT_MAX;
    if( widget->preferred_height == preferred_height )
        return true;
    widget->preferred_height = preferred_height;
    ctx->host->panel_invalidated[slot] = true;
    plugin_panel_bump(&ctx->host->panel_model_revision);
    plugin_panel_change_widget(
        ctx->host, slot, TORIRS_PLUGIN_PANEL_CHANGE_HEIGHT);
    return true;
}

static bool
api_panel_set_attention(
    struct PluginContext* ctx,
    bool attention)
{
    assert(ctx);
    if( !ctx->host->panel_registered[ctx->index] || !ctx->running )
        return false;
    if( ctx->host->panel_attention[ctx->index] == attention )
        return true;
    ctx->host->panel_attention[ctx->index] = attention;
    plugin_panel_bump(&ctx->host->panel_registry_revision);
    return true;
}

static void
api_panel_clear(struct PluginContext* ctx)
{
    assert(ctx);
    if( ctx->host->panel_active != ctx->index )
        return;
    plugin_panel_clear_model(ctx->host);
    if( !ctx->host->panel_building )
        ctx->host->panel_needs_build = true;
}

static void
api_panel_invalidate(
    struct PluginContext* ctx,
    char const* custom_view_id)
{
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, custom_view_id, &slot) ||
        ctx->host->panel_widgets[slot].kind != TORIRS_PANEL_WIDGET_CUSTOM ||
        ctx->host->panel_invalidated[slot] )
        return;
    ctx->host->panel_invalidated[slot] = true;
}

/** Leave the mounted page before its plugin stops or another page replaces
 * it. The invisible event is delivered while the old model still exists, and
 * the model is gone before this returns. */
static int
plugin_panel_deactivate(struct ToriRS_PluginHost* host)
{
    struct ToriRS_PanelLayoutEvent ev;
    int const old = host->panel_active;
    uint32_t const generation = host->panel_selection_generation;

    assert(host);
    if( old < 0 )
        return 0;

    if( host->panel_has_layout && host->panel_visible && !host->panel_transitioning &&
        old < host->plugin_count && host->plugins[old].running )
    {
        memset(&ev, 0, sizeof(ev));
        ev.width = host->panel_width;
        ev.height = host->panel_height;
        ev.scale_milli = host->panel_scale_milli;
        ev.size_class = host->panel_size_class;
        ev.visible = false;
        ev.game_visible = host->panel_game_visible;
        ev.selection_generation = generation;
        host->panel_visible = false;
        host->panel_transitioning++;
        plugin_dispatch_one(host, old, PLUGIN_CALLBACK_PANEL_LAYOUT, &ev);
        host->panel_transitioning--;
    }

    /* A callback above may have torn itself down and completed these steps. */
    if( host->panel_active == old )
    {
        plugin_panel_clear_model(host);
        host->panel_active = -1;
        host->panel_needs_build = false;
        host->panel_has_layout = false;
        host->panel_visible = false;
        plugin_panel_generation_next(host);
        plugin_panel_bump(&host->panel_model_revision);
    }
    return 1;
}

static void
plugin_panel_unregister(
    struct ToriRS_PluginHost* host,
    int plugin_index)
{
    bool const registered = host->panel_registered[plugin_index];

    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);

    if( host->panel_active == plugin_index )
    {
        if( host->panel_transitioning )
        {
            plugin_panel_clear_model(host);
            host->panel_active = -1;
            host->panel_needs_build = false;
            host->panel_has_layout = false;
            host->panel_visible = false;
            plugin_panel_generation_next(host);
            plugin_panel_bump(&host->panel_model_revision);
        }
        else
            (void)plugin_panel_deactivate(host);
    }

    host->panel_registered[plugin_index] = false;
    host->panel_title[plugin_index][0] = '\0';
    host->panel_icon[plugin_index][0] = '\0';
    host->panel_icon_image[plugin_index] = -1;
    plugin_panel_bump(&host->panel_icon_revision[plugin_index]);
    host->panel_preferred_width[plugin_index] = 0;
    host->panel_attention[plugin_index] = false;
    if( host->panel_last_selected == plugin_index )
        host->panel_last_selected = -1;
    if( registered )
        plugin_panel_bump(&host->panel_registry_revision);
}

static int
plugin_panel_build_active(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation)
{
    int view;
    int plugin;

    assert(host);
    if( selection_generation == 0 || selection_generation != host->panel_selection_generation )
        return 0;
    plugin = host->panel_active;
    if( plugin < 0 || plugin >= host->plugin_count || !host->panel_registered[plugin] ||
        !host->plugins[plugin].running )
        return 0;
    if( !host->panel_needs_build )
        return 1;

    plugin_panel_clear_model(host);
    host->panel_needs_build = false;
    host->panel_building = true;
    view = host->panel_view;
    plugin_dispatch_one(host, plugin, PLUGIN_CALLBACK_PANEL_BUILD, &view);
    host->panel_building = false;

    /* A build handler is allowed to fault/disable itself. */
    return host->panel_active == plugin && host->panel_selection_generation == selection_generation
               ? 1
               : 0;
}

/* -- drawing -- */

/* One gate for every draw call: the window must be open, and the plugin must
 * still be inside its per-frame allotment. Clipping is reported once per frame
 * per plugin -- a silent cap reads as "drew everything" when it did not. */
static bool
plugin_draw_allow(
    struct PluginContext* ctx,
    void* surface)
{
    assert(ctx);
    /* Drawing outside on_draw_world would push into a list the emit walk has
     * already read. */
    assert(ctx->host->draw_surface);
    assert(surface == ctx->host->draw_surface);
    (void)surface;

    if( ctx->draw_used >= TORIRS_PLUGIN_DRAW_BUDGET )
    {
        if( !ctx->draw_clipped )
        {
            ctx->draw_clipped = true;
            TORIRS_LOG(
                "plugin: %s hit its %d-item draw budget this frame; "
                "the rest of its overlay was dropped\n",
                ctx->name,
                TORIRS_PLUGIN_DRAW_BUDGET);
        }
        return false;
    }
    return true;
}

/*
 * The extra gate on the two WORLD-only verbs.
 *
 * A tile and a hull are both named in scene terms -- an absolute tile, a scene
 * element -- and the canvas surface has no scene behind it. Drawing one there
 * is not a thing that comes out slightly wrong; it is a call whose arguments
 * cannot be resolved at all, which makes it a contract violation and not a
 * runtime state.
 */
static void
plugin_draw_require_world(struct PluginContext* ctx)
{
    (void)ctx;
    assert(ctx);
    assert(
        ctx->host->draw_canvas == PLUGIN_DRAW_SURFACE_WORLD &&
        "draw_tile/draw_hull name something in the scene; the screen surfaces have none");
}

static void
api_draw_tile(
    struct PluginContext* ctx,
    void* surface,
    int tile_x,
    int tile_z,
    int level,
    uint32_t rgb,
    uint32_t fill_rgb,
    int fill_alpha)
{
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx, surface) )
        return;
    ctx->draw_used += ctx->host->engine.draw_tile(
        ctx->host->engine.user, tile_x, tile_z, level, rgb, fill_rgb, fill_alpha);
}

static void
api_draw_hull(
    struct PluginContext* ctx,
    void* surface,
    int element_id,
    uint32_t rgb,
    int fill_alpha,
    int shape)
{
    assert(shape == TORIRS_HULL_BOUNDS || shape == TORIRS_HULL_MESH);
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx, surface) )
        return;
    /* An entity whose APPEARANCE facet another plugin owns is that plugin's to
     * outline. Refusal is silent, like an element that is not on screen. */
    if( !plugin_entity_hull_allowed(ctx->host, ctx->index, element_id) )
        return;
    ctx->draw_used +=
        ctx->host->engine.draw_hull(ctx->host->engine.user, element_id, rgb, fill_alpha, shape);
}

static void
api_draw_line(
    struct PluginContext* ctx,
    void* surface,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t rgb)
{
    if( !plugin_draw_allow(ctx, surface) )
        return;
    ctx->draw_used += ctx->host->engine.draw_line(ctx->host->engine.user, x0, y0, x1, y1, rgb);
}

static void
api_draw_text(
    struct PluginContext* ctx,
    void* surface,
    int x,
    int y,
    char const* text,
    uint32_t rgb)
{
    assert(text);
    if( !plugin_draw_allow(ctx, surface) )
        return;
    ctx->draw_used += ctx->host->engine.draw_text(ctx->host->engine.user, x, y, text, rgb);
}

static void
api_draw_rect(
    struct PluginContext* ctx,
    void* surface,
    int x,
    int y,
    int w,
    int h,
    uint32_t rgb,
    int fill_alpha)
{
    if( !plugin_draw_allow(ctx, surface) )
        return;
    ctx->draw_used +=
        ctx->host->engine.draw_rect(ctx->host->engine.user, x, y, w, h, rgb, fill_alpha);
}

/* -- images -- */

static int
api_image_load(
    struct PluginContext* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);

    struct ToriRS_PluginHost* host = ctx->host;
    int free_slot = -1;

    if( !plugin_asset_name_ok(ctx, name) )
        return -1;

    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
    {
        /* Already asked for. One image per (plugin, file), so a plugin that
         * calls this from on_start and again from a config change gets the
         * handle it already has rather than a second copy in the scene. */
        if( host->images[i].plugin == ctx->index && strcmp(host->images[i].asset, name) == 0 )
            return i;
        if( host->images[i].plugin < 0 && free_slot < 0 )
            free_slot = i;
    }

    if( free_slot < 0 )
    {
        TORIRS_LOG(
            "plugin: %s image '%s' not loaded, the resident image table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_IMAGES_MAX);
        return -1;
    }

    host->images[free_slot].plugin = ctx->index;
    snprintf(host->images[free_slot].asset, sizeof(host->images[free_slot].asset), "%s", name);
    host->images[free_slot].width = 0;
    host->images[free_slot].height = 0;
    host->images[free_slot].published = false;

    /*
     * The bytes come through the ordinary asset path, so an image is a file in
     * the same sandbox as any other and the arrival is delivered to the same
     * place. What is different is only what happens to the bytes when they
     * land -- see PluginHost_AssetDeliver, which publishes any image waiting
     * on that name.
     *
     * A load already resident answers immediately; the publish is done here so
     * that an image of a file the plugin had already loaded as raw bytes does
     * not wait for a read that will never be queued.
     */
    if( api_asset_load(ctx, name) )
    {
        int size = 0;
        void const* data = api_asset_data(ctx, name, &size);
        if( data )
            plugin_image_publish(host, free_slot, data, size);
    }
    return free_slot;
}

/* -- shipped models -- */

/*
 * api_image_load's twin. Same slot rule (one per plugin+file), same sandbox on
 * the name, same asset path for the bytes; only what happens to them on
 * arrival differs.
 */
static int
api_model_load(
    struct PluginContext* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);

    struct ToriRS_PluginHost* host = ctx->host;
    int free_slot = -1;

    if( !plugin_asset_name_ok(ctx, name) )
        return -1;

    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
    {
        if( host->models[i].plugin == ctx->index && strcmp(host->models[i].asset, name) == 0 )
            return i;
        if( host->models[i].plugin < 0 && free_slot < 0 )
            free_slot = i;
    }

    if( free_slot < 0 )
    {
        TORIRS_LOG(
            "plugin: %s model '%s' not loaded, the resident model table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_MODELS_MAX);
        return -1;
    }

    host->models[free_slot].plugin = ctx->index;
    snprintf(host->models[free_slot].asset, sizeof(host->models[free_slot].asset), "%s", name);
    host->models[free_slot].published = false;

    if( api_asset_load(ctx, name) )
    {
        int size = 0;
        void const* data = api_asset_data(ctx, name, &size);
        if( data )
            plugin_model_publish(host, free_slot, data, size);
    }
    return free_slot;
}

/**
 * Pixels the plugin composed, published under `name`.
 *
 * The slot search is image_load's, deliberately: one image per (plugin, name)
 * whichever way it was made, so a plugin that composes the same picture every
 * frame reuses one slot rather than exhausting the table in a second. What is
 * different is only where the pixels come from -- there is no asset read, and
 * so no pending state and no frame in which the handle is live but empty.
 */
static int
api_image_compose(
    struct PluginContext* ctx,
    char const* name,
    int w,
    int h,
    uint32_t const* argb)
{
    assert(ctx);
    assert(name);
    assert(argb);

    struct ToriRS_PluginHost* host = ctx->host;
    int slot = -1;
    int free_slot = -1;

    if( !plugin_asset_name_ok(ctx, name) )
        return -1;
    /* A size is a NUMBER the plugin computed -- from a config key, in every
     * case this exists for -- so a silly one is bad input, not a bug in the
     * caller's frame. The engine refuses it too; refusing here as well is what
     * keeps a plugin from spending a slot on it. */
    if( w <= 0 || h <= 0 || w > 4096 || h > 4096 )
    {
        TORIRS_LOG(
            "plugin: %s composed image '%s' is %dx%d, which is not a picture\n",
            ctx->name,
            name,
            w,
            h);
        return -1;
    }

    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
    {
        if( host->images[i].plugin == ctx->index && strcmp(host->images[i].asset, name) == 0 )
        {
            slot = i;
            break;
        }
        if( host->images[i].plugin < 0 && free_slot < 0 )
            free_slot = i;
    }
    if( slot < 0 )
        slot = free_slot;
    if( slot < 0 )
    {
        TORIRS_LOG(
            "plugin: %s image '%s' not composed, the resident image table is full "
            "(%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_IMAGES_MAX);
        return -1;
    }

    if( !host->engine.image_publish_argb(host->engine.user, slot, w, h, argb) )
        return -1;

    host->images[slot].plugin = ctx->index;
    snprintf(host->images[slot].asset, sizeof(host->images[slot].asset), "%s", name);
    host->images[slot].width = w;
    host->images[slot].height = h;
    host->images[slot].published = true;
    return slot;
}

static int
api_image_pixels(
    struct PluginContext* ctx,
    int image,
    uint32_t* out,
    int max)
{
    /* Reads obey the same per-plugin ownership and incarnation rules as draw
     * and release. A plugin may compose its own images, never another
     * provider's retained resource. */
    struct PluginImage const* slot = plugin_image_readable(ctx, image);

    assert(out);
    /* Still pending is the ORDINARY state for the first frames after a load,
     * so it answers 0 rather than asserting -- the same answer, and for the
     * same reason, that image_size gives while a read is in flight. */
    if( !slot || !slot->published )
        return 0;
    if( max < slot->width * slot->height )
        return 0;
    return ctx->host->engine.image_read(ctx->host->engine.user, image, out, max);
}

static int
api_image_size(
    struct PluginContext* ctx,
    int image,
    int* out_w,
    int* out_h)
{
    /* A stale or foreign handle reads like an unavailable image: 0x0 and a
     * false return. */
    struct PluginImage const* slot = plugin_image_readable(ctx, image);

    if( out_w )
        *out_w = slot ? slot->width : 0;
    if( out_h )
        *out_h = slot ? slot->height : 0;
    return slot && slot->published ? 1 : 0;
}

static void
api_image_release(
    struct PluginContext* ctx,
    int image)
{
    /* Only the owning plugin may release a handle. */
    if( !plugin_image_owned(ctx, image) )
        return;
    /*
     * A CACHED ITEM ICON is owned by this plugin's slot and still is not the
     * plugin's to free: the entry pointing at it would go on handing the
     * handle out, and the next caller would draw whatever landed in the
     * recycled slot. The cache decides when an icon goes.
     * @see obj_image.
     */
    if( plugin_obj_icon_owns(ctx->host, image) )
        return;
    plugin_image_drop(ctx->host, image);
}

/* ------------------------------------------------------------ entity claims */

/** This plugin's row for `part`, or NULL. */
static struct PluginEntityClaim*
plugin_entity_row(
    struct ToriRS_PluginHost* host,
    int plugin,
    char const* part)
{
    assert(host);
    assert(part);
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
        if( host->entity_claims[i].plugin == plugin &&
            strcmp(host->entity_claims[i].part, part) == 0 )
            return &host->entity_claims[i];
    return NULL;
}

static struct PluginEntityClaim const*
plugin_entity_holder(
    struct ToriRS_PluginHost const* host,
    char const* part,
    int scope)
{
    assert(host);
    assert(part);
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
        if( host->entity_claims[i].plugin >= 0 &&
            (host->entity_claims[i].scopes & scope) != 0 &&
            strcmp(host->entity_claims[i].part, part) == 0 )
            return &host->entity_claims[i];
    return NULL;
}

static struct PluginEntityClaim*
plugin_entity_claim(
    struct PluginContext* ctx,
    char const* part,
    int scope)
{
    struct PluginEntityClaim* own;
    struct PluginEntityClaim const* holder;
    int free_row = -1;

    assert(ctx);
    assert(part);
    if( !plugin_entity_parse(part, NULL, NULL, NULL, NULL) )
        return NULL;
    own = plugin_entity_row(ctx->host, ctx->index, part);
    if( own )
    {
        own->scopes |= scope;
        return own;
    }
    holder = plugin_entity_holder(ctx->host, part, scope);
    if( holder )
        return NULL;
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
        if( ctx->host->entity_claims[i].plugin < 0 )
        {
            free_row = i;
            break;
        }
    if( free_row < 0 )
        return NULL;
    own = &ctx->host->entity_claims[free_row];
    memset(own, 0, sizeof(*own));
    own->plugin = ctx->index;
    own->scopes = scope;
    own->element_id = -1;
    snprintf(own->part, sizeof(own->part), "%s", part);
    return own;
}

static void
plugin_entity_drop_plugin(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    assert(host);
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
        if( host->entity_claims[i].plugin == plugin )
        {
            memset(&host->entity_claims[i], 0, sizeof(host->entity_claims[i]));
            host->entity_claims[i].plugin = -1;
        }
}

static int
plugin_entity_parse(
    char const* part,
    int* out_a,
    int* out_b,
    int* out_c,
    int* out_d)
{
    int kind;
    char const* rest;
    int n[4] = { 0, 0, 0, 0 };
    int want;

    assert(part);
    if( strncmp(part, "npc:", 4) == 0 )
    {
        kind = TORIRS_ENTITY_NPC;
        rest = part + 4;
        want = 1;
    }
    else if( strncmp(part, "player:", 7) == 0 )
    {
        kind = TORIRS_ENTITY_PLAYER;
        rest = part + 7;
        want = 1;
    }
    else if( strncmp(part, "loc:", 4) == 0 )
    {
        kind = TORIRS_ENTITY_LOC;
        rest = part + 4;
        want = 4;
    }
    else if( strncmp(part, "obj:", 4) == 0 )
    {
        kind = TORIRS_ENTITY_OBJ;
        rest = part + 4;
        want = 4;
    }
    else
        return 0;

    if( want == 1 )
    {
        if( sscanf(rest, "%d", &n[0]) != 1 )
            return 0;
    }
    else if( sscanf(rest, "%d,%d,%d,%d", &n[0], &n[1], &n[2], &n[3]) != 4 )
        return 0;

    if( out_a )
        *out_a = n[0];
    if( out_b )
        *out_b = n[1];
    if( out_c )
        *out_c = n[2];
    if( out_d )
        *out_d = n[3];
    return kind;
}

static char const*
api_entity_part(
    struct PluginContext* ctx,
    int kind,
    int a,
    int b,
    int c,
    int d,
    char* buf,
    int cap)
{
    int n;

    (void)ctx;
    assert(buf);
    switch( kind )
    {
    case TORIRS_ENTITY_NPC:
        n = snprintf(buf, (size_t)cap, "npc:%d", a);
        break;
    case TORIRS_ENTITY_PLAYER:
        n = snprintf(buf, (size_t)cap, "player:%d", a);
        break;
    case TORIRS_ENTITY_LOC:
        n = snprintf(buf, (size_t)cap, "loc:%d,%d,%d,%d", a, b, c, d);
        break;
    case TORIRS_ENTITY_OBJ:
        n = snprintf(buf, (size_t)cap, "obj:%d,%d,%d,%d", a, b, c, d);
        break;
    default:
        return NULL;
    }
    return n > 0 && n < cap ? buf : NULL;
}

/**
 * The scene element an entity part names THIS frame, or -1.
 *
 * Through the same snapshot walks a plugin would use, so the two cannot
 * disagree about which npc is in slot 12. A walk per claim per frame, and
 * claims are few.
 */
static int
plugin_entity_element(
    struct ToriRS_PluginHost* host,
    char const* part)
{
    int a;
    int b;
    int c;
    int d;
    int kind;

    assert(host);
    kind = plugin_entity_parse(part, &a, &b, &c, &d);
    switch( kind )
    {
    case TORIRS_ENTITY_NPC:
    {
        struct ToriRS_NpcSnapshot snap;
        return host->engine.npc_by_slot(host->engine.user, a, &snap) ? snap.element_id : -1;
    }
    case TORIRS_ENTITY_PLAYER:
    {
        struct ToriRS_PlayerSnapshot snap;
        int iter = -1;
        if( host->engine.local_player(host->engine.user, &snap) && snap.server_pid == a )
            return snap.element_id;
        while( (iter = host->engine.player_next(host->engine.user, iter, &snap)) >= 0 )
            if( snap.server_pid == a )
                return snap.element_id;
        return -1;
    }
    case TORIRS_ENTITY_LOC:
    {
        struct ToriRS_ScenerySnapshot snap;
        int iter = -1;
        while( (iter = host->engine.loc_next(host->engine.user, iter, &snap)) >= 0 )
            if( snap.tile_x == a && snap.tile_z == b && snap.level == c && snap.loc_id == d )
                return snap.element_id;
        return -1;
    }
    case TORIRS_ENTITY_OBJ:
    {
        struct ToriRS_GroundItemSnapshot snap;
        int iter = -1;
        while( (iter = host->engine.obj_next(host->engine.user, iter, &snap)) >= 0 )
            if( snap.tile_x == a && snap.tile_z == b && snap.level == c && snap.obj_id == d )
                return snap.element_id;
        return -1;
    }
    default:
        return -1;
    }
}

/** Bind every entity claim to its element for this frame. */
static void
plugin_entity_resolve_all(struct ToriRS_PluginHost* host)
{
    assert(host);
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
    {
        struct PluginEntityClaim* row = &host->entity_claims[i];
        if( row->plugin < 0 )
            continue;
        row->element_id = plugin_entity_element(host, row->part);
    }
}

/**
 * May `plugin` outline `element_id` right now?
 *
 * Yes unless ANOTHER plugin holds the APPEARANCE of the entity that element
 * is. An unclaimed entity is everybody's, which is what every highlighter
 * written before this tier expects; a claimed one is its holder's.
 */
static int
plugin_entity_hull_allowed(
    struct ToriRS_PluginHost* host,
    int plugin,
    int element_id)
{
    assert(host);
    if( element_id < 0 )
        return 1;
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
    {
        struct PluginEntityClaim const* row = &host->entity_claims[i];
        if( row->plugin < 0 || row->element_id != element_id )
            continue;
        if( (row->scopes & PLUGIN_ENTITY_APPEARANCE) && row->plugin != plugin )
            return 0;
    }
    return 1;
}

/** Paint every APPEARANCE holder's standing look. Called from the world
 *  draw, after the plugins' own drawing. */
static void
plugin_entity_paint_looks(struct ToriRS_PluginHost* host)
{
    assert(host);
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
    {
        struct PluginEntityClaim const* row = &host->entity_claims[i];
        if( row->plugin < 0 || row->element_id < 0 )
            continue;
        if( !(row->scopes & PLUGIN_ENTITY_APPEARANCE) || !row->look.hull )
            continue;
        if( !(host->plugins[row->plugin].enabled && host->plugins[row->plugin].running) )
            continue;
        (void)host->engine.draw_hull(
            host->engine.user,
            row->element_id,
            row->look.rgb,
            row->look.fill_alpha,
            row->look.shape);
    }
}

static int
api_entity_look(
    struct PluginContext* ctx,
    char const* part,
    struct ToriRS_EntityAppearance const* look)
{
    struct PluginEntityClaim* row;

    assert(ctx);
    assert(part);
    assert(look);
    assert(look->shape == TORIRS_HULL_BOUNDS || look->shape == TORIRS_HULL_MESH);

    row = plugin_entity_claim(ctx, part, PLUGIN_ENTITY_APPEARANCE);
    if( !row )
        return 0;
    row->look = *look;
    return 1;
}

static int
api_entity_ops(
    struct PluginContext* ctx,
    char const* part,
    int mode,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    struct PluginEntityClaim* row;

    assert(ctx);
    assert(part);

    if( mode < TORIRS_ENTITY_OPS_APPEND || mode > TORIRS_ENTITY_OPS_NONE )
        return 0;
    row = plugin_entity_claim(ctx, part, PLUGIN_ENTITY_HITBOX);
    if( !row )
        return 0;

    if( op_count < 0 )
        op_count = 0;
    if( op_count > TORIRS_PLUGIN_REGION_OPS_MAX )
        op_count = TORIRS_PLUGIN_REGION_OPS_MAX;
    row->op_count = 0;
    for( int i = 0; i < op_count; i++ )
    {
        if( !ops || !ops[i] || ops[i][0] == '\0' )
            continue;
        snprintf(row->ops[row->op_count], sizeof(row->ops[0]), "%s", ops[i]);
        row->op_count++;
    }
    row->tag = tag;
    row->ops_mode = (uint8_t)mode;
    row->has_ops = 1;
    return 1;
}

/**
 * The HITBOX holder whose entity a built menu row is about, or NULL.
 *
 * A row names its subject in server terms -- slot, pid, id -- and for a loc
 * or a ground item the tile comes from what is under the pointer, because a
 * menu is only ever built about the thing under the pointer.
 */
static struct PluginEntityClaim const*
plugin_entity_row_holder(
    struct ToriRS_PluginHost* host,
    struct ToriRS_MenuRow const* row,
    struct ToriRS_HoverTarget const* hover)
{
    assert(host);
    assert(row);
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
    {
        struct PluginEntityClaim const* claim = &host->entity_claims[i];
        int a;
        int b;
        int c;
        int d;
        int kind;

        if( claim->plugin < 0 || !claim->has_ops )
            continue;
        if( !(claim->scopes & PLUGIN_ENTITY_HITBOX) )
            continue;
        if( !(host->plugins[claim->plugin].enabled && host->plugins[claim->plugin].running) )
            continue;
        kind = plugin_entity_parse(claim->part, &a, &b, &c, &d);
        switch( kind )
        {
        case TORIRS_ENTITY_NPC:
            if( row->pick_kind == UI_MINIMENU_PICK_NPC && row->npc_slot == a )
                return claim;
            break;
        case TORIRS_ENTITY_PLAYER:
            if( row->pick_kind == UI_MINIMENU_PICK_PLAYER && row->player_pid == a )
                return claim;
            break;
        case TORIRS_ENTITY_LOC:
            if( row->pick_kind == UI_MINIMENU_PICK_SCENERY && row->target_id == d && hover &&
                hover->kind == TORIRS_HOVER_SCENERY && hover->tile_x == a &&
                hover->tile_z == b && hover->level == c )
                return claim;
            break;
        case TORIRS_ENTITY_OBJ:
            if( row->pick_kind == UI_MINIMENU_PICK_OBJ && row->target_id == d && hover &&
                hover->kind == TORIRS_HOVER_OBJ && hover->tile_x == a &&
                hover->tile_z == b && hover->level == c )
                return claim;
            break;
        default:
            break;
        }
    }
    return NULL;
}

/**
 * Apply every HITBOX holder's declaration to the menu just built: drop the
 * game's rows a REPLACE or NONE holder does not want, then add each holder's
 * own. Runs AFTER the plugins' on_menu_build, so a dropped row's text was
 * never handed to anybody stale.
 */
static void
plugin_entity_apply_ops(
    struct ToriRS_PluginHost* host,
    void* cursor,
    struct ToriRS_MenuBuildEvent const* menu)
{
    struct ToriRS_HoverTarget hover;
    int have_hover;
    struct PluginEntityClaim const* holders[TORIRS_PLUGIN_MENU_ROWS_MAX];
    int holder_count = 0;

    assert(host);
    assert(cursor);
    assert(menu);

    have_hover = host->engine.hover_entity(host->engine.user, &hover);

    /* Highest index first, so each drop leaves every lower index true. */
    for( int i = menu->row_count - 1; i >= 0; i-- )
    {
        struct PluginEntityClaim const* holder =
            plugin_entity_row_holder(host, &menu->rows[i], have_hover ? &hover : NULL);
        int seen = 0;

        if( !holder )
            continue;
        for( int j = 0; j < holder_count; j++ )
            if( holders[j] == holder )
                seen = 1;
        if( !seen && holder_count < TORIRS_PLUGIN_MENU_ROWS_MAX )
            holders[holder_count++] = holder;
        if( holder->ops_mode != TORIRS_ENTITY_OPS_APPEND )
            (void)host->engine.menu_drop(host->engine.user, cursor, i);
    }

    for( int h = 0; h < holder_count; h++ )
    {
        struct PluginEntityClaim const* holder = holders[h];
        if( holder->ops_mode == TORIRS_ENTITY_OPS_NONE )
            continue;
        /* Last op first: rows draw bottom-to-top, so adding in reverse puts
         * op 0 on top -- the same order a region's own verbs are added in. */
        for( int op = holder->op_count - 1; op >= 0; op-- )
        {
            int action;
            struct PluginMenuRoute* route;
            if( host->route_count >= TORIRS_PLUGIN_MENU_ROUTES_MAX )
                break;
            action = PLUGIN_MENU_ACTION_BASE + host->route_count;
            if( !host->engine.menu_add(host->engine.user, cursor, holder->ops[op], action) )
                break;
            route = &host->routes[host->route_count++];
            route->action = action;
            route->plugin = holder->plugin;
            route->tag = holder->tag;
        }
    }
}

static int
host_frame_surface_skin(
    struct PluginContext* ctx,
    int slot,
    int art,
    int mask)
{
    struct ToriRS_PluginHost* host;
    struct PluginLayoutSkin* staged;
    int x = 0, y = 0, w = 0, h = 0;

    assert(ctx);
    host = ctx->host;
    assert(host->layout_declaring && "frame skins are legal only during frame build");
    assert(host->layout_declarer == ctx->index);
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
    {
        plugin_layout_candidate_fail(host, "The frame skinned an unknown surface.");
        return 0;
    }
    if( slot != TORIRS_HOST_SURFACE_MINIMAP && slot != TORIRS_HOST_SURFACE_COMPASS )
    {
        plugin_layout_candidate_fail(host, "The frame skinned a surface that cannot be skinned.");
        return 0;
    }
    /* The minimap picture is the live baked world; only its cut-out belongs to
     * the frame. Compass art is static frame art and is replaceable. */
    if( slot == TORIRS_HOST_SURFACE_MINIMAP && art >= 0 )
    {
        plugin_layout_candidate_fail(host, "The frame replaced the live minimap image.");
        return 0;
    }
    /*
     * Not resident yet is the ORDINARY state for the first frames after a
     * load -- api_draw_image says the same and for the same reason -- so an
     * image still crossing the IO queue leaves the surface as it was rather
     * than skinning it with nothing. The layout pass runs again at the next
     * resize or rebuild, which is when the handle has pixels behind it.
     */
    if( art >= 0 )
    {
        struct PluginImage const* image = plugin_image_owned(ctx, art);
        if( !image || !image->published )
        {
            plugin_layout_candidate_fail(host, "The frame used foreign or unready skin artwork.");
            return 0;
        }
    }
    if( mask >= 0 )
    {
        struct PluginImage const* image = plugin_image_owned(ctx, mask);
        if( !image || !image->published )
        {
            plugin_layout_candidate_fail(host, "The frame used foreign or unready mask artwork.");
            return 0;
        }
    }
    staged = &host->layout_candidate.skins[slot];
    if( staged->declared )
    {
        plugin_layout_candidate_fail(host, "The frame skinned one surface twice.");
        return 0;
    }
    staged->declared = true;
    staged->art = art;
    staged->mask = mask;
    return host->engine.slot_rect(host->engine.user, slot, &x, &y, &w, &h);
}

static int
host_frame_surface_overlay(
    struct PluginContext* ctx,
    int slot,
    int image,
    int x,
    int y,
    int trans)
{
    struct PluginImage const* owned;
    struct ToriRS_PluginHost* host;
    struct PluginLayoutOverlay* staged;
    int sx = 0, sy = 0, sw = 0, sh = 0;

    assert(ctx);
    host = ctx->host;
    assert(host->layout_declaring && "frame overlays are legal only during frame build");
    assert(host->layout_declarer == ctx->index);
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
    {
        plugin_layout_candidate_fail(host, "The frame overlaid an unknown surface.");
        return 0;
    }
    if( trans < 0 || trans > 255 )
    {
        plugin_layout_candidate_fail(host, "The frame declared invalid overlay transparency.");
        return 0;
    }

    /* Unlike a skin, this is an ordinary sprite scene. Its natural dimensions
     * are read by the renderer after publication, so retaining a valid pending
     * handle is both safe and necessary: otherwise an initial frame build would
     * discard the declaration and the housing would not appear until resize. */
    owned = plugin_image_owned(ctx, image);
    if( !owned || !owned->published )
    {
        plugin_layout_candidate_fail(host, "The frame used foreign or unready overlay artwork.");
        return 0;
    }
    staged = &host->layout_candidate.overlays[slot];
    if( staged->declared )
    {
        plugin_layout_candidate_fail(host, "The frame overlaid one surface twice.");
        return 0;
    }
    staged->declared = true;
    staged->image = image;
    staged->x = x;
    staged->y = y;
    staged->trans = trans;
    return host->engine.slot_rect(host->engine.user, slot, &sx, &sy, &sw, &sh);
}

static int
host_frame_scrollbar(
    struct PluginContext* ctx,
    int trough,
    int dragger_top,
    int dragger_mid,
    int dragger_bottom,
    int arrow_up,
    int arrow_down)
{
    int images[6];
    struct ToriRS_PluginHost* host;
    int absent = 0;

    assert(ctx);
    host = ctx->host;
    assert(host->layout_declaring && "frame scrollbars are legal only during frame build");
    assert(host->layout_declarer == ctx->index);

    if( host->layout_candidate.scrollbar_declared )
    {
        plugin_layout_candidate_fail(host, "The frame declared its scrollbar twice.");
        return 0;
    }

    images[0] = trough;
    images[1] = dragger_top;
    images[2] = dragger_mid;
    images[3] = dragger_bottom;
    images[4] = arrow_up;
    images[5] = arrow_down;
    for( int i = 0; i < 6; i++ )
    {
        struct PluginImage const* image;
        /* Six absent pieces explicitly select the lane's scrollbar. A partial
         * set is neither that nor a complete skin and is rejected atomically. */
        if( images[i] < 0 )
        {
            absent++;
            continue;
        }
        image = plugin_image_owned(ctx, images[i]);
        if( !image || !image->published )
        {
            plugin_layout_candidate_fail(
                host, "The frame used foreign or unready scrollbar artwork.");
            return 0;
        }
    }
    if( absent != 0 && absent != 6 )
    {
        plugin_layout_candidate_fail(host, "The frame declared an incomplete scrollbar skin.");
        return 0;
    }
    host->layout_candidate.scrollbar_declared = true;
    host->layout_candidate.scrollbar_count = absent == 6 ? 0 : 6;
    if( absent == 0 )
        memcpy(host->layout_candidate.scrollbar, images, sizeof(images));
    return 1;
}

static void
api_draw_image(
    struct PluginContext* ctx,
    void* surface,
    int image,
    int x,
    int y,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    int trans)
{
    struct PluginImage const* slot = plugin_image_readable(ctx, image);

    if( !plugin_draw_allow(ctx, surface) )
        return;
    /* Not resident yet is ordinary during loading, so it draws nothing rather
     * than asserting. Foreign and stale handles also resolve to NULL. */
    if( !slot || !slot->published )
        return;
    ctx->draw_used += ctx->host->engine.draw_image(
        ctx->host->engine.user,
        image,
        x,
        y,
        slot->width,
        slot->height,
        clip_x,
        clip_y,
        clip_w,
        clip_h,
        trans);
}

/* -- canvas hit regions, and the one verb that acts -- */

static int
api_hit_region(
    struct PluginContext* ctx,
    void* surface,
    int x,
    int y,
    int w,
    int h,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    assert(ctx);
    /* The same window test the draw verbs make, and for the same reason: a
     * region declared outside the draw dispatch would go into a list the
     * engine has already read and answer clicks for a frame that is gone. */
    assert(ctx->host->draw_surface);
    assert(surface == ctx->host->draw_surface);
    /* Legal on both screen surfaces and not on the world one. A layout draws
     * its tab stones through FrameOffer.draw and they have to be clickable, so
     * the test is "not the world" rather than "the canvas": the world surface
     * is cut to the viewport and its coordinates are the scene's, which is
     * what a region cannot be expressed in. */
    assert(
        ctx->host->draw_canvas != PLUGIN_DRAW_SURFACE_WORLD &&
        "a hit region is a rectangle of the SCREEN; the world surface has none");
    (void)surface;

    if( w <= 0 || h <= 0 )
        return 0;
    if( !ops || op_count < 0 )
        op_count = 0;
    if( op_count > TORIRS_PLUGIN_REGION_OPS_MAX )
        op_count = TORIRS_PLUGIN_REGION_OPS_MAX;
    return ctx->host->engine.hit_region(
        ctx->host->engine.user, ctx->index, x, y, w, h, ops, op_count, tag);
}

static void
api_text_input(
    struct PluginContext* ctx,
    int on)
{
    assert(ctx);
    if( !ctx->host->engine.text_input )
        return;
    ctx->host->engine.text_input(ctx->host->engine.user, on ? 1 : 0);
}

static void
api_chat_focus(
    struct PluginContext* ctx,
    int on)
{
    assert(ctx);
    if( !ctx->host->engine.chat_focus )
        return;
    ctx->host->engine.chat_focus(ctx->host->engine.user, on ? 1 : 0);
}

static int
api_if_click(
    struct PluginContext* ctx,
    int component_id,
    int op)
{
    assert(ctx);
    /* Both are numbers a plugin read out of its own config, so both are bad
     * INPUT rather than broken contracts -- and a config that names no button
     * is the ordinary case, not a fault. */
    if( component_id < 0 || op < 0 || op > 10 )
        return 0;
    return ctx->host->engine.if_click(ctx->host->engine.user, component_id, op);
}

/* --------------------------------------------------------------- lifecycle */

struct ToriRS_PluginHost*
PluginHost_New(struct ToriRS_PluginEngine const* engine)
{
    assert(engine);
    assert(engine->screen);
    assert(engine->world_cycle);
    assert(engine->frame_ms);
    assert(engine->frame_work_us);
    assert(engine->frame_activate);
    assert(engine->layout_begin);
    assert(engine->layout_end);
    assert(engine->layout_slot);
    assert(engine->layout_slot_skin);
    assert(engine->layout_slot_overlay);
    assert(engine->layout_scrollbar);
    assert(engine->tab_active);
    assert(engine->tab_select);
    assert(engine->tab_enabled);
    assert(engine->local_player);
    assert(engine->npc_next);
    assert(engine->npc_by_slot);
    assert(engine->player_next);
    assert(engine->obj_next);
    assert(engine->key_held);
    assert(engine->loc_next);
    assert(engine->highlight_next);
    assert(engine->notify);
    assert(engine->hover_tile);
    assert(engine->hover_entity);
    assert(engine->element_height);
    assert(engine->feature_next);
    assert(engine->feature_get);
    assert(engine->feature_set);
    assert(engine->display_setting);
    assert(engine->display_setting_set);
    assert(engine->varbit);
    assert(engine->varp);
    assert(engine->project);
    assert(engine->draw_tile);
    assert(engine->draw_hull);
    assert(engine->draw_line);
    assert(engine->draw_text);
    assert(engine->draw_rect);
    assert(engine->menu_add);
    assert(engine->asset_read);
    assert(engine->asset_write);
    assert(engine->screenshot);
    assert(engine->model_publish);
    assert(engine->model_release);
    assert(engine->mesh_create);
    assert(engine->mesh_destroy);
    assert(engine->mesh_vertex);
    assert(engine->mesh_face);
    assert(engine->object_create);
    assert(engine->object_destroy);
    assert(engine->object_set_model);
    assert(engine->object_recolor);
    assert(engine->object_clear_recolors);
    assert(engine->object_set_anim);
    assert(engine->object_set_light);
    assert(engine->object_set_position);
    assert(engine->object_set_active);
    assert(engine->object_ready);
    assert(engine->hsl_from_rgb);
    assert(engine->hsl_to_rgb);
    assert(engine->mouse_pos);
    assert(engine->slot_rect);
    assert(engine->slot_member_rect);
    assert(engine->slot_native_size);
    assert(engine->component_rect);
    assert(engine->role_rect);
    assert(engine->role_visible);
    assert(engine->role_click);
    assert(engine->menu_drop);
    assert(engine->ui_boundary);
    assert(engine->stat);
    assert(engine->stat_xp);
    assert(engine->skill_name);
    assert(engine->run_energy);
    assert(engine->draw_select_canvas);
    assert(engine->image_publish);
    assert(engine->image_publish_argb);
    assert(engine->image_read);
    assert(engine->image_release);
    assert(engine->obj_image);
    assert(engine->loot_source_next);
    assert(engine->loot_row_next);
    assert(engine->draw_image);
    assert(engine->hit_region);
    assert(engine->if_click);
    assert(engine->obj_info);
    assert(engine->inv_slot);
    assert(engine->inv_size);

    struct ToriRS_PluginHost* host = calloc(1, sizeof(*host));
    assert(host);

    host->engine = *engine;
    host->dispatching = -1;
    host->dispatch_event = -1;
    host->panel_active = -1;
    host->panel_last_selected = -1;
    /* Zero is the invalid/stale sentinel carried by queued presenter work. */
    host->panel_selection_generation = 1;
    host->panel_registry_revision = 1;
    host->panel_model_revision = 1;
    /* The event is "it changed", never "here is what it is" -- so the baseline
     * is the answer at init, not a sentinel that would fire a phantom change
     * on the first frame. */
    host->last_screen = engine->screen(engine->user);
    PluginFrameCatalog_Init(&host->frame_catalog);
    ToriRS_UiRegistry_Init(&host->ui_registry);
    host->ui_presentation_revision =
        ToriRS_UiRegistry_Revision(&host->ui_registry);
    snprintf(
        host->frame_selection.requested_id, sizeof(host->frame_selection.requested_id), "%s", "auto");
    host->frame_selection.struct_size = sizeof(host->frame_selection);
    snprintf(
        host->frame_selection.active_id, sizeof(host->frame_selection.active_id), "%s", "core/native");
    host->frame_selection.status = TORIRS_FRAME_STATUS_NATIVE;
    host->frame_selection.revision = 1;
    host->frame_active_entry = -1;
    host->frame_target_entry = -1;
    host->frame_selection_dirty = 1;
    host->placement_cache_revision = -1;
    /* 0 is a real plugin index, so an empty reservation row needs a value of
     * its own rather than the calloc's zero. */
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        host->placement_reservations[i].plugin = -1;
        host->placement_resolved_reservations[i].plugin = -1;
    }
    /* Same reasoning again for the chrome tier's two tables. */
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
        host->entity_claims[i].plugin = -1;
    host->layout_declarer = -1;
    host->layout_candidate_entry = -1;
    /* -1 is the free marker and 0 is plugin index zero, so the calloc above
     * would have handed every image slot to the first plugin registered. */
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
        host->images[i].plugin = -1;
    for( int i = 0; i < TORIRS_PLUGIN_MAX; i++ )
        host->panel_icon_image[i] = -1;
    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
        host->models[i].plugin = -1;
    /* And the icon cache, whose free marker is the same -1 in two fields: an
     * entry is free when nobody owns it, and its image slot is free when it
     * points at none. */
    for( int i = 0; i < TORIRS_PLUGIN_OBJ_ICONS_MAX; i++ )
    {
        host->obj_icons[i].obj_id = -1;
        host->obj_icons[i].plugin = -1;
        host->obj_icons[i].image = -1;
    }

    return host;
}

void
PluginHost_Free(struct ToriRS_PluginHost* host)
{
    if( !host )
        return;

    plugin_ui_present_suppressions(
        host, host->ui_presentations, host->ui_presentation_count, false);

    /* Keep the same lifecycle ordering as runtime disable: the selected page
     * becomes invisible while its handler is still subscribed. */
    (void)plugin_panel_deactivate(host);

    for( int i = host->plugin_count - 1; i >= 0; i-- )
    {
        struct PluginContext* ctx = &host->plugins[i];
        if( !ctx->running )
            continue;
        host->dispatching = i;
        host->dispatch_event = PLUGIN_CALLBACK_STOP;
        if( ctx->def->callbacks.on_stop )
            ctx->def->callbacks.on_stop(
                &ctx->v2->runtime.api, ctx->v2->state);
        host->dispatching = -1;
        host->dispatch_event = -1;
        plugin_v2_shutdown(ctx);
        ctx->running = false;
        plugin_objects_destroy_all(host, ctx);
        plugin_meshes_destroy_all(host, ctx);
    }
    for( int i = host->asset_count - 1; i >= 0; i-- )
        plugin_asset_drop(host, &host->assets[i]);
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
        plugin_image_drop(host, i);
    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
        plugin_model_drop(host, i);
    for( int i = 0; i < host->plugin_count; i++ )
        if( host->plugins[i].v2 )
        {
            free(host->plugins[i].v2->state);
            host->plugins[i].v2->state = NULL;
            free(host->plugins[i].v2);
            host->plugins[i].v2 = NULL;
        }
    free(host);
}

/*
 * Fill in ctx->title from the def: the declared one, or one derived from the
 * name when the def carries none.
 *
 * Deriving rather than falling back to the raw name is the point. A roster of
 * `entity-highlighter` and `tile-indicator-lua` reads as a config file that
 * escaped onto the screen, and the reader who most needs the panel -- someone
 * who has never seen the source -- is exactly the one the slug tells nothing.
 * Separators become spaces and each word takes a capital, which turns every
 * id this tree uses into something sayable.
 *
 * Called again from PluginHost_Reload: a scripted plugin rewrites its def in
 * place, so a script that gained or changed a `title` comes back with it.
 */
static void
plugin_title_refresh(struct PluginContext* ctx)
{
    char const* at;
    size_t out = 0;
    bool word_start = true;

    assert(ctx);
    assert(ctx->def);

    if( ctx->def->title && ctx->def->title[0] )
    {
        plugin_copy_str(ctx->title, sizeof(ctx->title), ctx->def->title);
        return;
    }

    for( at = ctx->name; *at && out + 1 < sizeof(ctx->title); at++ )
    {
        char c = *at;
        if( c == '-' || c == '_' )
        {
            ctx->title[out++] = ' ';
            word_start = true;
            continue;
        }
        if( word_start && c >= 'a' && c <= 'z' )
            c = (char)(c - 'a' + 'A');
        ctx->title[out++] = c;
        word_start = false;
    }
    ctx->title[out] = '\0';
}

/** Read the one device preference after the preferences file has landed. */
static void
plugin_frame_preference_load(struct ToriRS_PluginHost* host)
{
    char requested[TORIRS_PLUGIN_FRAME_ID_MAX] = "auto";
    int migration = 0;

    assert(host);
    if( host->frame_preference_loaded )
        return;
    host->frame_preference_loaded = 1;

    if( host->engine.frame_preference )
        (void)host->engine.frame_preference(
            host->engine.user, requested, (int)sizeof(requested), &migration);
    if( !plugin_frame_preference_id_valid(requested) )
    {
        TORIRS_REPORT("plugin: invalid saved gameframe '%s'; using auto\n", requested);
        snprintf(requested, sizeof(requested), "%s", "auto");
    }

    snprintf(
        host->frame_selection.requested_id, sizeof(host->frame_selection.requested_id), "%s", requested);
    host->frame_selection_dirty = 1;
}

static void
plugin_frame_selection_active(
    struct ToriRS_PluginHost* host,
    char const* active,
    int status,
    char const* reason)
{
    char const* why = reason ? reason : "";

    assert(host);
    assert(active);
    if( strcmp(host->frame_selection.active_id, active) == 0 &&
        host->frame_selection.status == status && strcmp(host->frame_selection.reason, why) == 0 )
        return;
    snprintf(host->frame_selection.active_id, sizeof(host->frame_selection.active_id), "%s", active);
    host->frame_selection.status = status;
    snprintf(host->frame_selection.reason, sizeof(host->frame_selection.reason), "%s", why);
    host->frame_selection.revision++;
}

static void
plugin_frame_engine_activate(
    struct ToriRS_PluginHost* host,
    int entry_index)
{
    struct PluginFrameCatalogEntry const* entry;
    int owner = -1;
    int canvas = TORIRS_FRAME_CANVAS_WINDOW;
    int width = 0;
    int height = 0;

    assert(host);
    entry = PluginFrameCatalog_At(&host->frame_catalog, entry_index);
    if( entry )
    {
        owner = entry->plugin;
        canvas = entry->canvas;
        width = entry->width;
        height = entry->height;
    }
    if( host->frame_active_entry == entry_index && plugin_frame_owner(host) == owner &&
        host->layout_canvas == canvas && host->layout_fixed_w == width &&
        host->layout_fixed_h == height )
        return;

    host->frame_selection_epoch++;
    host->frame_active_entry = entry_index;
    host->layout_canvas = canvas;
    host->layout_fixed_w = width;
    host->layout_fixed_h = height;
    plugin_layout_publish(host);
}

static int
plugin_frame_provider_assets(
    struct ToriRS_PluginHost const* host,
    int plugin,
    char* reason,
    size_t reason_size)
{
    assert(host);
    assert(plugin >= 0);
    assert(plugin < host->plugin_count);
    assert(reason);
    assert(reason_size > 0);
    reason[0] = '\0';

    for( int i = 0; i < host->asset_count; i++ )
    {
        struct PluginAsset const* asset = &host->assets[i];
        if( asset->plugin != plugin )
            continue;
        if( asset->missing )
        {
            snprintf(
                reason,
                reason_size,
                "Required gameframe asset '%s' could not be loaded.",
                asset->name);
            return -1;
        }
        if( asset->pending )
        {
            snprintf(reason, reason_size, "Loading gameframe artwork (%s).", asset->name);
            return 0;
        }
    }
    return 1;
}

static char const*
plugin_frame_committed_id(struct ToriRS_PluginHost const* host)
{
    struct PluginFrameCatalogEntry const* entry;

    assert(host);
    entry = PluginFrameCatalog_At(&host->frame_catalog, host->frame_active_entry);
    return entry ? entry->id : "core/native";
}

static void
plugin_frame_target_set(
    struct ToriRS_PluginHost* host,
    int entry_index)
{
    assert(host);
    if( host->frame_target_entry == entry_index )
        return;
    host->frame_target_entry = entry_index;
    host->frame_layout_requested = 0;
    /* Fence a builder that changed the request from inside its own callback. */
    host->frame_selection_epoch++;
}

/**
 * Resolve the complete catalogue as one transaction. Registration order never
 * participates: the exact persisted id names the provider, or native does.
 */
static void
plugin_frame_resolve(struct ToriRS_PluginHost* host)
{
    struct PluginFrameCatalogEntry const* target = NULL;
    int target_index = -1;
    int wanted_plugin = -1;
    int committed_plugin;
    int asset_state = 1;
    char asset_reason[160];
    char const* committed_id;

    assert(host);
    if( host->frame_resolving )
        return;
    /* No providers means the native frame remains authoritative. */
    if( PluginFrameCatalog_Count(&host->frame_catalog) == 0 )
    {
        host->frame_selection_dirty = 0;
        return;
    }
    host->frame_resolving = 1;

    if( strcmp(host->frame_selection.requested_id, "auto") != 0 )
    {
        target_index =
            PluginFrameCatalog_Find(&host->frame_catalog, host->frame_selection.requested_id);
        target = PluginFrameCatalog_At(&host->frame_catalog, target_index);
        if( target && target->available )
            wanted_plugin = target->plugin;
    }

    /* Native is a complete candidate of its own. It can commit immediately;
     * no plugin callback or asset can make it partial. Off the game screen it
     * is also the only safe committed frame, while the requested provider may
     * stay warm for the next login. */
    if( strcmp(host->frame_selection.requested_id, "auto") == 0 )
    {
        plugin_frame_target_set(host, -1);
        plugin_frame_engine_activate(host, -1);
        plugin_frame_selection_active(host, "core/native", TORIRS_FRAME_STATUS_NATIVE, "");
        wanted_plugin = -1;
    }
    else if( host->engine.screen(host->engine.user) != TORIRS_SCREEN_GAME )
    {
        plugin_frame_target_set(host, target && target->available ? target_index : -1);
        plugin_frame_engine_activate(host, -1);
        plugin_frame_selection_active(
            host,
            "core/native",
            TORIRS_FRAME_STATUS_LOADING,
            "The requested gameframe will activate after login.");
    }
    else if( !target || !target->available ||
             (target->plugin >= 0 && host->plugins[target->plugin].refused) )
    {
        if( target && target->plugin >= 0 && host->plugins[target->plugin].refused )
            wanted_plugin = -1;
        plugin_frame_target_set(host, -1);
    }
    else
        plugin_frame_target_set(host, target_index);

    committed_plugin = plugin_frame_owner(host);

    /* Provider enablement is derived from selection. During a transition both
     * ends remain alive: the committed provider keeps rendering while the
     * requested provider starts and prepares a candidate. */
    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct PluginContext* ctx = &host->plugins[i];
        int const wanted = plugin_provides_frames(ctx) &&
                           (i == wanted_plugin || i == committed_plugin);

        if( !plugin_provides_frames(ctx) )
            continue;
        if( ctx->running && !wanted )
            plugin_teardown(host, i);
        ctx->enabled = wanted ? true : false;
    }
    if( target && target->available && host->plugins[target->plugin].running &&
        !host->plugins[target->plugin].refused )
        asset_state =
            plugin_frame_provider_assets(host, target->plugin, asset_reason, sizeof(asset_reason));

    committed_id = plugin_frame_committed_id(host);
    host->frame_layout_requested = 0;
    if( strcmp(host->frame_selection.requested_id, "auto") == 0 )
    {
        /* Already committed above. */
    }
    else if( host->engine.screen(host->engine.user) != TORIRS_SCREEN_GAME )
    {
        /* Native was committed above; keep the target provider warm. */
    }
    else if( !target )
    {
        plugin_frame_selection_active(
            host,
            committed_id,
            TORIRS_FRAME_STATUS_FALLBACK,
            "The requested gameframe is not installed in this build.");
    }
    else if( !target->available || host->plugins[target->plugin].refused )
    {
        plugin_frame_selection_active(
            host,
            committed_id,
            TORIRS_FRAME_STATUS_FALLBACK,
            host->plugins[target->plugin].error[0]
                ? host->plugins[target->plugin].error
                : "The requested gameframe is unavailable on this lane.");
    }
    else if( !host->plugins[target->plugin].running )
    {
        plugin_frame_selection_active(
            host,
            committed_id,
            TORIRS_FRAME_STATUS_LOADING,
            "Starting the requested gameframe provider.");
    }
    else if( asset_state < 0 )
    {
        plugin_frame_selection_active(
            host, committed_id, TORIRS_FRAME_STATUS_FALLBACK, asset_reason);
    }
    else if( asset_state == 0 )
    {
        plugin_frame_selection_active(
            host, committed_id, TORIRS_FRAME_STATUS_LOADING, asset_reason);
    }
    else if( host->frame_active_entry == target_index )
        plugin_frame_selection_active(host, target->id, TORIRS_FRAME_STATUS_ACTIVE, "");
    else
    {
        /* Ready to TRY, not ready to publish. PluginHost_Layout consumes this
         * request and commits only after the complete scratch declaration and
         * named tree validate. */
        host->frame_layout_requested = 1;
        plugin_frame_selection_active(
            host,
            committed_id,
            TORIRS_FRAME_STATUS_LOADING,
            "Validating the requested gameframe.");
    }

    host->frame_selection_dirty = 0;
    host->frame_resolving = 0;
}

static struct ToriRS_UiNodeRef
plugin_v2_ui_ref(
    void* user,
    struct PluginContext* context,
    char const* name)
{
    struct ToriRS_PluginHost* host = user;

    assert(host);
    assert(context);
    assert(context->host == host);
    (void)context;
    return PluginHost_UiRef(host, context->index, name);
}

static bool
plugin_v2_ui_info(
    void* user,
    struct PluginContext* context,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeInfo* out)
{
    struct ToriRS_PluginHost* host = user;

    assert(host);
    assert(context);
    assert(context->host == host);
    (void)context;
    return PluginHost_UiInfo(host, node, out);
}

static bool
plugin_v2_ui_invoke(
    void* user,
    struct PluginContext* context,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct ToriRS_PluginHost* host = user;

    assert(host);
    assert(context);
    assert(context->host == host);
    (void)context;
    return PluginHost_UiInvoke(host, node, action);
}

static bool
plugin_v2_ui_contribution_info_hook(
    void* user,
    struct PluginContext* context,
    char const* node,
    uint32_t facets,
    struct ToriRS_UiContributionInfo* out)
{
    struct ToriRS_UiContributionStatus status;
    struct ToriRS_UiContributionInfo snapshot;
    struct ToriRS_PluginHost* host = user;
    uint32_t capacity;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(out);
    (void)host;
    capacity = out->struct_size;
    if( capacity < TORIRS_UI_CONTRIBUTION_INFO_REQUIRED_SIZE )
        return false;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    if( facets == 0 || (facets & ~TORIRS_UI_FACET_ALL) != 0 ||
        !plugin_ui_contribution_status(context, node, &status) )
        return false;
    snapshot.state = status.state;
    snapshot.active_facets = status.active_facets & facets;
    (void)snprintf(
        snapshot.conflict_plugin,
        sizeof(snapshot.conflict_plugin),
        "%s",
        status.conflict_plugin);
    memcpy(out, &snapshot, capacity < sizeof(snapshot) ? capacity : sizeof(snapshot));
    return true;
}

static enum ToriRS_Result
plugin_v2_ui_update_image_ready(
    struct PluginContext* context,
    struct ToriRS_ImageRef image)
{
    int image_slot;
    struct PluginImage const* owned;

    assert(context);
    assert(context->v2);
    if( image.value == 0 )
        return TORIRS_RESULT_OK;
    image_slot = plugin_v2_runtime_image_slot(&context->v2->runtime, image);
    if( image_slot < 0 )
        return TORIRS_RESULT_INVALID;
    owned = plugin_image_owned(context, image_slot);
    if( !owned )
        return TORIRS_RESULT_INVALID;
    return owned->published ? TORIRS_RESULT_OK : TORIRS_RESULT_PENDING;
}

static enum ToriRS_Result
plugin_v2_ui_update_hook(
    void* user,
    struct PluginContext* context,
    struct ToriRS_UiNodeRef node,
    uint32_t facets,
    struct ToriRS_UiNode const* value)
{
    struct ToriRS_PluginHost* host = user;
    struct ToriRS_UiContributionRef contribution = { 0 };
    uint32_t revision;

    assert(host);
    assert(context);
    assert(value);
    if( context->host != host || node.value == 0 || !context->ui_contributions_registered ||
        !context->def->ui_contributions || facets == 0 ||
        (facets & ~TORIRS_UI_FACET_ALL) != 0 )
        return TORIRS_RESULT_INVALID;

    for( int i = 0; i < context->ui_contribution_count; i++ )
    {
        struct ToriRS_UiNodeRef const declared = ToriRS_UiRegistry_PrivateRef(
            &host->ui_registry, context->name, context->def->ui_contributions[i].node);
        if( declared.value != node.value )
            continue;
        if( (facets & ~context->def->ui_contributions[i].facets) != 0 )
            return TORIRS_RESULT_INVALID;
        contribution = context->ui_contribution_refs[i];
        break;
    }
    if( contribution.value == 0 )
        return TORIRS_RESULT_NOT_FOUND;

    if( (facets & TORIRS_UI_FACET_APPEARANCE) != 0 )
    {
        size_t const node_size =
            value->struct_size ? value->struct_size : TORIRS_UI_NODE_V2_0_SIZE;
        enum ToriRS_Result image_result =
            plugin_v2_ui_update_image_ready(context, value->image);
        if( image_result != TORIRS_RESULT_OK )
            return image_result;
        if( node_size >= offsetof(struct ToriRS_UiNode, state_images) +
                            sizeof(value->state_images) )
            for( int state = 0; state < TORIRS_UI_VISUAL_STATE_COUNT; state++ )
                if( (value->state_image_mask & (1u << state)) != 0 )
                {
                    image_result = plugin_v2_ui_update_image_ready(
                        context, value->state_images[state]);
                    if( image_result != TORIRS_RESULT_OK )
                        return image_result;
                }
    }

    revision = ToriRS_UiRegistry_Revision(&host->ui_registry);
    switch( ToriRS_UiRegistry_UpdateContribution(
        &host->ui_registry, contribution, facets, value) )
    {
    case TORIRS_UI_REGISTRY_OK:
        if( revision != ToriRS_UiRegistry_Revision(&host->ui_registry) )
        {
            host->placement_cache_valid = 0;
            host->layout_notify_pending = 1;
        }
        return TORIRS_RESULT_OK;
    case TORIRS_UI_REGISTRY_FULL:
        return TORIRS_RESULT_BUDGET;
    case TORIRS_UI_REGISTRY_DUPLICATE:
        return TORIRS_RESULT_CONFLICT;
    default:
        return TORIRS_RESULT_INVALID;
    }
}

static enum ToriRS_Result
plugin_v2_ui_set_enabled_hook(
    void* user,
    struct PluginContext* context,
    struct ToriRS_UiNodeRef node,
    bool enabled)
{
    struct ToriRS_PluginHost* host = user;
    struct ToriRS_UiContributionRef contribution = { 0 };
    uint32_t revision;

    assert(host);
    assert(context);
    if( context->host != host || node.value == 0 ||
        !context->ui_contributions_registered ||
        !context->def->ui_contributions )
        return TORIRS_RESULT_INVALID;
    for( int i = 0; i < context->ui_contribution_count; i++ )
    {
        struct ToriRS_UiNodeRef const declared = ToriRS_UiRegistry_PrivateRef(
            &host->ui_registry,
            context->name,
            context->def->ui_contributions[i].node);
        if( declared.value == node.value )
        {
            contribution = context->ui_contribution_refs[i];
            break;
        }
    }
    if( contribution.value == 0 )
        return TORIRS_RESULT_NOT_FOUND;
    revision = ToriRS_UiRegistry_Revision(&host->ui_registry);
    if( ToriRS_UiRegistry_SetContributionEnabled(
            &host->ui_registry, contribution, enabled) != TORIRS_UI_REGISTRY_OK )
        return TORIRS_RESULT_INVALID;
    if( revision != ToriRS_UiRegistry_Revision(&host->ui_registry) )
    {
        host->placement_cache_valid = 0;
        host->layout_notify_pending = 1;
    }
    return TORIRS_RESULT_OK;
}

static void
plugin_v2_frame_node_repoint(struct PluginV2FrameNode* node)
{
    assert(node);
    node->value.parent = node->parent[0] ? node->parent : NULL;
    node->value.label = node->label[0] ? node->label : NULL;
    node->value.action = node->action[0] ? node->action : NULL;
    for( int i = 0; i < TORIRS_UI_NAMED_ACTIONS_MAX; i++ )
        node->value.actions[i] =
            i < (int)node->value.action_count && node->actions[i][0] ? node->actions[i] : NULL;
}

static int
plugin_v2_copy_frame_node(
    struct PluginV2FrameNode* out,
    char const* name,
    struct ToriRS_UiNode const* node)
{
    size_t copy_size;
    int written;

    assert(out);
    assert(name);
    assert(node);
    copy_size = node->struct_size ? node->struct_size : TORIRS_UI_NODE_V2_0_SIZE;
    if( copy_size < TORIRS_UI_NODE_V2_0_SIZE )
        return 0;
    memset(out, 0, sizeof(*out));
    written = snprintf(out->name, sizeof(out->name), "%s", name);
    if( written <= 0 || written >= (int)sizeof(out->name) )
        return 0;
    if( copy_size > sizeof(out->value) )
        copy_size = sizeof(out->value);
    memcpy(&out->value, node, copy_size);
    out->value.struct_size = sizeof(out->value);
    if( node->parent )
    {
        written = snprintf(out->parent, sizeof(out->parent), "%s", node->parent);
        if( written < 0 || written >= (int)sizeof(out->parent) )
            return 0;
    }
    if( node->label )
    {
        written = snprintf(out->label, sizeof(out->label), "%s", node->label);
        if( written < 0 || written >= (int)sizeof(out->label) )
            return 0;
    }
    if( node->action )
    {
        written = snprintf(out->action, sizeof(out->action), "%s", node->action);
        if( written < 0 || written >= (int)sizeof(out->action) )
            return 0;
    }
    if( copy_size >= offsetof(struct ToriRS_UiNode, action_count) + sizeof(node->action_count) )
    {
        if( node->action_count > TORIRS_UI_NAMED_ACTIONS_MAX ||
            (node->action_count != 0 &&
             copy_size < offsetof(struct ToriRS_UiNode, actions) + sizeof(node->actions)) )
            return 0;
        for( uint32_t i = 0; i < node->action_count; i++ )
        {
            if( !node->actions[i] )
                return 0;
            written = snprintf(out->actions[i], sizeof(out->actions[i]), "%s", node->actions[i]);
            if( written <= 0 || written >= (int)sizeof(out->actions[i]) )
                return 0;
        }
    }
    plugin_v2_frame_node_repoint(out);
    return 1;
}

static void
plugin_v2_frame_ui_node(
    void* user,
    struct PluginContext* context,
    char const* name,
    struct ToriRS_UiNode const* node)
{
    struct ToriRS_PluginHost* host = user;
    struct PluginV2Instance* v2;
    struct ToriRS_UiNode normalized;
    struct ToriRS_UiNodeRef name_ref;
    char const* canonical_name;
    size_t node_size;

    assert(host);
    assert(context);
    assert(context->host == host);
    (void)host;
    v2 = context->v2;
    assert(v2);
    name_ref = ToriRS_UiRegistry_PrivateRef(&context->host->ui_registry, context->name, name);
    canonical_name = ToriRS_UiRegistry_Name(&context->host->ui_registry, name_ref);
    if( !canonical_name )
    {
        v2->frame_ui_candidate_invalid = true;
        return;
    }
    node_size = node->struct_size ? node->struct_size : TORIRS_UI_NODE_V2_0_SIZE;
    if( node_size < TORIRS_UI_NODE_V2_0_SIZE )
    {
        v2->frame_ui_candidate_invalid = true;
        return;
    }
    memset(&normalized, 0, sizeof(normalized));
    memcpy(&normalized, node, node_size < sizeof(normalized) ? node_size : sizeof(normalized));
    normalized.struct_size = sizeof(normalized);
    if( node->parent && node->parent[0] )
    {
        struct ToriRS_UiNodeRef const parent_ref =
            ToriRS_UiRegistry_PrivateRef(&context->host->ui_registry, context->name, node->parent);
        normalized.parent = ToriRS_UiRegistry_Name(&context->host->ui_registry, parent_ref);
        if( !normalized.parent )
        {
            v2->frame_ui_candidate_invalid = true;
            return;
        }
    }
    if( v2->frame_ui_candidate_count >= PLUGIN_V2_FRAME_UI_MAX )
    {
        v2->frame_ui_candidate_invalid = true;
        return;
    }
    for( int i = 0; i < v2->frame_ui_candidate_count; i++ )
        if( strcmp(v2->frame_ui_candidate[i].name, canonical_name) == 0 )
        {
            v2->frame_ui_candidate_invalid = true;
            return;
        }
    if( !plugin_v2_copy_frame_node(
            &v2->frame_ui_candidate[v2->frame_ui_candidate_count], canonical_name, &normalized) )
    {
        v2->frame_ui_candidate_invalid = true;
        return;
    }
    v2->frame_ui_candidate_count++;
}

static void
plugin_v2_image_release(
    void* user,
    struct PluginContext* context,
    struct ToriRS_ImageRef image)
{
    struct ToriRS_PluginHost* host = user;
    struct PluginV2Instance* v2;
    int image_slot;
    bool retained_by_frame = false;

    assert(host);
    assert(context);
    assert(context->host == host);
    v2 = context->v2;
    assert(v2);
    image_slot = plugin_v2_runtime_image_slot(&v2->runtime, image);
    if( image_slot < 0 )
        return;
    for( int i = 0; i < v2->frame_image_count; i++ )
        if( v2->frame_images[i].value == image.value )
        {
            retained_by_frame = true;
            break;
        }

    /* The engine's committed layout consumes internal image slots. Take a
     * frame that retained this exact token off-screen before freeing the slot;
     * otherwise a subsequent allocation in the same slot would repaint the
     * old frame with unrelated art before its next layout transaction. */
    if( retained_by_frame && plugin_frame_owner(host) == context->index )
    {
        plugin_frame_engine_activate(host, -1);
        plugin_frame_selection_active(
            host,
            "core/native",
            TORIRS_FRAME_STATUS_FALLBACK,
            "The selected gameframe released artwork it retained.");
        host->frame_selection_dirty = 1;
        v2->frame_image_count = 0;
    }
    api_image_release(context, image_slot);
}

static void
plugin_v2_model_release(
    void* user,
    struct PluginContext* context,
    struct ToriRS_ModelRef model)
{
    struct ToriRS_PluginHost* host = user;
    int model_slot;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(context->v2);
    model_slot =
        plugin_v2_runtime_model_slot(&context->v2->runtime, model);
    if( model_slot < 0 || model_slot >= TORIRS_PLUGIN_MODELS_MAX ||
        host->models[model_slot].plugin != context->index )
        return;
    plugin_model_drop(host, model_slot);
}

static void
plugin_v2_panel_select(
    void* user,
    struct PluginContext* context,
    char const* id,
    char const* label,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    struct ToriRS_PluginHost* host = user;
    struct ToriRS_PanelWidget* widget;
    int first;
    int slot;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(id);
    assert(value);
    if( option_count < 0 || option_count > TORIRS_PLUGIN_SELECT_OPTIONS_MAX ||
        (option_count > 0 && !options) ||
        strlen(value) >= TORIRS_PLUGIN_SELECT_VALUE_MAX ||
        host->panel_select_option_count + option_count > TORIRS_PLUGIN_SELECT_OPTIONS_MAX )
    {
        PluginHost_SetError(host, context->index, "structured select option budget exceeded");
        return;
    }
    for( int i = 0; i < option_count; i++ )
    {
        if( options[i].struct_size < TORIRS_SELECT_OPTION_REQUIRED_SIZE ||
            !options[i].value || !options[i].value[0] || !options[i].label ||
            strlen(options[i].value) >= TORIRS_PLUGIN_SELECT_VALUE_MAX )
        {
            PluginHost_SetError(host, context->index, "structured select option is invalid");
            return;
        }
        for( int j = 0; j < i; j++ )
            if( strcmp(options[i].value, options[j].value) == 0 )
            {
                PluginHost_SetError(
                    host,
                    context->index,
                    "structured select stable values must be unique");
                return;
            }
    }
    if( plugin_panel_find(host, id) >= 0 )
    {
        PluginHost_SetError(host, context->index, "structured select id is duplicated");
        return;
    }
    if( !api_panel_widget(context, TORIRS_PANEL_WIDGET_DROPDOWN, id, label) )
        return;
    slot = plugin_panel_find(host, id);
    assert(slot >= 0);
    widget = &host->panel_widgets[slot];
    first = host->panel_select_option_count;
    for( int i = 0; i < option_count; i++ )
    {
        struct ToriRS_PluginSelectOption* destination =
            &host->panel_select_options[first + i];

        memset(destination, 0, sizeof(*destination));
        plugin_copy_str(destination->value, sizeof(destination->value), options[i].value);
        plugin_copy_str(destination->label, sizeof(destination->label), options[i].label);
        plugin_copy_str(
            destination->detail,
            sizeof(destination->detail),
            options[i].detail ? options[i].detail : "");
        destination->enabled = options[i].enabled;
        if( strcmp(options[i].value, value) == 0 )
            widget->selected = i;
    }
    host->panel_select_option_count += option_count;
    widget->structured_select = true;
    widget->select_options = &host->panel_select_options[first];
    widget->select_option_count = option_count;
    widget->value = widget->selected;
    plugin_copy_str(widget->selected_value, sizeof(widget->selected_value), value);
    plugin_copy_str(widget->text, sizeof(widget->text), value);
    plugin_panel_bump(&host->panel_model_revision);
}

static enum ToriRS_Result
plugin_v2_panel_set_options(
    void* user,
    struct PluginContext* context,
    char const* id,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    struct ToriRS_PluginHost* host = user;
    struct ToriRS_PanelWidget* widget;
    int selected = -1;
    int slot;
    bool changed = false;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(id);
    assert(value);
    if( !plugin_panel_mutable(context, id, &slot) )
        return TORIRS_RESULT_NOT_FOUND;
    widget = &host->panel_widgets[slot];
    if( widget->kind != TORIRS_PANEL_WIDGET_DROPDOWN || !widget->structured_select ||
        option_count < 0 || option_count != widget->select_option_count ||
        (option_count > 0 && !options) ||
        strlen(value) >= TORIRS_PLUGIN_SELECT_VALUE_MAX )
        return TORIRS_RESULT_INVALID;
    for( int i = 0; i < option_count; i++ )
    {
        struct ToriRS_PluginSelectOption* destination = &widget->select_options[i];
        char const* detail;
        if( options[i].struct_size < TORIRS_SELECT_OPTION_REQUIRED_SIZE ||
            !options[i].value || !options[i].value[0] || !options[i].label ||
            strlen(options[i].value) >= TORIRS_PLUGIN_SELECT_VALUE_MAX )
            return TORIRS_RESULT_INVALID;
        for( int j = 0; j < i; j++ )
            if( strcmp(options[i].value, options[j].value) == 0 )
                return TORIRS_RESULT_INVALID;
        detail = options[i].detail ? options[i].detail : "";
        if( strcmp(destination->value, options[i].value) != 0 ||
            strcmp(destination->label, options[i].label) != 0 ||
            strcmp(destination->detail, detail) != 0 ||
            destination->enabled != options[i].enabled )
            changed = true;
        plugin_copy_str(destination->value, sizeof(destination->value), options[i].value);
        plugin_copy_str(destination->label, sizeof(destination->label), options[i].label);
        plugin_copy_str(destination->detail, sizeof(destination->detail), detail);
        destination->enabled = options[i].enabled;
        if( strcmp(options[i].value, value) == 0 )
            selected = i;
    }
    if( widget->selected != selected || strcmp(widget->selected_value, value) != 0 )
        changed = true;
    widget->selected = selected;
    widget->value = selected;
    plugin_copy_str(widget->selected_value, sizeof(widget->selected_value), value);
    plugin_copy_str(widget->text, sizeof(widget->text), value);
    if( changed )
    {
        plugin_panel_bump(&host->panel_model_revision);
        plugin_panel_change_widget(
            host,
            slot,
            TORIRS_PLUGIN_PANEL_CHANGE_OPTIONS |
                TORIRS_PLUGIN_PANEL_CHANGE_VALUE);
    }
    return TORIRS_RESULT_OK;
}

static bool
plugin_v2_capability(
    void* user,
    struct PluginContext* context,
    char const* name)
{
    struct ToriRS_PluginHost* host = user;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(name);
    (void)context;
    return host->engine.capability &&
           host->engine.capability(host->engine.user, name) != 0;
}

static size_t
plugin_v2_memory_bytes(
    void* user,
    struct PluginContext* context)
{
    struct ToriRS_PluginHost* host = user;
    assert(host);
    assert(context);
    assert(context->host == host);
    (void)context;
    return host->engine.memory_bytes
               ? host->engine.memory_bytes(host->engine.user)
               : 0;
}

static uint64_t
plugin_v2_loot_revision(
    void* user,
    struct PluginContext* context)
{
    struct ToriRS_PluginHost* host = user;
    assert(host);
    assert(context && context->host == host);
    (void)context;
    return host->engine.loot_revision
               ? host->engine.loot_revision(host->engine.user)
               : 0;
}

static bool
plugin_v2_loot_source_clear(
    void* user,
    struct PluginContext* context,
    int source_id)
{
    struct ToriRS_PluginHost* host = user;
    assert(host);
    assert(context && context->host == host);
    (void)context;
    return host->engine.loot_source_clear &&
           host->engine.loot_source_clear(host->engine.user, source_id) != 0;
}

static enum ToriRS_AssetState
plugin_v2_asset_slot_state(struct PluginAsset const* asset)
{
    if( !asset )
        return TORIRS_ASSET_ERROR;
    if( asset->ready )
        return TORIRS_ASSET_READY;
    if( asset->pending )
        return TORIRS_ASSET_PENDING;
    if( asset->missing )
        return TORIRS_ASSET_MISSING;
    return TORIRS_ASSET_ERROR;
}

static enum ToriRS_AssetState
plugin_v2_asset_request(
    void* user,
    struct PluginContext* context,
    char const* name)
{
    struct ToriRS_PluginHost* host = user;
    struct PluginAsset* asset;

    assert(host);
    assert(context);
    assert(context->host == host);
    if( !plugin_asset_name_ok(context, name) )
        return TORIRS_ASSET_INVALID;
    asset = plugin_asset_find(host, context->index, name);
    if( asset )
        return plugin_v2_asset_slot_state(asset);
    if( host->asset_count >= TORIRS_PLUGIN_ASSETS_MAX )
        return TORIRS_ASSET_BUDGET;
    (void)api_asset_load(context, name);
    return plugin_v2_asset_slot_state(
        plugin_asset_find(host, context->index, name));
}

static enum ToriRS_AssetState
plugin_v2_image_request(
    void* user,
    struct PluginContext* context,
    char const* name,
    int* out_image)
{
    struct ToriRS_PluginHost* host = user;
    struct PluginAsset* asset;
    enum ToriRS_AssetState state;
    int image = -1;
    int free_image = -1;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(out_image);
    *out_image = -1;
    if( !plugin_asset_name_ok(context, name) )
        return TORIRS_ASSET_INVALID;
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
    {
        if( host->images[i].plugin == context->index &&
            strcmp(host->images[i].asset, name) == 0 )
        {
            image = i;
            break;
        }
        if( host->images[i].plugin < 0 && free_image < 0 )
            free_image = i;
    }
    asset = plugin_asset_find(host, context->index, name);
    if( image < 0 )
    {
        if( free_image < 0 || (!asset && host->asset_count >= TORIRS_PLUGIN_ASSETS_MAX) )
            return TORIRS_ASSET_BUDGET;
        image = api_image_load(context, name);
        if( image < 0 )
            return TORIRS_ASSET_BUDGET;
        asset = plugin_asset_find(host, context->index, name);
    }
    if( host->images[image].published )
        state = TORIRS_ASSET_READY;
    else
    {
        state = plugin_v2_asset_slot_state(asset);
        if( state == TORIRS_ASSET_READY )
            state = TORIRS_ASSET_ERROR; /* bytes landed, decode did not */
    }
    if( state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY )
        *out_image = image;
    return state;
}

static enum ToriRS_AssetState
plugin_v2_model_request(
    void* user,
    struct PluginContext* context,
    char const* name,
    int* out_model)
{
    struct ToriRS_PluginHost* host = user;
    struct PluginAsset* asset;
    enum ToriRS_AssetState state;
    int model = -1;
    int free_model = -1;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(out_model);
    *out_model = -1;
    if( !plugin_asset_name_ok(context, name) )
        return TORIRS_ASSET_INVALID;
    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
    {
        if( host->models[i].plugin == context->index &&
            strcmp(host->models[i].asset, name) == 0 )
        {
            model = i;
            break;
        }
        if( host->models[i].plugin < 0 && free_model < 0 )
            free_model = i;
    }
    asset = plugin_asset_find(host, context->index, name);
    if( model < 0 )
    {
        if( free_model < 0 || (!asset && host->asset_count >= TORIRS_PLUGIN_ASSETS_MAX) )
            return TORIRS_ASSET_BUDGET;
        model = api_model_load(context, name);
        if( model < 0 )
            return TORIRS_ASSET_BUDGET;
        asset = plugin_asset_find(host, context->index, name);
    }
    if( host->models[model].published )
        state = TORIRS_ASSET_READY;
    else
    {
        state = plugin_v2_asset_slot_state(asset);
        if( state == TORIRS_ASSET_READY )
            state = TORIRS_ASSET_ERROR;
    }
    if( state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_READY )
        *out_model = model;
    return state;
}

static enum ToriRS_AssetState
plugin_v2_item_image(
    void* user,
    struct PluginContext* context,
    int obj_id,
    int count,
    int style,
    int* out_image,
    uint64_t* out_revision)
{
    struct ToriRS_PluginHost* host = user;
    int image;

    assert(host);
    assert(context);
    assert(context->host == host);
    assert(out_image);
    assert(out_revision);
    *out_image = -1;
    *out_revision = 0;
    if( obj_id < 0 || count < 0 || style < TORIRS_ITEM_ICON_PLAIN ||
        style > TORIRS_ITEM_ICON_SELECTED )
        return TORIRS_ASSET_INVALID;
    if( count == 0 )
        count = 1;
    image = api_obj_image(context, obj_id, count, style);
    if( image < 0 )
        return TORIRS_ASSET_PENDING;
    for( int i = 0; i < TORIRS_PLUGIN_OBJ_ICONS_MAX; i++ )
        if( host->obj_icons[i].plugin == context->index &&
            host->obj_icons[i].image == image &&
            host->obj_icons[i].obj_id == obj_id &&
            host->obj_icons[i].count == count &&
            host->obj_icons[i].style == style )
        {
            *out_image = image;
            *out_revision = host->obj_icons[i].revision;
            return TORIRS_ASSET_READY;
        }
    return TORIRS_ASSET_ERROR;
}

/* The V2 module runtime is part of the host translation unit so every module
 * reaches these ownership-checked primitives directly. */
#include "plugin/torirs_plugin_runtime.inc"

static int
plugin_v2_has_event_callback(
    struct ToriRS_PluginDefV2 const* def,
    enum PluginCallbackKind event)
{
    assert(def);
    switch( event )
    {
    case PLUGIN_CALLBACK_START:
        return def->callbacks.on_start != NULL;
    case PLUGIN_CALLBACK_STOP:
        return def->callbacks.on_stop != NULL;
    case PLUGIN_CALLBACK_FRAME_START:
        return def->callbacks.on_frame_start != NULL;
    case PLUGIN_CALLBACK_LOGIC_TICK:
        return def->callbacks.on_logic_tick != NULL;
    case PLUGIN_CALLBACK_SERVER_TICK:
        return def->callbacks.on_server_tick != NULL;
    case PLUGIN_CALLBACK_WORLD_LOADED:
        return def->callbacks.on_world_loaded != NULL;
    case PLUGIN_CALLBACK_SCREEN_CHANGE:
        return def->callbacks.on_screen_changed != NULL;
    case PLUGIN_CALLBACK_NPC_SPAWN:
        return def->callbacks.on_npc_spawn != NULL;
    case PLUGIN_CALLBACK_NPC_RETYPE:
        return def->callbacks.on_npc_retype != NULL;
    case PLUGIN_CALLBACK_NPC_DESPAWN:
        return def->callbacks.on_npc_despawn != NULL;
    case PLUGIN_CALLBACK_OBJ_SPAWN:
        return def->callbacks.on_item_spawn != NULL;
    case PLUGIN_CALLBACK_OBJ_COUNT:
        return def->callbacks.on_item_changed != NULL;
    case PLUGIN_CALLBACK_OBJ_DESPAWN:
        return def->callbacks.on_item_despawn != NULL;
    case PLUGIN_CALLBACK_CONFIG_CHANGED:
        return def->callbacks.on_config_changed != NULL;
    case PLUGIN_CALLBACK_ASSET:
        return def->callbacks.on_asset != NULL;
    case PLUGIN_CALLBACK_CHAT_MESSAGE:
        return def->callbacks.on_chat_message != NULL;
    case PLUGIN_CALLBACK_GAME_EVENT:
        return def->callbacks.on_game_event != NULL;
    case PLUGIN_CALLBACK_KEY:
        return def->callbacks.on_key != NULL;
    case PLUGIN_CALLBACK_MENU_BUILD:
        return def->callbacks.on_menu_build != NULL;
    case PLUGIN_CALLBACK_MENU_SELECT:
        return def->callbacks.on_menu_select != NULL;
    case PLUGIN_CALLBACK_DRAW_WORLD:
        return def->callbacks.on_draw_world != NULL;
    case PLUGIN_CALLBACK_DRAW_CANVAS:
        return def->callbacks.on_draw_canvas != NULL;
    case PLUGIN_CALLBACK_PANEL_BUILD:
        return def->callbacks.on_ui_build != NULL;
    case PLUGIN_CALLBACK_PANEL_ACTION:
        return def->callbacks.on_ui_action != NULL;
    case PLUGIN_CALLBACK_PANEL_DRAW:
        return def->callbacks.on_ui_draw != NULL;
    case PLUGIN_CALLBACK_PANEL_LAYOUT:
        return def->callbacks.on_ui_layout != NULL;
    case PLUGIN_CALLBACK_PLACEMENT_CHANGED:
        return def->callbacks.on_placement_changed != NULL;
    default:
        return 0;
    }
}

static enum ToriRS_CallbackResult
plugin_v2_event(
    struct PluginContext* ctx,
    void* event,
    void* userdata)
{
    enum PluginCallbackKind const kind = (enum PluginCallbackKind)((intptr_t)userdata - 1);
    struct PluginV2Instance* v2;
    struct ToriRS_ApiV2* api;
    void* state;

    assert(ctx);
    v2 = ctx->v2;
    assert(v2);
    api = &v2->runtime.api;
    state = v2->state;
    switch( kind )
    {
    case PLUGIN_CALLBACK_START:
        v2->definition->callbacks.on_start(api, state);
        break;
    case PLUGIN_CALLBACK_STOP:
        v2->definition->callbacks.on_stop(api, state);
        break;
    case PLUGIN_CALLBACK_FRAME_START:
        v2->definition->callbacks.on_frame_start(api, state, event);
        break;
    case PLUGIN_CALLBACK_LOGIC_TICK:
        v2->definition->callbacks.on_logic_tick(api, state, event);
        break;
    case PLUGIN_CALLBACK_SERVER_TICK:
        v2->definition->callbacks.on_server_tick(api, state, event);
        break;
    case PLUGIN_CALLBACK_WORLD_LOADED:
        v2->definition->callbacks.on_world_loaded(api, state, event);
        break;
    case PLUGIN_CALLBACK_SCREEN_CHANGE:
        v2->definition->callbacks.on_screen_changed(api, state, event);
        break;
    case PLUGIN_CALLBACK_NPC_SPAWN:
        v2->definition->callbacks.on_npc_spawn(api, state, event);
        break;
    case PLUGIN_CALLBACK_NPC_RETYPE:
        v2->definition->callbacks.on_npc_retype(api, state, event);
        break;
    case PLUGIN_CALLBACK_NPC_DESPAWN:
        v2->definition->callbacks.on_npc_despawn(api, state, event);
        break;
    case PLUGIN_CALLBACK_OBJ_SPAWN:
        v2->definition->callbacks.on_item_spawn(api, state, event);
        break;
    case PLUGIN_CALLBACK_OBJ_COUNT:
        v2->definition->callbacks.on_item_changed(api, state, event);
        break;
    case PLUGIN_CALLBACK_OBJ_DESPAWN:
        v2->definition->callbacks.on_item_despawn(api, state, event);
        break;
    case PLUGIN_CALLBACK_CONFIG_CHANGED:
        v2->definition->callbacks.on_config_changed(api, state, event);
        break;
    case PLUGIN_CALLBACK_ASSET:
        v2->definition->callbacks.on_asset(api, state, event);
        break;
    case PLUGIN_CALLBACK_CHAT_MESSAGE:
        v2->definition->callbacks.on_chat_message(api, state, event);
        break;
    case PLUGIN_CALLBACK_GAME_EVENT:
        v2->definition->callbacks.on_game_event(api, state, event);
        break;
    case PLUGIN_CALLBACK_KEY:
        return v2->definition->callbacks.on_key(api, state, event) == TORIRS_CALLBACK_CONSUME
                   ? TORIRS_CALLBACK_CONSUME
                   : TORIRS_CALLBACK_CONTINUE;
    case PLUGIN_CALLBACK_MENU_BUILD:
        return v2->definition->callbacks.on_menu_build(api, state, event) == TORIRS_CALLBACK_CONSUME
                   ? TORIRS_CALLBACK_CONSUME
                   : TORIRS_CALLBACK_CONTINUE;
    case PLUGIN_CALLBACK_MENU_SELECT:
        return v2->definition->callbacks.on_menu_select(api, state, event) ==
                       TORIRS_CALLBACK_CONSUME
                   ? TORIRS_CALLBACK_CONSUME
                   : TORIRS_CALLBACK_CONTINUE;
    case PLUGIN_CALLBACK_DRAW_WORLD:
    {
        struct PluginV2DrawScope scope;
        struct ToriRS_DrawBuilder builder;
        plugin_v2_runtime_draw_begin(&v2->runtime, event, &scope, &builder);
        v2->definition->callbacks.on_draw_world(api, state, &builder);
        plugin_v2_runtime_draw_end(&scope, &builder);
        break;
    }
    case PLUGIN_CALLBACK_DRAW_CANVAS:
    {
        struct PluginCanvasDispatch const* canvas = event;
        struct PluginV2DrawScope scope;
        struct ToriRS_DrawBuilder builder;
        plugin_v2_runtime_draw_begin(&v2->runtime, canvas->surface, &scope, &builder);
        plugin_v2_runtime_draw_region(&scope, canvas->bounds);
        v2->definition->callbacks.on_draw_canvas(api, state, &builder);
        plugin_v2_runtime_draw_end(&scope, &builder);
        break;
    }
    case PLUGIN_CALLBACK_PANEL_BUILD:
    {
        struct PluginV2PanelScope scope;
        struct ToriRS_PanelBuilder builder;
        plugin_v2_runtime_panel_begin(&v2->runtime, &scope, &builder);
        v2->definition->callbacks.on_ui_build(api, state, &builder, *(int const*)event);
        plugin_v2_runtime_panel_end(&scope, &builder);
        break;
    }
    case PLUGIN_CALLBACK_PANEL_ACTION:
        v2->definition->callbacks.on_ui_action(api, state, event);
        break;
    case PLUGIN_CALLBACK_PANEL_DRAW:
    {
        struct PluginV2DrawScope scope;
        struct ToriRS_DrawBuilder builder;
        struct PluginPanelDraw const* draw = event;
        plugin_v2_runtime_draw_begin(&v2->runtime, draw->surface, &scope, &builder);
        plugin_v2_runtime_draw_region(
            &scope,
            (struct ToriRS_Rect){ draw->x, draw->y, draw->width, draw->height });
        v2->definition->callbacks.on_ui_draw(api, state, draw->id, &builder);
        plugin_v2_runtime_draw_end(&scope, &builder);
        break;
    }
    case PLUGIN_CALLBACK_PANEL_LAYOUT:
        v2->definition->callbacks.on_ui_layout(api, state, event);
        break;
    default:
        assert(!"unmapped v2 callback event");
    }
    return TORIRS_CALLBACK_CONTINUE;
}

static void
plugin_v2_init(
    struct PluginContext* ctx)
{
    struct PluginV2Instance* v2;
    bool initialized;

    assert(ctx);
    v2 = ctx->v2;
    assert(v2);
    assert(!v2->state);
    if( v2->definition->state_size > 0 )
    {
        v2->state = calloc(1, v2->definition->state_size);
        assert(v2->state);
    }
    initialized = v2->runtime_initialized_once
                      ? plugin_v2_runtime_reinit(&v2->runtime, ctx)
                      : plugin_v2_runtime_init(&v2->runtime, ctx);
    assert(initialized);
    if( !initialized )
        return;
    v2->runtime_initialized_once = true;
}

static void
plugin_v2_shutdown(struct PluginContext* ctx)
{
    struct PluginV2Instance* v2;

    assert(ctx);
    v2 = ctx->v2;
    assert(v2);
    free(v2->state);
    v2->state = NULL;
    plugin_v2_runtime_reset(&v2->runtime);
    v2->frame_ui_count = 0;
    v2->frame_ui_candidate_count = 0;
    v2->frame_ui_candidate_invalid = false;
    memset(v2->frame_images, 0, sizeof(v2->frame_images));
    memset(v2->frame_images_candidate, 0, sizeof(v2->frame_images_candidate));
    v2->frame_image_count = 0;
    v2->frame_image_candidate_count = 0;
}

static int
plugin_ui_contributions_start(struct PluginContext* ctx)
{
    int count = 0;

    assert(ctx);
    if( !ctx->def->ui_contributions || ctx->ui_contributions_registered )
        return 1;
    while( ctx->def->ui_contributions[count].node )
        count++;
    if( count == 0 )
    {
        ctx->ui_contributions_registered = true;
        return 1;
    }
    if( count > PLUGIN_UI_CONTRIBUTIONS_MAX )
    {
        PluginHost_SetError(
            ctx->host, ctx->index, "Its named UI declaration count exceeds the per-plugin budget.");
        return 0;
    }

    ctx->ui_contribution_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_UiContributionRef ref;
        if( ToriRS_UiRegistry_AddContribution(
                &ctx->host->ui_registry, ctx->name, &ctx->def->ui_contributions[i], &ref) !=
            TORIRS_UI_REGISTRY_OK )
        {
            (void)ToriRS_UiRegistry_RemovePlugin(&ctx->host->ui_registry, ctx->name);
            memset(ctx->ui_contribution_refs, 0, sizeof(ctx->ui_contribution_refs));
            ctx->ui_contribution_count = 0;
            PluginHost_SetError(
                ctx->host, ctx->index, "Its named UI declarations are invalid for this build.");
            return 0;
        }
        ctx->ui_contribution_refs[ctx->ui_contribution_count++] = ref;
    }
    ctx->ui_contributions_registered = true;
    ctx->host->placement_cache_valid = 0;
    return 2;
}

static int
plugin_v2_id_valid(char const* id)
{
    size_t length;

    if( !id )
        return 0;
    length = strlen(id);
    if( length == 0 || length >= TORIRS_PLUGIN_NAME_MAX || id[0] == '-' || id[length - 1] == '-' )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = id[i];
        if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' )
            continue;
        return 0;
    }
    return 1;
}

static bool
plugin_v2_field_available(
    uint32_t struct_size,
    size_t offset,
    size_t field_size)
{
    return struct_size >= offset + field_size;
}

#define PLUGIN_V2_FIELD_AVAILABLE(pointer, type, field)                                  \
    plugin_v2_field_available(                                                           \
        (pointer)->struct_size, offsetof(struct type, field), sizeof((pointer)->field))

static void
plugin_v2_order_insert(
    struct ToriRS_PluginHost* host,
    int plugin)
{
    int const count = host->plugin_count;
    int event_at = count;
    int draw_at = count;

    for( int i = 0; i < count; i++ )
        if( host->plugins[plugin].def->event_priority >
            host->plugins[host->event_order[i]].def->event_priority )
        {
            event_at = i;
            break;
        }
    for( int i = 0; i < count; i++ )
        if( host->plugins[plugin].def->draw_order <
            host->plugins[host->draw_order[i]].def->draw_order )
        {
            draw_at = i;
            break;
        }
    for( int i = count; i > event_at; i-- )
        host->event_order[i] = host->event_order[i - 1];
    for( int i = count; i > draw_at; i-- )
        host->draw_order[i] = host->draw_order[i - 1];
    host->event_order[event_at] = plugin;
    host->draw_order[draw_at] = plugin;

    for( int event = 0; event < PLUGIN_CALLBACK_COUNT; event++ )
        if( plugin_v2_has_event_callback(
                host->plugins[plugin].def,
                (enum PluginCallbackKind)event) )
            host->callback_count[event]++;
}

int
PluginHost_RegisterV2(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginDefV2 const* def)
{
    static uint32_t const KNOWN_FLAGS = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT |
                                        TORIRS_PLUGIN_V2_ESSENTIAL | TORIRS_PLUGIN_V2_RUNTIME_HOST |
                                        TORIRS_PLUGIN_V2_HIDDEN;
    struct PluginV2Instance* v2;
    struct ToriRS_FrameOffer const* source_frames = NULL;
    struct ToriRS_UiContribution const* contributions = NULL;
    uint32_t flags = 0;
    int event_priority = 0;
    int draw_order = 0;
    int frame_count = 0;
    int schema_count = 0;
    int index;

    assert(host);
    assert(def);
    if( def->struct_size < TORIRS_PLUGIN_DEF_V2_REQUIRED_SIZE ||
        !plugin_v2_id_valid(def->id) || !def->title ||
        !def->title[0] || strlen(def->title) >= TORIRS_PLUGIN_TITLE_MAX || !def->version ||
        !def->version[0] ||
        def->callbacks.struct_size < TORIRS_PLUGIN_CALLBACKS_REQUIRED_SIZE ||
        def->callbacks.struct_size >
            def->struct_size - offsetof(struct ToriRS_PluginDefV2, callbacks) ||
        def->state_size > 1024u * 1024u )
    {
        TORIRS_ERR("plugin: invalid v2 definition refused\n");
        return -1;
    }
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDefV2, frames) )
        source_frames = def->frames;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDefV2, ui_contributions) )
        contributions = def->ui_contributions;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDefV2, flags) )
        flags = def->flags;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDefV2, event_priority) )
        event_priority = def->event_priority;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDefV2, draw_order) )
        draw_order = def->draw_order;
    if( (flags & ~KNOWN_FLAGS) != 0 )
    {
        TORIRS_ERR("plugin: v2 plugin '%s' has unknown policy flags\n", def->id);
        return -1;
    }
    if( def->config &&
        (def->config->struct_size <
             offsetof(struct ToriRS_ConfigSchema, items) + sizeof(def->config->items) ||
         !def->config->items) )
    {
        TORIRS_ERR("plugin: v2 plugin '%s' has an invalid config schema\n", def->id);
        return -1;
    }
    {
        int schema_row;
        enum PluginConfigSchemaResult const schema_result = plugin_config_schema_validate(
            def->config ? def->config->items : NULL, &schema_count, &schema_row);
        if( schema_result != PLUGIN_CONFIG_SCHEMA_OK )
        {
            TORIRS_ERR(
                "plugin: v2 plugin '%s' config schema has %s at item %d\n",
                def->id,
                plugin_config_schema_result_text(schema_result),
                schema_row + 1);
            return -1;
        }
    }

    v2 = calloc(1, sizeof(*v2));
    assert(v2);
    memcpy(
        &v2->callbacks_storage,
        &def->callbacks,
        def->callbacks.struct_size < sizeof(v2->callbacks_storage)
            ? def->callbacks.struct_size
            : sizeof(v2->callbacks_storage));
    v2->callbacks_storage.struct_size = sizeof(v2->callbacks_storage);

    if( source_frames )
    {
        while( source_frames[frame_count].id )
        {
            struct ToriRS_FrameOffer const* offer = &source_frames[frame_count];
            struct ToriRS_FrameOffer* stored;
            if( frame_count >= TORIRS_PLUGIN_FRAME_OFFERS_MAX ||
                offer->struct_size < TORIRS_FRAME_OFFER_REQUIRED_SIZE ||
                !offer->title || !offer->title[0] ||
                !offer->build ||
                (offer->canvas != TORIRS_FRAME_CANVAS_FIXED &&
                 offer->canvas != TORIRS_FRAME_CANVAS_WINDOW) ||
                (offer->canvas == TORIRS_FRAME_CANVAS_FIXED &&
                 (offer->width <= 0 || offer->height <= 0)) ||
                (offer->canvas == TORIRS_FRAME_CANVAS_WINDOW &&
                 (offer->min_width <= 0 || offer->min_height <= 0)) )
            {
                TORIRS_ERR(
                    "plugin: v2 frame offer %d from '%s' is invalid\n", frame_count, def->id);
                free(v2);
                return -1;
            }
            stored = &v2->frame_offers[frame_count];
            stored->struct_size = sizeof(*stored);
            stored->id = offer->id;
            stored->title = offer->title;
            stored->canvas = offer->canvas;
            stored->width = offer->width;
            stored->height = offer->height;
            stored->min_width = offer->min_width;
            stored->min_height = offer->min_height;
            stored->build = offer->build;
            if( PLUGIN_V2_FIELD_AVAILABLE(offer, ToriRS_FrameOffer, draw) )
                stored->draw = offer->draw;
            frame_count++;
        }
    }

    v2->definition_storage.struct_size = sizeof(v2->definition_storage);
    v2->definition_storage.id = def->id;
    v2->definition_storage.title = def->title;
    v2->definition_storage.version = def->version;
    v2->definition_storage.state_size = def->state_size;
    if( def->config )
    {
        v2->config_storage.struct_size = sizeof(v2->config_storage);
        v2->config_storage.items = def->config->items;
        v2->definition_storage.config = &v2->config_storage;
    }
    v2->definition_storage.callbacks = v2->callbacks_storage;
    v2->definition_storage.frames = frame_count > 0 ? v2->frame_offers : NULL;
    v2->definition_storage.ui_contributions = contributions;
    v2->definition_storage.flags = flags;
    v2->definition_storage.event_priority = event_priority;
    v2->definition_storage.draw_order = draw_order;
    v2->definition = &v2->definition_storage;
    v2->frame_count = frame_count;

    if( host->plugin_count >= TORIRS_PLUGIN_MAX )
    {
        TORIRS_ERR("plugin: table full, refusing '%s'\n", def->id);
        free(v2);
        return -1;
    }
    if( PluginHost_IndexOf(host, def->id) >= 0 )
    {
        TORIRS_ERR(
            "plugin: '%s' is already registered; plugin ids must be unique\n",
            def->id);
        free(v2);
        return -1;
    }
    index = host->plugin_count;
    if( frame_count > 0 )
    {
        enum PluginFrameCatalogResult const result = PluginFrameCatalog_Add(
            &host->frame_catalog, index, def->id, v2->frame_offers);
        if( result != PLUGIN_FRAME_CATALOG_OK )
        {
            char const* why = result == PLUGIN_FRAME_CATALOG_DUPLICATE
                                  ? "a duplicate canonical id"
                              : result == PLUGIN_FRAME_CATALOG_FULL
                                  ? "the frame catalogue is full"
                                  : "an invalid id, title, canvas policy, or size";
            TORIRS_ERR(
                "plugin: '%s' has invalid frame offers (%s); refusing the provider\n",
                def->id,
                why);
            free(v2);
            return -1;
        }
    }
    if( contributions )
    {
        struct ToriRS_UiRegistry* validation;
        int contribution_count = 0;
        while( contributions[contribution_count].node )
            contribution_count++;
        if( contribution_count > PLUGIN_UI_CONTRIBUTIONS_MAX )
        {
            if( frame_count > 0 )
                PluginFrameCatalog_RemovePlugin(&host->frame_catalog, index);
            TORIRS_ERR(
                "plugin: '%s' declares %d named UI contributions; the per-plugin budget is %d\n",
                def->id,
                contribution_count,
                PLUGIN_UI_CONTRIBUTIONS_MAX);
            free(v2);
            return -1;
        }
        validation = malloc(sizeof(*validation));
        assert(validation);
        *validation = host->ui_registry;
        for( int i = 0; i < contribution_count; i++ )
        {
            struct ToriRS_UiContributionRef contribution;
            enum ToriRS_UiRegistryResult const result = ToriRS_UiRegistry_AddContribution(
                validation, def->id, &contributions[i], &contribution);
            if( result != TORIRS_UI_REGISTRY_OK )
            {
                free(validation);
                if( frame_count > 0 )
                    PluginFrameCatalog_RemovePlugin(&host->frame_catalog, index);
                TORIRS_ERR(
                    "plugin: '%s' has an invalid named-UI contribution at row %d\n",
                    def->id,
                    i);
                free(v2);
                return -1;
            }
        }
        free(validation);
    }

    {
        struct PluginContext* ctx = &host->plugins[index];
        memset(ctx, 0, sizeof(*ctx));
        ctx->host = host;
        ctx->def = v2->definition;
        ctx->v2 = v2;
        ctx->index = index;
        ctx->enabled = (flags & TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT) == 0;
        snprintf(ctx->name, sizeof(ctx->name), "%s", def->id);
        plugin_title_refresh(ctx);
        plugin_config_seed(ctx, schema_count);
    }
    plugin_v2_order_insert(host, index);
    host->plugin_count++;
    return index;
}

void
PluginHost_Start(struct ToriRS_PluginHost* host)
{
    bool contributions_changed = false;

    assert(host);

    plugin_frame_preference_load(host);
    plugin_frame_resolve(host);

    /* Publish the complete retained declaration set before any newly started
     * plugin runs. An on_start query therefore cannot observe only the
     * contributions of plugins that happened to register before it. */
    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct PluginContext* ctx = &host->plugins[i];
        if( ctx->running || !ctx->enabled || ctx->refused )
            continue;
        int const contribution_result = plugin_ui_contributions_start(ctx);
        if( contribution_result == 0 )
        {
            ctx->refused = true;
            continue;
        }
        if( contribution_result == 2 )
            contributions_changed = true;
    }
    if( contributions_changed )
        PluginHost_LayoutChanged(host);

    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct PluginContext* ctx = &host->plugins[i];
        /* `refused` and not `enabled`, so a plugin that stood down on this
         * lane stays down for every later Start -- a script finishing its load
         * runs this again, and without the flag every refusal would be
         * reconsidered, re-taken and re-logged on each one. */
        if( ctx->running || !ctx->enabled || ctx->refused )
            continue;
        ctx->running = true;
        host->dispatching = i;
        plugin_v2_init(ctx);
        host->dispatching = -1;

        struct ToriRS_FrameEvent ev = { 0 };
        plugin_dispatch_one(host, i, PLUGIN_CALLBACK_START, &ev);
    }
    /* The first pass selected which provider should run; this pass can commit
     * it now that its init/START callbacks have completed. */
    plugin_frame_resolve(host);
}

/**
 * Take one plugin down: stop it and release everything it holds.
 *
 * The teardown half of both disabling and reloading, in one place because they
 * ARE the same teardown -- and because getting it half right is how a reload
 * leaves a beam burning in the world or a tab in the window dispatching to a
 * plugin that is no longer running. Leaves `enabled` alone: what the caller
 * means to do next is the caller's business.
 */
static void
plugin_teardown(
    struct ToriRS_PluginHost* host,
    int plugin_index)
{
    struct PluginContext* ctx = plugin_at(host, plugin_index);
    bool ui_changed = false;

    if( ctx->tearing_down )
        return;
    ctx->tearing_down = true;

    /* Selection is released first: the plugin learns it became invisible
     * while its subscriptions and page model still exist, and no later STOP
     * callback can leave interactive chrome behind. */
    plugin_panel_unregister(host, plugin_index);

    if( ctx->running )
    {
        host->dispatching = plugin_index;
        host->dispatch_event = PLUGIN_CALLBACK_STOP;
        if( ctx->def->callbacks.on_stop )
            ctx->def->callbacks.on_stop(
                &ctx->v2->runtime.api, ctx->v2->state);
        host->dispatching = -1;
        host->dispatch_event = -1;
        /* on_stop observes the plugin as live. From this point onward no
         * direct retained-state notifier may target it: v2 shutdown frees
         * state and clears the runtime before reservation/UI cleanup can
         * publish placement changes. */
        ctx->running = false;
        plugin_v2_shutdown(ctx);
    }
    /* Geometry and bytes leave with the stopped instance. */
    plugin_objects_destroy_all(host, ctx);
    plugin_meshes_destroy_all(host, ctx);
    plugin_assets_drop_plugin(host, plugin_index);
    plugin_obj_icons_drop_plugin(host, plugin_index);
    plugin_images_drop_plugin(host, plugin_index);
    /* Reservations go too, and that is what makes `reserve` safe to use: a
     * dock that is switched off gives its edge back without anybody asking,
     * and the readouts beside it widen on the next frame. */
    plugin_placement_reservations_drop_plugin(host, plugin_index);
    plugin_models_drop_plugin(host, plugin_index);
    /* Drop its retained entity appearance/action declarations. */
    plugin_entity_drop_plugin(host, plugin_index);
    if( ctx->ui_contributions_registered )
    {
        ui_changed = ToriRS_UiRegistry_RemovePlugin(&host->ui_registry, ctx->name) > 0;
        ctx->ui_contributions_registered = false;
        memset(ctx->ui_contribution_refs, 0, sizeof(ctx->ui_contribution_refs));
        ctx->ui_contribution_count = 0;
        host->placement_cache_valid = 0;
    }
    if( ui_changed )
        plugin_ui_present_reconcile(host);
    /* The tab goes with them: a stopped plugin's controls would otherwise sit
     * in the window still taking clicks, dispatching to a plugin that is not
     * running and silently doing nothing. */
    /* The frame goes back to the lane. A layout plugin that stopped while
     * holding it would leave the client with the client's chrome suppressed
     * and nobody drawing any: a black surround and an inventory floating in
     * it. */
    if( plugin_frame_owner(host) == plugin_index )
    {
        plugin_frame_engine_activate(host, -1);
        plugin_frame_selection_active(
            host,
            "core/native",
            TORIRS_FRAME_STATUS_FALLBACK,
            "The committed gameframe provider stopped.");
    }
    if( host->frame_target_entry >= 0 )
    {
        struct PluginFrameCatalogEntry const* target =
            PluginFrameCatalog_At(&host->frame_catalog, host->frame_target_entry);
        if( target && target->plugin == plugin_index )
            plugin_frame_target_set(host, -1);
    }
    ctx->tearing_down = false;
    if( ui_changed )
        PluginHost_LayoutChanged(host);
}

void
PluginHost_SetEnabled(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    bool enabled)
{
    assert(host);

    struct PluginContext* ctx = plugin_at(host, plugin_index);
    /* A frame provider's runtime state is derived from the one Gameframe
     * preference. It deliberately has no independent checkbox. */
    if( plugin_provides_frames(ctx) )
        return;
    /* `refused` is part of the state being asked about: a plugin that stood
     * down reads as off in the roster, so the switch the user then flips asks
     * for a state it already nominally has, and returning here would make that
     * click do nothing at all. */
    if( ctx->enabled == enabled && !ctx->refused )
        return;

    /*
     * An essential plugin has one state. Refused rather than asserted because
     * the ask does not only come from the panel -- which draws no switch for
     * it -- but from a plugin_prefs.ini written by a build where the plugin
     * was ordinary, and a saved line is not a caller's bug to abort on.
     */
    if( !enabled && plugin_policy(ctx, TORIRS_PLUGIN_V2_ESSENTIAL) )
        return;

    /* Enable state is saved state: without this a panel toggle would hold for
     * the session and be forgotten at the next launch. */
    host->config_dirty = true;

    if( !enabled )
    {
        plugin_teardown(host, plugin_index);
        ctx->enabled = false;
        ctx->refused = false;
        return;
    }

    ctx->enabled = true;
    /* Asked for explicitly, so the lane question goes back to the plugin. It
     * may well stand down again -- the lane has not changed -- and that is the
     * honest answer to the click: the reason lands beside the row the switch
     * is on. */
    ctx->refused = false;
    PluginHost_Start(host);
}

void
PluginHost_SetReloadHandler(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    void (*handler)(struct ToriRS_PluginHost*, int, void*),
    void* user)
{
    struct PluginContext* context;
    assert(host);
    context = plugin_at(host, plugin_index);
    context->reload_handler = handler;
    context->reload_user = handler ? user : NULL;
}

void
PluginHost_Reload(
    struct ToriRS_PluginHost* host,
    int plugin_index)
{
    struct PluginContext* ctx;
    bool panel_was_selected;
    int schema_count;

    assert(host);
    ctx = plugin_at(host, plugin_index);

    /* A disabled plugin has nothing to reload: it is already torn down, and
     * restarting it here would switch it on behind the user's back. */
    if( !ctx->enabled )
        return;

    panel_was_selected = host->panel_active == plugin_index;
    plugin_teardown(host, plugin_index);

    /*
     * The runtime's chance to rebuild from source. It may rewrite the def in
     * place -- a script that grew a config key or a handler comes back with
     * it -- so everything below rereads through ctx->def rather than caching
     * anything from before this call.
     */
    if( ctx->reload_handler )
        ctx->reload_handler(host, plugin_index, ctx->reload_user);

    /* Reread through the new def, for the same reason as the schema below. */
    plugin_title_refresh(ctx);

    {
        int schema_row;
        enum PluginConfigSchemaResult const schema_result = plugin_config_schema_validate(
            plugin_schema(ctx), &schema_count, &schema_row);
        if( schema_result != PLUGIN_CONFIG_SCHEMA_OK )
        {
            char error[160];
            snprintf(
                error,
                sizeof(error),
                "Its config schema has %s at item %d.",
                plugin_config_schema_result_text(schema_result),
                schema_row + 1);
            TORIRS_ERR("plugin: '%s' reload refused: %s\n", ctx->name, error);
            ctx->schema_count = 0;
            PluginHost_SetError(host, plugin_index, error);
            ctx->refused = true;
            if( plugin_provides_frames(ctx) )
            {
                PluginFrameCatalog_SetAvailable(&host->frame_catalog, plugin_index, 0);
                host->frame_selection_dirty = 1;
            }
            return;
        }
    }

    /*
     * Re-seed the schema, PRESERVING values that already have one.
     *
     * Not plugin_config_seed: that writes every declared default over the
     * store, which on a reload would throw away the very settings the user
     * just saved -- the reload exists to make them take effect, so wiping them
     * would make Save a button that resets the plugin. What this does add is
     * defaults for keys the reloaded source declares and the store has never
     * seen, so a script that gained a setting comes back with it populated.
     */
    ctx->schema_count = schema_count;
    if( plugin_schema(ctx) )
    {
        struct ToriRS_ConfigItem const* schema = plugin_schema(ctx);
        for( int i = 0; i < schema_count; i++ )
        {
            struct ToriRS_ConfigItem const* item = &schema[i];
            struct PluginConfigSlot* slot;
            bool const existed = plugin_config_slot(ctx, item->key, false) != NULL;

            slot = plugin_config_slot(ctx, item->key, true);
            if( !slot )
                continue;
            /* Re-point the slot at its item in the NEW schema: the old index
             * may name a different key, or none at all. */
            slot->schema_index = plugin_schema_index(ctx, item->key);
            if( !existed )
                plugin_copy_str(
                    slot->value,
                    sizeof(slot->value),
                    item->default_value ? item->default_value : "");
        }
    }

    /* Whatever the last run faulted with was about the run that just ended,
     * and so was any refusal: a reload is a fresh run, decided again from
     * whatever the new source says. */
    ctx->error[0] = '\0';
    ctx->refused = false;
    if( plugin_provides_frames(ctx) )
    {
        PluginFrameCatalog_SetAvailable(&host->frame_catalog, plugin_index, 1);
        host->frame_selection_dirty = 1;
    }

    PluginHost_Start(host);
    /* Reload is replacement in place, not a user navigation action. If the
     * plugin registered its page again, rebuild it as the still-selected
     * entry; otherwise the shell remains collapsed. */
    if( panel_was_selected && host->panel_registered[plugin_index] && ctx->running )
        (void)PluginHost_PanelSelect(host, plugin_index);
}

bool
PluginHost_IsEnabled(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].enabled && !host->plugins[plugin_index].refused;
}

int
PluginHost_Count(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->plugin_count;
}

char const*
PluginHost_Name(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].name;
}

char const*
PluginHost_Title(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].title;
}

bool
PluginHost_IsRuntimeHost(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return plugin_policy(
        &host->plugins[plugin_index], TORIRS_PLUGIN_V2_RUNTIME_HOST);
}

bool
PluginHost_IsHidden(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    {
        struct PluginContext const* ctx = &host->plugins[plugin_index];
        /* Providers with settings keep one locked row so their advanced
         * controls remain reachable. A provider with no settings has nothing
         * useful to show beside the one Gameframe selector. */
        return plugin_policy(ctx, TORIRS_PLUGIN_V2_HIDDEN) ||
               (plugin_provides_frames(ctx) && ctx->schema_count == 0);
    }
}

bool
PluginHost_IsEssential(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return plugin_policy(
               &host->plugins[plugin_index], TORIRS_PLUGIN_V2_ESSENTIAL) ||
           plugin_provides_frames(&host->plugins[plugin_index]);
}

char const*
PluginHost_Error(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].error[0] ? host->plugins[plugin_index].error : NULL;
}

void
PluginHost_SetError(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* text)
{
    assert(host);
    struct PluginContext* ctx = plugin_at(host, plugin_index);
    if( !text )
    {
        ctx->error[0] = '\0';
        return;
    }
    snprintf(ctx->error, sizeof(ctx->error), "%s", text);
}

int
PluginHost_IndexOf(
    struct ToriRS_PluginHost const* host,
    char const* name)
{
    assert(host);
    assert(name);
    for( int i = 0; i < host->plugin_count; i++ )
    {
        if( strcmp(host->plugins[i].name, name) == 0 )
            return i;
    }
    return -1;
}


struct ToriRS_UiNodeRef
PluginHost_UiRef(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* name)
{
    struct PluginContext* ctx;

    assert(host);
    assert(name);
    ctx = plugin_at(host, plugin_index);
    return ToriRS_UiRegistry_PrivateRef(&host->ui_registry, ctx->name, name);
}

bool
PluginHost_UiInfo(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_UiNodeInfo* out)
{
    struct ToriRS_UiNodeInfo snapshot;
    struct ToriRS_UiResolvedNode resolved;
    uint32_t capacity;

    assert(host);
    assert(out);
    capacity = out->struct_size;
    if( capacity < TORIRS_UI_NODE_INFO_V2_0_SIZE )
        return false;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = capacity < sizeof(snapshot) ? capacity : sizeof(snapshot);
    if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, node, &resolved) )
        return false;
    snapshot.bounds = resolved.value.bounds;
    snapshot.available_facets = resolved.available_facets;
    snapshot.visible = (resolved.value.flags & TORIRS_UI_NODE_VISIBLE) != 0;
    snapshot.enabled = (resolved.value.flags & TORIRS_UI_NODE_ENABLED) != 0;
    snapshot.active = (resolved.value.flags & TORIRS_UI_NODE_ACTIVE) != 0;
    snapshot.parent = resolved.value.parent;
    snapshot.anchor = resolved.value.anchor;
    snapshot.paint_order = resolved.value.paint_order;
    snapshot.clip = resolved.value.clip;
    memcpy(snapshot.state_images, resolved.value.state_images, sizeof(snapshot.state_images));
    (void)snprintf(snapshot.label, sizeof(snapshot.label), "%s", resolved.value.label);
    snapshot.label_x = resolved.value.label_x;
    snapshot.label_y = resolved.value.label_y;
    snapshot.hit_rect = resolved.value.hit_rect;
    snapshot.action_count = resolved.value.action_count;
    memcpy(snapshot.actions, resolved.value.actions, sizeof(snapshot.actions));
    memcpy(out, &snapshot, capacity < sizeof(snapshot) ? capacity : sizeof(snapshot));
    return true;
}

bool
PluginHost_UiInvoke(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct ToriRS_UiResolvedNode resolved;
    char role[TORIRS_PLUGIN_ROLE_NAME_MAX];
    char const* name;
    bool action_declared = false;

    assert(host);
    assert(action);
    name = ToriRS_UiRegistry_Name(&host->ui_registry, node);
    if( !name )
        return false;
    if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, node, &resolved) ||
        (resolved.available_facets & TORIRS_UI_FACET_ACTIONS) == 0 ||
        (resolved.value.flags & TORIRS_UI_NODE_ENABLED) == 0 )
        return false;
    for( uint32_t i = 0; i < resolved.value.action_count; i++ )
        if( strcmp(resolved.value.actions[i], action) == 0 )
        {
            action_declared = true;
            break;
        }
    /* Compatibility base nodes predate named-action metadata; their one
     * conventional operation remains `activate` until RevConfig owns it. */
    if( resolved.value.action_count == 0 )
        action_declared = strcmp(action, "activate") == 0;
    if( !action_declared )
        return false;

    /* A plugin-provided action facet gets first refusal. Returning CONTINUE
     * deliberately falls through to the lane route for `activate`, while a
     * custom spelling has no lane-specific numeric fallback. */
    {
        int const provider = plugin_ui_present_provider(
            host,
            node,
            resolved.actions_provider,
            TORIRS_UI_FACET_ACTIONS);
        if( provider >= 0 && host->plugins[provider].enabled && host->plugins[provider].running &&
            host->plugins[provider].v2 &&
            host->plugins[provider].v2->definition->callbacks.on_ui_node_action )
        {
            struct PluginV2Instance* v2 = host->plugins[provider].v2;
            int const previous = host->dispatching;
            enum ToriRS_CallbackResult result;

            host->dispatching = provider;
            result = v2->definition->callbacks.on_ui_node_action(
                &v2->runtime.api, v2->state, node, action);
            host->dispatching = previous;
            if( result == TORIRS_CALLBACK_CONSUME )
                return true;
        }
    }
    if( strcmp(action, "activate") != 0 )
        return false;

    role[0] = '\0';
    if( strcmp(name, "frame.chat.button.report") == 0 )
        snprintf(role, sizeof(role), "%s", "report_button");
    else if( strcmp(name, "frame.orb.hitpoints") == 0 )
        snprintf(role, sizeof(role), "%s", "orb_hitpoints");
    else if( strcmp(name, "frame.orb.prayer") == 0 )
        snprintf(role, sizeof(role), "%s", "orb_prayer");
    else if( strcmp(name, "frame.orb.run") == 0 )
        snprintf(role, sizeof(role), "%s", "orb_run");
    else if( strcmp(name, "frame.orb.special") == 0 )
        snprintf(role, sizeof(role), "%s", "orb_spec");
    else if( strncmp(name, "frame.sidebar.tab.", 18) == 0 )
    {
        char* end = NULL;
        long const tab = strtol(name + 18, &end, 10);
        if( end && !*end && tab >= 0 && tab < 14 && host->engine.tab_select )
            return host->engine.tab_select(host->engine.user, (int)tab) != 0;
        return false;
    }
    if( !role[0] )
        return false;

    /* IF1's unnumbered action is 0; IF3's primary op is 1. Only try the
     * second spelling if the first did not dispatch, so one invocation can
     * never fire twice. RevConfig named actions will eventually own this
     * compatibility mapping. */
    if( host->engine.role_click(host->engine.user, role, 0) )
        return true;
    return host->engine.role_click(host->engine.user, role, 1) != 0;
}

static void
plugin_ui_observe_change(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiChange const* change)
{
    int tail;
    int node;

    assert(host);
    assert(change);
    if( change->node.value == 0 ||
        change->node.value > TORIRS_UI_REGISTRY_NODES_MAX )
        return;
    node = (int)change->node.value - 1;
    if( host->ui_observed_change_facets[node] == 0 )
    {
        assert(host->ui_observed_change_count < TORIRS_UI_REGISTRY_NODES_MAX);
        tail = (host->ui_observed_change_head +
                host->ui_observed_change_count) % TORIRS_UI_REGISTRY_NODES_MAX;
        host->ui_observed_change_queue[tail] = node;
        host->ui_observed_change_count++;
    }
    host->ui_observed_change_facets[node] |= change->facets;
    host->ui_observed_change_revision[node] = change->revision;
}

int
PluginHost_UiChangeNext(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiChange* out)
{
    int node;

    assert(host);
    assert(out);
    /* The presenter is the registry journal's sole stateful consumer.
     * Diagnostics observe its coalesced copy and can no longer steal a change
     * from rendering by polling first. */
    plugin_ui_present_reconcile(host);
    if( host->ui_observed_change_count <= 0 )
        return 0;
    node = host->ui_observed_change_queue[host->ui_observed_change_head];
    host->ui_observed_change_head =
        (host->ui_observed_change_head + 1) % TORIRS_UI_REGISTRY_NODES_MAX;
    host->ui_observed_change_count--;
    out->node.value = (uint32_t)node + 1u;
    out->facets = host->ui_observed_change_facets[node];
    out->revision = host->ui_observed_change_revision[node];
    host->ui_observed_change_facets[node] = 0;
    host->ui_observed_change_revision[node] = 0;
    return 1;
}

static int
plugin_ui_present_provider(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    char const* provider,
    uint32_t facet)
{
    int plugin;

    assert(host);
    assert(provider);
    plugin = PluginHost_IndexOf(host, provider);
    if( plugin < 0 )
    {
        int const offer = PluginFrameCatalog_Find(&host->frame_catalog, provider);
        struct PluginFrameCatalogEntry const* entry =
            offer >= 0 ? PluginFrameCatalog_At(&host->frame_catalog, offer) : NULL;

        plugin = entry ? entry->plugin : -1;
        if( plugin >= 0 && plugin < host->plugin_count &&
            host->plugins[plugin].v2 && host->plugins[plugin].enabled &&
            host->plugins[plugin].running )
        {
            struct PluginV2Instance const* v2 = host->plugins[plugin].v2;
            char const* name = ToriRS_UiRegistry_Name(&host->ui_registry, node);

            for( int i = 0; name && i < v2->frame_ui_count; i++ )
                if( strcmp(v2->frame_ui[i].name, name) == 0 )
                {
                    if( facet == TORIRS_UI_FACET_APPEARANCE )
                        return plugin;
                    if( facet == TORIRS_UI_FACET_ACTIONS &&
                        v2->frame_ui[i].value.action_count > 0 )
                        return plugin;
                    return -1;
                }
        }
        return -1;
    }
    if( !host->plugins[plugin].v2 || !host->plugins[plugin].enabled ||
        !host->plugins[plugin].running || !host->plugins[plugin].ui_contributions_registered )
        return -1;
    for( int i = 0; i < host->plugins[plugin].ui_contribution_count; i++ )
    {
        struct ToriRS_UiContributionStatus status;
        struct ToriRS_UiNodeRef const declared = ToriRS_UiRegistry_PrivateRef(
            &host->ui_registry,
            host->plugins[plugin].name,
            host->plugins[plugin].def->ui_contributions[i].node);
        if( declared.value == node.value &&
            ToriRS_UiRegistry_ContributionStatus(
                &host->ui_registry,
                host->plugins[plugin].ui_contribution_refs[i],
                &status) &&
            (status.active_facets & facet) != 0 )
            return plugin;
    }
    return -1;
}

static bool
plugin_ui_rect_intersect(
    struct ToriRS_Rect* destination,
    struct ToriRS_Rect const* source)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    assert(destination);
    assert(source);
    left = destination->x > source->x ? destination->x : source->x;
    top = destination->y > source->y ? destination->y : source->y;
    right = (int64_t)destination->x + destination->width;
    if( right > (int64_t)source->x + source->width )
        right = (int64_t)source->x + source->width;
    bottom = (int64_t)destination->y + destination->height;
    if( bottom > (int64_t)source->y + source->height )
        bottom = (int64_t)source->y + source->height;
    if( right <= left || bottom <= top )
        return false;
    *destination =
        (struct ToriRS_Rect){ (int)left, (int)top, (int)(right - left), (int)(bottom - top) };
    return true;
}

static bool
plugin_ui_present_tree_state(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    bool* out_clip_active,
    struct ToriRS_Rect* out_clip,
    uint64_t* out_identity)
{
    bool clip_active = false;
    uint64_t identity = UINT64_C(1469598103934665603);

    assert(host);
    assert(out_clip_active);
    assert(out_clip);
    assert(out_identity);
    *out_clip_active = false;
    *out_clip = (struct ToriRS_Rect){ 0 };
    for( int depth = 0; node.value != 0 && depth < TORIRS_UI_REGISTRY_NODES_MAX; depth++ )
    {
        struct ToriRS_UiResolvedNode current;
        struct ToriRS_UiResolvedNode parent;

        if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, node, &current) )
        {
            *out_identity = identity;
            return false;
        }
        identity ^= node.value;
        identity *= UINT64_C(1099511628211);
        /* Hash the ancestry facts this state actually depends on. A registry-
         * wide revision made an unrelated node update invalidate every open
         * action token, which defeats exact mutation consumption. */
        {
            uint32_t const facts[] = {
                current.available_facets,
                current.value.flags & TORIRS_UI_NODE_VISIBLE,
                current.value.parent.value,
                (uint32_t)current.value.bounds.x,
                (uint32_t)current.value.bounds.y,
                (uint32_t)current.value.bounds.width,
                (uint32_t)current.value.bounds.height,
                (uint32_t)current.value.clip,
            };
            for( size_t i = 0; i < sizeof(facts) / sizeof(facts[0]); i++ )
            {
                identity ^= facts[i];
                identity *= UINT64_C(1099511628211);
            }
        }
        if( (current.available_facets & TORIRS_UI_FACET_APPEARANCE) != 0 &&
            (current.value.flags & TORIRS_UI_NODE_VISIBLE) == 0 )
        {
            *out_identity = identity;
            return false;
        }
        if( current.value.clip == TORIRS_UI_CLIP_BOUNDS )
        {
            if( !clip_active )
            {
                *out_clip = current.value.bounds;
                clip_active = true;
            }
            else if( !plugin_ui_rect_intersect(out_clip, &current.value.bounds) )
            {
                *out_identity = identity;
                return false;
            }
        }
        else if( current.value.clip == TORIRS_UI_CLIP_PARENT &&
                 current.value.parent.value != 0 )
        {
            if( !ToriRS_UiRegistry_Resolve(
                    &host->ui_registry, current.value.parent, &parent) )
            {
                *out_identity = identity;
                return false;
            }
            if( !clip_active )
            {
                *out_clip = parent.value.bounds;
                clip_active = true;
            }
            else if( !plugin_ui_rect_intersect(out_clip, &parent.value.bounds) )
            {
                *out_identity = identity;
                return false;
            }
        }
        node = current.value.parent;
    }
    if( node.value != 0 )
    {
        *out_identity = identity;
        return false;
    }
    *out_clip_active = clip_active;
    *out_identity = identity;
    return true;
}

static char const*
plugin_ui_present_role(
    char const* name,
    char* dynamic,
    size_t dynamic_size)
{
    assert(name);
    assert(dynamic);
    if( strcmp(name, "frame.viewport") == 0 )
        return "viewport";
    if( strcmp(name, "frame.minimap") == 0 )
        return "minimap";
    if( strcmp(name, "frame.minimap.housing") == 0 )
        return "minimap_edge";
    if( strcmp(name, "frame.compass") == 0 )
        return "compass";
    if( strcmp(name, "frame.chat") == 0 )
        return "chat";
    if( strcmp(name, "frame.chat.buttons") == 0 )
        return "chat_buttons";
    if( strcmp(name, "frame.chat.button.report") == 0 )
        return "report_button";
    if( strcmp(name, "frame.sidebar") == 0 )
        return "sidebar";
    if( strcmp(name, "frame.sidebar.rail") == 0 )
        return "lane_chrome_0";
    if( strncmp(name, "frame.sidebar.tab.", 18) == 0 )
    {
        (void)snprintf(dynamic, dynamic_size, "sidetab_%s", name + 18);
        return dynamic;
    }
    if( strcmp(name, "frame.modal") == 0 )
        return "main_modal";
    if( strcmp(name, "frame.orbs") == 0 )
        return "orbs";
    if( strcmp(name, "frame.orb.hitpoints") == 0 )
        return "orb_hitpoints";
    if( strcmp(name, "frame.orb.prayer") == 0 )
        return "orb_prayer";
    if( strcmp(name, "frame.orb.run") == 0 )
        return "orb_run";
    if( strcmp(name, "frame.orb.special") == 0 )
        return "orb_spec";
    if( strcmp(name, "frame.xp.drops") == 0 )
        return "xp_drops";
    return NULL;
}

static int
plugin_ui_present_index(
    struct ToriRS_PluginHost const* host,
    struct ToriRS_UiNodeRef node)
{
    int indexed;

    assert(host);
    if( node.value == 0 || node.value > TORIRS_UI_REGISTRY_NODES_MAX )
        return -1;
    indexed = host->ui_presentation_by_node[node.value - 1u];
    return indexed > 0 ? indexed - 1 : -1;
}

static void
plugin_ui_present_reindex(struct ToriRS_PluginHost* host)
{
    assert(host);
    memset(
        host->ui_presentation_by_node,
        0,
        sizeof(host->ui_presentation_by_node));
    for( int i = 0; i < host->ui_presentation_count; i++ )
    {
        uint32_t const node = host->ui_presentations[i].node.value;
        assert(node > 0 && node <= TORIRS_UI_REGISTRY_NODES_MAX);
        assert(host->ui_presentation_by_node[node - 1u] == 0);
        host->ui_presentation_by_node[node - 1u] = i + 1;
    }
}

static bool
plugin_ui_present_mapped_role(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    char* out,
    size_t out_size)
{
    char dynamic[TORIRS_PLUGIN_ROLE_NAME_MAX];
    char const* name = ToriRS_UiRegistry_Name(&host->ui_registry, node);
    char const* role = name ? plugin_ui_present_role(name, dynamic, sizeof(dynamic)) : NULL;

    if( !role )
        return false;
    (void)snprintf(out, out_size, "%s", role);
    return true;
}

static bool
plugin_ui_present_role_live(
    struct ToriRS_PluginHost* host,
    char const* role)
{
    int x, y, w, h;

    assert(host);
    return role && role[0] &&
           host->engine.role_rect(host->engine.user, role, &x, &y, &w, &h) &&
           host->engine.role_visible(host->engine.user, role);
}

static void
plugin_ui_present_boundary(
    struct ToriRS_PluginHost* host,
    int index)
{
    struct PluginUiPresentation* rows = host->ui_presentations;
    struct ToriRS_UiNodeRef boundary;
    int root = index;

    assert(host);
    rows[index].target_role[0] = '\0';
    rows[index].boundary_role[0] = '\0';
    rows[index].pending_boundary_role[0] = '\0';
    (void)plugin_ui_present_mapped_role(
        host, rows[index].node, rows[index].target_role,
        sizeof(rows[index].target_role));

    /* The closest live node inside the presented semantic ancestry is the
     * subtree boundary. Every provided descendant below it shares SELF, and
     * the recursive ordering pass supplies before/parent/after within that
     * one contiguous insertion point. */
    for( int current = index;; )
    {
        char role[TORIRS_PLUGIN_ROLE_NAME_MAX];
        int const parent = plugin_ui_present_index(
            host, rows[current].value.parent);

        if( plugin_ui_present_mapped_role(host, rows[current].node, role, sizeof(role)) )
        {
            if( plugin_ui_present_role_live(host, role) )
            {
                (void)snprintf(
                    rows[index].boundary_role,
                    sizeof(rows[index].boundary_role),
                    "%s",
                    role);
                rows[index].boundary_place = PLUGIN_UI_BOUNDARY_SELF;
                return;
            }
            if( !rows[index].pending_boundary_role[0] )
                (void)snprintf(
                    rows[index].pending_boundary_role,
                    sizeof(rows[index].pending_boundary_role),
                    "%s",
                    role);
        }
        if( parent < 0 )
            break;
        root = parent;
        current = parent;
    }

    rows[index].boundary_place =
        rows[root].value.paint_order == TORIRS_UI_PAINT_BEFORE_PARENT
            ? PLUGIN_UI_BOUNDARY_BEFORE
            : PLUGIN_UI_BOUNDARY_AFTER;
    boundary = rows[root].value.parent;
    for( int depth = 0; boundary.value != 0 && depth < TORIRS_UI_REGISTRY_NODES_MAX; depth++ )
    {
        struct ToriRS_UiResolvedNode parent;
        char role[TORIRS_PLUGIN_ROLE_NAME_MAX];
        if( plugin_ui_present_mapped_role(host, boundary, role, sizeof(role)) )
        {
            if( plugin_ui_present_role_live(host, role) )
            {
                (void)snprintf(
                    rows[index].boundary_role,
                    sizeof(rows[index].boundary_role),
                    "%s",
                    role);
                return;
            }
            if( !rows[index].pending_boundary_role[0] )
                (void)snprintf(
                    rows[index].pending_boundary_role,
                    sizeof(rows[index].pending_boundary_role),
                    "%s",
                    role);
        }
        if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, boundary, &parent) )
            return;
        boundary = parent.value.parent;
    }
}

static void
plugin_ui_present_order_visit(
    struct PluginUiPresentation const* source,
    int index,
    int const* before_head,
    int const* after_head,
    int const* next,
    bool* emitted,
    struct PluginUiPresentation* destination,
    int* destination_count)
{
    if( emitted[index] )
        return;
    for( int child = before_head[index]; child >= 0; child = next[child] )
        plugin_ui_present_order_visit(
            source,
            child,
            before_head,
            after_head,
            next,
            emitted,
            destination,
            destination_count);
    emitted[index] = true;
    destination[(*destination_count)++] = source[index];
    for( int child = after_head[index]; child >= 0; child = next[child] )
        plugin_ui_present_order_visit(
            source,
            child,
            before_head,
            after_head,
            next,
            emitted,
            destination,
            destination_count);
}

/** Restore semantic before/parent/after order in O(active + interned nodes). */
static void
plugin_ui_present_reorder(struct ToriRS_PluginHost* host)
{
    struct PluginUiPresentation* ordered;
    int before_head[PLUGIN_UI_PRESENTATIONS_MAX];
    int before_tail[PLUGIN_UI_PRESENTATIONS_MAX];
    int after_head[PLUGIN_UI_PRESENTATIONS_MAX];
    int after_tail[PLUGIN_UI_PRESENTATIONS_MAX];
    int next[PLUGIN_UI_PRESENTATIONS_MAX];
    bool emitted[PLUGIN_UI_PRESENTATIONS_MAX] = { false };
    int root_head = -1;
    int root_tail = -1;
    int ordered_count = 0;

    assert(host);
    if( host->ui_presentation_count <= 1 )
    {
        plugin_ui_present_reindex(host);
        return;
    }
    for( int i = 0; i < PLUGIN_UI_PRESENTATIONS_MAX; i++ )
    {
        before_head[i] = before_tail[i] = -1;
        after_head[i] = after_tail[i] = -1;
        next[i] = -1;
    }
    /* Node refs are interned deterministically. Walking that index, not compact
     * slot order, keeps sibling order stable after swap-removal. */
    for( uint32_t node = 1;
         node <= (uint32_t)ToriRS_UiRegistry_NodeCount(&host->ui_registry);
         node++ )
    {
        struct ToriRS_UiNodeRef const ref = { node };
        int const child = plugin_ui_present_index(host, ref);
        int parent;
        int* head;
        int* tail;

        if( child < 0 )
            continue;
        parent = plugin_ui_present_index(
            host, host->ui_presentations[child].value.parent);
        if( parent < 0 )
        {
            if( root_tail < 0 )
                root_head = child;
            else
                next[root_tail] = child;
            root_tail = child;
            continue;
        }
        if( host->ui_presentations[child].value.paint_order ==
            TORIRS_UI_PAINT_BEFORE_PARENT )
        {
            head = &before_head[parent];
            tail = &before_tail[parent];
        }
        else
        {
            head = &after_head[parent];
            tail = &after_tail[parent];
        }
        if( *tail < 0 )
            *head = child;
        else
            next[*tail] = child;
        *tail = child;
    }

    ordered = calloc((size_t)host->ui_presentation_count, sizeof(*ordered));
    assert(ordered);
    for( int root = root_head; root >= 0; root = next[root] )
        plugin_ui_present_order_visit(
            host->ui_presentations,
            root,
            before_head,
            after_head,
            next,
            emitted,
            ordered,
            &ordered_count);
    /* Registry validation forbids cycles. Keep this defensive tail so a
     * damaged presenter cache cannot silently drop a row. */
    for( int i = 0; i < host->ui_presentation_count; i++ )
        plugin_ui_present_order_visit(
            host->ui_presentations,
            i,
            before_head,
            after_head,
            next,
            emitted,
            ordered,
            &ordered_count);
    assert(ordered_count == host->ui_presentation_count);
    memcpy(
        host->ui_presentations,
        ordered,
        (size_t)ordered_count * sizeof(*ordered));
    free(ordered);
    plugin_ui_present_reindex(host);
}

static void
plugin_ui_present_suppressions(
    struct ToriRS_PluginHost* host,
    struct PluginUiPresentation const* rows,
    int count,
    bool enabled)
{
    if( !host->engine.role_suppress_facets )
        return;
    for( int i = 0; i < count; i++ )
    {
        if( !rows[i].target_role[0] )
            continue;
        /* Canonical frame names map injectively to lane roles; aliases intern
         * to the same node before reaching this list. One row therefore owns
         * one exact suppression update, with no prior/suffix search. */
        (void)host->engine.role_suppress_facets(
            host->engine.user,
            rows[i].target_role,
            enabled && rows[i].appearance_plugin >= 0,
            enabled && rows[i].actions_plugin >= 0);
    }
}

static bool
plugin_ui_present_anchor_available(
    struct ToriRS_PluginHost* host,
    struct PluginUiPresentation const* row)
{
    assert(host);
    assert(row);
    if( row->boundary_role[0] )
        return plugin_ui_present_role_live(host, row->boundary_role);
    /* A mapped native boundary which has not appeared yet is not permission
     * to promote the contribution to the global canvas. A genuinely private
     * root has neither string and remains a valid canvas-local declaration. */
    return !row->pending_boundary_role[0];
}

static uint32_t
plugin_ui_present_action_token_next(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->ui_action_token = (host->ui_action_token + 1u) &
                            ~PLUGIN_UI_PRESENT_TAG_BIT;
    if( host->ui_action_token == 0 )
        host->ui_action_token = 1;
    return host->ui_action_token;
}

static bool
plugin_ui_present_actions_equal(
    struct PluginUiPresentation const* old,
    struct PluginUiPresentation const* candidate)
{
    if( !old || old->actions_plugin != candidate->actions_plugin ||
        old->value.action_count != candidate->value.action_count ||
        old->presentable != candidate->presentable ||
        old->state_identity != candidate->state_identity ||
        ((old->value.flags ^ candidate->value.flags) &
         (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED)) != 0 ||
        old->boundary_place != candidate->boundary_place ||
        strcmp(old->boundary_role, candidate->boundary_role) != 0 ||
        strcmp(old->pending_boundary_role, candidate->pending_boundary_role) != 0 )
        return false;
    for( uint32_t i = 0; i < candidate->value.action_count; i++ )
        if( strcmp(old->value.actions[i], candidate->value.actions[i]) != 0 )
            return false;
    return true;
}

/** Resolve only the changed semantic node into a presenter candidate. */
static bool
plugin_ui_present_candidate(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    struct PluginUiPresentation* out)
{
    struct ToriRS_UiResolvedNode resolved;
    int appearance;
    int actions;

    assert(host);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, node, &resolved) ||
        (resolved.available_facets & TORIRS_UI_FACET_BOUNDS) == 0 )
        return false;
    appearance = plugin_ui_present_provider(
        host, node, resolved.appearance_provider, TORIRS_UI_FACET_APPEARANCE);
    actions = plugin_ui_present_provider(
        host, node, resolved.actions_provider, TORIRS_UI_FACET_ACTIONS);
    if( appearance < 0 && actions < 0 )
        return false;
    out->node = node;
    out->value = resolved.value;
    out->appearance_plugin = appearance;
    out->actions_plugin = actions;
    return true;
}

static void
plugin_ui_present_suppression_one(
    struct ToriRS_PluginHost* host,
    struct PluginUiPresentation const* row,
    bool enabled)
{
    if( host->engine.role_suppress_facets && row && row->target_role[0] )
        (void)host->engine.role_suppress_facets(
            host->engine.user,
            row->target_role,
            enabled && row->appearance_plugin >= 0,
            enabled && row->actions_plugin >= 0);
}

/** Maintain an exact compact list of rows with a not-yet-live role boundary. */
static void
plugin_ui_present_pending_set(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    bool pending)
{
    int at;

    assert(host);
    assert(node.value > 0 && node.value <= TORIRS_UI_REGISTRY_NODES_MAX);
    at = host->ui_pending_role_index[node.value - 1u] - 1;
    if( pending )
    {
        if( at >= 0 )
            return;
        assert(host->ui_pending_role_count < TORIRS_UI_REGISTRY_NODES_MAX);
        at = host->ui_pending_role_count++;
        host->ui_pending_role_nodes[at] = (int)node.value;
        host->ui_pending_role_index[node.value - 1u] = at + 1;
        return;
    }
    if( at < 0 )
        return;
    {
        int const last = --host->ui_pending_role_count;
        int const moved = host->ui_pending_role_nodes[last];
        host->ui_pending_role_index[node.value - 1u] = 0;
        if( at != last )
        {
            host->ui_pending_role_nodes[at] = moved;
            host->ui_pending_role_index[moved - 1] = at + 1;
        }
        host->ui_pending_role_nodes[last] = 0;
    }
}

/** Re-resolve ancestry/boundary state for one already-indexed row. */
static void
plugin_ui_present_finish_row(
    struct ToriRS_PluginHost* host,
    int index,
    struct PluginUiPresentation const* old)
{
    struct PluginUiPresentation* row;
    bool tree_presentable;

    assert(host);
    assert(index >= 0 && index < host->ui_presentation_count);
    row = &host->ui_presentations[index];
    plugin_ui_present_suppression_one(host, old, false);
    tree_presentable = plugin_ui_present_tree_state(
        host,
        row->node,
        &row->clip_active,
        &row->clip,
        &row->state_identity);
    plugin_ui_present_boundary(host, index);
    row->presentable = tree_presentable &&
                       plugin_ui_present_anchor_available(host, row);
    if( row->actions_plugin >= 0 && row->value.action_count > 0 )
        row->action_token =
            old && plugin_ui_present_actions_equal(old, row)
                ? old->action_token
                : plugin_ui_present_action_token_next(host);
    else
        row->action_token = 0;
    plugin_ui_present_pending_set(
        host, row->node, row->pending_boundary_role[0] != '\0');
    plugin_ui_present_suppression_one(host, row, true);
}

/** Swap-remove one presenter row while preserving the direct node index. */
static void
plugin_ui_present_remove(struct ToriRS_PluginHost* host, int index)
{
    struct PluginUiPresentation old;
    int const last = host->ui_presentation_count - 1;

    assert(host);
    assert(index >= 0 && index <= last);
    old = host->ui_presentations[index];
    plugin_ui_present_suppression_one(host, &old, false);
    plugin_ui_present_pending_set(host, old.node, false);
    host->ui_presentation_by_node[old.node.value - 1u] = 0;
    if( index != last )
    {
        host->ui_presentations[index] = host->ui_presentations[last];
        host->ui_presentation_by_node[
            host->ui_presentations[index].node.value - 1u] = index + 1;
    }
    memset(&host->ui_presentations[last], 0, sizeof(host->ui_presentations[last]));
    host->ui_presentation_count--;
}

/** Apply one registry-journal row without visiting an unrelated named node. */
static void
plugin_ui_present_apply_change(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiChange const* change,
    bool* order_dirty)
{
    struct PluginUiPresentation candidate;
    struct PluginUiPresentation old;
    int index;

    assert(host);
    assert(change);
    assert(order_dirty);
    index = plugin_ui_present_index(host, change->node);
    if( !plugin_ui_present_candidate(host, change->node, &candidate) )
    {
        if( index >= 0 )
        {
            plugin_ui_present_remove(host, index);
            *order_dirty = true;
        }
        return;
    }
    if( index < 0 )
    {
        if( host->ui_presentation_count >= PLUGIN_UI_PRESENTATIONS_MAX )
            return;
        index = host->ui_presentation_count++;
        host->ui_presentations[index] = candidate;
        host->ui_presentation_by_node[change->node.value - 1u] = index + 1;
        plugin_ui_present_finish_row(host, index, NULL);
        *order_dirty = true;
        return;
    }

    old = host->ui_presentations[index];
    host->ui_presentations[index] = candidate;
    plugin_ui_present_finish_row(host, index, &old);
    if( old.value.parent.value != candidate.value.parent.value ||
        old.value.paint_order != candidate.value.paint_order )
        *order_dirty = true;
}

static bool
plugin_ui_present_descends_from(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node,
    bool const* changed)
{
    for( int depth = 0; node.value != 0 &&
         depth < TORIRS_UI_REGISTRY_NODES_MAX; depth++ )
    {
        struct ToriRS_UiResolvedNode current;
        if( node.value <= TORIRS_UI_REGISTRY_NODES_MAX &&
            changed[node.value - 1u] )
            return true;
        if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, node, &current) )
            return false;
        node = current.value.parent;
    }
    return false;
}

/** An ancestor visibility/clip/parent mutation affects only its descendants. */
static void
plugin_ui_present_refresh_dependents(
    struct ToriRS_PluginHost* host,
    bool const* directly_changed)
{
    for( int i = 0; i < host->ui_presentation_count; i++ )
    {
        struct PluginUiPresentation old;
        struct ToriRS_UiNodeRef parent = host->ui_presentations[i].value.parent;
        if( !plugin_ui_present_descends_from(host, parent, directly_changed) )
            continue;
        old = host->ui_presentations[i];
        plugin_ui_present_finish_row(host, i, &old);
    }
}

/** Probe only the exceptional live-role dependencies, never the registry. */
static bool
plugin_ui_present_refresh_roles(struct ToriRS_PluginHost* host)
{
    bool changed = false;

    assert(host);
    if( !host->ui_presentation_roles_dirty &&
        host->ui_pending_role_count == 0 )
        return false;
    if( host->ui_presentation_roles_dirty )
    {
        for( int i = 0; i < host->ui_presentation_count; i++ )
        {
            struct PluginUiPresentation old = host->ui_presentations[i];
            host->ui_presentation_role_probe_visits++;
            plugin_ui_present_finish_row(host, i, &old);
        }
        changed = true;
    }
    else
    {
        for( int pending = 0; pending < host->ui_pending_role_count; )
        {
            struct ToriRS_UiNodeRef const node = {
                (uint32_t)host->ui_pending_role_nodes[pending]
            };
            int const index = plugin_ui_present_index(host, node);
            int const count_before = host->ui_pending_role_count;

            if( index < 0 ||
                !host->ui_presentations[index].pending_boundary_role[0] )
            {
                plugin_ui_present_pending_set(host, node, false);
                continue;
            }
            host->ui_presentation_role_probe_visits++;
            if( plugin_ui_present_role_live(
                    host,
                    host->ui_presentations[index].pending_boundary_role) )
            {
                struct PluginUiPresentation old = host->ui_presentations[index];
                plugin_ui_present_finish_row(host, index, &old);
                changed = true;
            }
            if( host->ui_pending_role_count == count_before )
                pending++;
        }
    }
    host->ui_presentation_roles_dirty = false;
    return changed;
}

/**
 * Did this node's appearance change whether descendants may be presented?
 * Label/image/active-state updates stay local; only absence or VISIBLE moves
 * through semantic ancestry.
 */
static bool
plugin_ui_present_visibility_changed(
    struct ToriRS_PluginHost* host,
    struct ToriRS_UiNodeRef node)
{
    struct ToriRS_UiResolvedNode resolved;
    uint8_t next;
    uint8_t old;

    assert(host);
    if( node.value == 0 || node.value > TORIRS_UI_REGISTRY_NODES_MAX )
        return true;
    if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, node, &resolved) )
        next = 1; /* absent: an ancestry walk stops here */
    else if( (resolved.available_facets & TORIRS_UI_FACET_APPEARANCE) != 0 &&
             (resolved.value.flags & TORIRS_UI_NODE_VISIBLE) == 0 )
        next = 3;
    else
        next = 2;
    old = host->ui_node_visibility_state[node.value - 1u];
    host->ui_node_visibility_state[node.value - 1u] = next;
    return old == 0 || old != next;
}

/** Recovery-only complete rebuild; ordinary revisions use ChangeNext below. */
static void
plugin_ui_present_rebuild_all(struct ToriRS_PluginHost* host)
{
    int const nodes = ToriRS_UiRegistry_NodeCount(&host->ui_registry);

    plugin_ui_present_suppressions(
        host, host->ui_presentations, host->ui_presentation_count, false);
    memset(host->ui_presentations, 0, sizeof(host->ui_presentations));
    memset(
        host->ui_presentation_by_node,
        0,
        sizeof(host->ui_presentation_by_node));
    memset(host->ui_pending_role_nodes, 0, sizeof(host->ui_pending_role_nodes));
    memset(host->ui_pending_role_index, 0, sizeof(host->ui_pending_role_index));
    memset(
        host->ui_node_visibility_state,
        0,
        sizeof(host->ui_node_visibility_state));
    host->ui_presentation_count = 0;
    host->ui_pending_role_count = 0;
    for( int i = 0; i < nodes; i++ )
    {
        struct ToriRS_UiNodeRef const node =
            ToriRS_UiRegistry_NodeAt(&host->ui_registry, i);
        struct PluginUiPresentation candidate;
        int index;

        host->ui_presentation_registry_visits++;
        (void)plugin_ui_present_visibility_changed(host, node);
        if( !plugin_ui_present_candidate(host, node, &candidate) ||
            host->ui_presentation_count >= PLUGIN_UI_PRESENTATIONS_MAX )
            continue;
        index = host->ui_presentation_count++;
        host->ui_presentations[index] = candidate;
        host->ui_presentation_by_node[node.value - 1u] = index + 1;
    }
    for( int i = 0; i < host->ui_presentation_count; i++ )
        plugin_ui_present_finish_row(host, i, NULL);
    plugin_ui_present_reorder(host);
}

static void
plugin_ui_present_reconcile(struct ToriRS_PluginHost* host)
{
    bool directly_changed[TORIRS_UI_REGISTRY_NODES_MAX] = { false };
    bool ancestor_dirty = false;
    bool order_dirty = false;
    bool any_change = false;
    bool reconciled = false;
    struct ToriRS_UiChange change;
    uint32_t const revision = ToriRS_UiRegistry_Revision(&host->ui_registry);

    assert(host);
    while( ToriRS_UiRegistry_ChangeNext(&host->ui_registry, &change) )
    {
        host->ui_presentation_change_visits++;
        plugin_ui_observe_change(host, &change);
        if( change.node.value > 0 &&
            change.node.value <= TORIRS_UI_REGISTRY_NODES_MAX )
            directly_changed[change.node.value - 1u] = true;
        if( (change.facets & TORIRS_UI_FACET_BOUNDS) != 0 ||
            ((change.facets & TORIRS_UI_FACET_APPEARANCE) != 0 &&
             plugin_ui_present_visibility_changed(host, change.node)) )
            ancestor_dirty = true;
        plugin_ui_present_apply_change(host, &change, &order_dirty);
        any_change = true;
    }
    if( any_change )
    {
        if( ancestor_dirty )
            plugin_ui_present_refresh_dependents(host, directly_changed);
        if( order_dirty )
        {
            plugin_ui_present_reorder(host);
            /* Map indices moved; boundaries use that map but semantic ancestry
             * did not, so no row state needs another refresh. */
        }
        host->ui_presentation_revision = revision;
        reconciled = true;
    }
    else if( host->ui_presentation_revision != revision )
    {
        /* The raw registry journal has a single consumer. Reaching this branch
         * means a diagnostic or future integration bypassed the host wrapper;
         * recover once rather than trusting a skipped delta. */
        plugin_ui_present_rebuild_all(host);
        host->ui_presentation_revision = revision;
        reconciled = true;
    }
    if( plugin_ui_present_refresh_roles(host) )
        reconciled = true;
    if( reconciled )
        host->ui_presentation_rebuilds++;
}

static bool
plugin_ui_present_anchor(
    struct ToriRS_PluginHost* host,
    struct PluginUiPresentation const* row)
{
    assert(host);
    assert(row);
    if( !row->boundary_role[0] )
        return true;
    if( host->engine.ui_boundary(
            host->engine.user,
            row->boundary_role,
            row->boundary_place) )
        return true;
    /* The live role disappeared between reconciliation and paint. Do not scan
     * all rows speculatively each frame; this failed exact dependency is the
     * event that schedules one role-state reconciliation on the next fence. */
    host->ui_presentation_roles_dirty = true;
    return false;
}

static void
plugin_ui_present_anchor_reset(
    struct ToriRS_PluginHost* host,
    struct PluginUiPresentation const* row)
{
    if( row->boundary_role[0] )
        (void)host->engine.ui_boundary(host->engine.user, NULL, 0);
}

static struct ToriRS_ImageRef
plugin_ui_present_image(
    struct PluginUiPresentation const* row,
    bool hovered)
{
    struct ToriRS_ImageRef image = { 0 };
    int wanted;

    assert(row);
    if( !(row->value.flags & TORIRS_UI_NODE_ENABLED) )
        wanted = TORIRS_UI_VISUAL_DISABLED;
    else if( row->value.flags & TORIRS_UI_NODE_ACTIVE )
        wanted = hovered ? TORIRS_UI_VISUAL_ACTIVE_HOVER : TORIRS_UI_VISUAL_ACTIVE;
    else
        wanted = hovered ? TORIRS_UI_VISUAL_HOVER : TORIRS_UI_VISUAL_IDLE;
    image = row->value.state_images[wanted];
    if( image.value == 0 && wanted == TORIRS_UI_VISUAL_ACTIVE_HOVER )
        image = row->value.state_images[TORIRS_UI_VISUAL_ACTIVE];
    if( image.value == 0 && hovered )
        image = row->value.state_images[TORIRS_UI_VISUAL_HOVER];
    if( image.value == 0 )
        image = row->value.state_images[TORIRS_UI_VISUAL_IDLE];
    return image;
}

static void
plugin_ui_present_draw(
    struct ToriRS_PluginHost* host,
    void* surface)
{
    int mouse_x = 0;
    int mouse_y = 0;
    bool const have_mouse = host->engine.mouse_pos(host->engine.user, &mouse_x, &mouse_y) != 0;

    assert(host);
    assert(surface);
    plugin_ui_present_reconcile(host);
    for( int i = 0; i < host->ui_presentation_count; i++ )
    {
        struct PluginUiPresentation const* row = &host->ui_presentations[i];
        bool const hovered = have_mouse && mouse_x >= row->value.hit_rect.x &&
                             mouse_x < row->value.hit_rect.x + row->value.hit_rect.width &&
                             mouse_y >= row->value.hit_rect.y &&
                             mouse_y < row->value.hit_rect.y + row->value.hit_rect.height;

        if( !row->presentable )
            continue;

        if( row->appearance_plugin >= 0 && (row->value.flags & TORIRS_UI_NODE_VISIBLE) &&
            plugin_ui_present_anchor(host, row) )
        {
            struct PluginContext* context = &host->plugins[row->appearance_plugin];
            struct PluginV2Instance* v2 = context->v2;
            struct PluginV2DrawScope scope;
            struct ToriRS_DrawBuilder builder;
            struct ToriRS_ImageRef const image = plugin_ui_present_image(row, hovered);
            int const previous_dispatching = host->dispatching;
            int const previous_event = host->dispatch_event;

            host->dispatching = row->appearance_plugin;
            host->dispatch_event = PLUGIN_CALLBACK_DRAW_CANVAS;
            plugin_v2_runtime_draw_begin(&v2->runtime, surface, &scope, &builder);
            if( row->clip_active )
                plugin_v2_runtime_draw_clip(&scope, row->clip);
            if( image.value != 0 )
            {
                int const image_slot =
                    plugin_v2_runtime_image_slot(&v2->runtime, image);
                struct PluginImage const* owned = plugin_image_owned(context, image_slot);
                if( owned && owned->published )
                    builder.image(
                        &builder, image, row->value.bounds.x, row->value.bounds.y, 255);
            }
            if( row->value.label[0] )
                builder.text(
                    &builder,
                    row->value.bounds.x + row->value.label_x,
                    row->value.bounds.y + row->value.label_y,
                    row->value.label,
                    0xffffffu);
            if( v2->definition->callbacks.on_ui_node_draw )
                v2->definition->callbacks.on_ui_node_draw(
                    &v2->runtime.api, v2->state, row->node, &builder);
            plugin_v2_runtime_draw_end(&scope, &builder);
            host->dispatching = previous_dispatching;
            host->dispatch_event = previous_event;
        }
        plugin_ui_present_anchor_reset(host, row);

        if( row->actions_plugin >= 0 && (row->value.flags & TORIRS_UI_NODE_VISIBLE) &&
            (row->value.flags & TORIRS_UI_NODE_ENABLED) && row->value.action_count > 0 &&
            plugin_ui_present_anchor(host, row) )
        {
            struct ToriRS_Rect hit = row->value.hit_rect;
            char const* actions[TORIRS_UI_NAMED_ACTIONS_MAX];
            int count = (int)row->value.action_count;

            if( row->clip_active && !plugin_ui_rect_intersect(&hit, &row->clip) )
                count = 0;
            if( count > TORIRS_PLUGIN_REGION_OPS_MAX )
                count = TORIRS_PLUGIN_REGION_OPS_MAX;
            for( int action = 0; action < count; action++ )
                actions[action] = row->value.actions[action];
            if( count > 0 )
                (void)host->engine.hit_region(
                    host->engine.user,
                    row->actions_plugin,
                    hit.x,
                    hit.y,
                    hit.width,
                    hit.height,
                    actions,
                    count,
                    PLUGIN_UI_PRESENT_TAG_BIT | row->action_token);
        }
        plugin_ui_present_anchor_reset(host, row);
    }
}

int
PluginHost_UiPresentationCount(struct ToriRS_PluginHost const* host)
{
    return host ? host->ui_presentation_count : 0;
}

uint32_t
PluginHost_UiPresentationRebuilds(struct ToriRS_PluginHost const* host)
{
    return host ? host->ui_presentation_rebuilds : 0;
}

uint32_t
PluginHost_UiPresentationChangeVisits(struct ToriRS_PluginHost const* host)
{
    return host ? host->ui_presentation_change_visits : 0;
}

uint32_t
PluginHost_UiPresentationRegistryVisits(struct ToriRS_PluginHost const* host)
{
    return host ? host->ui_presentation_registry_visits : 0;
}

uint32_t
PluginHost_UiPresentationRoleProbeVisits(struct ToriRS_PluginHost const* host)
{
    return host ? host->ui_presentation_role_probe_visits : 0;
}

/* ------------------------------------------------------------------ seams */

void
PluginHost_FrameStart(
    struct ToriRS_PluginHost* host,
    uint64_t now_ms,
    uint64_t drawn_frames)
{
    if( !host )
        return;
    /* The frame boundary is also where per-frame draw budgets reset. */
    for( int i = 0; i < host->plugin_count; i++ )
    {
        host->plugins[i].draw_used = 0;
        host->plugins[i].draw_clipped = false;
    }

    /* A placement callback may restate a reservation. That schedules this
     * later transaction instead of recursively entering the callback list. */
    if( host->layout_notify_pending )
        plugin_layout_notifications_run(host);

    plugin_ui_tab_state_poll(host);

    /* The screen poll. Here rather than at the transitions themselves because
     * the app changes screens from half a dozen places (login, logout, a
     * disconnect, the title's own tabs) and a seam per place is how one gets
     * missed; the boundary sees them all. Before on_frame_start, so a frame
     * handler polling api->screen never contradicts an on_screen_changed it has
     * not received yet. The baseline advances whether or not anyone listens --
     * a subscriber must never be handed a change that predates it. */
    {
        int const screen = host->engine.screen(host->engine.user);
        if( screen != host->last_screen )
        {
            struct ToriRS_ScreenChangedEvent ev = { screen, host->last_screen };
            host->last_screen = screen;
            host->frame_selection_dirty = 1;
            if( host->callback_count[PLUGIN_CALLBACK_SCREEN_CHANGE] > 0 )
                plugin_dispatch(host, PLUGIN_CALLBACK_SCREEN_CHANGE, &ev);
        }
    }

    if( host->frame_selection_dirty )
        PluginHost_Start(host);

    if( host->callback_count[PLUGIN_CALLBACK_FRAME_START] == 0 )
        return;

    struct ToriRS_FrameEvent ev = { now_ms, drawn_frames };
    plugin_dispatch(host, PLUGIN_CALLBACK_FRAME_START, &ev);
}

void
PluginHost_LogicTick(
    struct ToriRS_PluginHost* host,
    int logic_cycle)
{
    if( !host || host->callback_count[PLUGIN_CALLBACK_LOGIC_TICK] == 0 )
        return;
    struct ToriRS_TickEvent ev = { logic_cycle };
    plugin_dispatch(host, PLUGIN_CALLBACK_LOGIC_TICK, &ev);
}

void
PluginHost_ServerTick(
    struct ToriRS_PluginHost* host,
    int world_cycle)
{
    if( !host || host->callback_count[PLUGIN_CALLBACK_SERVER_TICK] == 0 )
        return;
    struct ToriRS_TickEvent ev = { world_cycle };
    plugin_dispatch(host, PLUGIN_CALLBACK_SERVER_TICK, &ev);
}

void
PluginHost_WorldLoaded(
    struct ToriRS_PluginHost* host,
    int base_tile_x,
    int base_tile_z)
{
    if( !host || host->callback_count[PLUGIN_CALLBACK_WORLD_LOADED] == 0 )
        return;
    struct ToriRS_WorldLoadedEvent ev = { base_tile_x, base_tile_z };
    plugin_dispatch(host, PLUGIN_CALLBACK_WORLD_LOADED, &ev);
}

static void
plugin_npc_event(
    struct ToriRS_PluginHost* host,
    enum PluginCallbackKind which,
    struct ToriRS_NpcSnapshot const* npc)
{
    if( !host || host->callback_count[which] == 0 )
        return;
    assert(npc);
    plugin_dispatch(host, which, (void*)npc);
}

void
PluginHost_NpcSpawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_NpcSnapshot const* npc)
{
    plugin_npc_event(host, PLUGIN_CALLBACK_NPC_SPAWN, npc);
}

void
PluginHost_NpcRetype(
    struct ToriRS_PluginHost* host,
    struct ToriRS_NpcSnapshot const* npc)
{
    plugin_npc_event(host, PLUGIN_CALLBACK_NPC_RETYPE, npc);
}

void
PluginHost_NpcDespawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_NpcSnapshot const* npc)
{
    plugin_npc_event(host, PLUGIN_CALLBACK_NPC_DESPAWN, npc);
}

static void
plugin_obj_event(
    struct ToriRS_PluginHost* host,
    enum PluginCallbackKind which,
    struct ToriRS_GroundItemSnapshot const* obj)
{
    if( !host || host->callback_count[which] == 0 )
        return;
    assert(obj);
    plugin_dispatch(host, which, (void*)obj);
}

void
PluginHost_ObjSpawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_GroundItemSnapshot const* obj)
{
    plugin_obj_event(host, PLUGIN_CALLBACK_OBJ_SPAWN, obj);
}

void
PluginHost_ObjCount(
    struct ToriRS_PluginHost* host,
    struct ToriRS_GroundItemSnapshot const* obj)
{
    plugin_obj_event(host, PLUGIN_CALLBACK_OBJ_COUNT, obj);
}

void
PluginHost_ObjDespawn(
    struct ToriRS_PluginHost* host,
    struct ToriRS_GroundItemSnapshot const* obj)
{
    plugin_obj_event(host, PLUGIN_CALLBACK_OBJ_DESPAWN, obj);
}

void
PluginHost_ChatMessage(
    struct ToriRS_PluginHost* host,
    int type,
    char const* sender,
    char const* text)
{
    if( !host || host->callback_count[PLUGIN_CALLBACK_CHAT_MESSAGE] == 0 )
        return;

    struct ToriRS_ChatMessageEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    /* Both are absent on ordinary lines -- a system message has no sender, and
     * IF_SETTEXT blanks a row by writing nothing -- so they are guards, not
     * asserts, and a plugin always reads a string rather than a NULL. */
    if( sender )
        snprintf(ev.sender, sizeof(ev.sender), "%s", sender);
    if( text )
        snprintf(ev.text, sizeof(ev.text), "%s", text);
    plugin_dispatch(host, PLUGIN_CALLBACK_CHAT_MESSAGE, &ev);
}

void
PluginHost_GameEvent(
    struct ToriRS_PluginHost* host,
    char const* kind,
    char const* subject,
    int value,
    char const* text)
{
    if( !host || host->callback_count[PLUGIN_CALLBACK_GAME_EVENT] == 0 )
        return;
    /* The kind is the whole event -- a plugin switches on it -- so an absent
     * one is a caller bug rather than a moment with nothing to say. */
    assert(kind);

    struct ToriRS_GameEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.value = value;
    if( subject )
        snprintf(ev.subject, sizeof(ev.subject), "%s", subject);
    if( text )
        snprintf(ev.text, sizeof(ev.text), "%s", text);
    plugin_dispatch(host, PLUGIN_CALLBACK_GAME_EVENT, &ev);
}

int
PluginHost_Key(
    struct ToriRS_PluginHost* host,
    int key,
    int ch,
    bool down)
{
    if( !host || host->callback_count[PLUGIN_CALLBACK_KEY] == 0 )
        return 0;

    struct ToriRS_KeyEvent ev = { key, ch, down };
    return plugin_dispatch(host, PLUGIN_CALLBACK_KEY, &ev) == TORIRS_CALLBACK_CONSUME ? 1 : 0;
}

void
PluginHost_DrawWorld(struct ToriRS_PluginHost* host)
{
    if( !host )
        return;

    /* Entity claims bind to this frame's elements before anybody draws, so
     * the draw_hull gate and the standing looks below agree about which
     * element is whose. */
    plugin_entity_resolve_all(host);

    /* The surface token is the host's own address: it is not dereferenced,
     * only compared, so that a stale token from a previous frame is caught by
     * the same assert that catches drawing outside the window. */
    host->draw_surface = host;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
    if( host->callback_count[PLUGIN_CALLBACK_DRAW_WORLD] > 0 )
        plugin_dispatch(host, PLUGIN_CALLBACK_DRAW_WORLD, host->draw_surface);
    /* The declared looks, after the imperative drawing: a holder's standing
     * hull goes over whatever anyone else marked around it. */
    plugin_entity_paint_looks(host);
    host->draw_surface = NULL;
}

void
PluginHost_DrawCanvas(
    struct ToriRS_PluginHost* host,
    int width,
    int height)
{
    struct PluginCanvasDispatch canvas;

    if( !host )
        return;

    /*
     * A DIFFERENT token from the world surface's, and that is the point: the
     * token is compared, never dereferenced, so `&host->api` here and `host`
     * there is all it takes for a handler that kept the wrong event's surface
     * to be caught by the same assert that catches drawing outside a window.
     */
    host->draw_surface = host;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_CANVAS;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_CANVAS);
    canvas.surface = host->draw_surface;
    canvas.bounds = (struct ToriRS_Rect){ 0, 0, width, height };
    /* Retained contributions are a compact presenter list rebuilt only at
     * named-registry revisions. Only facet winners enter this pass, so two
     * providers cannot draw the same semantic node. */
    plugin_ui_present_draw(host, host->draw_surface);
    if( host->callback_count[PLUGIN_CALLBACK_DRAW_CANVAS] > 0 )
        plugin_dispatch(host, PLUGIN_CALLBACK_DRAW_CANVAS, &canvas);
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
}

void
PluginHost_ReconcileUi(struct ToriRS_PluginHost* host)
{
    if( !host )
        return;
    plugin_ui_present_reconcile(host);
}
static struct ToriRS_FrameOffer const*
plugin_v2_frame_offer(
    struct PluginContext* ctx,
    int entry_index)
{
    struct PluginFrameCatalogEntry const* entry;

    assert(ctx);
    if( !ctx->v2 || !ctx->v2->definition->frames )
        return NULL;
    entry = PluginFrameCatalog_At(&ctx->host->frame_catalog, entry_index);
    if( !entry || entry->plugin != ctx->index )
        return NULL;
    for( int i = 0; i < ctx->v2->frame_count; i++ )
        if( strcmp(ctx->v2->definition->frames[i].id, entry->local_id) == 0 )
            return &ctx->v2->definition->frames[i];
    return NULL;
}

void
PluginHost_DrawFrame(
    struct ToriRS_PluginHost* host,
    int width,
    int height)
{
    struct PluginCanvasDispatch canvas;
    struct ToriRS_FrameOffer const* v2_offer;
    int owner;

    if( !host )
        return;
    owner = plugin_frame_owner(host);
    if( owner < 0 )
        return;
    v2_offer = plugin_v2_frame_offer(&host->plugins[owner], host->frame_active_entry);
    if( !v2_offer || !v2_offer->draw )
        return;

    /* A third token, for the third surface, on the same reasoning as the
     * canvas one: compared and never dereferenced, so three distinct addresses
     * are the whole mechanism that catches a handler which kept the wrong
     * event's surface. */
    host->draw_surface = &host->engine;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_FRAME;
    canvas.surface = &host->engine;
    canvas.bounds = (struct ToriRS_Rect){ 0, 0, width, height };
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_FRAME);
    /* The owner and nobody else. Chrome drawn under the interfaces of a frame
     * somebody else is arranging is chrome in the wrong place. */
    if( v2_offer )
    {
        if( v2_offer->draw )
        {
            struct PluginV2Instance* v2 = host->plugins[owner].v2;
            struct PluginV2DrawScope scope;
            struct ToriRS_DrawBuilder builder;
            int const previous_dispatching = host->dispatching;
            int const previous_event = host->dispatch_event;

            host->dispatching = owner;
            host->dispatch_event = PLUGIN_CALLBACK_DRAW_FRAME;
            plugin_v2_runtime_draw_begin(&v2->runtime, canvas.surface, &scope, &builder);
            plugin_v2_runtime_draw_region(&scope, canvas.bounds);
            v2_offer->draw(&v2->runtime.api, v2->state, &builder);
            plugin_v2_runtime_draw_end(&scope, &builder);
            host->dispatching = previous_dispatching;
            host->dispatch_event = previous_event;
        }
    }
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
}

/** Deliver one immutable resolved-placement transaction to v2 plugins. */
static void
plugin_placement_notify_v2(struct ToriRS_PluginHost* host)
{
    uint32_t revision;
    int const previous_dispatching = host->dispatching;
    int const previous_event = host->dispatch_event;

    assert(host);
    if( !host->placement_notify_pending ||
        host->placement_notified_revision == host->placement_revision )
    {
        host->placement_notify_pending = 0;
        return;
    }

    revision = host->placement_revision;
    /* A callback can invalidate placement again. Acknowledge this snapshot
     * before entering user code so that mutation schedules the next one. */
    host->placement_notify_pending = 0;
    host->placement_notified_revision = revision;
    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct PluginContext* ctx = &host->plugins[i];
        struct PluginV2Instance* v2 = ctx->v2;

        if( !ctx->enabled || !ctx->running || ctx->tearing_down || !v2 ||
            !v2->definition->callbacks.on_placement_changed )
            continue;
        host->dispatching = i;
        host->dispatch_event = PLUGIN_CALLBACK_PLACEMENT_CHANGED;
        v2->definition->callbacks.on_placement_changed(
            &v2->runtime.api, v2->state, revision);
        host->dispatching = previous_dispatching;
        host->dispatch_event = previous_event;
    }
}

/**
 * Resolve one canonical placement result and publish it to V2 consumers.
 */
static void
plugin_layout_notifications_run(struct ToriRS_PluginHost* host)
{
    assert(host);
    if( host->layout_notifying )
    {
        host->layout_notify_pending = 1;
        return;
    }

    /* Establish the transaction's first immutable placement snapshot before
     * callbacks. The forced rebuild below folds in changes callbacks make. */
    (void)plugin_placement_rebuild(host, 1);
    host->layout_notifying = 1;
    host->layout_notify_pending = 0;
    /* Fold callback-side reservation changes into the next non-recursive
     * placement transaction. */
    if( host->layout_notify_pending )
        (void)plugin_placement_rebuild(host, 1);
    host->layout_notify_pending = 0;
    plugin_placement_notify_v2(host);
    /* A v2 callback which mutates placement leaves layout_notify_pending set.
     * FrameStart drains it as a later transaction, never recursively. */
    host->layout_notifying = 0;
}

void
PluginHost_LayoutChanged(struct ToriRS_PluginHost* host)
{
    if( !host )
        return;
    host->placement_cache_valid = 0;

    /* Publish semantic names from the geometry the engine has just settled
     * before any subscriber is told to query them. ReplaceBase is atomic. */
    host->ui_presentation_roles_dirty = true;
    plugin_ui_refresh_base(host);

    /*
     * The revision moves for EVERY change, including one made from inside the
     * notification. Readers compare it and re-read a live region, so they are
     * correct either way.
     */
    host->layout_revision++;
    if( host->layout_notifying )
    {
        host->layout_notify_pending = 1;
        return;
    }
    plugin_layout_notifications_run(host);
}

bool
PluginHost_FrameNeedsLayout(struct ToriRS_PluginHost const* host)
{
    return host && host->frame_layout_requested;
}

static char const*
plugin_frame_surface_name(int slot)
{
    static char const* const NAMES[TORIRS_HOST_SURFACE_PLACEABLE_COUNT] = {
        [TORIRS_HOST_SURFACE_VIEWPORT] = "frame.viewport",
        [TORIRS_HOST_SURFACE_MINIMAP] = "frame.minimap",
        [TORIRS_HOST_SURFACE_COMPASS] = "frame.compass",
        [TORIRS_HOST_SURFACE_CHAT] = "frame.chat",
        [TORIRS_HOST_SURFACE_CHAT_BUTTONS] = "frame.chat.buttons",
        [TORIRS_HOST_SURFACE_SIDEBAR] = "frame.sidebar",
        [TORIRS_HOST_SURFACE_MODAL] = "frame.modal",
        [TORIRS_HOST_SURFACE_ORBS] = "frame.orbs",
    };

    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_PLACEABLE_COUNT )
        return NULL;
    return NAMES[slot];
}

static uint32_t
plugin_frame_surface_flags(int slot)
{
    switch( slot )
    {
    case TORIRS_HOST_SURFACE_MINIMAP:
    case TORIRS_HOST_SURFACE_COMPASS:
    case TORIRS_HOST_SURFACE_CHAT:
    case TORIRS_HOST_SURFACE_CHAT_BUTTONS:
    case TORIRS_HOST_SURFACE_SIDEBAR:
    case TORIRS_HOST_SURFACE_ORBS:
        return TORIRS_UI_NODE_BLOCKS_OVERLAY;
    default:
        return 0;
    }
}

static int
plugin_frame_ui_image_ready(
    struct PluginContext* ctx,
    struct ToriRS_ImageRef image)
{
    struct PluginImage const* owned;
    int image_slot;

    assert(ctx);
    assert(ctx->v2);
    if( image.value == 0 )
        return 1;
    image_slot = plugin_v2_runtime_image_slot(&ctx->v2->runtime, image);
    if( image_slot < 0 )
        return 0;
    owned = plugin_image_owned(ctx, image_slot);
    return owned && owned->published;
}

/** Validate the named half on a private registry copy before geometry moves. */
static int
plugin_frame_candidate_ui_valid(
    struct ToriRS_PluginHost* host,
    int owner,
    char* reason,
    size_t reason_size)
{
    struct ToriRS_UiBaseDeclaration declarations[48];
    struct PluginV2Instance* v2 = host->plugins[owner].v2;
    struct PluginFrameCatalogEntry const* target =
        PluginFrameCatalog_At(&host->frame_catalog, host->layout_candidate_entry);
    struct ToriRS_UiRegistry* validation;
    char const* provider = target ? target->id : host->plugins[owner].name;
    int count = 0;

    memset(declarations, 0, sizeof(declarations));
    for( int i = 0; i < host->layout_candidate.rect_count; i++ )
    {
        struct PluginLayoutRect const* rect = &host->layout_candidate.rects[i];
        char const* name;

        if( rect->member != -1 )
            continue;
        name = plugin_frame_surface_name(rect->slot);
        if( !name )
            continue;
        count = plugin_ui_base_add_rect(
            declarations,
            count,
            (int)(sizeof(declarations) / sizeof(declarations[0])),
            provider,
            name,
            rect->x,
            rect->y,
            rect->w,
            rect->h,
            plugin_frame_surface_flags(rect->slot));
    }

    if( v2 )
        for( int i = 0; i < v2->frame_ui_candidate_count; i++ )
        {
            struct PluginV2FrameNode* node = &v2->frame_ui_candidate[i];
            int destination = -1;

            if( !plugin_frame_ui_image_ready(&host->plugins[owner], node->value.image) )
            {
                snprintf(reason, reason_size, "%s", "The frame used foreign or unready named artwork.");
                return 0;
            }
            for( int state = 0; state < TORIRS_UI_VISUAL_STATE_COUNT; state++ )
                if( (node->value.state_image_mask & (1u << state)) != 0 &&
                    !plugin_frame_ui_image_ready(
                        &host->plugins[owner], node->value.state_images[state]) )
                {
                    snprintf(
                        reason,
                        reason_size,
                        "%s",
                        "The frame used foreign or unready named state artwork.");
                    return 0;
                }
            for( int j = 0; j < count; j++ )
                if( strcmp(declarations[j].node, node->name) == 0 )
                {
                    destination = j;
                    break;
                }
            if( destination < 0 )
            {
                if( count >= (int)(sizeof(declarations) / sizeof(declarations[0])) )
                {
                    snprintf(reason, reason_size, "%s", "The frame declared too many named nodes.");
                    return 0;
                }
                destination = count++;
            }
            declarations[destination] = (struct ToriRS_UiBaseDeclaration){
                .provider = provider,
                .node = node->name,
                .facets = TORIRS_UI_FACET_ALL,
                .value = node->value,
            };
        }

    validation = malloc(sizeof(*validation));
    assert(validation);
    *validation = host->ui_registry;
    if( ToriRS_UiRegistry_ReplaceBase(validation, declarations, count) !=
        TORIRS_UI_REGISTRY_OK )
    {
        free(validation);
        snprintf(reason, reason_size, "%s", "The frame's named UI tree is invalid or cyclic.");
        return 0;
    }
    free(validation);
    return 1;
}

static void
plugin_layout_candidate_apply(struct ToriRS_PluginHost* host)
{
    struct PluginLayoutCandidate const* candidate = &host->layout_candidate;

    assert(host);
    host->engine.layout_begin(host->engine.user);
    for( int i = 0; i < candidate->rect_count; i++ )
    {
        struct PluginLayoutRect const* rect = &candidate->rects[i];
        (void)host->engine.layout_slot(
            host->engine.user,
            rect->slot,
            rect->member,
            rect->x,
            rect->y,
            rect->w,
            rect->h);
    }
    for( int slot = 0; slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT; slot++ )
    {
        if( candidate->skins[slot].declared )
            (void)host->engine.layout_slot_skin(
                host->engine.user,
                slot,
                candidate->skins[slot].art,
                candidate->skins[slot].mask);
        if( candidate->overlays[slot].declared )
            (void)host->engine.layout_slot_overlay(
                host->engine.user,
                slot,
                candidate->overlays[slot].image,
                candidate->overlays[slot].x,
                candidate->overlays[slot].y,
                candidate->overlays[slot].trans);
    }
    if( candidate->scrollbar_declared )
        (void)host->engine.layout_scrollbar(
            host->engine.user,
            candidate->scrollbar_count ? candidate->scrollbar : NULL,
            candidate->scrollbar_count);

    host->engine.layout_end(host->engine.user);
}

void
PluginHost_Layout(
    struct ToriRS_PluginHost* host,
    int width,
    int height)
{
    struct PluginFrameLayout ev;
    struct PluginFrameCatalogEntry const* entry;
    struct ToriRS_FrameOffer const* v2_offer;
    enum ToriRS_FrameBuildResult v2_result = TORIRS_FRAME_READY;
    char v2_reason[TORIRS_FRAME_REASON_MAX] = { 0 };
    char validation_reason[TORIRS_FRAME_REASON_MAX] = { 0 };
    int build_entry;
    int transitioning;
    int owner;
    uint32_t selection_epoch;

    if( !host )
        return;
    transitioning = host->frame_layout_requested && host->frame_target_entry >= 0 &&
                    host->frame_target_entry != host->frame_active_entry;
    build_entry = transitioning ? host->frame_target_entry : host->frame_active_entry;
    entry = PluginFrameCatalog_At(&host->frame_catalog, build_entry);
    if( !entry )
        return;
    owner = entry->plugin;
    if( owner < 0 || owner >= host->plugin_count || !host->plugins[owner].running )
        return;
    v2_offer = plugin_v2_frame_offer(&host->plugins[owner], build_entry);
    if( !v2_offer )
        return;
    host->placement_cache_valid = 0;
    /*
     * A canvas of nothing is refused rather than laid out against.
     *
     * Every anchor in a resizable layout is measured from an edge, so a zero
     * canvas does not produce a small frame -- it produces one at negative
     * coordinates, declared and drawn and invisible. Saying so is the
     * difference between a caller that fixes its call and one that spends an
     * afternoon looking at the plugin.
     */
    if( entry->canvas != TORIRS_FRAME_CANVAS_FIXED && (width <= 0 || height <= 0) )
    {
        TORIRS_LOG(
            "plugin: %s asked to lay out against a %dx%d canvas; nothing declared\n",
            host->plugins[owner].name,
            width,
            height);
        return;
    }

    /*
     * FIXED reads back its own pinned size, not the window's.
     *
     * The alternative -- handing over whatever the platform last set -- makes
     * the plugin's arithmetic depend on when in the boot it was asked, because
     * the canvas is not pinned until the engine has applied the selection. A
     * layout that asked for 765x503 is entitled to be told 765x503 the first
     * time it is asked, and every time after.
     */
    if( entry->canvas == TORIRS_FRAME_CANVAS_FIXED )
    {
        ev.width = entry->width;
        ev.height = entry->height;
    }
    else
    {
        ev.width = width;
        ev.height = height;
    }
    ev.canvas = entry->canvas;

    /* Consume only the attempt we are about to make. A callback that changes
     * selection or invalidates again raises a fresh request and the epoch
     * fence below preserves it. */
    if( transitioning )
        host->frame_layout_requested = 0;
    selection_epoch = host->frame_selection_epoch;
    memset(&host->layout_candidate, 0, sizeof(host->layout_candidate));
    host->layout_candidate_entry = build_entry;
    host->layout_declaring = 1;
    host->layout_declarer = owner;
    if( v2_offer )
    {
        struct PluginV2Instance* v2 = host->plugins[owner].v2;
        struct PluginV2FrameScope scope;
        struct ToriRS_FrameBuilder builder;
        struct ToriRS_FrameBuildContext context;
        int const previous_dispatching = host->dispatching;
        int const previous_event = host->dispatch_event;

        memset(v2->frame_ui_candidate, 0, sizeof(v2->frame_ui_candidate));
        v2->frame_ui_candidate_count = 0;
        v2->frame_ui_candidate_invalid = false;
        memset(v2->frame_images_candidate, 0, sizeof(v2->frame_images_candidate));
        v2->frame_image_candidate_count = 0;
        memset(&context, 0, sizeof(context));
        context.struct_size = sizeof(context);
        context.offer_id = v2_offer->id;
        context.canvas = entry->canvas == TORIRS_FRAME_CANVAS_FIXED
                             ? TORIRS_FRAME_CANVAS_FIXED
                             : TORIRS_FRAME_CANVAS_WINDOW;
        context.logical_canvas = (struct ToriRS_Rect){ 0, 0, ev.width, ev.height };
        context.available =
            v2->runtime.api.placement.area(&v2->runtime.api, TORIRS_AREA_FRAME_BUILD);
        (void)v2->runtime.api.core.lane(&v2->runtime.api, &context.lane);

        host->dispatching = owner;
        host->dispatch_event = PLUGIN_CALLBACK_LAYOUT;
        plugin_v2_runtime_frame_begin(&v2->runtime, &scope, &builder);
        v2_result = v2_offer->build(&v2->runtime.api, v2->state, &builder, &context);
        if( v2_result == TORIRS_FRAME_READY &&
            !plugin_v2_runtime_frame_valid(&scope) )
            v2->frame_ui_candidate_invalid = true;
        v2->frame_image_candidate_count = scope.image_ref_count;
        memcpy(
            v2->frame_images_candidate,
            scope.image_refs,
            (size_t)scope.image_ref_count * sizeof(scope.image_refs[0]));
        (void)snprintf(
            v2_reason, sizeof(v2_reason), "%s", plugin_v2_runtime_frame_reason(&scope));
        plugin_v2_runtime_frame_end(&scope, &builder);
        host->dispatching = previous_dispatching;
        host->dispatch_event = previous_event;
        if( v2->frame_ui_candidate_invalid || v2_result < TORIRS_FRAME_READY ||
            v2_result > TORIRS_FRAME_ERROR )
        {
            v2_result = TORIRS_FRAME_ERROR;
            if( !v2_reason[0] )
                snprintf(
                    v2_reason,
                    sizeof(v2_reason),
                    "%s",
                    "The frame's named UI declaration is invalid or over budget.");
        }
    }
    host->layout_declaring = 0;
    host->layout_declarer = -1;

    if( host->frame_selection_epoch != selection_epoch ||
        (transitioning && host->frame_target_entry != build_entry) ||
        (!transitioning && host->frame_active_entry != build_entry) )
        return;

    if( !host->layout_candidate.viewport_declared )
        plugin_layout_candidate_fail(host, "The frame did not declare its required viewport.");
    for( int slot = 0; slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT; slot++ )
        if( host->layout_candidate.skins[slot].declared )
        {
            int surface_declared = 0;
            for( int i = 0; i < host->layout_candidate.rect_count; i++ )
                if( host->layout_candidate.rects[i].slot == slot &&
                    host->layout_candidate.rects[i].member == -1 )
                {
                    surface_declared = 1;
                    break;
                }
            if( !surface_declared )
                plugin_layout_candidate_fail(
                    host,
                    "The frame skinned a surface it did not declare in this candidate.");
        }
    if( v2_offer && host->plugins[owner].v2->frame_ui_candidate_invalid )
        plugin_layout_candidate_fail(
            host, "The frame's named UI declaration is invalid or over budget.");

    if( v2_offer && v2_result != TORIRS_FRAME_READY )
    {
        int const status = v2_result == TORIRS_FRAME_PENDING ? TORIRS_FRAME_STATUS_LOADING
                                                             : TORIRS_FRAME_STATUS_FALLBACK;
        char const* reason =
            v2_reason[0]                            ? v2_reason
            : v2_result == TORIRS_FRAME_PENDING     ? "The selected gameframe is still loading."
            : v2_result == TORIRS_FRAME_UNSUPPORTED ? "The selected gameframe is unsupported here."
                                                    : "The selected gameframe could not be built.";

        plugin_frame_selection_active(host, plugin_frame_committed_id(host), status, reason);
        return;
    }

    if( host->layout_candidate.invalid )
    {
        plugin_frame_selection_active(
            host,
            plugin_frame_committed_id(host),
            TORIRS_FRAME_STATUS_FALLBACK,
            host->layout_candidate.reason[0]
                ? host->layout_candidate.reason
                : "The requested gameframe declaration is invalid.");
        return;
    }
    if( !plugin_frame_candidate_ui_valid(
            host, owner, validation_reason, sizeof(validation_reason)) )
    {
        plugin_frame_selection_active(
            host,
            plugin_frame_committed_id(host),
            TORIRS_FRAME_STATUS_FALLBACK,
            validation_reason);
        return;
    }

    /* The only publication point. Geometry, frame art and named nodes were
     * all validated above while the old frame was untouched. */
    {
        int const old_owner = plugin_frame_owner(host);

        plugin_frame_engine_activate(host, build_entry);
        if( v2_offer )
        {
            struct PluginV2Instance* v2 = host->plugins[owner].v2;
            memcpy(v2->frame_ui, v2->frame_ui_candidate, sizeof(v2->frame_ui));
            v2->frame_ui_count = v2->frame_ui_candidate_count;
            memcpy(
                v2->frame_images,
                v2->frame_images_candidate,
                sizeof(v2->frame_images));
            v2->frame_image_count = v2->frame_image_candidate_count;
            for( int i = 0; i < v2->frame_ui_count; i++ )
                plugin_v2_frame_node_repoint(&v2->frame_ui[i]);
        }
        plugin_layout_candidate_apply(host);
        plugin_frame_selection_active(host, entry->id, TORIRS_FRAME_STATUS_ACTIVE, "");
        plugin_ui_refresh_base(host);
        host->placement_cache_valid = 0;

        /* Teardown follows publication, so no frame exists where neither end
         * owns the live geometry or rendering surface. */
        if( old_owner >= 0 && old_owner != owner )
        {
            host->plugins[old_owner].enabled = false;
            plugin_teardown(host, old_owner);
        }
    }
}

void
PluginHost_CanvasClick(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    uint32_t tag,
    int op,
    int x,
    int y)
{
    struct PluginContext* ctx;
    int prev;
    int prev_event;

    if( !host )
        return;
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return;

    ctx = &host->plugins[plugin_index];
    /* A region outlives the frame it was declared in by one -- the menu is
     * built from the previous frame's list -- so a plugin disabled in between
     * can still be named by a click. It hears nothing. */
    if( !ctx->enabled || !ctx->running )
        return;

    if( ctx->v2 && (tag & PLUGIN_UI_PRESENT_TAG_BIT) != 0 )
    {
        uint32_t const action_token = tag & ~PLUGIN_UI_PRESENT_TAG_BIT;

        plugin_ui_present_reconcile(host);
        for( int i = 0; i < host->ui_presentation_count; i++ )
        {
            struct PluginUiPresentation const* row = &host->ui_presentations[i];
            struct ToriRS_UiResolvedNode current;
            struct ToriRS_Rect clip = { 0 };
            bool clip_active = false;
            uint64_t state_identity = 0;

            if( row->action_token != action_token || row->actions_plugin != plugin_index || op < 0 ||
                op >= (int)row->value.action_count || !row->presentable ||
                (row->value.flags &
                 (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED)) !=
                    (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED) )
                continue;
            if( !ToriRS_UiRegistry_Resolve(&host->ui_registry, row->node, &current) ||
                plugin_ui_present_provider(
                    host,
                    row->node,
                    current.actions_provider,
                    TORIRS_UI_FACET_ACTIONS) != plugin_index ||
                op >= (int)current.value.action_count ||
                strcmp(current.value.actions[op], row->value.actions[op]) != 0 ||
                (current.value.flags &
                 (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED)) !=
                    (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED) ||
                !plugin_ui_present_tree_state(
                    host,
                    row->node,
                    &clip_active,
                    &clip,
                    &state_identity) ||
                state_identity != row->state_identity ||
                !plugin_ui_present_anchor_available(host, row) )
                continue;
            (void)PluginHost_UiInvoke(host, row->node, row->value.actions[op]);
            return;
        }
        return;
    }

    {
        struct PluginV2Instance* v2 = ctx->v2;
        enum ToriRS_CallbackResult (*callback)(
            struct ToriRS_ApiV2*, void*, uint32_t, int, int, int) =
            v2->definition->callbacks.on_canvas_action;

        if( callback && op >= 0 )
        {
            prev = host->dispatching;
            prev_event = host->dispatch_event;
            host->dispatching = plugin_index;
            host->dispatch_event = PLUGIN_CALLBACK_CANVAS_CLICK;
            (void)callback(&v2->runtime.api, v2->state, tag, op, x, y);
            host->dispatching = prev;
            host->dispatch_event = prev_event;
        }
    }
}

void
PluginHost_MenuBuild(
    struct ToriRS_PluginHost* host,
    void* cursor,
    struct ToriRS_MenuBuildEvent* menu,
    bool hover_pass)
{
    if( !host )
        return;

    /* Routes are per build. The hover pass rebuilds the menu every frame, so
     * accumulating them would exhaust the table in well under a second. */
    host->route_count = 0;

    assert(menu);
    assert(cursor);

    menu->hover_pass = hover_pass;
    menu->host_cursor = cursor;
    host->menu_cursor = cursor;
    if( host->callback_count[PLUGIN_CALLBACK_MENU_BUILD] > 0 )
        plugin_dispatch(host, PLUGIN_CALLBACK_MENU_BUILD, menu);
    /* The entity HITBOX holders, after the plugins' own rows: dropping a row
     * shifts the ones above it, and a payload handed to a handler must not
     * shift under it. */
    plugin_entity_apply_ops(host, cursor, menu);
    host->menu_cursor = NULL;
}

bool
PluginHost_OwnsMenuAction(
    struct ToriRS_PluginHost const* host,
    int action)
{
    if( !host )
        return false;
    for( int i = 0; i < host->route_count; i++ )
    {
        if( host->routes[i].action == action )
            return true;
    }
    return false;
}

int
PluginHost_MenuSelect(
    struct ToriRS_PluginHost* host,
    struct ToriRS_MenuRow const* row,
    int click_x,
    int click_y)
{
    if( !host )
        return 0;
    assert(row);

    struct PluginMenuRoute const* route = NULL;
    for( int i = 0; i < host->route_count && !route; i++ )
    {
        if( host->routes[i].action == row->action )
            route = &host->routes[i];
    }

    /* A plugin row has no engine behaviour to fall through to, so it is always
     * suppressed -- even when the owning plugin subscribed to nothing. */
    if( !route && host->callback_count[PLUGIN_CALLBACK_MENU_SELECT] == 0 )
        return 0;

    struct ToriRS_MenuSelectEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.row = *row;
    ev.plugin_tag = route ? route->tag : 0;
    ev.click_x = click_x;
    ev.click_y = click_y;

    int consumed = 0;
    if( route )
    {
        /* Owned rows go only to their owner: two plugins adding rows in the
         * same build must not see each other's selections. */
        struct PluginContext* ctx = &host->plugins[route->plugin];
        ev.owned = true;
        if( ctx->enabled && ctx->running )
            plugin_dispatch_one(
                host, route->plugin, PLUGIN_CALLBACK_MENU_SELECT, &ev);
        consumed = 1;
    }
    else
    {
        consumed = plugin_dispatch(host, PLUGIN_CALLBACK_MENU_SELECT, &ev) == TORIRS_CALLBACK_CONSUME
                       ? 1
                       : 0;
    }
    return consumed;
}

/* ----------------------------------------------------------------- config */

void
PluginHost_ConfigApply(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* key,
    char const* value)
{
    assert(host);
    assert(plugin_name);
    assert(key);
    assert(value);

    if( !plugin_config_key_valid(key) || !plugin_config_value_valid(value) )
        return;

    int const index = PluginHost_IndexOf(host, plugin_name);
    if( index < 0 )
        return;

    struct PluginContext* ctx = &host->plugins[index];

    /* `enabled` is host state, not plugin config: it is what the settings
     * panel toggles and what a crashed script gets cleared by. */
    if( strcmp(key, "enabled") == 0 )
    {
        /* Same refusal as PluginHost_SetEnabled, and it has to be here too:
         * this path writes the field directly, so an `enabled=0` left over
         * from a build where the plugin was ordinary would switch it off
         * behind both the panel and the host. */
        if( !plugin_policy(ctx, TORIRS_PLUGIN_V2_ESSENTIAL) )
            ctx->enabled = atoi(value) != 0;
        return;
    }

    struct PluginConfigSlot* slot = plugin_config_slot(ctx, key, true);
    if( !slot )
        return;
    snprintf(slot->value, sizeof(slot->value), "%s", value);
}

/* Minimal INI reader: [plugin:name] sections, key=value lines, ';' and '#'
 * comments. Hand-written rather than routed through 3rd/ini because the store
 * has to survive keys it does not recognise, and because this runs against a
 * memory image the IO queue delivered, not a path. */
void
PluginHost_ConfigDecode(
    struct ToriRS_PluginHost* host,
    void const* data,
    int size)
{
    assert(host);

    if( !data || size <= 0 )
        return;

    char const* p = (char const*)data;
    char const* end = p + size;
    char section[TORIRS_PLUGIN_NAME_MAX] = { 0 };

    while( p < end )
    {
        char const* line = p;
        while( p < end && *p != '\n' )
            p++;
        char const* line_end = p;
        if( p < end )
            p++;

        if( memchr(line, '\0', (size_t)(line_end - line)) )
            continue;

        while( line < line_end && (*line == ' ' || *line == '\t' || *line == '\r') )
            line++;
        while( line_end > line &&
               (line_end[-1] == ' ' || line_end[-1] == '\t' || line_end[-1] == '\r') )
            line_end--;
        if( line == line_end || *line == ';' || *line == '#' )
            continue;

        if( *line == '[' )
        {
            char const* close = line;
            while( close < line_end && *close != ']' )
                close++;
            if( close >= line_end )
                continue;
            char const* name = line + 1;
            /* "[plugin:foo]" and a bare "[foo]" both name plugin foo. */
            for( char const* c = name; c < close; c++ )
            {
                if( *c == ':' )
                {
                    name = c + 1;
                    break;
                }
            }
            int len = (int)(close - name);
            if( len >= (int)sizeof(section) )
            {
                section[0] = '\0';
                continue;
            }
            memcpy(section, name, (size_t)len);
            section[len] = '\0';
            continue;
        }

        if( section[0] == '\0' )
            continue;

        char const* eq = line;
        while( eq < line_end && *eq != '=' )
            eq++;
        if( eq >= line_end )
            continue;

        char key[64];
        char value[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
        char const* key_end = eq;
        while( key_end > line && (key_end[-1] == ' ' || key_end[-1] == '\t') )
            key_end--;
        int klen = (int)(key_end - line);
        if( klen <= 0 || klen >= (int)sizeof(key) )
            continue;
        memcpy(key, line, (size_t)klen);
        key[klen] = '\0';

        char const* val = eq + 1;
        while( val < line_end && (*val == ' ' || *val == '\t') )
            val++;
        int vlen = (int)(line_end - val);
        if( vlen >= (int)sizeof(value) )
            continue;
        memcpy(value, val, (size_t)vlen);
        value[vlen] = '\0';

        PluginHost_ConfigApply(host, section, key, value);
    }
}

int
PluginHost_ConfigEncode(
    struct ToriRS_PluginHost const* host,
    void** out_data,
    int* out_size)
{
    assert(host);
    assert(out_data);
    assert(out_size);

    /* Bounded by the store's own fixed caps, so one sizing pass is enough.
     * The 512 is the banner below, which is written whatever the store holds. */
    size_t cap = 512;
    for( int i = 0; i < host->plugin_count; i++ )
        cap +=
            96 + (size_t)host->plugins[i].config_count * (64 + TORIRS_PLUGIN_CONFIG_VALUE_MAX + 4);

    char* buf = malloc(cap);
    assert(buf);

    size_t at = 0;
    /* A banner rather than nothing, because this file is REWRITTEN on every
     * save: a comment somebody adds by hand to remind themselves what a value
     * may say is gone at the next launch, so the reminder has to be written
     * from here to survive. */
    at += (size_t)snprintf(
        buf + at,
        cap - at,
        "; torirs plugin settings\n"
        ";\n"
        "; A numeric value is an integer expression, as in a revconfig profile:\n"
        ";   12   0x1F   1Fh   0b1010   #FF8000   1 << 4   (1088 << 16) | 255\n"
        ";   rgb(255, 128, 0)   rgba(0, 0, 0, 128)   hsl16(hue, sat, lum)\n"
        "; Colours are read as 0xRRGGBB; hsl16() packs the client's own palette\n"
        "; index (hue 0..63, saturation 0..7, lightness 0..127).\n");

    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct PluginContext const* ctx = &host->plugins[i];
        /* A section is written only when it carries something: an all-default,
         * enabled plugin leaves no trace, the way RS_Prefs omits defaults. */
        bool wrote_section = false;

        /* Written only when it differs from what the plugin declared, so a
         * default-off plugin left off leaves no trace, and a default-on plugin
         * left on leaves none either -- the RS_Prefs rule. */
        if( !plugin_provides_frames(ctx) &&
            ctx->enabled == plugin_policy(ctx, TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT) )
        {
            at += (size_t)snprintf(
                buf + at, cap - at, "\n[plugin:%s]\nenabled=%d\n", ctx->name, ctx->enabled ? 1 : 0);
            wrote_section = true;
        }

        for( int c = 0; c < ctx->config_count; c++ )
        {
            struct PluginConfigSlot const* slot = &ctx->config[c];
            /* Defense in depth for corrupt in-process state: never serialize
             * a record that could escape its line. */
            if( !plugin_config_key_valid(slot->key) ||
                !plugin_config_value_valid(slot->value) )
                continue;
            if( slot->schema_index >= 0 )
            {
                char const* def = plugin_schema(ctx)[slot->schema_index].default_value;
                if( def && strcmp(slot->value, def) == 0 )
                    continue;
                if( !def && slot->value[0] == '\0' )
                    continue;
            }
            if( !wrote_section )
            {
                at += (size_t)snprintf(buf + at, cap - at, "\n[plugin:%s]\n", ctx->name);
                wrote_section = true;
            }
            at += (size_t)snprintf(buf + at, cap - at, "%s=%s\n", slot->key, slot->value);
        }
    }

    *out_data = buf;
    *out_size = (int)at;
    return 1;
}

bool
PluginHost_ConfigDirty(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->config_dirty;
}

void
PluginHost_ConfigClearDirty(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->config_dirty = false;
}

int
PluginHost_ConfigCount(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].schema_count;
}

struct ToriRS_ConfigItem const*
PluginHost_ConfigItem(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    int item_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);

    struct PluginContext const* ctx = &host->plugins[plugin_index];
    if( !plugin_schema(ctx) || item_index < 0 || item_index >= ctx->schema_count )
        return NULL;
    return &plugin_schema(ctx)[item_index];
}

/* ---- the plugin window, public face -------------------------------------- */

/**
 * Dispatch one event to ONE plugin.
 *
 * The window events are plugin-scoped in a way no other event is: a control
 * belongs to exactly one tab, and delivering its use to every subscriber would
 * hand every plugin every other plugin's clicks. Shares the snapshot-and-step
 * discipline of plugin_dispatch for the same reason -- a handler may disable
 * its own plugin, rewriting the list underneath the walk.
 */
static void
plugin_dispatch_one(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    enum PluginCallbackKind ev,
    void* payload)
{
    int const prev_dispatching = host->dispatching;
    int const prev_event = host->dispatch_event;

    assert(host);
    assert(ev >= 0);
    assert(ev < PLUGIN_CALLBACK_COUNT);

    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return;

    struct PluginContext* ctx = &host->plugins[plugin_index];
    if( !ctx->enabled || !ctx->running ||
        !plugin_v2_has_event_callback(ctx->def, ev) )
        return;

    host->dispatching = plugin_index;
    host->dispatch_event = ev;
    (void)plugin_v2_event(ctx, payload, (void*)(intptr_t)(ev + 1));
    host->dispatching = prev_dispatching;
    host->dispatch_event = prev_event;
}

bool
PluginHost_PanelHasPage(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return false;
    return host->panel_registered[plugin_index] && host->plugins[plugin_index].running;
}

char const*
PluginHost_PanelTitle(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return "";
    return host->panel_title[plugin_index];
}

char const*
PluginHost_PanelIconAsset(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return "";
    return host->panel_icon[plugin_index];
}

int
PluginHost_PanelPreferredWidth(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return 0;
    return host->panel_preferred_width[plugin_index];
}

bool
PluginHost_PanelWantsAttention(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return false;
    return host->panel_attention[plugin_index];
}

uint32_t
PluginHost_PanelIconRevision(
    struct ToriRS_PluginHost const* host,
    int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return 0;
    return host->panel_icon_revision[plugin_index];
}

int
PluginHost_PanelIconPixels(
    struct ToriRS_PluginHost const* host,
    int plugin_index,
    uint32_t* out_argb,
    int max_pixels,
    int* out_width,
    int* out_height)
{
    struct PluginImage const* image;
    struct PluginAsset const* asset = NULL;
    int slot;
    int pixels;

    assert(host);
    if( out_width )
        *out_width = 0;
    if( out_height )
        *out_height = 0;
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] || !host->plugins[plugin_index].running ||
        !out_argb || max_pixels <= 0 )
        return 0;
    slot = host->panel_icon_image[plugin_index];
    if( slot < 0 || slot >= TORIRS_PLUGIN_IMAGES_MAX )
        return 0;
    image = &host->images[slot];
    for( int i = 0; i < host->asset_count; i++ )
        if( host->assets[i].plugin == plugin_index &&
            strcmp(host->assets[i].name, host->panel_icon[plugin_index]) == 0 )
        {
            asset = &host->assets[i];
            break;
        }
    if( image->plugin != plugin_index || !image->published || !asset || !asset->data ||
        asset->size <= 0 || asset->size > TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX ||
        strcmp(image->asset, host->panel_icon[plugin_index]) != 0 || image->width <= 0 ||
        image->height <= 0 || image->width > 64 || image->height > 64 )
        return 0;
    pixels = image->width * image->height;
    if( pixels > max_pixels ||
        host->engine.image_read(host->engine.user, slot, out_argb, max_pixels) != pixels )
        return 0;
    if( out_width )
        *out_width = image->width;
    if( out_height )
        *out_height = image->height;
    return pixels;
}

uint32_t
PluginHost_PanelRegistryRevision(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_registry_revision;
}

int
PluginHost_PanelActive(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_active;
}

int
PluginHost_PanelLastSelected(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_last_selected;
}

uint32_t
PluginHost_PanelSelectionGeneration(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_selection_generation;
}

int
PluginHost_PanelSelect(
    struct ToriRS_PluginHost* host,
    int plugin_index)
{
    return PluginHost_PanelSelectView(host, plugin_index, TORIRS_PANEL_VIEW_PAGE);
}

int
PluginHost_PanelView(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_active >= 0 ? host->panel_view : TORIRS_PANEL_VIEW_PAGE;
}

int
PluginHost_PanelSelectView(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    int view)
{
    assert(host);
    if( view < TORIRS_PANEL_VIEW_PAGE || view > TORIRS_PANEL_VIEW_SETTINGS )
        return 0;
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] || !host->plugins[plugin_index].enabled ||
        !host->plugins[plugin_index].running )
        return 0;

    /*
     * Selecting what is already mounted COLLAPSES it -- the stone is its own
     * off switch. Asking for the OTHER face of the same plugin is not that: it
     * is a different destination that happens to share a registration, so it
     * takes the replacement path below and gets a fresh generation like any
     * other move.
     */
    if( host->panel_active == plugin_index && host->panel_view == view )
        return plugin_panel_deactivate(host);

    (void)plugin_panel_deactivate(host);
    /* The old plugin's invisible callback may have changed lifecycle state. */
    if( !host->panel_registered[plugin_index] || !host->plugins[plugin_index].enabled ||
        !host->plugins[plugin_index].running )
        return 0;

    plugin_panel_generation_next(host);
    host->panel_active = plugin_index;
    host->panel_view = view;
    host->panel_last_selected = plugin_index;
    host->panel_needs_build = true;
    host->panel_has_layout = false;
    host->panel_visible = false;
    plugin_panel_bump(&host->panel_model_revision);

    /* Attention is a request to be seen, and selecting it is the acknowledgement. */
    if( host->panel_attention[plugin_index] )
    {
        host->panel_attention[plugin_index] = false;
        plugin_panel_bump(&host->panel_registry_revision);
    }
    return plugin_panel_build_active(host, host->panel_selection_generation);
}

int
PluginHost_PanelClose(struct ToriRS_PluginHost* host)
{
    assert(host);
    return plugin_panel_deactivate(host);
}

int
PluginHost_PanelEnsureBuilt(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation)
{
    assert(host);
    return plugin_panel_build_active(host, selection_generation);
}

int
PluginHost_PanelWidgetCount(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation)
{
    assert(host);
    if( selection_generation == 0 || selection_generation != host->panel_selection_generation ||
        host->panel_active < 0 )
        return 0;
    return host->panel_widget_count;
}

struct ToriRS_PanelWidget const*
PluginHost_PanelWidgetAt(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation,
    int widget_index)
{
    assert(host);
    if( selection_generation == 0 || selection_generation != host->panel_selection_generation ||
        host->panel_active < 0 || widget_index < 0 || widget_index >= host->panel_widget_count )
        return NULL;
    return &host->panel_widgets[widget_index];
}

int
PluginHost_PanelChangeNext(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    struct ToriRS_PluginPanelChange* out)
{
    int slot;
    uint32_t flags;

    assert(host);
    assert(out);
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation ||
        host->panel_active < 0 || host->panel_change_rebuild )
        return -1;
    if( host->panel_change_count <= 0 )
        return 0;

    slot = host->panel_change_queue[host->panel_change_head];
    host->panel_change_head =
        (host->panel_change_head + 1) % TORIRS_PLUGIN_WIDGETS_MAX;
    host->panel_change_count--;
    flags = slot >= 0 && slot < TORIRS_PLUGIN_WIDGETS_MAX
                ? host->panel_change_flags[slot]
                : 0;
    if( slot < 0 || slot >= host->panel_widget_count || flags == 0 ||
        host->panel_widgets[slot].serial == 0 )
    {
        plugin_panel_change_structural(host);
        return -1;
    }
    host->panel_change_flags[slot] = 0;
    memset(out, 0, sizeof(*out));
    out->widget_index = slot;
    out->widget_serial = host->panel_widgets[slot].serial;
    out->flags = flags;
    out->model_revision = host->panel_model_revision;
    host->panel_change_visits++;
    return 1;
}

void
PluginHost_PanelChangesAcknowledge(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation)
{
    assert(host);
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation )
        return;
    plugin_panel_changes_reset(host);
    host->panel_change_rebuild = false;
}

uint32_t
PluginHost_PanelChangeVisits(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_change_visits;
}

uint32_t
PluginHost_PanelModelRevision(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_model_revision;
}

int
PluginHost_PanelLayout(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    int width,
    int height,
    int scale_milli,
    int size_class,
    bool visible,
    bool game_visible)
{
    struct ToriRS_PanelLayoutEvent ev;
    int plugin;

    assert(host);
    if( selection_generation == 0 || selection_generation != host->panel_selection_generation )
        return 0;
    plugin = host->panel_active;
    if( plugin < 0 || !host->panel_registered[plugin] || !host->plugins[plugin].running )
        return 0;
    if( width < 0 || height < 0 || scale_milli <= 0 || size_class < TORIRS_PANEL_SIZE_COMPACT ||
        size_class > TORIRS_PANEL_SIZE_EXPANDED || (visible && (width == 0 || height == 0)) )
        return 0;

    if( host->panel_has_layout && host->panel_width == width && host->panel_height == height &&
        host->panel_scale_milli == scale_milli && host->panel_size_class == size_class &&
        host->panel_visible == visible && host->panel_game_visible == game_visible )
        return 1;

    host->panel_has_layout = true;
    host->panel_width = width;
    host->panel_height = height;
    host->panel_scale_milli = scale_milli;
    host->panel_size_class = size_class;
    host->panel_visible = visible;
    host->panel_game_visible = game_visible;

    memset(&ev, 0, sizeof(ev));
    ev.width = width;
    ev.height = height;
    ev.scale_milli = scale_milli;
    ev.size_class = size_class;
    ev.visible = visible;
    ev.game_visible = game_visible;
    ev.selection_generation = selection_generation;
    plugin_dispatch_one(host, plugin, PLUGIN_CALLBACK_PANEL_LAYOUT, &ev);
    return 1;
}

int
PluginHost_PanelDispatch(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial,
    uint64_t intent_sequence,
    char const* widget_id,
    int action,
    int value,
    char const* text,
    int x,
    int y)
{
    struct ToriRS_PanelActionEvent ev;
    struct ToriRS_PanelWidget* widget;
    char event_id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char event_text[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    int plugin;
    int slot;
    bool changed = false;
    uint32_t change_flags = 0;

    assert(host);
    if( !widget_id || selection_generation == 0 ||
        selection_generation != host->panel_selection_generation || intent_sequence == 0 ||
        intent_sequence <= host->panel_last_intent_sequence || !host->panel_visible )
        return 0;
    plugin = host->panel_active;
    if( plugin < 0 || !host->panel_registered[plugin] || !host->plugins[plugin].running )
        return 0;
    slot = plugin_panel_find_serial(host, widget_serial);
    if( slot < 0 || strcmp(host->panel_widgets[slot].id, widget_id) != 0 )
        return 0;
    if( action < TORIRS_PANEL_ACTION_ACTIVATE || action > TORIRS_PANEL_ACTION_KEY )
        return 0;
    widget = &host->panel_widgets[slot];
    if( action >= TORIRS_PANEL_ACTION_DRAG && widget->kind != TORIRS_PANEL_WIDGET_CUSTOM )
        return 0;

    /* A structured selection is identified by its stable value as well as its
     * index. Requiring both rejects a queued pick authored against an older
     * option list whose index has since been reused. Disabled rows may remain
     * selected for display, but no input path may choose one. */
    if( action == TORIRS_PANEL_ACTION_PICK && widget->structured_select )
    {
        struct ToriRS_PluginSelectOption const* option;

        if( value < 0 || value >= widget->select_option_count ||
            !widget->select_options || !text )
            return 0;
        option = &widget->select_options[value];
        if( !option->enabled || strcmp(text, option->value) != 0 )
            return 0;
    }

    plugin_copy_str(event_id, sizeof(event_id), widget->id);
    plugin_copy_str(event_text, sizeof(event_text), text ? text : widget->text);
    if( action == TORIRS_PANEL_ACTION_PICK && widget->structured_select )
        plugin_copy_str(
            event_text,
            sizeof(event_text),
            widget->select_options[value].value);

    /* Result state is committed before dispatch, matching win_* compatibility
     * and making the model authoritative for native controls. */
    switch( action )
    {
    case TORIRS_PANEL_ACTION_TOGGLE:
        changed = widget->checked != (value ? 1 : 0) || widget->value != (value ? 1 : 0);
        widget->checked = value ? 1 : 0;
        widget->value = widget->checked;
        change_flags = TORIRS_PLUGIN_PANEL_CHANGE_VALUE;
        break;
    case TORIRS_PANEL_ACTION_TEXT:
        changed = strcmp(widget->text, event_text) != 0;
        plugin_copy_str(widget->text, sizeof(widget->text), event_text);
        change_flags = TORIRS_PLUGIN_PANEL_CHANGE_TEXT;
        break;
    case TORIRS_PANEL_ACTION_PICK:
        changed = widget->selected != value || widget->value != value ||
                  strcmp(widget->text, event_text) != 0 ||
                  (widget->structured_select &&
                   strcmp(widget->selected_value, event_text) != 0);
        widget->selected = value;
        widget->value = value;
        plugin_copy_str(widget->text, sizeof(widget->text), event_text);
        if( widget->structured_select )
            plugin_copy_str(
                widget->selected_value,
                sizeof(widget->selected_value),
                event_text);
        change_flags = TORIRS_PLUGIN_PANEL_CHANGE_VALUE;
        break;
    default:
        break;
    }
    if( changed )
    {
        plugin_panel_bump(&host->panel_model_revision);
        plugin_panel_change_widget(host, slot, change_flags);
    }

    /* Mark first so a re-entrant copy of a momentary action is still a duplicate. */
    host->panel_last_intent_sequence = intent_sequence;
    memset(&ev, 0, sizeof(ev));
    ev.id = event_id;
    ev.action = action;
    ev.value = value;
    ev.text = event_text;
    ev.x = x;
    ev.y = y;
    ev.selection_generation = selection_generation;
    ev.widget_serial = widget_serial;
    ev.intent_sequence = intent_sequence;
    plugin_dispatch_one(host, plugin, PLUGIN_CALLBACK_PANEL_ACTION, &ev);
    return 1;
}

bool
PluginHost_PanelNeedsDraw(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation,
    uint32_t widget_serial)
{
    int slot;

    assert(host);
    if( selection_generation == 0 || selection_generation != host->panel_selection_generation ||
        host->panel_active < 0 || !host->panel_visible )
        return false;
    slot = plugin_panel_find_serial(host, widget_serial);
    return slot >= 0 && host->panel_widgets[slot].kind == TORIRS_PANEL_WIDGET_CUSTOM &&
           host->panel_invalidated[slot];
}

int
PluginHost_PanelInvalidate(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial)
{
    int slot;

    assert(host);
    if( selection_generation == 0 || selection_generation != host->panel_selection_generation ||
        host->panel_active < 0 )
        return 0;
    slot = plugin_panel_find_serial(host, widget_serial);
    if( slot < 0 || host->panel_widgets[slot].kind != TORIRS_PANEL_WIDGET_CUSTOM )
        return 0;
    if( !host->panel_invalidated[slot] )
        host->panel_invalidated[slot] = true;
    return 1;
}

int
PluginHost_PanelDraw(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial,
    void* surface,
    int x,
    int y,
    int width,
    int height)
{
    struct PluginPanelDraw ev;
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    int plugin;
    int slot;

    assert(host);
    if( !surface || width <= 0 || height <= 0 ||
        selection_generation != host->panel_selection_generation ||
        !PluginHost_PanelNeedsDraw(host, selection_generation, widget_serial) )
        return 0;
    plugin = host->panel_active;
    slot = plugin_panel_find_serial(host, widget_serial);
    if( plugin < 0 || slot < 0 )
        return 0;

    plugin_copy_str(id, sizeof(id), host->panel_widgets[slot].id);
    host->panel_invalidated[slot] = false;
    memset(&ev, 0, sizeof(ev));
    ev.id = id;
    ev.surface = surface;
    ev.x = x;
    ev.y = y;
    ev.width = width;
    ev.height = height;

    assert(!host->draw_surface && "panel draw cannot nest another plugin draw pass");
    host->draw_surface = surface;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_PANEL;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_PANEL);
    plugin_dispatch_one(host, plugin, PLUGIN_CALLBACK_PANEL_DRAW, &ev);
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
    return 1;
}
