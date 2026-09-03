#include "plugin/torirs_plugin_host.h"

#include "revconfig/revconfig.h"
#include "ui/uitree_minimenu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "log/torirs_log.h"

/*
 * Plugin action ids start well clear of the four the editors already own
 * (RS_MINIMENU_ACTION_* are CLIENT_BASE + 0..3). Being at or above
 * CLIENT_BASE is what exempts an id from SortPriorityActions' +/-2000 bias;
 * being above 1000 is what keeps a plugin row out of the left-click default
 * (RS_Minimenu_DefaultOptionIndex takes the first action < 1000). Both matter:
 * a plugin must be able to add a row without silently stealing left-click.
 */
#define PLUGIN_MENU_ACTION_BASE (UITREE_MINIMENU_ACTION_CLIENT_BASE + 16)

static void plugin_panel_bump(uint32_t* revision);

struct PluginSub
{
    ToriRS_PluginHandler handler;
    void* userdata;
    /* Owning plugin, so a disable can drop its subscriptions in place. */
    int plugin;
};

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

struct ToriRS_PluginCtx
{
    struct ToriRS_PluginHost* host;
    struct ToriRS_PluginDef const* def;
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
     * ToriRS_PluginApi::disable_self.
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
    /* A read is in flight. The slot is claimed before the IO starts so a
     * second asset_load of the same name joins the first rather than queuing
     * a duplicate read. */
    bool pending;
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

#define TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX 64

struct PluginRoleReplacement
{
    int plugin;
    char role[TORIRS_PLUGIN_ROLE_NAME_MAX];
};

/*
 * Chrome: the second tier of the frame. @see ToriRS_PluginApi::chrome_claim.
 *
 * 64, matching the role replacement table beside it, because the two bound the
 * same kind of thing -- a name a plugin has taken exclusive responsibility for
 * -- and a client with more than a handful of either is doing something no
 * gameframe in this tree does.
 */
#define TORIRS_PLUGIN_CHROME_CLAIMS_MAX 64
/** Bytes of one hit-region verb on a chrome part, terminator included. */
#define TORIRS_PLUGIN_CHROME_OP_MAX 32
/**
 * Arranger-declared parts standing at once.
 *
 * A frame declares one per CONTROL it dresses, which on the widest gameframe
 * in this tree is four chat buttons and fourteen sidebar stones. 32 is that
 * with room, and it is a per-declaration table rather than a per-plugin one
 * because only the frame's owner may write it.
 */
#define TORIRS_PLUGIN_SLOT_ART_MAX 32
/** A 64x64 PNG has no reason to approach this; cap pathological compressed
 * input before the automatic rail-icon decode spends work on it. */
#define TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX (256 * 1024)

/** One part a plugin has taken exclusive responsibility for dressing. */
struct PluginChromeClaim
{
    /** -1 for a free row. */
    int plugin;
    char part[TORIRS_PLUGIN_ROLE_NAME_MAX];
    /** Mask of enum ToriRS_PluginChromeScope this row holds on `part`. */
    int scopes;
    /** A POSITION declaration changed since the last layout pass, so one is
     *  owed. @see PluginHost_ChromeTick. */
    uint8_t moved;
    /**
     * ADDED parts only: the role this part hangs off, and the reason its box
     * is stated relative rather than absolute.
     */
    char anchor[TORIRS_PLUGIN_ROLE_NAME_MAX];
    /** Which side of `anchor` an added part paints on. @see chrome_add. */
    int place;
    uint8_t added;

    /**
     * A LANE part this claim has taken a role replacement on.
     *
     * Kept because the routing has to be RECONCILED and not merely taken: a
     * part that was the cache's own becomes a plugin arranger's the moment a
     * gameframe plugin claims the frame, and a replacement left standing over
     * it would prune a node the arranger is now placing.
     */
    uint8_t lane_replaced;

    /** An ENTITY part: `part` parses as `<kind>:<ids>`. @see the entities
     *  section of the contract. */
    uint8_t entity;
    /** The scene element the entity part names this frame, or -1. Rebound
     *  once per world frame by plugin_entity_resolve_all. */
    int element_id;
    /** An APPEARANCE holder's standing look for an entity. */
    struct ToriRS_PluginEntityLook look;
    /** enum ToriRS_PluginEntityOpsMode, for a HITBOX holder's entity. */
    uint8_t ops_mode;

    /** chrome_paint was called for this part in the last EV_CHROME. */
    uint8_t declared;
    /** The declaration is stale: re-raise EV_CHROME for this plugin. Set by a
     *  fresh claim, a layout change, and a borrowed image becoming resident.
     *  @see plugin_chrome_tick. */
    uint8_t needs_declare;
    /** enum ToriRS_PluginChromeState, as chrome_state last selected it. */
    uint8_t state;

    /** What chrome_paint said. Anchor-relative for an added part, canvas
     *  coordinates for every other kind. */
    struct ToriRS_PluginChromePart art;

    char ops[TORIRS_PLUGIN_REGION_OPS_MAX][TORIRS_PLUGIN_CHROME_OP_MAX];
    int op_count;
    uint32_t tag;
    uint8_t has_ops;
};

/** One part the frame's OWNER declared, against the member it just placed. */
struct PluginSlotArt
{
    uint8_t used;
    uint8_t slot;
    int member;
    /** enum ToriRS_PluginChromeState, as layout_slot_state last selected it. */
    uint8_t state;
    struct ToriRS_PluginChromePart art;
};

/**
 * One plugin's permission to draw another's image.
 *
 * The handle IS the lender's slot index -- there is no translation layer --
 * because a second numbering would have to be kept in step with a table that
 * deliberately never compacts. What this row grants is the RIGHT to pass that
 * handle to the read verbs, and nothing else: release and compose still go
 * through plugin_image_owned and still refuse.
 *
 * `generation` is what makes a borrow safe across the lender dropping the
 * image. The slot is recycled by the next image_load, so a row that only
 * remembered the index would silently start drawing a different picture.
 */
struct PluginChromeBorrow
{
    /** -1 for a free row. */
    int borrower;
    int lender_image;
    uint32_t generation;
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
    /** Over the scene, under the interfaces. @see EV_DRAW_FRAME. */
    PLUGIN_DRAW_SURFACE_FRAME = 2,
    /** Panel-local custom region prepared by the application shell. */
    PLUGIN_DRAW_SURFACE_PANEL = TORIRS_PLUGIN_ENGINE_DRAW_PANEL
};

struct ToriRS_PluginHost
{
    struct ToriRS_PluginEngine engine;
    struct ToriRS_PluginApi api;

    struct ToriRS_PluginCtx plugins[TORIRS_PLUGIN_MAX];
    int plugin_count;

    struct PluginSub subs[TORIRS_PLUGIN_EV_COUNT][TORIRS_PLUGIN_SUBS_MAX];
    int sub_count[TORIRS_PLUGIN_EV_COUNT];

    /* Menu routes live for one build. The hover pass rebuilds the menu every
     * frame, so they are reset per build rather than accumulated. */
    struct PluginMenuRoute routes[TORIRS_PLUGIN_MENU_ROUTES_MAX];
    int route_count;
    int next_action;

    /* The plugin currently being dispatched, so the api knows whose ctx it is
     * serving without every call carrying it separately. */
    int dispatching;
    /** enum ToriRS_PluginEvent for that callback, or -1 outside one. Needed by
     *  panel_request's EV_START-only registration rule. */
    int dispatch_event;
    /** engine.screen's answer at the last frame boundary, so the boundary can
     *  tell a change from a steady state. @see TORIRS_PLUGIN_EV_SCREEN_CHANGE. */
    int last_screen;
    /* Non-NULL only between the open and close of a draw window. */
    void* draw_surface;
    /* Which surface that is -- enum PluginDrawSurface. Read by the two
     * world-only draw verbs, which have nothing to mean on screen/panel. */
    int draw_canvas;

    /*
     * The gameframe claim.
     *
     * -1 when the lane's own chrome is in charge, which is the state every
     * session starts in and returns to. One index and not a stack: a frame
     * being arranged by two plugins is not a state worth being able to
     * represent, so a second claimant is refused rather than queued.
     */
    int layout_owner;
    /** enum ToriRS_PluginLayoutCanvas, and the pinned size for FIXED. */
    int layout_canvas;
    int layout_fixed_w;
    int layout_fixed_h;
    /** Advances across every claim/release transaction, even when the same
     *  owner immediately reacquires. Fences an in-flight EV_LAYOUT scratch
     *  declaration from committing across that ownership transition. */
    uint32_t layout_claim_epoch;
    /** Non-zero only inside an EV_LAYOUT dispatch: layout_slot is legal then
     *  and at no other time, for the same reason hit_region is. */
    int layout_declaring;

    /** Exclusive, persistent semantic replacements. The role spelling is
     * retained rather than a node/id: only the engine can re-resolve it after
     * a CS2 subtree rebuild, and reconciliation happens before interaction. */
    struct PluginRoleReplacement
        role_replacements[TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX];

    /*
     * Reservations: bites taken out of a derived region, one per
     * (plugin, region, edge).
     *
     * A flat table rather than a per-plugin array for the reason the window's
     * widget pool is flat: almost every plugin reserves nothing, one or two
     * reserve a single edge, and sizing a slice for the greediest and
     * multiplying it by thirty-two spends memory on a case that does not
     * happen. Declaration ORDER is meaningful -- two plugins on the same edge
     * stack in the order they asked -- so nothing here is compacted or sorted.
     */
    struct PluginReserve
    {
        /** -1 for a free row. */
        int plugin;
        uint8_t slot;
        uint8_t edge;
        int px;
    } reserves[TORIRS_PLUGIN_RESERVES_MAX];

    /*
     * The chrome tier: parts, and who dresses them.
     *
     * Flat tables and not per-plugin slices, on the reservation table's own
     * reasoning: almost every plugin claims nothing, one or two claim a
     * handful, and sizing a slice for the greediest and multiplying it by
     * thirty-two spends memory on a case that does not happen. Declaration
     * ORDER is meaningful here too -- claimants are dispatched and drawn in
     * the order they claimed -- so nothing is compacted or sorted.
     */
    struct PluginChromeClaim chrome_claims[TORIRS_PLUGIN_CHROME_CLAIMS_MAX];
    /** The frame owner's part declarations, rebuilt whole each EV_LAYOUT. */
    struct PluginSlotArt slot_art[TORIRS_PLUGIN_SLOT_ART_MAX];
    /** Read permissions on other plugins' images. @see PluginChromeBorrow. */
    struct PluginChromeBorrow
        chrome_borrows[TORIRS_PLUGIN_CHROME_CLAIMS_MAX * TORIRS_PLUGIN_CHROME_STATE_COUNT];
    /** Non-zero only inside an EV_CHROME dispatch: chrome_paint and chrome_ops
     *  are legal then and at no other time, for the same reason layout_slot is
     *  legal only inside EV_LAYOUT. */
    int chrome_declaring;
    /** The plugin whose declaration is being taken, while chrome_declaring. */
    int chrome_declarer;
    /** Set while the claim table is being walked, so a claim taken from inside
     *  a handler is refused rather than invalidating the walk. */
    int chrome_iterating;

    /** Moves whenever anything about the layout does. @see layout_revision. */
    int layout_revision;
    /** Set while EV_LAYOUT_CHANGED is being delivered. @see
     *  PluginHost_LayoutChanged. */
    int layout_notifying;
    /* Non-NULL only during an EV_MENU_BUILD dispatch. */
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
        /**
         * Bumped every time this slot is dropped, so a BORROW taken against
         * the old occupant can tell it is stale.
         *
         * The table deliberately never compacts and the slot index IS the
         * handle, so a recycled slot is the one way a borrower could end up
         * drawing a picture it never asked for. Comparing one counter closes
         * it. @see PluginChromeBorrow.
         */
        uint32_t generation;
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
        /** enum ToriRS_PluginObjIconStyle. */
        int style;
        /** Which plugin's image slot holds the pixels. An icon is per-plugin
         *  in the image table even though the picture is not, because
         *  draw_image resolves a slot against the plugin that owns it. */
        int plugin;
        /** The `images` slot, which is also the handle handed out. */
        int image;
        /**
         * The value of `icon_clock` when this was last asked for. A COUNTER
         * and not frame_ms: two icons fetched in the same millisecond by one
         * page build must still order against each other, or eviction picks
         * between them arbitrarily and can drop the one being drawn.
         */
        uint64_t used;
    } obj_icons[TORIRS_PLUGIN_OBJ_ICONS_MAX];
    uint64_t icon_clock;

    /*
     * The shared plugin window.
     *
     * One flat pool of controls, each stamped with its owning plugin, rather
     * than a fixed slice per plugin: a pool of 256 lets one plugin with a rich
     * settings tab spend what fifteen quiet ones never will, while
     * TORIRS_PLUGIN_WIDGETS_MAX still stops any single plugin from taking the
     * lot. A per-plugin array would have to be sized for the greediest and
     * multiplied by thirty-two.
     */
    struct ToriRS_PluginWinWidget win_widgets[TORIRS_PLUGIN_WIN_WIDGETS_MAX];
    /** Owning plugin index per slot, or -1 for a free one. */
    int win_owner[TORIRS_PLUGIN_WIN_WIDGETS_MAX];
    int win_widget_count;
    /** Per plugin: has a tab, and what it is called. */
    bool win_tab[TORIRS_PLUGIN_MAX];
    char win_tab_title[TORIRS_PLUGIN_MAX][64];
    int win_revision;
    /** Set while an EV_UI_BUILD dispatch is in flight, so the win_* verbs can
     *  tell a declaration from a mutation. */
    bool win_building;

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
    char panel_badge[TORIRS_PLUGIN_MAX][TORIRS_PLUGIN_PANEL_BADGE_MAX];
    bool panel_attention[TORIRS_PLUGIN_MAX];
    int panel_active;
    /**
     * Which FACE the active selection is showing.
     * @see enum ToriRS_PluginPanelView.
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
    struct ToriRS_PluginWinWidget panel_widgets[TORIRS_PLUGIN_WIDGETS_MAX];
    bool panel_invalidated[TORIRS_PLUGIN_WIDGETS_MAX];
    int panel_widget_count;
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

/* ---------------------------------------------------------------- helpers */

static struct ToriRS_PluginCtx*
plugin_at(struct ToriRS_PluginHost* host, int index)
{
    assert(host);
    assert(index >= 0);
    assert(index < host->plugin_count);
    return &host->plugins[index];
}

static int
plugin_schema_index(struct ToriRS_PluginCtx const* ctx, char const* key)
{
    assert(ctx);
    assert(key);

    if( !ctx->def->config )
        return -1;
    for( int i = 0; ctx->def->config[i].key; i++ )
    {
        if( strcmp(ctx->def->config[i].key, key) == 0 )
            return i;
    }
    return -1;
}

static struct PluginConfigSlot*
plugin_config_slot(struct ToriRS_PluginCtx* ctx, char const* key, bool create)
{
    assert(ctx);
    assert(key);

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
        TORIRS_LOG("plugin: '%s' config store is full (%d); '%s' is not kept\n",
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
plugin_copy_str(char* dst, size_t cap, char const* src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/** Whether copying `src` with plugin_copy_str would change this fixed field.
 *  Comparing the unbounded source directly makes an already-truncated value
 *  look new forever, which turns one long live label into per-frame deltas. */
static bool
plugin_copy_str_would_change(char const* dst, size_t cap, char const* src)
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
plugin_config_seed(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);

    ctx->schema_count = 0;
    if( !ctx->def->config )
        return;
    for( int i = 0; ctx->def->config[i].key; i++ )
    {
        struct ToriRS_PluginConfigItem const* item = &ctx->def->config[i];
        struct PluginConfigSlot* slot = plugin_config_slot(ctx, item->key, true);
        ctx->schema_count++;
        if( !slot )
            continue;
        snprintf(
            slot->value,
            sizeof(slot->value),
            "%s",
            item->default_value ? item->default_value : "");
    }
}

static void
plugin_drop_subs(struct ToriRS_PluginHost* host, int plugin_index)
{
    assert(host);

    for( int ev = 0; ev < TORIRS_PLUGIN_EV_COUNT; ev++ )
    {
        int keep = 0;
        for( int i = 0; i < host->sub_count[ev]; i++ )
        {
            if( host->subs[ev][i].plugin == plugin_index )
                continue;
            host->subs[ev][keep++] = host->subs[ev][i];
        }
        host->sub_count[ev] = keep;
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
plugin_teardown(struct ToriRS_PluginHost* host, int plugin_index);
static void plugin_dispatch_one(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    enum ToriRS_PluginEvent ev,
    void* payload);

/* The entity half of the chrome tier, used by the claim and draw verbs that
 * are defined before it. @see the entities section below. */
static int
plugin_entity_parse(char const* part, int* out_a, int* out_b, int* out_c, int* out_d);
static int
plugin_entity_hull_allowed(struct ToriRS_PluginHost* host, int plugin, int element_id);
static void
plugin_chrome_paint_all(struct ToriRS_PluginHost* host, int canvas_pass);
static int
role_replacement_find(struct ToriRS_PluginHost const* host, char const* role);
static int
role_replaced_by(struct ToriRS_PluginHost const* host, char const* role);

static enum ToriRS_PluginVerdict
plugin_dispatch(struct ToriRS_PluginHost* host, enum ToriRS_PluginEvent ev, void* payload)
{
    assert(host);
    assert(ev >= 0);
    assert(ev < TORIRS_PLUGIN_EV_COUNT);

    enum ToriRS_PluginVerdict verdict = TORIRS_PLUGIN_PASS;
    int const prev_dispatching = host->dispatching;
    int const prev_event = host->dispatch_event;

    for( int i = 0; i < host->sub_count[ev]; i++ )
    {
        struct PluginSub const sub = host->subs[ev][i];
        struct ToriRS_PluginCtx* ctx;

        if( sub.plugin < 0 || sub.plugin >= host->plugin_count )
            continue;
        ctx = &host->plugins[sub.plugin];
        if( !ctx->enabled || !ctx->running )
            continue;

        /* A canvas anchor is subscriber-local. Reset on both sides of every
         * callback so an early return, a plugin with no anchor call, or the
         * next plugin in draw order can never inherit the previous target. */
        if( ev == TORIRS_PLUGIN_EV_DRAW_CANVAS )
            (void)host->engine.role_anchor(host->engine.user, -1, NULL, 0, 0);
        host->dispatching = sub.plugin;
        host->dispatch_event = ev;
        verdict = sub.handler(ctx, payload, sub.userdata);
        host->dispatching = prev_dispatching;
        host->dispatch_event = prev_event;
        if( ev == TORIRS_PLUGIN_EV_DRAW_CANVAS )
            (void)host->engine.role_anchor(host->engine.user, -1, NULL, 0, 0);

        if( verdict == TORIRS_PLUGIN_CONSUME )
            return TORIRS_PLUGIN_CONSUME;

        /* The handler may have unsubscribed itself or its neighbours; step
         * back so the entry that slid into this slot is not skipped. */
        if( i < host->sub_count[ev] && host->subs[ev][i].handler != sub.handler )
            i--;
    }
    return TORIRS_PLUGIN_PASS;
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
plugin_ev_is_draw(enum ToriRS_PluginEvent ev)
{
    return ev == TORIRS_PLUGIN_EV_DRAW_WORLD || ev == TORIRS_PLUGIN_EV_DRAW_CANVAS ||
           ev == TORIRS_PLUGIN_EV_DRAW_FRAME || ev == TORIRS_PLUGIN_EV_PANEL_DRAW;
}

/*
 * The key this event sorts a subscriber by, HIGHEST FIRST.
 *
 * A draw pass negates draw_order so that a higher one runs later and therefore
 * lands on top: "nearer the viewer" is the direction a person means by a z
 * order, and the list is walked front to back.
 */
static int
plugin_sub_key(
    struct ToriRS_PluginHost const* host,
    enum ToriRS_PluginEvent ev,
    int plugin)
{
    struct ToriRS_PluginDef const* def = host->plugins[plugin].def;
    return plugin_ev_is_draw(ev) ? -def->draw_order : def->priority;
}

/* Key-ordered insert, so a plugin that declared a higher priority sees an
 * event first -- or, on a draw pass, a higher draw_order lands on top --
 * regardless of registration order. Stable within a key. */
static void
plugin_sub_insert(
    struct ToriRS_PluginHost* host,
    enum ToriRS_PluginEvent ev,
    struct PluginSub const* sub)
{
    assert(host);
    assert(sub);

    int const count = host->sub_count[ev];
    int const priority = plugin_sub_key(host, ev, sub->plugin);
    int at = count;

    for( int i = 0; i < count; i++ )
    {
        int const other = plugin_sub_key(host, ev, host->subs[ev][i].plugin);
        if( priority > other )
        {
            at = i;
            break;
        }
    }
    for( int i = count; i > at; i-- )
        host->subs[ev][i] = host->subs[ev][i - 1];
    host->subs[ev][at] = *sub;
    host->sub_count[ev] = count + 1;
}

/* ------------------------------------------------------------ api surface */

static void
api_subscribe(
    struct ToriRS_PluginCtx* ctx,
    enum ToriRS_PluginEvent ev,
    ToriRS_PluginHandler handler,
    void* userdata)
{
    assert(ctx);
    assert(handler);
    assert(ev >= 0);
    assert(ev < TORIRS_PLUGIN_EV_COUNT);

    struct ToriRS_PluginHost* host = ctx->host;
    if( host->sub_count[ev] >= TORIRS_PLUGIN_SUBS_MAX )
    {
        /* Loud, not silent: a dropped subscription is a plugin that appears to
         * load and then never fires. */
        TORIRS_LOG("plugin: %s subscription to event %d dropped, bus slot full (%d)\n",
            ctx->name,
            (int)ev,
            TORIRS_PLUGIN_SUBS_MAX);
        return;
    }

    struct PluginSub sub = { handler, userdata, ctx->index };
    plugin_sub_insert(host, ev, &sub);
}

static void
api_log(struct ToriRS_PluginCtx* ctx, char const* fmt, ...)
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
_Static_assert((int)TORIRS_PLUGIN_SCREEN_BOOT == 0, "plugin screen BOOT");
_Static_assert((int)TORIRS_PLUGIN_SCREEN_TITLE == 10, "plugin screen TITLE");
_Static_assert((int)TORIRS_PLUGIN_SCREEN_CONNECTING == 20, "plugin screen CONNECTING");
_Static_assert((int)TORIRS_PLUGIN_SCREEN_GAME == 30, "plugin screen GAME");

static int
api_screen(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->engine.screen(ctx->host->engine.user);
}

/*
 * NOT gated on host_frame_exists, alone among the geometry verbs: the OS
 * covering the window is a fact about the WINDOW, and it is as true on the
 * title screen as in game. An engine with no notion of OS occlusion (a test
 * fake, a desktop that never wired one) answers with the whole canvas, which
 * is the honest answer on a machine with no soft keyboard.
 */
static int
api_safe_os(
    struct ToriRS_PluginCtx* ctx,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    assert(ctx);
    if( ctx->host->engine.safe_os )
        return ctx->host->engine.safe_os(
            ctx->host->engine.user, out_x, out_y, out_w, out_h);
    return ctx->host->engine.slot_rect(
        ctx->host->engine.user, TORIRS_PLUGIN_SLOT_CANVAS, out_x, out_y, out_w, out_h);
}

static int
api_world_cycle(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->engine.world_cycle(ctx->host->engine.user);
}

static uint64_t
api_frame_ms(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->engine.frame_ms(ctx->host->engine.user);
}

static uint64_t
api_frame_work_us(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->engine.frame_work_us(ctx->host->engine.user);
}

static int
api_local_player(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginPlayerSnap* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.local_player(ctx->host->engine.user, out);
}

static int
api_npc_next(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginNpcSnap* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.npc_next(ctx->host->engine.user, iter, out);
}

static int
api_npc_by_slot(struct ToriRS_PluginCtx* ctx, int slot, struct ToriRS_PluginNpcSnap* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.npc_by_slot(ctx->host->engine.user, slot, out);
}

static int
api_player_next(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginPlayerSnap* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.player_next(ctx->host->engine.user, iter, out);
}

static int
api_loc_next(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginLocSnap* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.loc_next(ctx->host->engine.user, iter, out);
}

static int
api_highlight_next(
    struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginHighlightItem* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.highlight_next(ctx->host->engine.user, iter, out);
}

static void
api_notify(struct ToriRS_PluginCtx* ctx, char const* text)
{
    assert(ctx);
    assert(text);
    ctx->host->engine.notify(ctx->host->engine.user, text);
}

static int
api_key_held(struct ToriRS_PluginCtx* ctx, int keycode)
{
    assert(ctx);
    return ctx->host->engine.key_held(ctx->host->engine.user, keycode);
}

static int
api_hover_tile(
    struct ToriRS_PluginCtx* ctx,
    int* out_tile_x,
    int* out_tile_z,
    int* out_level)
{
    assert(ctx);
    assert(out_tile_x);
    assert(out_tile_z);
    assert(out_level);
    return ctx->host->engine.hover_tile(
        ctx->host->engine.user, out_tile_x, out_tile_z, out_level);
}

static int
api_hover_entity(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginHoverEntity* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.hover_entity(ctx->host->engine.user, out);
}

static int
api_element_height(struct ToriRS_PluginCtx* ctx, int element_id)
{
    assert(ctx);
    return ctx->host->engine.element_height(ctx->host->engine.user, element_id);
}

static int
api_mouse_pos(struct ToriRS_PluginCtx* ctx, int* out_x, int* out_y)
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
    enum ToriRS_PluginEvent ev,
    void* payload);

/*
 * Tell the engine what the claim is now, so it can switch the lane's own
 * chrome off (or back on) and pin or unpin the canvas.
 *
 * Called on every transition and on no-ops besides, because the engine's copy
 * of this is what the layout pass reads and a claim the engine never heard
 * about is a plugin drawing stones over a frame that is still drawing its own.
 */
static void
plugin_layout_publish(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->engine.layout_set(
        host->engine.user,
        host->layout_owner >= 0 ? 1 : 0,
        host->layout_canvas,
        host->layout_fixed_w,
        host->layout_fixed_h);
}

static bool
api_layout_claim(
    struct ToriRS_PluginCtx* ctx,
    int canvas,
    int fixed_w,
    int fixed_h)
{
    struct ToriRS_PluginHost* host;

    assert(ctx);
    host = ctx->host;
    /* Both arrive from a plugin's own config, so a value out of range is bad
     * INPUT and not a broken contract: a layout told to pin a canvas of 0x0
     * should be refused and say so, not abort the client. */
    if( canvas != TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW && canvas != TORIRS_PLUGIN_CANVAS_FIXED )
        return false;
    if( canvas == TORIRS_PLUGIN_CANVAS_FIXED && (fixed_w <= 0 || fixed_h <= 0) )
        return false;
    /*
     * There is no frame to claim before there is a frame.
     *
     * A granted claim hands the plugin the canvas and takes the client's own
     * frame out of the picture; on the title screen that means the background,
     * the logo and the login box are given away to a plugin which then draws a
     * gameframe's worth of nothing, because the parts it dresses do not exist
     * yet. The symptom is a login screen with most of its art missing and no
     * error anywhere -- every plugin behaved exactly as written.
     *
     * Refused HERE and not left to each plugin, even though the frame dressers
     * gate themselves too (@see gameframe.c frame_on_layout): a plugin asking
     * for something by name on a tree that has no gameframe gets whatever else
     * happens to answer to that name, so "ask nicely" is not a property the
     * host can rely on. This is the one door, so this is where it closes.
     */
    if( host->engine.screen(host->engine.user) != TORIRS_PLUGIN_SCREEN_GAME )
        return false;
    if( host->layout_owner >= 0 && host->layout_owner != ctx->index )
        return false;

    host->layout_claim_epoch++;
    host->layout_owner = ctx->index;
    host->layout_canvas = canvas;
    /*
     * Kept for BOTH modes, and they mean different things in each: the pinned
     * canvas for FIXED, the smallest canvas the layout can be declared against
     * for FOLLOW_WINDOW. @see layout_claim.
     *
     * They used to be zeroed for FOLLOW_WINDOW, on the reasoning that a frame
     * following the window has no size of its own to state. It has one: the
     * size below which its own pieces stop fitting each other. Without it the
     * engine has nothing to clamp to but the client's 765x503 -- the REVCONFIG
     * gameframe's floor -- and a layout authored for a phone is clamped up to a
     * desktop canvas and letterboxed into the shape it was written to avoid.
     *
     * A claim that names no minimum still passes zeroes, which the engine reads
     * as "no opinion" and answers with its own floor.
     */
    host->layout_fixed_w = fixed_w > 0 ? fixed_w : 0;
    host->layout_fixed_h = fixed_h > 0 ? fixed_h : 0;
    /*
     * Publishing the claim is the whole of it: the engine marks the frame
     * needing a declaration and raises EV_LAYOUT on its next layout pass, with
     * the canvas it actually has.
     *
     * This used to declare here as well, passing 0x0 for the canvas because
     * the host has no window and no way to ask. For a FIXED claim that was
     * harmless -- it reads its pinned size back -- and for a FOLLOW_WINDOW one
     * it was silently fatal: every anchor is measured from an edge, so a
     * canvas of nothing puts the sidebar at x = -245 and the chatbox at
     * y = -142, and the frame is declared, drawn, and entirely off-screen. The
     * plugin reports twenty pieces drawn and the screen shows none of them.
     *
     * The host cannot invent a canvas. The engine is the only thing that knows
     * one, so the engine is what declares.
     */
    plugin_layout_publish(host);
    return true;
}

static void
api_layout_release(struct ToriRS_PluginCtx* ctx)
{
    struct ToriRS_PluginHost* host;

    assert(ctx);
    host = ctx->host;
    if( host->layout_owner != ctx->index )
        return;
    host->layout_claim_epoch++;
    host->layout_owner = -1;
    host->layout_canvas = TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW;
    host->layout_fixed_w = 0;
    host->layout_fixed_h = 0;
    plugin_layout_publish(host);
}

static int
api_layout_owned(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->layout_owner == ctx->index ? 1 : 0;
}

static int
api_layout_slot_at(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int member,
    int x,
    int y,
    int w,
    int h)
{
    assert(ctx);
    /* The same window test the draw verbs make. A slot placed outside the
     * declaration would land in a table the engine has already applied, so it
     * would take effect a frame late and survive a declaration that never
     * mentioned it -- which is exactly the drift the rebuilt-from-nothing rule
     * exists to prevent. */
    assert(
        ctx->host->layout_declaring &&
        "layout_slot is legal only inside EV_LAYOUT");
    assert(ctx->host->layout_owner == ctx->index);
    /* A number a plugin computed, so out of range is input. */
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;
    if( w <= 0 || h <= 0 )
        return 0;
    return ctx->host->engine.layout_slot(ctx->host->engine.user, slot, member, x, y, w, h);
}

/* The whole role, which is `member` -1. One entry point with a name for the
 * common case, rather than every caller writing the sentinel. */
static int
api_layout_slot(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int x,
    int y,
    int w,
    int h)
{
    return api_layout_slot_at(ctx, slot, -1, x, y, w, h);
}

static int
api_tab_active(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->engine.tab_active(ctx->host->engine.user);
}

static bool
api_tab_select(struct ToriRS_PluginCtx* ctx, int tabno)
{
    assert(ctx);
    /* A tab number a plugin read off its own stone table. */
    if( tabno < 0 )
        return false;
    return ctx->host->engine.tab_select(ctx->host->engine.user, tabno) ? true : false;
}

static int
api_tab_enabled(struct ToriRS_PluginCtx* ctx, int tabno)
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
static struct PluginRect
plugin_rect_subtract(struct PluginRect box, struct PluginRect cut)
{
    struct PluginRect best = box;
    int best_area = -1;
    int const bx1 = box.x + box.w;
    int const by1 = box.y + box.h;
    int const cx1 = cut.x + cut.w;
    int const cy1 = cut.y + cut.h;
    struct PluginRect candidate[4];

    if( cut.w <= 0 || cut.h <= 0 )
        return box;
    /* Somewhere else on the screen entirely. */
    if( cx1 <= box.x || cut.x >= bx1 || cy1 <= box.y || cut.y >= by1 )
        return box;

    /* Left of it, right of it, above it, below it. */
    candidate[0].x = box.x;
    candidate[0].y = box.y;
    candidate[0].w = cut.x - box.x;
    candidate[0].h = box.h;
    candidate[1].x = cx1;
    candidate[1].y = box.y;
    candidate[1].w = bx1 - cx1;
    candidate[1].h = box.h;
    candidate[2].x = box.x;
    candidate[2].y = box.y;
    candidate[2].w = box.w;
    candidate[2].h = cut.y - box.y;
    candidate[3].x = box.x;
    candidate[3].y = cy1;
    candidate[3].w = box.w;
    candidate[3].h = by1 - cy1;

    for( int i = 0; i < 4; i++ )
    {
        int const area = candidate[i].w > 0 && candidate[i].h > 0
                             ? candidate[i].w * candidate[i].h
                             : 0;
        if( area > best_area )
        {
            best_area = area;
            best = candidate[i];
        }
    }
    /* Every slice was empty: the cut covers the box. Answer with nothing
     * rather than with a negative rectangle -- a caller's fallback chain reads
     * that as "this frame has no such region", which is true. */
    if( best_area <= 0 )
    {
        best.w = 0;
        best.h = 0;
    }
    return best;
}

/** Ask the engine for a placeable region, or CANVAS. */
static int
plugin_engine_rect(struct ToriRS_PluginHost* host, int slot, struct PluginRect* out)
{
    assert(host);
    assert(out);
    return host->engine.slot_rect(
        host->engine.user, slot, &out->x, &out->y, &out->w, &out->h);
}

/**
 * SAFE_GAMECHROME: the scene's box with the chrome and every reservation taken out.
 *
 * Starts from VIEWPORT rather than from CANVAS because on a fixed frame that
 * is already the answer -- the chrome sits outside the scene box and every
 * subtraction below misses. On a resizable frame the viewport IS the window,
 * and the same code then does real work as the minimap, the chatbox and the
 * sidebar are cut out of it one at a time. One rule, both window modes.
 *
 * MAIN_MODAL is deliberately not an occluder: it is the region a modal opens
 * INTO, which is the middle of the safe area rather than something covering
 * it.
 */
static int
plugin_safe_gamechrome_rect(struct ToriRS_PluginHost* host, struct PluginRect* out)
{
    static int const OCCLUDER[] = {
        TORIRS_PLUGIN_SLOT_MINIMAP,
        TORIRS_PLUGIN_SLOT_COMPASS,
        TORIRS_PLUGIN_SLOT_CHAT,
        TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,
        TORIRS_PLUGIN_SLOT_SIDEBAR,
    };
    struct PluginRect box;

    assert(host);
    assert(out);

    if( !plugin_engine_rect(host, TORIRS_PLUGIN_SLOT_VIEWPORT, &box) &&
        !plugin_engine_rect(host, TORIRS_PLUGIN_SLOT_CANVAS, &box) )
        return 0;

    for( size_t i = 0; i < sizeof(OCCLUDER) / sizeof(OCCLUDER[0]); i++ )
    {
        struct PluginRect cut;
        if( plugin_engine_rect(host, OCCLUDER[i], &cut) )
            box = plugin_rect_subtract(box, cut);
    }

    /*
     * Then the reservations, in DECLARATION order.
     *
     * Order is what makes two plugins on the same edge stack instead of fight:
     * the first one's 180 pixels are gone before the second one's 120 are
     * measured, so the second gets the 120 next to it rather than the same 120
     * on top of it.
     */
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        struct PluginReserve const* r = &host->reserves[i];
        int px;

        if( r->plugin < 0 || r->slot != TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME || r->px <= 0 )
            continue;
        switch( r->edge )
        {
        case TORIRS_PLUGIN_EDGE_LEFT:
            px = r->px > box.w ? box.w : r->px;
            box.x += px;
            box.w -= px;
            break;
        case TORIRS_PLUGIN_EDGE_RIGHT:
            px = r->px > box.w ? box.w : r->px;
            box.w -= px;
            break;
        case TORIRS_PLUGIN_EDGE_TOP:
            px = r->px > box.h ? box.h : r->px;
            box.y += px;
            box.h -= px;
            break;
        default:
            px = r->px > box.h ? box.h : r->px;
            box.h -= px;
            break;
        }
    }

    *out = box;
    return box.w > 0 && box.h > 0;
}

/**
 * Does the GAMEFRAME exist right now?
 *
 * Every verb below that answers a question about the frame -- by role name, by
 * slot, by component id -- answers "no such thing" when it does not, rather
 * than resolving the name against whatever tree happens to be up.
 *
 * The title screen has a tree, and it has components, and some of them answer
 * to the names a frame dresser asks for. A plugin that asks for a rectangle by
 * name and is handed one has no way to tell that it belongs to the login box
 * rather than to the chat frame, so it dresses it -- and the login screen loses
 * its art to furniture drawn for a screen nobody is on. "Ask for things by
 * name" is only safe while the names mean what the asker thinks they mean, and
 * off the gameframe they do not.
 *
 * A refusal, not an abort: a plugin polling role_rect every frame across a
 * logout is doing nothing wrong, and this is the answer it should get.
 */
static int
host_frame_exists(struct ToriRS_PluginCtx const* ctx)
{
    assert(ctx);
    return ctx->host->engine.screen(ctx->host->engine.user) == TORIRS_PLUGIN_SCREEN_GAME;
}

static int
api_slot_rect(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    struct PluginRect box;
    int got;

    if( !host_frame_exists(ctx) )
        return 0; /* @see host_frame_exists */

    assert(ctx);

    /* A region id out of range is a plugin's arithmetic, not a broken
     * contract -- it may have come from a config key or from a script -- so it
     * answers "this frame has no such region", like any other absent one. */
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_COUNT )
        return 0;

    got = slot == TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME ? plugin_safe_gamechrome_rect(ctx->host, &box)
                                          : plugin_engine_rect(ctx->host, slot, &box);
    if( !got || box.w <= 0 || box.h <= 0 )
        return 0;

    if( out_x )
        *out_x = box.x;
    if( out_y )
        *out_y = box.y;
    if( out_w )
        *out_w = box.w;
    if( out_h )
        *out_h = box.h;
    return 1;
}

/* ------------------------------------------------------------ roles */

/*
 * Where a role's element is. @see ToriRS_PluginApi::role_rect.
 *
 * `safe_gamechrome` is answered here and never reaches the engine, for the same reason
 * SLOT_SAFE_GAMECHROME does not: it is the placeable regions minus every plugin's edge
 * reservation, and the reservation table is the host's. Routing it through
 * api_slot_rect rather than re-deriving it is what keeps the name and the
 * region enum answering with one rectangle.
 */
static int
api_role_rect(
    struct ToriRS_PluginCtx* ctx,
    char const* role,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;

    if( !host_frame_exists(ctx) )
        return 0; /* @see host_frame_exists */

    assert(ctx);
    /* An empty name is a plugin's own string handling, and the answer is the
     * same one an undeclared role gets. */
    if( !role || role[0] == '\0' )
        return 0;

    if( strcmp(role, "safe_gamechrome") == 0 )
        return api_slot_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, out_x, out_y, out_w, out_h);

    if( !ctx->host->engine.role_rect(ctx->host->engine.user, role, &x, &y, &w, &h) )
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
api_role_visible(struct ToriRS_PluginCtx* ctx, char const* role)
{
    assert(ctx);
    if( !role || role[0] == '\0' )
        return 0;
    if( !host_frame_exists(ctx) )
        return 0; /* @see host_frame_exists */
    /* `safe_gamechrome` and `canvas` are rectangles rather than things that can be
     * hidden, and a derived region is on screen whenever the client is. */
    if( strcmp(role, "safe_gamechrome") == 0 || strcmp(role, "canvas") == 0 )
        return api_role_rect(ctx, role, NULL, NULL, NULL, NULL);
    /*
     * A REPLACED role is on screen: its provider paints it at the tombstone
     * every frame the provider runs. The engine would answer "hidden", which
     * is true of the lane's own node and false of the object -- and the
     * object is what a caller means. A plugin deciding whether to hang
     * something off `minimap_edge` needs "is there a minimap_edge", not
     * "is the cache's plate visible".
     */
    {
        int const by = role_replaced_by(ctx->host, role);
        if( by >= 0 && ctx->host->plugins[by].running )
            return 1;
    }
    return ctx->host->engine.role_visible(ctx->host->engine.user, role);
}

static int
api_role_click(struct ToriRS_PluginCtx* ctx, char const* role, int op)
{
    assert(ctx);
    if( !role || role[0] == '\0' )
        return 0;
    if( !host_frame_exists(ctx) )
        return 0; /* @see host_frame_exists -- and a click on a name that means
                   * something else is worse than a rectangle that does. */
    /* Same reading as if_click's: an op out of range came from a config key or
     * a script, so it is bad input and not a broken contract. */
    if( op < 0 || op > 10 )
        return 0;
    return ctx->host->engine.role_click(ctx->host->engine.user, role, op);
}

static int
api_role_id(struct ToriRS_PluginCtx* ctx, char const* role)
{
    assert(ctx);
    if( !role || role[0] == '\0' )
        return -1;
    if( !host_frame_exists(ctx) )
        return -1; /* @see host_frame_exists */
    return ctx->host->engine.role_id(ctx->host->engine.user, role);
}

static int
role_replacement_find(struct ToriRS_PluginHost const* host, char const* role)
{
    assert(host);
    assert(role);
    for( int i = 0; i < TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX; i++ )
        if( host->role_replacements[i].plugin >= 0 &&
            strcmp(host->role_replacements[i].role, role) == 0 )
            return i;
    return -1;
}

/**
 * Which plugin currently PROVIDES `role` in place of the lane, or -1.
 *
 * Two tables answer, because a role is replaced two ways: a plugin says so
 * outright (role_replace), or it claims the APPEARANCE of a lane part and
 * the chrome pass replaces the node for it (PluginHost_ChromeTick,
 * `lane_replaced`). The second never entered role_replacements, so every
 * verb that asked "is this role replaced" -- role_visible, role_anchor, the
 * tombstone paint -- saw the claimed housing as unprovided: an orb column
 * asked whether `minimap_edge` was on screen, heard no, hung itself off the
 * map instead, and painted under the very plate it meant to sit on.
 */
static int
role_replaced_by(struct ToriRS_PluginHost const* host, char const* role)
{
    int at;

    assert(host);
    assert(role);
    at = role_replacement_find(host, role);
    if( at >= 0 )
        return host->role_replacements[at].plugin;
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* row = &host->chrome_claims[i];
        if( row->plugin >= 0 && row->lane_replaced && strcmp(row->part, role) == 0 )
            return row->plugin;
    }
    return -1;
}

static int
role_replacement_free(struct ToriRS_PluginHost const* host)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX; i++ )
        if( host->role_replacements[i].plugin < 0 )
            return i;
    return -1;
}

static void
role_replacements_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX; i++ )
    {
        struct PluginRoleReplacement* claim = &host->role_replacements[i];
        if( claim->plugin != plugin )
            continue;
        (void)host->engine.role_replace(
            host->engine.user, plugin, claim->role, /*enabled=*/0);
        claim->plugin = -1;
        claim->role[0] = '\0';
    }
}

static int
api_role_replace(
    struct ToriRS_PluginCtx* ctx,
    char const* role,
    int enabled)
{
    struct ToriRS_PluginHost* host;
    int at;

    assert(ctx);
    host = ctx->host;
    if( !role || !role[0] || strlen(role) >= TORIRS_PLUGIN_ROLE_NAME_MAX )
        return 0;
    /* These are derived rectangles, not semantic component identities. */
    if( strcmp(role, "safe_gamechrome") == 0 || strcmp(role, "canvas") == 0 )
        return 0;

    at = role_replacement_find(host, role);
    if( enabled )
    {
        if( at >= 0 && host->role_replacements[at].plugin != ctx->index )
            return 0;
        if( at < 0 )
        {
            at = role_replacement_free(host);
            if( at < 0 )
                return 0;
            host->role_replacements[at].plugin = ctx->index;
            snprintf(
                host->role_replacements[at].role,
                sizeof(host->role_replacements[at].role),
                "%s",
                role);
        }
        /* Resolution is intentionally not the return value. The persistent
         * claim must survive a temporarily missing role and bind when its next
         * incarnation appears. */
        (void)host->engine.role_replace(host->engine.user, ctx->index, role, 1);
        return 1;
    }

    if( at < 0 )
        return 1; /* idempotent release */
    if( host->role_replacements[at].plugin != ctx->index )
        return 0;
    (void)host->engine.role_replace(host->engine.user, ctx->index, role, 0);
    host->role_replacements[at].plugin = -1;
    host->role_replacements[at].role[0] = '\0';
    return 1;
}

static int
api_role_anchor(struct ToriRS_PluginCtx* ctx, char const* role, int place)
{
    struct ToriRS_PluginHost* host;
    int replace = 0;

    assert(ctx);
    host = ctx->host;
    assert(
        host->draw_surface && host->draw_canvas == PLUGIN_DRAW_SURFACE_CANVAS &&
        "role_anchor is legal only inside EV_DRAW_CANVAS");
    if( !role || !role[0] || strlen(role) >= TORIRS_PLUGIN_ROLE_NAME_MAX )
    {
        /* Non-NULL empty is the engine-side active-invalid sentinel. A failed
         * retarget must drop subsequent declarations from this subscriber,
         * never leave its previous anchor active or fall back to global
         * Canvas. Empty is safe because it is not a legal public role name. */
        (void)host->engine.role_anchor(host->engine.user, ctx->index, "", 0, 0);
        return 0;
    }
    if( strcmp(role, "safe_gamechrome") == 0 || strcmp(role, "canvas") == 0 )
    {
        (void)host->engine.role_anchor(host->engine.user, ctx->index, "", 0, 0);
        return 0;
    }

    /*
     * A REPLACED role is anchored at its tombstone by everyone, not only by
     * the plugin that replaced it.
     *
     * The name is the object. When one plugin provides `minimap_edge` in
     * place of the lane's own plate, a second plugin hanging its orb column
     * off `minimap_edge` is hanging it off that object -- and the object now
     * paints at the tombstone, so that is where the column has to paint too.
     * Refusing the second plugin (which this used to do, as an "active
     * invalid" anchor) made every replacement an island: a semantic name
     * whose meaning changed for exactly the plugins that had done the right
     * thing and asked for it by name.
     *
     * Order inside the tombstone is arrival order, and the host paints the
     * replacement's own declaration first (plugin_chrome_paint_all runs
     * before EV_DRAW_CANVAS), so what is hung off the object lands on top
     * of it.
     */
    if( role_replaced_by(host, role) >= 0 )
        replace = 1;
    return host->engine.role_anchor(
        host->engine.user,
        ctx->index,
        role,
        replace,
        place == TORIRS_PLUGIN_ANCHOR_BEFORE ? TORIRS_PLUGIN_ANCHOR_BEFORE
                                             : TORIRS_PLUGIN_ANCHOR_AFTER);
}

/*
 * One member of a region. @see ToriRS_PluginApi::slot_member_rect.
 *
 * SAFE is absent on purpose and is not an oversight: it is derived from the
 * placeable regions and has no members to number.
 */
static int
api_slot_member_rect(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int member,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;

    if( !host_frame_exists(ctx) )
        return 0; /* @see host_frame_exists */

    assert(ctx);

    /* Same reading as slot_rect's: an id out of range is a plugin's
     * arithmetic, and the answer is "this frame has no such region". */
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT || member < 0 )
        return 0;

    if( !ctx->host->engine.slot_member_rect(ctx->host->engine.user, slot, member, &x, &y, &w, &h) )
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

/*
 * Where a component is. @see ToriRS_PluginApi::component_rect.
 *
 * No range test on the id, unlike the region verbs above: every 32-bit value
 * is a well-formed component id, and "the tree has no such component" is the
 * engine's answer to all of them.
 */
static int
api_component_rect(
    struct ToriRS_PluginCtx* ctx,
    int component_id,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int x = 0, y = 0, w = 0, h = 0;

    if( !host_frame_exists(ctx) )
        return 0; /* @see host_frame_exists */

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
api_layout_reserve(struct ToriRS_PluginCtx* ctx, int slot, int edge, int px)
{
    struct ToriRS_PluginHost* host;
    int free_row = -1;

    assert(ctx);
    host = ctx->host;

    /* Only the derived regions. A placeable role is whatever the frame says it
     * is, and eating an edge of it here would be arguing with the layout
     * rather than making room beside it. */
    if( slot != TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME )
    {
        TORIRS_LOG("plugin: %s reserved from region %d; only SAFE can be reserved from\n",
            ctx->name,
            slot);
        return 0;
    }
    if( edge < 0 || edge >= TORIRS_PLUGIN_EDGE_COUNT )
        return 0;
    if( px < 0 )
        px = 0;

    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        struct PluginReserve* r = &host->reserves[i];
        if( r->plugin == ctx->index && r->slot == slot && r->edge == edge )
        {
            /* Replaced IN PLACE, keeping this plugin's position in the
             * declaration order: one that re-states its width on a config
             * change must not jump to the back of the queue and swap sides
             * with a neighbour that has not moved. */
            if( r->px == px )
                return 1;
            r->px = px;
            if( px == 0 )
                r->plugin = -1;
            PluginHost_LayoutChanged(host);
            return 1;
        }
        if( r->plugin < 0 && free_row < 0 )
            free_row = i;
    }

    if( px == 0 )
        return 1; /* nothing of this plugin's to drop */
    if( free_row < 0 )
    {
        TORIRS_LOG("plugin: %s could not reserve; the reservation table is full (%d)\n",
            ctx->name,
            TORIRS_PLUGIN_RESERVES_MAX);
        return 0;
    }
    host->reserves[free_row].plugin = ctx->index;
    host->reserves[free_row].slot = (uint8_t)slot;
    host->reserves[free_row].edge = (uint8_t)edge;
    host->reserves[free_row].px = px;
    PluginHost_LayoutChanged(host);
    return 1;
}

static int
api_layout_revision(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->layout_revision;
}

static void
plugin_reserves_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
{
    int dropped = 0;

    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
    {
        if( host->reserves[i].plugin != plugin )
            continue;
        host->reserves[i].plugin = -1;
        host->reserves[i].px = 0;
        dropped = 1;
    }
    if( dropped )
        PluginHost_LayoutChanged(host);
}

static int
api_stat(struct ToriRS_PluginCtx* ctx, int skill, int* out_current, int* out_base)
{
    assert(ctx);
    /* `skill` is a NUMBER a plugin computed or read out of its own config, so
     * an out-of-range one is bad input rather than a broken contract: the
     * engine answers 0 and leaves the outs alone. */
    return ctx->host->engine.stat(ctx->host->engine.user, skill, out_current, out_base);
}

static int
api_stat_xp(
    struct ToriRS_PluginCtx* ctx,
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
api_skill_name(struct ToriRS_PluginCtx* ctx, int skill)
{
    assert(ctx);
    return ctx->host->engine.skill_name(ctx->host->engine.user, skill);
}

static int
api_run_energy(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    return ctx->host->engine.run_energy(ctx->host->engine.user);
}

static int
api_project(
    struct ToriRS_PluginCtx* ctx,
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
PluginHost_ConfigGet(struct ToriRS_PluginHost const* host, int plugin_index, char const* key)
{
    assert(host);
    assert(key);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);

    struct ToriRS_PluginCtx const* ctx = &host->plugins[plugin_index];
    for( int i = 0; i < ctx->config_count; i++ )
    {
        if( strcmp(ctx->config[i].key, key) == 0 )
            return ctx->config[i].value;
    }
    return NULL;
}

static int
api_cfg_has(struct ToriRS_PluginCtx* ctx, char const* key)
{
    assert(ctx);
    assert(key);
    return PluginHost_ConfigGet(ctx->host, ctx->index, key) != NULL;
}

static char const*
api_cfg_str(struct ToriRS_PluginCtx* ctx, char const* key)
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
plugin_cfg_number(char const* value, int fallback)
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
api_cfg_bool(struct ToriRS_PluginCtx* ctx, char const* key)
{
    char const* value = api_cfg_str(ctx, key);
    if( value[0] == '\0' )
        return 0;
    if( strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 )
        return 1;
    return plugin_cfg_number(value, 0) != 0;
}

static int
api_cfg_int(struct ToriRS_PluginCtx* ctx, char const* key)
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
api_cfg_color(struct ToriRS_PluginCtx* ctx, char const* key)
{
    return (uint32_t)plugin_cfg_number(api_cfg_str(ctx, key), 0) & 0xffffffu;
}

void
PluginHost_ConfigSet(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* key,
    char const* value)
{
    assert(host);
    assert(key);
    assert(value);

    struct ToriRS_PluginCtx* ctx = plugin_at(host, plugin_index);
    struct PluginConfigSlot* slot = plugin_config_slot(ctx, key, true);
    if( !slot )
        return;
    if( strcmp(slot->value, value) == 0 )
        return;

    snprintf(slot->value, sizeof(slot->value), "%s", value);
    host->config_dirty = true;

    if( ctx->enabled && ctx->running )
    {
        struct ToriRS_PluginEvConfig ev = { slot->key };
        int const prev = host->dispatching;
        host->dispatching = plugin_index;
        /* Only the owning plugin hears about its own key. A config change is
         * not a broadcast: nobody else has a stake in it. */
        for( int i = 0; i < host->sub_count[TORIRS_PLUGIN_EV_CONFIG_CHANGED]; i++ )
        {
            struct PluginSub const* sub = &host->subs[TORIRS_PLUGIN_EV_CONFIG_CHANGED][i];
            if( sub->plugin != plugin_index )
                continue;
            if( sub->handler(ctx, &ev, sub->userdata) == TORIRS_PLUGIN_CONSUME )
                break;
        }
        host->dispatching = prev;
    }
}

static void
api_cfg_set(struct ToriRS_PluginCtx* ctx, char const* key, char const* value)
{
    assert(ctx);
    assert(key);
    assert(value);
    PluginHost_ConfigSet(ctx->host, ctx->index, key, value);
}

/* -- the client's own variables -- */

static int
api_varbit(struct ToriRS_PluginCtx* ctx, int varbit_id)
{
    assert(ctx);
    return ctx->host->engine.varbit(ctx->host->engine.user, varbit_id);
}

static int
api_feature_next(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginFeature* out)
{
    assert(ctx);
    assert(out);
    return ctx->host->engine.feature_next(ctx->host->engine.user, iter, out);
}

static int
api_feature_get(struct ToriRS_PluginCtx* ctx, char const* key)
{
    assert(ctx);
    assert(key);
    return ctx->host->engine.feature_get(ctx->host->engine.user, key);
}

static bool
api_feature_set(struct ToriRS_PluginCtx* ctx, char const* key, int value)
{
    assert(ctx);
    assert(key);
    return ctx->host->engine.feature_set(ctx->host->engine.user, key, value) != 0;
}

static int
api_display_setting(
    struct ToriRS_PluginCtx* ctx,
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
    if( setting < 0 || setting >= TORIRS_PLUGIN_DISPLAY_SETTING_COUNT )
        return 0;
    return ctx->host->engine.display_setting(
        ctx->host->engine.user, setting, out_value, out_min, out_max);
}

static int
api_display_setting_set(
    struct ToriRS_PluginCtx* ctx,
    int setting,
    int value)
{
    assert(ctx);
    if( setting < 0 || setting >= TORIRS_PLUGIN_DISPLAY_SETTING_COUNT )
        return 0;
    return ctx->host->engine.display_setting_set(ctx->host->engine.user, setting, value);
}

static int
api_varp(struct ToriRS_PluginCtx* ctx, int varp_id)
{
    assert(ctx);
    return ctx->host->engine.varp(ctx->host->engine.user, varp_id);
}

static int
api_cache_id(struct ToriRS_PluginCtx* ctx, char const* kind, char const* name)
{
    assert(ctx);
    assert(kind);
    assert(name);
    if( !ctx->host->engine.cache_id )
        return -1;
    return ctx->host->engine.cache_id(ctx->host->engine.user, kind, name);
}

static int
api_lane(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginLane* out)
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

/*
 * A plugin standing down.
 *
 * The teardown is PluginHost_SetEnabled's, and what it does NOT do is the
 * point: `enabled` is left alone and the store is not marked dirty, so the
 * user's saved switch survives a lane that cannot run the plugin.
 */
static void
api_disable_self(struct ToriRS_PluginCtx* ctx, char const* reason)
{
    struct ToriRS_PluginHost* host;
    int prev;

    assert(ctx);
    assert(reason);
    /* An essential plugin has one state -- the roster draws no switch for it
     * and SetEnabled refuses to clear it -- so a def that declares itself
     * essential and then stands down is that plugin's own bug. */
    assert(!ctx->def->essential);

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
}

static int
api_obj_info(
    struct ToriRS_PluginCtx* ctx,
    int obj_id,
    struct ToriRS_PluginObjInfo* out)
{
    assert(ctx);
    assert(out);
    /* An id the plugin computed, so out of range is bad input rather than a
     * broken contract -- the engine answers 0 and leaves `out` alone. */
    return ctx->host->engine.obj_info(ctx->host->engine.user, obj_id, out);
}

static int
api_inv_slot(
    struct ToriRS_PluginCtx* ctx,
    int inv,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    assert(ctx);
    return ctx->host->engine.inv_slot(
        ctx->host->engine.user, inv, slot, out_obj_id, out_count);
}

static int
api_inv_size(struct ToriRS_PluginCtx* ctx, int inv)
{
    assert(ctx);
    return ctx->host->engine.inv_size(ctx->host->engine.user, inv);
}

static uint32_t
api_setting_color(struct ToriRS_PluginCtx* ctx, int varp_id, uint32_t fallback)
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
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginEvMenuBuild* menu,
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
api_obj_next(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginObjSnap* out)
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
plugin_asset_name_ok(struct ToriRS_PluginCtx* ctx, char const* name)
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
        TORIRS_ERR("plugin: %s asked for asset '%s'; asset names are bare filenames of "
            "[A-Za-z0-9._-] with no '..', so that one is refused\n",
            ctx->name,
            name);
    return ok;
}

static struct PluginAsset*
plugin_asset_find(struct ToriRS_PluginHost* host, int plugin, char const* name)
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
plugin_asset_drop(struct ToriRS_PluginHost* host, struct PluginAsset* slot)
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
plugin_asset_claim(struct ToriRS_PluginHost* host, int plugin, char const* name)
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
plugin_assets_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
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
plugin_image_owned(struct ToriRS_PluginCtx* ctx, int image)
{
    assert(ctx);

    if( image < 0 || image >= TORIRS_PLUGIN_IMAGES_MAX )
        return NULL;
    if( ctx->host->images[image].plugin != ctx->index )
    {
        TORIRS_LOG("plugin: %s used image handle %d, which it does not own\n",
            ctx->name,
            image);
        return NULL;
    }
    return &ctx->host->images[image];
}

/* ------------------------------------------------------------------ borrows */

/**
 * Is this plugin allowed to READ `image` -- because it owns it, or because
 * chrome_part lent it one?
 *
 * The read verbs go through here and the write verbs do not, which is the
 * whole of what a borrow permits. image_release still asks plugin_image_owned
 * and so is a no-op for a borrower; image_compose takes a NAME in the caller's
 * own namespace and can never reach a foreign slot at all.
 *
 * Silent about a handle it refuses, unlike plugin_image_owned: a borrow going
 * stale is an ORDINARY event -- the lender was switched off -- and the caller's
 * correct response is to draw nothing, which is what it already does for an
 * image whose pixels have not landed.
 */
static struct PluginImage const*
plugin_image_readable(struct ToriRS_PluginCtx* ctx, int image)
{
    struct ToriRS_PluginHost const* host;

    assert(ctx);
    if( image < 0 || image >= TORIRS_PLUGIN_IMAGES_MAX )
        return NULL;

    host = ctx->host;
    if( host->images[image].plugin == ctx->index )
        return &host->images[image];

    for( int i = 0; i < (int)(sizeof(host->chrome_borrows) /
                              sizeof(host->chrome_borrows[0])); i++ )
    {
        struct PluginChromeBorrow const* row = &host->chrome_borrows[i];
        if( row->borrower != ctx->index || row->lender_image != image )
            continue;
        /* Stale reads exactly as pending: the slot was dropped and possibly
         * refilled, so the picture behind this handle is not the one that was
         * lent. @see PluginChromeBorrow. */
        if( row->generation != host->images[image].generation )
            return NULL;
        return &host->images[image];
    }
    return NULL;
}

/**
 * Lend `lender_image` to `borrower`, returning the handle it should use.
 *
 * Idempotent on (borrower, image): a plugin asking chrome_part every EV_CHROME
 * gets the same row back rather than one per pass. A row whose generation has
 * moved is REBOUND rather than duplicated, so a lender that dropped and
 * reloaded its art is picked up on the next ask without the borrower knowing
 * anything happened.
 *
 * @return the handle, or -1 when the table is full -- which the caller reports
 * as a part with no art rather than as no part, because the BOX is still true
 * and a dresser that ships its own picture needs nothing else.
 */
static int
plugin_chrome_borrow(struct ToriRS_PluginHost* host, int borrower, int lender_image)
{
    int free_row = -1;

    assert(host);
    if( lender_image < 0 || lender_image >= TORIRS_PLUGIN_IMAGES_MAX )
        return -1;
    /* A plugin never borrows from itself: the handle it already holds is the
     * same number, and a row for it would only be a row to keep in step. */
    if( host->images[lender_image].plugin == borrower )
        return lender_image;
    if( host->images[lender_image].plugin < 0 )
        return -1;

    for( int i = 0; i < (int)(sizeof(host->chrome_borrows) /
                              sizeof(host->chrome_borrows[0])); i++ )
    {
        struct PluginChromeBorrow* row = &host->chrome_borrows[i];
        if( row->borrower < 0 )
        {
            if( free_row < 0 )
                free_row = i;
            continue;
        }
        if( row->borrower == borrower && row->lender_image == lender_image )
        {
            row->generation = host->images[lender_image].generation;
            return lender_image;
        }
    }

    if( free_row < 0 )
    {
        TORIRS_LOG("plugin: %s could not borrow image %d; the borrow table is full\n",
            host->plugins[borrower].name,
            lender_image);
        return -1;
    }
    host->chrome_borrows[free_row].borrower = borrower;
    host->chrome_borrows[free_row].lender_image = lender_image;
    host->chrome_borrows[free_row].generation = host->images[lender_image].generation;
    return lender_image;
}

/** Drop every borrow a plugin holds, and every borrow of ITS images. Both
 *  halves, because a teardown ends the lending in both directions. */
static void
plugin_chrome_borrows_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
{
    assert(host);
    for( int i = 0; i < (int)(sizeof(host->chrome_borrows) /
                              sizeof(host->chrome_borrows[0])); i++ )
    {
        struct PluginChromeBorrow* row = &host->chrome_borrows[i];
        if( row->borrower < 0 )
            continue;
        if( row->borrower == plugin ||
            host->images[row->lender_image].plugin == plugin )
        {
            row->borrower = -1;
            row->lender_image = 0;
            row->generation = 0;
        }
    }
}

/** Free a slot's scene entry and mark it free. Idempotent. */
static void
plugin_image_drop(struct ToriRS_PluginHost* host, int image)
{
    assert(host);
    assert(image >= 0 && image < TORIRS_PLUGIN_IMAGES_MAX);

    if( host->images[image].published )
        host->engine.image_release(host->engine.user, image);
    {
        /*
         * The generation SURVIVES the wipe and moves on, because it is a fact
         * about the slot rather than about the image that was in it. A borrow
         * taken against the old occupant compares it and reads as stale; if it
         * were reset here, the next load into this slot would hand that borrow
         * a matching counter and a different picture.
         */
        uint32_t const generation = host->images[image].generation + 1u;
        memset(&host->images[image], 0, sizeof(host->images[image]));
        host->images[image].generation = generation;
    }
    host->images[image].plugin = -1;
}

static void
plugin_model_drop(struct ToriRS_PluginHost* host, int model)
{
    assert(host);
    assert(model >= 0 && model < TORIRS_PLUGIN_MODELS_MAX);

    if( host->models[model].published )
        host->engine.model_release(host->engine.user, model);
    memset(&host->models[model], 0, sizeof(host->models[model]));
    host->models[model].plugin = -1;
}

static void
plugin_models_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
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
    struct ToriRS_PluginHost* host, int model, void const* data, int size)
{
    struct PluginModel* slot;

    assert(host);
    assert(model >= 0 && model < TORIRS_PLUGIN_MODELS_MAX);
    assert(data);

    slot = &host->models[model];
    if( !host->engine.model_publish(host->engine.user, model, data, size) )
    {
        TORIRS_LOG("plugin: %s model '%s' would not decode (%d bytes); it draws nothing\n",
            host->plugins[slot->plugin].name,
            slot->asset,
            size);
        return;
    }
    slot->published = true;
}

static void
plugin_images_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
        if( host->images[i].plugin == plugin )
            plugin_image_drop(host, i);
}

/* ---------------------------------------------------------- the icon cache */

/** Forget one cached icon and free the image slot behind it. Idempotent. */
static void
plugin_obj_icon_drop(struct ToriRS_PluginHost* host, int entry)
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
plugin_obj_icons_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_OBJ_ICONS_MAX; i++ )
        if( host->obj_icons[i].plugin == plugin )
            plugin_obj_icon_drop(host, i);
}

/** Is this handle one of the icon cache's, rather than the plugin's own? */
static bool
plugin_obj_icon_owns(struct ToriRS_PluginHost const* host, int image)
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
api_obj_image(struct ToriRS_PluginCtx* ctx, int obj_id, int count, int style)
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
    if( obj_id < 0 || count < 0 || style < 0 ||
        style > TORIRS_PLUGIN_OBJ_ICON_SELECTED )
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
        if( row->plugin == ctx->index && row->obj_id == obj_id &&
            row->count == count && row->style == style )
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
        TORIRS_LOG("plugin: %s obj icon %d not built, the resident image table is full "
            "(%d)\n",
            ctx->name,
            obj_id,
            TORIRS_PLUGIN_IMAGES_MAX);
        return -1;
    }

    if( !host->engine.obj_image(
            host->engine.user, free_image, obj_id, count, style, &w, &h) )
        return -1;

    host->images[free_image].plugin = ctx->index;
    /*
     * A synthetic name no asset_load could produce -- the sandbox refuses a
     * ':' -- so the (plugin, name) search image_load and image_compose do can
     * never match a cached icon and hand a plugin's own art this slot.
     */
    snprintf(
        host->images[free_image].asset, sizeof(host->images[free_image].asset),
        "obj:%d:%d:%d", obj_id, count, style);
    host->images[free_image].width = w;
    host->images[free_image].height = h;
    host->images[free_image].published = true;

    host->obj_icons[free_entry].obj_id = obj_id;
    host->obj_icons[free_entry].count = count;
    host->obj_icons[free_entry].style = style;
    host->obj_icons[free_entry].plugin = ctx->index;
    host->obj_icons[free_entry].image = free_image;
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
    struct ToriRS_PluginHost* host, int image, void const* data, int size)
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
        TORIRS_LOG("plugin: %s image '%s' would not decode (%d bytes); it draws nothing\n",
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
api_asset_load(struct ToriRS_PluginCtx* ctx, char const* name)
{
    assert(ctx);
    assert(name);

    struct ToriRS_PluginHost* host = ctx->host;
    if( !plugin_asset_name_ok(ctx, name) )
        return 0;

    struct PluginAsset* slot = plugin_asset_find(host, ctx->index, name);
    if( slot && slot->data )
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
        TORIRS_LOG("plugin: %s asset '%s' not loaded, the resident asset table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_ASSETS_MAX);
        return 0;
    }

    slot->pending = true;
    if( !host->engine.asset_read(host->engine.user, ctx->name, name) )
    {
        slot->pending = false;
        plugin_asset_drop(host, slot);
        return 0;
    }
    return 0;
}

static void const*
api_asset_data(struct ToriRS_PluginCtx* ctx, char const* name, int* out_size)
{
    assert(ctx);
    assert(name);

    struct PluginAsset const* slot = plugin_asset_find(ctx->host, ctx->index, name);
    if( out_size )
        *out_size = slot ? slot->size : 0;
    return slot ? slot->data : NULL;
}

static int
api_asset_save(struct ToriRS_PluginCtx* ctx, char const* name, void const* data, int size)
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
        TORIRS_LOG("plugin: %s asset '%s' not saved, the resident asset table is full (%d)\n",
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

    return host->engine.asset_write(host->engine.user, ctx->name, name, data, size);
}

static void
api_asset_release(struct ToriRS_PluginCtx* ctx, char const* name)
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
plugin_screenshot_dir_ok(struct ToriRS_PluginCtx* ctx, char const* dir)
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
        TORIRS_LOG("plugin: %s asked to write a screenshot to '%s'; a destination is a path of "
            "[A-Za-z0-9._- /:] with no '..'\n",
            ctx->name,
            dir);
    return ok;
}

static int
api_screenshot(
    struct ToriRS_PluginCtx* ctx,
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
api_datestamp(struct ToriRS_PluginCtx* ctx, char* out, int out_size)
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
                !(host->panel_registered[plugin] &&
                  host->panel_icon_image[plugin] == i &&
                  size > TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX) )
                plugin_image_publish(host, i, slot->data, slot->size);
        for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
            if( host->models[i].plugin == plugin &&
                strcmp(host->models[i].asset, asset_name) == 0 )
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

    struct ToriRS_PluginCtx* ctx = &host->plugins[plugin];
    if( !ctx->enabled || !ctx->running )
        return;

    struct ToriRS_PluginEvAsset ev = { asset_name, data ? size : 0, data != NULL };
    int const prev = host->dispatching;
    host->dispatching = plugin;
    /* Only the plugin that asked. An asset is not a broadcast: the name means
     * nothing outside its own namespace. */
    for( int i = 0; i < host->sub_count[TORIRS_PLUGIN_EV_ASSET]; i++ )
    {
        struct PluginSub const* sub = &host->subs[TORIRS_PLUGIN_EV_ASSET][i];
        if( sub->plugin != plugin )
            continue;
        if( sub->handler(ctx, &ev, sub->userdata) == TORIRS_PLUGIN_CONSUME )
            break;
    }
    host->dispatching = prev;
}

/* -- authored meshes -- */

/* Same rule the object handles are held to: a handle the plugin was never
 * given is a contract violation, and the budget is not. */
static void
plugin_mesh_assert_owned(struct ToriRS_PluginCtx* ctx, int mesh)
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
api_mesh_create(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);

    struct ToriRS_PluginHost* host = ctx->host;
    if( ctx->mesh_count >= TORIRS_PLUGIN_MESH_BUDGET )
    {
        if( !ctx->mesh_clipped )
        {
            ctx->mesh_clipped = true;
            TORIRS_ERR("plugin: %s is at its %d mesh budget; further mesh_create "
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
api_mesh_destroy(struct ToriRS_PluginCtx* ctx, int mesh)
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

static void
api_mesh_clear(struct ToriRS_PluginCtx* ctx, int mesh)
{
    plugin_mesh_assert_owned(ctx, mesh);
    ctx->host->engine.mesh_clear(ctx->host->engine.user, mesh);
}

static int
api_mesh_vertex(struct ToriRS_PluginCtx* ctx, int mesh, int x, int y, int z)
{
    plugin_mesh_assert_owned(ctx, mesh);
    return ctx->host->engine.mesh_vertex(ctx->host->engine.user, mesh, x, y, z);
}

static int
api_mesh_face(struct ToriRS_PluginCtx* ctx, int mesh, int a, int b, int c, int hsl, int alpha)
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
plugin_object_assert_owned(struct ToriRS_PluginCtx* ctx, int object)
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
api_object_create(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);

    struct ToriRS_PluginHost* host = ctx->host;
    if( ctx->object_count >= TORIRS_PLUGIN_OBJECT_BUDGET )
    {
        if( !ctx->object_clipped )
        {
            ctx->object_clipped = true;
            TORIRS_ERR("plugin: %s is at its %d world-object budget; further "
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
api_object_destroy(struct ToriRS_PluginCtx* ctx, int object)
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
plugin_objects_destroy_all(struct ToriRS_PluginHost* host, struct ToriRS_PluginCtx* ctx)
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
plugin_meshes_destroy_all(struct ToriRS_PluginHost* host, struct ToriRS_PluginCtx* ctx)
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
    struct ToriRS_PluginCtx* ctx,
    int object,
    enum ToriRS_PluginModelSource source,
    int id)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_model(ctx->host->engine.user, object, (int)source, id);
}

static void
api_object_recolor(struct ToriRS_PluginCtx* ctx, int object, int hsl_from, int hsl_to)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_recolor(ctx->host->engine.user, object, hsl_from, hsl_to);
}

static void
api_object_clear_recolors(struct ToriRS_PluginCtx* ctx, int object)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_clear_recolors(ctx->host->engine.user, object);
}

static void
api_object_set_anim(struct ToriRS_PluginCtx* ctx, int object, int seq_id, int loop)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_anim(ctx->host->engine.user, object, seq_id, loop);
}

static void
api_object_set_light(struct ToriRS_PluginCtx* ctx, int object, int ambient, int contrast)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_light(ctx->host->engine.user, object, ambient, contrast);
}

static void
api_object_set_position(
    struct ToriRS_PluginCtx* ctx,
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
api_object_set_active(struct ToriRS_PluginCtx* ctx, int object, int active)
{
    plugin_object_assert_owned(ctx, object);
    ctx->host->engine.object_set_active(ctx->host->engine.user, object, active);
}

static int
api_object_ready(struct ToriRS_PluginCtx* ctx, int object)
{
    plugin_object_assert_owned(ctx, object);
    return ctx->host->engine.object_ready(ctx->host->engine.user, object);
}

/* -- colour -- */

static int
api_hsl_from_rgb(struct ToriRS_PluginCtx* ctx, uint32_t rgb)
{
    assert(ctx);
    return ctx->host->engine.hsl_from_rgb(ctx->host->engine.user, rgb);
}

static uint32_t
api_hsl_to_rgb(struct ToriRS_PluginCtx* ctx, int hsl)
{
    assert(ctx);
    return ctx->host->engine.hsl_to_rgb(ctx->host->engine.user, hsl);
}

/* -- the plugin window -----------------------------------------------------
 *
 * The registry is a flat pool stamped with owners, walked linearly. Linear
 * because a plugin's tab is a couple of dozen controls at most and the walks
 * happen when a control is declared or used -- never per frame, which is what
 * the revision counter exists to make true.
 */

/** Slot index of `id` within `plugin`'s controls, or -1. */
static int
plugin_win_find(struct ToriRS_PluginHost const* host, int plugin_index, char const* id)
{
    assert(host);
    assert(id);
    for( int i = 0; i < host->win_widget_count; i++ )
        if( host->win_owner[i] == plugin_index && strcmp(host->win_widgets[i].id, id) == 0 )
            return i;
    return -1;
}

static int
plugin_win_count_owned(struct ToriRS_PluginHost const* host, int plugin_index)
{
    int n = 0;
    for( int i = 0; i < host->win_widget_count; i++ )
        if( host->win_owner[i] == plugin_index )
            n++;
    return n;
}

/** A zero serial is reserved for "not a widget" in queued presenter work. */
static uint32_t
plugin_widget_next_serial(struct ToriRS_PluginHost* host)
{
    assert(host);
    host->next_widget_serial++;
    if( host->next_widget_serial == 0 )
        host->next_widget_serial++;
    return host->next_widget_serial;
}

/**
 * Drop every control of one plugin, compacting the pool.
 *
 * Compaction rather than a free list because these slots are addressed by
 * POSITION only within a walk that recomputes it -- nothing outside this file
 * holds a slot index across a call, so there is no handle to invalidate, and a
 * compact array keeps the enumeration the panel does a straight scan.
 */
static void
plugin_win_drop(struct ToriRS_PluginHost* host, int plugin_index)
{
    int keep = 0;

    assert(host);
    for( int i = 0; i < host->win_widget_count; i++ )
    {
        if( host->win_owner[i] == plugin_index )
            continue;
        host->win_widgets[keep] = host->win_widgets[i];
        host->win_owner[keep] = host->win_owner[i];
        keep++;
    }
    if( keep != host->win_widget_count )
    {
        host->win_widget_count = keep;
        host->win_revision++;
    }
}

static bool
api_win_request(struct ToriRS_PluginCtx* ctx, char const* tab_title)
{
    struct ToriRS_PluginHost* host;

    assert(ctx);
    assert(tab_title);
    host = ctx->host;

    if( !host->win_tab[ctx->index] )
    {
        host->win_tab[ctx->index] = true;
        host->win_revision++;
    }
    if( strcmp(host->win_tab_title[ctx->index], tab_title) != 0 )
    {
        plugin_copy_str(
            host->win_tab_title[ctx->index], sizeof(host->win_tab_title[ctx->index]), tab_title);
        host->win_revision++;
    }
    return true;
}

static bool
api_win_widget(struct ToriRS_PluginCtx* ctx, int kind, char const* id, char const* label)
{
    struct ToriRS_PluginHost* host;
    struct ToriRS_PluginWinWidget* w;

    assert(ctx);
    assert(id);
    host = ctx->host;

    /* A tab is implied by putting something on it, so a plugin that only ever
     * wanted controls does not have to remember to ask for the tab first. */
    if( !host->win_tab[ctx->index] )
        api_win_request(ctx, ctx->name);

    /* Re-declaring an id is a no-op rather than a duplicate: EV_UI_BUILD can be
     * raised more than once for the same tab, and a plugin that declares its
     * controls unconditionally there must not stack them up. */
    if( plugin_win_find(host, ctx->index, id) >= 0 )
        return true;

    if( plugin_win_count_owned(host, ctx->index) >= TORIRS_PLUGIN_WIDGETS_MAX ||
        host->win_widget_count >= TORIRS_PLUGIN_WIN_WIDGETS_MAX )
    {
        PluginHost_SetError(host, ctx->index, "window control budget exhausted");
        return false;
    }

    w = &host->win_widgets[host->win_widget_count];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->selected = -1;
    w->value = 0;
    w->serial = plugin_widget_next_serial(host);
    plugin_copy_str(w->id, sizeof(w->id), id);
    plugin_copy_str(w->label, sizeof(w->label), label ? label : "");
    host->win_owner[host->win_widget_count] = ctx->index;
    host->win_widget_count++;
    host->win_revision++;
    return true;
}

static bool
api_win_set_text(struct ToriRS_PluginCtx* ctx, char const* id, char const* text)
{
    int slot;

    assert(ctx);
    assert(id);
    slot = plugin_win_find(ctx->host, ctx->index, id);
    if( slot < 0 )
        return false;
    /* A value change deliberately does not bump the revision -- see the note on
     * PluginHost_WinRevision. The presentation mirrors it onto the control it
     * already has rather than rebuilding the tab around it. */
    plugin_copy_str(
        ctx->host->win_widgets[slot].text, sizeof(ctx->host->win_widgets[slot].text),
        text ? text : "");
    return true;
}

static bool
api_win_set_checked(struct ToriRS_PluginCtx* ctx, char const* id, bool on)
{
    int slot;

    assert(ctx);
    assert(id);
    slot = plugin_win_find(ctx->host, ctx->index, id);
    if( slot < 0 )
        return false;
    ctx->host->win_widgets[slot].checked = on ? 1 : 0;
    ctx->host->win_widgets[slot].value = on ? 1 : 0;
    return true;
}

static bool
api_win_set_options(
    struct ToriRS_PluginCtx* ctx, char const* id, char const* choices, int selected)
{
    struct ToriRS_PluginWinWidget* w;
    int slot;

    assert(ctx);
    assert(id);
    slot = plugin_win_find(ctx->host, ctx->index, id);
    if( slot < 0 )
        return false;
    w = &ctx->host->win_widgets[slot];
    if( strcmp(w->choices, choices ? choices : "") != 0 )
    {
        plugin_copy_str(w->choices, sizeof(w->choices), choices ? choices : "");
        /* The LIST is shape, unlike the selection into it: a presentation
         * holding a native combo box has to be rebuilt around a new list. */
        ctx->host->win_revision++;
    }
    w->selected = selected;
    w->value = selected;
    return true;
}

static void
api_win_clear(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    plugin_win_drop(ctx->host, ctx->index);
}

/* -- the application plugin panel ----------------------------------------
 *
 * Registration is per plugin; content is not. Only panel_active owns the
 * small array below, which is what makes "one shell, most recently selected
 * plugin only" an invariant of the authority rather than a convention every
 * presenter has to remember.
 */

static int api_image_load(struct ToriRS_PluginCtx* ctx, char const* name);

static void
plugin_panel_bump(uint32_t* revision)
{
    assert(revision);
    (*revision)++;
    if( *revision == 0 )
        (*revision)++;
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
plugin_panel_find(struct ToriRS_PluginHost const* host, char const* id)
{
    assert(host);
    assert(id);
    for( int i = 0; i < host->panel_widget_count; i++ )
        if( strcmp(host->panel_widgets[i].id, id) == 0 )
            return i;
    return -1;
}

static int
plugin_panel_find_serial(struct ToriRS_PluginHost const* host, uint32_t serial)
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
        return;
    memset(host->panel_widgets, 0, sizeof(host->panel_widgets));
    memset(host->panel_invalidated, 0, sizeof(host->panel_invalidated));
    host->panel_widget_count = 0;
    plugin_panel_bump(&host->panel_model_revision);
}

static bool
api_panel_request(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginPanelDesc const* desc)
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
     * moment to make it: its EV_START callback. */
    if( host->dispatching != ctx->index ||
        host->dispatch_event != TORIRS_PLUGIN_EV_START || !desc )
        return false;

    title = desc->title && desc->title[0] ? desc->title : ctx->title;
    icon = desc->icon_asset ? desc->icon_asset : "";
    if( icon[0] && !plugin_asset_name_ok(ctx, icon) )
        return false;
    width = desc->preferred_width;
    if( width == 0 )
        width = TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT;
    if( width < TORIRS_PLUGIN_PANEL_WIDTH_MIN )
        width = TORIRS_PLUGIN_PANEL_WIDTH_MIN;
    if( width > TORIRS_PLUGIN_PANEL_WIDTH_MAX )
        width = TORIRS_PLUGIN_PANEL_WIDTH_MAX;

    changed = !host->panel_registered[ctx->index] ||
              plugin_copy_str_would_change(
                  host->panel_title[ctx->index],
                  sizeof(host->panel_title[ctx->index]),
                  title) ||
              plugin_copy_str_would_change(
                  host->panel_icon[ctx->index],
                  sizeof(host->panel_icon[ctx->index]),
                  icon) ||
              host->panel_preferred_width[ctx->index] != width;
    host->panel_registered[ctx->index] = true;
    plugin_copy_str(
        host->panel_title[ctx->index], sizeof(host->panel_title[ctx->index]), title);
    plugin_copy_str(
        host->panel_icon[ctx->index], sizeof(host->panel_icon[ctx->index]), icon);
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
        struct PluginAsset const* resident =
            plugin_asset_find(host, ctx->index, icon);
        host->panel_icon_image[ctx->index] =
            resident && resident->data &&
                    resident->size > TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX
                ? -1
                : api_image_load(ctx, icon);
    }
    else
        host->panel_icon_image[ctx->index] = -1;
    return true;
}

static bool
api_panel_widget(
    struct ToriRS_PluginCtx* ctx,
    int kind,
    char const* id,
    char const* label)
{
    struct ToriRS_PluginHost* host;
    struct ToriRS_PluginWinWidget* widget;

    assert(ctx);
    host = ctx->host;
    if( host->panel_active != ctx->index || !host->panel_building ||
        host->dispatching != ctx->index ||
        host->dispatch_event != TORIRS_PLUGIN_EV_PANEL_BUILD )
        return false;
    if( kind < 0 || kind >= TORIRS_PLUGIN_W_COUNT || !plugin_panel_id_ok(id) )
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
    if( kind == TORIRS_PLUGIN_W_CUSTOM )
        widget->preferred_height = TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_DEFAULT;
    widget->serial = plugin_widget_next_serial(host);
    plugin_copy_str(widget->id, sizeof(widget->id), id);
    plugin_copy_str(widget->label, sizeof(widget->label), label);
    host->panel_invalidated[host->panel_widget_count] =
        kind == TORIRS_PLUGIN_W_CUSTOM;
    host->panel_widget_count++;
    plugin_panel_bump(&host->panel_model_revision);
    return true;
}

static bool
plugin_panel_mutable(
    struct ToriRS_PluginCtx* ctx,
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
api_panel_set_text(struct ToriRS_PluginCtx* ctx, char const* id, char const* text)
{
    struct ToriRS_PluginWinWidget* widget;
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
    return true;
}

static bool
api_panel_set_value(struct ToriRS_PluginCtx* ctx, char const* id, int value)
{
    struct ToriRS_PluginWinWidget* widget;
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
    if( widget->kind == TORIRS_PLUGIN_W_CHECKBOX ||
        widget->kind == TORIRS_PLUGIN_W_TOGGLE ||
        widget->kind == TORIRS_PLUGIN_W_LIST_ROW )
        widget->checked = value ? 1 : 0;
    if( widget->kind == TORIRS_PLUGIN_W_DROPDOWN )
        widget->selected = value;
    if( old_checked != widget->checked || old_selected != widget->selected ||
        old_value != widget->value )
        plugin_panel_bump(&ctx->host->panel_model_revision);
    return true;
}

static bool
api_panel_set_height(
    struct ToriRS_PluginCtx* ctx,
    char const* custom_view_id,
    int preferred_height)
{
    struct ToriRS_PluginWinWidget* widget;
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, custom_view_id, &slot) )
        return false;
    widget = &ctx->host->panel_widgets[slot];
    if( widget->kind != TORIRS_PLUGIN_W_CUSTOM )
        return false;
    if( preferred_height == 0 )
        preferred_height = TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_DEFAULT;
    if( preferred_height < TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MIN )
        preferred_height = TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MIN;
    if( preferred_height > TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MAX )
        preferred_height = TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MAX;
    if( widget->preferred_height == preferred_height )
        return true;
    widget->preferred_height = preferred_height;
    ctx->host->panel_invalidated[slot] = true;
    plugin_panel_bump(&ctx->host->panel_model_revision);
    return true;
}

static bool
api_panel_set_options(
    struct ToriRS_PluginCtx* ctx,
    char const* id,
    char const* choices,
    int selected)
{
    struct ToriRS_PluginWinWidget* widget;
    char const* next = choices ? choices : "";
    bool changed;
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, id, &slot) )
        return false;
    widget = &ctx->host->panel_widgets[slot];
    changed = plugin_copy_str_would_change(
                  widget->choices, sizeof(widget->choices), next) ||
              widget->selected != selected || widget->value != selected;
    plugin_copy_str(widget->choices, sizeof(widget->choices), next);
    widget->selected = selected;
    widget->value = selected;
    if( changed )
        plugin_panel_bump(&ctx->host->panel_model_revision);
    return true;
}

static bool
api_panel_set_badge(struct ToriRS_PluginCtx* ctx, char const* text)
{
    char const* next = text ? text : "";

    assert(ctx);
    if( !ctx->host->panel_registered[ctx->index] || !ctx->running )
        return false;
    if( !plugin_copy_str_would_change(
            ctx->host->panel_badge[ctx->index],
            sizeof(ctx->host->panel_badge[ctx->index]),
            next) )
        return true;
    plugin_copy_str(
        ctx->host->panel_badge[ctx->index],
        sizeof(ctx->host->panel_badge[ctx->index]),
        next);
    plugin_panel_bump(&ctx->host->panel_registry_revision);
    return true;
}

static bool
api_panel_set_attention(struct ToriRS_PluginCtx* ctx, bool attention)
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
api_panel_clear(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    if( ctx->host->panel_active != ctx->index )
        return;
    plugin_panel_clear_model(ctx->host);
    if( !ctx->host->panel_building )
        ctx->host->panel_needs_build = true;
}

static void
api_panel_invalidate(struct ToriRS_PluginCtx* ctx, char const* custom_view_id)
{
    int slot;

    assert(ctx);
    if( !plugin_panel_mutable(ctx, custom_view_id, &slot) ||
        ctx->host->panel_widgets[slot].kind != TORIRS_PLUGIN_W_CUSTOM ||
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
    struct ToriRS_PluginEvPanelLayout ev;
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
        plugin_dispatch_one(host, old, TORIRS_PLUGIN_EV_PANEL_LAYOUT, &ev);
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
plugin_panel_unregister(struct ToriRS_PluginHost* host, int plugin_index)
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
    host->panel_badge[plugin_index][0] = '\0';
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
    struct ToriRS_PluginEvPanelBuild ev;
    int plugin;

    assert(host);
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation )
        return 0;
    plugin = host->panel_active;
    if( plugin < 0 || plugin >= host->plugin_count ||
        !host->panel_registered[plugin] || !host->plugins[plugin].running )
        return 0;
    if( !host->panel_needs_build )
        return 1;

    plugin_panel_clear_model(host);
    host->panel_needs_build = false;
    host->panel_building = true;
    memset(&ev, 0, sizeof(ev));
    ev.selection_generation = selection_generation;
    ev.view = host->panel_view;
    plugin_dispatch_one(host, plugin, TORIRS_PLUGIN_EV_PANEL_BUILD, &ev);
    host->panel_building = false;

    /* A build handler is allowed to fault/disable itself. */
    return host->panel_active == plugin &&
                   host->panel_selection_generation == selection_generation
               ? 1
               : 0;
}

/* -- drawing -- */

/* One gate for every draw call: the window must be open, and the plugin must
 * still be inside its per-frame allotment. Clipping is reported once per frame
 * per plugin -- a silent cap reads as "drew everything" when it did not. */
static bool
plugin_draw_allow(struct ToriRS_PluginCtx* ctx, void* surface)
{
    assert(ctx);
    /* Drawing outside EV_DRAW_WORLD would push into a list the emit walk has
     * already read. */
    assert(ctx->host->draw_surface);
    assert(surface == ctx->host->draw_surface);
    (void)surface;

    if( ctx->draw_used >= TORIRS_PLUGIN_DRAW_BUDGET )
    {
        if( !ctx->draw_clipped )
        {
            ctx->draw_clipped = true;
            TORIRS_LOG("plugin: %s hit its %d-item draw budget this frame; "
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
plugin_draw_require_world(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    assert(ctx);
    assert(
        ctx->host->draw_canvas == PLUGIN_DRAW_SURFACE_WORLD &&
        "draw_tile/draw_hull name something in the scene; the screen surfaces have none");
}

static void
api_draw_tile(
    struct ToriRS_PluginCtx* ctx,
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
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int element_id,
    uint32_t rgb,
    int fill_alpha,
    int shape)
{
    assert(shape == TORIRS_PLUGIN_HULL_BOUNDS || shape == TORIRS_PLUGIN_HULL_MESH);
    plugin_draw_require_world(ctx);
    if( !plugin_draw_allow(ctx, surface) )
        return;
    /* An entity whose APPEARANCE another plugin holds is that plugin's to
     * outline. Silent, like a stale borrow: the caller's correct response is
     * to draw nothing, and it already does that for an element that is not
     * on screen. @see plugin_entity_hull_allowed. */
    if( !plugin_entity_hull_allowed(ctx->host, ctx->index, element_id) )
        return;
    ctx->draw_used +=
        ctx->host->engine.draw_hull(ctx->host->engine.user, element_id, rgb, fill_alpha, shape);
}

static void
api_draw_line(
    struct ToriRS_PluginCtx* ctx,
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
    struct ToriRS_PluginCtx* ctx,
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
    struct ToriRS_PluginCtx* ctx,
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
api_image_load(struct ToriRS_PluginCtx* ctx, char const* name)
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
        if( host->images[i].plugin == ctx->index &&
            strcmp(host->images[i].asset, name) == 0 )
            return i;
        if( host->images[i].plugin < 0 && free_slot < 0 )
            free_slot = i;
    }

    if( free_slot < 0 )
    {
        TORIRS_LOG("plugin: %s image '%s' not loaded, the resident image table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_IMAGES_MAX);
        return -1;
    }

    host->images[free_slot].plugin = ctx->index;
    snprintf(
        host->images[free_slot].asset, sizeof(host->images[free_slot].asset), "%s", name);
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
api_model_load(struct ToriRS_PluginCtx* ctx, char const* name)
{
    assert(ctx);
    assert(name);

    struct ToriRS_PluginHost* host = ctx->host;
    int free_slot = -1;

    if( !plugin_asset_name_ok(ctx, name) )
        return -1;

    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
    {
        if( host->models[i].plugin == ctx->index &&
            strcmp(host->models[i].asset, name) == 0 )
            return i;
        if( host->models[i].plugin < 0 && free_slot < 0 )
            free_slot = i;
    }

    if( free_slot < 0 )
    {
        TORIRS_LOG("plugin: %s model '%s' not loaded, the resident model table is full (%d)\n",
            ctx->name,
            name,
            TORIRS_PLUGIN_MODELS_MAX);
        return -1;
    }

    host->models[free_slot].plugin = ctx->index;
    snprintf(
        host->models[free_slot].asset, sizeof(host->models[free_slot].asset), "%s", name);
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
    struct ToriRS_PluginCtx* ctx,
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
        TORIRS_LOG("plugin: %s composed image '%s' is %dx%d, which is not a picture\n",
            ctx->name,
            name,
            w,
            h);
        return -1;
    }

    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
    {
        if( host->images[i].plugin == ctx->index &&
            strcmp(host->images[i].asset, name) == 0 )
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
        TORIRS_LOG("plugin: %s image '%s' not composed, the resident image table is full "
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
api_image_pixels(struct ToriRS_PluginCtx* ctx, int image, uint32_t* out, int max)
{
    /*
     * READABLE and not owned, so a BORROWED handle can be read back.
     *
     * That is what makes "keep the frame's plate and put my icon on it"
     * expressible: read the lender's pixels, compose them with the plugin's
     * own art under a name in its OWN namespace, declare the result. Without
     * it a dresser could only blit a borrowed picture whole, and every
     * composite would need a PNG decoder inside the plugin.
     *
     * It stays a read. image_compose names a slot this plugin owns, so nothing
     * here opens a path to writing into somebody else's.
     */
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
api_image_size(struct ToriRS_PluginCtx* ctx, int image, int* out_w, int* out_h)
{
    /* A borrowed handle measures like an owned one, and a STALE borrow
     * measures like a pending image: 0x0 and a 0 return. A caller laying out
     * against it has nothing to do differently for the two. */
    struct PluginImage const* slot = plugin_image_readable(ctx, image);

    if( out_w )
        *out_w = slot ? slot->width : 0;
    if( out_h )
        *out_h = slot ? slot->height : 0;
    return slot && slot->published ? 1 : 0;
}

static void
api_image_release(struct ToriRS_PluginCtx* ctx, int image)
{
    /* OWNED, deliberately: a borrower releasing a handle it was lent would
     * free the lender's picture out from under the frame. The refusal needs no
     * special case -- a borrowed slot's `plugin` is the lender's index. */
    if( !plugin_image_owned(ctx, image) )
        return;
    /*
     * A CACHED ITEM ICON is owned by this plugin's slot and still is not the
     * plugin's to free: the entry pointing at it would go on handing the
     * handle out, and the next caller would draw whatever landed in the
     * recycled slot. The cache decides when an icon goes.
     * @see ToriRS_PluginApi::obj_image.
     */
    if( plugin_obj_icon_owns(ctx->host, image) )
        return;
    plugin_image_drop(ctx->host, image);
}

/* ------------------------------------------------------------------- chrome */

/*
 * The second tier of the frame. @see ToriRS_PluginApi::chrome_claim.
 *
 * Everything here lives in the HOST and nothing new was needed from the
 * engine beyond one reverse lookup, which is not an accident: a chrome part is
 * a box, a picture and a click, and the engine already answers "where is this
 * role" (role_rect), "what does this name mean here" (role_slot), "hide the
 * cache's own" (role_replace), "put my drawing inside that subtree"
 * (role_anchor) and "blit this" (draw_image). The tier is those verbs
 * arranged, plus arbitration -- and arbitration between plugins was never the
 * engine's to do.
 *
 * A claim is on (part, SCOPE). One row per (plugin, part) carries the mask of
 * scopes that plugin holds there, so three plugins may hold the three scopes
 * of one part and a resolve COMPOSES them: the box from whoever holds
 * POSITION, the pictures from whoever holds APPEARANCE, the click from
 * whoever holds HITBOX, and whatever provided the part underneath for each
 * scope nobody took.
 */

/** This plugin's row for `part`, or NULL. */
static struct PluginChromeClaim*
plugin_chrome_row(struct ToriRS_PluginHost* host, int plugin, char const* part)
{
    assert(host);
    assert(part);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        if( row->plugin == plugin && strcmp(row->part, part) == 0 )
            return row;
    }
    return NULL;
}

/** Whoever holds `scope` of `part`, or NULL when nobody does. */
static struct PluginChromeClaim*
plugin_chrome_holder(struct ToriRS_PluginHost* host, char const* part, int scope)
{
    assert(host);
    assert(part);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        if( row->plugin >= 0 && (row->scopes & scope) && strcmp(row->part, part) == 0 )
            return row;
    }
    return NULL;
}

/** The mask of `scopes` held by anyone but `except` (-1 for anyone at all). */
static int
plugin_chrome_held(struct ToriRS_PluginHost* host, char const* part, int scopes, int except)
{
    int held = 0;

    assert(host);
    assert(part);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* row = &host->chrome_claims[i];
        if( row->plugin < 0 || row->plugin == except )
            continue;
        if( strcmp(row->part, part) == 0 )
            held |= row->scopes & scopes;
    }
    return held;
}

/** Any row at all for `part` -- the ADDED row is the one that matters. */
static struct PluginChromeClaim*
plugin_chrome_added_row(struct ToriRS_PluginHost* host, char const* part)
{
    assert(host);
    assert(part);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        if( row->plugin >= 0 && row->added && strcmp(row->part, part) == 0 )
            return row;
    }
    return NULL;
}

/** The arranger's declaration for one member, or NULL. */
static struct PluginSlotArt*
plugin_slot_art_find(struct ToriRS_PluginHost* host, int slot, int member)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_SLOT_ART_MAX; i++ )
    {
        struct PluginSlotArt* row = &host->slot_art[i];
        if( row->used && row->slot == (uint8_t)slot && row->member == member )
            return row;
    }
    return NULL;
}

/**
 * What a part is UNDERNEATH every claim: its source, its box and its pictures
 * as the lane, the frame's arranger, or the introducing plugin provide them.
 *
 * Three authorities, most specific first; each step is an answer rather than
 * a fallback, and NONE is an answer too.
 *
 * `out_state` is the arranger's chosen state for a FRAME part, IDLE otherwise.
 */
static int
plugin_chrome_base(
    struct ToriRS_PluginHost* host,
    char const* part,
    struct ToriRS_PluginChromePart* out,
    int* out_state)
{
    struct PluginChromeClaim* added;
    int slot = -1;
    int member = -1;

    assert(host);
    assert(part);
    assert(out);
    assert(out_state);

    memset(out, 0, sizeof(*out));
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
        out->art[i] = -1;
    *out_state = TORIRS_PLUGIN_CHROME_IDLE;

    added = plugin_chrome_added_row(host, part);
    if( added )
    {
        int ax = 0;
        int ay = 0;
        int aw = 0;
        int ah = 0;

        /* An added part has no box of its own: it is an offset from something
         * that does. An anchor this revision has not got means the part is
         * not anywhere, which is the honest answer to "put an orb column on a
         * gameframe with no minimap". */
        if( !host->engine.role_rect(host->engine.user, added->anchor, &ax, &ay, &aw, &ah) )
            return 0;
        /* The introducer's OWN declaration is the base for its part; the
         * composition below then lets other holders override the scopes they
         * took from it. Its box is anchor-relative and is made absolute here,
         * once, so every reader sees canvas coordinates. */
        *out = added->art;
        out->x = ax + added->art.x;
        out->y = ay + added->art.y;
        out->source = TORIRS_PLUGIN_CHROME_SOURCE_ADDED;
        *out_state = added->state;
        return 1;
    }

    if( host->engine.role_slot(host->engine.user, part, &slot, &member) )
    {
        struct PluginSlotArt const* declared = plugin_slot_art_find(host, slot, member);
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        int got = member < 0
                      ? host->engine.slot_rect(host->engine.user, slot, &x, &y, &w, &h)
                      : host->engine.slot_member_rect(
                            host->engine.user, slot, member, &x, &y, &w, &h);
        if( declared )
        {
            *out = declared->art;
            out->source = TORIRS_PLUGIN_CHROME_SOURCE_FRAME;
            *out_state = declared->state;
            return 1;
        }
        if( !got )
            return 0;
        out->x = x;
        out->y = y;
        out->w = w;
        out->h = h;
        out->source = TORIRS_PLUGIN_CHROME_SOURCE_LANE;
        return 1;
    }

    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        if( !host->engine.role_rect(host->engine.user, part, &x, &y, &w, &h) )
            return 0;
        out->x = x;
        out->y = y;
        out->w = w;
        out->h = h;
        out->source = TORIRS_PLUGIN_CHROME_SOURCE_LANE;
        return 1;
    }
}

/**
 * A part as it is RIGHT NOW: the base with every held scope overlaid by its
 * holder's declaration. Canvas coordinates. Art handles are lent to
 * `borrower` on the way out when it is not -1.
 *
 * `out_hit` is the plugin whose region serves the click, or -1 when the
 * HITBOX is nobody's -- the lane's own, or an arranger's imperative region.
 * `out_ops` is that holder's row, or NULL.
 */
static int
plugin_chrome_resolve(
    struct ToriRS_PluginHost* host,
    char const* part,
    int borrower,
    struct ToriRS_PluginChromePart* out,
    int* out_state,
    struct PluginChromeClaim const** out_ops)
{
    struct ToriRS_PluginChromePart found;
    struct PluginChromeClaim const* holder;
    int state;
    int source;

    assert(host);
    assert(part);
    assert(out);

    if( !plugin_chrome_base(host, part, &found, &state) )
        return 0;
    source = found.source;

    /*
     * The composition, and the whole of what a scope MEANS: each holder's
     * declaration replaces exactly its own fields. The introducer of an added
     * part is skipped -- its declaration IS the base -- so that a second
     * plugin holding one scope of an orb overrides that one scope of it.
     */
    holder = plugin_chrome_holder(host, part, TORIRS_PLUGIN_CHROME_SCOPE_POSITION);
    if( holder && holder->declared && !holder->added && holder->art.w > 0 && holder->art.h > 0 )
    {
        if( source == TORIRS_PLUGIN_CHROME_SOURCE_ADDED )
        {
            /* Relative to the same anchor the introducer used. */
            struct PluginChromeClaim const* added = plugin_chrome_added_row(host, part);
            int ax = 0;
            int ay = 0;
            int aw = 0;
            int ah = 0;
            assert(added);
            (void)host->engine.role_rect(host->engine.user, added->anchor, &ax, &ay, &aw, &ah);
            found.x = ax + holder->art.x;
            found.y = ay + holder->art.y;
        }
        else
        {
            found.x = holder->art.x;
            found.y = holder->art.y;
        }
        found.w = holder->art.w;
        found.h = holder->art.h;
    }

    holder = plugin_chrome_holder(host, part, TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE);
    if( holder && holder->declared && !holder->added )
    {
        memcpy(found.art, holder->art.art, sizeof(found.art));
        found.label_x = holder->art.label_x;
        found.label_y = holder->art.label_y;
        state = holder->state;
    }

    holder = plugin_chrome_holder(host, part, TORIRS_PLUGIN_CHROME_SCOPE_HITBOX);
    if( out_ops )
        *out_ops = holder && holder->has_ops ? holder : NULL;

    if( borrower >= 0 )
        for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
            if( found.art[i] >= 0 )
                found.art[i] = plugin_chrome_borrow(host, borrower, found.art[i]);

    found.source = source;
    *out = found;
    if( out_state )
        *out_state = state;
    return 1;
}

/** Mark every claimant's declaration stale, so the next chrome tick re-asks.
 *  Called wherever a box could have moved under one. */
static void
plugin_chrome_invalidate(struct ToriRS_PluginHost* host)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
        if( host->chrome_claims[i].plugin >= 0 )
            host->chrome_claims[i].needs_declare = 1;
}

/**
 * Take, restate or drop scopes on one part. The engine half of chrome_claim
 * and chrome_add both land here; `anchor` non-NULL introduces the part.
 *
 * @return the mask of `scopes` this plugin now holds, or -1 for a refusal
 * that is not about ownership (a bad name, a re-entrant call, a full table).
 */
static int
plugin_chrome_claim_set(
    struct ToriRS_PluginCtx* ctx,
    char const* part,
    char const* anchor,
    int scopes,
    int enabled)
{
    struct ToriRS_PluginHost* host = ctx->host;
    struct PluginChromeClaim* row;
    int free_row = -1;
    int taken;
    int granted;

    assert(ctx);
    assert(part);

    scopes &= TORIRS_PLUGIN_CHROME_SCOPE_ALL;
    if( part[0] == '\0' || strlen(part) >= TORIRS_PLUGIN_ROLE_NAME_MAX )
        return -1;
    /*
     * Refused rather than asserted while the table is being walked. A plugin
     * legitimately learns mid-pass that it wants a part; what it must not do
     * is add a row to the array somebody is iterating. It takes the claim from
     * its tick instead and has it on the next pass.
     */
    if( host->chrome_iterating )
    {
        TORIRS_LOG("plugin: %s tried to claim '%s' from inside a chrome pass\n",
            ctx->name,
            part);
        return -1;
    }

    row = plugin_chrome_row(host, ctx->index, part);
    if( !enabled )
    {
        if( !row )
            return 0;
        row->scopes &= ~scopes;
        /* An introducer releasing everything REMOVES the part: unlike a native
         * one there is nothing underneath to fall back to. */
        if( row->scopes == 0 || (row->added && !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_ALL)) )
        {
            if( row->lane_replaced )
                (void)host->engine.role_replace(host->engine.user, ctx->index, part, 0);
            memset(row, 0, sizeof(*row));
            row->plugin = -1;
        }
        PluginHost_LayoutChanged(host);
        return 0;
    }

    taken = plugin_chrome_held(host, part, scopes, ctx->index);
    granted = scopes & ~taken;
    if( row )
    {
        /* Idempotent restatement, widened by whatever is still free. An added
         * part keeps the anchor it was introduced with: re-anchoring is a
         * different part, not the same one said again. */
        row->scopes |= granted;
        row->needs_declare = 1;
        return row->scopes & scopes;
    }
    if( !granted )
        return 0;

    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
        if( host->chrome_claims[i].plugin < 0 )
        {
            free_row = i;
            break;
        }
    if( free_row < 0 )
    {
        TORIRS_LOG("plugin: %s could not claim '%s'; the claim table is full (%d)\n",
            ctx->name,
            part,
            TORIRS_PLUGIN_CHROME_CLAIMS_MAX);
        return -1;
    }

    row = &host->chrome_claims[free_row];
    memset(row, 0, sizeof(*row));
    row->plugin = ctx->index;
    row->scopes = granted;
    snprintf(row->part, sizeof(row->part), "%s", part);
    row->entity = plugin_entity_parse(part, NULL, NULL, NULL, NULL) != 0;
    row->element_id = -1;
    if( anchor )
    {
        snprintf(row->anchor, sizeof(row->anchor), "%s", anchor);
        row->added = 1;
    }
    row->needs_declare = 1;
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
        row->art.art[i] = -1;
    PluginHost_LayoutChanged(host);
    return granted;
}

static int
api_chrome_claim(struct ToriRS_PluginCtx* ctx, char const* part, int scopes, int enabled)
{
    struct ToriRS_PluginChromePart probe;
    int state;
    int result;

    assert(ctx);
    assert(part);

    if( !enabled )
        return plugin_chrome_claim_set(ctx, part, NULL, scopes, 0);

    /*
     * An ENTITY part skips the existence probe: the thing it names comes and
     * goes with the scene, and a claim on a slot that has not spawned yet is
     * the ordinary way to claim it. POSITION is the server's and is refused
     * out loud. A malformed entity name is -1, as any part nothing has is.
     */
    if( strchr(part, ':') )
    {
        if( !plugin_entity_parse(part, NULL, NULL, NULL, NULL) )
            return -1;
        if( scopes & TORIRS_PLUGIN_CHROME_SCOPE_POSITION )
        {
            TORIRS_LOG("plugin: %s asked to move '%s'; where an entity is is the server's\n",
                ctx->name,
                part);
            scopes &= ~TORIRS_PLUGIN_CHROME_SCOPE_POSITION;
        }
        result = plugin_chrome_claim_set(ctx, part, NULL, scopes, 1);
        return result < 0 ? 0 : result;
    }

    /*
     * "Does anything provide this" is asked BEFORE the table, so a part no
     * revision has answers -1 without leaving a row behind. A claim on
     * nothing would stand for ever, be reported as held, and stop the plugin
     * that later adds one for real.
     *
     * A part somebody else CLAIMED counts as provided even when the frame
     * cannot resolve it this instant -- the gameframe is between rebuilds,
     * the anchor is off screen -- which is what keeps ownership from flapping.
     */
    if( plugin_chrome_held(ctx->host, part, TORIRS_PLUGIN_CHROME_SCOPE_ALL, -1) == 0 &&
        !plugin_chrome_base(ctx->host, part, &probe, &state) )
        return -1;

    /*
     * POSITION on a LANE part is refused out loud. This tier can hide a
     * native node and draw over it; it cannot MOVE one, and a claim that said
     * yes and moved nothing would be exactly the silent no-op the contract
     * forbids. Asked of the base and not the composed part, because what
     * matters is what is underneath.
     */
    if( (scopes & TORIRS_PLUGIN_CHROME_SCOPE_POSITION) &&
        plugin_chrome_base(ctx->host, part, &probe, &state) &&
        probe.source == TORIRS_PLUGIN_CHROME_SOURCE_LANE )
    {
        TORIRS_LOG(
            "plugin: %s asked to move '%s', which this lane draws itself; position refused\n",
            ctx->name,
            part);
        scopes &= ~TORIRS_PLUGIN_CHROME_SCOPE_POSITION;
    }

    result = plugin_chrome_claim_set(ctx, part, NULL, scopes, 1);
    return result < 0 ? 0 : result;
}

static int
api_chrome_add(
    struct ToriRS_PluginCtx* ctx,
    char const* part,
    char const* anchor,
    int place,
    struct ToriRS_PluginChromePart const* initial)
{
    struct ToriRS_PluginHost* host = ctx->host;
    struct ToriRS_PluginChromePart probe;
    int state;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int result;

    assert(ctx);
    assert(part);
    assert(anchor);

    if( anchor[0] == '\0' || strlen(anchor) >= TORIRS_PLUGIN_ROLE_NAME_MAX )
        return -1;
    /*
     * A name this revision already HAS is not a name to add: it is one to
     * claim. Adding would shadow a real node with a plugin's own picture and
     * leave the node underneath still drawing -- and it is the migration path
     * that matters here, because the day a profile binds `orb_hitpoints` to
     * interface 160 this call has to start meaning "claim it" without the
     * plugin changing a line. @see chrome_add.
     */
    if( plugin_chrome_held(host, part, TORIRS_PLUGIN_CHROME_SCOPE_ALL, -1) ||
        plugin_chrome_base(host, part, &probe, &state) )
        return api_chrome_claim(ctx, part, TORIRS_PLUGIN_CHROME_SCOPE_ALL, 1);
    /* An anchor with no box is nowhere to hang anything. */
    if( !host->engine.role_rect(host->engine.user, anchor, &x, &y, &w, &h) )
        return -1;

    result = plugin_chrome_claim_set(ctx, part, anchor, TORIRS_PLUGIN_CHROME_SCOPE_ALL, 1);
    if( result > 0 )
    {
        struct PluginChromeClaim* row = plugin_chrome_row(host, ctx->index, part);
        assert(row);
        row->place = place == TORIRS_PLUGIN_ANCHOR_BEFORE ? TORIRS_PLUGIN_ANCHOR_BEFORE
                                                          : TORIRS_PLUGIN_ANCHOR_AFTER;
    }
    if( result > 0 && initial )
    {
        struct PluginChromeClaim* row = plugin_chrome_row(host, ctx->index, part);
        assert(row);
        if( initial->w > 0 && initial->h > 0 )
        {
            row->art = *initial;
            row->declared = 1;
        }
    }
    return result < 0 ? -1 : result;
}

static char const*
api_chrome_owner(struct ToriRS_PluginCtx* ctx, char const* part, int scope)
{
    struct PluginChromeClaim const* row;

    assert(ctx);
    assert(part);
    row = plugin_chrome_holder(ctx->host, part, scope);
    if( !row )
        return NULL;
    /* The TITLE and not the name: this is the half of a report a person reads,
     * and "OSRS Gameframe" is what they switched on. */
    return ctx->host->plugins[row->plugin].title;
}

static int
api_chrome_claimed(struct ToriRS_PluginCtx* ctx, char const* part, int scopes)
{
    assert(ctx);
    assert(part);
    return plugin_chrome_held(ctx->host, part, scopes, ctx->index);
}

static int
api_chrome_part(
    struct ToriRS_PluginCtx* ctx,
    char const* part,
    struct ToriRS_PluginChromePart* out)
{
    struct ToriRS_PluginChromePart found;

    assert(ctx);
    assert(part);
    assert(out);

    if( !plugin_chrome_resolve(ctx->host, part, ctx->index, &found, NULL, NULL) )
        return 0;
    *out = found;
    return 1;
}

static int
api_chrome_paint(
    struct ToriRS_PluginCtx* ctx,
    char const* part,
    struct ToriRS_PluginChromePart const* art)
{
    struct PluginChromeClaim* row;

    assert(ctx);
    assert(part);
    assert(art);
    assert(
        ctx->host->chrome_declaring && ctx->host->chrome_declarer == ctx->index &&
        "chrome_paint is legal only inside EV_CHROME");

    row = plugin_chrome_row(ctx->host, ctx->index, part);
    if( !row )
        return 0;
    /*
     * A non-positive box from a POSITION holder is refused and said out loud,
     * because it is arithmetic gone wrong rather than a small part. The lesson
     * is next door: a layout declared against a zero canvas does not produce a
     * small frame, it produces one at negative coordinates, drawn and
     * invisible. A holder of the other scopes has no box to give.
     */
    if( (row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_POSITION) && (art->w <= 0 || art->h <= 0) )
    {
        TORIRS_LOG("plugin: %s painted '%s' at %dx%d; nothing that size is drawn\n",
            ctx->name,
            part,
            art->w,
            art->h);
        return 0;
    }

    /* A POSITION that moved wants a layout pass behind it, because on a FRAME
     * part the host re-places the member there and the arranger's other
     * regions have to hear about it. @see PluginHost_Layout. */
    if( (row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_POSITION) &&
        (row->art.x != art->x || row->art.y != art->y || row->art.w != art->w ||
         row->art.h != art->h) )
        row->moved = 1;

    row->art = *art;
    row->declared = 1;
    return 1;
}

static int
api_chrome_ops(
    struct ToriRS_PluginCtx* ctx,
    char const* part,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    struct PluginChromeClaim* row;

    assert(ctx);
    assert(part);
    assert(
        ctx->host->chrome_declaring && ctx->host->chrome_declarer == ctx->index &&
        "chrome_ops is legal only inside EV_CHROME");

    row = plugin_chrome_row(ctx->host, ctx->index, part);
    if( !row || !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_HITBOX) )
        return 0;

    if( op_count < 0 )
        op_count = 0;
    if( op_count > TORIRS_PLUGIN_REGION_OPS_MAX )
        op_count = TORIRS_PLUGIN_REGION_OPS_MAX;
    row->op_count = 0;
    for( int i = 0; i < op_count; i++ )
    {
        /* Empty rows are skipped rather than kept, so a caller with a
         * fixed-size table need not compact it -- hit_region's own rule. */
        if( !ops || !ops[i] || ops[i][0] == '\0' )
            continue;
        snprintf(row->ops[row->op_count], sizeof(row->ops[0]), "%s", ops[i]);
        row->op_count++;
    }
    row->tag = tag;
    row->has_ops = 1;
    return 1;
}

static int
api_chrome_state(struct ToriRS_PluginCtx* ctx, char const* part, int state)
{
    struct PluginChromeClaim* row;

    assert(ctx);
    assert(part);

    if( state < 0 || state >= TORIRS_PLUGIN_CHROME_STATE_COUNT )
        return 0;
    row = plugin_chrome_row(ctx->host, ctx->index, part);
    if( !row || !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE) )
        return 0;
    row->state = (uint8_t)state;
    return 1;
}

static int
api_layout_slot_art(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int member,
    struct ToriRS_PluginChromePart const* part)
{
    struct ToriRS_PluginHost* host = ctx->host;
    struct PluginSlotArt* row;

    assert(ctx);
    assert(
        host->layout_declaring && "layout_slot_art is legal only inside EV_LAYOUT");
    assert(host->layout_owner == ctx->index);

    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;

    row = plugin_slot_art_find(host, slot, member);
    if( !part )
    {
        if( row )
            memset(row, 0, sizeof(*row));
        return 0;
    }
    if( part->w <= 0 || part->h <= 0 )
    {
        TORIRS_LOG("plugin: %s declared slot %d member %d art at %dx%d; not drawn\n",
            ctx->name,
            slot,
            member,
            part->w,
            part->h);
        return 0;
    }

    if( !row )
        for( int i = 0; i < TORIRS_PLUGIN_SLOT_ART_MAX; i++ )
            if( !host->slot_art[i].used )
            {
                row = &host->slot_art[i];
                break;
            }
    if( !row )
    {
        TORIRS_LOG("plugin: %s declared more than %d parts; the rest are not drawn\n",
            ctx->name,
            TORIRS_PLUGIN_SLOT_ART_MAX);
        return 0;
    }

    /* The state SURVIVES the re-declaration: it was set by layout_slot_state
     * at some earlier tick and the arranger has not been asked to say it
     * again, any more than a claimant is. */
    {
        uint8_t const state = row->used ? row->state : (uint8_t)TORIRS_PLUGIN_CHROME_IDLE;
        row->used = 1;
        row->slot = (uint8_t)slot;
        row->member = member;
        row->art = *part;
        row->state = state;
    }

    /* The same "does this frame have one" answer layout_slot_at gives, asked
     * the same way, so an arranger can place and dress in one breath and get
     * one truth back from both. */
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        return member < 0
                   ? host->engine.slot_rect(host->engine.user, slot, &x, &y, &w, &h)
                   : host->engine.slot_member_rect(
                         host->engine.user, slot, member, &x, &y, &w, &h);
    }
}

/**
 * The role name this revision gives a member, or NULL when it has none.
 *
 * A linear pass over the claims, because the only reverse lookup that MATTERS
 * is "does some claim name this member" -- a member with no name has no claim
 * and needs none.
 */
static char const*
plugin_chrome_slot_name(struct ToriRS_PluginHost* host, int slot, int member)
{
    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* row = &host->chrome_claims[i];
        int row_slot = -1;
        int row_member = -1;

        if( row->plugin < 0 || row->added )
            continue;
        if( !host->engine.role_slot(host->engine.user, row->part, &row_slot, &row_member) )
            continue;
        if( row_slot == slot && row_member == member )
            return row->part;
    }
    return NULL;
}

static int
api_layout_slot_claimed(struct ToriRS_PluginCtx* ctx, int slot, int member, int scopes)
{
    char const* name;

    assert(ctx);
    assert(ctx->host->layout_owner == ctx->index);
    name = plugin_chrome_slot_name(ctx->host, slot, member);
    return name ? plugin_chrome_held(ctx->host, name, scopes, ctx->index) : 0;
}

static int
api_layout_slot_state(struct ToriRS_PluginCtx* ctx, int slot, int member, int state)
{
    struct PluginSlotArt* row;

    assert(ctx);
    assert(ctx->host->layout_owner == ctx->index);
    if( state < 0 || state >= TORIRS_PLUGIN_CHROME_STATE_COUNT )
        return 0;
    row = plugin_slot_art_find(ctx->host, slot, member);
    if( !row )
        return 0;
    row->state = (uint8_t)state;
    return 1;
}

/** Drop everything one plugin holds in this tier. @see plugin_teardown. */
static void
plugin_chrome_drop_plugin(struct ToriRS_PluginHost* host, int plugin)
{
    int dropped = 0;

    assert(host);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        if( row->plugin != plugin )
            continue;
        if( row->lane_replaced )
            (void)host->engine.role_replace(host->engine.user, plugin, row->part, 0);
        memset(row, 0, sizeof(*row));
        row->plugin = -1;
        dropped = 1;
    }
    plugin_chrome_borrows_drop_plugin(host, plugin);
    if( dropped )
        PluginHost_LayoutChanged(host);
}

/**
 * Which picture a part is wearing right now.
 *
 * The two halves of the choice come from different places and neither can
 * derive the other's: the holder said whether the part is SELECTED
 * (chrome_state / layout_slot_state), and the host knows whether the pointer
 * is on it. A state whose handle is -1 falls back to IDLE, so a part with one
 * picture never has to think about any of this.
 */
static int
plugin_chrome_pick_art(
    struct ToriRS_PluginChromePart const* part, int state, int hovered)
{
    int wanted;

    assert(part);
    if( state == TORIRS_PLUGIN_CHROME_ACTIVE ||
        state == TORIRS_PLUGIN_CHROME_ACTIVE_HOVER )
        wanted = hovered ? TORIRS_PLUGIN_CHROME_ACTIVE_HOVER
                         : TORIRS_PLUGIN_CHROME_ACTIVE;
    else if( state == TORIRS_PLUGIN_CHROME_DISABLED )
        wanted = TORIRS_PLUGIN_CHROME_DISABLED;
    else
        wanted = hovered ? TORIRS_PLUGIN_CHROME_HOVER : TORIRS_PLUGIN_CHROME_IDLE;

    if( part->art[wanted] >= 0 )
        return part->art[wanted];
    /* One step back before IDLE, so a part with ACTIVE but no ACTIVE_HOVER
     * stays selected under the pointer instead of appearing to deselect. */
    if( wanted == TORIRS_PLUGIN_CHROME_ACTIVE_HOVER &&
        part->art[TORIRS_PLUGIN_CHROME_ACTIVE] >= 0 )
        return part->art[TORIRS_PLUGIN_CHROME_ACTIVE];
    return part->art[TORIRS_PLUGIN_CHROME_IDLE];
}

/** Blit one resolved part and, when a HITBOX holder declared verbs, claim the
 *  rectangle for them. `ops` may be NULL for a part whose click is not a
 *  plugin's. */
static void
plugin_chrome_paint_one(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginChromePart const* part,
    int state,
    int have_mouse,
    int mx,
    int my,
    struct PluginChromeClaim const* ops)
{
    struct PluginImage const* slot;
    int hovered;
    int image;

    assert(host);
    assert(part);

    hovered = have_mouse && mx >= part->x && mx < part->x + part->w && my >= part->y &&
              my < part->y + part->h;
    image = plugin_chrome_pick_art(part, state, hovered);

    if( image >= 0 && image < TORIRS_PLUGIN_IMAGES_MAX )
    {
        slot = &host->images[image];
        /* Pending art draws nothing and is not an error: the part keeps its
         * box, the region below is still claimed, and the picture appears on
         * the frame after the read lands. */
        if( slot->plugin >= 0 && slot->published )
            (void)host->engine.draw_image(
                host->engine.user,
                image,
                part->x,
                part->y,
                slot->width,
                slot->height,
                part->x,
                part->y,
                part->w,
                part->h,
                0);
    }

    if( ops && ops->has_ops )
    {
        char const* verbs[TORIRS_PLUGIN_REGION_OPS_MAX];
        for( int i = 0; i < ops->op_count; i++ )
            verbs[i] = ops->ops[i];
        (void)host->engine.hit_region(
            host->engine.user,
            ops->plugin,
            part->x,
            part->y,
            part->w,
            part->h,
            ops->op_count ? verbs : NULL,
            ops->op_count,
            ops->tag);
    }
}

/* ----------------------------------------------------------------- entities */

/*
 * The entity half of the tier. @see the "Entities" section of the contract.
 *
 * Same table, same rows, same arbitration; what is different is RESOLUTION.
 * A chrome part resolves to a box through the role table; an entity part
 * resolves to a scene ELEMENT through the snapshot walks, once per world
 * frame, and the element is what draw_hull and the pick set speak in.
 */

/** Parse `<kind>:<a>[,<b>,<c>,<d>]`. @return the kind, or 0 for a name that
 *  is not an entity's -- which is every chrome part's, and is not an error. */
static int
plugin_entity_parse(char const* part, int* out_a, int* out_b, int* out_c, int* out_d)
{
    int kind;
    char const* rest;
    int n[4] = { 0, 0, 0, 0 };
    int want;

    assert(part);
    if( strncmp(part, "npc:", 4) == 0 )
    {
        kind = TORIRS_PLUGIN_ENTITY_NPC;
        rest = part + 4;
        want = 1;
    }
    else if( strncmp(part, "player:", 7) == 0 )
    {
        kind = TORIRS_PLUGIN_ENTITY_PLAYER;
        rest = part + 7;
        want = 1;
    }
    else if( strncmp(part, "loc:", 4) == 0 )
    {
        kind = TORIRS_PLUGIN_ENTITY_LOC;
        rest = part + 4;
        want = 4;
    }
    else if( strncmp(part, "obj:", 4) == 0 )
    {
        kind = TORIRS_PLUGIN_ENTITY_OBJ;
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
    struct ToriRS_PluginCtx* ctx, int kind, int a, int b, int c, int d, char* buf, int cap)
{
    int n;

    (void)ctx;
    assert(buf);
    switch( kind )
    {
    case TORIRS_PLUGIN_ENTITY_NPC:
        n = snprintf(buf, (size_t)cap, "npc:%d", a);
        break;
    case TORIRS_PLUGIN_ENTITY_PLAYER:
        n = snprintf(buf, (size_t)cap, "player:%d", a);
        break;
    case TORIRS_PLUGIN_ENTITY_LOC:
        n = snprintf(buf, (size_t)cap, "loc:%d,%d,%d,%d", a, b, c, d);
        break;
    case TORIRS_PLUGIN_ENTITY_OBJ:
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
plugin_entity_element(struct ToriRS_PluginHost* host, char const* part)
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
    case TORIRS_PLUGIN_ENTITY_NPC:
    {
        struct ToriRS_PluginNpcSnap snap;
        return host->engine.npc_by_slot(host->engine.user, a, &snap) ? snap.element_id : -1;
    }
    case TORIRS_PLUGIN_ENTITY_PLAYER:
    {
        struct ToriRS_PluginPlayerSnap snap;
        int iter = -1;
        if( host->engine.local_player(host->engine.user, &snap) && snap.server_pid == a )
            return snap.element_id;
        while( (iter = host->engine.player_next(host->engine.user, iter, &snap)) >= 0 )
            if( snap.server_pid == a )
                return snap.element_id;
        return -1;
    }
    case TORIRS_PLUGIN_ENTITY_LOC:
    {
        struct ToriRS_PluginLocSnap snap;
        int iter = -1;
        while( (iter = host->engine.loc_next(host->engine.user, iter, &snap)) >= 0 )
            if( snap.tile_x == a && snap.tile_z == b && snap.level == c && snap.loc_id == d )
                return snap.element_id;
        return -1;
    }
    case TORIRS_PLUGIN_ENTITY_OBJ:
    {
        struct ToriRS_PluginObjSnap snap;
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
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        if( row->plugin < 0 || !row->entity )
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
plugin_entity_hull_allowed(struct ToriRS_PluginHost* host, int plugin, int element_id)
{
    assert(host);
    if( element_id < 0 )
        return 1;
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* row = &host->chrome_claims[i];
        if( row->plugin < 0 || !row->entity || row->element_id != element_id )
            continue;
        if( (row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE) && row->plugin != plugin )
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
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* row = &host->chrome_claims[i];
        if( row->plugin < 0 || !row->entity || row->element_id < 0 )
            continue;
        if( !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE) || !row->look.hull )
            continue;
        if( !(host->plugins[row->plugin].enabled && host->plugins[row->plugin].running) )
            continue;
        (void)host->engine.draw_hull(
            host->engine.user, row->element_id, row->look.rgb, row->look.fill_alpha, row->look.shape);
    }
}

static int
api_entity_look(
    struct ToriRS_PluginCtx* ctx, char const* part, struct ToriRS_PluginEntityLook const* look)
{
    struct PluginChromeClaim* row;

    assert(ctx);
    assert(part);
    assert(look);
    assert(
        look->shape == TORIRS_PLUGIN_HULL_BOUNDS || look->shape == TORIRS_PLUGIN_HULL_MESH);

    row = plugin_chrome_row(ctx->host, ctx->index, part);
    if( !row || !row->entity || !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE) )
        return 0;
    row->look = *look;
    return 1;
}

static int
api_entity_ops(
    struct ToriRS_PluginCtx* ctx,
    char const* part,
    int mode,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    struct PluginChromeClaim* row;

    assert(ctx);
    assert(part);

    if( mode < TORIRS_PLUGIN_ENTITY_OPS_APPEND || mode > TORIRS_PLUGIN_ENTITY_OPS_NONE )
        return 0;
    row = plugin_chrome_row(ctx->host, ctx->index, part);
    if( !row || !row->entity || !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_HITBOX) )
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
static struct PluginChromeClaim const*
plugin_entity_row_holder(
    struct ToriRS_PluginHost* host,
    struct ToriRS_PluginMenuRow const* row,
    struct ToriRS_PluginHoverEntity const* hover)
{
    assert(host);
    assert(row);
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* claim = &host->chrome_claims[i];
        int a;
        int b;
        int c;
        int d;
        int kind;

        if( claim->plugin < 0 || !claim->entity || !claim->has_ops )
            continue;
        if( !(claim->scopes & TORIRS_PLUGIN_CHROME_SCOPE_HITBOX) )
            continue;
        if( !(host->plugins[claim->plugin].enabled && host->plugins[claim->plugin].running) )
            continue;
        kind = plugin_entity_parse(claim->part, &a, &b, &c, &d);
        switch( kind )
        {
        case TORIRS_PLUGIN_ENTITY_NPC:
            if( row->pick_kind == UI_MINIMENU_PICK_NPC && row->npc_slot == a )
                return claim;
            break;
        case TORIRS_PLUGIN_ENTITY_PLAYER:
            if( row->pick_kind == UI_MINIMENU_PICK_PLAYER && row->player_pid == a )
                return claim;
            break;
        case TORIRS_PLUGIN_ENTITY_LOC:
            if( row->pick_kind == UI_MINIMENU_PICK_SCENERY && row->target_id == d && hover &&
                hover->kind == TORIRS_PLUGIN_HOVER_SCENERY && hover->tile_x == a &&
                hover->tile_z == b && hover->level == c )
                return claim;
            break;
        case TORIRS_PLUGIN_ENTITY_OBJ:
            if( row->pick_kind == UI_MINIMENU_PICK_OBJ && row->target_id == d && hover &&
                hover->kind == TORIRS_PLUGIN_HOVER_OBJ && hover->tile_x == a &&
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
 * own. Runs AFTER the plugins' EV_MENU_BUILD, so a dropped row's text was
 * never handed to anybody stale.
 */
static void
plugin_entity_apply_ops(
    struct ToriRS_PluginHost* host, void* cursor, struct ToriRS_PluginEvMenuBuild const* menu)
{
    struct ToriRS_PluginHoverEntity hover;
    int have_hover;
    struct PluginChromeClaim const* holders[TORIRS_PLUGIN_MENU_ROWS_MAX];
    int holder_count = 0;

    assert(host);
    assert(cursor);
    assert(menu);

    have_hover = host->engine.hover_entity(host->engine.user, &hover);

    /* Highest index first, so each drop leaves every lower index true. */
    for( int i = menu->row_count - 1; i >= 0; i-- )
    {
        struct PluginChromeClaim const* holder =
            plugin_entity_row_holder(host, &menu->rows[i], have_hover ? &hover : NULL);
        int seen = 0;

        if( !holder )
            continue;
        for( int j = 0; j < holder_count; j++ )
            if( holders[j] == holder )
                seen = 1;
        if( !seen && holder_count < TORIRS_PLUGIN_MENU_ROWS_MAX )
            holders[holder_count++] = holder;
        if( holder->ops_mode != TORIRS_PLUGIN_ENTITY_OPS_APPEND )
            (void)host->engine.menu_drop(host->engine.user, cursor, i);
    }

    for( int h = 0; h < holder_count; h++ )
    {
        struct PluginChromeClaim const* holder = holders[h];
        if( holder->ops_mode == TORIRS_PLUGIN_ENTITY_OPS_NONE )
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
api_layout_slot_skin(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int art,
    int mask)
{
    assert(ctx);
    assert(
        ctx->host->layout_declaring &&
        "layout_slot_skin is legal only inside EV_LAYOUT");
    assert(ctx->host->layout_owner == ctx->index);
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;
    if( slot != TORIRS_PLUGIN_SLOT_MINIMAP && slot != TORIRS_PLUGIN_SLOT_COMPASS )
        return 0;
    /* The minimap picture is the live baked world; only its cut-out belongs to
     * the frame. Compass art is static frame art and is replaceable. */
    if( slot == TORIRS_PLUGIN_SLOT_MINIMAP && art >= 0 )
        return 0;
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
            return 0;
    }
    if( mask >= 0 )
    {
        struct PluginImage const* image = plugin_image_owned(ctx, mask);
        if( !image || !image->published )
            return 0;
    }
    return ctx->host->engine.layout_slot_skin(ctx->host->engine.user, slot, art, mask);
}

static int
api_layout_slot_overlay(
    struct ToriRS_PluginCtx* ctx,
    int slot,
    int image,
    int x,
    int y,
    int trans)
{
    struct PluginImage const* owned;

    assert(ctx);
    assert(
        ctx->host->layout_declaring &&
        "layout_slot_overlay is legal only inside EV_LAYOUT");
    assert(ctx->host->layout_owner == ctx->index);
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT )
        return 0;
    if( trans < 0 || trans > 255 )
        return 0;

    /* Unlike a skin, this is an ordinary sprite scene. Its natural dimensions
     * are read by the renderer after publication, so retaining a valid pending
     * handle is both safe and necessary: otherwise a startup EV_LAYOUT would
     * discard the declaration and the housing would not appear until resize. */
    owned = plugin_image_owned(ctx, image);
    if( !owned )
        return 0;
    return ctx->host->engine.layout_slot_overlay(
        ctx->host->engine.user, slot, image, x, y, trans);
}

static int
api_layout_scrollbar(
    struct ToriRS_PluginCtx* ctx,
    int trough,
    int dragger_top,
    int dragger_mid,
    int dragger_bottom,
    int arrow_up,
    int arrow_down)
{
    int images[6];

    assert(ctx);
    assert(
        ctx->host->layout_declaring &&
        "layout_scrollbar is legal only inside EV_LAYOUT");
    assert(ctx->host->layout_owner == ctx->index);

    images[0] = trough;
    images[1] = dragger_top;
    images[2] = dragger_mid;
    images[3] = dragger_bottom;
    images[4] = arrow_up;
    images[5] = arrow_down;
    for( int i = 0; i < 6; i++ )
    {
        struct PluginImage const* image;
        /* Any piece missing clears the whole skin, which is what a layout with
         * no scrollbar art asks for by passing -1 -- and also what a layout
         * whose art has not crossed the IO queue yet gets, until it has. */
        if( images[i] < 0 )
            return ctx->host->engine.layout_scrollbar(ctx->host->engine.user, NULL, 0);
        image = plugin_image_owned(ctx, images[i]);
        if( !image || !image->published )
            return ctx->host->engine.layout_scrollbar(ctx->host->engine.user, NULL, 0);
    }
    return ctx->host->engine.layout_scrollbar(
        ctx->host->engine.user, images, 6);
}

static void
api_draw_image(
    struct ToriRS_PluginCtx* ctx,
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
    /* Not resident yet is the ORDINARY state for the first frames after a
     * load, so it draws nothing rather than asserting. A handle this plugin
     * neither owns nor has borrowed is the other thing plugin_image_readable
     * answers with NULL. */
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
    struct ToriRS_PluginCtx* ctx,
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
     * its tab stones through EV_DRAW_FRAME and they have to be clickable, so
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
api_text_input(struct ToriRS_PluginCtx* ctx, int on)
{
    assert(ctx);
    if( !ctx->host->engine.text_input )
        return;
    ctx->host->engine.text_input(ctx->host->engine.user, on ? 1 : 0);
}

static void
api_chat_focus(struct ToriRS_PluginCtx* ctx, int on)
{
    assert(ctx);
    if( !ctx->host->engine.chat_focus )
        return;
    ctx->host->engine.chat_focus(ctx->host->engine.user, on ? 1 : 0);
}

static int
api_if_click(struct ToriRS_PluginCtx* ctx, int component_id, int op)
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
    assert(engine->layout_set);
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
    assert(engine->mesh_clear);
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
    assert(engine->component_rect);
    assert(engine->role_rect);
    assert(engine->role_visible);
    assert(engine->role_click);
    assert(engine->role_id);
    assert(engine->role_slot);
    assert(engine->menu_drop);
    assert(engine->role_replace);
    assert(engine->role_anchor);
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
    /* Same trap as the image slots below: 0 is a plugin index, so a calloc'd
     * owner would mean "the first plugin registered owns the gameframe" and
     * every lane would boot with its own chrome suppressed. */
    host->layout_owner = -1;
    /* 0 is a real plugin index, so an empty reservation row needs a value of
     * its own rather than the calloc's zero. */
    for( int i = 0; i < TORIRS_PLUGIN_RESERVES_MAX; i++ )
        host->reserves[i].plugin = -1;
    for( int i = 0; i < TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX; i++ )
        host->role_replacements[i].plugin = -1;
    /* Same reasoning again for the chrome tier's two tables. */
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
        host->chrome_claims[i].plugin = -1;
    for( int i = 0; i < (int)(sizeof(host->chrome_borrows) /
                              sizeof(host->chrome_borrows[0])); i++ )
        host->chrome_borrows[i].borrower = -1;
    host->chrome_declarer = -1;
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

    struct ToriRS_PluginApi api = {
        .abi_version = TORIRS_PLUGIN_ABI,
        .subscribe = api_subscribe,
        .log = api_log,
        .notify = api_notify,
        .screen = api_screen,
        .safe_os = api_safe_os,
        .world_cycle = api_world_cycle,
        .frame_ms = api_frame_ms,
        .frame_work_us = api_frame_work_us,
        .local_player = api_local_player,
        .npc_next = api_npc_next,
        .npc_by_slot = api_npc_by_slot,
        .player_next = api_player_next,
        .obj_next = api_obj_next,
        .key_held = api_key_held,
        .loc_next = api_loc_next,
        .highlight_next = api_highlight_next,
        .hover_tile = api_hover_tile,
        .hover_entity = api_hover_entity,
        .element_height = api_element_height,
        .mouse_pos = api_mouse_pos,
        .slot_rect = api_slot_rect,
        .slot_member_rect = api_slot_member_rect,
        .component_rect = api_component_rect,
        .role_rect = api_role_rect,
        .role_visible = api_role_visible,
        .role_click = api_role_click,
        .role_id = api_role_id,
        .layout_reserve = api_layout_reserve,
        .layout_revision = api_layout_revision,
        .layout_slot_art = api_layout_slot_art,
        .chrome_claim = api_chrome_claim,
        .chrome_add = api_chrome_add,
        .chrome_owner = api_chrome_owner,
        .chrome_claimed = api_chrome_claimed,
        .chrome_part = api_chrome_part,
        .chrome_paint = api_chrome_paint,
        .chrome_ops = api_chrome_ops,
        .chrome_state = api_chrome_state,
        .layout_slot_claimed = api_layout_slot_claimed,
        .layout_slot_state = api_layout_slot_state,
        .entity_part = api_entity_part,
        .entity_look = api_entity_look,
        .entity_ops = api_entity_ops,
        .layout_claim = api_layout_claim,
        .layout_release = api_layout_release,
        .layout_owned = api_layout_owned,
        .layout_slot = api_layout_slot,
        .layout_slot_at = api_layout_slot_at,
        .layout_slot_skin = api_layout_slot_skin,
        .layout_scrollbar = api_layout_scrollbar,
        .tab_active = api_tab_active,
        .tab_select = api_tab_select,
        .tab_enabled = api_tab_enabled,
        .stat = api_stat,
        .stat_xp = api_stat_xp,
        .skill_name = api_skill_name,
        .run_energy = api_run_energy,
        .project = api_project,
        .cfg_bool = api_cfg_bool,
        .cfg_int = api_cfg_int,
        .cfg_color = api_cfg_color,
        .cfg_str = api_cfg_str,
        .cfg_has = api_cfg_has,
        .cfg_set = api_cfg_set,
        .feature_next = api_feature_next,
        .feature_get = api_feature_get,
        .feature_set = api_feature_set,
        .display_setting = api_display_setting,
        .display_setting_set = api_display_setting_set,
        .varbit = api_varbit,
        .varp = api_varp,
        .cache_id = api_cache_id,
        .lane = api_lane,
        .disable_self = api_disable_self,
        .obj_info = api_obj_info,
        .inv_slot = api_inv_slot,
        .inv_size = api_inv_size,
        .setting_color = api_setting_color,
        .menu_add = api_menu_add,
        .draw_tile = api_draw_tile,
        .draw_hull = api_draw_hull,
        .draw_line = api_draw_line,
        .draw_text = api_draw_text,
        .draw_rect = api_draw_rect,
        .image_load = api_image_load,
        .image_compose = api_image_compose,
        .image_pixels = api_image_pixels,
        .image_size = api_image_size,
        .image_release = api_image_release,
        .draw_image = api_draw_image,
        .hit_region = api_hit_region,
        .if_click = api_if_click,
        .text_input = api_text_input,
        .chat_focus = api_chat_focus,
        .asset_load = api_asset_load,
        .asset_data = api_asset_data,
        .asset_save = api_asset_save,
        .asset_release = api_asset_release,
        .screenshot = api_screenshot,
        .datestamp = api_datestamp,
        .model_load = api_model_load,
        .mesh_create = api_mesh_create,
        .mesh_destroy = api_mesh_destroy,
        .mesh_clear = api_mesh_clear,
        .mesh_vertex = api_mesh_vertex,
        .mesh_face = api_mesh_face,
        .object_create = api_object_create,
        .object_destroy = api_object_destroy,
        .object_set_model = api_object_set_model,
        .object_recolor = api_object_recolor,
        .object_clear_recolors = api_object_clear_recolors,
        .object_set_anim = api_object_set_anim,
        .object_set_light = api_object_set_light,
        .object_set_position = api_object_set_position,
        .object_set_active = api_object_set_active,
        .object_ready = api_object_ready,
        .hsl_from_rgb = api_hsl_from_rgb,
        .hsl_to_rgb = api_hsl_to_rgb,
        .win_request = api_win_request,
        .win_widget = api_win_widget,
        .win_set_text = api_win_set_text,
        .win_set_checked = api_win_set_checked,
        .win_set_options = api_win_set_options,
        .win_clear = api_win_clear,
        .layout_slot_overlay = api_layout_slot_overlay,
        .role_replace = api_role_replace,
        .role_anchor = api_role_anchor,
        .panel_request = api_panel_request,
        .panel_widget = api_panel_widget,
        .panel_set_text = api_panel_set_text,
        .panel_set_value = api_panel_set_value,
        .panel_set_height = api_panel_set_height,
        .panel_set_options = api_panel_set_options,
        .panel_set_badge = api_panel_set_badge,
        .panel_set_attention = api_panel_set_attention,
        .panel_clear = api_panel_clear,
        .panel_invalidate = api_panel_invalidate,
        .obj_image = api_obj_image,
    };
    host->api = api;
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
        struct ToriRS_PluginCtx* ctx = &host->plugins[i];
        if( !ctx->running )
            continue;
        struct ToriRS_PluginEvFrame ev = { 0 };
        host->dispatching = i;
        for( int s = 0; s < host->sub_count[TORIRS_PLUGIN_EV_STOP]; s++ )
        {
            struct PluginSub const* sub = &host->subs[TORIRS_PLUGIN_EV_STOP][s];
            if( sub->plugin == i )
                sub->handler(ctx, &ev, sub->userdata);
        }
        host->dispatching = -1;
        if( ctx->def->shutdown )
            ctx->def->shutdown(ctx);
        role_replacements_drop_plugin(host, i);
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
plugin_title_refresh(struct ToriRS_PluginCtx* ctx)
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

int
PluginHost_Register(struct ToriRS_PluginHost* host, struct ToriRS_PluginDef const* def)
{
    assert(host);
    assert(def);
    assert(def->name);

    if( host->plugin_count >= TORIRS_PLUGIN_MAX )
    {
        TORIRS_ERR("plugin: table full, refusing '%s'\n", def->name);
        return -1;
    }

    /* A name is an identity, not a label: it keys the settings section, the
     * panel row and the manifest entry. Two plugins sharing one would silently
     * share all three -- each overwriting the other's saved settings -- so the
     * second is refused rather than admitted to fight over them. */
    if( PluginHost_IndexOf(host, def->name) >= 0 )
    {
        TORIRS_ERR("plugin: '%s' is already registered; the second one is refused "
            "(names key the settings file, so they must be unique)\n",
            def->name);
        return -1;
    }

    /*
     * A name is an identity, not a label.
     *
     * It keys the ini section, the panel row and PluginHost_IndexOf, so two
     * plugins answering to one name share a config store and resolve to
     * whichever registered first -- a script would silently inherit a C
     * plugin's settings, and disabling one would disable the other. Refusing
     * is the only outcome that cannot be mistaken for working; merging them
     * quietly is what produces a bug report about settings that "reset
     * themselves".
     */
    if( PluginHost_IndexOf(host, def->name) >= 0 )
    {
        TORIRS_ERR("plugin: '%s' is already registered -- refusing the second one. "
            "Two plugins cannot share a name: it is the ini section and the "
            "panel row. Rename one.\n",
            def->name);
        return -1;
    }

    /*
     * A schema the store cannot hold is refused, not truncated.
     *
     * plugin_config_seed below skips the items that do not fit, and every
     * symptom of that is remote from the cause: the panel still lists the
     * dropped rows (they are counted off the schema, not the store), and the
     * plugin runs until the first read of one, which for a Lua script is a
     * "config key was never declared" error thrown out of whichever handler
     * happened to touch it first. Refusing here names the plugin and both
     * numbers instead.
     */
    {
        int schema_count = 0;
        if( def->config )
            while( def->config[schema_count].key )
                schema_count++;
        if( schema_count > TORIRS_PLUGIN_CONFIG_MAX )
        {
            TORIRS_ERR("plugin: '%s' declares %d config items; the store holds %d. "
                "Refusing it rather than dropping the last %d in silence.\n",
                def->name,
                schema_count,
                TORIRS_PLUGIN_CONFIG_MAX,
                schema_count - TORIRS_PLUGIN_CONFIG_MAX);
            return -1;
        }
    }

    int const index = host->plugin_count++;
    struct ToriRS_PluginCtx* ctx = &host->plugins[index];
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->def = def;
    ctx->index = index;
    ctx->enabled = !def->disabled_by_default;
    ctx->running = false;
    snprintf(ctx->name, sizeof(ctx->name), "%s", def->name);
    plugin_title_refresh(ctx);
    plugin_config_seed(ctx);
    return index;
}

void
PluginHost_Start(struct ToriRS_PluginHost* host)
{
    assert(host);

    for( int i = 0; i < host->plugin_count; i++ )
    {
        struct ToriRS_PluginCtx* ctx = &host->plugins[i];
        /* `refused` and not `enabled`, so a plugin that stood down on this
         * lane stays down for every later Start -- a script finishing its load
         * runs this again, and without the flag every refusal would be
         * reconsidered, re-taken and re-logged on each one. */
        if( ctx->running || !ctx->enabled || ctx->refused )
            continue;

        ctx->running = true;
        host->dispatching = i;
        if( ctx->def->init )
            ctx->def->init(ctx, &host->api);
        host->dispatching = -1;

        struct ToriRS_PluginEvFrame ev = { 0 };
        plugin_dispatch(host, TORIRS_PLUGIN_EV_START, &ev);
    }
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
plugin_teardown(struct ToriRS_PluginHost* host, int plugin_index)
{
    struct ToriRS_PluginCtx* ctx = plugin_at(host, plugin_index);

    if( ctx->tearing_down )
        return;
    ctx->tearing_down = true;

    /* Selection is released first: the plugin learns it became invisible
     * while its subscriptions and page model still exist, and no later STOP
     * callback can leave interactive chrome behind. */
    plugin_panel_unregister(host, plugin_index);

    if( ctx->running )
    {
        struct ToriRS_PluginEvFrame ev = { 0 };
        host->dispatching = plugin_index;
        for( int s = 0; s < host->sub_count[TORIRS_PLUGIN_EV_STOP]; s++ )
        {
            struct PluginSub const* sub = &host->subs[TORIRS_PLUGIN_EV_STOP][s];
            if( sub->plugin == plugin_index )
                sub->handler(ctx, &ev, sub->userdata);
        }
        host->dispatching = -1;
        if( ctx->def->shutdown )
            ctx->def->shutdown(ctx);
    }
    plugin_drop_subs(host, plugin_index);
    /* Geometry and bytes go out with the subscriptions, and for the same
     * reason: nothing is left running that could remove them later. */
    plugin_objects_destroy_all(host, ctx);
    plugin_meshes_destroy_all(host, ctx);
    plugin_assets_drop_plugin(host, plugin_index);
    plugin_obj_icons_drop_plugin(host, plugin_index);
    plugin_images_drop_plugin(host, plugin_index);
    /* Reservations go too, and that is what makes `reserve` safe to use: a
     * dock that is switched off gives its edge back without anybody asking,
     * and the readouts beside it widen on the next frame. */
    plugin_reserves_drop_plugin(host, plugin_index);
    plugin_models_drop_plugin(host, plugin_index);
    role_replacements_drop_plugin(host, plugin_index);
    /* And the chrome tier, in both directions: the parts this plugin was
     * dressing go back to whatever provides them underneath, and every borrow
     * to or from it goes with the images it pointed at. A plugin that degraded
     * because this one held a part does NOT silently reacquire it -- it
     * claimed nothing, so there is nothing to reinstate, and picking up parts
     * at a moment nobody can see is worse than a gap. */
    plugin_chrome_drop_plugin(host, plugin_index);
    /* The tab goes with them: a stopped plugin's controls would otherwise sit
     * in the window still taking clicks, dispatching to a plugin that is not
     * running and silently doing nothing. */
    /* The frame goes back to the lane. A layout plugin that stopped while
     * holding it would leave the client with the client's chrome suppressed
     * and nobody drawing any: a black surround and an inventory floating in
     * it. */
    if( host->layout_owner == plugin_index )
    {
        host->layout_claim_epoch++;
        host->layout_owner = -1;
        host->layout_canvas = TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW;
        host->layout_fixed_w = 0;
        host->layout_fixed_h = 0;
        plugin_layout_publish(host);
    }
    plugin_win_drop(host, plugin_index);
    if( host->win_tab[plugin_index] )
    {
        host->win_tab[plugin_index] = false;
        host->win_tab_title[plugin_index][0] = '\0';
        host->win_revision++;
    }
    ctx->running = false;
    ctx->tearing_down = false;
}

void
PluginHost_SetEnabled(struct ToriRS_PluginHost* host, int plugin_index, bool enabled)
{
    assert(host);

    struct ToriRS_PluginCtx* ctx = plugin_at(host, plugin_index);
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
    if( !enabled && ctx->def->essential )
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
PluginHost_Reload(struct ToriRS_PluginHost* host, int plugin_index)
{
    struct ToriRS_PluginCtx* ctx;
    bool panel_was_selected;

    assert(host);
    ctx = plugin_at(host, plugin_index);

    /* A disabled plugin has nothing to reload: it is already torn down, and
     * restarting it here would switch it on behind the user's back. */
    if( !ctx->enabled )
        return;

    panel_was_selected = host->panel_active == plugin_index;
    plugin_teardown(host, plugin_index);

    /*
     * The adapter's chance to rebuild from source. It may rewrite the def in
     * place -- a script that grew a config key or a handler comes back with
     * it -- so everything below rereads through ctx->def rather than caching
     * anything from before this call.
     */
    if( ctx->def->reload )
        ctx->def->reload(ctx);

    /* Reread through the new def, for the same reason as the schema below. */
    plugin_title_refresh(ctx);

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
    ctx->schema_count = 0;
    if( ctx->def->config )
    {
        for( int i = 0; ctx->def->config[i].key; i++ )
        {
            struct ToriRS_PluginConfigItem const* item = &ctx->def->config[i];
            struct PluginConfigSlot* slot;
            bool const existed = plugin_config_slot(ctx, item->key, false) != NULL;

            slot = plugin_config_slot(ctx, item->key, true);
            ctx->schema_count++;
            if( !slot )
                continue;
            /* Re-point the slot at its item in the NEW schema: the old index
             * may name a different key, or none at all. */
            slot->schema_index = plugin_schema_index(ctx, item->key);
            if( !existed )
                plugin_copy_str(
                    slot->value, sizeof(slot->value),
                    item->default_value ? item->default_value : "");
        }
    }

    /* Whatever the last run faulted with was about the run that just ended,
     * and so was any refusal: a reload is a fresh run, decided again from
     * whatever the new source says. */
    ctx->error[0] = '\0';
    ctx->refused = false;

    PluginHost_Start(host);
    /* Reload is replacement in place, not a user navigation action. If the
     * plugin registered its page again, rebuild it as the still-selected
     * entry; otherwise the shell remains collapsed. */
    if( panel_was_selected && host->panel_registered[plugin_index] && ctx->running )
        (void)PluginHost_PanelSelect(host, plugin_index);
}

bool
PluginHost_IsEnabled(struct ToriRS_PluginHost const* host, int plugin_index)
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
PluginHost_Name(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].name;
}

char const*
PluginHost_Title(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].title;
}

bool
PluginHost_IsAdapter(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].def->adapter;
}

bool
PluginHost_IsHidden(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].def->hidden;
}

bool
PluginHost_IsEssential(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].def->essential;
}

char const*
PluginHost_Error(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].error[0] ? host->plugins[plugin_index].error : NULL;
}

void
PluginHost_SetError(struct ToriRS_PluginHost* host, int plugin_index, char const* text)
{
    assert(host);
    struct ToriRS_PluginCtx* ctx = plugin_at(host, plugin_index);
    if( !text )
    {
        ctx->error[0] = '\0';
        return;
    }
    snprintf(ctx->error, sizeof(ctx->error), "%s", text);
}

int
PluginHost_IndexOf(struct ToriRS_PluginHost const* host, char const* name)
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

struct ToriRS_PluginApi const*
PluginHost_Api(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return &host->api;
}

struct ToriRS_PluginCtx*
PluginHost_Ctx(struct ToriRS_PluginHost* host, int plugin_index)
{
    assert(host);
    return plugin_at(host, plugin_index);
}

int
PluginHost_CtxIndex(struct ToriRS_PluginCtx const* ctx)
{
    assert(ctx);
    return ctx->index;
}

/* ------------------------------------------------------------------ seams */

void
PluginHost_FrameStart(
    struct ToriRS_PluginHost* host, uint64_t now_ms, uint64_t drawn_frames)
{
    if( !host )
        return;
    /* The frame boundary is also where per-frame draw budgets reset. */
    for( int i = 0; i < host->plugin_count; i++ )
    {
        host->plugins[i].draw_used = 0;
        host->plugins[i].draw_clipped = false;
    }

    /* The screen poll. Here rather than at the transitions themselves because
     * the app changes screens from half a dozen places (login, logout, a
     * disconnect, the title's own tabs) and a seam per place is how one gets
     * missed; the boundary sees them all. Before EV_FRAME_START, so a frame
     * handler polling api->screen never contradicts an EV_SCREEN_CHANGE it has
     * not received yet. The baseline advances whether or not anyone listens --
     * a subscriber must never be handed a change that predates it. */
    {
        int const screen = host->engine.screen(host->engine.user);
        if( screen != host->last_screen )
        {
            struct ToriRS_PluginEvScreen ev = { screen, host->last_screen };
            host->last_screen = screen;
            if( host->sub_count[TORIRS_PLUGIN_EV_SCREEN_CHANGE] > 0 )
                plugin_dispatch(host, TORIRS_PLUGIN_EV_SCREEN_CHANGE, &ev);
        }
    }

    if( host->sub_count[TORIRS_PLUGIN_EV_FRAME_START] == 0 )
        return;

    struct ToriRS_PluginEvFrame ev = { now_ms, drawn_frames };
    plugin_dispatch(host, TORIRS_PLUGIN_EV_FRAME_START, &ev);
}

void
PluginHost_LogicTick(struct ToriRS_PluginHost* host, int logic_cycle)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_LOGIC_TICK] == 0 )
        return;
    struct ToriRS_PluginEvTick ev = { logic_cycle };
    plugin_dispatch(host, TORIRS_PLUGIN_EV_LOGIC_TICK, &ev);
}

void
PluginHost_ServerTick(struct ToriRS_PluginHost* host, int world_cycle)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_SERVER_TICK] == 0 )
        return;
    struct ToriRS_PluginEvTick ev = { world_cycle };
    plugin_dispatch(host, TORIRS_PLUGIN_EV_SERVER_TICK, &ev);
}

void
PluginHost_WorldLoaded(struct ToriRS_PluginHost* host, int base_tile_x, int base_tile_z)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_WORLD_LOADED] == 0 )
        return;
    struct ToriRS_PluginEvWorld ev = { base_tile_x, base_tile_z };
    plugin_dispatch(host, TORIRS_PLUGIN_EV_WORLD_LOADED, &ev);
}

static void
plugin_npc_event(
    struct ToriRS_PluginHost* host,
    enum ToriRS_PluginEvent which,
    struct ToriRS_PluginNpcSnap const* npc)
{
    if( !host || host->sub_count[which] == 0 )
        return;
    assert(npc);
    struct ToriRS_PluginEvNpc ev;
    ev.npc = *npc;
    plugin_dispatch(host, which, &ev);
}

void
PluginHost_NpcSpawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginNpcSnap const* npc)
{
    plugin_npc_event(host, TORIRS_PLUGIN_EV_NPC_SPAWN, npc);
}

void
PluginHost_NpcRetype(struct ToriRS_PluginHost* host, struct ToriRS_PluginNpcSnap const* npc)
{
    plugin_npc_event(host, TORIRS_PLUGIN_EV_NPC_RETYPE, npc);
}

void
PluginHost_NpcDespawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginNpcSnap const* npc)
{
    plugin_npc_event(host, TORIRS_PLUGIN_EV_NPC_DESPAWN, npc);
}

static void
plugin_obj_event(
    struct ToriRS_PluginHost* host,
    enum ToriRS_PluginEvent which,
    struct ToriRS_PluginObjSnap const* obj)
{
    if( !host || host->sub_count[which] == 0 )
        return;
    assert(obj);
    struct ToriRS_PluginEvObj ev;
    ev.obj = *obj;
    plugin_dispatch(host, which, &ev);
}

void
PluginHost_ObjSpawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginObjSnap const* obj)
{
    plugin_obj_event(host, TORIRS_PLUGIN_EV_OBJ_SPAWN, obj);
}

void
PluginHost_ObjCount(struct ToriRS_PluginHost* host, struct ToriRS_PluginObjSnap const* obj)
{
    plugin_obj_event(host, TORIRS_PLUGIN_EV_OBJ_COUNT, obj);
}

void
PluginHost_ObjDespawn(struct ToriRS_PluginHost* host, struct ToriRS_PluginObjSnap const* obj)
{
    plugin_obj_event(host, TORIRS_PLUGIN_EV_OBJ_DESPAWN, obj);
}

void
PluginHost_ChatMessage(
    struct ToriRS_PluginHost* host,
    int type,
    char const* sender,
    char const* text)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_CHAT_MESSAGE] == 0 )
        return;

    struct ToriRS_PluginEvChat ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    /* Both are absent on ordinary lines -- a system message has no sender, and
     * IF_SETTEXT blanks a row by writing nothing -- so they are guards, not
     * asserts, and a plugin always reads a string rather than a NULL. */
    if( sender )
        snprintf(ev.sender, sizeof(ev.sender), "%s", sender);
    if( text )
        snprintf(ev.text, sizeof(ev.text), "%s", text);
    plugin_dispatch(host, TORIRS_PLUGIN_EV_CHAT_MESSAGE, &ev);
}

void
PluginHost_Setting(struct ToriRS_PluginHost* host, int setting_id, int value)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_SETTING] == 0 )
        return;

    struct ToriRS_PluginEvSetting ev;
    memset(&ev, 0, sizeof(ev));
    ev.setting_id = setting_id;
    ev.value = value;
    plugin_dispatch(host, TORIRS_PLUGIN_EV_SETTING, &ev);
}

void
PluginHost_GameEvent(
    struct ToriRS_PluginHost* host,
    char const* kind,
    char const* subject,
    int value,
    char const* text)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_GAME_EVENT] == 0 )
        return;
    /* The kind is the whole event -- a plugin switches on it -- so an absent
     * one is a caller bug rather than a moment with nothing to say. */
    assert(kind);

    struct ToriRS_PluginEvGameEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.value = value;
    if( subject )
        snprintf(ev.subject, sizeof(ev.subject), "%s", subject);
    if( text )
        snprintf(ev.text, sizeof(ev.text), "%s", text);
    plugin_dispatch(host, TORIRS_PLUGIN_EV_GAME_EVENT, &ev);
}

int
PluginHost_PacketIn(struct ToriRS_PluginHost* host, int packet_name, int size)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_PACKET_IN] == 0 )
        return 0;

    struct ToriRS_PluginEvPacketIn ev = { packet_name, size, false };
    plugin_dispatch(host, TORIRS_PLUGIN_EV_PACKET_IN, &ev);
    return ev.drop ? 1 : 0;
}

int
PluginHost_PacketOutVeto(struct ToriRS_PluginHost* host, char const* builder_expr)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_PACKET_OUT] == 0 )
        return 0;

    assert(builder_expr);

    /* The macro stringifies the whole call, arguments and all. Everything up
     * to the first '(' is the builder's name, which is the part that names the
     * packet; the arguments are call-site noise. Trimmed here, once, and only
     * when something is actually subscribed. */
    char name[64];
    size_t len = 0;
    while( builder_expr[len] && builder_expr[len] != '(' && len + 1 < sizeof(name) )
    {
        name[len] = builder_expr[len];
        len++;
    }
    while( len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t') )
        len--;
    name[len] = '\0';

    struct ToriRS_PluginEvPacketOut ev;
    ev.builder = name;
    ev.drop = false;
    plugin_dispatch(host, TORIRS_PLUGIN_EV_PACKET_OUT, &ev);
    return ev.drop ? 1 : 0;
}

int
PluginHost_Key(struct ToriRS_PluginHost* host, int key, int ch, bool down)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_KEY] == 0 )
        return 0;

    struct ToriRS_PluginEvKey ev = { key, ch, down };
    return plugin_dispatch(host, TORIRS_PLUGIN_EV_KEY, &ev) == TORIRS_PLUGIN_CONSUME ? 1 : 0;
}

void
PluginHost_DrawWorld(struct ToriRS_PluginHost* host)
{
    struct ToriRS_PluginEvDraw ev;

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
    ev.surface = host;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
    if( host->sub_count[TORIRS_PLUGIN_EV_DRAW_WORLD] > 0 )
        plugin_dispatch(host, TORIRS_PLUGIN_EV_DRAW_WORLD, &ev);
    /* The declared looks, after the imperative drawing: a holder's standing
     * hull goes over whatever anyone else marked around it. */
    plugin_entity_paint_looks(host);
    host->draw_surface = NULL;
}

void
PluginHost_DrawCanvas(struct ToriRS_PluginHost* host, int width, int height)
{
    if( !host )
        return;

    /*
     * A DIFFERENT token from the world surface's, and that is the point: the
     * token is compared, never dereferenced, so `&host->api` here and `host`
     * there is all it takes for a handler that kept the wrong event's surface
     * to be caught by the same assert that catches drawing outside a window.
     */
    struct ToriRS_PluginEvDrawCanvas ev;
    host->draw_surface = &host->api;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_CANVAS;
    ev.surface = &host->api;
    ev.width = width;
    ev.height = height;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_CANVAS);
    /* The lane and added parts FIRST, so a holder's own per-tick drawing --
     * an orb's fill over the plate the host put down -- lands on top. */
    plugin_chrome_paint_all(host, 1);
    if( host->sub_count[TORIRS_PLUGIN_EV_DRAW_CANVAS] > 0 )
        plugin_dispatch(host, TORIRS_PLUGIN_EV_DRAW_CANVAS, &ev);
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
}

void
PluginHost_ReconcileRoleReplacements(struct ToriRS_PluginHost* host)
{
    if( !host )
        return;
    for( int i = 0; i < TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX; i++ )
    {
        struct PluginRoleReplacement const* claim = &host->role_replacements[i];
        if( claim->plugin < 0 )
            continue;
        (void)host->engine.role_replace(
            host->engine.user, claim->plugin, claim->role, /*enabled=*/1);
    }
}

/**
 * The chrome pass: reconcile who is suppressing what, then ask every claimant
 * whose declaration went stale to state it again.
 *
 * Driven per frame rather than only from the layout pass, and that is what
 * makes one edge case go away: an image crosses the IO queue and lands with no
 * layout in flight, so a dresser that skipped a pass because its borrowed
 * plate had no pixels yet would never be asked again. Here it is asked on the
 * frame after the pixels arrive.
 *
 * Called AFTER PluginHost_Layout in the same frame, which is the whole of the
 * ordering promise between the tiers: the arranger has already stated the
 * frame, so every box a dresser reads is this pass's.
 */
void
PluginHost_ChromeTick(struct ToriRS_PluginHost* host, int width, int height)
{
    struct ToriRS_PluginEvLayout ev;
    int moved = 0;

    if( !host )
        return;

    ev.width = width;
    ev.height = height;
    ev.canvas = host->layout_canvas;

    host->chrome_iterating = 1;
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        struct ToriRS_PluginChromePart base;
        int state;
        int lane;

        if( row->plugin < 0 )
            continue;
        if( !host->plugins[row->plugin].running )
            continue;

        /*
         * The suppression is RECONCILED and not merely taken at claim time,
         * and it follows the APPEARANCE scope alone: hiding a native subtree
         * takes its pixels and its click together, which is right for a
         * plugin replacing the picture and wrong for one that only wants the
         * click -- that one lays its region OVER the native one, and paint
         * order gives it precedence.
         *
         * A part that was the cache's own becomes a plugin arranger's the
         * moment a gameframe plugin claims the frame, and a replacement left
         * standing over it would prune the very node the arranger is now
         * placing -- a button that vanishes when a layout plugin is switched
         * on, with nothing in either plugin to explain it.
         */
        lane = (row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE) &&
               plugin_chrome_base(host, row->part, &base, &state) &&
               base.source == TORIRS_PLUGIN_CHROME_SOURCE_LANE;
        if( lane && !row->lane_replaced )
        {
            if( host->engine.role_replace(host->engine.user, row->plugin, row->part, 1) )
                row->lane_replaced = 1;
        }
        else if( !lane && row->lane_replaced )
        {
            (void)host->engine.role_replace(host->engine.user, row->plugin, row->part, 0);
            row->lane_replaced = 0;
        }
        else if( lane )
        {
            /* Restating is the per-frame rebind: the engine's claim is fenced
             * to an exact node incarnation, and a CS2 subtree rebuild moves
             * it. @see app_plugin_role_replace. */
            (void)host->engine.role_replace(host->engine.user, row->plugin, row->part, 1);
        }
    }
    host->chrome_iterating = 0;

    if( host->sub_count[TORIRS_PLUGIN_EV_CHROME] == 0 )
        return;

    /*
     * In CLAIM ORDER, which is table order because the table is never
     * compacted or sorted. Two claimants never contend for one scope -- claims
     * are unique per (part, scope) -- so the order is not arbitration, it is
     * only determinism.
     */
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim* row = &host->chrome_claims[i];
        int plugin;

        if( row->plugin < 0 || !row->needs_declare )
            continue;
        plugin = row->plugin;
        if( !host->plugins[plugin].enabled || !host->plugins[plugin].running )
            continue;

        /*
         * Emptied before and applied after, exactly as EV_LAYOUT is: the
         * dispatch IS the declaration, so a part the handler does not paint
         * this time is a part the plugin no longer draws. Only the parts
         * belonging to THIS plugin are cleared -- one claimant's silence is
         * not another's -- and an introducer's initial art is kept, because
         * chrome_add already said it.
         */
        host->chrome_iterating = 1;
        for( int j = 0; j < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; j++ )
            if( host->chrome_claims[j].plugin == plugin )
            {
                if( !host->chrome_claims[j].added )
                    host->chrome_claims[j].declared = 0;
                host->chrome_claims[j].has_ops = 0;
                host->chrome_claims[j].op_count = 0;
                host->chrome_claims[j].needs_declare = 0;
            }

        host->chrome_declaring = 1;
        host->chrome_declarer = plugin;
        plugin_dispatch_one(host, plugin, TORIRS_PLUGIN_EV_CHROME, &ev);
        host->chrome_declaring = 0;
        host->chrome_declarer = -1;
        host->chrome_iterating = 0;
    }

    /* A POSITION holder that moved a FRAME part wants the member re-placed
     * under it, which only a layout pass can do. One nudge, coalesced. */
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
        if( host->chrome_claims[i].plugin >= 0 && host->chrome_claims[i].moved )
        {
            host->chrome_claims[i].moved = 0;
            moved = 1;
        }
    if( moved && host->layout_owner >= 0 )
        plugin_layout_publish(host);
}

/**
 * Paint every declared part, over whatever the arranger drew for a backdrop.
 *
 * The host does this rather than the plugins, and that is the mechanism the
 * whole tier rests on: because the picture is a DECLARATION, a claimed scope
 * simply stops being painted from the arranger's declaration and starts being
 * painted from the claimant's. Neither plugin is told, neither has a
 * "did somebody replace me" branch, and there is no frame on which both draw.
 *
 * Ordered after the owner's own EV_DRAW_FRAME because a plate goes ON the
 * surround it sits in, never under it.
 */
/**
 * Which SURFACE a part is painted on follows where the thing it replaces
 * lives, because a plate has to land where the pointer expects it and under
 * what should cover it:
 *
 *   FRAME   an arranger's part sits on the arranger's own surface, under the
 *           interfaces, beside the stones it declared -- PluginHost_DrawFrame.
 *   LANE    a cache widget's replacement paints at that widget's TOMBSTONE,
 *           in the canvas pass, anchored to the role with the replacement
 *           flag -- exactly where role_anchor puts a plugin's own drawing
 *           over a role it replaced. Painted on the frame surface it would
 *           be under the cache's own panels.
 *   ADDED   an introduced part hangs off its anchor and paints inside the
 *           anchor's subtree, in the canvas pass, so it inherits the anchor's
 *           clip and fate. An orb column drawn under the interfaces would be
 *           behind the map's housing on every fixed frame.
 */
static void
plugin_chrome_paint_all(struct ToriRS_PluginHost* host, int canvas_pass)
{
    uint8_t painted[TORIRS_PLUGIN_SLOT_ART_MAX];
    int mx = 0;
    int my = 0;
    int have_mouse;

    assert(host);
    have_mouse = host->engine.mouse_pos(host->engine.user, &mx, &my);
    memset(painted, 0, sizeof(painted));

    host->chrome_iterating = 1;

    /*
     * Every NAMED part once, composed. A part with two claimants -- one on
     * the box, one on the picture -- has two rows here and one painting, so
     * the first row to reach it paints and the rest find it done.
     */
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
    {
        struct PluginChromeClaim const* row = &host->chrome_claims[i];
        struct ToriRS_PluginChromePart resolved;
        struct PluginChromeClaim const* ops = NULL;
        int state;
        int slot = -1;
        int member = -1;
        int dup = 0;

        if( row->plugin < 0 )
            continue;
        for( int j = 0; j < i; j++ )
            if( host->chrome_claims[j].plugin >= 0 &&
                strcmp(host->chrome_claims[j].part, row->part) == 0 )
            {
                dup = 1;
                break;
            }
        if( dup )
            continue;

        if( !plugin_chrome_resolve(host, row->part, -1, &resolved, &state, &ops) )
            continue;
        if( (resolved.source == TORIRS_PLUGIN_CHROME_SOURCE_FRAME) == (canvas_pass != 0) )
            continue;
        if( ops && !(host->plugins[ops->plugin].enabled && host->plugins[ops->plugin].running) )
            ops = NULL;
        /* Nothing to draw and nothing to click is a part hidden by its
         * holders, which is a sentence the API lets them say. */
        if( resolved.art[TORIRS_PLUGIN_CHROME_IDLE] < 0 && !ops )
        {
            int all_absent = 1;
            for( int k = 0; k < TORIRS_PLUGIN_CHROME_STATE_COUNT; k++ )
                if( resolved.art[k] >= 0 )
                    all_absent = 0;
            if( all_absent )
            {
                if( !row->added && host->engine.role_slot(host->engine.user, row->part, &slot, &member) )
                {
                    struct PluginSlotArt const* art = plugin_slot_art_find(host, slot, member);
                    if( art )
                        painted[art - host->slot_art] = 1;
                }
                continue;
            }
        }
        if( canvas_pass )
        {
            /* Anchored for the draw and released after it, exactly as a
             * plugin's own role_anchor lasts until its handler returns. The
             * holder whose region is installed is the one the anchor is
             * attributed to. */
            struct PluginChromeClaim const* added = plugin_chrome_added_row(host, row->part);
            int const who = ops ? ops->plugin : row->plugin;
            /* An added part hung off a role somebody REPLACED paints at that
             * replacement's tombstone, exactly as api_role_anchor routes a
             * plugin's own drawing: the anchor is the object, and the object
             * is wherever its provider paints it. */
            /* A replacement's own appearance is the object itself -- SELF --
             * so a part hung BEFORE it paints under and one hung AFTER paints
             * over, whichever claimed first. */
            int anchored =
                added ? host->engine.role_anchor(
                            host->engine.user,
                            who,
                            added->anchor,
                            role_replaced_by(host, added->anchor) >= 0,
                            added->place)
                      : host->engine.role_anchor(
                            host->engine.user, who, row->part, 1, PLUGIN_ANCHOR_PLACE_SELF);
            if( anchored )
                plugin_chrome_paint_one(host, &resolved, state, have_mouse, mx, my, ops);
            (void)host->engine.role_anchor(host->engine.user, who, NULL, 0, 0);
        }
        else
            plugin_chrome_paint_one(host, &resolved, state, have_mouse, mx, my, ops);

        if( !row->added && host->engine.role_slot(host->engine.user, row->part, &slot, &member) )
        {
            struct PluginSlotArt const* art = plugin_slot_art_find(host, slot, member);
            if( art )
                painted[art - host->slot_art] = 1;
        }
    }

    /* The arranger's remaining parts: the ones no claim named. */
    for( int i = 0; i < TORIRS_PLUGIN_SLOT_ART_MAX && !canvas_pass; i++ )
    {
        struct PluginSlotArt const* row = &host->slot_art[i];
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        if( !row->used || painted[i] || host->layout_owner < 0 )
            continue;
        if( !(host->plugins[host->layout_owner].enabled && host->plugins[host->layout_owner].running) )
            continue;
        if( !(row->member < 0
                  ? host->engine.slot_rect(host->engine.user, row->slot, &x, &y, &w, &h)
                  : host->engine.slot_member_rect(
                        host->engine.user, row->slot, row->member, &x, &y, &w, &h)) )
            continue;
        plugin_chrome_paint_one(host, &row->art, row->state, have_mouse, mx, my, NULL);
    }

    host->chrome_iterating = 0;
}

void
PluginHost_DrawFrame(struct ToriRS_PluginHost* host, int width, int height)
{
    struct ToriRS_PluginEvDrawCanvas ev;

    if( !host || host->layout_owner < 0 )
        return;
    if( host->sub_count[TORIRS_PLUGIN_EV_DRAW_FRAME] == 0 )
        return;

    /* A third token, for the third surface, on the same reasoning as the
     * canvas one: compared and never dereferenced, so three distinct addresses
     * are the whole mechanism that catches a handler which kept the wrong
     * event's surface. */
    host->draw_surface = &host->engine;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_FRAME;
    ev.surface = &host->engine;
    ev.width = width;
    ev.height = height;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_FRAME);
    /* The owner and nobody else. Chrome drawn under the interfaces of a frame
     * somebody else is arranging is chrome in the wrong place. */
    plugin_dispatch_one(host, host->layout_owner, TORIRS_PLUGIN_EV_DRAW_FRAME, &ev);
    /* Over the backdrop the owner just drew, because a plate goes ON its
     * surround and never under it. @see plugin_chrome_paint_all. */
    plugin_chrome_paint_all(host, 0);
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
}

void
PluginHost_LayoutChanged(struct ToriRS_PluginHost* host)
{
    struct ToriRS_PluginEvTick ev;

    if( !host )
        return;

    /*
     * The revision moves for EVERY change, including one made from inside the
     * notification. Readers compare it and re-read a live region, so they are
     * correct either way.
     */
    host->layout_revision++;
    /* Every claimant measured its parts against boxes that have just moved, so
     * all of them are re-asked on the next chrome tick. Cheap: the flag is a
     * byte, and a plugin whose parts did not really change simply declares the
     * same numbers again. */
    plugin_chrome_invalidate(host);
    if( host->sub_count[TORIRS_PLUGIN_EV_LAYOUT_CHANGED] == 0 )
        return;

    /*
     * The EVENT does not nest, and this is not a nicety.
     *
     * A cooperative layout invites exactly the shape that would spin: a dock
     * hears that the safe region moved, recalculates the width it wants, and
     * reserves -- which changes the layout, which notifies it again. Two docks
     * responding to each other need not even disagree to do it forever.
     *
     * Coalescing rather than refusing: the claim is still recorded and the
     * revision still moves, so the next read -- and every read is live -- sees
     * it. What is dropped is only the second telling, which every subscriber
     * is about to be told anyway.
     */
    if( host->layout_notifying )
        return;
    host->layout_notifying = 1;
    ev.cycle = host->layout_revision;
    plugin_dispatch(host, TORIRS_PLUGIN_EV_LAYOUT_CHANGED, &ev);
    host->layout_notifying = 0;
}

void
PluginHost_Layout(struct ToriRS_PluginHost* host, int width, int height)
{
    struct ToriRS_PluginEvLayout ev;
    int owner;
    uint32_t claim_epoch;

    if( !host || host->layout_owner < 0 )
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
    if( host->layout_canvas != TORIRS_PLUGIN_CANVAS_FIXED && (width <= 0 || height <= 0) )
    {
        TORIRS_LOG("plugin: %s asked to lay out against a %dx%d canvas; nothing declared\n",
            host->plugins[host->layout_owner].name,
            width,
            height);
        return;
    }

    /*
     * FIXED reads back its own pinned size, not the window's.
     *
     * The alternative -- handing over whatever the platform last set -- makes
     * the plugin's arithmetic depend on when in the boot it was asked, because
     * the canvas is not pinned until the engine has acted on the claim. A
     * layout that asked for 765x503 is entitled to be told 765x503 the first
     * time it is asked, and every time after.
     */
    if( host->layout_canvas == TORIRS_PLUGIN_CANVAS_FIXED )
    {
        ev.width = host->layout_fixed_w;
        ev.height = host->layout_fixed_h;
    }
    else
    {
        ev.width = width;
        ev.height = height;
    }
    ev.canvas = host->layout_canvas;

    /* Empty first, apply after: the dispatch is the whole declaration, so a
     * slot the handler does not mention this time is one the frame no longer
     * has. @see EV_LAYOUT. */
    owner = host->layout_owner;
    claim_epoch = host->layout_claim_epoch;
    /* The part table goes with the slot table, for the same reason: the
     * dispatch is the whole declaration, so a part the arranger does not
     * declare this time is a part the frame no longer has. */
    memset(host->slot_art, 0, sizeof(host->slot_art));
    host->engine.layout_begin(host->engine.user);
    host->layout_declaring = 1;
    plugin_dispatch_one(host, owner, TORIRS_PLUGIN_EV_LAYOUT, &ev);
    host->layout_declaring = 0;
    if( host->layout_owner == owner && host->layout_claim_epoch == claim_epoch )
    {
        /*
         * POSITION holders, applied over the arranger's placement and before
         * the declaration is committed.
         *
         * A plugin that moved the report button has to move the MEMBER -- the
         * node the label mounts on -- and not just where the plate is
         * painted, or the two come apart. Only a layout pass can place a
         * node, so the standing declaration is re-applied here, on every
         * pass, after the arranger has said where it wanted things. It is
         * the last writer, which is the whole of what holding POSITION means.
         *
         * A holder whose box is anchor-relative (an added part) has no member
         * to move and is skipped; its box is composed at paint time instead.
         */
        for( int i = 0; i < TORIRS_PLUGIN_CHROME_CLAIMS_MAX; i++ )
        {
            struct PluginChromeClaim const* row = &host->chrome_claims[i];
            int slot = -1;
            int member = -1;

            if( row->plugin < 0 || row->added || !row->declared )
                continue;
            if( !(row->scopes & TORIRS_PLUGIN_CHROME_SCOPE_POSITION) )
                continue;
            if( row->art.w <= 0 || row->art.h <= 0 )
                continue;
            if( !host->engine.role_slot(host->engine.user, row->part, &slot, &member) )
                continue;
            (void)host->engine.layout_slot(
                host->engine.user, slot, member, row->art.x, row->art.y, row->art.w, row->art.h);
        }
        host->engine.layout_end(host->engine.user);
    }
}

int
PluginHost_LayoutOwner(struct ToriRS_PluginHost const* host)
{
    return host ? host->layout_owner : -1;
}

void
PluginHost_CanvasClick(
    struct ToriRS_PluginHost* host, int plugin_index, uint32_t tag, int op, int x, int y)
{
    struct ToriRS_PluginCtx* ctx;
    struct ToriRS_PluginEvCanvasClick ev;
    int prev;

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

    ev.tag = tag;
    ev.op = op;
    ev.x = x;
    ev.y = y;
    prev = host->dispatching;
    host->dispatching = plugin_index;
    /* Only the plugin that declared the region, like EV_ASSET and EV_UI: a
     * click on one plugin's orb is not news to another. */
    for( int i = 0; i < host->sub_count[TORIRS_PLUGIN_EV_CANVAS_CLICK]; i++ )
    {
        struct PluginSub const* sub = &host->subs[TORIRS_PLUGIN_EV_CANVAS_CLICK][i];
        if( sub->plugin != plugin_index )
            continue;
        if( sub->handler(ctx, &ev, sub->userdata) == TORIRS_PLUGIN_CONSUME )
            break;
    }
    host->dispatching = prev;
}

void
PluginHost_MenuBuild(
    struct ToriRS_PluginHost* host,
    void* cursor,
    struct ToriRS_PluginEvMenuBuild* menu,
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
    if( host->sub_count[TORIRS_PLUGIN_EV_MENU_BUILD] > 0 )
        plugin_dispatch(host, TORIRS_PLUGIN_EV_MENU_BUILD, menu);
    /* The entity HITBOX holders, after the plugins' own rows: dropping a row
     * shifts the ones above it, and a payload handed to a handler must not
     * shift under it. */
    plugin_entity_apply_ops(host, cursor, menu);
    host->menu_cursor = NULL;
}

bool
PluginHost_OwnsMenuAction(struct ToriRS_PluginHost const* host, int action)
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
    struct ToriRS_PluginMenuRow const* row,
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
    if( !route && host->sub_count[TORIRS_PLUGIN_EV_MENU_SELECT] == 0 )
        return 0;

    struct ToriRS_PluginEvMenuSelect ev;
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
        struct ToriRS_PluginCtx* ctx = &host->plugins[route->plugin];
        ev.owned = true;
        if( ctx->enabled && ctx->running )
        {
            int const prev = host->dispatching;
            host->dispatching = route->plugin;
            for( int i = 0; i < host->sub_count[TORIRS_PLUGIN_EV_MENU_SELECT]; i++ )
            {
                struct PluginSub const* sub = &host->subs[TORIRS_PLUGIN_EV_MENU_SELECT][i];
                if( sub->plugin != route->plugin )
                    continue;
                if( sub->handler(ctx, &ev, sub->userdata) == TORIRS_PLUGIN_CONSUME )
                    break;
            }
            host->dispatching = prev;
        }
        consumed = 1;
    }
    else
    {
        consumed = plugin_dispatch(host, TORIRS_PLUGIN_EV_MENU_SELECT, &ev) ==
                           TORIRS_PLUGIN_CONSUME
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

    int const index = PluginHost_IndexOf(host, plugin_name);
    if( index < 0 )
        return;

    struct ToriRS_PluginCtx* ctx = &host->plugins[index];

    /* `enabled` is host state, not plugin config: it is what the settings
     * panel toggles and what a crashed script gets cleared by. */
    if( strcmp(key, "enabled") == 0 )
    {
        /* Same refusal as PluginHost_SetEnabled, and it has to be here too:
         * this path writes the field directly, so an `enabled=0` left over
         * from a build where the plugin was ordinary would switch it off
         * behind both the panel and the host. */
        if( !ctx->def->essential )
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
PluginHost_ConfigDecode(struct ToriRS_PluginHost* host, void const* data, int size)
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
            if( len < 0 )
                len = 0;
            if( len >= (int)sizeof(section) )
                len = (int)sizeof(section) - 1;
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
        if( klen >= (int)sizeof(key) )
            klen = (int)sizeof(key) - 1;
        memcpy(key, line, (size_t)klen);
        key[klen] = '\0';

        char const* val = eq + 1;
        while( val < line_end && (*val == ' ' || *val == '\t') )
            val++;
        int vlen = (int)(line_end - val);
        if( vlen < 0 )
            vlen = 0;
        if( vlen >= (int)sizeof(value) )
            vlen = (int)sizeof(value) - 1;
        memcpy(value, val, (size_t)vlen);
        value[vlen] = '\0';

        if( key[0] )
            PluginHost_ConfigApply(host, section, key, value);
    }
}

int
PluginHost_ConfigEncode(struct ToriRS_PluginHost const* host, void** out_data, int* out_size)
{
    assert(host);
    assert(out_data);
    assert(out_size);

    /* Bounded by the store's own fixed caps, so one sizing pass is enough.
     * The 512 is the banner below, which is written whatever the store holds. */
    size_t cap = 512;
    for( int i = 0; i < host->plugin_count; i++ )
        cap += 96 + (size_t)host->plugins[i].config_count *
                        (64 + TORIRS_PLUGIN_CONFIG_VALUE_MAX + 4);

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
        struct ToriRS_PluginCtx const* ctx = &host->plugins[i];
        /* A section is written only when it carries something: an all-default,
         * enabled plugin leaves no trace, the way RS_Prefs omits defaults. */
        bool wrote_section = false;

        /* Written only when it differs from what the plugin declared, so a
         * default-off plugin left off leaves no trace, and a default-on plugin
         * left on leaves none either -- the RS_Prefs rule. */
        if( ctx->enabled == ctx->def->disabled_by_default )
        {
            at += (size_t)snprintf(
                buf + at,
                cap - at,
                "\n[plugin:%s]\nenabled=%d\n",
                ctx->name,
                ctx->enabled ? 1 : 0);
            wrote_section = true;
        }

        for( int c = 0; c < ctx->config_count; c++ )
        {
            struct PluginConfigSlot const* slot = &ctx->config[c];
            if( slot->schema_index >= 0 )
            {
                char const* def = ctx->def->config[slot->schema_index].default_value;
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
PluginHost_ConfigCount(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].schema_count;
}

struct ToriRS_PluginConfigItem const*
PluginHost_ConfigItem(struct ToriRS_PluginHost const* host, int plugin_index, int item_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);

    struct ToriRS_PluginCtx const* ctx = &host->plugins[plugin_index];
    if( !ctx->def->config || item_index < 0 || item_index >= ctx->schema_count )
        return NULL;
    return &ctx->def->config[item_index];
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
    enum ToriRS_PluginEvent ev,
    void* payload)
{
    int const prev_dispatching = host->dispatching;
    int const prev_event = host->dispatch_event;

    assert(host);
    assert(ev >= 0);
    assert(ev < TORIRS_PLUGIN_EV_COUNT);

    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return;

    for( int i = 0; i < host->sub_count[ev]; i++ )
    {
        struct PluginSub const sub = host->subs[ev][i];
        struct ToriRS_PluginCtx* ctx;

        if( sub.plugin != plugin_index )
            continue;
        ctx = &host->plugins[sub.plugin];
        if( !ctx->enabled || !ctx->running )
            continue;

        host->dispatching = sub.plugin;
        host->dispatch_event = ev;
        sub.handler(ctx, payload, sub.userdata);
        host->dispatching = prev_dispatching;
        host->dispatch_event = prev_event;

        if( i < host->sub_count[ev] && host->subs[ev][i].handler != sub.handler )
            i--;
    }
}

bool
PluginHost_WinHasTab(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return false;
    return host->win_tab[plugin_index];
}

char const*
PluginHost_WinTabTitle(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return "";
    return host->win_tab_title[plugin_index];
}

int
PluginHost_WinWidgetCount(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return 0;
    return plugin_win_count_owned(host, plugin_index);
}

struct ToriRS_PluginWinWidget const*
PluginHost_WinWidgetAt(
    struct ToriRS_PluginHost const* host, int plugin_index, int widget_index)
{
    int n = 0;

    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count || widget_index < 0 )
        return NULL;
    for( int i = 0; i < host->win_widget_count; i++ )
    {
        if( host->win_owner[i] != plugin_index )
            continue;
        if( n == widget_index )
            return &host->win_widgets[i];
        n++;
    }
    return NULL;
}

void
PluginHost_WinBuild(struct ToriRS_PluginHost* host, int plugin_index)
{
    struct ToriRS_PluginEvUi ev;

    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return;
    /* Only when the tab is empty: a plugin that keeps live state in its
     * controls -- a running count, a last-seen value -- must not have them
     * reset every time something else opens the window. */
    if( plugin_win_count_owned(host, plugin_index) > 0 )
        return;

    memset(&ev, 0, sizeof(ev));
    ev.widget_id = NULL;
    ev.action = -1;
    ev.value = -1;
    ev.text = "";
    host->win_building = true;
    plugin_dispatch_one(host, plugin_index, TORIRS_PLUGIN_EV_UI_BUILD, &ev);
    host->win_building = false;
}

int
PluginHost_WinDispatch(
    struct ToriRS_PluginHost* host,
    int plugin_index,
    char const* widget_id,
    int action,
    int value,
    char const* text)
{
    struct ToriRS_PluginEvUi ev;
    struct ToriRS_PluginWinWidget* w;
    int slot;

    assert(host);
    assert(widget_id);

    slot = plugin_win_find(host, plugin_index, widget_id);
    if( slot < 0 )
        return 0;
    w = &host->win_widgets[slot];

    /*
     * The host's copy is updated BEFORE the plugin hears about it, so a handler
     * that reads its own control back sees the value the user just set rather
     * than the one it replaced. Without this a plugin's only way to know its
     * own state would be to shadow every control itself.
     */
    switch( action )
    {
    case TORIRS_PLUGIN_UI_TOGGLE:
        w->checked = value ? 1 : 0;
        w->value = w->checked;
        break;
    case TORIRS_PLUGIN_UI_TEXT:
        plugin_copy_str(w->text, sizeof(w->text), text);
        break;
    case TORIRS_PLUGIN_UI_PICK:
        w->selected = value;
        w->value = value;
        plugin_copy_str(w->text, sizeof(w->text), text);
        break;
    default:
        break;
    }

    memset(&ev, 0, sizeof(ev));
    ev.widget_id = w->id;
    ev.action = action;
    ev.value = value;
    ev.text = text ? text : w->text;
    plugin_dispatch_one(host, plugin_index, TORIRS_PLUGIN_EV_UI, &ev);
    return 1;
}

void
PluginHost_WinClearPlugin(struct ToriRS_PluginHost* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return;
    plugin_win_drop(host, plugin_index);
    if( host->win_tab[plugin_index] )
    {
        host->win_tab[plugin_index] = false;
        host->win_tab_title[plugin_index][0] = '\0';
        host->win_revision++;
    }
}

int
PluginHost_WinRevision(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->win_revision;
}

bool
PluginHost_PanelHasPage(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count )
        return false;
    return host->panel_registered[plugin_index] && host->plugins[plugin_index].running;
}

char const*
PluginHost_PanelTitle(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return "";
    return host->panel_title[plugin_index];
}

char const*
PluginHost_PanelIconAsset(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return "";
    return host->panel_icon[plugin_index];
}

int
PluginHost_PanelPreferredWidth(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return 0;
    return host->panel_preferred_width[plugin_index];
}

char const*
PluginHost_PanelBadge(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return "";
    return host->panel_badge[plugin_index];
}

bool
PluginHost_PanelWantsAttention(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    if( plugin_index < 0 || plugin_index >= host->plugin_count ||
        !host->panel_registered[plugin_index] )
        return false;
    return host->panel_attention[plugin_index];
}

uint32_t
PluginHost_PanelIconRevision(
    struct ToriRS_PluginHost const* host, int plugin_index)
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
    if( image->plugin != plugin_index || !image->published ||
        !asset || !asset->data || asset->size <= 0 ||
        asset->size > TORIRS_PLUGIN_PANEL_ICON_BYTES_MAX ||
        strcmp(image->asset, host->panel_icon[plugin_index]) != 0 ||
        image->width <= 0 || image->height <= 0 || image->width > 64 ||
        image->height > 64 )
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
PluginHost_PanelSelect(struct ToriRS_PluginHost* host, int plugin_index)
{
    return PluginHost_PanelSelectView(
        host, plugin_index, TORIRS_PLUGIN_PANEL_VIEW_PAGE);
}

int
PluginHost_PanelView(struct ToriRS_PluginHost const* host)
{
    assert(host);
    return host->panel_active >= 0 ? host->panel_view
                                   : TORIRS_PLUGIN_PANEL_VIEW_PAGE;
}

int
PluginHost_PanelSelectView(struct ToriRS_PluginHost* host, int plugin_index, int view)
{
    assert(host);
    if( view < TORIRS_PLUGIN_PANEL_VIEW_PAGE ||
        view > TORIRS_PLUGIN_PANEL_VIEW_SETTINGS )
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
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation || host->panel_active < 0 )
        return 0;
    return host->panel_widget_count;
}

struct ToriRS_PluginWinWidget const*
PluginHost_PanelWidgetAt(
    struct ToriRS_PluginHost const* host,
    uint32_t selection_generation,
    int widget_index)
{
    assert(host);
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation || host->panel_active < 0 ||
        widget_index < 0 || widget_index >= host->panel_widget_count )
        return NULL;
    return &host->panel_widgets[widget_index];
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
    struct ToriRS_PluginEvPanelLayout ev;
    int plugin;

    assert(host);
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation )
        return 0;
    plugin = host->panel_active;
    if( plugin < 0 || !host->panel_registered[plugin] || !host->plugins[plugin].running )
        return 0;
    if( width < 0 || height < 0 || scale_milli <= 0 ||
        size_class < TORIRS_PLUGIN_PANEL_COMPACT ||
        size_class > TORIRS_PLUGIN_PANEL_EXPANDED ||
        (visible && (width == 0 || height == 0)) )
        return 0;

    if( host->panel_has_layout && host->panel_width == width &&
        host->panel_height == height && host->panel_scale_milli == scale_milli &&
        host->panel_size_class == size_class && host->panel_visible == visible &&
        host->panel_game_visible == game_visible )
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
    plugin_dispatch_one(host, plugin, TORIRS_PLUGIN_EV_PANEL_LAYOUT, &ev);
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
    struct ToriRS_PluginEvPanelAction ev;
    struct ToriRS_PluginWinWidget* widget;
    char event_id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char event_text[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    int plugin;
    int slot;
    bool changed = false;

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
    if( action < TORIRS_PLUGIN_UI_ACTIVATE || action > TORIRS_PLUGIN_UI_KEY )
        return 0;
    widget = &host->panel_widgets[slot];
    if( action >= TORIRS_PLUGIN_UI_DRAG && widget->kind != TORIRS_PLUGIN_W_CUSTOM )
        return 0;

    plugin_copy_str(event_id, sizeof(event_id), widget->id);
    plugin_copy_str(
        event_text,
        sizeof(event_text),
        text ? text : widget->text);

    /* Result state is committed before dispatch, matching win_* compatibility
     * and making the model authoritative for native controls. */
    switch( action )
    {
    case TORIRS_PLUGIN_UI_TOGGLE:
        changed = widget->checked != (value ? 1 : 0) ||
                  widget->value != (value ? 1 : 0);
        widget->checked = value ? 1 : 0;
        widget->value = widget->checked;
        break;
    case TORIRS_PLUGIN_UI_TEXT:
        changed = strcmp(widget->text, event_text) != 0;
        plugin_copy_str(widget->text, sizeof(widget->text), event_text);
        break;
    case TORIRS_PLUGIN_UI_PICK:
        changed = widget->selected != value || widget->value != value ||
                  strcmp(widget->text, event_text) != 0;
        widget->selected = value;
        widget->value = value;
        plugin_copy_str(widget->text, sizeof(widget->text), event_text);
        break;
    default:
        break;
    }
    if( changed )
        plugin_panel_bump(&host->panel_model_revision);

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
    plugin_dispatch_one(host, plugin, TORIRS_PLUGIN_EV_PANEL_ACTION, &ev);
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
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation || host->panel_active < 0 ||
        !host->panel_visible )
        return false;
    slot = plugin_panel_find_serial(host, widget_serial);
    return slot >= 0 && host->panel_widgets[slot].kind == TORIRS_PLUGIN_W_CUSTOM &&
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
    if( selection_generation == 0 ||
        selection_generation != host->panel_selection_generation || host->panel_active < 0 )
        return 0;
    slot = plugin_panel_find_serial(host, widget_serial);
    if( slot < 0 || host->panel_widgets[slot].kind != TORIRS_PLUGIN_W_CUSTOM )
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
    struct ToriRS_PluginEvPanelDraw ev;
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
    ev.scale_milli = host->panel_scale_milli;
    ev.selection_generation = selection_generation;
    ev.widget_serial = widget_serial;

    assert(!host->draw_surface && "panel draw cannot nest another plugin draw pass");
    host->draw_surface = surface;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_PANEL;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_PANEL);
    plugin_dispatch_one(host, plugin, TORIRS_PLUGIN_EV_PANEL_DRAW, &ev);
    host->draw_surface = NULL;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
    return 1;
}
