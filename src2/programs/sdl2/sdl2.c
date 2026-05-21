#include "../../commands/libtorirs_command_queue.h"
#include "../../platforms/platform_sdl2.h"
#include "../../platforms/platform_x_cache.h"
#include "../../platforms/platform_x_lua.h"
#include "../../scripting/libtorirs_scripting.h"

#include <stdio.h>

#define CACHE_DAT_PATH "/Users/matthewevers/Documents/git_repos/3draster/cache254"

int
main(
    int argc,
    char* argv[])
{
    (void)argc;
    (void)argv;

    printf("Hello from main!\n");

    struct LibToriRS_Instance* instance = LibToriRS_InstanceNew();
    if( !instance )
    {
        printf("Failed to create instance\n");
        goto error_exit;
    }

    struct LibToriPlatformX_Lua* lua = LibToriPlatformX_LuaNew(instance);
    if( !lua )
    {
        printf("Failed to create Lua\n");
        goto error_exit;
    }

    if( LibToriPlatformX_LuaCacheIOInit(lua, CACHE_MODE_DAT1, CACHE_DAT_PATH) !=
        LIBTORI_PLATFORM_X_LUA_OK )
    {
        printf("Failed to init cache\n");
        goto error_exit;
    }

    struct LibToriRS_Script* script =
        LibToriRS_ScriptQueueEmplace(LibToriRS_GetScriptQueue(instance), "init.lua");
    script->is_inline = false;

    struct LibToriPlatformSDL2* platform = LibToriPlatformSDL2_New();
    if( !platform )
    {
        printf("Failed to create SDL2 platform\n");
        goto error_exit;
    }

    const int screen_w = 800;
    const int screen_h = 600;
    if( !LibToriPlatformSDL2_InitForSoft3D(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 window\n");
        goto error_exit;
    }

    struct LibToriRS_CommandQueue* command_queue = LibToriRS_CommandQueue_New();
    if( !command_queue )
    {
        printf("Failed to create command queue\n");
        goto error_exit;
    }

    uint64_t time = SDL_GetTicks64();
    LibToriRS_InitTime(instance, time);

    while( LibToriRS_IsRunning(instance) )
    {
        time = SDL_GetTicks64();

        LibToriPlatformSDL2_PollEvents(platform, command_queue);
        LibToriRS_TickInput(instance, command_queue, time);

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
                goto error_exit;
            }
        }

        LibToriRS_GameStep(instance);

        LibToriPlatformSDL2_Render(platform, instance, 0);

        SDL_Delay(1);
    }

exit:
    if( lua )
        LibToriPlatformX_LuaFree(lua);
    if( instance )
        LibToriRS_InstanceFree(instance);
    if( command_queue )
        LibToriRS_CommandQueue_Free(command_queue);
    if( platform )
        LibToriPlatformSDL2_Free(platform);
    return 0;

error_exit:
    printf("ERROR EXIT");
    goto exit;
}
