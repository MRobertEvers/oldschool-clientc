#include "cachepack.h"
#include "rscache_register.h"

#include <assert.h>
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

const struct CP_KeySpec*
cp_key_spec_in(
    const struct CP_KeySpec* table,
    const char* key)
{
    assert(table);
    assert(key);
    for( const struct CP_KeySpec* spec = table; spec->key; spec++ )
    {
        if( spec_matches(spec, key) )
            return spec;
    }
    return NULL;
}

const struct CP_KeySpec*
cp_key_spec(
    const struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const char* key)
{
    assert(ctx);
    assert(type);
    assert(type->keys);
    assert(key);
    for( const struct CP_KeySpec* spec = type->keys; spec->key; spec++ )
    {
        if( spec_matches(spec, key) && spec_applies(ctx, spec) )
            return spec;
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
    int ok = 1;

    for( const struct CP_KeySpec* spec = type->keys; spec->key; spec++ )
    {
        int matched = 0;
        int markers = 0;
        const char* marker = NULL;

        if( !spec_applies(ctx, spec) )
            continue;
        for( int i = 0; i < line_count; i++ )
        {
            char key[128];
            const char* value;

            get(block, i, key, sizeof(key), &value);
            if( !spec_matches(spec, key) )
                continue;
            matched++;
            if( cp_value_is_default(value) || cp_value_is_empty(value) )
            {
                markers++;
                marker = value;
            }
        }

        if( matched == 0 )
        {
            fprintf(stderr,
                    "cachepack: %s [%s]: no `%s` line — every key is stated, `%s=default` when "
                    "the record does not set it\n",
                    type->name, where, spec->key, spec->key);
            ok = 0;
            continue;
        }
        if( markers > 0 && matched > 1 )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s=%s` beside %d other `%s` line(s) — a marker "
                            "is the only line for its key\n",
                    type->name, where, spec->key, marker, matched - 1, spec->key);
            ok = 0;
        }
        if( marker && cp_value_is_empty(marker) &&
            !(spec->flags & (CP_KEY_LIST | CP_KEY_INDEXED)) )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s=empty` on a key that is not a list\n",
                    type->name, where, spec->key);
            ok = 0;
        }
        if( matched > 1 && !(spec->flags & (CP_KEY_LIST | CP_KEY_INDEXED)) )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s` stated %d times on a key that holds one "
                            "value\n",
                    type->name, where, spec->key, matched);
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
