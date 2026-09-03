#include "plugin/plugins/plugin_draw.h"
#include "plugin/torirs_plugin.h"

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
 * Both halves of that are portable here. `ToriRS_PluginNpcSnap` carries
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
/** The spine: 4 wide, inset 2, and 4 shorter than the band. */
#define LT_SPINE_W 4
#define LT_SPINE_X 2
#define LT_SPINE_INSET 2
/** One item cell, and the five-to-a-row grid script3042 lays out. */
#define LT_CELL_W 40
#define LT_CELL_H 36
#define LT_GRID_COLS 5
/** The first cell row sits `33 + 5` below the header's top. */
#define LT_GRID_TOP (LT_HEAD_H + 5)
/** The interfaces' own orange, which is what every label here is set in. */
#define LT_INK_HEAD 0xFF981Fu
#define LT_INK_VALUE 0xFFFFFFu

static struct ToriRS_PluginApi const* g_api;

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
    char name[64];
    int kills;
    struct LtItem items[LT_ITEMS_MAX];
    int item_count;
    /** Frame clock of the last drop, for the page's ordering. */
    uint64_t last_ms;
};

/** A despawn that has not yet been shown to be a kill. */
struct LtPending
{
    char name[64];
    int tile_x;
    int tile_z;
    int level;
    /** Footprint in tiles; loot lands anywhere inside it. */
    int size;
    uint64_t at_ms;
    /** Items collected so far. */
    struct LtItem items[LT_ITEMS_MAX];
    int item_count;
    /**
     * The health bar was at zero when it went: this IS a kill, and it is
     * recorded whether or not anything dropped.
     *
     * False is not "alive" -- it is "the wire never said" -- so it means only
     * that this despawn has to earn its record by producing loot.
     */
    bool confirmed;
};

static struct LtSource g_source[LT_SOURCES_MAX];
static int g_source_count;
static struct LtPending g_pending[LT_PENDING_MAX];
static int g_pending_count;

/** Which source's detail is open, or -1 for the list alone. */
static int g_detail = -1;
static int g_built_detail = -1;
static int g_built_rows;
static bool g_page_built;
/** Whether the page is on screen, as distinct from declared. @see the xp
 *  tracker's g_page_visible -- a collapsed shell keeps the model. */
static bool g_page_visible;

/* ---- the art, and what is drawn with it --------------------------------- */

/** The two faces script2907 and script3043 set their text in. */
static struct PluginDraw_Atlas g_bold;
static struct PluginDraw_Atlas g_text;
/** The header spine and the cell plate, in both their states. */
static int g_img_spine = -1;
static uint32_t* g_spine_px;
static int g_spine_w;
static int g_spine_h;
static int g_img_cell = -1;
static uint32_t* g_cell_px;
static int g_cell_w;
static int g_cell_h;

/** Which sources are EXPANDED. The CS2 header's first op is Collapse/Expand,
 *  so a record is a band with its grid under it or a band on its own. */
static bool g_expanded[LT_SOURCES_MAX];
/** The composed strip, and the size it was composed at. */
static uint32_t* g_compose;
static int g_compose_w;
static int g_compose_h;
static int g_well_w = TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT;
static uint64_t g_next_panel_ms;
/** Session totals, kept beside the records because a source dropped for room
 *  should not make the session look smaller than it was. */
static long long g_session_value;
static int g_session_kills;
static bool g_dirty;

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

/** "12.3K" / "4.5M", the reference's quantityToRSDecimalStack. */
static void
lt_short(long long value, char* out, size_t out_size)
{
    long long unit = 1;
    char suffix = '\0';

    assert(out);
    assert(out_size > 0);

    if( value < 0 )
        value = 0;
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
    else if( value >= 1000LL )
    {
        unit = 1000LL;
        suffix = 'K';
    }
    if( !suffix )
    {
        snprintf(out, out_size, "%lld", value);
        return;
    }
    if( value / unit < 100 )
        snprintf(
            out, out_size, "%lld.%lld%c", value / unit, (value % unit) * 10 / unit, suffix);
    else
        snprintf(out, out_size, "%lld%c", value / unit, suffix);
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
static long long
lt_unit_value(struct ToriRS_PluginCtx* ctx, struct LtItem const* item)
{
    char const* source;

    assert(ctx);
    assert(item);

    source = g_api->cfg_str(ctx, "price_source");
    /* High alchemy is three fifths of the cache cost, which is the game's own
     * formula and not an approximation of one. */
    if( source && lt_name_eq(source, "High alchemy") )
        return (long long)item->cost * 3 / 5;
    return item->cost;
}

/** Everything one source's drops are worth. */
static long long
lt_source_value(struct ToriRS_PluginCtx* ctx, struct LtSource const* src)
{
    long long total = 0;

    assert(ctx);
    assert(src);
    for( int i = 0; i < src->item_count; i++ )
        total += lt_unit_value(ctx, &src->items[i]) * src->items[i].quantity;
    return total;
}

/* ------------------------------------------------------------------------ */
/* The records                                                               */
/* ------------------------------------------------------------------------ */

/** The source called `name`, added if the table has room. -1 when it has not. */
static int
lt_source_find(struct ToriRS_PluginCtx* ctx, char const* name, bool create)
{
    int poorest = -1;

    assert(ctx);
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
        snprintf(g_source[index].name, sizeof(g_source[index].name), "%s", name);
        return index;
    }

    /*
     * Full. Drop the least VALUABLE record rather than the oldest or the
     * newest: the table is a session's history and the thing a player came to
     * it for is the big drop, which is exactly what an oldest-out policy
     * throws away first on a long trip.
     */
    for( int i = 0; i < g_source_count; i++ )
        if( poorest < 0 ||
            lt_source_value(ctx, &g_source[i]) < lt_source_value(ctx, &g_source[poorest]) )
            poorest = i;
    assert(poorest >= 0);
    memset(&g_source[poorest], 0, sizeof(g_source[poorest]));
    snprintf(g_source[poorest].name, sizeof(g_source[poorest].name), "%s", name);
    if( g_detail == poorest )
        g_detail = -1;
    return poorest;
}

/** Add `item` into `src`, merging with a stack of the same obj already there. */
static void
lt_source_add_item(struct LtSource* src, struct LtItem const* item)
{
    assert(src);
    assert(item);

    for( int i = 0; i < src->item_count; i++ )
        if( src->items[i].obj_id == item->obj_id )
        {
            src->items[i].quantity += item->quantity;
            return;
        }
    if( src->item_count >= LT_ITEMS_MAX )
        return;
    src->items[src->item_count++] = *item;
}

/**
 * A candidate's window has closed: make it a record, or forget it.
 *
 * The refusal is the load-bearing half. A despawn with no loot on it is an npc
 * that walked away, and the whole reason this plugin can work without a death
 * signal is that it declines to guess about those.
 */
static void
lt_pending_settle(struct ToriRS_PluginCtx* ctx, int index)
{
    struct LtPending* pending;
    struct LtSource* src;
    int source;
    long long value = 0;

    assert(ctx);
    assert(index >= 0);
    assert(index < g_pending_count);

    pending = &g_pending[index];
    if( pending->confirmed || pending->item_count > 0 )
    {
        source = lt_source_find(ctx, pending->name, true);
        assert(source >= 0);
        src = &g_source[source];
        src->kills++;
        src->last_ms = pending->at_ms;
        g_session_kills++;
        for( int i = 0; i < pending->item_count; i++ )
        {
            lt_source_add_item(src, &pending->items[i]);
            value += lt_unit_value(ctx, &pending->items[i]) * pending->items[i].quantity;
        }
        g_session_value += value;
        g_dirty = true;

        if( g_api->cfg_bool(ctx, "kill_chat_message") &&
            value >= g_api->cfg_int(ctx, "chat_value_threshold") )
        {
            char line[200];
            char amount[32];

            lt_commas(value, amount, sizeof(amount));
            snprintf(
                line, sizeof(line), "%s x%d loot: %s gp", src->name, src->kills, amount);
            g_api->notify(ctx, line);
        }
    }

    g_pending[index] = g_pending[--g_pending_count];
}

/** Drop every candidate whose window has closed. */
static void
lt_pending_expire(struct ToriRS_PluginCtx* ctx, uint64_t now)
{
    assert(ctx);
    for( int i = g_pending_count - 1; i >= 0; i-- )
        if( now >= g_pending[i].at_ms + LT_PENDING_MS )
            lt_pending_settle(ctx, i);
}

/**
 * An npc left the scene. Remember it as a candidate kill.
 *
 * The snapshot may be a HOLLOW one -- the world event exists to clean up the
 * render side after the world side let go, so the pool entry can already be
 * gone and the snapshot then carries an element id and nothing else. Those are
 * skipped rather than recorded under an empty name.
 */
static enum ToriRS_PluginVerdict
lt_npc_despawn(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvNpc const* ev = event;
    struct ToriRS_PluginPlayerSnap me;
    struct LtPending* pending;
    char name[64];

    assert(ctx);
    assert(ev);

    if( ev->npc.npc_id < 0 || !ev->npc.name[0] )
        return TORIRS_PLUGIN_PASS;
    lt_clean_name(ev->npc.name, name, sizeof(name));
    if( !name[0] )
        return TORIRS_PLUGIN_PASS;
    if( lt_listed(g_api->cfg_str(ctx, "ignored_sources"), name) )
        return TORIRS_PLUGIN_PASS;

    /* Yours to have seen. The server only sends ground items near you, so a
     * despawn across the map could never collect anything anyway -- but a
     * candidate that cannot collect is still a slot the crowd around you
     * needs. */
    if( !g_api->local_player(ctx, &me) )
        return TORIRS_PLUGIN_PASS;
    if( me.level != ev->npc.level )
        return TORIRS_PLUGIN_PASS;
    if( abs(me.true_x - ev->npc.true_x) > LT_PENDING_RANGE ||
        abs(me.true_z - ev->npc.true_z) > LT_PENDING_RANGE )
        return TORIRS_PLUGIN_PASS;

    /* Full: settle the oldest early rather than lose this one. A candidate
     * that has already collected becomes its record a fraction of a second
     * sooner, which nothing on the page can tell apart. */
    if( g_pending_count >= LT_PENDING_MAX )
        lt_pending_settle(ctx, 0);

    pending = &g_pending[g_pending_count++];
    memset(pending, 0, sizeof(*pending));
    snprintf(pending->name, sizeof(pending->name), "%s", name);
    pending->tile_x = ev->npc.true_x;
    pending->tile_z = ev->npc.true_z;
    pending->level = ev->npc.level;
    /* true_x/true_z is the SW corner and `size` is the footprint, so a big
     * monster's drop lands anywhere in the square it occupied -- which is the
     * reference's WorldArea, restated in the two numbers this bus carries. */
    pending->size = ev->npc.size > 0 ? ev->npc.size : 1;
    pending->at_ms = g_api->frame_ms(ctx);
    /*
     * The reference's isDying, as far as this client can state it: a bar that
     * exists and has reached empty. `health_ratio < 0` is "no bar was ever
     * sent", which is every npc that has not been in combat and is emphatically
     * not a corpse -- @see ToriRS_PluginNpcSnap::health_ratio.
     */
    pending->confirmed = ev->npc.health_ratio == 0 && ev->npc.health_scale > 0;
    return TORIRS_PLUGIN_PASS;
}

/**
 * A ground-item stack appeared. Hand it to the newest candidate it fits.
 *
 * NEWEST and not nearest: when two kills overlap, the one that just happened
 * is overwhelmingly the one that dropped this, and a distance tiebreak between
 * two npcs standing on each other answers arbitrarily anyway.
 */
static enum ToriRS_PluginVerdict
lt_obj_spawn(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvObj const* ev = event;
    struct LtItem item;
    int best = -1;

    assert(ctx);
    assert(ev);

    for( int i = 0; i < g_pending_count; i++ )
    {
        struct LtPending const* pending = &g_pending[i];

        if( pending->level != ev->obj.level )
            continue;
        if( ev->obj.tile_x < pending->tile_x ||
            ev->obj.tile_x >= pending->tile_x + pending->size )
            continue;
        if( ev->obj.tile_z < pending->tile_z ||
            ev->obj.tile_z >= pending->tile_z + pending->size )
            continue;
        if( best < 0 || pending->at_ms > g_pending[best].at_ms )
            best = i;
    }
    if( best < 0 )
        return TORIRS_PLUGIN_PASS;

    memset(&item, 0, sizeof(item));
    item.obj_id = ev->obj.obj_id;
    item.quantity = ev->obj.count > 0 ? ev->obj.count : 1;
    item.cost = ev->obj.cost;
    lt_clean_name(ev->obj.name, item.name, sizeof(item.name));

    if( lt_listed(g_api->cfg_str(ctx, "ignored_items"), item.name) )
        return TORIRS_PLUGIN_PASS;

    /* Straight onto the CANDIDATE and not onto the record: an UNCONFIRMED
     * despawn earns its record by producing loot, and settling is where the
     * two paths meet. */
    for( int i = 0; i < g_pending[best].item_count; i++ )
        if( g_pending[best].items[i].obj_id == item.obj_id )
        {
            g_pending[best].items[i].quantity += item.quantity;
            return TORIRS_PLUGIN_PASS;
        }
    if( g_pending[best].item_count < LT_ITEMS_MAX )
        g_pending[best].items[g_pending[best].item_count++] = item;
    return TORIRS_PLUGIN_PASS;
}

/**
 * A moment the client recognised.
 *
 * Only the rail flag is taken from these. The drop itself already arrives as a
 * ground-item spawn and counting it twice would double every valuable line;
 * what the chat line adds is that the player should LOOK, which is what the
 * attention marker on the plugin's rail entry says.
 */
static enum ToriRS_PluginVerdict
lt_game_event(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvGameEvent const* ev = event;

    assert(ctx);
    assert(ev);
    assert(ev->kind);

    if( strcmp(ev->kind, "valuable_drop") == 0 || strcmp(ev->kind, "pet") == 0 ||
        strcmp(ev->kind, "collection_log") == 0 )
        g_api->panel_set_attention(ctx, true);
    return TORIRS_PLUGIN_PASS;
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                               */
/* ------------------------------------------------------------------------ */

#define LT_STATE_ASSET "loot.txt"
#define LT_STATE_MAX 16384

/**
 * Write the session out.
 *
 * Line based, `S <kills> <name>` followed by one `I <obj> <qty> <cost> <name>`
 * per drop, because the alternative -- a packed struct -- is a file that
 * silently means something else the day a field is added. The item NAME is
 * stored beside its id for the same reason the item-stats table is keyed by
 * one: an id means a different item on a different revision, and a row that
 * came back reading "Rune scimitar" as something else would be worse than one
 * that came back unpriced.
 */
static void
lt_state_save(struct ToriRS_PluginCtx* ctx)
{
    char buf[LT_STATE_MAX];
    int at = 0;

    assert(ctx);
    /* Cleared either way. With the setting off there is nothing to write and
     * nothing to keep asking about -- a flag left standing would have the tick
     * calling this twice a second for the rest of the session. */
    g_dirty = false;
    if( !g_api->cfg_bool(ctx, "remember_loot") )
        return;

    for( int i = 0; i < g_source_count && at < (int)sizeof(buf); i++ )
    {
        struct LtSource const* src = &g_source[i];
        int written = snprintf(
            buf + at, sizeof(buf) - (size_t)at, "S %d %s\n", src->kills, src->name);

        if( written <= 0 || written >= (int)sizeof(buf) - at )
            break;
        at += written;
        for( int j = 0; j < src->item_count && at < (int)sizeof(buf); j++ )
        {
            written = snprintf(
                buf + at, sizeof(buf) - (size_t)at, "I %d %d %d %s\n",
                src->items[j].obj_id, src->items[j].quantity, src->items[j].cost,
                src->items[j].name);
            if( written <= 0 || written >= (int)sizeof(buf) - at )
                break;
            at += written;
        }
    }
    g_api->asset_save(ctx, LT_STATE_ASSET, buf, at);
}

/** Read the saved session back over an empty table. */
static void
lt_state_apply(struct ToriRS_PluginCtx* ctx)
{
    void const* data;
    int size = 0;
    char const* at;
    char const* end;
    int source = -1;

    assert(ctx);
    if( !g_api->cfg_bool(ctx, "remember_loot") )
        return;

    data = g_api->asset_data(ctx, LT_STATE_ASSET, &size);
    if( !data || size <= 0 )
        return;

    at = (char const*)data;
    end = at + size;
    while( at < end )
    {
        char const* line_end = memchr(at, '\n', (size_t)(end - at));
        char line[192];
        size_t len = line_end ? (size_t)(line_end - at) : (size_t)(end - at);

        if( len >= sizeof(line) )
            len = sizeof(line) - 1;
        memcpy(line, at, len);
        line[len] = '\0';
        at = line_end ? line_end + 1 : end;

        if( line[0] == 'S' )
        {
            int kills = 0;
            char name[64];

            if( sscanf(line, "S %d %63[^\n]", &kills, name) != 2 )
                continue;
            source = lt_source_find(ctx, name, true);
            if( source < 0 )
                continue;
            g_source[source].kills = kills;
            g_session_kills += kills;
        }
        else if( line[0] == 'I' && source >= 0 )
        {
            struct LtItem item;

            memset(&item, 0, sizeof(item));
            if( sscanf(
                    line, "I %d %d %d %63[^\n]", &item.obj_id, &item.quantity,
                    &item.cost, item.name) != 4 )
                continue;
            lt_source_add_item(&g_source[source], &item);
            g_session_value += lt_unit_value(ctx, &item) * item.quantity;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* The page                                                                  */
/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */
/* The strip                                                                 */
/* ------------------------------------------------------------------------ */

/** Everything the compose needs, resident. */
static int
lt_art_ready(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    if( !PluginDraw_AtlasLoad(ctx, g_api, &g_bold, "bold") )
        return 0;
    if( !PluginDraw_AtlasLoad(ctx, g_api, &g_text, "text") )
        return 0;
    if( !PluginDraw_ImageLoad(
            ctx, g_api, "cat_spine.png", &g_img_spine, &g_spine_px, &g_spine_w,
            &g_spine_h) )
        return 0;
    return PluginDraw_ImageLoad(
        ctx, g_api, "cell.png", &g_img_cell, &g_cell_px, &g_cell_w, &g_cell_h);
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
lt_source_h(int index)
{
    assert(index >= 0);
    assert(index < g_source_count);
    if( !g_expanded[index] || g_source[index].item_count == 0 )
        return LT_HEAD_H + LT_HEAD_GAP;
    return LT_GRID_TOP + lt_grid_rows(&g_source[index]) * LT_CELL_H + LT_HEAD_GAP;
}

/** The whole strip's height. */
static int
lt_strip_h(void)
{
    int total = 0;

    for( int i = 0; i < g_source_count; i++ )
        total += lt_source_h(i);
    /* The empty note still needs a line to sit on. */
    return total > 0 ? total : LT_HEAD_H;
}

/** The source a click at `y` landed on, and how far into it. -1 for neither. */
static int
lt_source_at(int y, int* out_local_y)
{
    int top = 0;

    for( int i = 0; i < g_source_count; i++ )
    {
        int const h = lt_source_h(i);
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
static void
lt_draw_source(
    struct ToriRS_PluginCtx* ctx, uint32_t* buf, int w, int h, int top, int index)
{
    struct LtSource const* src = &g_source[index];
    char text[96];
    char amount[32];
    int cell_x0;

    assert(ctx);
    assert(buf);

    /* The spine, tiled down the band's height as cc_settiling does. */
    if( g_spine_px )
        PluginDraw_Tile(
            buf, w, h, LT_SPINE_X, top + LT_SPINE_INSET, LT_SPINE_W,
            LT_HEAD_H - LT_SPINE_INSET * 2, g_spine_px, g_spine_w, g_spine_h, 0);

    /* The name on the left and the kill count on the right, both in the bold
     * face at the interfaces' orange. */
    snprintf(text, sizeof(text), "%s", src->name);
    PluginDraw_Text(buf, w, h, 6, top + 4, &g_bold, text, LT_INK_HEAD);

    lt_short(lt_source_value(ctx, src), amount, sizeof(amount));
    snprintf(text, sizeof(text), "x%d  %s gp", src->kills, amount);
    PluginDraw_TextRight(buf, w, h, w - 6, top + 4, &g_bold, text, LT_INK_HEAD);

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

        if( g_cell_px )
            PluginDraw_Blit(
                buf, w, h, x, y, g_cell_px, g_cell_w, g_cell_h, 0, 0, g_cell_w,
                g_cell_h, 0);

        /*
         * The obj at +2,+2 in the BORDERED variant, which is what
         * cc_setoutline(1) bakes. The quantity is part of the picture -- the
         * client stamps the stack digits into the sprite -- so it is asked for
         * at the quantity and drawn as one thing.
         */
        image = g_api->obj_image(
            ctx, src->items[i].obj_id, src->items[i].quantity,
            TORIRS_PLUGIN_OBJ_ICON_BORDERED);
        if( image < 0 || !g_api->image_size(ctx, image, &iw, &ih) )
            continue;
        {
            uint32_t* px = malloc((size_t)iw * (size_t)ih * sizeof(*px));
            assert(px);
            if( g_api->image_pixels(ctx, image, px, iw * ih) )
                PluginDraw_Blit(
                    buf, w, h, x + 2, y + 2, px, iw, ih, 0, 0, iw, ih, 0);
            free(px);
        }
    }
}

/** Rasterise every band and publish the strip. */
static void
lt_compose(struct ToriRS_PluginCtx* ctx, int width)
{
    int const height = lt_strip_h();
    size_t const pixels = (size_t)width * (size_t)height;
    int top = 0;

    assert(ctx);
    if( width <= 0 || height <= 0 )
        return;

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

    if( g_source_count == 0 )
        PluginDraw_Text(
            g_compose, width, height, 4, 3, &g_text, "No loot to display.",
            LT_INK_HEAD);

    for( int i = 0; i < g_source_count; i++ )
    {
        lt_draw_source(ctx, g_compose, width, height, top, i);
        top += lt_source_h(i);
    }

    g_api->image_compose(ctx, "strip", width, height, g_compose);
}

/** Rewrite every readout on the built page. */
static void
lt_page_refresh(struct ToriRS_PluginCtx* ctx)
{
    char text[128];

    assert(ctx);
    if( !g_page_built )
        return;

    lt_commas(g_session_kills, text, sizeof(text));
    g_api->panel_set_text(ctx, "total_kills", text);
    lt_commas(g_session_value, text, sizeof(text));
    g_api->panel_set_text(ctx, "total_value", text);

    /* The bands are pixels and redraw themselves. */
    g_api->panel_invalidate(ctx, "strip");

    if( g_detail >= 0 && g_detail < g_source_count )
    {
        struct LtSource const* src = &g_source[g_detail];

        lt_commas(src->kills, text, sizeof(text));
        g_api->panel_set_text(ctx, "d_kills", text);
        lt_commas(lt_source_value(ctx, src), text, sizeof(text));
        g_api->panel_set_text(ctx, "d_value", text);
        if( src->kills > 0 )
        {
            lt_commas(lt_source_value(ctx, src) / src->kills, text, sizeof(text));
            g_api->panel_set_text(ctx, "d_per_kill", text);
        }
    }
}

static enum ToriRS_PluginVerdict
lt_panel_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelBuild const* ev = event;

    assert(ctx);
    assert(ev);

    /* The SETTINGS face is the generated form and nothing else -- every knob
     * here is a config key. @see enum ToriRS_PluginPanelView. */
    if( ev->view != TORIRS_PLUGIN_PANEL_VIEW_PAGE )
    {
        g_page_built = false;
        return TORIRS_PLUGIN_PASS;
    }

    g_built_rows = lt_strip_h();

    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_SECTION, "sec_session", "Session");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_kills", "Kills");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_value", "Total value");

    /*
     * ONE drawing well for every band, which is what the CS2 tracker is: a
     * header per source with its drops under it. Built out of panel controls
     * it would be a header plus a control per item and would run out of the
     * 48-control budget inside one boss trip.
     */
    if( g_api->panel_widget(ctx, TORIRS_PLUGIN_W_CUSTOM, "strip", "Loot") )
        g_api->panel_set_height(ctx, "strip", lt_strip_h());

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
        g_api->panel_widget(
            ctx, TORIRS_PLUGIN_W_SECTION, "sec_detail", g_source[g_detail].name);
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_kills", "Kills");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_value", "Value");
        g_api->panel_widget(
            ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_per_kill", "Value per kill");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_clear", "Clear data");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_ignore", "Ignore");
    }
    else
        g_built_detail = -1;

    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "reset_all", "Clear all loot");

    g_page_built = true;
    lt_page_refresh(ctx);
    return TORIRS_PLUGIN_PASS;
}

/**
 * Draw the selected source's drops.
 *
 * Every icon is asked for again, on every pass, and that is the contract
 * rather than an oversight: `obj_image` hands back a handle out of a
 * host-owned evicting cache, and a plugin that remembered one across frames
 * would eventually draw nothing. @see ToriRS_PluginApi::obj_image.
 */
static enum ToriRS_PluginVerdict
lt_panel_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelDraw const* ev = event;
    int image;

    assert(ctx);
    assert(ev);
    assert(ev->id);

    if( strcmp(ev->id, "strip") != 0 || ev->width <= 0 )
        return TORIRS_PLUGIN_PASS;
    /* The art crosses the IO queue, so the first passes after a start have
     * nothing to draw with. The next invalidate fills it -- the same state the
     * client's own inventory icons are in for a frame or two. */
    if( !lt_art_ready(ctx) )
        return TORIRS_PLUGIN_PASS;

    g_well_w = ev->width;
    lt_compose(ctx, ev->width);
    image = g_api->image_load(ctx, "strip");
    if( image >= 0 )
        g_api->draw_image(ctx, ev->surface, image, ev->x, ev->y, 0, 0, 0, 0, 0);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
lt_panel_action(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelAction const* ev = event;

    assert(ctx);
    assert(ev);
    assert(ev->id);

    g_api->panel_set_attention(ctx, false);

    if( strcmp(ev->id, "reset_all") == 0 )
    {
        g_source_count = 0;
        g_pending_count = 0;
        g_session_value = 0;
        g_session_kills = 0;
        g_detail = -1;
        g_dirty = true;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    if( strcmp(ev->id, "d_close") == 0 )
    {
        g_detail = -1;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    if( strcmp(ev->id, "d_clear") == 0 && g_detail >= 0 && g_detail < g_source_count )
    {
        g_session_kills -= g_source[g_detail].kills;
        g_session_value -= lt_source_value(ctx, &g_source[g_detail]);
        g_source[g_detail] = g_source[--g_source_count];
        g_detail = -1;
        g_dirty = true;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    if( strcmp(ev->id, "d_ignore") == 0 && g_detail >= 0 && g_detail < g_source_count )
    {
        /* The header's third op. The list is the config key a person can also
         * type into, so both ways of saying it end up in one place. */
        char list[LT_CONFIG_VALUE_MAX];
        char const* existing = g_api->cfg_str(ctx, "ignored_sources");

        snprintf(
            list, sizeof(list), "%s%s%s", existing && existing[0] ? existing : "",
            existing && existing[0] ? "," : "", g_source[g_detail].name);
        g_api->cfg_set(ctx, "ignored_sources", list);
        g_session_kills -= g_source[g_detail].kills;
        g_session_value -= lt_source_value(ctx, &g_source[g_detail]);
        g_source[g_detail] = g_source[--g_source_count];
        g_detail = -1;
        g_dirty = true;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
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
        int const source = lt_source_at(ev->y, &local_y);

        if( source < 0 )
            return TORIRS_PLUGIN_PASS;
        if( local_y < LT_HEAD_H )
            g_expanded[source] = !g_expanded[source];
        g_detail = g_detail == source && local_y >= LT_HEAD_H ? -1 : source;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }
    return TORIRS_PLUGIN_PASS;
}

/** The shell moved, showed or hid this page. */
static enum ToriRS_PluginVerdict
lt_panel_layout(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelLayout const* ev = event;

    assert(ctx);
    assert(ev);

    g_page_visible = ev->visible;
    if( ev->width > 0 )
        g_well_w = ev->width;
    if( g_page_visible )
    {
        lt_page_refresh(ctx);
        g_api->panel_invalidate(ctx, "strip");
    }
    return TORIRS_PLUGIN_PASS;
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

static enum ToriRS_PluginVerdict
lt_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    struct ToriRS_PluginPanelDesc desc;

    assert(ctx);

    g_source_count = 0;
    g_pending_count = 0;
    g_session_value = 0;
    g_session_kills = 0;
    g_detail = -1;
    g_built_detail = -1;
    g_built_rows = 0;
    g_page_built = false;
    g_page_visible = false;
    g_next_panel_ms = 0;
    g_dirty = false;

    memset(&desc, 0, sizeof(desc));
    desc.title = "Loot Tracker";
    /* RuneLite's own, so a person who has used the plugin there recognises
     * the row here. @see script/plugins/assets/loot-tracker/panel_icon.txt. */
    desc.icon_asset = "panel_icon.png";
    desc.preferred_width = TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT;
    g_api->panel_request(ctx, &desc);

    if( g_api->cfg_bool(ctx, "remember_loot") &&
        g_api->asset_load(ctx, LT_STATE_ASSET) == 1 )
        lt_state_apply(ctx);

    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
lt_asset(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvAsset const* ev = event;

    assert(ctx);
    assert(ev);

    if( ev->ok && ev->name && strcmp(ev->name, LT_STATE_ASSET) == 0 )
    {
        lt_state_apply(ctx);
        g_api->panel_clear(ctx);
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
lt_stop(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    lt_state_save(ctx);
    g_page_built = false;
    g_page_visible = false;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
lt_tick(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    uint64_t const now = g_api->frame_ms(ctx);
    char badge[TORIRS_PLUGIN_PANEL_BADGE_MAX];

    assert(ctx);

    lt_pending_expire(ctx, now);

    if( now < g_next_panel_ms )
        return TORIRS_PLUGIN_PASS;
    g_next_panel_ms = now + LT_PANEL_REFRESH_MS;

    /* The rail entry carries the session's value, so the number a player opens
     * this for is legible without opening it. */
    lt_short(g_session_value, badge, sizeof(badge));
    g_api->panel_set_badge(ctx, g_session_value > 0 ? badge : "");

    /* A record added or removed changes the row SET, which only a rebuild can
     * state; anything else is a number a refresh rewrites in place. Neither is
     * worth doing for a page nobody is looking at -- the rebuild happens when
     * it is selected again. */
    if( g_page_visible )
    {
        if( g_page_built &&
            (g_built_rows != lt_strip_h() || g_built_detail != g_detail) )
            g_api->panel_clear(ctx);
        else
        {
            lt_page_refresh(ctx);
            if( g_detail >= 0 )
                g_api->panel_invalidate(ctx, "d_items");
        }
    }

    if( g_dirty )
        lt_state_save(ctx);
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginConfigItem const LT_CONFIG[] = {
    { "remember_loot",     TORIRS_PLUGIN_CFG_BOOL, "Remember loot between sessions", "1", 0, 0, NULL, 0 },
    { "price_source",      TORIRS_PLUGIN_CFG_ENUM, "Value items by",                 "Cache value", 0, 0, "Cache value|High alchemy", 0 },
    { "kill_chat_message", TORIRS_PLUGIN_CFG_BOOL, "Announce loot in chat",          "0", 0, 0, NULL, 0 },
    { "chat_value_threshold", TORIRS_PLUGIN_CFG_INT, "Announce only above (gp)",     "0", 0, 100000000, NULL, 0 },
    { "ignored_items",     TORIRS_PLUGIN_CFG_TEXT, "Ignored items",                  "", 0, 0, NULL, 4 },
    { "ignored_sources",   TORIRS_PLUGIN_CFG_TEXT, "Ignored sources",                "", 0, 0, NULL, 4 },
    { NULL,                TORIRS_PLUGIN_CFG_BOOL, NULL,                             NULL, 0, 0, NULL, 0 },
};

static void
lt_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, lt_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, lt_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_ASSET, lt_asset, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_NPC_DESPAWN, lt_npc_despawn, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_OBJ_SPAWN, lt_obj_spawn, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_GAME_EVENT, lt_game_event, NULL);
    /* EV_LOGIC_TICK and not EV_SERVER_TICK: the tick fence is only on the wire
     * for osrs230, osrs239 and the rsprot bridge, and a candidate that never
     * expired would never become a record on any other lane. */
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, lt_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_BUILD, lt_panel_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_ACTION, lt_panel_action, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_LAYOUT, lt_panel_layout, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_DRAW, lt_panel_draw, NULL);
}

/** Give the composed strip and the decoded art back. */
static void
lt_shutdown(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;

    free(g_compose);
    g_compose = NULL;
    g_compose_w = 0;
    g_compose_h = 0;
    PluginDraw_AtlasFree(&g_bold);
    PluginDraw_AtlasFree(&g_text);
    PluginDraw_ImageFree(&g_spine_px, &g_img_spine);
    PluginDraw_ImageFree(&g_cell_px, &g_img_cell);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER = {
    .name = "loot-tracker",
    .title = "Loot Tracker",
    .version = "1.0.0",
    .priority = 0,
    .config = LT_CONFIG,
    .init = lt_init,
    .shutdown = lt_shutdown,
};
