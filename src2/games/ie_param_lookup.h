#ifndef IE_PARAM_LOOKUP_H
#define IE_PARAM_LOOKUP_H

#include <stdbool.h>

struct Dat2BuildCache;

bool
ie_param_is_string(
    struct Dat2BuildCache* bc,
    int param_id);

int
ie_param_default_int(
    struct Dat2BuildCache* bc,
    int param_id);

char const*
ie_param_default_string(
    struct Dat2BuildCache* bc,
    int param_id);

#endif
