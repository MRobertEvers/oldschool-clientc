#include "revconfig_refs.h"

#include "revconfig_load.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
RevConfigRefs_Init(struct RevConfigRefs* refs)
{
    assert(refs);
    memset(refs, 0, sizeof(*refs));
}

void
RevConfigRefs_Free(struct RevConfigRefs* refs)
{
    if( !refs )
        return;
    free(refs->entries);
    memset(refs, 0, sizeof(*refs));
}

static struct RevConfigRefEntry*
refs_find(
    struct RevConfigRefs* refs,
    char const* kind,
    char const* name)
{
    assert(refs);
    assert(kind);
    assert(name);
    for( int i = 0; i < refs->count; i++ )
    {
        if( strcmp(refs->entries[i].kind, kind) == 0 &&
            strcmp(refs->entries[i].name, name) == 0 )
            return &refs->entries[i];
    }
    return NULL;
}

static void
refs_set(
    struct RevConfigRefs* refs,
    char const* kind,
    char const* name,
    int id,
    int alt_id)
{
    struct RevConfigRefEntry* entry;

    assert(refs);
    assert(kind);
    assert(name);
    if( kind[0] == '\0' || name[0] == '\0' )
        return;

    entry = refs_find(refs, kind, name);
    if( !entry )
    {
        if( refs->count >= refs->capacity )
        {
            int grown = refs->capacity == 0 ? 32 : refs->capacity * 2;
            struct RevConfigRefEntry* items =
                realloc(refs->entries, (size_t)grown * sizeof(*items));
            assert(items);
            refs->entries = items;
            refs->capacity = grown;
        }
        entry = &refs->entries[refs->count++];
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->kind, kind, sizeof(entry->kind) - 1);
        strncpy(entry->name, name, sizeof(entry->name) - 1);
    }
    entry->id = id;
    entry->alt_id = alt_id;
}

/* ------------------------------------------------------------------------ */
/* Sidebar tabs                                                              */
/* ------------------------------------------------------------------------ */

/*
 * The four kinds a `[tabs]` family resolves through, all of them plain ints so
 * that RevConfigRefs_Get answers every one of them:
 *
 *   tab        <name>          -> the tab NUMBER. The base map.
 *   tabcol     <root>:<name>   -> which column of that root the tab is in.
 *   tabpos     <root>:<name>   -> where in that column, top first.
 *   tabdetach  <root>:<name>   -> 1 for a tab that root places outside every
 *                                 column. Absent reads -1, i.e. "not
 *                                 detached", which is the same answer as "this
 *                                 root states no arrangement at all" -- and
 *                                 that is right: a root with no override lays
 *                                 its tabs out in one plain run.
 *
 * Why not the raw `columns=` string under one name: this table answers ints,
 * and a caller handed the string would have to re-parse the grammar the
 * profile already wrote. These four are the questions a caller actually asks.
 */
#define REVCONFIG_REFS_TAB_KIND "tab"
#define REVCONFIG_REFS_TAB_COLUMN_KIND "tabcol"
#define REVCONFIG_REFS_TAB_POSITION_KIND "tabpos"
#define REVCONFIG_REFS_TAB_DETACHED_KIND "tabdetach"

/** `<root>:<name>` into `out`; 0 when it does not fit. */
static int
refs_tab_key(
    char* out,
    size_t capacity,
    int root,
    char const* name,
    size_t name_length)
{
    int written;

    assert(out);
    assert(name);

    if( name_length == 0 || name_length >= capacity )
        return 0;
    written = snprintf(out, capacity, "%d:%.*s", root, (int)name_length, name);
    return written > 0 && (size_t)written < capacity;
}

/*
 * Register one comma-separated run of tab names -- one `columns=` column, or
 * the whole of `detached=`.
 *
 * `position_kind` NULL is the detached run, which has no order to record: its
 * whole content is that those tabs are NOT in a column.
 */
static void
refs_add_tab_run(
    struct RevConfigRefs* refs,
    int root,
    char const* run,
    char const* column_kind,
    char const* position_kind,
    int column_index)
{
    char const* cursor;
    int position = 0;

    assert(refs);
    assert(run);
    assert(column_kind);

    for( cursor = run; *cursor; )
    {
        char const* comma = strchr(cursor, ',');
        size_t length = comma ? (size_t)(comma - cursor) : strlen(cursor);
        char key[REVCONFIG_REFS_NAME_LEN];

        while( length > 0 && (cursor[0] == ' ' || cursor[0] == '\t') )
        {
            cursor++;
            length--;
        }
        while( length > 0 && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t') )
            length--;

        if( length > 0 && refs_tab_key(key, sizeof(key), root, cursor, length) )
        {
            refs_set(refs, column_kind, key, column_index, -1);
            if( position_kind )
                refs_set(refs, position_kind, key, position, -1);
            position++;
        }

        if( !comma )
            return;
        cursor = comma + 1;
    }
}

/** One `[tabs]` / `[tabs:<root>]` item, as the kinds above. */
static void
refs_add_tabs_item(
    struct RevConfigRefs* refs,
    struct RevConfigTabsItem const* tabs)
{
    assert(refs);
    assert(tabs);

    for( int i = 0; i < tabs->entry_count; i++ )
        refs_set(
            refs, REVCONFIG_REFS_TAB_KIND, tabs->entries[i].name, tabs->entries[i].number, -1);

    /* The arrangement keys are a per-root fact and mean nothing without one, so
     * a `[tabs]` with no root states the base map only. */
    if( tabs->root < 0 )
        return;
    for( int i = 0; i < tabs->column_count; i++ )
        refs_add_tab_run(
            refs,
            tabs->root,
            tabs->columns[i],
            REVCONFIG_REFS_TAB_COLUMN_KIND,
            REVCONFIG_REFS_TAB_POSITION_KIND,
            i);
    if( tabs->detached[0] != '\0' )
        refs_add_tab_run(
            refs, tabs->root, tabs->detached, REVCONFIG_REFS_TAB_DETACHED_KIND, NULL, 1);
}

/*
 * The dat1 fallback: `[role:panel_<name>] match=slot(sidebar, <n>)`.
 *
 * A 2004 profile carries the tab numbering already, in the only place it can.
 * Its sidebar mounts are found BY tab number, so every `panel_<name>` role IS
 * a name -> number row, written in the role grammar. Reading it here is what
 * lets `RevConfigRefs_Get(refs, "tab", "inventory")` answer on a lane with no
 * `[tabs]` section, instead of every caller learning which of two spellings
 * the lane it booted on uses.
 *
 * It never fires on a cache lane: there the sidebar mounts come from the CS2
 * toplevel, so `panel_<name>` is stated as `id(if(<iface>, 0))` and carries no
 * tab number at all -- which is exactly why the dat2 profile states `[tabs]`.
 *
 * An explicit row wins in either order: a `[tabs]` read earlier is left alone
 * by the guard below, and one read later replaces this the way any later
 * declaration replaces an earlier one.
 */
static void
refs_add_tab_from_panel_role(
    struct RevConfigRefs* refs,
    struct RevConfigRoleItem const* role)
{
    static char const PANEL_PREFIX[] = "panel_";
    size_t const prefix_length = sizeof(PANEL_PREFIX) - 1;
    char const* name;

    assert(refs);
    assert(role);

    if( strncmp(role->name, PANEL_PREFIX, prefix_length) != 0 )
        return;
    name = role->name + prefix_length;
    if( name[0] == '\0' )
        return;
    if( refs_find(refs, REVCONFIG_REFS_TAB_KIND, name) )
        return;

    for( int i = 0; i < role->matcher_count; i++ )
    {
        struct RevConfigRoleMatcher const* matcher = &role->matchers[i];
        int number;

        if( matcher->kind != REVCONFIG_ROLE_MATCH_SLOT )
            continue;
        if( strcmp(matcher->slot, "sidebar") != 0 || matcher->member[0] == '\0' )
            continue;
        number = revconfig_parse_int(matcher->member);
        if( number < 0 )
            continue;
        refs_set(refs, REVCONFIG_REFS_TAB_KIND, name, number, -1);
        return;
    }
}

void
RevConfigRefs_AddItems(
    struct RevConfigRefs* refs,
    struct RevConfigItemBuffer const* items)
{
    assert(refs);
    assert(items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_CACHE_REF )
            refs_set(
                refs, item->u.cacheref.kind, item->u.cacheref.name, item->u.cacheref.id, -1);
        else if( item->kind == RCITEM_CACHE_FONT )
            refs_set(
                refs, "font", item->u.font.name, item->u.font.archive_id,
                item->u.font.cache_font_id);
        else if( item->kind == RCITEM_TABS )
            refs_add_tabs_item(refs, &item->u.tabs);
        else if( item->kind == RCITEM_ROLE )
            refs_add_tab_from_panel_role(refs, &item->u.role);
    }
}

/** Parse one source into `refs`. `prefix` NULL/"" is the unprefixed dialect. */
static void
refs_load_one(
    struct RevConfigRefs* refs,
    char const* path,
    char const* prefix)
{
    struct RevConfigBuffer* fields;
    struct RevConfigItemBuffer* items;

    assert(refs);
    if( !path || path[0] == '\0' )
        return;

    fields = revconfig_buffer_new(256);
    assert(fields);
    items = revconfig_item_buffer_new(64);
    assert(items);

    revconfig_load_fields_from_ini_prefixed(path, prefix, fields);
    revconfig_items_build(fields, items);
    RevConfigRefs_AddItems(refs, items);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

int
RevConfigRefs_LoadSources(
    struct RevConfigRefs* refs,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini)
{
    char generated_roles[512];

    assert(refs);
    /* Same order as UIBuilderManifestSources: shared files first, the boot
     * manifest's own inline sections last, so a lane can override one id
     * without copying the whole profile. */
    refs_load_one(refs, ui_ini, NULL);
    refs_load_one(refs, cache_ini, NULL);
    /*
     * The generated companion to a dat2 lane's cache ini -- tools/
     * revconfig_roles_from_pack.py's [iface:] output, chained here rather
     * than into the hand-edited file it sits beside (osrs239's
     * osrs239_dat2_cache.ini -> osrs239_dat2_roles.gen.ini). Loaded right
     * after cache_ini so a role in either file can reference one of its
     * [iface:] sections through this same refs table.
     *
     * UITreeRoleLoad_LoadSources chains the matching [role:] half the same
     * way, from the same derived path. A lane with no generated sibling
     * (dat1, or a dat2 lane this generator has not run for) derives nothing
     * here and refs_load_one's own missing-file handling no-ops on it.
     */
    if( cache_ini &&
        revconfig_derive_sibling_path(
            cache_ini,
            "_dat2_cache.ini",
            "_dat2_roles.gen.ini",
            generated_roles,
            sizeof(generated_roles)) )
        refs_load_one(refs, generated_roles, NULL);
    refs_load_one(refs, inline_ini, "revconfig");
    return refs->count;
}

int
RevConfigRefs_Get(
    struct RevConfigRefs const* refs,
    char const* kind,
    char const* name)
{
    struct RevConfigRefEntry const* entry;
    assert(refs);
    assert(kind);
    assert(name);
    entry = refs_find((struct RevConfigRefs*)refs, kind, name);
    return entry ? entry->id : -1;
}

int
RevConfigRefs_FontCacheId(
    struct RevConfigRefs const* refs,
    char const* name,
    int dat1)
{
    struct RevConfigRefEntry const* entry;
    assert(refs);
    assert(name);
    entry = refs_find((struct RevConfigRefs*)refs, "font", name);
    if( !entry )
        return -1;
    return dat1 ? entry->alt_id : entry->id;
}
