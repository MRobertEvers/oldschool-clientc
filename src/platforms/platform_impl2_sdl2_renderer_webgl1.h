#ifndef PLATFORM_IMPL2_SDL2_RENDERER_WEBGL1_H
#define PLATFORM_IMPL2_SDL2_RENDERER_WEBGL1_H

#ifdef __EMSCRIPTEN__

#include "platforms/ToriRSPlatformKit/src/backends/webgl1/trspk_webgl1.h"
#include "platforms/ToriRSPlatformKit/src/tools/trspk_facebuffer.h"
#include "platforms/platform_impl2_sdl2.h"

#include <SDL.h>
#include <stdint.h>
#include <vector>

#define WEBGL1_UI_CLIP_STACK 16

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

    /* 2D UI software raster layer — written to by DRAW_SPRITE/FONT/RECT commands,
     * then uploaded as a GL texture and composited over the 3D scene. */
    int* ui_pixel_buffer = nullptr;
    uint8_t* ui_rgba_buffer = nullptr;
    int ui_buf_width = 0;
    int ui_buf_height = 0;

    /* Clip stack for PUSH_CLIP / POP_CLIP (mirrors soft3d behaviour). */
    int ui_clip_stack_top = -1;
    int ui_clip_stack_l[WEBGL1_UI_CLIP_STACK] = {};
    int ui_clip_stack_t[WEBGL1_UI_CLIP_STACK] = {};
    int ui_clip_stack_r[WEBGL1_UI_CLIP_STACK] = {};
    int ui_clip_stack_b[WEBGL1_UI_CLIP_STACK] = {};

    /* Current clip bounds (kept in sync with stack). */
    int ui_clip_l = 0, ui_clip_t = 0, ui_clip_r = 0, ui_clip_b = 0;

    /* GL resources for uploading and compositing the 2D UI layer. */
    GLuint ui2d_texture = 0;
    GLuint ui2d_prog = 0;
    GLuint ui2d_vbo = 0;
    GLint  ui2d_a_pos = -1;
    GLint  ui2d_a_uv  = -1;
    GLint  ui2d_u_tex = -1;
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
