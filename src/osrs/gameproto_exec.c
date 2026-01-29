#include "gameproto_exec.h"

#include "dashlib.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "osrs/_light_model_default.u.c"
#include "packets/pkt_player_info.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "rscache/tables/model.h"

static int npc_sizes[8096] = { 0 };

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
        model_count++;
    }
    if( obj->manwear2 != -1 )
    {
        models[model_count] = buildcachedat_get_model(game->buildcachedat, obj->manwear2);
        model_count++;
    }
    if( obj->manwear3 != -1 )
    {
        models[model_count] = buildcachedat_get_model(game->buildcachedat, obj->manwear3);
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    buildcachedat_add_obj_model(game->buildcachedat, obj_id, merged);
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

    struct DashModel* dash_model = dashmodel_new_from_cache_model(merged);
    _light_model_default(dash_model, 0, 0);

    return dash_model;
}

static struct DashModel*
npc_model(
    struct GGame* game,
    int npc_id,
    int npc_idx)
{
    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(game->buildcachedat, npc_id);
    assert(npc && "Npc must be found");

    assert(npc->size > 0 && "Npc size must be greater than 0");
    npc_sizes[npc_idx] = npc->size;

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

    dash_model = dashmodel_new_from_cache_model(copy);
    _light_model_default(dash_model, 0, 0);

    return dash_model;
}

static struct DashModel* models[8096] = { 0 };

static int sxs[8096] = { 0 };
static int szs[8096] = { 0 };

static void
add_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, packet->_npc_info.data, packet->_npc_info.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    int count = gbits(&buf, 8);
    for( int i = 0; i < count; i++ )
    {
        //
    }

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

    static int g_npcs[2048];
    int npc_count = 0;

    while( ((buf.byte_position * 8) + buf.bit_offset + 21) < packet->_npc_info.length * 8 )
    {
        int npc_idx = gbits(&buf, 13);
        if( npc_idx == 8191 )
        {
            break;
        }
        g_npcs[npc_count] = npc_idx;

        int npc_id = gbits(&buf, 11);

        models[npc_idx] = npc_model(game, npc_id, npc_idx);
        assert(npc_sizes[npc_idx] > 0 && "Npc size must be greater than 0");

        int dx = gbits(&buf, 5);
        if( dx > 15 )
            dx -= 32;
        int dy = gbits(&buf, 5);
        if( dy > 15 )
            dy -= 32;

        sxs[npc_idx] = dx;
        szs[npc_idx] = dy;

        int has_extended_info = gbits(&buf, 1);
        npc_count++;
    }

    struct PlayerEntity* player = &game->players[ACTIVE_PLAYER_SLOT];

    for( int i = 0; i < npc_count; i++ )
    {
        int idx = g_npcs[i];
        struct NPCEntity* npc = &game->npcs[idx];

        if( player->alive )
        {
            int dx = sxs[idx];
            int dz = szs[idx];
            int size_x = npc_sizes[idx];
            int size_z = npc_sizes[idx];

            int x = player->position.sx + dx;
            int z = player->position.sz + dz;

            npc->alive = true;
            npc->position.sx = x;
            npc->position.sz = z;
            npc->size_x = size_x;
            npc->size_z = size_z;

            struct SceneElement* scene_element = NULL;
            if( npc->scene_element )
            {
                scene_element = npc->scene_element;
            }
            else
            {
                scene_element = (struct SceneElement*)malloc(sizeof(struct SceneElement));
                memset(scene_element, 0, sizeof(struct SceneElement));
                scene_element->dash_model = models[idx];

                struct DashPosition* dash_position = NULL;
                dash_position = (struct DashPosition*)malloc(sizeof(struct DashPosition));
                memset(dash_position, 0, sizeof(struct DashPosition));

                scene_element->dash_position = dash_position;

                npc->scene_element = scene_element;
            }

            scene_element->dash_position->x = x * 128 + 64 * size_x;
            scene_element->dash_position->z = z * 128 + 64 * size_z;
            scene_element->dash_position->y = scene_terrain_height_center(game->scene, x, z, 0);
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
    int is_local_player = false;
    for( int i = 0; i < count; i++ )
    {
        struct PktPlayerInfoOp* op = &ops[i];

        int player_idx = is_local_player ? ACTIVE_PLAYER_SLOT : ACTIVE_PLAYER_SLOT;
        struct PlayerEntity* player = &game->players[player_idx];

        struct SceneElement* scene_element = NULL;
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
        case PKT_PLAYER_INFO_MODE_LOCAL_PLAYER:
        {
            is_local_player = true;
            player->alive = true;
            break;
        }
        case PKT_PLAYER_INFO_MODE_PLAYER_ENTITY_IDX:
        case PKT_PLAYER_INFO_MODE_PLAYER_ENTITY_ID:
        {
            is_local_player = false;
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