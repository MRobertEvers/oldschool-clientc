#ifndef PASS_3D_BUILDER2_OPENGL3_H
#define PASS_3D_BUILDER2_OPENGL3_H

#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cstdint>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#endif

struct Platform2_SDL2_Renderer_OpenGL3;

inline constexpr uint32_t kPass3DBuilder2OpenGL3DynamicIndexCapacity = 1u << 21;

/** Uniform / attribute locations for world 3D program (stride-48 interleaved mesh). */
struct OpenGL3WorldShaderLocs
{
    GLint a_position = -1;
    GLint a_color = -1;
    GLint a_texcoord = -1;
    GLint a_tex_id = -1;
    GLint a_uv_mode = -1;
    GLint u_modelViewMatrix = -1;
    GLint u_projectionMatrix = -1;
    GLint u_clock = -1;
    GLint s_atlas = -1;
    GLint u_tileA = -1;
    GLint u_tileB = -1;
};

struct Pass3DOpenGL3Slice
{
    GPUResourceHandle vbo = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
};

/** Scenery-only: uint32 indices (GL_UNSIGNED_INT); one `glDrawElements` per slice (per mesh VBO).
 */
class Pass3DBuilder2OpenGL3
{
private:
    bool is_building_ = false;
    std::vector<uint32_t> indices_pool_;
    std::vector<Pass3DOpenGL3Slice> slices_;

public:
    Pass3DBuilder2OpenGL3()
    {
        indices_pool_.reserve(4096u * 16u);
        slices_.reserve(8u);
    }

    void
    Begin();

    void
    End()
    {
        is_building_ = false;
    }

    bool
    IsBuilding() const
    {
        return is_building_;
    }

    bool
    HasCommands() const
    {
        return !indices_pool_.empty() && !slices_.empty();
    }

    void
    ClearAfterSubmit();

    const std::vector<uint32_t>&
    Indices() const
    {
        return indices_pool_;
    }

    bool
    HasDynamicIndices() const
    {
        return !indices_pool_.empty();
    }

    const std::vector<Pass3DOpenGL3Slice>&
    Slices() const
    {
        return slices_;
    }

    void
    AppendSortedDraw(
        GPUResourceHandle vbo,
        uint32_t pose_vbo_vertex_offset,
        const uint32_t* sorted_indices,
        uint32_t index_count);
};

void
Pass3DBuilder2SubmitOpenGL3(
    Pass3DBuilder2OpenGL3& builder,
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    GLuint program,
    const OpenGL3WorldShaderLocs& locs,
    GLuint fragment_atlas_texture,
    const float modelViewMatrix[16],
    const float projectionMatrix[16],
    float u_clock);

#endif
