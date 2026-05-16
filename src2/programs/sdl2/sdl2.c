#include "../../commands/libtorirs_command_queue.h"
#include "../../platforms/platform_sdl2.h"
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
    script->is_inline = false;

    struct LibToriPlatformSDL2* platform = LibToriPlatformSDL2_New();
    if( !platform )
    {
        printf("Failed to create SDL2 platform\n");
        LibToriRS_InstanceFree(instance);
        LibToriPlatformX_LuaFree(lua);
        return 1;
    }

    const int screen_w = 800;
    const int screen_h = 600;
    if( !LibToriPlatformSDL2_InitForSoft3D(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 window\n");
        LibToriPlatformSDL2_Free(platform);
        LibToriRS_InstanceFree(instance);
        LibToriPlatformX_LuaFree(lua);
        return 1;
    }

    struct LibToriRS_CommandQueue* command_queue = LibToriRS_CommandQueue_New();
    if( !command_queue )
    {
        printf("Failed to create command queue\n");
        LibToriPlatformSDL2_Free(platform);
        LibToriRS_InstanceFree(instance);
        LibToriPlatformX_LuaFree(lua);
        return 1;
    }

    uint64_t time = SDL_GetTicks64();
    LibToriRS_InitTime(instance, time);

    while( LibToriRS_IsRunning(instance) )
    {
        time = SDL_GetTicks64();

        LibToriRS_CommandQueue_Clear(command_queue);

        LibToriPlatformSDL2_PollEvents(platform, command_queue);

        LibToriRS_ProcessCommandQueue(instance, command_queue, time);

        LibToriRS_ProcessInput(instance);

        if( !LibToriRS_IsRunning(instance) )
            break;

        while( !LibToriRS_ScriptQueueIsEmpty(LibToriRS_GetScriptQueue(instance)) )
        {
            int rc = LibToriPlatformX_LuaRun(lua, instance);
            while( rc == LIBTORI_PLATFORM_X_LUA_YIELDED )
            {
                rc = LibToriPlatformX_LuaContinue(lua, instance);
            }
            if( rc != LIBTORI_PLATFORM_X_LUA_OK )
            {
                printf("Failed to run script\n");
                LibToriRS_InstanceFree(instance);
                LibToriPlatformX_LuaFree(lua);
                return 1;
            }
        }

        SDL_Delay(1);
    }

    LibToriRS_CommandQueue_Free(command_queue);
    LibToriPlatformSDL2_Free(platform);
    LibToriRS_InstanceFree(instance);
    LibToriPlatformX_LuaFree(lua);
    return 0;
}
