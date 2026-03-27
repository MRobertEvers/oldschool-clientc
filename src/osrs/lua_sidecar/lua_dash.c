#include "lua_dash.h"

#include "osrs/buildcachedat.h"

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
    struct LuaGameType* args)
{
    (void)args;
    struct DashMapIter* iter = buildcachedat_iter_new_textures(buildcachedat);

    int texture_id = 0;
    while( buildcachedat_iter_next_texture_id(iter, &texture_id) )
    {
        struct DashTexture* texture = buildcachedat_get_texture(buildcachedat, texture_id);
        if( texture )
            dash3d_add_texture(dash, texture_id, texture);
    }
    dashmap_iter_free(iter);

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
        return LuaDash_load_textures(dash, buildcachedat, args);
    return NULL;
}