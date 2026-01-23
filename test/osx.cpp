extern "C" {
#include "libg.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
}

#include "platforms/platform_impl2_osx_sdl2.h"
#include "platforms/platform_impl2_osx_sdl2_renderer_soft3d.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 500

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
    printf("Hello, World!\n");

    bool has_message = false;
    struct GIOQueue* io = gioq_new();
    struct GGame* game = libg_game_new(io, 512, 334);
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

    PlatformImpl2_OSX_SDL2_Renderer_Soft3D_SetDashOffset(renderer, 5, 5);

    libg_game_step_tasks(game, &input, render_command_buffer);
    while( libg_game_is_running(game) )
    {
        // Poll backend
        Platform2_OSX_SDL2_PollIO(platform, io);
        Platform2_OSX_SDL2_PollEvents(platform, &input);

        libg_game_step(game, &input, render_command_buffer);

        PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(renderer, game, render_command_buffer);
    }

    return 0;
}