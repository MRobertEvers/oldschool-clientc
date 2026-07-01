#include "ui_font_lookup.h"

#include <assert.h>
#include <string.h>

void
ui_font_lookup_init(struct UIFontLookup* lookup)
{
    assert(lookup);
    memset(lookup, 0, sizeof(*lookup));
}

bool
ui_font_lookup_add(
    struct UIFontLookup* lookup,
    char const* name,
    int font_id,
    int cache_font_id,
    int cache_archive_id)
{
    assert(lookup && name);
    if( name[0] == '\0' || lookup->count >= UI_FONT_LOOKUP_MAX )
        return false;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( strcmp(lookup->entries[i].name, name) == 0 )
        {
            lookup->entries[i].font_id = font_id;
            lookup->entries[i].cache_font_id = cache_font_id;
            lookup->entries[i].cache_archive_id = cache_archive_id;
            return true;
        }
    }

    struct UIFontLookupEntry* e = &lookup->entries[lookup->count++];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->font_id = font_id;
    e->cache_font_id = cache_font_id;
    e->cache_archive_id = cache_archive_id;
    return true;
}

int
ui_font_lookup_find(
    struct UIFontLookup const* lookup,
    char const* name)
{
    assert(lookup && name);
    if( name[0] == '\0' )
        return -1;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( strcmp(lookup->entries[i].name, name) == 0 )
            return lookup->entries[i].font_id;
    }
    return -1;
}

int
ui_font_lookup_find_by_cache_font_id(
    struct UIFontLookup const* lookup,
    int cache_font_id)
{
    if( !lookup || cache_font_id < 0 )
        return -1;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( lookup->entries[i].cache_font_id == cache_font_id )
            return lookup->entries[i].font_id;
    }
    return -1;
}

int
ui_font_lookup_find_by_archive_id(
    struct UIFontLookup const* lookup,
    int cache_archive_id)
{
    if( !lookup || cache_archive_id < 0 )
        return -1;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( lookup->entries[i].cache_archive_id == cache_archive_id )
            return lookup->entries[i].font_id;
    }
    return -1;
}

int
ui_font_lookup_resolve_cache_font_index(
    struct UIFontLookup const* lookup,
    int cache_font_idx)
{
    static char const* const cache_font_names[] = { "p11", "p12", "b12", "q8" };
    if( !lookup || cache_font_idx < 0 || cache_font_idx >= 4 )
        return -1;

    int font_id = ui_font_lookup_find_by_cache_font_id(lookup, cache_font_idx);
    if( font_id >= 0 )
        return font_id;

    return ui_font_lookup_find(lookup, cache_font_names[cache_font_idx]);
}
