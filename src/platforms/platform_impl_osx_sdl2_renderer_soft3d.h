#ifndef PLATFORM_IMPL_OSX_RENDERER_SDL2_H
#define PLATFORM_IMPL_OSX_RENDERER_SDL2_H

extern "C" {
#include "graphics/render.h"
#include "libgame.h"
#include "libgame.u.h"
}
#include "platform_impl_osx_sdl2.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct Renderer
{
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixel_buffer;
    int width;
    int height;

    struct SceneOp* ops;
    int op_count;
    int op_capacity;

    float time_delta_accumulator;
};

struct Renderer* PlatformImpl_OSX_SDL2_Renderer_Soft3D_New(int width, int height);

void PlatformImpl_OSX_SDL2_Renderer_Soft3D_Free(struct Renderer* renderer);

bool
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Init(struct Renderer* renderer, struct Platform* platform);

void PlatformImpl_OSX_SDL2_Renderer_Soft3D_Shutdown(struct Renderer* renderer);

void PlatformImpl_OSX_SDL2_Renderer_Soft3D_Render(
    struct Renderer* renderer, struct Game* game, struct GameGfxOpList* gfx_op_list);

#endif