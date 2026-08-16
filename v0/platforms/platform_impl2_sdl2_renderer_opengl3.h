#ifndef PLATFORM_IMPL2_SDL2_RENDERER_OPENGL3_H
#define PLATFORM_IMPL2_SDL2_RENDERER_OPENGL3_H

#include "platforms/ToriRSPlatformKit/src/backends/opengl3/trspk_opengl3.h"
#include "platforms/ToriRSPlatformKit/src/tools/trspk_facebuffer.h"
#include "platforms/platform_impl2_sdl2.h"

#include <SDL.h>
#include <stdint.h>

struct nk_context;

struct Platform2_SDL2_Renderer_OpenGL3
{
    struct Platform2_SDL2* platform = nullptr;
    SDL_GLContext gl_context = nullptr;
    TRSPK_OpenGL3Renderer* trspk = nullptr;
    struct nk_context* nk_ctx = nullptr;
    Uint64 nk_ui_prev_perf = 0;
    int width = 0;
    int height = 0;
    bool gl_ready = false;
    uint32_t current_model_batch_id = 0;
    bool current_model_batch_active = false;
    TRSPK_FaceBuffer32 facebuffer = {};
};

struct Platform2_SDL2_Renderer_OpenGL3*
PlatformImpl2_SDL2_Renderer_OpenGL3_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Free(struct Platform2_SDL2_Renderer_OpenGL3* renderer);

bool
PlatformImpl2_SDL2_Renderer_OpenGL3_Init(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
