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
