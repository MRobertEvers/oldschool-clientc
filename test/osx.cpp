extern "C" {
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/lclogin.h"
#include "server/server.h"
#include "tori_rs.h"
}

#include "platforms/platform_impl2_osx_sdl2.h"
#include "platforms/platform_impl2_osx_sdl2_renderer_soft3d.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <SDL.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 500
#define LOGIN_PORT 43594

// Helper function to create and connect socket
static int
create_login_socket(
    const char* host,
    int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( sockfd < 0 )
    {
        printf("Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    // Set socket to non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    if( flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0 )
    {
        printf("Failed to set non-blocking: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if( inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0 )
    {
        printf("Invalid address: %s\n", host);
        close(sockfd);
        return -1;
    }

    // Try to connect (non-blocking)
    int result = connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if( result < 0 && errno != EINPROGRESS )
    {
        printf("Failed to connect: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    // For non-blocking connect, we need to check if connection is ready
    // Using select to wait for connection
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
    if( result <= 0 )
    {
        printf("Connection timeout or error\n");
        close(sockfd);
        return -1;
    }

    // Check if connection succeeded
    int error = 0;
    socklen_t len = sizeof(error);
    if( getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0 )
    {
        printf("Connection failed: %s\n", error ? strerror(error) : "unknown error");
        close(sockfd);
        return -1;
    }

    printf("Connected to %s:%d\n", host, port);
    return sockfd;
}

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
    struct GRenderCommandBuffer* render_command_buffer = grendercb_new(1024);
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

    // Initialize login state machine
    struct LCLogin login;
    int32_t jag_checksum[9] = {
        0
    }; // Initialize with zeros - should be set from cache in real implementation

    // Initialize login
    lclogin_init(&login, 317, jag_checksum, false);

    // Load RSA public key from environment variables with defaults
    if( lclogin_load_rsa_public_key_from_env(&login) < 0 )
    {
        printf("Failed to load RSA public key from environment variables\n");
        // Try falling back to PEM file
        if( lclogin_load_rsa_public_key(&login, "../public.pem") < 0 )
        {
            printf("Failed to load RSA public key from public.pem\n");
            // Continue anyway - login will fail gracefully
        }
    }

    // Create socket connection to login server
    int login_socket = create_login_socket("127.0.0.1", LOGIN_PORT);
    if( login_socket < 0 )
    {
        printf("Failed to create login socket\n");
        // Continue anyway - login will fail gracefully
    }
    else
    {
        lclogin_set_socket(&login, login_socket);

        // Start login process (using placeholder credentials - adjust as needed)
        // In a real implementation, these would come from user input or config
        const char* username = "asdf";
        const char* password = "a";
        lclogin_start(&login, username, password, false);

        printf("Login process started\n");
    }

    renderer->clicked_tile_x = -1;
    renderer->clicked_tile_z = -1;

    LibToriRS_GameStepTasks(game, &input, render_command_buffer);
    while( LibToriRS_GameIsRunning(game) )
    {
        // Process login state machine
        lclogin_state_t login_state = lclogin_get_state(&login);
        if( login_state != LCLOGIN_STATE_IDLE && login_state != LCLOGIN_STATE_SUCCESS &&
            login_state != LCLOGIN_STATE_ERROR )
        {
            int login_result = lclogin_process(&login);
            if( login_result != 0 )
            {
                // Login completed (success or error)
                login_state = lclogin_get_state(&login);
                if( login_state == LCLOGIN_STATE_SUCCESS )
                {
                    printf("Login successful!\n");
                    lclogin_result_t result = lclogin_get_result(&login);
                    printf("Login result: %d\n", (int)result);
                }
                else if( login_state == LCLOGIN_STATE_ERROR )
                {
                    printf(
                        "Login failed: %s - %s\n",
                        lclogin_get_message0(&login),
                        lclogin_get_message1(&login));
                    lclogin_result_t result = lclogin_get_result(&login);
                    printf("Login result: %d\n", (int)result);
                }
            }
        }

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

    // Cleanup login
    lclogin_cleanup(&login);

    // Cleanup server
    server_free(server);

    return 0;
}