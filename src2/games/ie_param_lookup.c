#include "ie_param_lookup.h"

#include "buildcache/dat2_buildcache.h"
#include <assert.h>

bool
ie_param_is_string(
    struct Dat2BuildCache* bc,
    int param_id)
{
    struct RSCacheDat2A_ConfigParam* param = dat2_buildcache_param_get(bc, param_id);
    return param && param->type_char == 's';
}

int
ie_param_default_int(
    struct Dat2BuildCache* bc,
    int param_id)
{
    struct RSCacheDat2A_ConfigParam* param = dat2_buildcache_param_get(bc, param_id);
    return param ? param->default_int : 0;
}

char const*
ie_param_default_string(
    struct Dat2BuildCache* bc,
    int param_id)
{
    struct RSCacheDat2A_ConfigParam* param = dat2_buildcache_param_get(bc, param_id);
    return param ? param->default_string : NULL;
}
