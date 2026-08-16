#ifndef INTERFACE161_STRUCT_LOOKUP_H
#define INTERFACE161_STRUCT_LOOKUP_H

#include <stdbool.h>

#include "osrs/rscache/dat2disk/dat2disk.h"

bool
interface161_struct_param_lookup(
    struct RSCacheDat2Disk* cache,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str);

#endif
