#include "game/rs_preload.h"

#include "revconfig/revconfig_load.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void
RS_Preload_Init(struct RS_PreloadTable* table)
{
    assert(table);
    memset(table, 0, sizeof(*table));
}

void
RS_Preload_Free(struct RS_PreloadTable* table)
{
    /* A deallocator takes NULL: see the project's own rule for why this one
     * guard is not a silent-failure guard. */
    if( !table )
        return;
    free(table->steps);
    table->steps = NULL;
    table->count = 0;
    table->capacity = 0;
}

static enum RS_PreloadKind
kind_from_name(char const* name)
{
    assert(name);
    if( strcmp(name, "crc") == 0 )
        return RS_PRELOAD_KIND_CRC;
    if( strcmp(name, "jagfile") == 0 )
        return RS_PRELOAD_KIND_JAGFILE;
    if( strcmp(name, "index") == 0 )
        return RS_PRELOAD_KIND_INDEX;
    if( strcmp(name, "ondemand") == 0 )
        return RS_PRELOAD_KIND_ONDEMAND;
    if( strcmp(name, "unpack") == 0 )
        return RS_PRELOAD_KIND_UNPACK;
    if( strcmp(name, "prepare") == 0 )
        return RS_PRELOAD_KIND_PREPARE;
    return RS_PRELOAD_KIND_UNKNOWN;
}

void
RS_Preload_AddFromItems(
    struct RS_PreloadTable* table,
    struct RevConfigItemBuffer const* items)
{
    assert(table);
    assert(items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        struct RevConfigPreloadItem const* src = &item->u.preload;
        struct RS_PreloadStep* dst = NULL;

        if( item->kind != RCITEM_PRELOAD )
            continue;

        /* A later source restates a step rather than adding a second one with
         * the same name: an inline `[preload:]` in a manifest is how a world
         * overrides one step of its revision's list. */
        for( int at = 0; at < table->count; at++ )
        {
            if( strcmp(table->steps[at].name, src->name) == 0 )
            {
                dst = &table->steps[at];
                break;
            }
        }

        if( !dst )
        {
            if( table->count == table->capacity )
            {
                int cap = table->capacity == 0 ? 16 : table->capacity * 2;
                struct RS_PreloadStep* grown =
                    realloc(table->steps, (size_t)cap * sizeof(*grown));
                assert(grown);
                table->steps = grown;
                table->capacity = cap;
            }
            dst = &table->steps[table->count++];
        }

        memset(dst, 0, sizeof(*dst));
        strncpy(dst->name, src->name, sizeof(dst->name) - 1);
        strncpy(dst->kind_name, src->kind, sizeof(dst->kind_name) - 1);
        strncpy(dst->archive, src->archive, sizeof(dst->archive) - 1);
        strncpy(dst->say, src->say, sizeof(dst->say) - 1);
        dst->kind = kind_from_name(dst->kind_name);
        dst->id = src->id;
        dst->percent = src->percent;
        dst->weight = src->weight;
        dst->render = src->render;
        dst->order = src->order;
    }
}

/** Parse one source into `table`. `prefix` NULL/"" is the unprefixed dialect. */
static void
load_one(
    struct RS_PreloadTable* table,
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
    RS_Preload_AddFromItems(table, items);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

/* Insertion sort on `order`, which keeps file order for ties. The list is
 * twenty entries at most and sorted once per session, so the simple stable
 * thing is the right thing. */
static void
sort_by_order(struct RS_PreloadTable* table)
{
    assert(table);
    for( int i = 1; i < table->count; i++ )
    {
        struct RS_PreloadStep held = table->steps[i];
        int at = i - 1;

        while( at >= 0 && table->steps[at].order > held.order )
        {
            table->steps[at + 1] = table->steps[at];
            at--;
        }
        table->steps[at + 1] = held;
    }
}

void
RS_Preload_LoadSources(
    struct RS_PreloadTable* table,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini)
{
    assert(table);
    load_one(table, ui_ini, NULL);
    load_one(table, cache_ini, NULL);
    load_one(table, inline_ini, "revconfig");
    sort_by_order(table);
}

struct RS_PreloadStep const*
RS_Preload_At(
    struct RS_PreloadTable const* table,
    int index)
{
    assert(table);
    if( index < 0 || index >= table->count )
        return NULL;
    return &table->steps[index];
}

int
RS_Preload_TotalWeight(struct RS_PreloadTable const* table)
{
    int total = 0;

    assert(table);
    for( int i = 0; i < table->count; i++ )
        total += table->steps[i].weight;
    return total;
}
