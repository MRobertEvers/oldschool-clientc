#ifndef GAMEPROTO_PACKETS_WRITE_U_C
#define GAMEPROTO_PACKETS_WRITE_U_C

#include "rscache/rsbuf.h"

#include <assert.h>

static inline int
gameproto_packet_write_maprebuild8_z16_x16(
    uint8_t* buffer,
    int packet_type,
    int buffer_size,
    int zonex,
    int zonez)
{
    assert(buffer_size > 4);

    struct RSBuffer rsbuf = { .data = buffer, .size = buffer_size, .position = 0 };

    p1(&rsbuf, packet_type);
    p2(&rsbuf, zonex);
    p2(&rsbuf, zonez);

    return rsbuf.position;
}

#endif