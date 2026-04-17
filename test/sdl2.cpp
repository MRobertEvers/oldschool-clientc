#define SDL_MAIN_HANDLED

extern "C" {
#include "LibToriRSPlatformC.h"
#include "osrs/ginput.h"
#include "platforms/common/sockstream.h"
#include "tori_rs.h"
}
extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
}
#include "platforms/platform_impl2_sdl2.h"
#if defined(__APPLE__)
#include "platforms/platform_impl2_sdl2_renderer_metal.h"
#endif
#if !defined(_WIN32)
#include "platforms/platform_impl2_sdl2_renderer_opengl3.h"
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
#include "platforms/platform_impl2_sdl2_renderer_d3d11.h"
#endif
#include "platforms/platform_impl2_sdl2_renderer_soft3d.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>
#endif

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 500
#define LOGIN_PORT 43594

enum RendererKind
{
    RENDERER_SOFT3D,
#if !defined(_WIN32)
    RENDERER_OPENGL3,
#endif
#if defined(__APPLE__)
    RENDERER_METAL,
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
    RENDERER_D3D11,
#endif
};

static RendererKind
select_renderer(
    int argc,
    char* argv[])
{
    const char* env_renderer = getenv("TORIRS_RENDERER");
    if( env_renderer )
    {
#if !defined(_WIN32)
        if( strcmp(env_renderer, "opengl3") == 0 || strcmp(env_renderer, "gl3") == 0 )
            return RENDERER_OPENGL3;
#endif
        if( strcmp(env_renderer, "soft3d") == 0 || strcmp(env_renderer, "soft") == 0 )
            return RENDERER_SOFT3D;
#if defined(__APPLE__)
        if( strcmp(env_renderer, "metal") == 0 )
            return RENDERER_METAL;
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
        if( strcmp(env_renderer, "d3d11") == 0 || strcmp(env_renderer, "dx11") == 0 )
            return RENDERER_D3D11;
#endif
    }

    for( int i = 1; i < argc; i++ )
    {
#if !defined(_WIN32)
        if( strcmp(argv[i], "--renderer=opengl3") == 0 || strcmp(argv[i], "--opengl3") == 0 )
            return RENDERER_OPENGL3;
#endif
        if( strcmp(argv[i], "--renderer=soft3d") == 0 || strcmp(argv[i], "--soft3d") == 0 )
            return RENDERER_SOFT3D;
#if defined(__APPLE__)
        if( strcmp(argv[i], "--renderer=metal") == 0 || strcmp(argv[i], "--metal") == 0 )
            return RENDERER_METAL;
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
        if( strcmp(argv[i], "--renderer=d3d11") == 0 || strcmp(argv[i], "--d3d11") == 0 ||
            strcmp(argv[i], "--renderer=dx11") == 0 || strcmp(argv[i], "--dx11") == 0 )
            return RENDERER_D3D11;
#endif
    }

    return RENDERER_SOFT3D;
}

static void
osx_abort_startup(
    struct GGame* game,
    struct Platform2_SDL2* platform,
    struct ToriRSRenderCommandBuffer* rcb,
    struct ToriRSNetSharedBuffer* net)
{
    if( game )
        LibToriRS_GameFree(game);
    if( platform )
        Platform2_SDL2_Free(platform);
    if( rcb )
        LibToriRS_RenderCommandBufferFree(rcb);
    if( net )
        LibToriRS_NetFreeBuffer(net);
}
#if defined(__APPLE__)
static void
osx_print_heap_at_exit(void)
{
    malloc_statistics_t mz = { 0 };
    malloc_zone_statistics(malloc_default_zone(), &mz);

    task_vm_info_data_t vm = { 0 };
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if( task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) != KERN_SUCCESS )
    {
        printf(
            "At exit: malloc default zone in use ~%zu bytes (%.2f MiB), allocated %zu bytes "
            "(%.2f MiB)\n",
            mz.size_in_use,
            mz.size_in_use / (1024.0 * 1024.0),
            mz.size_allocated,
            mz.size_allocated / (1024.0 * 1024.0));
        return;
    }

    printf(
        "At exit: malloc default zone in use ~%zu bytes (%.2f MiB), allocated %zu bytes (%.2f "
        "MiB); "
        "task phys_footprint ~%llu bytes (%.2f MiB)\n",
        mz.size_in_use,
        mz.size_in_use / (1024.0 * 1024.0),
        mz.size_allocated,
        mz.size_allocated / (1024.0 * 1024.0),
        (unsigned long long)vm.phys_footprint,
        vm.phys_footprint / (1024.0 * 1024.0));
}
#endif

int
main(
    int argc,
    char* argv[])
{
    const RendererKind renderer_kind = select_renderer(argc, argv);
    bool has_message = false;
    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();
    struct GGame* game = LibToriRS_GameNew(net_shared, 513, 335);
    struct GInput input = { 0 };
    const int game_width = 765;
    const int game_height = 503;
    const int render_max_width = game_width;
    const int render_max_height = game_height;
    struct ToriRSRenderCommandBuffer* render_command_buffer =
        LibToriRS_RenderCommandBufferNew(1024);
    struct Platform2_SDL2* platform = Platform2_SDL2_New();
    if( !platform )
    {
        printf("Failed to create platform\n");
        osx_abort_startup(game, NULL, render_command_buffer, net_shared);
        return 1;
    }
    platform->current_game = game;

#if !defined(_WIN32)
    if( renderer_kind == RENDERER_OPENGL3 )
    {
        if( !Platform2_SDL2_InitForOpenGL3(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
        {
            printf("Failed to initialize platform for OpenGL3\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }
    else
#endif
#if defined(__APPLE__)
        if( renderer_kind == RENDERER_METAL )
    {
        if( !Platform2_SDL2_InitForMetal(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
        {
            printf("Failed to initialize platform for Metal\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }
    else
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
        if( renderer_kind == RENDERER_D3D11 )
    {
        if( !Platform2_SDL2_InitForD3D11(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
        {
            printf("Failed to initialize platform for D3D11\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }
    else
#endif
        if( !Platform2_SDL2_InitForSoft3D(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
    {
        printf("Failed to initialize platform\n");
        osx_abort_startup(game, platform, render_command_buffer, net_shared);
        return 1;
    }

    Platform2_SDL2_Renderer_Soft3D* renderer_soft3d = NULL;
#if !defined(_WIN32)
    struct Platform2_SDL2_Renderer_OpenGL3* renderer_opengl3 = NULL;
#endif
#if defined(__APPLE__)
    struct Platform2_SDL2_Renderer_Metal* renderer_metal = NULL;
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
    struct Platform2_SDL2_Renderer_D3D11* renderer_d3d11 = NULL;
#endif

#if !defined(_WIN32)
    if( renderer_kind == RENDERER_OPENGL3 )
    {
        renderer_opengl3 = PlatformImpl2_SDL2_Renderer_OpenGL3_New(SCREEN_WIDTH, SCREEN_HEIGHT);
        if( !renderer_opengl3 )
        {
            printf("Failed to create OpenGL3 renderer\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
        if( !PlatformImpl2_SDL2_Renderer_OpenGL3_Init(renderer_opengl3, platform) )
        {
            printf("Failed to initialize OpenGL3 renderer\n");
            PlatformImpl2_SDL2_Renderer_OpenGL3_Free(renderer_opengl3);
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }
    else
#endif
#if defined(__APPLE__)
        if( renderer_kind == RENDERER_METAL )
    {
        renderer_metal = PlatformImpl2_SDL2_Renderer_Metal_New(SCREEN_WIDTH, SCREEN_HEIGHT);
        if( !renderer_metal )
        {
            printf("Failed to create Metal renderer\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
        if( !PlatformImpl2_SDL2_Renderer_Metal_Init(renderer_metal, platform) )
        {
            printf("Failed to initialize Metal renderer\n");
            PlatformImpl2_SDL2_Renderer_Metal_Free(renderer_metal);
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }
    else
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
        if( renderer_kind == RENDERER_D3D11 )
    {
        renderer_d3d11 = PlatformImpl2_SDL2_Renderer_D3D11_New(SCREEN_WIDTH, SCREEN_HEIGHT);
        if( !renderer_d3d11 )
        {
            printf("Failed to create D3D11 renderer\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
        if( !PlatformImpl2_SDL2_Renderer_D3D11_Init(renderer_d3d11, platform) )
        {
            printf("Failed to initialize D3D11 renderer\n");
            PlatformImpl2_SDL2_Renderer_D3D11_Free(renderer_d3d11);
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }
    else
#endif
    {
        renderer_soft3d =
            PlatformImpl2_SDL2_Renderer_Soft3D_New(SCREEN_WIDTH, SCREEN_HEIGHT, render_max_width, render_max_height);
        if( !renderer_soft3d )
        {
            printf("Failed to create Soft3D renderer\n");
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
        if( !PlatformImpl2_SDL2_Renderer_Soft3D_Init(renderer_soft3d, platform) )
        {
            printf("Failed to initialize Soft3D renderer\n");
            PlatformImpl2_SDL2_Renderer_Soft3D_Free(renderer_soft3d);
            osx_abort_startup(game, platform, render_command_buffer, net_shared);
            return 1;
        }
    }

    /* Fixed game output size — iface_view_port is locked to this so letterboxing
     * scales a consistent 765×503 frame to the window size. */

    game->iface_view_port->width = game_width;
    game->iface_view_port->height = game_height;
    game->iface_view_port->x_center = game_width / 2;
    game->iface_view_port->y_center = game_height / 2;
    game->iface_view_port->stride = game_width;
    game->iface_view_port->clip_left = 0;
    game->iface_view_port->clip_top = 0;
    game->iface_view_port->clip_right = game_width;
    game->iface_view_port->clip_bottom = game_height;

    /* Keep viewport config identical across renderer backends. */
    game->viewport_offset_x = 4;
    game->viewport_offset_y = 4;
    if( renderer_soft3d )
        PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(renderer_soft3d, 4, 4);

    struct SockStream* login_stream = NULL;
    sockstream_init();
    login_stream = sockstream_new();

    if( renderer_soft3d )
    {
        renderer_soft3d->clicked_tile_x = -1;
        renderer_soft3d->clicked_tile_z = -1;
    }

    char const* host = "127.0.0.1:43594";
    LibToriRS_NetConnectLogin(game, host, "asdf2", "a");

    uint8_t recv_buffer[4096];
    int reconnect_requested = 0;
    while( LibToriRS_GameIsRunning(game) )
    {
        Platform2_SDL2_RunLuaScripts(platform, game);

        LibToriRSPlatformC_NetPoll(game->net_shared, login_stream);
        LibToriRS_NetPump(game);

        uint64_t timestamp_ms = SDL_GetTicks64();

        Platform2_SDL2_PollEvents(platform, &input, 0);

        game->tick_ms = timestamp_ms;

        LibToriRS_GameStep(game, &input, render_command_buffer);

#if !defined(_WIN32)
        if( renderer_opengl3 )
            PlatformImpl2_SDL2_Renderer_OpenGL3_Render(
                renderer_opengl3, game, render_command_buffer);
        else
#endif
#if defined(__APPLE__)
            if( renderer_metal )
            PlatformImpl2_SDL2_Renderer_Metal_Render(
                renderer_metal, game, render_command_buffer);
        else
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
            if( renderer_d3d11 )
            PlatformImpl2_SDL2_Renderer_D3D11_Render(
                renderer_d3d11, game, render_command_buffer);
        else
#endif
            PlatformImpl2_SDL2_Renderer_Soft3D_Render(
                renderer_soft3d, game, render_command_buffer);
    }

    if( login_stream )
    {
        sockstream_free(login_stream);
        login_stream = NULL;
    }

    sockstream_cleanup();
#if !defined(_WIN32)
    if( renderer_opengl3 )
        PlatformImpl2_SDL2_Renderer_OpenGL3_Free(renderer_opengl3);
#endif
#if defined(__APPLE__)
    if( renderer_metal )
        PlatformImpl2_SDL2_Renderer_Metal_Free(renderer_metal);
#endif
#if defined(_WIN32) && !defined(TORIRS_NO_D3D11)
    if( renderer_d3d11 )
        PlatformImpl2_SDL2_Renderer_D3D11_Free(renderer_d3d11);
#endif
    if( renderer_soft3d )
        PlatformImpl2_SDL2_Renderer_Soft3D_Free(renderer_soft3d);

    LibToriRS_GameFree(game);
    Platform2_SDL2_Free(platform);
    LibToriRS_RenderCommandBufferFree(render_command_buffer);
    LibToriRS_NetFreeBuffer(net_shared);
#if defined(__APPLE__)
    osx_print_heap_at_exit();
#endif
    return 0;
}
