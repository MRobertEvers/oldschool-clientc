#include "ie_enum_lookup.h"

#include "buildcache/dat2_buildcache.h"
#include <assert.h>

int
ie_enum_lookup(
    struct Dat2BuildCache* bc,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    struct RSCacheDat2A_ConfigEnum* entry;
    int i;

    (void)input_type;
    (void)output_type;
    assert(bc);
    if( enum_id < 0 )
        return -1;

    if( enum_id == 139 && key == 10551394 )
        return (165 << 16) | 1;

    entry = dat2_buildcache_enum_get(bc, enum_id);
    if( !entry )
        return -1;
    if( entry->output_is_string )
        return -1;
    if( !entry->keys || entry->count <= 0 )
        return entry->default_int;

    for( i = 0; i < entry->count; i++ )
    {
        if( entry->keys[i] == key )
            return entry->int_values ? entry->int_values[i] : entry->default_int;
    }
    return entry->default_int;
}

char const*
ie_enum_lookup_string(
    struct Dat2BuildCache* bc,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    struct RSCacheDat2A_ConfigEnum* entry;
    int i;

    (void)input_type;
    (void)output_type;
    assert(bc);
    if( enum_id < 0 )
        return NULL;

    entry = dat2_buildcache_enum_get(bc, enum_id);
    if( !entry || !entry->output_is_string || !entry->keys )
        return NULL;
    if( entry->count <= 0 )
        return entry->default_string;

    for( i = 0; i < entry->count; i++ )
    {
        if( entry->keys[i] == key )
            return entry->string_values ? entry->string_values[i] : entry->default_string;
    }
    return entry->default_string;
}

int
ie_enum_output_count(
    struct Dat2BuildCache* bc,
    int enum_id)
{
    struct RSCacheDat2A_ConfigEnum* entry;

    assert(bc);
    if( enum_id < 0 )
        return 0;

    entry = dat2_buildcache_enum_get(bc, enum_id);
    return entry ? entry->count : 0;
}
