#ifndef INTERFACE161_ENUM_LOOKUP_H
#define INTERFACE161_ENUM_LOOKUP_H

#include "osrs/rscache/dat2disk/dat2disk.h"

int
interface161_enum_lookup(
    struct RSCacheDat2Disk* cache,
    int input_type,
    int output_type,
    int enum_id,
    int key);

int
interface161_enum_output_count(
    struct RSCacheDat2Disk* cache,
    int enum_id);

/* Debug: print every (key -> value) pair of an int->int enum to stdout. */
void
interface161_enum_dump(
    struct RSCacheDat2Disk* cache,
    int enum_id);

#endif
