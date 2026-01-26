#ifndef GAMEPROTO_PACKETS_U_C
#define GAMEPROTO_PACKETS_U_C

#include "osrs/gametask.h"
#include "packetin.h"
#include "rscache/rsbuf.h"

#define SCENE_TILE_WIDTH 104

static inline void
gameproto_packet_maprebuild8_z16_x16(
    struct GGame* game,
    uint8_t* data,
    int data_size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, data_size);

    int zonex;
    int zonez;

    zonex = g2(&buffer);
    zonez = g2(&buffer);
    assert(buffer.position == data_size);

    // Rebuild.

    int zone_padding = SCENE_TILE_WIDTH / (2 * 8);
    int map_sw_x = (zonex - zone_padding) / 8;
    int map_sw_z = (zonez - zone_padding) / 8;
    int map_ne_x = (zonex + zone_padding) / 8;
    int map_ne_z = (zonez + zone_padding) / 8;
}

#endif