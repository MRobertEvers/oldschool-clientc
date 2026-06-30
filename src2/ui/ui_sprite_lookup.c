#include "ui_sprite_lookup.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
ui_sprite_lookup_init(struct UISpriteLookup* lookup)
{
    assert(lookup);
    memset(lookup, 0, sizeof(*lookup));
}

bool
ui_sprite_lookup_add(
    struct UISpriteLookup* lookup,
    char const* name,
    int element_id,
    int atlas_count)
{
    assert(lookup && name);
    assert(lookup->count < UI_SPRITE_LOOKUP_MAX);
    if( name[0] == '\0' )
        return false;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( strcmp(lookup->entries[i].name, name) == 0 )
        {
            lookup->entries[i].element_id = element_id;
            lookup->entries[i].atlas_count = atlas_count;
            return true;
        }
    }

    struct UISpriteLookupEntry* e = &lookup->entries[lookup->count++];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->element_id = element_id;
    e->atlas_count = atlas_count;
    return true;
}

int
ui_sprite_lookup_find(
    struct UISpriteLookup const* lookup,
    char const* name,
    int* out_atlas_count)
{
    assert(lookup && name);
    if( name[0] == '\0' )
        return -1;

    for( int i = 0; i < lookup->count; i++ )
    {
        if( strcmp(lookup->entries[i].name, name) == 0 )
        {
            if( out_atlas_count )
                *out_atlas_count = lookup->entries[i].atlas_count;
            return lookup->entries[i].element_id;
        }
    }
    return -1;
}

int
ui_sprite_lookup_resolve_ref(
    struct UISpriteLookup const* lookup,
    char const* sprite_ref,
    int* out_atlas_index)
{
    assert(lookup && sprite_ref);
    if( sprite_ref[0] == '\0' )
        return -1;

    char base[64];
    int atlas = 0;
    char const* comma = strchr(sprite_ref, ',');
    char const* bracket = strchr(sprite_ref, '[');

    if( comma )
    {
        size_t len = (size_t)(comma - sprite_ref);
        if( len >= sizeof(base) )
            len = sizeof(base) - 1;
        memcpy(base, sprite_ref, len);
        base[len] = '\0';
        atlas = atoi(comma + 1);
    }
    else if( bracket )
    {
        size_t len = (size_t)(bracket - sprite_ref);
        if( len >= sizeof(base) )
            len = sizeof(base) - 1;
        memcpy(base, sprite_ref, len);
        base[len] = '\0';
        char const* end = strchr(bracket + 1, ']');
        if( end )
        {
            char tmp[16];
            size_t alen = (size_t)(end - bracket - 1);
            if( alen >= sizeof(tmp) )
                alen = sizeof(tmp) - 1;
            memcpy(tmp, bracket + 1, alen);
            tmp[alen] = '\0';
            atlas = atoi(tmp);
        }
    }
    else
    {
        strncpy(base, sprite_ref, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
    }

    if( out_atlas_index )
        *out_atlas_index = atlas;
    return ui_sprite_lookup_find(lookup, base, NULL);
}
