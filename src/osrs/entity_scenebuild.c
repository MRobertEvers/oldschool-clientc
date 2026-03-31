#include "entity_scenebuild.h"

#include "dash_utils.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "graphics/dash.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/model_transforms.h"
#include "osrs/world_scenebuild.h"
#include "osrs/zone_state.h"
#include "rscache/tables/model.h"
#include "rscache/tables_dat/config_idk.h"
#include "rscache/tables_dat/config_npc.h"
#include "rscache/tables_dat/config_obj.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

/* Ground object model (obj->model) for world-rendered items. Uses countobj for stack
 * variations (e.g. coin stacks). Returns CacheModel*; caller must not free (cached). */
static struct CacheModel*
obj_ground_model(
    struct GGame* game,
    int obj_id,
    int count)
{
    struct CacheDatConfigObj* obj = buildcachedat_get_obj(game->buildcachedat, obj_id);
    if( !obj )
        return NULL;

    /* Handle count-based object variations (e.g., coin stacks) */
    if( obj->countobj && obj->countco && count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < obj->countobj_count && i < 10; i++ )
        {
            if( count >= obj->countco[i] && obj->countco[i] != 0 )
            {
                countobj_id = obj->countobj[i];
            }
        }
        if( countobj_id != -1 )
            return obj_ground_model(game, countobj_id, 1);
    }

    if( obj->model == 0 || obj->model == -1 )
        return NULL;

    struct CacheModel* model = buildcachedat_get_model(game->buildcachedat, obj->model);
    if( !model )
        return NULL;

    struct CacheModel* copy = model_new_copy(model);
    for( int i = 0; i < obj->recol_count; i++ )
        model_transform_recolor(copy, obj->recol_s[i], obj->recol_d[i]);
    return copy;
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

/** Update or create the SceneElement for the top obj at (level, sx, sz). Client.ts sortObjStacks:
 * top by cost (stackable: cost *= count+1). Call after obj_add, obj_del, obj_count. */
void
entity_scenebuild_obj_stack_update_tile(
    struct GGame* game,
    int level,
    int sx,
    int sz)
{
    // struct ObjStackEntry* head = game->obj_stacks[level][sx][sz];
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

void
entity_scenebuild_player_change_appearance(
    struct GGame* game,
    int player_id,
    struct PlayerAppearance* appearance)
{
    struct PlayerEntity* player = &game->world->players[player_id];
    struct Scene2Element* scene_element =
        scene2_element_at(game->world->scene2, player->scene_element2.element_id);

    /* Store appearance in entity for IF_SETPLAYERHEAD; copy to local player for head model */
    for( int i = 0; i < 12; i++ )
        player->appearance.slots[i] = appearance->appearance[i];
    for( int i = 0; i < 5; i++ )
        player->appearance.colors[i] = appearance->color[i];

    player->animation.readyanim = appearance->readyanim;
    player->animation.turnanim = appearance->turnanim;
    player->animation.walkanim = appearance->walkanim;
    player->animation.walkanim_b = appearance->walkanim_b;
    player->animation.walkanim_l = appearance->walkanim_l;
    player->animation.walkanim_r = appearance->walkanim_r;
    player->animation.runanim = appearance->runanim;

    if( !scene_element->dash_model )
        scene_element->dash_model = dashmodel_new();
    player_appearance_model(game, appearance, scene_element->dash_model);
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

/* Head model helpers for chat/interface MODEL components.
 * Client.ts: IdkType.getHeadModel(), ObjType.getHeadModel(), NpcType.getHeadModel(),
 * ClientPlayer.getHeadModel() */

static struct CacheModel*
idk_head_model(
    struct GGame* game,
    int idk_id)
{
    struct CacheDatConfigIdk* idk = buildcachedat_get_idk(game->buildcachedat, idk_id);
    if( !idk )
    {
        printf("idk_head_model: idk_id=%d not found in buildcachedat\n", idk_id);
        return NULL;
    }

    struct CacheModel* models[5] = { 0 };
    int model_count = 0;
    for( int i = 0; i < 5; i++ )
    {
        if( idk->heads[i] != -1 && idk->heads[i] != 0 )
        {
            struct CacheModel* m = buildcachedat_get_model(game->buildcachedat, idk->heads[i]);
            if( m )
                models[model_count++] = m;
            else
                printf(
                    "idk_head_model: idk_id=%d heads[%d]=%d not loaded\n",
                    idk_id,
                    i,
                    idk->heads[i]);
        }
    }
    if( model_count == 0 )
    {
        printf("idk_head_model: idk_id=%d no head models could be loaded\n", idk_id);
        return NULL;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    for( int i = 0; i < 10 && idk->recol_s[i] != 0; i++ )
        model_transform_recolor(merged, idk->recol_s[i], idk->recol_d[i]);
    return merged;
}

static struct CacheModel*
obj_head_model(
    struct GGame* game,
    int obj_id,
    int gender)
{
    struct CacheDatConfigObj* obj = buildcachedat_get_obj(game->buildcachedat, obj_id);
    if( !obj )
    {
        printf("obj_head_model: obj_id=%d not found in buildcachedat\n", obj_id);
        return NULL;
    }

    int head1 = gender == 1 ? obj->womanhead : obj->manhead;
    int head2 = gender == 1 ? obj->womanhead2 : obj->manhead2;

    if( head1 == -1 )
    {
        printf("obj_head_model: obj_id=%d has no head model (manhead=-1)\n", obj_id);
        return NULL;
    }

    struct CacheModel* m1 = buildcachedat_get_model(game->buildcachedat, head1);
    if( !m1 )
    {
        printf("obj_head_model: obj_id=%d head model %d not loaded\n", obj_id, head1);
        return NULL;
    }

    if( head2 != -1 )
    {
        struct CacheModel* m2 = buildcachedat_get_model(game->buildcachedat, head2);
        if( m2 )
        {
            struct CacheModel* models[2] = { m1, m2 };
            m1 = model_new_merge(models, 2);
        }
    }

    for( int i = 0; i < obj->recol_count; i++ )
        model_transform_recolor(m1, obj->recol_s[i], obj->recol_d[i]);

    return m1;
}

/* Build player head model from appearance (ClientPlayer.getHeadModel).
 * Uses slots as uint16_t appearance values. */
static struct CacheModel*
player_head_model(
    struct GGame* game,
    int* slots,
    int* colors)
{
    struct AppearanceOp op;
    struct CacheModel* models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < 12; i++ )
    {
        uint16_t appearance_storage[12];
        for( int j = 0; j < 12; j++ )
            appearance_storage[j] = (uint16_t)slots[j];
        appearances_decode(&op, appearance_storage, i);
        struct CacheModel* model = NULL;
        switch( op.kind )
        {
        case APPEARANCE_KIND_IDK:
            model = idk_head_model(game, op.id);
            break;
        case APPEARANCE_KIND_OBJ:
            model = obj_head_model(game, op.id, 0);
            break;
        default:
            break;
        }
        if( model )
            models[model_count++] = model;
    }
    if( model_count == 0 )
        return NULL;

    struct CacheModel* merged = model_new_merge(models, model_count);
    /* TODO: apply colors (recol_s/recol_d from design) for hair, torso, etc. */
    (void)colors;
    return merged;
}

/* Build NPC head model from npc type (NpcType.getHeadModel). */
static struct CacheModel*
npc_head_model(
    struct GGame* game,
    int npc_type)
{
    struct CacheDatConfigNpc* npc = buildcachedat_get_npc(game->buildcachedat, npc_type);
    if( !npc )
    {
        printf("npc_head_model: npc_type=%d not found in buildcachedat\n", npc_type);
        return NULL;
    }
    if( !npc->heads )
    {
        printf("npc_head_model: npc_type=%d has no heads array\n", npc_type);
        return NULL;
    }

    struct CacheModel* models[16] = { 0 };
    int model_count = 0;
    for( int i = 0; i < npc->heads_count; i++ )
    {
        if( npc->heads[i] != -1 )
        {
            struct CacheModel* m = buildcachedat_get_model(game->buildcachedat, npc->heads[i]);
            if( m )
                models[model_count++] = m;
            else
                printf(
                    "npc_head_model: npc_type=%d heads[%d]=%d not loaded\n",
                    npc_type,
                    i,
                    npc->heads[i]);
        }
    }
    if( model_count == 0 )
    {
        printf("npc_head_model: npc_type=%d no head models could be loaded\n", npc_type);
        return NULL;
    }

    struct CacheModel* merged = model_new_merge(models, model_count);
    for( int i = 0; i < npc->recol_count; i++ )
        model_transform_recolor(merged, npc->recol_s[i], npc->recol_d[i]);

    return merged;
}

/* Get head model as DashModel for rendering. Caller must free dash_model via dashmodel_free. */
struct DashModel*
entity_scenebuild_head_model_for_component(
    struct GGame* game,
    int model_type,
    int model_id,
    int* slots,
    int* colors)
{
    struct CacheModel* cache = NULL;
    if( model_type == 2 )
        cache = npc_head_model(game, model_id);
    else if( model_type == 3 && slots )
        cache = player_head_model(game, slots, colors);

    if( !cache )
        return NULL;

    struct DashModel* dash = dashmodel_new_from_cache_model(cache);
    if( dash )
        _light_model_default(dash, 0, 0);
    return dash;
}