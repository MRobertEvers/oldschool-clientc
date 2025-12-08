#ifndef PLATFORM_IMPL2_OSX_SDL2_RENDERER_SOFT3D_H
#define PLATFORM_IMPL2_OSX_SDL2_RENDERER_SOFT3D_H

#include "platform_impl2_osx_sdl2.h"

struct Platform2_OSX_SDL2_Renderer_Soft3D
{
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    struct Platform2_OSX_SDL2* platform;
    int* pixel_buffer;
    int width;
    int height;
    int max_width;
    int max_height;

    float time_delta_accumulator;
};

struct Platform2_OSX_SDL2_Renderer_Soft3D*
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(int width, int height, int max_width, int max_height);

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Free(struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

bool PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer, struct Platform2_OSX_SDL2* platform);

void PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Shutdown(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

void PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct GRenderCommand* commands,
    int command_count);

#endif