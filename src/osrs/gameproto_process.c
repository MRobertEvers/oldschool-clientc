#include "gameproto_process.h"

#include "datastruct/vec.h"
#include "gametask.h"
#include "jbase37.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/maps.h"
#include "query_engine.h"
#include "query_executor_dat.h"
#include "rscache/bitbuffer.h"

//     int queue_models[50] = { 0 };
//     int queue_models_count = 0;
//     int right_hand_value = -1;
//     int left_hand_value = -1;

//     struct CacheModel* models[50] = { 0 };
//     int build_models[50] = { 0 };
//     int build_models_count = 0;

//     for( int i = 0; i < 12; i++ )
//     {
//         int appearance = game->player_slots[i];
//         if( right_hand_value >= 0 && i == 3 )
//         {
//             appearance = right_hand_value;
//         }
//         if( left_hand_value >= 0 && i == 5 )
//         {
//             appearance = left_hand_value;
//         }
//         if( appearance >= 0x100 && appearance < 0x200 )
//         {
//             appearance -= 0x100;
//             int model_id = idk_model(game, appearance);
//             if( model_id >= 0 )
//             {
//                 struct CacheModel* model = buildcachedat_get_idk_model(buildcachedat, model_id);
//                 assert(model && "Model must be found");
//                 models[build_models_count++] = model;
//             }
//             else if( model_id != -2 )
//             {
//                 game->awaiting_models++;
//             }
//         }
//         else if( appearance >= 0x200 )
//         {
//             appearance -= 0x200;
//             int model_id = obj_model(game, appearance);
//             if( model_id >= 0 )
//             {
//                 struct CacheModel* model = buildcachedat_get_obj_model(buildcachedat, model_id);
//                 assert(model && "Model must be found");
//                 models[build_models_count++] = model;
//             }
//             else if( model_id != -2 )
//             {
//                 game->awaiting_models++;
//             }
//         }
//     }

static void
process_appearance(
    struct GGame* game,
    int* appearances)
{
    //
    int idk_ids[12] = { 0 };
    int idk_ids_count = 0;
    int obj_ids[12] = { 0 };
    int obj_ids_count = 0;
    int right_hand_value = -1;
    int left_hand_value = -1;

    for( int i = 0; i < 12; i++ )
    {
        int appearance = appearances[i];
        if( right_hand_value >= 0 && i == 3 )
        {
            appearance = right_hand_value;
        }
        if( left_hand_value >= 0 && i == 5 )
        {
            appearance = left_hand_value;
        }

        if( appearance >= 0x100 && appearance < 0x200 )
        {
            appearance -= 0x100;

            idk_ids[idk_ids_count++] = appearance;
        }

        else if( appearance >= 0x200 )
        {
            appearance -= 0x200;
            obj_ids[obj_ids_count++] = appearance;
        }
    }

    struct QEQuery* q = query_engine_qnew();
    query_engine_qpush_op(q, QEDAT_DT_CONFIG_IDKS, QE_FN_0, QE_STORE_SET_0);
    query_engine_qpush_argx(q, idk_ids, idk_ids_count);
    query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_FROM_0, QE_STORE_DISCARD);
    query_engine_qpush_op(q, QEDAT_DT_CONFIG_OBJS, QE_FN_0, QE_STORE_SET_1);
    query_engine_qpush_argx(q, obj_ids, obj_ids_count);
    query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_FROM_1, QE_STORE_DISCARD);

    gametask_new_query(game, q);
    gametask_new_packet(game, game->io);
}

static void
player_appearance_process(
    struct GGame* game,
    uint8_t* appearance_buf,
    int len)
{
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, appearance_buf, len);
    int appearances[12];
    int colors[5];
    int gender = g1(&rsbuf);
    int head_icon = g1(&rsbuf);

    for( int i = 0; i < 12; i++ )
    {
        int msb = g1(&rsbuf);
        if( msb == 0 )
        {
            appearances[i] = 0;
        }
        else
        {
            appearances[i] = (msb << 8) | g1(&rsbuf);
        }
    }

    for( int i = 0; i < 5; i++ )
    {
        colors[i] = g1(&rsbuf);
    }

    int readyanim = g2(&rsbuf);
    if( readyanim == 65535 )
        readyanim = -1;

    int turnanim = g2(&rsbuf);
    if( turnanim == 65535 )
        turnanim = -1;

    int walkanim = g2(&rsbuf);
    if( walkanim == 65535 )
        walkanim = -1;

    int walkanim_b = g2(&rsbuf);
    if( walkanim_b == 65535 )
        walkanim_b = -1;

    int walkanim_l = g2(&rsbuf);
    if( walkanim_l == 65535 )
        walkanim_l = -1;

    int walkanim_r = g2(&rsbuf);
    if( walkanim_r == 65535 )
        walkanim_r = -1;

    int runanim = g2(&rsbuf);
    if( runanim == 65535 )
        runanim = -1;

    uint64_t name = g8(&rsbuf);
    char name_str[13] = { 0 };
    base37tostr(name, name_str, sizeof(name_str));

    int combat_level = g1(&rsbuf);

    process_appearance(game, appearances);
}

void
gameproto_process(
    struct GGame* game,
    struct GIOQueue* io)
{
    if( game->packets_lc245_2 )
    {
        struct RevPacket_LC245_2_Item* item = game->packets_lc245_2;
        game->packets_lc245_2 = item->next_nullable;

        switch( item->packet.packet_type )
        {
        case PKTIN_LC245_2_REBUILD_NORMAL:
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
        break;
        case PKTIN_LC245_2_PLAYER_INFO:
        {
            struct BitBuffer buf;
            struct RSBuffer rsbuf;
            rsbuf_init(&rsbuf, item->packet._player_info.data, item->packet._player_info.length);
            bitbuffer_init_from_rsbuf(&buf, &rsbuf);
            bits(&buf);

            int player_updates_count = 0;
            int player_updates_idxs[2048] = { 0 };

            // Player local
            int info = gbits(&buf, 1);
            if( info != 0 )
            {
                int op = gbits(&buf, 2);
                switch( op )
                {
                case 0:

                    break;
                case 1:
                { // walkdir
                    gbits(&buf, 3);
                    // has extended info
                    int has_extended_info = gbits(&buf, 1);
                    if( has_extended_info )
                        player_updates_idxs[player_updates_count++] = 2047;
                }
                break;
                case 2:
                { //
                    // walkdir
                    gbits(&buf, 3);
                    // rundir
                    gbits(&buf, 3);
                    // has extended info
                    int has_extended_info = gbits(&buf, 1);
                    if( has_extended_info )
                        player_updates_idxs[player_updates_count++] = 2047;
                }
                break;
                case 3:
                {
                    int level = gbits(&buf, 2);
                    int sx = gbits(&buf, 7);
                    int sz = gbits(&buf, 7);
                    int jump = gbits(&buf, 1);
                    int has_extended_info = gbits(&buf, 1);
                    if( has_extended_info )
                        player_updates_idxs[player_updates_count++] = 2047;
                }
                break;
                }
            }
            else
            {
                player_updates_idxs[player_updates_count++] = 2047;
            }

            // Player Old Vis
            int count = gbits(&buf, 8);
            for( int i = 0; i < count; i++ )
            {
                int info = gbits(&buf, 1);
                if( info != 0 )
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
                }
            }

            // Player New Vis
            while( ((buf.byte_position * 8) + buf.bit_offset + 10) <
                   item->packet._player_info.length * 8 )
            {
                int player_idx = gbits(&buf, 11);
                if( player_idx == 2047 )
                {
                    break;
                }

                int dx = gbits(&buf, 5);
                int dy = gbits(&buf, 5);
                int jump = gbits(&buf, 1);

                int has_extended_info = gbits(&buf, 1);
                if( has_extended_info )
                    player_updates_idxs[player_updates_count++] = player_idx;
            }

            // Extended Info
            uint8_t* appearance_buf = NULL;
            rsbuf.position = buf.byte_position + (buf.bit_offset + 7) / 8;
            for( int i = 0; i < player_updates_count; i++ )
            {
                int player_idx = player_updates_idxs[i];
                if( player_idx != 2047 )
                {
                    //
                }

                int mask = g1(&rsbuf);
                if( (mask & 0x80) != 0 )
                {
                    mask += g1(&rsbuf) << 8;
                }

                // Info

                // Appearance
                if( mask & 0x01 )
                {
                    int len = g1(&rsbuf);

                    appearance_buf = malloc(len);
                    greadto(&rsbuf, appearance_buf, len, len);
                    player_appearance_process(game, appearance_buf, len);
                }

                break;
            }
        }
        break;
        case PKTIN_LC245_2_NPC_INFO:
        {
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