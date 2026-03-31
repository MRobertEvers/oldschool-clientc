#include "platform_impl2_emscripten_sdl2_renderer_soft3d.h"

Platform2_Emscripten_SDL2_Renderer_Soft3D*
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    return (Platform2_Emscripten_SDL2_Renderer_Soft3D*)PlatformImpl2_SDL2_Renderer_Soft3D_New(
        width, height, max_width, max_height);
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Free(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer)
{
    PlatformImpl2_SDL2_Renderer_Soft3D_Free((struct Platform2_SDL2_Renderer_Soft3D*)renderer);
}

bool
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Init(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_Emscripten_SDL2* platform)
{
    return PlatformImpl2_SDL2_Renderer_Soft3D_Init(
        (struct Platform2_SDL2_Renderer_Soft3D*)renderer, (void*)platform);
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Shutdown(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer)
{
    PlatformImpl2_SDL2_Renderer_Soft3D_Shutdown((struct Platform2_SDL2_Renderer_Soft3D*)renderer);
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Render(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    PlatformImpl2_SDL2_Renderer_Soft3D_Render(
        (struct Platform2_SDL2_Renderer_Soft3D*)renderer, game, render_command_buffer);
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_SetDashOffset(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y)
{
    PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(
        (struct Platform2_SDL2_Renderer_Soft3D*)renderer, offset_x, offset_y);
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_SetDynamicPixelSize(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    bool dynamic)
{
    PlatformImpl2_SDL2_Renderer_Soft3D_SetDynamicPixelSize(
        (struct Platform2_SDL2_Renderer_Soft3D*)renderer, dynamic);
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_SetViewportChangedCallback(
    Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata)
{
    PlatformImpl2_SDL2_Renderer_Soft3D_SetViewportChangedCallback(
        (struct Platform2_SDL2_Renderer_Soft3D*)renderer, callback, userdata);
}
