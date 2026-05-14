#include "../../platforms/platform_x_lua.h"
#include "../../scripting/libtorirs_scripting.h"

#include <stdio.h>

int
main(
    int argc,
    char* argv[])
{
    (void)argc;
    (void)argv;

    printf("Hello from sdl2!\n");

    struct LibToriPlatformX_Lua* lua = LibToriPlatformX_LuaNew();
    if( !lua )
    {
        printf("Failed to create Lua\n");
        return 1;
    }

    struct LibToriRS_Instance* instance = LibToriRS_InstanceNew();
    if( !instance )
    {
        printf("Failed to create instance\n");
        LibToriPlatformX_LuaFree(lua);
        return 1;
    }

    struct LibToriRS_Script* script =
        LibToriRS_ScriptQueueEmplace(LibToriRS_GetScriptQueue(instance), "init.lua");

    int rc = LibToriPlatformX_LuaRun(lua, instance);
    while( rc == LIBTORI_PLATFORM_X_LUA_YIELDED )
    {
        rc = LibToriPlatformX_LuaContinue(lua, instance);
    }
    if( rc != LIBTORI_PLATFORM_X_LUA_OK )
    {
        printf("Failed to run script\n");
        return 1;
    }

    LibToriRS_InstanceFree(instance);
    LibToriPlatformX_LuaFree(lua);
    return 0;
}
