#include "entity_scenebuild.h"

#include "dash_utils.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/model_transforms.h"
#include "osrs/scene.h"
#include "rscache/tables/model.h"
#include "rscache/tables_dat/config_idk.h"
#include "rscache/tables_dat/config_npc.h"
#include "rscache/tables_dat/config_obj.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

static void
player_appearance_model(
    struct GGame* game,
    struct PlayerAppearance* appearance,
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

    merged = model_new_merge(models, model_count);
    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");
    dashmodel_move_from_cache_model(dash_model, merged);
    _light_model_default(dash_model, 0, 0);
}

struct PlayerEntity*
entity_scenebuild_player_get(
    struct GGame* game,
    int player_id)
{
    struct PlayerEntity* player = &game->players[player_id];
    if( player->alive )
        return player;

    player->alive = true;
    player->scene_element = (void*)scene_element_new(game->scene);

    return player;
}

void
entity_scenebuild_player_change_appearance(
    struct GGame* game,
    int player_id,
    struct PlayerAppearance* appearance)
{
    struct PlayerEntity* player = entity_scenebuild_player_get(game, player_id);
    struct SceneElement* scene_element = (struct SceneElement*)player->scene_element;

    player->animation.readyanim = appearance->readyanim;
    player->animation.turnanim = appearance->turnanim;
    player->animation.walkanim = appearance->walkanim;
    player->animation.walkanim_b = appearance->walkanim_b;
    player->animation.walkanim_l = appearance->walkanim_l;
    player->animation.walkanim_r = appearance->walkanim_r;
    player->animation.runanim = appearance->runanim;

    // scene_element_reset(scene_element);

    // TODO: Cleanup.
    if( !scene_element->dash_model )
        scene_element->dash_model = dashmodel_new();
    player_appearance_model(game, appearance, scene_element->dash_model);
}

void
entity_scenebuild_player_release(
    struct GGame* game,
    int player_id)
{
    struct PlayerEntity* player = &game->players[player_id];
    assert(player->alive && "Player must be alive");
    scene_element_free((struct SceneElement*)player->scene_element);
    memset(player, 0, sizeof(struct PlayerEntity));
}

struct NPCEntity*
entity_scenebuild_npc_get(
    struct GGame* game,
    int npc_id)
{
    struct NPCEntity* npc = &game->npcs[npc_id];
    if( npc->alive )
        return npc;

    assert(!npc->scene_element && "Npc must not have a scene element");
    npc->alive = true;
    npc->scene_element = (void*)scene_element_new(game->scene);

    return npc;
}

static void
npc_model(
    struct GGame* game,
    int npc_type,
    struct DashModel* dash_model)
{
    struct CacheDatConfigNpc* npc = NULL;
    struct CacheModel* models[20];
    struct CacheModel* copy = NULL;
    struct CacheModel* merged = NULL;

    struct CacheModel* model = buildcachedat_get_npc_model(game->buildcachedat, npc_type);
    if( model )
    {
        dashmodel_move_from_cache_model(dash_model, model_new_copy(model));
        _light_model_default(dash_model, 0, 0);
        return;
    }
    npc = buildcachedat_get_npc(game->buildcachedat, npc_type);

    int model_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct CacheModel* got_model = buildcachedat_get_model(game->buildcachedat, npc->models[i]);
        assert(got_model && "Model must be found");

        models[model_count] = got_model;
        model_count++;
    }

    merged = model_new_merge(models, model_count);

    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");

    buildcachedat_add_npc_model(game->buildcachedat, npc_type, merged);

    copy = model_new_copy(merged);

    for( int i = 0; i < npc->recol_count; i++ )
    {
        model_transform_recolor(copy, npc->recol_s[i], npc->recol_d[i]);
    }

    dashmodel_move_from_cache_model(dash_model, copy);
    _light_model_default(dash_model, 0, 0);
}

void
entity_scenebuild_npc_change_type(
    struct GGame* game,
    int npc_id,
    int npc_type)
{
    struct NPCEntity* npc = entity_scenebuild_npc_get(game, npc_id);
    struct CacheDatConfigNpc* npc_config = buildcachedat_get_npc(game->buildcachedat, npc_type);
    struct SceneElement* scene_element = (struct SceneElement*)npc->scene_element;

    npc->size_x = npc_config->size;
    npc->size_z = npc_config->size;

    npc->animation.readyanim = npc_config->readyanim;
    npc->animation.walkanim = npc_config->walkanim;
    // npc->animation.turnanim = npc->turnanim;
    npc->animation.walkanim_b = npc_config->walkanim_b;
    npc->animation.walkanim_r = npc_config->walkanim_r;
    npc->animation.walkanim_l = npc_config->walkanim_l;

    scene_element_reset(scene_element);
    npc_model(game, npc_type, scene_element->dash_model);
}

void
entity_scenebuild_npc_release(
    struct GGame* game,
    int npc_entity_id)
{
    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];
    assert(npc_entity->alive && "Npc entity must be alive");

    struct SceneElement* scene_element = (struct SceneElement*)npc_entity->scene_element;
    scene_element_free(scene_element);
    npc_entity->scene_element = NULL;
    npc_entity->alive = false;
    memset(npc_entity, 0, sizeof(struct NPCEntity));
}