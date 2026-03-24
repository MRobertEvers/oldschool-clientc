#include "lua_game.h"

#include "osrs/buildcachedat_loader.h"
#include "osrs/game.h"
#include "osrs/gameproto_exec.h"

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
    char command[strlen(full_command) - sizeof(g_prefix) + 1];
    strncpy(
        command, full_command + sizeof(g_prefix) - 1, strlen(full_command) - sizeof(g_prefix) + 1);
    command[strlen(full_command) - sizeof(g_prefix) + 1] = '\0';

    if( strcmp(command, "build_scene") == 0 )
        return LuaGame_build_scene(game, args);
    else if( strcmp(command, "build_scene_centerzone") == 0 )
        return LuaGame_build_scene_centerzone(game, args);
    else if( strcmp(command, "exec_pkt_player_info") == 0 )
        return LuaGame_exec_pkt_player_info(game, args);
    else if( strcmp(command, "exec_pkt_npc_info") == 0 )
        return LuaGame_exec_pkt_npc_info(game, args);
    else
    {
        printf("Unknown command: %s\n", command);
        assert(false);
        return NULL;
    }
}