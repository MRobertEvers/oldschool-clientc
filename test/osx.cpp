#define SDL_MAIN_HANDLED

extern "C" {
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "platforms/common/sockstream.h"
#include "server/server.h"
#include "tori_rs.h"
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

void
test_gio(void)
{
    struct GIOQueue* queue = gioq_new();
    struct GIOMessage message = { 0 };

    gioq_submit(queue, GIO_REQ_ASSET, 1, 0, 0);

    bool has_message = gioq_poll(queue, &message);
    assert(!has_message);

    memset(&message, 0, sizeof(struct GIOMessage));
    while( gioqb_read_next(queue, &message) )
    {
        printf(
            "Message: %d, kind: %d, command: %d, param_b: %lld, param_a: %lld, data: %p, "
            "data_size: %d\n",
            message.message_id,
            message.kind,
            message.command,
            message.param_b,
            message.param_a,
            message.data,
            message.data_size);
        gioqb_mark_inflight(queue, message.message_id);
        gioqb_mark_done(
            queue, message.message_id, message.command, message.param_b, message.param_a, NULL, 0);
    }

    has_message = gioq_poll(queue, &message);
    assert(has_message);

    gioq_free(queue);
}

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
        tori_rs_render_command_buffer_new(1024);
    struct GIOMessage message = { 0 };
    struct Platform2_OSX_SDL2* platform = Platform2_OSX_SDL2_New();
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

    // Create server instance (independent of renderer)
    struct Server* server = server_new();
    if( !server )
    {
        printf("Failed to create server\n");
        return 1;
    }

    struct SockStream* login_stream = NULL;
    sockstream_init();
    // Create socket connection to login server
    login_stream = sockstream_connect("127.0.0.1", LOGIN_PORT, 5);
    if( !login_stream )
    {
        printf("Failed to create login socket\n");
        // Continue anyway - login will fail gracefully
    }
    else
    {
        // Poll for connection completion since connect is non-blocking
        printf("Polling for connection...\n");
        int poll_count = 0;
        const int max_polls = 50; // 5 seconds max (50 * 100ms)
        while( !sockstream_is_connected(login_stream) && poll_count < max_polls )
        {
            if( sockstream_poll_connect(login_stream) )
            {
                printf("Login socket connected\n");
                break;
            }
            // Wait a bit before polling again (non-blocking, so we can do other work)
            SDL_Delay(100);
            poll_count++;
        }
        if( !sockstream_is_connected(login_stream) )
        {
            printf("Login socket connection timeout or failed\n");
            sockstream_close(login_stream);
            login_stream = NULL;
        }
    }

    printf("Login socket created\n");

    renderer->clicked_tile_x = -1;
    renderer->clicked_tile_z = -1;

    uint8_t buffer[4096];

    LibToriRS_NetConnect(game, "asdf2", "a");
    while( LibToriRS_GameIsRunning(game) )
    {
        if( LibToriRS_NetIsReady(game) && sockstream_is_connected(login_stream) )
        {
            LibToriRS_NetPump(game);

            int outgoing_size = LibToriRS_NetGetOutgoing(game, buffer, sizeof(buffer));
            if( outgoing_size > 0 )
            {
                sockstream_send(login_stream, buffer, outgoing_size);
            }
            int received = sockstream_recv(login_stream, buffer, sizeof(buffer));
            if( received > 0 )
            {
                LibToriRS_NetRecv(game, buffer, received);
            }
            else if( received < 0 )
            {
                int error = sockstream_lasterror(login_stream);
                if (error != SOCKSTREAM_ERROR_WOULDBLOCK)
                {
                    // Connection error
                    printf("Login socket error: %s\n", sockstream_strerror(error));
                    sockstream_close(login_stream);
                    login_stream = NULL;
                    LibToriRS_NetDisconnected(game);
                }
            }
        }

        // Socket error

        // Process server messages from previous frame and step server
        uint64_t timestamp_ms = SDL_GetTicks64();
        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_ProcessServer(renderer, server, game, timestamp_ms);

        // Poll backend
        Platform2_OSX_SDL2_PollIO(platform, io);
        Platform2_OSX_SDL2_PollEvents(platform, &input);

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

    // Cleanup server
    server_free(server);

    sockstream_cleanup();
    return 0;
}