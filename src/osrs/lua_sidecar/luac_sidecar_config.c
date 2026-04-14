#include "luac_sidecar_config.h"

#include "lua_configfile.h"
#include "lua_gametypes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_DIRECTORY
#define CONFIG_DIRECTORY "../src/osrs/revconfig/configs"
#endif

struct FileData
{
    uint8_t* data;
    int size;
};

static void
load_file_data(
    struct FileData* file_data,
    const char* name)
{
    static char path[256] = { 0 };
    snprintf(path, sizeof path, "%s/%s", CONFIG_DIRECTORY, name);
    printf("Loading file: %s\n", path);
    FILE* f = fopen(path, "rb");
    if( !f )
        return;
    fseek(f, 0, SEEK_END);
    file_data->size = ftell(f);
    fseek(f, 0, SEEK_SET);
    file_data->data = malloc(file_data->size);
    fread(file_data->data, 1, file_data->size, f);
    fclose(f);
}

struct LuaConfigFile*
load_lua_userdata_from_file(const char* name)
{
    struct FileData file_data = { 0 };
    load_file_data(&file_data, name);
    if( file_data.size <= 0 )
    {
        fprintf(stderr, "Failed to load file: %s\n", name);
        return NULL;
    }

    struct LuaConfigFile* lua_file = malloc(sizeof(struct LuaConfigFile));
    if( !lua_file )
        return NULL;
    memset(lua_file, 0, sizeof(struct LuaConfigFile));

    strcpy(lua_file->name, name);
    lua_file->data = file_data.data;
    lua_file->size = file_data.size;
    return lua_file;
}

struct LuaGameType*
LuaCSidecar_Config_LoadConfig(struct LuaGameType* args)
{
    if( !args || LuaGameType_GetVarTypeArrayCount(args) < 1 )
        return LuaGameType_NewVoid();

    struct LuaGameType* path_gt = LuaGameType_GetVarTypeArrayAt(args, 0);
    if( path_gt->kind != LUAGAMETYPE_STRING )
        return LuaGameType_NewVoid();

    const char* config_file = LuaGameType_GetString(path_gt);
    if( !config_file )
        return LuaGameType_NewVoid();

    struct LuaConfigFile* file = load_lua_userdata_from_file(config_file);
    if( !file )
        return LuaGameType_NewVoid();

    return LuaGameType_NewUserData(file);
}

struct LuaGameType*
LuaCSidecar_Config_LoadConfigs(struct LuaGameType* args)
{
    int count = LuaGameType_GetVarTypeArrayCount(args);
    if( count <= 0 )
        return LuaGameType_NewVoid();

    struct LuaGameType* files = LuaGameType_NewUserDataArraySpread(count);
    for( int i = 0; i < count; i++ )
    {
        struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
        char* name = LuaGameType_GetString(elem);
        if( !name )
            return LuaGameType_NewVoid();

        struct LuaConfigFile* file = load_lua_userdata_from_file(name);
        printf("file: %p\n", file);
        if( !file )
            return LuaGameType_NewVoid();
        LuaGameType_UserDataArrayPush(files, file);
    }

    return files;
}