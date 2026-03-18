#define SDL_MAIN_HANDLED

extern "C" {
#include "LibToriRSPlatformC.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "platforms/common/sockstream.h"
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
    struct ToriRSNetSharedBuffer* net_shared = LibToriRS_NetNewBuffer();
    struct GGame* game = LibToriRS_GameNew(net_shared, 513, 335);
    struct GInput input = { 0 };
    struct ToriRSRenderCommandBuffer* render_command_buffer =
        LibToriRS_RenderCommandBufferNew(1024);
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

    struct SockStream* login_stream = NULL;
    sockstream_init();
    login_stream = sockstream_new();

    renderer->clicked_tile_x = -1;
    renderer->clicked_tile_z = -1;

    LibToriRS_NetConnectLogin(game, "asdf2", "a");

    uint8_t recv_buffer[4096];
    int reconnect_requested = 0;
    while( LibToriRS_GameIsRunning(game) )
    {
        Platform2_OSX_SDL2_RunLuaScripts(platform, game);

        LibToriRSPlatformC_NetPoll(game->net_shared, login_stream);
        LibToriRS_NetPump(game);

        // Process server messages from previous frame and step server
        uint64_t timestamp_ms = SDL_GetTicks64();

        Platform2_OSX_SDL2_PollEvents(
            platform, &input, (game->chat_interface_id == -1 && game->chat_input_focused) ? 1 : 0);

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

    sockstream_cleanup();
    return 0;
}