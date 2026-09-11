#include "revconfig_refs.h"

#include "revconfig_load.h"

#include <assert.h>
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
    assert(refs);
    /* Same order as UIBuilderManifestSources: shared files first, the boot
     * manifest's own inline sections last, so a lane can override one id
     * without copying the whole profile. */
    refs_load_one(refs, ui_ini, NULL);
    refs_load_one(refs, cache_ini, NULL);
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
