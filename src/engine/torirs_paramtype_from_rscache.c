#include "mem/torimem_rscache.h"
#include "mem/torirs_mem.h"
#include "engine/torirs_paramtype_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_ParamType*
ToriRS_ParamTypeFromRSCacheDat2(
    int id,
    struct RSCache_Dat2ConfigParam* entry)
{
    struct ToriRS_ParamType* param;

    assert(entry);

    param = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*param));
    assert(param);
    param->id = id;
    param->type = entry->type;
    param->default_int = entry->default_int;
    param->default_long = entry->default_long;
    param->is_string = entry->type == 's';
    if( entry->default_string )
        param->default_string = ToriMem_TakeStrFromRSCache(ToriMem_Assets(), entry->default_string);
    entry->default_string = NULL;

    RSCache_Dat2ConfigParamFreeInplace(entry);
    return param;
}
