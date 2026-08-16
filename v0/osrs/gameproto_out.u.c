#ifndef GAMEPROTO_OUT_U_C
#define GAMEPROTO_OUT_U_C

#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <assert.h>
#include <stdint.h>

static inline int
packetin_write_rebuild_region(
    uint8_t* data,
    int data_size,
    int zonex,
    int zonez)
{
    assert(data_size > 4);

    struct RSCacheShared_RSBuffer buffer = { .data = data, .size = data_size, .position = 0 };

    p1(&buffer, PKTIN_REBUILD_REGION);
    p2(&buffer, zonex);
    p2(&buffer, zonez);

    return buffer.position;
}

#endif