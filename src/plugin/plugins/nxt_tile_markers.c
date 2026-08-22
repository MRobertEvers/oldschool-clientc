#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/*
 * All Settings > Activities > General:
 *
 *   112  Tile highlighting  -- "hold shift and right-click the ground to place
 *                              highlights"
 *   113  Tile highlight colour
 *   117  Clear your highlighted tiles   (a button)
 *
 * Shift + right-click puts a "Mark tile" / "Unmark tile" row on the minimenu,
 * and the row is what places the marker. The setting's own sentence compresses
 * that to "right-click to place", but the click itself cannot be the placement
 * here: EV_MENU_BUILD's non-hover pass is raised by the LEFT click path too
 * (app.c builds a scratch menu to resolve what a left click hit), so acting on
 * the build directly would drop a marker every time someone shift-dropped an
 * item. The row is also the only form of it a person can discover without
 * having read the setting's description.
 *
 * Markers are ABSOLUTE tiles and outlive a scene rebuild, which is the whole
 * reason the plugin api speaks absolute. They are saved as this plugin's own
 * asset rather than as config: a config key is a SETTING, something a person
 * sets once and might hand-edit, and a list of a hundred tiles collected by
 * clicking is neither.
 */

/*
 * Markers held at once.
 *
 * RuneLite's ground markers are unbounded and stored per region; this is a
 * flat list, so it needs a ceiling. 512 is roughly four full screens of marked
 * ground, past which the feature has stopped being a marker and become a
 * second terrain -- and a fixed array keeps the whole plugin allocation-free.
 * Placing past it says so rather than silently doing nothing.
 */
#define NXT_TILE_MARKERS_MAX 512

/** Bytes of the saved list. "-99999 -99999 3\n" is 16, so the ceiling above
 *  fits with room to spare. */
#define NXT_TILE_MARKERS_FILE_MAX (NXT_TILE_MARKERS_MAX * 20)

#define NXT_TILE_MARKERS_ASSET "tiles.txt"

/**
 * How far from the player a marker is still drawn, in tiles.
 *
 * Not an optimisation: api->draw_tile projects through the scene's own height
 * field, and a tile outside the loaded scene has no column to sample. The
 * scene is 104 tiles across, so anything past half of that from the player is
 * off it.
 */
#define NXT_TILE_MARKERS_DRAW_RANGE 52

struct NxtTileMarker
{
    int tile_x;
    int tile_z;
    int level;
};

static struct ToriRS_PluginApi const* g_api;
static struct NxtTileMarker g_markers[NXT_TILE_MARKERS_MAX];
static int g_marker_count;
/** Set when the list has changed and not yet been written back. Saving is
 *  deferred to the next frame so a burst of placements is one write. */
static bool g_markers_dirty;

/* The menu tag: one row, so the value only has to be non-zero and its own. */
#define NXT_TILE_MARKER_TAG 0x4D41524Bu /* 'MARK' */

static int
nxt_marker_find(int tile_x, int tile_z, int level)
{
    for( int i = 0; i < g_marker_count; i++ )
        if( g_markers[i].tile_x == tile_x && g_markers[i].tile_z == tile_z &&
            g_markers[i].level == level )
            return i;
    return -1;
}

static void
nxt_marker_toggle(struct ToriRS_PluginCtx* ctx, int tile_x, int tile_z, int level)
{
    int const at = nxt_marker_find(tile_x, tile_z, level);

    assert(ctx);

    if( at >= 0 )
    {
        /* Order is not meaningful -- every marker is drawn -- so the hole is
         * filled from the end rather than by shifting the tail down. */
        g_markers[at] = g_markers[--g_marker_count];
        g_markers_dirty = true;
        return;
    }

    if( g_marker_count >= NXT_TILE_MARKERS_MAX )
    {
        g_api->log(
            ctx,
            "already holding %d marked tiles, which is all this can hold; clear some "
            "with All Settings > Activities > \"Clear your highlighted tiles\"",
            NXT_TILE_MARKERS_MAX);
        return;
    }

    g_markers[g_marker_count].tile_x = tile_x;
    g_markers[g_marker_count].tile_z = tile_z;
    g_markers[g_marker_count].level = level;
    g_marker_count++;
    g_markers_dirty = true;
}

/* ---- persistence ------------------------------------------------------- */

static void
nxt_markers_parse(char const* text, int size)
{
    int pos = 0;

    assert(text);

    g_marker_count = 0;
    while( pos < size && g_marker_count < NXT_TILE_MARKERS_MAX )
    {
        int x = 0;
        int z = 0;
        int level = 0;
        int consumed = 0;

        /* `%n` after the fields, so a trailing line without a newline is still
         * consumed and the loop cannot spin on it. */
        if( sscanf(&text[pos], "%d %d %d%n", &x, &z, &level, &consumed) != 3 )
            break;
        pos += consumed;
        while( pos < size && (text[pos] == '\n' || text[pos] == '\r' || text[pos] == ' ') )
            pos++;

        g_markers[g_marker_count].tile_x = x;
        g_markers[g_marker_count].tile_z = z;
        g_markers[g_marker_count].level = level;
        g_marker_count++;
    }
}

static void
nxt_markers_save(struct ToriRS_PluginCtx* ctx)
{
    static char buf[NXT_TILE_MARKERS_FILE_MAX];
    int used = 0;

    assert(ctx);

    for( int i = 0; i < g_marker_count; i++ )
    {
        int const wrote = snprintf(
            &buf[used],
            sizeof(buf) - (size_t)used,
            "%d %d %d\n",
            g_markers[i].tile_x,
            g_markers[i].tile_z,
            g_markers[i].level);
        if( wrote <= 0 || wrote >= (int)(sizeof(buf) - (size_t)used) )
            break;
        used += wrote;
    }
    g_api->asset_save(ctx, NXT_TILE_MARKERS_ASSET, buf, used);
    g_markers_dirty = false;
}

static enum ToriRS_PluginVerdict
nxt_markers_asset(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvAsset* ev = (struct ToriRS_PluginEvAsset*)event;
    void const* data;
    int size = 0;

    assert(ctx);
    assert(ev);

    if( !ev->name || strcmp(ev->name, NXT_TILE_MARKERS_ASSET) != 0 )
        return TORIRS_PLUGIN_PASS;
    /* No file yet is the ordinary first run, not a fault: there is nothing to
     * say and nothing to parse. */
    if( !ev->ok )
        return TORIRS_PLUGIN_PASS;

    data = g_api->asset_data(ctx, NXT_TILE_MARKERS_ASSET, &size);
    if( data && size > 0 )
        nxt_markers_parse((char const*)data, size);
    /* The bytes are the host's and are not needed again: the parsed list is
     * the live copy from here on, and a save rewrites the file whole. */
    g_api->asset_release(ctx, NXT_TILE_MARKERS_ASSET);
    return TORIRS_PLUGIN_PASS;
}

/* ---- the menu row ------------------------------------------------------ */

static enum ToriRS_PluginVerdict
nxt_markers_menu_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvMenuBuild* ev = (struct ToriRS_PluginEvMenuBuild*)event;
    int tile_x;
    int tile_z;
    int level;

    assert(ctx);
    assert(ev);

    /* The hover pass runs every frame to build the top-left hover text; a row
     * added there would be offered as the LEFT-click action. */
    if( ev->hover_pass )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->varbit(ctx, NXT_VARBIT_TILE_MARKERS) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->key_held(ctx, TORIRS_PLUGIN_KEY_SHIFT) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->hover_tile(ctx, &tile_x, &tile_z, &level) )
        return TORIRS_PLUGIN_PASS;

    g_api->menu_add(
        ctx,
        ev,
        nxt_marker_find(tile_x, tile_z, level) >= 0 ? "Unmark tile" : "Mark tile",
        NXT_TILE_MARKER_TAG);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
nxt_markers_menu_select(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvMenuSelect* ev = (struct ToriRS_PluginEvMenuSelect*)event;
    int tile_x;
    int tile_z;
    int level;

    assert(ctx);
    assert(ev);

    if( !ev->owned || ev->plugin_tag != NXT_TILE_MARKER_TAG )
        return TORIRS_PLUGIN_PASS;

    /*
     * The tile is read again here rather than carried in the tag.
     *
     * A tag is 32 bits and a tile is 3 numbers, so it would have to be packed
     * and could not hold a negative coordinate; and the pointer has not moved
     * between the row being built and the row being chosen, because the menu
     * is modal over the world. Re-reading is both simpler and exact.
     */
    if( g_api->hover_tile(ctx, &tile_x, &tile_z, &level) )
        nxt_marker_toggle(ctx, tile_x, tile_z, level);
    return TORIRS_PLUGIN_CONSUME;
}

/* ---- the panel's own rows ---------------------------------------------- */

static enum ToriRS_PluginVerdict
nxt_markers_setting(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvSetting* ev = (struct ToriRS_PluginEvSetting*)event;

    assert(ctx);
    assert(ev);

    if( ev->setting_id != NXT_SETTING_CLEAR_TILE_MARKERS )
        return TORIRS_PLUGIN_PASS;

    /* Clearing an already-empty list still writes: the file may hold markers
     * this session never loaded (a read that failed), and "clear" has to mean
     * cleared on disk too. */
    g_marker_count = 0;
    g_markers_dirty = true;
    return TORIRS_PLUGIN_PASS;
}

/* ---- drawing ----------------------------------------------------------- */

static enum ToriRS_PluginVerdict
nxt_markers_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    struct ToriRS_PluginPlayerSnap me;
    uint32_t rgb;

    assert(ctx);
    assert(ev);

    if( g_marker_count == 0 )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->varbit(ctx, NXT_VARBIT_TILE_MARKERS) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->local_player(ctx, &me) )
        return TORIRS_PLUGIN_PASS;

    rgb = g_api->setting_color(ctx, NXT_VARP_TILE_MARKER_COLOR, NXT_COL_TILE_MARKER);

    for( int i = 0; i < g_marker_count; i++ )
    {
        int const dx = g_markers[i].tile_x - me.true_x;
        int const dz = g_markers[i].tile_z - me.true_z;

        if( g_markers[i].level != me.level )
            continue;
        if( dx < -NXT_TILE_MARKERS_DRAW_RANGE || dx > NXT_TILE_MARKERS_DRAW_RANGE ||
            dz < -NXT_TILE_MARKERS_DRAW_RANGE || dz > NXT_TILE_MARKERS_DRAW_RANGE )
            continue;

        g_api->draw_tile(
            ctx,
            ev->surface,
            g_markers[i].tile_x,
            g_markers[i].tile_z,
            g_markers[i].level,
            rgb,
            rgb,
            0);
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
nxt_markers_frame(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    if( g_markers_dirty )
        nxt_markers_save(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
nxt_markers_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    /* 1 means it was already resident and no EV_ASSET follows, so the parse
     * has to happen on both paths. */
    if( g_api->asset_load(ctx, NXT_TILE_MARKERS_ASSET) == 1 )
    {
        int size = 0;
        void const* data = g_api->asset_data(ctx, NXT_TILE_MARKERS_ASSET, &size);
        if( data && size > 0 )
            nxt_markers_parse((char const*)data, size);
    }
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_markers_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, nxt_markers_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_ASSET, nxt_markers_asset, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_FRAME_START, nxt_markers_frame, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_BUILD, nxt_markers_menu_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_SELECT, nxt_markers_menu_select, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_SETTING, nxt_markers_setting, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_markers_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_TILE_MARKERS = {
    .name = "nxt-tile-markers",
    .title = "Tile highlighting (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_markers_init,
    .shutdown = NULL,
};
