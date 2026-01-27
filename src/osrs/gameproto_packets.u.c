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

    game->packet_queue_lc245_2_nullable = malloc(sizeof(struct RevPacket_LC245_2_Item));
    game->packet_queue_lc245_2_nullable->packet.packet_type = PKTIN_LC245_2_REBUILD_NORMAL;
    game->packet_queue_lc245_2_nullable->packet._map_rebuild.zonex = zonex;
    game->packet_queue_lc245_2_nullable->packet._map_rebuild.zonez = zonez;

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