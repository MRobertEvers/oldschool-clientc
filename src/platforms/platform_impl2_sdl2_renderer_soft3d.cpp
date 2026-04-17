#include "platform_impl2_sdl2_renderer_soft3d.h"

extern "C" {
#include "osrs/game.h"
#include "server/prot.h"
#include "server/server.h"
}

struct Platform2_SDL2_Renderer_Soft3D*
PlatformImpl2_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    return PlatformImpl2_SDL2_Renderer_Soft3DShared_New(width, height, max_width, max_height);
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_Free(struct Platform2_SDL2_Renderer_Soft3D* renderer)
{
    PlatformImpl2_SDL2_Renderer_Soft3DShared_Free(renderer);
}

bool
PlatformImpl2_SDL2_Renderer_Soft3D_Init(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_SDL2* platform)
{
    return PlatformImpl2_SDL2_Renderer_Soft3DShared_Init(renderer, (void*)platform);
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_Shutdown(struct Platform2_SDL2_Renderer_Soft3D* renderer)
{
    PlatformImpl2_SDL2_Renderer_Soft3DShared_Shutdown(renderer);
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_Render(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    PlatformImpl2_SDL2_Renderer_Soft3DShared_Render(renderer, game, render_command_buffer);
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y)
{
    PlatformImpl2_SDL2_Renderer_Soft3DShared_SetDashOffset(renderer, offset_x, offset_y);
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_InitExampleInterface(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game)
{
    (void)renderer;
    (void)game;
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_ProcessServer(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct Server* server,
    struct GGame* game,
    uint64_t timestamp_ms)
{
    (void)game;
    (void)timestamp_ms;

    if( !server )
    {
        return;
    }

    if( renderer->first_frame == 0 )
    {
        struct ProtConnect connect;
        connect.pid = 0;
        renderer->first_frame = 1;
    }

    if( renderer->clicked_tile_x != -1 && renderer->clicked_tile_z != -1 )
    {
        struct ProtTileClick tile_click;
        tile_click.x = renderer->clicked_tile_x;
        tile_click.z = renderer->clicked_tile_z;

        renderer->clicked_tile_x = -1;
        renderer->clicked_tile_z = -1;
        renderer->clicked_tile_level = -1;
    }
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetDynamicPixelSize(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    bool dynamic)
{
    PlatformImpl2_SDL2_Renderer_Soft3DShared_SetDynamicPixelSize(renderer, dynamic);
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetViewportChangedCallback(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata)
{
    PlatformImpl2_SDL2_Renderer_Soft3DShared_SetViewportChangedCallback(renderer, callback, userdata);
}
