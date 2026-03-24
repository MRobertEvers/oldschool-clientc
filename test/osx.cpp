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
#include "platforms/platform_impl2_osx_sdl2_renderer_opengl3.h"
#include "platforms/platform_impl2_osx_sdl2_renderer_soft3d.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 500
#define LOGIN_PORT 43594

static bool
should_use_opengl3(
    int argc,
    char* argv[])
{
    const char* env_renderer = getenv("TORIRS_RENDERER");
    if( env_renderer )
    {
        if( strcmp(env_renderer, "opengl3") == 0 || strcmp(env_renderer, "gl3") == 0 )
            return true;
        if( strcmp(env_renderer, "soft3d") == 0 || strcmp(env_renderer, "soft") == 0 )
            return false;
    }

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--renderer=opengl3") == 0 || strcmp(argv[i], "--opengl3") == 0 )
            return true;
        if( strcmp(argv[i], "--renderer=soft3d") == 0 || strcmp(argv[i], "--soft3d") == 0 )
            return false;
    }

    return false;
}

int
main(
    int argc,
    char* argv[])
{
    const bool use_opengl3 = should_use_opengl3(argc, argv);
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
    if( use_opengl3 )
    {
        if( !Platform2_OSX_SDL2_InitForOpenGL3(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
        {
            printf("Failed to initialize platform for OpenGL3\n");
            return 1;
        }
    }
    else if( !Platform2_OSX_SDL2_InitForSoft3D(platform, SCREEN_WIDTH, SCREEN_HEIGHT) )
    {
        printf("Failed to initialize platform\n");
        return 1;
    }

    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer_soft3d = NULL;
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer_opengl3 = NULL;
    if( use_opengl3 )
    {
        renderer_opengl3 = PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_New(SCREEN_WIDTH, SCREEN_HEIGHT);
        if( !renderer_opengl3 )
        {
            printf("Failed to create OpenGL3 renderer\n");
            return 1;
        }
        if( !PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Init(renderer_opengl3, platform) )
        {
            printf("Failed to initialize OpenGL3 renderer\n");
            return 1;
        }
    }
    else
    {
        renderer_soft3d =
            PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(SCREEN_WIDTH, SCREEN_HEIGHT, 1600, 900);
        if( !renderer_soft3d )
        {
            printf("Failed to create Soft3D renderer\n");
            return 1;
        }
        if( !PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(renderer_soft3d, platform) )
        {
            printf("Failed to initialize Soft3D renderer\n");
            return 1;
        }
    }

    const int initial_width = platform->window_width > 0 ? platform->window_width : SCREEN_WIDTH;
    const int initial_height = platform->window_height > 0 ? platform->window_height : SCREEN_HEIGHT;
    game->iface_view_port->width = initial_width;
    game->iface_view_port->height = initial_height;
    game->iface_view_port->x_center = initial_width / 2;
    game->iface_view_port->y_center = initial_height / 2;
    game->iface_view_port->stride = initial_width;

    /* Keep viewport config identical across renderer backends. */
    game->viewport_offset_x = 4;
    game->viewport_offset_y = 4;
    if( !use_opengl3 )
    {
        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_SetDashOffset(renderer_soft3d, 4, 4);
    }

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
        Platform2_OSX_SDL2_RunLuaScripts(platform, game);

        LibToriRSPlatformC_NetPoll(game->net_shared, login_stream);
        LibToriRS_NetPump(game);

        // Process server messages from previous frame and step server
        uint64_t timestamp_ms = SDL_GetTicks64();

        Platform2_OSX_SDL2_PollEvents(
            platform, &input, (game->chat_interface_id == -1 && game->chat_input_focused) ? 1 : 0);

        game->tick_ms = timestamp_ms;

        LibToriRS_GameStep(game, &input, render_command_buffer);

        if( renderer_opengl3 )
            PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Render(
                renderer_opengl3, game, render_command_buffer);
        else
            PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(
                renderer_soft3d, game, render_command_buffer);
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
    if( renderer_opengl3 )
        PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Free(renderer_opengl3);
    if( renderer_soft3d )
        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Free(renderer_soft3d);
    return 0;
}