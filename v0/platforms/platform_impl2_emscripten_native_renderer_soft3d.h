#ifndef PLATFORM_IMPL2_EMSCRIPTEN_NATIVE_RENDERER_SOFT3D_H
#define PLATFORM_IMPL2_EMSCRIPTEN_NATIVE_RENDERER_SOFT3D_H

#include "platform_impl2_emscripten_native.h"
#include <stdbool.h>
#include <stdint.h>

struct Platform2_Emscripten_Native_Renderer_Soft3D
{
    struct Platform2_Emscripten_Native* platform;
    int* pixel_buffer;
    uint8_t* rgba_buffer;
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

struct Platform2_Emscripten_Native_Renderer_Soft3D*
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height);

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Free(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer);

bool
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Init(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    struct Platform2_Emscripten_Native* platform);

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Render(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetDashOffset(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y);

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetDynamicPixelSize(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    bool dynamic);

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetViewportChangedCallback(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata);

#endif
