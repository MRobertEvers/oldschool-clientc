#include "plugin/porcelain/torirs_porcelain.h"
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
 *
 * WHAT PORCELAIN IS HERE FOR, AND WHAT IT IS NOT
 *
 * Not the picture. Three primitives, in one order, with the same six
 * arguments each, on every lane. The ledger calls this plugin the yardstick
 * and every one of its behaviour rows is SUPPORTED; a port that moved a pixel
 * of it would be a defect and not a port. So the draw path below is untouched,
 * and it is untouched on purpose.
 *
 * It does not ask for Porcelain_DrawContext. That verb answers the pass's
 * drawable rectangle and whether that rectangle IS the canvas, and a world
 * tile is named in SCENE terms: v2_builder_world_tile hands the address
 * straight to api_draw_tile without the scope's origin or clip touching it, so
 * the pass region is not an input to a single one of these three calls.
 * Returning early on the false it can answer would SUPPRESS three markers that
 * would have drawn correctly, which is a behaviour change dressed as a check.
 *
 * It does not ask Porcelain_Has("map_flag") either: the destination is always
 * answered by the snapshot, so that capability could never be false, and a
 * capability that cannot answer no is not a capability.
 *
 * What the layer IS opened for is the refusal channel: nothing here calls a
 * Porcelain verb, so a marker this plugin could not draw would go unsaid
 * without it, exactly as the Lua twin's own open() comment says. It declares
 * no limitation -- there is none left to declare. @see tileind_on_start.
 *
 * There is no describe, no fence and no commit: nothing here is retained, so
 * there is nothing to reconcile. After on_start this plugin makes no layer
 * call at all, and tileind_v2_test.c reads the counters to say so.
 */

struct TileindState
{
    /*
     * The handle exists only to open the findings channel in on_start and to
     * be closed in on_stop. It is per-instance rather than file-scope because
     * a handle is a slot in the layer's fixed table, and a disable/enable
     * round trip through the settings panel would otherwise leak one every
     * time.
     */
    struct Porcelain* porcelain;
};

/* Named by tileind_on_start, which opens the layer against this plugin's own
 * definition; the definition itself is at the foot of the file. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_TILEIND;

/*
 * Start declares NOTHING, and that is the point worth writing down.
 *
 * There was a Porcelain_ExpectUnsupported("draw_refusal_readout") here, and it
 * is gone with the limitation it declared. It was true when it was written:
 * api_draw_tile returned void, so v2_builder_world_tile answered
 * TORIRS_RESULT_OK whatever the frame allotment had said, and a truncated
 * overlay was something this plugin could name but never report. The
 * nxt-highlight port made that refusal an ANSWER -- api_draw_tile is
 * `static enum ToriRS_Result` and returns TORIRS_RESULT_BUDGET when
 * plugin_draw_allow refuses, and v2_builder_world_tile returns that answer
 * verbatim -- so keeping the declaration files a FALSE one on every capture,
 * on every lane, marked expected.
 *
 * A declaration has to be true in both directions or it is not evidence, and
 * a false one is exactly what the bidirectional rule exists to catch. It was
 * not caught from the other side because an expect_unsupported cannot go
 * stale on its own: it fires unconditionally, so the gate's staleness check
 * sees a declaration that always fires and says nothing. What caught it is the
 * parity twin -- script/plugins/tile_indicator.lua retired the same
 * declaration in the same words and files no finding at all, and two twins
 * that exist to be compared disagreed by exactly one line per capture.
 *
 * What is NOT done here, and deliberately: reading the answer. This overlay's
 * shape is that no frame costs a single call into the layer, and the twin
 * calls draw.world_tile raw for the same reason; routing the three markers
 * through Porcelain_Tile to test a result that only ever differs at the 512th
 * primitive would make the twins diverge to report a case neither of them
 * reaches. That is the owner's call to revisit, not this line's.
 *
 * Unlike the Lua twin there is no "no layer" arm: Lua reaches these verbs
 * through api->porcelain, which a host may not have installed, while a C
 * plugin calls the library directly and Porcelain_Open cannot fail.
 */
static void
tileind_on_start(
    struct ToriRS_Api* api,
    void* state_ptr)
{
    struct TileindState* state = state_ptr;

    assert(api);
    assert(state_ptr);
    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_TILEIND, state);
}

static void
tileind_on_stop(
    struct ToriRS_Api* api,
    void* state_ptr)
{
    struct TileindState* state = state_ptr;

    (void)api;
    assert(state_ptr);
    Porcelain_Close(state->porcelain);
    state->porcelain = NULL;
}

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
        (void)draw->world_tile(
            draw,
            hover_x,
            hover_z,
            hover_level,
            tileind_config_color(api, "hover_fill_color", 0xFFFFFFu),
            tileind_config_color(api, "hover_color", 0xFFFFFFu),
            /* This plugin's markers have no thickness setting -- they are an
             * outline colour, a fill colour and the fill's opacity -- so they
             * ask for the width every tile border was drawn at before the
             * thickness was a parameter, and are unchanged by its existing. */
            TORIRS_TILE_OUTLINE_WIDTH_DEFAULT,
            tileind_config_int(api, "hover_fill_alpha", 0),
            /* Over the scene, for all three markers below as well.
             *
             * Not the default falling through: these are the PLUGIN's own
             * markers, not a cache highlight group's, and nothing here carries
             * the settings row that would say otherwise. A tile indicator you
             * cannot see because you are standing on it has not indicated the
             * tile, which is the whole reason the reference draws its own over
             * the scene too. */
            TORIRS_TILE_ON_TOP);

    if( !api->world.local_player(api, &me) )
        return;

    (void)draw->world_tile(
        draw,
        me.true_x,
        me.true_z,
        me.level,
        tileind_config_color(api, "true_fill_color", 0x00FFFFu),
        tileind_config_color(api, "true_color", 0x00FFFFu),
        TORIRS_TILE_OUTLINE_WIDTH_DEFAULT,
        tileind_config_int(api, "true_fill_alpha", 40),
        TORIRS_TILE_ON_TOP);

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
        (void)draw->world_tile(
            draw,
            me.dest_x,
            me.dest_z,
            me.level,
            tileind_config_color(api, "dest_fill_color", 0xFFFF00u),
            tileind_config_color(api, "dest_color", 0xFFFF00u),
            TORIRS_TILE_OUTLINE_WIDTH_DEFAULT,
            tileind_config_int(api, "dest_fill_alpha", 0),
            TORIRS_TILE_ON_TOP);
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
    .state_size = sizeof(struct TileindState),
    .config = &TILEIND_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = tileind_on_start,
        .on_stop = tileind_on_stop,
        .on_draw_world = tileind_draw,
    },
};
