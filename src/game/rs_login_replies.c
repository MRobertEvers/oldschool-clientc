#include "game/rs_login_replies.h"

#include "revconfig/revconfig_load.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
RS_LoginReplies_Init(struct RS_LoginReplyTable* table)
{
    assert(table);
    memset(table, 0, sizeof(*table));
}

void
RS_LoginReplies_Free(struct RS_LoginReplyTable* table)
{
    if( !table )
        return;
    free(table->entries);
    free(table->strings);
    memset(table, 0, sizeof(*table));
}

/* The entry for `code`, or NULL. Exact match only; the default fallback is
 * RS_LoginReplies_Get's, so that a profile CAN state a blank answer for one
 * code without the default speaking over it. */
static struct RS_LoginReply*
find_exact(
    struct RS_LoginReplyTable const* table,
    int code)
{
    for( int i = 0; i < table->count; i++ )
    {
        if( table->entries[i].code == code )
            return &table->entries[i];
    }
    return NULL;
}

void
RS_LoginReplies_AddFromItems(
    struct RS_LoginReplyTable* table,
    struct RevConfigItemBuffer const* items)
{
    assert(table);
    assert(items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigLoginReplyItem const* src;
        struct RS_LoginReply* dst;

        if( items->items[i].kind == RCITEM_STRING )
        {
            struct RevConfigStringItem const* str = &items->items[i].u.string;
            struct RS_LoginString* slot = NULL;

            for( int k = 0; k < table->string_count; k++ )
            {
                if( strcmp(table->strings[k].name, str->name) == 0 )
                    slot = &table->strings[k];
            }
            if( !slot )
            {
                if( table->string_count == table->string_capacity )
                {
                    int cap = table->string_capacity == 0 ? 16 : table->string_capacity * 2;
                    struct RS_LoginString* grown =
                        realloc(table->strings, (size_t)cap * sizeof(*grown));
                    assert(grown);
                    table->strings = grown;
                    table->string_capacity = cap;
                }
                slot = &table->strings[table->string_count++];
            }
            memset(slot, 0, sizeof(*slot));
            strncpy(slot->name, str->name, sizeof(slot->name) - 1);
            strncpy(slot->text, str->text, sizeof(slot->text) - 1);
            continue;
        }
        if( items->items[i].kind != RCITEM_LOGIN_REPLY )
            continue;
        src = &items->items[i].u.login_reply;

        /* A later source restating a code replaces it rather than shadowing
         * it, so a profile can override one line of a shared table. */
        dst = find_exact(table, src->code);
        if( !dst )
        {
            if( table->count == table->capacity )
            {
                int cap = table->capacity == 0 ? 16 : table->capacity * 2;
                struct RS_LoginReply* grown =
                    realloc(table->entries, (size_t)cap * sizeof(*grown));
                assert(grown);
                table->entries = grown;
                table->capacity = cap;
            }
            dst = &table->entries[table->count++];
        }

        memset(dst, 0, sizeof(*dst));
        dst->code = src->code;
        dst->screen = src->screen;
        for( int line = 0; line < RS_LOGIN_REPLY_LINES; line++ )
        {
            strncpy(dst->line[line], src->line[line], sizeof(dst->line[line]) - 1);
            dst->line[line][sizeof(dst->line[line]) - 1] = '\0';
        }
    }
}

/** Parse one source into `table`. `prefix` NULL/"" is the unprefixed dialect. */
static void
load_one(
    struct RS_LoginReplyTable* table,
    char const* path,
    char const* prefix)
{
    struct RevConfigBuffer* fields;
    struct RevConfigItemBuffer* items;

    assert(table);
    if( !path || path[0] == '\0' )
        return;

    fields = revconfig_buffer_new(256);
    assert(fields);
    items = revconfig_item_buffer_new(64);
    assert(items);

    revconfig_load_fields_from_ini_prefixed(path, prefix, fields);
    revconfig_items_build(fields, items);
    RS_LoginReplies_AddFromItems(table, items);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

void
RS_LoginReplies_LoadSources(
    struct RS_LoginReplyTable* table,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini)
{
    assert(table);
    load_one(table, ui_ini, NULL);
    load_one(table, cache_ini, NULL);
    load_one(table, inline_ini, "revconfig");
}

char const*
RS_LoginReplies_String(
    struct RS_LoginReplyTable const* table,
    char const* name)
{
    assert(table);
    assert(name);
    for( int i = 0; i < table->string_count; i++ )
    {
        if( strcmp(table->strings[i].name, name) == 0 )
            return table->strings[i].text;
    }
    return NULL;
}

struct RS_LoginReply const*
RS_LoginReplies_Get(
    struct RS_LoginReplyTable const* table,
    int code)
{
    struct RS_LoginReply const* found;

    assert(table);

    found = find_exact(table, code);
    if( found )
        return found;
    return find_exact(table, REVCONFIG_LOGIN_REPLY_CODE_DEFAULT);
}
