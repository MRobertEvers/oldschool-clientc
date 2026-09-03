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
 * ---- how a drop is attributed, and why it is not the reference's way ----
 *
 * RuneLite's LootManager watches for an npc that is DYING -- a death animation
 * out of a table, or `isDying()` on a health ratio of zero -- and collects
 * every item that spawns inside that npc's tiles on the same tick.
 *
 * This client's plugin bus has no dying signal at all: EV_NPC_DESPAWN is
 * raised from the world's EntityRemoved event and the snapshot it carries says
 * where the npc was and what it was called, not how it left. So the port keeps
 * the SECOND half of the reference's mechanism, which is the half that does
 * not need one -- and it is not a fallback invented here, it is the path
 * RuneLite itself uses for the npcs that despawn with hitpoints left (the
 * gargoyle family): a despawn is remembered as a CANDIDATE, and it becomes a
 * kill only if loot lands on its footprint within the window.
 *
 * What that trades away is stated plainly rather than papered over:
 *
 *   - A kill that drops NOTHING is not counted. There is no signal that
 *     separates it from an npc walking out of view, and a kill count that
 *     ticked up every time something wandered off would be worse than one
 *     that undercounts a dry kill.
 *   - A drop you cannot SEE is not counted either, but that is the server's
 *     doing and not this plugin's: ground items are only sent for tiles near
 *     you, and an item somebody else's kill dropped across the room never
 *     reaches the client at all.
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

/** The item well's grid, in logical units. One icon is 36x32. */
#define LT_CELL_W 40
#define LT_CELL_H 36
#define LT_GRID_COLS 6

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
    /** Items collected so far. A candidate with none is not a kill. */
    struct LtItem items[LT_ITEMS_MAX];
    int item_count;
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
    if( pending->item_count > 0 )
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

    /* Straight onto the CANDIDATE and not onto the record: a candidate with
     * nothing on it is not a kill, and that test is what stands in for the
     * death signal this bus does not raise. */
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

static void
lt_row_id(int index, char* out, size_t out_size)
{
    assert(out);
    snprintf(out, out_size, "src%d", index);
}

static int
lt_row_index(char const* id)
{
    int index;

    assert(id);
    if( sscanf(id, "src%d", &index) != 1 )
        return -1;
    if( index < 0 || index >= g_source_count )
        return -1;
    return index;
}

/** Rewrite every readout on the built page. */
static void
lt_page_refresh(struct ToriRS_PluginCtx* ctx)
{
    char text[128];
    char amount[32];

    assert(ctx);
    if( !g_page_built )
        return;

    lt_commas(g_session_kills, text, sizeof(text));
    g_api->panel_set_text(ctx, "total_kills", text);
    lt_commas(g_session_value, text, sizeof(text));
    g_api->panel_set_text(ctx, "total_value", text);

    for( int i = 0; i < g_source_count && i < g_built_rows; i++ )
    {
        char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

        lt_row_id(i, id, sizeof(id));
        lt_short(lt_source_value(ctx, &g_source[i]), amount, sizeof(amount));
        snprintf(text, sizeof(text), "x%d  ·  %s gp", g_source[i].kills, amount);
        g_api->panel_set_text(ctx, id, text);
        /*
         * The switch reads as TRACKED, so turning it off is what stops
         * tracking -- the direction every other switch on the page runs in. A
         * row left unchecked would mean the opposite in the same column, and
         * the only way to find out which was to press it.
         */
        g_api->panel_set_value(ctx, id, 1);
    }

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

    g_built_rows = 0;

    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_SECTION, "sec_session", "Session");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_kills", "Kills");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "total_value", "Total value");
    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "reset_all", "Clear all loot");

    g_api->panel_widget(ctx, TORIRS_PLUGIN_W_SECTION, "sec_sources", "Sources");
    for( int i = 0; i < g_source_count && i < LT_ROWS_MAX; i++ )
    {
        char id[TORIRS_PLUGIN_WIDGET_ID_MAX];

        lt_row_id(i, id, sizeof(id));
        if( !g_api->panel_widget(ctx, TORIRS_PLUGIN_W_LIST_ROW, id, g_source[i].name) )
        {
            g_api->log(
                ctx, "loot-tracker: no room on the page for '%s' and what follows",
                g_source[i].name);
            break;
        }
        g_built_rows++;
    }
    if( g_built_rows == 0 )
        g_api->panel_widget(
            ctx, TORIRS_PLUGIN_W_PARAGRAPH, "empty", "No loot recorded yet.");

    g_built_detail = g_detail;
    if( g_detail >= 0 && g_detail < g_built_rows )
    {
        g_api->panel_widget(
            ctx, TORIRS_PLUGIN_W_SECTION, "sec_detail", g_source[g_detail].name);
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_kills", "Kills");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_value", "Value");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_KEY_VALUE, "d_per_kill", "Value per kill");
        /*
         * The drops as PICTURES, in the one bounded drawing well the page
         * gives a plugin. A row of names would be the same information and a
         * worse readout: an item is recognised by its icon at a glance, the
         * quantity is already baked into the client's own sprite, and thirty
         * drops as thirty text rows would not fit on the page at all.
         */
        if( g_api->panel_widget(ctx, TORIRS_PLUGIN_W_CUSTOM, "d_items", "Drops") )
        {
            int const rows =
                (g_source[g_detail].item_count + LT_GRID_COLS - 1) / LT_GRID_COLS;
            g_api->panel_set_height(ctx, "d_items", (rows > 0 ? rows : 1) * LT_CELL_H + 8);
        }
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_clear", "Clear this record");
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "d_close", "Close details");
    }
    else
        g_built_detail = -1;

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
    struct LtSource const* src;

    assert(ctx);
    assert(ev);
    assert(ev->id);

    if( strcmp(ev->id, "d_items") != 0 )
        return TORIRS_PLUGIN_PASS;
    if( g_detail < 0 || g_detail >= g_source_count )
        return TORIRS_PLUGIN_PASS;
    src = &g_source[g_detail];

    for( int i = 0; i < src->item_count; i++ )
    {
        int const col = i % LT_GRID_COLS;
        int const row = i / LT_GRID_COLS;
        int image;
        int w = 0;
        int h = 0;

        image = g_api->obj_image(
            ctx, src->items[i].obj_id, src->items[i].quantity,
            TORIRS_PLUGIN_OBJ_ICON_BORDERED);
        /* Not resident yet. The cell stays empty this pass and the next
         * invalidate fills it -- the same state the client's own inventory is
         * in for the first frames after a login. */
        if( image < 0 || !g_api->image_size(ctx, image, &w, &h) )
            continue;
        /* Natural size: draw_image takes a position and a clip and no scale,
         * which is right here -- an icon rasterised at 36x32 and stretched to
         * a cell would be a blurred copy of a picture the client already has
         * at exactly the size it is drawn at everywhere else. */
        g_api->draw_image(
            ctx,
            ev->surface,
            image,
            ev->x + col * LT_CELL_W + 2,
            ev->y + row * LT_CELL_H + 2,
            0,
            0,
            0,
            0,
            0);
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
lt_panel_layout(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelLayout const* ev = event;

    assert(ctx);
    assert(ev);

    g_page_visible = ev->visible;
    if( g_page_visible )
    {
        lt_page_refresh(ctx);
        if( g_detail >= 0 )
            g_api->panel_invalidate(ctx, "d_items");
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
lt_panel_action(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvPanelAction const* ev = event;
    int index;

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

    index = lt_row_index(ev->id);
    if( index < 0 )
        return TORIRS_PLUGIN_PASS;

    /*
     * The row opens the record; its switch adds the source to the ignore list,
     * which is the reference's `ignoredEvents` reached without a second
     * control. The list is the config key the user can also type into, so both
     * ways of saying it end up in one place.
     */
    if( ev->action == TORIRS_PLUGIN_UI_TOGGLE )
    {
        char list[LT_CONFIG_VALUE_MAX];

        /* Only turning it OFF means anything: the row is drawn checked, so a
         * value of 1 is the switch coming back to where it already was. */
        if( ev->value != 0 )
            return TORIRS_PLUGIN_PASS;
        char const* existing = g_api->cfg_str(ctx, "ignored_sources");

        snprintf(
            list, sizeof(list), "%s%s%s", existing && existing[0] ? existing : "",
            existing && existing[0] ? "," : "", g_source[index].name);
        g_api->cfg_set(ctx, "ignored_sources", list);
        g_session_kills -= g_source[index].kills;
        g_session_value -= lt_source_value(ctx, &g_source[index]);
        g_source[index] = g_source[--g_source_count];
        g_detail = -1;
        g_dirty = true;
        g_api->panel_clear(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    g_detail = g_detail == index ? -1 : index;
    g_api->panel_clear(ctx);
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
        if( g_page_built && (g_built_rows != (g_source_count < LT_ROWS_MAX
                                                  ? g_source_count
                                                  : LT_ROWS_MAX) ||
                             g_built_detail != g_detail) )
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

struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER = {
    .name = "loot-tracker",
    .title = "Loot Tracker",
    .version = "1.0.0",
    .priority = 0,
    .config = LT_CONFIG,
    .init = lt_init,
    .shutdown = NULL,
};
