#define SDL_MAIN_HANDLED

extern "C" {
#include "LibToriRSPlatformC.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "platforms/common/sockstream.h"
#include "server/server.h"
#include "tori_rs.h"
}
extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
}
#include "platforms/platform_impl2_osx_sdl2.h"
#include "platforms/platform_impl2_osx_sdl2_renderer_soft3d.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 500
#define LOGIN_PORT 43594

int
main(
    int argc,
    char* argv[])
{
    bool has_message = false;
    struct GIOQueue* io = gioq_new();
    struct GGame* game = LibToriRS_GameNew(io, 513, 335);
    struct GInput input = { 0 };
    struct ToriRSRenderCommandBuffer* render_command_buffer =
        LibToriRS_RenderCommandBufferNew(1024);
    struct GIOMessage message = { 0 };
    struct Platform2_OSX_SDL2* platform = Platform2_OSX_SDL2_New();
    platform->current_game = game;
    if( !platform )
    {
        printf("Failed to create platform\n");
        return 1;
    }
    if( !Platform2_OSX_SDL2_InitForSoft3D(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
    {
        printf("Failed to initialize platform\n");
        return 1;
    }

    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer =
        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(SCREEN_WIDTH, SCREEN_HEIGHT, 1600, 900);
    if( !renderer )
    {
        printf("Failed to create renderer\n");
        return 1;
    }
    if( !PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(renderer, platform) )
    {
        printf("Failed to initialize renderer\n");
        return 1;
    }

    game->iface_view_port->width = renderer->width;
    game->iface_view_port->height = renderer->height;
    game->iface_view_port->x_center = renderer->width / 2;
    game->iface_view_port->y_center = renderer->height / 2;
    game->iface_view_port->stride = renderer->width;

    PlatformImpl2_OSX_SDL2_Renderer_Soft3D_SetDashOffset(renderer, 4, 4);
    game->viewport_offset_x = 4;
    game->viewport_offset_y = 4;

    // Create server instance (independent of renderer)
    struct Server* server = server_new();
    if( !server )
    {
        printf("Failed to create server\n");
        return 1;
    }

    struct SockStream* login_stream = NULL;
    sockstream_init();
    /* Optional: start connection; LibToriRSPlatformC_NetPoll will poll for completion each frame. */
    login_stream = sockstream_connect("127.0.0.1", LOGIN_PORT, 5);

    renderer->clicked_tile_x = -1;
    renderer->clicked_tile_z = -1;

    LibToriRS_NetContext* net_ctx = LibToriRS_NetNewContext();
    if( !net_ctx )
    {
        printf("Failed to create net context\n");
        return 1;
    }
    LibToriRS_NetInit(game, net_ctx);

    LibToriRS_NetConnectLogin(game, "asdf2", "a");

    uint8_t recv_buffer[4096];
    int reconnect_requested = 0;
    while( LibToriRS_GameIsRunning(game) )
    {
        Platform2_OSX_SDL2_RunLuaScripts(platform, game);

        reconnect_requested = 0;
        int poll_status = LibToriRSPlatformC_NetPoll(
            login_stream, net_ctx, recv_buffer, sizeof(recv_buffer), &reconnect_requested);
        if( poll_status == LibToriRSPlatformC_NetPoll_ReconnectRequested )
        {
            if( login_stream )
            {
                sockstream_close(login_stream);
                login_stream = NULL;
            }
            /* LibToriRSPlatformC_NetPoll will call sockstream_poll_connect each frame. */
            login_stream = sockstream_connect("127.0.0.1", LOGIN_PORT, 5);
        }
        else if( poll_status == LibToriRSPlatformC_NetPoll_Error )
        {
            printf("Login socket error\n");
            if( login_stream )
            {
                sockstream_close(login_stream);
                login_stream = NULL;
            }
        }

        LibToriRS_NetPoll(net_ctx);

        // Process server messages from previous frame and step server
        uint64_t timestamp_ms = SDL_GetTicks64();
        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_ProcessServer(renderer, server, game, timestamp_ms);

        // Poll backend
        // Platform2_OSX_SDL2_PollIO(platform, io);
        Platform2_OSX_SDL2_PollEvents(
            platform, &input, (game->chat_interface_id == -1 && game->chat_input_focused) ? 1 : 0);

        // Update game tick time for camera movement timing
        game->tick_ms = timestamp_ms;

        LibToriRS_GameStep(game, &input, render_command_buffer);

        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(renderer, game, render_command_buffer);
    }

    // // Cleanup login
    // lclogin_cleanup(&login);

    // Cleanup socket
    if( login_stream )
    {
        sockstream_close(login_stream);
        login_stream = NULL;
    }

    LibToriRS_NetFreeContext(net_ctx);

    // Cleanup server
    server_free(server);

    sockstream_cleanup();
    return 0;
}