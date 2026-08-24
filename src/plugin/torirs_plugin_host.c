#include "plugin/torirs_plugin_host.h"

#include "revconfig/revconfig.h"
#include "ui/uitree_minimenu.h"

#include <assert.h>
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
    bool enabled;
    bool running;
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
};

#define TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX 64

struct PluginRoleReplacement
{
    int plugin;
    char role[TORIRS_PLUGIN_ROLE_NAME_MAX];
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
    PLUGIN_DRAW_SURFACE_FRAME = 2
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
    /* Non-NULL only between the open and close of a draw window. */
    void* draw_surface;
    /* Which surface that is -- enum PluginDrawSurface. Read by the two
     * world-only draw verbs, which have nothing to mean on the other two. */
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
        fprintf(
            stderr,
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
plugin_copy_str(char* dst, size_t cap, char const* src)
{
    snprintf(dst, cap, "%s", src ? src : "");
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
static enum ToriRS_PluginVerdict
plugin_dispatch(struct ToriRS_PluginHost* host, enum ToriRS_PluginEvent ev, void* payload)
{
    assert(host);
    assert(ev >= 0);
    assert(ev < TORIRS_PLUGIN_EV_COUNT);

    enum ToriRS_PluginVerdict verdict = TORIRS_PLUGIN_PASS;
    int const prev_dispatching = host->dispatching;

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
            (void)host->engine.role_anchor(host->engine.user, -1, NULL, 0);
        host->dispatching = sub.plugin;
        verdict = sub.handler(ctx, payload, sub.userdata);
        host->dispatching = prev_dispatching;
        if( ev == TORIRS_PLUGIN_EV_DRAW_CANVAS )
            (void)host->engine.role_anchor(host->engine.user, -1, NULL, 0);

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
           ev == TORIRS_PLUGIN_EV_DRAW_FRAME;
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
        fprintf(
            stderr,
            "plugin: %s subscription to event %d dropped, bus slot full (%d)\n",
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

    va_list args;
    fprintf(stderr, "[%s] ", ctx->name);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
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

static int
api_minimap_rect(
    struct ToriRS_PluginCtx* ctx, int* out_x, int* out_y, int* out_w, int* out_h)
{
    assert(ctx);
    return ctx->host->engine.minimap_rect(
        ctx->host->engine.user, out_x, out_y, out_w, out_h);
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
    if( host->layout_owner >= 0 && host->layout_owner != ctx->index )
        return false;

    host->layout_claim_epoch++;
    host->layout_owner = ctx->index;
    host->layout_canvas = canvas;
    host->layout_fixed_w = canvas == TORIRS_PLUGIN_CANVAS_FIXED ? fixed_w : 0;
    host->layout_fixed_h = canvas == TORIRS_PLUGIN_CANVAS_FIXED ? fixed_h : 0;
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
 * SAFE: the scene's box with the chrome and every reservation taken out.
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
plugin_safe_rect(struct ToriRS_PluginHost* host, struct PluginRect* out)
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

        if( r->plugin < 0 || r->slot != TORIRS_PLUGIN_SLOT_SAFE || r->px <= 0 )
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

    assert(ctx);

    /* A region id out of range is a plugin's arithmetic, not a broken
     * contract -- it may have come from a config key or from a script -- so it
     * answers "this frame has no such region", like any other absent one. */
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_COUNT )
        return 0;

    got = slot == TORIRS_PLUGIN_SLOT_SAFE ? plugin_safe_rect(ctx->host, &box)
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
 * `safe` is answered here and never reaches the engine, for the same reason
 * SLOT_SAFE does not: it is the placeable regions minus every plugin's edge
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

    assert(ctx);
    /* An empty name is a plugin's own string handling, and the answer is the
     * same one an undeclared role gets. */
    if( !role || role[0] == '\0' )
        return 0;

    if( strcmp(role, "safe") == 0 )
        return api_slot_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE, out_x, out_y, out_w, out_h);

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
    /* `safe` and `canvas` are rectangles rather than things that can be
     * hidden, and a derived region is on screen whenever the client is. */
    if( strcmp(role, "safe") == 0 || strcmp(role, "canvas") == 0 )
        return api_role_rect(ctx, role, NULL, NULL, NULL, NULL);
    return ctx->host->engine.role_visible(ctx->host->engine.user, role);
}

static int
api_role_click(struct ToriRS_PluginCtx* ctx, char const* role, int op)
{
    assert(ctx);
    if( !role || role[0] == '\0' )
        return 0;
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
    if( strcmp(role, "safe") == 0 || strcmp(role, "canvas") == 0 )
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
api_role_anchor(struct ToriRS_PluginCtx* ctx, char const* role)
{
    struct ToriRS_PluginHost* host;
    int at;
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
        (void)host->engine.role_anchor(host->engine.user, ctx->index, "", 0);
        return 0;
    }
    if( strcmp(role, "safe") == 0 || strcmp(role, "canvas") == 0 )
    {
        (void)host->engine.role_anchor(host->engine.user, ctx->index, "", 0);
        return 0;
    }

    at = role_replacement_find(host, role);
    if( at >= 0 )
    {
        if( host->role_replacements[at].plugin != ctx->index )
        {
            (void)host->engine.role_anchor(host->engine.user, ctx->index, "", 0);
            return 0;
        }
        replace = 1;
    }
    return host->engine.role_anchor(
        host->engine.user, ctx->index, role, replace);
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
    if( slot != TORIRS_PLUGIN_SLOT_SAFE )
    {
        fprintf(
            stderr,
            "plugin: %s reserved from region %d; only SAFE can be reserved from\n",
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
        fprintf(
            stderr,
            "plugin: %s could not reserve; the reservation table is full (%d)\n",
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
        fprintf(
            stderr,
            "plugin: %s asked for asset '%s'; asset names are bare filenames of "
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
        fprintf(
            stderr,
            "plugin: %s used image handle %d, which it does not own\n",
            ctx->name,
            image);
        return NULL;
    }
    return &ctx->host->images[image];
}

/** Free a slot's scene entry and mark it free. Idempotent. */
static void
plugin_image_drop(struct ToriRS_PluginHost* host, int image)
{
    assert(host);
    assert(image >= 0 && image < TORIRS_PLUGIN_IMAGES_MAX);

    if( host->images[image].published )
        host->engine.image_release(host->engine.user, image);
    memset(&host->images[image], 0, sizeof(host->images[image]));
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
        fprintf(
            stderr,
            "plugin: %s model '%s' would not decode (%d bytes); it draws nothing\n",
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
static void
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
        fprintf(
            stderr,
            "plugin: %s image '%s' would not decode (%d bytes); it draws nothing\n",
            host->plugins[slot->plugin].name,
            slot->asset,
            size);
        return;
    }
    slot->width = w;
    slot->height = h;
    slot->published = true;
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

    slot = plugin_asset_claim(host, ctx->index, name);
    if( !slot )
    {
        fprintf(
            stderr,
            "plugin: %s asset '%s' not loaded, the resident asset table is full (%d)\n",
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
        fprintf(
            stderr,
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
        fprintf(
            stderr,
            "plugin: %s asked to write a screenshot to '%s'; a destination is a path of "
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
                strcmp(host->images[i].asset, asset_name) == 0 )
                plugin_image_publish(host, i, slot->data, slot->size);
        for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
            if( host->models[i].plugin == plugin &&
                strcmp(host->models[i].asset, asset_name) == 0 )
                plugin_model_publish(host, i, slot->data, slot->size);
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
            fprintf(
                stderr,
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
            fprintf(
                stderr,
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
    return true;
}

static void
api_win_clear(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    plugin_win_drop(ctx->host, ctx->index);
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
            fprintf(
                stderr,
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
plugin_draw_require_world(struct ToriRS_PluginCtx* ctx)
{
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
        fprintf(
            stderr,
            "plugin: %s image '%s' not loaded, the resident image table is full (%d)\n",
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
        fprintf(
            stderr,
            "plugin: %s model '%s' not loaded, the resident model table is full (%d)\n",
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
        fprintf(
            stderr,
            "plugin: %s composed image '%s' is %dx%d, which is not a picture\n",
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
        fprintf(
            stderr,
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
api_image_pixels(struct ToriRS_PluginCtx* ctx, int image, uint32_t* out, int max)
{
    struct PluginImage const* slot = plugin_image_owned(ctx, image);

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
    struct PluginImage const* slot = plugin_image_owned(ctx, image);

    if( out_w )
        *out_w = slot ? slot->width : 0;
    if( out_h )
        *out_h = slot ? slot->height : 0;
    return slot && slot->published ? 1 : 0;
}

static void
api_image_release(struct ToriRS_PluginCtx* ctx, int image)
{
    if( !plugin_image_owned(ctx, image) )
        return;
    plugin_image_drop(ctx->host, image);
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
    struct PluginImage const* slot = plugin_image_owned(ctx, image);

    if( !plugin_draw_allow(ctx, surface) )
        return;
    /* Not resident yet is the ORDINARY state for the first frames after a
     * load, so it draws nothing rather than asserting. A handle this plugin
     * does not own is the other thing plugin_image_owned answers with NULL,
     * and that one it has already complained about. */
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
    assert(engine->world_cycle);
    assert(engine->frame_ms);
    assert(engine->layout_set);
    assert(engine->layout_begin);
    assert(engine->layout_end);
    assert(engine->layout_slot);
    assert(engine->layout_slot_skin);
    assert(engine->layout_slot_overlay);
    assert(engine->layout_scrollbar);
    assert(engine->tab_active);
    assert(engine->tab_select);
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
    assert(engine->minimap_rect);
    assert(engine->slot_rect);
    assert(engine->slot_member_rect);
    assert(engine->component_rect);
    assert(engine->role_rect);
    assert(engine->role_visible);
    assert(engine->role_click);
    assert(engine->role_id);
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
    /* -1 is the free marker and 0 is plugin index zero, so the calloc above
     * would have handed every image slot to the first plugin registered. */
    for( int i = 0; i < TORIRS_PLUGIN_IMAGES_MAX; i++ )
        host->images[i].plugin = -1;
    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
        host->models[i].plugin = -1;

    struct ToriRS_PluginApi api = {
        .abi_version = TORIRS_PLUGIN_ABI,
        .subscribe = api_subscribe,
        .log = api_log,
        .notify = api_notify,
        .world_cycle = api_world_cycle,
        .frame_ms = api_frame_ms,
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
        .minimap_rect = api_minimap_rect,
        .slot_rect = api_slot_rect,
        .slot_member_rect = api_slot_member_rect,
        .component_rect = api_component_rect,
        .role_rect = api_role_rect,
        .role_visible = api_role_visible,
        .role_click = api_role_click,
        .role_id = api_role_id,
        .layout_reserve = api_layout_reserve,
        .layout_revision = api_layout_revision,
        .layout_claim = api_layout_claim,
        .layout_release = api_layout_release,
        .layout_owned = api_layout_owned,
        .layout_slot = api_layout_slot,
        .layout_slot_at = api_layout_slot_at,
        .layout_slot_skin = api_layout_slot_skin,
        .layout_scrollbar = api_layout_scrollbar,
        .tab_active = api_tab_active,
        .tab_select = api_tab_select,
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
    };
    host->api = api;
    return host;
}

void
PluginHost_Free(struct ToriRS_PluginHost* host)
{
    if( !host )
        return;

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
        fprintf(stderr, "plugin: table full, refusing '%s'\n", def->name);
        return -1;
    }

    /* A name is an identity, not a label: it keys the settings section, the
     * panel row and the manifest entry. Two plugins sharing one would silently
     * share all three -- each overwriting the other's saved settings -- so the
     * second is refused rather than admitted to fight over them. */
    if( PluginHost_IndexOf(host, def->name) >= 0 )
    {
        fprintf(
            stderr,
            "plugin: '%s' is already registered; the second one is refused "
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
        fprintf(
            stderr,
            "plugin: '%s' is already registered -- refusing the second one. "
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
            fprintf(
                stderr,
                "plugin: '%s' declares %d config items; the store holds %d. "
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
        if( ctx->running || !ctx->enabled )
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
    plugin_images_drop_plugin(host, plugin_index);
    /* Reservations go too, and that is what makes `reserve` safe to use: a
     * dock that is switched off gives its edge back without anybody asking,
     * and the readouts beside it widen on the next frame. */
    plugin_reserves_drop_plugin(host, plugin_index);
    plugin_models_drop_plugin(host, plugin_index);
    role_replacements_drop_plugin(host, plugin_index);
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
}

void
PluginHost_SetEnabled(struct ToriRS_PluginHost* host, int plugin_index, bool enabled)
{
    assert(host);

    struct ToriRS_PluginCtx* ctx = plugin_at(host, plugin_index);
    if( ctx->enabled == enabled )
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
        return;
    }

    ctx->enabled = true;
    PluginHost_Start(host);
}

void
PluginHost_Reload(struct ToriRS_PluginHost* host, int plugin_index)
{
    struct ToriRS_PluginCtx* ctx;

    assert(host);
    ctx = plugin_at(host, plugin_index);

    /* A disabled plugin has nothing to reload: it is already torn down, and
     * restarting it here would switch it on behind the user's back. */
    if( !ctx->enabled )
        return;

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

    /* Whatever the last run faulted with was about the run that just ended. */
    ctx->error[0] = '\0';

    PluginHost_Start(host);
}

bool
PluginHost_IsEnabled(struct ToriRS_PluginHost const* host, int plugin_index)
{
    assert(host);
    assert(plugin_index >= 0);
    assert(plugin_index < host->plugin_count);
    return host->plugins[plugin_index].enabled;
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
PluginHost_FrameStart(struct ToriRS_PluginHost* host, uint64_t now_ms)
{
    if( !host )
        return;
    /* The frame boundary is also where per-frame draw budgets reset. */
    for( int i = 0; i < host->plugin_count; i++ )
    {
        host->plugins[i].draw_used = 0;
        host->plugins[i].draw_clipped = false;
    }
    if( host->sub_count[TORIRS_PLUGIN_EV_FRAME_START] == 0 )
        return;

    struct ToriRS_PluginEvFrame ev = { now_ms };
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
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_DRAW_WORLD] == 0 )
        return;

    /* The surface token is the host's own address: it is not dereferenced,
     * only compared, so that a stale token from a previous frame is caught by
     * the same assert that catches drawing outside the window. */
    struct ToriRS_PluginEvDraw ev;
    host->draw_surface = host;
    host->draw_canvas = PLUGIN_DRAW_SURFACE_WORLD;
    ev.surface = host;
    host->engine.draw_select_canvas(host->engine.user, PLUGIN_DRAW_SURFACE_WORLD);
    plugin_dispatch(host, TORIRS_PLUGIN_EV_DRAW_WORLD, &ev);
    host->draw_surface = NULL;
}

void
PluginHost_DrawCanvas(struct ToriRS_PluginHost* host, int width, int height)
{
    if( !host || host->sub_count[TORIRS_PLUGIN_EV_DRAW_CANVAS] == 0 )
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
        fprintf(
            stderr,
            "plugin: %s asked to lay out against a %dx%d canvas; nothing declared\n",
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
    host->engine.layout_begin(host->engine.user);
    host->layout_declaring = 1;
    plugin_dispatch_one(host, owner, TORIRS_PLUGIN_EV_LAYOUT, &ev);
    host->layout_declaring = 0;
    if( host->layout_owner == owner && host->layout_claim_epoch == claim_epoch )
        host->engine.layout_end(host->engine.user);
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
    if( host->sub_count[TORIRS_PLUGIN_EV_MENU_BUILD] == 0 )
        return;

    assert(menu);
    assert(cursor);

    menu->hover_pass = hover_pass;
    menu->host_cursor = cursor;
    host->menu_cursor = cursor;
    plugin_dispatch(host, TORIRS_PLUGIN_EV_MENU_BUILD, menu);
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
        sub.handler(ctx, payload, sub.userdata);
        host->dispatching = prev_dispatching;

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
        break;
    case TORIRS_PLUGIN_UI_TEXT:
        plugin_copy_str(w->text, sizeof(w->text), text);
        break;
    case TORIRS_PLUGIN_UI_PICK:
        w->selected = value;
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
