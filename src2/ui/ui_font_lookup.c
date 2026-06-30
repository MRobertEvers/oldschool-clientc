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
    int font_id)
{
    assert(lookup && name);
    if( name[0] == '\0' || lookup->count >= UI_FONT_LOOKUP_MAX )
        return false;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( strcmp(lookup->entries[i].name, name) == 0 )
        {
            lookup->entries[i].font_id = font_id;
            return true;
        }
    }

    struct UIFontLookupEntry* e = &lookup->entries[lookup->count++];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->font_id = font_id;
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
