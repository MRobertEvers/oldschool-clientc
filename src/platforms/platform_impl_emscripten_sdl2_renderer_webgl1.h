#ifndef PLATFORM_IMPL_EMSCRIPTEN_RENDERER_WEBGL1_H
#define PLATFORM_IMPL_EMSCRIPTEN_RENDERER_WEBGL1_H

#include "platform_impl_emscripten_sdl2.h"

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "graphics/render.h"
#include "libgame.h"
#include "libgame.u.h"
#include "osrs/pix3dgl.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct RendererEmscripten_SDL2WebGL1
{
    SDL_GLContext gl_context;
    struct Platform* platform;
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

    SDL_Texture* soft3d_texture_nullable; // Unused, kept for API compatibility
    int* soft3d_pixel_buffer_nullable;
    int soft3d_width;
    int soft3d_height;
    int soft3d_max_width;
    int soft3d_max_height;
    char soft3d_canvas_id[64];
};

struct RendererEmscripten_SDL2WebGL1*
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_New(int width, int height);

void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Free(struct RendererEmscripten_SDL2WebGL1* renderer);

bool PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Init(
    struct RendererEmscripten_SDL2WebGL1* renderer, struct Platform* platform);

bool PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_InitSoft3D(
    struct RendererEmscripten_SDL2WebGL1* renderer,
    int max_width,
    int max_height,
    const char* canvas_id);
void PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_CleanupSoft3D(
    struct RendererEmscripten_SDL2WebGL1* renderer);

void PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Shutdown(
    struct RendererEmscripten_SDL2WebGL1* renderer);

void PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Render(
    struct RendererEmscripten_SDL2WebGL1* renderer,
    struct Game* game,
    struct GameGfxOpList* gfx_op_list);

#ifdef __cplusplus
}
#endif

#endif
