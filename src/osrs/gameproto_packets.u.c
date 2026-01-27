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

    //     {
    // #define MAPXZR(x, z) ((x) << 8 | (z))
    //         int regions[2] = {
    //             MAPXZR(48, 48),
    //             MAPXZR(48, 49),
    //         };

    //         struct QEQuery* q = query_engine_qnew();
    //         query_engine_qpush_op(q, QEDAT_DT_MAPS_SCENERY, QE_FN_0, QE_STORE_SET_0);
    //         query_engine_qpush_argx(q, regions, 2);
    //         query_engine_qpush_op(q, QEDAT_DT_CONFIG_LOCIDS, QE_FN_FROM_0, QE_STORE_DISCARD);
    //         query_engine_qpush_op(q, QEDAT_DT_MAPS_TERRAIN, QE_FN_0, QE_STORE_DISCARD);

    //         gametask_new_query(game, q);
    //     }
}

#endif