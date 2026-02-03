#include "gameproto_process.h"

#include "datastruct/vec.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "gametask.h"
#include "jbase37.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/maps.h"
#include "packets/pkt_npc_info.h"
#include "packets/pkt_player_info.h"
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

    struct AppearanceOp op;
    for( int i = 0; i < 12; i++ )
    {
        appearances_decode(&op, appearances, i);
        switch( op.kind )
        {
        case APPEARANCE_KIND_IDK:
            idk_ids[idk_ids_count++] = op.id;
            break;
        case APPEARANCE_KIND_OBJ:
            obj_ids[obj_ids_count++] = op.id;
            break;
        }
    }

    for( int i = 0; i < idk_ids_count; i++ )
    {
        printf("IDK ID: %d\n", idk_ids[i]);
    }
    for( int i = 0; i < obj_ids_count; i++ )
    {
        printf("OBJ ID: %d\n", obj_ids[i]);
    }
    struct QEQuery* q = query_engine_qnew();
    query_engine_qpush_op(q, QEDAT_DT_CONFIG_IDKS, QE_FN_0, QE_STORE_SET_0);
    query_engine_qpush_argx(q, idk_ids, idk_ids_count);
    query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_FROM_0, QE_STORE_DISCARD);
    query_engine_qpush_op(q, QEDAT_DT_CONFIG_OBJS, QE_FN_0, QE_STORE_SET_1);
    query_engine_qpush_argx(q, obj_ids, obj_ids_count);
    query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_FROM_1, QE_STORE_DISCARD);

    gametask_new_query(game, q);
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
        }
        break;
        case PKTIN_LC245_2_PLAYER_INFO:
        {
            struct BitBuffer buf;
            struct RSBuffer rsbuf;
            rsbuf_init(&rsbuf, item->packet._player_info.data, item->packet._player_info.length);
            bitbuffer_init_from_rsbuf(&buf, &rsbuf);
            bits(&buf);

            struct PktPlayerInfoReader reader;
            reader.extended_count = 0;
            reader.current_op = 0;
            reader.max_ops = 2048;

            struct PktPlayerInfoOp ops[2048];
            int count = pkt_player_info_reader_read(&reader, &item->packet._player_info, ops, 2048);
            for( int i = 0; i < count; i++ )
            {
                struct PktPlayerInfoOp* op = &ops[i];
                switch( op->kind )
                {
                case PKT_PLAYER_INFO_OP_APPEARANCE:
                {
                    struct PlayerAppearance appearance;
                    player_appearance_decode(
                        &appearance, op->_appearance.appearance, op->_appearance.len);
                    process_appearance(game, appearance.appearance);
                }
                break;
                }
            }
        }
        break;
        case PKTIN_LC245_2_NPC_INFO:
        {
            struct BitBuffer buf;
            struct RSBuffer rsbuf;
            rsbuf_init(&rsbuf, item->packet._npc_info.data, item->packet._npc_info.length);
            bitbuffer_init_from_rsbuf(&buf, &rsbuf);
            bits(&buf);

            struct PktNpcInfoReader reader = { 0 };
            reader.extended_count = 0;
            reader.current_op = 0;
            reader.max_ops = 2048;

            struct PktNpcInfoOp ops[2048];
            int npc_ids[2048];
            int npc_ids_count = 0;
            int count = pkt_npc_info_reader_read(&reader, &item->packet._npc_info, ops, 2048);

            for( int i = 0; i < count; i++ )
            {
                struct PktNpcInfoOp* op = &ops[i];
                switch( op->kind )
                {
                case PKT_NPC_INFO_OPBITS_NPCTYPE:
                {
                    int npc_type = op->_bitvalue;
                    npc_ids[npc_ids_count++] = npc_type;
                }
                break;
                }
            }

            struct QEQuery* q = query_engine_qnew();
            query_engine_qpush_op(q, QEDAT_DT_CONFIG_NPCS, QE_FN_0, QE_STORE_SET_0);
            query_engine_qpush_argx(q, npc_ids, npc_ids_count);
            query_engine_qpush_op(q, QEDAT_DT_MODELS, QE_FN_FROM_0, QE_STORE_DISCARD);
            gametask_new_query(game, q);
            break;
        }
        default:
            break;
        }

        gametask_new_packet(game, io);

        item->next_nullable = NULL;

        if( !game->packets_lc245_2_inflight )
        {
            game->packets_lc245_2_inflight = item;
        }
        else
        {
            struct RevPacket_LC245_2_Item* list = game->packets_lc245_2_inflight;
            int count = 0;
            while( list->next_nullable )
            {
                count++;
                list = list->next_nullable;
            }
            printf("Adding to count Count: %d\n", count);
            list->next_nullable = item;
        }
    }
}