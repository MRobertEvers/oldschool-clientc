#include "../../commands/libtorirs_command_queue.h"
#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../platforms/platform_sdl2/platform_sdl2.h"
#include "../../platforms/platform_sdl2/platform_sdl2_renderer_opengl3.h"
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

    struct LibToriPlatformX_Lua* lua = NULL;
    struct LibToriPlatformSDL2* platform = NULL;
    struct LibToriPlatformSDL2_RendererGL3* renderer_gl3 = NULL;
    struct LibToriRS_CommandQueue* command_queue = NULL;

    struct LibToriRS_Instance* instance = LibToriRS_InstanceNew();
    if( !instance )
    {
        printf("Failed to create instance\n");
        goto error_exit;
    }

    lua = LibToriPlatformX_LuaNew(instance);
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

    platform = LibToriPlatformSDL2_New();
    if( !platform )
    {
        printf("Failed to create SDL2 platform\n");
        goto error_exit;
    }

    const int screen_w = 800;
    const int screen_h = 600;
    if( !LibToriPlatformSDL2_InitForOpenGL3(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 OpenGL window\n");
        goto error_exit;
    }

    renderer_gl3 = LibToriPlatformSDL2_RendererGL3_New(screen_w, screen_h);
    if( !renderer_gl3 )
    {
        printf("Failed to create OpenGL3 renderer\n");
        goto error_exit;
    }
    if( !LibToriPlatformSDL2_RendererGL3_Init(
            renderer_gl3, LibToriPlatformSDL2_GetWindow(platform)) )
    {
        printf("Failed to init OpenGL3 renderer\n");
        goto error_exit;
    }

    command_queue = LibToriRS_CommandQueue_New();
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
        LibToriRS_IOQueueClear(LibToriRS_GetIOQueue(instance));
        LibToriRS_ScriptQueueClear(LibToriRS_GetScriptQueue(instance));

        LibToriPlatformSDL2_RendererGL3_Render(renderer_gl3, instance);

        SDL_Delay(1);
    }

exit:
    if( renderer_gl3 )
        LibToriPlatformSDL2_RendererGL3_Free(renderer_gl3);
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
