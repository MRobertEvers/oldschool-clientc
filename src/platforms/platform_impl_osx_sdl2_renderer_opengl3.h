#ifndef PLATFORM_IMPL_OSX_RENDERER_OPENGL3_H
#define PLATFORM_IMPL_OSX_RENDERER_OPENGL3_H

#include "platform_impl_osx_sdl2.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.h"
#include "libgame.u.h"
#include "osrs/pix3dgl.h"
}

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct RendererOSX_SDL2OpenGL3
{
    SDL_GLContext gl_context;
    struct Platform* platform;
    int* pixel_buffer;
    int width;
    int height;

    struct SceneOp* ops;
    int op_count;
    int op_capacity;

    float time_delta_accumulator;

    struct Pix3DGL* pix3dgl;

    // Performance metrics
    double face_order_time_ms;
    double render_time_ms;
};

struct RendererOSX_SDL2OpenGL3* PlatformImpl_OSX_SDL2_Renderer_OpenGL3_New(int width, int height);

void PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Free(struct RendererOSX_SDL2OpenGL3* renderer);

bool PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Init(
    struct RendererOSX_SDL2OpenGL3* renderer, struct Platform* platform);

void PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Shutdown(struct RendererOSX_SDL2OpenGL3* renderer);

void PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Render(
    struct RendererOSX_SDL2OpenGL3* renderer, struct Game* game, struct GameGfxOpList* gfx_op_list);

#endif