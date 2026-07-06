#ifndef IE_ENUM_LOOKUP_H
#define IE_ENUM_LOOKUP_H

struct RSCacheDat2Disk;

int
ie_enum_lookup(
    struct RSCacheDat2Disk* cache,
    int input_type,
    int output_type,
    int enum_id,
    int key);

/** Returns NULL when the enum is not string-valued; otherwise returns the mapped string. */
char const*
ie_enum_lookup_string(
    struct RSCacheDat2Disk* cache,
    int input_type,
    int output_type,
    int enum_id,
    int key);

int
ie_enum_output_count(
    struct RSCacheDat2Disk* cache,
    int enum_id);

#endif
