#include "mem/torirs_mem.h"
#include "mem/torimem_rscache.h"
#include "engine/torirs_struct_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_Struct*
ToriRS_StructFromRSCacheDat2(
    int struct_id,
    struct RSCache_Dat2ConfigStruct* entry)
{
    struct ToriRS_Struct* s;
    int i;

    assert(entry);

    s = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*s));
    assert(s);
    s->id = struct_id;
    s->param_count = entry->params.count;
    if( s->param_count > 0 )
    {
        s->params = ToriMem_Calloc(ToriMem_Assets(), (size_t)s->param_count, sizeof(*s->params));
        assert(s->params);
        for( i = 0; i < s->param_count; i++ )
        {
            s->params[i].key = entry->params.keys[i];
            if( entry->params.kinds && entry->params.kinds[i] == RSCACHE_PARAM_STRING )
            {
                if( entry->params.values[i] )
                    s->params[i].string_value =
                        ToriMem_TakeStrFromRSCache(ToriMem_Assets(), (char*)entry->params.values[i]);
                entry->params.values[i] = NULL;
            }
            else if( entry->params.values[i] )
            {
                /* INT and LONG both land in int_value for the runtime struct —
                 * long params truncate; the CS2 host only consumes int/string. */
                if( entry->params.kinds && entry->params.kinds[i] == RSCACHE_PARAM_LONG )
                    s->params[i].int_value = (int)*(int64_t*)entry->params.values[i];
                else
                    s->params[i].int_value = *(int*)entry->params.values[i];
                RSCACHE_FREE(entry->params.values[i]); /* the decoder's */
                entry->params.values[i] = NULL;
            }
        }
    }

    RSCache_Dat2ConfigStructFreeInplace(entry);
    return s;
}
