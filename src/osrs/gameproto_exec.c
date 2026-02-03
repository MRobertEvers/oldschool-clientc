#include "gameproto_exec.h"

#include "dashlib.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "model_transforms.h"
#include "osrs/_light_model_default.u.c"
#include "packets/pkt_npc_info.h"
#include "packets/pkt_player_info.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "rscache/tables/model.h"

static struct CacheModel*
idk_model(
    struct GGame* game,
    int idk_id)
{
    struct CacheModel* idk_model = buildcachedat_get_idk_model(game->buildcachedat, idk_id);
    if( idk_model )
    {
        return idk_model;
    }

    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(game->buildcachedat, idk_id);
    if( !idk )
    {
        return NULL;
    }

    struct CacheModel* models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->models_count; i++ )
    {
        models[model_count] = buildcachedat_get_model(game->buildcachedat, idk->models[i]);
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    buildcachedat_add_idk_model(game->buildcachedat, idk_id, merged);
    return merged;
}

static struct CacheModel*
obj_model(
    struct GGame* game,
    int obj_id)
{
    struct CacheModel* obj_model = buildcachedat_get_obj_model(game->buildcachedat, obj_id);
    if( obj_model )
    {
        return obj_model;
    }

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(game->buildcachedat, obj_id);
    if( !obj )
    {
        return NULL;
    }
    struct CacheModel* models[12] = { 0 };
    int model_count = 0;

    if( obj->manwear != -1 )
    {
        models[model_count] = buildcachedat_get_model(game->buildcachedat, obj->manwear);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }
    if( obj->manwear2 != -1 )
    {
        models[model_count] = buildcachedat_get_model(game->buildcachedat, obj->manwear2);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }
    if( obj->manwear3 != -1 )
    {
        models[model_count] = buildcachedat_get_model(game->buildcachedat, obj->manwear3);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    buildcachedat_add_obj_model(game->buildcachedat, obj_id, merged);

    for( int i = 0; i < obj->recol_count; i++ )
    {
        model_transform_recolor(merged, obj->recol_s[i], obj->recol_d[i]);
    }

    return merged;
}

static struct DashModel*
player_appearance_model(
    struct GGame* game,
    struct PlayerAppearance* appearance)
{
    struct CacheModel* model = NULL;
    struct AppearanceOp op;
    int model_count = 0;
    struct CacheModel* models[12];
    for( int i = 0; i < 12; i++ )
    {
        model = NULL;
        appearances_decode(&op, appearance->appearance, i);
        switch( op.kind )
        {
        case APPEARANCE_KIND_IDK:
            model = idk_model(game, op.id);
            break;
        case APPEARANCE_KIND_OBJ:
            model = obj_model(game, op.id);
            break;
        default:
            break;
        }
        if( model )
        {
            models[model_count] = model;
            model_count++;
        }
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");

    struct DashModel* dash_model = dashmodel_new_from_cache_model(model_new_copy(merged));
    _light_model_default(dash_model, 0, 0);

    return dash_model;
}

static struct DashModel*
npc_model(
    struct GGame* game,
    int npc_id)
{
    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(game->buildcachedat, npc_id);
    assert(npc && "Npc must be found");

    assert(npc->size > 0 && "Npc size must be greater than 0");

    struct DashModel* dash_model = NULL;
    struct CacheModel* model = buildcachedat_get_npc_model(game->buildcachedat, npc_id);
    if( model )
    {
        model = model_new_copy(model);
        dash_model = dashmodel_new_from_cache_model(model);
        _light_model_default(dash_model, 0, 0);
        return dash_model;
    }

    struct CacheModel* models[20];
    int model_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct CacheModel* got_model = buildcachedat_get_model(game->buildcachedat, npc->models[i]);
        assert(got_model && "Model must be found");

        models[model_count] = got_model;
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);

    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");

    buildcachedat_add_npc_model(game->buildcachedat, npc_id, merged);

    struct CacheModel* copy = model_new_copy(merged);

    for( int i = 0; i < npc->recol_count; i++ )
    {
        model_transform_recolor(copy, npc->recol_s[i], npc->recol_d[i]);
    }

    dash_model = dashmodel_new_from_cache_model(copy);
    _light_model_default(dash_model, 0, 0);

    return dash_model;
}

static struct PktNpcInfoReader npc_info_reader = { 0 };

static void
add_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    npc_info_reader.extended_count = 0;
    npc_info_reader.current_op = 0;
    npc_info_reader.max_ops = 2048;
    struct PktNpcInfoOp ops[2048];
    int count = pkt_npc_info_reader_read(&npc_info_reader, &packet->_npc_info, ops, 2048);

    struct PlayerEntity* player = &game->players[ACTIVE_PLAYER_SLOT];
    if( !player->alive )
        return;

    int npc_id = -1;
    int prev_count = game->npc_count;
    int removed_count = 0;
    game->npc_count = 0;
    struct NPCEntity* npc = NULL;
    for( int i = 0; i < count; i++ )
    {
        struct PktNpcInfoOp* op = &ops[i];

        if( npc_id != -1 )
        {
            npc = &game->npcs[npc_id];
            npc->alive = true;
        }
        else
            npc = NULL;

        switch( op->kind )
        {
        case PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID:
        {
            npc_id = op->_bitvalue;
            game->active_npcs[game->npc_count] = npc_id;
            game->npc_count += 1;
            printf("PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID: %d\n", npc_id);

            break;
        }
        case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        {
            assert(op->_bitvalue >= game->npc_count);
            npc_id = game->active_npcs[op->_bitvalue];
            game->active_npcs[game->npc_count] = npc_id;
            printf(
                "PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX: %d, %d, %d\n",
                npc_id,
                op->_bitvalue,
                game->npc_count);
            game->npc_count += 1;

            break;
        }
        case PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX:
        {
            npc_id = game->active_npcs[op->_bitvalue];
            printf("PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX: %d, %d\n", npc_id, op->_bitvalue);
            break;
        }
        case PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX:
        {
            npc_id = game->active_npcs[op->_bitvalue];
            printf("PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX: %d, %d\n", npc_id, op->_bitvalue);
            memset(&game->npcs[npc_id], 0, sizeof(struct NPCEntity));
            game->active_npcs[op->_bitvalue] = -1;
            npc_id = -1;
            break;
        }
        case PKT_NPC_INFO_OPBITS_COUNT_RESET:
        {
            for( int idx = op->_bitvalue; idx < prev_count; idx++ )
            {
                memset(&game->npcs[game->active_npcs[idx]], 0, sizeof(struct NPCEntity));
            }
            break;
        }
        case PKT_NPC_INFO_OPBITS_DZ:
        {
            int dz = op->_bitvalue;
            npc->position.sz = dz + player->position.sz;
            break;
        }
        case PKT_NPC_INFO_OPBITS_DX:
        {
            int dx = op->_bitvalue;
            npc->position.sx = dx + player->position.sx;
            break;
        }
        case PKT_NPC_INFO_OPBITS_WALKDIR:
        case PKT_NPC_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            int next_x = npc->position.sx;
            int next_z = npc->position.sz;
            if( direction == 0 )
            {
                next_x--;
                next_z++;
            }
            else if( direction == 1 )
            {
                next_z++;
            }
            else if( direction == 2 )
            {
                next_x++;
                next_z++;
            }
            else if( direction == 3 )
            {
                next_x--;
            }
            else if( direction == 4 )
            {
                next_x++;
            }
            else if( direction == 5 )
            {
                next_x--;
                next_z--;
            }
            else if( direction == 6 )
            {
                next_z--;
            }
            else if( direction == 7 )
            {
                next_x++;
                next_z--;
            }
            npc->position.sx = next_x;
            npc->position.sz = next_z;
            break;
        }
        case PKT_NPC_INFO_OPBITS_NPCTYPE:
        {
            if( npc && !npc->scene_element )
            {
                struct SceneElement* scene_element =
                    (struct SceneElement*)malloc(sizeof(struct SceneElement));
                memset(scene_element, 0, sizeof(struct SceneElement));
                scene_element->dash_model = npc_model(game, op->_bitvalue);
                npc->scene_element = scene_element;
                // TODO: Get from npctype.
                npc->size_x = 1;
                npc->size_z = 1;

                struct DashPosition* dash_position =
                    (struct DashPosition*)malloc(sizeof(struct DashPosition));
                memset(dash_position, 0, sizeof(struct DashPosition));
                scene_element->dash_position = dash_position;
            }
            break;
        }
        }
    }
}

static struct PktPlayerInfoReader player_info_reader = { 0 };

void
add_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    //
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, packet->_player_info.data, packet->_player_info.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    struct PktPlayerInfoOp ops[2048];

    struct SceneElement* scene_element = NULL;

    int count = pkt_player_info_reader_read(&player_info_reader, &packet->_player_info, ops, 2048);
    int player_id = ACTIVE_PLAYER_SLOT;

    game->player_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct PktPlayerInfoOp* op = &ops[i];

        struct PlayerEntity* player = &game->players[player_id];
        player->alive = true;

        if( !player->scene_element )
        {
            scene_element = (struct SceneElement*)malloc(sizeof(struct SceneElement));
            memset(scene_element, 0, sizeof(struct SceneElement));

            struct DashPosition* dash_position =
                (struct DashPosition*)malloc(sizeof(struct DashPosition));
            memset(dash_position, 0, sizeof(struct DashPosition));
            scene_element->dash_position = dash_position;

            player->scene_element = (void*)scene_element;
        }
        else
        {
            scene_element = (struct SceneElement*)player->scene_element;
        }

        switch( op->kind )
        {
        case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
        {
            player_id = ACTIVE_PLAYER_SLOT;
            break;
        }
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
        {
            player_id = game->active_players[op->_bitvalue];
            game->player_count += 1;
            break;
        }
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID:
        {
            player_id = op->_bitvalue;
            game->active_players[game->player_count] = player_id;
            game->player_count += 1;
            break;
        }
        case PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX:
        {
            player_id = game->active_players[op->_bitvalue];
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        case PKT_PLAYER_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            int next_x = player->position.sx;
            int next_z = player->position.sz;
            if( direction == 0 )
            {
                next_x--;
                next_z++;
            }
            else if( direction == 1 )
            {
                next_z++;
            }
            else if( direction == 2 )
            {
                next_x++;
                next_z++;
            }
            else if( direction == 3 )
            {
                next_x--;
            }
            else if( direction == 4 )
            {
                next_x++;
            }
            else if( direction == 5 )
            {
                next_x--;
                next_z--;
            }
            else if( direction == 6 )
            {
                next_z--;
            }
            else if( direction == 7 )
            {
                next_x++;
                next_z--;
            }
            player->position.sx = next_x;
            player->position.sz = next_z;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DX:
        {
            int x = game->players[ACTIVE_PLAYER_SLOT].position.sx;

            player->position.sx = op->_bitvalue + x;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DZ:
        {
            int z = game->players[ACTIVE_PLAYER_SLOT].position.sz;

            player->position.sz = op->_bitvalue + z;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_X:
        {
            player->position.sx = op->_bitvalue;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_Z:
        {
            player->position.sz = op->_bitvalue;
            break;
        }
        case PKT_PLAYER_INFO_OP_APPEARANCE:
        {
            struct PlayerAppearance appearance;
            player_appearance_decode(&appearance, op->_appearance.appearance, op->_appearance.len);
            struct DashModel* dash_model = player_appearance_model(game, &appearance);
            scene_element->dash_model = dash_model;
        }
        break;
        }
    }
}

void
gameproto_exec_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    switch( packet->packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
    {
#define SCENE_WIDTH 104
        int zone_padding = SCENE_WIDTH / (2 * 8);
        int zone_sw_x = packet->_map_rebuild.zonex - zone_padding;
        int zone_sw_z = packet->_map_rebuild.zonez - zone_padding;
        int zone_ne_x = packet->_map_rebuild.zonex + zone_padding;
        int zone_ne_z = packet->_map_rebuild.zonez + zone_padding;

        int map_sw_x = (zone_sw_x) / 8;
        int map_sw_z = (zone_sw_z) / 8;
        int map_ne_x = (zone_ne_x) / 8;
        int map_ne_z = (zone_ne_z) / 8;

        // this.sceneBaseTileX = (this.sceneCenterZoneX - 6) * 8;
        // this.sceneBaseTileZ = (this.sceneCenterZoneZ - 6) * 8;
        // const offsetx: number = (this.sceneMapIndex[i] >> 8) * 64 - this.sceneBaseTileX;
        // const offsetz: number = (this.sceneMapIndex[i] & 0xff) * 64 - this.sceneBaseTileZ;

        int base_tile_x = (packet->_map_rebuild.zonex - 6) * 8;
        int base_tile_z = (packet->_map_rebuild.zonez - 6) * 8;

        int levels = MAP_TERRAIN_LEVELS;

        game->sys_painter = painter_new(SCENE_WIDTH, SCENE_WIDTH, levels);
        game->sys_painter_buffer = painter_buffer_new();
        game->sys_minimap = minimap_new(
            zone_sw_x * 8, zone_sw_z * 8, zone_sw_x * 8 + 104, zone_sw_z * 8 + 104, levels);
        game->scenebuilder = scenebuilder_new_painter(game->sys_painter, game->sys_minimap);

        game->scene = scenebuilder_load_from_buildcachedat(
            game->scenebuilder,
            zone_sw_x * 8,
            zone_sw_z * 8,
            zone_ne_x * 8,
            zone_ne_z * 8,
            104,
            104,
            game->buildcachedat);
    }
    break;
    case PKTIN_LC245_2_NPC_INFO:
    {
        add_npc_info(game, packet);
    }
    break;
    case PKTIN_LC245_2_PLAYER_INFO:
    {
        add_player_info(game, packet);
    }
    break;
    default:
        break;
    }
}