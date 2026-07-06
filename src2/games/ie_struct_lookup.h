#ifndef IE_STRUCT_LOOKUP_H
#define IE_STRUCT_LOOKUP_H

#include <stdbool.h>

struct RSCacheDat2Disk;

bool
ie_struct_param_lookup(
    struct RSCacheDat2Disk* cache,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str);

#endif
