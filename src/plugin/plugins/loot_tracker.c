#include "plugin/torirs_plugin_v2.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Loot Tracker -- a native retained view of the client's loot store.
 *
 * What it keeps: one record per SOURCE (a monster's name), carrying how many
 * of them you have killed and the running total of every item they dropped,
 * with a value on each. The panel lists sources as native action rows and
 * drills into one source's native summary and item rows.
 *
 * ---- where the records come from ----
 *
 * Gameplay code writes the authoritative session record used by the cache's
 * own tracker. This plugin reads it through `loot_source_next` and
 * `loot_row_next`; it does not independently infer kills from npc despawns or
 * ground-item timing. That preserves zero-drop kills and the store's source
 * attribution.
 *
 * ---- what an item is WORTH ----
 *
 * There is no live Grand Exchange quote anywhere in this client, so the
 * reference's GE price is not portable. The store records the cache value
 * derived from `ObjType.cost`, the number CS2 reads through OC_COST. Both
 * configured price sources are computed from it: the cache value itself, and
 * high alchemy -- three fifths of it, the game's own formula -- which is the
 * one figure a player can actually realise for most drops.
 *
 * ---- not ported ----
 *
 * Event loot (barrows, raid chests, clue caskets) and PVP loot appear only when
 * a producer records them in the shared store. This view deliberately adds no
 * second attribution path.
 */

/** Maximum source records mirrored into the bounded panel state. The shared
 *  store remains authoritative for any records beyond this view. */
#define LT_SOURCES_MAX 48
/** Maximum distinct item records mirrored for one source. */
#define LT_ITEMS_MAX 32
/** Source rows the page will declare before it stops and says so. */
#define LT_ROWS_MAX 24
/** How often the page's numbers are rewritten, in ms. */
#define LT_PANEL_REFRESH_MS 500

/**
 * Bytes of one config value, stated here rather than included.
 *
 * The host's ceiling is TORIRS_PLUGIN_CONFIG_VALUE_MAX, but that lives in the
 * host header and a plugin has no business including one -- the whole contract
 * is torirs_plugin_v2.h. A shorter buffer here is not a disagreement, only a
 * shorter ignore list than the store would have held.
 */
#define LT_CONFIG_VALUE_MAX 192

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
};

struct LootTrackerState
{
    struct LtSource source[LT_SOURCES_MAX];
    int source_count;
    int detail;
    int built_detail;
    int built_rows;
    int built_source_id[LT_ROWS_MAX];
    char built_source_label[LT_ROWS_MAX][64];
    int built_detail_source_id;
    char built_detail_source_label[64];
    int built_item_obj_id[LT_ITEMS_MAX];
    char built_item_label[LT_ITEMS_MAX][64];
    bool page_built;
    bool page_visible;

    uint64_t next_panel_ms;
    long long session_value;
    int session_kills;
    bool dirty;
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
#define g_built_source_id (rt->state->built_source_id)
#define g_built_source_label (rt->state->built_source_label)
#define g_built_detail_source_id (rt->state->built_detail_source_id)
#define g_built_detail_source_label (rt->state->built_detail_source_label)
#define g_built_item_obj_id (rt->state->built_item_obj_id)
#define g_built_item_label (rt->state->built_item_label)
#define g_page_built (rt->state->page_built)
#define g_page_visible (rt->state->page_visible)
#define g_next_panel_ms (rt->state->next_panel_ms)
#define g_session_value (rt->state->session_value)
#define g_session_kills (rt->state->session_kills)
#define g_dirty (rt->state->dirty)
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
 * Tracking already lives in the shared loot store. These notifications only
 * say that the player should look, which is what the attention marker on the
 * plugin's rail entry represents.
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
/* Store mirror                                                             */
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
    bool had_detail = false;
    int selected_source_id = 0;

    assert(rt);

    if( g_detail >= 0 && g_detail < g_source_count )
    {
        had_detail = true;
        selected_source_id = g_page_built && g_built_detail >= 0
                                 ? g_built_detail_source_id
                                 : g_source[g_detail].id;
    }

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
    g_detail = had_detail ? lt_source_index_by_id(rt, selected_source_id) : -1;
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

/** Summary text for one native source row. */
static void
lt_source_summary(
    struct LootTrackerRuntime* rt,
    struct LtSource const* source,
    char* out,
    size_t out_size)
{
    char kills[32];
    char value[32];

    assert(rt);
    assert(source);
    lt_commas(source->kills, kills, sizeof(kills));
    lt_kmb(lt_source_value(rt, source), value, sizeof(value));
    snprintf(
        out,
        out_size,
        "%s %s \xc2\xb7 %s gp \xc2\xb7 %d item%s",
        kills,
        source->kills == 1 ? "kill" : "kills",
        value,
        source->item_count,
        source->item_count == 1 ? "" : "s");
}

/** Value text for one native item row. */
static void
lt_item_summary(
    struct LootTrackerRuntime* rt,
    struct LtItem const* item,
    char* out,
    size_t out_size)
{
    char quantity[32];
    char value[32];

    assert(rt);
    assert(item);
    lt_commas(item->quantity, quantity, sizeof(quantity));
    lt_commas(
        lt_unit_value(rt, item) * item->quantity, value, sizeof(value));
    snprintf(out, out_size, "%s \xc2\xb7 %s gp", quantity, value);
}

static char const*
lt_item_label(struct LtItem const* item)
{
    assert(item);
    return item->name[0] ? item->name : "Unknown item";
}

/** Patch live text on the already-retained native DOM rows. */
static void
lt_page_refresh(struct LootTrackerRuntime* rt)
{
    char text[192];

    assert(rt);
    if( !g_page_built )
        return;

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
        for( int i = 0; i < src->item_count && i < LT_ITEMS_MAX; i++ )
        {
            char id[32];

            snprintf(id, sizeof(id), "item_%d", i);
            lt_item_summary(rt, &src->items[i], text, sizeof(text));
            (void)g_api->panel.set_text(g_api, id, text);
        }
        return;
    }

    lt_commas(g_session_kills, text, sizeof(text));
    (void)g_api->panel.set_text(g_api, "total_count", text);
    lt_commas(g_session_value, text, sizeof(text));
    (void)g_api->panel.set_text(g_api, "total_value", text);
    for( int i = 0; i < g_source_count && i < LT_ROWS_MAX; i++ )
    {
        char id[32];

        snprintf(id, sizeof(id), "source_%d", i);
        lt_source_summary(rt, &g_source[i], text, sizeof(text));
        (void)g_api->panel.set_text(g_api, id, text);
    }
}

/** Whether retained row identity changed. Value-only changes stay on the
 * setter path; count, order, stable identity, or primary-label changes rebuild
 * the page before an immutable DOM row can describe a different record. */
static bool
lt_page_stale(struct LootTrackerRuntime* rt)
{
    int rows;

    assert(rt);
    if( !g_page_built )
        return false;
    if( g_built_detail != g_detail )
        return true;
    if( g_detail >= 0 && g_detail < g_source_count )
    {
        struct LtSource const* source = &g_source[g_detail];

        if( g_built_detail_source_id != source->id ||
            strcmp(g_built_detail_source_label, source->name) != 0 )
            return true;
        rows = source->item_count < LT_ITEMS_MAX ? source->item_count : LT_ITEMS_MAX;
        if( g_built_rows != rows )
            return true;
        for( int i = 0; i < rows; i++ )
            if( g_built_item_obj_id[i] != source->items[i].obj_id ||
                strcmp(g_built_item_label[i], lt_item_label(&source->items[i])) != 0 )
                return true;
        return false;
    }

    rows = g_source_count < LT_ROWS_MAX ? g_source_count : LT_ROWS_MAX;
    if( g_built_rows != rows )
        return true;
    for( int i = 0; i < rows; i++ )
        if( g_built_source_id[i] != g_source[i].id ||
            strcmp(g_built_source_label[i], g_source[i].name) != 0 )
            return true;
    return false;
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
    g_built_detail = g_detail;
    if( g_detail >= 0 && g_detail < g_source_count )
    {
        struct LtSource const* source = &g_source[g_detail];
        struct ToriRS_PanelNode heading = {
            .struct_size = sizeof(heading),
            .kind = TORIRS_PANEL_HEADING,
            .id = "sec_detail",
            .text = source->name,
        };
        char text[192];
        int const rows = source->item_count < LT_ITEMS_MAX
                             ? source->item_count
                             : LT_ITEMS_MAX;

        g_built_detail_source_id = source->id;
        snprintf(
            g_built_detail_source_label,
            sizeof(g_built_detail_source_label),
            "%s",
            source->name);

        panel->button(panel, "d_back", "Back to sources", true);
        (void)panel->node(panel, &heading);
        lt_commas(source->kills, text, sizeof(text));
        panel->key_value(panel, "d_kills", "Kills", text);
        lt_commas(lt_source_value(rt, source), text, sizeof(text));
        panel->key_value(panel, "d_value", "Value", text);
        if( source->kills > 0 )
            lt_commas(
                lt_source_value(rt, source) / source->kills, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "0");
        panel->key_value(panel, "d_per_kill", "Value per kill", text);
        panel->heading(panel, "Items");
        for( int i = 0; i < rows; i++ )
        {
            char id[32];
            char const* name = lt_item_label(&source->items[i]);

            snprintf(id, sizeof(id), "item_%d", i);
            g_built_item_obj_id[i] = source->items[i].obj_id;
            snprintf(
                g_built_item_label[i], sizeof(g_built_item_label[i]), "%s", name);
            lt_item_summary(rt, &source->items[i], text, sizeof(text));
            panel->key_value(panel, id, name, text);
        }
        panel->button(panel, "d_clear", "Clear data", true);
        panel->button(panel, "d_ignore", "Ignore", true);
        g_built_rows = rows;
    }
    else
    {
        char text[192];

        g_built_detail = -1;
        g_built_detail_source_id = 0;
        g_built_detail_source_label[0] = '\0';
        g_built_rows = g_source_count < LT_ROWS_MAX ? g_source_count : LT_ROWS_MAX;
        panel->heading(panel, "Session");
        lt_commas(g_session_kills, text, sizeof(text));
        panel->key_value(panel, "total_count", "Total kills", text);
        lt_commas(g_session_value, text, sizeof(text));
        panel->key_value(panel, "total_value", "Total value", text);
        if( g_source_count == 0 )
            panel->paragraph(panel, "No loot recorded this session.");
        for( int i = 0; i < g_built_rows; i++ )
        {
            char id[32];

            snprintf(id, sizeof(id), "source_%d", i);
            g_built_source_id[i] = g_source[i].id;
            snprintf(
                g_built_source_label[i], sizeof(g_built_source_label[i]), "%s",
                g_source[i].name);
            lt_source_summary(rt, &g_source[i], text, sizeof(text));
            panel->action_row(panel, id, g_source[i].name, text);
        }
        if( g_source_count > g_built_rows )
            panel->paragraph(panel, "More sources are recorded; refine ignored sources in Settings.");
    }

    g_page_built = true;
    lt_page_refresh(rt);
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

    if( strcmp(ev->id, "d_back") == 0 )
    {
        g_detail = -1;
        g_api->panel.invalidate(g_api);
        return;
    }
    if( strncmp(ev->id, "source_", 7) == 0 )
    {
        char* end = NULL;
        long const row = strtol(ev->id + 7, &end, 10);

        if( end && !*end && g_page_built && g_built_detail < 0 && row >= 0 &&
            row < g_built_rows )
        {
            int const source =
                lt_source_index_by_id(rt, g_built_source_id[(int)row]);
            if( source >= 0 )
                g_detail = source;
            g_api->panel.invalidate(g_api);
        }
        return;
    }
    if( strcmp(ev->id, "d_clear") == 0 && g_page_built && g_built_detail >= 0 )
    {
        int const source_id = g_built_detail_source_id;
        int const source = lt_source_index_by_id(rt, source_id);

        if( source < 0 )
        {
            g_api->panel.invalidate(g_api);
            return;
        }
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

    if( strcmp(ev->id, "d_ignore") == 0 && g_page_built && g_built_detail >= 0 )
    {
        /* The header's third op. The list is the config key a person can also
         * type into, so both ways of saying it end up in one place. */
        char list[LT_CONFIG_VALUE_MAX];
        char const* existing = lt_config_string(rt, "ignored_sources");
        char source_name[sizeof(g_source[0].name)];
        int const source =
            lt_source_index_by_id(rt, g_built_detail_source_id);

        if( source < 0 )
        {
            g_api->panel.invalidate(g_api);
            return;
        }
        snprintf(source_name, sizeof(source_name), "%s", g_source[source].name);
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
    g_built_detail_source_id = 0;
    g_built_detail_source_label[0] = '\0';
    g_page_built = false;
    g_page_visible = false;
    g_next_panel_ms = 0;
    g_dirty = false;
    g_loot_revision = 0;

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
    if( g_page_visible )
    {
        (void)lt_sync_changed(rt, false);
        if( lt_page_stale(rt) )
            g_api->panel.invalidate(g_api);
        else
            lt_page_refresh(rt);
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
    (void)rt;
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

    if( g_dirty )
    {
        if( lt_page_stale(rt) )
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
    bool const affects_panel = filtered ||
                               (key && strcmp(key, "price_source") == 0);

    if( filtered ) (void)lt_sync_changed(rt, true);
    else if( affects_panel ) lt_revalue(rt);
    if( !g_page_visible || !affects_panel ) return;
    if( lt_page_stale(rt) )
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
        .on_ui_layout = lt_panel_layout,
    },
};
