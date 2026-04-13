#include "lua_game.h"

#include "graphics/dashmap.h"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
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
            world_rebuild_centerzone_chunk(game->world, mapx, mapz);
        }
    }

    world_rebuild_centerzone_end(game->world);

    LibToriRS_WorldMinimapStaticRebuild(game);
    buildcachedat_clear(game->buildcachedat);

    return LuaGameType_NewVoid();
}

static char const g_prefix[] = "game_";

bool
LuaGame_CommandHasPrefix(const char* command)
{
    return strncmp(command, g_prefix, sizeof(g_prefix) - 1) == 0;
}

struct LuaGameType*
LuaGame_DispatchCommand(
    struct GGame* game,
    char* full_command,
    struct LuaGameType* args)
{
        assert(memcmp(full_command, g_prefix, sizeof(g_prefix) - 1) == 0);
        char command[256];
        size_t suffix_len = strlen(full_command) - sizeof(g_prefix) + 1;
        assert(suffix_len < sizeof(command));
        memcpy(command, full_command + sizeof(g_prefix) - 1, suffix_len);
        command[suffix_len] = '\0';

    if( strcmp(command, "build_scene") == 0 )
        return LuaGame_build_scene(game, args);
    else if( strcmp(command, "build_scene_centerzone") == 0 )
        return LuaGame_build_scene_centerzone(game, args);
    else if( strcmp(command, "exec_pkt_player_info") == 0 )
        return LuaGame_exec_pkt_player_info(game, args);
    else if( strcmp(command, "exec_pkt_npc_info") == 0 )
        return LuaGame_exec_pkt_npc_info(game, args);
    else if( strcmp(command, "exec_pkt_if_settab") == 0 )
        return LuaGame_exec_pkt_if_settab(game, args);
    else if( strcmp(command, "exec_pkt_update_inv_full") == 0 )
        return LuaGame_exec_pkt_update_inv_full(game, args);
    else if( strcmp(command, "get_inv_obj_ids") == 0 )
        return LuaGame_get_inv_obj_ids(game, args);
    else if( strcmp(command, "load_interfaces") == 0 )
        return LuaGame_load_interfaces(game, args);
    else if( strcmp(command, "load_component_sprites") == 0 )
        return LuaGame_load_component_sprites(game, args);
    else if( strcmp(command, "get_interface_model_ids") == 0 )
        return LuaGame_get_interface_model_ids(game, args);
    else if( strcmp(command, "get_heap_usage_mb") == 0 )
        return LuaGame_get_heap_usage_mb(game, args);
    else if( strcmp(command, "rebuild_centerzone_begin") == 0 )
        return LuaGame_rebuild_centerzone_begin(game, args);
    else if( strcmp(command, "rebuild_centerzone_chunk") == 0 )
        return LuaGame_rebuild_centerzone_chunk(game, args);
    else if( strcmp(command, "rebuild_centerzone_end") == 0 )
        return LuaGame_rebuild_centerzone_end(game, args);
    else if( strcmp(command, "rebuild_centerzone_slow") == 0 )
        return LuaGame_rebuild_centerzone_slow(game, args);
    else
    {
        printf("Unknown command: %s\n", command);
        assert(false);
        return NULL;
    }
}