#include "plugin/torirs_plugin_host.h"

#include "log/torirs_log.h"
#include "plugin/torirs_plugin_frame.h"
#include "plugin/torirs_plugin_runtime.h"
#include "revconfig/revconfig.h"
#include "ui/uitree_minimenu.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdatomic.h>
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
    PLUGIN_CALLBACK_SCRIPT_CALLBACK,
    PLUGIN_CALLBACK_NPC_SPAWN,
    PLUGIN_CALLBACK_NPC_RETYPE,
    PLUGIN_CALLBACK_NPC_DESPAWN,
    PLUGIN_CALLBACK_KEY,
    PLUGIN_CALLBACK_MENU_BUILD,
    PLUGIN_CALLBACK_MENU_SELECT,
    PLUGIN_CALLBACK_DRAW_WORLD,
    PLUGIN_CALLBACK_DRAW_CANVAS,
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
    PLUGIN_CALLBACK_SCREEN_CHANGE,
    PLUGIN_CALLBACK_PANEL_BUILD,
    PLUGIN_CALLBACK_PANEL_ACTION,
    PLUGIN_CALLBACK_PANEL_LAYOUT,
    PLUGIN_CALLBACK_PANEL_DRAW,
    PLUGIN_CALLBACK_WIDGET_BINDING,
    /* An owned control's operation: a player's click, so the input verbs
     * (tab_select and the like) are open to it. */
    PLUGIN_CALLBACK_WIDGET_OPERATION,
    PLUGIN_CALLBACK_GAMEFRAME,
    PLUGIN_CALLBACK_DRAW_MINIMAP,
    PLUGIN_CALLBACK_COUNT
};

struct PluginTelemetryScope
{
    struct PluginTelemetryScope* parent;
    uint64_t started_ns, child_ns;
};
struct PluginTelemetry
{
    uint64_t (*clock_ns)(void* user);
    void* clock_user;
    struct PluginTelemetryScope* active;
    struct ToriRS_PluginCallbackTelemetry callbacks[TORIRS_PLUGIN_MAX][PLUGIN_CALLBACK_COUNT];
    struct ToriRS_PluginMutationTelemetry mutations[TORIRS_PLUGIN_MAX][TORIRS_PLUGIN_MUTATION_COUNT];
};

struct PluginCanvasDispatch
{
    void* surface;
    struct ToriRS_Rect bounds;
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

struct PluginConfigSlot
{
    char key[64];
    char value[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    /* Index into the plugin's declared schema, or -1 for a value read from
     * the ini that no schema claims. Those are kept rather than dropped: the
     * plugin that owns them may not have finished loading, and rewriting the
     * file must not delete a section we merely did not understand yet. */
    int schema_index;
    /* This value would not parse as a number and the plugin has already been
     * told so once. Cleared whenever the value changes, so a second typo is
     * reported again rather than swallowed by the first one's flag. */
    bool number_warned;
};

/*
 * One saved line whose section names no registered plugin.
 *
 * Held verbatim -- name, key and value, exactly as the file spelled them --
 * because the host has no schema to interpret them against and no business
 * inventing one. The whole point is that a run which cannot account for a
 * section must still be able to write it back unchanged.
 *
 * @see plugin_orphan_store.
 */
struct PluginOrphanConfig
{
    char plugin[TORIRS_PLUGIN_NAME_MAX];
    char key[64];
    char value[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
};


struct PluginV2Instance;
static void plugin_v2_init(struct PluginContext* ctx);
static void plugin_v2_shutdown(struct PluginContext* ctx);
static int plugin_v2_has_event_callback(
    struct ToriRS_PluginDef const* def,
    enum PluginCallbackKind event);
static enum ToriRS_CallbackResult plugin_v2_event(
    struct PluginContext* ctx,
    void* event,
    void* userdata);

#define PLUGIN_WIDGET_WATCH_MAX 32
struct PluginWidgetWatch
{
    char role[TORIRS_UI_NAME_MAX];
    uint64_t serial;
    uint64_t tree_instance, tree_generation;
    bool tree_notified;
    struct ToriRS_WidgetRef current;
    ToriRS_WidgetListener listener;
    void* user;
};

#define PLUGIN_WIDGET_OP_MAX 128
struct PluginWidgetOp
{
    struct ToriRS_WidgetRef widget;
    uint64_t serial;
    ToriRS_WidgetListener listener;
    void* user;
};

struct PluginContext
{
    struct ToriRS_PluginHost* host;
    struct ToriRS_PluginDef const* def;
    int index;
    /* The USER's switch, as the settings file holds it. Never written by
     * anything the plugin itself does -- see `refused`. */
    bool enabled;
    bool running;
    uint64_t lifecycle;
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
    /* The shared asset table was full when this plugin asked, and it has been
     * told so. Per plugin rather than per host so every starved plugin is
     * named, and a flag rather than nothing so a frame provider asking for
     * ninety-seven files does not write ninety-seven lines. */
    bool asset_budget_reported;
    /* The same, for the resident IMAGE table. A separate flag so a plugin
     * starved by one is still told when it is later starved by the other. */
    bool image_budget_reported;
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
    struct PluginV2Instance* v2;
    struct PluginWidgetWatch* widget_watches;
    struct PluginWidgetOp* widget_ops;
    void (*reload_handler)(
        struct ToriRS_PluginHost* host,
        int plugin_index,
        void* user);
    void* reload_user;
};

/* Image tokens belong to one plugin and map to arbitrary engine slots.
 * The global table also holds other plugins and cached item images, so it
 * need not fit in a single owner's reference table. */
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
    struct ToriRS_PluginDef const* definition;
    struct ToriRS_PluginDef definition_storage;
    struct ToriRS_PluginCallbacks callbacks_storage;
    struct ToriRS_ConfigSchema config_storage;
    struct ToriRS_FrameOffer frame_offers[TORIRS_PLUGIN_FRAME_OFFERS_MAX + 1];
    int frame_count;
    struct PluginV2Runtime runtime;
    bool runtime_initialized_once;
    void* state;
    /* This plugin's offer was provided through on_gameframe(active) and has
     * not been released yet. */
    bool gameframe_provided;
};

struct PluginMenuRoute
{
    int action;
    int plugin;
    uint32_t tag;
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
    /* Opt-in delivery fixture retains real IO bytes without making them
     * visible through assets.bytes before their delayed terminal event. */
    void* delayed_data;
    int delayed_size;
    uint64_t delayed_until_ms;
    bool delivery_held;
    bool delivery_fixture_used;
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
 * Which of the three draw surfaces is open.
 *
 * The values are the engine's `draw_select_canvas` argument, so the order is
 * load-bearing: it names the list a draw verb appends to, and app.c switches
 * on the same numbers.
 */
enum PluginDrawSurface
{
    PLUGIN_DRAW_SURFACE_WORLD = TORIRS_PLUGIN_ENGINE_DRAW_WORLD,
    PLUGIN_DRAW_SURFACE_CANVAS = TORIRS_PLUGIN_ENGINE_DRAW_CANVAS,
    /** Panel-local custom region prepared by the application shell. */
    PLUGIN_DRAW_SURFACE_PANEL = TORIRS_PLUGIN_ENGINE_DRAW_PANEL,
    PLUGIN_DRAW_SURFACE_MINIMAP = TORIRS_PLUGIN_ENGINE_DRAW_MINIMAP
};

struct ToriRS_PluginHost
{
    struct ToriRS_PluginEngine engine;
    struct PluginTelemetry* telemetry;

    struct PluginContext plugins[TORIRS_PLUGIN_MAX];
    int plugin_count;

    /** Stable callback orders computed once at registration. Event callbacks
     * run high-priority first; drawing runs low-z first. */
    int event_order[TORIRS_PLUGIN_MAX];
    int draw_order[TORIRS_PLUGIN_MAX];
    int callback_count[PLUGIN_CALLBACK_COUNT];
    uint64_t widget_tree_instance, widget_tree_generation, widget_watch_serial;
    bool widget_watch_pending, widget_watch_dispatching;
    struct PluginScriptStack const* script_stack;
    struct ToriRS_ScriptRef script_ref;

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
    int frame_bound_root;
    /** Offer named by the current request, still only a candidate. */
    int frame_target_entry;
    /** One-shot request consumed by the app's next safe layout fence. */
    int frame_layout_requested;
    int frame_preference_loaded;
    /** Set only by an explicit startup call after the boot task has loaded both
     * preference stores. FrameStart must not turn its initial dirty bit into an
     * implicit start while those stores are still zero-filled. */
    bool started_once;
    int frame_resolving;
    int frame_selection_dirty;

    /** enum ToriRS_FrameCanvas, and the pinned size for FIXED. */
    int layout_canvas;
    int layout_fixed_w;
    int layout_fixed_h;
    /** The platform-safe rect the provided frame was last laid out against
     *  (ToriRS_GameframeEvent.safe). The frame boundary polls the engine's
     *  band against this, and a move re-asks the provider once. */
    int safe_raised_x;
    int safe_raised_y;
    int safe_raised_w;
    int safe_raised_h;
    /** And the canvas that raise was against: what `safe` falls back to when
     *  the engine reports no band. */
    int canvas_raised_w;
    int canvas_raised_h;
    /** Advances across every resolved offer transition. Fences an in-flight
     * frame-build candidate from committing after selection moved. */
    uint32_t frame_selection_epoch;
    /* Standing entity presentation/action claims. */
    struct PluginEntityClaim entity_claims[PLUGIN_ENTITY_CLAIMS_MAX];

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

    /* Saved settings for plugins this run does not have. Carried through the
     * rewrite untouched; adopted the moment their plugin registers. */
    struct PluginOrphanConfig* orphan_config;
    int orphan_config_count;
    int orphan_config_capacity;
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
        /* A declared schema cannot reach this -- PluginHost_Register refuses
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

/* -- settings belonging to plugins this run does not have ---------------- */

/*
 * Remember one line whose section names nothing registered.
 *
 * Keyed by (plugin, key) and REPLACED rather than appended, so replaying the
 * same file twice -- which a reconnect or a settings save will do -- cannot
 * grow the table.
 */
static void
plugin_orphan_store(
    struct ToriRS_PluginHost* host,
    char const* plugin_name,
    char const* key,
    char const* value)
{
    struct PluginOrphanConfig* row;

    assert(host);
    assert(plugin_name);
    assert(key);
    assert(value);

    /* A name too long to be a plugin's cannot be one this build is missing
     * either, so there is nothing here worth carrying. Data, not a bug: the
     * string came off a line in a file. */
    if( strlen(plugin_name) >= sizeof(row->plugin) )
        return;

    for( int i = 0; i < host->orphan_config_count; i++ )
    {
        row = &host->orphan_config[i];
        if( strcmp(row->plugin, plugin_name) == 0 && strcmp(row->key, key) == 0 )
        {
            snprintf(row->value, sizeof(row->value), "%s", value);
            return;
        }
    }

    if( host->orphan_config_count == host->orphan_config_capacity )
    {
        /* A rendering budget must never become a settings deletion budget. */
        assert(host->orphan_config_capacity <= INT_MAX / 2);
        int capacity = host->orphan_config_capacity ? host->orphan_config_capacity * 2 : 32;
        host->orphan_config = realloc(
            host->orphan_config, (size_t)capacity * sizeof(*host->orphan_config));
        assert(host->orphan_config);
        host->orphan_config_capacity = capacity;
    }

    row = &host->orphan_config[host->orphan_config_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->plugin, sizeof(row->plugin), "%s", plugin_name);
    snprintf(row->key, sizeof(row->key), "%s", key);
    snprintf(row->value, sizeof(row->value), "%s", value);
}

/*
 * A plugin just registered: hand it whatever the file had saved for it and
 * drop those rows.
 *
 * Registration after a decode is the ordinary case for a script the manifest
 * loads late, and it is also what keeps the encoder honest -- a name held in
 * both places would be written as two sections of the same plugin, the second
 * of them stale, and the next decode would apply the stale one last.
 */
static void
plugin_orphan_adopt(
    struct ToriRS_PluginHost* host,
    int index)
{
    int kept = 0;

    assert(host);
    assert(index >= 0);
    assert(index < host->plugin_count);

    for( int i = 0; i < host->orphan_config_count; i++ )
    {
        struct PluginOrphanConfig* row = &host->orphan_config[i];
        if( strcmp(row->plugin, host->plugins[index].name) != 0 )
        {
            if( kept != i )
                host->orphan_config[kept] = *row;
            kept++;
            continue;
        }
        PluginHost_ConfigApply(host, row->plugin, row->key, row->value);
    }
    host->orphan_config_count = kept;
}

static void
plugin_telemetry_begin(struct ToriRS_PluginHost* host, struct PluginTelemetryScope* scope)
{
    if( !host->telemetry ) return;
    scope->parent = host->telemetry->active;
    scope->child_ns = 0;
    scope->started_ns = host->telemetry->clock_ns(host->telemetry->clock_user);
    host->telemetry->active = scope;
}

static void
plugin_telemetry_end(struct ToriRS_PluginHost* host, int plugin,
    enum PluginCallbackKind kind, struct PluginTelemetryScope* scope)
{
    if( !host->telemetry ) return;
    struct PluginTelemetry* telemetry = host->telemetry;
    uint64_t const now = telemetry->clock_ns(telemetry->clock_user);
    assert(telemetry->active == scope);
    assert(now >= scope->started_ns);
    uint64_t const elapsed = now - scope->started_ns;
    assert(elapsed >= scope->child_ns);
    struct ToriRS_PluginCallbackTelemetry* row = &telemetry->callbacks[plugin][kind];
    row->calls++;
    row->elapsed_ns += elapsed;
    row->self_ns += elapsed - scope->child_ns;
    if( elapsed > row->max_ns ) row->max_ns = elapsed;
    telemetry->active = scope->parent;
    if( scope->parent ) scope->parent->child_ns += elapsed;
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

    struct DispatchEntry { int plugin; uint64_t lifecycle; } snapshot[TORIRS_PLUGIN_MAX];
    int count=0;
    int const* order = plugin_ev_is_draw(ev) ? host->draw_order : host->event_order;
    for( int i=0;i<host->plugin_count;++i )
    {
        struct PluginContext const* ctx=&host->plugins[order[i]];
        if( ctx->enabled && ctx->running && plugin_v2_has_event_callback(ctx->def,ev) )
            snapshot[count++]=(struct DispatchEntry){order[i],ctx->lifecycle};
    }
    for( int i = 0; i < count; i++ )
    {
        int const plugin = snapshot[i].plugin;
        struct PluginContext* ctx = &host->plugins[plugin];
        if( !ctx->enabled || !ctx->running || ctx->lifecycle!=snapshot[i].lifecycle ||
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
 * ToriRS_PluginDef::draw_order and the rest sort by `priority`. One list, two
 * keys, chosen here -- the alternative is a second subscription table that
 * only three events use.
 */
static int
plugin_ev_is_draw(enum PluginCallbackKind ev)
{
    return ev == PLUGIN_CALLBACK_DRAW_WORLD || ev == PLUGIN_CALLBACK_DRAW_CANVAS ||
           ev == PLUGIN_CALLBACK_PANEL_DRAW || ev == PLUGIN_CALLBACK_DRAW_MINIMAP;
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
    if( getenv("TORIRS_PLUGIN_LOG") )
    {
        char text[2048];
        va_start(args, fmt);
        vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        TORIRS_REPORT("[%s] %s\n", ctx->name, text);
        return;
    }
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
    host->engine.frame_activate(
        host->engine.user,
        plugin_frame_owner(host) >= 0 ? 1 : 0,
        host->layout_canvas,
        host->layout_fixed_w,
        host->layout_fixed_h);
}

static int
api_tab_active(struct PluginContext* ctx)
{
    assert(ctx);
    return ctx->host->engine.tab_active(ctx->host->engine.user);
}

static bool
api_tab_action_allowed(struct PluginContext* ctx)
{
    assert(ctx);
    switch( ctx->host->dispatch_event )
    {
    case PLUGIN_CALLBACK_KEY:
    case PLUGIN_CALLBACK_MENU_SELECT:
    case PLUGIN_CALLBACK_PANEL_ACTION:
    case PLUGIN_CALLBACK_WIDGET_OPERATION:
        return true;
    default:
        return false;
    }
}

static bool
api_tab_select(struct PluginContext* ctx, int tabno)
{
    assert(ctx);
    if( !api_tab_action_allowed(ctx) )
        return false;
    /* A tab number a plugin read off its own stone table. */
    if( tabno < 0 )
        return false;
    return ctx->host->engine.tab_select(ctx->host->engine.user, tabno) ? true : false;
}

static bool
api_tab_activate(struct PluginContext* ctx, int tabno)
{
    assert(ctx);
    if( !api_tab_action_allowed(ctx) || tabno < 0 || !ctx->host->engine.tab_activate )
        return false;
    return ctx->host->engine.tab_activate(ctx->host->engine.user, tabno) != 0;
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
 * A refusal, not an abort: a plugin polling a surface's size every frame
 * across a logout is doing nothing wrong, and this is the answer it should get.
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
 * @return false when the value is not one whole expression, leaving `*out`
 * alone. Whether that is worth a message is the CALLER's question -- see
 * plugin_cfg_number_checked, which is where every api accessor asks it.
 */
static bool
plugin_cfg_number_parse(
    char const* value,
    int* out)
{
    char const* end = NULL;
    int parsed = 0;

    assert(value);
    assert(out);

    if( !revconfig_parse_int_expr(value, &end, &parsed) )
        return false;
    /* One expression and nothing after it -- "5 apples" is a typo, not a 5. */
    while( *end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' )
        end++;
    if( *end != '\0' )
        return false;
    *out = parsed;
    return true;
}

/**
 * The spellings a settings file writes a switch in, both ways round.
 *
 * Named here rather than inside api_cfg_bool because a DECLARED DEFAULT is
 * written the same way -- `default_value = "true"` is the ordinary shape of a
 * bool schema item -- so the fallback path has to read it with the same rules
 * the stored value gets.
 */
static bool
plugin_cfg_bool_parse(
    char const* value,
    int* out)
{
    assert(value);
    assert(out);

    if( strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "on") == 0 )
    {
        *out = 1;
        return true;
    }
    if( strcmp(value, "false") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "off") == 0 )
    {
        *out = 0;
        return true;
    }
    return plugin_cfg_number_parse(value, out);
}

/**
 * A stored value as a number, falling back to what the PLUGIN declared.
 *
 * The fallback used to be a bare 0, which for the keys this exists for is a
 * colour (black) and a plausible id, so typing `cyan` into a colour field --
 * or leaving a stray character in a hex one -- produced a marker that drew,
 * in the wrong colour, with nothing said. Zero is also not what the plugin
 * asked for: it shipped a default for this key and that default is the answer
 * to "the file does not give me a usable value".
 *
 * Said out loud, once per slot rather than once per read, because a colour is
 * read on the draw path and the flag is cleared whenever the value changes --
 * so a second typo is reported again and a correction stops the message.
 *
 * @param fallback What to answer when the schema has no usable default either.
 */
static int
plugin_cfg_number_checked(
    struct PluginContext* ctx,
    char const* key,
    bool (*parse)(char const*, int*),
    int fallback)
{
    struct PluginConfigSlot* slot;
    char const* value;
    char const* declared = NULL;
    int parsed = 0;

    assert(ctx);
    assert(key);
    assert(parse);

    value = api_cfg_str(ctx, key);
    /* Empty is "unset", which is a state and not a typo: a key with no
     * declared default is seeded empty and reads as the caller's fallback,
     * silently, exactly as it always has. */
    if( value[0] == '\0' )
        return fallback;
    if( parse(value, &parsed) )
        return parsed;

    /* api_cfg_str has already asserted the key is one this plugin declared,
     * so the slot behind it exists. */
    slot = plugin_config_slot(ctx, key, false);
    assert(slot);
    if( slot->schema_index >= 0 )
        declared = plugin_schema(ctx)[slot->schema_index].default_value;

    if( declared && declared[0] && parse(declared, &parsed) )
    {
        if( !slot->number_warned )
        {
            slot->number_warned = true;
            TORIRS_ERR(
                "plugin: %s cannot read setting '%s' = '%s'; using the declared "
                "default '%s'\n",
                ctx->name,
                key,
                value,
                declared);
        }
        return parsed;
    }

    if( !slot->number_warned )
    {
        slot->number_warned = true;
        TORIRS_ERR(
            "plugin: %s cannot read setting '%s' = '%s', and declares no default it "
            "can read either; reading it as %d\n",
            ctx->name,
            key,
            value,
            fallback);
    }
    return fallback;
}

static int
api_cfg_bool(
    struct PluginContext* ctx,
    char const* key)
{
    return plugin_cfg_number_checked(ctx, key, plugin_cfg_bool_parse, 0) != 0;
}

bool
PluginHost_ConfigGetBool(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* key)
{
    assert(host);
    assert(key);
    return api_cfg_bool(plugin_at(host, plugin_index), key) != 0;
}

static int
api_cfg_int(
    struct PluginContext* ctx,
    char const* key)
{
    return plugin_cfg_number_checked(ctx, key, plugin_cfg_number_parse, 0);
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
    return (uint32_t)plugin_cfg_number_checked(ctx, key, plugin_cfg_number_parse, 0) &
           0xffffffu;
}

static bool
plugin_config_schema_value_valid(struct ToriRS_ConfigItem const* item, char const* value)
{
    int parsed;
    assert(item);
    assert(value);
    if( item->type == TORIRS_CONFIG_BOOL )
        return plugin_cfg_bool_parse(value, &parsed);
    if( item->type == TORIRS_CONFIG_INT || item->type == TORIRS_CONFIG_COLOR )
    {
        if( !plugin_cfg_number_parse(value, &parsed) )
            return false;
        return item->type != TORIRS_CONFIG_INT || item->max <= item->min ||
            (parsed >= item->min && parsed <= item->max);
    }
    if( item->type != TORIRS_CONFIG_ENUM )
        return true;
    if( !item->choices )
        return false;
    size_t const length = strlen(value);
    char const* choice = item->choices;
    for( ;; )
    {
        char const* end = strchr(choice, '|');
        size_t const size = end ? (size_t)(end - choice) : strlen(choice);
        if( size == length && memcmp(choice, value, size) == 0 )
            return true;
        if( !end )
            return false;
        choice = end + 1;
    }
}

bool
PluginHost_ConfigValidate(
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
    struct PluginConfigSlot* slot = plugin_config_slot(ctx, key, false);
    int const schema_index = slot ? slot->schema_index : plugin_schema_index(ctx, key);
    if( schema_index >= 0 &&
        !plugin_config_schema_value_valid(&plugin_schema(ctx)[schema_index], value) )
    {
        TORIRS_ERR("plugin: %s refused setting '%s' = '%s': outside its declared type, range or choices\n",
            ctx->name, key, value);
        return false;
    }
    return true;
}

bool
PluginHost_ConfigSet(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* key,
    char const* value)
{
    if( !PluginHost_ConfigValidate(host, plugin_index, key, value) )
        return false;
    struct PluginContext* ctx = plugin_at(host, plugin_index);
    struct PluginConfigSlot* slot = plugin_config_slot(ctx, key, true);
    if( !slot )
        return false;
    if( strcmp(slot->value, value) == 0 )
        return true;

    snprintf(slot->value, sizeof(slot->value), "%s", value);
    /* A new spelling gets a fresh hearing: the complaint about the old one
     * must not silence a complaint about this one, and a correction must be
     * able to stop the message. */
    slot->number_warned = false;
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
    assert(!plugin_policy(ctx, TORIRS_PLUGIN_ESSENTIAL));

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
    free(slot->delayed_data);
    int const at = (int)(slot - host->assets);
    for( int i = at; i < host->asset_count - 1; i++ )
        host->assets[i] = host->assets[i + 1];
    host->asset_count--;
    memset(&host->assets[host->asset_count], 0, sizeof(host->assets[0]));
}

/*
 * Say that the shared asset table refused this plugin.
 *
 * TORIRS_ERR and not TORIRS_LOG: narration is compiled out under OPT=1, which
 * is the build people run, so the old message existed only where nobody was
 * looking. A starved plugin does not crash -- it draws a page with no art on
 * it, or a frame with half its stones missing -- and "why is it blank" has no
 * other answer available from the outside.
 *
 * Once per plugin. The v2 asset and image paths refuse BEFORE reaching the
 * claim, so they call this too; all three roads to a refusal say the same
 * thing.
 */
static void
plugin_asset_budget_refused(
    struct PluginContext* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);

    if( ctx->asset_budget_reported )
        return;
    ctx->asset_budget_reported = true;
    TORIRS_ERR(
        "plugin: %s asked for '%s' but the shared asset table is full (%d of %d "
        "resident); this plugin's remaining art will not load\n",
        ctx->name,
        name,
        ctx->host->asset_count,
        TORIRS_PLUGIN_ASSETS_MAX);
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
        plugin_asset_budget_refused(ctx, name);
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
        plugin_asset_budget_refused(ctx, name);
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

    if( !slot->delivery_fixture_used )
    {
        char const* fixture = getenv("TORIRS_SIM_ASSET_DELIVERY_DELAY");
        char wanted_plugin[64] = {0};
        char wanted_asset[64] = {0};
        unsigned long delay = 0;
        if( fixture && sscanf(fixture, "%63[^,],%63[^,],%lu", wanted_plugin, wanted_asset, &delay) == 3 &&
            strcmp(plugin_name, wanted_plugin) == 0 && strcmp(asset_name, wanted_asset) == 0 )
        {
            assert(delay > 0);
            assert(delay <= 60000);
            assert(!slot->delivery_held);
            slot->delivery_fixture_used = true;
            slot->delivery_held = true;
            slot->delayed_data = data;
            slot->delayed_size = size;
            slot->delayed_until_ms = host->engine.frame_ms(host->engine.user) + delay;
            TORIRS_REPORT("ASSET_DELIVERY_HELD plugin=%s asset=%s delay_ms=%lu bytes=%d\n",
                plugin_name, asset_name, delay, size);
            return;
        }
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

/**
 * Set a custom well's preferred height, and SAY when the number was not used.
 *
 * A well is bounded -- TORIRS_PANEL_CUSTOM_HEIGHT_MAX is what the shell will
 * allocate -- and a plugin that composes a taller row than that is not making
 * a mistake it can see: its own draw callback receives the granted rect, so
 * everything past the bound simply is not there. The tracker pages are the
 * shape this bites: one asks for a row per kill source and the other for a box
 * per skill trained, and both compute a height that grows without limit over a
 * long trip. Answering `true` to a request that was cut left them drawing off
 * the end of the well with no way to learn they should page or shrink.
 *
 * So the clamp is still applied -- the shell cannot allocate more, and leaving
 * the old height in place would be worse than a bounded one -- but the answer
 * distinguishes it. @see TORIRS_RESULT_BUDGET.
 *
 * @return TORIRS_RESULT_OK when the exact request was recorded,
 *   TORIRS_RESULT_BUDGET when a bounded height was recorded instead,
 *   TORIRS_RESULT_NOT_FOUND when there is no such custom well to set.
 */
static enum ToriRS_Result
api_panel_set_height_result(
    struct PluginContext* ctx,
    char const* custom_view_id,
    int preferred_height)
{
    struct ToriRS_PanelWidget* widget;
    int requested;
    int granted;
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, custom_view_id, &slot) )
        return TORIRS_RESULT_NOT_FOUND;
    widget = &ctx->host->panel_widgets[slot];
    if( widget->kind != TORIRS_PANEL_WIDGET_CUSTOM )
        return TORIRS_RESULT_NOT_FOUND;

    /* Zero is the documented "give me the usual well", not a request for no
     * height, so it is a substitution and never a refusal. */
    requested = preferred_height == 0 ? TORIRS_PANEL_CUSTOM_HEIGHT_DEFAULT : preferred_height;
    granted = requested;
    if( granted < TORIRS_PANEL_CUSTOM_HEIGHT_MIN )
        granted = TORIRS_PANEL_CUSTOM_HEIGHT_MIN;
    if( granted > TORIRS_PANEL_CUSTOM_HEIGHT_MAX )
        granted = TORIRS_PANEL_CUSTOM_HEIGHT_MAX;

    if( widget->preferred_height != granted )
    {
        widget->preferred_height = granted;
        ctx->host->panel_invalidated[slot] = true;
        plugin_panel_bump(&ctx->host->panel_model_revision);
        plugin_panel_change_widget(
            ctx->host, slot, TORIRS_PLUGIN_PANEL_CHANGE_HEIGHT);
    }

    if( granted == requested )
    {
        widget->height_clamped = false;
        return TORIRS_RESULT_OK;
    }

    /*
     * The user hears it too, once per transition into the clamped state. A
     * plugin is free to ignore the return -- the shipped pages did, which is
     * how this stayed invisible -- and the person looking at a chart with its
     * bottom missing has no other way to find out.
     */
    if( !widget->height_clamped )
    {
        widget->height_clamped = true;
        TORIRS_ERR(
            "plugin: %s asked for a %dpx custom area '%s'; the page allocates at most "
            "%dpx, so everything past that is not drawn\n",
            ctx->name,
            requested,
            widget->id,
            TORIRS_PANEL_CUSTOM_HEIGHT_MAX);
    }
    return TORIRS_RESULT_BUDGET;
}

/*
 * The bool spelling the pre-ABI-21 call sites use.
 *
 * `false` for a clamp as well as for a missing well: those callers have no
 * richer answer to give, and "your number was not used" is the half of the
 * news they can act on. api_panel_set_height_result is what the v2 api should
 * return whole.
 */
static bool
api_panel_set_height(
    struct PluginContext* ctx,
    char const* custom_view_id,
    int preferred_height)
{
    return api_panel_set_height_result(ctx, custom_view_id, preferred_height) ==
           TORIRS_RESULT_OK;
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
plugin_draw_budget_allow(
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
            TORIRS_ERR(
                "plugin: %s hit its %d-item draw budget this frame; "
                "the rest of its overlay was dropped\n",
                ctx->name,
                TORIRS_PLUGIN_DRAW_BUDGET);
        }
        return false;
    }
    return true;
}

static bool plugin_draw_allow(struct PluginContext* ctx, void* surface)
{
    assert(ctx);
    assert(ctx->host->draw_canvas != PLUGIN_DRAW_SURFACE_MINIMAP);
    return plugin_draw_budget_allow(ctx,surface);
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

static enum ToriRS_Result
plugin_draw_account(struct PluginContext* ctx, int emitted)
{
    assert(ctx);
    if( emitted >= 0 )
    {
        ctx->draw_used += emitted;
        return TORIRS_RESULT_OK;
    }
    assert(emitted == -1 || emitted == -2);
    if( !ctx->draw_clipped )
    {
        ctx->draw_clipped = true;
        TORIRS_ERR("plugin: %s overlay was not drawn: %s\n", ctx->name,
                   emitted == -1 ? "the shared overlay pool is full"
                                 : "the primitive exceeds the remaining per-frame draw budget");
    }
    return TORIRS_RESULT_BUDGET;
}

static enum ToriRS_Result api_draw_minimap_tile(struct PluginContext* ctx, void* surface,
    int x, int z, int level, uint32_t fill, uint32_t outline, int alpha, int width)
{
    assert(ctx);
    assert(ctx->host->draw_canvas == PLUGIN_DRAW_SURFACE_MINIMAP);
    assert(ctx->host->draw_surface);
    assert(surface==ctx->host->draw_surface);
    if( !ctx->host->engine.draw_minimap_tile ) return TORIRS_RESULT_UNSUPPORTED;
    return plugin_draw_account(ctx,ctx->host->engine.draw_minimap_tile(ctx->host->engine.user,
        x,z,level,outline,fill,alpha,width,TORIRS_PLUGIN_DRAW_BUDGET-ctx->draw_used));
}

static enum ToriRS_Result
api_draw_tile_stroke(
    struct PluginContext* ctx,
    void* surface,
    int tile_x,
    int tile_z,
    int level,
    uint32_t rgb,
    uint32_t fill_rgb,
    int fill_alpha,
    int outline_width)
{
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx, surface) )
        return TORIRS_RESULT_BUDGET;
    if( ctx->host->engine.draw_tile_stroke )
        return plugin_draw_account(ctx, ctx->host->engine.draw_tile_stroke(
            ctx->host->engine.user, tile_x, tile_z, level, rgb, fill_rgb, fill_alpha,
            outline_width, TORIRS_PLUGIN_DRAW_BUDGET - ctx->draw_used));
    if( outline_width != 1 )
        return TORIRS_RESULT_UNSUPPORTED;
    return plugin_draw_account(ctx, ctx->host->engine.draw_tile(
        ctx->host->engine.user, tile_x, tile_z, level, rgb, fill_rgb, fill_alpha));
}

static enum ToriRS_Result
api_draw_hull_stroke(
    struct PluginContext* ctx,
    void* surface,
    int element_id,
    uint32_t rgb,
    int fill_alpha,
    int shape,
    int outline_width)
{
    assert(shape == TORIRS_HULL_BOUNDS || shape == TORIRS_HULL_MESH);
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx, surface) )
        return TORIRS_RESULT_BUDGET;
    /* An entity whose APPEARANCE facet another plugin owns is that plugin's to
     * outline. Refusal is silent, like an element that is not on screen. */
    if( !plugin_entity_hull_allowed(ctx->host, ctx->index, element_id) )
        return TORIRS_RESULT_OK;
    if( ctx->host->engine.draw_hull_stroke )
        return plugin_draw_account(ctx, ctx->host->engine.draw_hull_stroke(
            ctx->host->engine.user, element_id, rgb, fill_alpha, shape, outline_width,
            TORIRS_PLUGIN_DRAW_BUDGET - ctx->draw_used));
    if( outline_width != 1 )
        return TORIRS_RESULT_UNSUPPORTED;
    return plugin_draw_account(ctx,
        ctx->host->engine.draw_hull(ctx->host->engine.user, element_id, rgb, fill_alpha, shape));
}

static enum ToriRS_Result
api_draw_tile_styled(struct PluginContext* ctx,void* surface,int x,int z,int level,
    uint32_t rgb,uint32_t fill,int alpha,int width,uint32_t flags)
{
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx,surface) ) return TORIRS_RESULT_BUDGET;
    if( !ctx->host->engine.draw_tile_styled ) return TORIRS_RESULT_UNSUPPORTED;
    return plugin_draw_account(ctx,ctx->host->engine.draw_tile_styled(
        ctx->host->engine.user,x,z,level,rgb,fill,alpha,width,flags,
        TORIRS_PLUGIN_DRAW_BUDGET-ctx->draw_used));
}

static enum ToriRS_Result
api_draw_hull_styled(struct PluginContext* ctx,void* surface,int element_id,
    uint32_t rgb,int fill_alpha,int shape,int outline_width,uint32_t flags)
{
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx,surface) ) return TORIRS_RESULT_BUDGET;
    if( !plugin_entity_hull_allowed(ctx->host,ctx->index,element_id) )
        return TORIRS_RESULT_OK;
    if( !ctx->host->engine.draw_hull_styled ) return TORIRS_RESULT_UNSUPPORTED;
    return plugin_draw_account(ctx,ctx->host->engine.draw_hull_styled(
        ctx->host->engine.user,element_id,rgb,fill_alpha,shape,outline_width,flags,
        TORIRS_PLUGIN_DRAW_BUDGET-ctx->draw_used));
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

/* -- the one verb that acts -- */

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
    assert(engine->frame_provide);
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
    assert(engine->slot_native_size);
    assert(engine->component_rect);
    assert(engine->menu_drop);
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
    /* 0 is a real plugin index, so an empty claim row needs a value of its
     * own rather than the calloc's zero. */
    for( int i = 0; i < PLUGIN_ENTITY_CLAIMS_MAX; i++ )
        host->entity_claims[i].plugin = -1;
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
            (void)plugin_v2_event(ctx, NULL,
                (void*)(intptr_t)(PLUGIN_CALLBACK_STOP + 1));
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
    free(host->orphan_config);
    free(host->telemetry);
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

#define PLUGIN_FRAME_PREFERENCE_MIGRATION 1

/* Map the pre-catalogue gameframe-layout setting onto the stable V2 offer id.
 * Old files contain either the visible label or its former numeric row. */
static char const*
plugin_frame_legacy_gameframe_choice(struct PluginContext* ctx)
{
    struct PluginConfigSlot const* slot;
    char const* value;

    assert(ctx);
    slot = plugin_config_slot(ctx, "layout", false);
    value = slot ? slot->value : NULL;
    if( !value || !value[0] || strcmp(value, "Auto") == 0 ||
        strcmp(value, "3") == 0 )
        return "auto";
    if( strcmp(value, "Classic Fixed") == 0 || strcmp(value, "0") == 0 )
        return "gameframe-layout/classic-fixed";
    if( strcmp(value, "Modern Fixed") == 0 || strcmp(value, "1") == 0 )
        return "gameframe-layout/modern-fixed";
    if( strcmp(value, "Modern Resizable") == 0 || strcmp(value, "2") == 0 )
        return "gameframe-layout/modern-resizable";
    return "auto";
}

/** Read or migrate the one device preference after both preference stores
 * have landed. */
static void
plugin_frame_preference_load(struct ToriRS_PluginHost* host)
{
    char requested[TORIRS_PLUGIN_FRAME_ID_MAX] = "auto";
    int migration = 0;
    int present = 0;

    assert(host);
    if( host->frame_preference_loaded )
        return;
    host->frame_preference_loaded = 1;

    if( host->engine.frame_preference )
        present = host->engine.frame_preference(
            host->engine.user, requested, (int)sizeof(requested), &migration);
    if( !plugin_frame_preference_id_valid(requested) )
    {
        TORIRS_REPORT("plugin: invalid saved gameframe '%s'; using auto\n", requested);
        snprintf(requested, sizeof(requested), "%s", "auto");
    }

    if( migration < PLUGIN_FRAME_PREFERENCE_MIGRATION )
    {
        if( !present )
        {
            int const desktop = PluginHost_IndexOf(host, "gameframe-layout");
            int const mobile = PluginHost_IndexOf(host, "mobile-gameframe");

            if( desktop >= 0 && host->plugins[desktop].enabled )
            {
                snprintf(
                    requested,
                    sizeof(requested),
                    "%s",
                    plugin_frame_legacy_gameframe_choice(&host->plugins[desktop]));
                if( mobile >= 0 && host->plugins[mobile].enabled )
                    TORIRS_REPORT(
                        "plugin: both legacy gameframe plugins were enabled; preserving "
                        "gameframe-layout, which previously won by registry order\n");
            }
            else if( mobile >= 0 && host->plugins[mobile].enabled )
                snprintf(
                    requested,
                    sizeof(requested),
                    "%s",
                    "mobile-gameframe/stone-drawer");
        }
        if( host->engine.frame_preference_set )
            (void)host->engine.frame_preference_set(
                host->engine.user,
                requested,
                PLUGIN_FRAME_PREFERENCE_MIGRATION);
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
    if( getenv("TORIRS_FRAME_ROLE_AUDIT") )
    {
        TORIRS_REPORT("frame_selection: requested=%s active=%s status=%d reason=%s\n",
                   host->frame_selection.requested_id, active, status, why);
        int total_images = 0;
        for( int image = 0; image < TORIRS_PLUGIN_IMAGES_MAX; image++ )
            total_images += host->images[image].plugin >= 0;
        for( int plugin = 0; plugin < host->plugin_count; plugin++ )
        {
            struct PluginContext const* ctx = &host->plugins[plugin];
            int images = 0, assets = 0, pending = 0;
            if( !plugin_provides_frames(ctx) )
                continue;
            for( int image = 0; image < TORIRS_PLUGIN_IMAGES_MAX; image++ )
                images += host->images[image].plugin == plugin;
            for( int asset = 0; asset < host->asset_count; asset++ )
                if( host->assets[asset].plugin == plugin )
                {
                    assets++;
                    pending += host->assets[asset].pending;
                }
            TORIRS_REPORT("FRAME_RESIDENCY provider=%s running=%d image_slots=%d assets=%d pending=%d total_image_slots=%d capacity=%d active=%s requested=%s status=%d\n",
                ctx->name, ctx->running, images, assets, pending, total_images,
                TORIRS_PLUGIN_IMAGES_MAX, active, host->frame_selection.requested_id, status);
        }
    }
}

/* The canvas less the platform's band, as ToriRS_GameframeEvent.safe: the
 * engine's answer when it has one, else the whole `width` x `height`. */
static void
plugin_safe_rect_read(
    struct ToriRS_PluginHost const* host,
    int width,
    int height,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int x = 0;
    int y = 0;
    int w = width;
    int h = height;

    assert(host);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    if( host->engine.platform_safe_rect &&
        host->engine.platform_safe_rect(host->engine.user, &x, &y, &w, &h) )
    {
        assert(w > 0);
        assert(h > 0);
    }
    else
    {
        x = 0;
        y = 0;
        w = width;
        h = height;
    }
    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

/* Tell a provider its offer is released. Dispatch context matches a build. */
static void
plugin_gameframe_release(struct ToriRS_PluginHost* host, int owner)
{
    struct PluginV2Instance* v2;
    struct PluginFrameCatalogEntry const* entry;
    struct ToriRS_GameframeEvent ev;
    char reason[TORIRS_FRAME_REASON_MAX] = { 0 };
    int const previous_dispatching = host->dispatching;
    int const previous_event = host->dispatch_event;

    assert(host);
    assert(owner >= 0);
    v2 = host->plugins[owner].v2;
    assert(v2);
    v2->gameframe_provided = false;
    if( !host->plugins[owner].running || !v2->definition->callbacks.on_gameframe )
        return;
    entry = PluginFrameCatalog_At(&host->frame_catalog, host->frame_active_entry);
    memset(&ev, 0, sizeof(ev));
    ev.offer_id = entry ? entry->local_id : "";
    ev.active = false;
    ev.canvas = host->layout_canvas;
    ev.width = host->layout_fixed_w;
    ev.height = host->layout_fixed_h;
    (void)v2->runtime.api.core.lane(&v2->runtime.api, &ev.lane);
    ev.reason = reason;
    ev.reason_capacity = sizeof(reason);
    plugin_safe_rect_read(
        host, ev.width, ev.height, &ev.safe.x, &ev.safe.y, &ev.safe.width, &ev.safe.height);
    host->dispatching = owner;
    host->dispatch_event = PLUGIN_CALLBACK_LAYOUT;
    struct PluginTelemetryScope scope;
    plugin_telemetry_begin(host, &scope);
    (void)v2->definition->callbacks.on_gameframe(&v2->runtime.api, v2->state, &ev);
    plugin_telemetry_end(host, owner, PLUGIN_CALLBACK_GAMEFRAME, &scope);
    host->dispatching = previous_dispatching;
    host->dispatch_event = previous_event;
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

    /* The provider that held the frame hears the release while it still runs;
     * its teardown, when selection drops it, follows. */
    {
        int const previous = plugin_frame_owner(host);
        if( previous >= 0 && previous != owner && host->plugins[previous].v2 &&
            host->plugins[previous].v2->gameframe_provided )
            plugin_gameframe_release(host, previous);
    }
    host->frame_selection_epoch++;
    host->frame_active_entry = entry_index;
    host->layout_canvas = canvas;
    host->layout_fixed_w = width;
    host->layout_fixed_h = height;
    plugin_layout_publish(host);
}

static void plugin_frame_target_set(struct ToriRS_PluginHost* host, int entry);

static void
plugin_frame_discard_stale_binding(struct ToriRS_PluginHost* host, int desired_entry)
{
    if( host->frame_active_entry < 0 ) return;
    bool stale = host->engine.frame_root && host->engine.frame_root(host->engine.user) != host->frame_bound_root;
    if( !stale ) return;
    plugin_frame_engine_activate(host, -1);
    plugin_frame_target_set(host, desired_entry);
    host->frame_layout_requested = 1;
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

static void
plugin_v2_image_release(
    void* user,
    struct PluginContext* context,
    struct ToriRS_ImageRef image)
{
    struct PluginV2Instance* v2;
    int image_slot;

    assert(user);
    assert(context);
    assert(context->host == (struct ToriRS_PluginHost*)user);
    (void)user;
    v2 = context->v2;
    assert(v2);
    image_slot = plugin_v2_runtime_image_slot(&v2->runtime, image);
    if( image_slot < 0 )
        return;
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
    struct ToriRS_PluginSelectOption replacement[TORIRS_PLUGIN_SELECT_OPTIONS_MAX];
    char selected_value[TORIRS_PLUGIN_SELECT_VALUE_MAX];
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
        option_count < 0 || option_count > TORIRS_PLUGIN_SELECT_OPTIONS_MAX ||
        host->panel_select_option_count - widget->select_option_count + option_count >
            TORIRS_PLUGIN_SELECT_OPTIONS_MAX ||
        (option_count > 0 && !options) ||
        strlen(value) >= TORIRS_PLUGIN_SELECT_VALUE_MAX )
        return TORIRS_RESULT_INVALID;
    plugin_copy_str(selected_value, sizeof(selected_value), value);
    changed = option_count != widget->select_option_count;
    /* Validate and copy the complete replacement before touching the retained
     * pool. A rejected row must not leave an earlier row partially updated. */
    for( int i = 0; i < option_count; i++ )
    {
        struct ToriRS_PluginSelectOption* destination = &replacement[i];
        char const* detail;
        if( options[i].struct_size < TORIRS_SELECT_OPTION_REQUIRED_SIZE ||
            !options[i].value || !options[i].value[0] || !options[i].label ||
            strlen(options[i].value) >= TORIRS_PLUGIN_SELECT_VALUE_MAX )
            return TORIRS_RESULT_INVALID;
        for( int j = 0; j < i; j++ )
            if( strcmp(options[i].value, options[j].value) == 0 )
                return TORIRS_RESULT_INVALID;
        detail = options[i].detail ? options[i].detail : "";
        memset(destination, 0, sizeof(*destination));
        plugin_copy_str(destination->value, sizeof(destination->value), options[i].value);
        plugin_copy_str(destination->label, sizeof(destination->label), options[i].label);
        plugin_copy_str(destination->detail, sizeof(destination->detail), detail);
        destination->enabled = options[i].enabled;
        if( i >= widget->select_option_count ||
            memcmp(destination, &widget->select_options[i], sizeof(*destination)) != 0 )
            changed = true;
        if( strcmp(options[i].value, selected_value) == 0 )
            selected = i;
    }
    if( widget->selected != selected || strcmp(widget->selected_value, selected_value) != 0 )
        changed = true;
    if( !changed )
    {
        PluginHost_RecordRetainedMutation(
            host, context->index + 1, TORIRS_PLUGIN_MUTATION_PANEL, false, false);
        return TORIRS_RESULT_OK;
    }
    int const first = (int)(widget->select_options - host->panel_select_options);
    int const tail = first + widget->select_option_count;
    int const delta = option_count - widget->select_option_count;
    assert(first >= 0);
    assert(tail <= host->panel_select_option_count);
    memmove(
        &host->panel_select_options[first + option_count],
        &host->panel_select_options[tail],
        (size_t)(host->panel_select_option_count - tail) * sizeof(replacement[0]));
    memcpy(&host->panel_select_options[first], replacement,
        (size_t)option_count * sizeof(replacement[0]));
    host->panel_select_option_count += delta;
    widget->select_option_count = option_count;
    /* Declarations append their slices in widget order, including empty
     * lists. Only following slices move; their widget identities stay put. */
    for( int i = slot + 1; i < host->panel_widget_count; i++ )
        if( host->panel_widgets[i].structured_select )
            host->panel_widgets[i].select_options += delta;
    widget->selected = selected;
    widget->value = selected;
    plugin_copy_str(widget->selected_value, sizeof(widget->selected_value), selected_value);
    plugin_copy_str(widget->text, sizeof(widget->text), selected_value);
    if( changed )
    {
        PluginHost_RecordRetainedMutation(
            host, context->index + 1, TORIRS_PLUGIN_MUTATION_PANEL, true, true);
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
    {
        plugin_asset_budget_refused(context, name);
        return TORIRS_ASSET_BUDGET;
    }
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
        {
            /* Two tables can refuse here and the caller cannot tell them
             * apart, so name the one that actually did. Neither is narration:
             * an image that never arrives is art that never draws. */
            if( free_image < 0 )
            {
                if( !context->image_budget_reported )
                {
                    context->image_budget_reported = true;
                    TORIRS_ERR(
                        "plugin: %s asked for image '%s' but the resident image table "
                        "is full (%d); this plugin's remaining art will not draw\n",
                        context->name,
                        name,
                        TORIRS_PLUGIN_IMAGES_MAX);
                }
            }
            else
                plugin_asset_budget_refused(context, name);
            return TORIRS_ASSET_BUDGET;
        }
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
    struct ToriRS_PluginDef const* def,
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
    case PLUGIN_CALLBACK_SCRIPT_CALLBACK:
        return def->callbacks.on_script_callback != NULL;
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
    case PLUGIN_CALLBACK_DRAW_MINIMAP:
        return def->callbacks.on_draw_minimap != NULL;
    case PLUGIN_CALLBACK_PANEL_BUILD:
        return def->callbacks.on_ui_build != NULL;
    case PLUGIN_CALLBACK_PANEL_ACTION:
        return def->callbacks.on_ui_action != NULL;
    case PLUGIN_CALLBACK_PANEL_DRAW:
        return def->callbacks.on_ui_draw != NULL;
    case PLUGIN_CALLBACK_PANEL_LAYOUT:
        return def->callbacks.on_ui_layout != NULL;
    default:
        return 0;
    }
}

static enum ToriRS_CallbackResult
plugin_v2_event_untimed(
    struct PluginContext* ctx,
    void* event,
    void* userdata)
{
    enum PluginCallbackKind const kind = (enum PluginCallbackKind)((intptr_t)userdata - 1);
    struct PluginV2Instance* v2;
    struct ToriRS_Api* api;
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
    case PLUGIN_CALLBACK_SCRIPT_CALLBACK:
        v2->definition->callbacks.on_script_callback(api,state,event);
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
    case PLUGIN_CALLBACK_DRAW_MINIMAP:
    {
        struct PluginV2DrawScope scope;
        struct ToriRS_Graphics builder;
        plugin_v2_runtime_draw_begin(&v2->runtime,event,&scope,&builder);
        v2->definition->callbacks.on_draw_minimap(api,state,&builder);
        plugin_v2_runtime_draw_end(&scope,&builder);
        break;
    }
    case PLUGIN_CALLBACK_DRAW_WORLD:
    {
        struct PluginV2DrawScope scope;
        struct ToriRS_Graphics builder;
        plugin_v2_runtime_draw_begin(&v2->runtime, event, &scope, &builder);
        v2->definition->callbacks.on_draw_world(api, state, &builder);
        plugin_v2_runtime_draw_end(&scope, &builder);
        break;
    }
    case PLUGIN_CALLBACK_DRAW_CANVAS:
    {
        struct PluginCanvasDispatch const* canvas = event;
        struct PluginV2DrawScope scope;
        struct ToriRS_Graphics builder;
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
        struct ToriRS_Graphics builder;
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

static enum ToriRS_CallbackResult
plugin_v2_event(struct PluginContext* ctx, void* event, void* userdata)
{
    assert(ctx);
    struct PluginTelemetryScope scope;
    enum PluginCallbackKind const kind = (enum PluginCallbackKind)((intptr_t)userdata - 1);
    plugin_telemetry_begin(ctx->host, &scope);
    enum ToriRS_CallbackResult const result = plugin_v2_event_untimed(ctx, event, userdata);
    plugin_telemetry_end(ctx->host, ctx->index, kind, &scope);
    return result;
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
                host->plugins[host->event_order[i]].def->event_priority ||
            (host->plugins[plugin].def->event_priority ==
                 host->plugins[host->event_order[i]].def->event_priority &&
             strcmp(host->plugins[plugin].name, host->plugins[host->event_order[i]].name) < 0) )
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
PluginHost_Register(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginDef const* def)
{
    static uint32_t const KNOWN_FLAGS = TORIRS_PLUGIN_DISABLED_BY_DEFAULT |
                                        TORIRS_PLUGIN_ESSENTIAL | TORIRS_PLUGIN_RUNTIME_HOST |
                                        TORIRS_PLUGIN_HIDDEN;
    struct PluginV2Instance* v2;
    struct ToriRS_FrameOffer const* source_frames = NULL;
    uint32_t flags = 0;
    int event_priority = 0;
    int draw_order = 0;
    int frame_count = 0;
    int schema_count = 0;
    int index;

    assert(host);
    assert(def);
    if( def->struct_size < TORIRS_PLUGIN_DEF_REQUIRED_SIZE ||
        !plugin_v2_id_valid(def->id) || !def->title ||
        !def->title[0] || strlen(def->title) >= TORIRS_PLUGIN_TITLE_MAX || !def->version ||
        !def->version[0] ||
        def->callbacks.struct_size < TORIRS_PLUGIN_CALLBACKS_REQUIRED_SIZE ||
        def->callbacks.struct_size >
            def->struct_size - offsetof(struct ToriRS_PluginDef, callbacks) ||
        def->state_size > 1024u * 1024u )
    {
        TORIRS_ERR("plugin: invalid v2 definition refused\n");
        return -1;
    }
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDef, frames) )
        source_frames = def->frames;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDef, flags) )
        flags = def->flags;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDef, event_priority) )
        event_priority = def->event_priority;
    if( PLUGIN_V2_FIELD_AVAILABLE(def, ToriRS_PluginDef, draw_order) )
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
                !v2->callbacks_storage.on_gameframe ||
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
    {
        struct PluginContext* ctx = &host->plugins[index];
        memset(ctx, 0, sizeof(*ctx));
        ctx->host = host;
        ctx->def = v2->definition;
        ctx->v2 = v2;
        ctx->index = index;
        ctx->enabled = (flags & TORIRS_PLUGIN_DISABLED_BY_DEFAULT) == 0;
        snprintf(ctx->name, sizeof(ctx->name), "%s", def->id);
        plugin_title_refresh(ctx);
        plugin_config_seed(ctx, schema_count);
    }
    plugin_v2_order_insert(host, index);
    host->plugin_count++;
    /* A plugin registering after the settings file was read -- a script the
     * manifest loads late, or one reloaded during a session -- takes over the
     * lines that were being held for its name. It has to happen HERE and not
     * at the next decode: nothing re-reads the file, so those lines would
     * otherwise sit unclaimed while the plugin ran on schema defaults. */
    plugin_orphan_adopt(host, index);
    return index;
}

void
PluginHost_Start(struct ToriRS_PluginHost* host)
{
    assert(host);
    int const previous_owner=host->dispatching, previous_event=host->dispatch_event;

    /* This public call is the boot task's publication fence. Later calls come
     * from normal enable/reload/frame-resolution paths; before the first one,
     * a frame boundary is not permission to start plugins on defaults. */
    host->started_once = true;

    plugin_frame_preference_load(host);
    plugin_frame_resolve(host);

    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct PluginContext* ctx = &host->plugins[i];
        /* `refused` and not `enabled`, so a plugin that stood down on this
         * lane stays down for every later Start -- a script finishing its load
         * runs this again, and without the flag every refusal would be
         * reconsidered, re-taken and re-logged on each one. */
        if( ctx->running || !ctx->enabled || ctx->refused )
            continue;
        if( ctx->lifecycle==UINT64_MAX )
        {
            PluginHost_SetError(host,i,"Plugin lifecycle counter exhausted");
            ctx->refused=true;
            continue;
        }
        ++ctx->lifecycle;
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
    host->dispatching=previous_owner;
    host->dispatch_event=previous_event;
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
    int const previous_owner=host->dispatching, previous_event=host->dispatch_event;

    if( ctx->tearing_down )
        return;
    ctx->tearing_down = true;
    free(ctx->widget_ops);
    ctx->widget_ops=NULL;
    free(ctx->widget_watches);
    ctx->widget_watches = NULL;

    /* Selection is released first: the plugin learns it became invisible
     * while its subscriptions and page model still exist, and no later STOP
     * callback can leave interactive chrome behind. */
    plugin_panel_unregister(host, plugin_index);

    if( ctx->running )
    {
        host->dispatching = plugin_index;
        host->dispatch_event = PLUGIN_CALLBACK_STOP;
        if( ctx->def->callbacks.on_stop )
            (void)plugin_v2_event(ctx, NULL,
                (void*)(intptr_t)(PLUGIN_CALLBACK_STOP + 1));
        host->dispatching = -1;
        host->dispatch_event = -1;
        /* on_stop observes the plugin as live. From this point onward no
         * direct retained-state notifier may target it: v2 shutdown frees
         * state and clears the runtime before the resource cleanup below. */
        ctx->running = false;
        plugin_v2_shutdown(ctx);
    }
    if( host->engine.widget_request )
    {
        struct PluginWidgetRequest request = {.kind=PLUGIN_WIDGET_RESET_OWNER};
        host->engine.widget_request(host->engine.user, (uint64_t)plugin_index + 1, &request);
    }
    /* Geometry and bytes leave with the stopped instance. */
    plugin_objects_destroy_all(host, ctx);
    plugin_meshes_destroy_all(host, ctx);
    plugin_assets_drop_plugin(host, plugin_index);
    plugin_obj_icons_drop_plugin(host, plugin_index);
    plugin_images_drop_plugin(host, plugin_index);
    plugin_models_drop_plugin(host, plugin_index);
    /* Drop its retained entity appearance/action declarations. */
    plugin_entity_drop_plugin(host, plugin_index);
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
    host->dispatching=previous_owner;
    host->dispatch_event=previous_event;
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
    if( !enabled && plugin_policy(ctx, TORIRS_PLUGIN_ESSENTIAL) )
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
    /* During asynchronous boot, config/manifest decode is still assembling
     * the complete enabled set. The boot task's final explicit Start publishes
     * it all at once; only a later user enable starts immediately. */
    if( host->started_once )
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
PluginHost_SetCallbacks(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    struct ToriRS_PluginCallbacks const* callbacks)
{
    struct PluginContext* context;
    struct PluginV2Instance* v2;

    assert(host);
    assert(callbacks);
    assert(callbacks->struct_size == sizeof(*callbacks));
    context = plugin_at(host, plugin_index);
    assert(!context->running);
    v2 = context->v2;
    assert(v2);
    v2->callbacks_storage = *callbacks;
    v2->definition_storage.callbacks = *callbacks;
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

    /* A removed key remains persisted, but no longer belongs to any schema
     * row. Clear old indices even if the replacement schema is refused: an
     * old index may now name another setting or lie beyond its terminator. */
    for( int i = 0; i < ctx->config_count; i++ )
        ctx->config[i].schema_index = -1;

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

bool PluginHost_IsRunning(struct ToriRS_PluginHost const* host, int plugin_index)
{
    return host && plugin_index>=0 && plugin_index<host->plugin_count &&
           host->plugins[plugin_index].running && PluginHost_IsEnabled(host,plugin_index);
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
        &host->plugins[plugin_index], TORIRS_PLUGIN_RUNTIME_HOST);
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
        return plugin_policy(ctx, TORIRS_PLUGIN_HIDDEN) ||
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
               &host->plugins[plugin_index], TORIRS_PLUGIN_ESSENTIAL) ||
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


/* ------------------------------------------------------------------ seams */

void
PluginHost_FrameStart(
    struct ToriRS_PluginHost* host,
    uint64_t now_ms,
    uint64_t drawn_frames)
{
    if( !host )
        return;
    /* Delayed real IO completions stay PENDING until this ordinary frame
     * boundary. Delivery may release/compact the asset table in its callback. */
    for( int i = 0; i < host->asset_count; )
    {
        struct PluginAsset* slot = &host->assets[i];
        if( !slot->delivery_held || host->engine.frame_ms(host->engine.user) < slot->delayed_until_ms )
        {
            ++i;
            continue;
        }
        char plugin[TORIRS_PLUGIN_NAME_MAX];
        char asset[TORIRS_PLUGIN_ASSET_NAME_MAX];
        snprintf(plugin, sizeof plugin, "%s", host->plugins[slot->plugin].def->id);
        snprintf(asset, sizeof asset, "%s", slot->name);
        void* data = slot->delayed_data;
        int const size = slot->delayed_size;
        slot->delayed_data = NULL;
        slot->delayed_size = 0;
        slot->delivery_held = false;
        TORIRS_REPORT("ASSET_DELIVERY_RELEASE plugin=%s asset=%s bytes=%d\n", plugin, asset, size);
        PluginHost_AssetDeliver(host, plugin, asset, data, size);
        i = 0;
    }
    /* The frame boundary is also where per-frame draw budgets reset. */
    for( int i = 0; i < host->plugin_count; i++ )
    {
        host->plugins[i].draw_used = 0;
        host->plugins[i].draw_clipped = false;
    }

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

    if( host->started_once && host->frame_selection_dirty )
        PluginHost_Start(host);

    /* The platform band poll. A provided frame was laid out against one
     * safe rect (ToriRS_GameframeEvent.safe); when the engine's answer leaves
     * it -- the phone's keyboard coming up or going away -- the provider is
     * asked again, once, through the same one-shot request a selection
     * transition uses. Only a provided frame is watched: nothing else was
     * told the old rect. Without a hook the rect is the canvas and only a
     * canvas change (which re-asks on its own) can move it. */
    if( host->engine.platform_safe_rect )
    {
        int const owner = plugin_frame_owner(host);
        if( owner >= 0 && host->plugins[owner].v2 &&
            host->plugins[owner].v2->gameframe_provided && !host->frame_layout_requested )
        {
            int x;
            int y;
            int w;
            int h;
            plugin_safe_rect_read(host, host->canvas_raised_w, host->canvas_raised_h, &x, &y, &w, &h);
            if( x != host->safe_raised_x || y != host->safe_raised_y ||
                w != host->safe_raised_w || h != host->safe_raised_h )
                host->frame_layout_requested = 1;
        }
    }

    if( host->callback_count[PLUGIN_CALLBACK_FRAME_START] == 0 )
        return;

    struct ToriRS_FrameEvent ev = { now_ms, drawn_frames };
    plugin_dispatch(host, PLUGIN_CALLBACK_FRAME_START, &ev);
}

/* The native dispatcher has already re-checked the retained row: node identity,
 * action signature (which includes the registration serial) and native input
 * availability. This only confirms that the owner still holds that exact
 * registration and delivers the operation in a mutating event context. */
bool PluginHost_WidgetOperation(struct ToriRS_PluginHost* host,uint64_t owner,
                                struct ToriRS_WidgetRef widget,uint64_t registration)
{
    assert(host);
    if( !owner || owner>(uint64_t)host->plugin_count || !registration || !widget.opaque[2] ) return false;
    int index=(int)owner-1;
    struct PluginContext* ctx=&host->plugins[index];
    if( !ctx->enabled || !ctx->running || ctx->tearing_down || !ctx->v2 || !ctx->widget_ops ) return false;
    for( int i=0;i<PLUGIN_WIDGET_OP_MAX;++i )
    {
        /* Copied: the listener may replace or remove this registration. */
        struct PluginWidgetOp op=ctx->widget_ops[i];
        if( op.serial!=registration || !ToriRS_WidgetRefEqual(op.widget,widget) ) continue;
        struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_OPERATION,.widget=widget,
            .operation=1,.native_revision=registration,.role=""};
        int previous_owner=host->dispatching,previous_event=host->dispatch_event;
        host->dispatching=index;host->dispatch_event=PLUGIN_CALLBACK_WIDGET_OPERATION;
        struct PluginTelemetryScope scope;
        plugin_telemetry_begin(host, &scope);
        op.listener(&ctx->v2->runtime.api,op.user,&event);
        plugin_telemetry_end(host, index, PLUGIN_CALLBACK_WIDGET_OPERATION, &scope);
        host->dispatching=previous_owner;host->dispatch_event=previous_event;
        return true;
    }
    return false;
}

static struct PluginWidgetWatch*
plugin_widget_watch_current(struct ToriRS_PluginHost* host, int owner, int slot, uint64_t serial)
{
    struct PluginContext* ctx = &host->plugins[owner];
    if( !ctx->enabled || !ctx->running || ctx->tearing_down || !ctx->widget_watches ||
        ctx->widget_watches[slot].serial != serial ) return NULL;
    return &ctx->widget_watches[slot];
}

static void
plugin_widget_watch_call(struct ToriRS_PluginHost* host, int owner, int slot, uint64_t serial,
                         struct ToriRS_WidgetEvent const* event)
{
    struct PluginWidgetWatch* watch = plugin_widget_watch_current(host, owner, slot, serial);
    if( !watch ) return;
    int previous_owner = host->dispatching, previous_event = host->dispatch_event;
    host->dispatching = owner;
    host->dispatch_event = PLUGIN_CALLBACK_WIDGET_BINDING;
    struct PluginTelemetryScope scope;
    plugin_telemetry_begin(host, &scope);
    watch->listener(&host->plugins[owner].v2->runtime.api, watch->user, event);
    plugin_telemetry_end(host, owner, PLUGIN_CALLBACK_WIDGET_BINDING, &scope);
    host->dispatching = previous_owner;
    host->dispatch_event = previous_event;
}

void
PluginHost_WidgetBindingsInvalidate(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->widget_watch_pending = true;
    for( int owner = 0; owner < host->plugin_count; owner++ )
    {
        struct PluginContext* ctx = &host->plugins[owner];
        if( !ctx->widget_watches )
            continue;
        for( int i = 0; i < PLUGIN_WIDGET_WATCH_MAX; i++ )
            if( ctx->widget_watches[i].serial &&
                strcmp(ctx->widget_watches[i].role, "@tree") == 0 )
                ctx->widget_watches[i].tree_notified = false;
    }
}

void
PluginHost_WidgetsChanged(struct ToriRS_PluginHost* host, uint64_t instance, uint64_t generation)
{
    if( !host ) return;
    if( host->widget_watch_dispatching ) { host->widget_watch_pending = true; return; }
    if( !host->widget_watch_pending && host->widget_tree_instance == instance &&
        host->widget_tree_generation == generation ) return;
    host->widget_tree_instance = instance;
    host->widget_tree_generation = generation;
    host->widget_watch_pending = false;
    host->widget_watch_dispatching = true;
    struct WatchDispatch { int owner, slot; uint64_t serial; };
    struct WatchDispatch snapshot[TORIRS_PLUGIN_MAX * PLUGIN_WIDGET_WATCH_MAX];
    int count = 0;
    for( int order = 0; order < host->plugin_count; ++order )
    {
        int owner = host->event_order[order];
        struct PluginContext* ctx = &host->plugins[owner];
        if( !ctx->running || !ctx->enabled || !ctx->widget_watches ) continue;
        int const first = count;
        for( int slot = 0; slot < PLUGIN_WIDGET_WATCH_MAX; ++slot )
            if( ctx->widget_watches[slot].serial )
            {
                struct WatchDispatch item = {owner, slot, ctx->widget_watches[slot].serial};
                int at = count++;
                while( at > first && snapshot[at - 1].serial > item.serial )
                { snapshot[at] = snapshot[at - 1]; --at; }
                snapshot[at] = item;
            }
    }
    for( int i = 0; i < count; ++i )
    {
        struct WatchDispatch item = snapshot[i];
        struct PluginWidgetWatch* watch = plugin_widget_watch_current(host, item.owner, item.slot, item.serial);
        if( !watch ) continue;
        /* All data used after a callback is copied or revalidated: the callback
         * can replace this watch or disable/reload either participating plugin. */
        char role[TORIRS_UI_NAME_MAX];
        snprintf(role, sizeof(role), "%s", watch->role);
        if( strcmp(role,"@tree")==0 )
        {
            if( watch->tree_notified && watch->tree_instance==instance &&
                watch->tree_generation==generation ) continue;
            watch->tree_notified=true;
            watch->tree_instance=instance;watch->tree_generation=generation;
            struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_TREE_CHANGED,
                .native_revision=generation,.role=""};
            plugin_widget_watch_call(host,item.owner,item.slot,item.serial,&event);
            continue;
        }
        struct ToriRS_WidgetRef previous = watch->current, current = {0};
        if( instance && host->engine.widget_request )
        {
            struct PluginWidgetRequest request = {.kind=PLUGIN_WIDGET_FIND, .name=role, .refs=&current};
            enum ToriRS_ContractResult result = host->engine.widget_request(
                host->engine.user, (uint64_t)item.owner + 1, &request);
            if( result != TORIRS_CONTRACT_OK && result != TORIRS_CONTRACT_UNAVAILABLE ) continue;
        }
        if( memcmp(&previous, &current, sizeof(current)) == 0 ) continue;
        watch->current = current;
        struct ToriRS_WidgetEvent event = {.native_revision=generation, .role=role};
        if( previous.opaque[2] )
        {
            event.type = TORIRS_WIDGET_UNBOUND; event.widget = previous;
            plugin_widget_watch_call(host, item.owner, item.slot, item.serial, &event);
        }
        if( current.opaque[2] )
        {
            event.type = TORIRS_WIDGET_BOUND; event.widget = current;
            plugin_widget_watch_call(host, item.owner, item.slot, item.serial, &event);
        }
    }
    host->widget_watch_dispatching = false;
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

void PluginHost_ScriptCallback(struct ToriRS_PluginHost* host,char const* name,int script_id,
    struct PluginScriptStack const* stack)
{
    static _Atomic uint64_t serial;
    if( !host || !stack || !name || host->script_stack ||
        !host->callback_count[PLUGIN_CALLBACK_SCRIPT_CALLBACK] ) return;
    uint64_t previous=atomic_load(&serial);
    do { if( previous==UINT64_MAX ) return; }
    while( !atomic_compare_exchange_weak(&serial,&previous,previous+1) );
    host->script_ref.token=previous+1;
    host->script_stack=stack;
    struct ToriRS_ScriptEvent event={.name=name,.script_id=script_id,.ref=host->script_ref,.widget=stack->widget};
    plugin_dispatch(host,PLUGIN_CALLBACK_SCRIPT_CALLBACK,&event);
    host->script_stack=NULL;
    host->script_ref.token=0;
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

void PluginHost_DrawMinimap(struct ToriRS_PluginHost* host)
{
    assert(host);
    if( !host->callback_count[PLUGIN_CALLBACK_DRAW_MINIMAP] ) return;
    void* previous_surface=host->draw_surface;
    int const previous_canvas=host->draw_canvas;
    host->draw_surface=host;
    host->draw_canvas=PLUGIN_DRAW_SURFACE_MINIMAP;
    host->engine.draw_select_canvas(host->engine.user,PLUGIN_DRAW_SURFACE_MINIMAP);
    plugin_dispatch(host,PLUGIN_CALLBACK_DRAW_MINIMAP,host->draw_surface);
    host->draw_surface=previous_surface;
    host->draw_canvas=previous_canvas;
    host->engine.draw_select_canvas(host->engine.user,previous_canvas);
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
    if( host->callback_count[PLUGIN_CALLBACK_DRAW_CANVAS] > 0 )
        plugin_dispatch(host, PLUGIN_CALLBACK_DRAW_CANVAS, &canvas);
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
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

bool
PluginHost_FrameNeedsLayout(struct ToriRS_PluginHost const* host)
{
    return host && host->frame_layout_requested;
}

void
PluginHost_Layout(
    struct ToriRS_PluginHost* host,
    int width,
    int height)
{
    struct PluginFrameCatalogEntry const* entry;
    struct ToriRS_FrameOffer const* v2_offer;
    struct PluginV2Instance* v2;
    struct ToriRS_GameframeEvent gameframe;
    enum ToriRS_FrameBuildResult v2_result;
    char v2_reason[TORIRS_FRAME_REASON_MAX] = { 0 };
    int build_entry;
    int transitioning;
    int owner;
    int previous_dispatching;
    int previous_event;
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
            "plugin: %s asked to lay out against a %dx%d canvas; not asked to provide\n",
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
        width = entry->width;
        height = entry->height;
    }

    /* Consume only the attempt we are about to make -- a transition to the
     * target, or a re-ask of the standing frame (the platform band moved). A
     * callback that changes selection or invalidates again raises a fresh
     * request and the epoch fence below preserves it. */
    host->frame_layout_requested = 0;
    selection_epoch = host->frame_selection_epoch;

    /* Provided, not declared: the offer's on_gameframe edits widgets and
     * answers. The engine only takes the chrome and binds the roles
     * (frame_provide); the provider's retained widget edits are the layout. */
    v2 = host->plugins[owner].v2;
    assert(v2->definition->callbacks.on_gameframe);
    memset(&gameframe, 0, sizeof(gameframe));
    gameframe.offer_id = v2_offer->id;
    gameframe.active = true;
    gameframe.canvas = entry->canvas == TORIRS_FRAME_CANVAS_FIXED ? TORIRS_FRAME_CANVAS_FIXED
                                                                   : TORIRS_FRAME_CANVAS_WINDOW;
    gameframe.width = width;
    gameframe.height = height;
    (void)v2->runtime.api.core.lane(&v2->runtime.api, &gameframe.lane);
    gameframe.reason = v2_reason;
    gameframe.reason_capacity = sizeof(v2_reason);
    plugin_safe_rect_read(
        host,
        width,
        height,
        &gameframe.safe.x,
        &gameframe.safe.y,
        &gameframe.safe.width,
        &gameframe.safe.height);
    /* What the provider is being laid out against; the frame boundary
     * re-asks when the engine's band leaves it. Recorded before the call so
     * a band that moves DURING the callback is still seen as a move. */
    host->safe_raised_x = gameframe.safe.x;
    host->safe_raised_y = gameframe.safe.y;
    host->safe_raised_w = gameframe.safe.width;
    host->safe_raised_h = gameframe.safe.height;
    host->canvas_raised_w = width;
    host->canvas_raised_h = height;
    previous_dispatching = host->dispatching;
    previous_event = host->dispatch_event;
    host->dispatching = owner;
    host->dispatch_event = PLUGIN_CALLBACK_LAYOUT;
    struct PluginTelemetryScope scope;
    plugin_telemetry_begin(host, &scope);
    v2_result = v2->definition->callbacks.on_gameframe(&v2->runtime.api, v2->state, &gameframe);
    plugin_telemetry_end(host, owner, PLUGIN_CALLBACK_GAMEFRAME, &scope);
    host->dispatching = previous_dispatching;
    host->dispatch_event = previous_event;
    if( v2_result < TORIRS_FRAME_READY || v2_result > TORIRS_FRAME_ERROR )
        v2_result = TORIRS_FRAME_ERROR;

    if( host->frame_selection_epoch != selection_epoch ||
        (transitioning && host->frame_target_entry != build_entry) ||
        (!transitioning && host->frame_active_entry != build_entry) )
        return;

    if( v2_result != TORIRS_FRAME_READY )
    {
        int const status = v2_result == TORIRS_FRAME_PENDING ? TORIRS_FRAME_STATUS_LOADING
                                                             : TORIRS_FRAME_STATUS_FALLBACK;
        char const* reason =
            v2_reason[0]                            ? v2_reason
            : v2_result == TORIRS_FRAME_PENDING     ? "The selected gameframe is still loading."
            : v2_result == TORIRS_FRAME_UNSUPPORTED ? "The selected gameframe is unsupported here."
                                                    : "The selected gameframe could not be built.";

        plugin_frame_discard_stale_binding(host, build_entry);
        /* A provided frame that stops answering READY is released: its
         * retained edits are the layout, and a provider that says it cannot
         * lay this lane out must not keep the chrome suppressed under it. */
        if( host->frame_active_entry == build_entry )
            plugin_frame_engine_activate(host, -1);
        /* PENDING from a provider means "ask again": its roles have not bound
         * yet, or its art is still crossing the IO queue. Keep the request
         * standing so the next layout fence asks; a provider that never
         * becomes READY costs one refused find per tick and nothing else. */
        if( v2_result == TORIRS_FRAME_PENDING )
            host->frame_layout_requested = 1;
        plugin_frame_selection_active(host, plugin_frame_committed_id(host), status, reason);
        return;
    }

    /* The only publication point. */
    {
        int const old_owner = plugin_frame_owner(host);

        plugin_frame_engine_activate(host, build_entry);
        v2->gameframe_provided = true;
        /* The provider's widget-edit owner id, the same `index + 1` its own
         * set_position requests carry (widget_request). */
        host->engine.frame_provide(host->engine.user, (uint64_t)owner + 1);
        host->frame_bound_root = host->engine.frame_root ? host->engine.frame_root(host->engine.user) : -1;
        plugin_frame_selection_active(host, entry->id, TORIRS_FRAME_STATUS_ACTIVE, "");
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
    {
        /*
         * Nothing registered answers to this name, which is NOT the same as
         * "this line is junk". A script that would not compile, one the
         * manifest is not listing today, a C plugin another build carries --
         * every one of those is a plugin whose settings the user still owns.
         * The line is kept verbatim so the rewrite this file gets on every
         * save puts it back rather than deleting it.
         */
        plugin_orphan_store(host, plugin_name, key, value);
        return;
    }

    struct PluginContext* ctx = &host->plugins[index];

    /* `enabled` is host state, not plugin config: it is what the settings
     * panel toggles and what a crashed script gets cleared by. */
    if( strcmp(key, "enabled") == 0 )
    {
        /* Same refusal as PluginHost_SetEnabled, and it has to be here too:
         * this path writes the field directly, so an `enabled=0` left over
         * from a build where the plugin was ordinary would switch it off
         * behind both the panel and the host. */
        if( !plugin_policy(ctx, TORIRS_PLUGIN_ESSENTIAL) )
            ctx->enabled = atoi(value) != 0;
        return;
    }

    struct PluginConfigSlot* slot = plugin_config_slot(ctx, key, true);
    if( !slot )
        return;
    if( strcmp(slot->value, value) != 0 )
        slot->number_warned = false;
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
    /* Carried-through sections cost a header apiece in the worst case, which
     * is every one of them belonging to a different absent plugin. */
    cap += (size_t)host->orphan_config_count *
           (96 + TORIRS_PLUGIN_NAME_MAX + 64 + TORIRS_PLUGIN_CONFIG_VALUE_MAX + 4);

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
            ctx->enabled == plugin_policy(ctx, TORIRS_PLUGIN_DISABLED_BY_DEFAULT) )
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

    /*
     * Sections nothing registered claims, written back exactly as they were
     * read.
     *
     * Without this the file the user gets back is the file this PROCESS could
     * account for, and a plugin that failed to compile, or that this build
     * does not carry, has its settings deleted by the first save anything else
     * makes. @see plugin_orphan_store.
     *
     * Grouped by name in one pass over a table small enough that the quadratic
     * walk is cheaper than sorting a copy of it.
     */
    for( int i = 0; i < host->orphan_config_count; i++ )
    {
        struct PluginOrphanConfig const* first = &host->orphan_config[i];
        bool earlier = false;

        for( int j = 0; j < i; j++ )
        {
            if( strcmp(host->orphan_config[j].plugin, first->plugin) == 0 )
            {
                earlier = true;
                break;
            }
        }
        if( earlier )
            continue;

        at += (size_t)snprintf(buf + at, cap - at, "\n[plugin:%s]\n", first->plugin);
        for( int j = i; j < host->orphan_config_count; j++ )
        {
            struct PluginOrphanConfig const* row = &host->orphan_config[j];
            if( strcmp(row->plugin, first->plugin) != 0 )
                continue;
            at += (size_t)snprintf(buf + at, cap - at, "%s=%s\n", row->key, row->value);
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
    return PluginHost_PanelDispatchRegion(
        host, selection_generation, widget_serial, intent_sequence, widget_id,
        action, value, text, x, y, 0, 0);
}

int
PluginHost_PanelDispatchRegion(
    struct ToriRS_PluginHost* host,
    uint32_t selection_generation,
    uint32_t widget_serial,
    uint64_t intent_sequence,
    char const* widget_id,
    int action,
    int value,
    char const* text,
    int x,
    int y,
    int region_width,
    int region_height)
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
    if( region_width < 0 || region_height < 0 ||
        (region_width == 0) != (region_height == 0) ||
        (region_width > 0 && widget->kind != TORIRS_PANEL_WIDGET_CUSTOM) )
        return 0;
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
    ev.region_width = region_width;
    ev.region_height = region_height;
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

/* The slot order is host-private. Consumers match these stable callback names. */
static char const* const plugin_callback_names[] = {
    "on_start", "on_stop", "on_frame_start", "on_logic_tick", "on_server_tick",
    "on_world_loaded", "on_script_callback", "on_npc_spawn", "on_npc_retype", "on_npc_despawn",
    "on_key", "on_menu_build", "on_menu_select", "on_draw_world", "on_draw_canvas",
    "on_config_changed", "on_item_spawn", "on_item_changed", "on_item_despawn",
    "on_asset", "on_chat_message", "on_game_event", "legacy_ui", "legacy_ui_build",
    "legacy_layout", "on_screen_changed", "on_ui_build", "on_ui_action",
    "on_ui_layout", "on_ui_draw", "widget_binding", "widget_operation", "on_gameframe", "on_draw_minimap"
};
_Static_assert(sizeof(plugin_callback_names)/sizeof(plugin_callback_names[0]) == PLUGIN_CALLBACK_COUNT,
    "Every host callback phase must have a telemetry name");

void PluginHost_TelemetryStart(struct ToriRS_PluginHost* host,
    uint64_t (*clock_ns)(void* user), void* clock_user)
{
    assert(host);
    assert(clock_ns);
    assert(host->dispatching < 0);
    if( !host->telemetry )
    {
        host->telemetry = calloc(1, sizeof(*host->telemetry));
        assert(host->telemetry);
    }
    host->telemetry->clock_ns = clock_ns;
    host->telemetry->clock_user = clock_user;
    PluginHost_TelemetryReset(host);
}

void PluginHost_TelemetryReset(struct ToriRS_PluginHost* host)
{
    assert(host);
    assert(host->dispatching < 0);
    if( !host->telemetry ) return;
    assert(!host->telemetry->active);
    memset(host->telemetry->callbacks, 0, sizeof(host->telemetry->callbacks));
    memset(host->telemetry->mutations, 0, sizeof(host->telemetry->mutations));
}

int PluginHost_TelemetryCallbackCount(void) { return PLUGIN_CALLBACK_COUNT; }

void PluginHost_TelemetryReadCallback(struct ToriRS_PluginHost const* host,
    int plugin_index, int callback_index, struct ToriRS_PluginCallbackTelemetry* out)
{
    assert(host);
    assert(out);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    assert(callback_index >= 0);
    assert(callback_index < PLUGIN_CALLBACK_COUNT);
    memset(out, 0, sizeof(*out));
    if( host->telemetry ) *out = host->telemetry->callbacks[plugin_index][callback_index];
    out->callback = plugin_callback_names[callback_index];
    struct PluginContext const* ctx = &host->plugins[plugin_index];
    if( callback_index == PLUGIN_CALLBACK_GAMEFRAME )
        out->subscribed = ctx->def->callbacks.on_gameframe != NULL;
    else if( callback_index == PLUGIN_CALLBACK_WIDGET_BINDING && ctx->widget_watches )
    {
        for( int i = 0; i < PLUGIN_WIDGET_WATCH_MAX; ++i )
            out->subscribed |= ctx->widget_watches[i].serial != 0;
    }
    else if( callback_index == PLUGIN_CALLBACK_WIDGET_OPERATION && ctx->widget_ops )
    {
        for( int i = 0; i < PLUGIN_WIDGET_OP_MAX; ++i )
            out->subscribed |= ctx->widget_ops[i].serial != 0;
    }
    else
        out->subscribed = plugin_v2_has_event_callback(ctx->def, (enum PluginCallbackKind)callback_index);
}

void PluginHost_TelemetryReadMutation(struct ToriRS_PluginHost const* host,
    int plugin_index, enum ToriRS_PluginMutationCategory category,
    struct ToriRS_PluginMutationTelemetry* out)
{
    assert(host);
    assert(out);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    assert(category >= 0);
    assert(category < TORIRS_PLUGIN_MUTATION_COUNT);
    memset(out, 0, sizeof(*out));
    if( host->telemetry ) *out = host->telemetry->mutations[plugin_index][category];
}

void PluginHost_RecordRetainedMutation(struct ToriRS_PluginHost* host,
    uint64_t owner, enum ToriRS_PluginMutationCategory category,
    bool changed, bool redraw_requested)
{
    assert(host);
    assert(owner > 0);
    assert(owner <= (uint64_t)host->plugin_count);
    assert(category >= 0);
    assert(category < TORIRS_PLUGIN_MUTATION_COUNT);
    if( !host->telemetry ) return;
    struct ToriRS_PluginMutationTelemetry* row = &host->telemetry->mutations[owner - 1][category];
    row->attempts++;
    row->changes += changed ? 1 : 0;
    row->redraw_requests += redraw_requested ? 1 : 0;
}

void PluginHost_RecordCurrentRetainedMutation(struct ToriRS_PluginHost* host,
    enum ToriRS_PluginMutationCategory category, bool changed, bool redraw_requested)
{
    assert(host);
    if( !host->telemetry ) return;
    assert(host->dispatching >= 0);
    PluginHost_RecordRetainedMutation(host, (uint64_t)host->dispatching + 1,
        category, changed, redraw_requested);
}
