#include "gameproto_process.h"

#include "datastruct/vec.h"
#include "gametask.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/maps.h"
#include "query_engine.h"
#include "query_executor_dat.h"
#include "rscache/bitbuffer.h"

void
gameproto_process(
    struct GGame* game,
    struct GIOQueue* io)
{
    if( game->packets_lc245_2 )
    {
        struct RevPacket_LC245_2_Item* item = game->packets_lc245_2;
        game->packets_lc245_2 = item->next_nullable;

        static int g_already = 0;
        g_already++;
        printf("g_already: %d, packet_type: %d\n", g_already, item->packet.packet_type);
        switch( item->packet.packet_type )
        {
        case PKTIN_LC245_2_REBUILD_NORMAL:
        {
            // if( g_already != 1 )
            //     break;
            int regions[20] = { 0 };

            int zone_padding = 104 / (2 * 8);
            int map_sw_x = (item->packet._map_rebuild.zonex - zone_padding) / 8;
            int map_sw_z = (item->packet._map_rebuild.zonez - zone_padding) / 8;
            int map_ne_x = (item->packet._map_rebuild.zonex + zone_padding) / 8;
            int map_ne_z = (item->packet._map_rebuild.zonez + zone_padding) / 8;

            if( map_sw_x > 100 || map_sw_z < 30 || map_sw_z > 100 || map_ne_z < 30 )
                break;
            if( map_ne_x < 3 || map_ne_z < 3 )
                break;
            int index = 0;
            for( int x = map_sw_x; x <= map_ne_x; x++ )
            {
                for( int z = map_sw_z; z <= map_ne_z; z++ )
                {
                    if( x == 44 && z == 34 )
                        printf("x: %d, z: %d\n", x, z);
                    printf("x: %d, z: %d\n", x, z);
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
        break;
        case PKTIN_LC245_2_NPC_INFO:
        {
            static int g_already = 0;
            g_already++;
            if( g_already != 1 )
                break;
            struct Vec* npc_ids = vec_new(sizeof(int), 64);
            struct QEQuery* q = query_engine_qnew();
            struct BitBuffer buf;
            struct RSBuffer rsbuf;
            rsbuf_init(&rsbuf, item->packet._npc_info.data, item->packet._npc_info.length);
            bitbuffer_init_from_rsbuf(&buf, &rsbuf);
            bits(&buf);

            int count = gbits(&buf, 8);
            for( int i = 0; i < count; i++ )
            {
                //
            }

            int npc_count;
            for( int i = 0; i < count; i++ )
            {
                // int index = npc_ids[i];

                int info = gbits(&buf, 1);
                if( info == 0 )
                {
                    //
                }
                else
                {
                    int op = gbits(&buf, 2);
                    switch( op )
                    {
                    case 0:
                        //
                        break;
                    case 1:
                        // walkdir
                        gbits(&buf, 3);
                        // has extended info
                        gbits(&buf, 1);
                        break;
                    case 2:
                        // walkdir
                        gbits(&buf, 3);
                        // rundir
                        gbits(&buf, 3);
                        // has extended info
                        gbits(&buf, 1);
                        break;
                    case 3:
                        //
                        break;
                    }
                    //
                }
            }

            while( ((buf.byte_position * 8) + buf.bit_offset + 21) <
                   item->packet._npc_info.length * 8 )
            {
                int npc_idx = gbits(&buf, 13);
                if( npc_idx == 8191 )
                {
                    break;
                }

                int npc_id = gbits(&buf, 11);
                vec_push(npc_ids, &npc_id);

                int dx = gbits(&buf, 5);
                int dy = gbits(&buf, 5);

                int has_extended_info = gbits(&buf, 1);
            }

            struct Vec* models = vec_new(sizeof(int), 64);
            for( int i = 0; i < vec_size(npc_ids); i++ )
            {
                int npc_id = *(int*)vec_get(npc_ids, i);
                struct CacheDatConfigNpc* npc = buildcachedat_get_npc(game->buildcachedat, npc_id);
                assert(npc && "Npc must be found");

                for( int j = 0; j < npc->models_count; j++ )
                {
                    int model_id = npc->models[j];
                    vec_push(models, &model_id);
                }
            }

            query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_0, QE_STORE_DISCARD);
            query_engine_qpush_argx(q, vec_data(models), vec_size(models));
            gametask_new_query(game, q);
            gametask_new_packet(game, io);
            break;
        }
        default:
            break;
        }

        item->next_nullable = NULL;
        if( !game->packets_lc245_2_inflight )
        {
            game->packets_lc245_2_inflight = item;
        }
        else
        {
            struct RevPacket_LC245_2_Item* list = game->packets_lc245_2_inflight;
            while( list->next_nullable )
                list = list->next_nullable;
            list->next_nullable = item;
        }
    }
}