#include "world_scenebuild.h"

#include "datatypes/appearances.h"
#include "osrs/buildcachedat.h"
#include "osrs/dash_utils.h"
#include "osrs/model_transforms.h"

// clang-format off
#include "osrs/_light_model_default.u.c"
// clang-format on

static struct CacheModel*
obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id)
{
    struct CacheModel* obj_model = buildcachedat_get_obj_model(buildcachedat, obj_id);
    if( obj_model )
    {
        return obj_model;
    }

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
    if( !obj )
    {
        return NULL;
    }
    struct CacheModel* models[12] = { 0 };
    int model_count = 0;

    if( obj->manwear != -1 )
    {
        models[model_count] = buildcachedat_get_model(buildcachedat, obj->manwear);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }
    if( obj->manwear2 != -1 )
    {
        models[model_count] = buildcachedat_get_model(buildcachedat, obj->manwear2);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }
    if( obj->manwear3 != -1 )
    {
        models[model_count] = buildcachedat_get_model(buildcachedat, obj->manwear3);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    buildcachedat_add_obj_model(buildcachedat, obj_id, merged);

    for( int i = 0; i < obj->recol_count; i++ )
    {
        model_transform_recolor(merged, obj->recol_s[i], obj->recol_d[i]);
    }

    return merged;
}

static struct CacheModel*
idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id)
{
    struct CacheModel* idk_model = buildcachedat_get_idk_model(buildcachedat, idk_id);
    if( idk_model )
    {
        return idk_model;
    }

    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(buildcachedat, idk_id);
    if( !idk )
    {
        return NULL;
    }

    struct CacheModel* models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->models_count; i++ )
    {
        models[model_count] = buildcachedat_get_model(buildcachedat, idk->models[i]);
        model_count++;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    buildcachedat_add_idk_model(buildcachedat, idk_id, merged);
    return merged;
}

static void
npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_type,
    struct DashModel* dash_model)
{
    struct CacheDatConfigNpc* npc = NULL;
    struct CacheModel* models[20];
    struct CacheModel* copy = NULL;
    struct CacheModel* merged = NULL;

    struct CacheModel* model = buildcachedat_get_npc_model(buildcachedat, npc_type);
    if( model )
    {
        dashmodel_move_from_cache_model(dash_model, model_new_copy(model));
        _light_model_default(dash_model, 0, 0);
        return;
    }
    npc = buildcachedat_get_npc(buildcachedat, npc_type);

    int model_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct CacheModel* got_model = buildcachedat_get_model(buildcachedat, npc->models[i]);
        assert(got_model && "Model must be found");

        models[model_count] = got_model;
        model_count++;
    }

    merged = model_new_merge(models, model_count);

    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");

    buildcachedat_add_npc_model(buildcachedat, npc_type, merged);

    copy = model_new_copy(merged);

    for( int i = 0; i < npc->recol_count; i++ )
    {
        model_transform_recolor(copy, npc->recol_s[i], npc->recol_d[i]);
    }

    dashmodel_move_from_cache_model(dash_model, copy);
    _light_model_default(dash_model, 0, 0);
}

static void
player_appearance_model(
    struct BuildCacheDat* buildcachedat,
    uint16_t* appearances,
    uint16_t* colors,
    struct DashModel* dash_model)
{
    // assert(dash_model && !dash_model->loaded && "Dash model must be provided");
    assert(dash_model && "Dash model must be provided");
    struct CacheModel* model = NULL;
    struct CacheModel* merged = NULL;
    struct AppearanceOp op;
    int model_count = 0;
    struct CacheModel* models[12];
    for( int i = 0; i < 12; i++ )
    {
        model = NULL;
        appearances_decode(&op, appearances, i);
        switch( op.kind )
        {
        case APPEARANCE_KIND_IDK:
            model = idk_model(buildcachedat, op.id);
            break;
        case APPEARANCE_KIND_OBJ:
            model = obj_model(buildcachedat, op.id);
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

    merged = model_new_merge(models, model_count);
    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");
    dashmodel_move_from_cache_model(dash_model, merged);
    _light_model_default(dash_model, 0, 0);
}

void
world_scenebuild_player_entity_set_appearance(
    struct World* world,
    int player_entity_id,
    uint16_t* appearances,
    uint16_t* colors)
{
    struct PlayerEntity* player = &world->players[player_entity_id];
    struct Scene2Element* element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    assert(element && "Element must be found");

    struct DashModel* dash_model = dashmodel_new();
    player_appearance_model(world->buildcachedat, appearances, colors, dash_model);

    scene2_element_set_dash_model(element, dash_model);
}

void
world_scenebuild_npc_entity_set_npc_type(
    struct World* world,
    int npc_entity_id,
    int npc_type)
{
    struct NPCEntity* npc = &world->npcs[npc_entity_id];
    struct Scene2Element* element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    assert(element && "Element must be found");

    struct DashModel* dash_model = dashmodel_new();
    npc_model(world->buildcachedat, npc_type, dash_model);
    scene2_element_set_dash_model(element, dash_model);
}