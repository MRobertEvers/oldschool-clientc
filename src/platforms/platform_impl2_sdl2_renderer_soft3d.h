#ifndef PLATFORM_IMPL2_SDL2_RENDERER_SOFT3D_H
#define PLATFORM_IMPL2_SDL2_RENDERER_SOFT3D_H

#include "platform_impl2_sdl2.h"
#include "platform_impl2_sdl2_renderer_soft3d_shared.h"

struct Server;

struct Platform2_SDL2_Renderer_Soft3D*
PlatformImpl2_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_SDL2_Renderer_Soft3D_Free(struct Platform2_SDL2_Renderer_Soft3D* renderer);

bool
PlatformImpl2_SDL2_Renderer_Soft3D_Init(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_Soft3D_Shutdown(struct Platform2_SDL2_Renderer_Soft3D* renderer);

void
PlatformImpl2_SDL2_Renderer_Soft3D_Render(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y);

void
PlatformImpl2_SDL2_Renderer_Soft3D_InitExampleInterface(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game);

void
PlatformImpl2_SDL2_Renderer_Soft3D_ProcessServer(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct Server* server,
    struct GGame* game,
    uint64_t timestamp_ms);

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetDynamicPixelSize(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    bool dynamic);

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetViewportChangedCallback(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata);

#endif
