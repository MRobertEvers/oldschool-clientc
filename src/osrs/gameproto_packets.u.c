#ifndef GAMEPROTO_PACKETS_U_C
#define GAMEPROTO_PACKETS_U_C

#include "packetin.h"
#include "osrs/rscache/shared/rscache_shared_rs_buffer.h"

#define SCENE_TILE_WIDTH 104

static inline void
gameproto_packet_maprebuild8_z16_x16(
    struct GGame* game,
    uint8_t* data,
    int data_size)
{
    struct RSCacheShared_RSBuffer buffer;
    RSCacheShared_RSBufferInit(&buffer, data, data_size);

    int zonex;
    int zonez;

    zonex = g2(&buffer);
    zonez = g2(&buffer);
    assert(buffer.position == data_size);

    // Rebuild.
}

#endif