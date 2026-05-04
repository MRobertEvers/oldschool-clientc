#include "osrs/core/gameproto_core_parse.h"

#include "osrs/rscache/rsbuf.h"

#include <assert.h>

int
gameproto_core_parse_maprebuild8_z16_x16_v1(
    uint8_t* data,
    int n,
    int* out_zonex,
    int* out_zonez)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, (int8_t*)data, n);
    *out_zonex = g2(&buffer);
    *out_zonez = g2(&buffer);
    assert(buffer.position == n);
    return 1;
}
