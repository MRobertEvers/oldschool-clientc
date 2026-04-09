#include "lua_dash.h"

#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/scene2.h"

#include <assert.h>
#include <string.h>

/* Helper: get int at args[i]. args must be VarTypeArray. */
static int
arg_int(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetInt(elem);
}

/* Helper: get userdata at args[i]. */
static void*
arg_userdata(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetUserData(elem);
}

struct LuaGameType*
LuaDash_load_textures(
    struct DashGraphics* dash,
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)buildcachedat;
    (void)args;
    if( !game || !game->scene2 )
        return LuaGameType_NewVoid();

    struct Scene2* s2 = game->scene2;
    for( int i = 0; i < s2->textures_count; i++ )
    {
        int tid = s2->textures[i].id;
        struct DashTexture* texture = s2->textures[i].texture;
        if( texture )
            dash3d_add_texture(dash, tid, texture);
    }

    return LuaGameType_NewVoid();
}

static char const g_prefix[] = "dash_";

bool
LuaDash_CommandHasPrefix(const char* command)
{
    return strncmp(command, g_prefix, sizeof(g_prefix) - 1) == 0;
}

struct LuaGameType*
LuaDash_DispatchCommand(
    struct DashGraphics* dash,
    struct BuildCacheDat* buildcachedat,
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

    if( strcmp(command, "load_textures") == 0 )
        return LuaDash_load_textures(dash, buildcachedat, game, args);
    return NULL;
}