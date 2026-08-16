#include "ie_struct_lookup.h"

#include "buildcache/dat2_buildcache.h"
#include <assert.h>

bool
ie_struct_param_lookup(
    struct Dat2BuildCache* bc,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    struct RSCacheDat2A_ConfigStruct* entry;
    int i;

    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    assert(bc);
    if( struct_id < 0 || param_id < 0 )
        return false;

    entry = dat2_buildcache_struct_get(bc, struct_id);
    if( !entry || entry->params.count <= 0 )
        return false;

    for( i = 0; i < entry->params.count; i++ )
    {
        if( entry->params.keys[i] != param_id )
            continue;
        if( entry->params.is_string[i] )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = (char const*)entry->params.values[i];
            return true;
        }
        if( out_int )
            *out_int = entry->params.values[i] ? *(int*)entry->params.values[i] : 0;
        return true;
    }
    return false;
}
