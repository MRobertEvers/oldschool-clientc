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

static char const g_prefix[] = "misc_";

bool
LuaSidecarMisc_CommandHasPrefix(const char* command)
{
    return strncmp(command, g_prefix, sizeof(g_prefix) - 1) == 0;
}

struct LuaGameType*
LuaSidecarMisc_DispatchCommand(
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

    if( strcmp(command, "read_cullmap_from_blob") == 0 )
        return LuaSidecarMisc_read_cullmap_from_blob(game, args);
    if( strcmp(command, "save_camera") == 0 )
        return LuaSidecarMisc_save_camera(game, args);
    if( strcmp(command, "load_camera") == 0 )
        return LuaSidecarMisc_load_camera(game, args);

    printf("Unknown misc_ command: %s\n", command);
    assert(false);
    return LuaGameType_NewVoid();
}
