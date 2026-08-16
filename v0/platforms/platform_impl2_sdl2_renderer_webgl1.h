#ifndef PLATFORM_IMPL2_SDL2_RENDERER_WEBGL1_H
#define PLATFORM_IMPL2_SDL2_RENDERER_WEBGL1_H

#ifdef __EMSCRIPTEN__

#include "platforms/ToriRSPlatformKit/src/backends/webgl1/trspk_webgl1.h"
#include "platforms/ToriRSPlatformKit/src/tools/trspk_facebuffer.h"
#include "platforms/platform_impl2_sdl2.h"

#include <SDL.h>
#include <stdint.h>
#include <vector>

struct Platform2_SDL2_Renderer_WebGL1
{
    SDL_GLContext gl_context = nullptr;
    struct Platform2_SDL2* platform = nullptr;
    TRSPK_WebGL1Renderer* trspk = nullptr;
    int width = 0;
    int height = 0;
    bool gl_ready = false;
    uint32_t current_model_batch_id = 0;
    bool current_model_batch_active = false;
    uint32_t diag_frame_model_draw_cmds = 0;
    uint32_t diag_frame_pose_invalid_skips = 0;
    uint32_t diag_frame_submitted_model_draws = 0;
    std::vector<uint8_t> rgba_scratch;
    TRSPK_FaceBuffer16 facebuffer;
};

Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(Platform2_SDL2_Renderer_WebGL1* renderer);

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif // __EMSCRIPTEN__

#endif
