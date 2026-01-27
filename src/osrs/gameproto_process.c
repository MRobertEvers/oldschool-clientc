#include "gameproto_process.h"

#include "gametask.h"
#include "osrs/rscache/tables/maps.h"
#include "query_engine.h"
#include "query_executor_dat.h"

void
gameproto_process(
    struct GGame* game,
    struct GIOQueue* io)
{
    if( game->packet_queue_lc245_2_nullable )
    {
        struct RevPacket_LC245_2_Item* item = game->packet_queue_lc245_2_nullable;

        switch( item->packet.packet_type )
        {
        case PKTIN_LC245_2_REBUILD_NORMAL:
        {
            {
                int regions[20] = { 0 };

                int zone_padding = 104 / (2 * 8);
                int map_sw_x = (item->packet._map_rebuild.zonex - zone_padding) / 8;
                int map_sw_z = (item->packet._map_rebuild.zonez - zone_padding) / 8;
                int map_ne_x = (item->packet._map_rebuild.zonex + zone_padding) / 8;
                int map_ne_z = (item->packet._map_rebuild.zonez + zone_padding) / 8;

                int index = 0;
                for( int x = map_sw_x; x <= map_ne_x; x++ )
                {
                    for( int z = map_sw_z; z <= map_ne_z; z++ )
                    {
                        regions[index] = MAPREGIONXZ(x, z);
                        index++;
                    }
                }

                struct QEQuery* q = query_engine_qnew();
                query_engine_qpush_op(q, QEDAT_DT_MAPS_SCENERY, QE_FN_0, QE_STORE_SET_0);
                query_engine_qpush_argx(q, regions, index);
                query_engine_qpush_op(q, QEDAT_DT_CONFIG_LOCIDS, QE_FN_FROM_0, QE_STORE_DISCARD);
                query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_FROM_0, QE_STORE_DISCARD);
                query_engine_qpush_op(q, QEDAT_DT_MAPS_TERRAIN, QE_FN_0, QE_STORE_DISCARD);
                query_engine_qpush_argx(q, regions, index);

                gametask_new_query(game, q);

                gametask_new_packet(game, io);
            }
        }
        break;
        default:
            break;
        }
    }
}