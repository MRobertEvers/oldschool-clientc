#include "lua_gamecache.h"

#include "lua_gametypes.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache_from_buildcachedat.h"

#include <stddef.h>

static int
arg_int(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetInt(elem);
}

static struct LuaGameType*
void_result(void)
{
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaGameCache_convert_all_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_all_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_reftables_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_reftables_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_floortypes_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_floortypes_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_sequences_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_sequences_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_animbaseframes_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_animbaseframes_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_npcs_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_npcs_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_objs_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_objs_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_idks_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_idks_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_spotanims_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_spotanims_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_components_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_components_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_map_terrain_chunk_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    int mapx = arg_int(args, 0);
    int mapz = arg_int(args, 1);
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_map_terrain_chunk_from_buildcachedat(
        game->gamecache, game->buildcachedat, mapx, mapz);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_scenery_chunk_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    int mapx = arg_int(args, 0);
    int mapz = arg_int(args, 1);
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_scenery_chunk_from_buildcachedat(
        game->gamecache, game->buildcachedat, mapx, mapz);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_loc_configs_chunk_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    int mapx = arg_int(args, 0);
    int mapz = arg_int(args, 1);
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_loc_configs_chunk_from_buildcachedat(
        game->gamecache, game->buildcachedat, mapx, mapz);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_models_chunk_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    int mapx = arg_int(args, 0);
    int mapz = arg_int(args, 1);
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_models_chunk_from_buildcachedat(
        game->gamecache, game->buildcachedat, mapx, mapz);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_npc_models_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_npc_models_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_obj_models_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_obj_models_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}

struct LuaGameType*
LuaGameCache_convert_idk_models_from_buildcachedat(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->gamecache || !game->buildcachedat )
        return void_result();
    gamecache_convert_idk_models_from_buildcachedat(game->gamecache, game->buildcachedat);
    return void_result();
}
