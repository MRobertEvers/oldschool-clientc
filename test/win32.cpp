#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

extern "C" {
#include "LibToriRSPlatformC.h"
#include "osrs/ginput.h"
#include "platforms/common/sockstream.h"
#include "tori_rs.h"
}

#include "platforms/platform_impl2_win32.h"
#include "platforms/platform_impl2_win32_renderer_gdisoft3d.h"
#if defined(TORIRS_HAS_D3D8)
#include "platforms/platform_impl2_win32_renderer_d3d8.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

/** 0 = GDI Soft3D, 1 = D3D8 (requires TORIRS_HAS_D3D8). */
static int
win32_pick_renderer_kind(int argc, char** argv)
{
    const char* env = getenv("TORIRS_WIN32_RENDERER");
    if( env )
    {
        if( strcmp(env, "d3d8") == 0 )
            return 1;
        if( strcmp(env, "gdi") == 0 )
            return 0;
    }
    for( int i = 1; i < argc; ++i )
    {
        if( strncmp(argv[i], "--renderer=", 11) == 0 )
        {
            const char* v = argv[i] + 11;
            if( strcmp(v, "d3d8") == 0 )
                return 1;
            if( strcmp(v, "gdi") == 0 )
                return 0;
        }
    }
    return 0;
}

static void
win32_log_renderer_selection(int argc, char** argv, int want_kind)
{
    const char* env = getenv("TORIRS_WIN32_RENDERER");
    fprintf(
        stderr,
        "[win32] TORIRS_WIN32_RENDERER=%s\n",
        env ? env : "(unset)");
    const char* arg_renderer = nullptr;
    for( int i = 1; i < argc; ++i )
    {
        if( strncmp(argv[i], "--renderer=", 11) == 0 )
            arg_renderer = argv[i] + 11;
    }
    fprintf(
        stderr,
        "[win32] --renderer= from argv: %s\n",
        arg_renderer ? arg_renderer : "(not set)");
    fprintf(
        stderr,
        "[win32] resolved requested renderer: %s\n",
        want_kind == 1 ? "d3d8" : "gdi");
    fflush(stderr);
}

int
main(
    int argc,
    char* argv[])
{
#if defined(TORIRS_HAS_D3D8)
#    if defined(TORIRS_D3D8_STATIC_LINK)
    fprintf(
        stderr,
        "[win32] D3D8 renderer: available (compiled in, statically linked to d3d8)\n");
#    else
    fprintf(
        stderr,
        "[win32] D3D8 renderer: available (compiled in, loads d3d8.dll at runtime)\n");
#    endif
#else
    fprintf(stderr, "[win32] D3D8 renderer: not compiled in (d3d8.h missing at configure)\n");
#endif

    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();
    struct GGame* game = LibToriRS_GameNew(net_shared, 513, 335);
    struct GInput input = { 0 };
    const int game_width = 765;
    const int game_height = 503;
    const int render_max_width = game_width;
    const int render_max_height = game_height;
    struct ToriRSRenderCommandBuffer* render_command_buffer =
        LibToriRS_RenderCommandBufferNew(1024);
    struct Platform2_Win32* platform = Platform2_Win32_New();
    if( !platform )
    {
        printf("Failed to create platform\n");
        LibToriRS_GameFree(game);
        LibToriRS_RenderCommandBufferFree(render_command_buffer);
        LibToriRS_NetFreeBuffer(net_shared);
        return 1;
    }

    platform->current_game = game;
    platform->render_command_buffer = render_command_buffer;

    if( !Platform2_Win32_InitForSoft3D(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
    {
        printf("Failed to initialize Win32 platform\n");
        Platform2_Win32_Free(platform);
        LibToriRS_GameFree(game);
        LibToriRS_RenderCommandBufferFree(render_command_buffer);
        LibToriRS_NetFreeBuffer(net_shared);
        return 1;
    }

    const int want_renderer = win32_pick_renderer_kind(argc, argv);
    win32_log_renderer_selection(argc, argv, want_renderer);

    struct Platform2_Win32_Renderer_GDISoft3D* renderer_gdi = nullptr;
#if defined(TORIRS_HAS_D3D8)
    struct Platform2_Win32_Renderer_D3D8* renderer_d3d8 = nullptr;
#endif

    bool using_d3d8 = false;

#if defined(TORIRS_HAS_D3D8)
    if( want_renderer == 1 )
    {
        renderer_d3d8 = PlatformImpl2_Win32_Renderer_D3D8_New(
            game_width, game_height, render_max_width, render_max_height);
        if( renderer_d3d8 && PlatformImpl2_Win32_Renderer_D3D8_Init(renderer_d3d8, platform) )
        {
            platform->win32_renderer_for_paint = (void*)renderer_d3d8;
            platform->win32_renderer_kind = TORIRS_WIN32_RENDERER_KIND_D3D8;
            using_d3d8 = true;
            fprintf(stderr, "[win32] Active renderer: D3D8\n");
            fflush(stderr);
        }
        else
        {
            fprintf(
                stderr,
                "[win32] D3D8 renderer init failed; falling back to GDI Soft3D. "
                "(Try updating GPU drivers / d3d8.dll.)\n");
            if( renderer_d3d8 )
            {
                PlatformImpl2_Win32_Renderer_D3D8_Free(renderer_d3d8);
                renderer_d3d8 = nullptr;
            }
        }
    }
#else
    if( want_renderer == 1 )
        fprintf(
            stderr,
            "[win32] D3D8 renderer was requested but this build was compiled without "
            "TORIRS_HAS_D3D8; using GDI Soft3D.\n");
#endif

    if( !using_d3d8 )
    {
        renderer_gdi = PlatformImpl2_Win32_Renderer_GDISoft3D_New(
            game_width, game_height, render_max_width, render_max_height);
        if( !renderer_gdi || !PlatformImpl2_Win32_Renderer_GDISoft3D_Init(renderer_gdi, platform) )
        {
            printf("Failed to create GDI Soft3D renderer\n");
#if defined(TORIRS_HAS_D3D8)
            if( renderer_d3d8 )
                PlatformImpl2_Win32_Renderer_D3D8_Free(renderer_d3d8);
#endif
            if( renderer_gdi )
                PlatformImpl2_Win32_Renderer_GDISoft3D_Free(renderer_gdi);
            Platform2_Win32_Free(platform);
            LibToriRS_GameFree(game);
            LibToriRS_RenderCommandBufferFree(render_command_buffer);
            LibToriRS_NetFreeBuffer(net_shared);
            return 1;
        }
        platform->win32_renderer_for_paint = (void*)renderer_gdi;
        platform->win32_renderer_kind = TORIRS_WIN32_RENDERER_KIND_GDI;
        fprintf(stderr, "[win32] Active renderer: GDI Soft3D\n");
        fflush(stderr);
    }

    game->iface_view_port->width = game_width;
    game->iface_view_port->height = game_height;
    game->iface_view_port->x_center = game_width / 2;
    game->iface_view_port->y_center = game_height / 2;
    game->iface_view_port->stride = game_width;
    game->iface_view_port->clip_left = 0;
    game->iface_view_port->clip_top = 0;
    game->iface_view_port->clip_right = game_width;
    game->iface_view_port->clip_bottom = game_height;

    game->viewport_offset_x = 4;
    game->viewport_offset_y = 4;
    if( using_d3d8 )
    {
#if defined(TORIRS_HAS_D3D8)
        PlatformImpl2_Win32_Renderer_D3D8_SetDashOffset(renderer_d3d8, 4, 4);
#endif
    }
    else
    {
        PlatformImpl2_Win32_Renderer_GDISoft3D_SetDashOffset(renderer_gdi, 4, 4);
        renderer_gdi->clicked_tile_x = -1;
        renderer_gdi->clicked_tile_z = -1;
        renderer_gdi->clicked_tile_level = -1;
    }

    struct SockStream* login_stream = NULL;
    sockstream_init();
    // login_stream = sockstream_new();

    char const* host = "127.0.0.1:43594";
    // LibToriRS_NetConnectLogin(game, host, "asdf2", "a");

    while( LibToriRS_GameIsRunning(game) )
    {
        Platform2_Win32_RunLuaScripts(platform, game);

        LibToriRSPlatformC_NetPoll(game->net_shared, login_stream);
        LibToriRS_NetPump(game);

        game->tick_ms = (uint64_t)(uint32_t)GetTickCount();

        Platform2_Win32_PollEvents(platform, &input, 0);

        LibToriRS_GameStep(game, &input, render_command_buffer);

        if( using_d3d8 )
        {
#if defined(TORIRS_HAS_D3D8)
            PlatformImpl2_Win32_Renderer_D3D8_Render(
                renderer_d3d8, game, render_command_buffer);
#endif
        }
        else
        {
            PlatformImpl2_Win32_Renderer_GDISoft3D_Render(
                renderer_gdi, game, render_command_buffer);
        }
    }

    if( login_stream )
    {
        sockstream_free(login_stream);
        login_stream = NULL;
    }
    sockstream_cleanup();

    if( using_d3d8 )
    {
#if defined(TORIRS_HAS_D3D8)
        PlatformImpl2_Win32_Renderer_D3D8_Free(renderer_d3d8);
#endif
    }
    else
    {
        PlatformImpl2_Win32_Renderer_GDISoft3D_Free(renderer_gdi);
    }
    LibToriRS_GameFree(game);
    Platform2_Win32_Free(platform);
    LibToriRS_RenderCommandBufferFree(render_command_buffer);
    LibToriRS_NetFreeBuffer(net_shared);
    return 0;
}

#else

int
main(void)
{
    return 0;
}

#endif
