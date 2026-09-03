#include "plugin/plugins/plugin_draw.h"
#include "plugin/torirs_plugin_v2.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Loot Tracker -- a port of RuneLite's `loottracker` plugin and the
 * `LootManager` underneath it.
 *
 * What it keeps: one record per SOURCE (a monster's name), carrying how many
 * of them you have killed and the running total of every item they dropped,
 * with a value on each. The page lists the sources, drills into one, and draws
 * that one's drops as the client's own item icons.
 *
 * ---- how a drop is attributed ----
 *
 * The reference's LootManager watches for an npc that is DYING and collects
 * every item that spawns inside that npc's tiles on the same tick. Its
 * `NpcUtil.isDying` is built on `Actor.getHealthRatio() == 0` -- the overhead
 * health bar reaching empty -- plus a hand-written table for the monsters that
 * do not die at zero (the gargoyle family, which is finished with an item, and
 * the bosses that transform).
 *
 * Both halves of that are portable here. `ToriRS_NpcSnapshot` carries
 * `health_ratio`, which is the same number off the same HEADBAR block, so a
 * despawn with a bar at zero is a CONFIRMED kill and is counted whether or not
 * anything dropped. A despawn with hitpoints left -- the gargoyle case, and
 * every npc that simply walked out of view -- is only a CANDIDATE, and becomes
 * a kill if loot lands on its footprint within the window. That is the same
 * two-path shape the reference has, and for the same reason.
 *
 * What is still traded away, stated plainly:
 *
 *   - A drop you cannot SEE is not counted, but that is the server's doing
 *     and not this plugin's: ground items are only sent for tiles near you,
 *     and an item somebody else's kill dropped across the room never reaches
 *     the client at all.
 *   - A gargoyle killed with no drop is not counted, because at zero-with-
 *     hitpoints-left this cannot tell a kill from a monster wandering off.
 *     The reference answers that with a per-npc table; this does not carry
 *     one, because a table of ids is a thing that rots per revision and this
 *     client boots several.
 *   - Two of the same monster dying on adjacent tiles in one tick can hand
 *     one's drop to the other. They are the same record, so the totals are
 *     right and only the per-kill split is not -- and this plugin does not
 *     keep a per-kill split.
 *
 * ---- what an item is WORTH ----
 *
 * There is no live Grand Exchange quote anywhere in this client, so the
 * reference's GE price is not portable. What the cache does state is
 * `ObjType.cost`, which is the number CS2 reads through OC_COST and the same
 * one the ground-item snapshot carries. Both of the config's price sources are
 * computed from it: the cache value itself, and high alchemy -- three fifths
 * of it, the game's own formula -- which is the one figure a player can
 * actually realise for most drops.
 *
 * ---- not ported ----
 *
 * Event loot (barrows, raid chests, clue caskets) is inventory-diff work that
 * needs a reliable "this interface just opened" fence per revision, and PVP
 * loot needs a player-death signal the bus does not raise. Both are named here
 * so the gap is a decision rather than an omission.
 */

/** Sources the session will remember. Past it the least valuable is dropped
 *  rather than the newest refused -- a trip that met one new monster should
 *  not silently stop recording it. */
#define LT_SOURCES_MAX 48
/** Distinct items one source may accumulate. A drop table's whole spread. */
#define LT_ITEMS_MAX 32
/** Deaths waiting for their loot. Deliberately small: a candidate lives for
 *  about two server ticks, and more than eight npcs leaving the scene in that
 *  span is a crowd rather than a kill. */
#define LT_PENDING_MAX 8
/**
 * How long a despawn stays a candidate, in milliseconds.
 *
 * Two server ticks. The reference collects on the death tick alone; this needs
 * the extra one because the despawn and the item spawn arrive from different
 * packets, and nothing here guarantees they were executed in the same client
 * cycle.
 */
#define LT_PENDING_MS 1200
/** How far from the player a despawn may be and still be a candidate kill, in
 *  tiles. Loot outside this is not yours to have seen. */
#define LT_PENDING_RANGE 15

/** Source rows the page will draw before it stops and says so. */
#define LT_ROWS_MAX 24
/** How often the page's numbers are rewritten, in ms. */
#define LT_PANEL_REFRESH_MS 500

/**
 * Bytes of one config value, stated here rather than included.
 *
 * The host's ceiling is TORIRS_PLUGIN_CONFIG_VALUE_MAX, but that lives in the
 * host header and a plugin has no business including one -- the whole contract
 * is torirs_plugin.h. A shorter buffer here is not a disagreement, only a
 * shorter ignore list than the store would have held.
 */
#define LT_CONFIG_VALUE_MAX 192

/* ------------------------------------------------------------------------ */
/* The CS2 loot tracker's own measurements and palette                       */
/*                                                                           */
/* Read out of the torirs_loot_* clientscripts, which build interface 650    */
/* (`loottools`), rather than chosen here. To re-derive:                     */
/*                                                                           */
/*     3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 \           */
/*         --rev osrs239 --out /tmp/cs2loot 2907 3042 3043 3044              */
/*                                                                           */
/*   script2907  the category HEADER: a 33-tall band, a 4px tiled spine at   */
/*               x=2 (graphic_897, or graphic_4948 when ignored), the name    */
/*               in fontmetrics_496 at 0xff981f on the left and its count on  */
/*               the right. Its ops are Collapse/Expand, Clear data and       */
/*               Ignore/Stop ignoring.                                       */
/*   script3042  one item CELL: 40x36, FIVE to a row, background graphic_1120 */
/*               (graphic_155 when ignored) with the obj drawn 36x32 at +2,+2 */
/*               under cc_setoutline(1) -- the BORDERED icon variant. Its ops */
/*               are Check and Ignore/Stop ignoring.                         */
/*   script3043  "No loot to display." in fontmetrics_494, also 0xff981f.     */
/*   script3044  the totals, as "<count><br><value> gp".                     */
/* ------------------------------------------------------------------------ */

/** The header band, and the gap the next one leaves above it. */
#define LT_HEAD_H 33
#define LT_HEAD_GAP 2
/*
 * The band PLATE, inset 2 all round.
 *
 * `cc_setsize(4, 33 - 4, 1, 0)` in script2907 is width mode 1, which is PARENT
 * MINUS the value -- not a 4-pixel-wide spine, which is how it reads at a
 * glance and how this was drawn at first. The same sprite is used untiled at
 * 244x42 for the totals band (interface 650:58), so it is a plate that takes
 * whatever size it is given, and it is what puts a border around every band in
 * the game's own tracker.
 */
#define LT_PLATE_INSET 2
/** One item cell, and the five-to-a-row grid script3042 lays out. */
#define LT_CELL_W 40
#define LT_CELL_H 36
#define LT_GRID_COLS 5
/** The first cell row sits `33 + 5` below the header's top. */
#define LT_GRID_TOP (LT_HEAD_H + 5)
/*
 * The TOTALS band, which the game's tracker puts above the categories
 * (interface 650:57..64): a 42-tall plate carrying two lines --
 * "Total count:" and "Total value:" as keys at x=36, their values at x=97,
 * both in fontmetrics_494.
 */
#define LT_TOTALS_H 42
/* ---- the totals band's own controls, as interface 650 places them --------
 *
 * Four 30x30 buttons in a 244-wide band at y+6: the VIEW toggle hard against
 * the left at x=4, and the other three anchored to the RIGHT at 4, 35 and 66
 * in from that edge (the cache authors them at x=651/682/713 in a band that
 * starts at 503 and runs 244 wide). Anchoring the right-hand three from the
 * right is what keeps them in place when the panel is not the cache's width.
 */
#define LT_BTN 30
#define LT_BTN_Y 6
#define LT_BTN_LEFT_X 4
#define LT_BTN_R0 4
#define LT_BTN_R1 35
#define LT_BTN_R2 66

#define LT_TOTALS_ICON 24
#define LT_TOTALS_ICON_X 6
#define LT_TOTALS_KEY_X 36
#define LT_TOTALS_VAL_X 97

/** The interfaces' own orange, which every heading is set in, and the white
 *  the totals' values are. */
#define LT_INK_HEAD 0xFF981Fu
#define LT_INK_VALUE 0xFFFFFFu

/** One item, summed across every kill of one source. */
struct LtItem
{
    int obj_id;
    int quantity;
    /** ObjType.cost for ONE of them, as the cache states it. The displayed
     *  value is this through the configured price source, times quantity. */
    int cost;
    char name[64];
};

/** One source: everything one kind of monster has ever given up this session. */
struct LtSource
{
    /** The store's own id, which addresses its rows. */
    int id;
    char name[64];
    int kills;
    struct LtItem items[LT_ITEMS_MAX];
    int item_count;
    /** Frame clock of the last drop, for the page's ordering. */
    uint64_t last_ms;
};

struct LootTrackerState
{
    struct LtSource source[LT_SOURCES_MAX];
    int source_count;
    int detail;
    int built_detail;
    int built_rows;
    bool page_built;
    bool page_visible;

    struct PluginDraw_Atlas bold;
    struct ToriRS_ImageRef img_over;
    uint32_t* over_px;
    int over_w;
    int over_h;
    struct PluginDraw_Atlas text;
    struct ToriRS_ImageRef img_spine;
    uint32_t* spine_px;
    int spine_w;
    int spine_h;
    struct ToriRS_ImageRef img_cell;
    uint32_t* cell_px;
    int cell_w;
    int cell_h;

    bool expanded[LT_SOURCES_MAX];
    bool drop_view;
    struct ToriRS_ImageRef img_view;
    uint32_t* view_px;
    int view_w;
    int view_h;
    struct ToriRS_ImageRef img_view2;
    uint32_t* view2_px;
    int view2_w;
    int view2_h;
    struct ToriRS_ImageRef img_alch;
    uint32_t* alch_px;
    int alch_w;
    int alch_h;
    struct ToriRS_ImageRef img_collapse;
    uint32_t* collapse_px;
    int collapse_w;
    int collapse_h;
    struct ToriRS_ImageRef img_ignored;
    uint32_t* ignored_px;
    int ignored_w;
    int ignored_h;

    uint32_t* compose;
    int compose_w;
    int compose_h;
    struct ToriRS_ImageRef strip_image;
    uint64_t compose_key;
    int well_w;
    uint64_t next_panel_ms;
    long long session_value;
    int session_kills;
    bool dirty;
    bool redraw_pending;
    uint64_t loot_revision;
};

struct LootTrackerRuntime
{
    struct ToriRS_ApiV2* api;
    struct LootTrackerState* state;
};

#define g_api (rt->api)
#define g_source (rt->state->source)
#define g_source_count (rt->state->source_count)
#define g_detail (rt->state->detail)
#define g_built_detail (rt->state->built_detail)
#define g_built_rows (rt->state->built_rows)
#define g_page_built (rt->state->page_built)
#define g_page_visible (rt->state->page_visible)
#define g_bold (rt->state->bold)
#define g_img_over (rt->state->img_over)
#define g_over_px (rt->state->over_px)
#define g_over_w (rt->state->over_w)
#define g_over_h (rt->state->over_h)
#define g_text (rt->state->text)
#define g_img_spine (rt->state->img_spine)
#define g_spine_px (rt->state->spine_px)
#define g_spine_w (rt->state->spine_w)
#define g_spine_h (rt->state->spine_h)
#define g_img_cell (rt->state->img_cell)
#define g_cell_px (rt->state->cell_px)
#define g_cell_w (rt->state->cell_w)
#define g_cell_h (rt->state->cell_h)
#define g_expanded (rt->state->expanded)
#define g_drop_view (rt->state->drop_view)
#define g_img_view (rt->state->img_view)
#define g_view_px (rt->state->view_px)
#define g_view_w (rt->state->view_w)
#define g_view_h (rt->state->view_h)
#define g_img_view2 (rt->state->img_view2)
#define g_view2_px (rt->state->view2_px)
#define g_view2_w (rt->state->view2_w)
#define g_view2_h (rt->state->view2_h)
#define g_img_alch (rt->state->img_alch)
#define g_alch_px (rt->state->alch_px)
#define g_alch_w (rt->state->alch_w)
#define g_alch_h (rt->state->alch_h)
#define g_img_collapse (rt->state->img_collapse)
#define g_collapse_px (rt->state->collapse_px)
#define g_collapse_w (rt->state->collapse_w)
#define g_collapse_h (rt->state->collapse_h)
#define g_img_ignored (rt->state->img_ignored)
#define g_ignored_px (rt->state->ignored_px)
#define g_ignored_w (rt->state->ignored_w)
#define g_ignored_h (rt->state->ignored_h)
#define g_compose (rt->state->compose)
#define g_compose_w (rt->state->compose_w)
#define g_compose_h (rt->state->compose_h)
#define g_strip_image (rt->state->strip_image)
#define g_compose_key (rt->state->compose_key)
#define g_well_w (rt->state->well_w)
#define g_next_panel_ms (rt->state->next_panel_ms)
#define g_session_value (rt->state->session_value)
#define g_session_kills (rt->state->session_kills)
#define g_dirty (rt->state->dirty)
#define g_redraw_pending (rt->state->redraw_pending)
#define g_loot_revision (rt->state->loot_revision)

/* ------------------------------------------------------------------------ */
/* Names and numbers                                                         */
/* ------------------------------------------------------------------------ */

/** "1,234,567". */
static void
lt_commas(long long value, char* out, size_t out_size)
{
    char digits[32];
    int len;
    size_t at = 0;
    bool negative = value < 0;

    assert(out);
    assert(out_size > 0);

    if( negative )
        value = -value;
    len = snprintf(digits, sizeof(digits), "%lld", value);
    if( len < 0 )
        len = 0;
    if( negative && at + 1 < out_size )
        out[at++] = '-';
    for( int i = 0; i < len; i++ )
    {
        int const remaining = len - i;
        if( i > 0 && remaining % 3 == 0 && at + 1 < out_size )
            out[at++] = ',';
        if( at + 1 < out_size )
            out[at++] = digits[i];
    }
    out[at] = '\0';
}

/**
 * A value the way the tracker's own `~torirs_fmt_kmb` (script 7122) writes
 * one: `fmt_kmb(v, ".", 1)`, which is how `torirs_loot_totals` and every band
 * in interface 650 sets its gp.
 *
 * One decimal, and the decimal is DROPPED when the remainder does not reach
 * it -- 37,100 is "37.1K" and 37,000 is "37K", not "37.0K". Under a thousand
 * there is no unit at all, which is why the reference reads "124 gp" and
 * "0 gp" rather than "0.1K".
 */
static void
lt_kmb(long long value, char* out, size_t out_size)
{
    long long unit;
    char suffix;
    long long whole;
    long long rem;
    long long step;

    assert(out);
    assert(out_size > 0);

    if( value >= 2147483647LL )
    {
        snprintf(out, out_size, "Lots");
        return;
    }
    if( value < 1000 )
    {
        snprintf(out, out_size, "%lld", value < 0 ? 0 : value);
        return;
    }
    if( value >= 1000000000LL )
    {
        unit = 1000000000LL;
        suffix = 'B';
    }
    else if( value >= 1000000LL )
    {
        unit = 1000000LL;
        suffix = 'M';
    }
    else
    {
        unit = 1000LL;
        suffix = 'K';
    }
    step = unit / 10;
    whole = value / unit;
    rem = value - whole * unit;
    if( rem >= step )
        snprintf(out, out_size, "%lld.%lld%c", whole, rem / step, suffix);
    else
        snprintf(out, out_size, "%lld%c", whole, suffix);
}

/**
 * Strip the colour markup a name arrives wearing.
 *
 * Snapshot names come out of the right-click builder "colour tags and all",
 * and a record keyed on the painted spelling would open a second row for the
 * same monster the moment something tinted it.
 */
static void
lt_clean_name(char const* in, char* out, size_t out_size)
{
    size_t at = 0;

    assert(in);
    assert(out);
    assert(out_size > 0);

    while( *in && at + 1 < out_size )
    {
        if( *in == '<' )
        {
            char const* close = strchr(in, '>');
            if( close )
            {
                in = close + 1;
                continue;
            }
        }
        else if( *in == '@' && in[1] && in[2] && in[3] && in[4] == '@' )
        {
            in += 5;
            continue;
        }
        out[at++] = *in++;
    }
    while( at > 0 && (out[at - 1] == ' ' || out[at - 1] == '\t') )
        at--;
    out[at] = '\0';
}

/** Case-insensitive whole-string compare, for the ignore lists. */
static bool
lt_name_eq(char const* a, char const* b)
{
    assert(a);
    assert(b);

    for( ; *a && *b; a++, b++ )
    {
        int const ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        int const cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if( ca != cb )
            return false;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * Is `name` one of the comma-separated entries in `list`?
 *
 * The list is a config key the user typed, so entries are trimmed and matched
 * without regard to case -- "vial" and " Vial " both name the vial. Empty
 * entries match nothing, which is what makes a trailing comma harmless.
 */
static bool
lt_listed(char const* list, char const* name)
{
    char entry[64];

    assert(name);
    if( !list || !list[0] )
        return false;

    while( *list )
    {
        char const* end = strchr(list, ',');
        size_t len = end ? (size_t)(end - list) : strlen(list);
        size_t start = 0;

        while( start < len && (list[start] == ' ' || list[start] == '\t') )
            start++;
        while( len > start && (list[len - 1] == ' ' || list[len - 1] == '\t') )
            len--;
        if( len > start )
        {
            size_t const copy = len - start < sizeof(entry) - 1 ? len - start
                                                                : sizeof(entry) - 1;
            memcpy(entry, list + start, copy);
            entry[copy] = '\0';
            if( lt_name_eq(entry, name) )
                return true;
        }
        if( !end )
            break;
        list = end + 1;
    }
    return false;
}

/** What one of `item` is worth, through the configured price source. */
static char const*
lt_config_string(struct LootTrackerRuntime* rt, char const* key)
{
    char const* value = "";
    (void)g_api->config.get_string(g_api, key, &value);
    return value ? value : "";
}

static long long
lt_unit_value(struct LootTrackerRuntime* rt, struct LtItem const* item)
{
    char const* source;

    assert(rt);
    assert(item);

    source = lt_config_string(rt, "price_source");
    /* High alchemy is three fifths of the cache cost, which is the game's own
     * formula and not an approximation of one. */
    if( source && lt_name_eq(source, "High alchemy") )
        return (long long)item->cost * 3 / 5;
    return item->cost;
}

/** Everything one source's drops are worth. */
static long long
lt_source_value(struct LootTrackerRuntime* rt, struct LtSource const* src)
{
    long long total = 0;

    assert(rt);
    assert(src);
    for( int i = 0; i < src->item_count; i++ )
        total += lt_unit_value(rt, &src->items[i]) * src->items[i].quantity;
    return total;
}

static void
lt_revalue(struct LootTrackerRuntime* rt)
{
    g_session_value = 0;
    for( int i = 0; i < g_source_count; i++ )
        g_session_value += lt_source_value(rt, &g_source[i]);
}

/* ------------------------------------------------------------------------ */
/* The records                                                               */
/* ------------------------------------------------------------------------ */







/**
 * A moment the client recognised.
 *
 * Only the rail flag is taken from these. The drop itself already arrives as a
 * ground-item spawn and counting it twice would double every valuable line;
 * what the chat line adds is that the player should LOOK, which is what the
 * attention marker on the plugin's rail entry says.
 */
static void
lt_game_event(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_GameEvent const* ev)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(ev);
    assert(ev->kind);

    if( strcmp(ev->kind, "valuable_drop") == 0 || strcmp(ev->kind, "pet") == 0 ||
        strcmp(ev->kind, "collection_log") == 0 )
        g_api->panel.attention(g_api, true);
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */
/* The strip                                                                 */
/* ------------------------------------------------------------------------ */

/** Everything the compose needs, resident. */
static int
lt_art_ready(struct LootTrackerRuntime* rt)
{
    assert(rt);
    if( !PluginDraw_AtlasLoad(g_api, &g_bold, "bold") )
        return 0;
    (void)PluginDraw_ImageLoad(
        g_api, "panel_icon.png", &g_img_over, &g_over_px, &g_over_w, &g_over_h);
    /*
     * The band's four controls, cut from the cache: graphic_4915/4916 are the
     * two faces of the view toggle, 4912 the value basis, 4917 collapse-all,
     * 4913 the ignore list. Wanted but not REQUIRED -- a band with a gap where
     * a button belongs still reads, and a page that refuses to draw does not.
     */
    (void)PluginDraw_ImageLoad(
        g_api, "btn_dropview.png", &g_img_view, &g_view_px, &g_view_w, &g_view_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_sourceview.png", &g_img_view2, &g_view2_px, &g_view2_w,
        &g_view2_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_alch.png", &g_img_alch, &g_alch_px, &g_alch_w, &g_alch_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_collapse.png", &g_img_collapse, &g_collapse_px, &g_collapse_w,
        &g_collapse_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_ignored.png", &g_img_ignored, &g_ignored_px, &g_ignored_w,
        &g_ignored_h);
    if( !PluginDraw_AtlasLoad(g_api, &g_text, "text") )
        return 0;
    if( !PluginDraw_ImageLoad(
            g_api, "cat_spine.png", &g_img_spine, &g_spine_px, &g_spine_w,
            &g_spine_h) )
        return 0;
    return PluginDraw_ImageLoad(
        g_api, "cell.png", &g_img_cell, &g_cell_px, &g_cell_w, &g_cell_h);
}

/** How many cell rows one source's drops occupy. */
static int
lt_grid_rows(struct LtSource const* src)
{
    assert(src);
    return (src->item_count + LT_GRID_COLS - 1) / LT_GRID_COLS;
}

/** How tall one source's band is, expanded or not. */
static int
lt_source_h(struct LootTrackerRuntime* rt, int index)
{
    assert(index >= 0);
    assert(index < g_source_count);
    if( !g_expanded[index] || g_source[index].item_count == 0 )
        return LT_HEAD_H + LT_HEAD_GAP;
    return LT_GRID_TOP + lt_grid_rows(&g_source[index]) * LT_CELL_H + LT_HEAD_GAP;
}

/** The whole strip's height: the totals band, then every category. */
/**
 * Every drop in the log, summed across the sources.
 *
 * The DROP view's whole content: interface 650 hides its band container and
 * shows a flat one, because "what did I get" and "what dropped it" are two
 * questions and a list grouped by killer answers only the second. Items are
 * merged by obj id, so twenty goblins' worth of bones is one cell.
 */
static int
lt_collect_drops(struct LootTrackerRuntime* rt, struct LtItem* out, int max)
{
    int n = 0;

    assert(out);
    for( int i = 0; i < g_source_count; i++ )
        for( int j = 0; j < g_source[i].item_count; j++ )
        {
            struct LtItem const* row = &g_source[i].items[j];
            int at = -1;
            for( int k = 0; k < n; k++ )
                if( out[k].obj_id == row->obj_id )
                {
                    at = k;
                    break;
                }
            if( at >= 0 )
            {
                out[at].quantity += row->quantity;
                continue;
            }
            if( n >= max )
                continue;
            out[n++] = *row;
        }
    return n;
}

/** Rows the drop grid needs for `n` cells. */
static int
lt_drop_rows(int n)
{
    return (n + LT_GRID_COLS - 1) / LT_GRID_COLS;
}

static int
lt_strip_h(struct LootTrackerRuntime* rt)
{
    int total = LT_TOTALS_H + LT_HEAD_GAP;

    if( g_drop_view )
    {
        struct LtItem drops[LT_SOURCES_MAX * 4];
        int const n = lt_collect_drops(
            rt, drops, (int)(sizeof(drops) / sizeof(drops[0])));
        return total + (n > 0 ? lt_drop_rows(n) * LT_CELL_H + 6 : LT_HEAD_H);
    }

    for( int i = 0; i < g_source_count; i++ )
        total += lt_source_h(rt, i);
    /* The empty note still needs a line to sit on. */
    return g_source_count > 0 ? total : total + LT_HEAD_H;
}

/** The totals band, which the game's tracker puts above the categories. */
static void
lt_draw_totals(struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h)
{
    char text[64];

    assert(buf);

    if( g_spine_px )
        PluginDraw_Tile(
            buf, w, h, LT_PLATE_INSET, LT_PLATE_INSET, w - LT_PLATE_INSET * 2,
            LT_TOTALS_H - LT_PLATE_INSET * 2, g_spine_px, g_spine_w, g_spine_h, 0);

    /*
     * No panel stone at the left of the band: the thing the cache puts there
     * is the VIEW TOGGLE (interface 650:62 at x=4), and it is drawn below with
     * the band's other three controls. An icon here as well sat on top of it.
     */

    PluginDraw_Text(
        buf, w, h, LT_TOTALS_KEY_X, 8, &g_text, "Total count:", LT_INK_VALUE);
    PluginDraw_Text(
        buf, w, h, LT_TOTALS_KEY_X, 22, &g_text, "Total value:", LT_INK_VALUE);

    lt_kmb(g_session_kills, text, sizeof(text));
    PluginDraw_Text(buf, w, h, LT_TOTALS_VAL_X, 8, &g_text, text, LT_INK_VALUE);
    lt_kmb(g_session_value, text, sizeof(text));
    snprintf(text + strlen(text), sizeof(text) - strlen(text), " gp");
    PluginDraw_Text(buf, w, h, LT_TOTALS_VAL_X, 22, &g_text, text, LT_INK_VALUE);

    /*
     * The band's four controls, at the offsets interface 650 places them.
     *
     * The view toggle wears the face of the view it will move TO -- 4915 while
     * the source bands are up, 4916 while the flat drop grid is -- which is
     * why the two sprites read as each other's opposite rather than as an
     * on/off pair.
     */
    if( g_drop_view ? g_view2_px != NULL : g_view_px != NULL )
    {
        uint32_t const* px = g_drop_view ? g_view2_px : g_view_px;
        int const pw = g_drop_view ? g_view2_w : g_view_w;
        int const ph = g_drop_view ? g_view2_h : g_view_h;
        PluginDraw_Blit(
            buf, w, h, LT_BTN_LEFT_X, LT_BTN_Y, px, pw, ph, 0, 0, pw, ph, 0);
    }
    if( g_ignored_px )
        PluginDraw_Blit(
            buf, w, h, w - LT_BTN_R0 - LT_BTN, LT_BTN_Y, g_ignored_px, g_ignored_w,
            g_ignored_h, 0, 0, g_ignored_w, g_ignored_h, 0);
    if( g_collapse_px )
        PluginDraw_Blit(
            buf, w, h, w - LT_BTN_R1 - LT_BTN, LT_BTN_Y, g_collapse_px, g_collapse_w,
            g_collapse_h, 0, 0, g_collapse_w, g_collapse_h, 0);
    if( g_alch_px )
        PluginDraw_Blit(
            buf, w, h, w - LT_BTN_R2 - LT_BTN, LT_BTN_Y, g_alch_px, g_alch_w,
            g_alch_h, 0, 0, g_alch_w, g_alch_h, 0);
}

/** The source a click at `y` landed on, and how far into it. -1 for neither. */
static int
lt_source_at(struct LootTrackerRuntime* rt, int y, int* out_local_y)
{
    int top = LT_TOTALS_H + LT_HEAD_GAP;

    for( int i = 0; i < g_source_count; i++ )
    {
        int const h = lt_source_h(rt, i);
        if( y >= top && y < top + h )
        {
            if( out_local_y )
                *out_local_y = y - top;
            return i;
        }
        top += h;
    }
    return -1;
}

/**
 * One source: the header band, then its drops if it is expanded.
 *
 * Every measurement is script2907's and script3042's; @see the block comment
 * above LT_HEAD_H.
 */
/**
 * One item cell: the plate, then the client's own icon at +2,+2.
 *
 * Shared by both views, which is the point of pulling it out -- the source
 * bands and the flat drop grid draw the same cell, and two copies of a blit
 * that has to line an icon up inside a plate is two chances to line it up
 * differently.
 *
 * The BORDERED variant is what `cc_setoutline(1)` bakes, and the quantity is
 * part of the picture rather than drawn over it: the client stamps the stack
 * digits into the sprite, so the icon is asked for AT the quantity and blitted
 * as one thing.
 */
static void
lt_draw_cell(
    struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h, int x, int y,
    struct LtItem const* item)
{
    struct ToriRS_ImageRef image = { 0 };
    int iw = 0;
    int ih = 0;
    size_t copied = 0;

    assert(rt);
    assert(buf);
    assert(item);

    if( g_cell_px )
        PluginDraw_Blit(
            buf, w, h, x, y, g_cell_px, g_cell_w, g_cell_h, 0, 0, g_cell_w, g_cell_h, 0);

    if( !g_api->game ||
        g_api->game->item_image(
            g_api, item->obj_id, item->quantity,
            TORIRS_ITEM_ICON_BORDERED, &image) != TORIRS_ASSET_READY ||
        !g_api->assets.image_size(g_api, image, &iw, &ih) )
    {
        g_redraw_pending = true;
        g_compose_key = 0;
        if( image.value ) g_api->assets.image_release(g_api, image);
        return;
    }
    {
        uint32_t* px = malloc((size_t)iw * (size_t)ih * sizeof(*px));
        assert(px);
        if( g_api->assets.image_pixels(
                g_api, image, px, (size_t)iw * (size_t)ih, &copied) &&
            copied == (size_t)iw * (size_t)ih )
            PluginDraw_Blit(buf, w, h, x + 2, y + 2, px, iw, ih, 0, 0, iw, ih, 0);
        free(px);
    }
    g_api->assets.image_release(g_api, image);
}

static void
lt_draw_source(
    struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h, int top, int index)
{
    struct LtSource const* src = &g_source[index];
    char text[96];
    char amount[32];
    int cell_x0;

    assert(rt);
    assert(buf);

    /* The plate, inset 2 all round -- @see LT_PLATE_INSET. */
    if( g_spine_px )
        PluginDraw_Tile(
            buf, w, h, LT_PLATE_INSET, top + LT_PLATE_INSET,
            w - LT_PLATE_INSET * 2, LT_HEAD_H - LT_PLATE_INSET * 2, g_spine_px,
            g_spine_w, g_spine_h, 0);

    /* "Goblin x 2" on the left and its value on the right, both in the bold
     * face at the interfaces' orange, as script2907 sets them. */
    snprintf(text, sizeof(text), "%s x %d", src->name, src->kills);
    PluginDraw_Text(buf, w, h, 8, top + 9, &g_bold, text, LT_INK_HEAD);

    lt_kmb(lt_source_value(rt, src), amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s gp", amount);
    PluginDraw_TextRight(buf, w, h, w - 8, top + 9, &g_bold, text, LT_INK_HEAD);

    if( !g_expanded[index] || src->item_count == 0 )
        return;

    /*
     * The grid, centred the way script3042 centres it: five 40-wide cells is
     * 200, and what is left over is shared as four gaps.
     */
    cell_x0 = (w - LT_GRID_COLS * LT_CELL_W) / 2;
    if( cell_x0 < 0 )
        cell_x0 = 0;

    for( int i = 0; i < src->item_count; i++ )
    {
        int const col = i % LT_GRID_COLS;
        int const row = i / LT_GRID_COLS;
        int const x = cell_x0 + col * LT_CELL_W;
        int const y = top + LT_GRID_TOP + row * LT_CELL_H;
        int image;
        int iw = 0;
        int ih = 0;

        lt_draw_cell(rt, buf, w, h, x, y, &src->items[i]);
        (void)image;
        (void)iw;
        (void)ih;
    }
}

/** Rasterise every band and publish the strip. */
/**
 * Everything the strip's picture depends on, in one number.
 *
 * The compose is the expensive half of this plugin -- a plate and a text pass
 * per band, and an icon read back per cell -- and the draw event fires
 * whenever the well is dirty, which is every frame the panel is up. Composing
 * unconditionally therefore re-rasterised and RE-PUBLISHED the same picture
 * sixty times a second; the republish replaces the scene sprite the overlay
 * item is about to reference, and the frames that landed between the two are
 * what the loot list was flickering with.
 *
 * So the picture is hashed on its inputs and only rebuilt when one moves. The
 * hash has to cover everything a reader can SEE -- the view, the width, the
 * price basis, every band's name, count and value, and every cell's obj and
 * quantity -- because anything left out is a change that will not redraw.
 */
static uint64_t
lt_compose_key(struct LootTrackerRuntime* rt, int width)
{
    uint64_t k = 1469598103934665603ull;
    char const* price = lt_config_string(rt, "price_source");

#define LT_MIX(v)                                                                        \
    do                                                                                   \
    {                                                                                    \
        k ^= (uint64_t)(v);                                                              \
        k *= 1099511628211ull;                                                           \
    } while( 0 )

    assert(rt);
    LT_MIX(width);
    LT_MIX(g_drop_view ? 1 : 0);
    LT_MIX(g_source_count);
    LT_MIX(g_session_kills);
    LT_MIX(g_session_value);
    for( char const* at = price ? price : ""; *at; at++ )
        LT_MIX((unsigned char)*at);
    for( int i = 0; i < g_source_count; i++ )
    {
        struct LtSource const* src = &g_source[i];
        LT_MIX(src->kills);
        LT_MIX(src->item_count);
        LT_MIX(g_expanded[i] ? 1 : 0);
        for( char const* at = src->name; *at; at++ )
            LT_MIX((unsigned char)*at);
        for( int j = 0; j < src->item_count; j++ )
        {
            LT_MIX(src->items[j].obj_id);
            LT_MIX(src->items[j].quantity);
            LT_MIX(src->items[j].cost);
        }
    }
#undef LT_MIX
    return k;
}

static void
lt_compose(struct LootTrackerRuntime* rt, int width)
{
    int const height = lt_strip_h(rt);
    size_t const pixels = (size_t)width * (size_t)height;
    uint64_t const key = lt_compose_key(rt, width);
    int top = 0;

    assert(rt);
    if( width <= 0 || height <= 0 )
        return;
    /*
     * Nothing moved, so the published picture is still the right one. An icon
     * that was not resident when it was composed is the one thing this would
     * miss, and lt_art_ready gates the whole draw on the art rather than on
     * any one cell, so a late icon arrives with the next real change.
     */
    if( key == g_compose_key && g_compose && g_compose_w == width &&
        g_compose_h == height )
        return;
    g_compose_key = key;

    if( !g_compose || g_compose_w != width || g_compose_h != height )
    {
        free(g_compose);
        g_compose = malloc(pixels * sizeof(*g_compose));
        assert(g_compose);
        g_compose_w = width;
        g_compose_h = height;
    }
    /* Transparent, so the panel's own backing shows through exactly as the
     * interface's does behind its bands. */
    memset(g_compose, 0, pixels * sizeof(*g_compose));

    lt_draw_totals(rt, g_compose, width, height);
    top = LT_TOTALS_H + LT_HEAD_GAP;

    if( g_source_count == 0 )
        PluginDraw_Text(
            g_compose, width, height, 4, top + 3, &g_text, "No loot to display.",
            LT_INK_HEAD);
    else if( g_drop_view )
    {
        struct LtItem drops[LT_SOURCES_MAX * 4];
        int const n = lt_collect_drops(
            rt, drops, (int)(sizeof(drops) / sizeof(drops[0])));
        int const x0 = (width - LT_GRID_COLS * LT_CELL_W) / 2;

        for( int i = 0; i < n; i++ )
            lt_draw_cell(
                rt, g_compose, width, height,
                (x0 < 0 ? 0 : x0) + (i % LT_GRID_COLS) * LT_CELL_W,
                top + (i / LT_GRID_COLS) * LT_CELL_H, &drops[i]);
    }
    else
        for( int i = 0; i < g_source_count; i++ )
        {
            lt_draw_source(rt, g_compose, width, height, top, i);
            top += lt_source_h(rt, i);
        }

    (void)g_api->assets.image_compose(
        g_api, "strip", width, height, g_compose, &g_strip_image);
}

/**
 * Ask for a redraw of the strip only when the picture would differ.
 *
 * The refresh runs on a timer, and an unconditional invalidate would put the
 * well through a full draw pass twice a second for a picture that is already
 * on screen -- every one of those passes a chance to catch the art or an obj
 * icon mid-flight and publish a frame that is missing one. Composing is keyed
 * on the drawn values; so is asking for the pass at all.
 */
static void
lt_strip_invalidate(struct LootTrackerRuntime* rt)
{
    assert(rt);
    g_api->panel.redraw(g_api, "strip");
}

/**
 * Pull the client's own loot record into the page's tables.
 *
 * THE STORE IS THE TRUTH, and this is the whole of how a record gets here now.
 * The loot tracker is a client-side feature of the game: no packet carries it,
 * the server's kill hook feeds game/rs_loot_store.c directly, and the cache's
 * own tracker interface reads that store. Correlating despawns with item
 * spawns -- which is what this plugin did before the store was reachable, and
 * what RuneLite has to do because its client exposes no such thing -- cannot
 * see a kill that dropped nothing and cannot tell two of a monster dying on
 * one tile apart. Reading the store gets the game's answer instead of an
 * approximation of it.
 *
 * @return true when anything changed, which is what decides a rebuild.
 */
static bool
lt_sync_store(struct LootTrackerRuntime* rt)
{
    struct ToriRS_LootSource src;
    char const* ignored;
    int before = g_source_count;
    int count = 0;
    bool changed = false;

    assert(rt);

    ignored = lt_config_string(rt, "ignored_sources");
    g_session_kills = 0;
    g_session_value = 0;

    for( int it = g_api->game->loot_source_next(g_api, -1, &src); it >= 0;
         it = g_api->game->loot_source_next(g_api, it, &src) )
    {
        struct LtSource* dst;
        struct LtSource previous;
        struct ToriRS_LootRow row;
        char name[64];

        lt_clean_name(src.name, name, sizeof(name));
        if( !name[0] || lt_listed(ignored, name) )
            continue;
        if( count >= LT_SOURCES_MAX )
            break;

        dst = &g_source[count];
        previous = *dst;
        memset(dst, 0, sizeof(*dst));
        snprintf(dst->name, sizeof(dst->name), "%s", name);
        dst->kills = src.kill_count;
        dst->id = src.id;

        for( int r = g_api->game->loot_row_next(g_api, src.id, -1, &row); r >= 0;
             r = g_api->game->loot_row_next(g_api, src.id, r, &row) )
        {
            struct ToriRS_ItemInfo info;
            struct LtItem item;

            if( dst->item_count >= LT_ITEMS_MAX )
                break;
            memset(&item, 0, sizeof(item));
            item.obj_id = row.obj_id;
            item.quantity = row.quantity;
            /* The store priced it when it landed; a name still has to be
             * asked for, and an objtype that is not resident yet simply has
             * none this pass. */
            item.cost = row.value;
            if( g_api->game->item_info(g_api, row.obj_id, &info) )
                lt_clean_name(info.name, item.name, sizeof(item.name));
            if( lt_listed(lt_config_string(rt, "ignored_items"), item.name) )
                continue;
            dst->items[dst->item_count++] = item;
        }

        g_session_kills += dst->kills;
        g_session_value += lt_source_value(rt, dst);
        if( count >= before || memcmp(&previous, dst, sizeof(*dst)) != 0 )
            changed = true;
        count++;
    }

    g_source_count = count;
    if( before != count )
        changed = true;
    if( g_detail >= g_source_count )
        g_detail = -1;
    return changed;
}

/** O(1) idle gate over the authoritative loot store. Revision zero is the
 * compatibility answer for a host without the minor capability, where the
 * bounded two-Hz snapshot remains the safe fallback. */
static bool
lt_sync_changed(struct LootTrackerRuntime* rt, bool force)
{
    uint64_t const revision = g_api->game->loot_revision
                                  ? g_api->game->loot_revision(g_api)
                                  : 0;
    bool changed;

    if( !force && revision != 0 && revision == g_loot_revision )
        return false;
    changed = lt_sync_store(rt);
    g_loot_revision = g_api->game->loot_revision
                          ? g_api->game->loot_revision(g_api)
                          : revision;
    return changed;
}

/** Rewrite every readout on the built page. */
static void
lt_page_refresh(struct LootTrackerRuntime* rt)
{
    char text[128];

    assert(rt);
    if( !g_page_built )
        return;

    /*
     * A band arriving, or a drop grid growing a row, makes the well TALLER.
     * That is a property of a widget the page already has, so the retained
     * page states it in place -- the same call the build makes. This used to
     * be a panel_clear, and re-declaring the whole page every time a kill
     * landed is what the list flashed on.
     */
    g_built_rows = lt_strip_h(rt);
    (void)g_api->panel.set_height(g_api, "strip", g_built_rows);

    /* The bands are pixels and redraw themselves. */
    lt_strip_invalidate(rt);

    if( g_detail >= 0 && g_detail < g_source_count )
    {
        struct LtSource const* src = &g_source[g_detail];

        (void)g_api->panel.set_text(g_api, "sec_detail", src->name);
        lt_commas(src->kills, text, sizeof(text));
        (void)g_api->panel.set_text(g_api, "d_kills", text);
        lt_commas(lt_source_value(rt, src), text, sizeof(text));
        (void)g_api->panel.set_text(g_api, "d_value", text);
        if( src->kills > 0 )
        {
            lt_commas(lt_source_value(rt, src) / src->kills, text, sizeof(text));
            (void)g_api->panel.set_text(g_api, "d_per_kill", text);
        }
        else
            (void)g_api->panel.set_text(g_api, "d_per_kill", "0");
        g_built_detail = g_detail;
    }
}

static void
lt_panel_build(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(panel);

    /* The SETTINGS face is the generated form and nothing else -- every knob
     * here is a config key. @see enum ToriRS_PanelView. */
    if( view != TORIRS_PANEL_VIEW_PAGE )
    {
        g_page_built = false;
        return;
    }

    (void)lt_sync_changed(rt, true);
    g_built_rows = lt_strip_h(rt);

    /* No Session rows: the strip's own totals band carries them, exactly as
     * the game's tracker does, and two readouts of one number that round
     * differently is how they come to disagree. */

    /*
     * ONE drawing well for every band, which is what the CS2 tracker is: a
     * header per source with its drops under it. Built out of panel controls
     * it would be a header plus a control per item and would run out of the
     * 48-control budget inside one boss trip.
     */
    panel->custom(panel, "strip", lt_strip_h(rt));

    /*
     * The header's own ops, as buttons under the source they act on. The CS2
     * header offers Collapse/Expand, Clear data and Ignore/Stop ignoring, and
     * an item cell offers Ignore -- the same set, reached the only way a
     * drawing well can offer one, because a panel has no secondary-click
     * channel to hang a menu on.
     */
    g_built_detail = g_detail;
    if( g_detail >= 0 && g_detail < g_source_count )
    {
        struct ToriRS_PanelNode heading = {
            .struct_size = sizeof(heading),
            .kind = TORIRS_PANEL_HEADING,
            .id = "sec_detail",
            .text = g_source[g_detail].name,
        };
        char text[64];
        (void)panel->node(panel, &heading);
        lt_commas(g_source[g_detail].kills, text, sizeof(text));
        panel->key_value(panel, "d_kills", "Kills", text);
        lt_commas(lt_source_value(rt, &g_source[g_detail]), text, sizeof(text));
        panel->key_value(panel, "d_value", "Value", text);
        if( g_source[g_detail].kills > 0 )
            lt_commas(
                lt_source_value(rt, &g_source[g_detail]) / g_source[g_detail].kills,
                text, sizeof(text));
        else
            snprintf(text, sizeof(text), "0");
        panel->key_value(panel, "d_per_kill", "Value per kill", text);
        panel->button(panel, "d_clear", "Clear data", true);
        panel->button(panel, "d_ignore", "Ignore", true);
    }
    else
        g_built_detail = -1;

    /*
     * No "clear all" row. The tracker this is a port of has no such control:
     * its clears are ops on a BAND -- the detail block's own Clear data, one
     * source at a time -- and a page-wide button was this port's invention.
     * @see the same note on the xp tracker's page.
     */

    g_page_built = true;
    lt_page_refresh(rt);
}

/**
 * Draw the selected source's drops.
 *
 * Every icon is asked for again, on every pass, and that is the contract
 * rather than an oversight: `obj_image` hands back a handle out of a
 * host-owned evicting cache, and a plugin that remembered one across frames
 * would eventually draw nothing. @see ToriRS_GameApiV2::item_image.
 */
static void
lt_panel_draw(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    char const* node,
    struct ToriRS_DrawBuilder* draw)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    struct ToriRS_DrawContext context = { .struct_size = sizeof(context) };

    assert(api);
    assert(node);
    assert(draw);

    if( strcmp(node, "strip") != 0 || !draw->context(draw, &context) ||
        context.bounds.width <= 0 )
        return;
    /* The art crosses the IO queue, so the first passes after a start have
     * nothing to draw with. The next invalidate fills it -- the same state the
     * client's own inventory icons are in for a frame or two. */
    if( !lt_art_ready(rt) )
    {
        g_redraw_pending = true;
        return;
    }

    g_well_w = context.bounds.width;
    g_redraw_pending = false;
    lt_compose(rt, context.bounds.width);
    if( g_strip_image.value )
        draw->image(draw, g_strip_image, 0, 0, 255);
}

static void
lt_panel_action(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PanelActionEvent const* ev)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(ev);
    assert(ev->id);

    g_api->panel.attention(g_api, false);

    if( strcmp(ev->id, "d_close") == 0 )
    {
        g_detail = -1;
        g_api->panel.invalidate(g_api);
        return;
    }
    if( strcmp(ev->id, "d_clear") == 0 && g_detail >= 0 && g_detail < g_source_count )
    {
        int const source_id = g_source[g_detail].id;
        if( !g_api->game->loot_source_clear ||
            !g_api->game->loot_source_clear(g_api, source_id) )
        {
            g_api->core.log(api, "loot-tracker: could not clear loot source %d", source_id);
            return;
        }
        g_detail = -1;
        (void)lt_sync_changed(rt, true);
        g_dirty = true;
        g_api->panel.invalidate(g_api);
        return;
    }

    if( strcmp(ev->id, "d_ignore") == 0 && g_detail >= 0 && g_detail < g_source_count )
    {
        /* The header's third op. The list is the config key a person can also
         * type into, so both ways of saying it end up in one place. */
        char list[LT_CONFIG_VALUE_MAX];
        char const* existing = lt_config_string(rt, "ignored_sources");
        char source_name[sizeof(g_source[0].name)];

        snprintf(source_name, sizeof(source_name), "%s", g_source[g_detail].name);
        snprintf(
            list, sizeof(list), "%s%s%s", existing && existing[0] ? existing : "",
            existing && existing[0] ? "," : "", source_name);
        /* Config callbacks are synchronous in the host. Close the detail
         * before publishing the filter so re-entrant reconciliation cannot
         * leave this action indexing a row it just removed. */
        g_detail = -1;
        (void)g_api->config.set(g_api, "ignored_sources", list);
        (void)lt_sync_changed(rt, true);
        g_dirty = true;
        g_api->panel.invalidate(g_api);
        return;
    }

    /*
     * A click in the strip. The bands are variable height -- an expanded one
     * carries its grid -- so the source is found by walking them rather than
     * by dividing, and the header's own first op decides what the click means:
     * inside the 33-tall band it EXPANDS or collapses, which is
     * script2907's Collapse/Expand, and it also selects the source so the
     * other two ops have something to act on.
     */
    if( strcmp(ev->id, "strip") == 0 )
    {
        int local_y = 0;
        int source;

        /*
         * The TOTALS band's own four controls first, because they sit above
         * every source and a click there is not a click on a band.
         *
         * Same four the cache offers, in the same places: the view toggle at
         * the left, then value-basis, collapse-all and the ignore list at the
         * right. `g_well_w` is the width the strip was last composed at, which
         * is what the right-anchored three were placed against.
         */
        if( ev->y < LT_TOTALS_H )
        {
            int const w = g_well_w;
            if( ev->x >= LT_BTN_LEFT_X && ev->x < LT_BTN_LEFT_X + LT_BTN )
                g_drop_view = !g_drop_view;
            else if( ev->x >= w - LT_BTN_R0 - LT_BTN && ev->x < w - LT_BTN_R0 )
            {
                /* The ignore list: the settings form edits the same key, so
                 * the panel's shortcut and the typed list are one thing. */
                g_detail = -1;
            }
            else if( ev->x >= w - LT_BTN_R1 - LT_BTN && ev->x < w - LT_BTN_R1 )
            {
                /* Collapse all -- or expand all when everything is already
                 * shut, which is what makes one button enough. */
                bool any = false;
                for( int i = 0; i < g_source_count; i++ )
                    any = any || g_expanded[i];
                for( int i = 0; i < g_source_count; i++ )
                    g_expanded[i] = !any;
            }
            else if( ev->x >= w - LT_BTN_R2 - LT_BTN && ev->x < w - LT_BTN_R2 )
            {
                /* The value basis, which is the same config key the settings
                 * form offers as a dropdown. */
                char const* now = lt_config_string(rt, "price_source");
                (void)g_api->config.set(
                    g_api, "price_source",
                    now && lt_name_eq(now, "High alchemy") ? "Cache value"
                                                           : "High alchemy");
            }
            else
                return;
            if( g_built_detail >= 0 && g_detail < 0 )
                g_api->panel.invalidate(g_api);
            else
                lt_page_refresh(rt);
            return;
        }

        /* The flat drop grid has no bands to open. */
        if( g_drop_view )
            return;

        source = lt_source_at(rt, ev->y, &local_y);
        if( source < 0 )
            return;
        if( local_y < LT_HEAD_H )
            g_expanded[source] = !g_expanded[source];
        g_detail = g_detail == source && local_y >= LT_HEAD_H ? -1 : source;
        if( (g_built_detail >= 0) != (g_detail >= 0) )
            g_api->panel.invalidate(g_api);
        else
            lt_page_refresh(rt);
        return;
    }
}


static void
lt_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    struct ToriRS_PanelDescriptor desc;

    assert(api);

    g_source_count = 0;
    g_session_value = 0;
    g_session_kills = 0;
    g_detail = -1;
    g_built_detail = -1;
    g_built_rows = 0;
    g_page_built = false;
    g_page_visible = false;
    g_next_panel_ms = 0;
    g_dirty = false;
    g_redraw_pending = false;
    g_loot_revision = 0;
    g_well_w = TORIRS_PANEL_WIDTH_DEFAULT;
    g_strip_image.value = 0;
    /*
     * OPEN by default, which is what the game's own tracker does: a band with
     * its drops under it is the thing a person opened the panel to see, and a
     * list of closed bands is a list of names. The row click still collapses
     * one, so a long trip can be tidied.
     */
    for( size_t i = 0; i < sizeof(g_expanded) / sizeof(g_expanded[0]); i++ )
        g_expanded[i] = true;
    g_compose_key = 0;

    memset(&desc, 0, sizeof(desc));
    /* RuneLite's own, so a person who has used the plugin there recognises
     * the row. @see script/plugins/assets/loot-tracker/panel_icon.txt. */
    desc.icon_asset = "panel_icon.png";
    desc.preferred_width = TORIRS_PANEL_WIDTH_DEFAULT;
    (void)g_api->panel.request(g_api, &desc);
}

/** The shell moved, showed or hid this page. */
static void
lt_panel_layout(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PanelLayoutEvent const* ev)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(ev);

    g_page_visible = ev->visible;
    if( ev->width > 0 )
        g_well_w = ev->width;
    if( g_page_visible )
    {
        (void)lt_sync_changed(rt, false);
        lt_page_refresh(rt);
        g_api->panel.redraw(g_api, "strip");
    }
}

static void
lt_stop(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    g_page_built = false;
    g_page_visible = false;
    free(g_compose);
    g_compose = NULL;
    g_compose_w = 0;
    g_compose_h = 0;
    if( g_strip_image.value ) g_api->assets.image_release(g_api, g_strip_image);
    PluginDraw_AtlasFree(g_api, &g_bold);
    PluginDraw_AtlasFree(g_api, &g_text);
    PluginDraw_ImageFree(g_api, &g_over_px, &g_img_over);
    PluginDraw_ImageFree(g_api, &g_spine_px, &g_img_spine);
    PluginDraw_ImageFree(g_api, &g_cell_px, &g_img_cell);
    PluginDraw_ImageFree(g_api, &g_view_px, &g_img_view);
    PluginDraw_ImageFree(g_api, &g_view2_px, &g_img_view2);
    PluginDraw_ImageFree(g_api, &g_alch_px, &g_img_alch);
    PluginDraw_ImageFree(g_api, &g_collapse_px, &g_img_collapse);
    PluginDraw_ImageFree(g_api, &g_ignored_px, &g_img_ignored);
}

static void
lt_tick(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    (void)event;

    uint64_t const now = g_api->core.frame_ms(g_api);

    assert(api);

    if( now < g_next_panel_ms )
        return;
    g_next_panel_ms = now + LT_PANEL_REFRESH_MS;

    if( !g_page_visible )
        return;

    /* The client's own record, which is what the game's tracker shows. */
    if( lt_sync_changed(rt, false) )
        g_dirty = true;

    /*
     * Opening or closing the detail block changes WHICH widgets the page has,
     * and that is the only thing a rebuild is for. Everything else a kill can
     * do -- a new band, a taller drop grid, a bigger number -- the refresh
     * states on the page that is already there. Neither is worth doing for a
     * page nobody is looking at; the rebuild happens when it is selected
     * again.
     */
    if( g_dirty || g_redraw_pending )
    {
        if( g_page_built && ((g_built_detail >= 0) != (g_detail >= 0)) )
            g_api->panel.invalidate(g_api);
        else
            lt_page_refresh(rt);
        g_dirty = false;
    }
}

static struct ToriRS_ConfigItem const LT_CONFIG[] = {
    { "price_source",      TORIRS_CONFIG_ENUM, "Value items by",                 "Cache value", 0, 0, "Cache value|High alchemy", 0 },
    { "kill_chat_message", TORIRS_CONFIG_BOOL, "Announce loot in chat",          "0", 0, 0, NULL, 0 },
    { "chat_value_threshold", TORIRS_CONFIG_INT, "Announce only above (gp)",     "0", 0, 100000000, NULL, 0 },
    { "ignored_items",     TORIRS_CONFIG_TEXT, "Ignored items",                  "", 0, 0, NULL, 4 },
    { "ignored_sources",   TORIRS_CONFIG_TEXT, "Ignored sources",                "", 0, 0, NULL, 4 },
    { NULL,                TORIRS_CONFIG_BOOL, NULL,                             NULL, 0, 0, NULL, 0 },
};

static void
lt_config_changed(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    char const* key)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    bool const filtered = key &&
                          (strcmp(key, "ignored_items") == 0 ||
                           strcmp(key, "ignored_sources") == 0);
    bool const affects_picture = filtered ||
                                 (key && strcmp(key, "price_source") == 0);

    if( filtered ) (void)lt_sync_changed(rt, true);
    else if( affects_picture ) lt_revalue(rt);
    if( !g_page_visible || !affects_picture ) return;
    if( (g_built_detail >= 0) != (g_detail >= 0) )
        g_api->panel.invalidate(g_api);
    else
        lt_page_refresh(rt);
}

static struct ToriRS_ConfigSchema const LT_SCHEMA = {
    .struct_size = sizeof(LT_SCHEMA),
    .items = LT_CONFIG,
};

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LOOT_TRACKER = {
    .struct_size = sizeof(TORIRS_PLUGIN_LOOT_TRACKER),
    .id = "loot-tracker",
    .title = "Loot Tracker",
    .version = "2.0.0",
    .state_size = sizeof(struct LootTrackerState),
    .config = &LT_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = lt_start,
        .on_stop = lt_stop,
        .on_logic_tick = lt_tick,
        .on_game_event = lt_game_event,
        .on_config_changed = lt_config_changed,
        .on_ui_build = lt_panel_build,
        .on_ui_action = lt_panel_action,
        .on_ui_draw = lt_panel_draw,
        .on_ui_layout = lt_panel_layout,
    },
};
