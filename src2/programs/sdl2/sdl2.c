#include "../../commands/libtorirs_command_queue.h"
#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../platforms/platform_sdl2/platform_sdl2.h"
#include "../../platforms/platform_sdl2/platform_sdl2_renderer_soft3d.h"
#include "../../platforms/platform_x/cachelib.h"
#include "../../platforms/platform_x/cachelib_platform.h"
#include "../../platforms/platform_x_io_reactor.h"
#include "../../platforms/platform_x_lua.h"
#include "../../scripting/libtorirs_scripting.h"

#if defined(_WIN32)
#include "../../platforms/platform_sdl2/platform_sdl2_renderer_d3d9.h"
#else
#include "../../platforms/platform_sdl2/platform_sdl2_renderer_opengl3.h"
#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool
has_flag(
    int argc,
    char* argv[],
    char const* flag)
{
    for( int i = 1; i < argc; i++ )
    {
        if( argv[i] && strcmp(argv[i], flag) == 0 )
            return true;
    }
    return false;
}

static char const*
resolve_cache_dat_path(
    int argc,
    char* argv[])
{
    for( int i = 1; i < argc; i++ )
    {
        if( !argv[i] || argv[i][0] == '\0' )
            continue;
        if( argv[i][0] == '-' )
            continue;
        return argv[i];
    }

#ifdef CACHE_DAT_PATH
    return CACHE_DAT_PATH;
#else
    static char const* const candidates[] = {
        "cache254",          "../cache254",          "../../cache254",
        "../../../cache254", "../../../../cache254", NULL,
    };

    char dat_path[512];
    for( int i = 0; candidates[i]; i++ )
    {
        snprintf(dat_path, sizeof(dat_path), "%s/main_file_cache.dat", candidates[i]);
        FILE* f = fopen(dat_path, "rb");
        if( f )
        {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
#endif
}

int
main(
    int argc,
    char* argv[])
{
    printf("Hello from main!\n");

    bool const use_soft3d = has_flag(argc, argv, "--soft3d");

    struct LibToriPlatformX_Lua* lua = NULL;
    struct LibToriPlatformX_IOReactor* io_reactor = NULL;
    struct CacheLib* cache = NULL;
    struct LibToriPlatformSDL2* platform = NULL;
    struct LibToriPlatformSDL2_RendererSoft3D* renderer_soft3d = NULL;
#if defined(_WIN32)
    struct LibToriPlatformSDL2_RendererD3D9* renderer = NULL;
#else
    struct LibToriPlatformSDL2_RendererGL3* renderer = NULL;
#endif
    struct LibToriRS_CommandQueue* command_queue = NULL;
    struct LibToriRS_Instance* instance = NULL;

    char const* cache_dat_path = resolve_cache_dat_path(argc, argv);
    if( !cache_dat_path )
    {
        printf(
            "Failed to find cache254 (expected main_file_cache.dat). "
            "Run from a directory that contains cache254/, or pass the cache path as an "
            "argument.\n");
        goto error_exit;
    }
    printf("Using cache: %s\n", cache_dat_path);
    if( use_soft3d )
        printf("Renderer: software 3D (CPU)\n");

    instance = LibToriRS_InstanceNew();
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

    cache = cachelib_new(CACHE_MODE_DAT1);
    if( !cache )
    {
        printf("Failed to create cache\n");
        goto error_exit;
    }

    if( cachelib_platform_init(cache, cache_dat_path) != 1 )
    {
        printf("Failed to init cache\n");
        goto error_exit;
    }

    io_reactor = LibToriPlatformX_IOReactorNew(cache);
    if( !io_reactor )
    {
        printf("Failed to create IO reactor\n");
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

    if( use_soft3d )
    {
        if( !LibToriPlatformSDL2_InitForSoft3D(platform, screen_w, screen_h) )
        {
            printf("Failed to init SDL2 soft3d window\n");
            goto error_exit;
        }
        renderer_soft3d = LibToriPlatformSDL2_RendererSoft3D_New(screen_w, screen_h);
        if( !renderer_soft3d )
        {
            printf("Failed to create soft3d renderer\n");
            goto error_exit;
        }
        if( !LibToriPlatformSDL2_RendererSoft3D_Init(
                renderer_soft3d, LibToriPlatformSDL2_GetWindow(platform)) )
        {
            printf("Failed to init soft3d renderer\n");
            goto error_exit;
        }
    }
#if defined(_WIN32)
    else if( !LibToriPlatformSDL2_InitForD3D9(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 D3D9 window\n");
        goto error_exit;
    }
    else
    {
        renderer = LibToriPlatformSDL2_RendererD3D9_New(screen_w, screen_h);
        if( !renderer )
        {
            printf("Failed to create D3D9 renderer\n");
            goto error_exit;
        }
        if( !LibToriPlatformSDL2_RendererD3D9_Init(
                renderer, LibToriPlatformSDL2_GetWindow(platform)) )
        {
            printf("Failed to init D3D9 renderer\n");
            goto error_exit;
        }
    }
#else
    else if( !LibToriPlatformSDL2_InitForOpenGL3(platform, screen_w, screen_h) )
    {
        printf("Failed to init SDL2 OpenGL window\n");
        goto error_exit;
    }
    else
    {
        renderer = LibToriPlatformSDL2_RendererGL3_New(screen_w, screen_h);
        if( !renderer )
        {
            printf("Failed to create OpenGL3 renderer\n");
            goto error_exit;
        }
        if( !LibToriPlatformSDL2_RendererGL3_Init(
                renderer, LibToriPlatformSDL2_GetWindow(platform)) )
        {
            printf("Failed to init OpenGL3 renderer\n");
            goto error_exit;
        }
    }
#endif

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
                LibToriPlatformX_IOReactorProcess(io_reactor, LibToriRS_GetIOQueue(instance));
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

        if( use_soft3d )
            LibToriPlatformSDL2_RendererSoft3D_Render(renderer_soft3d, instance);
#if defined(_WIN32)
        else
            LibToriPlatformSDL2_RendererD3D9_Render(renderer, instance);
#else
        else
            LibToriPlatformSDL2_RendererGL3_Render(renderer, instance);
#endif

        SDL_Delay(1);
    }

exit:
    if( renderer_soft3d )
        LibToriPlatformSDL2_RendererSoft3D_Free(renderer_soft3d);
    if( renderer )
    {
#if defined(_WIN32)
        LibToriPlatformSDL2_RendererD3D9_Free(renderer);
#else
        LibToriPlatformSDL2_RendererGL3_Free(renderer);
#endif
    }
    if( io_reactor )
        LibToriPlatformX_IOReactorFree(io_reactor);
    if( cache )
        cachelib_free(cache);
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
