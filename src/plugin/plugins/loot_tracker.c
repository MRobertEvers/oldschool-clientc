#include "plugin/plugins/plugin_draw.h"
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

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
 * OldSchool's CS2 LOOT_ADD path populates the client's authoritative loot
 * store, so that lane is read through `loot_source_next` / `loot_row_next` and
 * never inferred a second time. A 2004 RS2 lane has no such opcode. There the
 * plugin uses RuneLite's portable fallback: a dying NPC despawn opens a short
 * candidate window and ground items on its footprint are assigned to it.
 * The lane gate is what prevents one OSRS drop being counted through both
 * paths while keeping the tracker functional on rs289lc.
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
/** Deaths waiting for inferred RS2 loot. Deliberately small: a candidate lives for
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
 * Frames a BLANK well may ask again on before it falls back to that cadence.
 *
 * The art it is waiting for crosses the IO queue once. It is there within a
 * few frames, or it is not coming at all and asking sixty times a second will
 * not fetch it -- so this is a bound on the per-frame arm rather than a
 * deadline for the art. Two seconds at sixty frames.
 */
#define LT_BLANK_RETRY_FRAMES 120

/**
 * Bytes of one config value, stated here rather than included.
 *
 * The host's ceiling is TORIRS_PLUGIN_CONFIG_VALUE_MAX, but that lives in the
 * host header and a plugin has no business including one -- the whole contract
 * is torirs_plugin_api.h. A shorter buffer here is not a disagreement, only a
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

/** The header band. `thinbox_specific` leaves four clear rows before the next
 *  source header; the two-pixel gap above the first header is owned by the
 *  sources container itself. */
#define LT_HEAD_H 33
#define LT_HEAD_GAP 2
#define LT_SOURCE_GAP 4
#define LT_THIN_OUTER 0x0E0E0Cu
#define LT_THIN_INNER 0x474745u
/*
 * The band PLATE, inset 2 all round.
 *
 * `cc_setsize(4, 33 - 4, 1, 0)` in script2907 is width mode 1, which is PARENT
 * MINUS the value -- not a 4-pixel-wide spine, which is how it reads at a
 * glance and how this was drawn at first. The same sprite is used untiled and
 * scaled across the 42-high totals band (interface 650:58). The visible border
 * around a category is the pair of rectangles from `thinbox_specific`, not an
 * edge baked into this texture.
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

#define LT_TOTALS_KEY_X 36
#define LT_TOTALS_VAL_X 97

/** The interfaces' own orange, which every heading is set in, and the white
 *  the totals' values are. */
#define LT_INK_HEAD 0xFF981Fu
#define LT_INK_VALUE 0xFFFFFFu
/*
 * The stack count's three bands, which are the CLIENT's own and not chosen
 * here: `uitree_emit_inv_number` writes `<col=ffff00>n</col>` below a hundred
 * thousand, `<col=ffffff>nK</col>` below ten million and `<col=00ff80>nM</col>`
 * above it, and the reference passes the same 16776960 as its fallback. A cell
 * in this page has to read as the same cell the inventory draws.
 */
#define LT_INK_STACK_ONES 0xFFFF00u
#define LT_INK_STACK_K 0xFFFFFFu
#define LT_INK_STACK_M 0x00FF80u

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

/** A despawn waiting briefly for ground items on its footprint. */
struct LtPending
{
    char name[64];
    int tile_x;
    int tile_z;
    int level;
    int size;
    uint64_t at_ms;
    struct LtItem items[LT_ITEMS_MAX];
    int item_count;
    bool confirmed;
};

/** What the secondary-click channel has open over the well. */
enum LtMenuKind
{
    LT_MENU_NONE = 0,
    /** script2907's own three: Collapse/Expand, Clear data, Ignore. */
    LT_MENU_BAND,
    /** script3042's two: Check and Ignore. */
    LT_MENU_CELL
};

struct LootTrackerState
{
    struct LtSource source[LT_SOURCES_MAX];
    int source_count;
    /**
     * The selected source, as the store's own ID and not an array index.
     *
     * Zero is "nothing selected". A resync reorders or drops rows, so an index
     * remembered across one points at whatever moved into that slot; the id
     * re-finds the source or clears the selection, which is the only answer
     * that survives the store rewriting itself twice a second.
     */
    int detail_source_id;
    struct LtPending pending[LT_PENDING_MAX];
    int pending_count;
    int next_fallback_source_id;
    bool page_visible;

    struct PluginDraw_Atlas bold;
    struct PluginDraw_Atlas text;
    struct ToriRS_ImageRef img_spine;
    uint32_t* spine_px;
    int spine_w;
    int spine_h;
    struct ToriRS_ImageRef img_spine_ignored;
    uint32_t* spine_ignored_px;
    int spine_ignored_w;
    int spine_ignored_h;
    struct ToriRS_ImageRef img_cell;
    uint32_t* cell_px;
    int cell_w;
    int cell_h;
    struct ToriRS_ImageRef img_cell_ignored;
    uint32_t* cell_ignored_px;
    int cell_ignored_w;
    int cell_ignored_h;

    bool expanded[LT_SOURCES_MAX];
    bool drop_view;
    bool show_ignored;
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
    struct ToriRS_ImageRef img_cache;
    uint32_t* cache_px;
    int cache_w;
    int cache_h;
    struct ToriRS_ImageRef img_collapse;
    uint32_t* collapse_px;
    int collapse_w;
    int collapse_h;
    struct ToriRS_ImageRef img_expand;
    uint32_t* expand_px;
    int expand_w;
    int expand_h;
    struct ToriRS_ImageRef img_ignored;
    uint32_t* ignored_px;
    int ignored_w;
    int ignored_h;
    struct ToriRS_ImageRef img_ignored_hide;
    uint32_t* ignored_hide_px;
    int ignored_hide_w;
    int ignored_hide_h;

    int well_w;
    uint64_t next_panel_ms;
    long long session_value;
    int session_kills;
    uint64_t loot_revision;
    /** Whether the store has been read once. The first read is not a set of
     *  new kills; announcing it would greet a person opening the page with
     *  one chat line per source the session already held. */
    bool synced_once;
    /**
     * A compose that could not finish -- the art still in flight, or an obj
     * icon the host answered PENDING.
     *
     * Retried at the REFRESH cadence and never per frame. Zeroing the picture
     * key on a pending icon is the unbounded per-frame recompose the ledger
     * names (H7); a counter that moves twice a second is bounded by
     * construction, and stops moving the moment the last icon is resident.
     */
    bool paint_incomplete;
    uint32_t paint_retry;
    /**
     * The last draw pass the host asked for staged NOTHING.
     *
     * A pass that stages nothing is a DECLINE and not an erasure: the host
     * keeps the retained run rather than swapping the last complete picture
     * for an empty one. That rule is right, and for a well that has never
     * staged anything it means "keep the nothing that is there" -- the page is
     * blank, and stays blank until something asks for another paint.
     *
     * So the two kinds of nothing are not the same thing and must not share a
     * clock. A strip that HAS a picture and is one obj icon short is a
     * complete-enough page and waits for the refresh cadence
     * (@see paint_incomplete). A strip showing NOTHING is an empty page and is
     * asked again on the frame (@see lt_frame_start).
     *
     * "The last pass declined" and not "there is no picture yet", because the
     * two differ for a well the host never asks to paint at all -- one
     * scrolled out of the page, or a page with no draw pass behind it. Those
     * are not blank, they are unasked, and a flag that could not tell them
     * apart would ask for a redraw on every frame for ever.
     */
    bool strip_blank;
    /** Frames spent asking on the frame rather than on the cadence.
     *  @see LT_BLANK_RETRY_FRAMES. */
    int blank_frames;
    /* What the secondary click opened, and where. @see enum LtMenuKind. */
    int menu_kind;
    int menu_source_id;
    int menu_obj_id;
    /** The CELL the menu was opened over, as it read at that moment. The ops
     *  are about the item a person right-clicked, and the store rewrites its
     *  rows twice a second underneath the open menu. */
    char menu_item_name[64];
    int menu_item_quantity;
    int menu_item_cost;
    int menu_x;
    int menu_y;
    /** The lane HAS a loot store, so the inference path must never run.
     *  Answered once, by capability, at on_start. */
    bool store_lane;
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
};

struct LootTrackerRuntime
{
    struct ToriRS_Api* api;
    struct LootTrackerState* state;
};

#define g_api (rt->api)
#define g_source (rt->state->source)
#define g_source_count (rt->state->source_count)
#define g_detail_source_id (rt->state->detail_source_id)
#define g_pending (rt->state->pending)
#define g_pending_count (rt->state->pending_count)
#define g_next_fallback_source_id (rt->state->next_fallback_source_id)
#define g_page_visible (rt->state->page_visible)
#define g_porcelain (rt->state->porcelain)
#define g_store_lane (rt->state->store_lane)
#define g_synced_once (rt->state->synced_once)
#define g_paint_incomplete (rt->state->paint_incomplete)
#define g_paint_retry (rt->state->paint_retry)
#define g_strip_blank (rt->state->strip_blank)
#define g_blank_frames (rt->state->blank_frames)
#define g_menu_kind (rt->state->menu_kind)
#define g_menu_source_id (rt->state->menu_source_id)
#define g_menu_obj_id (rt->state->menu_obj_id)
#define g_menu_item_name (rt->state->menu_item_name)
#define g_menu_item_quantity (rt->state->menu_item_quantity)
#define g_menu_item_cost (rt->state->menu_item_cost)
#define g_menu_x (rt->state->menu_x)
#define g_menu_y (rt->state->menu_y)
#define g_bold (rt->state->bold)
#define g_text (rt->state->text)
#define g_img_spine (rt->state->img_spine)
#define g_spine_px (rt->state->spine_px)
#define g_spine_w (rt->state->spine_w)
#define g_spine_h (rt->state->spine_h)
#define g_img_spine_ignored (rt->state->img_spine_ignored)
#define g_spine_ignored_px (rt->state->spine_ignored_px)
#define g_spine_ignored_w (rt->state->spine_ignored_w)
#define g_spine_ignored_h (rt->state->spine_ignored_h)
#define g_img_cell (rt->state->img_cell)
#define g_cell_px (rt->state->cell_px)
#define g_cell_w (rt->state->cell_w)
#define g_cell_h (rt->state->cell_h)
#define g_img_cell_ignored (rt->state->img_cell_ignored)
#define g_cell_ignored_px (rt->state->cell_ignored_px)
#define g_cell_ignored_w (rt->state->cell_ignored_w)
#define g_cell_ignored_h (rt->state->cell_ignored_h)
#define g_expanded (rt->state->expanded)
#define g_drop_view (rt->state->drop_view)
#define g_show_ignored (rt->state->show_ignored)
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
#define g_img_cache (rt->state->img_cache)
#define g_cache_px (rt->state->cache_px)
#define g_cache_w (rt->state->cache_w)
#define g_cache_h (rt->state->cache_h)
#define g_img_collapse (rt->state->img_collapse)
#define g_collapse_px (rt->state->collapse_px)
#define g_collapse_w (rt->state->collapse_w)
#define g_collapse_h (rt->state->collapse_h)
#define g_img_expand (rt->state->img_expand)
#define g_expand_px (rt->state->expand_px)
#define g_expand_w (rt->state->expand_w)
#define g_expand_h (rt->state->expand_h)
#define g_img_ignored (rt->state->img_ignored)
#define g_ignored_px (rt->state->ignored_px)
#define g_ignored_w (rt->state->ignored_w)
#define g_ignored_h (rt->state->ignored_h)
#define g_img_ignored_hide (rt->state->img_ignored_hide)
#define g_ignored_hide_px (rt->state->ignored_hide_px)
#define g_ignored_hide_w (rt->state->ignored_hide_w)
#define g_ignored_hide_h (rt->state->ignored_hide_h)
#define g_well_w (rt->state->well_w)
#define g_next_panel_ms (rt->state->next_panel_ms)
#define g_session_value (rt->state->session_value)
#define g_session_kills (rt->state->session_kills)
#define g_loot_revision (rt->state->loot_revision)

/**
 * Something the description reads has moved.
 *
 * The whole page -- the well's height, its identity, its picture, the detail
 * block's readouts and its caption -- is one describe function over the plugin
 * state, so there is nothing to push: the layer re-runs the describe at the
 * next fence, diffs it, and pays for exactly what differs. This is the only
 * "the page is out of date" flag left, and it replaces `dirty`,
 * `redraw_pending`, the built-topology tables and every invalidate call site.
 */
static void
lt_changed(struct LootTrackerRuntime* rt)
{
    assert(rt);
    Porcelain_Note(g_porcelain, PORCELAIN_INPUT_EXPLICIT);
}

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
lt_kmb_precision(long long value, int decimals, char* out, size_t out_size)
{
    long long unit;
    char suffix;
    long long whole;
    long long rem;
    long long step;

    assert(out);
    assert(out_size > 0);
    assert(decimals >= 0 && decimals <= 2);

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
    step = unit;
    for( int i = 0; i < decimals; i++ )
        step /= 10;
    whole = value / unit;
    rem = value - whole * unit;
    if( decimals > 0 && rem >= step )
        snprintf(
            out,
            out_size,
            decimals == 1 ? "%lld.%01lld%c" : "%lld.%02lld%c",
            whole,
            rem / step,
            suffix);
    else
        snprintf(out, out_size, "%lld%c", whole, suffix);
}

static void
lt_kmb(long long value, char* out, size_t out_size)
{
    lt_kmb_precision(value, 1, out, out_size);
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

static bool
lt_config_bool(struct LootTrackerRuntime* rt, char const* key)
{
    bool value = false;
    (void)g_api->config.get_bool(g_api, key, &value);
    return value;
}

static int
lt_config_int(struct LootTrackerRuntime* rt, char const* key)
{
    int value = 0;
    (void)g_api->config.get_int(g_api, key, &value);
    return value;
}

/**
 * Add or remove one whole entry of a stored list.
 *
 * Porcelain measures before it joins and REFUSES a list that would not fit
 * the host's value ceiling, leaving the stored list exactly as it was and
 * recording one finding. The hand-rolled version this replaces joined into a
 * 192-byte local with snprintf, so the 193rd byte of an ignore list was
 * silently dropped -- a truncated last entry that then matched nothing and
 * could never be removed, because the name a person typed was no longer in
 * the store.
 */
static void
lt_list_toggle(struct LootTrackerRuntime* rt, char const* key, char const* name)
{
    assert(rt);
    assert(key);
    assert(name);
    assert(name[0]);
    if( lt_listed(lt_config_string(rt, key), name) )
        (void)Porcelain_ConfigListRemove(g_porcelain, key, name);
    else
        (void)Porcelain_ConfigListAdd(g_porcelain, key, name);
}

/**
 * Does this lane have to INFER loot from despawns and spawns?
 *
 * The gate used to be `core.lane()->game == TORIRS_GAME_RS2` -- a lineage
 * read, per despawn, per spawn, per tick and per action, and the wrong shape
 * twice over: the loot store exists on every lane and answers revision 1 on
 * all of them, so neither its presence nor its revision separates the two.
 * The producers are the CS2 host's, so the question is "does this lane raise
 * loot events", which is a capability, answered once at on_start.
 *
 * @see the ledger's "Lane gate" row and host fix H6.
 */
static bool
lt_infers_loot(struct LootTrackerRuntime* rt)
{
    assert(rt);
    return !g_store_lane;
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
lt_source_value_visible(
    struct LootTrackerRuntime* rt,
    struct LtSource const* src,
    bool include_ignored)
{
    long long total = 0;
    char const* ignored_items = lt_config_string(rt, "ignored_items");

    assert(rt);
    assert(src);
    for( int i = 0; i < src->item_count; i++ )
        if( include_ignored || !lt_listed(ignored_items, src->items[i].name) )
            total += lt_unit_value(rt, &src->items[i]) * src->items[i].quantity;
    return total;
}

static long long
lt_source_value(struct LootTrackerRuntime* rt, struct LtSource const* src)
{
    return lt_source_value_visible(rt, src, false);
}

static bool
lt_source_ignored(struct LootTrackerRuntime* rt, struct LtSource const* src)
{
    assert(rt);
    assert(src);
    return lt_listed(lt_config_string(rt, "ignored_sources"), src->name);
}

static bool
lt_item_ignored(struct LootTrackerRuntime* rt, struct LtItem const* item)
{
    assert(rt);
    assert(item);
    return lt_listed(lt_config_string(rt, "ignored_items"), item->name);
}

static bool
lt_source_visible(struct LootTrackerRuntime* rt, int index)
{
    assert(rt);
    assert(index >= 0 && index < g_source_count);
    return g_show_ignored || !lt_source_ignored(rt, &g_source[index]);
}

static bool
lt_item_visible(struct LootTrackerRuntime* rt, struct LtItem const* item)
{
    return g_show_ignored || !lt_item_ignored(rt, item);
}

static int
lt_source_visible_items(struct LootTrackerRuntime* rt, struct LtSource const* src)
{
    int count = 0;

    assert(rt);
    assert(src);
    for( int i = 0; i < src->item_count; i++ )
        if( lt_item_visible(rt, &src->items[i]) )
            count++;
    return count;
}

static void
lt_revalue(struct LootTrackerRuntime* rt)
{
    g_session_value = 0;
    g_session_kills = 0;
    for( int i = 0; i < g_source_count; i++ )
    {
        if( lt_source_ignored(rt, &g_source[i]) )
            continue;
        g_session_kills += g_source[i].kills;
        g_session_value += lt_source_value(rt, &g_source[i]);
    }
}

static void
lt_display_totals(
    struct LootTrackerRuntime* rt,
    long long* out_value,
    int* out_count)
{
    long long value = 0;
    int count = 0;

    assert(rt);
    for( int i = 0; i < g_source_count; i++ )
    {
        if( !lt_source_visible(rt, i) )
            continue;
        count += g_source[i].kills;
        value += lt_source_value_visible(rt, &g_source[i], g_show_ignored);
    }
    if( out_value )
        *out_value = value;
    if( out_count )
        *out_count = count;
}

/* ------------------------------------------------------------------------ */
/* The records                                                               */
/* ------------------------------------------------------------------------ */

static int
lt_source_index_by_id(struct LootTrackerRuntime* rt, int source_id)
{
    assert(rt);
    for( int i = 0; i < g_source_count; i++ )
        if( g_source[i].id == source_id )
            return i;
    return -1;
}

/** Where the SELECTED source sits now, or -1 for no selection and for one the
 *  store has since dropped. @see LootTrackerState::detail_source_id. */
static int
lt_detail_index(struct LootTrackerRuntime* rt)
{
    assert(rt);
    if( g_detail_source_id <= 0 )
        return -1;
    return lt_source_index_by_id(rt, g_detail_source_id);
}

/** Is a detail block declared at all: a selected source that is also VISIBLE
 *  under the current filters. */
static int
lt_detail_visible_index(struct LootTrackerRuntime* rt)
{
    int const index = lt_detail_index(rt);
    return index >= 0 && lt_source_visible(rt, index) ? index : -1;
}

static int
lt_source_find(struct LootTrackerRuntime* rt, char const* name, bool create)
{
    int poorest = -1;

    assert(rt);
    assert(name);
    for( int i = 0; i < g_source_count; i++ )
        if( lt_name_eq(g_source[i].name, name) )
            return i;
    if( !create )
        return -1;
    if( g_source_count < LT_SOURCES_MAX )
    {
        int const index = g_source_count++;
        memset(&g_source[index], 0, sizeof(g_source[index]));
        g_source[index].id = ++g_next_fallback_source_id;
        snprintf(g_source[index].name, sizeof(g_source[index].name), "%s", name);
        g_expanded[index] = true;
        return index;
    }
    for( int i = 0; i < g_source_count; i++ )
        if( poorest < 0 ||
            lt_source_value(rt, &g_source[i]) <
                lt_source_value(rt, &g_source[poorest]) )
            poorest = i;
    if( poorest < 0 )
        return -1;
    memset(&g_source[poorest], 0, sizeof(g_source[poorest]));
    g_source[poorest].id = ++g_next_fallback_source_id;
    snprintf(g_source[poorest].name, sizeof(g_source[poorest].name), "%s", name);
    g_expanded[poorest] = true;
    return poorest;
}

static void
lt_source_add_item(struct LtSource* source, struct LtItem const* item)
{
    assert(source);
    assert(item);
    for( int i = 0; i < source->item_count; i++ )
        if( source->items[i].obj_id == item->obj_id )
        {
            source->items[i].quantity += item->quantity;
            return;
        }
    if( source->item_count < LT_ITEMS_MAX )
        source->items[source->item_count++] = *item;
}

static void
lt_source_remove(struct LootTrackerRuntime* rt, int index)
{
    assert(rt);
    if( index < 0 || index >= g_source_count )
        return;
    for( int i = index; i + 1 < g_source_count; i++ )
    {
        g_source[i] = g_source[i + 1];
        g_expanded[i] = g_expanded[i + 1];
    }
    memset(&g_source[g_source_count - 1], 0, sizeof(g_source[0]));
    g_expanded[g_source_count - 1] = true;
    g_source_count--;
    lt_revalue(rt);
    lt_changed(rt);
}

/**
 * "Goblin x12 loot: 1,204 gp", when the kill was worth announcing.
 *
 * Shared by both lanes, which it was not: this lived inside the INFERENCE
 * path's settle, so on every CS2 lane -- where the store runs and the
 * inference path never fires -- the `kill_chat_message` row was dead. Setting
 * it did nothing, ever. The store's per-source kill-count delta raises the
 * same line now. @see lt_sync_store.
 */
static void
lt_announce(
    struct LootTrackerRuntime* rt, char const* name, int kills, long long value)
{
    char line[200];
    char amount[32];

    assert(rt);
    assert(name);
    if( !lt_config_bool(rt, "kill_chat_message") )
        return;
    if( value < lt_config_int(rt, "chat_value_threshold") )
        return;
    lt_commas(value, amount, sizeof(amount));
    snprintf(line, sizeof(line), "%s x%d loot: %s gp", name, kills, amount);
    g_api->core.notify(g_api, line);
}

static void
lt_pending_settle(struct LootTrackerRuntime* rt, int index)
{
    struct LtPending* pending;

    assert(rt);
    assert(index >= 0 && index < g_pending_count);
    pending = &g_pending[index];
    if( pending->confirmed || pending->item_count > 0 )
    {
        int const source_index = lt_source_find(rt, pending->name, true);
        if( source_index >= 0 )
        {
            struct LtSource* source = &g_source[source_index];
            long long value = 0;

            source->kills++;
            source->last_ms = pending->at_ms;
            for( int i = 0; i < pending->item_count; i++ )
            {
                lt_source_add_item(source, &pending->items[i]);
                value += lt_unit_value(rt, &pending->items[i]) *
                         pending->items[i].quantity;
            }
            lt_revalue(rt);
            lt_changed(rt);
            lt_announce(rt, source->name, source->kills, value);
        }
    }
    g_pending[index] = g_pending[--g_pending_count];
}

static void
lt_pending_expire(struct LootTrackerRuntime* rt, uint64_t now)
{
    for( int i = g_pending_count - 1; i >= 0; i-- )
        if( now >= g_pending[i].at_ms + LT_PENDING_MS )
            lt_pending_settle(rt, i);
}

static void
lt_npc_despawn(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_NpcSnapshot const* npc)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    struct ToriRS_PlayerSnapshot player;
    struct LtPending* pending;
    char name[64];

    assert(api);
    assert(npc);
    if( !lt_infers_loot(rt) || npc->npc_id < 0 || !npc->name[0] )
        return;
    lt_clean_name(npc->name, name, sizeof(name));
    if( !name[0] )
        return;
    memset(&player, 0, sizeof(player));
    if( !g_api->world.local_player(g_api, &player) || player.level != npc->level ||
        abs(player.true_x - npc->true_x) > LT_PENDING_RANGE ||
        abs(player.true_z - npc->true_z) > LT_PENDING_RANGE )
        return;
    if( g_pending_count >= LT_PENDING_MAX )
        lt_pending_settle(rt, 0);
    pending = &g_pending[g_pending_count++];
    memset(pending, 0, sizeof(*pending));
    snprintf(pending->name, sizeof(pending->name), "%s", name);
    pending->tile_x = npc->true_x;
    pending->tile_z = npc->true_z;
    pending->level = npc->level;
    pending->size = npc->size > 0 ? npc->size : 1;
    pending->at_ms = g_api->core.frame_ms(g_api);
    pending->confirmed = npc->health_ratio == 0 && npc->health_scale > 0;
}

static void
lt_item_spawn(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GroundItemSnapshot const* ground)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    struct LtItem item;
    int best = -1;

    assert(api);
    assert(ground);
    if( !lt_infers_loot(rt) )
        return;
    for( int i = 0; i < g_pending_count; i++ )
    {
        struct LtPending const* pending = &g_pending[i];
        if( pending->level != ground->level || ground->tile_x < pending->tile_x ||
            ground->tile_x >= pending->tile_x + pending->size ||
            ground->tile_z < pending->tile_z ||
            ground->tile_z >= pending->tile_z + pending->size )
            continue;
        if( best < 0 || pending->at_ms > g_pending[best].at_ms )
            best = i;
    }
    if( best < 0 )
        return;
    memset(&item, 0, sizeof(item));
    item.obj_id = ground->obj_id;
    item.quantity = ground->count > 0 ? ground->count : 1;
    item.cost = ground->cost;
    lt_clean_name(ground->name, item.name, sizeof(item.name));
    for( int i = 0; i < g_pending[best].item_count; i++ )
        if( g_pending[best].items[i].obj_id == item.obj_id )
        {
            g_pending[best].items[i].quantity += item.quantity;
            return;
        }
    if( g_pending[best].item_count < LT_ITEMS_MAX )
        g_pending[best].items[g_pending[best].item_count++] = item;
}

static void
lt_world_loaded(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_WorldLoadedEvent const* event)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    (void)event;
    if( lt_infers_loot(rt) )
        g_pending_count = 0;
}

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
    struct ToriRS_Api* api,
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

/**
 * Everything the compose needs, resident.
 *
 * EVERY piece is asked for on every call and the answer is the AND of them.
 * There is no early return at the first one that has not landed, and that is
 * the whole of what this function is careful about.
 *
 * `PluginDraw_AtlasLoad` and `PluginDraw_ImageLoad` do not merely REPORT
 * whether a file is resident: calling one is what STARTS its request. So a
 * short-circuit here did not skip a test, it skipped a load -- the six
 * required pieces were fetched strictly one at a time, each one only asked for
 * once its predecessor had arrived. The only thing that calls this is the
 * well's paint, and the only thing that re-runs the paint while the art is
 * missing is the LT_PANEL_REFRESH_MS retry, so the chain advanced one file
 * every half second: eight polls, three and a half seconds of BLANK WELL after
 * every open, measured the same to within forty milliseconds on every run.
 * That is the interval the screenshots were landing inside.
 *
 * @see lt_frame_start for the other half -- a well with no picture at all is
 *      an empty page rather than a stale one, and does not wait on that clock.
 */
static int
lt_art_ready(struct LootTrackerRuntime* rt)
{
    int ready = 1;

    assert(rt);
    ready &= PluginDraw_AtlasLoad(g_api, &g_bold, "bold") ? 1 : 0;
    ready &= PluginDraw_AtlasLoad(g_api, &g_text, "text") ? 1 : 0;
    ready &= PluginDraw_ImageLoad(
                 g_api, "cat_spine.png", &g_img_spine, &g_spine_px, &g_spine_w,
                 &g_spine_h)
                 ? 1
                 : 0;
    ready &= PluginDraw_ImageLoad(
                 g_api, "cat_spine_ignored.png", &g_img_spine_ignored,
                 &g_spine_ignored_px, &g_spine_ignored_w, &g_spine_ignored_h)
                 ? 1
                 : 0;
    ready &= PluginDraw_ImageLoad(
                 g_api, "cell.png", &g_img_cell, &g_cell_px, &g_cell_w, &g_cell_h)
                 ? 1
                 : 0;
    ready &= PluginDraw_ImageLoad(
                 g_api, "cell_ignored.png", &g_img_cell_ignored, &g_cell_ignored_px,
                 &g_cell_ignored_w, &g_cell_ignored_h)
                 ? 1
                 : 0;
    /*
     * The band's four controls, cut from the cache: graphic_4915/4916 are the
     * two faces of the view toggle, 4912/4911 the value-basis actions,
     * 4917/4919 collapse/expand-all, and 4913/4914 show/hide ignored. Wanted
     * but not REQUIRED -- a band with a gap where a button belongs still
     * reads, and a page that refuses to draw does not.
     */
    (void)PluginDraw_ImageLoad(
        g_api, "btn_dropview.png", &g_img_view, &g_view_px, &g_view_w, &g_view_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_sourceview.png", &g_img_view2, &g_view2_px, &g_view2_w,
        &g_view2_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_alch.png", &g_img_alch, &g_alch_px, &g_alch_w, &g_alch_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_cache.png", &g_img_cache, &g_cache_px, &g_cache_w, &g_cache_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_collapse.png", &g_img_collapse, &g_collapse_px, &g_collapse_w,
        &g_collapse_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_expand.png", &g_img_expand, &g_expand_px, &g_expand_w,
        &g_expand_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_ignored.png", &g_img_ignored, &g_ignored_px, &g_ignored_w,
        &g_ignored_h);
    (void)PluginDraw_ImageLoad(
        g_api, "btn_ignored_hide.png", &g_img_ignored_hide, &g_ignored_hide_px,
        &g_ignored_hide_w, &g_ignored_hide_h);
    return ready;
}

/** How many five-cell rows `count` entries occupy. */
static int
lt_grid_rows(int count)
{
    return (count + LT_GRID_COLS - 1) / LT_GRID_COLS;
}

/** How tall one source's band is, expanded or not. */
static int
lt_source_h(struct LootTrackerRuntime* rt, int index)
{
    int items;

    assert(rt);
    assert(index >= 0);
    assert(index < g_source_count);
    if( !lt_source_visible(rt, index) )
        return 0;
    if( !g_expanded[index] )
        return LT_HEAD_H + LT_SOURCE_GAP;
    items = lt_source_visible_items(rt, &g_source[index]);
    /* With items, the body is rows*36+10 high, beginning one pixel over the
     * header's bottom edge; four clear rows then separate the next source. An
     * expanded zero-drop source gets script3043's 20px framed empty body. */
    return items > 0 ? lt_grid_rows(items) * LT_CELL_H + 46 : 56;
}

/** script3042's exact five-column placement. Partial rows remain left
 *  aligned; only the four gaps BETWEEN columns absorb spare width. */
static int
lt_grid_x(int width, int column)
{
    int const gap = (width - 212) / 4;
    return 5 + column * (LT_CELL_W + gap);
}

/** The two unfilled rectangles `thinbox_specific` creates. */
static void
lt_thinbox(uint32_t* buf, int w, int h, int x, int y, int rw, int rh)
{
    if( rw <= 0 || rh <= 0 )
        return;
    PluginDraw_Frame(buf, w, h, x, y, rw, rh, LT_THIN_OUTER);
    if( rw > 2 && rh > 2 )
        PluginDraw_Frame(buf, w, h, x + 1, y + 1, rw - 2, rh - 2, LT_THIN_INNER);
}

/** An untiled IF3 graphic scales to its component box. Nearest-neighbour is
 *  the cache renderer's pixel-preserving path for this opaque stone plate. */
static void
lt_blit_scaled(
    uint32_t* dst,
    int dw,
    int dh,
    int dx,
    int dy,
    int rw,
    int rh,
    uint32_t const* src,
    int sw,
    int sh)
{
    assert(dst);
    if( !src || rw <= 0 || rh <= 0 || sw <= 0 || sh <= 0 )
        return;
    for( int y = 0; y < rh; y++ )
        for( int x = 0; x < rw; x++ )
        {
            uint32_t const p = src[(size_t)(y * sh / rh) * (size_t)sw +
                                   (size_t)(x * sw / rw)];
            int const alpha = (int)(p >> 24);
            if( alpha > 0 )
                PluginDraw_Pixel(dst, dw, dh, dx + x, dy + y, p, alpha);
        }
}

static bool
lt_any_expanded(struct LootTrackerRuntime* rt)
{
    assert(rt);
    for( int i = 0; i < g_source_count; i++ )
        if( lt_source_visible(rt, i) && g_expanded[i] )
            return true;
    return false;
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
    {
        if( !lt_source_visible(rt, i) )
            continue;
        for( int j = 0; j < g_source[i].item_count; j++ )
        {
            struct LtItem const* row = &g_source[i].items[j];
            int at = -1;
            if( !lt_item_visible(rt, row) )
                continue;
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
    int visible_sources = 0;

    if( g_drop_view )
    {
        struct LtItem drops[LT_SOURCES_MAX * 4];
        int const n = lt_collect_drops(
            rt, drops, (int)(sizeof(drops) / sizeof(drops[0])));
        return total + (n > 0 ? lt_drop_rows(n) * LT_CELL_H + 6 : LT_HEAD_H);
    }

    for( int i = 0; i < g_source_count; i++ )
    {
        if( lt_source_visible(rt, i) )
            visible_sources++;
        total += lt_source_h(rt, i);
    }
    /* The empty note still needs a line to sit on. */
    return visible_sources > 0 ? total : total + LT_HEAD_H;
}

/** The totals band, which the game's tracker puts above the categories. */
static void
lt_draw_totals(struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h)
{
    char text[64];
    long long total_value = 0;
    int total_count = 0;

    assert(buf);

    if( g_spine_px )
        lt_blit_scaled(
            buf, w, h, 0, 0, w, LT_TOTALS_H,
            g_spine_px, g_spine_w, g_spine_h);

    /*
     * No panel stone at the left of the band: the thing the cache puts there
     * is the VIEW TOGGLE (interface 650:62 at x=4), and it is drawn below with
     * the band's other three controls. An icon here as well sat on top of it.
     */

    PluginDraw_Text(
        buf, w, h, LT_TOTALS_KEY_X, 8, &g_text, "Total count:", LT_INK_VALUE);
    PluginDraw_Text(
        buf, w, h, LT_TOTALS_KEY_X, 22, &g_text, "Total value:", LT_INK_VALUE);

    lt_display_totals(rt, &total_value, &total_count);
    lt_kmb(total_count, text, sizeof(text));
    PluginDraw_Text(buf, w, h, LT_TOTALS_VAL_X, 8, &g_text, text, LT_INK_VALUE);
    lt_kmb(total_value, text, sizeof(text));
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
    {
        uint32_t const* px = g_show_ignored ? g_ignored_hide_px : g_ignored_px;
        int const pw = g_show_ignored ? g_ignored_hide_w : g_ignored_w;
        int const ph = g_show_ignored ? g_ignored_hide_h : g_ignored_h;
        if( px )
            PluginDraw_Blit(
                buf, w, h, w - LT_BTN_R0 - LT_BTN, LT_BTN_Y,
                px, pw, ph, 0, 0, pw, ph, 0);
    }
    {
        bool const collapse = lt_any_expanded(rt);
        uint32_t const* px = collapse ? g_collapse_px : g_expand_px;
        int const pw = collapse ? g_collapse_w : g_expand_w;
        int const ph = collapse ? g_collapse_h : g_expand_h;
        if( px )
            PluginDraw_Blit(
                buf, w, h, w - LT_BTN_R1 - LT_BTN, LT_BTN_Y,
                px, pw, ph, 0, 0, pw, ph, 0);
    }
    {
        bool const high_alch = lt_name_eq(
            lt_config_string(rt, "price_source"), "High alchemy");
        uint32_t const* px = high_alch ? g_cache_px : g_alch_px;
        int const pw = high_alch ? g_cache_w : g_alch_w;
        int const ph = high_alch ? g_cache_h : g_alch_h;
        if( px )
            PluginDraw_Blit(
                buf, w, h, w - LT_BTN_R2 - LT_BTN, LT_BTN_Y,
                px, pw, ph, 0, 0, pw, ph, 0);
    }
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
 * A stack count as the client writes one.
 *
 * `uitree_emit_inv_number`'s rule, spelled the same way: the number itself
 * below a hundred thousand, thousands with a K below ten million, and millions
 * with an M above that. It is the inventory's rule because a cell here is the
 * inventory's cell -- script3042 draws an obj at a quantity and the client
 * numbers it, and a page that numbered its drops differently from the backpack
 * two inches to the right would be the thing a reader noticed.
 */
static void
lt_stack_count(int amount, char* out, size_t out_size, uint32_t* out_ink)
{
    assert(out);
    assert(out_size > 0);
    assert(out_ink);
    if( amount < 100000 )
    {
        snprintf(out, out_size, "%d", amount);
        *out_ink = LT_INK_STACK_ONES;
        return;
    }
    if( amount < 10000000 )
    {
        snprintf(out, out_size, "%dK", amount / 1000);
        *out_ink = LT_INK_STACK_K;
        return;
    }
    snprintf(out, out_size, "%dM", amount / 1000000);
    *out_ink = LT_INK_STACK_M;
}

/**
 * One item cell: the plate, the client's own icon at +2,+2, and the count.
 *
 * Shared by both views, which is the point of pulling it out -- the source
 * bands and the flat drop grid draw the same cell, and two copies of a blit
 * that has to line an icon up inside a plate is two chances to line it up
 * differently.
 *
 * The BORDERED variant is what `cc_setoutline(1)` bakes. The QUANTITY is not
 * baked with it, and believing it was is why these cells carried no number:
 * `obj_image` resolves the stack VARIANT -- the art for a big pile of coins
 * rather than one coin -- and stops there. Every stack count on screen in this
 * client is a separate text pass the emitter makes over the icon
 * (`emit_obj_stack_count`), and a plugin drawing into its own bitmap has to
 * make that pass itself.
 */
static void
lt_draw_cell(
    struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h, int x, int y,
    struct LtItem const* item, bool ignored)
{
    struct ToriRS_ImageRef image = { 0 };
    enum ToriRS_AssetState state;
    int iw = 0;
    int ih = 0;
    size_t copied = 0;

    assert(rt);
    assert(buf);
    assert(item);

    if( ignored ? g_cell_ignored_px != NULL : g_cell_px != NULL )
    {
        uint32_t const* plate = ignored ? g_cell_ignored_px : g_cell_px;
        int const plate_w = ignored ? g_cell_ignored_w : g_cell_w;
        int const plate_h = ignored ? g_cell_ignored_h : g_cell_h;
        PluginDraw_Blit(
            buf, w, h, x, y, plate, plate_w, plate_h, 0, 0, plate_w, plate_h, 0);
    }

    state = g_api->game ? g_api->game->item_image(
                              g_api, item->obj_id, item->quantity,
                              TORIRS_ITEM_ICON_BORDERED, &image)
                        : TORIRS_ASSET_MISSING;
    if( state != TORIRS_ASSET_READY ||
        !g_api->assets.image_size(g_api, image, &iw, &ih) )
    {
        /*
         * PENDING is "not yet" and everything else is "not ever", and the two
         * are not the same answer.
         *
         * Not yet: the objtype and its inventory model are on their way, the
         * picture keeps the plate it just drew, the compose is marked
         * incomplete and the refresh cadence asks again. (Zeroing the picture
         * key here -- which is what this did -- made every later frame miss
         * the cache and recompose the whole strip, for ever, because one
         * PENDING icon can stay pending. @see host fix H7.)
         *
         * Not yet also covers BUDGET: the client's icon cache is full of
         * somebody else's pictures this instant and will not be for ever.
         *
         * Not ever: MISSING, ERROR, INVALID -- this obj has no icon and no
         * amount of waiting will make one. Marking the compose incomplete for
         * it would rebuild the whole strip twice a second for the rest of the
         * session over a picture that is not coming, which is the same
         * unbounded retry wearing a slower clock. The cell keeps its plate and
         * the page stops asking.
         */
        if( state == TORIRS_ASSET_PENDING || state == TORIRS_ASSET_BUDGET )
            g_paint_incomplete = true;
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

    /*
     * Over the icon, exactly as the emitter stamps it on a cell. A single
     * unstackable is not numbered -- the client numbers a stack.
     *
     * One pass and not two: the shadow the client draws under a stack count is
     * already BAKED into the atlas, and the tint multiply leaves it black
     * while colouring the ink. @see PluginDraw_Blit. A second black pass at
     * +1,+1 is a shadow on a shadow, and at this glyph size it reads as a
     * smudge rather than an edge.
     */
    if( item->quantity > 1 )
    {
        char count[24];
        uint32_t ink = LT_INK_STACK_ONES;

        lt_stack_count(item->quantity, count, sizeof(count), &ink);
        PluginDraw_Text(buf, w, h, x + 3, y + 2, &g_text, count, ink);
    }
}

static void
lt_source_label(
    struct LootTrackerRuntime* rt,
    struct LtSource const* src,
    char* out,
    size_t out_size)
{
    char count[32];
    size_t keep;

    assert(rt);
    assert(src);
    assert(out);
    lt_kmb_precision(src->kills, 2, count, sizeof(count));
    snprintf(out, out_size, "%s x %s", src->name, count);
    if( PluginDraw_TextWidth(&g_bold, out) <= 160 )
        return;
    keep = strlen(src->name);
    while( keep > 1 )
    {
        keep--;
        snprintf(out, out_size, "%.*s... x %s", (int)keep, src->name, count);
        if( PluginDraw_TextWidth(&g_bold, out) <= 160 )
            return;
    }
}

static void
lt_draw_source(
    struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h, int top, int index)
{
    struct LtSource const* src = &g_source[index];
    char text[96];
    char amount[32];
    bool const source_ignored = lt_source_ignored(rt, src);
    int const item_count = lt_source_visible_items(rt, src);
    int item_at = 0;

    assert(rt);
    assert(buf);
    assert(lt_source_visible(rt, index));

    /* The cache's thinbox, then its tiled plate inset two pixels. */
    lt_thinbox(buf, w, h, 0, top, w, LT_HEAD_H);
    if( source_ignored ? g_spine_ignored_px != NULL : g_spine_px != NULL )
    {
        uint32_t const* plate = source_ignored ? g_spine_ignored_px : g_spine_px;
        int const plate_w = source_ignored ? g_spine_ignored_w : g_spine_w;
        int const plate_h = source_ignored ? g_spine_ignored_h : g_spine_h;
        PluginDraw_Tile(
            buf, w, h, LT_PLATE_INSET, top + LT_PLATE_INSET,
            w - LT_PLATE_INSET * 2, LT_HEAD_H - LT_PLATE_INSET * 2,
            plate, plate_w, plate_h, 0);
    }

    /* "Goblin x 2" on the left and its value on the right, both in the bold
     * face at the interfaces' orange, as script2907 sets them. */
    lt_source_label(rt, src, text, sizeof(text));
    PluginDraw_Text(buf, w, h, 6, top + 10, &g_bold, text, LT_INK_HEAD);

    lt_kmb(
        lt_source_value_visible(rt, src, g_show_ignored),
        amount,
        sizeof(amount));
    snprintf(text, sizeof(text), "%s gp", amount);
    PluginDraw_TextRight(buf, w, h, w - 6, top + 10, &g_bold, text, LT_INK_HEAD);

    if( !g_expanded[index] )
        return;

    /* This second thinbox begins one pixel over the header's bottom border,
     * just as script2907's `y + 33 - 1` does. */
    lt_thinbox(
        buf,
        w,
        h,
        0,
        top + LT_HEAD_H - 1,
        w,
        item_count > 0 ? lt_grid_rows(item_count) * LT_CELL_H + 10 : 20);

    if( item_count == 0 )
    {
        PluginDraw_Text(
            buf, w, h, 4, top + LT_HEAD_H + 3,
            &g_text, "No loot to display.", LT_INK_HEAD);
        return;
    }

    for( int i = 0; i < src->item_count; i++ )
    {
        int col;
        int row;
        int x;
        int y;

        if( !lt_item_visible(rt, &src->items[i]) )
            continue;
        col = item_at % LT_GRID_COLS;
        row = item_at / LT_GRID_COLS;
        x = lt_grid_x(w, col);
        y = top + LT_GRID_TOP + row * LT_CELL_H;
        lt_draw_cell(
            rt, buf, w, h, x, y, &src->items[i],
            lt_item_ignored(rt, &src->items[i]));
        item_at++;
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
    char const* ignored_sources = lt_config_string(rt, "ignored_sources");
    char const* ignored_items = lt_config_string(rt, "ignored_items");

#define LT_MIX(v)                                                                        \
    do                                                                                   \
    {                                                                                    \
        k ^= (uint64_t)(v);                                                              \
        k *= 1099511628211ull;                                                           \
    } while( 0 )

    assert(rt);
    LT_MIX(width);
    LT_MIX(g_drop_view ? 1 : 0);
    LT_MIX(g_show_ignored ? 1 : 0);
    LT_MIX(g_source_count);
    LT_MIX(g_session_kills);
    LT_MIX(g_session_value);
    for( char const* at = price ? price : ""; *at; at++ )
        LT_MIX((unsigned char)*at);
    for( char const* at = ignored_sources ? ignored_sources : ""; *at; at++ )
        LT_MIX((unsigned char)*at);
    for( char const* at = ignored_items ? ignored_items : ""; *at; at++ )
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
            for( char const* at = src->items[j].name; *at; at++ )
                LT_MIX((unsigned char)*at);
        }
    }
    /*
     * The band or cell MENU, which is part of the picture: it is painted into
     * the same well, so a menu that opened without moving this hash would not
     * appear until something else changed.
     */
    LT_MIX(g_menu_kind);
    LT_MIX(g_menu_source_id);
    LT_MIX(g_menu_obj_id);
    LT_MIX(g_menu_x);
    LT_MIX(g_menu_y);
    /* And the retry, which is how an icon that arrived late gets drawn: the
     * inputs are otherwise identical, so nothing would ask for the compose
     * that would now succeed. It moves at the refresh cadence and only while
     * a compose is incomplete. */
    LT_MIX(g_paint_retry);
#undef LT_MIX
    return k;
}

/* ------------------------------------------------------------------------ */
/* The band and cell menus                                                   */
/*                                                                           */
/* script2907 offers Collapse/Expand, Clear data and Ignore on a category     */
/* header, and script3042 offers Check and Ignore on an item cell. Both are   */
/* SECONDARY-click menus in the cache's own tracker, and a CUSTOM well had no */
/* secondary-click channel at all until this round: every one of these ops    */
/* had to stand as a button under a selected row, which is why selecting was  */
/* a separate gesture from expanding.                                        */
/*                                                                           */
/* The channel exists now -- PorcelainRow::on_menu, with well-local x and y   */
/* under the same generation and serial fences a click carries. What it does  */
/* NOT carry is a chooser: the panel presenters have no popup of their own,   */
/* and the world's minimenu is suppressed over the plugin window by           */
/* app_chrome_wants_pointer, so a right click that reaches a well is the LAST */
/* thing the client will do about it. The list is therefore painted into the  */
/* well, which is what a well is for, and picked by the next primary click.   */
/* @see the report's note on the shape of this channel.                       */
/* ------------------------------------------------------------------------ */

#define LT_MENU_W 108
#define LT_MENU_ROW_H 15
#define LT_MENU_PAD 2
#define LT_MENU_ROWS_MAX 3

/** The ops this menu offers, in the cache's own order. @see LtMenuKind. */
static int
lt_menu_rows(struct LootTrackerRuntime* rt, char rows[LT_MENU_ROWS_MAX][32])
{
    assert(rt);
    assert(rows);

    if( g_menu_kind == LT_MENU_BAND )
    {
        int const index = lt_source_index_by_id(rt, g_menu_source_id);
        if( index < 0 )
            return 0;
        snprintf(
            rows[0], sizeof(rows[0]), "%s",
            g_expanded[index] ? "Collapse" : "Expand");
        snprintf(rows[1], sizeof(rows[1]), "Clear data");
        snprintf(
            rows[2], sizeof(rows[2]), "%s",
            lt_source_ignored(rt, &g_source[index]) ? "Stop ignoring" : "Ignore");
        return 3;
    }
    if( g_menu_kind == LT_MENU_CELL )
    {
        snprintf(rows[0], sizeof(rows[0]), "Check");
        snprintf(
            rows[1], sizeof(rows[1]), "%s",
            lt_listed(lt_config_string(rt, "ignored_items"), g_menu_item_name)
                ? "Stop ignoring"
                : "Ignore");
        return 2;
    }
    return 0;
}

static int
lt_menu_h(int rows)
{
    return rows * LT_MENU_ROW_H + LT_MENU_PAD * 2;
}

/** The menu, over everything: it is the last thing the compose draws. */
static void
lt_draw_menu(struct LootTrackerRuntime* rt, uint32_t* buf, int w, int h)
{
    char rows[LT_MENU_ROWS_MAX][32];
    int const count = lt_menu_rows(rt, rows);
    int height;

    assert(rt);
    assert(buf);
    if( count <= 0 )
        return;
    height = lt_menu_h(count);

    lt_thinbox(buf, w, h, g_menu_x, g_menu_y, LT_MENU_W, height);
    if( g_spine_px )
        PluginDraw_Tile(
            buf, w, h, g_menu_x + LT_PLATE_INSET, g_menu_y + LT_PLATE_INSET,
            LT_MENU_W - LT_PLATE_INSET * 2, height - LT_PLATE_INSET * 2,
            g_spine_px, g_spine_w, g_spine_h, 0);
    for( int i = 0; i < count; i++ )
        PluginDraw_Text(
            buf, w, h, g_menu_x + 5,
            g_menu_y + LT_MENU_PAD + i * LT_MENU_ROW_H + 3, &g_text, rows[i],
            LT_INK_HEAD);
}

/* ------------------------------------------------------------------------ */
/* The picture                                                               */
/* ------------------------------------------------------------------------ */

/**
 * Rasterise the whole strip.
 *
 * This is a `PorcelainPaintFn`: the layer calls it at most once per (key,
 * inputs) and publishes what it fills, so the "has anything moved" test that
 * used to live at the top of this function -- and the malloc'd buffer it kept
 * to answer it -- are the layer's now. @see Porcelain_Derived.
 *
 * It returns true even when a cell's icon was not resident, because false is
 * TERMINAL for these inputs: a strip that failed once because one obj icon was
 * a frame late would never be drawn again. The incomplete flag and the retry
 * counter are what bring it back. @see lt_draw_cell.
 */
static bool
lt_paint_strip(struct ToriRS_Api* api, void* user, uint32_t* argb, int width, int height)
{
    struct LootTrackerRuntime runtime = { api, user };
    struct LootTrackerRuntime* rt = &runtime;
    int top = 0;
    int visible_sources = 0;

    assert(api);
    assert(user);
    assert(argb);

    /* Transparent, so the panel's own backing shows through exactly as the
     * interface's does behind its bands. */
    memset(argb, 0, (size_t)width * (size_t)height * sizeof(*argb));
    g_paint_incomplete = false;

    lt_draw_totals(rt, argb, width, height);
    top = LT_TOTALS_H + LT_HEAD_GAP;
    for( int i = 0; i < g_source_count; i++ )
        visible_sources += lt_source_visible(rt, i) ? 1 : 0;

    if( visible_sources == 0 )
        PluginDraw_Text(
            argb, width, height, 4, top + 3, &g_text, "No loot to display.",
            LT_INK_HEAD);
    else if( g_drop_view )
    {
        struct LtItem drops[LT_SOURCES_MAX * 4];
        int const n = lt_collect_drops(
            rt, drops, (int)(sizeof(drops) / sizeof(drops[0])));

        if( n == 0 )
            PluginDraw_Text(
                argb, width, height, 4, top + 3,
                &g_text, "No loot to display.", LT_INK_HEAD);
        for( int i = 0; i < n; i++ )
            lt_draw_cell(
                rt, argb, width, height,
                lt_grid_x(width, i % LT_GRID_COLS),
                top + (i / LT_GRID_COLS) * LT_CELL_H, &drops[i],
                lt_item_ignored(rt, &drops[i]));
    }
    else
        for( int i = 0; i < g_source_count; i++ )
        {
            int const source_h = lt_source_h(rt, i);
            if( source_h <= 0 )
                continue;
            lt_draw_source(rt, argb, width, height, top, i);
            top += source_h;
        }

    lt_draw_menu(rt, argb, width, height);
    return true;
}

/**
 * The well's own draw pass: hand the layer the picture's inputs and blit what
 * comes back.
 *
 * The compose is keyed on everything a reader can SEE, so an unchanged strip
 * costs one hash compare and no rasterisation -- and, because the row's
 * `paint_key` is that same number, an unchanged strip is not even asked to
 * draw. The 2 Hz unconditional redraw the ledger calls SUPPORTED-SLOW is
 * gone with it.
 */
static void
lt_paint(
    struct ToriRS_Api* api, void* user, char const* key, struct ToriRS_Graphics* draw)
{
    struct LootTrackerRuntime runtime = { api, user };
    struct LootTrackerRuntime* rt = &runtime;
    struct ToriRS_DrawContext context = { .struct_size = sizeof(context) };
    enum PorcelainDerivedState state = PORCELAIN_DERIVED_FAILED;
    struct ToriRS_ImageRef image;
    uint64_t inputs;
    int height;

    assert(api);
    assert(user);
    assert(key);
    assert(draw);
    (void)key;

    if( !draw->context(draw, &context) || context.bounds.width <= 0 )
    {
        /* No region to draw into is not a blank page: there is no page. */
        return;
    }
    g_strip_blank = true;
    /*
     * The art crosses the IO queue, so the first passes after a start have
     * nothing to draw with. A CUSTOM paint that stages nothing keeps the last
     * picture, which for the first pass is nothing at all; the retry brings
     * it back. @see LootTrackerState::paint_incomplete.
     */
    if( !lt_art_ready(rt) )
    {
        g_paint_incomplete = true;
        return;
    }

    /* The picture and the right-anchored hit boxes are measured against the
     * SAME width, which is what keeps them from disagreeing. */
    if( g_well_w != context.bounds.width )
    {
        g_well_w = context.bounds.width;
        lt_changed(rt);
    }
    height = lt_strip_h(rt);
    if( height <= 0 )
        return;
    inputs = lt_compose_key(rt, context.bounds.width);
    image = Porcelain_Derived(
        g_porcelain, "strip", &inputs, sizeof(inputs), context.bounds.width,
        height, lt_paint_strip, rt->state, &state);
    if( state == PORCELAIN_DERIVED_READY && image.value )
    {
        draw->image(draw, image, 0, 0, 255);
        g_strip_blank = false;
        g_blank_frames = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* The store                                                                 */
/* ------------------------------------------------------------------------ */

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
 * @return true when anything changed, which is what decides a re-describe.
 */
static bool
lt_sync_store(struct LootTrackerRuntime* rt)
{
    struct ToriRS_LootSource src;
    int before = g_source_count;
    int count = 0;
    bool changed = false;
    int old_id[LT_SOURCES_MAX];
    int old_kills[LT_SOURCES_MAX];
    long long old_value[LT_SOURCES_MAX];
    bool old_expanded[LT_SOURCES_MAX];

    assert(rt);

    for( int i = 0; i < before; i++ )
    {
        old_id[i] = g_source[i].id;
        old_kills[i] = g_source[i].kills;
        old_value[i] = lt_source_value_visible(rt, &g_source[i], true);
        old_expanded[i] = g_expanded[i];
    }

    g_session_kills = 0;
    g_session_value = 0;

    for( int it = g_api->game->loot_source_next(g_api, -1, &src); it >= 0;
         it = g_api->game->loot_source_next(g_api, it, &src) )
    {
        struct LtSource* dst;
        struct LtSource previous;
        struct ToriRS_LootRow row;
        char name[64];
        int was = -1;

        lt_clean_name(src.name, name, sizeof(name));
        if( !name[0] )
            continue;
        if( count >= LT_SOURCES_MAX )
            break;

        dst = &g_source[count];
        previous = *dst;
        memset(dst, 0, sizeof(*dst));
        snprintf(dst->name, sizeof(dst->name), "%s", name);
        dst->kills = src.kill_count;
        dst->id = src.id;
        g_expanded[count] = true;
        for( int old = 0; old < before; old++ )
            if( old_id[old] == src.id )
            {
                g_expanded[count] = old_expanded[old];
                was = old;
                break;
            }

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
            dst->items[dst->item_count++] = item;
        }
        if( count >= before || memcmp(&previous, dst, sizeof(*dst)) != 0 )
            changed = true;
        /*
         * The kill announcement, on the lane that HAS the store.
         *
         * The delta and not the total: what a person wants told is what this
         * kill was worth. The first sync of a session announces nothing --
         * every source is new there, and greeting somebody who opened the
         * page with one line per source it already held is not the feature.
         */
        if( g_synced_once && dst->kills > (was >= 0 ? old_kills[was] : 0) )
            lt_announce(
                rt, dst->name, dst->kills,
                lt_source_value_visible(rt, dst, true) -
                    (was >= 0 ? old_value[was] : 0));
        count++;
    }

    g_source_count = count;
    if( before != count )
        changed = true;
    /* The selection is an ID, so a reorder re-finds it and a drop clears it
     * without anything here having to say so. */
    lt_revalue(rt);
    g_synced_once = true;
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

/* ------------------------------------------------------------------------ */
/* The description                                                           */
/* ------------------------------------------------------------------------ */

/**
 * The well's INPUT identity: what a click at a given y means.
 *
 * The ordered list of (visible source id, visible item count), the view, the
 * expansion state and whatever menu is open -- everything that decides which
 * thing lies under a coordinate, and nothing that does not. A value change is
 * deliberately absent: a band whose gp figure moved is the same band in the
 * same place, and putting the figure in here would retire the well's retained
 * run twice a second for a picture whose geometry never moved.
 */
static uint64_t
lt_hit_key(struct LootTrackerRuntime* rt)
{
    uint64_t k = 1469598103934665603ull;

#define LT_MIX(v)                                                                        \
    do                                                                                   \
    {                                                                                    \
        k ^= (uint64_t)(v);                                                              \
        k *= 1099511628211ull;                                                           \
    } while( 0 )

    assert(rt);
    LT_MIX(g_drop_view ? 1 : 0);
    LT_MIX(g_show_ignored ? 1 : 0);
    LT_MIX(g_menu_kind);
    LT_MIX(g_menu_x);
    LT_MIX(g_menu_y);
    for( int i = 0; i < g_source_count; i++ )
    {
        if( !lt_source_visible(rt, i) )
            continue;
        LT_MIX(g_source[i].id);
        LT_MIX(lt_source_visible_items(rt, &g_source[i]));
        LT_MIX(g_expanded[i] ? 1 : 0);
    }
#undef LT_MIX
    return k;
}

static void lt_strip_click(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action);
static void lt_strip_menu(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action);
static void lt_detail_action(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action);

/**
 * The page, described.
 *
 * One well and, when a source is selected, the detail block. The ordered
 * (key, kind) sequence IS the declaration: opening or closing the detail block
 * is the page's one legitimate rebuild, and everything else a kill can do --
 * a taller strip, a new band, a bigger number, a caption that flipped from
 * Ignore to Stop ignoring -- is a property of a row the page already has.
 *
 * What is NOT here is the bookkeeping that used to be: built_rows,
 * built_detail, built_detail_source_id, built_source_id[], built_source_items[]
 * and page_built existed to answer "does the host's copy still match mine",
 * which is the question the reconciler is.
 */
static void
lt_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct LootTrackerRuntime runtime = { ((struct LootTrackerState*)user)->api, user };
    struct LootTrackerRuntime* rt = &runtime;
    struct PorcelainRow row;
    int const detail = lt_detail_visible_index(rt);
    char kills[64];
    char value[64];
    char per_kill[64];

    assert(describe);
    assert(user);

    /*
     * ONE drawing well for every band, which is what the CS2 tracker is: a
     * header per source with its drops under it. Built out of panel controls
     * it would be a header plus a control per item and would run out of the
     * 48-control budget inside one boss trip.
     */
    memset(&row, 0, sizeof(row));
    row.key = "strip";
    row.kind = PORCELAIN_ROW_CUSTOM;
    row.height = lt_strip_h(rt);
    row.hit_key = lt_hit_key(rt);
    row.paint_key = lt_compose_key(rt, g_well_w);
    row.paint = lt_paint;
    row.on_action = lt_strip_click;
    row.on_menu = lt_strip_menu;
    row.user = user;
    Porcelain_Row(describe, &row);

    /*
     * No Session rows: the strip's own totals band carries them, exactly as
     * the game's tracker does, and two readouts of one number that round
     * differently is how they come to disagree.
     *
     * No "clear all" row either. The tracker this is a port of has no such
     * control: its clears are ops on a BAND -- one source at a time -- and a
     * page-wide button was this port's invention.
     */
    if( detail < 0 )
        return;

    {
        struct LtSource const* src = &g_source[detail];
        long long const total = lt_source_value_visible(rt, src, g_show_ignored);

        lt_commas(src->kills, kills, sizeof(kills));
        lt_commas(total, value, sizeof(value));
        lt_commas(src->kills > 0 ? total / src->kills : 0, per_kill, sizeof(per_kill));

        memset(&row, 0, sizeof(row));
        row.key = "sec_detail";
        row.kind = PORCELAIN_ROW_HEADING;
        row.text = src->name;
        Porcelain_Row(describe, &row);

        memset(&row, 0, sizeof(row));
        row.key = "d_kills";
        row.kind = PORCELAIN_ROW_KEY_VALUE;
        row.label = "Kills";
        row.text = kills;
        Porcelain_Row(describe, &row);

        memset(&row, 0, sizeof(row));
        row.key = "d_value";
        row.kind = PORCELAIN_ROW_KEY_VALUE;
        row.label = "Value";
        row.text = value;
        Porcelain_Row(describe, &row);

        memset(&row, 0, sizeof(row));
        row.key = "d_per_kill";
        row.kind = PORCELAIN_ROW_KEY_VALUE;
        row.label = "Value per kill";
        row.text = per_kill;
        Porcelain_Row(describe, &row);

        memset(&row, 0, sizeof(row));
        row.key = "d_clear";
        row.kind = PORCELAIN_ROW_BUTTON;
        row.text = "Clear data";
        row.on_action = lt_detail_action;
        row.user = user;
        Porcelain_Row(describe, &row);

        /*
         * The caption is the state the press will LEAVE, so it has to flip the
         * moment the list changes. It is a patched property of this row now;
         * before host fix H1 a caption set on its own reported OK and applied
         * nothing, and this one only ever changed because the action that
         * wrote the list also re-declared the whole page.
         */
        memset(&row, 0, sizeof(row));
        row.key = "d_ignore";
        row.kind = PORCELAIN_ROW_BUTTON;
        row.text = lt_source_ignored(rt, src) ? "Stop ignoring" : "Ignore";
        row.on_action = lt_detail_action;
        row.user = user;
        Porcelain_Row(describe, &row);
    }
}

/* ------------------------------------------------------------------------ */
/* What a person did                                                         */
/* ------------------------------------------------------------------------ */

/** The item cell at a well-local point, and the source it belongs to.
 *  False for a point that is not over one. */
static bool
lt_cell_at(
    struct LootTrackerRuntime* rt, int x, int y, struct LtItem* out_item)
{
    int const top = LT_TOTALS_H + LT_HEAD_GAP;
    int column = -1;
    int index;

    assert(rt);
    assert(out_item);

    for( int c = 0; c < LT_GRID_COLS; c++ )
    {
        int const at = lt_grid_x(g_well_w, c);
        if( x >= at && x < at + LT_CELL_W )
            column = c;
    }
    if( column < 0 )
        return false;

    if( g_drop_view )
    {
        struct LtItem drops[LT_SOURCES_MAX * 4];
        int const n = lt_collect_drops(
            rt, drops, (int)(sizeof(drops) / sizeof(drops[0])));
        if( y < top )
            return false;
        index = (y - top) / LT_CELL_H * LT_GRID_COLS + column;
        if( index < 0 || index >= n )
            return false;
        *out_item = drops[index];
        return true;
    }
    {
        int local_y = 0;
        int const source = lt_source_at(rt, y, &local_y);
        int seen = 0;
        if( source < 0 || !g_expanded[source] || local_y < LT_GRID_TOP )
            return false;
        index = (local_y - LT_GRID_TOP) / LT_CELL_H * LT_GRID_COLS + column;
        for( int i = 0; i < g_source[source].item_count; i++ )
        {
            if( !lt_item_visible(rt, &g_source[source].items[i]) )
                continue;
            if( seen++ != index )
                continue;
            *out_item = g_source[source].items[i];
            return true;
        }
    }
    return false;
}

static void
lt_menu_close(struct LootTrackerRuntime* rt)
{
    assert(rt);
    if( g_menu_kind == LT_MENU_NONE )
        return;
    g_menu_kind = LT_MENU_NONE;
    g_menu_source_id = 0;
    g_menu_obj_id = 0;
    g_menu_item_name[0] = '\0';
    lt_changed(rt);
}

/** Clear one source's record: the store's own op on the store lane, and this
 *  plugin's record on the lane that has none. A refusal is logged and the page
 *  is left alone rather than clearing locally. */
static void
lt_clear_source(struct LootTrackerRuntime* rt, int source_id)
{
    int const source = lt_source_index_by_id(rt, source_id);

    assert(rt);
    if( source < 0 )
        return;
    if( lt_infers_loot(rt) )
    {
        lt_source_remove(rt, source);
        if( g_detail_source_id == source_id )
            g_detail_source_id = 0;
        return;
    }
    if( !g_api->game->loot_source_clear ||
        !g_api->game->loot_source_clear(g_api, source_id) )
    {
        Porcelain_Finding(
            g_porcelain, "loot_source_clear", PORCELAIN_EL(NONE),
            PORCELAIN_FINDING_REFUSED, "clear data");
        g_api->core.log(g_api, "loot-tracker: could not clear loot source %d", source_id);
        return;
    }
    if( g_detail_source_id == source_id )
        g_detail_source_id = 0;
    (void)lt_sync_changed(rt, true);
    lt_changed(rt);
}

/**
 * Ignore or stop ignoring one source.
 *
 * Config callbacks are synchronous in the host, so the detail block is closed
 * BEFORE the filter is published or the re-entrant reconciliation would index
 * a row it just removed.
 */
static void
lt_ignore_source(struct LootTrackerRuntime* rt, int source_id)
{
    int const source = lt_source_index_by_id(rt, source_id);
    char name[sizeof(g_source[0].name)];

    assert(rt);
    if( source < 0 )
        return;
    snprintf(name, sizeof(name), "%s", g_source[source].name);
    g_detail_source_id = 0;
    lt_list_toggle(rt, "ignored_sources", name);
    if( !lt_infers_loot(rt) )
        (void)lt_sync_changed(rt, true);
    lt_changed(rt);
}

/**
 * The band's own menu, and the cell's, as a picked row.
 *
 * The ops are exactly script2907's and script3042's, in their order, and the
 * FIRST of each is what a primary click already does -- Collapse/Expand on a
 * header -- which is the rule every minimenu in this game follows.
 */
static void
lt_menu_pick(struct LootTrackerRuntime* rt, int picked)
{
    int const kind = g_menu_kind;
    int const source_id = g_menu_source_id;
    char item[sizeof(g_menu_item_name)];
    int const quantity = g_menu_item_quantity;
    long long unit;

    assert(rt);
    snprintf(item, sizeof(item), "%s", g_menu_item_name);
    {
        struct LtItem snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.cost = g_menu_item_cost;
        unit = lt_unit_value(rt, &snapshot);
    }
    lt_menu_close(rt);

    if( kind == LT_MENU_BAND )
    {
        int const source = lt_source_index_by_id(rt, source_id);
        if( source < 0 )
            return;
        if( picked == 0 )
        {
            g_expanded[source] = !g_expanded[source];
            lt_changed(rt);
            return;
        }
        if( picked == 1 )
        {
            lt_clear_source(rt, source_id);
            return;
        }
        lt_ignore_source(rt, source_id);
        return;
    }
    if( kind != LT_MENU_CELL || !item[0] )
        return;
    if( picked == 0 )
    {
        /* Check: what this stack is worth through the configured basis, which
         * is the one number the cell itself has no room to print. */
        char line[200];
        char amount[32];
        lt_commas(unit * quantity, amount, sizeof(amount));
        snprintf(line, sizeof(line), "%s x%d: %s gp", item, quantity, amount);
        g_api->core.notify(g_api, line);
        return;
    }
    lt_list_toggle(rt, "ignored_items", item);
    lt_changed(rt);
}

/** A SECONDARY click in the well: open the band's or the cell's menu. */
static void
lt_strip_menu(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct LootTrackerRuntime runtime = { api, user };
    struct LootTrackerRuntime* rt = &runtime;
    struct LtItem item;
    char rows[LT_MENU_ROWS_MAX][32];
    int count;
    int height;

    assert(api);
    assert(user);
    assert(action);

    g_api->panel.attention(g_api, false);
    lt_menu_close(rt);
    /* The totals band's four controls are not a subject, so a right click
     * there means nothing, exactly as it does in the cache's own band. */
    if( action->y < LT_TOTALS_H )
        return;

    if( lt_cell_at(rt, action->x, action->y, &item) )
    {
        g_menu_kind = LT_MENU_CELL;
        g_menu_obj_id = item.obj_id;
        g_menu_item_quantity = item.quantity;
        g_menu_item_cost = item.cost;
        snprintf(g_menu_item_name, sizeof(g_menu_item_name), "%s", item.name);
    }
    else if( !g_drop_view )
    {
        int local_y = 0;
        int const source = lt_source_at(rt, action->y, &local_y);
        if( source < 0 )
            return;
        g_menu_kind = LT_MENU_BAND;
        g_menu_source_id = g_source[source].id;
    }
    else
        return;

    count = lt_menu_rows(rt, rows);
    if( count <= 0 )
    {
        g_menu_kind = LT_MENU_NONE;
        return;
    }
    height = lt_menu_h(count);
    /* Placed where the click was, and clamped so the whole list is inside the
     * well: the well is the only surface this plugin can draw on, and a menu
     * hanging off the bottom of it is a menu whose last op cannot be reached. */
    g_menu_x = action->x;
    if( g_menu_x > g_well_w - LT_MENU_W )
        g_menu_x = g_well_w - LT_MENU_W;
    if( g_menu_x < 0 )
        g_menu_x = 0;
    g_menu_y = action->y;
    if( g_menu_y > lt_strip_h(rt) - height )
        g_menu_y = lt_strip_h(rt) - height;
    if( g_menu_y < 0 )
        g_menu_y = 0;
    lt_changed(rt);
}

/**
 * A primary click in the well.
 *
 * The bands are variable height -- an expanded one carries its grid -- so the
 * source is found by walking them rather than by dividing, and the header's
 * own first op decides what the click means: inside the 33-tall band it
 * EXPANDS or collapses, which is script2907's Collapse/Expand, and it also
 * selects the source so the detail block has something to act on.
 */
static void
lt_strip_click(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct LootTrackerRuntime runtime = { api, user };
    struct LootTrackerRuntime* rt = &runtime;
    int local_y = 0;
    int source;

    assert(api);
    assert(user);
    assert(action);

    g_api->panel.attention(g_api, false);

    /* An open menu is modal to the well: the click that follows one belongs to
     * picking from it or to dismissing it, never to what is underneath. */
    if( g_menu_kind != LT_MENU_NONE )
    {
        char rows[LT_MENU_ROWS_MAX][32];
        int const count = lt_menu_rows(rt, rows);
        int const height = lt_menu_h(count);
        if( count > 0 && action->x >= g_menu_x && action->x < g_menu_x + LT_MENU_W &&
            action->y >= g_menu_y && action->y < g_menu_y + height )
        {
            int picked = (action->y - g_menu_y - LT_MENU_PAD) / LT_MENU_ROW_H;
            if( picked < 0 )
                picked = 0;
            if( picked >= count )
                picked = count - 1;
            lt_menu_pick(rt, picked);
            return;
        }
        lt_menu_close(rt);
        return;
    }

    /*
     * The TOTALS band's own four controls first, because they sit above every
     * source and a click there is not a click on a band.
     *
     * Same four the cache offers, in the same places: the view toggle at the
     * left, then value-basis, collapse-all and the ignore list at the right.
     * `g_well_w` is the width the strip was last COMPOSED at, which is what
     * the right-anchored three were placed against.
     */
    if( action->y < LT_TOTALS_H )
    {
        int const w = g_well_w;
        if( action->x >= LT_BTN_LEFT_X && action->x < LT_BTN_LEFT_X + LT_BTN )
            g_drop_view = !g_drop_view;
        else if( action->x >= w - LT_BTN_R0 - LT_BTN && action->x < w - LT_BTN_R0 )
        {
            /* Same show/hide ignored mode as interface 650:59. Data is
             * retained while hidden, so the opposite icon and the exact
             * ignored source/item plates can be shown immediately. */
            int detail;
            g_show_ignored = !g_show_ignored;
            detail = lt_detail_index(rt);
            if( !g_show_ignored && detail >= 0 &&
                lt_source_ignored(rt, &g_source[detail]) )
                g_detail_source_id = 0;
        }
        else if( action->x >= w - LT_BTN_R1 - LT_BTN && action->x < w - LT_BTN_R1 )
        {
            /* Collapse all -- or expand all when everything is already shut,
             * which is what makes one button enough. */
            bool const any = lt_any_expanded(rt);
            for( int i = 0; i < g_source_count; i++ )
                if( lt_source_visible(rt, i) )
                    g_expanded[i] = !any;
        }
        else if( action->x >= w - LT_BTN_R2 - LT_BTN && action->x < w - LT_BTN_R2 )
        {
            /* The value basis, which is the same config key the settings form
             * offers as a dropdown. */
            char const* now = lt_config_string(rt, "price_source");
            (void)g_api->config.set(
                g_api, "price_source",
                now && lt_name_eq(now, "High alchemy") ? "Cache value"
                                                       : "High alchemy");
        }
        else
            return;
        lt_changed(rt);
        return;
    }

    /* The flat drop grid has no bands to open. */
    if( g_drop_view )
        return;

    source = lt_source_at(rt, action->y, &local_y);
    if( source < 0 )
        return;
    if( local_y < LT_HEAD_H )
        g_expanded[source] = !g_expanded[source];
    g_detail_source_id =
        g_detail_source_id == g_source[source].id && local_y >= LT_HEAD_H
            ? 0
            : g_source[source].id;
    lt_changed(rt);
}

/** The detail block's two buttons, which are ops on the BAND they sit under.
 *  The `d_close` branch that used to be here is gone with the row that never
 *  existed: keys are declared once, in the description, so an action for a key
 *  nobody described cannot arrive. */
static void
lt_detail_action(
    struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct LootTrackerRuntime runtime = { api, user };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(user);
    assert(action);
    assert(action->key);

    g_api->panel.attention(g_api, false);
    if( strcmp(action->key, "d_clear") == 0 )
        lt_clear_source(rt, g_detail_source_id);
    else
        lt_ignore_source(rt, g_detail_source_id);
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER;

static void
lt_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(state_ptr);
    /* A page with no layer is a page that cannot be described. */
    assert(api->porcelain);

    rt->state->api = api;
    g_source_count = 0;
    g_session_value = 0;
    g_session_kills = 0;
    g_detail_source_id = 0;
    g_pending_count = 0;
    g_next_fallback_source_id = 0;
    g_page_visible = false;
    g_next_panel_ms = 0;
    g_loot_revision = 0;
    g_synced_once = false;
    g_paint_incomplete = false;
    g_paint_retry = 0;
    g_menu_kind = LT_MENU_NONE;
    g_well_w = TORIRS_PANEL_WIDTH_DEFAULT;
    g_show_ignored = false;
    /*
     * OPEN by default, which is what the game's own tracker does: a band with
     * its drops under it is the thing a person opened the panel to see, and a
     * list of closed bands is a list of names. The row click still collapses
     * one, so a long trip can be tidied.
     */
    for( size_t i = 0; i < sizeof(g_expanded) / sizeof(g_expanded[0]); i++ )
        g_expanded[i] = true;

    g_porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_LOOT_TRACKER, state_ptr);
    /*
     * The lane gate, asked ONCE. `loot_events` is App_UiLogic == CS2, which is
     * a boot fact: the producers are the CS2 host's. @see lt_infers_loot.
     */
    g_store_lane = Porcelain_Has(g_porcelain, "loot_events");
    /*
     * Event loot -- barrows, raid chests, clue caskets -- is inventory-diff
     * work needing a reliable "this interface just opened" fence per revision,
     * and PVP loot needs a player-death signal the bus does not raise. Declared
     * rather than left as a silence, so the clean-findings gate is satisfiable
     * and the absence is a decision somebody can read.
     */
    Porcelain_ExpectUnsupported(
        g_porcelain, "event_loot",
        "no per-revision interface-open edge to diff an inventory against");
    Porcelain_ExpectUnsupported(
        g_porcelain, "pvp_loot", "the bus raises no player-death event");
    /* The cache popout's own Loot Tools icon (graphic 4900), matching the CS2
     * panel this page reproduces. The PAGE face only: every knob this plugin
     * has is a config key, so its settings face is the generated form. */
    Porcelain_Panel(
        g_porcelain, "panel_icon.png", TORIRS_PANEL_WIDTH_DEFAULT,
        PORCELAIN_FACE_PAGE);
    Porcelain_Describe(g_porcelain, lt_describe, state_ptr);
}

static void
lt_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    g_page_visible = false;
    g_pending_count = 0;
    /* Porcelain releases the derived picture with everything else it holds. */
    Porcelain_Close(g_porcelain);
    g_porcelain = NULL;
    PluginDraw_AtlasFree(g_api, &g_bold);
    PluginDraw_AtlasFree(g_api, &g_text);
    PluginDraw_ImageFree(g_api, &g_spine_px, &g_img_spine);
    PluginDraw_ImageFree(
        g_api, &g_spine_ignored_px, &g_img_spine_ignored);
    PluginDraw_ImageFree(g_api, &g_cell_px, &g_img_cell);
    PluginDraw_ImageFree(
        g_api, &g_cell_ignored_px, &g_img_cell_ignored);
    PluginDraw_ImageFree(g_api, &g_view_px, &g_img_view);
    PluginDraw_ImageFree(g_api, &g_view2_px, &g_img_view2);
    PluginDraw_ImageFree(g_api, &g_alch_px, &g_img_alch);
    PluginDraw_ImageFree(g_api, &g_cache_px, &g_img_cache);
    PluginDraw_ImageFree(g_api, &g_collapse_px, &g_img_collapse);
    PluginDraw_ImageFree(g_api, &g_expand_px, &g_img_expand);
    PluginDraw_ImageFree(g_api, &g_ignored_px, &g_img_ignored);
    PluginDraw_ImageFree(
        g_api, &g_ignored_hide_px, &g_img_ignored_hide);
}

/**
 * The fence, once a frame, and only while the page is on screen.
 *
 * Nothing runs while the page is hidden: no sync, no describe, no reconcile
 * and no compose. The state still advances -- the store keeps recording and
 * the inference candidates keep expiring -- and the work happens when the page
 * is selected again.
 */
static void
lt_frame_start(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    (void)event;

    assert(api);
    assert(state_ptr);
    if( !g_page_visible )
        return;
    /*
     * A well with no picture at all is an EMPTY PAGE, and nothing else in this
     * plugin will ask for it again before the half-second refresh. That wait
     * is what a reader sees as a blank box: the art crosses the IO queue, the
     * paint returns having staged nothing, the layer declines rather than
     * erasing, and the next chance to draw is a clock tick away.
     *
     * A stale readout can wait half a second. A blank page cannot, so it is
     * asked again on this frame. The poll costs the art loads and NO
     * rasterisation -- lt_paint returns before the compose while the art is
     * missing -- and it stops on the frame the first picture is staged.
     *
     * Two bounds keep this from becoming the unbounded per-frame recompose the
     * ledger names (H7). It runs only while the last pass the host ASKED for
     * declined, so a well nobody is painting is not asking; and it gives up
     * after LT_BLANK_RETRY_FRAMES, because art that is two seconds late is not
     * late, it is absent.
     */
    if( g_strip_blank && g_blank_frames < LT_BLANK_RETRY_FRAMES )
    {
        g_blank_frames++;
        g_paint_retry++;
        lt_changed(rt);
    }
    Porcelain_Fence(g_porcelain);
    Porcelain_Commit(api);
}

static void
lt_panel_build(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(panel);

    /* The store is read before the first declaration so the page arrives whole
     * rather than empty and then grown, which would be a rebuild for nothing. */
    if( view == TORIRS_PANEL_VIEW_PAGE && !lt_infers_loot(rt) )
        (void)lt_sync_changed(rt, true);
    Porcelain_PanelBuild(g_porcelain, panel, view);
}

static void
lt_panel_draw(
    struct ToriRS_Api* api,
    void* state_ptr,
    char const* node,
    struct ToriRS_Graphics* draw)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(node);
    assert(draw);
    (void)Porcelain_PanelDraw(g_porcelain, node, draw);
}

static void
lt_panel_action(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_PanelActionEvent const* ev)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;

    assert(api);
    assert(ev);
    (void)Porcelain_PanelAction(g_porcelain, ev);
}

/** The shell moved, showed or hid this page. */
static void
lt_panel_layout(
    struct ToriRS_Api* api,
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
    if( !g_page_visible )
    {
        /* A menu nobody can see is a menu whose next click lands somewhere
         * else entirely. */
        g_menu_kind = LT_MENU_NONE;
        return;
    }
    if( !lt_infers_loot(rt) )
        (void)lt_sync_changed(rt, false);
    lt_changed(rt);
}

static void
lt_tick(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct LootTrackerRuntime runtime = { api, state_ptr };
    struct LootTrackerRuntime* rt = &runtime;
    uint64_t const now = g_api->core.frame_ms(g_api);
    (void)event;

    assert(api);

    if( lt_infers_loot(rt) )
        lt_pending_expire(rt, now);

    if( now < g_next_panel_ms )
        return;
    g_next_panel_ms = now + LT_PANEL_REFRESH_MS;

    if( !g_page_visible )
        return;

    /* OldSchool mirrors its authoritative store; RS2 was updated directly by
     * the inference callbacks above. */
    if( !lt_infers_loot(rt) && lt_sync_changed(rt, false) )
        lt_changed(rt);

    /* The art, or an obj icon, was not resident when the picture was last
     * composed. This is the ONLY thing that retries it, and it runs at the
     * refresh cadence rather than per frame. */
    if( g_paint_incomplete )
    {
        g_paint_retry++;
        lt_changed(rt);
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
    struct ToriRS_Api* api,
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

    assert(api);
    if( filtered )
    {
        /*
         * Ignoring is a VIEW over retained loot, not deletion. That is what
         * makes interface 650's Show/Hide ignored button reversible on both
         * the CS2 store lane and rs289's inferred-store lane.
         *
         * A filter change also moves which source or item lies under every
         * later y, so the well takes a new input identity -- which the hit key
         * states by construction, because the visible list IS the hit key.
         */
        int const detail = lt_detail_index(rt);
        if( !g_show_ignored && detail >= 0 &&
            lt_source_ignored(rt, &g_source[detail]) )
            g_detail_source_id = 0;
    }
    if( affects_picture )
        lt_revalue(rt);
    /* Config is one of the layer's own inputs; saying so is the whole of the
     * routing that used to be here. */
    Porcelain_Note(g_porcelain, PORCELAIN_INPUT_CONFIG);
}

static struct ToriRS_ConfigSchema const LT_SCHEMA = {
    .struct_size = sizeof(LT_SCHEMA),
    .items = LT_CONFIG,
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER = {
    .struct_size = sizeof(TORIRS_PLUGIN_LOOT_TRACKER),
    .id = "loot-tracker",
    .title = "Loot Tracker",
    .version = "3.0.0",
    .state_size = sizeof(struct LootTrackerState),
    .config = &LT_SCHEMA,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = lt_start,
        .on_stop = lt_stop,
        .on_frame_start = lt_frame_start,
        .on_logic_tick = lt_tick,
        .on_world_loaded = lt_world_loaded,
        .on_npc_despawn = lt_npc_despawn,
        .on_item_spawn = lt_item_spawn,
        .on_game_event = lt_game_event,
        .on_config_changed = lt_config_changed,
        .on_ui_build = lt_panel_build,
        .on_ui_action = lt_panel_action,
        .on_ui_draw = lt_panel_draw,
        .on_ui_layout = lt_panel_layout,
    },
};
