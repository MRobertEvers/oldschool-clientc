#ifndef PLATFORMS_OPENGL3_OPENGL3_RENDERER_CORE_H
#define PLATFORMS_OPENGL3_OPENGL3_RENDERER_CORE_H

#    include "platforms/common/pass_3d_builder/batch_buffer_opengl3.h"
#    include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#    include "platforms/common/pass_3d_builder/pass_3d_builder2_opengl3.h"
#    include "platforms/platform_impl2_sdl2.h"

#    include <SDL.h>
#    include <cstdint>
#    include <vector>

#    if defined(__APPLE__)
#        include <OpenGL/gl3.h>
#    else
#        define GL_GLEXT_PROTOTYPES
#        include <GL/glcorearb.h>
#    endif

extern "C" {
#    include "osrs/game.h"
#    include "tori_rs.h"
}

/** GPU world texture tile (matches Shaders.metal AtlasTile / WebGL1). */
struct OpenGL3AtlasTile
{
    float u0, v0, du, dv;
    float anim_u, anim_v;
    float opaque;
    float _pad;
};

static constexpr int kOpenGL3WorldAtlasSize = 2048;
static constexpr int kOpenGL3Max3dPassesPerFrame = 32;
static constexpr size_t kOpenGL3UniformBufferDynamicAlign = 256;

struct OpenGL3WorldUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

inline size_t
opengl3_uniforms_stride_padded()
{
    constexpr size_t a = kOpenGL3UniformBufferDynamicAlign;
    return (sizeof(OpenGL3WorldUniforms) + a - 1u) / a * a;
}

struct LogicalViewportRect
{
    int x, y, width, height;
};

struct ToriGlViewportPixels
{
    int x, y, width, height;
};

struct OpenGL3PassState
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

struct Platform2_SDL2_Renderer_OpenGL3
{
    SDL_GLContext gl_context = nullptr;

    struct Platform2_SDL2* platform = nullptr;
    int width = 0;
    int height = 0;
    bool gl_ready = false;

    Pass3DBuilder2OpenGL3 pass3d_builder;
    GPU3DCache2<GPU3DMeshVertexWebGL1> model_cache2;

    GLuint prog_world3d = 0;
    OpenGL3WorldShaderLocs world_locs{};

    GLuint cache2_atlas_tex = 0;
    std::vector<OpenGL3AtlasTile> tiles_cpu;
    bool tiles_dirty = true;

    std::vector<uint8_t> rgba_scratch;

    uint32_t diag_frame_model_draw_cmds = 0u;
    uint32_t diag_frame_pose_invalid_skips = 0u;
    uint32_t diag_frame_submitted_model_draws = 0u;
    uint32_t diag_frame_dynamic_index_draws = 0u;

    GLuint pass3d_dynamic_ibo = 0;
    /** Core-profile VAO for world mesh attribute state during Pass3D submit. */
    GLuint world_draw_vao = 0;

    OpenGL3BatchBuffer batch3d_staging;

    OpenGL3PassState pass;
};

#endif
