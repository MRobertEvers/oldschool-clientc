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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 500

int
main(
    int argc,
    char* argv[])
{
    (void)argc;
    (void)argv;

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

    struct Platform2_Win32_Renderer_GDISoft3D* renderer =
        PlatformImpl2_Win32_Renderer_GDISoft3D_New(
            game_width, game_height, render_max_width, render_max_height);
    if( !renderer || !PlatformImpl2_Win32_Renderer_GDISoft3D_Init(renderer, platform) )
    {
        printf("Failed to create GDI Soft3D renderer\n");
        if( renderer )
            PlatformImpl2_Win32_Renderer_GDISoft3D_Free(renderer);
        Platform2_Win32_Free(platform);
        LibToriRS_GameFree(game);
        LibToriRS_RenderCommandBufferFree(render_command_buffer);
        LibToriRS_NetFreeBuffer(net_shared);
        return 1;
    }

    platform->gdi_renderer_for_paint = (void*)renderer;

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
    PlatformImpl2_Win32_Renderer_GDISoft3D_SetDashOffset(renderer, 4, 4);
    renderer->clicked_tile_x = -1;
    renderer->clicked_tile_z = -1;
    renderer->clicked_tile_level = -1;

    struct SockStream* login_stream = NULL;
    sockstream_init();
    login_stream = sockstream_new();

    char const* host = "127.0.0.1:43594";
    LibToriRS_NetConnectLogin(game, host, "asdf2", "a");

    while( LibToriRS_GameIsRunning(game) )
    {
        Platform2_Win32_RunLuaScripts(platform, game);

        LibToriRSPlatformC_NetPoll(game->net_shared, login_stream);
        LibToriRS_NetPump(game);

        game->tick_ms = (uint64_t)(uint32_t)GetTickCount();

        Platform2_Win32_PollEvents(platform, &input, 0);

        LibToriRS_GameStep(game, &input, render_command_buffer);

        PlatformImpl2_Win32_Renderer_GDISoft3D_Render(
            renderer, game, render_command_buffer);
    }

    if( login_stream )
    {
        sockstream_free(login_stream);
        login_stream = NULL;
    }
    sockstream_cleanup();

    PlatformImpl2_Win32_Renderer_GDISoft3D_Free(renderer);
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
