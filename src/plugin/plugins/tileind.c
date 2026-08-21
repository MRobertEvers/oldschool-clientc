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
            g_api->cfg_color(ctx, "hover_fill_color"),
            g_api->cfg_int(ctx, "hover_fill_alpha"));

    if( !g_api->local_player(ctx, &me) )
        return TORIRS_PLUGIN_PASS;

    g_api->draw_tile(
        ctx,
        ev->surface,
        me.true_x,
        me.true_z,
        me.level,
        g_api->cfg_color(ctx, "true_color"),
        g_api->cfg_color(ctx, "true_fill_color"),
        g_api->cfg_int(ctx, "true_fill_alpha"));

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
            g_api->cfg_color(ctx, "dest_fill_color"),
            g_api->cfg_int(ctx, "dest_fill_alpha"));

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

/*
 * Every marker is an outline colour, a fill colour and the fill's opacity.
 *
 * The fill used to be the opacity alone, washed in the outline's colour, and
 * it read as a text box asking for a number between 0 and 255 next to two
 * colour rows -- the one setting on the tab you could not point at. Splitting
 * it puts a picker on the fill, where a picker belongs, and leaves the number
 * to say the one thing a palette entry cannot: how much of the ground below
 * still shows through, with 0 meaning outline only.
 *
 * The opacities keep the markers looking as they did -- a light wash under the
 * player, nothing under the destination or the pointer -- so the new pickers
 * are an offer rather than a change of appearance.
 */
static struct ToriRS_PluginConfigItem const TILEIND_CONFIG[] = {
    { "true_color",       TORIRS_PLUGIN_CFG_COLOR, "True tile colour",         "#00FFFF", 0, 0,   NULL },
    { "true_fill_color",  TORIRS_PLUGIN_CFG_COLOR, "True tile fill",           "#00FFFF", 0, 0,   NULL },
    { "true_fill_alpha",  TORIRS_PLUGIN_CFG_INT,   "True tile fill opacity",   "40",      0, 255, NULL },
    { "dest_color",       TORIRS_PLUGIN_CFG_COLOR, "Destination colour",       "#FFFF00", 0, 0,   NULL },
    { "dest_fill_color",  TORIRS_PLUGIN_CFG_COLOR, "Destination fill",         "#FFFF00", 0, 0,   NULL },
    { "dest_fill_alpha",  TORIRS_PLUGIN_CFG_INT,   "Destination fill opacity", "0",       0, 255, NULL },
    { "show_dest",        TORIRS_PLUGIN_CFG_BOOL,  "Show destination",         "1",       0, 0,   NULL },
    { "hover_color",      TORIRS_PLUGIN_CFG_COLOR, "Hover tile colour",        "#FFFFFF", 0, 0,   NULL },
    { "hover_fill_color", TORIRS_PLUGIN_CFG_COLOR, "Hover tile fill",          "#FFFFFF", 0, 0,   NULL },
    { "hover_fill_alpha", TORIRS_PLUGIN_CFG_INT,   "Hover tile fill opacity",  "0",       0, 255, NULL },
    { "show_hover",       TORIRS_PLUGIN_CFG_BOOL,  "Show hover tile",          "1",       0, 0,   NULL },
    { NULL,               TORIRS_PLUGIN_CFG_BOOL,  NULL,                       NULL,      0, 0,   NULL },
};

/*
 * On by default, like everything else that ships.
 *
 * It was off, for two reasons that are worth keeping written down. A marker
 * under the player is a debugging aid nobody asked for on first launch; and
 * this is the PARITY TWIN of `plugins/tile_indicator.lua`, so with both
 * running every tile is drawn twice, by two implementations that exist to be
 * compared against each other. Neither reason has gone away -- the default
 * changed, not the situation. If the doubled marker is unwanted, the fix is to
 * drop one of the two from `script/plugins/plugins.ini`, not to put this back
 * to off: which twin runs is a question about the list, and the list is where
 * it can be answered per lane.
 */
struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND = {
    /* Not "tile-indicator": that name belongs to the Lua script this is the
     * twin of, and a name is what keys the settings section -- two plugins
     * sharing one would overwrite each other's saved colours. */
    .name = "tile-indicator-c",
    /* Says which twin this is, because both are in the roster and "Tile
     * Indicator" twice would leave the reader to guess which switch is which. */
    .title = "Tile Indicator (C)",
    .version = "1.0.0",
    .priority = 0,
    .config = TILEIND_CONFIG,
    .init = tileind_init,
    .shutdown = NULL,
};
