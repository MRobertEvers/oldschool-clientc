#ifndef PLATFORMS_WEBGL1_WEBGL1_RENDERER_CORE_H
#define PLATFORMS_WEBGL1_WEBGL1_RENDERER_CORE_H

#ifdef __EMSCRIPTEN__

#    include "platforms/common/pass_3d_builder/batch_buffer_webgl1.h"
#    include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#    include "platforms/webgl1/pass_3d_builder2_webgl1.h"
#    include "platforms/platform_impl2_sdl2.h"

#    include <GLES2/gl2.h>
#    include <SDL.h>
#    include <cstdint>
#    include <vector>

extern "C" {
#    include "osrs/game.h"
#    include "tori_rs.h"
}

/** GPU world texture tile (matches Shaders.metal AtlasTile / MetalAtlasTile). */
struct WebGL1AtlasTile
{
    float u0, v0, du, dv;
    float anim_u, anim_v;
    float opaque;
    float _pad;
};

static constexpr int kWebGL1WorldAtlasSize = 2048;
static constexpr int kWebGL1Max3dPassesPerFrame = 32;
static constexpr size_t kWebGL1UniformBufferDynamicAlign = 256;

struct MetalUniformsWebGL
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

inline size_t
webgl1_uniforms_stride_padded()
{
    constexpr size_t a = kWebGL1UniformBufferDynamicAlign;
    return (sizeof(MetalUniformsWebGL) + a - 1u) / a * a;
}

struct LogicalViewportRect
{
    int x, y, width, height;
};

struct ToriGlViewportPixels
{
    int x, y, width, height;
};

struct WebGL1PassState
{
    int win_width = 0;
    int win_height = 0;
    LogicalViewportRect pass_3d_dst_logical{};
    ToriGlViewportPixels world_gl_vp{};
    int clear_rect_slot = 0;

    uint32_t current_model_batch_id = 0;
    bool current_model_batch_active = false;

    uint32_t uniform_pass_subslot = 0;
};

struct WebGL1RendererCore
{
    SDL_GLContext gl_context = nullptr;

    struct Platform2_SDL2* platform = nullptr;
    int width = 0;
    int height = 0;
    bool gl_ready = false;

    Pass3DBuilder2WebGL1 pass3d_builder;
    GPU3DCache2<GPU3DMeshVertexWebGL1> model_cache2;

    GLuint prog_world3d = 0;
    WebGL1WorldShaderLocs world_locs{};

    GLuint prog_clear_depth = 0;
    GLint clear_depth_a_pos = -1;

    GLuint cache2_atlas_tex = 0;
    std::vector<WebGL1AtlasTile> tiles_cpu;
    bool tiles_dirty = true;

    std::vector<uint8_t> rgba_scratch;

    GLuint clear_quad_vbo = 0;

    uint32_t diag_frame_model_draw_cmds = 0;
    uint32_t diag_frame_pose_invalid_skips = 0;
    uint32_t diag_frame_submitted_model_draws = 0;
    uint32_t diag_frame_dynamic_index_draws = 0;

    GLuint pass3d_dynamic_ibo = 0;

    WebGL1BatchBuffer batch3d_staging;

    WebGL1PassState pass;
};

typedef WebGL1RendererCore Platform2_SDL2_Renderer_WebGL1;

Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(int width, int height);

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
