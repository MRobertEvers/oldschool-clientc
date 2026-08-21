#include "plugin/torirs_plugin_host.h"

#include "ui/uitree_minimenu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    /* Non-NULL only during an EV_MENU_BUILD dispatch. */
    void* menu_cursor;

    struct PluginAsset assets[TORIRS_PLUGIN_ASSETS_MAX];
    int asset_count;

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
        return NULL;

    struct PluginConfigSlot* slot = &ctx->config[ctx->config_count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->schema_index = plugin_schema_index(ctx, key);
    return slot;
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

        host->dispatching = sub.plugin;
        verdict = sub.handler(ctx, payload, sub.userdata);
        host->dispatching = prev_dispatching;

        if( verdict == TORIRS_PLUGIN_CONSUME )
            return TORIRS_PLUGIN_CONSUME;

        /* The handler may have unsubscribed itself or its neighbours; step
         * back so the entry that slid into this slot is not skipped. */
        if( i < host->sub_count[ev] && host->subs[ev][i].handler != sub.handler )
            i--;
    }
    return TORIRS_PLUGIN_PASS;
}

/* Priority-ordered insert, so a plugin that declared a higher priority sees an
 * event first regardless of registration order. Stable within a priority. */
static void
plugin_sub_insert(
    struct ToriRS_PluginHost* host,
    enum ToriRS_PluginEvent ev,
    struct PluginSub const* sub)
{
    assert(host);
    assert(sub);

    int const count = host->sub_count[ev];
    int const priority = host->plugins[sub->plugin].def->priority;
    int at = count;

    for( int i = 0; i < count; i++ )
    {
        int const other = host->plugins[host->subs[ev][i].plugin].def->priority;
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
api_key_held(struct ToriRS_PluginCtx* ctx, int keycode)
{
    assert(ctx);
    return ctx->host->engine.key_held(ctx->host->engine.user, keycode);
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

static int
api_cfg_bool(struct ToriRS_PluginCtx* ctx, char const* key)
{
    char const* value = api_cfg_str(ctx, key);
    if( value[0] == '\0' )
        return 0;
    if( strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 )
        return 1;
    return atoi(value) != 0;
}

static int
api_cfg_int(struct ToriRS_PluginCtx* ctx, char const* key)
{
    return atoi(api_cfg_str(ctx, key));
}

static uint32_t
api_cfg_color(struct ToriRS_PluginCtx* ctx, char const* key)
{
    char const* value = api_cfg_str(ctx, key);
    if( value[0] == '#' )
        return (uint32_t)strtoul(value + 1, NULL, 16) & 0xffffffu;
    if( value[0] == '0' && (value[1] == 'x' || value[1] == 'X') )
        return (uint32_t)strtoul(value + 2, NULL, 16) & 0xffffffu;
    return (uint32_t)strtoul(value, NULL, 10) & 0xffffffu;
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

static void
api_draw_tile(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int tile_x,
    int tile_z,
    int level,
    uint32_t rgb,
    int fill_alpha)
{
    if( !plugin_draw_allow(ctx, surface) )
        return;
    ctx->draw_used += ctx->host->engine.draw_tile(
        ctx->host->engine.user, tile_x, tile_z, level, rgb, fill_alpha);
}

static void
api_draw_hull(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int element_id,
    uint32_t rgb,
    int fill_alpha)
{
    if( !plugin_draw_allow(ctx, surface) )
        return;
    ctx->draw_used +=
        ctx->host->engine.draw_hull(ctx->host->engine.user, element_id, rgb, fill_alpha);
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

/* --------------------------------------------------------------- lifecycle */

struct ToriRS_PluginHost*
PluginHost_New(struct ToriRS_PluginEngine const* engine)
{
    assert(engine);
    assert(engine->world_cycle);
    assert(engine->frame_ms);
    assert(engine->local_player);
    assert(engine->npc_next);
    assert(engine->npc_by_slot);
    assert(engine->player_next);
    assert(engine->obj_next);
    assert(engine->key_held);
    assert(engine->project);
    assert(engine->draw_tile);
    assert(engine->draw_hull);
    assert(engine->draw_line);
    assert(engine->draw_text);
    assert(engine->draw_rect);
    assert(engine->menu_add);
    assert(engine->asset_read);
    assert(engine->asset_write);
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

    struct ToriRS_PluginHost* host = calloc(1, sizeof(*host));
    assert(host);

    host->engine = *engine;
    host->dispatching = -1;

    struct ToriRS_PluginApi api = {
        .abi_version = TORIRS_PLUGIN_ABI,
        .subscribe = api_subscribe,
        .log = api_log,
        .world_cycle = api_world_cycle,
        .frame_ms = api_frame_ms,
        .local_player = api_local_player,
        .npc_next = api_npc_next,
        .npc_by_slot = api_npc_by_slot,
        .player_next = api_player_next,
        .obj_next = api_obj_next,
        .key_held = api_key_held,
        .project = api_project,
        .cfg_bool = api_cfg_bool,
        .cfg_int = api_cfg_int,
        .cfg_color = api_cfg_color,
        .cfg_str = api_cfg_str,
        .cfg_set = api_cfg_set,
        .menu_add = api_menu_add,
        .draw_tile = api_draw_tile,
        .draw_hull = api_draw_hull,
        .draw_line = api_draw_line,
        .draw_text = api_draw_text,
        .draw_rect = api_draw_rect,
        .asset_load = api_asset_load,
        .asset_data = api_asset_data,
        .asset_save = api_asset_save,
        .asset_release = api_asset_release,
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
        ctx->running = false;
        plugin_objects_destroy_all(host, ctx);
    }
    for( int i = host->asset_count - 1; i >= 0; i-- )
        plugin_asset_drop(host, &host->assets[i]);
    free(host);
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

    int const index = host->plugin_count++;
    struct ToriRS_PluginCtx* ctx = &host->plugins[index];
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->def = def;
    ctx->index = index;
    ctx->enabled = !def->disabled_by_default;
    ctx->running = false;
    snprintf(ctx->name, sizeof(ctx->name), "%s", def->name);
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

void
PluginHost_SetEnabled(struct ToriRS_PluginHost* host, int plugin_index, bool enabled)
{
    assert(host);

    struct ToriRS_PluginCtx* ctx = plugin_at(host, plugin_index);
    if( ctx->enabled == enabled )
        return;

    /* Enable state is saved state: without this a panel toggle would hold for
     * the session and be forgotten at the next launch. */
    host->config_dirty = true;

    if( !enabled )
    {
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
        plugin_assets_drop_plugin(host, plugin_index);
        ctx->running = false;
        ctx->enabled = false;
        return;
    }

    ctx->enabled = true;
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
    ev.surface = host;
    plugin_dispatch(host, TORIRS_PLUGIN_EV_DRAW_WORLD, &ev);
    host->draw_surface = NULL;
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

    /* Bounded by the store's own fixed caps, so one sizing pass is enough. */
    size_t cap = 128;
    for( int i = 0; i < host->plugin_count; i++ )
        cap += 96 + (size_t)host->plugins[i].config_count *
                        (64 + TORIRS_PLUGIN_CONFIG_VALUE_MAX + 4);

    char* buf = malloc(cap);
    assert(buf);

    size_t at = 0;
    at += (size_t)snprintf(buf + at, cap - at, "; torirs plugin settings\n");

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
