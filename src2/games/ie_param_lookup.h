#ifndef IE_PARAM_LOOKUP_H
#define IE_PARAM_LOOKUP_H

#include "osrs/rscache/dat2disk/dat2disk.h"

#include <stdbool.h>

bool
ie_param_is_string(
    struct RSCacheDat2Disk* cache,
    int param_id);

int
ie_param_default_int(
    struct RSCacheDat2Disk* cache,
    int param_id);

char const*
ie_param_default_string(
    struct RSCacheDat2Disk* cache,
    int param_id);

#endif
