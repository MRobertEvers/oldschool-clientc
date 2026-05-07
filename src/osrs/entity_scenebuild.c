#include "entity_scenebuild.h"

#include "dash_utils.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "graphics/dash.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/collision_map.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/heightmap.h"
#include "osrs/model_transforms.h"
#include "osrs/world.h"
#include "osrs/world_scenebuild.h"
#include "osrs/zone_state.h"
#include "rscache/tables/config_locs.h"
#include "rscache/tables/maps.h"
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
#include <string.h>

static struct GameCacheModel*
idk_model(
    struct GGame* game,
    int idk_id)
{
    struct GameCacheModel* idk_model = gamecache_get_idk_model(game->gamecache, idk_id);
    if( idk_model )
    {
        return idk_model;
    }

    struct GameCacheIdk* idk = gamecache_get_idk(game->gamecache, idk_id);
    if( !idk )
    {
        return NULL;
    }

    struct GameCacheModel* models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < idk->models_count; i++ )
    {
        models[model_count] = gamecache_get_model(game->gamecache, idk->models[i]);
        model_count++;
    }

    struct GameCacheModel* merged = gamecache_model_new_merge(models, model_count);
    gamecache_add_idk_model(game->gamecache, idk_id, merged);
    return merged;
}

/* Ground object model (obj->model) for world-rendered items. Uses countobj for stack
 * variations (e.g. coin stacks). Returns a heap copy consumed by dashmodel_move_from_gamecache_model.
 */
static struct GameCacheModel*
obj_ground_model_gc(
    struct GameCache* gc,
    int obj_id,
    int count)
{
    if( !gc )
        return NULL;
    struct GameCacheObj* obj = gamecache_get_obj(gc, obj_id);
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
            return obj_ground_model_gc(gc, countobj_id, 1);
    }

    if( obj->model == 0 || obj->model == -1 )
        return NULL;

    struct GameCacheModel* model = gamecache_get_model(gc, obj->model);
    if( !model )
        return NULL;

    struct GameCacheModel* copy = gamecache_model_new_copy(model);
    for( int i = 0; i < obj->recol_count; i++ )
        gamecache_model_transform_recolor(copy, obj->recol_s[i], obj->recol_d[i]);
    return copy;
}

static struct GameCacheModel*
obj_ground_model(
    struct GGame* game,
    int obj_id,
    int count)
{
    if( !game || !game->gamecache )
        return NULL;
    return obj_ground_model_gc(game->gamecache, obj_id, count);
}

static struct GameCacheModel*
obj_model(
    struct GGame* game,
    int obj_id)
{
    struct GameCacheModel* obj_model = gamecache_get_obj_model(game->gamecache, obj_id);
    if( obj_model )
    {
        return obj_model;
    }

    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id);
    if( !obj )
    {
        return NULL;
    }
    struct GameCacheModel* models[12] = { 0 };
    int model_count = 0;

    if( obj->manwear != -1 )
    {
        models[model_count] = gamecache_get_model(game->gamecache, obj->manwear);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }
    if( obj->manwear2 != -1 )
    {
        models[model_count] = gamecache_get_model(game->gamecache, obj->manwear2);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }
    if( obj->manwear3 != -1 )
    {
        models[model_count] = gamecache_get_model(game->gamecache, obj->manwear3);
        assert(models[model_count] && "Model must be found");
        model_count++;
    }

    struct GameCacheModel* merged = gamecache_model_new_merge(models, model_count);
    gamecache_add_obj_model(game->gamecache, obj_id, merged);

    for( int i = 0; i < obj->recol_count; i++ )
    {
        gamecache_model_transform_recolor(merged, obj->recol_s[i], obj->recol_d[i]);
    }

    return merged;
}

static int64_t
obj_stack_sort_key(
    struct GameCache* gc,
    struct ObjStackEntry* e)
{
    struct GameCacheObj* o = gamecache_get_obj(gc, e->obj_id);
    if( !o )
        return 0;
    int64_t c = (int64_t)o->cost;
    if( o->stackable )
        c *= (int64_t)(e->count + 1);
    return c;
}

static void
obj_stack_move_top_to_front(
    struct GameCache* gc,
    struct ObjStackEntry** head_out)
{
    struct ObjStackEntry* list = *head_out;
    if( !list || !list->next )
        return;

    struct ObjStackEntry* top = list;
    int64_t top_key = obj_stack_sort_key(gc, top);
    for( struct ObjStackEntry* e = list->next; e; e = e->next )
    {
        int64_t k = obj_stack_sort_key(gc, e);
        if( k > top_key )
        {
            top_key = k;
            top = e;
        }
    }

    if( top == list )
        return;

    struct ObjStackEntry* prev = list;
    while( prev->next != top )
        prev = prev->next;
    prev->next = top->next;
    top->next = list;
    *head_out = top;
}

/** Client.ts showObject: bottom = first distinct obj_id after top; middle = next distinct. */
static void
obj_stack_pick_bottom_middle(
    struct ObjStackEntry* head,
    struct ObjStackEntry** bottom_out,
    struct ObjStackEntry** middle_out)
{
    *bottom_out = NULL;
    *middle_out = NULL;
    if( !head )
        return;

    int top_id = head->obj_id;
    for( struct ObjStackEntry* obj = head; obj; obj = obj->next )
    {
        if( obj->obj_id != top_id && !*bottom_out )
            *bottom_out = obj;

        if( obj->obj_id != top_id && *bottom_out && obj->obj_id != (*bottom_out)->obj_id &&
            !*middle_out )
            *middle_out = obj;
    }
}

static void
obj_stack_scene_release_layer(
    struct World* w,
    struct ObjStackEntity* ent,
    int layer)
{
    if( ent->scene_element[layer].element_id >= 0 && w->scene2 )
        scene2_element_release(w->scene2, ent->scene_element[layer].element_id);
    ent->scene_element[layer].element_id = -1;
}

/** Update Scene2 for dropped items at (level, sx, sz). Client.ts showObject + World.setObj:
 * move highest-cost entry to front; bottom/middle = next distinct obj_ids; three painter layers. */
void
entity_scenebuild_obj_stack_update_tile(
    struct GGame* game,
    int level,
    int sx,
    int sz)
{
    if( !game || !game->world )
        return;

    struct World* w = game->world;
    if( !w->obj_stacks || !w->obj_stack_entity_by_tile )
        return;
    if( level < 0 || level >= MAP_TERRAIN_LEVELS )
        return;
    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;

    struct ObjStackEntry** head_cell = &w->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
    obj_stack_move_top_to_front(game->gamecache, head_cell);
    struct ObjStackEntry* head = *head_cell;
    int tflat = world_obj_stack_tile_flat(level, sx, sz);

    if( !head )
    {
        int eid = w->obj_stack_entity_by_tile[tflat];
        if( eid >= 0 )
        {
            world_cleanup_obj_stack_entity(w, eid);
            w->obj_stack_entity_by_tile[tflat] = -1;
        }
        return;
    }

    struct ObjStackEntry* bottom_e = NULL;
    struct ObjStackEntry* middle_e = NULL;
    obj_stack_pick_bottom_middle(head, &bottom_e, &middle_e);
    struct ObjStackEntry* layer_entry[3] = { bottom_e, middle_e, head };

    int eid = w->obj_stack_entity_by_tile[tflat];
    if( eid < 0 )
    {
        eid = world_obj_stack_entity_create(w);
        if( eid < 0 )
            return;
        w->obj_stack_entity_by_tile[tflat] = eid;
    }

    struct ObjStackEntity* ent = world_obj_stack_entity(w, eid);
    ent->world_tile_x = sx;
    ent->world_tile_z = sz;
    ent->level = level;
    ent->entity_id = eid;

    ent->scene_coord.sx = (uint32_t)sx;
    ent->scene_coord.sz = (uint32_t)sz;
    ent->scene_coord.slevel = (uint32_t)(level & 0xf);

    int wx = sx * 128 + 64;
    int wz = sz * 128 + 64;
    int ground_y = w->heightmap ? heightmap_get_interpolated(w->heightmap, wx, wz, level) : 0;

    for( int layer = 0; layer < 3; layer++ )
    {
        struct ObjStackEntry* entry = layer_entry[layer];
        if( !entry )
        {
            obj_stack_scene_release_layer(w, ent, layer);
            continue;
        }

        struct GameCacheModel* cm = obj_ground_model(game, entry->obj_id, entry->count);
        if( !cm )
        {
            world_cleanup_obj_stack_entity(w, eid);
            w->obj_stack_entity_by_tile[tflat] = -1;
            return;
        }

        int total_el = scene2_elements_total(w->scene2);
        int sid = ent->scene_element[layer].element_id;
        if( sid < 0 || sid >= total_el )
        {
            ent->scene_element[layer].element_id = scene2_element_acquire_fast(
                w->scene2,
                (int)entity_obj_stack_unified_id(eid, layer),
                SCENE2_ELEMENT_SCENERY,
                0,
                0);
            sid = ent->scene_element[layer].element_id;
        }

        struct Scene2Element* se = scene2_element_at(w->scene2, sid);
        scene2_element_expect(se, "entity_scenebuild_obj_stack_update_tile");

        if( !scene2_element_dash_model(se) )
            scene2_element_set_dash_model(w->scene2, se, dashmodel_new());
        dashmodel_move_from_gamecache_model(scene2_element_dash_model(se), cm);
        _light_model_default(scene2_element_dash_model(se), 0, 0);

        if( !scene2_element_dash_position(se) )
            scene2_element_set_dash_position_ptr(se, dashposition_new());
        struct DashPosition* pos = scene2_element_dash_position(se);
        pos->x = wx;
        pos->z = wz;
        pos->yaw = 0;
        pos->y = ground_y;
    }
}

/** Each frame: re-sort stack, refresh DashModel from cache, Y from heightmap — mirrors
 * world_scenebuild_npc_entity_reload_scene2_model before painter emit. */
void
entity_scenebuild_obj_stack_reload_scene2_for_active_entity(
    struct World* w,
    struct GameCache* gc,
    int eid)
{
    if( !w || !gc || !w->scene2 || !w->obj_stacks || !w->obj_stack_entity_by_tile )
        return;
    if( eid < 0 || eid >= entity_vec_count(&w->obj_stack_entities) )
        return;

    struct ObjStackEntity* ent = world_obj_stack_entity(w, eid);
    if( !ent->alive )
        return;

    int level = ent->level;
    int sx    = ent->world_tile_x;
    int sz    = ent->world_tile_z;
    if( level < 0 || level >= MAP_TERRAIN_LEVELS || sx < 0 || sz < 0 || sx >= ZONE_SCENE_SIZE ||
        sz >= ZONE_SCENE_SIZE )
        return;

    int tflat = world_obj_stack_tile_flat(level, sx, sz);
    if( w->obj_stack_entity_by_tile[tflat] != eid )
        return;

    struct ObjStackEntry** head_cell = &w->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
    obj_stack_move_top_to_front(gc, head_cell);
    struct ObjStackEntry* head = *head_cell;

    if( !head )
    {
        world_cleanup_obj_stack_entity(w, eid);
        w->obj_stack_entity_by_tile[tflat] = -1;
        return;
    }

    struct ObjStackEntry* bottom_e = NULL;
    struct ObjStackEntry* middle_e = NULL;
    obj_stack_pick_bottom_middle(head, &bottom_e, &middle_e);
    struct ObjStackEntry* layer_entry[3] = { bottom_e, middle_e, head };

    int wx = sx * 128 + 64;
    int wz = sz * 128 + 64;
    int ground_y = w->heightmap ? heightmap_get_interpolated(w->heightmap, wx, wz, level) : 0;

    for( int layer = 0; layer < 3; layer++ )
    {
        struct ObjStackEntry* entry = layer_entry[layer];
        if( !entry )
        {
            obj_stack_scene_release_layer(w, ent, layer);
            continue;
        }

        struct GameCacheModel* cm = obj_ground_model_gc(gc, entry->obj_id, entry->count);
        if( !cm )
        {
            world_cleanup_obj_stack_entity(w, eid);
            w->obj_stack_entity_by_tile[tflat] = -1;
            return;
        }

        int total_el = scene2_elements_total(w->scene2);
        int sid      = ent->scene_element[layer].element_id;
        if( sid < 0 || sid >= total_el )
        {
            ent->scene_element[layer].element_id = scene2_element_acquire_fast(
                w->scene2,
                (int)entity_obj_stack_unified_id(eid, layer),
                SCENE2_ELEMENT_SCENERY,
                0,
                0);
            sid = ent->scene_element[layer].element_id;
        }

        struct Scene2Element* se = scene2_element_at(w->scene2, sid);
        scene2_element_expect(se, "entity_scenebuild_obj_stack_reload_scene2_for_active_entity");

        if( !scene2_element_dash_model(se) )
            scene2_element_set_dash_model(w->scene2, se, dashmodel_new());
        dashmodel_move_from_gamecache_model(scene2_element_dash_model(se), cm);
        _light_model_default(scene2_element_dash_model(se), 0, 0);

        if( !scene2_element_dash_position(se) )
            scene2_element_set_dash_position_ptr(se, dashposition_new());
        struct DashPosition* pos = scene2_element_dash_position(se);
        pos->x   = wx;
        pos->z   = wz;
        pos->yaw = 0;
        pos->y   = ground_y;
    }
}

static void
player_appearance_model(
    struct GGame* game,
    struct PlayerAppearance* appearance,
    struct EntityAnimationStep const* primary_anim,
    struct DashModel* dash_model)
{
    // assert(dash_model && !dash_model->loaded && "Dash model must be provided");
    assert(dash_model && "Dash model must be provided");
    struct GameCacheModel* model = NULL;
    struct GameCacheModel* merged = NULL;
    struct AppearanceOp op;
    int model_count = 0;
    struct GameCacheModel* models[12];
    uint16_t effective[12];
    for( int i = 0; i < 12; i++ )
    {
        effective[i] = appearance->appearance[i];
    }
    if( game->gamecache && game->gamecache->sequences_hmap && primary_anim )
    {
        uint16_t anim_id = primary_anim->anim_id;
        if( anim_id != 0 && anim_id != (uint16_t)-1 && primary_anim->delay == 0 )
        {
            struct GameCacheSequence* seq = gamecache_get_sequence(game->gamecache, (int)anim_id);
            if( seq )
            {
                if( seq->replaceheldright >= 0 )
                {
                    effective[3] = (uint16_t)seq->replaceheldright;
                }
                if( seq->replaceheldleft >= 0 )
                {
                    effective[5] = (uint16_t)seq->replaceheldleft;
                }
            }
        }
    }
    for( int i = 0; i < 12; i++ )
    {
        model = NULL;
        appearances_decode(&op, effective, i);
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

    merged = gamecache_model_new_merge(models, model_count);
    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");
    dashmodel_move_from_gamecache_model(dash_model, merged);
    _light_model_default(dash_model, 0, 0);
}

void
entity_scenebuild_player_change_appearance(
    struct GGame* game,
    int player_id,
    struct PlayerAppearance* appearance)
{
    struct PlayerEntity* player = world_player(game->world, player_id);
    struct Scene2Element* scene_element =
        scene2_element_at(game->world->scene2, player->scene_element2.element_id);
    scene2_element_expect(scene_element, "entity_scenebuild_player_change_appearance");

    /* Store appearance in entity for IF_SETPLAYERHEAD; copy to local player for head model */
    for( int i = 0; i < 12; i++ )
        player->appearance.slots[i] = appearance->appearance[i];
    for( int i = 0; i < 5; i++ )
        player->appearance.colors[i] = appearance->color[i];
    player->appearance.gender = appearance->gender;

    player->animation.readyanim = appearance->readyanim;
    player->animation.turnanim = appearance->turnanim;
    player->animation.walkanim = appearance->walkanim;
    player->animation.walkanim_b = appearance->walkanim_b;
    player->animation.walkanim_l = appearance->walkanim_l;
    player->animation.walkanim_r = appearance->walkanim_r;
    player->animation.runanim = appearance->runanim;

    if( !scene2_element_dash_model(scene_element) )
        scene2_element_set_dash_model(game->world->scene2, scene_element, dashmodel_new());
    player_appearance_model(
        game,
        appearance,
        &player->animation.primary_anim,
        scene2_element_dash_model(scene_element));
}

static void
npc_model(
    struct GGame* game,
    int npc_type,
    struct DashModel* dash_model)
{
    struct GameCacheModel* models[20];
    struct GameCacheModel* merged = NULL;

    struct GameCacheModel* model = gamecache_get_npc_model(game->gamecache, npc_type);
    if( model )
    {
        dashmodel_move_from_gamecache_model(dash_model, gamecache_model_new_copy(model));
        _light_model_default(dash_model, 0, 0);
        return;
    }
    struct GameCacheNpc* npc = gamecache_get_npc(game->gamecache, npc_type);

    int model_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct GameCacheModel* got_model = gamecache_get_model(game->gamecache, npc->models[i]);
        assert(got_model && "Model must be found");

        models[model_count] = got_model;
        model_count++;
    }

    merged = gamecache_model_new_merge(models, model_count);

    assert(merged->vertices_x && "Merged model must have vertices");
    assert(merged->vertices_y && "Merged model must have vertices");
    assert(merged->vertices_z && "Merged model must have vertices");

    gamecache_add_npc_model(game->gamecache, npc_type, merged);

    for( int i = 0; i < npc->recol_count; i++ )
    {
        gamecache_model_transform_recolor(merged, npc->recol_s[i], npc->recol_d[i]);
    }

    dashmodel_move_from_gamecache_model(dash_model, gamecache_model_new_copy(merged));
    _light_model_default(dash_model, 0, 0);
}

/* Head model helpers for chat/interface MODEL components.
 * Client.ts: IdkType.getHeadModel(), ObjType.getHeadModel(), NpcType.getHeadModel(),
 * ClientPlayer.getHeadModel() */

static struct GameCacheModel*
idk_head_model(
    struct GGame* game,
    int idk_id)
{
    struct GameCacheIdk* idk = gamecache_get_idk(game->gamecache, idk_id);
    if( !idk )
    {
        printf("idk_head_model: idk_id=%d not found in buildcachedat\n", idk_id);
        return NULL;
    }

    struct GameCacheModel* models[10] = { 0 };
    int model_count = 0;
    int head_id_count = 0;
    for( int i = 0; i < 10; i++ )
    {
        if( idk->heads[i] != -1 && idk->heads[i] != 0 )
        {
            head_id_count++;
            struct GameCacheModel* m = gamecache_get_model(game->gamecache, idk->heads[i]);
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
        /* Normal for body/legs idks with no head opcodes in idk.dat (ClientPlayer.getHeadModel). */
        if( head_id_count > 0 )
            printf("idk_head_model: idk_id=%d no head models could be loaded\n", idk_id);
        return NULL;
    }

    struct GameCacheModel* merged = gamecache_model_new_merge(models, model_count);
    for( int i = 0; i < 10 && idk->recol_s[i] != 0; i++ )
        gamecache_model_transform_recolor(merged, idk->recol_s[i], idk->recol_d[i]);
    return merged;
}

static struct GameCacheModel*
obj_head_model(
    struct GGame* game,
    int obj_id,
    int gender)
{
    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id);
    if( !obj )
    {
        printf("obj_head_model: obj_id=%d not found in buildcachedat\n", obj_id);
        return NULL;
    }

    int head1 = gender == 1 ? obj->womanhead : obj->manhead;
    int head2 = gender == 1 ? obj->womanhead2 : obj->manhead2;

    if( head1 == -1 )
    {
        /* ObjType.getHeadModelNoCheck: no head mesh for this gender — not an error. */
        return NULL;
    }

    struct GameCacheModel* m1 = gamecache_get_model(game->gamecache, head1);
    if( !m1 )
    {
        printf("obj_head_model: obj_id=%d head model %d not loaded\n", obj_id, head1);
        return NULL;
    }

    if( head2 != -1 )
    {
        struct GameCacheModel* m2 = gamecache_get_model(game->gamecache, head2);
        if( m2 )
        {
            struct GameCacheModel* models[2] = { m1, m2 };
            m1 = gamecache_model_new_merge(models, 2);
        }
    }

    for( int i = 0; i < obj->recol_count; i++ )
        gamecache_model_transform_recolor(m1, obj->recol_s[i], obj->recol_d[i]);

    return m1;
}

/* Build player head model from appearance (ClientPlayer.getHeadModel).
 * Uses slots as uint16_t appearance values. */
static struct GameCacheModel*
player_head_model(
    struct GGame* game,
    int* slots,
    int* colors)
{
    struct PlayerEntity* local = world_player(game->world, ACTIVE_PLAYER_SLOT);
    int gender = (local && (local->appearance.gender & 1) == 1) ? 1 : 0;

    struct AppearanceOp op;
    struct GameCacheModel* models[12] = { 0 };
    int model_count = 0;
    for( int i = 0; i < 12; i++ )
    {
        uint16_t appearance_storage[12];
        for( int j = 0; j < 12; j++ )
            appearance_storage[j] = (uint16_t)slots[j];
        appearances_decode(&op, appearance_storage, i);
        struct GameCacheModel* model = NULL;
        switch( op.kind )
        {
        case APPEARANCE_KIND_IDK:
            model = idk_head_model(game, op.id);
            break;
        case APPEARANCE_KIND_OBJ:
            model = obj_head_model(game, op.id, gender);
            break;
        default:
            break;
        }
        if( model )
            models[model_count++] = model;
    }
    if( model_count == 0 )
        return NULL;

    struct GameCacheModel* merged = gamecache_model_new_merge(models, model_count);
    /* TODO: apply colors (recol_s/recol_d from design) for hair, torso, etc. */
    (void)colors;
    return merged;
}

/* Build NPC head model from npc type (NpcType.getHeadModel). */
static struct GameCacheModel*
npc_head_model(
    struct GGame* game,
    int npc_type)
{
    struct GameCacheNpc* npc = gamecache_get_npc(game->gamecache, npc_type);
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

    struct GameCacheModel* models[16] = { 0 };
    int model_count = 0;
    for( int i = 0; i < npc->heads_count; i++ )
    {
        if( npc->heads[i] != -1 )
        {
            struct GameCacheModel* m = gamecache_get_model(game->gamecache, npc->heads[i]);
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

    struct GameCacheModel* merged = gamecache_model_new_merge(models, model_count);
    for( int i = 0; i < npc->recol_count; i++ )
        gamecache_model_transform_recolor(merged, npc->recol_s[i], npc->recol_d[i]);

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
    struct GameCacheModel* cache = NULL;
    if( model_type == 2 )
        cache = npc_head_model(game, model_id);
    else if( model_type == 3 && slots )
        cache = player_head_model(game, slots, colors);

    if( !cache )
        return NULL;

    struct DashModel* dash = dashmodel_new_from_gamecache_model(cache);
    if( dash )
        _light_model_default(dash, 0, 0);
    return dash;
}

/* ---- Dynamic loc state (doors, LOC_ADD_CHANGE / LOC_DEL) ---- */

/* Map shape → layer: 0=WALL, 1=WALL_DECOR, 2=GROUND, 3=GROUND_DECOR.
 * Mirrors Client.ts LOC_SHAPE_TO_LAYER from LocShape.ts. */
static int
loc_shape_layer(int shape)
{
    if( shape <= 3 )
        return 0;
    if( shape <= 8 )
        return 1;
    if( shape <= 21 )
        return 2;
    return 3;
}

/* Find the MapBuildLocEntity for a dynamic zone update at (sx, sz).
 * Prefer the entity whose shape-layer matches the packet (door slot), but if the server
 * reports a shape that maps to a different layer than the static map entity (uncommon),
 * fall back to the only loc at that tile so we still apply the update. */
static int
loc_entity_find_for_zone_packet(
    struct World* w,
    int level,
    int sx,
    int sz,
    int packet_shape)
{
    int want_layer = loc_shape_layer(packet_shape);
    int fallback = -1;
    for( int i = 0; i < w->active_loc_entity_count; i++ )
    {
        int eid = w->active_loc_entities[i];
        if( eid < 0 )
            continue;
        struct MapBuildLocEntity* e = world_loc_entity(w, eid);
        if( (int)e->scene_coord.sx != sx || (int)e->scene_coord.sz != sz ||
            (int)e->scene_coord.slevel != level )
            continue;

        if( loc_shape_layer(e->shape_select) == want_layer )
            return eid;
        if( fallback < 0 )
            fallback = eid;
    }
    return fallback;
}

/* Update collision flags for one loc entity: remove if remove=true, add if remove=false. */
static void
loc_entity_collision_update(
    struct World* w,
    int sx,
    int sz,
    int shape,
    int angle,
    struct GameCacheLoc* config_loc,
    bool remove)
{
    if( !w->collision_map || !config_loc )
        return;

    /* Match CollisionMap / world chunk build: orientation is only low 2 bits. */
    angle &= 0x3;

    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    int blockrange = config_loc->blocks_projectiles ? 1 : 0;

    switch( shape )
    {
    case LOC_SHAPE_FLOOR_DECORATION:
        if( config_loc->blocks_walk != 0 && config_loc->is_interactive )
        {
            if( remove )
                collision_map_remove_floor(w->collision_map, sx, sz);
            else
                collision_map_add_floor(w->collision_map, sx, sz);
        }
        break;
    case LOC_SHAPE_WALL_SINGLE_SIDE:
    case LOC_SHAPE_WALL_TRI_CORNER:
    case LOC_SHAPE_WALL_TWO_SIDES:
    case LOC_SHAPE_WALL_RECT_CORNER:
        if( config_loc->blocks_walk )
        {
            if( remove )
                collision_map_remove_wall(w->collision_map, sx, sz, shape, angle, blockrange);
            else
                collision_map_add_wall(w->collision_map, sx, sz, shape, angle, blockrange);
        }
        break;
    case LOC_SHAPE_WALL_DIAGONAL:
    case LOC_SHAPE_SCENERY:
    case LOC_SHAPE_SCENERY_DIAGIONAL:
    case LOC_SHAPE_ROOF_SLOPED:
    case LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
    case LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
    case LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
    case LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
    case LOC_SHAPE_ROOF_FLAT:
    case LOC_SHAPE_ROOF_SLOPED_OVERHANG:
    case LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
    case LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
    case LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
        if( config_loc->blocks_walk )
        {
            if( remove )
                collision_map_remove_loc(
                    w->collision_map, sx, sz, size_x, size_z, angle, blockrange);
            else
                collision_map_add_loc(w->collision_map, sx, sz, size_x, size_z, angle, blockrange);
        }
        break;
    default:
        break;
    }
}

void
entity_scenebuild_loc_apply_change(
    struct GGame* game,
    int sx,
    int sz,
    int shape,
    int angle,
    int loc_id)
{
    if( !game || !game->world )
        return;

    struct World* w = game->world;
    int plane = (int)game->local_player_plane;
    if( plane < 0 || plane >= MAP_TERRAIN_LEVELS )
        plane = 0;

    int eid = loc_entity_find_for_zone_packet(w, plane, sx, sz, shape);
    int spawned = 0;

    if( eid < 0 && loc_id >= 0 )
    {
        /* No static loc at this tile — allocate a row so LOC_ADD_CHANGE can attach Scene2. */
        struct GameCacheLoc* add_config = gamecache_get_config_loc(w->gamecache, loc_id);
        if( !add_config )
        {
            printf(
                "[zone_loc] loc_apply_change skip no GameCacheLoc for loc_id=%d tile=(%d,%d)\n",
                loc_id,
                sx,
                sz);
            return;
        }
        eid = world_map_build_loc_entity_allocate_at_scene(w, sx, sz, plane);
        if( eid < 0 )
        {
            printf(
                "[zone_loc] loc_apply_change skip MapBuildLocEntity pool full tile=(%d,%d)\n",
                sx,
                sz);
            return;
        }
        spawned = 1;
        printf(
            "[zone_loc] loc_apply_change spawned eid=%d tile=(%d,%d) loc_id=%d\n",
            eid,
            sx,
            sz,
            loc_id);
    }

    if( eid < 0 )
    {
        printf(
            "[zone_loc] loc_apply_change skip no entity tile=(%d,%d) shape=%d loc_id=%d\n",
            sx,
            sz,
            shape,
            loc_id);
        return;
    }

    struct MapBuildLocEntity* entity = world_loc_entity(w, eid);

    /* Remove old collision. */
    struct GameCacheLoc* old_config = gamecache_get_config_loc(w->gamecache, entity->loc_type_id);
    if( old_config )
        loc_entity_collision_update(
            w, sx, sz, entity->shape_select, entity->orientation, old_config, true);

    if( loc_id < 0 )
    {
        /* LOC_DEL: hide the entity by releasing its Scene2 elements. */
        scene2_element_release(w->scene2, entity->scene_element.element_id);
        scene2_element_release(w->scene2, entity->scene_element_two.element_id);
        entity->scene_element.element_id = -1;
        entity->scene_element_two.element_id = -1;
        entity->loc_type_id = -1;
        printf("[zone_loc] loc_apply_change DEL done eid=%d tile=(%d,%d)\n", eid, sx, sz);
        return;
    }

    /* LOC_ADD_CHANGE: update entity fields and reload model. */
    entity->loc_type_id = loc_id;
    entity->shape_select = (uint8_t)shape;
    entity->orientation = (uint8_t)angle;

    world_loc_entity_reload_model(w, eid, loc_id, shape, angle);

    struct GameCacheLoc* new_config = gamecache_get_config_loc(w->gamecache, loc_id);
    if( new_config )
        loc_entity_collision_update(w, sx, sz, shape, angle, new_config, false);

    printf(
        "[zone_loc] loc_apply_change ADD done eid=%d tile=(%d,%d) loc_id=%d %s\n",
        eid,
        sx,
        sz,
        loc_id,
        spawned ? "spawned" : "existing");
}

void
entity_scenebuild_loc_apply_del(
    struct GGame* game,
    int sx,
    int sz,
    int shape,
    int angle)
{
    entity_scenebuild_loc_apply_change(game, sx, sz, shape, angle, -1);
}

void
entity_scenebuild_loc_apply_anim(
    struct GGame* game,
    int sx,
    int sz,
    int shape,
    int seq_id)
{
    if( !game || !game->world )
        return;

    struct World* w = game->world;
    int plane = (int)game->local_player_plane;
    if( plane < 0 || plane >= MAP_TERRAIN_LEVELS )
        plane = 0;

    int eid = loc_entity_find_for_zone_packet(w, plane, sx, sz, shape);
    if( eid < 0 )
        return;

    world_map_build_loc_entity_set_animation(w, eid, seq_id);
}