#include "plugin/torirs_plugin_api.h"

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
 * Boat-aware for free: aboard a vessel the api reports deck tiles as
 * STAGING-ABSOLUTE addresses and world_tile draws that band through the
 * hull's live transform, so the markers ride the boat with no code here.
 *
 * The C twin of script/plugins/tile_indicator.lua: same inputs and same
 * output. Keeping both is what proves the contract is language-agnostic
 * rather than Lua-shaped.
 */

static bool
tileind_config_bool(
    struct ToriRS_Api* api,
    char const* key,
    bool fallback)
{
    bool value = fallback;

    assert(api);
    assert(key);
    (void)api->config.get_bool(api, key, &value);
    return value;
}

static int
tileind_config_int(
    struct ToriRS_Api* api,
    char const* key,
    int fallback)
{
    int value = fallback;

    assert(api);
    assert(key);
    (void)api->config.get_int(api, key, &value);
    return value;
}

static uint32_t
tileind_config_color(
    struct ToriRS_Api* api,
    char const* key,
    uint32_t fallback)
{
    uint32_t value = fallback;

    assert(api);
    assert(key);
    (void)api->config.get_color(api, key, &value);
    return value;
}

/*
 * One marker, drawn and then CHECKED.
 *
 * The result is not decoration. The client's world overlay pool is finite, and
 * in a crowded scene -- a hundred health bars, hitsplats and overhead names --
 * it fills, at which point a quad pushed into it is dropped. On screen that is
 * indistinguishable from the plugin being switched off, so the log has to be
 * the thing that tells them apart.
 *
 * Once per session, not once per frame: the condition lasts as long as the
 * crowd does, and a line a frame would bury everything else in the log. The
 * flag is process-wide because the plugin is stateless by design (state_size
 * is 0) and there is exactly one registration of it.
 */
static void
tileind_marker(
    struct ToriRS_Api* api,
    struct ToriRS_Graphics* draw,
    int tile_x,
    int tile_z,
    int level,
    uint32_t fill_rgb,
    uint32_t outline_rgb,
    int alpha)
{
    static bool reported = false;
    enum ToriRS_Result result;

    assert(api);
    assert(draw);

    result = draw->world_tile(
        draw, tile_x, tile_z, level, fill_rgb, outline_rgb, alpha);
    if( result == TORIRS_RESULT_OK )
        return;
    if( reported )
        return;
    reported = true;
    api->core.log(
        api,
        "TILEIND_MARKER_DROPPED result=%d tile=%d,%d level=%d",
        (int)result,
        tile_x,
        tile_z,
        level);
}

static void
tileind_draw(
    struct ToriRS_Api* api,
    void* state,
    struct ToriRS_Graphics* draw)
{
    struct ToriRS_PlayerSnapshot me;
    int hover_x;
    int hover_z;
    int hover_level;

    (void)state;
    assert(api);
    assert(draw);

    /*
     * The pointer's tile first, because it is the one marker that does not
     * depend on a local player being resident: it is the tile a click would
     * act on, drawn at the level the pick landed on rather than the player's
     * -- on a bridge deck those are different meshes, and using the player's
     * would drop the marker to the ground underneath.
     */
    if( tileind_config_bool(api, "show_hover", true) &&
        api->input.hover_tile(api, &hover_x, &hover_z, &hover_level) )
        tileind_marker(
            api,
            draw,
            hover_x,
            hover_z,
            hover_level,
            tileind_config_color(api, "hover_fill_color", 0xFFFFFFu),
            tileind_config_color(api, "hover_color", 0xFFFFFFu),
            tileind_config_int(api, "hover_fill_alpha", 0));

    if( !api->world.local_player(api, &me) )
        return;

    tileind_marker(
        api,
        draw,
        me.true_x,
        me.true_z,
        me.level,
        tileind_config_color(api, "true_fill_color", 0x00FFFFu),
        tileind_config_color(api, "true_color", 0x00FFFFu),
        tileind_config_int(api, "true_fill_alpha", 40));

    if( !tileind_config_bool(api, "show_dest", true) )
        return;

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
        tileind_marker(
            api,
            draw,
            me.dest_x,
            me.dest_z,
            me.level,
            tileind_config_color(api, "dest_fill_color", 0xFFFF00u),
            tileind_config_color(api, "dest_color", 0xFFFF00u),
            tileind_config_int(api, "dest_fill_alpha", 0));
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
static struct ToriRS_ConfigItem const TILEIND_CONFIG[] = {
    { "true_color",       TORIRS_CONFIG_COLOR, "True tile colour",         "#00FFFF", 0, 0,   NULL, 0 },
    { "true_fill_color",  TORIRS_CONFIG_COLOR, "True tile fill",           "#00FFFF", 0, 0,   NULL, 0 },
    { "true_fill_alpha",  TORIRS_CONFIG_INT,   "True tile fill opacity",   "40",      0, 255, NULL, 0 },
    { "dest_color",       TORIRS_CONFIG_COLOR, "Destination colour",       "#FFFF00", 0, 0,   NULL, 0 },
    { "dest_fill_color",  TORIRS_CONFIG_COLOR, "Destination fill",         "#FFFF00", 0, 0,   NULL, 0 },
    { "dest_fill_alpha",  TORIRS_CONFIG_INT,   "Destination fill opacity", "0",       0, 255, NULL, 0 },
    { "show_dest",        TORIRS_CONFIG_BOOL,  "Show destination",         "1",       0, 0,   NULL, 0 },
    { "hover_color",      TORIRS_CONFIG_COLOR, "Hover tile colour",        "#FFFFFF", 0, 0,   NULL, 0 },
    { "hover_fill_color", TORIRS_CONFIG_COLOR, "Hover tile fill",          "#FFFFFF", 0, 0,   NULL, 0 },
    { "hover_fill_alpha", TORIRS_CONFIG_INT,   "Hover tile fill opacity",  "0",       0, 255, NULL, 0 },
    { "show_hover",       TORIRS_CONFIG_BOOL,  "Show hover tile",          "1",       0, 0,   NULL, 0 },
    { NULL,               TORIRS_CONFIG_BOOL,  NULL,                       NULL,      0, 0,   NULL, 0 },
};

/*
 * On by default, like everything else that ships, and it is the twin that
 * ships on.
 *
 * This is the PARITY TWIN of `plugins/tile_indicator.lua`: the two draw the
 * same markers from the same api, which is what keeps the contract honest
 * about being language-agnostic. Both on is not a configuration -- every
 * marker is then composited twice, so the alpha-40 wash under the player comes
 * out roughly twice as strong as the setting says, and turning either twin off
 * leaves the marker on screen, which reads as a broken switch. So the Lua twin
 * ships `enabled=0` in `script/plugins/plugins.ini` and this one carries the
 * feature. That is the author's switch and not the user's: enabling the Lua
 * twin in the roster still works, and the comparison the pair exists for is
 * one line of the manifest away.
 */
static struct ToriRS_ConfigSchema const TILEIND_SCHEMA = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = TILEIND_CONFIG,
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    /* Not "tile-indicator": that name belongs to the Lua script this is the
     * twin of, and a name is what keys the settings section -- two plugins
     * sharing one would overwrite each other's saved colours. */
    .id = "tile-indicator-c",
    /* Says which twin this is, because both are in the roster and "Tile
     * Indicator" twice would leave the reader to guess which switch is which. */
    .title = "Tile Indicator (C)",
    .version = "1.0.0",
    .state_size = 0,
    .config = &TILEIND_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_draw_world = tileind_draw,
    },
};
