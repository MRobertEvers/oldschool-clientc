#ifndef PLATFORM_IMPL2_SDL2_RENDERER_SOFT3D_SHARED_H
#define PLATFORM_IMPL2_SDL2_RENDERER_SOFT3D_SHARED_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

struct GGame;
struct ToriRSRenderCommandBuffer;

/** Shared soft3D renderer state (native SDL2 + Emscripten SDL2). `platform` is
 *  `Platform2_SDL2*`; use accessors in shared.cpp. */
struct Platform2_SDL2_Renderer_Soft3D
{
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    void* platform;
    int* pixel_buffer;
    int width;
    int height;
    int max_width;
    int max_height;

    int dash_offset_x;
    int dash_offset_y;

    float time_delta_accumulator;

    int first_frame;
    int clicked_tile_x;
    int clicked_tile_z;
    int clicked_tile_level;

    int initial_width;
    int initial_height;

    int initial_view_port_width;
    int initial_view_port_height;

    bool pixel_size_dynamic;

    void (*on_viewport_changed)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata);
    void* on_viewport_changed_userdata;
};

/** Pixel buffer is `width`×`height`; `max_width`/`max_height` only clamp viewport scaling. */
struct Platform2_SDL2_Renderer_Soft3D*
PlatformImpl2_SDL2_Renderer_Soft3DShared_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_Shutdown(struct Platform2_SDL2_Renderer_Soft3D* renderer);

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_Free(struct Platform2_SDL2_Renderer_Soft3D* renderer);

bool
PlatformImpl2_SDL2_Renderer_Soft3DShared_Init(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    void* platform);

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_Render(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_SetDashOffset(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y);

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_SetDynamicPixelSize(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    bool dynamic);

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_SetViewportChangedCallback(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata);

#endif
