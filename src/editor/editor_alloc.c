#include "editor_alloc.h"

#include "editor_cfg.h"

#include "content/content_register.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- name -> id ----------------------------------------------------------- */

static uint32_t
hash_bytes(
    const char* name,
    size_t length)
{
    uint32_t hash = 2166136261u;

    for( size_t i = 0; i < length; i++ )
    {
        hash ^= (unsigned char)name[i];
        hash *= 16777619u;
    }
    return hash;
}

static int
names_slot(
    const struct Editor_AllocNames* map,
    const char* name,
    size_t length)
{
    uint32_t mask = (uint32_t)map->capacity - 1;
    uint32_t at = hash_bytes(name, length) & mask;

    while( map->names[at] )
    {
        if( strlen(map->names[at]) == length && memcmp(map->names[at], name, length) == 0 )
            return (int)at;
        at = (at + 1) & mask;
    }
    return (int)at;
}

static void
names_grow(struct Editor_AllocNames* map)
{
    struct Editor_AllocNames next;

    next.capacity = map->capacity ? map->capacity * 2 : 1024;
    next.count = 0;
    next.names = ToriMem_Calloc(ToriMem_Editor(), (size_t)next.capacity, sizeof(*next.names));
    next.spellings =
        ToriMem_Calloc(ToriMem_Editor(), (size_t)next.capacity, sizeof(*next.spellings));
    next.ids = ToriMem_Calloc(ToriMem_Editor(), (size_t)next.capacity, sizeof(*next.ids));
    assert(next.names);
    assert(next.spellings);
    assert(next.ids);
    for( int i = 0; i < map->capacity; i++ )
    {
        if( map->names[i] )
        {
            int slot = names_slot(&next, map->names[i], strlen(map->names[i]));
            next.names[slot] = map->names[i];
            next.spellings[slot] = map->spellings[i];
            next.ids[slot] = map->ids[i];
            next.count++;
        }
    }
    ToriMem_Free(ToriMem_Editor(), map->names);
    ToriMem_Free(ToriMem_Editor(), map->spellings);
    ToriMem_Free(ToriMem_Editor(), map->ids);
    *map = next;
}

/** Bind `name` (length bytes). `replace`: a dict assignment; else
 *  setdefault. `spelling` (may be NULL) is kept beside a new binding.
 *  Returns 1 when the binding changed. */
static int
names_put_spelled(
    struct Editor_AllocNames* map,
    const char* name,
    size_t length,
    int id,
    int replace,
    const char* spelling,
    size_t spelling_length)
{
    int slot;

    if( (map->count + 1) * 10 >= map->capacity * 7 )
        names_grow(map);
    slot = names_slot(map, name, length);
    if( map->names[slot] )
    {
        if( !replace || map->ids[slot] == id )
            return 0;
        map->ids[slot] = id;
        return 1;
    }
    map->names[slot] = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(map->names[slot]);
    memcpy(map->names[slot], name, length);
    map->names[slot][length] = '\0';
    if( spelling )
    {
        map->spellings[slot] = ToriMem_Alloc(ToriMem_Editor(), spelling_length + 1);
        assert(map->spellings[slot]);
        memcpy(map->spellings[slot], spelling, spelling_length);
        map->spellings[slot][spelling_length] = '\0';
    }
    map->ids[slot] = id;
    map->count++;
    return 1;
}

static int
names_put(
    struct Editor_AllocNames* map,
    const char* name,
    size_t length,
    int id,
    int replace)
{
    return names_put_spelled(map, name, length, id, replace, NULL, 0);
}

/** 1 when `name` is bound: its id, and its spelling (the stored spelling
 *  when there is one, else the name). */
static int
names_get(
    const struct Editor_AllocNames* map,
    const char* name,
    size_t length,
    int* out_id,
    const char** out_spelling)
{
    int slot;

    if( map->capacity == 0 )
        return 0;
    slot = names_slot(map, name, length);
    if( !map->names[slot] )
        return 0;
    *out_id = map->ids[slot];
    *out_spelling = map->spellings[slot] ? map->spellings[slot] : map->names[slot];
    return 1;
}

static void
names_free(struct Editor_AllocNames* map)
{
    for( int i = 0; i < map->capacity; i++ )
    {
        ToriMem_Free(ToriMem_Editor(), map->names[i]);
        ToriMem_Free(ToriMem_Editor(), map->spellings[i]);
    }
    ToriMem_Free(ToriMem_Editor(), map->names);
    ToriMem_Free(ToriMem_Editor(), map->spellings);
    ToriMem_Free(ToriMem_Editor(), map->ids);
    memset(map, 0, sizeof(*map));
}

/* ---- id set ----------------------------------------------------------------- */

static int
ids_slot(
    const struct Editor_AllocIds* set,
    int id)
{
    uint32_t mask = (uint32_t)set->capacity - 1;
    uint32_t at = ((uint32_t)id * 2654435761u) & mask;

    while( set->used[at] && set->slots[at] != id )
        at = (at + 1) & mask;
    return (int)at;
}

static void ids_add(struct Editor_AllocIds* set, int id);

static void
ids_grow(struct Editor_AllocIds* set)
{
    struct Editor_AllocIds old = *set;

    set->capacity = old.capacity ? old.capacity * 2 : 1024;
    set->count = 0;
    set->slots = ToriMem_Calloc(ToriMem_Editor(), (size_t)set->capacity, sizeof(*set->slots));
    set->used = ToriMem_Calloc(ToriMem_Editor(), (size_t)set->capacity, 1);
    assert(set->slots);
    assert(set->used);
    for( int i = 0; i < old.capacity; i++ )
    {
        if( old.used[i] )
            ids_add(set, old.slots[i]);
    }
    ToriMem_Free(ToriMem_Editor(), old.slots);
    ToriMem_Free(ToriMem_Editor(), old.used);
}

static void
ids_add(
    struct Editor_AllocIds* set,
    int id)
{
    int slot;

    if( (set->count + 1) * 10 >= set->capacity * 7 )
        ids_grow(set);
    slot = ids_slot(set, id);
    if( set->used[slot] )
        return;
    set->used[slot] = 1;
    set->slots[slot] = id;
    set->count++;
}

static int
ids_has(
    const struct Editor_AllocIds* set,
    int id)
{
    return set->capacity > 0 && set->used[ids_slot(set, id)];
}

static void
ids_free(struct Editor_AllocIds* set)
{
    ToriMem_Free(ToriMem_Editor(), set->slots);
    ToriMem_Free(ToriMem_Editor(), set->used);
    memset(set, 0, sizeof(*set));
}

/* ---- state -------------------------------------------------------------------- */

/** ss_allocate.py's VAR_PREFIX. */
static const char*
var_prefix(const char* ns)
{
    if( strcmp(ns, "varp") == 0 )
        return "varp";
    if( strcmp(ns, "varbit") == 0 )
        return "varb";
    if( strcmp(ns, "varc") == 0 )
        return "varc";
    return "";
}

/** ss_allocate.py's NON_CONFIG_NAMESPACES: their member index is a pack. */
static int
non_config(const char* ns)
{
    return strcmp(ns, "3_interfaces") == 0 || strcmp(ns, "component") == 0
           || strcmp(ns, "stat") == 0 || strcmp(ns, "category") == 0;
}

void
Editor_AllocNsInit(
    struct Editor_AllocNs* state,
    const char* ns,
    int floor,
    int ids_server)
{
    assert(state);
    assert(ns);
    assert(strlen(ns) < sizeof(state->ns));

    memset(state, 0, sizeof(*state));
    snprintf(state->ns, sizeof(state->ns), "%s", ns);
    snprintf(state->prefix, sizeof(state->prefix), "%s", var_prefix(ns));
    state->floor = floor;
    state->ids_server = ids_server;
    state->not_allocatable = strcmp(ns, "component") == 0;
    state->ceiling = strcmp(ns, "varp") == 0 ? EDITOR_ALLOC_VARP_CEILING : 0;
    state->highest_other = -1;
    state->highest_ledger = -1;
    state->cursor = -1;
}

void
Editor_AllocNsFree(struct Editor_AllocNs* state)
{
    if( !state )
        return;
    names_free(&state->ledger);
    names_free(&state->other);
    names_free(&state->bare);
    ids_free(&state->held);
    memset(state, 0, sizeof(*state));
}

/** `<prefix><digits>_<rest>`: the offset of rest, or 0. */
static size_t
prefixed_rest(
    const struct Editor_AllocNs* state,
    const char* name,
    size_t length,
    int* out_id)
{
    size_t prefix_length = strlen(state->prefix);
    size_t at;
    long id = 0;

    if( prefix_length == 0 || length <= prefix_length
        || memcmp(name, state->prefix, prefix_length) != 0 )
        return 0;
    at = prefix_length;
    if( at >= length || !isdigit((unsigned char)name[at]) )
        return 0;
    while( at < length && isdigit((unsigned char)name[at]) )
    {
        if( id < 100000000 )
            id = id * 10 + (name[at] - '0');
        at++;
    }
    if( at >= length - 1 || name[at] != '_' )
        return 0;
    if( out_id )
        *out_id = (int)id;
    return at + 1;
}

static void
note_bare(
    struct Editor_AllocNs* state,
    const char* name,
    size_t length,
    int id)
{
    size_t rest = prefixed_rest(state, name, length, NULL);

    if( rest )
        names_put_spelled(&state->bare, name + rest, length - rest, id, 0, name, length);
}

/** Rebuild the held set: every id the ledger's (final) bindings and the
 *  other layers' (first) bindings hold — ss_allocate.py's `held`. */
static void
rebuild_held(struct Editor_AllocNs* state)
{
    ids_free(&state->held);
    for( int i = 0; i < state->ledger.capacity; i++ )
    {
        if( state->ledger.names[i] )
            ids_add(&state->held, state->ledger.ids[i]);
    }
    for( int i = 0; i < state->other.capacity; i++ )
    {
        if( state->other.names[i] )
            ids_add(&state->held, state->other.ids[i]);
    }
}

static int
ledger_highest(const struct Editor_AllocNs* state)
{
    int highest = -1;

    for( int i = 0; i < state->ledger.capacity; i++ )
    {
        if( state->ledger.names[i] && state->ledger.ids[i] > highest )
            highest = state->ledger.ids[i];
    }
    return highest;
}

/* ---- reading layers --------------------------------------------------------- */

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/** Trim [*start, *end) of whitespace. */
static void
trim(
    const char* text,
    size_t* start,
    size_t* end)
{
    while( *start < *end && is_space(text[*start]) )
        (*start)++;
    while( *end > *start && is_space(text[*end - 1]) )
        (*end)--;
}

/** `ident.strip().lstrip('-').isdigit()` then int(): 1 and the value. */
static int
parse_ident(
    const char* text,
    size_t start,
    size_t end,
    int* out)
{
    long value = 0;
    int negative = 0;

    trim(text, &start, &end);
    while( start < end && text[start] == '-' )
    {
        negative = 1;
        start++;
    }
    if( start == end )
        return 0;
    for( size_t i = start; i < end; i++ )
    {
        if( !isdigit((unsigned char)text[i]) )
            return 0;
        if( value < 1000000000L )
            value = value * 10 + (text[i] - '0');
    }
    *out = negative ? -(int)value : (int)value;
    return 1;
}

/** The script's name cleanup (drop a trailing "// note" and the space
 *  before it, then strip): cut at the first "//", trim. */
static void
clean_name(
    const char* text,
    size_t* start,
    size_t* end)
{
    for( size_t i = *start; i + 1 < *end; i++ )
    {
        if( text[i] == '/' && text[i + 1] == '/' )
        {
            *end = i;
            break;
        }
    }
    trim(text, start, end);
}

/** Calls `line(state, text, start, end)` for every line (no newline). */
static void
each_line(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length,
    void (*line)(struct Editor_AllocNs*, const char*, size_t, size_t))
{
    size_t at = 0;

    while( at < length )
    {
        const char* newline = memchr(text + at, '\n', length - at);
        size_t stop = newline ? (size_t)(newline - text) : length;

        line(state, text, at, stop);
        at = newline ? stop + 1 : length;
    }
}

static void
member_line(
    struct Editor_AllocNs* state,
    const char* text,
    size_t start,
    size_t end)
{
    const char* equals;
    size_t name_start;
    size_t name_end;
    int id;

    trim(text, &start, &end);
    if( start == end || (end - start >= 2 && text[start] == '/' && text[start + 1] == '/') )
        return;
    equals = memchr(text + start, '=', end - start);
    if( !equals )
        return;
    if( !parse_ident(text, start, (size_t)(equals - text), &id) )
        return;
    name_start = (size_t)(equals - text) + 1;
    name_end = end;
    clean_name(text, &name_start, &name_end);
    if( names_put(&state->other, text + name_start, name_end - name_start, id, 0) )
        note_bare(state, text + name_start, name_end - name_start, id);
    if( id > state->highest_other )
        state->highest_other = id;
}

static int
is_marker(
    const char* text,
    size_t start,
    size_t end)
{
    static const char marker[] = EDITOR_ALLOC_MARKER;

    trim(text, &start, &end);
    return end - start == sizeof(marker) - 1 && memcmp(text + start, marker, end - start) == 0;
}

static void
ledger_line(
    struct Editor_AllocNs* state,
    const char* text,
    size_t start,
    size_t end)
{
    const char* equals;
    size_t name_start;
    size_t name_end;
    int id;

    if( is_marker(text, start, end) )
        return;
    equals = memchr(text + start, '=', end - start);
    if( !equals || !parse_ident(text, start, (size_t)(equals - text), &id) )
        return;
    name_start = (size_t)(equals - text) + 1;
    name_end = end;
    clean_name(text, &name_start, &name_end);
    if( names_put(&state->ledger, text + name_start, name_end - name_start, id, 1) )
        note_bare(state, text + name_start, name_end - name_start, id);
}

static void
config_line(
    struct Editor_AllocNs* state,
    const char* text,
    size_t start,
    size_t end)
{
    size_t ns_length = strlen(state->ns);
    size_t at = start;
    long id = 0;
    size_t digits = 0;

    while( at < end && is_space(text[at]) )
        at++;
    if( at >= end || text[at] != '[' )
        return;
    at++;
    if( end - at < ns_length + 1 || memcmp(text + at, state->ns, ns_length) != 0
        || text[at + ns_length] != '_' )
        return;
    at += ns_length + 1;
    while( at < end && isdigit((unsigned char)text[at]) )
    {
        if( id < 1000000000L )
            id = id * 10 + (text[at] - '0');
        at++;
        digits++;
    }
    if( digits == 0 || at >= end || text[at] != ']' )
        return;
    for( at++; at < end; at++ )
    {
        if( !is_space(text[at]) )
            return;
    }
    if( (int)id > state->highest_other )
        state->highest_other = (int)id;
}

void
Editor_AllocNsAddMembers(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length)
{
    assert(state);
    assert(text || length == 0);
    each_line(state, text, length, member_line);
}

void
Editor_AllocNsAddLedger(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length)
{
    assert(state);
    assert(text || length == 0);
    each_line(state, text, length, ledger_line);
    state->highest_ledger = ledger_highest(state);
}

void
Editor_AllocNsAddConfig(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length)
{
    assert(state);
    assert(text || length == 0);
    each_line(state, text, length, config_line);
}

/* ---- the tree ------------------------------------------------------------------ */

static char*
read_file(
    const char* path,
    size_t* out_length)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    *out_length = 0;
    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 )
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        ToriMem_Free(ToriMem_Editor(), data);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_length = (size_t)size;
    return data;
}

static void
feed_file(
    struct Editor_AllocNs* state,
    const char* path,
    void (*feed)(struct Editor_AllocNs*, const char*, size_t))
{
    size_t length;
    char* text = read_file(path, &length);

    if( !text )
        return; /* an absent layer is an empty one */
    feed(state, text, length);
    ToriMem_Free(ToriMem_Editor(), text);
}

static void
member_index_path(
    char* out,
    size_t out_size,
    const char* tree,
    const char* ns)
{
    if( non_config(ns) )
        snprintf(out, out_size, "%s/pack/%s.pack", tree, ns);
    else
        snprintf(out, out_size, "%s/configs/all.%s.compack", tree, ns);
}

/** content.ini's `key` for `[namespace:<ns>]`, ss_allocate.py's reading:
 *  `[;#]` starts a comment, lines are stripped. `first`: the first match
 *  (declared_base returns at it), else the last (id_authority's dict). */
static int
content_ini_value(
    const char* tree,
    const char* ns,
    const char* key,
    int first,
    char* out,
    size_t out_size)
{
    char path[4096];
    size_t length;
    char* text;
    size_t at = 0;
    int in_section = 0;
    int found = 0;

    snprintf(path, sizeof(path), "%s/content.ini", tree);
    text = read_file(path, &length);
    if( !text )
        return 0;
    while( at < length )
    {
        const char* newline = memchr(text + at, '\n', length - at);
        size_t stop = newline ? (size_t)(newline - text) : length;
        size_t start = at;
        size_t end = stop;
        const char* equals;

        at = newline ? stop + 1 : length;
        for( size_t i = start; i < end; i++ )
        {
            if( text[i] == ';' || text[i] == '#' )
            {
                end = i;
                break;
            }
        }
        trim(text, &start, &end);
        /* Only `[namespace:<name>]` changes the section, as the script's
         * regex: any other bracketed line is just a line without '='. */
        if( end - start > 12 && memcmp(text + start, "[namespace:", 11) == 0 && text[end - 1] == ']' )
        {
            int word = 1;

            for( size_t i = start + 11; i < end - 1; i++ )
                word &= isalnum((unsigned char)text[i]) || text[i] == '_';
            if( word )
            {
                in_section = end - 1 - (start + 11) == strlen(ns)
                             && memcmp(text + start + 11, ns, strlen(ns)) == 0;
                continue;
            }
        }
        if( !in_section )
            continue;
        equals = memchr(text + start, '=', end - start);
        if( !equals )
            continue;
        {
            size_t key_start = start;
            size_t key_end = (size_t)(equals - text);
            size_t value_start = key_end + 1;
            size_t value_end = end;

            trim(text, &key_start, &key_end);
            trim(text, &value_start, &value_end);
            if( key_end - key_start == strlen(key)
                && memcmp(text + key_start, key, key_end - key_start) == 0 )
            {
                snprintf(out, out_size, "%.*s", (int)(value_end - value_start), text + value_start);
                found = 1;
                if( first )
                    break;
            }
        }
    }
    ToriMem_Free(ToriMem_Editor(), text);
    return found;
}

static int
compare_strings(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

int
Editor_AllocNsLoad(
    struct Editor_AllocNs* state,
    const char* content_dir,
    const char* ns)
{
    return Editor_AllocNsLoadWith(state, content_dir, ns, NULL, NULL);
}

/** Feed one layer from `text` when it is given (an open document's current
 *  text; text NULL = absent), else from the file at `path`. */
static void
feed_layer(
    struct Editor_AllocNs* state,
    const char* path,
    const struct Editor_AllocText* text,
    void (*feed)(struct Editor_AllocNs*, const char*, size_t))
{
    if( !text )
    {
        feed_file(state, path, feed);
        return;
    }
    if( text->text && text->length > 0 )
        feed(state, text->text, text->length);
}

int
Editor_AllocNsLoadWith(
    struct Editor_AllocNs* state,
    const char* content_dir,
    const char* ns,
    const struct Editor_AllocText* index_text,
    const struct Editor_AllocText* ledger_text)
{
    char value[64];
    char path[4096];
    int floor = 0;
    int ids_server = 1;
    struct stat st;

    assert(state);
    assert(content_dir);
    assert(ns);

    if( stat(content_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
        return 0;

    /* The floor: content.ini overlays the register's built-in table. */
    if( content_ini_value(content_dir, ns, "base", 1, value, sizeof(value)) )
        floor = strcmp(value, "none") == 0 ? 0 : atoi(value);
    else
    {
        struct ContentRegister defaults;
        const struct ContentNamespace* entry;

        ContentRegister_Defaults(&defaults);
        entry = ContentRegister_Find(&defaults, ns);
        floor = entry ? entry->server_base : 0;
    }
    if( content_ini_value(content_dir, ns, "ids", 0, value, sizeof(value)) )
        ids_server = strcmp(value, "server") == 0;

    Editor_AllocNsInit(state, ns, floor, ids_server);
    if( content_ini_value(content_dir, ns, "ids", 0, value, sizeof(value))
        && strcmp(value, "protocol") == 0 )
        state->not_allocatable = 1;

    /* The cache layer's member index, then every lane's, in name order. */
    member_index_path(path, sizeof(path), content_dir, ns);
    feed_layer(state, path, index_text, Editor_AllocNsAddMembers);
    snprintf(path, sizeof(path), "%s/ported", content_dir);
    {
        DIR* dir = opendir(path);
        char** lanes = NULL;
        int count = 0;
        int capacity = 0;
        struct dirent* entry;

        while( dir && (entry = readdir(dir)) != NULL )
        {
            char lane_path[4096];

            if( entry->d_name[0] == '.' )
                continue;
            snprintf(lane_path, sizeof(lane_path), "%s/%s", path, entry->d_name);
            if( stat(lane_path, &st) != 0 || !S_ISDIR(st.st_mode) )
                continue;
            if( count == capacity )
            {
                capacity = capacity ? capacity * 2 : 16;
                lanes = ToriMem_Realloc(ToriMem_Editor(), lanes, sizeof(*lanes) * (size_t)capacity);
                assert(lanes);
            }
            lanes[count] = ToriMem_Strdup(ToriMem_Editor(), entry->d_name);
            assert(lanes[count]);
            count++;
        }
        if( dir )
            closedir(dir);
        /* Not with no lanes: lanes is NULL then, and qsort's base is declared
         * nonnull, so the optimizer took lanes for non-NULL and dropped the
         * NULL test in the inlined ToriMem_Free below (a tree with no ported/
         * crashed every copy at -O3 and ran at -O0). */
        if( count > 1 )
            qsort(lanes, (size_t)count, sizeof(*lanes), compare_strings);
        for( int i = 0; i < count; i++ )
        {
            char lane_tree[4096];

            snprintf(lane_tree, sizeof(lane_tree), "%s/%s", path, lanes[i]);
            {
                char lane_file[4200];

                member_index_path(lane_file, sizeof(lane_file), lane_tree, ns);
                feed_file(state, lane_file, Editor_AllocNsAddMembers);
                snprintf(lane_file, sizeof(lane_file), "%s/pack/%s.alloc", lane_tree, ns);
                feed_file(state, lane_file, Editor_AllocNsAddMembers);
            }
            ToriMem_Free(ToriMem_Editor(), lanes[i]);
        }
        ToriMem_Free(ToriMem_Editor(), lanes);
    }

    /* Unnamed cache records are as real as named ones. */
    snprintf(path, sizeof(path), "%s/configs/all.%s", content_dir, ns);
    feed_file(state, path, Editor_AllocNsAddConfig);

    snprintf(path, sizeof(path), "%s/pack/%s.alloc", content_dir, ns);
    feed_layer(state, path, ledger_text, Editor_AllocNsAddLedger);
    return 1;
}

/* ---- choosing ------------------------------------------------------------------- */

int
Editor_AllocNsLookup(
    const struct Editor_AllocNs* state,
    const char* name,
    int* out_id,
    char* out_spelling,
    size_t spelling_size)
{
    size_t length;
    const char* spelling = NULL;
    int id = -1;
    int found;

    assert(state);
    assert(name);

    length = strlen(name);
    found = names_get(&state->ledger, name, length, &id, &spelling);
    if( !found )
        found = names_get(&state->other, name, length, &id, &spelling);
    /* A bare name some layer holds prefixed is that var. */
    if( !found && state->prefix[0] )
        found = names_get(&state->bare, name, length, &id, &spelling);
    if( !found )
        return 0;
    if( out_id )
        *out_id = id;
    if( out_spelling )
        snprintf(out_spelling, spelling_size, "%s", spelling);
    return 1;
}

int
Editor_AllocNsBase(const struct Editor_AllocNs* state)
{
    int highest;
    int mark;

    assert(state);

    highest = state->highest_other > state->highest_ledger ? state->highest_other
                                                           : state->highest_ledger;
    mark = highest + 1;
    return mark > state->floor ? mark : state->floor;
}

static int
next_free(
    struct Editor_AllocNs* state,
    int from)
{
    rebuild_held(state);
    while( ids_has(&state->held, from) )
        from++;
    return from;
}

int
Editor_AllocNsNextId(struct Editor_AllocNs* state)
{
    assert(state);
    return next_free(state, state->cursor >= 0 ? state->cursor : Editor_AllocNsBase(state));
}

static int
block_name_valid(const char* name)
{
    /* ss_allocate.py's `[block]`: [A-Za-z0-9_+.-]+. */
    if( !name[0] )
        return 0;
    for( const char* at = name; *at; at++ )
    {
        if( !isalnum((unsigned char)*at) && *at != '_' && *at != '+' && *at != '.' && *at != '-' )
            return 0;
    }
    return 1;
}

/** Choose without recording. `*out_adopted` = a prefixed name kept its id. */
static enum Editor_AllocStatus
choose(
    struct Editor_AllocNs* state,
    enum Editor_AllocTarget target,
    const char* name,
    int* out_id,
    char* out_name,
    size_t name_size,
    int* out_adopted)
{
    int prefixed_id = -1;
    size_t rest;
    int written;

    *out_adopted = 0;
    if( state->not_allocatable || (target == EDITOR_ALLOC_LEDGER && !state->ids_server) )
        return EDITOR_ALLOC_REFUSED;
    if( !block_name_valid(name) )
        return EDITOR_ALLOC_BAD_NAME;

    if( Editor_AllocNsLookup(state, name, out_id, out_name, name_size) )
        return EDITOR_ALLOC_EXISTS;

    rest = prefixed_rest(state, name, strlen(name), &prefixed_id);
    if( rest )
    {
        /* A name that already spells `<prefix><N>_`: keep N if it is free. */
        if( Editor_AllocNsLookup(state, name + rest, out_id, out_name, name_size) )
            return EDITOR_ALLOC_EXISTS;
        rebuild_held(state);
        if( ids_has(&state->held, prefixed_id) || prefixed_id < state->floor )
            return EDITOR_ALLOC_ID_TAKEN;
        if( state->ceiling > 0 && prefixed_id >= state->ceiling )
            return EDITOR_ALLOC_FULL;
        if( strlen(name) + 1 > name_size || strlen(name) + 1 > EDITOR_CMD_NAME_MAX )
            return EDITOR_ALLOC_BAD_NAME;
        snprintf(out_name, name_size, "%s", name);
        *out_id = prefixed_id;
        *out_adopted = 1;
        return EDITOR_ALLOC_OK;
    }

    *out_id = Editor_AllocNsNextId(state);
    if( state->ceiling > 0 && *out_id >= state->ceiling )
        return EDITOR_ALLOC_FULL;
    if( state->prefix[0] )
        written = snprintf(out_name, name_size, "%s%d_%s", state->prefix, *out_id, name);
    else
        written = snprintf(out_name, name_size, "%s", name);
    if( written < 0 || (size_t)written >= name_size || written + 1 > EDITOR_CMD_NAME_MAX )
        return EDITOR_ALLOC_BAD_NAME;
    return EDITOR_ALLOC_OK;
}

static void
record(
    struct Editor_AllocNs* state,
    enum Editor_AllocTarget target,
    int id,
    const char* name,
    int adopted)
{
    size_t length = strlen(name);

    /* The base is fixed before the first binding moves the mark, as the
     * script computes it once per run; an adopted id does not move it. */
    if( state->cursor < 0 )
        state->cursor = Editor_AllocNsBase(state);
    if( !adopted )
        state->cursor = id + 1;
    if( target == EDITOR_ALLOC_LEDGER )
    {
        names_put(&state->ledger, name, length, id, 1);
        if( id > state->highest_ledger )
            state->highest_ledger = id;
    }
    else
    {
        names_put(&state->other, name, length, id, 0);
        if( id > state->highest_other )
            state->highest_other = id;
    }
    note_bare(state, name, length, id);
}

enum Editor_AllocStatus
Editor_AllocNsCoin(
    struct Editor_AllocNs* state,
    enum Editor_AllocTarget target,
    const char* name,
    int* out_id,
    char* out_name,
    size_t name_size)
{
    enum Editor_AllocStatus status;
    int adopted;

    assert(state);
    assert(name);
    assert(out_id);
    assert(out_name);
    assert(name_size > 0);

    out_name[0] = '\0';
    *out_id = -1;
    status = choose(state, target, name, out_id, out_name, name_size, &adopted);
    if( status == EDITOR_ALLOC_OK )
        record(state, target, *out_id, out_name, adopted);
    return status;
}

/* ---- emitting --------------------------------------------------------------------- */

int
Editor_AllocDocKey(
    const char* ns,
    const char* which,
    char* out,
    size_t out_size)
{
    int written;

    assert(ns);
    assert(which);
    assert(out);

    if( strcmp(which, "index") == 0 )
    {
        if( non_config(ns) )
            written = snprintf(out, out_size, "pack:%s.pack", ns);
        else
            written = snprintf(out, out_size, "compack:%s", ns);
    }
    else if( strcmp(which, "alloc") == 0 || strcmp(which, "client") == 0
             || strcmp(which, "server") == 0 )
        written = snprintf(out, out_size, "pack:%s.%s", ns, which);
    else
        return 0;
    return written > 0 && (size_t)written < out_size;
}

static const char* const k_ledger_header[] = {
    "// The `%s` namespace: ids this server allocated.",
    "//",
    "// The server's allocation ledger \xe2\x80\x94 one layer of the namespace whose other",
    "// layer is configs/all.%s.compack, the cache's member index. Every reader",
    "// loads both and refuses a name bound in each. Being listed here also routes",
    "// the record server-side in `cachepack pack`: the allocation is the",
    "// membership, so no pack/%s.server line is needed.",
    "//",
    "// Appended by tools/ss_allocate.py from the `[block]` names in",
    "// server/scripts/**/configs/*.%s, allocated from one past the largest id",
    "// anything else claims. See docs/CONTENT_PACK_PLAN.md \302\2474.",
    "//",
    "// Prose anywhere in this file survives: the allocator only ever appends, and",
    "// never rewrites a line it did not add.",
};

/** Append formatted bytes to out[*at]; 0 when they do not fit. */
static int
put(
    char* out,
    size_t out_size,
    size_t* at,
    const char* format,
    ...)
{
    va_list args;
    int written;

    if( *at >= out_size )
        return 0;
    va_start(args, format);
    written = vsnprintf(out + *at, out_size - *at, format, args);
    va_end(args);
    if( written < 0 || (size_t)written >= out_size - *at )
        return 0;
    *at += (size_t)written;
    return 1;
}

size_t
Editor_AllocLedgerAppend(
    const char* ns,
    const struct Editor_AllocText* current,
    int id,
    const char* name,
    char* out,
    size_t out_size)
{
    const char* text;
    size_t length;
    size_t at = 0;
    int has_marker = 0;
    int last_blank = 1;
    int ok = 1;

    assert(ns);
    assert(current);
    assert(name);
    assert(out);

    text = current->text ? current->text : "";
    length = current->text ? current->length : 0;

    /* Read the ledger the way write_pack does: its lines, any marker, and
     * whether the last line is blank. */
    {
        size_t line_at = 0;

        while( line_at < length )
        {
            const char* newline = memchr(text + line_at, '\n', length - line_at);
            size_t stop = newline ? (size_t)(newline - text) : length;
            size_t start = line_at;
            size_t end = stop;

            if( is_marker(text, start, end) )
                has_marker = 1;
            trim(text, &start, &end);
            last_blank = start == end;
            line_at = newline ? stop + 1 : length;
        }
    }

    if( length > 0 && text[length - 1] != '\n' )
        ok &= put(out, out_size, &at, "\n");
    if( length == 0 )
    {
        for( size_t i = 0; i < sizeof(k_ledger_header) / sizeof(k_ledger_header[0]); i++ )
        {
            char line[256];

            snprintf(line, sizeof(line), k_ledger_header[i], ns);
            ok &= put(out, out_size, &at, "%s\n", line);
        }
        last_blank = 0;
    }
    if( !has_marker )
    {
        if( !last_blank )
            ok &= put(out, out_size, &at, "\n");
        ok &= put(out, out_size, &at, "%s\n", EDITOR_ALLOC_MARKER);
    }
    ok &= put(out, out_size, &at, "%d=%s\n", id, name);
    return ok ? at : 0;
}

int
Editor_AllocEmitAppend(
    const char* doc_key,
    const struct Editor_AllocText* current,
    const char* bytes,
    size_t length,
    struct Editor_Cmd* out,
    int max)
{
    size_t offset;
    size_t done = 0;
    int count = 0;

    assert(doc_key);
    assert(current);
    assert(bytes || length == 0);
    assert(out || max == 0);

    offset = current->text ? current->length : 0;
    while( done < length )
    {
        size_t chunk = length - done;

        if( chunk > EDITOR_CMD_SPLICE_MAX )
            chunk = EDITOR_CMD_SPLICE_MAX;
        if( count == max )
            return -1;
        Editor_CmdInit(&out[count], EDITOR_DOC_TEXT, doc_key, EDITOR_CMD_TEXT_SPLICE);
        out[count].has_before = 0;
        out[count].has_after = 1;
        out[count].u.text_splice.offset = (uint32_t)(offset + done);
        out[count].u.text_splice.before_len = 0;
        out[count].u.text_splice.after_len = (uint32_t)chunk;
        memcpy(out[count].u.text_splice.text, bytes + done, chunk);
        done += chunk;
        count++;
    }
    return count;
}

/** One line appended to a text: a missing final newline first. */
static size_t
line_append(
    const struct Editor_AllocText* current,
    const char* line,
    char* out,
    size_t out_size)
{
    size_t at = 0;
    int ok = 1;

    if( current->text && current->length > 0 && current->text[current->length - 1] != '\n' )
        ok &= put(out, out_size, &at, "\n");
    ok &= put(out, out_size, &at, "%s\n", line);
    return ok ? at : 0;
}

/** 1 when a membership file already names `name` on a line of its own. */
static int
names_line(
    const struct Editor_AllocText* current,
    const char* name)
{
    size_t at = 0;
    size_t name_length = strlen(name);

    if( !current->text )
        return 0;
    while( at < current->length )
    {
        const char* newline = memchr(current->text + at, '\n', current->length - at);
        size_t stop = newline ? (size_t)(newline - current->text) : current->length;
        size_t start = at;
        size_t end = stop;

        trim(current->text, &start, &end);
        if( end - start == name_length && memcmp(current->text + start, name, name_length) == 0 )
            return 1;
        at = newline ? stop + 1 : current->length;
    }
    return 0;
}

enum Editor_AllocStatus
Editor_AllocPlan(
    struct Editor_AllocNs* state,
    const struct Editor_AllocRequest* request,
    struct Editor_Cmd* out,
    int max,
    struct Editor_AllocResult* result)
{
    char key[EDITOR_DOC_KEY_MAX];
    char bytes[4096];
    size_t length;
    int adopted;
    int count = 0;
    int written;

    assert(state);
    assert(request);
    assert(request->ns);
    assert(request->name);
    assert(out || max == 0);
    assert(result);
    assert(strcmp(request->ns, state->ns) == 0);

    memset(result, 0, sizeof(*result));
    result->id = -1;
    result->status = choose(
        state,
        request->target,
        request->name,
        &result->id,
        result->name,
        sizeof(result->name),
        &adopted);
    if( result->status != EDITOR_ALLOC_OK )
        return result->status;

    /* The id line. */
    if( request->target == EDITOR_ALLOC_LEDGER )
    {
        Editor_AllocDocKey(state->ns, "alloc", key, sizeof(key));
        length = Editor_AllocLedgerAppend(
            state->ns, &request->id_file, result->id, result->name, bytes, sizeof(bytes));
    }
    else
    {
        char line[EDITOR_CMD_NAME_MAX + 16];

        Editor_AllocDocKey(state->ns, "index", key, sizeof(key));
        snprintf(line, sizeof(line), "%d=%s", result->id, result->name);
        length = line_append(&request->id_file, line, bytes, sizeof(bytes));
    }
    assert(length > 0);
    written = Editor_AllocEmitAppend(key, &request->id_file, bytes, length, out + count, max - count);
    if( written < 0 )
        goto no_room;
    count += written;

    /* Membership, where it is not already stated. */
    for( int half = 0; half < 2; half++ )
    {
        int wanted = half == 0 ? request->member_client : request->member_server;
        const struct Editor_AllocText* file =
            half == 0 ? &request->client_file : &request->server_file;

        if( !wanted || names_line(file, result->name) )
            continue;
        Editor_AllocDocKey(state->ns, half == 0 ? "client" : "server", key, sizeof(key));
        length = line_append(file, result->name, bytes, sizeof(bytes));
        written = Editor_AllocEmitAppend(key, file, bytes, length, out + count, max - count);
        if( written < 0 )
            goto no_room;
        count += written;
    }

    /* The record itself. */
    if( request->add_record )
    {
        if( count == max )
            goto no_room;
        snprintf(key, sizeof(key), "%s:%s", state->ns, result->name);
        Editor_CmdInit(&out[count], EDITOR_DOC_CFG, key, EDITOR_CMD_CFG_BLOCK);
        out[count].has_after = 1;
        snprintf(out[count].u.cfg_block.name, sizeof(out[count].u.cfg_block.name), "%s", result->name);
        count++;
    }

    record(state, request->target, result->id, result->name, adopted);
    result->command_count = count;
    return EDITOR_ALLOC_OK;

no_room:
    result->status = EDITOR_ALLOC_NO_ROOM;
    result->command_count = 0;
    return EDITOR_ALLOC_NO_ROOM;
}

/* ---- copying a record ---------------------------------------------------------- */

int
Editor_AllocCopyCommandBound(const struct Editor_AllocCopyRequest* request)
{
    int lines = 1;

    assert(request);
    assert(request->source_text || request->source_length == 0);
    for( size_t i = 0; i < request->source_length; i++ )
        lines += request->source_text[i] == '\n';
    /* id line + two membership lines, each at most a few splices of a
     * header-sized append, plus the block add. */
    return lines + 3 * 8 + 1;
}

/** "key=value\n" as a TEXT_SPLICE at `offset` of the new record. */
static void
copy_splice_line(
    struct Editor_Cmd* out,
    const char* doc_key,
    size_t offset,
    const char* line,
    size_t length)
{
    Editor_CmdInit(out, EDITOR_DOC_CFG, doc_key, EDITOR_CMD_TEXT_SPLICE);
    out->has_before = 0;
    out->has_after = 1;
    out->u.text_splice.offset = (uint32_t)offset;
    out->u.text_splice.before_len = 0;
    out->u.text_splice.after_len = (uint32_t)(length + 1);
    memcpy(out->u.text_splice.text, line, length);
    out->u.text_splice.text[length] = '\n';
}

/**
 * Every FIELD (and OTHER) line of `source` below its header, as commands on
 * the CFG document `doc_key`, whose block is "[<name>]\n" and nothing else
 * when they apply: a CFG_FIELD insert positioned at its line, or a
 * TEXT_SPLICE of the whole line for one CFG_FIELD cannot carry. `out` has
 * room for one command per line of the source (asserted). Returns the
 * commands written.
 */
static int
copy_block_lines(
    const struct Editor_CfgBlock* source,
    int header,
    const char* doc_key,
    const char* name,
    struct Editor_Cmd* out,
    int max)
{
    int count = 0;
    int line_index = 1;
    /* What CFG_BLOCK add leaves: "[name]\n". */
    size_t record_length = strlen(name) + 3;

    assert(header >= 0);
    (void)max; /* read by the assert below only */
    for( int i = header + 1; i < source->line_count; i++ )
    {
        const struct Editor_CfgLine* line = &source->lines[i];
        const char* text = source->text + line->start;
        char key[EDITOR_CMD_NAME_MAX];
        char spec[EDITOR_CMD_NAME_MAX];
        size_t value_length = line->kind == EDITOR_CFG_LINE_FIELD ? line->length - line->part - 1 : 0;
        int occurrence = 0;
        int spec_ok = 0;

        if( line->kind != EDITOR_CFG_LINE_FIELD && line->kind != EDITOR_CFG_LINE_OTHER )
            continue;
        assert(count < max);
        if( line->kind == EDITOR_CFG_LINE_FIELD && line->part < sizeof(key) )
        {
            memcpy(key, text, line->part);
            key[line->part] = '\0';
            /* The occurrence among the lines copied so far. */
            for( int j = header + 1; j < i; j++ )
                if( source->lines[j].kind == EDITOR_CFG_LINE_FIELD && source->lines[j].part == line->part
                    && memcmp(source->text + source->lines[j].start, key, line->part) == 0 )
                    occurrence++;
            {
                char parsed[EDITOR_CMD_NAME_MAX];
                int parsed_occurrence;
                int parsed_line;

                /* A key the spec grammar cannot name ('#', '@', ...) goes
                 * across as a splice instead. */
                spec_ok = Editor_CfgFieldSpecFormat(key, occurrence, line_index, spec, sizeof(spec))
                          && Editor_CfgFieldSpecParse(
                              spec, parsed, sizeof(parsed), &parsed_occurrence, &parsed_line)
                          && strcmp(parsed, key) == 0;
            }
        }
        if( spec_ok && value_length > 0 && value_length < EDITOR_CMD_VALUE_MAX )
        {
            Editor_CmdInit(&out[count], EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_FIELD);
            out[count].has_before = 0;
            out[count].has_after = 1;
            snprintf(out[count].u.cfg_field.key, sizeof(out[count].u.cfg_field.key), "%s", spec);
            memcpy(out[count].u.cfg_field.after, text + line->part + 1, value_length);
            out[count].u.cfg_field.after[value_length] = '\0';
        }
        else
            copy_splice_line(&out[count], doc_key, record_length, text, line->length);
        count++;
        record_length += line->length + 1;
        line_index++;
    }
    return count;
}

enum Editor_AllocStatus
Editor_AllocCopyPlan(
    struct Editor_AllocNs* state,
    const struct Editor_AllocCopyRequest* request,
    struct Editor_Cmd* out,
    int max,
    struct Editor_AllocResult* result)
{
    struct Editor_AllocRequest alloc;
    struct Editor_CfgBlock source;
    char doc_key[EDITOR_DOC_KEY_MAX];
    enum Editor_AllocStatus status;
    int header;
    int count;

    assert(state);
    assert(request);
    assert(request->ns);
    assert(request->source_name);
    assert(request->new_name);
    assert(request->source_text || request->source_length == 0);
    assert(out || max == 0);
    assert(result);

    memset(result, 0, sizeof(*result));
    result->id = -1;
    Editor_CfgBlockInit(&source);
    Editor_CfgBlockSet(&source, request->source_text ? request->source_text : "", request->source_length);
    header = Editor_CfgBlockHeader(&source);
    if( header < 0 || !Editor_CfgBlockNameIs(&source, request->source_name) )
    {
        Editor_CfgBlockFree(&source);
        result->status = EDITOR_ALLOC_REFUSED;
        return EDITOR_ALLOC_REFUSED;
    }
    /* Room is decided before anything is coined: a plan that cannot be
     * emitted must leave the state as it was. */
    status = EDITOR_ALLOC_OK;
    if( max < Editor_AllocCopyCommandBound(request) )
        status = EDITOR_ALLOC_NO_ROOM;
    for( int i = header + 1; i < source.line_count; i++ )
        if( source.lines[i].length + 1 > EDITOR_CMD_SPLICE_MAX )
            status = EDITOR_ALLOC_NO_ROOM;
    if( status != EDITOR_ALLOC_OK )
    {
        Editor_CfgBlockFree(&source);
        result->status = status;
        return status;
    }

    memset(&alloc, 0, sizeof(alloc));
    alloc.ns = request->ns;
    alloc.name = request->new_name;
    alloc.target = request->target;
    alloc.member_client = request->member_client;
    alloc.member_server = request->member_server;
    alloc.add_record = 1;
    alloc.id_file = request->id_file;
    alloc.client_file = request->client_file;
    alloc.server_file = request->server_file;
    status = Editor_AllocPlan(state, &alloc, out, max, result);
    if( status != EDITOR_ALLOC_OK )
    {
        Editor_CfgBlockFree(&source);
        return status;
    }
    count = result->command_count;
    snprintf(doc_key, sizeof(doc_key), "%s:%s", request->ns, result->name);
    count += copy_block_lines(&source, header, doc_key, result->name, out + count, max - count);
    Editor_CfgBlockFree(&source);
    result->command_count = count;
    return EDITOR_ALLOC_OK;
}

/* ---- the server half of a copy -------------------------------------------------- */

int
Editor_AllocCopyOverlayPath(
    const char* ns,
    const char* new_name,
    char* out,
    size_t out_size)
{
    int written;

    assert(ns);
    assert(new_name);
    assert(out);
    written = snprintf(out, out_size, "edited/%s/configs/%s.%s", new_name, new_name, ns);
    return written > 0 && (size_t)written < out_size;
}

int
Editor_AllocCopyOverlayBound(
    const struct Editor_AllocOverlayLayer* layers,
    int count)
{
    int lines = 1;

    assert(layers || count == 0);
    for( int l = 0; l < count; l++ )
    {
        assert(layers[l].text || layers[l].length == 0);
        lines++;
        for( size_t i = 0; i < layers[l].length; i++ )
            lines += layers[l].text[i] == '\n';
    }
    /* The CFG_BLOCK add, then at most one command per line of every layer. */
    return lines + 1;
}

int
Editor_AllocCopyOverlayPlan(
    const char* ns,
    const char* source_name,
    const char* new_name,
    const struct Editor_AllocOverlayLayer* layers,
    int count,
    struct Editor_Cmd* out,
    int max)
{
    struct Editor_CfgRecord* record;
    struct Editor_CfgBlock merged;
    char overlay[EDITOR_DOC_KEY_MAX];
    char doc_key[EDITOR_DOC_KEY_MAX];
    char* text;
    size_t capacity = 16;
    size_t used = 0;
    int stated = 0;
    int emitted = 0;

    assert(ns);
    assert(source_name);
    assert(new_name);
    assert(layers || count == 0);
    assert(out || max == 0);

    if( count == 0 )
        return 0;
    if( !Editor_AllocCopyOverlayPath(ns, new_name, overlay, sizeof(overlay))
        || !Editor_CfgOverlayKey(doc_key, ns, new_name, overlay) )
        return -1;
    if( max < Editor_AllocCopyOverlayBound(layers, count) )
        return -1;

    /* The layers resolved the way the pack merges them (rank, then path; the
     * last statement of a field wins, a param by its name): what the record
     * panel shows as the winning rows is what the server reads. */
    record = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*record));
    assert(record);
    Editor_CfgRecordInit(record, ns, source_name);
    for( int l = 0; l < count; l++ )
    {
        assert(layers[l].overlay);
        assert(layers[l].overlay[0]);
        capacity += layers[l].length + 2;
        if( !Editor_CfgRecordAddLayer(record, layers[l].overlay, layers[l].text, layers[l].length) )
        {
            Editor_CfgRecordFree(record);
            ToriMem_Free(ToriMem_Editor(), record);
            return -1;
        }
    }
    Editor_CfgRecordResolve(record, NULL, 0);

    /* One block of the winning statements: "[source]", then key=value. No
     * line is longer than the layer line it came from. */
    capacity += strlen(source_name) + 4;
    text = ToriMem_Alloc(ToriMem_Editor(), capacity);
    assert(text);
    used += (size_t)snprintf(text + used, capacity - used, "[%s]\n", source_name);
    for( int r = 0; r < record->row_count; r++ )
    {
        const struct Editor_CfgRecordRow* row = &record->rows[r];
        int written;

        /* A layer index of 0 is a server overlay here: no cache half was
         * added. */
        if( row->layer < 0 )
            continue;
        if( row->sub[0] )
            written = snprintf(text + used, capacity - used, "%s=%s,%s\n", row->key, row->sub, row->value);
        else
            written = snprintf(text + used, capacity - used, "%s=%s\n", row->key, row->value);
        assert(written > 0);
        assert((size_t)written < capacity - used);
        used += (size_t)written;
        stated++;
    }
    Editor_CfgRecordFree(record);
    ToriMem_Free(ToriMem_Editor(), record);
    if( stated == 0 )
    {
        ToriMem_Free(ToriMem_Editor(), text);
        return 0;
    }

    Editor_CfgBlockInit(&merged);
    Editor_CfgBlockSet(&merged, text, used);
    ToriMem_Free(ToriMem_Editor(), text);
    Editor_CmdInit(&out[emitted], EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_BLOCK);
    out[emitted].has_after = 1;
    snprintf(out[emitted].u.cfg_block.name, sizeof(out[emitted].u.cfg_block.name), "%s", new_name);
    emitted++;
    emitted += copy_block_lines(&merged, Editor_CfgBlockHeader(&merged), doc_key, new_name, out + emitted, max - emitted);
    Editor_CfgBlockFree(&merged);
    return emitted;
}
