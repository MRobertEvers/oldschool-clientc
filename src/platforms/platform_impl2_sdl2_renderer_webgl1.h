#ifndef PLATFORM_IMPL2_SDL2_RENDERER_WEBGL1_H
#define PLATFORM_IMPL2_SDL2_RENDERER_WEBGL1_H

#ifdef __EMSCRIPTEN__

#    include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#    include "platforms/common/pass_3d_builder/pass_3d_builder2.h"
#    include "platforms/common/pass_3d_builder/pass_3d_builder2_webgl1.h"
#    include "platforms/platform_impl2_sdl2.h"

#    include <GLES2/gl2.h>
#    include <SDL.h>
#    include <cstdint>
#    include <unordered_map>
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
static constexpr int kWebGL1InflightFrames = 3;
static constexpr int kWebGL1Max3dPassesPerFrame = 32;
static constexpr size_t kWebGL1UniformBufferDynamicAlign = 256;
static constexpr uint32_t kWebGL1SoloModelBatchBase = 0xF0000000u;

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

struct Platform2_SDL2_Renderer_WebGL1
{
    SDL_GLContext gl_context = nullptr;

    struct Platform2_SDL2* platform = nullptr;
    int width = 0;
    int height = 0;
    bool gl_ready = false;

    Pass3DBuilder2<GPU3DTransformUniformMetal> pass3d_builder;
    GPU3DCache2<GPU3DMeshVertexWebGL1> model_cache2;

    GLuint prog_world3d = 0;
    WebGL1WorldShaderLocs world_locs{};

    GLuint prog_clear_depth = 0;
    GLint clear_depth_a_pos = -1;

    GLuint pass3d_instance_buf = 0;
    GLuint pass3d_index_buf = 0;
    /** Short-lived index uploads for dynamic draws (rebased indices). */
    GLuint scratch_idx_buf = 0;

    struct WebGL1Cache2BatchEntry
    {
        GLuint vbo = 0;
        GLuint ebo = 0;
        std::vector<uint16_t> model_ids;
    };
    std::unordered_map<uint32_t, WebGL1Cache2BatchEntry> model_cache2_batch_map;

    GLuint cache2_atlas_tex = 0;
    std::vector<WebGL1AtlasTile> tiles_cpu;
    bool tiles_dirty = true;

    uint32_t current_model_batch_id = 0;

    uint32_t uniform_frame_slot = 0;
    uint32_t uniform_pass_subslot = 0;
    size_t pass3d_inst_upload_ofs = 0;
    size_t pass3d_idx_upload_ofs = 0;

    std::vector<uint8_t> rgba_scratch;

    GLuint clear_quad_vbo = 0;
};

struct Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(int width, int height);

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(struct Platform2_SDL2_Renderer_WebGL1* renderer);

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif // __EMSCRIPTEN__

#endif
