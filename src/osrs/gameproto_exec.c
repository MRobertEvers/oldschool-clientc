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

    struct CacheModel* model = buildcachedat_get_npc_model(game->buildcachedat, npc_id);
    struct DashModel* dash_model = NULL;
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

static void
npc_move(
    struct GGame* game,
    int npc_entity_id,
    int x,
    int z)
{
    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];

    // Hack
    struct SceneElement* scene_element = (struct SceneElement*)npc_entity->scene_element;

    int dx = x / 128 - npc_entity->pathing.route_x[0];
    int dz = z / 128 - npc_entity->pathing.route_z[0];
    int animatable_distance = dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8;
    if( animatable_distance )
    {
        if( npc_entity->pathing.route_length < 9 )
            npc_entity->pathing.route_length++;

        for( int i = npc_entity->pathing.route_length; i > 0; i-- )
        {
            npc_entity->pathing.route_x[i] = npc_entity->pathing.route_x[i - 1];
            npc_entity->pathing.route_z[i] = npc_entity->pathing.route_z[i - 1];
        }
        npc_entity->pathing.route_x[0] = x / 128;
        npc_entity->pathing.route_z[0] = z / 128;
        npc_entity->pathing.route_run[0] = 0;
    }
    else
    {
        npc_entity->pathing.route_length = 0;
        npc_entity->pathing.route_x[0] = x / 128;
        npc_entity->pathing.route_z[0] = z / 128;

        npc_entity->position.x = x + 64;
        npc_entity->position.z = z + 64;
    }

    scene_element->dash_position->x = npc_entity->position.x;
    scene_element->dash_position->z = npc_entity->position.z;
}

static void
init_npc_entity(
    struct GGame* game,
    int npc_entity_id,
    int npc_id)
{
    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(game->buildcachedat, npc_id);
    assert(npc && "Npc must be found");

    struct DashModel* dash_model = npc_model(game, npc_id);

    struct SceneElement* scene_element = (struct SceneElement*)malloc(sizeof(struct SceneElement));
    memset(scene_element, 0, sizeof(struct SceneElement));
    scene_element->dash_model = dash_model;

    struct DashPosition* dash_position = (struct DashPosition*)malloc(sizeof(struct DashPosition));
    memset(dash_position, 0, sizeof(struct DashPosition));
    scene_element->dash_position = dash_position;

    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];
    npc_entity->scene_element = (void*)scene_element;
    npc_entity->size_x = npc->size;
    npc_entity->size_z = npc->size;

    npc_entity->animation.readyanim = npc->readyanim;
    npc_entity->animation.walkanim = npc->walkanim;
    // npc_entity->animation.turnanim = npc->turnanim;
    npc_entity->animation.walkanim_b = npc->walkanim_b;
    npc_entity->animation.walkanim_r = npc->walkanim_r;
    npc_entity->animation.walkanim_l = npc->walkanim_l;
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
            npc->position.z = (dz * 128) + (player->position.z / 128 * 128);
            npc_move(game, npc_id, npc->position.x, npc->position.z);
            break;
        }
        case PKT_NPC_INFO_OPBITS_DX:
        {
            int dx = op->_bitvalue;
            npc->position.x = (dx * 128) + (player->position.x / 128 * 128);
            break;
        }
        case PKT_NPC_INFO_OPBITS_WALKDIR:
        case PKT_NPC_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            int next_x = npc->pathing.route_x[0];
            int next_z = npc->pathing.route_z[0];
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

            if( npc->pathing.route_length < 9 )
                npc->pathing.route_length++;

            for( int i = npc->pathing.route_length; i > 0; i-- )
            {
                npc->pathing.route_x[i] = npc->pathing.route_x[i - 1];
                npc->pathing.route_z[i] = npc->pathing.route_z[i - 1];
                npc->pathing.route_run[i] = npc->pathing.route_run[i - 1];
            }

            npc->pathing.route_x[0] = next_x;
            npc->pathing.route_z[0] = next_z;
            npc->pathing.route_run[0] = 0;
            break;
        }
        case PKT_NPC_INFO_OPBITS_NPCTYPE:
        {
            if( npc && !npc->scene_element )
            {
                init_npc_entity(game, npc_id, op->_bitvalue);
            }
            break;
        }
        }
    }
}

void
gameproto_exec_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    add_npc_info(game, packet);
}

static struct PktPlayerInfoReader player_info_reader = { 0 };

static void
player_move(
    struct GGame* game,
    int player_id,
    int x,
    int z)
{
    struct PlayerEntity* player = &game->players[player_id];

    // Hack
    struct SceneElement* scene_element = (struct SceneElement*)player->scene_element;

    int dx = x / 128 - player->pathing.route_x[0];
    int dz = z / 128 - player->pathing.route_z[0];
    int animatable_distance = dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8;
    if( animatable_distance )
    {
        if( player->pathing.route_length < 9 )
            player->pathing.route_length++;

        for( int i = player->pathing.route_length; i > 0; i-- )
        {
            player->pathing.route_x[i] = player->pathing.route_x[i - 1];
            player->pathing.route_z[i] = player->pathing.route_z[i - 1];
        }
        player->pathing.route_x[0] = x / 128;
        player->pathing.route_z[0] = z / 128;
        player->pathing.route_run[0] = 0;
    }
    else
    {
        player->pathing.route_length = 0;
        player->pathing.route_x[0] = x / 128;
        player->pathing.route_z[0] = z / 128;

        player->position.x = x + 64;
        player->position.z = z + 64;
    }
}

// static void
// init_player_entity(
//     struct GGame* game,
//     int player_id,
//     struct PlayerAppearance* appearance)
// {
//     struct SceneElement* scene_element = (struct SceneElement*)malloc(sizeof(struct
//     SceneElement)); memset(scene_element, 0, sizeof(struct SceneElement));
//     scene_element->dash_model = dash_model;

//     struct DashPosition* dash_position = (struct DashPosition*)malloc(sizeof(struct
//     DashPosition)); memset(dash_position, 0, sizeof(struct DashPosition));
//     scene_element->dash_position = dash_position;

//     struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];
//     npc_entity->scene_element = (void*)scene_element;
//     npc_entity->size_x = npc->size;
//     npc_entity->size_z = npc->size;

//     npc_entity->animation.readyanim = npc->readyanim;
//     npc_entity->animation.walkanim = npc->walkanim;
//     // npc_entity->animation.turnanim = npc->turnanim;
//     npc_entity->animation.walkanim_b = npc->walkanim_b;
//     npc_entity->animation.walkanim_r = npc->walkanim_r;
//     npc_entity->animation.walkanim_l = npc->walkanim_l;
// }

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
            game->active_players[game->player_count] = player_id;
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
        case PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX:
        {
            player_id = game->active_players[op->_bitvalue];
            // TODO: Free player
            memset(&game->players[player_id], 0, sizeof(struct PlayerEntity));
            game->active_players[op->_bitvalue] = -1;
            player_id = -1;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        case PKT_PLAYER_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            int next_x = player->pathing.route_x[0];
            int next_z = player->pathing.route_z[0];
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

            if( player->pathing.route_length < 9 )
                player->pathing.route_length++;

            for( int i = player->pathing.route_length; i > 0; i-- )
            {
                player->pathing.route_x[i] = player->pathing.route_x[i - 1];
                player->pathing.route_z[i] = player->pathing.route_z[i - 1];
            }

            player->pathing.route_x[0] = next_x;
            player->pathing.route_z[0] = next_z;
            player->pathing.route_run[0] = 0;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DX:
        {
            int x = (game->players[ACTIVE_PLAYER_SLOT].position.x / 128 * 128);

            player->position.x = (op->_bitvalue * 128) + x;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DZ:
        {
            int z = (game->players[ACTIVE_PLAYER_SLOT].position.z / 128 * 128);

            player->position.z = (op->_bitvalue * 128) + z;
            player_move(game, player_id, player->position.x, player->position.z);
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_X:
        {
            player->position.x = (op->_bitvalue * 128) + 64;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_Z:
        {
            player->position.z = (op->_bitvalue * 128) + 64;
            break;
        }
        case PKT_PLAYER_INFO_OP_APPEARANCE:
        {
            struct PlayerAppearance appearance;
            player_appearance_decode(&appearance, op->_appearance.appearance, op->_appearance.len);
            player->animation.readyanim = appearance.readyanim;
            player->animation.turnanim = appearance.turnanim;
            player->animation.walkanim = appearance.walkanim;
            player->animation.walkanim_b = appearance.walkanim_b;
            player->animation.walkanim_l = appearance.walkanim_l;
            player->animation.walkanim_r = appearance.walkanim_r;
            player->animation.runanim = appearance.runanim;
            struct DashModel* dash_model = player_appearance_model(game, &appearance);
            scene_element->dash_model = dash_model;
        }
        break;
        }
    }
}

void
gameproto_exec_rebuild_normal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
#define SCENE_WIDTH 104
    int zone_padding = SCENE_WIDTH / (2 * 8);
    int zone_sw_x = packet->_map_rebuild.zonex - zone_padding;
    int zone_sw_z = packet->_map_rebuild.zonez - zone_padding;
    int zone_ne_x = packet->_map_rebuild.zonex + zone_padding;
    int zone_ne_z = packet->_map_rebuild.zonez + zone_padding;

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

void
gameproto_exec_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    add_player_info(game, packet);
}

void
gameproto_exec_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    switch( packet->packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
        gameproto_exec_rebuild_normal(game, packet);
        break;
    case PKTIN_LC245_2_NPC_INFO:
    {
        add_npc_info(game, packet);
    }
    break;
    case PKTIN_LC245_2_PLAYER_INFO:
        gameproto_exec_player_info(game, packet);
        break;
    default:
        break;
    }
}