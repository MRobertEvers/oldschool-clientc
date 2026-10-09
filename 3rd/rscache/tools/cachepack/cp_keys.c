#include "cachepack.h"
#include "rscache_register.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The key tables, enforced.
 *
 * A config block states every key of its type. That is the whole defence
 * against the July 2026 loss, so it is checked at both ends: the unpacker's
 * output before it is written (an emitter that forgot a key fails the unpack,
 * not a woodcutting animation three months later), and every client record the
 * packer is about to encode (a hand-written new record, or a rank-0 file someone
 * truncated, fails the pack).
 *
 * An INDEXED key is a family spelled with a number after the stem -- spotanim's
 * `recol1s`/`recol1d` .. pairs are one opcode with a count -- so its lines match
 * the stem followed by a digit, and its markers are written on the bare stem
 * (`recol=default`).
 */

static int
spec_applies(
    const struct CP_Ctx* ctx,
    const struct CP_KeySpec* spec)
{
    return !spec->applies || spec->applies(ctx);
}

/** Does a line spelled `line_key` belong to `spec`? */
static int
spec_matches(
    const struct CP_KeySpec* spec,
    const char* line_key)
{
    size_t stem = strlen(spec->key);

    if( strcmp(spec->key, line_key) == 0 )
        return 1;
    if( !(spec->flags & CP_KEY_INDEXED) )
        return 0;
    return strncmp(spec->key, line_key, stem) == 0 && line_key[stem] >= '0' &&
           line_key[stem] <= '9';
}

/*
 * Which specs of a table a line key can match, remembered per (table, key).
 *
 * A key table is a static array and a line key is one of a few hundred
 * spellings, but the lookup is asked once per line of every config block — the
 * server pack asks it millions of times, and the linear scan with a strlen and
 * a strcmp per row was a third of a two-minute build. The candidates are the
 * specs `spec_matches` accepts, in table order; `applies` still runs on every
 * lookup, because it reads the profile rather than the key.
 */
struct KeyCandidates
{
    const struct CP_KeySpec* table;
    char* key;
    uint32_t hash;
    int count;
    const struct CP_KeySpec** specs;
};

static struct KeyCandidates* g_key_cache;
static uint32_t g_key_cache_capacity;
static uint32_t g_key_cache_used;

static uint32_t
key_cache_hash(
    const struct CP_KeySpec* table,
    const char* key)
{
    uint32_t hash = 2166136261u ^ (uint32_t)((uintptr_t)table >> 4);

    for( ; *key; key++ )
        hash = (hash ^ (uint8_t)*key) * 16777619u;
    return hash;
}

static const struct KeyCandidates*
key_candidates(
    const struct CP_KeySpec* table,
    const char* key)
{
    uint32_t hash = key_cache_hash(table, key);
    uint32_t slot;
    struct KeyCandidates* entry;
    int count = 0;

    if( (g_key_cache_used + 1) * 2 > g_key_cache_capacity )
    {
        uint32_t old_capacity = g_key_cache_capacity;
        struct KeyCandidates* old = g_key_cache;

        g_key_cache_capacity = old_capacity ? old_capacity * 2 : 1024;
        g_key_cache = (struct KeyCandidates*)calloc(g_key_cache_capacity, sizeof(*g_key_cache));
        assert(g_key_cache);
        for( uint32_t i = 0; i < old_capacity; i++ )
        {
            if( !old[i].key )
                continue;
            slot = old[i].hash & (g_key_cache_capacity - 1);
            while( g_key_cache[slot].key )
                slot = (slot + 1) & (g_key_cache_capacity - 1);
            g_key_cache[slot] = old[i];
        }
        free(old);
    }
    slot = hash & (g_key_cache_capacity - 1);
    while( g_key_cache[slot].key )
    {
        if( g_key_cache[slot].hash == hash && g_key_cache[slot].table == table &&
            strcmp(g_key_cache[slot].key, key) == 0 )
            return &g_key_cache[slot];
        slot = (slot + 1) & (g_key_cache_capacity - 1);
    }
    entry = &g_key_cache[slot];
    entry->table = table;
    entry->key = strdup(key);
    assert(entry->key);
    entry->hash = hash;
    for( const struct CP_KeySpec* spec = table; spec->key; spec++ )
        count += spec_matches(spec, key);
    entry->specs = (const struct CP_KeySpec**)malloc((size_t)(count ? count : 1) * sizeof(*entry->specs));
    assert(entry->specs);
    for( const struct CP_KeySpec* spec = table; spec->key; spec++ )
    {
        if( spec_matches(spec, key) )
            entry->specs[entry->count++] = spec;
    }
    g_key_cache_used++;
    return entry;
}

const struct CP_KeySpec*
cp_key_spec_in(
    const struct CP_KeySpec* table,
    const char* key)
{
    const struct KeyCandidates* candidates;

    assert(table);
    assert(key);
    candidates = key_candidates(table, key);
    return candidates->count ? candidates->specs[0] : NULL;
}

const struct CP_KeySpec*
cp_key_spec(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const char* key)
{
    const struct KeyCandidates* candidates;

    assert(ctx);
    assert(type);
    assert(type->keys);
    assert(key);
    candidates = key_candidates(type->keys, key);
    for( int i = 0; i < candidates->count; i++ )
    {
        if( spec_applies(ctx, candidates->specs[i]) )
            return candidates->specs[i];
    }
    return NULL;
}

/*
 * One pass for both shapes of block: `get(i, &key, &value)` yields line i. A
 * block is small (tens of lines), so the per-key rescan is cheaper than any
 * index would be to build.
 */
typedef void (*line_get_fn)(
    const void* block,
    int index,
    char* key_out,
    size_t key_size,
    const char** value_out);

/**
 * Is register field `field` a key of the type that the client table does not
 * already cover? Those are the server's keys: `hitpoints`, `respawnrate`, a
 * varp's `scope`. A `text = param` field is an entry of the param map, which the
 * client table's `param` key covers; a `client = native` field is a client key.
 */
static int
server_key(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_RegisterField* field)
{
    return field->text != RSCACHE_REGISTER_TEXT_PARAM &&
           field->client != RSCACHE_REGISTER_CLIENT_NATIVE && !cp_key_spec(ctx, type, field->name);
}

/** A list field: stated by its bare-key marker or by any number of lines. */
static int
server_key_is_list(const struct RSCache_RegisterField* field)
{
    return field->text == RSCACHE_REGISTER_TEXT_INDEXED || field->text == RSCACHE_REGISTER_TEXT_LIST;
}

/** Does a line spelled `key` state `field` (`patrol3` states an indexed `patrol`)? */
static int
server_key_line(
    const struct RSCache_RegisterField* field,
    const char* key)
{
    size_t stem = strlen(field->name);
    const char* digit;

    if( strcmp(key, field->name) == 0 )
        return 1;
    if( field->text != RSCACHE_REGISTER_TEXT_INDEXED || strncmp(key, field->name, stem) != 0 )
        return 0;
    digit = key + stem;
    if( *digit < '0' || *digit > '9' )
        return 0;
    while( *digit >= '0' && *digit <= '9' )
        digit++;
    return *digit == '\0';
}

static int
check_server_keys(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const char* where,
    const void* block,
    int line_count,
    line_get_fn get)
{
    int ok = 1;

    for( int f = 0; f < fields->count; f++ )
    {
        const struct RSCache_RegisterField* field = &fields->entries[f];
        int matched = 0;

        if( !server_key(ctx, type, field) )
            continue;
        for( int i = 0; i < line_count; i++ )
        {
            char key[128];
            const char* value;

            get(block, i, key, sizeof(key), &value);
            if( !server_key_line(field, key) )
                continue;
            matched++;
            if( cp_value_is_empty(value) && !server_key_is_list(field) )
            {
                fprintf(stderr, "cachepack: %s [%s]: `%s=empty` on a key that is not a list\n",
                        type->name, where, field->name);
                ok = 0;
            }
        }
        if( matched == 0 )
        {
            fprintf(stderr,
                    "cachepack: %s [%s]: no `%s` line — every key is stated, `%s=default` when "
                    "the record does not set it (fields/%s.ini)\n",
                    type->name, where, field->name, field->name, type->name);
            ok = 0;
        }
        else if( matched > 1 && !server_key_is_list(field) )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s` stated %d times on a key that holds one "
                            "value\n",
                    type->name, where, field->name, matched);
            ok = 0;
        }
    }
    return ok;
}

static int
check_block(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const char* where,
    const void* block,
    int line_count,
    line_get_fn get)
{
    enum
    {
        MAX_SPECS = 512
    };
    int matched[MAX_SPECS];
    int markers[MAX_SPECS];
    const char* marker[MAX_SPECS];
    int spec_count = 0;
    int ok = 1;

    /*
     * One pass over the lines, crediting each to the specs it matches, then the
     * per-spec verdicts in table order. It was a pass over the lines per spec —
     * a hundred specs times thirty lines times every record — and the verdicts
     * are the same: `marker` is still the last matching marker line.
     */
    for( const struct CP_KeySpec* spec = type->keys; spec->key; spec++ )
        spec_count++;
    assert(spec_count <= MAX_SPECS);
    memset(matched, 0, sizeof(int) * (size_t)spec_count);
    memset(markers, 0, sizeof(int) * (size_t)spec_count);
    memset(marker, 0, sizeof(const char*) * (size_t)spec_count);
    for( int i = 0; i < line_count; i++ )
    {
        char key[128];
        const char* value;
        const struct KeyCandidates* candidates;
        int is_marker;

        get(block, i, key, sizeof(key), &value);
        candidates = key_candidates(type->keys, key);
        is_marker = cp_value_is_default(value) || cp_value_is_empty(value);
        for( int c = 0; c < candidates->count; c++ )
        {
            int index = (int)(candidates->specs[c] - type->keys);

            matched[index]++;
            if( is_marker )
            {
                markers[index]++;
                marker[index] = value;
            }
        }
    }

    for( int index = 0; index < spec_count; index++ )
    {
        const struct CP_KeySpec* spec = &type->keys[index];

        if( !spec_applies(ctx, spec) )
            continue;
        if( matched[index] == 0 )
        {
            fprintf(stderr,
                    "cachepack: %s [%s]: no `%s` line — every key is stated, `%s=default` when "
                    "the record does not set it\n",
                    type->name, where, spec->key, spec->key);
            ok = 0;
            continue;
        }
        if( markers[index] > 0 && matched[index] > 1 )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s=%s` beside %d other `%s` line(s) — a marker "
                            "is the only line for its key\n",
                    type->name, where, spec->key, marker[index], matched[index] - 1, spec->key);
            ok = 0;
        }
        if( marker[index] && cp_value_is_empty(marker[index]) &&
            !(spec->flags & (CP_KEY_LIST | CP_KEY_INDEXED)) )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s=empty` on a key that is not a list\n",
                    type->name, where, spec->key);
            ok = 0;
        }
        if( matched[index] > 1 && !(spec->flags & (CP_KEY_LIST | CP_KEY_INDEXED)) )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s` stated %d times on a key that holds one "
                            "value\n",
                    type->name, where, spec->key, matched[index]);
            ok = 0;
        }
    }
    return ok;
}

static void
lines_get(
    const void* block,
    int index,
    char* key_out,
    size_t key_size,
    const char** value_out)
{
    const struct CP_Lines* lines = block;
    const char* line = lines->lines[index];
    const char* eq = strchr(line, '=');
    size_t n;

    assert(eq);
    n = (size_t)(eq - line);
    assert(n < key_size);
    memcpy(key_out, line, n);
    key_out[n] = '\0';
    *value_out = eq + 1;
}

static void
config_get(
    const void* block,
    int index,
    char* key_out,
    size_t key_size,
    const char** value_out)
{
    const struct CP_Config* config = block;
    size_t n = strlen(config->lines[index].key);

    assert(n < key_size);
    memcpy(key_out, config->lines[index].key, n + 1);
    *value_out = config->lines[index].value;
}

int
cp_keys_check_lines(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const char* record_name,
    const struct CP_Lines* lines)
{
    assert(ctx);
    assert(type);
    assert(type->keys);
    assert(record_name);
    assert(lines);
    return check_block(ctx, type, record_name, lines, lines->count, lines_get);
}

int
cp_keys_check_config(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct CP_Config* config,
    const char* where)
{
    assert(ctx);
    assert(type);
    assert(type->keys);
    assert(config);
    assert(where);
    return check_block(ctx, type, where, config, config->count, config_get);
}

int
cp_keys_check_lines_full(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const char* record_name,
    const struct CP_Lines* lines)
{
    int ok;

    assert(fields);
    ok = cp_keys_check_lines(ctx, type, record_name, lines);
    return check_server_keys(ctx, type, fields, record_name, lines, lines->count, lines_get) &&
           ok;
}

int
cp_keys_check_config_full(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const struct CP_Config* config,
    const char* where)
{
    int ok;

    assert(fields);
    ok = cp_keys_check_config(ctx, type, config, where);
    return check_server_keys(ctx, type, fields, where, config, config->count, config_get) &&
           ok;
}

void
cp_keys_add_server_defaults(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    struct CP_Lines* lines)
{
    assert(ctx);
    assert(type);
    assert(fields);
    assert(lines);
    for( int f = 0; f < fields->count; f++ )
    {
        if( server_key(ctx, type, &fields->entries[f]) )
            cp_lines_add_default(lines, fields->entries[f].name);
    }
}

int
cp_keys_is_server_key(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_RegisterField* field)
{
    assert(ctx);
    assert(type);
    assert(field);
    return server_key(ctx, type, field);
}

const struct RSCache_Register*
cp_ctx_fields(
    struct CP_Ctx* ctx,
    enum CP_TypeId type)
{
    assert(ctx);
    assert(type >= 0);
    assert(type < CP_TYPE_COUNT);
    if( !ctx->register_fields[type] )
    {
        ctx->register_fields[type] = malloc(sizeof(struct RSCache_Register));
        assert(ctx->register_fields[type]);
        /* No file is a type the register says nothing about: an empty table. */
        RSCache_RegisterLoad(ctx->register_fields[type], ctx->srcdir, cp_type(type)->name);
    }
    return ctx->register_fields[type];
}

void
cp_ctx_fields_free(struct CP_Ctx* ctx)
{
    if( !ctx )
        return;
    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        free(ctx->register_fields[t]);
        ctx->register_fields[t] = NULL;
    }
}

int
cp_keys_missing(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct RSCache_Register* fields,
    const struct CP_Config* config,
    void (*missing)(void* user, const char* key, const char* marker),
    void* user)
{
    int count = 0;

    assert(ctx);
    assert(type);
    assert(fields);
    assert(config);
    assert(missing);
    for( const struct CP_KeySpec* spec = type->keys; spec->key; spec++ )
    {
        int matched = 0;

        if( !spec_applies(ctx, spec) )
            continue;
        for( int i = 0; i < config->count && !matched; i++ )
            matched = spec_matches(spec, config->lines[i].key);
        if( !matched )
        {
            int sibling_stated = 0;

            for( int i = 0; spec->sibling && i < config->count && !sibling_stated; i++ )
                sibling_stated = strcmp(config->lines[i].key, spec->sibling) == 0;
            missing(user, spec->key, sibling_stated ? CP_VALUE_EMPTY : CP_VALUE_DEFAULT);
            count++;
        }
    }
    for( int f = 0; f < fields->count; f++ )
    {
        const struct RSCache_RegisterField* field = &fields->entries[f];
        int matched = 0;

        if( !server_key(ctx, type, field) )
            continue;
        for( int i = 0; i < config->count && !matched; i++ )
            matched = server_key_line(field, config->lines[i].key);
        if( !matched )
        {
            missing(user, field->name, CP_VALUE_DEFAULT);
            count++;
        }
    }
    return count;
}

/*
 * RSCache_RegisterFind / RSCache_RegisterFindLine, memoised for this tool.
 *
 * Both are linear over the register's rows (and FindLine runs a strlen per row
 * on a miss), and the merge asks them once per line of every record: a quarter
 * of a server pack build. The library is shared with the server, so the memo
 * lives here. It is keyed on the register's address and its type and row count,
 * and a remembered hit is re-checked by name before it is trusted, so a
 * register reloaded into the same storage cannot hand back a stale row.
 */
struct RegisterMemo
{
    const struct RSCache_Register* reg;
    int count;
    int line;
    uint32_t hash;
    char* key;
    int index; /* -1: not in the register */
};

static struct RegisterMemo* g_register_memo;
static uint32_t g_register_memo_capacity;
static uint32_t g_register_memo_used;

static const struct RSCache_RegisterField*
register_memo_find(
    const struct RSCache_Register* reg,
    const char* key,
    int line)
{
    uint32_t hash = 2166136261u ^ (uint32_t)((uintptr_t)reg >> 4) ^ (uint32_t)line;
    uint32_t slot;
    const struct RSCache_RegisterField* field;

    assert(reg);
    assert(key);
    for( const char* c = reg->type; *c; c++ )
        hash = (hash ^ (uint8_t)*c) * 16777619u;
    for( const char* c = key; *c; c++ )
        hash = (hash ^ (uint8_t)*c) * 16777619u;
    if( (g_register_memo_used + 1) * 2 > g_register_memo_capacity )
    {
        uint32_t old_capacity = g_register_memo_capacity;
        struct RegisterMemo* old = g_register_memo;

        g_register_memo_capacity = old_capacity ? old_capacity * 2 : 4096;
        g_register_memo =
            (struct RegisterMemo*)calloc(g_register_memo_capacity, sizeof(*g_register_memo));
        assert(g_register_memo);
        for( uint32_t i = 0; i < old_capacity; i++ )
        {
            if( !old[i].key )
                continue;
            slot = old[i].hash & (g_register_memo_capacity - 1);
            while( g_register_memo[slot].key )
                slot = (slot + 1) & (g_register_memo_capacity - 1);
            g_register_memo[slot] = old[i];
        }
        free(old);
    }
    slot = hash & (g_register_memo_capacity - 1);
    while( g_register_memo[slot].key )
    {
        struct RegisterMemo* memo = &g_register_memo[slot];

        if( memo->hash == hash && memo->reg == reg && memo->count == reg->count &&
            memo->line == line && strcmp(memo->key, key) == 0 )
        {
            if( memo->index < 0 )
                return NULL;
            /* Exact rows are re-checked by name; stem rows by the library. */
            if( memo->index < reg->count &&
                (strcmp(reg->entries[memo->index].name, key) == 0 ||
                 (line && RSCache_RegisterFindLine(reg, key) == &reg->entries[memo->index])) )
                return &reg->entries[memo->index];
            field = line ? RSCache_RegisterFindLine(reg, key) : RSCache_RegisterFind(reg, key);
            memo->index = field ? (int)(field - reg->entries) : -1;
            return field;
        }
        slot = (slot + 1) & (g_register_memo_capacity - 1);
    }
    field = line ? RSCache_RegisterFindLine(reg, key) : RSCache_RegisterFind(reg, key);
    g_register_memo[slot].reg = reg;
    g_register_memo[slot].count = reg->count;
    g_register_memo[slot].line = line;
    g_register_memo[slot].hash = hash;
    g_register_memo[slot].key = strdup(key);
    assert(g_register_memo[slot].key);
    g_register_memo[slot].index = field ? (int)(field - reg->entries) : -1;
    g_register_memo_used++;
    return field;
}

const struct RSCache_RegisterField*
cp_register_find(
    const struct RSCache_Register* reg,
    const char* name)
{
    return register_memo_find(reg, name, 0);
}

const struct RSCache_RegisterField*
cp_register_find_line(
    const struct RSCache_Register* reg,
    const char* key)
{
    return register_memo_find(reg, key, 1);
}
