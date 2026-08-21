#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>

/*
 * Tile indicator: the local player's TRUE tile, where they are walking to, and
 * the tile under the mouse pointer.
 *
 * The distinction is the whole point. The draw position is interpolated
 * between tiles every frame, but the server only ever knows the entity as
 * being on one tile -- route[0] of the pathing queue, which the snapshot
 * carries as true_x/true_z. Anything timed against the server (a tick-perfect
 * step, a safespot, a stall) is reasoning about that tile, not the one the
 * model happens to be sliding across.
 *
 * The C twin of script/plugins/tile_indicator.lua: same events, same api,
 * same output. Keeping both is what proves the contract is language-agnostic
 * rather than Lua-shaped.
 */

static struct ToriRS_PluginApi const* g_api;

static enum ToriRS_PluginVerdict
tileind_draw(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    struct ToriRS_PluginPlayerSnap me;
    int hover_x;
    int hover_z;
    int hover_level;

    assert(ctx);
    assert(ev);

    /*
     * The pointer's tile first, because it is the one marker that does not
     * depend on a local player being resident: it is the tile a click would
     * act on, drawn at the level the pick landed on rather than the player's
     * -- on a bridge deck those are different meshes, and using the player's
     * would drop the marker to the ground underneath.
     */
    if( g_api->cfg_bool(ctx, "show_hover") &&
        g_api->hover_tile(ctx, &hover_x, &hover_z, &hover_level) )
        g_api->draw_tile(
            ctx,
            ev->surface,
            hover_x,
            hover_z,
            hover_level,
            g_api->cfg_color(ctx, "hover_color"),
            g_api->cfg_int(ctx, "hover_fill"));

    if( !g_api->local_player(ctx, &me) )
        return TORIRS_PLUGIN_PASS;

    g_api->draw_tile(
        ctx,
        ev->surface,
        me.true_x,
        me.true_z,
        me.level,
        g_api->cfg_color(ctx, "true_color"),
        g_api->cfg_int(ctx, "true_fill"));

    if( !g_api->cfg_bool(ctx, "show_dest") )
        return TORIRS_PLUGIN_PASS;

    /*
     * Where the walk ends, which is the map flag and nothing else -- the same
     * value RuneLite's tile indicator draws through
     * Client.getLocalDestinationLocation().
     *
     * Reading the route queue instead is what made this marker vanish a tick
     * after the click: that queue is the interpolator's history, so its far
     * end trails behind the player and never holds the destination at all.
     * The flag is set from the routed destination and cleared on arrival,
     * which is exactly as long as a destination marker should live.
     */
    if( me.dest_x != me.true_x || me.dest_z != me.true_z )
        g_api->draw_tile(
            ctx,
            ev->surface,
            me.dest_x,
            me.dest_z,
            me.level,
            g_api->cfg_color(ctx, "dest_color"),
            0);

    return TORIRS_PLUGIN_PASS;
}

static void
tileind_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, tileind_draw, NULL);
}

static struct ToriRS_PluginConfigItem const TILEIND_CONFIG[] = {
    { "true_color", TORIRS_PLUGIN_CFG_COLOR, "True tile colour",   "#00FFFF", 0, 0,   NULL },
    { "true_fill",  TORIRS_PLUGIN_CFG_INT,   "True tile fill",     "40",      0, 255, NULL },
    { "dest_color", TORIRS_PLUGIN_CFG_COLOR, "Destination colour", "#FFFF00", 0, 0,   NULL },
    { "show_dest",  TORIRS_PLUGIN_CFG_BOOL,  "Show destination",   "1",       0, 0,   NULL },
    { "hover_color", TORIRS_PLUGIN_CFG_COLOR, "Hover tile colour", "#FFFFFF", 0, 0,   NULL },
    { "hover_fill", TORIRS_PLUGIN_CFG_INT,   "Hover tile fill",    "0",       0, 255, NULL },
    { "show_hover", TORIRS_PLUGIN_CFG_BOOL,  "Show hover tile",    "1",       0, 0,   NULL },
    { NULL,         TORIRS_PLUGIN_CFG_BOOL,  NULL,                 NULL,      0, 0,   NULL },
};

/* Off by default, and for two reasons: a marker under the player is a
 * debugging aid nobody asked for on first launch, and this is the parity twin
 * of the Lua script -- running both at once draws every tile twice. Switch it
 * on to compare the two implementations. */
struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND = {
    /* Not "tile-indicator": that name belongs to the Lua script this is the
     * twin of, and a name is what keys the settings section -- two plugins
     * sharing one would overwrite each other's saved colours. */
    .name = "tile-indicator-c",
    .version = "1.0.0",
    .priority = 0,
    .config = TILEIND_CONFIG,
    .disabled_by_default = true,
    .init = tileind_init,
    .shutdown = NULL,
};
