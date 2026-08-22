#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>

/*
 * All Settings > Activities > General: the three tile indicators.
 *
 *   172 / 173 / 174   Highlight hovered tile      (+ always on top, + colour)
 *   175 / 176 / 177   Highlight current tile      (+ always on top, + colour)
 *   178 / 179 / 180   Highlight destination tile  (+ always on top, + colour)
 *
 * The distinction between the three is the whole point, and it is the same one
 * `tile-indicator-c` documents: the draw position is interpolated between
 * tiles every frame, the server only ever knows the entity as being on ONE
 * tile, and anything timed against the server is reasoning about that tile.
 *
 * This is NOT a second copy of that plugin. `tile-indicator-c` and its Lua
 * twin are ports of RuneLite's tile indicator, configured from the plugin
 * panel, and they exist to prove the plugin contract is language-agnostic.
 * This one implements three rows of the cache's own settings panel: no config
 * of its own, no roster row, and every question it asks -- on or off, what
 * colour -- is answered by a varbit the user set in All Settings. Two plugins
 * can draw a marker under the player without either being redundant, because
 * they answer to different switches; whoever does not want both turns one off
 * where that one's switch is.
 *
 * ---- "- Always on top" (173 / 176 / 179) is READ AND NOT HONOURED ----
 *
 * The row means "draw the marker over the scenery in front of it". This
 * client's overlay layer, which is where api->draw_tile lands, is composited
 * after the scene and is therefore ALWAYS on top -- so the ON state is what
 * you get either way and the OFF state cannot be produced at all. Honouring it
 * needs a depth-tested ground primitive (ToriDraw's z-buffer scratch is per
 * MODEL, TORIDRAW_SCENE_MODEL_ZBUFFER, and the overlay layer has none), which
 * is a renderer change and not a plugin one.
 *
 * The varbit is deliberately still read below, and deliberately not acted on.
 * Reading it keeps the dependency visible where the fix will go; acting on it
 * -- hiding the marker when "always on top" is off -- would be worse than
 * doing nothing, because that is not what the row says and a user turning it
 * off would lose the marker entirely.
 */

static struct ToriRS_PluginApi const* g_api;

/** One marker: the enable varbit, the colour varp, and the colour the panel
 *  shows before anyone has chosen one (`param_1230`). */
struct NxtTileRow
{
    int varbit_on;
    int varbit_on_top;
    int varp_color;
    uint32_t default_color;
};

static struct NxtTileRow const ROW_HOVER = {
    NXT_VARBIT_HOVER_TILE, NXT_VARBIT_HOVER_TILE_ONTOP,
    NXT_VARP_HOVER_TILE_COLOR, NXT_COL_HOVER_TILE
};
static struct NxtTileRow const ROW_CURRENT = {
    NXT_VARBIT_CURRENT_TILE, NXT_VARBIT_CURRENT_TILE_ONTOP,
    NXT_VARP_CURRENT_TILE_COLOR, NXT_COL_CURRENT_TILE
};
static struct NxtTileRow const ROW_DEST = {
    NXT_VARBIT_DEST_TILE, NXT_VARBIT_DEST_TILE_ONTOP,
    NXT_VARP_DEST_TILE_COLOR, NXT_COL_DEST_TILE
};

/*
 * Outline only, no wash.
 *
 * The reference's markers are outlines: the tile under the pointer moves with
 * every mouse motion and a filled quad following the cursor across the ground
 * reads as a selection, not as an indicator. `fill_alpha` 0 is what draw_tile
 * documents as "outline only", so the fill colour is the outline's and is
 * never used.
 */
static void
nxt_tile_draw(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    struct NxtTileRow const* row,
    int tile_x,
    int tile_z,
    int level)
{
    uint32_t rgb;

    assert(ctx);
    assert(row);

    if( !g_api->varbit(ctx, row->varbit_on) )
        return;
    rgb = g_api->setting_color(ctx, row->varp_color, row->default_color);
    g_api->draw_tile(ctx, surface, tile_x, tile_z, level, rgb, rgb, 0);
}

static enum ToriRS_PluginVerdict
nxt_tile_indicator_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
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
     * need a local player: it is the tile a click would act on, at the level
     * the pick landed on. On a bridge deck that is a different mesh from the
     * player's, and using the player's would drop the marker to the ground
     * underneath.
     */
    if( g_api->hover_tile(ctx, &hover_x, &hover_z, &hover_level) )
        nxt_tile_draw(ctx, ev->surface, &ROW_HOVER, hover_x, hover_z, hover_level);

    if( !g_api->local_player(ctx, &me) )
        return TORIRS_PLUGIN_PASS;

    nxt_tile_draw(ctx, ev->surface, &ROW_CURRENT, me.true_x, me.true_z, me.level);

    /*
     * The destination is the MAP FLAG and nothing else, which is why the
     * snapshot unfolds it into flag_x/flag_z: dest_* equals true_* both when
     * the walk has ended and when the player is standing on their own
     * destination, and only flag_x can say "there is no destination".
     */
    if( me.flag_x >= 0 && (me.dest_x != me.true_x || me.dest_z != me.true_z) )
        nxt_tile_draw(ctx, ev->surface, &ROW_DEST, me.dest_x, me.dest_z, me.level);

    return TORIRS_PLUGIN_PASS;
}

static void
nxt_tile_indicator_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_tile_indicator_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_TILE_INDICATOR = {
    .name = "nxt-tile-indicator",
    .title = "Tile indicators (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    /* None. Every setting this has is the cache's, in All Settings, and a
     * second copy here would be a second answer to the same question. */
    .config = NULL,
    .hidden = true,
    .init = nxt_tile_indicator_init,
    .shutdown = NULL,
};
