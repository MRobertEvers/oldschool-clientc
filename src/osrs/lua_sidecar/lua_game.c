#include "lua_game.h"

#include "graphics/dashmap.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/gameproto_exec.h"
#include "osrs/packets/revpacket_lc245_2.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/world.h"
#include "platforms/common/platform_memory.h"

#include <assert.h>

/* Forward declaration: defined in tori_rs_minimap.u.c / tori_rs.h. */
void LibToriRS_WorldMinimapStaticRebuild(struct GGame* game);

/* Helper: get int at args[i]. args must be VarTypeArray. */
static int
arg_int(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetInt(elem);
}

static void*
arg_userdata(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetUserData(elem);
}

struct LuaGameType*
LuaGame_build_scene(
    struct GGame* game,
    struct LuaGameType* args)
{
    int wx_sw = arg_int(args, 0);
    int wz_sw = arg_int(args, 1);
    int wx_ne = arg_int(args, 2);
    int wz_ne = arg_int(args, 3);
    buildcachedat_loader_finalize_scene(game->buildcachedat, game, wx_sw, wz_sw, wx_ne, wz_ne);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_build_scene_centerzone(
    struct GGame* game,
    struct LuaGameType* args)
{
    int zonex = arg_int(args, 0);
    int zonez = arg_int(args, 1);
    int size = arg_int(args, 2);

    buildcachedat_loader_finalize_scene_centerzone(game->buildcachedat, game, zonex, zonez, size);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_get_heap_usage_mb(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)game;
    (void)args;
    struct PlatformMemoryInfo mem = { 0 };
    if( !platform_get_memory_info(&mem) )
        return LuaGameType_NewFloat(0.0f);
    float mb = (float)((double)mem.heap_used / (1024.0 * 1024.0));
    return LuaGameType_NewFloat(mb);
}

struct LuaGameType*
LuaGame_exec_pkt_player_info(
    struct GGame* game,
    struct LuaGameType* args)
{
    void* data = arg_userdata(args, 0);
    int length = arg_int(args, 1);
    gameproto_exec_player_info_raw(game, data, length);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_exec_pkt_npc_info(
    struct GGame* game,
    struct LuaGameType* args)
{
    void* data = arg_userdata(args, 0);
    int length = arg_int(args, 1);
    gameproto_exec_npc_info_raw(game, data, length);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_exec_pkt_if_settab(
    struct GGame* game,
    struct LuaGameType* args)
{
    struct RevPacket_LC245_2_Item* item =
        (struct RevPacket_LC245_2_Item*)arg_userdata(args, 0);
    if( item )
        gameproto_exec_if_settab(game, &item->packet);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_exec_pkt_update_inv_full(
    struct GGame* game,
    struct LuaGameType* args)
{
    struct RevPacket_LC245_2_Item* item =
        (struct RevPacket_LC245_2_Item*)arg_userdata(args, 0);
    if( item )
        gameproto_exec_update_inv_full(game, &item->packet);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_get_inv_obj_ids(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)game;
    struct RevPacket_LC245_2_Item* item =
        (struct RevPacket_LC245_2_Item*)arg_userdata(args, 0);
    if( !item )
        return LuaGameType_NewIntArray(0);

    int size = item->packet._update_inv_full.size;
    int count = 0;
    for( int i = 0; i < size; i++ )
    {
        if( item->packet._update_inv_full.obj_ids[i] > 0 )
            count++;
    }

    struct LuaGameType* arr = LuaGameType_NewIntArray(count);
    for( int i = 0; i < size; i++ )
    {
        int obj_id = item->packet._update_inv_full.obj_ids[i];
        if( obj_id > 0 )
            LuaGameType_IntArrayPush(arr, obj_id);
    }
    return arr;
}

struct LuaGameType*
LuaGame_load_interfaces(
    struct GGame* game,
    struct LuaGameType* args)
{
    struct CacheDatArchive* archive = (struct CacheDatArchive*)arg_userdata(args, 0);
    if( archive )
    {
        buildcachedat_loader_load_interfaces(game->buildcachedat, archive->data, archive->data_size);
        cache_dat_archive_free(archive);
    }
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_load_component_sprites(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    buildcachedat_loader_load_component_sprites_from_media(game->buildcachedat, game);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_get_interface_model_ids(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->buildcachedat || !game->buildcachedat->component_hmap )
        return LuaGameType_NewIntArray(0);

    enum
    {
        kMaxIds = 16384
    };
    int tmp[kMaxIds];
    int n = 0;

    struct DashMapIter* it = buildcachedat_component_iter_new(game->buildcachedat);
    if( !it )
        return LuaGameType_NewIntArray(0);

    int cid = 0;
    struct CacheDatConfigComponent* c = NULL;
    while( (c = buildcachedat_component_iter_next(it, &cid)) != NULL )
    {
        if( c->type != COMPONENT_TYPE_MODEL || c->modelType != 1 )
            continue;
        int mid = c->model;
        if( mid < 0 )
            continue;
        int dup = 0;
        for( int i = 0; i < n; i++ )
        {
            if( tmp[i] == mid )
            {
                dup = 1;
                break;
            }
        }
        if( dup )
            continue;
        if( n >= kMaxIds )
            break;
        tmp[n++] = mid;
    }
    dashmap_iter_free(it);

    struct LuaGameType* arr = LuaGameType_NewIntArray(n);
    for( int i = 0; i < n; i++ )
        LuaGameType_IntArrayPush(arr, tmp[i]);
    return arr;
}

struct LuaGameType*
LuaGame_rebuild_centerzone_begin(
    struct GGame* game,
    struct LuaGameType* args)
{
    int zonex = arg_int(args, 0);
    int zonez = arg_int(args, 1);
    int size = arg_int(args, 2);

    buildcachedat_loader_prepare_scene_centerzone(game->buildcachedat, game);
    world_rebuild_centerzone_begin(game->world, zonex, zonez, size);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_rebuild_centerzone_chunk(
    struct GGame* game,
    struct LuaGameType* args)
{
    int mapx = arg_int(args, 0);
    int mapz = arg_int(args, 1);

    assert(game->world && "world_rebuild_centerzone_begin must be called first");
    world_rebuild_centerzone_chunk(game->world, mapx, mapz);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_rebuild_centerzone_end(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;

    assert(game->world && "world_rebuild_centerzone_begin must be called first");
    world_rebuild_centerzone_end(game->world);

    LibToriRS_WorldMinimapStaticRebuild(game);
    buildcachedat_clear(game->buildcachedat);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_rebuild_centerzone_slow(
    struct GGame* game,
    struct LuaGameType* args)
{
    int zonex = arg_int(args, 0);
    int zonez = arg_int(args, 1);
    int size = arg_int(args, 2);

    buildcachedat_loader_prepare_scene_centerzone(game->buildcachedat, game);
    world_rebuild_centerzone_begin(game->world, zonex, zonez, size);

    int chunk_sw_x = game->world->_chunk_sw_x;
    int chunk_sw_z = game->world->_chunk_sw_z;
    int chunk_ne_x = game->world->_chunk_ne_x;
    int chunk_ne_z = game->world->_chunk_ne_z;

    for( int mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( int mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            world_rebuild_centerzone_chunk_terrain(game->world, mapx, mapz);
        }
    }

    for( int mapx = chunk_sw_x; mapx <= chunk_ne_x; mapx++ )
    {
        for( int mapz = chunk_sw_z; mapz <= chunk_ne_z; mapz++ )
        {
            world_rebuild_centerzone_chunk_scenery(game->world, mapx, mapz);
        }
    }

    world_rebuild_centerzone_end(game->world);

    LibToriRS_WorldMinimapStaticRebuild(game);
    buildcachedat_clear(game->buildcachedat);

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGame_spawn_element(
    struct GGame* game,
    struct LuaGameType* args)
{
    int world_x = arg_int(args, 0);
    int world_z = arg_int(args, 1);
    int level = arg_int(args, 2);
    int model_id = arg_int(args, 3);
    int seq_id = arg_int(args, 4);

    if( !game || !game->scene2 || !game->buildcachedat )
        return LuaGameType_NewVoid();

    struct CacheModel* cache_model = buildcachedat_get_model(game->buildcachedat, model_id);
    if( !cache_model )
        return LuaGameType_NewVoid();

    struct CacheModel* model_copy = model_new_copy(cache_model);
    struct DashModel* dash_model = dashmodel_new_from_cache_model(model_copy);
    model_free(model_copy);
    if( !dash_model )
        return LuaGameType_NewVoid();

    _light_model_default(dash_model, 0, 0);

    int element_id = scene2_element_acquire_full(game->scene2, 0, SCENE2_ELEMENT_PROJECTILE);
    struct Scene2Element* element = scene2_element_at(game->scene2, element_id);
    scene2_element_expect(element, "LuaGame_spawn_element");
    scene2_element_set_dash_position_ptr(element, dashposition_new());

    struct DashPosition* position = scene2_element_dash_position(element);
    position->x = world_x;
    position->z = world_z;
    position->y = game->world && game->world->heightmap
                      ? heightmap_get_interpolated(game->world->heightmap, world_x, world_z, level)
                      : 0;

    scene2_element_set_dash_model(game->scene2, element, dash_model);

    if( game->world )
    {
        if( game->world->spawned_element_count < MAX_SPAWNED_ELEMENTS )
        {
            int idx = game->world->spawned_element_count++;
            game->world->spawned_element_ids[idx] = element_id;
            game->world->spawned_element_levels[idx] = level;
        }
        if( game->world->painter )
        {
            painter_add_normal_scenery(
                game->world->painter, world_x / 128, world_z / 128, level, element_id, 1, 1);
        }
    }

    struct CacheDatSequence* sequence = buildcachedat_get_sequence(game->buildcachedat, seq_id);
    if( sequence )
    {
        struct DashFramemap* framemap = NULL;
        for( int i = 0; i < sequence->frame_count; i++ )
        {
            struct CacheAnimframe* animframe =
                buildcachedat_get_animframe(game->buildcachedat, sequence->frames[i]);
            if( !animframe )
                continue;

            if( !framemap )
            {
                framemap = dashframemap_new_from_animframe(animframe);
                scene2_element_set_framemap(element, framemap);
            }

            int length = sequence->delay[i];
            if( length == 0 )
                length = animframe->delay;

            scene2_element_push_animation_frame(
                game->scene2,
                element,
                seq_id,
                i,
                dashframe_new_from_animframe(animframe),
                length);
        }

        struct Scene2Frames* frames = scene2_element_primary_frames(element);
        if( frames && frames->count > 0 && framemap )
        {
            dashmodel_animate(dash_model, frames->frames[0], framemap);
            scene2_element_set_active_anim_id(element, (uint16_t)seq_id);
            scene2_element_set_active_animation_index(element, 0);
            scene2_element_set_active_frame(element, 0);
        }
    }

    return LuaGameType_NewVoid();
}
