#include "mem/torimem_rscache.h"
#include "mem/torirs_mem.h"
#include "engine/torirs_enum_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_Enum*
ToriRS_EnumFromRSCacheDat2(
    int enum_id,
    struct RSCache_Dat2ConfigEnum* entry)
{
    struct ToriRS_Enum* e;

    assert(entry);

    e = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*e));
    assert(e);
    e->id = enum_id;
    e->input_type = entry->input_type;
    e->output_is_string = entry->output_is_string;
    e->default_int = entry->default_int;
    /* The decoder's arrays become the cache's: each is taken from RSCache
     * into ASSET, where ToriRS_EnumFree releases it. An array of no entries
     * is dropped rather than taken. */
    size_t n = entry->count > 0 ? (size_t)entry->count : 0;
    if( entry->default_string )
        e->default_string = ToriMem_TakeStrFromRSCache(ToriMem_Assets(), entry->default_string);
    if( entry->keys && n > 0 )
        e->keys = ToriMem_TakeFromRSCache(ToriMem_Assets(), entry->keys, n * sizeof(*entry->keys));
    else
        RSCACHE_FREE(entry->keys);
    if( entry->int_values && n > 0 )
        e->int_values =
            ToriMem_TakeFromRSCache(ToriMem_Assets(), entry->int_values, n * sizeof(*entry->int_values));
    else
        RSCACHE_FREE(entry->int_values);
    if( entry->string_values && n > 0 )
    {
        for( size_t i = 0; i < n; i++ )
            if( entry->string_values[i] )
                entry->string_values[i] =
                    ToriMem_TakeStrFromRSCache(ToriMem_Assets(), entry->string_values[i]);
        e->string_values = ToriMem_TakeFromRSCache(
            ToriMem_Assets(), entry->string_values, n * sizeof(*entry->string_values));
    }
    else
        RSCACHE_FREE(entry->string_values);
    e->count = entry->count;

    entry->default_string = NULL;
    entry->keys = NULL;
    entry->int_values = NULL;
    entry->string_values = NULL;
    entry->count = 0;
    return e;
}
