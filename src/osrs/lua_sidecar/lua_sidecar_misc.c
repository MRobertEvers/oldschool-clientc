#include "lua_sidecar_misc.h"

#include "lua_configfile.h"
#include "osrs/painters.h"
#include "osrs/world.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct LuaGameType*
LuaSidecarMisc_read_cullmap_from_blob(
    struct GGame* game,
    struct LuaGameType* args)
{
    if( !game || !args || LuaGameType_GetVarTypeArrayCount(args) < 2 )
        return LuaGameType_NewVoid();

    struct LuaGameType* file_gt = LuaGameType_GetVarTypeArrayAt(args, 0);
    struct LuaGameType* radius_gt = LuaGameType_GetVarTypeArrayAt(args, 1);
    if( file_gt->kind != LUAGAMETYPE_USERDATA || radius_gt->kind != LUAGAMETYPE_INT )
        return LuaGameType_NewVoid();

    struct LuaConfigFile* file = (struct LuaConfigFile*)LuaGameType_GetUserData(file_gt);
    int radius = LuaGameType_GetInt(radius_gt);
    if( !file || !file->data || file->size <= 0 || radius < 1 )
        return LuaGameType_NewVoid();

    struct PaintersCullMap* cm =
        painters_cullmap_from_blob((const uint8_t*)file->data, (size_t)file->size, radius);
    if( cm && game->world )
        world_set_painters_cullmap(game->world, cm);
    else if( cm )
        painters_cullmap_free(cm);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaSidecarMisc_save_camera(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaSidecarMisc_load_camera(
    struct GGame* game,
    struct LuaGameType* args)
{
    (void)args;
    if( !game )
        return LuaGameType_NewVoid();
    struct LuaGameType* out = LuaGameType_NewVarTypeArray(5);
    LuaGameType_VarTypeArrayPush(out, LuaGameType_NewInt(game->camera_world_x));
    LuaGameType_VarTypeArrayPush(out, LuaGameType_NewInt(game->camera_world_y));
    LuaGameType_VarTypeArrayPush(out, LuaGameType_NewInt(game->camera_world_z));
    LuaGameType_VarTypeArrayPush(out, LuaGameType_NewInt(game->camera_pitch));
    LuaGameType_VarTypeArrayPush(out, LuaGameType_NewInt(game->camera_yaw));
    return out;
}

