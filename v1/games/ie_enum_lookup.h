#ifndef IE_ENUM_LOOKUP_H
#define IE_ENUM_LOOKUP_H

struct Dat2BuildCache;

int
ie_enum_lookup(
    struct Dat2BuildCache* bc,
    int input_type,
    int output_type,
    int enum_id,
    int key);

char const*
ie_enum_lookup_string(
    struct Dat2BuildCache* bc,
    int input_type,
    int output_type,
    int enum_id,
    int key);

int
ie_enum_output_count(
    struct Dat2BuildCache* bc,
    int enum_id);

#endif
